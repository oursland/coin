// src/rendering/SoIDPickBuffer.cpp

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rendering/SoIDPickBuffer.h"
#include "rendering/SoModernIR.h"
#include "rendering/SoVAO.h"
#include "CoinTracyConfig.h"

#include <Inventor/errors/SoDebugError.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/system/gl.h>

// macOS legacy GL headers provide VAO functions with APPLE suffix
#if defined(__APPLE__) && !defined(glGenVertexArrays)
#define glGenVertexArrays    glGenVertexArraysAPPLE
#define glBindVertexArray    glBindVertexArrayAPPLE
#define glDeleteVertexArrays glDeleteVertexArraysAPPLE
#endif

#include <Inventor/C/glue/gl.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

// -----------------------------------------------------------------------
// ID Shader sources (GLSL 1.20 for maximum compatibility)
// -----------------------------------------------------------------------

static const char * idVertexShader = R"(
#version 120
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec4 aIdColor;
varying vec4 vIdColor;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
void main() {
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
    vIdColor = aIdColor;
}
)";

static const char * idFragmentShader = R"(
#version 120
varying vec4 vIdColor;
void main() {
    gl_FragColor = vIdColor;
}
)";

// -----------------------------------------------------------------------
// Encode / Decode
// -----------------------------------------------------------------------

// Encode: bits 31-30 = element type (0=face, 1=edge, 2=vertex, 3=reserved)
//         bits 29-0  = LUT index (1-based, max ~1 billion)
static void encodeIdWithType(uint32_t lutId, uint8_t elementType, uint8_t out[4])
{
  uint32_t encoded = (static_cast<uint32_t>(elementType & 0x3) << 30) | (lutId & 0x3FFFFFFF);
  out[0] = static_cast<uint8_t>((encoded >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((encoded >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((encoded >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(encoded & 0xFF);
}

// Legacy encode without type (for backward compat)
static void encodeIdToRGBA(uint32_t id, uint8_t out[4])
{
  encodeIdWithType(id, 0, out);
}

uint32_t
SoIDPickBuffer::decodeId(const uint8_t rgba[4])
{
  uint32_t raw = (static_cast<uint32_t>(rgba[0]) << 24) |
                 (static_cast<uint32_t>(rgba[1]) << 16) |
                 (static_cast<uint32_t>(rgba[2]) << 8)  |
                 static_cast<uint32_t>(rgba[3]);
  // Strip type bits, return LUT index
  return raw & 0x3FFFFFFF;
}

// -----------------------------------------------------------------------
// Shader helpers
// -----------------------------------------------------------------------

static GLuint compileShader(GLenum type, const char * src)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    SoDebugError::post("SoIDPickBuffer", "Shader compile error: %s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint linkProgram(GLuint vs, GLuint fs)
{
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "aPos");
  glBindAttribLocation(prog, 1, "aNormal");
  glBindAttribLocation(prog, 2, "aIdColor");
  glLinkProgram(prog);
  GLint ok;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(prog, sizeof(log), NULL, log);
    SoDebugError::post("SoIDPickBuffer", "Program link error: %s", log);
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------

SoIDPickBuffer::SoIDPickBuffer() = default;

SoIDPickBuffer::~SoIDPickBuffer()
{
  for (uint32_t vbo : idColorVBOs) {
    if (vbo) glDeleteBuffers(1, &vbo);
  }
  for (uint32_t vao : idVAOs) {
    if (vao) glDeleteVertexArrays(1, &vao);
  }
  if (tempPosVBO) glDeleteBuffers(1, &tempPosVBO);
  if (tempIdxVBO) glDeleteBuffers(1, &tempIdxVBO);
  if (pbo[0]) glDeleteBuffers(2, pbo);
  if (colorTex) glDeleteTextures(1, &colorTex);
  if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (shaderProgram) glDeleteProgram(shaderProgram);
}

// -----------------------------------------------------------------------
// Initialize
// -----------------------------------------------------------------------

SbBool
SoIDPickBuffer::initialize()
{
  if (shaderInitialized) return TRUE;

  GLuint vs = compileShader(GL_VERTEX_SHADER, idVertexShader);
  if (!vs) return FALSE;
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, idFragmentShader);
  if (!fs) { glDeleteShader(vs); return FALSE; }

  shaderProgram = linkProgram(vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (!shaderProgram) return FALSE;

  uIdView = glGetUniformLocation(shaderProgram, "uView");
  uIdProj = glGetUniformLocation(shaderProgram, "uProj");
  uIdModel = glGetUniformLocation(shaderProgram, "uModel");
  cachedPosLoc = glGetAttribLocation(shaderProgram, "aPos");
  cachedIdColorLoc = glGetAttribLocation(shaderProgram, "aIdColor");

  shaderInitialized = TRUE;
  return TRUE;
}

// -----------------------------------------------------------------------
// Resize
// -----------------------------------------------------------------------

void
SoIDPickBuffer::resize(int width, int height)
{
  if (width == fbWidth && height == fbHeight && fbo != 0) return;

  if (pbo[0]) { glDeleteBuffers(2, pbo); pbo[0] = pbo[1] = 0; pboInitialized = FALSE; }
  if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
  if (depthRbo) { glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
  if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }

  fbWidth = width;
  fbHeight = height;
  if (width <= 0 || height <= 0) return;

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  glGenTextures(1, &colorTex);
  glBindTexture(GL_TEXTURE_2D, colorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

  glGenRenderbuffers(1, &depthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    SoDebugError::post("SoIDPickBuffer", "FBO incomplete (0x%x)", status);
    glDeleteFramebuffers(1, &fbo);
    fbo = 0;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// -----------------------------------------------------------------------
// Build per-vertex ID color VBOs
// -----------------------------------------------------------------------

void
SoIDPickBuffer::buildIdColorVBOs(const SoDrawList & drawlist, uint32_t /*contextId*/)
{
  ZoneScopedN("buildIdColorVBOs");
  const auto & lut = drawlist.getPickLUT();
  int numCmds = drawlist.getNumCommands();

  // Ensure we have enough VBO slots
  if (static_cast<int>(idColorVBOs.size()) < numCmds) {
    idColorVBOs.resize(numCmds, 0);
    idColorVertexCounts.resize(numCmds, 0);
  }

  // Group LUT entries by command index
  std::unordered_map<int, std::vector<std::pair<uint32_t, const SoPickLUTEntry *>>> byCmd;
  for (uint32_t i = 0; i < lut.size(); i++) {
    byCmd[lut[i].commandIndex].push_back({i + 1, &lut[i]});
  }

  for (auto & [ci, entries] : byCmd) {
    if (ci < 0 || ci >= numCmds) continue;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    int numVerts = static_cast<int>(cmd.geometry.vertexCount);
    if (numVerts <= 0) continue;

    // Allocate RGBA8 per-vertex color buffer
    std::vector<uint8_t> colors(static_cast<size_t>(numVerts) * 4, 0);

    for (const auto & [lutId, le] : entries) {
      uint8_t rgba[4];
      // Encode element type in upper 2 bits: 0=face, 1=edge, 2=vertex
      uint8_t typeCode = 0;
      if (le->elementType == SO_PICK_EDGE) typeCode = 1;
      else if (le->elementType == SO_PICK_VERTEX) typeCode = 2;
      encodeIdWithType(lutId, typeCode, rgba);

      if (le->eboCount > 0 && cmd.geometry.indices) {
        // Per-face/edge: color vertices referenced by this element's index range
        int start = le->eboOffset;
        int end = std::min(start + le->eboCount,
                           static_cast<int>(cmd.geometry.indexCount));
        for (int idx = start; idx < end; idx++) {
          uint32_t vi = cmd.geometry.indices[idx];
          if (vi < static_cast<uint32_t>(numVerts)) {
            colors[vi * 4 + 0] = rgba[0];
            colors[vi * 4 + 1] = rgba[1];
            colors[vi * 4 + 2] = rgba[2];
            colors[vi * 4 + 3] = rgba[3];
          }
        }
      }
      else if (le->eboCount == 1 && !cmd.geometry.indices) {
        // Per-vertex (non-indexed): color single vertex at eboOffset
        int vi = le->eboOffset;
        if (vi >= 0 && vi < numVerts) {
          colors[vi * 4 + 0] = rgba[0];
          colors[vi * 4 + 1] = rgba[1];
          colors[vi * 4 + 2] = rgba[2];
          colors[vi * 4 + 3] = rgba[3];
        }
      }
      else {
        // Whole command: all vertices get the same color
        for (int v = 0; v < numVerts; v++) {
          colors[v * 4 + 0] = rgba[0];
          colors[v * 4 + 1] = rgba[1];
          colors[v * 4 + 2] = rgba[2];
          colors[v * 4 + 3] = rgba[3];
        }
      }
    }

    // Upload VBO
    if (idColorVBOs[ci] == 0) {
      glGenBuffers(1, &idColorVBOs[ci]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, idColorVBOs[ci]);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(colors.size()),
                 colors.data(), GL_DYNAMIC_DRAW);
    idColorVertexCounts[ci] = numVerts;
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// -----------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------

void
SoIDPickBuffer::render(const float * viewMatrix, const float * projMatrix,
                       const SoDrawList & drawlist,
                       const SoIDPassVBOInfo * vboCache, int vboCacheCount)
{
  ZoneScopedN("IDPickBuffer::render");
  if (!fbo || !shaderInitialized) return;

  GLint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

  renderIdPass(viewMatrix, projMatrix, drawlist, vboCache, vboCacheCount);

  // PBO double-buffer async readback — reads previous frame's data,
  // starts DMA for current frame. One frame latency is acceptable for picking.
  size_t numPixels = static_cast<size_t>(fbWidth) * static_cast<size_t>(fbHeight);
  pboSize = numPixels * 4;

  if (!pboInitialized) {
    glGenBuffers(2, pbo);
    for (int i = 0; i < 2; i++) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
      glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(pboSize),
                   NULL, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboInitialized = TRUE;
    // First frame: synchronous readback
    cachedColor.resize(pboSize);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, cachedColor.data());
    // Prime both PBOs
    for (int i = 0; i < 2; i++) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
      glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
  }
  else {
    // Read previous frame's PBO
    int readPbo = 1 - pboIndex;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[readPbo]);
    const uint8_t * ptr = static_cast<const uint8_t *>(
        glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    if (ptr) {
      cachedColor.resize(pboSize);
      std::memcpy(cachedColor.data(), ptr, pboSize);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    // Start async DMA for current frame
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pboIndex]);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboIndex = 1 - pboIndex;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void
SoIDPickBuffer::renderIdPass(const float * viewMatrix, const float * projMatrix,
                             const SoDrawList & drawlist,
                             const SoIDPassVBOInfo * vboCache, int vboCacheCount)
{
  ZoneScopedN("IDPickBuffer::renderIdPass");

  // Save GL state that we modify
  GLint prevViewport[4];
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  GLfloat prevLineWidth;
  glGetFloatv(GL_LINE_WIDTH, &prevLineWidth);
  GLfloat prevPointSize;
  glGetFloatv(GL_POINT_SIZE, &prevPointSize);
  GLint prevProgram;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, fbWidth, fbHeight);

  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  glUseProgram(shaderProgram);
  glUniformMatrix4fv(uIdView, 1, GL_FALSE, viewMatrix);
  glUniformMatrix4fv(uIdProj, 1, GL_FALSE, projMatrix);

  int numCmds = drawlist.getNumCommands();

  // Temp VBOs as fallback when no cached VBOs available
  if (tempPosVBO == 0) glGenBuffers(1, &tempPosVBO);
  if (tempIdxVBO == 0) glGenBuffers(1, &tempIdxVBO);

  GLint posLoc = cachedPosLoc;
  GLint idColorLoc = cachedIdColorLoc;

  // Ensure ID VAO vectors are large enough
  if (static_cast<int>(idVAOs.size()) < numCmds) {
    idVAOs.resize(numCmds, 0);
    idVAOColorKey.resize(numCmds, 0);
    idVAOPosKey.resize(numCmds, 0);
  }

  // Helper: draw one command using cached VBOs + ID VAO when available
  auto drawIdCmd = [&](const SoRenderCommand & cmd, int ci, GLenum prim) {
    if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) return;
    if (!cmd.geometry.positions || cmd.geometry.vertexCount == 0) return;

    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(uIdModel, 1, GL_FALSE, &modelMat[0][0]);

    bool useCached = (vboCache && ci < vboCacheCount && vboCache[ci].posVBO != 0);

    if (useCached) {
      // Check if ID VAO needs (re)building
      uint32_t curPosVBO = vboCache[ci].posVBO;
      uint32_t curColorVBO = idColorVBOs[ci];
      if (idVAOs[ci] == 0 || idVAOColorKey[ci] != curColorVBO
          || idVAOPosKey[ci] != curPosVBO) {
        // Build/rebuild the ID VAO
        if (idVAOs[ci] == 0) glGenVertexArrays(1, &idVAOs[ci]);
        glBindVertexArray(idVAOs[ci]);

        GLsizei stride = static_cast<GLsizei>(vboCache[ci].vertexStride);
        if (posLoc >= 0) {
          glBindBuffer(GL_ARRAY_BUFFER, curPosVBO);
          glEnableVertexAttribArray(posLoc);
          glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, stride, NULL);
        }
        if (idColorLoc >= 0) {
          glBindBuffer(GL_ARRAY_BUFFER, curColorVBO);
          glEnableVertexAttribArray(idColorLoc);
          glVertexAttribPointer(idColorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
        }
        if (vboCache[ci].idxVBO != 0) {
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboCache[ci].idxVBO);
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        idVAOColorKey[ci] = curColorVBO;
        idVAOPosKey[ci] = curPosVBO;
      }

      // Fast path: bind VAO + draw (3 GL calls)
      glBindVertexArray(idVAOs[ci]);
      if (cmd.geometry.indexCount > 0) {
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, NULL);
      }
      else {
        glDrawArrays(prim, 0, cmd.geometry.vertexCount);
      }
    }
    else {
      // Fallback: manual attribute setup with temp VBOs
      GLsizei stride = static_cast<GLsizei>(
        cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
      glBindBuffer(GL_ARRAY_BUFFER, tempPosVBO);
      glBufferData(GL_ARRAY_BUFFER, cmd.geometry.vertexCount * stride,
                   cmd.geometry.positions, GL_STREAM_DRAW);
      if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, stride, NULL);
      }
      if (idColorLoc >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, idColorVBOs[ci]);
        glEnableVertexAttribArray(idColorLoc);
        glVertexAttribPointer(idColorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
      }
      if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tempIdxVBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     cmd.geometry.indexCount * sizeof(uint32_t),
                     cmd.geometry.indices, GL_STREAM_DRAW);
        glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, NULL);
      }
      else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDrawArrays(prim, 0, cmd.geometry.vertexCount);
      }
      if (posLoc >= 0) glDisableVertexAttribArray(posLoc);
      if (idColorLoc >= 0) glDisableVertexAttribArray(idColorLoc);
    }
  };

  // Pass 1: Triangles — normal depth test, standard rendering
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_TRIANGLES &&
        cmd.geometry.topology != SO_TOPOLOGY_TRIANGLE_STRIP) continue;
    drawIdCmd(cmd, ci, GL_TRIANGLES);
  }

  // Pass 2: Edges — GL_LEQUAL so edges overwrite their own coplanar faces
  // but are correctly occluded by nearer geometry. No depth write so the
  // face depth buffer stays intact for the vertex pass.
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_LINES &&
        cmd.geometry.topology != SO_TOPOLOGY_LINE_STRIP) continue;
    glLineWidth(std::max(cmd.state.raster.lineWidth, pickLineWidth));
    drawIdCmd(cmd, ci, GL_LINES);
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  // Pass 3: Vertices — GL_LEQUAL so vertices overwrite coplanar faces/edges
  // but are occluded by nearer geometry.
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_POINTS) continue;
    glPointSize(std::max(cmd.state.raster.lineWidth, pickPointSize));
    drawIdCmd(cmd, ci, GL_POINTS);
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  // Restore GL state
  glLineWidth(prevLineWidth);
  glPointSize(prevPointSize);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
  glUseProgram(prevProgram);
}

// -----------------------------------------------------------------------
// Pick
// -----------------------------------------------------------------------

uint32_t
SoIDPickBuffer::pick(int x, int y, int pickRadius) const
{
  ZoneScopedN("IDPickBuffer::pick");
  if (!fbo || cachedColor.empty()) return 0;

  // Scale viewport coordinates to ID buffer resolution
  int sx = static_cast<int>(x * pickScaleX);
  int sy = static_cast<int>(y * pickScaleY);
  int sr = std::max(1, static_cast<int>(pickRadius * std::min(pickScaleX, pickScaleY)));

  int side = 2 * sr + 1;
  int x0 = std::max(0, sx - sr);
  int y0 = std::max(0, sy - sr);
  int x1 = std::min(fbWidth, x0 + side);
  int y1 = std::min(fbHeight, y0 + side);
  if (x1 <= x0 || y1 <= y0) return 0;

  // Center-priority pick: prefer the non-zero ID closest to (sx, sy).
  float cx = static_cast<float>(sx);
  float cy = static_cast<float>(sy);

  uint32_t bestId = 0;
  float bestDistSq = static_cast<float>(sr * sr + 1);

  for (int py = y0; py < y1; py++) {
    for (int px = x0; px < x1; px++) {
      size_t idx = static_cast<size_t>(py) * static_cast<size_t>(fbWidth)
                 + static_cast<size_t>(px);
      const uint8_t * rgba = &cachedColor[idx * 4];
      uint32_t id = decodeId(rgba);
      if (id == 0) continue;

      float dx = static_cast<float>(px) - cx;
      float dy = static_cast<float>(py) - cy;
      float distSq = dx * dx + dy * dy;
      if (distSq < bestDistSq) {
        bestDistSq = distSq;
        bestId = id;
      }
    }
  }

  return bestId;
}

// -----------------------------------------------------------------------
// Compute intersection
// -----------------------------------------------------------------------

bool
SoIDPickBuffer::computeIntersection(uint32_t lutIndex, const SoDrawList & drawlist,
                                    const float * viewMatrix, const float * projMatrix,
                                    int pixelX, int pixelY, int vpWidth, int vpHeight,
                                    SbVec3f & outWorldPoint) const
{
  if (lutIndex == 0 || vpWidth <= 0 || vpHeight <= 0) return false;
  const auto & lut = drawlist.getPickLUT();
  if (lutIndex > lut.size()) return false;

  const SoPickLUTEntry & entry = lut[lutIndex - 1];
  int cmdIdx = entry.commandIndex;
  if (cmdIdx < 0 || cmdIdx >= drawlist.getNumCommands()) return false;

  const SoRenderCommand & cmd = drawlist.getCommand(cmdIdx);
  if (!cmd.geometry.positions) return false;

  // Build inverse(proj * view) for unprojection
  SbMatrix view, proj;
  view = SbMatrix(viewMatrix[0], viewMatrix[1], viewMatrix[2], viewMatrix[3],
                  viewMatrix[4], viewMatrix[5], viewMatrix[6], viewMatrix[7],
                  viewMatrix[8], viewMatrix[9], viewMatrix[10], viewMatrix[11],
                  viewMatrix[12], viewMatrix[13], viewMatrix[14], viewMatrix[15]);
  proj = SbMatrix(projMatrix[0], projMatrix[1], projMatrix[2], projMatrix[3],
                  projMatrix[4], projMatrix[5], projMatrix[6], projMatrix[7],
                  projMatrix[8], projMatrix[9], projMatrix[10], projMatrix[11],
                  projMatrix[12], projMatrix[13], projMatrix[14], projMatrix[15]);

  SbMatrix vpMat = view * proj;
  SbMatrix invVP = vpMat.inverse();

  // Unproject pixel to NDC then to world ray
  float ndcX = (2.0f * pixelX / vpWidth) - 1.0f;
  float ndcY = (2.0f * pixelY / vpHeight) - 1.0f;

  SbVec3f nearPt, farPt;
  SbVec4f nearNDC(ndcX, ndcY, -1.0f, 1.0f);
  SbVec4f farNDC(ndcX, ndcY, 1.0f, 1.0f);

  // Transform by inverse VP
  // Manual 4x4 * vec4 since SbMatrix doesn't have SbVec4f overload
  SbMat inv;
  invVP.getValue(inv);
  auto mul4 = [&inv](float x, float y, float z, float w) -> SbVec4f {
    return SbVec4f(
      inv[0][0]*x + inv[1][0]*y + inv[2][0]*z + inv[3][0]*w,
      inv[0][1]*x + inv[1][1]*y + inv[2][1]*z + inv[3][1]*w,
      inv[0][2]*x + inv[1][2]*y + inv[2][2]*z + inv[3][2]*w,
      inv[0][3]*x + inv[1][3]*y + inv[2][3]*z + inv[3][3]*w);
  };
  SbVec4f nearW = mul4(ndcX, ndcY, -1.0f, 1.0f);
  SbVec4f farW = mul4(ndcX, ndcY, 1.0f, 1.0f);

  if (std::abs(nearW[3]) < 1e-10f || std::abs(farW[3]) < 1e-10f) return false;
  nearPt = SbVec3f(nearW[0]/nearW[3], nearW[1]/nearW[3], nearW[2]/nearW[3]);
  farPt = SbVec3f(farW[0]/farW[3], farW[1]/farW[3], farW[2]/farW[3]);

  SbVec3f rayOrigin = nearPt;
  SbVec3f rayDir = farPt - nearPt;
  rayDir.normalize();

  // Get the model matrix for this command
  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  SbMatrix model(modelMat);

  uint32_t stride = cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3;

  // For faces: intersect triangles in the element's EBO range
  if (entry.elementType == SO_PICK_FACE && cmd.geometry.indices &&
      entry.eboCount >= 3) {
    int start = entry.eboOffset;
    int end = std::min(start + entry.eboCount, static_cast<int>(cmd.geometry.indexCount));

    float bestT = 1e30f;
    bool hit = false;

    for (int i = start; i + 2 < end; i += 3) {
      uint32_t i0 = cmd.geometry.indices[i];
      uint32_t i1 = cmd.geometry.indices[i + 1];
      uint32_t i2 = cmd.geometry.indices[i + 2];

      // Bounds check vertex indices
      if (i0 >= cmd.geometry.vertexCount || i1 >= cmd.geometry.vertexCount
          || i2 >= cmd.geometry.vertexCount) continue;

      auto getVert = [&](uint32_t idx) -> SbVec3f {
        const float * p = reinterpret_cast<const float *>(
          reinterpret_cast<const char *>(cmd.geometry.positions) + idx * stride);
        SbVec3f objPt(p[0], p[1], p[2]);
        SbVec3f worldPt;
        model.multVecMatrix(objPt, worldPt);
        return worldPt;
      };

      SbVec3f v0 = getVert(i0), v1 = getVert(i1), v2 = getVert(i2);

      // Möller-Trumbore ray-triangle intersection
      SbVec3f e1 = v1 - v0, e2 = v2 - v0;
      SbVec3f h = rayDir.cross(e2);
      float a = e1.dot(h);
      if (std::abs(a) < 1e-10f) continue;
      float f = 1.0f / a;
      SbVec3f s = rayOrigin - v0;
      float u = f * s.dot(h);
      if (u < 0.0f || u > 1.0f) continue;
      SbVec3f q = s.cross(e1);
      float v = f * rayDir.dot(q);
      if (v < 0.0f || u + v > 1.0f) continue;
      float t = f * e2.dot(q);
      if (t > 1e-6f && t < bestT) {
        bestT = t;
        outWorldPoint = rayOrigin + rayDir * t;
        hit = true;
      }
    }
    return hit;
  }

  // For edges/vertices: use the element's position directly
  if ((entry.elementType == SO_PICK_EDGE || entry.elementType == SO_PICK_VERTEX)
      && cmd.geometry.positions) {
    // Use the first vertex of the element as the intersection point
    uint32_t vertIdx = 0;
    if (cmd.geometry.indices && entry.eboOffset < static_cast<int>(cmd.geometry.indexCount)) {
      vertIdx = cmd.geometry.indices[entry.eboOffset];
    } else if (entry.eboOffset >= 0) {
      vertIdx = static_cast<uint32_t>(entry.eboOffset);
    }
    const float * p = reinterpret_cast<const float *>(
      reinterpret_cast<const char *>(cmd.geometry.positions) + vertIdx * stride);
    SbVec3f objPt(p[0], p[1], p[2]);
    model.multVecMatrix(objPt, outWorldPoint);
    return true;
  }

  return false;
}

// -----------------------------------------------------------------------
// Debug blit
// -----------------------------------------------------------------------

void
SoIDPickBuffer::blitToScreen(int screenWidth, int screenHeight) const
{
  if (!fbo || !colorTex) return;

  // Use glBlitFramebuffer for compatibility (no legacy GL needed)
  GLint prevReadFbo = 0, prevDrawFbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));

  glBlitFramebuffer(
    0, 0, fbWidth, fbHeight,
    0, 0, screenWidth, screenHeight,
    GL_COLOR_BUFFER_BIT, GL_NEAREST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
}
