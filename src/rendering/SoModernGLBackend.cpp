// src/rendering/SoModernGLBackend.cpp

#include "rendering/SoModernGLBackend.h"
#include "rendering/SoVBO.h"
#include "CoinTracyConfig.h"

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
  this->normalBuffer = 0;
  this->indexBuffer = 0;
  this->uViewLocation = -1;
  this->uProjLocation = -1;
  this->uModelLocation = -1;
  this->uColorLocation = -1;
  this->uEmissiveLocation = -1;
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
  if (this->normalBuffer) {
    glDeleteBuffers(1, &this->normalBuffer);
    this->normalBuffer = 0;
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
  ZoneScopedN("GLBackend::render");
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
  if (this->normalBuffer == 0) {
    glGenBuffers(1, &this->normalBuffer);
  }
  if (this->indexBuffer == 0) {
    glGenBuffers(1, &this->indexBuffer);
  }

  // Enable depth test
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  glUseProgram(this->shaderProgram);
  glBindVertexArray(this->vao);
  this->vaoInitialized = TRUE;

  // Upload view and projection matrices (once per frame)
  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);
  glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
  glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

  // Cache attribute locations (once)
  GLint posLoc = glGetAttribLocation(this->shaderProgram, "a_position");
  GLint normLoc = glGetAttribLocation(this->shaderProgram, "a_normal");

  // Default: lighting enabled
  glUniform1f(this->uEmissiveLocation, 0.0f);

  // Lambda to draw a single command using the fallback shader + CPU data path
  auto drawCommand = [&](const SoRenderCommand & cmd) {
    if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) return;
    // Guard against stale pointers during scene graph changes
    if (cmd.geometry.vertexCount > 10000000) return;  // sanity limit
    if (cmd.geometry.indexCount > 0 && !cmd.geometry.indices) return;

    // Per-command model matrix
    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);

    // Per-command color
    const SbVec4f & diffuse = cmd.material.diffuse;
    glUniform4f(this->uColorLocation,
                diffuse[0], diffuse[1], diffuse[2], diffuse[3]);

    GLenum prim = topologyToGL(cmd.geometry.topology);
    if (prim == GL_POINTS) {
      glPointSize(std::max(cmd.state.raster.lineWidth, 4.0f));
    }
    else if (prim == GL_LINES || prim == GL_LINE_STRIP) {
      glLineWidth(std::max(cmd.state.raster.lineWidth, 1.0f));
    }
    GLsizei stride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

    // Upload positions
    glBindBuffer(GL_ARRAY_BUFFER, this->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 cmd.geometry.vertexCount * stride,
                 cmd.geometry.positions, GL_STREAM_DRAW);
    if (posLoc >= 0) {
      glEnableVertexAttribArray(posLoc);
      glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    }

    // Upload normals
    if (cmd.geometry.normals && normLoc >= 0) {
      glBindBuffer(GL_ARRAY_BUFFER, this->normalBuffer);
      glBufferData(GL_ARRAY_BUFFER,
                   cmd.geometry.vertexCount * stride,
                   cmd.geometry.normals, GL_STREAM_DRAW);
      glEnableVertexAttribArray(normLoc);
      glVertexAttribPointer(normLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    }
    else if (normLoc >= 0) {
      glDisableVertexAttribArray(normLoc);
      glVertexAttrib3f(normLoc, 0.0f, 0.0f, 1.0f);
    }

    // Draw
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->indexBuffer);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   cmd.geometry.indexCount * sizeof(uint32_t),
                   cmd.geometry.indices, GL_STREAM_DRAW);
      glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      glDrawArrays(prim, 0, cmd.geometry.vertexCount);
    }

    if (posLoc >= 0) glDisableVertexAttribArray(posLoc);
    if (normLoc >= 0) glDisableVertexAttribArray(normLoc);
  };

  const int count = drawlist.getNumCommands();

  // Pass 1: Opaque — depth write on, no blending
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.pass != SO_RENDERPASS_OPAQUE) continue;
    drawCommand(cmd);
  }

  // Pass 2: Transparent — depth write off, alpha blending
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.pass == SO_RENDERPASS_OPAQUE) continue;
    drawCommand(cmd);
  }

  // Pass 3: Selection/highlight overlays — emissive flat color on top
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUniform1f(this->uEmissiveLocation, 1.0f);

  {
    int hlCount = 0, selCount = 0;
    for (int i = 0; i < count; ++i) {
      const SoRenderCommand & c = drawlist.getCommand(i);
      if (c.selection.highlightElement != -1) {
        hlCount++;
        // Log the memory address to see if it's the same command or reused memory
        ZoneScopedN("hl found");
        char buf[128];
        std::snprintf(buf, sizeof(buf), "cmd=%d hl=%d addr=%p topo=%d verts=%u",
                      i, c.selection.highlightElement, (void*)&c,
                      (int)c.geometry.topology, c.geometry.vertexCount);
        ZoneText(buf, std::strlen(buf));
      }
      if (!c.selection.selectedElements.empty()) selCount++;
    }
    if (hlCount > 0 || selCount > 0) {
      ZoneScopedN("overlay stats");
      char buf[64];
      std::snprintf(buf, sizeof(buf), "hl=%d sel=%d total=%d", hlCount, selCount, count);
      ZoneText(buf, std::strlen(buf));
    }
  }

  // Helper: draw a sub-range or whole command for overlay
  auto drawElementRange = [&](const SoRenderCommand & cmd, int elemIdx, GLenum prim) {
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      // Indexed geometry (faces, edges): use faceStart/faceCount for sub-range
      if (elemIdx >= 0 && elemIdx < static_cast<int>(cmd.pick.faceStart.size())) {
        int offset = cmd.pick.faceStart[elemIdx];
        int cnt = cmd.pick.faceCount[elemIdx];
        glDrawElements(prim, cnt, GL_UNSIGNED_INT,
                       reinterpret_cast<const void *>(
                         static_cast<uintptr_t>(offset * sizeof(uint32_t))));
      }
      else {
        // Whole body (-2) or out of range
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }
    else {
      // Non-indexed geometry (points): elemIdx is the vertex offset
      if (elemIdx >= 0 && elemIdx < static_cast<int>(cmd.geometry.vertexCount)) {
        glPointSize(8.0f);  // Larger point for highlighted vertex
        glDrawArrays(prim, elemIdx, 1);
      }
      else {
        glDrawArrays(prim, 0, cmd.geometry.vertexCount);
      }
    }
  };

  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    int hlElem = cmd.selection.highlightElement;
    bool hasHighlight = (hlElem != -1);
    bool hasSelection = !cmd.selection.selectedElements.empty();
    if (!hasHighlight && !hasSelection) continue;

    {
      ZoneScopedN("overlay cmd");
      char buf[256];
      std::snprintf(buf, sizeof(buf), "cmd=%d hl=%d sel=%zu id=%.60s topo=%d",
                    i, hlElem, cmd.selection.selectedElements.size(),
                    cmd.pick.pickIdentity.c_str(), (int)cmd.geometry.topology);
      ZoneText(buf, std::strlen(buf));
    }
    if (!cmd.geometry.positions || posLoc < 0) continue;

    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);

    GLenum prim = topologyToGL(cmd.geometry.topology);
    GLsizei stride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

    // Upload geometry once
    glBindBuffer(GL_ARRAY_BUFFER, this->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 cmd.geometry.vertexCount * stride,
                 cmd.geometry.positions, GL_STREAM_DRAW);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    if (normLoc >= 0) {
      glDisableVertexAttribArray(normLoc);
      glVertexAttrib3f(normLoc, 0.0f, 0.0f, 1.0f);
    }
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->indexBuffer);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   cmd.geometry.indexCount * sizeof(uint32_t),
                   cmd.geometry.indices, GL_STREAM_DRAW);
    }

    // Draw selection overlays first (underneath highlight)
    if (hasSelection) {
      const SbVec4f & sc = cmd.selection.selectionColor;
      glUniform4f(this->uColorLocation, sc[0], sc[1], sc[2], 0.5f);
      for (int elem : cmd.selection.selectedElements) {
        drawElementRange(cmd, elem, prim);
      }
    }

    // Draw highlight on top
    if (hasHighlight) {
      const SbVec4f & hc = cmd.selection.highlightColor;
      glUniform4f(this->uColorLocation, hc[0], hc[1], hc[2], 0.6f);
      drawElementRange(cmd, hlElem, prim);
    }

    if (posLoc >= 0) glDisableVertexAttribArray(posLoc);
  }

  glUniform1f(this->uEmissiveLocation, 0.0f);  // Restore lighting mode
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
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

      // Debug: count commands with/without ID color VBOs by topology
      int faceWithVbo = 0, faceNoVbo = 0;
      int edgeWithVbo = 0, edgeNoVbo = 0;
      int pointWithVbo = 0, pointNoVbo = 0;
      for (int i = 0; i < count; ++i) {
        const auto & c = drawlist.getCommand(i);
        bool hasVbo = i < static_cast<int>(pickBuffer->getIdColorVBOCount())
              && pickBuffer->hasIdColorVBO(i);
        if (c.geometry.topology == SO_TOPOLOGY_TRIANGLES) {
          if (hasVbo) faceWithVbo++; else faceNoVbo++;
        }
        else if (c.geometry.topology == SO_TOPOLOGY_LINES) {
          if (hasVbo) edgeWithVbo++; else edgeNoVbo++;
        }
        else if (c.geometry.topology == SO_TOPOLOGY_POINTS) {
          if (hasVbo) pointWithVbo++; else pointNoVbo++;
        }
      }
      std::fprintf(stderr,
        "ModernGLBackend: LUT=%zu cmds=%d faces=%d/%d edges=%d/%d points=%d/%d (with/noVBO)\n",
        lut.size(), count,
        faceWithVbo, faceNoVbo,
        edgeWithVbo, edgeNoVbo,
        pointWithVbo, pointNoVbo);
    }

    // Only re-render ID buffer when camera moved or LUT changed
    if (pickBufferDirty && !lut.empty()) {
      SbMat viewMat, projMat;
      params.viewMatrix.getValue(viewMat);
      params.projMatrix.getValue(projMat);
      pickBuffer->render(&viewMat[0][0], &projMat[0][0], drawlist);
      pickBufferDirty = false;
    }

    // Debug: visualize the ID buffer when FREECAD_SHOW_ID_BUFFER=1
    static int showIdBuffer = -1;
    if (showIdBuffer < 0) {
      const char * env = coin_getenv("FREECAD_SHOW_ID_BUFFER");
      showIdBuffer = (env && env[0] == '1') ? 1 : 0;
    }
    if (showIdBuffer) {
      pickBuffer->blitToScreen(vpSize[0], vpSize[1]);
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
  // Blinn-Phong shader — view-space headlight
  static const char * vertexSource =
    "#version 120\n"
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_model;\n"
    "uniform vec4 u_color;\n"
    "varying vec3 v_eyePos;\n"
    "varying vec3 v_eyeNormal;\n"
    "varying vec4 v_color;\n"
    // Compute inverse of 3x3 matrix (for normal transform)
    "mat3 inverse3(mat3 m) {\n"
    "  float det = dot(m[0], cross(m[1], m[2]));\n"
    "  if (abs(det) < 1e-10) return mat3(1.0);\n"
    "  float invDet = 1.0 / det;\n"
    "  return mat3(\n"
    "    cross(m[1], m[2]) * invDet,\n"
    "    cross(m[2], m[0]) * invDet,\n"
    "    cross(m[0], m[1]) * invDet\n"
    "  );\n"
    "}\n"
    "void main() {\n"
    "  vec4 worldPos = u_model * vec4(a_position, 1.0);\n"
    "  vec4 eyePos = u_view * worldPos;\n"
    "  v_eyePos = eyePos.xyz;\n"
    // Normal matrix = transpose(inverse(mat3(modelView)))
    // This is correct regardless of matrix convention issues
    "  mat3 mv3 = mat3(u_view) * mat3(u_model);\n"
    "  mat3 normalMatrix = transpose(inverse3(mv3));\n"
    "  v_eyeNormal = normalMatrix * a_normal;\n"
    "  gl_Position = u_proj * eyePos;\n"
    "  v_color = u_color;\n"
    "}\n";

  static const char * fragmentSource =
    "#version 120\n"
    "uniform float u_emissive;\n"
    "varying vec3 v_eyePos;\n"
    "varying vec3 v_eyeNormal;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  if (u_emissive > 0.5) {\n"
    "    gl_FragColor = v_color;\n"  // Flat emissive — no lighting
    "    return;\n"
    "  }\n"
    "  vec3 N = normalize(v_eyeNormal);\n"
    "  if (dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) N = -N;\n"
    "  vec3 L = vec3(0.0, 0.0, 1.0);\n"
    "  float NdotL = dot(N, L);\n"
    "  vec3 V = normalize(-v_eyePos);\n"
    "  vec3 H = normalize(L + V);\n"
    "  float NdotH = max(dot(N, H), 0.0);\n"
    "  float spec = pow(NdotH, 64.0);\n"
    "  vec3 ambient = 0.25 * v_color.rgb;\n"
    "  vec3 diffuse = 0.85 * NdotL * v_color.rgb;\n"
    "  vec3 specular = 0.12 * spec * vec3(1.0);\n"
    "  gl_FragColor = vec4(ambient + diffuse + specular, v_color.a);\n"
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

  this->uViewLocation = glGetUniformLocation(this->shaderProgram, "u_view");
  this->uProjLocation = glGetUniformLocation(this->shaderProgram, "u_proj");
  this->uModelLocation = glGetUniformLocation(this->shaderProgram, "u_model");
  this->uColorLocation = glGetUniformLocation(this->shaderProgram, "u_color");
  this->uEmissiveLocation = glGetUniformLocation(this->shaderProgram, "u_emissive");
  return TRUE;
}
