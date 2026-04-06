# Phase 2: Compute Culling & GPU Storage Integration Tasks

This document contains the detailed breakdown of tasks for Phase 2 of the Coin3D Modernization Plan, focused on synchronizing the CPU-side Shadow ECS directly to the GPU and executing Frustum Culling natively on the device via Compute Shaders.

## 1. OpenGL 4.3 Context & Subsystem Initialization
- [ ] Ensure OpenGL 4.3+ profile initialization mapping is correctly toggled in `ModernGLBackend` or equivalent Coin renderer backend.
- [ ] Implement or verify OpenGL function loader bindings (e.g., `glew`, `glad`, or internal Coin3D `cc_glglue`) for core GL 4.3 features:
  - `glDispatchCompute`
  - `glBindBufferBase` (for Shader Storage Buffer Objects - SSBO)
  - `glBufferStorage` or `glNamedBufferStorage` (for zero-copy / persistent mapped buffers).

## 2. SSBO GPU Memory Allocation
- [ ] Create a `GPUStateManager` to mirror `PersistentSceneManager` arrays.
- [ ] Define C++ layouts tightly packing data to match GLSL `std430` layout alignment:
  - [ ] Transform Array buffer (`struct TransformData { mat4 matrix; };`).
  - [ ] Material Array buffer (`struct MaterialData { vec4 color; };`).
  - [ ] Bounding Box Array buffer (`struct BoundingBoxData { vec4 min; vec4 max; };`).
- [ ] Implement OpenGL Buffer allocation (`glGenBuffers` / `glBufferData`) generating the SSBOs for the structures above.

## 3. CPU to GPU Asynchronous Streaming
- [ ] Hook into the pre-render pass of `SoModernRenderAction` or equivalent frame dispatcher.
- [ ] Perform `glMapBufferRange` or fast `glBufferSubData` to stream modified continuous data from `PersistentSceneManager` into the SSBOs.
- [ ] Track "dirty" ranges inside the Shadow ECS to avoid re-uploading unchanged memory arrays (Optional Optimization).

## 4. Compute Shader: View Frustum Culling
- [ ] Write the GLSL Compute Shader file (`culling_compute.glsl`).
- [ ] Define the inputs inside the Compute Shader:
  - [ ] `layout(std430, binding = 0) readonly buffer BoundingBoxes`
  - [ ] `layout(std140, binding = 1) uniform CameraContext` (holding 6 view-frustum planes).
- [ ] Implement the box/frustum intersection algorithm checking all 8 corners across the 6 planes directly on the GPU.

## 5. Indirect Buffer Output
- [ ] Define the std430 `DrawElementsIndirectCommand` buffer struct on the GPU:
  ```glsl
  struct DrawElementsIndirectCommand {
      uint count;
      uint instanceCount;
      uint firstIndex;
      uint baseVertex;
      uint baseInstance;
  };
  ```
- [ ] Bind this struct array as `layout(std430, binding = 2) writeonly buffer DrawCommands`.
- [ ] Update the Compute Shader to conditionally append an indirect command into this buffer using `atomicAdd` on the command count, strictly if the bounding box passes the frustum test.

## 6. Execution and Verification
- [ ] Implement the C++ shader compilation sequence for the Compute Shader.
- [ ] Dispatch the compute shader (`glDispatchCompute(numShapes / workGroupSize, 1, 1)`).
- [ ] Place a GPU Memory Barrier (`glMemoryBarrier(GL_COMMAND_BARRIER_BIT)`) to ensure indirect buffers are fully written before draw.
- [ ] Write a visual unit test to verify shapes that are positioned strictly outside the camera view are truncated from the indirect draw output.
- [ ] Benchmark Compute Shader culling runtime mapped against legacy CPU traversal.
