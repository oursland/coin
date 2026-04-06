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

void VulkanStateManager::upload(const PersistentSceneManager* sceneManager) {
    size_t numTransforms = sceneManager->getNumTransforms();
    size_t numMaterials = sceneManager->getNumMaterials();
    size_t maxElements = numTransforms > numMaterials ? numTransforms : numMaterials;

    if (maxElements == 0) return;

    allocateStorageBuffers(maxElements);
    allocateStagingBuffers(maxElements);

    VkDevice device = vkBackend->getDevice();

    // 1. Map and Copy Transforms
    if (numTransforms > 0) {
        const void* transformData = sceneManager->getTransformData();
        void* mappedData;
        vkMapMemory(device, stagingTransformMemory, 0, sizeof(TransformData) * numTransforms, 0, &mappedData);
        memcpy(mappedData, transformData, sizeof(TransformData) * numTransforms);
        vkUnmapMemory(device, stagingTransformMemory);
    }

    // 2. Map and Copy Materials
    if (numMaterials > 0) {
        const void* materialData = sceneManager->getMaterialData();
        void* mappedData;
        vkMapMemory(device, stagingMaterialMemory, 0, sizeof(MaterialData) * numMaterials, 0, &mappedData);
        memcpy(mappedData, materialData, sizeof(MaterialData) * numMaterials);
        vkUnmapMemory(device, stagingMaterialMemory);
    }

    // 3. Issue Asynchronous Upload Compute (vkCmdCopyBuffer) via Single-Time Commands
    VkCommandBuffer cmdBuffer = vkBackend->beginSingleTimeCommands();

    if (numTransforms > 0) {
        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(TransformData) * numTransforms;
        vkCmdCopyBuffer(cmdBuffer, stagingTransformBuffer, transformBuffer, 1, &copyRegion);
    }

    if (numMaterials > 0) {
        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(MaterialData) * numMaterials;
        vkCmdCopyBuffer(cmdBuffer, stagingMaterialBuffer, materialBuffer, 1, &copyRegion);
    }

    vkBackend->endSingleTimeCommands(cmdBuffer);
}

