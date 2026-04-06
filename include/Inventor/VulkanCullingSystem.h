#ifndef COIN_VULKANCULLINGSYSTEM_H
#define COIN_VULKANCULLINGSYSTEM_H

#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanStateManager.h>
#include <vulkan/vulkan.h>
#include <string>

// Frustum planes input mapping matching std140 CameraData UBO
struct CameraData {
    float frustumPlanes[6][4]; // left, right, bottom, top, near, far
    uint32_t numElements;
    uint32_t padding[3];
};

class VulkanCullingSystem {
public:
    VulkanCullingSystem(ModernVulkanBackend* backend, VulkanStateManager* stateManager);
    ~VulkanCullingSystem();

    void init(const std::string& shaderPath);
    void updateDescriptorSets(VkBuffer cameraUBO);
    void dispatchCulling(VkCommandBuffer cmdBuffer, uint32_t numElements);

private:
    VkShaderModule createShaderModule(const std::string& path);

    ModernVulkanBackend* vkBackend;
    VulkanStateManager* stateManager;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

#endif // COIN_VULKANCULLINGSYSTEM_H
