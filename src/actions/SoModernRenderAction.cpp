// src/actions/SoModernRenderAction.cpp

#include <Inventor/actions/SoModernRenderAction.h>

#include <Inventor/C/tidbits.h>
#include <Inventor/SbBasic.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoGLShaderProgramElement.h>
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoCreaseAngleElement.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoProfileElement.h>
#include <Inventor/elements/SoProfileCoordinateElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoSwitchElement.h>
#include <Inventor/elements/SoUnitsElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoFocalDistanceElement.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoGLViewportRegionElement.h>
#include <Inventor/elements/SoGLUpdateAreaElement.h>
#include <Inventor/elements/SoGLRenderPassElement.h>
#include <Inventor/elements/SoGLLightIdElement.h>
#include <Inventor/elements/SoDecimationPercentageElement.h>
#include <Inventor/elements/SoDecimationTypeElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoWindowElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoShape.h>

#include "actions/SoSubActionP.h"

#include <cassert>

SO_ACTION_SOURCE(SoModernRenderAction);

class SoModernRenderActionP {
public:
  SoModernRenderActionP() = default;

  SoIRBuffer geometryPool;
  SbList<SoModernRenderAction::PrimitiveCollector *> collectorStack;
};

#define PRIVATE(obj) (obj->pimpl)

void
SoModernRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoModernRenderAction, SoAction);

  if (SoGLShaderProgramElement::getClassTypeId() == SoType::badType()) {
    SoGLShaderProgramElement::initClass();
  }
  if (SoCacheElement::getClassTypeId() == SoType::badType()) {
    SoCacheElement::initClass();
  }

  SO_ACTION_ADD_METHOD_INTERNAL(SoNode, SoModernRenderAction::renderNode);
  SO_ACTION_ADD_METHOD_INTERNAL(SoShape, SoModernRenderAction::renderShape);

  SO_ENABLE(SoModernRenderAction, SoViewportRegionElement);
  SO_ENABLE(SoModernRenderAction, SoViewVolumeElement);
  SO_ENABLE(SoModernRenderAction, SoMultiTextureImageElement);
  SO_ENABLE(SoModernRenderAction, SoMultiTextureMatrixElement);
  SO_ENABLE(SoModernRenderAction, SoLinePatternElement);
  SO_ENABLE(SoModernRenderAction, SoOverrideElement);
  SO_ENABLE(SoModernRenderAction, SoModelMatrixElement);
  SO_ENABLE(SoModernRenderAction, SoLazyElement);
  SO_ENABLE(SoModernRenderAction, SoDepthBufferElement);
  SO_ENABLE(SoModernRenderAction, SoDrawStyleElement);
  SO_ENABLE(SoModernRenderAction, SoLineWidthElement);
  SO_ENABLE(SoModernRenderAction, SoPolygonOffsetElement);
  SO_ENABLE(SoModernRenderAction, SoShapeStyleElement);
  SO_ENABLE(SoModernRenderAction, SoLightModelElement);
  SO_ENABLE(SoModernRenderAction, SoMaterialBindingElement);
  SO_ENABLE(SoModernRenderAction, SoNormalBindingElement);
  SO_ENABLE(SoModernRenderAction, SoGLShaderProgramElement);
  SO_ENABLE(SoModernRenderAction, SoCacheElement);
  SO_ENABLE(SoModernRenderAction, SoBumpMapCoordinateElement);
  SO_ENABLE(SoModernRenderAction, SoGLCacheContextElement);
  SO_ENABLE(SoModernRenderAction, SoMultiTextureEnabledElement);

  // Elements needed by generatePrimitives() fallback in SoShape::render()
  SO_ENABLE(SoModernRenderAction, SoCoordinateElement);
  SO_ENABLE(SoModernRenderAction, SoNormalElement);
  SO_ENABLE(SoModernRenderAction, SoCreaseAngleElement);
  SO_ENABLE(SoModernRenderAction, SoComplexityElement);
  SO_ENABLE(SoModernRenderAction, SoComplexityTypeElement);
  SO_ENABLE(SoModernRenderAction, SoMultiTextureCoordinateElement);
  SO_ENABLE(SoModernRenderAction, SoProfileElement);
  SO_ENABLE(SoModernRenderAction, SoProfileCoordinateElement);
  SO_ENABLE(SoModernRenderAction, SoTextureQualityElement);
  SO_ENABLE(SoModernRenderAction, SoSwitchElement);
  SO_ENABLE(SoModernRenderAction, SoUnitsElement);

  // Scene state elements needed by standard nodes during traversal
  SO_ENABLE(SoModernRenderAction, SoShapeHintsElement);
  SO_ENABLE(SoModernRenderAction, SoFocalDistanceElement);
  SO_ENABLE(SoModernRenderAction, SoFontNameElement);
  SO_ENABLE(SoModernRenderAction, SoFontSizeElement);
  SO_ENABLE(SoModernRenderAction, SoPointSizeElement);
  SO_ENABLE(SoModernRenderAction, SoGLViewportRegionElement);
  SO_ENABLE(SoModernRenderAction, SoGLUpdateAreaElement);
  SO_ENABLE(SoModernRenderAction, SoGLRenderPassElement);
  SO_ENABLE(SoModernRenderAction, SoGLLightIdElement);
  SO_ENABLE(SoModernRenderAction, SoDecimationPercentageElement);
  SO_ENABLE(SoModernRenderAction, SoDecimationTypeElement);
  SO_ENABLE(SoModernRenderAction, SoTextureOverrideElement);
  SO_ENABLE(SoModernRenderAction, SoWindowElement);
}

SoModernRenderAction::SoModernRenderAction(const SbViewportRegion & vp)
  : SoAction(), vpRegion(vp), camera(NULL), pimpl(new SoModernRenderActionP)
{
  SO_ACTION_CONSTRUCTOR(SoModernRenderAction);
}

SoModernRenderAction::~SoModernRenderAction()
{
  delete PRIVATE(this);
  PRIVATE(this) = NULL;
}

void
SoModernRenderAction::setViewportRegion(const SbViewportRegion & vp)
{
  this->vpRegion = vp;
}

void
SoModernRenderAction::setCamera(SoCamera * cam)
{
  this->camera = cam;
}

void
SoModernRenderAction::apply(SoNode * root)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(root);
}

void
SoModernRenderAction::storeCommandPath(int commandIndex, const SoPath * path)
{
  (void)commandIndex;
  (void)path;
}

SoPath *
SoModernRenderAction::getCommandPath(int commandIndex) const
{
  (void)commandIndex;
  return nullptr;
}

void
SoModernRenderAction::apply(SoPath * path)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(path);
}

void
SoModernRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(pathlist, obeysrules);
}

void
SoModernRenderAction::traverseAdditionalRoot(SoNode * root)
{
  // Don't clear draw list or reset frame resources — append to existing
  if (!root) return;
  this->state->push();
  SoViewportRegionElement::set(this->state, this->vpRegion);
  this->traverse(root);
  this->state->pop();
}

void
SoModernRenderAction::beginTraversal(SoNode * node)
{
  SoViewportRegionElement::set(this->state, this->vpRegion);
  inherited::beginTraversal(node);
}

void
SoModernRenderAction::endTraversal(SoNode * node)
{
  inherited::endTraversal(node);
}

void
SoModernRenderAction::renderNode(SoAction * a, SoNode * node)
{
  // Special case shader programs so they can populate SoGLShaderProgramElement
  if (node->isOfType(SoShaderProgram::getClassTypeId())) {
    SoModernRenderAction * action = static_cast<SoModernRenderAction *>(a);
    static_cast<SoShaderProgram *>(node)->render(action);
    return;
  }
  node->doAction(a);
}

void
SoModernRenderAction::renderShape(SoAction * a, SoNode * node)
{
  SoModernRenderAction * action = static_cast<SoModernRenderAction *>(a);
  SoShape * shape = static_cast<SoShape *>(node);
  shape->render(action);
}

// Shader programs are handled in renderNode() without explicit registration.

void
SoModernRenderAction::pushPrimitiveCollector(PrimitiveCollector * collector)
{
  assert(collector != NULL);
  PRIVATE(this)->collectorStack.append(collector);
}

void
SoModernRenderAction::popPrimitiveCollector(PrimitiveCollector * collector)
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  assert(count > 0);
  assert(PRIVATE(this)->collectorStack[count - 1] == collector);
  PRIVATE(this)->collectorStack.remove(count - 1);
}

SoModernRenderAction::PrimitiveCollector *
SoModernRenderAction::getActivePrimitiveCollector(void) const
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  if (count == 0) return NULL;
  return PRIVATE(this)->collectorStack[count - 1];
}

void *
SoModernRenderAction::allocateGeometryStorage(size_t bytes, size_t alignment)
{
  return PRIVATE(this)->geometryPool.allocate(bytes, alignment);
}

void
SoModernRenderAction::resetFrameResources()
{
  PRIVATE(this)->geometryPool.clear();
  PRIVATE(this)->collectorStack.truncate(0);
}
