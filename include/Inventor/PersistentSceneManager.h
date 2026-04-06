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

private:
  static void sensorCallback(void * data, SoSensor * sensor);

  SoNode * sceneroot;
  SoNodeSensor * rootSensor;
  
  struct PersistentSceneManagerP * pimpl;
};

#endif // !COIN_PERSISTENTSCENEMANAGER_H
