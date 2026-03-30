// src/rendering/SoIDPickBuffer.h

#ifndef COIN_SOIDPICKBUFFER_H
#define COIN_SOIDPICKBUFFER_H

#include <Inventor/SbBasic.h>

#include "rendering/SoModernIR.h"

#include <cstdint>
#include <vector>

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
  void render(const float * viewMatrix, const float * projMatrix,
              const SoDrawList & drawlist);

  /// Pick at pixel coordinates. Returns pick LUT index (1-based), or 0.
  /// Reads from CPU cache — no GL calls, safe from any thread.
  uint32_t pick(int x, int y, int pickRadius = 5) const;

  SbBool isInitialized() const { return shaderInitialized; }

  /// Debug: blit the ID buffer to the current framebuffer.
  void blitToScreen(int screenWidth, int screenHeight) const;

  // --- Encode/decode (trivial: uint32 ↔ RGBA8) ---
  static uint32_t decodeId(const uint8_t rgba[4]);

private:
  void renderIdPass(const float * viewMatrix, const float * projMatrix,
                    const SoDrawList & drawlist);

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

  // Temporary VBOs for CPU data upload in ID pass
  uint32_t tempPosVBO = 0;
  uint32_t tempIdxVBO = 0;

  // ID pass shader
  uint32_t shaderProgram = 0;
  int uIdView = -1;
  int uIdProj = -1;
  int uIdModel = -1;
};

#endif // COIN_SOIDPICKBUFFER_H
