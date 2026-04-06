#include "../../include/Inventor/PersistentSceneManager.h"
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/lists/SbList.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbColor.h>

#include "SbHash.h"

#include <Inventor/misc/SoNotRec.h>

struct PersistentSceneManagerP {
  // SoA: Transform Arrays (float[16] per matrix)
  SbList<SbMatrix> transforms;
  // Hashing Dictionary binding SoNode* to array index
  SbHash<const SoNode*, size_t> transformMap;
  
  // SoA: Material Arrays (e.g. Diffuse Colors)
  SbList<SbColor> materials;
  SbHash<const SoNode*, size_t> materialMap;
  
  // SoA: Bounding Box Arrays 
  SbList<SbVec3f> boundingBoxesMin;
  SbList<SbVec3f> boundingBoxesMax;
  SbHash<const SoNode*, size_t> shapeMap;
};

PersistentSceneManager::PersistentSceneManager(void)
  : sceneroot(NULL)
{
  this->pimpl = new PersistentSceneManagerP();
  this->rootSensor = new SoNodeSensor(PersistentSceneManager::sensorCallback, this);
  this->rootSensor->setPriority(0);
}

PersistentSceneManager::~PersistentSceneManager()
{
  if (this->sceneroot) {
    this->sceneroot->unref();
  }
  delete this->rootSensor;
  delete this->pimpl;
}

static void traverseNodeInit(SoNode * node, PersistentSceneManagerP * pimpl)
{
  if (!node) return;

  if (node->isOfType(SoTransform::getClassTypeId())) {
    SoTransform * t = (SoTransform *)node;
    size_t currentIndex = pimpl->transforms.getLength();
    pimpl->transformMap.put(node, currentIndex);

    SbMatrix mat;
    mat.setTransform(t->translation.getValue(), t->rotation.getValue(), t->scaleFactor.getValue(), t->scaleOrientation.getValue(), t->center.getValue());
    pimpl->transforms.append(mat);
  }
  else if (node->isOfType(SoMaterial::getClassTypeId())) {
    SoMaterial * m = (SoMaterial *)node;
    size_t currentIndex = pimpl->materials.getLength();
    pimpl->materialMap.put(node, currentIndex);

    if (m->diffuseColor.getNum() > 0)
      pimpl->materials.append(m->diffuseColor[0]);
    else
      pimpl->materials.append(SbColor(0.8f, 0.8f, 0.8f));
  }
  else if (node->isOfType(SoShape::getClassTypeId())) {
    size_t currentIndex = pimpl->boundingBoxesMin.getLength();
    pimpl->shapeMap.put(node, currentIndex);

    pimpl->boundingBoxesMin.append(SbVec3f(-1,-1,-1));
    pimpl->boundingBoxesMax.append(SbVec3f(1,1,1));
  }

  if (node->isOfType(SoGroup::getClassTypeId())) {
    SoGroup * group = (SoGroup *)node;
    for (int i = 0; i < group->getNumChildren(); ++i) {
      traverseNodeInit(group->getChild(i), pimpl);
    }
  }
}

void
PersistentSceneManager::setSceneGraph(SoNode * root)
{
  if (this->sceneroot == root) return;

  if (this->sceneroot) {
    this->rootSensor->detach();
    this->sceneroot->unref();
  }

  this->sceneroot = root;

  if (this->sceneroot) {
    this->sceneroot->ref();
    this->rootSensor->attach(this->sceneroot);
    traverseNodeInit(this->sceneroot, this->pimpl);
  }
}

SoNode *
PersistentSceneManager::getSceneGraph(void) const
{
  return this->sceneroot;
}

size_t PersistentSceneManager::getNumTransforms() const {
    return this->pimpl->transforms.getLength();
}

const void* PersistentSceneManager::getTransformData() const {
    if (this->pimpl->transforms.getLength() == 0) return nullptr;
    return this->pimpl->transforms.getArrayPtr();
}

size_t PersistentSceneManager::getNumMaterials() const {
    return this->pimpl->materials.getLength();
}

const void* PersistentSceneManager::getMaterialData() const {
    if (this->pimpl->materials.getLength() == 0) return nullptr;
    return this->pimpl->materials.getArrayPtr();
}

void
PersistentSceneManager::sensorCallback(void * data, SoSensor * sensor)
{
  PersistentSceneManager * thisp = (PersistentSceneManager *)data;
  SoNodeSensor * nsensor = (SoNodeSensor *)sensor;
  
  SoNode * triggerNode = nsensor->getTriggerNode();
  if (triggerNode) {
      if (triggerNode->isOfType(SoTransform::getClassTypeId())) {
          size_t idx;
          if (thisp->pimpl->transformMap.get(triggerNode, idx)) {
              SoTransform * t = (SoTransform *)triggerNode;
              SbMatrix mat;
              mat.setTransform(t->translation.getValue(), t->rotation.getValue(), t->scaleFactor.getValue(), t->scaleOrientation.getValue(), t->center.getValue());
              thisp->pimpl->transforms[idx] = mat; // update existing element
          }
      }
      else if (triggerNode->isOfType(SoMaterial::getClassTypeId())) {
          size_t idx;
          if (thisp->pimpl->materialMap.get(triggerNode, idx)) {
              SoMaterial * m = (SoMaterial *)triggerNode;
              if (m->diffuseColor.getNum() > 0)
                  thisp->pimpl->materials[idx] = m->diffuseColor[0];
          }
      }
      else if (triggerNode->isOfType(SoGroup::getClassTypeId())) {
          // Structural changes (Node Insertions/Removals) emit from the parent SoGroup.
          // For now, our robust fallback is to compact/rebuild the active SoA arrays 
          // to ensure indices correctly mirror the newly structured tree.
          thisp->pimpl->transforms.truncate(0);
          thisp->pimpl->materials.truncate(0);
          thisp->pimpl->boundingBoxesMin.truncate(0);
          thisp->pimpl->boundingBoxesMax.truncate(0);
          thisp->pimpl->transformMap.clear();
          thisp->pimpl->materialMap.clear();
          thisp->pimpl->shapeMap.clear();
          
          traverseNodeInit(thisp->sceneroot, thisp->pimpl);
      }
  }
}
