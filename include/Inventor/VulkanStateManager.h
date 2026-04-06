#ifndef COIN_VULKANSTATEMANAGER_H
#define COIN_VULKANSTATEMANAGER_H

#include <Inventor/ModernVulkanBackend.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <cstddef>

// Note: Using arrays of 4 floats to guarantee strict std430 16-byte alignment
// to avoid spir-v layout misalignment when reading vec3 / vec4.
struct TransformData {
    float matrix[16];
};

struct MaterialData {
    float color[4];
};

struct BoundingBoxData {
    float min[4];
    float max[4];
};

class VulkanStateManager {
public:
    VulkanStateManager(ModernVulkanBackend* backend);
    ~VulkanStateManager();

    void allocateStorageBuffers(size_t numElements);
    void freeStorageBuffers();

    void allocateStagingBuffers(size_t numElements);
    void freeStagingBuffers();

    class PersistentSceneManager* sceneManager;
    void upload(class PersistentSceneManager* sceneManager);

    size_t getCurrentCapacity() const { return currentCapacity; }

    VkBuffer getTransformBuffer() const { return transformBuffer; }
    VkBuffer getMaterialBuffer() const { return materialBuffer; }
    VkBuffer getBoundingBoxBuffer() const { return boundingBoxBuffer; }
    VkBuffer getVisibilityBuffer() const { return visibilityBuffer; }
    VkBuffer getIndirectDrawBuffer() const { return indirectDrawBuffer; }
    VkBuffer getDrawCountBuffer() const { return drawCountBuffer; }

private:
    ModernVulkanBackend* vkBackend;

    VkBuffer transformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory transformMemory = VK_NULL_HANDLE;

    VkBuffer materialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory = VK_NULL_HANDLE;

    VkBuffer boundingBoxBuffer = VK_NULL_HANDLE;
    VkDeviceMemory boundingBoxMemory = VK_NULL_HANDLE;

    VkBuffer visibilityBuffer = VK_NULL_HANDLE;
    VkDeviceMemory visibilityMemory = VK_NULL_HANDLE;

    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectDrawMemory = VK_NULL_HANDLE;

    VkBuffer drawCountBuffer = VK_NULL_HANDLE;
    VkDeviceMemory drawCountMemory = VK_NULL_HANDLE;

    // Staging buffers mapping CPU-visible memory
    VkBuffer stagingTransformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingTransformMemory = VK_NULL_HANDLE;

    VkBuffer stagingMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMaterialMemory = VK_NULL_HANDLE;

    VkBuffer stagingBoundingBoxBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBoundingBoxMemory = VK_NULL_HANDLE;
    
    size_t currentCapacity = 0;
    size_t stagingCapacity = 0;
};

#endif // COIN_VULKANSTATEMANAGER_H
