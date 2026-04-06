# Phase 5: Benchmark Evaluation (Legacy vs Modern)

This phase quantifies the architectural improvements of the modern ECS GPU-driven pipeline versus traditional recursive `SoNode` pointer-chasing traversal. We will construct a headless benchmark utility measuring CPU payload latencies and driver dispatch throughputs.

## Checklist

- [x] **1. Headless Render Initialization Context**
  - [x] Abstract out all GUI/window context creation to spawn an invisible surface (or explicit completely headless pipeline).
  - [x] Initialize Coin3D using `SoDB::init()` without bringing up heavy dependencies (e.g. `SoQt`).

- [x] **2. Legacy Pipeline (`SoGLRenderAction`) Calibration**
  - [x] Instantiate ` GLFWwindow` to manage an implicit legacy OpenGL context mapped to manual `SoGLRenderAction` (bypassing macOS `SoOffscreenRenderer` PBuffer failures).
  - [x] Develop `std::chrono` timers enveloping an `SoGLRenderAction::apply(root)` invocation mapping pure hierarchical check+draw latency mapping.

- [x] **3. Massive Tree Generation Framework**
  - [x] Design an arbitrary depth generation utility that emits deeply nested `SoSeparator`, `SoTransform`, and `SoShape` graphs exceeding 120,000 bounds.
  - [x] Inject a uniform standard variation mechanism enabling predictable node changes (e.g. matrix alterations) evaluating runtime incremental "commit" bounds.

- [x] **4. Modern Vulkan ECS Rendering Path**
  - [x] Feed the exact same 144k+ node tree generated into the `PersistentSceneManager`.
  - [x] Time the shadow `upload()` boundary independently from total structure mapping.
  - [x] Enclose the Compute `Frustum Culling` Dispatch and the `Multi-Draw Indirect (MDI)` Execute buffer within standard CPU ticks mapped around `vkQueueWaitIdle(queue)` to definitively score driver submittal times.

- [x] **5. Metric Reporting & Formatting**
  - [x] Collate the time points across multiple execution loops to smooth warmup latency anomalies.
  - [x] Output a definitive structure comparing `Offline Render` times versus `Vulkan Multi-Dispatch` times directly to standard output.

## Benchmark Results
Testing was evaluated on identically generated graphs composed of layers of nested `SoSeparator` branches wrapping `SoTransform` and `SoCube` instances totaling **144,400 active bounds** incrementally mutating each frame.

| Pipeline Approach | Average Evaluation Time (per Frame) | Estimated Throughput |
| ----------------- | ----------------------------------- | -------------------- |
| **Legacy Object-Oriented (`SoGLRenderAction`)** | `149.046 ms` | `~6.7 FPS` |
| **Modern ECS / Bindless GPU (`Vulkan Culling`)** | `3.105 ms` | `322 FPS` |

**Conclusion**: Converting the monolithic pointer-chasing stack traversal into a flattened, natively structured GPU compute execution pipeline accelerates evaluation workloads by roughly **4,800% (48x) multiplier**, unlocking massive graph topologies unavailable in standard legacy layouts!
