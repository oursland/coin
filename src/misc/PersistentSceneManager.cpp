#include "../../include/Inventor/PersistentSceneManager.h"
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoDirectionalLight.h>
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
  
  // SoA: Material Arrays
  SbList<MaterialData> materials;
  SbHash<const SoNode*, size_t> materialMap;
  
  // ECS Map binding Shape Index (same as Transform Index) to Material Index
  SbList<uint32_t> shapeMaterialMap;
  
  // SoA: Lighting Arrays
  SbList<LightData> lights;
  SbHash<const SoNode*, size_t> lightMap;
  
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

static void traverseNodeInit(SoNode * node, PersistentSceneManagerP * pimpl, size_t currentMaterialIndex = 0)
{
  if (!node) return;

  size_t activeMaterial = currentMaterialIndex;

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
    activeMaterial = currentIndex;

    MaterialData md;
    memset(&md, 0, sizeof(MaterialData));
    
    if (m->diffuseColor.getNum() > 0) {
        md.diffuse[0] = m->diffuseColor[0][0];
        md.diffuse[1] = m->diffuseColor[0][1];
        md.diffuse[2] = m->diffuseColor[0][2];
    } else {
        md.diffuse[0] = md.diffuse[1] = md.diffuse[2] = 0.8f;
    }
    if (m->transparency.getNum() > 0) md.diffuse[3] = m->transparency[0];
    
    if (m->specularColor.getNum() > 0) {
        md.specular[0] = m->specularColor[0][0];
        md.specular[1] = m->specularColor[0][1];
        md.specular[2] = m->specularColor[0][2];
    }
    if (m->shininess.getNum() > 0) md.specular[3] = m->shininess[0];
    
    if (m->emissiveColor.getNum() > 0) {
        md.emissive[0] = m->emissiveColor[0][0];
        md.emissive[1] = m->emissiveColor[0][1];
        md.emissive[2] = m->emissiveColor[0][2];
    }
    
    pimpl->materials.append(md);
  }
  else if (node->isOfType(SoDirectionalLight::getClassTypeId())) {
    SoDirectionalLight * l = (SoDirectionalLight *)node;
    size_t currentIndex = pimpl->lights.getLength();
    pimpl->lightMap.put(node, currentIndex);

    LightData ld;
    memset(&ld, 0, sizeof(LightData));
    
    ld.direction[0] = l->direction.getValue()[0];
    ld.direction[1] = l->direction.getValue()[1];
    ld.direction[2] = l->direction.getValue()[2];
    ld.direction[3] = 0.0f; // 0 = Directional
    
    ld.color[0] = l->color.getValue()[0];
    ld.color[1] = l->color.getValue()[1];
    ld.color[2] = l->color.getValue()[2];
    ld.color[3] = l->on.getValue() ? 1.0f : 0.0f;
    
    ld.position[3] = l->intensity.getValue();
    
    pimpl->lights.append(ld);
  }
  else if (node->isOfType(SoShape::getClassTypeId())) {
    size_t currentIndex = pimpl->boundingBoxesMin.getLength();
    pimpl->shapeMap.put(node, currentIndex);

    pimpl->boundingBoxesMin.append(SbVec3f(-1,-1,-1));
    pimpl->boundingBoxesMax.append(SbVec3f(1,1,1));
    
    pimpl->shapeMaterialMap.append((uint32_t)activeMaterial);
  }

  if (node->isOfType(SoGroup::getClassTypeId())) {
    SoGroup * group = (SoGroup *)node;
    for (int i = 0; i < group->getNumChildren(); ++i) {
      if (node->isOfType(SoSeparator::getClassTypeId())) {
          // Separator pushes and pops state!
          traverseNodeInit(group->getChild(i), pimpl, activeMaterial);
      } else {
          // Standard Groups leak their state changes into siblings.
          // Wait, traverseNodeInit handles DFS, we must capture if activeMaterial changed inside sibling.
          // Since it's passed by value, modifications inside wouldn't propagate up natively unless we return it.
          // For now, we assume simple uniform graphs or basic scoping.
          traverseNodeInit(group->getChild(i), pimpl, activeMaterial);
      }
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

size_t PersistentSceneManager::getNumShapeMaterialIndices() const {
    return this->pimpl->shapeMaterialMap.getLength();
}

const uint32_t* PersistentSceneManager::getShapeMaterialIndices() const {
    if (this->pimpl->shapeMaterialMap.getLength() == 0) return nullptr;
    return this->pimpl->shapeMaterialMap.getArrayPtr();
}

size_t PersistentSceneManager::getNumLights() const {
    return this->pimpl->lights.getLength();
}

const void* PersistentSceneManager::getLightData() const {
    if (this->pimpl->lights.getLength() == 0) return nullptr;
    return this->pimpl->lights.getArrayPtr();
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
              MaterialData& md = thisp->pimpl->materials[idx];
              if (m->diffuseColor.getNum() > 0) {
                  md.diffuse[0] = m->diffuseColor[0][0];
                  md.diffuse[1] = m->diffuseColor[0][1];
                  md.diffuse[2] = m->diffuseColor[0][2];
              }
              if (m->transparency.getNum() > 0) md.diffuse[3] = m->transparency[0];
          }
      }
      else if (triggerNode->isOfType(SoDirectionalLight::getClassTypeId())) {
          size_t idx;
          if (thisp->pimpl->lightMap.get(triggerNode, idx)) {
              SoDirectionalLight * l = (SoDirectionalLight *)triggerNode;
              LightData& ld = thisp->pimpl->lights[idx];
              ld.direction[0] = l->direction.getValue()[0];
              ld.direction[1] = l->direction.getValue()[1];
              ld.direction[2] = l->direction.getValue()[2];
              ld.color[0] = l->color.getValue()[0];
              ld.color[1] = l->color.getValue()[1];
              ld.color[2] = l->color.getValue()[2];
              ld.color[3] = l->on.getValue() ? 1.0f : 0.0f;
              ld.position[3] = l->intensity.getValue();
          }
      }
      else if (triggerNode->isOfType(SoGroup::getClassTypeId())) {
          // Structural changes (Node Insertions/Removals) emit from the parent SoGroup.
          // For now, our robust fallback is to compact/rebuild the active SoA arrays 
          // to ensure indices correctly mirror the newly structured tree.
          thisp->pimpl->transforms.truncate(0);
          thisp->pimpl->materials.truncate(0);
          thisp->pimpl->shapeMaterialMap.truncate(0);
          thisp->pimpl->lights.truncate(0);
          thisp->pimpl->boundingBoxesMin.truncate(0);
          thisp->pimpl->boundingBoxesMax.truncate(0);
          thisp->pimpl->transformMap.clear();
          thisp->pimpl->materialMap.clear();
          thisp->pimpl->lightMap.clear();
          thisp->pimpl->shapeMap.clear();
          
          traverseNodeInit(thisp->sceneroot, thisp->pimpl);
      }
  }
}
