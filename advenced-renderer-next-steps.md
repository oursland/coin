# Advanced PBR Renderer: Next Steps Roadmap

Expanding the high-performance GPU-driven foundation into a fully featured Physically Based Rendering (PBR) pipeline—suitable for modern FreeCAD integration—requires implementing a standard modern rendering feature set. 

Since the CPU traversal bottleneck is resolved and the compute-culling architecture is established, adding PBR primarily involves expanding our data payloads and shader math.

Here is the roadmap of what is required, broken down into functional categories:

## 1. PBR Material Model (glTF 2.0 Standard)
Currently, Coin3D and legacy FreeCAD heavily rely on the legacy Phong lighting model (`SoMaterial`).
* **New Nodes:** Introduce a new node (e.g., `SoPhysicalMaterial`) that supports the standard Metallic-Roughness workflow.
* **ECS Expansion:** Expand the `MaterialData` struct in the SSBO to store Base Color, Roughness, Metallic, Emissive, and Ambient Occlusion coefficients.
* **Math Upgrade:** Replace the `basic.frag` shader with a Cook-Torrance BRDF implementation, migrating from ambient/diffuse/specular calculations to microfacet equations (e.g., GGX for specular distribution).

## 2. Bindless Texture Arrays
PBR relies heavily on high-resolution property maps (Albedo, Normal maps, ORM maps). 
* **Texture Upload Pipeline:** Parse legacy `SoTexture2` nodes, allocate Vulkan `VkImage` objects, and upload their pixel data.
* **Bindless Array:** Bind all uploaded textures to the GPU as a single massive continuous array (`layout(binding = X) uniform sampler2D globalTextures[]`).
* **ECS Indexing:** Each shape's `MaterialData` in the SSBO simply stores integer indices pointing to which textures to sample from the global array.

## 3. Image-Based Lighting (IBL)
PBR materials look completely flat without an environment to reflect.
* **Environment Maps:** Introduce an `SoEnvironment` or `SoHDREnvironmentMap` node to load HDRI skies.
* **Pre-computation:** The engine needs pipeline steps to generate Irradiance Cubemaps (for diffuse global illumination), Prefiltered Specular Cubemaps, and a BRDF Integration LUT. These allow the materials to realistically reflect ambient environments.

## 4. Linear Color Space & Tone Mapping
Legacy OpenGL operates in raw sRGB space, mutating colors. PBR requires strict adherence to linear physical light values.
* **Gamma Correction:** Ensure all Albedo textures are sampled in sRGB and converted to Linear space, while Roughness/Normal maps remain strictly Linear.
* **Post-Processing (Tone Mapping):** PBR calculations generate light values greater than 1.0 (High Dynamic Range). Apply an exposure level and a Tone Mapper (like ACES filmic) at the end of the fragment shader before pushing pixels to the screen.

## 5. FreeCAD Qt Interoperability (The Bridge)
FreeCAD embeds Coin3D inside a GUI (typically Qt via `SoQt` or `Quarter`), which currently relies strictly on deeply embedded OpenGL states.
* **Vulkan/Qt Bridge:** Utilize Qt's modern rendering abstractions (`QVulkanWindow` or specific Vulkan wrapper layers) rather than OpenGL contexts.
* **Texture Sharing:** Alternatively, if FreeCAD continues using OpenGL for the broader application GUI, utilize `VK_KHR_external_memory` to render the Vulkan PBR image off-screen, and pass that memory handle efficiently back to OpenGL to draw as a simple quad inside the FreeCAD viewport.
