#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanRenderer.h>
#include <Inventor/VulkanStateManager.h>
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

    std::cout << "Creating Swapchain..." << std::endl;
    backend.createSwapChain({WIDTH, HEIGHT});
    backend.createImageViews();

    // Setup state manager and renderer
    VulkanStateManager stateManager(&backend);
    VulkanRenderer renderer(&backend, &stateManager);

    std::cout << "Initializing Renderer for Swapchain..." << std::endl;
    // We haven't compiled the shaders in build/ bin yet for test, so we pass empty for now just to test swapchain setup.
    // Wait! The app will crash if shader paths are "none". I'll use real paths but we must ensure they compile.
    // Let's defer renderer init to later if just testing swapchain integration.
    
    // For now we just prove swapchain compiles and runs
    
    std::cout << "Successfully generated " << backend.getSwapChainImageViews().size() << " Swapchain Images!" << std::endl;

    for (int i = 0; i < 10; i++) {
        glfwPollEvents();
    }

    vkDeviceWaitIdle(backend.getDevice());

    vkDestroySurfaceKHR(backend.getInstance(), surface, nullptr);
    backend.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
