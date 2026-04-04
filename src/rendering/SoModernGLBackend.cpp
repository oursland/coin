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

// -----------------------------------------------------------------------
// Named constants — extracted from magic numbers throughout render()
// -----------------------------------------------------------------------

// Blinn-Phong lighting coefficients (used in GLSL shader source)
static constexpr float AMBIENT_COEFF  = 0.25f;
static constexpr float DIFFUSE_COEFF  = 0.85f;
static constexpr float SPECULAR_COEFF = 0.12f;
static constexpr float DEFAULT_SHININESS = 64.0f;

// Alpha thresholds for selection/highlight overlays
static constexpr float HIGHLIGHT_ALPHA = 0.6f;
static constexpr float SELECTION_ALPHA = 0.5f;

// Texture alpha discard threshold (shader-side)
static constexpr float ALPHA_DISCARD_THRESHOLD = 0.3f;

// Minimum point size for highlight overlay (ensures visibility)
static constexpr float MIN_HIGHLIGHT_POINT_SIZE = 20.0f;

// Cache GC: entries unused for this many frames are destroyed
static constexpr int CACHE_UNUSED_FRAME_THRESHOLD = 3;

// Safety limit: skip commands with absurd vertex counts
static constexpr int MAX_VERTEX_COUNT = 10000000;

// Default pick line width / point size when no pick buffer exists
static constexpr float DEFAULT_PICK_SIZE = 7.0f;

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
  this->colorLoc = glGetAttribLocation(this->shaderProgram, "a_color");

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

  // Per-vertex color VBO (RGBA float, 4 components per vertex)
  if (cmd.geometry.colors && cmd.geometry.vertexCount > 0) {
    if (entry.colorVBO == 0) glGenBuffers(1, &entry.colorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.colorVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 cmd.geometry.vertexCount * sizeof(float) * 4,
                 cmd.geometry.colors, GL_STATIC_DRAW);
  }
  else if (entry.colorVBO != 0) {
    glDeleteBuffers(1, &entry.colorVBO);
    entry.colorVBO = 0;
  }

  // Texcoord VBO + texture upload (for SoImage etc.)
  if (cmd.geometry.texcoords && cmd.material.texture.pixels
      && cmd.geometry.vertexCount > 0) {
    // Texcoord VBO — extract vec2 from vec4 texcoords
    if (entry.texcoordVBO == 0) glGenBuffers(1, &entry.texcoordVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.texcoordVBO);
    uint32_t tcStride = cmd.geometry.texcoordStride
      ? cmd.geometry.texcoordStride : sizeof(float) * 4;
    if (tcStride == sizeof(float) * 4) {
      std::vector<float> tc2(cmd.geometry.vertexCount * 2);
      const float * src = cmd.geometry.texcoords;
      for (uint32_t i = 0; i < cmd.geometry.vertexCount; i++) {
        tc2[i * 2] = src[i * 4];
        tc2[i * 2 + 1] = src[i * 4 + 1];
      }
      glBufferData(GL_ARRAY_BUFFER, tc2.size() * sizeof(float),
                   tc2.data(), GL_STATIC_DRAW);
    } else {
      glBufferData(GL_ARRAY_BUFFER,
                   cmd.geometry.vertexCount * sizeof(float) * 2,
                   cmd.geometry.texcoords, GL_STATIC_DRAW);
    }

    // Upload texture — expand 1/2-component to RGBA on CPU to avoid
    // GL_LUMINANCE/GL_LUMINANCE_ALPHA which are removed in Core Profile.
    if (entry.textureId == 0) glGenTextures(1, &entry.textureId);
    glBindTexture(GL_TEXTURE_2D, entry.textureId);
    int nc = cmd.material.texture.numComponents;
    int tw = cmd.material.texture.width;
    int th = cmd.material.texture.height;
    const unsigned char * src = cmd.material.texture.pixels;
    std::vector<unsigned char> expanded;
    if (nc == 1 || nc == 2) {
      int npx = tw * th;
      expanded.resize(npx * 4);
      for (int px = 0; px < npx; px++) {
        unsigned char lum = src[px * nc];
        unsigned char alpha = (nc == 2) ? src[px * nc + 1] : 255;
        expanded[px * 4]     = lum;
        expanded[px * 4 + 1] = lum;
        expanded[px * 4 + 2] = lum;
        expanded[px * 4 + 3] = alpha;
      }
      src = expanded.data();
      nc = 4;
    }
    GLenum fmt = (nc == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, fmt,
                 GL_UNSIGNED_BYTE, src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  else {
    // No texture on this command — clean up stale texture state from
    // a previous command that used the same cache entry (pool address reuse).
    if (entry.textureId) {
      glDeleteTextures(1, &entry.textureId);
      entry.textureId = 0;
    }
    if (entry.texcoordVBO) {
      glDeleteBuffers(1, &entry.texcoordVBO);
      entry.texcoordVBO = 0;
    }
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

  // Update cache keys (generation is set by the caller after upload)
  entry.posKey = cmd.geometry.positions;
  entry.normKey = cmd.geometry.normals;
  entry.colorKey = cmd.geometry.colors;
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

  // Per-vertex color attribute
  if (this->colorLoc >= 0) {
    if (entry.colorVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.colorVBO);
      glEnableVertexAttribArray(this->colorLoc);
      glVertexAttribPointer(this->colorLoc, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->colorLoc);
      glVertexAttrib4f(this->colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }
  }

  // Texcoord attribute (for textured commands — billboard/world-space)
  if (this->texcoordLoc >= 0) {
    if (entry.texcoordVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.texcoordVBO);
      glEnableVertexAttribArray(this->texcoordLoc);
      glVertexAttribPointer(this->texcoordLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->texcoordLoc);
      glVertexAttrib2f(this->texcoordLoc, 0.0f, 0.0f);
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
  if (entry.colorVBO) { glDeleteBuffers(1, &entry.colorVBO); entry.colorVBO = 0; }
  if (entry.texcoordVBO) { glDeleteBuffers(1, &entry.texcoordVBO); entry.texcoordVBO = 0; }
  if (entry.textureId) { glDeleteTextures(1, &entry.textureId); entry.textureId = 0; }
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
    if (frame - entry.lastUsedFrame > CACHE_UNUSED_FRAME_THRESHOLD) {
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
// Draw Command — per-command GL state, draw call, state restore
// -----------------------------------------------------------------------

void
SoModernGLBackend::drawCommand(const SoRenderCommand & cmd,
                               const SbMat & viewMat,
                               const SbMat & projMat,
                               const SoRenderParams & params)
{
  if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) return;
  if (cmd.geometry.vertexCount > MAX_VERTEX_COUNT) return;
  if (cmd.geometry.indexCount > 0 && !cmd.geometry.indices) return;

  auto it = ptrToCacheIndex.find(CacheKey{cmd.geometry.positions, cmd.geometry.indices});
  if (it == ptrToCacheIndex.end()) return;
  const CachedGPUCommand & entry = gpuCache[it->second];
  if (entry.vao == 0) return;

  // Per-command model matrix; view/proj from params (auto-clipped) for
  // main scene, or per-command for overlay/background (different camera).
  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);
  if (cmd.pass == SO_RENDERPASS_OVERLAY) {
    SbMat cmdViewMat, cmdProjMat;
    cmd.viewMatrix.getValue(cmdViewMat);
    cmd.projMatrix.getValue(cmdProjMat);
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &cmdViewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &cmdProjMat[0][0]);
  } else {
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);
  }

  // Per-command color — use vertex colors if available
  bool hasVertexColors = (entry.colorVBO != 0);
  glUniform1f(this->uUseVertexColorLocation, hasVertexColors ? 1.0f : 0.0f);
  const SbVec4f & diffuse = cmd.material.diffuse;
  glUniform4f(this->uColorLocation,
              diffuse[0], diffuse[1], diffuse[2], diffuse[3]);

  GLenum prim = topologyToGL(cmd.geometry.topology);

  // Per-command depth state: for non-overlay commands that individually
  // disable depth (e.g. constraint depth buffer nodes), handle per-command.
  // Overlay commands use the overlay pass's depth state instead.
  if (!cmd.state.depth.enabled && cmd.pass != SO_RENDERPASS_OVERLAY) {
    glDisable(GL_DEPTH_TEST);
  }

  // Per-command backface culling
  if (cmd.state.raster.cullMode != 0) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
  }

  // Flat (unlit) rendering for points, lines, and BASE_COLOR materials.
  // Points/lines have zero normals; BASE_COLOR materials use emissive
  // as the display color (e.g. rotation center sphere, annotations).
  bool flatColor = (prim == GL_POINTS || prim == GL_LINES || prim == GL_LINE_STRIP
                    || (cmd.material.featureFlags & SO_FEAT_BASE_COLOR));
  glUniform1f(this->uRenderModeLocation, flatColor ? 1.0f : 0.0f);

  // Per-command emissive color for Blinn-Phong (added to lighting result)
  const SbVec4f & ec = cmd.material.emissive;
  glUniform3f(this->uEmissiveColorLocation, ec[0], ec[1], ec[2]);

  // Wireframe draw style: render triangles as lines
  uint8_t fillMode = cmd.state.raster.fillMode;
  if (fillMode == 1 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else if (fillMode == 2 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
  }

  if (prim == GL_POINTS || fillMode == 2) {
    float ps = cmd.state.raster.pointSize;
    if (ps < 1.0f) ps = cmd.state.raster.lineWidth;
    glPointSize(std::max(ps, 1.0f));
    // Point circle discard via gl_PointCoord in shader (mode 1.5)
    glUniform1f(this->uRenderModeLocation, 1.5f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  if (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1) {
    glLineWidth(std::max(cmd.state.raster.lineWidth, 1.0f));
  }

  // Line stipple pattern (dashed/dotted lines)
  uint16_t pattern = cmd.state.raster.linePattern;
  bool useStipple = (pattern != 0 && pattern != 0xFFFF)
                 && (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1);
  if (useStipple) {
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(std::max(static_cast<int>(cmd.state.raster.linePatternScale), 1),
                  static_cast<GLushort>(pattern));
  }

  // Polygon offset: push faces back so coplanar edges render on top
  float oFactor = cmd.state.raster.polygonOffsetFactor;
  float oUnits = cmd.state.raster.polygonOffsetUnits;
  bool useOffset = (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)
                && fillMode == 0
                && (oFactor != 0.0f || oUnits != 0.0f);
  if (useOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(oFactor, oUnits);
  }

  // Textured commands: set renderMode and bind texture (same shader program)
  bool isTextured = (entry.textureId != 0 && entry.texcoordVBO != 0);
  if (isTextured) {
    bool isBillboard = (cmd.material.flags & SO_MAT_IS_BILLBOARD) != 0;
    // renderMode: 2=billboard, 3=world-space textured
    glUniform1f(this->uRenderModeLocation, isBillboard ? 2.0f : 3.0f);

    if (isBillboard) {
      // Compute quad center from vertex positions (average of all vertices)
      float cx = 0, cy = 0, cz = 0;
      GLsizei stride = static_cast<GLsizei>(
        cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
      for (uint32_t vi = 0; vi < cmd.geometry.vertexCount; vi++) {
        const float * p = reinterpret_cast<const float *>(
          reinterpret_cast<const char *>(cmd.geometry.positions) + vi * stride);
        cx += p[0]; cy += p[1]; cz += p[2];
      }
      float n = static_cast<float>(cmd.geometry.vertexCount);
      glUniform3f(this->uQuadCenterLocation, cx / n, cy / n, cz / n);

      // Texture pixel size and viewport size
      glUniform2f(this->uTexSizeLocation,
                  static_cast<float>(cmd.material.texture.width),
                  static_cast<float>(cmd.material.texture.height));
      SbVec2s vpSz = params.viewport.getViewportSizePixels();
      glUniform2f(this->uVpSizeLocation,
                  static_cast<float>(vpSz[0]),
                  static_cast<float>(vpSz[1]));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entry.textureId);
    glUniform1i(this->uTextureLocation, 0);
    // Modulate texture with diffuse color (MODULATE mode for NaviCube labels).
    // Billboard textures (SoImage, SoText2) use white modulation (pass-through).
    const SbVec4f & diff = cmd.material.diffuse;
    if (isBillboard) {
      glUniform4f(this->uTexModColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
      glUniform4f(this->uTexModColorLocation, diff[0], diff[1], diff[2], diff[3]);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (isBillboard) {
      glDepthFunc(GL_ALWAYS);  // billboards render on top
    }
  }

  glBindVertexArray(entry.vao);

  // --- Draw call ---
  if (cmd.geometry.indexCount > 0) {
    glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  else {
    glDrawArrays(prim, 0, cmd.geometry.vertexCount);
  }

  // --- State restore ---
  if (isTextured) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glUniform1f(this->uRenderModeLocation, 0.0f);  // restore to lit mode
  }

  if (useOffset) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  if (useStipple) {
    glDisable(GL_LINE_STIPPLE);
  }
  if (fillMode != 0 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  if (!cmd.state.depth.enabled && cmd.pass != SO_RENDERPASS_OVERLAY) {
    glEnable(GL_DEPTH_TEST);
  }
  if (prim == GL_POINTS || fillMode == 2) {
    glDisable(GL_BLEND);
    glPointSize(1.0f);
  }
  if (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1) {
    glLineWidth(1.0f);
  }
  if (cmd.state.raster.cullMode != 0) {
    glDisable(GL_CULL_FACE);
  }
}

// -----------------------------------------------------------------------
// Render pass methods
// -----------------------------------------------------------------------

void
SoModernGLBackend::beginFrame(const SoDrawList & drawlist,
                              const SoRenderParams & params)
{
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

  // Clear depth if requested (used for overlay passes)
  if (params.flags & SO_PARAM_CLEAR_DEPTH) {
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  // Establish default GL state for the frame
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
  glUniform1f(this->uRenderModeLocation, 0.0f);
}

void
SoModernGLBackend::updateGeometryCache(const SoDrawList & drawlist)
{
  ZoneScopedN("cacheUpdate");
  const int count = drawlist.getNumCommands();
  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) continue;
    if (cmd.geometry.vertexCount > MAX_VERTEX_COUNT) continue;

    GLsizei stride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

    CachedGPUCommand & entry = getOrCreateCache(cmd.geometry.positions, cmd.geometry.indices);
    uint32_t gen = drawlist.getGeneration();
    bool needsUpload = !entry.isGeometryValid(
      cmd.geometry.positions, cmd.geometry.normals,
      cmd.geometry.indices, cmd.geometry.vertexCount,
      cmd.geometry.indexCount, static_cast<uint32_t>(stride), gen);
    // Also upload if command has texture but cache doesn't
    if (!needsUpload && cmd.material.texture.pixels && entry.textureId == 0) {
      needsUpload = true;
    }
    if (needsUpload) {
      uploadGeometry(entry, cmd);
      setupVisualVAO(entry, cmd);
      entry.cacheGeneration = gen;
    }
    entry.lastUsedFrame = this->currentFrame;
  }
}

void
SoModernGLBackend::renderBackgroundPass(const SoDrawList & drawlist,
                                        const SbMat & viewMat,
                                        const SbMat & projMat,
                                        const SoRenderParams & params)
{
  int bgCount = params.bgCommandCount;
  int count = drawlist.getNumCommands();
  if (bgCount <= 0) return;

  // Background commands have their own view/proj matrices captured
  // per-command (identity for gradients, ortho for grids, etc.).
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  for (int i = 0; i < bgCount && i < count; ++i) {
    drawCommand(drawlist.getCommand(i), viewMat, projMat, params);
  }

  // Restore default state; clear depth so main scene renders on top
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void
SoModernGLBackend::renderMainScenePass(const SoDrawList & drawlist,
                                       const SbMat & viewMat,
                                       const SbMat & projMat,
                                       const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  int bgCount = params.bgCommandCount;

  // Main scene: sorted opaque front-to-back, then transparent back-to-front,
  // then overlay (annotations) last.
  const auto & order = drawlist.getSortedOrder();
  bool inTransparent = false;
  bool inOverlay = false;

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    // Skip background commands — already rendered
    if (ci < bgCount) continue;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);

    if (!inTransparent && cmd.pass == SO_RENDERPASS_TRANSPARENT) {
      // Switch to transparent state
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      inTransparent = true;
    }
    if (!inOverlay && cmd.pass == SO_RENDERPASS_OVERLAY) {
      // Switch to overlay state: disable depth so overlays render on top
      // of the main scene. Depth is disabled rather than cleared+enabled
      // to avoid ordering conflicts between overlay groups (annotations
      // vs NaviCube).
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      inOverlay = true;
    }
    drawCommand(cmd, viewMat, projMat, params);
  }

  // Restore default state after main scene
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoModernGLBackend::renderSelectionPass(const SoDrawList & drawlist,
                                       const SbMat & viewMat,
                                       const SbMat & projMat)
{
  const int count = drawlist.getNumCommands();

  // Selection/highlight overlays — emissive flat color on top
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUniform1f(this->uRenderModeLocation, 1.0f);

  // Helper: draw a sub-range or whole command for selection overlay
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
        float ps = cmd.state.raster.pointSize;
        if (ps < 1.0f) ps = cmd.state.raster.lineWidth;
        glPointSize(std::max(ps, MIN_HIGHLIGHT_POINT_SIZE));
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
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

    GLenum prim = topologyToGL(cmd.geometry.topology);

    if (prim == GL_POINTS) {
      // Point circle discard via gl_PointCoord in shader (mode 1.5)
      glUniform1f(this->uRenderModeLocation, 1.5f);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      // Use GL_ALWAYS so point highlights render on top of billboard markers
      glDepthFunc(GL_ALWAYS);
    }

    // Bind cached VAO (has pos + norm + idx already set up).
    // For flat overlay, normals are ignored by the shader (u_renderMode == 1).
    glBindVertexArray(entry.vao);

    if (hasSelection) {
      const SbVec4f & sc = cmd.selection.selectionColor;
      glUniform4f(this->uColorLocation, sc[0], sc[1], sc[2], SELECTION_ALPHA);
      for (int elem : cmd.selection.selectedElements) {
        drawElementRange(cmd, elem, prim);
      }
    }

    if (hasHighlight) {
      const SbVec4f & hc = cmd.selection.highlightColor;
      glUniform4f(this->uColorLocation, hc[0], hc[1], hc[2], HIGHLIGHT_ALPHA);
      drawElementRange(cmd, hlElem, prim);
    }

    if (prim == GL_POINTS) {
      glUniform1f(this->uRenderModeLocation, 1.0f);  // restore to flat
      glDepthFunc(GL_LEQUAL);
    }
  }

  // Restore default state
  glUniform1f(this->uRenderModeLocation, 0.0f);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoModernGLBackend::endFrame()
{
  glBindVertexArray(0);
  glUseProgram(0);
  gcStaleEntries(this->currentFrame);
}

void
SoModernGLBackend::renderIDBufferPass(const SoDrawList & drawlist,
                                      const SbMat & viewMat,
                                      const SbMat & projMat,
                                      const SoRenderParams & params)
{
  // Skip during interactive navigation (no preselection during orbit/pan/zoom)
  bool interactive = (params.flags & SO_PARAM_INTERACTIVE) != 0;
  bool skipIdBuffer = (params.flags & SO_PARAM_SKIP_ID) != 0;
  if (!pickBuffer || interactive || skipIdBuffer) return;

  const int count = drawlist.getNumCommands();
  const auto & lut = drawlist.getPickLUT();
  SbVec2s vpSize = params.viewport.getViewportSizePixels();

  // Render ID buffer at half resolution — 4x less fragment work.
  // Pick radius and line/point sizes still provide adequate coverage.
  int idW = std::max(1, static_cast<int>(vpSize[0]) / 2);
  int idH = std::max(1, static_cast<int>(vpSize[1]) / 2);
  pickBuffer->resize(idW, idH);
  pickBuffer->setPickScale(static_cast<float>(idW) / vpSize[0],
                           static_cast<float>(idH) / vpSize[1]);

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

// -----------------------------------------------------------------------
// Render — orchestrator
// -----------------------------------------------------------------------

SbBool
SoModernGLBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  ZoneScopedN("GLBackend::render");
  if (!this->shaderProgram) return TRUE;

  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);

  beginFrame(drawlist, params);
  updateGeometryCache(drawlist);
  renderBackgroundPass(drawlist, viewMat, projMat, params);
  renderMainScenePass(drawlist, viewMat, projMat, params);
  renderSelectionPass(drawlist, viewMat, projMat);
  endFrame();
  renderIDBufferPass(drawlist, viewMat, projMat, params);

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
  // Unified shader — Blinn-Phong + flat + billboard + textured
  // u_renderMode: 0=lit, 1=flat/unlit, 2=textured billboard, 3=textured world-space
  static const char * vertexSource =
    "#version 120\n"
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_texcoord;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_model;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_useVertexColor;\n"
    "uniform float u_renderMode;\n"
    // Billboard uniforms (mode 2)
    "uniform vec3 u_quadCenter;\n"
    "uniform vec2 u_texSize;\n"
    "uniform vec2 u_vpSize;\n"
    "varying vec3 v_eyePos;\n"
    "varying vec3 v_eyeNormal;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  v_color = (u_useVertexColor > 0.5) ? a_color : u_color;\n"
    "  v_texcoord = a_texcoord;\n"
    "  if (u_renderMode > 1.5 && u_renderMode < 2.5) {\n"
    // Billboard: project center, offset by pixel coords
    "    vec4 centerClip = u_proj * u_view * u_model * vec4(u_quadCenter, 1.0);\n"
    "    vec2 pixelOffset = (a_texcoord - vec2(0.5)) * u_texSize;\n"
    "    vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;\n"
    "    gl_Position = centerClip + vec4(ndcOffset * centerClip.w, 0.0, 0.0);\n"
    "    v_eyePos = vec3(0.0);\n"
    "    v_eyeNormal = vec3(0.0, 0.0, 1.0);\n"
    "  } else {\n"
    "    vec4 worldPos = u_model * vec4(a_position, 1.0);\n"
    "    vec4 eyePos = u_view * worldPos;\n"
    "    v_eyePos = eyePos.xyz;\n"
    "    v_eyeNormal = mat3(u_view) * mat3(u_model) * a_normal;\n"
    "    gl_Position = u_proj * eyePos;\n"
    "  }\n"
    "}\n";

  static const char * fragmentSource =
    "#version 120\n"
    "uniform float u_renderMode;\n"
    "uniform vec3 u_emissiveColor;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_texModColor;\n"
    "varying vec3 v_eyePos;\n"
    "varying vec3 v_eyeNormal;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    // Mode 1/1.5: flat/unlit (BASE_COLOR, lines, selection overlay, points)
    // 1.0 = flat, 1.5 = flat + point circle discard (replaces GL_POINT_SMOOTH)
    "  if (u_renderMode > 0.5 && u_renderMode < 1.8) {\n"
    "    if (u_renderMode > 1.2) {\n"
    "      vec2 pc = gl_PointCoord - vec2(0.5);\n"
    "      if (dot(pc, pc) > 0.25) discard;\n"
    "    }\n"
    "    gl_FragColor = v_color;\n"
    "    return;\n"
    "  }\n"
    // Mode 2: textured billboard
    "  if (u_renderMode > 1.8 && u_renderMode < 2.5) {\n"
    "    vec4 c = texture2D(u_texture, v_texcoord);\n"
    "    if (c.a < 0.3) discard;\n"  // ALPHA_DISCARD_THRESHOLD
    "    gl_FragColor = c * u_texModColor;\n"
    "    return;\n"
    "  }\n"
    // Mode 3: textured world-space (modulate with material)
    "  if (u_renderMode > 2.5) {\n"
    "    vec4 c = texture2D(u_texture, v_texcoord);\n"
    "    if (c.a < 0.3) discard;\n"  // ALPHA_DISCARD_THRESHOLD
    "    gl_FragColor = c * u_texModColor;\n"
    "    return;\n"
    "  }\n"
    // Mode 0: Blinn-Phong lit
    "  vec3 N = normalize(v_eyeNormal);\n"
    "  if (dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) N = -N;\n"
    "  vec3 L = vec3(0.0, 0.0, 1.0);\n"
    "  float NdotL = dot(N, L);\n"
    "  vec3 V = normalize(-v_eyePos);\n"
    "  vec3 H = normalize(L + V);\n"
    "  float NdotH = max(dot(N, H), 0.0);\n"
    "  float spec = pow(NdotH, 64.0);\n"  // DEFAULT_SHININESS
    "  vec3 ambient = 0.25 * v_color.rgb;\n"  // AMBIENT_COEFF
    "  vec3 diffuse = 0.85 * NdotL * v_color.rgb;\n"  // DIFFUSE_COEFF
    "  vec3 specular = 0.12 * spec * vec3(1.0);\n"  // SPECULAR_COEFF
    "  gl_FragColor = vec4(ambient + diffuse + specular + u_emissiveColor, v_color.a);\n"
    "}\n";

  GLuint vs = coin_compile_shader(GL_VERTEX_SHADER, vertexSource);
  GLuint fs = coin_compile_shader(GL_FRAGMENT_SHADER, fragmentSource);
  if (vs == 0 || fs == 0) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return FALSE;
  }

  // Bind attribute locations explicitly before linking — the macOS GLSL 1.20
  // compiler may reassign locations when a new attribute (a_texcoord) is added,
  // and we need stable locations that match the VAO setup.
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "a_position");
  glBindAttribLocation(prog, 1, "a_normal");
  glBindAttribLocation(prog, 2, "a_color");
  glBindAttribLocation(prog, 3, "a_texcoord");
  glLinkProgram(prog);
  GLint linkStatus = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
  if (linkStatus == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetProgramInfoLog(prog, length, &length, &log[0]);
      SoDebugError::postInfo("SoModernGLBackend::linkProgram", "%s", log.c_str());
    }
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return FALSE;
  }
  this->shaderProgram = prog;
  glDeleteShader(vs);
  glDeleteShader(fs);

  this->uViewLocation = glGetUniformLocation(this->shaderProgram, "u_view");
  this->uProjLocation = glGetUniformLocation(this->shaderProgram, "u_proj");
  this->uModelLocation = glGetUniformLocation(this->shaderProgram, "u_model");
  this->uColorLocation = glGetUniformLocation(this->shaderProgram, "u_color");
  this->uRenderModeLocation = glGetUniformLocation(this->shaderProgram, "u_renderMode");
  this->uEmissiveColorLocation = glGetUniformLocation(this->shaderProgram, "u_emissiveColor");
  this->uUseVertexColorLocation = glGetUniformLocation(this->shaderProgram, "u_useVertexColor");
  this->uTextureLocation = glGetUniformLocation(this->shaderProgram, "u_texture");
  this->uTexModColorLocation = glGetUniformLocation(this->shaderProgram, "u_texModColor");
  this->uQuadCenterLocation = glGetUniformLocation(this->shaderProgram, "u_quadCenter");
  this->uTexSizeLocation = glGetUniformLocation(this->shaderProgram, "u_texSize");
  this->uVpSizeLocation = glGetUniformLocation(this->shaderProgram, "u_vpSize");
  this->texcoordLoc = glGetAttribLocation(this->shaderProgram, "a_texcoord");

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
  return pickBuffer ? pickBuffer->getPickLineWidth() : DEFAULT_PICK_SIZE;
}

float
SoModernGLBackend::getPickPointSize() const
{
  return pickBuffer ? pickBuffer->getPickPointSize() : DEFAULT_PICK_SIZE;
}
