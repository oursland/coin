#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanStateManager.h>
#include <Inventor/PersistentSceneManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/SoDB.h>
#include <iostream>
#include <chrono>

int main(int argc, char** argv) {
    SoDB::init();
    try {
        ModernVulkanBackend backend;
        std::cout << "Initializing Vulkan/MoltenVK Backend..." << std::endl;
        backend.init();
        std::cout << "Vulkan Instance and Device successfully created!" << std::endl;

        VulkanStateManager stateManager(&backend);    
        PersistentSceneManager sceneManager;

        // Build a massive scene graph
        SoSeparator* root = new SoSeparator;
        root->ref();
        const int NUM_NODES = 100000;
        SoTransform* targetTransform = nullptr;

        for (int i = 0; i < NUM_NODES; ++i) {
            SoTransform* t = new SoTransform;
            t->translation.setValue(i, 0, 0);
            if (i == NUM_NODES / 2) {
                targetTransform = t; // we'll modify this one later
            }
            root->addChild(t);
        }

        std::cout << "Built CPU Scene Graph. Flattening into ECS..." << std::endl;
        sceneManager.setSceneGraph(root);

        std::cout << "Uploading Naive Buffer (" << NUM_NODES << " elements)..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        stateManager.upload(&sceneManager);
        auto end = std::chrono::steady_clock::now();
        
        std::chrono::duration<double, std::milli> baseUploadMs = end - start;
        std::cout << "Naive Vulkan Upload Time: " << baseUploadMs.count() << " ms" << std::endl;

        // Trigger an incremental update
        std::cout << "Triggering targeted ECS update..." << std::endl;
        targetTransform->translation.setValue(0, 50, 0);

        start = std::chrono::steady_clock::now();
        stateManager.upload(&sceneManager);
        end = std::chrono::steady_clock::now();

        std::chrono::duration<double, std::milli> incrementalUploadMs = end - start;
        std::cout << "Post-Update Upload Time (Currently Naive): " << incrementalUploadMs.count() << " ms" << std::endl;

        root->unref();

    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
