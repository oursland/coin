# Phase 5: Benchmark Evaluation (Legacy vs Modern)

This phase quantifies the architectural improvements of the modern ECS GPU-driven pipeline versus traditional recursive `SoNode` pointer-chasing traversal. We will construct a headless benchmark utility measuring CPU payload latencies and driver dispatch throughputs.

## Checklist

- [ ] **1. Headless Render Initialization Context**
  - [ ] Abstract out all GUI/window context creation to spawn an invisible surface (or explicit completely headless pipeline).
  - [ ] Initialize Coin3D using `SoDB::init()` without bringing up heavy dependencies (e.g. `SoQt`).

- [ ] **2. Legacy Pipeline (`SoGLRenderAction`) Calibration**
  - [ ] Instantiate `SoOffscreenRenderer` to manage an implicit legacy OpenGL context and FBO binding.
  - [ ] Develop `std::chrono` timers enveloping an `SoGLRenderAction::apply(root)` invocation mapping pure hierarchical check+draw latency mapping.

- [ ] **3. Massive Tree Generation Framework**
  - [ ] Design an arbitrary depth generation utility that emits deeply nested `SoSeparator`, `SoTransform`, and `SoShape` graphs exceeding 120,000 bounds.
  - [ ] Inject a uniform standard variation mechanism enabling predictable node changes (e.g. matrix alterations) evaluating runtime incremental "commit" bounds.

- [ ] **4. Modern Vulkan ECS Rendering Path**
  - [ ] Feed the exact same 120k+ node tree generated into the `PersistentSceneManager`.
  - [ ] Time the shadow `upload()` boundary independently from total structure mapping.
  - [ ] Enclose the Compute `Frustum Culling` Dispatch and the `Multi-Draw Indirect (MDI)` Execute buffer within standard CPU ticks mapped around `vkQueueWaitIdle(queue)` to definitively score driver submittal times.

- [ ] **5. Metric Reporting & Formatting**
  - [ ] Collate the time points across multiple execution loops to smooth warmup latency anomalies.
  - [ ] Output a definitive structure comparing `Offline Render` times versus `Vulkan Multi-Dispatch` times directly to standard output.
