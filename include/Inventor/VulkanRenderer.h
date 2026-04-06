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

    void init(const std::string& vertShaderPath, const std::string& fragShaderPath);
    void cleanup();

    void bindAndDrawHeadless(VkCommandBuffer cmdBuffer, uint32_t maxDrawCount);
    
    VkQueryPool getQueryPool() const { return queryPool; }

private:
    ModernVulkanBackend* backend;
    VulkanStateManager* stateManager;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Headless Framebuffer infrastructure
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;
    VkFramebuffer headlessFramebuffer = VK_NULL_HANDLE;
    // We strictly use 512x512 for headless metric evaluation
    const uint32_t HEADLESS_WIDTH = 512;
    const uint32_t HEADLESS_HEIGHT = 512;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkQueryPool queryPool = VK_NULL_HANDLE;

    void createRenderPass();
    void createHeadlessFramebuffer();
    void createDescriptorSetLayout();
    void createGraphicsPipeline(const std::string& vertPath, const std::string& fragPath);
    void createQueryPool();
    void allocateDescriptorSet();

    VkShaderModule createShaderModule(const std::string& path);
};

#endif // COIN_VULKANRENDERER_H
