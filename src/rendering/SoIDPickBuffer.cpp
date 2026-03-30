// src/rendering/SoIDPickBuffer.cpp

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rendering/SoIDPickBuffer.h"
#include "rendering/SoModernIR.h"

#include <Inventor/errors/SoDebugError.h>

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

static void encodeIdToRGBA(uint32_t id, uint8_t out[4])
{
  out[0] = static_cast<uint8_t>((id >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((id >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((id >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(id & 0xFF);
}

uint32_t
SoIDPickBuffer::decodeId(const uint8_t rgba[4])
{
  return (static_cast<uint32_t>(rgba[0]) << 24) |
         (static_cast<uint32_t>(rgba[1]) << 16) |
         (static_cast<uint32_t>(rgba[2]) << 8)  |
         static_cast<uint32_t>(rgba[3]);
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
      encodeIdToRGBA(lutId, rgba);

      if (le->eboCount > 0 && cmd.geometry.indices) {
        // Per-face: color vertices referenced by this face's index range
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
  if (!fbo || !shaderInitialized) return;

  GLint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

  renderIdPass(viewMatrix, projMatrix, drawlist);

  // PBO async readback
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
  }
  else {
    // Read previous frame's PBO (DMA already complete)
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
                             const SoDrawList & drawlist)
{
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

  // Pass 1: Triangles with polygon offset
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.0f, 1.0f);

  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_TRIANGLES &&
        cmd.geometry.topology != SO_TOPOLOGY_TRIANGLE_STRIP) continue;
    if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) continue;
    if (!cmd.geometry.cache.vao) continue;

    glUniformMatrix4fv(uIdModel, 1, GL_TRUE, cmd.modelMatrix[0]);
    glBindVertexArray(cmd.geometry.cache.vao->getGLId());

    // Bind per-vertex ID color as attribute 2
    glBindBuffer(GL_ARRAY_BUFFER, idColorVBOs[ci]);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);

    if (cmd.geometry.indexCount > 0) {
      glDrawElements(GL_TRIANGLES, cmd.geometry.indexCount, GL_UNSIGNED_INT, NULL);
    }
    else {
      glDrawArrays(GL_TRIANGLES, 0, cmd.geometry.vertexCount);
    }

    glDisableVertexAttribArray(2);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);

  // Pass 2: Lines
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_LINES &&
        cmd.geometry.topology != SO_TOPOLOGY_LINE_STRIP) continue;
    if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) continue;
    if (!cmd.geometry.cache.vao) continue;

    glUniformMatrix4fv(uIdModel, 1, GL_TRUE, cmd.modelMatrix[0]);
    glBindVertexArray(cmd.geometry.cache.vao->getGLId());

    glBindBuffer(GL_ARRAY_BUFFER, idColorVBOs[ci]);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);

    glLineWidth(std::max(cmd.state.raster.lineWidth, 5.0f));
    if (cmd.geometry.indexCount > 0) {
      glDrawElements(GL_LINES, cmd.geometry.indexCount, GL_UNSIGNED_INT, NULL);
    }
    else {
      glDrawArrays(GL_LINES, 0, cmd.geometry.vertexCount);
    }

    glDisableVertexAttribArray(2);
  }

  // Pass 3: Points
  for (int ci = 0; ci < numCmds; ci++) {
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.geometry.topology != SO_TOPOLOGY_POINTS) continue;
    if (ci >= static_cast<int>(idColorVBOs.size()) || idColorVBOs[ci] == 0) continue;
    if (!cmd.geometry.cache.vao) continue;

    glUniformMatrix4fv(uIdModel, 1, GL_TRUE, cmd.modelMatrix[0]);
    glBindVertexArray(cmd.geometry.cache.vao->getGLId());

    glBindBuffer(GL_ARRAY_BUFFER, idColorVBOs[ci]);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);

    glPointSize(7.0f);
    glDrawArrays(GL_POINTS, 0, cmd.geometry.vertexCount);

    glDisableVertexAttribArray(2);
  }

  glBindVertexArray(0);
}

// -----------------------------------------------------------------------
// Pick
// -----------------------------------------------------------------------

uint32_t
SoIDPickBuffer::pick(int x, int y, int pickRadius) const
{
  if (!fbo || cachedColor.empty()) return 0;

  int side = 2 * pickRadius + 1;
  int x0 = std::max(0, x - pickRadius);
  int y0 = std::max(0, y - pickRadius);
  int x1 = std::min(fbWidth, x0 + side);
  int y1 = std::min(fbHeight, y0 + side);
  if (x1 <= x0 || y1 <= y0) return 0;

  // Center-priority: prefer pixel closest to cursor
  uint32_t bestId = 0;
  float bestDistSq = static_cast<float>(pickRadius * pickRadius + 1);
  float cx = static_cast<float>(x);
  float cy = static_cast<float>(y);

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

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, colorTex);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, screenWidth, 0, screenHeight, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glUseProgram(0);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
  glTexCoord2f(1.0f, 0.0f); glVertex2f((float)screenWidth, 0.0f);
  glTexCoord2f(1.0f, 1.0f); glVertex2f((float)screenWidth, (float)screenHeight);
  glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (float)screenHeight);
  glEnd();

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glBindTexture(GL_TEXTURE_2D, 0);
}
