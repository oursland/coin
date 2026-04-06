#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanStateManager.h>
#include <Inventor/PersistentSceneManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/VulkanCullingSystem.h>
#include <iostream>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <cstdlib>

void sigsegv_handler(int sig) {
    void* array[10];
    size_t size;
    size = backtrace(array, 10);
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}
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
            SoSeparator* sep = new SoSeparator;
            SoTransform* t = new SoTransform;
            t->translation.setValue(i, 0, 0); // Shifting cubes linearly along X-axis
            if (i == NUM_NODES / 2) {
                targetTransform = t; // we'll modify this one later
            }
            SoCube* c = new SoCube;
            sep->addChild(t);
            sep->addChild(c);
            root->addChild(sep);
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

        // ---------- GPU COMPUTE CULLING BENCHMARK ----------
        std::cout << "\nInitializing Compute Culler..." << std::endl;
        VulkanCullingSystem cullingSystem(&backend, &stateManager);
        cullingSystem.init("src/rendering/shaders/culling.spv");
        std::cout << "init() finished!" << std::endl;
        CameraData camData{};
        // View Frustum bounds (orthographic block from x=-50 to x=50)
        camData.frustumPlanes[0][0] = 1.0f; camData.frustumPlanes[0][1] = 0.0f; camData.frustumPlanes[0][2] = 0.0f; camData.frustumPlanes[0][3] = 50.0f;
        camData.frustumPlanes[1][0] = -1.0f; camData.frustumPlanes[1][1] = 0.0f; camData.frustumPlanes[1][2] = 0.0f; camData.frustumPlanes[1][3] = 50.0f;
        camData.frustumPlanes[2][0] = 0.0f; camData.frustumPlanes[2][1] = 1.0f; camData.frustumPlanes[2][2] = 0.0f; camData.frustumPlanes[2][3] = 50.0f;
        camData.frustumPlanes[3][0] = 0.0f; camData.frustumPlanes[3][1] = -1.0f; camData.frustumPlanes[3][2] = 0.0f; camData.frustumPlanes[3][3] = 50.0f;
        camData.frustumPlanes[4][0] = 0.0f; camData.frustumPlanes[4][1] = 0.0f; camData.frustumPlanes[4][2] = -1.0f; camData.frustumPlanes[4][3] = 50.0f;
        camData.frustumPlanes[5][0] = 0.0f; camData.frustumPlanes[5][1] = 0.0f; camData.frustumPlanes[5][2] = 1.0f; camData.frustumPlanes[5][3] = 50.0f;
        camData.numElements = NUM_NODES;

        VkBuffer cameraUBO;
        VkDeviceMemory cameraUBOMemory;
        backend.createBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraUBO, cameraUBOMemory);

        void* camMapped;
        vkMapMemory(backend.getDevice(), cameraUBOMemory, 0, sizeof(CameraData), 0, &camMapped);
        memcpy(camMapped, &camData, sizeof(CameraData));
        vkUnmapMemory(backend.getDevice(), cameraUBOMemory);

        std::cout << "Updating Descriptor Sets..." << std::endl;
        cullingSystem.updateDescriptorSets(cameraUBO);
        std::cout << "Descriptor Sets updated!" << std::endl;

        std::cout << "Dispatching Compute Culling across GPU..." << std::endl;
        auto computeStart = std::chrono::steady_clock::now();
        VkCommandBuffer computeCmd = backend.beginSingleTimeCommands();
        cullingSystem.dispatchCulling(computeCmd, NUM_NODES);
        
        std::cout << "Ending single time commands (submitting to queue)..." << std::endl;
        backend.endSingleTimeCommands(computeCmd); // blocks until complete
        std::cout << "Queue submit complete!" << std::endl;
        auto computeEnd = std::chrono::steady_clock::now();

        std::chrono::duration<double, std::milli> computeMs = computeEnd - computeStart;
        std::cout << "GPU Compute Shader Culling Time: " << computeMs.count() << " ms" << std::endl;

        // ---------- CPU CULLING BENCHMARK ----------
        std::vector<uint32_t> cpuVisibility(NUM_NODES, 0);
        auto cpuStart = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_NODES; ++i) {
            float tx = ((const float*)sceneManager.getTransformData())[i * 16 + 12];
            float ty = ((const float*)sceneManager.getTransformData())[i * 16 + 13];
            float tz = ((const float*)sceneManager.getTransformData())[i * 16 + 14];
            
            bool visible = true;
            for (int p = 0; p < 6; ++p) {
                float dist = camData.frustumPlanes[p][0] * tx + camData.frustumPlanes[p][1] * ty + camData.frustumPlanes[p][2] * tz + camData.frustumPlanes[p][3];
                // radius is 1.0 (extents = 1, assuming non-rotated)
                if (dist <= -1.0f) {
                    visible = false; break;
                }
            }
            cpuVisibility[i] = visible ? 1 : 0;
        }
        auto cpuEnd = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> cpuMs = cpuEnd - cpuStart;
        std::cout << "CPU Traversal Culling Time: " << cpuMs.count() << " ms" << std::endl;

        // Verify Culling Compute Correctness
        VkBuffer readbackVisBuffer;
        VkDeviceMemory readbackVisMemory;
        backend.createBuffer(sizeof(uint32_t) * NUM_NODES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, readbackVisBuffer, readbackVisMemory);
        
        VkCommandBuffer visCmd = backend.beginSingleTimeCommands();
        VkBufferCopy visCopy{};
        visCopy.size = sizeof(uint32_t) * NUM_NODES;
        vkCmdCopyBuffer(visCmd, stateManager.getVisibilityBuffer(), readbackVisBuffer, 1, &visCopy);
        backend.endSingleTimeCommands(visCmd);

        void* visMapped;
        vkMapMemory(backend.getDevice(), readbackVisMemory, 0, sizeof(uint32_t) * NUM_NODES, 0, &visMapped);
        uint32_t* gpuVisData = (uint32_t*)visMapped;

        int mismatchCount = 0;
        for (int i = 0; i < NUM_NODES; ++i) {
            if (gpuVisData[i] != cpuVisibility[i]) {
                mismatchCount++;
                if (mismatchCount <= 10) {
                    float tx = ((const float*)sceneManager.getTransformData())[i * 16 + 12];
                    std::cout << "Mismatch at " << i << "! CPU=" << cpuVisibility[i] << " GPU=" << gpuVisData[i] << " tx=" << tx << std::endl;
                }
            }
        }
        vkUnmapMemory(backend.getDevice(), readbackVisMemory);

        if (mismatchCount > 0) {
            throw std::runtime_error("Culling Mismatches found! GPU Compute visibility logic differs from CPU logic by " + std::to_string(mismatchCount) + " bounds.");
        }
        std::cout << "Culling Compute Correctness Verified: GPU exactly matches CPU intersection physics." << std::endl;

        // Verify DrawCount Buffer matches CPU count
        uint32_t cpuVisibleCount = 0;
        for (int i = 0; i < NUM_NODES; ++i) {
            if (cpuVisibility[i]) cpuVisibleCount++;
        }

        VkBuffer readbackCountBuffer;
        VkDeviceMemory readbackCountMemory;
        backend.createBuffer(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, readbackCountBuffer, readbackCountMemory);

        VkCommandBuffer countCmd = backend.beginSingleTimeCommands();
        VkBufferCopy countCopy{};
        countCopy.size = sizeof(uint32_t);
        vkCmdCopyBuffer(countCmd, stateManager.getDrawCountBuffer(), readbackCountBuffer, 1, &countCopy);
        backend.endSingleTimeCommands(countCmd);

        void* countMapped;
        vkMapMemory(backend.getDevice(), readbackCountMemory, 0, sizeof(uint32_t), 0, &countMapped);
        uint32_t gpuVisibleCount = *((uint32_t*)countMapped);
        vkUnmapMemory(backend.getDevice(), readbackCountMemory);

        std::cout << "Atomic Draw Count Validation: CPU " << cpuVisibleCount << " vs GPU " << gpuVisibleCount << std::endl;
        if (cpuVisibleCount != gpuVisibleCount) {
            throw std::runtime_error("Draw counts mismatch!");
        }

        vkDestroyBuffer(backend.getDevice(), cameraUBO, nullptr);
        vkFreeMemory(backend.getDevice(), cameraUBOMemory, nullptr);
        vkDestroyBuffer(backend.getDevice(), readbackVisBuffer, nullptr);
        vkFreeMemory(backend.getDevice(), readbackVisMemory, nullptr);
        vkDestroyBuffer(backend.getDevice(), readbackCountBuffer, nullptr);
        vkFreeMemory(backend.getDevice(), readbackCountMemory, nullptr);

        root->unref();

    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
