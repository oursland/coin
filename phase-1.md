# Phase 1: PersistentSceneManager Integration Tasks

This document contains the detailed breakdown of tasks for Phase 1 of the Coin3D Modernization Plan, focused on building the Shadow ECS mapping layer.

## 1. Class Structure and Initialization
- [ ] Create `PersistentSceneManager` header (`PersistentSceneManager.h`) and source (`PersistentSceneManager.cpp`).
- [ ] Define the interface for initializing the manager with a root `SoNode` (e.g., `void setSceneGraph(SoNode* root)`).
- [ ] Implement the destructor to cleanly detach sensors and free ECS memory.

## 2. Structure of Arrays (SoA) Allocation
- [ ] Define the base data structures for the ECS arrays:
  - [ ] Transform Arrays (e.g., `float[16]` per matrix).
  - [ ] Material Arrays.
  - [ ] Bounding Box Arrays (`vec3` bounds).
- [ ] Implement initial linear array allocators for each data type.
- [ ] Create a hashing or mapping dictionary to bind a legacy `SoNode*` pointer to its specific array index.

## 3. Initial Scene Traversal and Mapping
- [ ] Implement a one-time hierarchical traversal algorithm to parse the input scene graph.
- [ ] Extract transforms from `SoTransform` nodes and allocate/populate mapping in the Transform Array.
- [ ] Extract material data from `SoMaterial` nodes and populate the Material Array.
- [ ] Extract shapes (`SoShape`) and their bounding boxes and populate the Bounding Box Array.
- [ ] Construct the flattened index topology representing the parent-child node relationships using array indices rather than pointers.

## 4. Sensor and Notification Integration
- [ ] Attach `SoNodeSensor` and/or `SoDataSensor` to the root node to listen for modifications.
- [ ] Implement the sensor callback function triggered upon scene changes.
- [ ] Add logic to parse the `SoNotList` within the callback to identify exactly which nodes were modified.

## 5. Dynamic Array Updating
- [ ] Map modified nodes retrieved from the `SoNotList` to their assigned array indices using the `SoNode*` dictionary.
- [ ] Implement memcpy/update functions for:
  - [ ] Updating modified transforms.
  - [ ] Updating modified materials.
- [ ] Add logic to handle structural changes:
  - [ ] Node insertions (allocating new array slots, updating hierarchy mappings).
  - [ ] Node removals (invalidating indices, managing holes or compacting arrays).

## 6. Testing and Validation
- [ ] Write logic to verify that zero modifications were required in `SoTransform.cpp` or sibling legacy modules.
- [ ] Create a basic test scene with dynamic updates (e.g., an animated `SoTransform`) to ensure array data mirrors the graph correctly in real-time.
- [ ] Benchmark overhead of the `PersistentSceneManager` synchronization vs native `SoNode` updates.
