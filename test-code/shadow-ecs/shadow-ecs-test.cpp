#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/PersistentSceneManager.h>
#include <iostream>
#include <chrono>

int main(int argc, char ** argv)
{
    SoDB::init();

    SoSeparator * root = new SoSeparator;
    root->ref();

    SoTransform * transform = new SoTransform;
    root->addChild(transform);

    SoMaterial * material = new SoMaterial;
    material->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
    root->addChild(material);

    SoSphere * sphere = new SoSphere;
    root->addChild(sphere);

    std::cout << "--- LEGACY BASELINE: Native SoTransform Updates ---" << std::endl;
    auto startLegacy = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        transform->translation.setValue(i * 0.1f, 0.0f, 0.0f);
    }
    auto endLegacy = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diffLegacy = endLegacy - startLegacy;
    double msLegacy = diffLegacy.count();
    std::cout << "1,000,000 transform updates took " << msLegacy << " ms." << std::endl;

    // Initialize our new Shadow ECS manager
    PersistentSceneManager * manager = new PersistentSceneManager;
    manager->setSceneGraph(root);

    std::cout << "\n--- SHADOW ECS: Synchronous Sensor Array Updates ---" << std::endl;
    // We do 1,000,000 rapid transformations. Because rootSensor priority is 0,
    // it will call PersistentSceneManager::sensorCallback synchronously for each update,
    // performing an SbHash lookup and memory copy, updating the ECS arrays.
    
    auto startECS = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        transform->translation.setValue(i * 0.1f, 0.0f, 0.0f);
    }
    auto endECS = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diffECS = endECS - startECS;
    double msECS = diffECS.count();

    std::cout << "1,000,000 transform updates took " << msECS << " ms." << std::endl;
    std::cout << "Differential overhead of synchronous ECS replication: " << ((msECS - msLegacy) * 1000.0) / 1000000.0 << " microseconds per update." << std::endl;

    delete manager;
    root->unref();
    return 0;
}
