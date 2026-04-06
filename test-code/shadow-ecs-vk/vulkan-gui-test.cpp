#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Inventor/ModernVulkanBackend.h>
#include <iostream>

const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 720;

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Coin3D Vulkan GUI Test", nullptr, nullptr);

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    ModernVulkanBackend backend;
    std::cout << "Creating Vulkan Instance with GLFW extensions..." << std::endl;
    backend.initInstance(extensions);

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(backend.getInstance(), window, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "failed to create window surface!" << std::endl;
        return 1;
    }

    std::cout << "Initializing Vulkan Device..." << std::endl;
    backend.initDevice(surface);

    std::cout << "Successfully created window and bound Vulkan Device to surface!" << std::endl;

    // We will just wait 1 second to verify things launch headless automatically, since we are non-interactive AI.
    // Wait, let's poll a few times just to be safe.
    for (int i=0; i<10; i++) {
        glfwPollEvents();
    }

    // Cleanup securely!
    // We cannot destroy surface after instance is destroyed.
    // ModernVulkanBackend cleans up everything, but it currently clears the instance.
    // Let's destroy it before cleaning the backend!
    
    // Wait, the device might need to wait for idle before destroying
    vkDeviceWaitIdle(backend.getDevice());
    
    vkDestroySurfaceKHR(backend.getInstance(), surface, nullptr);
    backend.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
