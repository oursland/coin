# Phase 3: GPU-Driven Indirect Rendering Tasks

This document contains the detailed breakdown of tasks for Phase 3 of the Coin3D Modernization Plan. The goal is to bind the dynamically generated `VkDrawIndexedIndirectCommand` metrics from Phase 2 into a formal Vulkan Graphics Pipeline, effectively eliminating the CPU from the actual geometric draw dispatch overhead entirely.

## 1. Headless Graphics Pipeline Infrastructure
- [ ] Create `VulkanRenderer.cpp` (and `.h`) integrating a headless rendering architecture.
- [ ] Create a `VkRenderPass` defining standard color and depth attachments suitable for offscreen rendering.
- [ ] Create a `VkPipelineLayout` factoring in descriptors for uniform buffers and vertex storage buffers.
- [ ] Construct a standard `VkPipeline` specifically configured for `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`.
- [ ] Write a minimal pass-through Vertex Shader (`basic.vert`) fetching instance transforms natively via `gl_InstanceIndex`.
- [ ] Write a minimal Fragment Shader (`basic.frag`) outputting flat/standard color data to the headless buffer.

## 2. Unified Geometry Storage (Bindless Preparation)
- [ ] Extract the basic structural geometry representation (e.g., standard cube indices and vertices).
- [ ] Pre-allocate a unified `VkBuffer` (Vertex Buffer) in the `VulkanStateManager` serving all indirect draw instances.
- [ ] Pre-allocate a unified `VkBuffer` (Index Buffer) matching the vertex definitions mapping.
- [ ] Modify `culling.comp` `VkDrawIndexedIndirectCommand` generation carefully assigning the correct `firstIndex` and indices count strictly mimicking the unified layout mapped structures.

## 3. Asynchronous Rendering Dispatch
- [ ] Prepare an automated execution sequence in `vulkan-test` bridging Compute and Graphics outputs:
  1. `vkCmdDispatch` (Shadow ECS Frustum checks)
  2. `vkCmdPipelineBarrier` (Synchronizing indirect writeonly metrics prior to draw consumption)
  3. `vkCmdBeginRenderPass` (Headless Context Initiation)
  4. `vkCmdBindPipeline` (Bind generated geometric states)
  5. `vkCmdDrawIndexedIndirect` (Multi-Draw execution driven dynamically from the `DrawCountBuffer` variable bounds!)
  6. `vkCmdEndRenderPass`
  
## 4. Hardware Verification & Execution Tracking
- [ ] Instantiate `VK_QUERY_TYPE_PIPELINE_STATISTICS` surrounding the command execution block strictly validating isolated GPU metrics.
- [ ] Compile and evaluate the readback output enforcing `inputAssemblyPrimitives` correctly equals `(Drawn Shapes) * 12` ensuring rendering occurs implicitly.
- [ ] Benchmark combined pure GPU times (Compute + Draw natively chained entirely async).
