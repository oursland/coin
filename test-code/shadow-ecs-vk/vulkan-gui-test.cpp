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
    // Provide absolute or relative paths to the compiled shaders in the build tree
    renderer.init(backend.getSwapChainImageFormat(), backend.getSwapChainExtent(), "build/src/rendering/shaders/basic.vert.spv", "build/src/rendering/shaders/basic.frag.spv");
    renderer.createFramebuffers(backend.getSwapChainImageViews());

    std::cout << "Successfully generated " << backend.getSwapChainImageViews().size() << " Swapchain Images and Framebuffers!" << std::endl;

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
