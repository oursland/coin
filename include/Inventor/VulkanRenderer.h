#ifndef COIN_VULKANRENDERER_H
#define COIN_VULKANRENDERER_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

class ModernVulkanBackend;
class VulkanStateManager;

class VulkanRenderer {
public:
    VulkanRenderer(ModernVulkanBackend* backend, VulkanStateManager* stateManager);
    ~VulkanRenderer();

    void init(VkFormat colorFormat, VkExtent2D extent, const std::string& vertShaderPath, const std::string& fragShaderPath, VkBuffer cameraUBO);
    void createFramebuffers(const std::vector<VkImageView>& imageViews);
    void cleanup();

    void bindAndDraw(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex, uint32_t maxDrawCount);
    
    VkQueryPool getQueryPool() const { return queryPool; }

private:
    ModernVulkanBackend* backend;
    VulkanStateManager* stateManager;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkExtent2D renderExtent;
    VkFormat targetFormat;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkQueryPool queryPool = VK_NULL_HANDLE;

    void createRenderPass(VkFormat colorFormat);
    void createDescriptorSetLayout();
    void createGraphicsPipeline(const std::string& vertPath, const std::string& fragPath);
    void createQueryPool();
    void allocateDescriptorSet(VkBuffer cameraUBO);

    VkShaderModule createShaderModule(const std::string& path);
};

#endif // COIN_VULKANRENDERER_H
