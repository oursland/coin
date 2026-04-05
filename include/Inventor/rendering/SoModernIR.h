// src/rendering/SoModernIR.h

#ifndef COIN_SOMODERNIR_H
#define COIN_SOMODERNIR_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/misc/SoState.h>

#include <Inventor/lists/SbList.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class SoGLShaderProgram;

class SoVBO;
class SoVAO;
class SoVertexLayout;
class SoPrimitiveVertexCache;
class SoModernRenderAction;
class SoShape;

/*!
  \enum SoPrimitiveTopology
  \brief Enumerates how primitives referenced by a geometry buffer should be interpreted.
*/
enum SoPrimitiveTopology : uint8_t {
  SO_TOPOLOGY_TRIANGLES = 0,
  SO_TOPOLOGY_LINES,
  SO_TOPOLOGY_POINTS,
  SO_TOPOLOGY_TRIANGLE_STRIP,
  SO_TOPOLOGY_LINE_STRIP,
  SO_TOPOLOGY_COUNT
};

/*!
  \struct SoGeometryDesc
  \brief Describes vertex/index data for a single draw call.

  All pointers remain owned by the producer (typically SoModernRenderAction).
  They must stay valid until the backend’s render() call completes. Backends
  are free to copy the data into GPU buffers if needed.
*/
struct SoGeometryDesc {
  SoPrimitiveTopology topology = SO_TOPOLOGY_TRIANGLES;
  uint32_t            vertexCount = 0;
  uint32_t            normalCount = 0;
  uint32_t            indexCount = 0;

  const float *       positions = nullptr;
  const float *       normals = nullptr;
  const float *       texcoords = nullptr;
  const float *       colors = nullptr;
  const uint32_t *    indices = nullptr;

  uint32_t            vertexStride = 0;
  uint32_t            texcoordStride = 0;

  struct CacheHandle {
    uint32_t contextId = 0;
    SoVBO * vertexVbo = nullptr;
    SoVBO * indexVbo = nullptr;
    SoVAO * vao = nullptr;
    const SoVertexLayout * vertexLayout = nullptr;
  } cache = {};
};

// --- Material flags (SoMaterialData::flags) ---
static constexpr uint32_t SO_MAT_HAS_TEXTURE = 0x1;  //!< Command carries embedded texture data
static constexpr uint32_t SO_MAT_IS_BILLBOARD = 0x2;  //!< Screen-space billboard sizing

// --- Feature flags (SoMaterialData::featureFlags) ---
static constexpr uint32_t SO_FEAT_BASE_COLOR = 0x1;   //!< Flat/unlit rendering (BASE_COLOR light model)

// --- Render param flags (SoRenderParams::flags) ---
static constexpr uint32_t SO_PARAM_CLEAR_WINDOW = 1u;
static constexpr uint32_t SO_PARAM_INTERACTIVE  = 2u;  //!< Camera orbiting/panning — skip ID buffer
static constexpr uint32_t SO_PARAM_CLEAR_DEPTH  = 4u;  //!< Clear depth buffer before rendering
static constexpr uint32_t SO_PARAM_SKIP_ID      = 8u;  //!< Skip ID buffer rendering entirely

/*!
  \struct SoMaterialData
  \brief Snapshot of the logical Inventor material state for one draw call.

  Texture pointers are backend-defined handles; IR does not own the memory.
*/
/*! Embedded texture data for commands that carry their own image (SoImage). */
struct SoTextureData {
  const unsigned char * pixels = nullptr;
  int width = 0;
  int height = 0;
  int numComponents = 0; // 1=L, 2=LA, 3=RGB, 4=RGBA
};

struct SoMaterialData {
  SbVec4f  diffuse = {0.8f, 0.8f, 0.8f, 1.0f};
  SbVec4f  ambient = {0.2f, 0.2f, 0.2f, 1.0f};
  SbVec4f  specular = {0.0f, 0.0f, 0.0f, 1.0f};
  SbVec4f  emissive = {0.0f, 0.0f, 0.0f, 1.0f};
  float    shininess = 0.2f;
  float    opacity = 1.0f;

  SoTextureData texture;  //!< Embedded texture (from SoImage, SoTexture2)

  void *   diffuseTexture = nullptr;
  void *   normalTexture = nullptr;
  void *   emissiveTexture = nullptr;

  float    metalness = 0.0f;
  float    roughness = 0.5f;

  uint32_t flags = 0;
  uint32_t featureFlags = 0;
};

/*!
  \struct SoDepthState
  \brief Depth-test configuration for a draw call.
*/
struct SoDepthState {
  SbBool  enabled = TRUE;
  SbBool  writeEnabled = TRUE;
  uint8_t func = 0; //!< Comparison function (GL-style enum value).
};

/*!
  \struct SoBlendState
  \brief Blending configuration (GL-style enums encoded as uint8_t).
*/
struct SoBlendState {
  SbBool  enabled = FALSE;
  uint8_t srcFactor = 0;
  uint8_t dstFactor = 0;
  uint8_t op = 0;
};

/*!
  \struct SoRasterState
  \brief Rasterizer properties (fill mode, culling, polygon offset).
*/
struct SoRasterState {
  uint8_t fillMode = 0;         // 0=filled, 1=lines (wireframe), 2=points
  uint8_t cullMode = 0;
  SbBool  scissorEnabled = FALSE;
  float   lineWidth = 1.0f;
  float   pointSize = 1.0f;
  uint16_t linePattern = 0xFFFF; // GL line stipple pattern (0xFFFF = solid)
  int16_t  linePatternScale = 1; // GL line stipple repeat factor
  float   polygonOffsetFactor = 0.0f;
  float   polygonOffsetUnits = 0.0f;
};

/*!
  \struct SoRenderState
  \brief Aggregates depth/blend/raster states plus precomputed sort keys.
*/
struct SoRenderState {
  SoDepthState depth;
  SoBlendState blend;
  SoRasterState raster;
  uint32_t opaqueKey = 0;
  uint32_t translucentKey = 0;
};

/*!
  \enum SoRenderPassType
  \brief Logical pass identifier used for coarse sorting.
*/
enum SoRenderPassType : uint8_t {
  SO_RENDERPASS_OPAQUE = 0,
  SO_RENDERPASS_TRANSPARENT,
  SO_RENDERPASS_OVERLAY,
  SO_RENDERPASS_SHADOW,
  SO_RENDERPASS_CUSTOM,
  SO_RENDERPASS_COUNT
};

typedef uint32_t SoLightingHandle;
typedef uint64_t SoPipelineKey;

/*!
  \struct SoPickData
  \brief Per-command pick identification data for GPU ID buffer picking.

  Populated during SoModernRenderAction traversal. The pick LUT maps
  sequential IDs to (commandIndex, elementType, elementIndex) tuples.
  The pickIdentity string carries application-level naming context
  (e.g., "docName\tobjName\tsubPrefix" in FreeCAD).
*/
struct SoPickData {
  uint32_t    pickLutBase = 0;   //!< First LUT entry index for this command
  uint32_t    pickLutCount = 0;  //!< Number of LUT entries (faces + edges + vertices)
  std::string pickIdentity;      //!< Application pick identity (opaque to Coin)

  //! Per-face element ranges for BRep shapes.
  //! faceStart[i] = index offset in EBO, faceCount[i] = element count.
  std::vector<int> faceStart;
  std::vector<int> faceCount;
};

/*!
  \struct SoSelectionData
  \brief Mutable selection/highlight state for a render command.

  These fields can be modified directly (without re-traversal) to update
  preselection highlighting and click selection at interactive rates.
*/
struct SoSelectionData {
  int         highlightElement = -1;  //!< -1=none, -2=whole body, >=0=face index
  SbVec4f     highlightColor;
  std::vector<int> selectedElements;  //!< empty=none, {-1}=all, else element IDs
  SbVec4f     selectionColor;
};

/*!
  \struct SoRenderCommand
  \brief Complete description of a single draw call in the IR.
*/
struct SoRenderCommand {
  SoGeometryDesc   geometry;
  SoMaterialData   material;
  SoRenderState    state;

  SbMatrix         modelMatrix;  // default-constructed to identity
  SbMatrix         viewMatrix;
  SbMatrix         projMatrix;

  SoRenderPassType pass = SO_RENDERPASS_OPAQUE;
  SoLightingHandle lightingHandle = 0;
  SoPipelineKey    pipelineKey = 0;
  SoGLShaderProgram * shaderProgram = nullptr;

  SoPickData       pick;       //!< GPU pick identification
  SoSelectionData  selection;  //!< Mutable highlight/selection state

  uint64_t         sortKey = 0;
  void *           userData = nullptr;
};

/*!
  \class SoIRBuffer
  \brief Chunk-based CPU scratch allocator for per-frame geometry data.

  Allocations are stable: pointers remain valid until clear() is called.
  Growth allocates new chunks without moving old data.
*/
class SoIRBuffer {
public:
  SoIRBuffer();
  ~SoIRBuffer() = default;

  void clear();
  void reserve(size_t bytes);
  void * allocate(size_t bytes, size_t alignment = alignof(float));

  template <typename T>
  T * allocateArray(size_t count, size_t alignment = alignof(T)) {
    return static_cast<T *>(this->allocate(count * sizeof(T), alignment));
  }

  size_t size() const { return this->totalAllocated; }

  //! Save current allocation state. Subsequent rewindTo() restores to
  //! this point, allowing re-allocation at the same addresses.
  struct SavePoint {
    std::vector<size_t> chunkCursors;
    size_t totalAllocated = 0;
  };
  SavePoint save() const;
  void rewindTo(const SavePoint & sp);

private:
  static constexpr size_t MIN_CHUNK_SIZE = 1024 * 1024; // 1 MB
  struct Chunk {
    std::vector<uint8_t> data;
    size_t cursor = 0;
  };
  std::vector<std::unique_ptr<Chunk>> chunks;
  size_t totalAllocated = 0;
  size_t highWaterMark = 0;  // largest total allocation seen across frames
};

/*!
  \enum SoPickElementType
  \brief Element types for pick identification.
*/
enum SoPickElementType : uint8_t {
  SO_PICK_FACE = 0,
  SO_PICK_EDGE = 1,
  SO_PICK_VERTEX = 2,
  SO_PICK_WHOLE_BODY = 3
};

/*!
  \struct SoPickLUTEntry
  \brief Maps a sequential render ID to a specific element of a draw command.
*/
struct SoPickLUTEntry {
  int                commandIndex;  //!< Index into SoDrawList
  SoPickElementType  elementType;
  int                elementIndex;  //!< Face/edge/vertex index (0-based)

  // Draw parameters for the ID buffer pass
  int                eboOffset;     //!< Index offset in EBO (for per-face draws)
  int                eboCount;      //!< Element count (for per-face draws)
};

/*!
  \class SoDrawList
  \brief Container holding the ordered list of SoRenderCommand objects for a frame.
*/
class SoDrawList {
public:
  SoDrawList();

  void clear();
  void reserve(int count);

  uint32_t getGeneration() const { return generation; }

  void addCommand(const SoRenderCommand & cmd);
  SoRenderCommand & emplaceCommand();

  int getNumCommands() const;
  void truncate(int count);  //!< Remove commands beyond index count (for partial rebuild)
  SoRenderCommand & getCommand(int i);
  const SoRenderCommand & getCommand(int i) const;

  SoRenderCommand * begin();
  SoRenderCommand * end();
  const SoRenderCommand * begin() const;
  const SoRenderCommand * end() const;

  // --- Pick LUT ---
  //! Access the pick look-up table (built after commands are collected).
  const std::vector<SoPickLUTEntry> & getPickLUT() const { return pickLUT; }
  std::vector<SoPickLUTEntry> & getMutablePickLUT() { return pickLUT; }

  //! Build a sorted index array for correct render ordering.
  //! The draw list itself is NOT reordered — command indices stay stable
  //! for pick LUT and command path lookups.
  void buildSortedOrder(const SbMatrix & viewMatrix);

  //! Get the sorted rendering order (indices into the command list).
  const std::vector<int> & getSortedOrder() const { return sortedOrder; }

  //! Generation counter — incremented on each buildPickLUT() call.
  //! Used by the backend to detect when ID color VBOs need rebuilding.
  uint64_t getPickLUTGeneration() const { return pickLUTGeneration; }

  //! Build the pick LUT from the current commands. Each face of BRep
  //! shapes gets a separate entry; edges/points/whole-body get one each.
  void buildPickLUT();

  //! Resolve a pick LUT index (1-based) to a pick identity string.
  //! Returns empty string if index is out of range.
  std::string resolvePickIdentity(uint32_t lutIndex) const;

private:
  SbList<SoRenderCommand> commands;
  std::vector<SoPickLUTEntry> pickLUT;
  std::vector<int> sortedOrder;
  uint32_t generation = 0;
  uint64_t pickLUTGeneration = 0;
};

/*! Utility helpers declared in SoModernIR.cpp */
uint64_t SoIRComputeSortKey(const SoRenderCommand & cmd,
                            uint32_t passOrderBits,
                            uint32_t depthBucket);

void SoIRDumpSummary(const SoDrawList & drawlist);
void SoIRDumpFirstN(const SoDrawList & drawlist, int count);

namespace SoModernIR {
void fillMaterialFromState(SoState * state, SoMaterialData & material);
void fillRenderStateFromState(SoState * state, SoRenderState & renderState);
bool isMaterialTransparent(const SoMaterialData & material);
SbBool appendCacheDrawCommands(const SoPrimitiveVertexCache * cache,
                               SoModernRenderAction * action,
                               SoShape * shape);
}

#endif // COIN_SOMODERNIR_H
