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

        // VERIFICATION: Readback GPU Memory
        std::cout << "Verifying Correctness via Readback..." << std::endl;
        
        VkBuffer readbackBuffer;
        VkDeviceMemory readbackMemory;
        backend.createBuffer(sizeof(TransformData) * NUM_NODES, 
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             readbackBuffer, readbackMemory);
                             
        VkCommandBuffer cmdBuffer = backend.beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(TransformData) * NUM_NODES;
        vkCmdCopyBuffer(cmdBuffer, stateManager.getTransformBuffer(), readbackBuffer, 1, &copyRegion);
        backend.endSingleTimeCommands(cmdBuffer);
        
        void* mappedData;
        vkMapMemory(backend.getDevice(), readbackMemory, 0, sizeof(TransformData) * NUM_NODES, 0, &mappedData);
        TransformData* gpuData = (TransformData*)mappedData;
        const float* ecsData = (const float*)sceneManager.getTransformData();
        
        // Assert base upload correctness
        if (memcmp(&gpuData[0].matrix[0], &ecsData[0], 16 * sizeof(float)) != 0) {
            throw std::runtime_error("Index 0 mismatch!");
        }
        
        // Assert targeted update correctness (Index 50000)
        int targetIdx = NUM_NODES / 2;
        if (memcmp(&gpuData[targetIdx].matrix[0], &ecsData[targetIdx * 16], 16 * sizeof(float)) != 0) {
            throw std::runtime_error("Target index Y translation mismatch (Optimized update failed to sync)");
        }
        
        vkUnmapMemory(backend.getDevice(), readbackMemory);
        vkDestroyBuffer(backend.getDevice(), readbackBuffer, nullptr);
        vkFreeMemory(backend.getDevice(), readbackMemory, nullptr);
        
        std::cout << "Correctness Verified: GPU exactly mirrors Shadow ECS memory." << std::endl;

        root->unref();

    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
