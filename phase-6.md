# Phase 6: Material and Lighting ECS Integration

This phase integrates Open Inventor's legacy state-driven Material and Light model directly into the bindless ECS storage blocks.

## Checklist

- [x] **1. Structural Material Parsing (`PersistentSceneManager`)**
  - [x] Implement state tracking for the "Active Material Index" recursively propagated during `traverseNodeInit`.
  - [x] Initialize an `SbList<uint32_t> shapeMaterialMap` allocating exactly 1 index parallel to every `SoShape`/`Transform` pair.
  - [x] Aggregate `SoMaterial` instances exclusively into an AoS array `SbList<MaterialData>` storing ambient, diffuse, specular, and emissive colors.

- [x] **2. Lighting Hierarchy Parsing**
  - [x] Expand traversing logic to parse `SoDirectionalLight` and `SoPointLight` and isolate their vectors and color intensities.
  - [x] Build a consolidated AoS layout defining `LightData` for global shader access.

- [x] **3. Buffer Allocation (`VulkanStateManager`)**
  - [x] Request allocations for `MaterialSSBO` mapping the `MaterialData` memory sequentially onto the GPU.
  - [x] Request allocations for `ShapeMaterialMapSSBO` enabling index tracking for instances dynamically.
  - [x] Allocate a single `LightUBO` defining the active scene lights up to a predefined limit (e.g. 8 lights).

- [x] **4. Fragment Pipeline Integration**
  - [x] Modify `basic.vert` to inject the native `gl_InstanceIndex` perfectly out to the Fragment Shader (`flat out uint in_InstanceID`).
  - [x] Reconfigure `basic.frag` mapping the Descriptor Sets: Set 0 Binding 1-3 (for ECS structures).
  - [x] Utilize `in_InstanceID` explicitly to retrieve `materialID = ShapeMaterialMapSSBO[in_InstanceID]` natively resolving fragments per sub-draw without driver calls!
  - [x] Execute `LightUBO` calculations extracting the standard Light equations per pixel fragment producing Phong lit geometries.

- [x] **5. Verification and Interactive Demo**
  - [x] Expand `vulkan-gui-test.cpp` instantiating colored `SoCube` fields.
  - [x] Instantiate dynamic runtime manipulation modifying materials ensuring pure `SoNotList` ECS memory syncs alter the fragment hues synchronously.
