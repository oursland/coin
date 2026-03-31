// src/rendering/SoIDPickBuffer.cpp

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rendering/SoIDPickBuffer.h"
#include "rendering/SoModernIR.h"
#include "rendering/SoVAO.h"
#include "CoinTracyConfig.h"

#include <Inventor/errors/SoDebugError.h>
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
  // Clean up VBOs
  for (uint32_t vbo : idColorVBOs) {
    if (vbo) glDeleteBuffers(1, &vbo);
  }
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
                       const SoDrawList & drawlist)
{
  ZoneScopedN("IDPickBuffer::render");
  if (!fbo || !shaderInitialized) return;

  GLint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

  renderIdPass(viewMatrix, projMatrix, drawlist);

  // Synchronous readback every frame — read directly from fbo
  // Note: renderIdPass leaves fbo bound, so we're already reading from it.
  // But bind explicitly to be safe.
  size_t numPixels = static_cast<size_t>(fbWidth) * static_cast<size_t>(fbHeight);
  pboSize = numPixels * 4;
  cachedColor.resize(pboSize);

  // Readback is now done inside renderIdPass after edge pass.
  // (moved there to debug edge visibility in readback)

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void
SoIDPickBuffer::renderIdPass(const float * viewMatrix, const float * projMatrix,
                             const SoDrawList & drawlist)
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

  // Ensure temp VBOs exist
  if (tempPosVBO == 0) glGenBuffers(1, &tempPosVBO);
  if (tempIdxVBO == 0) glGenBuffers(1, &tempIdxVBO);

  // Get attribute locations
  GLint posLoc = glGetAttribLocation(shaderProgram, "aPos");
  GLint idColorLoc = glGetAttribLocation(shaderProgram, "aIdColor");

  // Helper: draw one command with its ID color VBO
  auto drawIdCmd = [&](const SoRenderCommand & cmd, int ci, GLenum prim) {
    if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) return;
    if (!cmd.geometry.positions || cmd.geometry.vertexCount == 0) return;

    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(uIdModel, 1, GL_FALSE, &modelMat[0][0]);

    GLsizei stride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

    // Upload positions to temp VBO
    glBindBuffer(GL_ARRAY_BUFFER, tempPosVBO);
    glBufferData(GL_ARRAY_BUFFER, cmd.geometry.vertexCount * stride,
                 cmd.geometry.positions, GL_STREAM_DRAW);
    if (posLoc >= 0) {
      glEnableVertexAttribArray(posLoc);
      glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, stride, NULL);
    }

    // Bind per-vertex ID color
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
  };

  // Pass 1: Triangles — normal depth test
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_TRIANGLES &&
        cmd.geometry.topology != SO_TOPOLOGY_TRIANGLE_STRIP) continue;
    drawIdCmd(cmd, ci, GL_TRIANGLES);
  }

  // Pass 2: Lines — depth ALWAYS, no depth write
  glDepthFunc(GL_ALWAYS);
  glDepthMask(GL_FALSE);
  {
    int edgeDrawn = 0, edgeSkipped = 0;
    for (int ci = 0; ci < numCmds; ci++) {
      const SoRenderCommand & cmd = drawlist.getCommand(ci);
      if (cmd.geometry.topology != SO_TOPOLOGY_LINES &&
          cmd.geometry.topology != SO_TOPOLOGY_LINE_STRIP) continue;
      if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) {
        edgeSkipped++;
        continue;
      }
      glLineWidth(std::max(cmd.state.raster.lineWidth, 7.0f));
      drawIdCmd(cmd, ci, GL_LINES);
      edgeDrawn++;
    }
    static int edgeLogCount = 0;
    if (edgeLogCount < 3) {
      std::fprintf(stderr, "IDPickBuffer: edge pass drawn=%d skipped=%d total=%d\n",
                   edgeDrawn, edgeSkipped, numCmds);
      edgeLogCount++;
    }
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  // Readback HERE — right after edge pass, FBO still bound with edges
  {
    size_t numPx = static_cast<size_t>(fbWidth) * static_cast<size_t>(fbHeight);
    pboSize = numPx * 4;
    cachedColor.resize(pboSize);
    glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, cachedColor.data());
  }

  // Pass 3: Points
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_POINTS) continue;
    glPointSize(7.0f);
    drawIdCmd(cmd, ci, GL_POINTS);
  }

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

  int side = 2 * pickRadius + 1;
  int x0 = std::max(0, x - pickRadius);
  int y0 = std::max(0, y - pickRadius);
  int x1 = std::min(fbWidth, x0 + side);
  int y1 = std::min(fbHeight, y0 + side);
  if (x1 <= x0 || y1 <= y0) return 0;

  // Center-priority pick.
  float cx = static_cast<float>(x);
  float cy = static_cast<float>(y);

  // Debug: log center pixel and count unique IDs in region
  {
    uint32_t centerId = 0;
    size_t cIdx = static_cast<size_t>(y) * static_cast<size_t>(fbWidth)
                + static_cast<size_t>(x);
    if (cIdx * 4 + 3 < cachedColor.size()) {
      centerId = decodeId(&cachedColor[cIdx * 4]);
    }
    // Count unique IDs in pick region
    int uniqueCount = 0;
    int zeroCount = 0;
    uint32_t ids[4] = {0, 0, 0, 0};  // track up to 4 unique
    for (int py = y0; py < y1; py++) {
      for (int px = x0; px < x1; px++) {
        size_t idx = static_cast<size_t>(py) * static_cast<size_t>(fbWidth)
                   + static_cast<size_t>(px);
        uint32_t id = decodeId(&cachedColor[idx * 4]);
        if (id == 0) { zeroCount++; continue; }
        bool found = false;
        for (int k = 0; k < uniqueCount && k < 4; k++) {
          if (ids[k] == id) { found = true; break; }
        }
        if (!found && uniqueCount < 4) {
          ids[uniqueCount++] = id;
        }
      }
    }
    // Decode type from center pixel raw value
    uint32_t rawCenter = 0;
    {
      size_t ci2 = static_cast<size_t>(y) * static_cast<size_t>(fbWidth)
                  + static_cast<size_t>(x);
      if (ci2 * 4 + 3 < cachedColor.size()) {
        rawCenter = (static_cast<uint32_t>(cachedColor[ci2*4]) << 24) |
                    (static_cast<uint32_t>(cachedColor[ci2*4+1]) << 16) |
                    (static_cast<uint32_t>(cachedColor[ci2*4+2]) << 8)  |
                    static_cast<uint32_t>(cachedColor[ci2*4+3]);
      }
    }
    int centerType = (rawCenter >> 30) & 0x3;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "raw=0x%08x type=%d id=%u unique=%d zero=%d",
                  rawCenter, centerType, centerId, uniqueCount, zeroCount);
    ZoneText(buf, std::strlen(buf));
  }

  uint32_t bestId = 0;
  float bestDistSq = static_cast<float>(pickRadius * pickRadius + 1);

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
