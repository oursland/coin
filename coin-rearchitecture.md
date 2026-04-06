# Coin3D Scene Graph Re-architecture Plan

## Context

The Coin3D scene graph uses an OpenInventor-era architecture: a double-dispatch visitor pattern where actions traverse the entire scene graph depth-first, maintaining a state stack of ~126 element types. Every frame, the render action visits every node — function pointer dispatch, path stack manipulation, and potential state element copies per node.

For FreeCAD assemblies with 50K+ nodes this is a performance wall. The `unified-renderer` branch already introduced `SoModernRenderAction` which flattens the scene graph into a replayable draw list (`SoDrawList` of `SoRenderCommand` structs), eliminating per-frame GL calls per node. But the draw list is **rebuilt from scratch** on every scene graph change, and there is no spatial acceleration for culling.

This plan incrementally addresses these bottlenecks while preserving the OpenInventor API.

---

## Phase 1: Incremental Draw List Updates

**Goal**: Eliminate full scene graph re-traversal when only a subset of nodes change.

Currently, any `invalidateDrawList()` call discards the entire draw list and forces a complete re-traversal of the scene graph on the next frame. For a 50K-node scene, this costs 5-15ms even when only one node changed.

### Architecture

Introduce a **node-to-command mapping** so that when a node changes, only its commands are updated in place.

```
SoDrawList:
  commands[0..N]              // flat array of SoRenderCommand
  nodeCommandMap:             // SoNode* → (startIndex, count) in commands[]
  dirtyNodes: set<SoNode*>   // nodes needing re-traversal
```

On notification, instead of `invalidateDrawList()`, mark the originating node dirty. Before render, re-traverse only dirty subtrees and splice their updated commands into the draw list.

### Tasks

- [ ] **1.1 Build node-to-command index during traversal**
  - During `SoModernRenderAction` traversal, record which node emitted which command range
  - Store as a map from `SoNode*` → `(firstCommandIndex, commandCount)` in `SoDrawList`
  - Each shape node (SoBrepFaceSet, SoIndexedFaceSet, etc.) typically emits 1-3 commands (faces, edges, points)
  - Group nodes (SoSeparator) map to the range covering all their descendant commands
  - **Key file**: `src/actions/SoModernRenderAction.cpp` — extend `beginTraversal()` to populate the map

- [ ] **1.2 Replace invalidateDrawList() with markNodeDirty()**
  - Add `SoDrawList::markDirty(SoNode* node)` that inserts into a dirty set
  - Modify the node sensor callback in `SoRenderManager` to call `markDirty()` instead of `invalidateDrawList()` for non-structural changes (field modifications on existing nodes)
  - Structural changes (add/remove child) still require broader invalidation (see 1.5)
  - **Key file**: `src/rendering/SoRenderManager.cpp` — node sensor handler

- [ ] **1.3 Implement partial re-traversal**
  - Add `SoModernRenderAction::traverseSubtree(SoNode* root)` that:
    1. Reconstructs the state at the subtree's position by replaying the parent chain's state elements (cached from initial traversal)
    2. Traverses the subtree, collecting new commands into a temporary buffer
    3. Returns the new command list for that subtree
  - **Challenge**: State reconstruction. The state at any node depends on all ancestor state nodes. Options:
    - (a) Cache the full `SoState` snapshot at each SoSeparator during traversal (memory cost but O(1) restore)
    - (b) Re-traverse from root but skip non-dirty subtrees (simpler but still O(depth) per update)
    - Start with (b) as it's simpler and still avoids traversing large sibling subtrees

- [ ] **1.4 Splice updated commands into draw list**
  - Replace the old command range `[startIndex, startIndex+count)` with the new commands
  - If command count changed, shift subsequent commands and update the node-to-command map
  - Alternatively, use a slot-based allocator where each node "owns" a fixed-size slot, with overflow into a secondary list — avoids shifting
  - Update pick LUT after splice (call `buildPickLUT()` on affected range only, or mark LUT dirty)
  - **Key file**: `src/rendering/SoModernIR.cpp`

- [ ] **1.5 Handle structural changes (add/remove node)**
  - When `SoNotRec::operationType` is `GROUP_ADDCHILD`, `GROUP_REMOVECHILD`, or `GROUP_REMOVEALLCHILDREN`:
    - Invalidate the parent separator's entire command range
    - Insert/remove command slots in the draw list
    - Update the node-to-command map for all shifted commands
  - For initial implementation, structural changes can fall back to full re-traversal
  - Optimize later once field-change partial updates are proven

- [ ] **1.6 Invalidate GPU caches for updated commands only**
  - Currently `invalidateDrawList()` clears the entire VBO/VAO cache in `SoModernGLBackend`
  - With partial updates, only clear cache entries for commands whose geometry changed
  - Retain cache entries for unchanged commands
  - **Key file**: `src/rendering/SoModernGLBackend.cpp` — cache keyed by (positions ptr, indices ptr)

- [ ] **1.7 Update pick LUT incrementally**
  - `buildPickLUT()` currently rebuilds the entire LUT from scratch
  - With partial command updates, the LUT entries for unchanged commands are still valid
  - Either rebuild only the affected range, or mark the LUT dirty and rebuild lazily on next pick
  - **Key file**: `src/rendering/SoModernIR.cpp` — `buildPickLUT()`

---

## Phase 2: Spatial Index for Frustum Culling

**Goal**: Skip rendering commands for objects outside the view frustum. Currently all commands are rendered every frame regardless of visibility.

The `bvh-pick` branch contains a proven three-tier BVH system (per-shape, child, scene) built for ray picking. The same data structures and SAH build algorithm can be adapted for frustum culling of draw commands.

### Existing BVH Architecture (from bvh-pick branch)

The branch has three BVH levels, all using SAH (Surface Area Heuristic) with 16-bin partitioning and 32-byte cache-friendly nodes:

- **SoBVHCache** (`src/caches/SoBVHCache.h/.cpp`): Per-shape triangle-level BVH. 32-byte `SbBVHNode` with depth-first layout, left child at nodeIdx+1.
- **SoChildBVH** (`src/caches/SoChildBVH.h/.cpp`): Per-separator hierarchy over direct children. Skips state-affecting nodes (always traversed).
- **SoSceneBVH** (`src/caches/SoSceneBVH.h/.cpp`): Global shape-level BVH in world space. Singleton per scene root, lazy build, thread-safe.

### Tasks

- [ ] **2.1 Merge bvh-pick BVH infrastructure into unified-renderer**
  - Cherry-pick or merge the BVH source files: `SoBVHCache`, `SoChildBVH`, `SoSceneBVH`
  - Resolve any conflicts with unified-renderer changes to `SoShape.cpp`, `SoSeparator.cpp`
  - Verify ray picking still works after merge
  - **Source branch**: `bvh-pick`

- [ ] **2.2 Add frustum-AABB intersection test**
  - The existing `SoSceneBVH` uses ray-AABB slab tests (`rayIntersectsAABB`)
  - Add `frustumIntersectsAABB(SbBox3f bbox, SbPlane frustumPlanes[6])` utility
  - Standard 6-plane test: if the bbox is fully outside any plane, reject
  - Place in a shared utility header (e.g., `src/caches/SoBVHUtil.h`)

- [ ] **2.3 Build command-level BVH from draw list**
  - After `SoModernRenderAction` builds the draw list, construct a BVH over command bounding boxes
  - Each leaf = one `SoRenderCommand` (or a small cluster of spatially-adjacent commands)
  - Reuse `SoSceneBVH`'s SAH build algorithm, adapted for `SoRenderCommand` world-space AABBs
  - Store as `SoDrawListBVH` alongside `SoDrawList`
  - Rebuild when draw list changes (piggyback on Phase 1's incremental update — rebuild BVH after splice)

- [ ] **2.4 Add frustum query to draw list BVH**
  - `SoDrawListBVH::queryFrustum(SbPlane planes[6]) → std::vector<bool> visible`
  - Returns a visibility mask over the command array
  - Stack-based traversal matching existing BVH query pattern

- [ ] **2.5 Integrate frustum culling into SoModernGLBackend::render()**
  - Before rendering, query the draw list BVH with current view-projection frustum planes
  - Skip commands where `visible[i] == false`
  - Apply to both visual pass and ID pick pass
  - Use `buildSortedOrder()` — sorted rendering already uses an indirection array; filter it
  - **Key file**: `src/rendering/SoModernGLBackend.cpp`

- [ ] **2.6 Camera-aware BVH query caching**
  - If camera hasn't changed since last query, reuse the visibility mask
  - Piggyback on existing camera dirty tracking in `SoIDPickBuffer`
  - Only re-query when camera moves or draw list changes
  - **Key file**: `src/rendering/SoRenderManager.cpp` — existing `pickBufferDirty` pattern

- [ ] **2.7 Incremental BVH refit on draw list changes**
  - When Phase 1's partial update modifies commands, refit the BVH rather than full rebuild
  - BVH refit: walk leaf-to-root updating bounding boxes — O(log n) per changed command
  - Full rebuild only on structural changes (add/remove commands that change leaf count)

---

## Phase 3: Notification Batching

**Goal**: Coalesce rapid-fire scene graph notifications into a single draw list update per frame.

Currently each field change fires an immediate notification chain up to the root, potentially triggering multiple `invalidateDrawList()` / `scheduleRedraw()` calls per frame. During scene load, undo, or paste, thousands of notifications fire in rapid succession.

### Tasks

- [ ] **3.1 Add a dirty node collector to SoRenderManager**
  - Add `std::unordered_set<SoNode*> pendingDirtyNodes` to `SoRenderManager`
  - When a node sensor fires, insert the node into `pendingDirtyNodes` instead of immediately invalidating
  - Call `scheduleRedraw()` once (idempotent)

- [ ] **3.2 Process dirty set before render**
  - At the start of `renderModern()`, before traversal/backend render:
    1. Drain `pendingDirtyNodes`
    2. For each dirty node, call Phase 1's partial re-traversal
    3. Clear the set
    4. If the set was empty, skip re-traversal entirely (common case: camera-only change)
  - **Key file**: `src/rendering/SoRenderManager.cpp`

- [ ] **3.3 Classify notification types**
  - Extend the node sensor to distinguish:
    - **Camera changes**: only update view matrices and re-query frustum BVH — no draw list work
    - **Material/transform changes**: partial draw list update for affected subtree
    - **Structural changes**: full draw list rebuild (fallback)
    - **Selection/highlight changes**: direct draw list mutation (already implemented)
  - Use `SoNotRec::operationType` and sensor trigger node type to classify
  - **Key file**: `src/rendering/SoRenderManager.cpp` — existing sensor filtering logic

- [ ] **3.4 Batch structural changes during scene load**
  - Detect "burst mode" when many structural changes arrive in one event loop tick
  - Defer all processing until the burst ends (e.g., via a zero-timer or end-of-event-loop callback)
  - FreeCAD already calls `invalidateDrawList()` explicitly from `slotFinishRestoreDocument()` — ensure this remains the trigger for scene load, not individual node adds

- [ ] **3.5 Suppress duplicate notifications**
  - If a node is already in `pendingDirtyNodes`, skip re-insertion and notification propagation
  - Use the `SoNotList::stamp` unique ID to detect re-notification of the same event
  - Track `lastProcessedStamp` per node to avoid redundant processing

---

## Phase 4: Data Layout Optimization (SOA)

**Goal**: Improve CPU cache utilization and prepare data for GPU buffer upload by converting the draw list from Array-of-Structs (AOS) to Struct-of-Arrays (SOA).

Currently `SoRenderCommand` is a large struct (~200+ bytes) containing geometry, material, state, pick data, and selection data. Iterating over one field (e.g., model matrices for culling) touches all fields due to struct stride.

### Tasks

- [ ] **4.1 Profile current cache behavior**
  - Use Instruments / perf / vtune to measure L1/L2 cache miss rates during:
    - Draw list iteration in `SoModernGLBackend::render()`
    - Frustum culling in BVH query
    - Pick LUT building
  - Establish baseline metrics to validate SOA conversion impact

- [ ] **4.2 Define SOA draw list structure**
  - Split `SoRenderCommand` fields into parallel arrays:
    ```
    struct SoDrawListSOA {
        // Hot path: rendered every frame
        std::vector<SbMatrix>       modelMatrices;
        std::vector<SoGeometryDesc> geometries;
        std::vector<SoMaterialDesc> materials;   // diffuse, ambient, specular, shininess
        std::vector<uint32_t>       stateFlags;  // blend, depth, polygon offset, etc.

        // Warm path: used during culling
        std::vector<SbBox3f>        worldBounds;

        // Cold path: used only during picking or selection
        std::vector<SoPickData>     pickData;
        std::vector<SoSelectionData> selectionData;
        std::vector<SoFullPath*>    commandPaths;
    };
    ```
  - Index `i` across all arrays refers to the same logical command

- [ ] **4.3 Convert SoModernRenderAction to emit SOA**
  - During traversal, append to individual arrays instead of constructing `SoRenderCommand` structs
  - Maintain the same command index semantics
  - Update `SoDrawList` API to provide array accessors instead of struct accessors

- [ ] **4.4 Convert SoModernGLBackend to consume SOA**
  - Render loop iterates `modelMatrices[i]`, `geometries[i]`, `materials[i]` in lockstep
  - Hot arrays (matrices, geometries, materials) are contiguous — better prefetching
  - Cold arrays (pick data, selection data) untouched during render — no cache pollution
  - **Key file**: `src/rendering/SoModernGLBackend.cpp`

- [ ] **4.5 Convert frustum culling to use SOA bounds array**
  - BVH leaf nodes reference indices into `worldBounds[]` array
  - Frustum query iterates bounds contiguously — ideal for SIMD
  - Consider SIMD 4-wide frustum-AABB test (test 4 boxes against one plane simultaneously)

- [ ] **4.6 Convert pick LUT to use SOA pick data**
  - `buildPickLUT()` iterates only `pickData[]` and `geometries[]` (face counts)
  - Avoids touching material/matrix/selection data during LUT build

- [ ] **4.7 Prepare GPU upload buffers**
  - SOA arrays map directly to GPU SSBOs:
    - `modelMatrices[]` → transform SSBO
    - `materials[]` → material SSBO
    - `worldBounds[]` → culling SSBO
  - Upload only arrays that changed since last frame (dirty tracking per array)
  - This is the foundation for Phase 5's GPU-driven rendering

---

## Phase 5: GPU-Driven Rendering (Future)

**Goal**: Move culling and draw command generation to GPU compute shaders. CPU uploads scene deltas; GPU does visibility + draws.

This phase requires compute shaders and indirect draw, which are not available via macOS OpenGL (capped at 4.1 Core Profile). A new GPU API backend is required.

### Platform Considerations

**The Multi-Draw Indirect (MDI) gap on Metal:**

| Feature | Vulkan (Linux/Win) | D3D12 (Win) | Metal (macOS) |
|---------|-------------------|-------------|---------------|
| Compute shaders | Native | Native | Native |
| Storage buffers | Native | Native | Native |
| Single indirect draw | Native | Native | Native |
| Multi-draw indirect | Native | Native | **No native support** |

Metal lacks native MDI. Both wgpu and MoltenVK emulate it via a CPU loop over individual `draw_primitives_indirect` calls — benchmarks show ~5x slower than native MDI on Vulkan/D3D12. Metal does have Indirect Command Buffers (ICBs) for GPU-driven dispatch, but these are Metal-specific and not exposed through cross-platform abstractions.

**Backend options:**

| Approach | Pros | Cons |
|----------|------|------|
| **Dawn (WebGPU, C++)** | Single codebase, three platforms; native C++ debugging; strong validation; closest to stable webgpu.h | MDI emulated on Metal; no persistent mapped buffers; no bindless/mesh shaders; heavy build (Chromium deps) |
| **wgpu-native (WebGPU, C API)** | Single codebase; pre-compiled binaries; lighter than Dawn | MDI emulated on Metal; C API ~2 years behind spec; Rust internals harder to debug from C++ |
| **Vulkan + MoltenVK** | More control than WebGPU; proven path (Valve/Proton); one Vulkan codebase | Same MDI emulation gap on Metal; MoltenVK translation overhead |
| **Vulkan (Linux/Win) + native Metal (macOS)** | Best performance everywhere; Metal ICBs for true GPU-driven draws | Two backend implementations to maintain |

**Recommended approach: Material-bucketed indirect draw (avoids MDI entirely)**

Instead of one draw per command via MDI, use compute shaders to compact visible geometry into per-material-bucket index buffers. Each bucket is dispatched with a single `drawIndexedIndirect`. This works identically on all backends:

1. Compute: frustum cull → visibility buffer
2. Compute: compact visible command indices into per-bucket draw lists (atomic append)
3. Draw: one `drawIndexedIndirect` per material bucket (typically 10-50 buckets, not thousands of draws)

This sidesteps the MDI gap and is arguably more efficient — fewer draw calls, better GPU occupancy.

### Tasks

- [ ] **5.1 Choose and integrate GPU API backend**
  - Evaluate Dawn vs wgpu-native vs direct Vulkan+Metal for Coin's build/dependency constraints
  - Implement `SoRenderBackend` subclass for the chosen API
  - Must support: compute shaders, storage buffers, indirect draw, atomic operations
  - Wire into existing `SoRenderManager` backend selection
  - **Key file**: `src/rendering/SoRenderBackend.h` — abstract interface

- [ ] **5.2 Design GPU scene buffer layout**
  - Define storage buffer layouts matching Phase 4's SOA arrays:
    - Transform buffer: `mat4 modelMatrices[MAX_COMMANDS]`
    - Material buffer: `MaterialDesc materials[MAX_COMMANDS]`
    - Bounds buffer: `vec3 bboxMin[MAX_COMMANDS], vec3 bboxMax[MAX_COMMANDS]`
    - Draw params buffer: `DrawDesc draws[MAX_COMMANDS]` (vertex offset, index count, etc.)
    - Visibility buffer: `uint visible[MAX_COMMANDS]`
  - Use WGSL `var<storage>` (WebGPU) or `std430` layout (Vulkan) depending on backend choice

- [ ] **5.3 Implement GPU frustum culling compute shader**
  - Input: bounds buffer + 6 frustum planes (uniform)
  - Output: visibility buffer (1/0 per command)
  - Dispatch: `ceil(commandCount / 256)` workgroups, 256 threads each
  - Each thread tests one command's AABB against all 6 frustum planes

- [ ] **5.4 Implement material-bucketed draw compaction**
  - Compute shader reads visibility buffer and material IDs
  - Atomic-appends visible command indices into per-bucket output buffers
  - Writes one `DrawIndexedIndirectCommand` per bucket with compacted count
  - Typically 10-50 buckets (one per unique material/state combination)
  - Avoids MDI entirely — works on Metal, Vulkan, and D3D12 equally

- [ ] **5.5 Implement delta upload**
  - Track which commands changed since last frame (from Phase 1's dirty tracking)
  - Upload only changed entries in transform/material/bounds buffers
  - Use staging buffers with explicit copy (WebGPU) or `vkCmdUpdateBuffer` / persistent mapping (Vulkan)

- [ ] **5.6 Implement GPU-driven ID pick pass**
  - Same culling compute shader, but render visible commands with pick shader
  - Pick shader writes per-fragment command ID + element ID to pick buffer
  - Single indirect draw per bucket, shared geometry buffers from visual pass
  - Replaces current per-command ID VAO rendering in `SoIDPickBuffer`

- [ ] **5.7 Shader translation / porting**
  - Port current GLSL 1.20 Blinn-Phong shader to WGSL (if WebGPU) or GLSL 4.50 / SPIR-V (if Vulkan)
  - Port ID pick shader
  - Add compute shaders for culling and draw compaction (new)
  - Consider Naga (WGSL→SPIR-V→MSL→HLSL) for cross-backend shader compilation if using WebGPU

---

## Performance Expectations

| Phase | Metric | Before | After (estimated) |
|-------|--------|--------|--------------------|
| 1 | Draw list rebuild on single node change | 5-15ms (full traversal) | 0.1-1ms (subtree only) |
| 2 | Render time for partially visible scene | O(n) all commands | O(visible) + O(log n) cull |
| 3 | Scene load draw list rebuilds | N rebuilds (one per node) | 1 rebuild (batched) |
| 4 | Cache misses during render loop | High (200B stride) | Low (contiguous hot data) |
| 5 | CPU render overhead | O(n) per frame | O(1) per frame (GPU does work) |

## Dependencies Between Phases

```
Phase 1 (Incremental Updates)
    ↓
Phase 2 (Spatial Index) ← merges bvh-pick branch
    ↓
Phase 3 (Notification Batching) — can start in parallel with Phase 2
    ↓
Phase 4 (SOA Layout) — benefits from Phase 1-3 being stable
    ↓
Phase 5 (GPU-Driven) — requires Phase 4 SOA
    ├── 5.1 (Backend choice) gates all other Phase 5 tasks
    ├── 5.3 (Culling shader) + 5.4 (Draw compaction) are the core compute work
    └── 5.7 (Shader porting) can start as soon as 5.1 is decided
```

Phases 1-3 are where the largest practical gains are. Phase 4 is a code quality / preparation step. Phase 5 is the long-term vision requiring a new GPU API backend — the material-bucketed draw compaction approach avoids the Metal MDI gap and works consistently across all platforms.
