# Coin3D Modern Renderer

## Overview

The unified-renderer branch implements an abstract rendering backend interface for Coin3D, allowing pluggable GPU implementations (OpenGL, Vulkan, Metal, Hydra Storm). The canonical implementation is a Modern OpenGL backend.

Design doc: https://github.com/tritao/coin/blob/8d9d7f6dcc19225735bd10e67c14318c54eb7fae/docs/modern-renderer-design.md

## Key Components

### SoModernRenderAction (`src/actions/SoModernRenderAction.cpp`)
- Traverses the scene graph, builds SoDrawList of SoRenderCommand structs
- Stores command paths per-command via storeCommandPath (uses unrefNoDelete — see caveats below)
- Geometry pool for temporary allocations during traversal

### SoDrawList / SoRenderCommand (`src/rendering/SoModernIR.h/.cpp`)
- SoRenderCommand: geometry, material, state, model matrix, pick data, selection data, sort key
- SoGeometryDesc: positions, normals (with normalCount), indices, texcoords, colors, stride
- SoPickData: pickIdentity string, per-face faceStart/faceCount ranges
- SoSelectionData: mutable highlightElement/selectedElements/colors
- buildPickLUT(): maps sequential IDs to (commandIndex, elementType, elementIndex)
- buildSortedOrder(): index-based sort (commands stay in place for stable LUT indices)
- pickLUTGeneration counter for cache invalidation

### SoRenderBackend (`src/rendering/SoRenderBackend.h`)
- Abstract interface: initialize, shutdown, render, pick, resize
- pick line width / point size for edge/vertex picking

### SoModernGLBackend (`src/rendering/SoModernGLBackend.cpp/.h`)
- Per-command VBO/VAO caching keyed by (positions ptr, indices ptr) composite
- GL_STATIC_DRAW, only re-upload on geometry change
- 3-4 GL calls per command (bind VAO + uniforms + draw)
- Blinn-Phong headlight shader (GLSL 1.20 for macOS compat)
- Opaque/transparent/overlay render passes
- ID buffer rendering with shared VBOs from visual pass
- Camera-aware pick buffer dirty tracking
- Interactive mode flag skips ID buffer

### SoIDPickBuffer (`src/rendering/SoIDPickBuffer.cpp/.h`)
- Offscreen FBO with RGBA8 color + depth24
- Per-vertex ID colors with type encoding (bits 31-30 = face/edge/vertex)
- PBO double-buffer async readback
- 3-pass rendering: triangles (GL_LESS), edges (GL_LEQUAL), vertices (GL_LEQUAL)
- Per-command ID VAOs for the pick pass
- computeIntersection: ray-triangle Moller-Trumbore (currently disabled due to stale data)

### SoRenderManager (`src/rendering/SoRenderManager.cpp`)
- renderModern(): traversal + backend render + superimposition
- assemblePickedPoint(): constructs SoPickedPoint from ID buffer pick
- GPU pick API: gpuPick, getGpuPickPath, getGpuPickElement, getGpuPickElementType, resolveGpuPickIdentity
- Draw list highlight/selection: setDrawListHighlight, setDrawListSelection, clearDrawListSelection
- Node sensor: filters Camera/NULL/SoShape triggers for modern renderer
- invalidateDrawList(): explicit invalidation + scheduleRedraw + clear GPU cache + mark pick buffer stale
- Interactive mode: setInteractive for navigation

### SoHandleEventAction (`src/actions/SoHandleEventAction.cpp`)
- getPickedPoint/getPickedPointList delegate to GPU pick via setRenderManager
- gpuPickedPointList manages picked point lifecycle (currently leaks to avoid crash — see caveats)

## Build

```bash
cmake --build build/relwithdebinfo --parallel
cmake --install build/relwithdebinfo --prefix /path/to/freecad/.pixi/envs/default
```

## Caveats

### unrefNoDelete for Command Paths
Command paths stored during traversal use `unrefNoDelete()` when cleared. Using `unref()` causes premature node deletion which corrupts the scene graph (rendering artifacts). But `unrefNoDelete` leaves node pointers that can become dangling when the scene graph is rebuilt. SoPickedPoint copies of these paths crash on delete. Current workarounds:
- Hover: uses resolveGpuPickIdentity (string-based, no path copying)
- Click: gpuPickedPointList uses SbPList::truncate (leaks instead of deleting)
- assemblePickedPoint: guarded by drawListValid check

### Node Sensor Filtering
The node sensor for the modern renderer filters Camera, NULL, and SoShape triggers — only scheduling redraws, not invalidating the draw list. Scene structure changes are detected via explicit `invalidateDrawList()` calls from FreeCAD (addViewProvider, removeViewProvider, slotFinishRestoreDocument).

### macOS GL Compatibility
- VAO functions use APPLE suffix (glGenVertexArraysAPPLE etc.)
- GLSL 1.20 (not 3.30 — not supported on macOS)
- Various NV/ARB extension defines needed

### libbacktrace
Optional integration for debug stack traces. Enabled when libbacktrace is found. Used for diagnosing node sensor invalidation sources.
