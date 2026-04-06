#ifndef COIN_VULKANSTATEMANAGER_H
#define COIN_VULKANSTATEMANAGER_H

#include <Inventor/ModernVulkanBackend.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <Inventor/PersistentSceneManager.h>

// Note: Using arrays of 4 floats to guarantee strict std430 16-byte alignment
// to avoid spir-v layout misalignment when reading vec3 / vec4.
struct TransformData {
    float matrix[16];
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
    VkBuffer getShapeMaterialMapBuffer() const { return shapeMaterialMapBuffer; }
    VkBuffer getLightUBO() const { return lightUBO; }
    VkBuffer getBoundingBoxBuffer() const { return boundingBoxBuffer; }
    VkBuffer getVisibilityBuffer() const { return visibilityBuffer; }
    VkBuffer getIndirectDrawBuffer() const { return indirectDrawBuffer; }
    VkBuffer getDrawCountBuffer() const { return drawCountBuffer; }
    VkBuffer getGlobalVertexBuffer() const { return globalVertexBuffer; }
    VkBuffer getGlobalIndexBuffer() const { return globalIndexBuffer; }

private:
    ModernVulkanBackend* vkBackend;

    VkBuffer transformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory transformMemory = VK_NULL_HANDLE;

    VkBuffer materialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory = VK_NULL_HANDLE;

    VkBuffer shapeMaterialMapBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shapeMaterialMapMemory = VK_NULL_HANDLE;

    VkBuffer lightUBO = VK_NULL_HANDLE;
    VkDeviceMemory lightUBOMemory = VK_NULL_HANDLE;

    VkBuffer boundingBoxBuffer = VK_NULL_HANDLE;
    VkDeviceMemory boundingBoxMemory = VK_NULL_HANDLE;

    VkBuffer visibilityBuffer = VK_NULL_HANDLE;
    VkDeviceMemory visibilityMemory = VK_NULL_HANDLE;

    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectDrawMemory = VK_NULL_HANDLE;

    VkBuffer drawCountBuffer = VK_NULL_HANDLE;
    VkDeviceMemory drawCountMemory = VK_NULL_HANDLE;

    VkBuffer globalVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalVertexMemory = VK_NULL_HANDLE;

    VkBuffer globalIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalIndexMemory = VK_NULL_HANDLE;

    // Staging buffers mapping CPU-visible memory
    VkBuffer stagingTransformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingTransformMemory = VK_NULL_HANDLE;

    VkBuffer stagingMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMaterialMemory = VK_NULL_HANDLE;

    VkBuffer stagingShapeMaterialMapBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingShapeMaterialMapMemory = VK_NULL_HANDLE;

    VkBuffer stagingLightUBO = VK_NULL_HANDLE;
    VkDeviceMemory stagingLightUBOMemory = VK_NULL_HANDLE;

    VkBuffer stagingBoundingBoxBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBoundingBoxMemory = VK_NULL_HANDLE;
    
    size_t currentCapacity = 0;
    size_t stagingCapacity = 0;
};

#endif // COIN_VULKANSTATEMANAGER_H
