// src/rendering/SoIDPickBuffer.h

#ifndef COIN_SOIDPICKBUFFER_H
#define COIN_SOIDPICKBUFFER_H

#include <Inventor/SbBasic.h>

#include "rendering/SoModernIR.h"

#include <cstdint>
#include <vector>

/// Per-command cached VBO info passed from the visual pass backend
/// to the ID pick buffer, avoiding redundant geometry uploads.
struct SoIDPassVBOInfo {
  uint32_t posVBO;        ///< GL buffer name for positions (0 = not cached)
  uint32_t idxVBO;        ///< GL buffer name for indices (0 = not cached)
  uint32_t vertexStride;  ///< Stride used when VBO was uploaded
};

/*!
  \class SoIDPickBuffer
  \brief Off-screen FBO for GPU-based ID picking.

  Each renderable element (face, edge set, vertex set) is assigned a
  sequential uint32 ID via the pick LUT. The ID is encoded as per-vertex
  RGBA8 color and rendered to an off-screen FBO. A pixel read at the
  mouse position decodes the ID for O(1) element identification.

  This module is backend-agnostic in design but currently uses OpenGL
  directly for the FBO, shader, and readback. Future versions may
  accept an abstract GPU interface.

  Usage:
    1. Call initialize() with a valid GL context
    2. Call buildIdColorVBOs() after buildPickLUT() to create per-vertex
       ID color buffers for each render command
    3. Call render() each frame (or when camera/scene changes)
    4. Call pick() from any thread to read the CPU cache

  The module handles PBO double-buffering for async readback.
*/
class SoIDPickBuffer {
public:
  SoIDPickBuffer();
  ~SoIDPickBuffer();

  SoIDPickBuffer(const SoIDPickBuffer &) = delete;
  SoIDPickBuffer & operator=(const SoIDPickBuffer &) = delete;

  /// Initialize GL resources (shader, FBO). Requires valid GL context.
  SbBool initialize();

  /// Resize FBO when viewport changes. No-op if size unchanged.
  void resize(int width, int height);

  /// Build per-vertex ID color VBOs for each command in the draw list.
  /// Must be called after SoDrawList::buildPickLUT().
  /// @param drawlist  The draw list with commands and pick LUT
  /// @param contextId  GL context ID for VBO creation
  void buildIdColorVBOs(const SoDrawList & drawlist, uint32_t contextId);

  /// Render the ID buffer using the draw list's geometry + per-vertex IDs.
  /// @param viewMatrix   Column-major 4x4 view matrix
  /// @param projMatrix   Column-major 4x4 projection matrix
  /// @param drawlist     The draw list with commands (for VAO/transform access)
  /// @param vboCache     Optional per-command cached VBOs from the visual pass.
  ///                     When provided, the ID pass binds these instead of
  ///                     re-uploading positions/indices. Size must be >= numCommands.
  void render(const float * viewMatrix, const float * projMatrix,
              const SoDrawList & drawlist,
              const SoIDPassVBOInfo * vboCache = nullptr, int vboCacheCount = 0);

  /// Pick at pixel coordinates. Returns pick LUT index (1-based), or 0.
  /// Reads from CPU cache — no GL calls, safe from any thread.
  uint32_t pick(int x, int y, int pickRadius = 5) const;

  SbBool isInitialized() const { return shaderInitialized; }

  /// Set the pick line width for edges in the ID buffer (default 7.0)
  void setPickLineWidth(float w) { pickLineWidth = w; }
  float getPickLineWidth() const { return pickLineWidth; }

  /// Set the pick point size for vertices in the ID buffer (default 7.0)
  void setPickPointSize(float s) { pickPointSize = s; }
  float getPickPointSize() const { return pickPointSize; }
  size_t getIdColorVBOCount() const { return idColorVBOs.size(); }
  bool hasIdColorVBO(int ci) const {
    return ci >= 0 && ci < static_cast<int>(idColorVBOs.size()) && idColorVBOs[ci] != 0;
  }

  /// Debug: blit the ID buffer to the current framebuffer.
  void blitToScreen(int screenWidth, int screenHeight) const;

  // --- Encode/decode (trivial: uint32 ↔ RGBA8) ---
  static uint32_t decodeId(const uint8_t rgba[4]);

private:
  void renderIdPass(const float * viewMatrix, const float * projMatrix,
                    const SoDrawList & drawlist,
                    const SoIDPassVBOInfo * vboCache, int vboCacheCount);

  // GL resources
  uint32_t fbo = 0;
  uint32_t colorTex = 0;
  uint32_t depthRbo = 0;
  int fbWidth = 0;
  int fbHeight = 0;
  SbBool shaderInitialized = FALSE;

  // CPU-side pixel cache
  std::vector<uint8_t> cachedColor;

  // PBO double-buffering for async readback
  uint32_t pbo[2] = {0, 0};
  int pboIndex = 0;
  SbBool pboInitialized = FALSE;
  size_t pboSize = 0;

  // Per-command ID color VBOs (index = command index in draw list)
  std::vector<uint32_t> idColorVBOs;
  std::vector<int> idColorVertexCounts;

  // Temporary VBOs for CPU data upload in ID pass (fallback)
  uint32_t tempPosVBO = 0;
  uint32_t tempIdxVBO = 0;

  // Per-command ID VAOs (lazily built from cached VBOs + idColorVBOs)
  std::vector<uint32_t> idVAOs;
  std::vector<uint32_t> idVAOColorKey;  // idColorVBO bound when VAO was built
  std::vector<uint32_t> idVAOPosKey;    // posVBO bound when VAO was built

  // Cached attribute locations for the ID shader
  int cachedPosLoc = -1;
  int cachedIdColorLoc = -1;

  // Pick dimensions
  float pickLineWidth = 7.0f;
  float pickPointSize = 7.0f;

  // ID pass shader
  uint32_t shaderProgram = 0;
  int uIdView = -1;
  int uIdProj = -1;
  int uIdModel = -1;
};

#endif // COIN_SOIDPICKBUFFER_H
