#include "../../include/Inventor/ModernVulkanBackend.h"

ModernVulkanBackend::ModernVulkanBackend() {
}

ModernVulkanBackend::~ModernVulkanBackend() {
    cleanup();
}

void ModernVulkanBackend::init() {
    createInstance();
    pickPhysicalDevice();
    createLogicalDevice();
}

void ModernVulkanBackend::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

std::vector<const char*> ModernVulkanBackend::getRequiredExtensions() {
    std::vector<const char*> extensions;
    
    // Specifically target MoltenVK / Portable subsets required for macOS
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    
    return extensions;
}

void ModernVulkanBackend::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Coin3D ModernVulkanBackend";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Coin3D ECS Pipeline";
    appInfo.engineVersion = VK_MAKE_VERSION(4, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1; // Minimal API for compute and subgroups

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // Setting Flag for macOS portability enumeration
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Layers (e.g. Vulkan validation layers could be added here)
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vulkan instance!");
    }
}

void ModernVulkanBackend::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // We just pick the first standard one for the architectural prototype
    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(dev, &deviceProperties);
        
        // Print found device
        std::cout << "Found Vulkan Device: " << deviceProperties.deviceName << std::endl;
        
        // Simple selection
        physicalDevice = dev; 
        break;
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void ModernVulkanBackend::createLogicalDevice() {
    // For simplicity, we queue family index 0. A robust implementation would query vkGetPhysicalDeviceQueueFamilyProperties
    uint32_t graphicsFamily = 0; 

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsFamily;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    // We need portability subset on macOS
    std::vector<const char*> deviceExtensions = { "VK_KHR_portability_subset" };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
}
