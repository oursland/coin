// src/rendering/SoRenderBackend.h

#ifndef COIN_SORENDERBACKEND_H
#define COIN_SORENDERBACKEND_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/misc/SoState.h>

#include <cstdint>

#include "rendering/SoModernIR.h"

class SoDrawList;

typedef void (*SoRenderBackendLogFn)(const char * message, void * userdata);

/*!
  \struct SoRenderTargetInfo
  \brief Describes the framebuffer or surface the backend renders into.
  
  Each backend instance renders to exactly one target at a time
  (typically the viewer window’s default framebuffer or an offscreen
  texture). SoRenderManager defines the render target up-front and
  notifies the backend about size/format changes through this struct.
  The fields intentionally mirror low-level properties that GPU APIs
  expose so future backends can map them directly to FBOs, swapchains,
  etc.
*/
struct SoRenderTargetInfo {
  /*!
    \brief Pixel width/height of the target (in framebuffer coordinates).
  */
  SbVec2s  size;
  /*!
    \brief Number of MSAA samples: 1 = no MSAA, >1 enables multisampling.
    
    Backends can use this to configure renderbuffers or attachments with
    the correct sample count, or to pick equivalent default-sample flags
    in non-GL APIs.
  */
  int      samples;
  /*!
    \brief Placeholder for the color format (GL enum, VkFormat, etc.).
    
    At this stage we keep it as an integer to avoid committing to a
    specific API’s enum space. A value of 0 means “default format”.
  */
  int      colorFormat;
  /*!
    \brief Placeholder for the depth/stencil format.
    
    Similar to colorFormat, the exact meaning is backend-defined. Set to
    0 when the default depth buffer should be used.
  */
  int      depthFormat;
  /*!
    \brief Identifier for the render target (0 = default framebuffer).
    
    When SoRenderManager later manages multiple offscreen buffers, this
    allows a backend to distinguish which FBO/texture it should bind.
  */
  uint32_t targetId;
};

/*!
  \struct SoRenderParams
  \brief POD block describing per-frame and per-view state passed to render().
  
  The struct intentionally contains no virtual methods or dynamic
  allocations so it can be copied across threads or stored in logging
  buffers without additional serialization work. SoRenderManager fills it
  immediately before calling SoRenderBackend::render().
*/
struct SoRenderParams {
  // Per-frame:
  /*!
    \brief Monotonic frame counter maintained by SoRenderManager.
    
    Useful for debugging or for backends that want to batch work per
    frame (e.g. resetting statistics once per call).
  */
  int                frameIndex;
  /*!
    \brief Wall-clock time in seconds when the frame started.
    
    Usually sourced from SbTime::getTimeOfDay(). Backends can use this to
    drive animations or GPU timers without consulting global state.
  */
  double             time;

  // Per-view:
  /*!
    \brief Viewport rectangle describing where to render inside the target.
  */
  SbViewportRegion   viewport;
  /*!
    \brief View matrix transforming world space into camera space.
  */
  SbMatrix           viewMatrix;
  /*!
    \brief Projection matrix used for clip-space transform.
  */
  SbMatrix           projMatrix;
  /*!
    \brief Pre-multiplied view * projection matrix for convenience.
  */
  SbMatrix           viewProjMatrix;

  /*!
    \brief RGBA clear color applied before drawing when requested.
  */
  SbColor4f          clearColor;
  /*!
    \brief Depth value used when clearing the depth buffer.
    
    Defaults to 1.0 (far plane). Backends can ignore it if they do not
    clear depth for the current frame.
  */
  float              clearDepth;

  /*!
    \brief Pointer to the SoState instance used when building the draw list.
  */
  SoState *          state;

  /*!
    \brief GL context id associated with this render call (from SoGLCacheContextElement).
  */
  uint32_t           contextId;
  /*!
    \brief Bitfield for view-specific options.

    Bit 0: clear window requested
    Bit 1: interactive mode (camera orbiting/panning — skip ID buffer)
  */
  uint32_t           flags;
  /*!
    \brief Number of commands at the start of the draw list that are
    background (e.g. gradient). The backend renders these first, then
    clears the depth buffer before rendering the main scene.
  */
  int                bgCommandCount;
};

/*!
  \struct SoRenderBackendInitParams
  \brief Initialization arguments supplied when bringing a backend up.
  
  SoRenderManager forwards these parameters when switching renderers so
  each backend can create API resources with the correct context and
  logging hooks.
*/
struct SoRenderBackendInitParams {
  /*!
    \brief Initial render target description (size, formats, ids).
  */
  SoRenderTargetInfo    targetInfo;
  /*!
    \brief Backend-specific handle to a shared GPU context.
    
    This is intentionally a void pointer; WGL/GLX/EGL/Vulkan handles can
    be passed through without pulling in platform headers. The backend is
    responsible for casting it to the expected type.
  */
  void *                sharedContext;
  /*!
    \brief Caller-supplied pointer forwarded to log/error callbacks.
  */
  void *                userData;
  /*!
    \brief Optional logging hook invoked by emitLog().
    
    When null, SoRenderBackend falls back to SoDebugError::postInfo().
  */
  SoRenderBackendLogFn  logCallback;
  /*!
    \brief Optional error-reporting hook invoked by emitError().
    
    Allows applications to intercept backend failures and route them to
    custom telemetry. When null, SoDebugError::post() is used instead.
  */
  SoRenderBackendLogFn  errorCallback;
};

/*!
  \class SoRenderBackend
  \brief Abstract GPU backend interface consumed by SoRenderManager.
  
  The base class defines the lifecycle contract (initialize / render /
  shutdown) for any GPU implementation that wants to consume the modern
  draw list (SoDrawList). It also provides helper utilities for logging,
  initialization tracking and debug-time validation so each backend does
  not have to reimplement the same boilerplate.
*/
class SoRenderBackend {
public:
  SoRenderBackend();
  virtual ~SoRenderBackend();

  virtual const char * getName() const = 0;

  virtual SbBool initialize(const SoRenderBackendInitParams & params) = 0;
  virtual void shutdown() = 0;
  virtual SbBool render(const SoDrawList & drawlist,
                        const SoRenderParams & params) = 0;
  virtual void resizeTarget(const SoRenderTargetInfo & info);

  /*!
    \brief GPU pick at pixel coordinates using the ID buffer.

    Returns the pick LUT index (1-based) of the nearest visible element,
    or 0 for background/no hit. The draw list's resolvePickIdentity()
    can then map this to an application-level name.

    Backends that support GPU picking implement this; others return 0.
    \param x  Pixel X (left = 0)
    \param y  Pixel Y (bottom = 0, OpenGL convention)
    \param pickRadius  Half-size of the pick region (default 5 = 11x11)
  */
  virtual uint32_t pick(int x, int y, int pickRadius = 5) const;

  /// Set the line width used for edge picking in the ID buffer (default 7.0).
  /// Wider lines make edges easier to pick.
  virtual void setPickLineWidth(float width);
  /// Set the point size used for vertex picking in the ID buffer (default 7.0).
  virtual void setPickPointSize(float size);
  virtual float getPickLineWidth() const;
  virtual float getPickPointSize() const;

  SbBool isInitialized() const;

protected:
  void setInitialized(const SbBool state);
  void setInitParams(const SoRenderBackendInitParams & params);
  const SoRenderBackendInitParams & getInitParams() const;

  void emitLog(const char * message) const;
  void emitError(const char * message) const;

  void debugValidateDrawList(const SoDrawList & drawlist) const;

private:
  SbBool                         initialized;
  SoRenderBackendInitParams      initparams;
};

#endif // COIN_SORENDERBACKEND_H
