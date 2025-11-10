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
  SO_ACTION_ADD_METHOD_INTERNAL(SoShaderProgram, SoModernRenderAction::renderShaderProgram);

  SO_ENABLE(SoModernRenderAction, SoViewportRegionElement);
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
  node->doAction(a);
}

void
SoModernRenderAction::renderShape(SoAction * a, SoNode * node)
{
  SoModernRenderAction * action = static_cast<SoModernRenderAction *>(a);
  SoShape * shape = static_cast<SoShape *>(node);
  shape->render(action);
}

void
SoModernRenderAction::renderShaderProgram(SoAction * a, SoNode * node)
{
  SoModernRenderAction * action = static_cast<SoModernRenderAction *>(a);
  SoShaderProgram * shader = static_cast<SoShaderProgram *>(node);
  SoState * state = action->getState();
  if (!state || !shader) return;

  shader->render(action);
}

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
