# Unconstrained Data-Oriented Design (DOD) Implementation Plan for Coin3D

This implementation plan outlines the architecture for completely replacing Coin3D's Object-Oriented legacy structure with a native Entity-Component System (ECS). This approach intentionally **breaks Application Binary Interface (ABI) and Application Programming Interface (API) compatibility**, prioritizing raw performance, Cache-Locality (SoA), and parallel compute capabilities.

## User Review Required

> [!WARNING]
> **API Decimation:** Execution of this plan will unequivocally break compatibility with 100% of existing FreeCAD internal Coin3D code that relies on `SoNode`, `SoField`, or Open Inventor action traversal. Downstream clients will require entirely new APIs to interface with the core. 
> 
> Please review the `[DELETE]` sections carefully to understand the magnitude of this structural shift.

---

## Proposed Changes

### Core ECS Framework & Memory Strategy

The core foundation replaces all reference counting (`SoBase`), node polymorphism (`SoNode`), and granular fields (`SoField`) with an Entity Registry and flat Structure of Arrays (SoA).

#### [NEW] `include/Inventor/ECS/SoEntity.h`
- Define `SoEntityID` as `uint32_t`.
- Introduce generation hashing logic to safely reuse deleted IDs.

#### [NEW] `include/Inventor/ECS/SoRegistry.h`
- Implement the core ECS Manager.
- Provide contiguous memory block allocators (`std::vector` abstractions) for distinct component types (Transforms, Drawables, Hierarchy, Materials).
- Handle contiguous array packing on Entity deletion to avoid memory fragmentation over time.

---

### Component Data Layer (Structure of Arrays)

Replacing the embedded `SoField` properties scattered across isolated heap allocations. All structural properties are mapped precisely to the `SoEntityID` index.

#### [NEW] `include/Inventor/ECS/Components/SoTransformComponent.h`
- Flat SoA layout for matrix logic.
- `std::vector<SbVec3f> translations;` 
- `std::vector<SbRotation> rotations;`
- `std::vector<SbVec3f> scaleFactors;`
- `std::vector<SbMatrix> worldMatrices;`
- `std::vector<bool> isDirty;`

#### [NEW] `include/Inventor/ECS/Components/SoHierarchyComponent.h`
- Relational indices replacing `SoChildList`.
- `std::vector<SoEntityID> parentId;`
- `std::vector<SoEntityID> firstChildId;`
- `std::vector<SoEntityID> nextSiblingId;`
- `std::vector<SoEntityID> prevSiblingId;`

#### [NEW] `include/Inventor/ECS/Components/SoGeometryComponent.h`
- Mapping geometries statically in array bounds.
- Provides static offsets into Global Vertex / Index buffers for bindless multi-draw routines.

---

### Subsystem Execution Layer (Linear Compute Passes)

Execution replaces recursive tree recursion (`SoAction`, `SoGLRenderAction`) with linear execution algorithms.

#### [NEW] `src/ecs/systems/SoTransformSystem.cpp`
- **Topology Sorting:** Sorts `SoHierarchyComponent` data into a linear buffer where all parents are guaranteed to appear before their children natively.
- **Linear Pass:** Calculates `worldMatrices[child] = worldMatrices[parent] * localMatrix[child]` locally inside CPU caches traversing precisely $O(n)$ in one unbranched pass.

#### [NEW] `src/ecs/systems/SoVisibilitySystem.cpp`
- Replaces Box recursion. Passes a linear `std::vector<BoundingBox>` layout natively into a Vulkan Compute Shader to apply hierarchical Z-buffer occlusion and frustum culling completely asynchronously to CPU routines.

#### [NEW] `src/ecs/systems/SoRenderSystem.cpp`
- Directly interprets geometry array indices and compute outputs.
- Batches everything surviving the visibility culling directly into an indirect draw buffer array (`vkCmdDrawIndexedIndirect`). 

---

### File IO and Parsing 

Because `SoNode` instantiation via reflection strings (`SoType::createInstance`) is eliminated, scene description formats (`.iv` Inventor formats) require new compilation pipelines.

#### [NEW] `src/io/SoSceneImporter.cpp`
- Replaces legacy `.iv` string parsing trees.
- Reads `Separator` and `Transform` text tokens, translating them statically into batch Entity assignment queues sent to the `SoRegistry`.

---

### Legacy Annihilation (The Clean Sweep)

> [!CAUTION]
> The following represents the complete destruction of the Object-Oriented memory footprint of Open Inventor. 

#### [DELETE] `src/nodes/*` and `include/Inventor/nodes/*`
- Destruction of `SoNode.cpp`, `SoSeparator.cpp`, `SoTransform.cpp`, `SoGroup.cpp`.

#### [DELETE] `src/fields/*` and `include/Inventor/fields/*`
- Total removal of `SoFieldContainer.cpp`, `SoField.cpp`, `SoSFVec3f.cpp`, etc.
- Destruction of the macro reflection bindings (`SO_NODE_HEADER`, `SO_FIELD_INIT`).

#### [DELETE] `src/actions/*` and `include/Inventor/actions/*`
- Complete removal of `SoAction`, `SoGLRenderAction`, `SoHandleEventAction`.

---

## Open Questions

1. **Host Application Interactivity Interface:** FreeCAD heavily uses `SoHandleEventAction` passing mouse events down the pointer tree to manipulate objects. If `SoAction` is removed entirely, do we rewrite ray-casting to operate as an ECS System against the contiguous bounding boxes internally, or expose bounding box array bounds specifically for FreeCAD's internal picker to evaluate externally?
2. **Backwards Compatibility Format:** Do we strictly maintain the legacy `.iv` file parsing grammar and proxy it into ECS instantiation, or do we define a new standardized serialized geometry stream (e.g. leveraging `glTF` exclusively for structural caching)?
3. **Pacing of Phase Deployments:** Do we execute the entire node decimation simultaneously, or isolate it inside a CMake feature flag (`COIN_NATIVE_DOD_MODE`) to allow parallel build capability testing during the integration timeframe?

## Verification Plan

### Automated Tests
- Build verification ensuring `SoNode*` dependencies are eradicated from the entire codebase space.
- Run array benchmark frameworks (`benchmark-ecs-test`) parsing 500,000 instanced entities through `SoTransformSystem` to validate SIMD cache coherency performance multipliers compared to legacy `coin-ecs-lessons-learned.md` bounds.

### Manual Verification
- Deploy basic headless test binary validating Vulkan descriptor bindings onto Multi-Draw execution endpoints using the new SoA layout explicitly.
- Render test frames without `SoGLRenderAction` integration confirming that bindless ECS payload processing evaluates correctly visually.
