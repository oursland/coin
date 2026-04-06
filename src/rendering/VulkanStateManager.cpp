#include <Inventor/VulkanStateManager.h>
#include <Inventor/PersistentSceneManager.h>
#include <cstring>
#include <stdexcept>

VulkanStateManager::VulkanStateManager(ModernVulkanBackend* backend) 
    : vkBackend(backend) {
}

VulkanStateManager::~VulkanStateManager() {
    freeStorageBuffers();
}

void VulkanStateManager::allocateStorageBuffers(size_t numElements) {
    if (numElements == 0 || numElements <= currentCapacity) return;

    std::cout << "DEBUG: Inside allocateStorageBuffers. Freeing storage..." << std::endl;
    // Free existing buffers if we're expanding array topology
    freeStorageBuffers();

    std::cout << "DEBUG: allocating transforms" << std::endl;
    currentCapacity = numElements;

    // We store arrays directly on the GPU as Storage Buffers (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    // allowing compute shaders to arbitrarily read/write from massive element arrays.
    
    VkDeviceSize transformSize = sizeof(TransformData) * numElements;
    vkBackend->createBuffer(
        transformSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // Transfer DST so we can map & stream to it
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // Device-local (fast GPU memory)
        transformBuffer,
        transformMemory
    );

    VkDeviceSize materialSize = sizeof(MaterialData) * numElements;
    vkBackend->createBuffer(
        materialSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        materialBuffer,
        materialMemory
    );

    VkDeviceSize mapSize = sizeof(uint32_t) * numElements;
    vkBackend->createBuffer(
        mapSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        shapeMaterialMapBuffer,
        shapeMaterialMapMemory
    );
    
    // We treat LightData as an SSBO as well to support large arrays generically.
    VkDeviceSize lightSize = sizeof(LightData) * std::max((size_t)16, numElements);
    vkBackend->createBuffer(
        lightSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        lightUBO,
        lightUBOMemory
    );

    VkDeviceSize bboxSize = sizeof(BoundingBoxData) * numElements;
    vkBackend->createBuffer(
        bboxSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        boundingBoxBuffer,
        boundingBoxMemory
    );

    // Visibility bitmask buffer (array of unsigned ints)
    VkDeviceSize visSize = sizeof(uint32_t) * numElements;
    vkBackend->createBuffer(
        visSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        visibilityBuffer,
        visibilityMemory
    );

    VkDeviceSize indirectSize = sizeof(VkDrawIndexedIndirectCommand) * numElements;
    vkBackend->createBuffer(
        indirectSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indirectDrawBuffer,
        indirectDrawMemory
    );

    VkDeviceSize countSize = sizeof(uint32_t);
    vkBackend->createBuffer(
        countSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        drawCountBuffer,
        drawCountMemory
    );

    // Provide 8 vertices (3 floats each) and 36 indices for a complete unit cube
    VkDeviceSize vertSize = sizeof(float) * 3 * 8;
    if (globalVertexBuffer == VK_NULL_HANDLE) {
        // We make these HOST_VISIBLE to avoid staging buffer complexity for static shape arrays in the benchmark
        vkBackend->createBuffer(
            vertSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            globalVertexBuffer,
            globalVertexMemory
        );

        float cubeVertices[] = {
            -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f, 0.5f, -0.5f,  0.5f, 0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  0.5f
        };
        void* data = nullptr;
        if (vkMapMemory(vkBackend->getDevice(), globalVertexMemory, 0, vertSize, 0, &data) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map global vertex memory!");
        }
        memcpy(data, cubeVertices, (size_t)vertSize);
        vkUnmapMemory(vkBackend->getDevice(), globalVertexMemory);

        VkDeviceSize intSize = sizeof(uint32_t) * 36;
        vkBackend->createBuffer(
            intSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            globalIndexBuffer,
            globalIndexMemory
        );
        
        uint32_t cubeIndices[] = {
            0,1,2, 2,3,0, 1,5,6, 6,2,1, 7,6,5, 5,4,7, 4,0,3, 3,7,4, 4,5,1, 1,0,4, 3,2,6, 6,7,3
        };
        if (vkMapMemory(vkBackend->getDevice(), globalIndexMemory, 0, intSize, 0, &data) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map global index memory!");
        }
        memcpy(data, cubeIndices, (size_t)intSize);
        vkUnmapMemory(vkBackend->getDevice(), globalIndexMemory);
    }
}

void VulkanStateManager::freeStorageBuffers() {
    VkDevice device = vkBackend->getDevice();
    if (device == VK_NULL_HANDLE) return;

    if (transformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, transformBuffer, nullptr);
        vkFreeMemory(device, transformMemory, nullptr);
        transformBuffer = VK_NULL_HANDLE;
    }

    if (materialBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialBuffer, nullptr);
        vkFreeMemory(device, materialMemory, nullptr);
        materialBuffer = VK_NULL_HANDLE;
    }
    
    if (shapeMaterialMapBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, shapeMaterialMapBuffer, nullptr);
        vkFreeMemory(device, shapeMaterialMapMemory, nullptr);
        shapeMaterialMapBuffer = VK_NULL_HANDLE;
    }
    
    if (lightUBO != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, lightUBO, nullptr);
        vkFreeMemory(device, lightUBOMemory, nullptr);
        lightUBO = VK_NULL_HANDLE;
    }

    if (boundingBoxBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, boundingBoxBuffer, nullptr);
        vkFreeMemory(device, boundingBoxMemory, nullptr);
        boundingBoxBuffer = VK_NULL_HANDLE;
    }
    
    if (visibilityBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, visibilityBuffer, nullptr);
        vkFreeMemory(device, visibilityMemory, nullptr);
        visibilityBuffer = VK_NULL_HANDLE;
    }

    if (indirectDrawBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indirectDrawBuffer, nullptr);
        vkFreeMemory(device, indirectDrawMemory, nullptr);
        indirectDrawBuffer = VK_NULL_HANDLE;
    }

    if (drawCountBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, drawCountBuffer, nullptr);
        vkFreeMemory(device, drawCountMemory, nullptr);
        drawCountBuffer = VK_NULL_HANDLE;
    }

    if (globalVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, globalVertexBuffer, nullptr);
        vkFreeMemory(device, globalVertexMemory, nullptr);
        globalVertexBuffer = VK_NULL_HANDLE;
    }

    if (globalIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, globalIndexBuffer, nullptr);
        vkFreeMemory(device, globalIndexMemory, nullptr);
        globalIndexBuffer = VK_NULL_HANDLE;
    }
    
    currentCapacity = 0;
}

void VulkanStateManager::allocateStagingBuffers(size_t numElements) {
    if (numElements == 0 || numElements <= stagingCapacity) return;

    freeStagingBuffers();
    stagingCapacity = numElements;

    VkDeviceSize transformSize = sizeof(TransformData) * numElements;
    vkBackend->createBuffer(
        transformSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingTransformBuffer,
        stagingTransformMemory
    );

    VkDeviceSize materialSize = sizeof(MaterialData) * numElements;
    vkBackend->createBuffer(
        materialSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingMaterialBuffer,
        stagingMaterialMemory
    );

    VkDeviceSize mapSize = sizeof(uint32_t) * numElements;
    vkBackend->createBuffer(
        mapSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingShapeMaterialMapBuffer,
        stagingShapeMaterialMapMemory
    );
    
    VkDeviceSize lightSize = sizeof(LightData) * std::max((size_t)16, numElements);
    vkBackend->createBuffer(
        lightSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingLightUBO,
        stagingLightUBOMemory
    );

    VkDeviceSize bboxSize = sizeof(BoundingBoxData) * numElements;
    vkBackend->createBuffer(
        bboxSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBoundingBoxBuffer,
        stagingBoundingBoxMemory
    );
}

void VulkanStateManager::freeStagingBuffers() {
    VkDevice device = vkBackend->getDevice();
    if (device == VK_NULL_HANDLE) return;

    if (stagingTransformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingTransformBuffer, nullptr);
        vkFreeMemory(device, stagingTransformMemory, nullptr);
        stagingTransformBuffer = VK_NULL_HANDLE;
    }

    if (stagingMaterialBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingMaterialBuffer, nullptr);
        vkFreeMemory(device, stagingMaterialMemory, nullptr);
        stagingMaterialBuffer = VK_NULL_HANDLE;
    }
    
    if (stagingShapeMaterialMapBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingShapeMaterialMapBuffer, nullptr);
        vkFreeMemory(device, stagingShapeMaterialMapMemory, nullptr);
        stagingShapeMaterialMapBuffer = VK_NULL_HANDLE;
    }
    
    if (stagingLightUBO != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingLightUBO, nullptr);
        vkFreeMemory(device, stagingLightUBOMemory, nullptr);
        stagingLightUBO = VK_NULL_HANDLE;
    }

    if (stagingBoundingBoxBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingBoundingBoxBuffer, nullptr);
        vkFreeMemory(device, stagingBoundingBoxMemory, nullptr);
        stagingBoundingBoxBuffer = VK_NULL_HANDLE;
    }

    stagingCapacity = 0;
}

void VulkanStateManager::upload(PersistentSceneManager* sceneManager) {
    size_t numTransforms = sceneManager->getNumTransforms();
    size_t numMaterials = sceneManager->getNumMaterials();
    size_t numShapeMaps = sceneManager->getNumShapeMaterialIndices();
    size_t numLights = sceneManager->getNumLights();
    
    size_t maxElements = std::max({numTransforms, numMaterials, numShapeMaps, numLights});

    if (maxElements == 0) return;

    std::cout << "DEBUG: allocateStorageBuffers" << std::endl;
    allocateStorageBuffers(maxElements);
    std::cout << "DEBUG: allocateStagingBuffers" << std::endl;
    allocateStagingBuffers(maxElements);

    VkDevice device = vkBackend->getDevice();
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    size_t tMin, tMax;
    std::cout << "DEBUG: getTransformDirtyRange" << std::endl;
    sceneManager->getTransformDirtyRange(tMin, tMax);
    
    // 1. Map and Copy Transforms (Targeted Range)
    if (numTransforms > 0 && tMin <= tMax && tMax < numTransforms) {
        size_t count = tMax - tMin + 1;
        std::cout << "DEBUG: Map transforms (count: " << count << ")" << std::endl;
        const void* transformData = sceneManager->getTransformData();
        void* mappedData;
        
        vkMapMemory(device, stagingTransformMemory, sizeof(TransformData) * tMin, sizeof(TransformData) * count, 0, &mappedData);
        memcpy(mappedData, (const char*)transformData + (sizeof(TransformData) * tMin), sizeof(TransformData) * count);
        vkUnmapMemory(device, stagingTransformMemory);

        if (cmdBuffer == VK_NULL_HANDLE) cmdBuffer = vkBackend->beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = sizeof(TransformData) * tMin;
        copyRegion.dstOffset = sizeof(TransformData) * tMin;
        copyRegion.size = sizeof(TransformData) * count;
        vkCmdCopyBuffer(cmdBuffer, stagingTransformBuffer, transformBuffer, 1, &copyRegion);
        
        sceneManager->resetTransformDirtyRange();
    }

    // 2. Map and Copy Materials and ShapeMappings
    if (numMaterials > 0) { 
        std::cout << "DEBUG: Map materials" << std::endl;
        const void* materialData = sceneManager->getMaterialData();
        void* mappedData;
        vkMapMemory(device, stagingMaterialMemory, 0, sizeof(MaterialData) * numMaterials, 0, &mappedData);
        memcpy(mappedData, materialData, sizeof(MaterialData) * numMaterials);
        vkUnmapMemory(device, stagingMaterialMemory);

        if (cmdBuffer == VK_NULL_HANDLE) cmdBuffer = vkBackend->beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(MaterialData) * numMaterials;
        vkCmdCopyBuffer(cmdBuffer, stagingMaterialBuffer, materialBuffer, 1, &copyRegion);
    }
    
    if (numShapeMaps > 0) {
        const void* shapeMapData = sceneManager->getShapeMaterialIndices();
        void* mappedData;
        vkMapMemory(device, stagingShapeMaterialMapMemory, 0, sizeof(uint32_t) * numShapeMaps, 0, &mappedData);
        memcpy(mappedData, shapeMapData, sizeof(uint32_t) * numShapeMaps);
        vkUnmapMemory(device, stagingShapeMaterialMapMemory);

        if (cmdBuffer == VK_NULL_HANDLE) cmdBuffer = vkBackend->beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(uint32_t) * numShapeMaps;
        vkCmdCopyBuffer(cmdBuffer, stagingShapeMaterialMapBuffer, shapeMaterialMapBuffer, 1, &copyRegion);
    }
    
    if (numLights > 0) {
        const void* lightData = sceneManager->getLightData();
        void* mappedData;
        vkMapMemory(device, stagingLightUBOMemory, 0, sizeof(LightData) * numLights, 0, &mappedData);
        memcpy(mappedData, lightData, sizeof(LightData) * numLights);
        vkUnmapMemory(device, stagingLightUBOMemory);

        if (cmdBuffer == VK_NULL_HANDLE) cmdBuffer = vkBackend->beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(LightData) * numLights;
        vkCmdCopyBuffer(cmdBuffer, stagingLightUBO, lightUBO, 1, &copyRegion);
    }

    // 3. Map and Copy Bounding Boxes (Naive for now)
    size_t numBBoxes = sceneManager->getNumBoundingBoxes();
    if (numBBoxes > 0) {
        std::cout << "DEBUG: Map bounding boxes" << std::endl;
        const void* bboxData = sceneManager->getBoundingBoxData();
        void* mappedData;
        vkMapMemory(device, stagingBoundingBoxMemory, 0, sizeof(BoundingBoxData) * numBBoxes, 0, &mappedData);
        memcpy(mappedData, bboxData, sizeof(BoundingBoxData) * numBBoxes);
        vkUnmapMemory(device, stagingBoundingBoxMemory);

        if (cmdBuffer == VK_NULL_HANDLE) cmdBuffer = vkBackend->beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(BoundingBoxData) * numBBoxes;
        vkCmdCopyBuffer(cmdBuffer, stagingBoundingBoxBuffer, boundingBoxBuffer, 1, &copyRegion);
    }

    if (cmdBuffer != VK_NULL_HANDLE) {
        vkBackend->endSingleTimeCommands(cmdBuffer);
    }
}

