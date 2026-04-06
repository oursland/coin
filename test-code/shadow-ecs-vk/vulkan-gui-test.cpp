#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanRenderer.h>
#include <Inventor/VulkanStateManager.h>
#include <Inventor/VulkanCullingSystem.h>
#include <Inventor/PersistentSceneManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/SoDB.h>
#include <iostream>
#include <cmath>
#include <cstring>

// CameraData is defined via VulkanCullingSystem.h

struct Camera {
    float pos[3] = {0.0f, 0.0f, 10.0f};
    float front[3] = {0.0f, 0.0f, -1.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = 2.5f;

    void processInput(GLFWwindow* window, float dt) {
        float velocity = speed * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            pos[0] += front[0] * velocity; pos[1] += front[1] * velocity; pos[2] += front[2] * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            pos[0] -= front[0] * velocity; pos[1] -= front[1] * velocity; pos[2] -= front[2] * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            float right[3] = { front[1]*up[2] - front[2]*up[1], front[2]*up[0] - front[0]*up[2], front[0]*up[1] - front[1]*up[0] };
            float len = sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
            pos[0] -= (right[0]/len) * velocity; pos[1] -= (right[1]/len) * velocity; pos[2] -= (right[2]/len) * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            float right[3] = { front[1]*up[2] - front[2]*up[1], front[2]*up[0] - front[0]*up[2], front[0]*up[1] - front[1]*up[0] };
            float len = sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
            pos[0] += (right[0]/len) * velocity; pos[1] += (right[1]/len) * velocity; pos[2] += (right[2]/len) * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            pos[1] += velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            pos[1] -= velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetWindowShouldClose(window, true);
        }
    }

    void buildVP(float aspect, float nearP, float farP, float* vpOut) {
        // Perspective (Vulkan NDC)
        float fov = 45.0f * (M_PI / 180.0f);
        float halfTan = tan(fov / 2.0f);
        float proj[16] = {0};
        proj[0 * 4 + 0] = 1.0f / (aspect * halfTan);
        proj[1 * 4 + 1] = -(1.0f / halfTan); // Inverted Y
        proj[2 * 4 + 2] = farP / (nearP - farP);
        proj[2 * 4 + 3] = -1.0f;
        proj[3 * 4 + 2] = -(farP * nearP) / (farP - nearP);
        proj[3 * 4 + 3] = 0.0f;

        // View Matrix (LookAt structure)
        float zaxis[3] = { -front[0], -front[1], -front[2] };
        float xaxis[3] = { up[1]*zaxis[2] - up[2]*zaxis[1], up[2]*zaxis[0] - up[0]*zaxis[2], up[0]*zaxis[1] - up[1]*zaxis[0] };
        float xaxis_len = sqrt(xaxis[0]*xaxis[0] + xaxis[1]*xaxis[1] + xaxis[2]*xaxis[2]);
        xaxis[0] /= xaxis_len; xaxis[1] /= xaxis_len; xaxis[2] /= xaxis_len;
        
        float yaxis[3] = { zaxis[1]*xaxis[2] - zaxis[2]*xaxis[1], zaxis[2]*xaxis[0] - zaxis[0]*xaxis[2], zaxis[0]*xaxis[1] - zaxis[1]*xaxis[0] };
        
        float view[16] = {
            xaxis[0], yaxis[0], zaxis[0], 0,
            xaxis[1], yaxis[1], zaxis[1], 0,
            xaxis[2], yaxis[2], zaxis[2], 0,
            -(xaxis[0]*pos[0] + xaxis[1]*pos[1] + xaxis[2]*pos[2]),
            -(yaxis[0]*pos[0] + yaxis[1]*pos[1] + yaxis[2]*pos[2]),
            -(zaxis[0]*pos[0] + zaxis[1]*pos[1] + zaxis[2]*pos[2]),
            1
        };

        // Multiply Proj * View
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                float sum = 0.0f;
                for (int i = 0; i < 4; i++) {
                    sum += proj[i * 4 + c] * view[r * 4 + i]; // Column major
                }
                vpOut[r * 4 + c] = sum;
            }
        }
    }

    void extractFrustum(const float* vp, float planesOut[6][4]) {
        // Left
        planesOut[0][0] = vp[3] + vp[0]; planesOut[0][1] = vp[7] + vp[4]; planesOut[0][2] = vp[11] + vp[8]; planesOut[0][3] = vp[15] + vp[12];
        // Right
        planesOut[1][0] = vp[3] - vp[0]; planesOut[1][1] = vp[7] - vp[4]; planesOut[1][2] = vp[11] - vp[8]; planesOut[1][3] = vp[15] - vp[12];
        // Bottom
        planesOut[2][0] = vp[3] + vp[1]; planesOut[2][1] = vp[7] + vp[5]; planesOut[2][2] = vp[11] + vp[9]; planesOut[2][3] = vp[15] + vp[13];
        // Top
        planesOut[3][0] = vp[3] - vp[1]; planesOut[3][1] = vp[7] - vp[5]; planesOut[3][2] = vp[11] - vp[9]; planesOut[3][3] = vp[15] - vp[13];
        // Near (Vulkan 0-1)
        planesOut[4][0] = vp[2]; planesOut[4][1] = vp[6]; planesOut[4][2] = vp[10]; planesOut[4][3] = vp[14];
        // Far
        planesOut[5][0] = vp[3] - vp[2]; planesOut[5][1] = vp[7] - vp[6]; planesOut[5][2] = vp[11] - vp[10]; planesOut[5][3] = vp[15] - vp[14];
        
        for (int i=0; i<6; i++) {
            float len = sqrt(planesOut[i][0]*planesOut[i][0] + planesOut[i][1]*planesOut[i][1] + planesOut[i][2]*planesOut[i][2]);
            planesOut[i][0] /= len; planesOut[i][1] /= len; planesOut[i][2] /= len; planesOut[i][3] /= len;
        }
    }
    void updateVectors() {
        float f[3];
        f[0] = cos(pitch * (M_PI / 180.0f)) * cos(yaw * (M_PI / 180.0f));
        f[1] = sin(pitch * (M_PI / 180.0f));
        f[2] = cos(pitch * (M_PI / 180.0f)) * sin(yaw * (M_PI / 180.0f));
        float len = sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
        front[0] = f[0]/len; front[1] = f[1]/len; front[2] = f[2]/len;
    }
};

Camera cam;
bool firstMouse = true;
double lastX = 1280.0 / 2.0;
double lastY = 720.0 / 2.0;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    cam.yaw += xoffset * sensitivity;
    cam.pitch += yoffset * sensitivity;

    if (cam.pitch > 89.0f) cam.pitch = 89.0f;
    if (cam.pitch < -89.0f) cam.pitch = -89.0f;

    cam.updateVectors();
}

const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 720;

int main() {
    SoDB::init();
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Coin3D Vulkan GUI Test", nullptr, nullptr);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

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

    {
        // Setup state manager and renderer
        VulkanStateManager stateManager(&backend);
        VulkanRenderer renderer(&backend, &stateManager);

        // Allocate Camera UBO
        VkBuffer cameraUBO;
    VkDeviceMemory cameraUBOMemory;
    backend.createBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraUBO, cameraUBOMemory);

    std::cout << "Building 3D Voxel Scene for GPU Dispatch..." << std::endl;
    PersistentSceneManager sceneManager;
    SoSeparator* root = new SoSeparator;
    root->ref();
    int size = 20;
    for (int x = -size; x < size; x++) {
        for (int y = -size; y < size; y++) {
            SoSeparator* sep = new SoSeparator;
            SoTransform* t = new SoTransform;
            t->translation.setValue(x * 2.5f, 0, y * 2.5f);
            SoCube* c = new SoCube;
            sep->addChild(t);
            sep->addChild(c);
            root->addChild(sep);
        }
    }
    sceneManager.setSceneGraph(root);
    stateManager.upload(&sceneManager);
    uint32_t numElements = sceneManager.getNumTransforms();

    VulkanCullingSystem cullingSystem(&backend, &stateManager);
    std::cout << "Compiling Compute Culling Pipeline..." << std::endl;
    cullingSystem.init("src/rendering/shaders/culling.spv");
    cullingSystem.updateDescriptorSets(cameraUBO);

    std::cout << "Initializing Renderer for Swapchain..." << std::endl;
    renderer.init(backend.getSwapChainImageFormat(), backend.getSwapChainExtent(), "src/rendering/shaders/basic.vert.spv", "src/rendering/shaders/basic.frag.spv", cameraUBO);
    renderer.createFramebuffers(backend.getSwapChainImageViews());

    // Master Render Loop Pacing Objects
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    vkCreateSemaphore(backend.getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphore);
    vkCreateSemaphore(backend.getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphore);
    vkCreateFence(backend.getDevice(), &fenceInfo, nullptr, &inFlightFence);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = backend.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(backend.getDevice(), &allocInfo, &commandBuffer);

    CameraData camData{};
    double lastTime = glfwGetTime();

    std::cout << "\n[Entering Interactive Render Loop]" << std::endl;

    while (!glfwWindowShouldClose(window)) {
        double currentFrame = glfwGetTime();
        float dt = currentFrame - lastTime;
        lastTime = currentFrame;

        glfwPollEvents();
        cam.processInput(window, dt);

        float aspect = (float)WIDTH / (float)HEIGHT;
        cam.buildVP(aspect, 0.1f, 1000.0f, camData.viewProj);
        cam.extractFrustum(camData.viewProj, camData.frustumPlanes);
        camData.numElements = numElements;

        void* mapped;
        vkMapMemory(backend.getDevice(), cameraUBOMemory, 0, sizeof(CameraData), 0, &mapped);
        memcpy(mapped, &camData, sizeof(CameraData));
        vkUnmapMemory(backend.getDevice(), cameraUBOMemory);

        // 1. Frame Pacing: Wait for previous frame dispatch to release fence
        vkWaitForFences(backend.getDevice(), 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(backend.getDevice(), 1, &inFlightFence);

        // 2. Frame Pacing: Acquire next swapchain surface image
        uint32_t imageIndex;
        vkAcquireNextImageKHR(backend.getDevice(), backend.getSwapChain(), UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        // Reset & Record Command Buffer
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        // Step A: Dispatch GPU Culling Compute Shader
        cullingSystem.dispatchCulling(commandBuffer, numElements);

        // Step B: Submit strict Memory Barrier to ensure visibility output writes are finished
        VkMemoryBarrier drawBarrier{};
        drawBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        drawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &drawBarrier, 0, nullptr, 0, nullptr);

        // Step C: Render MDI Pipeline targeting Viewport
        renderer.bindAndDraw(commandBuffer, imageIndex, numElements);

        vkEndCommandBuffer(commandBuffer);

        // 3. Frame Pacing: Submit queues bounded by semaphores
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(backend.getGraphicsQueue(), 1, &submitInfo, inFlightFence);

        // 4. Frame Pacing: Inject rendered frame to display
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = {backend.getSwapChain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        
        vkQueuePresentKHR(backend.getPresentQueue(), &presentInfo);
    }

    vkDeviceWaitIdle(backend.getDevice());

    vkDestroySemaphore(backend.getDevice(), renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(backend.getDevice(), imageAvailableSemaphore, nullptr);
    vkDestroyFence(backend.getDevice(), inFlightFence, nullptr);
        vkDestroyBuffer(backend.getDevice(), cameraUBO, nullptr);
        vkFreeMemory(backend.getDevice(), cameraUBOMemory, nullptr);

        root->unref();
    }

    vkDestroySurfaceKHR(backend.getInstance(), surface, nullptr);
    backend.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
