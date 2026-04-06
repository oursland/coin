#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Inventor/ModernVulkanBackend.h>
#include <Inventor/VulkanRenderer.h>
#include <Inventor/VulkanStateManager.h>
#include <iostream>
#include <cmath>
#include <cstring>

struct CameraData {
    float viewProj[16];
    float frustumPlanes[6][4];
    uint32_t numElements;
    uint32_t padding[3];
};

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
        // Minimal A/D strafe later...
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
};

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

    // Allocate Camera UBO
    VkBuffer cameraUBO;
    VkDeviceMemory cameraUBOMemory;
    backend.createBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraUBO, cameraUBOMemory);

    std::cout << "Initializing Renderer for Swapchain..." << std::endl;
    // Provide absolute or relative paths to the compiled shaders in the build tree
    renderer.init(backend.getSwapChainImageFormat(), backend.getSwapChainExtent(), "build/src/rendering/shaders/basic.vert.spv", "build/src/rendering/shaders/basic.frag.spv", cameraUBO);
    renderer.createFramebuffers(backend.getSwapChainImageViews());

    std::cout << "Successfully generated " << backend.getSwapChainImageViews().size() << " Swapchain Images and Framebuffers!" << std::endl;

    Camera cam;
    CameraData camData{};
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double currentFrame = glfwGetTime();
        float dt = currentFrame - lastTime;
        lastTime = currentFrame;

        glfwPollEvents();
        cam.processInput(window, dt);

        float aspect = (float)WIDTH / (float)HEIGHT;
        cam.buildVP(aspect, 0.1f, 1000.0f, camData.viewProj);
        cam.extractFrustum(camData.viewProj, camData.frustumPlanes);
        camData.numElements = 0; // Set to actual count when geometry binds are finalized!

        void* mapped;
        vkMapMemory(backend.getDevice(), cameraUBOMemory, 0, sizeof(CameraData), 0, &mapped);
        memcpy(mapped, &camData, sizeof(CameraData));
        vkUnmapMemory(backend.getDevice(), cameraUBOMemory);

        // Frame pacing... acquire, and draw goes here...
    }

    vkDeviceWaitIdle(backend.getDevice());

    vkDestroyBuffer(backend.getDevice(), cameraUBO, nullptr);
    vkFreeMemory(backend.getDevice(), cameraUBOMemory, nullptr);

    vkDestroySurfaceKHR(backend.getInstance(), surface, nullptr);
    backend.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
