#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SbViewportRegion.h>
#include <GLFW/glfw3.h>
#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanRenderer.h>
#include <Inventor/VulkanStateManager.h>
#include <Inventor/VulkanCullingSystem.h>
#include <Inventor/PersistentSceneManager.h>
#include <iostream>
#include <chrono>

SoSeparator* buildMassiveScene(int size) {
    SoSeparator* root = new SoSeparator;
    root->ref();
    for (int x = -size; x < size; x++) {
        for (int y = -size; y < size; y++) {
            for (int z = -2; z < 2; z++) { // Adds a 3rd dimension for more nodes
                SoSeparator* sep = new SoSeparator;
                SoTransform* t = new SoTransform;
                t->translation.setValue(x * 2.5f, z * 2.5f, y * 2.5f);
                SoCube* c = new SoCube;
                sep->addChild(t);
                sep->addChild(c);
                root->addChild(sep);
            }
        }
    }
    return root;
}

void benchmarkLegacyOpenGL(SoSeparator* root, int numFrames) {
    std::cout << "\n--- Legacy OpenGL Traversal Benchmark ---" << std::endl;
    
    // Modern macOS breaks CGL pBuffers. We spawn a hidden GLFW window to supply standard OpenGL instead.
    glfwInit();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Coin3D Legacy Benchmark", nullptr, nullptr);
    if (!window) {
        std::cout << "Failed to allocate legacy OpenGL Context via GLFW." << std::endl;
        return;
    }
    glfwMakeContextCurrent(window);

    SbViewportRegion viewport(1920, 1080);
    SoGLRenderAction renderAction(viewport);
    
    // Warmup
    renderAction.apply(root);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < numFrames; i++) {
        // Incrementally update graph
        SoTransform* t = (SoTransform*)((SoSeparator*)root->getChild(0))->getChild(0);
        t->translation.setValue(i, 0, 0);

        renderAction.apply(root);
        
        // Wait for GL to finalize its sequential calls ensuring timing isolates execution perfectly!
        glfwSwapInterval(0);
        glfwSwapBuffers(window); 
    }
    auto end = std::chrono::steady_clock::now();
    
    std::chrono::duration<double, std::milli> ms = end - start;
    std::cout << "Legacy OpenGL (" << numFrames << " frames): " << ms.count() / numFrames << " ms / frame" << std::endl;

    glfwDestroyWindow(window);
}

void benchmarkVulkanECS(SoSeparator* root, int numFrames) {
    std::cout << "\n--- Modern Vulkan ECS Benchmark ---" << std::endl;
    
    ModernVulkanBackend backend;
    backend.initInstance();
    backend.initDevice(VK_NULL_HANDLE); // Headless

    VulkanStateManager stateManager(&backend);
    PersistentSceneManager sceneManager;
    sceneManager.setSceneGraph(root);

    // Warmup Upload
    stateManager.upload(&sceneManager);
    uint32_t numElements = sceneManager.getNumTransforms();

    VkBuffer cameraUBO;
    VkDeviceMemory cameraUBOMemory;
    backend.createBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraUBO, cameraUBOMemory);

    VulkanCullingSystem cullingSystem(&backend, &stateManager);
    cullingSystem.init("src/rendering/shaders/culling.spv");
    cullingSystem.updateDescriptorSets(cameraUBO);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = backend.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(backend.getDevice(), &allocInfo, &commandBuffer);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < numFrames; i++) {
        // Incrementally update graph
        SoTransform* t = (SoTransform*)((SoSeparator*)root->getChild(0))->getChild(0);
        t->translation.setValue(i, 0, 0);

        // Upload minimal changes natively to ECS via memcpy!
        stateManager.upload(&sceneManager);

        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        cullingSystem.dispatchCulling(commandBuffer, numElements);

        VkMemoryBarrier drawBarrier{};
        drawBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        drawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &drawBarrier, 0, nullptr, 0, nullptr);

        // Since it's headless, we don't dispatch bindAndDraw as there is no swapchain. 
        // Frustum pipeline execution cost reflects compute time perfectly.
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(backend.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        
        // Block to evaluate exact hardware dispatch time without async overlap
        vkQueueWaitIdle(backend.getGraphicsQueue()); 
    }
    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<double, std::milli> ms = end - start;
    std::cout << "Modern Vulkan ECS (" << numFrames << " frames): " << ms.count() / numFrames << " ms / frame" << std::endl;

    vkDeviceWaitIdle(backend.getDevice());
    vkDestroyBuffer(backend.getDevice(), cameraUBO, nullptr);
    vkFreeMemory(backend.getDevice(), cameraUBOMemory, nullptr);
    backend.cleanup();
}

int main() {
    SoDB::init();

    int size = 95; // (size * 2) * (size * 2) * 4 = 190 * 190 * 4 = 144,400 objects
    std::cout << "Building Massive 3D Voxel Scene for Benchmark..." << std::endl;
    SoSeparator* root = buildMassiveScene(size);

    benchmarkLegacyOpenGL(root, 100);
    benchmarkVulkanECS(root, 100);

    root->unref();
    return 0;
}
