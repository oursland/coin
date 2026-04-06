# Phase 6: Material and Lighting ECS Integration

This phase integrates Open Inventor's legacy state-driven Material and Light model directly into the bindless ECS storage blocks.

## Checklist

- [ ] **1. Structural Material Parsing (`PersistentSceneManager`)**
  - [ ] Implement state tracking for the "Active Material Index" recursively propagated during `traverseNodeInit`.
  - [ ] Initialize an `SbList<uint32_t> shapeMaterialMap` allocating exactly 1 index parallel to every `SoShape`/`Transform` pair.
  - [ ] Aggregate `SoMaterial` instances exclusively into an AoS array `SbList<MaterialData>` storing ambient, diffuse, specular, and emissive colors.

- [ ] **2. Lighting Hierarchy Parsing**
  - [ ] Expand traversing logic to parse `SoDirectionalLight` and `SoPointLight` and isolate their vectors and color intensities.
  - [ ] Build a consolidated AoS layout defining `LightData` for global shader access.

- [ ] **3. Buffer Allocation (`VulkanStateManager`)**
  - [ ] Request allocations for `MaterialSSBO` mapping the `MaterialData` memory sequentially onto the GPU.
  - [ ] Request allocations for `ShapeMaterialMapSSBO` enabling index tracking for instances dynamically.
  - [ ] Allocate a single `LightUBO` defining the active scene lights up to a predefined limit (e.g. 8 lights).

- [ ] **4. Fragment Pipeline Integration**
  - [ ] Modify `basic.vert` to inject the native `gl_InstanceIndex` perfectly out to the Fragment Shader (`flat out uint in_InstanceID`).
  - [ ] Reconfigure `basic.frag` mapping the Descriptor Sets: Set 0 Binding 1-3 (for ECS structures).
  - [ ] Utilize `in_InstanceID` explicitly to retrieve `materialID = ShapeMaterialMapSSBO[in_InstanceID]` natively resolving fragments per sub-draw without driver calls!
  - [ ] Execute `LightUBO` calculations extracting the standard Light equations per pixel fragment producing Phong lit geometries.

- [ ] **5. Verification and Interactive Demo**
  - [ ] Expand `vulkan-gui-test.cpp` instantiating colored `SoCube` fields.
  - [ ] Instantiate dynamic runtime manipulation modifying materials ensuring pure `SoNotList` ECS memory syncs alter the fragment hues synchronously.
