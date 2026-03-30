// src/rendering/SoModernGLBackend.h

#ifndef COIN_SOMODERNGLBACKEND_H
#define COIN_SOMODERNGLBACKEND_H

#include "rendering/SoRenderBackend.h"
#include "rendering/SoIDPickBuffer.h"

#include <Inventor/system/gl.h>

#include <memory>

/*!
  \class SoModernGLBackend
  \brief OpenGL backend that renders IR draw lists with GPU picking.

  Implements the SoRenderBackend interface with real GPU rendering:
  - Geometry rendering via VBO/VAO or CPU-uploaded vertex data
  - Custom shader support via SoGLShaderProgram
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

  /// Access the pick buffer for debug visualization.
  SoIDPickBuffer * getPickBuffer() { return pickBuffer.get(); }

private:
  void logFrameStats(const SoDrawList & drawlist,
                     const SoRenderParams & params) const;

  SoRenderBackendInitParams storedparams;
  GLuint shaderProgram;
  GLuint vao;
  GLuint vertexBuffer;
  GLuint normalBuffer;
  GLuint indexBuffer;
  GLint  uViewLocation;
  GLint  uProjLocation;
  GLint  uModelLocation;
  GLint  uColorLocation;
  GLint  uEmissiveLocation;
  bool   vaoInitialized;
  bool   createShaders();

  // GPU picking
  std::unique_ptr<SoIDPickBuffer> pickBuffer;
  bool pickBufferDirty = true;
  size_t lastPickLUTSize = 0;
};

#endif // COIN_SOMODERNGLBACKEND_H
