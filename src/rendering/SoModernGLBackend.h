// src/rendering/SoModernGLBackend.h

#ifndef COIN_SOMODERNGLBACKEND_H
#define COIN_SOMODERNGLBACKEND_H

#include "rendering/SoRenderBackend.h"
#include "rendering/SoIDPickBuffer.h"

#include <Inventor/system/gl.h>

#include <memory>
#include <unordered_map>
#include <vector>

/// Per-command GPU resource cache entry.
/// Caches VBOs and VAOs for a single draw command, keyed by CPU data pointer.
struct CachedGPUCommand {
  GLuint posVBO = 0;
  GLuint normVBO = 0;
  GLuint colorVBO = 0;
  GLuint idxVBO = 0;
  GLuint vao = 0;       // visual pass VAO (pos + norm + color + idx)
  GLuint idVAO = 0;     // ID pass VAO (pos + idColor + idx)

  // Cache invalidation keys (CPU pointer addresses + counts)
  const float *    posKey = nullptr;
  const float *    normKey = nullptr;
  const float *    colorKey = nullptr;
  const uint32_t * idxKey = nullptr;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint32_t vertexStride = 0;

  // ID color VBO that was bound when idVAO was last built
  GLuint boundIdColorVBO = 0;

  int lastUsedFrame = 0;

  bool isGeometryValid(const float * pos, const float * norm,
                       const uint32_t * idx, uint32_t vCount,
                       uint32_t iCount, uint32_t vStride) const {
    return posVBO != 0 && pos == posKey && norm == normKey
        && idx == idxKey && vCount == vertexCount
        && iCount == indexCount && vStride == vertexStride;
  }
};

/*!
  \class SoModernGLBackend
  \brief OpenGL backend that renders IR draw lists with GPU picking.

  Implements the SoRenderBackend interface with real GPU rendering:
  - Per-command VBO/VAO caching (GL_STATIC_DRAW, only re-upload on change)
  - GPU ID buffer picking via SoIDPickBuffer (O(1) per pick)
  - Pick LUT integration for per-face element identification
*/
class SoModernGLBackend : public SoRenderBackend {
public:
  SoModernGLBackend();
  ~SoModernGLBackend() override;

  const char * getName() const override;

  SbBool initialize(const SoRenderBackendInitParams & params) override;
  void shutdown() override;
  SbBool render(const SoDrawList & drawlist,
                const SoRenderParams & params) override;
  void resizeTarget(const SoRenderTargetInfo & info) override;

  /// GPU pick at pixel coordinates. Returns pick LUT index (1-based).
  uint32_t pick(int x, int y, int pickRadius = 5) const override;

  void setPickLineWidth(float width) override;
  void setPickPointSize(float size) override;
  float getPickLineWidth() const override;
  float getPickPointSize() const override;

  /// Access the pick buffer for debug visualization.
  SoIDPickBuffer * getPickBuffer() { return pickBuffer.get(); }

  /// Get cached GPU entry for a command index (for ID pass sharing).
  const CachedGPUCommand * getCachedCommand(int cmdIndex) const;

private:
  void logFrameStats(const SoDrawList & drawlist,
                     const SoRenderParams & params) const;
  bool createShaders();

  CachedGPUCommand & getOrCreateCache(const float * posPtr, const uint32_t * idxPtr);
  void uploadGeometry(CachedGPUCommand & entry, const SoRenderCommand & cmd);
  void setupVisualVAO(CachedGPUCommand & entry, const SoRenderCommand & cmd);
  void gcStaleEntries(int currentFrame);
  void destroyCacheEntry(CachedGPUCommand & entry);

  SoRenderBackendInitParams storedparams;
  GLuint shaderProgram = 0;
  GLint  uViewLocation = -1;
  GLint  uProjLocation = -1;
  GLint  uModelLocation = -1;
  GLint  uColorLocation = -1;
  GLint  uEmissiveLocation = -1;
  GLint  uUseVertexColorLocation = -1;

  // Cached attribute locations
  GLint posLoc = -1;
  GLint normLoc = -1;
  GLint colorLoc = -1;

  // Per-command GPU cache, keyed by (positions ptr, indices ptr) pair.
  // Two commands may share the same coordinate data but have different
  // index buffers (e.g., face set and edge set on same shape).
  struct CacheKey {
    const float * pos;
    const uint32_t * idx;
    bool operator==(const CacheKey & o) const { return pos == o.pos && idx == o.idx; }
  };
  struct CacheKeyHash {
    size_t operator()(const CacheKey & k) const {
      auto h1 = std::hash<const void *>()(k.pos);
      auto h2 = std::hash<const void *>()(k.idx);
      return h1 ^ (h2 << 1);
    }
  };
  std::vector<CachedGPUCommand> gpuCache;
  std::unordered_map<CacheKey, int, CacheKeyHash> ptrToCacheIndex;
  int currentFrame = 0;

  // GPU picking
  std::unique_ptr<SoIDPickBuffer> pickBuffer;
  bool pickBufferDirty = true;
  size_t lastPickLUTSize = 0;

  // Previous frame's view/proj for camera change detection
  SbMatrix lastViewMatrix;
  SbMatrix lastProjMatrix;
  bool matricesInitialized = false;
};

#endif // COIN_SOMODERNGLBACKEND_H
