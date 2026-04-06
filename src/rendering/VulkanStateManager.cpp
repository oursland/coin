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

    // Free existing buffers if we're expanding array topology
    freeStorageBuffers();

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

    VkDeviceSize bboxSize = sizeof(BoundingBoxData) * numElements;
    vkBackend->createBuffer(
        bboxSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        boundingBoxBuffer,
        boundingBoxMemory
    );
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

    stagingCapacity = 0;
}

void VulkanStateManager::upload(PersistentSceneManager* sceneManager) {
    size_t numTransforms = sceneManager->getNumTransforms();
    size_t numMaterials = sceneManager->getNumMaterials();
    size_t maxElements = numTransforms > numMaterials ? numTransforms : numMaterials;

    if (maxElements == 0) return;

    allocateStorageBuffers(maxElements);
    allocateStagingBuffers(maxElements);

    VkDevice device = vkBackend->getDevice();
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    size_t tMin, tMax;
    sceneManager->getTransformDirtyRange(tMin, tMax);
    
    // 1. Map and Copy Transforms (Targeted Range)
    if (numTransforms > 0 && tMin <= tMax && tMax < numTransforms) {
        size_t count = tMax - tMin + 1;
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

    // 2. Map and Copy Materials (Naive for now)
    if (numMaterials > 0) { // Should similarly implement material dirty ranges if implemented
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

    if (cmdBuffer != VK_NULL_HANDLE) {
        vkBackend->endSingleTimeCommands(cmdBuffer);
    }
}

