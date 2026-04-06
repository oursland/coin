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

    void init();
    void cleanup();

    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) const;

private:
    void createInstance();
    void pickPhysicalDevice();
    void createLogicalDevice();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    
    // Extensions required for macOS MoltenVK Portability
    std::vector<const char*> getRequiredExtensions();
};

#endif // COIN_MODERNVULKANBACKEND_H
