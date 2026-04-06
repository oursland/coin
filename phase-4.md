# Phase 4: Interactive GUI Testing Tasks

The goal of this phase is to execute the completed compute culling and multi-draw indirect pipelines against a live visual Vulkan Framebuffer hooked into an OS Window via GLFW. 

## 1. OS Window & Vulkan Surface Initialization
- [x] Add GLFW3 linkage to `CMakeLists.txt`.
- [x] Initialize `glfwInit()` and construct a `GLFWwindow*` in `vulkan-gui-test.cpp`.
- [x] Initialize the Vulkan `VkSurfaceKHR` using `glfwCreateWindowSurface()`.
- [x] Update `ModernVulkanBackend` to select a GPU providing dual Graphics & Present Queue Family support.

## 2. Swapchain Integration
- [x] Query Native Swapchain Support (Formats, Presentation Modes, Capabilities).
- [x] Instantiate `VkSwapchainKHR` targeting `VK_PRESENT_MODE_FIFO_KHR` or `MAILBOX`.
- [x] Extract Swapchain `VkImage` views.
- [x] Build Swapchain `VkFramebuffer` arrays integrating Depth Buffering.

## 3. Dynamic Rendering Pipeline
- [x] Extract hardcoded `HEADLESS_WIDTH` out of `VulkanRenderer` to utilize the dynamic Swapchain Extent.
- [x] Ensure the MVP matrix is uploaded into the standard Uniform Descriptors array allowing Vertex transformation.
- [x] Configure `VK_DYNAMIC_STATE_VIEWPORT` and `VK_DYNAMIC_STATE_SCISSOR`.

## 4. Input & Interactive Frustum Engine
- [x] Intercept GLFW Mouse and Keyboard callbacks (`W A S D` and mouse looking).
- [x] Construct the dynamic View/Projection math structures.
- [x] Compute all 6 bounding Frustum Planes from the combined Projection*View matrix dynamically on the CPU.
- [x] Bind dynamic data updating the `CameraData` Unified Storage Buffer prior to issuing the Dispatch.

## 5. Master Render Loop & Frame Pacer
- [ ] Produce `VkSemaphore` tracking `imageAvailable` and `renderFinished`.
- [ ] Wrap frame execution within a blocking `VkFence`.
- [ ] Implement `vkAcquireNextImageKHR` followed by `vkQueueSubmit` (triggering Culling -> MDI rendering seq).
- [ ] Request standard `vkQueuePresentKHR`.
