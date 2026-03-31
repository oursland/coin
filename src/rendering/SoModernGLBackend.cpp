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

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------

SoModernGLBackend::SoModernGLBackend()
{
  std::memset(&this->storedparams, 0, sizeof(SoRenderBackendInitParams));
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

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

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

  // Cache attribute locations
  this->posLoc = glGetAttribLocation(this->shaderProgram, "a_position");
  this->normLoc = glGetAttribLocation(this->shaderProgram, "a_normal");

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

  // Destroy all cached GPU resources
  for (auto & entry : gpuCache) {
    destroyCacheEntry(entry);
  }
  gpuCache.clear();
  ptrToCacheIndex.clear();

  if (this->shaderProgram) {
    glDeleteProgram(this->shaderProgram);
    this->shaderProgram = 0;
  }
  this->setInitialized(FALSE);
  this->emitLog("shutdown");
}

// -----------------------------------------------------------------------
// GPU Cache Management
// -----------------------------------------------------------------------

CachedGPUCommand &
SoModernGLBackend::getOrCreateCache(const float * posPtr, const uint32_t * idxPtr)
{
  CacheKey key{posPtr, idxPtr};
  auto it = ptrToCacheIndex.find(key);
  if (it != ptrToCacheIndex.end()) {
    return gpuCache[it->second];
  }
  int idx = static_cast<int>(gpuCache.size());
  gpuCache.emplace_back();
  ptrToCacheIndex[key] = idx;
  return gpuCache[idx];
}

void
SoModernGLBackend::uploadGeometry(CachedGPUCommand & entry,
                                  const SoRenderCommand & cmd)
{
  ZoneScopedN("uploadGeometry");
  GLsizei stride = static_cast<GLsizei>(
    cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

  // Position VBO
  if (entry.posVBO == 0) glGenBuffers(1, &entry.posVBO);
  glBindBuffer(GL_ARRAY_BUFFER, entry.posVBO);
  glBufferData(GL_ARRAY_BUFFER,
               cmd.geometry.vertexCount * stride,
               cmd.geometry.positions, GL_STATIC_DRAW);

  // Normal VBO — may be smaller than position VBO for BRep shapes
  // (coordinate node includes edge/point vertices that lack normals)
  if (cmd.geometry.normals && cmd.geometry.normalCount > 0) {
    if (entry.normVBO == 0) glGenBuffers(1, &entry.normVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.normVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 cmd.geometry.normalCount * stride,
                 cmd.geometry.normals, GL_STATIC_DRAW);
  }

  // Index VBO
  if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
    if (entry.idxVBO == 0) glGenBuffers(1, &entry.idxVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 cmd.geometry.indexCount * sizeof(uint32_t),
                 cmd.geometry.indices, GL_STATIC_DRAW);
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  // Update cache keys
  entry.posKey = cmd.geometry.positions;
  entry.normKey = cmd.geometry.normals;
  entry.idxKey = cmd.geometry.indices;
  entry.vertexCount = cmd.geometry.vertexCount;
  entry.indexCount = cmd.geometry.indexCount;
  entry.vertexStride = static_cast<uint32_t>(stride);
}

void
SoModernGLBackend::setupVisualVAO(CachedGPUCommand & entry,
                                  const SoRenderCommand & cmd)
{
  if (entry.vao == 0) glGenVertexArrays(1, &entry.vao);
  glBindVertexArray(entry.vao);

  GLsizei stride = static_cast<GLsizei>(entry.vertexStride);

  // Position attribute
  if (this->posLoc >= 0 && entry.posVBO) {
    glBindBuffer(GL_ARRAY_BUFFER, entry.posVBO);
    glEnableVertexAttribArray(this->posLoc);
    glVertexAttribPointer(this->posLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
  }

  // Normal attribute
  if (this->normLoc >= 0) {
    if (entry.normVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.normVBO);
      glEnableVertexAttribArray(this->normLoc);
      glVertexAttribPointer(this->normLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->normLoc);
      glVertexAttrib3f(this->normLoc, 0.0f, 0.0f, 1.0f);
    }
  }

  // Index buffer
  if (entry.idxVBO) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void
SoModernGLBackend::destroyCacheEntry(CachedGPUCommand & entry)
{
  if (entry.posVBO) { glDeleteBuffers(1, &entry.posVBO); entry.posVBO = 0; }
  if (entry.normVBO) { glDeleteBuffers(1, &entry.normVBO); entry.normVBO = 0; }
  if (entry.idxVBO) { glDeleteBuffers(1, &entry.idxVBO); entry.idxVBO = 0; }
  if (entry.vao) { glDeleteVertexArrays(1, &entry.vao); entry.vao = 0; }
  if (entry.idVAO) { glDeleteVertexArrays(1, &entry.idVAO); entry.idVAO = 0; }
}

void
SoModernGLBackend::gcStaleEntries(int frame)
{
  // Remove entries unused for 3+ frames
  // Build list of stale pointer keys, then erase from map and mark entries dead
  for (int i = 0; i < static_cast<int>(gpuCache.size()); i++) {
    auto & entry = gpuCache[i];
    if (entry.posVBO == 0) continue;  // already dead
    if (frame - entry.lastUsedFrame > 3) {
      ptrToCacheIndex.erase(CacheKey{entry.posKey, entry.idxKey});
      destroyCacheEntry(entry);
      entry.posKey = nullptr;
      entry.idxKey = nullptr;
    }
  }
}

const CachedGPUCommand *
SoModernGLBackend::getCachedCommand(int cmdIndex) const
{
  // cmdIndex maps to draw list position; we need to look up by pointer
  // This method is called by the ID pass which iterates draw list commands
  // The caller should use the position pointer to find the cache entry
  (void)cmdIndex;
  return nullptr;  // Use ptrToCacheIndex directly instead
}

// -----------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------

SbBool
SoModernGLBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  ZoneScopedN("GLBackend::render");
  this->debugValidateDrawList(drawlist);
  this->logFrameStats(drawlist, params);
  this->currentFrame = params.frameIndex;

  // Only re-render the ID buffer when camera or scene changes
  if (!matricesInitialized ||
      params.viewMatrix != lastViewMatrix ||
      params.projMatrix != lastProjMatrix) {
    this->pickBufferDirty = true;
    lastViewMatrix = params.viewMatrix;
    lastProjMatrix = params.projMatrix;
    matricesInitialized = true;
  }

  if (!this->shaderProgram) return TRUE;

  // Clear depth if requested (bit 2 — used for overlay passes)
  if (params.flags & 4u) {
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  // Enable depth test
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  glUseProgram(this->shaderProgram);

  // Upload view and projection matrices (once per frame)
  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);
  glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
  glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

  // Default: lighting enabled
  glUniform1f(this->uEmissiveLocation, 0.0f);

  const int count = drawlist.getNumCommands();

  // --- Cache-aware draw: ensure all commands have cached VBOs/VAOs ---
  {
    ZoneScopedN("cacheUpdate");
    for (int i = 0; i < count; ++i) {
      const SoRenderCommand & cmd = drawlist.getCommand(i);
      if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) continue;
      if (cmd.geometry.vertexCount > 10000000) continue;

      GLsizei stride = static_cast<GLsizei>(
        cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

      CachedGPUCommand & entry = getOrCreateCache(cmd.geometry.positions, cmd.geometry.indices);
      if (!entry.isGeometryValid(cmd.geometry.positions, cmd.geometry.normals,
                                 cmd.geometry.indices, cmd.geometry.vertexCount,
                                 cmd.geometry.indexCount, static_cast<uint32_t>(stride))) {
        uploadGeometry(entry, cmd);
        setupVisualVAO(entry, cmd);
      }
      entry.lastUsedFrame = this->currentFrame;
    }
  }

  // --- Draw lambda: bind cached VAO, set uniforms, draw ---
  auto drawCached = [&](const SoRenderCommand & cmd) {
    if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) return;
    if (cmd.geometry.vertexCount > 10000000) return;
    if (cmd.geometry.indexCount > 0 && !cmd.geometry.indices) return;

    auto it = ptrToCacheIndex.find(CacheKey{cmd.geometry.positions, cmd.geometry.indices});
    if (it == ptrToCacheIndex.end()) return;
    const CachedGPUCommand & entry = gpuCache[it->second];
    if (entry.vao == 0) return;

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

    glBindVertexArray(entry.vao);
    if (cmd.geometry.indexCount > 0) {
      glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else {
      glDrawArrays(prim, 0, cmd.geometry.vertexCount);
    }
  };

  // Iterate in sorted order: opaque front-to-back, then transparent back-to-front.
  // The sortedOrder array encodes pass type in the sort key, so opaque commands
  // come first, then transparent. We switch GL state at the boundary.
  const auto & order = drawlist.getSortedOrder();
  bool inTransparent = false;

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);

    if (!inTransparent && cmd.pass == SO_RENDERPASS_TRANSPARENT) {
      // Switch to transparent state
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      inTransparent = true;
    }
    drawCached(cmd);
  }

  // Pass 3: Selection/highlight overlays — emissive flat color on top
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUniform1f(this->uEmissiveLocation, 1.0f);

  // Helper: draw a sub-range or whole command for overlay
  auto drawElementRange = [](const SoRenderCommand & cmd, int elemIdx, GLenum prim) {
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      if (elemIdx >= 0 && elemIdx < static_cast<int>(cmd.pick.faceStart.size())) {
        int offset = cmd.pick.faceStart[elemIdx];
        int cnt = cmd.pick.faceCount[elemIdx];
        glDrawElements(prim, cnt, GL_UNSIGNED_INT,
                       reinterpret_cast<const void *>(
                         static_cast<uintptr_t>(offset * sizeof(uint32_t))));
      }
      else {
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }
    else {
      if (elemIdx >= 0 && elemIdx < static_cast<int>(cmd.geometry.vertexCount)) {
        glPointSize(8.0f);
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

    if (!cmd.geometry.positions) continue;
    auto it = ptrToCacheIndex.find(CacheKey{cmd.geometry.positions, cmd.geometry.indices});
    if (it == ptrToCacheIndex.end()) continue;
    const CachedGPUCommand & entry = gpuCache[it->second];
    if (entry.vao == 0) continue;

    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);

    GLenum prim = topologyToGL(cmd.geometry.topology);

    // Bind cached VAO (has pos + norm + idx already set up).
    // For emissive overlay, normals are ignored by the shader (u_emissive > 0.5).
    glBindVertexArray(entry.vao);

    if (hasSelection) {
      const SbVec4f & sc = cmd.selection.selectionColor;
      glUniform4f(this->uColorLocation, sc[0], sc[1], sc[2], 0.5f);
      for (int elem : cmd.selection.selectedElements) {
        drawElementRange(cmd, elem, prim);
      }
    }

    if (hasHighlight) {
      const SbVec4f & hc = cmd.selection.highlightColor;
      glUniform4f(this->uColorLocation, hc[0], hc[1], hc[2], 0.6f);
      drawElementRange(cmd, hlElem, prim);
    }
  }

  glUniform1f(this->uEmissiveLocation, 0.0f);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
  glUseProgram(0);

  // GC stale cache entries
  gcStaleEntries(this->currentFrame);

  // Render ID buffer for GPU picking — skip during interactive navigation
  // (no preselection during orbit/pan/zoom, saves ~11ms per frame)
  bool interactive = (params.flags & 2u) != 0;
  bool skipIdBuffer = (params.flags & 8u) != 0;
  if (pickBuffer && !interactive && !skipIdBuffer) {
    const auto & lut = drawlist.getPickLUT();
    SbVec2s vpSize = params.viewport.getViewportSizePixels();
    pickBuffer->resize(vpSize[0], vpSize[1]);

    if (lut.size() != lastPickLUTSize) {
      pickBuffer->buildIdColorVBOs(drawlist, params.contextId);
      lastPickLUTSize = lut.size();
      pickBufferDirty = true;
    }

    if (pickBufferDirty && !lut.empty()) {
      // Build per-command VBO info array so the ID pass can reuse cached VBOs
      std::vector<SoIDPassVBOInfo> vboInfo(count);
      for (int i = 0; i < count; ++i) {
        const SoRenderCommand & cmd = drawlist.getCommand(i);
        vboInfo[i] = {0, 0, 0};
        if (!cmd.geometry.positions) continue;
        auto it = ptrToCacheIndex.find(
          CacheKey{cmd.geometry.positions, cmd.geometry.indices});
        if (it != ptrToCacheIndex.end()) {
          const CachedGPUCommand & entry = gpuCache[it->second];
          vboInfo[i].posVBO = entry.posVBO;
          vboInfo[i].idxVBO = entry.idxVBO;
          vboInfo[i].vertexStride = entry.vertexStride;
        }
      }
      pickBuffer->render(&viewMat[0][0], &projMat[0][0], drawlist,
                         vboInfo.data(), count);
      pickBufferDirty = false;
    }

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

// -----------------------------------------------------------------------
// Misc
// -----------------------------------------------------------------------

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
    "void main() {\n"
    "  vec4 worldPos = u_model * vec4(a_position, 1.0);\n"
    "  vec4 eyePos = u_view * worldPos;\n"
    "  v_eyePos = eyePos.xyz;\n"
    "  v_eyeNormal = mat3(u_view) * mat3(u_model) * a_normal;\n"
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
    "    gl_FragColor = v_color;\n"
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

void
SoModernGLBackend::setPickLineWidth(float width)
{
  if (pickBuffer) pickBuffer->setPickLineWidth(width);
}

void
SoModernGLBackend::setPickPointSize(float size)
{
  if (pickBuffer) pickBuffer->setPickPointSize(size);
}

float
SoModernGLBackend::getPickLineWidth() const
{
  return pickBuffer ? pickBuffer->getPickLineWidth() : 7.0f;
}

float
SoModernGLBackend::getPickPointSize() const
{
  return pickBuffer ? pickBuffer->getPickPointSize() : 7.0f;
}
