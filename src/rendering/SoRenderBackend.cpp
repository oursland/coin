// src/rendering/SoRenderBackend.cpp

#include "rendering/SoRenderBackend.h"

#include <Inventor/errors/SoDebugError.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/C/basic.h>

#include <cassert>
#include <cstring>

SoRenderBackend::SoRenderBackend()
  : initialized(FALSE)
{
  std::memset(&this->initparams, 0, sizeof(SoRenderBackendInitParams));
}

SoRenderBackend::~SoRenderBackend()
{
#ifdef COIN_DEBUG
  assert(!this->initialized &&
         "SoRenderBackend destroyed without calling shutdown()");
#endif // COIN_DEBUG
}

void
SoRenderBackend::resizeTarget(const SoRenderTargetInfo & info)
{
  (void) info;
}

uint32_t
SoRenderBackend::pick(int x, int y, int pickRadius) const
{
  (void) x; (void) y; (void) pickRadius;
  return 0; // Default: no pick support
}

void
SoRenderBackend::setPickLineWidth(float /*width*/) {}

void
SoRenderBackend::setPickPointSize(float /*size*/) {}

float
SoRenderBackend::getPickLineWidth() const { return 7.0f; }

float
SoRenderBackend::getPickPointSize() const { return 7.0f; }

SbBool
SoRenderBackend::isInitialized() const
{
  return this->initialized;
}

void
SoRenderBackend::setInitialized(const SbBool state)
{
  this->initialized = state;
}

void
SoRenderBackend::setInitParams(const SoRenderBackendInitParams & params)
{
  this->initparams = params;
}

const SoRenderBackendInitParams &
SoRenderBackend::getInitParams() const
{
  return this->initparams;
}

void
SoRenderBackend::emitLog(const char * message) const
{
  if (this->initparams.logCallback) {
    this->initparams.logCallback(message, this->initparams.userData);
    return;
  }
  SoDebugError::postInfo("SoRenderBackend", "%s", message);
}

void
SoRenderBackend::emitError(const char * message) const
{
  if (this->initparams.errorCallback) {
    this->initparams.errorCallback(message, this->initparams.userData);
    return;
  }
  SoDebugError::post("SoRenderBackend", "%s", message);
}

void
SoRenderBackend::debugValidateDrawList(const SoDrawList & drawlist) const
{
#ifdef COIN_DEBUG
  const int num = drawlist.getNumCommands();
  for (int i = 0; i < num; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.geometry.topology >= SO_TOPOLOGY_COUNT) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has invalid topology (%d)",
                         i, static_cast<int>(cmd.geometry.topology));
    }
    if (cmd.geometry.vertexCount == 0 &&
        cmd.geometry.indexCount == 0) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has no vertices or indices", i);
    }
    if (cmd.geometry.vertexCount > 0 && cmd.geometry.positions == nullptr) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d missing position buffer", i);
    }
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices == nullptr) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d missing index buffer", i);
    }
    if (cmd.material.opacity < 0.0f || cmd.material.opacity > 1.0f) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has invalid opacity (%f)",
                         i, cmd.material.opacity);
    }
    if (cmd.pass >= SO_RENDERPASS_COUNT) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d references unknown render pass (%d)",
                         i, static_cast<int>(cmd.pass));
    }
  }
#else
  (void) drawlist;
#endif // COIN_DEBUG
}
