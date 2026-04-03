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
  SoPrimitiveTopology topology;
  uint32_t            vertexCount;
  uint32_t            normalCount;   //!< Number of normals (may be < vertexCount for BRep shapes).
  uint32_t            indexCount;

  const float *       positions;
  const float *       normals;
  const float *       texcoords;
  const float *       colors;
  const uint32_t *    indices;

  uint32_t            vertexStride;   //!< Bytes between vertices in the positions/normals arrays.
  uint32_t            texcoordStride; //!< Bytes between texture coordinates; 0 = tightly packed.

  struct CacheHandle {
    uint32_t contextId;
    SoVBO * vertexVbo;
    SoVBO * indexVbo;
    SoVAO * vao;
    const SoVertexLayout * vertexLayout;
  } cache;
};

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
  SbVec4f  diffuse;
  SbVec4f  ambient;
  SbVec4f  specular;
  SbVec4f  emissive;
  float    shininess;
  float    opacity;

  SoTextureData texture;  //!< Embedded texture (from SoImage, SoTexture2)

  void *   diffuseTexture;
  void *   normalTexture;
  void *   emissiveTexture;

  uint32_t flags;        //!< Material feature bits (vertex colors, two-sided, etc.)
  uint32_t featureFlags; //!< Mirrors shader feature selection; reserved for future use.
};

/*!
  \struct SoDepthState
  \brief Depth-test configuration for a draw call.
*/
struct SoDepthState {
  SbBool  enabled;
  SbBool  writeEnabled;
  uint8_t func; //!< Comparison function (GL-style enum value).
};

/*!
  \struct SoBlendState
  \brief Blending configuration (GL-style enums encoded as uint8_t).
*/
struct SoBlendState {
  SbBool  enabled;
  uint8_t srcFactor;
  uint8_t dstFactor;
  uint8_t op;
};

/*!
  \struct SoRasterState
  \brief Rasterizer properties (fill mode, culling, polygon offset).
*/
struct SoRasterState {
  uint8_t fillMode;         // 0=filled, 1=lines (wireframe), 2=points
  uint8_t cullMode;
  SbBool  scissorEnabled;
  float   lineWidth;
  float   pointSize;
  uint16_t linePattern;     // GL line stipple pattern (0xFFFF = solid)
  int16_t  linePatternScale; // GL line stipple repeat factor
  float   polygonOffsetFactor;
  float   polygonOffsetUnits;
};

/*!
  \struct SoRenderState
  \brief Aggregates depth/blend/raster states plus precomputed sort keys.
*/
struct SoRenderState {
  SoDepthState depth;
  SoBlendState blend;
  SoRasterState raster;
  uint32_t opaqueKey;
  uint32_t translucentKey;
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

  SbMatrix         modelMatrix;

  SoRenderPassType pass;
  SoLightingHandle lightingHandle;
  SoPipelineKey    pipelineKey;
  SoGLShaderProgram * shaderProgram;

  SoPickData       pick;       //!< GPU pick identification
  SoSelectionData  selection;  //!< Mutable highlight/selection state

  uint64_t         sortKey;
  void *           userData;
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
