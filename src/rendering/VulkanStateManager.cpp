#include <Inventor/VulkanStateManager.h>

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
