// include/Inventor/actions/SoModernRenderAction.h

#ifndef COIN_SOMODERNRENDERACTION_H
#define COIN_SOMODERNRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/lists/SbList.h>

#include <Inventor/rendering/SoModernIR.h>

#include <cstddef>

class SoPath;
class SoPathList;
class SoCamera;
class SoPrimitiveVertex;
class SoShape;
class SoModernRenderActionP;

/*!
  \class SoModernRenderAction SoModernRenderAction.h
  \brief New-style render action that builds an API-agnostic draw list (IR).
*/
class COIN_DLL_API SoModernRenderAction : public SoAction {
  typedef SoAction inherited;
  SO_ACTION_HEADER(SoModernRenderAction);

public:
  class PrimitiveCollector {
  public:
    virtual ~PrimitiveCollector() {}
    virtual void onTriangle(const SoPrimitiveVertex * v1,
                            const SoPrimitiveVertex * v2,
                            const SoPrimitiveVertex * v3) = 0;
    virtual void onLine(const SoPrimitiveVertex * v1,
                        const SoPrimitiveVertex * v2) = 0;
    virtual void onPoint(const SoPrimitiveVertex * v) = 0;
  };

  static void initClass(void);

  SoModernRenderAction(const SbViewportRegion & vp);
  virtual ~SoModernRenderAction();

  void setViewportRegion(const SbViewportRegion & vp);
  const SbViewportRegion & getViewportRegion(void) const { return this->vpRegion; }

  void setCamera(SoCamera * camera);
  SoCamera * getCamera(void) const { return this->camera; }

  // Standard entry points, mirroring SoGLRenderAction
  virtual void apply(SoNode * root);
  virtual void apply(SoPath * path);
  virtual void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE);

  /// Traverse an additional scene root without clearing the draw list.
  /// Commands from this traversal are appended to the existing draw list.
  void traverseAdditionalRoot(SoNode * root);

  // Access to generated IR for the current frame
  const SoDrawList & getDrawList(void) const { return this->drawlist; }
  SoDrawList & getMutableDrawList() { return this->drawlist; }

  /// Store the scene graph path for a command index (called by shapes during render()).
  /// The path is copied and ref'd; freed on next clear.
  void storeCommandPath(int commandIndex, const SoPath * path);

  /// Retrieve stored path for a command index. Returns NULL if not stored.
  SoPath * getCommandPath(int commandIndex) const;
  void * allocateGeometryStorage(size_t bytes, size_t alignment = alignof(float));

  void pushPrimitiveCollector(PrimitiveCollector * collector);
  void popPrimitiveCollector(PrimitiveCollector * collector);
  PrimitiveCollector * getActivePrimitiveCollector(void) const;

  /// Flag that the scene contains camera-dependent shapes (e.g. SoDatumLabel)
  /// that need re-traversal when the camera changes.
  void setHasCameraDependentShapes(bool v) { cameraDependentShapes = v; }
  bool hasCameraDependentShapes() const { return cameraDependentShapes; }

  // (later) hooks for backend:
  // uint32_t getCacheContext(void) const;
  // void setCacheContext(uint32_t ctx);

protected:
  virtual void beginTraversal(SoNode * node);
  virtual void endTraversal(SoNode * node);

  // static dispatchers installed in SoAction's method table
  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);
  static void renderShaderProgram(SoAction * action, SoNode * node);

private:
  void resetFrameResources();

  SbViewportRegion vpRegion;
  SoCamera *       camera;

  SoDrawList       drawlist;
  SoModernRenderActionP * pimpl;
  bool             cameraDependentShapes = false;
};

#endif // COIN_SOMODERNRENDERACTION_H
