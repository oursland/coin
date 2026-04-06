# Phase 2: Vulkan Compute Culling & GPU Storage Integration Tasks

This document contains the detailed breakdown of tasks for Phase 2 of the Coin3D Modernization Plan, focused on synchronizing the CPU-side Shadow ECS directly to the GPU using Vulkan (and MoltenVK on macOS) to execute Frustum Culling natively on the device via Compute Shaders.

## 1. Vulkan Context & Subsystem Initialization
- [x] Initialize the Vulkan Instance (`vkCreateInstance`), Physical Device, and Logical Device inside a new `ModernVulkanBackend`.
- [x] Ensure macOS compatibility by enabling MoltenVK extensions (`VK_KHR_portability_enumeration`, `VK_EXT_metal_surface`).
- [x] Initialize Vulkan Memory Allocator (VMA) or raw allocation mechanisms for large GPU-side buffers.

## 2. Vulkan Storage Buffer Allocation
- [x] Create a `VulkanStateManager` to mirror `PersistentSceneManager` arrays.
- [x] Define C++ layouts tightly packing data to match GLSL `std430` layout alignment (compiled to SPIR-V):
  - [x] Transform Array buffer (`struct TransformData { mat4 matrix; };`).
  - [x] Material Array buffer (`struct MaterialData { vec4 color; };`).
  - [x] Bounding Box Array buffer (`struct BoundingBoxData { vec4 min; vec4 max; };`).
- [x] Implement Vulkan Buffer allocation (`vkCreateBuffer`, `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`) generating the device-local buffers for the structures above.

## 3. CPU to GPU Asynchronous Streaming
- [x] Hook into the pre-render pass or backend frame dispatcher.
- [x] Allocate a Staging Buffer (`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`).
- [x] Perform `vkMapMemory` to copy modified contiguous ECS data from `PersistentSceneManager` into the staging buffer, and dispatch a `vkCmdCopyBuffer` to upload to the high-speed device-local Storage Buffers.
- [x] Track "dirty" ranges inside the Shadow ECS to avoid re-uploading unchanged memory arrays (Optional Optimization).

## 4. Vulkan Compute Shader: View Frustum Culling
- [ ] Write the GLSL Compute Shader file (`culling_compute.glsl`) and compile it to SPIR-V using `glslangValidator`.
- [ ] Define the inputs inside the Compute Shader:
  - [ ] `layout(std430, binding = 0) readonly buffer BoundingBoxes`
  - [ ] `layout(std140, binding = 1) uniform CameraContext` (holding 6 view-frustum planes).
- [ ] Create Vulkan Descriptor Sets tying the Storage Buffers and Uniform Buffers to the compute pipeline bindings.
- [ ] Implement the box/frustum intersection algorithm checking all 8 corners across the 6 planes directly on the GPU.

## 5. Indirect Buffer Output
- [ ] Define the Indirect Draw buffer layout struct on the GPU:
  ```glsl
  struct VkDrawIndexedIndirectCommand {
      uint indexCount;
      uint instanceCount;
      uint firstIndex;
      int  vertexOffset;
      uint firstInstance;
  };
  ```
- [ ] Bind this struct array as `layout(std430, binding = 2) writeonly buffer DrawCommands`.
- [ ] Update the Compute Shader to conditionally append an indirect command into this buffer using `atomicAdd` on the command count, strictly if the bounding box passes the frustum test.

## 6. Execution and Verification
- [ ] Implement the Vulkan `VkPipeline` and `VkComputePipelineCreateInfo` to load the SPIR-V compute shader.
- [ ] Dispatch the compute shader natively (`vkCmdDispatch(cmdBuffer, numShapes / workGroupSize, 1, 1)`).
- [ ] Place a Pipeline Barrier (`vkCmdPipelineBarrier`) waiting for the compute shader to finish writing the indirect buffer before the fragment stage reads it.
- [ ] Write a visual unit test to verify shapes that are positioned strictly outside the camera view are truncated from the indirect draw output.
- [ ] Benchmark Compute Shader culling runtime mapped against legacy CPU traversal.
