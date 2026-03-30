// src/rendering/SoModernGLBackend.cpp

#include "rendering/SoModernGLBackend.h"
#include "rendering/SoVBO.h"

// macOS legacy GL headers provide VAO functions with APPLE suffix
#if defined(__APPLE__) && !defined(glGenVertexArrays)
#define glGenVertexArrays    glGenVertexArraysAPPLE
#define glBindVertexArray    glBindVertexArrayAPPLE
#define glDeleteVertexArrays glDeleteVertexArraysAPPLE
#endif

#include <Inventor/SbBasic.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/SbMatrix.h>

#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>

#include "rendering/SoVertexLayout.h"
#include "shaders/SoGLShaderProgram.h"

static SbBool
coin_modern_ir_trace_enabled()
{
  static int initialized = 0;
  static SbBool enabled = FALSE;
  if (!initialized) {
    enabled = coin_getenv("COIN_DEBUG_RENDER_IR") ? TRUE : FALSE;
    initialized = 1;
  }
  return enabled;
}

static GLuint
coin_compile_shader(GLenum type, const char * source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetShaderInfoLog(shader, length, &length, &log[0]);
      SoDebugError::postInfo("SoModernGLBackend::compileShader", "%s", log.c_str());
    }
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint
coin_link_program(GLuint vs, GLuint fs)
{
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetProgramInfoLog(program, length, &length, &log[0]);
      SoDebugError::postInfo("SoModernGLBackend::linkProgram", "%s", log.c_str());
    }
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

namespace {
const char * shaderAttrNames[] = {
  "a_position",
  "a_normal",
  "a_tangent",
  "a_bitangent",
  "a_color0",
  "a_color1",
  "a_color2",
  "a_color3",
  "a_indices",
  "a_weights",
  "a_texCoord0",
  "a_texCoord1",
  "a_texCoord2",
  "a_texCoord3",
  "a_texCoord4",
  "a_texCoord5",
  "a_texCoord6",
  "a_texCoord7"
};

const GLenum attribTypeToGL[SoAttribType::Count] = {
  GL_UNSIGNED_BYTE,
  GL_SHORT,
  GL_HALF_FLOAT,
  GL_FLOAT
};

inline void disableAttributes(const std::vector<GLint> & attrs) {
  for (GLint loc : attrs) {
    glDisableVertexAttribArray(loc);
  }
}

inline void enableLayoutAttributes(GLuint program,
                                   const SoVertexLayout * layout,
                                   std::vector<GLint> & enabledLocations) {
  if (!program || !layout) return;
  const GLsizei stride = static_cast<GLsizei>(layout->getStride());
  for (int i = 0; i < SoAttrib::Count; ++i) {
    if (!layout->has(static_cast<SoAttrib::Enum>(i))) continue;
    const char * name = shaderAttrNames[i];
    if (name == nullptr) continue;
    GLint loc = glGetAttribLocation(program, name);
    if (loc < 0) continue;
    uint8_t num;
    SoAttribType::Enum type;
    bool normalized;
    bool asInt;
    layout->decode(static_cast<SoAttrib::Enum>(i), num, type, normalized, asInt);
    const GLenum gltype = attribTypeToGL[type];
    glEnableVertexAttribArray(loc);
    enabledLocations.push_back(loc);
    glVertexAttribPointer(loc,
                          num,
                          gltype,
                          normalized,
                          stride,
                          reinterpret_cast<const void *>(
                            static_cast<uintptr_t>(layout->getOffset(static_cast<SoAttrib::Enum>(i)))));
  }
}

inline void enableCPUAttribute(GLuint program,
                               const char * name,
                               GLint components,
                               const void * data,
                               GLsizei stride,
                               std::vector<GLint> & enabledLocations) {
  if (!program || !name || !data) return;
  GLint loc = glGetAttribLocation(program, name);
  if (loc < 0) return;
  glEnableVertexAttribArray(loc);
  enabledLocations.push_back(loc);
  glVertexAttribPointer(loc, components, GL_FLOAT, GL_FALSE, stride, data);
}

inline GLenum topologyToGL(SoPrimitiveTopology topology) {
  switch (topology) {
  case SO_TOPOLOGY_POINTS: return GL_POINTS;
  case SO_TOPOLOGY_LINES: return GL_LINES;
  case SO_TOPOLOGY_TRIANGLES: return GL_TRIANGLES;
  case SO_TOPOLOGY_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
  case SO_TOPOLOGY_LINE_STRIP: return GL_LINE_STRIP;
  default: return GL_TRIANGLES;
  }
}

} // namespace

SoModernGLBackend::SoModernGLBackend()
{
  std::memset(&this->storedparams, 0, sizeof(SoRenderBackendInitParams));
  this->shaderProgram = 0;
  this->vao = 0;
  this->vertexBuffer = 0;
  this->indexBuffer = 0;
  this->uMvpLocation = -1;
  this->uModelLocation = -1;
  this->uColorLocation = -1;
  this->vaoInitialized = FALSE;
}

SoModernGLBackend::~SoModernGLBackend()
{
  if (this->isInitialized()) {
    this->shutdown();
  }
}

const char *
SoModernGLBackend::getName() const
{
  return "ModernGLBackend";
}

SbBool
SoModernGLBackend::initialize(const SoRenderBackendInitParams & params)
{
  if (this->isInitialized()) {
    return TRUE;
  }

  this->storedparams = params;
  this->setInitParams(params);

  char buffer[128];
  std::snprintf(buffer, sizeof(buffer),
                "target=%dx%d samples=%d id=%u",
                params.targetInfo.size[0], params.targetInfo.size[1],
                params.targetInfo.samples, params.targetInfo.targetId);
  this->emitLog(buffer);

  if (!this->createShaders()) {
    this->emitError("failed to create ModernGL shader");
    return FALSE;
  }

  // Initialize GPU pick buffer
  pickBuffer = std::make_unique<SoIDPickBuffer>();
  if (!pickBuffer->initialize()) {
    this->emitLog("ID pick buffer initialization failed (picking disabled)");
    pickBuffer.reset();
  }

  this->setInitialized(TRUE);
  return TRUE;
}

void
SoModernGLBackend::shutdown()
{
  if (!this->isInitialized()) {
    return;
  }
  pickBuffer.reset();

  if (this->vertexBuffer) {
    glDeleteBuffers(1, &this->vertexBuffer);
    this->vertexBuffer = 0;
  }
  if (this->indexBuffer) {
    glDeleteBuffers(1, &this->indexBuffer);
    this->indexBuffer = 0;
  }
  if (this->vao) {
    glDeleteVertexArrays(1, &this->vao);
    this->vao = 0;
  }
  if (this->shaderProgram) {
    glDeleteProgram(this->shaderProgram);
    this->shaderProgram = 0;
  }
  this->setInitialized(FALSE);
  this->emitLog("shutdown");
}

SbBool
SoModernGLBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  this->debugValidateDrawList(drawlist);
  this->logFrameStats(drawlist, params);

  // Mark pick buffer dirty when camera changes
  // (viewProjMatrix changes between frames when camera moves)
  this->pickBufferDirty = true;  // Conservative: always dirty, optimized later

  if (!this->shaderProgram) return TRUE;
  if (this->vao == 0) {
    glGenVertexArrays(1, &this->vao);
  }
  if (this->vertexBuffer == 0) {
    glGenBuffers(1, &this->vertexBuffer);
  }
  if (this->indexBuffer == 0) {
    glGenBuffers(1, &this->indexBuffer);
  }

  glUseProgram(this->shaderProgram);
  glBindVertexArray(this->vao);
  if (!this->vaoInitialized) {
    glBindBuffer(GL_ARRAY_BUFFER, this->vertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->vaoInitialized = TRUE;
  }

  SbMat vpMat;
  params.viewProjMatrix.getValue(vpMat);
  glUniformMatrix4fv(this->uMvpLocation, 1, GL_FALSE, &vpMat[0][0]);

  // Set identity as default model matrix
  SbMatrix identity = SbMatrix::identity();
  SbMat identityMat;
  identity.getValue(identityMat);
  glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &identityMat[0][0]);

  SoGLShaderProgram * currentShader = nullptr;
  GLuint currentShaderHandle = 0;
  bool fallbackProgramBound = true;

  const int count = drawlist.getNumCommands();
  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.geometry.vertexCount == 0) continue;

    // Upload per-command model matrix (Coin stores row-major, GL expects column-major → transpose)
    if (fallbackProgramBound) {
      SbMat modelMat;
      cmd.modelMatrix.getValue(modelMat);
      glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);
    }

    const bool hasCustomShader =
      (cmd.shaderProgram != NULL) && (params.state != NULL);

    GLuint programHandle = this->shaderProgram;
    if (hasCustomShader) {
      if (fallbackProgramBound) {
        glBindVertexArray(0);
        fallbackProgramBound = false;
      }
      if (currentShader != cmd.shaderProgram) {
        if (currentShader) currentShader->disable(params.state);
        currentShader = cmd.shaderProgram;
        currentShader->enable(params.state);
        currentShaderHandle =
          currentShader->getGLSLShaderProgramHandle(params.state);
      }
      programHandle = currentShaderHandle;
    }
    else {
      if (currentShader) {
        currentShader->disable(params.state);
        currentShader = nullptr;
      }
      if (!fallbackProgramBound) {
        glUseProgram(this->shaderProgram);
        glBindVertexArray(this->vao);
        fallbackProgramBound = true;
      }
      const SbVec4f & diffuse = cmd.material.diffuse;
      glUniform4f(this->uColorLocation,
                  diffuse[0], diffuse[1], diffuse[2], diffuse[3]);
    }

    GLenum prim = topologyToGL(cmd.geometry.topology);
    bool drawn = false;

    if (cmd.geometry.cache.vertexVbo && cmd.geometry.cache.indexVbo) {
      const uint32_t cacheContextId =
        cmd.geometry.cache.contextId ? cmd.geometry.cache.contextId : params.contextId;
      cmd.geometry.cache.vertexVbo->bindBuffer(cacheContextId);
      if (hasCustomShader && programHandle && cmd.geometry.cache.vertexLayout) {
        std::vector<GLint> enabledLocs;
        enableLayoutAttributes(programHandle,
                               cmd.geometry.cache.vertexLayout,
                               enabledLocs);
        if (cmd.geometry.indexCount > 0) {
          cmd.geometry.cache.indexVbo->bindBuffer(cacheContextId);
          glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        else {
          glDrawArrays(prim, 0, cmd.geometry.vertexCount);
        }
        disableAttributes(enabledLocs);
        drawn = true;
      }
      else if (!hasCustomShader) {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        if (cmd.geometry.indexCount > 0) {
          cmd.geometry.cache.indexVbo->bindBuffer(cacheContextId);
          glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        else {
          glDrawArrays(prim, 0, cmd.geometry.vertexCount);
        }
        glDisableVertexAttribArray(0);
        drawn = true;
      }
    }

    if (drawn) continue;

    if (hasCustomShader && programHandle) {
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      std::vector<GLint> enabledLocs;
      const GLsizei vertexStride = static_cast<GLsizei>(
        cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
      enableCPUAttribute(programHandle, "a_position", 3,
                         cmd.geometry.positions,
                         vertexStride, enabledLocs);
      if (cmd.geometry.normals) {
        enableCPUAttribute(programHandle, "a_normal", 3,
                           cmd.geometry.normals,
                           vertexStride, enabledLocs);
      }
      if (cmd.geometry.texcoords) {
        const GLsizei texStride = static_cast<GLsizei>(
          cmd.geometry.texcoordStride ? cmd.geometry.texcoordStride : sizeof(float) * 4);
        enableCPUAttribute(programHandle, "a_texCoord0", 4,
                           cmd.geometry.texcoords,
                           texStride, enabledLocs);
      }
      if (cmd.geometry.colors) {
        enableCPUAttribute(programHandle, "a_color0", 4,
                           cmd.geometry.colors,
                           static_cast<GLsizei>(sizeof(float) * 4), enabledLocs);
      }

      if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     cmd.geometry.indexCount * sizeof(uint32_t),
                     cmd.geometry.indices,
                     GL_STREAM_DRAW);
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      }
      else {
        glDrawArrays(prim, 0, cmd.geometry.vertexCount);
      }
      disableAttributes(enabledLocs);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    else if (!hasCustomShader) {
      if (!cmd.geometry.positions) continue;
      glBindBuffer(GL_ARRAY_BUFFER, this->vertexBuffer);
      glBufferData(GL_ARRAY_BUFFER,
                   cmd.geometry.vertexCount * 3 * sizeof(float),
                   cmd.geometry.positions,
                   GL_STREAM_DRAW);

      if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     cmd.geometry.indexCount * sizeof(uint32_t),
                     cmd.geometry.indices,
                     GL_STREAM_DRAW);
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
      }
      else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDrawArrays(prim, 0, cmd.geometry.vertexCount);
      }
    }
  }

  if (currentShader) currentShader->disable(params.state);
  glBindVertexArray(0);
  glUseProgram(0);

  // Render ID buffer for GPU picking (when pick LUT exists)
  if (pickBuffer) {
    const auto & lut = drawlist.getPickLUT();
    SbVec2s vpSize = params.viewport.getViewportSizePixels();
    pickBuffer->resize(vpSize[0], vpSize[1]);

    // Only rebuild per-vertex ID colors when LUT changes
    if (lut.size() != lastPickLUTSize) {
      pickBuffer->buildIdColorVBOs(drawlist, params.contextId);
      lastPickLUTSize = lut.size();
      pickBufferDirty = true;
    }

    // Only re-render ID buffer when camera moved or LUT changed
    if (pickBufferDirty && !lut.empty()) {
      SbMat viewMat, projMat;
      params.viewMatrix.getValue(viewMat);
      params.projMatrix.getValue(projMat);
      pickBuffer->render(&viewMat[0][0], &projMat[0][0], drawlist);
      pickBufferDirty = false;
    }
  }

  return TRUE;
}

void
SoModernGLBackend::resizeTarget(const SoRenderTargetInfo & info)
{
  this->storedparams.targetInfo = info;
  this->pickBufferDirty = true;
  SoRenderBackend::resizeTarget(info);
}

uint32_t
SoModernGLBackend::pick(int x, int y, int pickRadius) const
{
  if (!pickBuffer) return 0;
  return pickBuffer->pick(x, y, pickRadius);
}

void
SoModernGLBackend::logFrameStats(const SoDrawList & drawlist,
                                 const SoRenderParams & params) const
{
  const int num = drawlist.getNumCommands();
  SoDebugError::postInfo("ModernGLBackend::render",
                         "frame=%d cmds=%d",
                         params.frameIndex, num);
  SoIRDumpSummary(drawlist);

  if (coin_modern_ir_trace_enabled()) {
    SoIRDumpFirstN(drawlist, 8);
  }
}

bool
SoModernGLBackend::createShaders()
{
  static const char * vertexSource =
    "#version 120\n"
    "attribute vec3 a_position;\n"
    "uniform mat4 u_viewProj;\n"
    "uniform mat4 u_model;\n"
    "varying vec4 v_color;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "  gl_Position = u_viewProj * u_model * vec4(a_position, 1.0);\n"
    "  v_color = u_color;\n"
    "}\n";

  static const char * fragmentSource =
    "#version 120\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_FragColor = v_color;\n"
    "}\n";

  GLuint vs = coin_compile_shader(GL_VERTEX_SHADER, vertexSource);
  GLuint fs = coin_compile_shader(GL_FRAGMENT_SHADER, fragmentSource);
  if (vs == 0 || fs == 0) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return FALSE;
  }

  this->shaderProgram = coin_link_program(vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (this->shaderProgram == 0) return FALSE;

  this->uMvpLocation = glGetUniformLocation(this->shaderProgram, "u_viewProj");
  this->uModelLocation = glGetUniformLocation(this->shaderProgram, "u_model");
  this->uColorLocation = glGetUniformLocation(this->shaderProgram, "u_color");
  return TRUE;
}
