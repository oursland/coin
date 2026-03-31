// src/rendering/SoModernIR.cpp

#include "rendering/SoModernIR.h"
#include "CoinTracyConfig.h"

#include <Inventor/actions/SoModernRenderAction.h>
#include <Inventor/caches/SoPrimitiveVertexCache.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoGLShaderProgramElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/nodes/SoShape.h>

#include "rendering/SoVBO.h"

#include <algorithm>
#include <cstring>
#include <climits>
#include <inttypes.h>

SoIRBuffer::SoIRBuffer()
  : cursor(0)
{
}

void
SoIRBuffer::clear()
{
  this->cursor = 0;
}

void
SoIRBuffer::reserve(size_t bytes)
{
  if (bytes > this->storage.size()) {
    this->storage.resize(bytes);
  }
}

void *
SoIRBuffer::allocate(size_t bytes, size_t alignment)
{
  if (alignment == 0) alignment = 1;
  size_t alignedCursor = (this->cursor + alignment - 1) & ~(alignment - 1);
  const size_t required = alignedCursor + bytes;
  if (required > this->storage.size()) {
    this->storage.resize(required);
  }
  void * ptr = this->storage.data() + alignedCursor;
  this->cursor = required;
  return ptr;
}

SoDrawList::SoDrawList()
{
}

void
SoDrawList::clear()
{
  this->commands.truncate(0);
  this->pickLUT.clear();
}

void
SoDrawList::reserve(int count)
{
  this->commands.ensureCapacity(count);
}

void
SoDrawList::addCommand(const SoRenderCommand & cmd)
{
  this->commands.append(cmd);
}

SoRenderCommand &
SoDrawList::emplaceCommand()
{
  const int idx = this->commands.getLength();
  this->commands.append(SoRenderCommand());
  return this->commands[idx];
}

int
SoDrawList::getNumCommands() const
{
  return this->commands.getLength();
}

SoRenderCommand &
SoDrawList::getCommand(int i)
{
  return this->commands[i];
}

const SoRenderCommand &
SoDrawList::getCommand(int i) const
{
  return *(this->commands.getArrayPtr() + i);
}

SoRenderCommand *
SoDrawList::begin()
{
  return this->commands.getLength() ?
         const_cast<SoRenderCommand *>(this->commands.getArrayPtr()) : nullptr;
}

SoRenderCommand *
SoDrawList::end()
{
  return this->commands.getLength() ?
         const_cast<SoRenderCommand *>(this->commands.getArrayPtr()) + this->commands.getLength() : nullptr;
}

const SoRenderCommand *
SoDrawList::begin() const
{
  return this->commands.getLength() ?
         this->commands.getArrayPtr() : nullptr;
}

const SoRenderCommand *
SoDrawList::end() const
{
  return this->commands.getLength() ?
         this->commands.getArrayPtr() + this->commands.getLength() : nullptr;
}

void
SoDrawList::buildPickLUT()
{
  ZoneScopedN("buildPickLUT");
  pickLUT.clear();
  int numCmds = this->getNumCommands();

  for (int ci = 0; ci < numCmds; ci++) {
    SoRenderCommand & cmd = this->getCommand(ci);
    cmd.pick.pickLutBase = static_cast<uint32_t>(pickLUT.size());

    if (!cmd.pick.faceStart.empty()) {
      // BRep shape with per-face ranges: one LUT entry per face
      int numFaces = static_cast<int>(cmd.pick.faceStart.size());
      for (int f = 0; f < numFaces; f++) {
        SoPickLUTEntry le;
        le.commandIndex = ci;
        le.elementType = SO_PICK_FACE;
        le.elementIndex = f;
        le.eboOffset = cmd.pick.faceStart[f];
        le.eboCount = cmd.pick.faceCount[f];
        pickLUT.push_back(le);
      }
    }
    else if (cmd.geometry.topology == SO_TOPOLOGY_TRIANGLES ||
             cmd.geometry.topology == SO_TOPOLOGY_TRIANGLE_STRIP) {
      // Triangle mesh without face ranges: whole body
      SoPickLUTEntry le;
      le.commandIndex = ci;
      le.elementType = SO_PICK_WHOLE_BODY;
      le.elementIndex = 0;
      le.eboOffset = 0;
      le.eboCount = 0;
      pickLUT.push_back(le);
    }
    else if (cmd.geometry.topology == SO_TOPOLOGY_LINES ||
             cmd.geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
      if (!cmd.pick.faceStart.empty()) {
        // Per-edge entries (faceStart/faceCount populated by SoBrepEdgeSet)
        int numEdges = static_cast<int>(cmd.pick.faceStart.size());
        for (int e = 0; e < numEdges; e++) {
          SoPickLUTEntry le;
          le.commandIndex = ci;
          le.elementType = SO_PICK_EDGE;
          le.elementIndex = e;
          le.eboOffset = cmd.pick.faceStart[e];
          le.eboCount = cmd.pick.faceCount[e];
          pickLUT.push_back(le);
        }
      }
      else {
        // Whole edge set
        SoPickLUTEntry le;
        le.commandIndex = ci;
        le.elementType = SO_PICK_EDGE;
        le.elementIndex = 0;
        le.eboOffset = 0;
        le.eboCount = 0;
        pickLUT.push_back(le);
      }
    }
    else if (cmd.geometry.topology == SO_TOPOLOGY_POINTS) {
      // Per-vertex entries: each vertex gets its own LUT entry
      int numVerts = static_cast<int>(cmd.geometry.vertexCount);
      for (int v = 0; v < numVerts; v++) {
        SoPickLUTEntry le;
        le.commandIndex = ci;
        le.elementType = SO_PICK_VERTEX;
        le.elementIndex = v;
        le.eboOffset = v;
        le.eboCount = 1;
        pickLUT.push_back(le);
      }
    }

    cmd.pick.pickLutCount = static_cast<uint32_t>(pickLUT.size()) - cmd.pick.pickLutBase;
  }
}

std::string
SoDrawList::resolvePickIdentity(uint32_t lutIndex) const
{
  ZoneScopedN("resolvePickIdentity");
  if (lutIndex == 0 || lutIndex > pickLUT.size()) {
    return {};
  }
  const SoPickLUTEntry & le = pickLUT[lutIndex - 1];
  if (le.commandIndex < 0 || le.commandIndex >= this->getNumCommands()) {
    return {};
  }
  const SoRenderCommand & cmd = this->getCommand(le.commandIndex);
  if (cmd.pick.pickIdentity.empty()) {
    return {};
  }

  // Compose: pickIdentity + element name suffix
  std::string result = cmd.pick.pickIdentity;

  switch (le.elementType) {
    case SO_PICK_FACE:
      result += "Face" + std::to_string(le.elementIndex + 1);
      break;
    case SO_PICK_EDGE:
      result += "Edge" + std::to_string(le.elementIndex + 1);
      break;
    case SO_PICK_VERTEX:
      result += "Vertex" + std::to_string(le.elementIndex + 1);
      break;
    case SO_PICK_WHOLE_BODY:
      // No element suffix
      break;
  }

  return result;
}

uint64_t
SoIRComputeSortKey(const SoRenderCommand & cmd,
                   uint32_t passOrderBits,
                   uint32_t depthBucket)
{
  const uint64_t passbits = (static_cast<uint64_t>(passOrderBits) & 0xffULL) << 56;
  const uint64_t depthbits = (static_cast<uint64_t>(depthBucket) & 0x00ffffffULL) << 32;
  const uint64_t pipelinebits = cmd.pipelineKey & 0x00000000ffffffffULL;
  return passbits | depthbits | pipelinebits;
}

static const char *
renderpass_name(SoRenderPassType pass)
{
  switch (pass) {
  case SO_RENDERPASS_OPAQUE: return "opaque";
  case SO_RENDERPASS_TRANSPARENT: return "transparent";
  case SO_RENDERPASS_OVERLAY: return "overlay";
  case SO_RENDERPASS_SHADOW: return "shadow";
  case SO_RENDERPASS_CUSTOM: return "custom";
  default: return "unknown";
  }
}

void
SoIRDumpSummary(const SoDrawList & drawlist)
{
  int counts[SO_RENDERPASS_COUNT] = { 0 };
  uint32_t minVerts = UINT32_MAX;
  uint32_t maxVerts = 0;
  const int num = drawlist.getNumCommands();
  for (int i = 0; i < num; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    const uint32_t vc = cmd.geometry.vertexCount;
    minVerts = std::min(minVerts, vc);
    maxVerts = std::max(maxVerts, vc);
    if (cmd.pass < SO_RENDERPASS_COUNT) {
      counts[cmd.pass]++;
    }
  }

  SoDebugError::postInfo("SoDrawList",
                         "commands=%d opaque=%d transparent=%d overlay=%d shadow=%d custom=%d minVerts=%u maxVerts=%u",
                         num,
                         counts[SO_RENDERPASS_OPAQUE],
                         counts[SO_RENDERPASS_TRANSPARENT],
                         counts[SO_RENDERPASS_OVERLAY],
                         counts[SO_RENDERPASS_SHADOW],
                         counts[SO_RENDERPASS_CUSTOM],
                         minVerts == UINT32_MAX ? 0 : minVerts,
                         maxVerts);
}

void
SoIRDumpFirstN(const SoDrawList & drawlist, int count)
{
  const int num = drawlist.getNumCommands();
  const int limit = std::min(num, count);
  for (int i = 0; i < limit; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    SoDebugError::postInfo("SoDrawList",
                           "[%d] pass=%s topo=%d verts=%u idx=%u pipeline=0x%016" PRIx64,
                           i,
                           renderpass_name(cmd.pass),
                           static_cast<int>(cmd.geometry.topology),
                           cmd.geometry.vertexCount,
                           cmd.geometry.indexCount,
                           static_cast<uint64_t>(cmd.pipelineKey));
  }
}

namespace SoModernIR {

void
fillMaterialFromState(SoState * state, SoMaterialData & material)
{
  SoState * mutableState = state;
  const SbColor & diffuse = SoLazyElement::getDiffuse(mutableState, 0);
  const SbColor & ambient = SoLazyElement::getAmbient(mutableState);
  const SbColor & specular = SoLazyElement::getSpecular(mutableState);
  const SbColor & emissive = SoLazyElement::getEmissive(mutableState);
  const float transparency = SoLazyElement::getTransparency(mutableState, 0);

  material.diffuse.setValue(diffuse[0], diffuse[1], diffuse[2],
                            1.0f - transparency);
  material.ambient.setValue(ambient[0], ambient[1], ambient[2], 1.0f);
  material.specular.setValue(specular[0], specular[1], specular[2], 1.0f);
  material.emissive.setValue(emissive[0], emissive[1], emissive[2], 1.0f);
  material.shininess = SoLazyElement::getShininess(mutableState);
  material.opacity = 1.0f - transparency;

  material.diffuseTexture = NULL;
  material.normalTexture = NULL;
  material.emissiveTexture = NULL;
  material.flags = 0;
  material.featureFlags = 0;
}

void
fillRenderStateFromState(SoState * state, SoRenderState & rs)
{
  SoState * mutableState = state;
  SbBool depthtest = TRUE;
  SbBool depthwrite = TRUE;
  SoDepthBufferElement::DepthWriteFunction depthfunc =
    SoDepthBufferElement::LEQUAL;
  SbVec2f range;
  SoDepthBufferElement::get(mutableState, depthtest, depthwrite, depthfunc, range);

  rs.depth.enabled = depthtest;
  rs.depth.writeEnabled = depthwrite;
  rs.depth.func = static_cast<uint8_t>(depthfunc);

  int srcfactor = 0;
  int dstfactor = 0;
  rs.blend.enabled = SoLazyElement::getBlending(mutableState, srcfactor, dstfactor);
  rs.blend.srcFactor = static_cast<uint8_t>(srcfactor);
  rs.blend.dstFactor = static_cast<uint8_t>(dstfactor);
  rs.blend.op = 0;

  SoDrawStyleElement::Style style = SoDrawStyleElement::get(mutableState);
  uint8_t fillmode = 0;
  switch (style) {
  case SoDrawStyleElement::LINES:
    fillmode = 1;
    break;
  case SoDrawStyleElement::POINTS:
    fillmode = 2;
    break;
  default:
    fillmode = 0;
    break;
  }
  rs.raster.fillMode = fillmode;
  rs.raster.cullMode = 0;
  rs.raster.scissorEnabled = FALSE;
  rs.raster.lineWidth = SoLineWidthElement::get(mutableState);

  float offsetfactor = 0.0f;
  float offsetunits = 0.0f;
  SoPolygonOffsetElement::Style offsetstyle = SoPolygonOffsetElement::FILLED;
  SbBool offseton = FALSE;
  SoPolygonOffsetElement::get(mutableState, offsetfactor, offsetunits,
                              offsetstyle, offseton);
  if (!offseton) {
    offsetfactor = 0.0f;
    offsetunits = 0.0f;
  }
  rs.raster.polygonOffsetFactor = offsetfactor;
  rs.raster.polygonOffsetUnits = offsetunits;

  rs.opaqueKey = 0;
  rs.translucentKey = 0;
}

bool
isMaterialTransparent(const SoMaterialData & material)
{
  return material.opacity < 0.999f;
}

SbBool
appendCacheDrawCommands(const SoPrimitiveVertexCache * cache,
                        SoModernRenderAction * action,
                        SoShape * shape)
{
  if (!cache || !action || !shape) return FALSE;
  SoState * state = action->getState();
  if (!state) return FALSE;
  if (!cache->isValid(state)) return FALSE;

  const int numverts = cache->getNumVertices();
  const int numtriangles = cache->getNumTriangleIndices();
  if (numverts == 0 || numtriangles == 0) return FALSE;

  const SbVec3f * vertexarray = cache->getVertexArray();
  const GLint * rawindices = cache->getTriangleIndices();
  if (!vertexarray || !rawindices) return FALSE;

  const uint32_t contextid = SoGLCacheContextElement::get(state);

  const SbBool color = cache->colorPerVertex() && (cache->getColorArray() != NULL);
  const SbBool normal = cache->getNormalArray() != NULL;
  const SbBool texture = cache->getTexCoordArray() != NULL;
  const SbBool * enabled = NULL;
  int lastenabled = -1;
  if (texture) {
    enabled = SoMultiTextureEnabledElement::getEnabledUnits(state, lastenabled);
  }

  SoRenderCommand cmd;
  std::memset(&cmd, 0, sizeof(SoRenderCommand));
  cmd.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  cmd.geometry.vertexCount = static_cast<uint32_t>(numverts);
  cmd.geometry.indexCount = static_cast<uint32_t>(numtriangles);
  cmd.shaderProgram = SoGLShaderProgramElement::get(state);

  const SbBool cacheReady =
    cache->ensureModernVBOs(state, contextid, color, normal,
                            texture, enabled, lastenabled);

  if (cacheReady && cache->getVertexVBO() && cache->getIndexVBO()) {
    cmd.geometry.cache.contextId = contextid;
    cmd.geometry.cache.vertexVbo = cache->getVertexVBO();
    cmd.geometry.cache.indexVbo = cache->getIndexVBO();
    cmd.geometry.cache.vao = cache->getVAO();
    if (cache->getVertexVBO()) {
      cmd.geometry.cache.vertexLayout = &cache->getVertexVBO()->getVertexLayout();
    }
  }
  else {
    float * positions = static_cast<float *>
      (action->allocateGeometryStorage(sizeof(float) * 3 * numverts));
    for (int i = 0; i < numverts; ++i) {
      const SbVec3f & v = vertexarray[i];
      float * dst = positions + (i * 3);
      dst[0] = v[0];
      dst[1] = v[1];
      dst[2] = v[2];
    }
    cmd.geometry.positions = positions;
    cmd.geometry.vertexStride = sizeof(float) * 3;

    const SbVec3f * normalarray = cache->getNormalArray();
    if (normalarray) {
      float * normals = static_cast<float *>
        (action->allocateGeometryStorage(sizeof(float) * 3 * numverts));
      for (int i = 0; i < numverts; ++i) {
        const SbVec3f & n = normalarray[i];
        float * dst = normals + (i * 3);
        dst[0] = n[0];
        dst[1] = n[1];
        dst[2] = n[2];
      }
      cmd.geometry.normals = normals;
    }

    const SbVec4f * texarray = cache->getTexCoordArray();
    if (texarray) {
      float * texcoords = static_cast<float *>
        (action->allocateGeometryStorage(sizeof(float) * 4 * numverts));
      for (int i = 0; i < numverts; ++i) {
        const SbVec4f & t = texarray[i];
        float * dst = texcoords + (i * 4);
        dst[0] = t[0];
        dst[1] = t[1];
        dst[2] = t[2];
        dst[3] = t[3];
      }
      cmd.geometry.texcoords = texcoords;
      cmd.geometry.texcoordStride = sizeof(float) * 4;
    }

    if (cache->colorPerVertex()) {
      const uint8_t * colorsrc = cache->getColorArray();
      if (colorsrc) {
        float * colors = static_cast<float *>
          (action->allocateGeometryStorage(sizeof(float) * 4 * numverts));
        for (int i = 0; i < numverts; ++i) {
          const uint8_t * src = colorsrc + (i * 4);
          float * dst = colors + (i * 4);
          dst[0] = static_cast<float>(src[0]) / 255.0f;
          dst[1] = static_cast<float>(src[1]) / 255.0f;
          dst[2] = static_cast<float>(src[2]) / 255.0f;
          dst[3] = static_cast<float>(src[3]) / 255.0f;
        }
        cmd.geometry.colors = colors;
      }
    }

    uint32_t * indices = static_cast<uint32_t *>
      (action->allocateGeometryStorage(sizeof(uint32_t) * numtriangles));
    for (int i = 0; i < numtriangles; ++i) {
      const GLint idx = rawindices[i];
      indices[i] = idx < 0 ? 0u : static_cast<uint32_t>(idx);
    }
    cmd.geometry.indices = indices;
  }

  SoModernIR::fillMaterialFromState(state, cmd.material);
  SoModernIR::fillRenderStateFromState(state, cmd.state);
  cmd.modelMatrix = SoModelMatrixElement::get(state);

  const bool transparent = SoModernIR::isMaterialTransparent(cmd.material);
  cmd.pass = transparent ? SO_RENDERPASS_TRANSPARENT : SO_RENDERPASS_OPAQUE;
  cmd.lightingHandle = 0;
  cmd.pipelineKey = cmd.shaderProgram ? reinterpret_cast<uint64_t>(cmd.shaderProgram) : 0;
  cmd.sortKey = SoIRComputeSortKey(cmd,
                                   static_cast<uint32_t>(cmd.pass),
                                   0);
  cmd.userData = shape;

  action->getMutableDrawList().addCommand(cmd);
  return TRUE;
}

} // namespace SoModernIR
