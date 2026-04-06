#ifndef COIN_PERSISTENTSCENEMANAGER_H
#define COIN_PERSISTENTSCENEMANAGER_H

#include <Inventor/C/errors/debugerror.h>

class SoNode;
class SoNodeSensor;
class SoSensor;

class PersistentSceneManager {
public:
  PersistentSceneManager(void);
  virtual ~PersistentSceneManager();

  void setSceneGraph(SoNode * root);
  SoNode * getSceneGraph(void) const;

  size_t getNumTransforms() const;
  const void* getTransformData() const;
  void getTransformDirtyRange(size_t& minIndex, size_t& maxIndex) const;
  void resetTransformDirtyRange();

  size_t getNumMaterials() const;
  const void* getMaterialData() const;

  size_t getNumBoundingBoxes() const;
  const void* getBoundingBoxData() const;

private:
  static void sensorCallback(void * data, SoSensor * sensor);

  SoNode * sceneroot;
  SoNodeSensor * rootSensor;
  
  struct PersistentSceneManagerP * pimpl;
};

#endif // !COIN_PERSISTENTSCENEMANAGER_H
