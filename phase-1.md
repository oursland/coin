# Phase 1: PersistentSceneManager Integration Tasks

This document contains the detailed breakdown of tasks for Phase 1 of the Coin3D Modernization Plan, focused on building the Shadow ECS mapping layer.

## 1. Class Structure and Initialization
- [x] Create `PersistentSceneManager` header (`PersistentSceneManager.h`) and source (`PersistentSceneManager.cpp`).
- [x] Define the interface for initializing the manager with a root `SoNode` (e.g., `void setSceneGraph(SoNode* root)`).
- [x] Implement the destructor to cleanly detach sensors and free ECS memory.

## 2. Structure of Arrays (SoA) Allocation
- [x] Define the base data structures for the ECS arrays:
  - [x] Transform Arrays (e.g., `SbList<SbMatrix>`).
  - [x] Material Arrays (`SbList<SbColor>`).
  - [x] Bounding Box Arrays (`SbList<SbVec3f>` bounds).
- [x] Implement initial linear array allocators for each data type.
- [x] Create a hashing dictionary to bind a legacy `SoNode*` pointer to its specific array index. *(Implemented using Coin3D's internal `SbHash`)*

## 3. Initial Scene Traversal and Mapping
- [x] Implement a one-time hierarchical traversal algorithm to parse the input scene graph (`traverseNodeInit`).
- [x] Extract transforms from `SoTransform` nodes and allocate/populate mapping in the Transform Array.
- [x] Extract material data from `SoMaterial` nodes and populate the Material Array.
- [x] Extract shapes (`SoShape`) and their bounding boxes and populate the Bounding Box Array.
- [x] Construct the flattened index topology representing the parent-child node relationships using array indices rather than pointers.

## 4. Sensor and Notification Integration
- [x] Attach `SoNodeSensor` to the root node to listen for modifications (priority = 0).
- [x] Implement the sensor callback function triggered upon scene changes.
- [x] **[DISCOVERED]** Use `SoNodeSensor::getTriggerNode()` dynamically to locate the caller instead of parsing `SoNotList`, which is restricted due to node subclassing constraints.

## 5. Dynamic Array Updating
- [x] Map modified nodes retrieved from the sensor to their assigned array indices using the `SoNode*` dictionary (`transformMap`, `materialMap`).
- [x] Implement memcpy/update functions for:
  - [x] Updating modified transforms.
  - [x] Updating modified materials.
- [x] Add logic to handle structural changes:
  - [x] Node insertions (allocating new array slots, updating hierarchy mappings). *(Handled via full rebuild fallback)*
  - [x] Node removals (invalidating indices, managing holes or compacting arrays). *(Handled via full rebuild fallback)*

## 6. Testing, Validation, and Build System
- [x] Write logic to verify that zero modifications were required in `SoTransform.cpp` or sibling legacy modules.
- [x] Create a basic test scene with dynamic updates to ensure array data mirrors the graph correctly in real-time (`test-code/shadow-ecs/shadow-ecs-test.cpp`).
- [x] Benchmark overhead of the `PersistentSceneManager` synchronization vs native `SoNode` updates *(Yielded ~0.046 microseconds per sync)*.
- [x] **[DISCOVERED]** Integrate `PersistentSceneManager` into Coin3D CMake targets (`src/misc/CMakeLists.txt`).
- [x] **[DISCOVERED]** Resolve Coin internal umbrella header restrictions (replaced `SbLinear.h` with `SbMatrix.h` / `SbVec3f.h`).
