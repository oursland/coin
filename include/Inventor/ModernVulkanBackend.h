#ifndef COIN_MODERNVULKANBACKEND_H
#define COIN_MODERNVULKANBACKEND_H

#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <iostream>

class ModernVulkanBackend {
public:
    ModernVulkanBackend();
    ~ModernVulkanBackend();

    void initInstance(const std::vector<const char*>& additionalExtensions = {});
    void initDevice(VkSurfaceKHR surface = VK_NULL_HANDLE);
    void cleanup();

    VkInstance getInstance() const { return instance; }
    VkDevice getDevice() const { return device; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    VkCommandPool getCommandPool() const { return commandPool; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkSurfaceKHR getSurface() const { return surface; }

    void createSwapChain(VkExtent2D windowExtent);
    void createImageViews();
    
    VkSwapchainKHR getSwapChain() const { return swapChain; }
    VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
    const std::vector<VkImageView>& getSwapChainImageViews() const { return swapChainImageViews; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) const;

    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

private:
    void createInstance(const std::vector<const char*>& additionalExtensions);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    // Swapchain support helpers
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
    
    // Extensions required for macOS MoltenVK Portability
    std::vector<const char*> getRequiredExtensions();
};

#endif // COIN_MODERNVULKANBACKEND_H
