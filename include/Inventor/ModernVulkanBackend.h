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
    
    // Extensions required for macOS MoltenVK Portability
    std::vector<const char*> getRequiredExtensions();
};

#endif // COIN_MODERNVULKANBACKEND_H
