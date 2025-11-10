# Coin Modern Renderer & Scene Rendering Architecture

**Status:** draft / WIP  
**Scope:** new renderer architecture for Coin, focusing on modern OpenGL first, but designed to be backend-agnostic and future-proof (Vulkan/Metal/etc.).  
**Non-goals (for now):** changing the high-level Inventor API (nodes, actions, fields, engines) beyond what’s explicitly listed here.

* * *

## 1. Goals

1. **Modernize rendering**
    
    * Remove dependence on legacy / fixed-function OpenGL.
        
    * Use modern GL features (VBOs, VAOs, shaders, UBOs, etc.).
        
2. **Decouple scene graph from GPU API**
    
    * Scene traversal and state (`SoState`, elements) should not hard-depend on OpenGL.
        
    * Rendering backend must be pluggable (legacy GL, modern GL, Vulkan, etc.).
        
3. **Preserve public API and semantics**
    
    * `SoGLRenderAction` + `SoShape::GLRender()` continue to work for existing applications.
        
    * New path introduced alongside old one; removal of legacy paths is a later, separate decision.
        
4. **Enable advanced rendering features**
    
    * Proper batching and sorting via a draw-list / IR.
        
    * More flexible lighting & materials.
        
    * Easier multi-view, offscreen rendering, GPU picking, shadows, etc.
        
5. **Make debugging & testing rendering easier**
    
    * Have a concrete intermediate representation (IR) of draw calls that can be logged, inspected, and tested independently from any GPU API.
        

* * *

## 2. High-Level Architecture

We split the rendering pipeline into clear stages:

1. **Scene graph & state**
    
    * Unchanged core: `SoNode`, `SoState`, elements, paths, etc.
        
2. **View abstraction (new, internal first)**
    
    * `SoView` (concept) encapsulates:
        
        * Camera
            
        * Viewport
            
        * Per-view settings (render mode, layer filtering, debug flags)
            
    * View produces a “visible set” and owns a render action to turn visibility into draw commands.
        
3. **Render actions**
    
    * Legacy: `SoGLRenderAction` → direct OpenGL calls.
        
    * New: `SoModernRenderAction` → traverses scene graph, builds draw list / IR, no direct GL.
        
4. **Intermediate Representation (IR)**
    
    * `SoDrawList` & `SoRenderCommand` describe _what_ to draw in an API-agnostic way.
        
5. **Render backend**
    
    * `RenderBackend` interface consumes draw lists and talks to the GPU:
        
        * `LegacyGLBackend` (existing path, gradually moved behind backend)
            
        * `ModernGLBackend` (new core-profile GL implementation)
            
        * Future: Vulkan/Metal/etc.
            

**Key principle:** traversal and state live on the Coin side; GPU calls live behind `RenderBackend`.

* * *

## 3. Components & Responsibilities

### 3.1 Scene graph & state

* We keep:
    
    * `SoNode`, `SoShape`, `SoGroup`, `SoSeparator`, etc.
        
    * `SoState` and element system (including `SoLazyElement`, `SoLightElement`, `SoModelMatrixElement`, etc.).
        
* Non-rendering nodes (transforms, groups, materials, lights, etc.):
    
    * Continue to manipulate `SoState` via `doAction()` / existing action methods.
        
    * Are reused as-is by both legacy and modern render paths.
        

### 3.2 View abstraction (`SoView` – conceptual)

**Concept:** A view encapsulates “how we look at the scene”.

A `SoView` (name TBD) owns:

* **Scene linkage**
    
    * A reference to a scene context / root node (initially just a root node/path).
        
* **Camera & viewport**
    
    * `SoCamera* camera`
        
    * `SbViewportRegion viewport`
        
* **View settings**
    
    * Render mode (wireframe, shaded, hidden-line, etc.)
        
    * Background (color/gradient/skybox)
        
    * Layer/filter masks (which nodes/categories are visible)
        
    * Debug flags (bounding boxes, overlays, etc.)
        

Responsibilities:

* `update(time)` – apply view-local updates, if any.
    
* `buildVisibleSet()` – optional culling / visibility determination.
    
* `buildDrawList(SoModernRenderAction&)` – run the modern action over visible nodes to generate draw commands.
    

**Implementation plan:**

* Phase 1: `SoView` is internal and effectively lives inside `SoRenderManager`.
    
* Phase 2: Expose `SoView` as a first-class type for advanced users (offscreen rendering, multi-view).
    

### 3.3 Render actions

#### 3.3.1 `SoGLRenderAction` (legacy)

* Kept as-is for:
    
    * Existing applications.
        
    * Legacy backend path that directly calls OpenGL.
        
* Continues to call `SoNode::GLRender(SoGLRenderAction*)` for shapes.
    

#### 3.3.2 `SoModernRenderAction` (new)

* New class derived from `SoAction`.
    
* Does **not** inherit from `SoGLRenderAction`.
    
* Main job: **build a draw list (IR) from the scene graph**.
    

Key responsibilities:

* Hold a `SbViewportRegion` and `SoCamera*`.
    
* In `beginTraversal()`:
    
    * Write viewport and camera data into `SoState` (via elements).
        
* For non-shape nodes:
    
    * Use existing `doAction()` behavior to update `SoState`.
        
* For shapes:
    
    * Call a new entry point `SoShape::render(SoModernRenderAction*)`.
        
    * `SoShape::render` snapshots transform/material/state and emits draw commands.
        

Public API sketch:

```cpp
class SoModernRenderAction : public SoAction {
  SO_ACTION_HEADER(SoModernRenderAction);
public:
  static void initClass();
  SoModernRenderAction(const SbViewportRegion & vp);
  virtual ~SoModernRenderAction();

  void setViewportRegion(const SbViewportRegion &);
  const SbViewportRegion & getViewportRegion() const;

  void setCamera(SoCamera * camera);
  SoCamera * getCamera() const;

  void apply(SoNode * root) override;
  void apply(SoPath * path) override;
  void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

  const SoDrawList & getDrawList() const;

protected:
  void beginTraversal(SoNode * node) override;
  void endTraversal(SoNode * node) override;

  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);

  SoDrawList & getMutableDrawList();

private:
  SbViewportRegion vpRegion;
  SoCamera *       camera;
  SoDrawList       drawlist;
};
```

* * *

## 4. Intermediate Representation (IR) – Draw List

We introduce a simple, API-agnostic IR for draw calls.

### 4.1 Core types

Initial version uses per-draw **descriptions** (structs) instead of pre-allocated handles; backends can internally cache/deduplicate as needed. Later we can evolve toward explicit handles if needed.

```cpp
enum class PrimitiveTopology {
  Triangles,
  Lines,
  Points,
  TriangleStrip,
  LineStrip
};

struct GeometryDesc {
  PrimitiveTopology topology;
  uint32_t          vertexCount;
  uint32_t          indexCount;
  // Pointers/offsets into CPU-side arrays or packed vertex structures
  const float *     positions;   // or struct-of-arrays
  const float *     normals;
  const float *     texcoords;
  const uint32_t *  indices;
  // Optional: stride info, vertex layout ID, bounding box, etc.
};

struct MaterialData {
  SbVec4f  diffuse;
  SbVec4f  ambient;
  SbVec4f  specular;
  SbVec4f  emissive;
  float    shininess;
  float    opacity;

  // Texture info – initially just IDs or pointers; later real handles.
  void *   diffuseTexture   = nullptr;
  void *   normalTexture    = nullptr;
  void *   emissiveTexture  = nullptr;

  uint32_t flags = 0; // vertex colors, two-sided, etc.
};

struct DepthState {
  bool    enabled      = true;
  bool    writeEnabled = true;
  uint8_t func         = /* LEQUAL / LESS / ... */;
};

struct BlendState {
  bool    enabled   = false;
  uint8_t srcFactor = 0;
  uint8_t dstFactor = 0;
  uint8_t op        = 0; // add, subtract, etc.
};

struct RasterState {
  uint8_t fillMode;   // fill/line/point
  uint8_t cullMode;   // none/back/front
  bool    scissorEnabled = false;
  // optional: polygon offset, line width, etc.
};

struct RenderState {
  DepthState  depth;
  BlendState  blend;
  RasterState raster;

  uint32_t    stateKeyOpaque       = 0; // precomputed for sorting
  uint32_t    stateKeyTranslucent  = 0;
};

enum class RenderPassType : uint8_t {
  Opaque,
  Transparent,
  Overlay,
  Shadow,
  Custom
};

using LightingHandle = uint32_t; // see lighting section
using PipelineKey    = uint64_t; // or structured; see pipelines section
```

The per-draw **command**:

```cpp
struct SoRenderCommand {
  GeometryDesc  geometry;
  MaterialData  material;
  RenderState   state;

  SbMatrix      modelMatrix;

  RenderPassType pass;
  LightingHandle lightingHandle; // per-draw lighting info
  PipelineKey    pipelineKey;    // used by backend to select pipeline

  uint64_t       sortKey;        // for backend sorting
  void *         userData;       // optional back-pointer to node/path
};

class SoDrawList {
public:
  void clear();
  void addCommand(const SoRenderCommand & cmd);

  int getNumCommands() const;
  const SoRenderCommand & getCommand(int i) const;

private:
  SbList<SoRenderCommand> commands;
};
```

**Notes:**

* **Phase 1**: IR carries `GeometryDesc`/`MaterialData` by value (or shared pointers); backend can deduplicate and cache internally.
    
* **Phase 2**: we may evolve `GeometryDesc`/`MaterialData` to be light-weight handles referencing persistent backend-managed resources.
    

* * *

## 5. Shape Rendering Path

We introduce a new virtual entry point for shapes, used only by the modern action.

### 5.1 New method on `SoShape`

We extend `SoShape`:

```cpp
class SoShape : public SoNode {
  // existing:
  virtual void GLRender(SoGLRenderAction * action);

  // new:
  virtual void render(SoModernRenderAction * action);
};
```

**Default implementation** of `SoShape::render`:

1. Access `SoState` from the action.
    
2. Snapshot:
    
    * Model matrix (`SoModelMatrixElement`).
        
    * Material state (via `SoLazyElement` and related elements).
        
    * Render-state info (cull mode, draw style, etc.).
        
3. Decide pass (opaque vs transparent) based on material opacity.
    
4. Use `generatePrimitives()` (or equivalent) to gather triangles/lines/points into a temporary `GeometryDesc`.
    
5. Fill a `SoRenderCommand` and append it to the action’s `SoDrawList`.
    

Later, shape subclasses can **override `render()`** to:

* Use pre-tessellated or cached geometry.
    
* Emit more optimized command sets (instancing, LOD, etc.).
    

### 5.2 Non-shape nodes

Non-shape nodes are handled via `SoModernRenderAction::renderNode`:

* Default implementation just calls `node->doAction(action)`, which:
    
    * Updates `SoState` just as in other actions.
        
* This means transforms, materials, lights, separators, etc. work the same way for both legacy and modern rendering.
    

* * *

## 6. Materials & `SoLazyElement`

We keep the existing **material aggregation** via `SoLazyElement` and treat it as the canonical source of “conceptual material state”.

### 6.1 Current roles

* `SoMaterial` / `SoLightModel` / property nodes write fields into `SoLazyElement` (colors, shininess, transparency, light model, two-sided, blending flags, etc.).
    
* `SoGLLazyElement` extends `SoLazyElement` to actually call GL (legacy path).
    
* `SoMaterialBundle` is a convenience class for shapes, used only by GL rendering.
    

### 6.2 New material snapshot helper

For the modern path, we introduce an internal helper (`SoMaterialUtil::snapshot`) that builds `MaterialData` from the current state:

```cpp
struct SoMaterialSnapshot {
  MaterialData data;
  bool         transparent;
};

class SoMaterialUtil {
public:
  static SoMaterialSnapshot fromState(SoState * state);
};
```

Behavior:

* Reads from `SoLazyElement` (and related elements like `SoMaterialBindingElement`).
    
* Computes a single per-draw `MaterialData` structure including:
    
    * RGBA color (opacity = 1 - transparency).
        
    * Specular, emissive, shininess, etc.
        
    * Two-sided / cull flags.
        
* Computes an `transparent` flag to help decide pass (opaque vs transparent).
    

`SoMaterialBundle` continues to exist **only for `SoGLRenderAction`**; modern path does not use it.

* * *

## 7. Lights & Shader Selection

### 7.1 Light representation

We introduce a canonical light description:

```cpp
enum class LightType : uint8_t {
  Directional,
  Point,
  Spot,
  Environment
};

struct LightData {
  LightType type;
  SbVec3f   color;
  float     intensity;

  SbVec3f   position;   // world space
  float     range;      // max distance

  SbVec3f   direction;  // world space
  float     innerCone;  // radians for spot
  float     outerCone;  // radians for spot

  uint32_t  flags;      // shadow-caster, headlight, etc.
};

struct LightingSetup {
  std::vector<LightData> lights;
};
```

### 7.2 LightingHandle and per-draw assignment

We support two stages:

* **Phase 1:** one `LightingSetup` per view (all enabled lights).
    
* **Phase 2:** optional per-draw `LightingHandle`:
    
    * During IR building, consult `SoLightElement` at each shape.
        
    * Build `LightingSetup` per distinct light set and assign its handle into `DrawCommand::lightingHandle`.
        
    * Use an internal `LightingCache` to deduplicate identical sets.
        

### 7.3 Shader / pipeline selection via feature flags

Instead of one shader per light count, we use **feature flags**:

```cpp
enum PipelineFeature : uint32_t {
  PF_LIT            = 1 << 0,
  PF_VERTEX_COLOR   = 1 << 1,
  PF_DIFFUSE_TEX    = 1 << 2,
  PF_NORMAL_MAP     = 1 << 3,
  PF_ALPHA_TEST     = 1 << 4,
  PF_RECEIVE_SHADOW = 1 << 5,
  // ...
};

enum ShadingModel : uint8_t {
  SHADING_UNLIT,
  SHADING_PHONG,
  SHADING_PBR
};

struct PipelineKey {
  uint32_t     features;     // OR of PipelineFeature
  ShadingModel shadingModel;
  uint8_t      passType;     // matches RenderPassType
  uint8_t      topology;     // matches PrimitiveTopology
  // optional: vertex layout ID, etc.
};
```

Decision rules:

* From `SoLightModelElement` + “are there active lights?”:
    
    * If no lights or `BASE_COLOR` → `SHADING_UNLIT`, clear `PF_LIT`.
        
    * Else → `SHADING_PHONG` (or PBR later), set `PF_LIT`.
        
* From `SoLazyElement` and texture elements:
    
    * Set `PF_DIFFUSE_TEX` for bound diffuse texture.
        
    * Set `PF_VERTEX_COLOR` if using per-vertex diffuse.
        
    * Set `PF_ALPHA_TEST` if we emulate alpha masking.
        
    * Set `PF_RECEIVE_SHADOW` if any active lights cast shadows.
        

The backend uses `PipelineKey` to select or create the appropriate GPU pipeline/shader program.

* * *

## 8. RenderBackend Interface

`RenderBackend` sits between IR and GPU API.

### 8.1 Interface

Internal header, e.g. `src/rendering/RenderBackend.h`:

```cpp
class RenderBackend {
public:
  virtual ~RenderBackend() {}

  struct Capabilities {
    bool supportsMultisampling = false;
    bool supportsShadowMaps    = false;
    bool supportsHDR           = false;
    // etc.
  };

  struct FrameParams {
    int    frameIndex = 0;
    double time       = 0.0;
  };

  struct ViewRenderParams {
    SbViewportRegion viewport;
    SbColor4f        clearColor;
    bool             clearColorBuffer = true;
    bool             clearDepthBuffer = true;
    SbMatrix         viewMatrix;
    SbMatrix         projMatrix;
    // optional: exposure, debug flags, etc.
  };

  struct LightingData {
    LightingSetup setup; // or reference to a cached structure
  };

  virtual Capabilities getCapabilities() const = 0;

  virtual void beginFrame(const FrameParams & frame) = 0;
  virtual void endFrame() = 0;

  virtual void renderView(const ViewRenderParams & view,
                          const SoDrawList & drawlist,
                          const LightingData & lighting) = 0;
};
```

`SoRenderManager` or `SoView` owns a `RenderBackend*` and uses it per frame.

### 8.2 Backends

We plan for at least two implementations:

1. **LegacyGLBackend**
    
    * Initially a thin wrapper around existing GL behavior (may still lean on `SoGLRenderAction` internally).
        
    * Over time, we can refactor it to consume IR like any other backend.
        
2. **ModernGLBackend**
    
    * True core-profile GL (3.3+ or 4.x):
        
        * VAOs/VBOs only.
            
        * GLSL shader pipelines.
            
        * UBOs/SSBOs for camera/lights/materials.
            
    * Consumes `SoDrawList` and `LightingData` to issue modern GL calls.
        

Future backends (Vulkan/Metal/D3D etc.) implement the same interface without touching scene graph code.

* * *

## 9. Legacy Compatibility & Migration Plan

We explicitly keep legacy behavior and introduce the new system in phases.

### 9.1 Keep `SoGLRenderAction` + `GLRender` for now

* `SoGLRenderAction` and `SoShape::GLRender(SoGLRenderAction*)` remain untouched and fully supported.
    
* `SoMaterialBundle`, `SoGLLazyElement`, and all GL-specific code continue to operate for the legacy backend.
    

### 9.2 Introduce `SoModernRenderAction` and new backend in parallel

**Phase 1 – bootstrap**

* Implement:
    
    * `SoModernRenderAction` (only basic behavior).
        
    * `SoDrawList`, `SoRenderCommand`, material snapshot helper.
        
* Implement one or two shapes’ `SoShape::render(SoModernRenderAction*)` via `generatePrimitives()`.
    
* Implement a **debug backend** that just prints the draw list to logs (no actual GL).
    

**Phase 2 – modern GL backend (minimal)**

* Implement `ModernGLBackend` that:
    
    * Uses VBOs/VAOs & shaders.
        
    * Accepts `SoDrawList` and draws a small subset of primitives (e.g. triangles from `SoFaceSet`).
        
* Integrate it so that `SoRenderManager` can switch between:
    
    * “legacy path” (`SoGLRenderAction`), and
        
    * “modern path” (`SoModernRenderAction` + `ModernGLBackend`).
        

**Phase 3 – broaden IR coverage**

* Gradually implement `render()` overrides for more shapes:
    
    * `SoFaceSet`, `SoIndexedFaceSet`, `SoLineSet`, `SoPointSet`, `SoSphere`, `SoCube`, `SoText*`, etc.
        
* Extend material & lighting handling as needed.
    
* Start relying more on the IR path in tests and for new features.
    

**Phase 4 – make modern path the default**

* Once coverage and parity are good enough:
    
    * Make modern backend the default for new builds.
        
    * Keep legacy path selectable for backwards compatibility for at least one major version cycle.
        

**Later (future decision)**

* Consider deprecating/removing direct GL calls from nodes and `SoGLRenderAction` when ecosystem is ready.
    

* * *

## 10. Open Questions / To-Decide Later

These are intentionally left flexible for iteration:

1. **Exact shape of `SoView`:**
    
    * How much of `SoRenderManager`’s logic moves into `SoView` vs stays where it is?
        
2. **Handle vs description-based IR:**
    
    * We start with “descriptions in IR” and backend-side caching.
        
    * We may later move to explicit handles (`GeometryHandle`, `MaterialHandle`, `PipelineHandle`) once patterns solidify.
        
3. **How far to push per-draw lighting handles:**
    
    * Phase 1: global `LightingSetup` per view.
        
    * Phase 2: per-draw `LightingHandle` derived from `SoLightElement` – we need to decide the performance/complexity trade-off.
        
4. **Multi-pass and advanced features:**
    
    * How we represent passes (shadow maps, G-buffer, ID buffers) in IR and `RenderBackend`.
        
5. **Threading / multi-core traversal:**
    
    * Once IR exists, it’s easier to parallelize culling and command building.
        
    * Details of thread safety and sync are left for a later design.
        

* * *

## 11. Summary

Design decisions we’ve agreed on:

* **Yes**, we introduce a new `SoModernRenderAction` to build an **API-agnostic draw list** instead of calling OpenGL directly.
    
* **Yes**, we keep `SoGLRenderAction` + `GLRender()` untouched for legacy compatibility.
    
* **Yes**, we introduce a **`RenderBackend`** interface with at least a **modern OpenGL implementation**, and optionally a legacy GL wrapper.
    
* **Yes**, we use `SoLazyElement` as the canonical source of material state, but treat `SoMaterialBundle` and `SoGLLazyElement` as GL-only.
    
* **Yes**, we represent lights in a backend-agnostic way (`LightData`, `LightingSetup`) and choose shaders via **feature flags + `PipelineKey`**, not per-light-count variants.
    
* **Yes**, we want a **view abstraction** (`SoView`) that owns camera/viewport/settings and builds draw lists per view.
    

This gives us a concrete slice we can start coding against, while still allowing evolution of details (IR exact structure, backend caching strategies, etc.) as we learn from the implementation.

* * *

If you want, I can next turn this into a set of **initial tasks / tickets** (e.g. “Add SoModernRenderAction skeleton”, “Implement SoDrawList + SoRenderCommand”, “Add debug backend”) so it’s easy to split work across people.

* * *

**You:**

Do we need LegacyGLBackend if we just keep the existing code paths?

* * *

**ChatGPT:**

Short answer: **no, you don’t _need_ a LegacyGLBackend at first** if you keep the existing `SoGLRenderAction` + `GLRender()` path. But **eventually having one is very nice**, because it unifies the architecture and gives you a GL backend that consumes the same IR as everything else.

Let’s separate _now_ vs _later_.

* * *

## Right now (starting work): you can skip LegacyGLBackend

You already have:

* `SoGLRenderAction`
    
* `SoShape::GLRender(SoGLRenderAction*)`
    
* `SoGLLazyElement`, `SoMaterialBundle`, etc.
    

If you introduce:

* `SoModernRenderAction` → produces `SoDrawList` (IR)
    
* `RenderBackend` → implemented only as **ModernGLBackend**
    

then you can run **two completely separate pipelines**:

1. **Legacy path**
    
    * Viewer / `SoSceneManager` → `SoGLRenderAction` → old GL calls.
        
    * No `RenderBackend` involved at all.
        
    * All existing apps keep working.
        
2. **Modern path**
    
    * Viewer / `SoRenderManager` (or a special viewer) → `SoModernRenderAction` → `SoDrawList` → `ModernGLBackend`.
        
    * Used by new code/tests/experimental builds.
        

In this model, a `LegacyGLBackend` is **not required**. The old code is the legacy backend.

For getting started, that’s perfectly fine and actually simpler:

* Less refactoring in the short term.
    
* You don’t have to make the old code obey the new `RenderBackend` interface right away.
    
* You can iterate on the IR + modern backend design in isolation.
    

So: for “let’s start coding this prototype”, **no**, don’t bother with a `LegacyGLBackend` yet.

* * *

## Longer term: why you might still want a LegacyGLBackend

Once the new architecture is working, a `LegacyGLBackend` becomes useful for a few reasons:

### 1. Single high-level pipeline

Right now, with no LegacyGLBackend, you effectively have:

```text
Path A: Scene → SoGLRenderAction → (direct GL)
Path B: Scene → SoModernRenderAction → IR → ModernGLBackend → (GL)
```

If you eventually make a `LegacyGLBackend` that also consumes IR, you can move toward:

```text
Scene → SoModernRenderAction → IR → RenderBackend (legacy GL OR modern GL OR Vulkan, etc.)
```

and demote the old `SoGLRenderAction` path to “compatibility only”.

That gives you:

* One “way” Coin renders things conceptually: via IR.
    
* Multiple backends implementing that concept.
    
* Less special-case code in viewers / `SoRenderManager`.
    

### 2. Testing & parity

A LegacyGLBackend that consumes the IR is a great **visual regression tool**:

* You can compare:
    
    * Old `SoGLRenderAction` output vs IR+LegacyGLBackend output.
        
* You can run them side-by-side to:
    
    * Catch differences in material / lighting / transparency logic.
        
    * Validate the IR faithfully represents what the old pipeline did.
        

Even if `LegacyGLBackend` is just a thin wrapper that replays GL calls similar to `GLRender()`, having it behind the `RenderBackend` interface lets you **exercise the IR** while still using known-good GL behavior.

### 3. Gradual migration of old code

At some point you might want to:

* Stop sprinkling new GL code into node `GLRender()` implementations.
    
* Move all GL-specific work into backends.
    

A LegacyGLBackend gives you a target to migrate to:

* Take the old GL logic (from `SoShape::GLRender`, `SoGLLazyElement`, etc.).
    
* Refactor it into a backend that understands `SoRenderCommand`, `MaterialData`, etc.
    
* Eventually, deprecate direct `GLRender()` for most nodes.
    

You don’t have to do this soon, but if you _never_ introduce a LegacyGLBackend, you’ll end up maintaining 2 separate rendering “worlds” indefinitely:

* Old GL that doesn’t use IR.
    
* New IR + ModernGLBackend.
    

* * *

## Practical recommendation

So, concretely:

### For the initial implementation:

* **Skip `LegacyGLBackend`.**
    
* Keep:
    
    * Old path: `SoGLRenderAction` → GL (unchanged).
        
    * New path: `SoModernRenderAction` → IR → `ModernGLBackend`.
        

Focus your effort on:

1. `SoDrawList` / IR design.
    
2. `SoModernRenderAction` + `SoShape::render()`.
    
3. A minimal `ModernGLBackend`.
    

This gets you something working and testable without touching the legacy path.

### For later, when the IR path is stable:

* Consider adding `LegacyGLBackend` as:
    
    * A backend that consumes IR and calls OpenGL in a way that mimics the legacy behavior.
        
    * A tool for regression testing and for eventually unifying everything behind `RenderBackend`.
        

So the answer is:

* **No, you don’t need a LegacyGLBackend to start** — the existing code _is_ your legacy backend.
    
* **Yes, it’s still a good idea in the longer run** if you want one clean IR-based architecture and smoother migration/testing story.