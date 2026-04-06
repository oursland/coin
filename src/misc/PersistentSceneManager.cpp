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
  size_t transformDirtyMin = (size_t)-1;
  size_t transformDirtyMax = 0;
  
  // SoA: Material Arrays (e.g. Diffuse Colors)
  SbList<SbColor> materials;
  SbHash<const SoNode*, size_t> materialMap;
  
  // SoA: Bounding Box Arrays 
  SbList<SbVec3f> boundingBoxesMin;
  SbList<SbVec3f> boundingBoxesMax;
  SbHash<const SoNode*, size_t> shapeMap;

  mutable SbList<float> boundingBoxesAoS;
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

    if (currentIndex < pimpl->transformDirtyMin) pimpl->transformDirtyMin = currentIndex;
    if (currentIndex > pimpl->transformDirtyMax) pimpl->transformDirtyMax = currentIndex;

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

void PersistentSceneManager::getTransformDirtyRange(size_t& minIndex, size_t& maxIndex) const {
    minIndex = this->pimpl->transformDirtyMin;
    maxIndex = this->pimpl->transformDirtyMax;
}

void PersistentSceneManager::resetTransformDirtyRange() {
    this->pimpl->transformDirtyMin = (size_t)-1;
    this->pimpl->transformDirtyMax = 0;
}

size_t PersistentSceneManager::getNumMaterials() const {
    return this->pimpl->materials.getLength();
}

const void* PersistentSceneManager::getMaterialData() const {
    if (this->pimpl->materials.getLength() == 0) return nullptr;
    return this->pimpl->materials.getArrayPtr();
}

size_t PersistentSceneManager::getNumBoundingBoxes() const {
    return this->pimpl->boundingBoxesMin.getLength();
}

const void* PersistentSceneManager::getBoundingBoxData() const {
    size_t num = pimpl->boundingBoxesMin.getLength();
    if (num == 0) return nullptr;

    pimpl->boundingBoxesAoS.truncate(0);
    // Expand to fit
    for (size_t i = 0; i < num; ++i) {
        // Min bounds
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMin[i][0]);
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMin[i][1]);
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMin[i][2]);
        pimpl->boundingBoxesAoS.append(0.0f); // pad
        // Max bounds
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMax[i][0]);
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMax[i][1]);
        pimpl->boundingBoxesAoS.append(pimpl->boundingBoxesMax[i][2]);
        pimpl->boundingBoxesAoS.append(0.0f); // pad
    }
    return pimpl->boundingBoxesAoS.getArrayPtr();
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
              
              if (idx < thisp->pimpl->transformDirtyMin) thisp->pimpl->transformDirtyMin = idx;
              if (idx > thisp->pimpl->transformDirtyMax) thisp->pimpl->transformDirtyMax = idx;
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
