// src/rendering/SoModernGLBackend.h

#ifndef COIN_SOMODERNGLBACKEND_H
#define COIN_SOMODERNGLBACKEND_H

#include "rendering/SoRenderBackend.h"

#include <Inventor/system/gl.h>

/*!
  \class SoModernGLBackend
  \brief Stub backend that validates and logs IR draw lists.

  The class implements the SoRenderBackend interface but deliberately
  stops short of issuing OpenGL calls. Instead it counts commands,
  prints pass breakdowns, and optionally dumps the first few commands
  when COIN_DEBUG_RENDER_IR is enabled. This lets developers verify the
  SoModernRenderAction + SoDrawList plumbing before investing in real
  GPU work.
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

private:
  void logFrameStats(const SoDrawList & drawlist,
                     const SoRenderParams & params) const;

  SoRenderBackendInitParams storedparams;
  GLuint shaderProgram;
  GLuint vao;
  GLuint vertexBuffer;
  GLuint indexBuffer;
  GLint  uMvpLocation;
  GLint  uColorLocation;
  bool   vaoInitialized;
  bool   createShaders();
};

#endif // COIN_SOMODERNGLBACKEND_H
