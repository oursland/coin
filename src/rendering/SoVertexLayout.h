#ifndef COIN_VERTEXLAYOUT_H
#define COIN_VERTEXLAYOUT_H

/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#ifndef COIN_INTERNAL
#error this is a private header file
#endif /* !COIN_INTERNAL */

#include <Inventor/system/renderer.h>
#include <Inventor/system/gl.h>
#include <Inventor/C/glue/gl.h>

#include "misc/SbHash.h"

class SoState;

/// Vertex attribute enum.
struct SoAttrib
{
  /// Corresponds to vertex shader attribute.
  enum Enum
  {
    Position,
    Normal,
    Tangent,
    Bitangent,
    Color0,
    Color1,
    Color2,
    Color3,
    Indices,
    Weight,
    TexCoord0,
    TexCoord1,
    TexCoord2,
    TexCoord3,
    TexCoord4,
    TexCoord5,
    TexCoord6,
    TexCoord7,

    Count
  };
};

/// Vertex attribute types.
struct SoAttribType
{
  enum Enum
  {
    Uint8,
    Int16,
    Half,
    Float,

    Count
  };
};

struct SoVertexLayout
{
  SoVertexLayout();

  /// Start VertexLayout.
  SoVertexLayout& begin();

  /// End VertexLayout.
  void end();

  /// Add attribute to VertexLayout.
  SoVertexLayout& add(SoAttrib::Enum _attrib, uint8_t _num,
    SoAttribType::Enum _type, bool _normalized = false, bool _asInt = false);

  /// Decode attribute.
  void decode(SoAttrib::Enum _attrib, uint8_t& _num,
    SoAttribType::Enum& _type, bool& _normalized, bool& _asInt) const;

  /// Returns `true` if VertexLayout contains attribute.
  bool has(SoAttrib::Enum _attrib) const { return UINT16_MAX != attributes[_attrib]; }

  /// Returns relative attribute offset from the vertex.
  uint16_t getOffset(SoAttrib::Enum _attrib) const { return offset[_attrib]; }

  /// Returns vertex stride.
  uint16_t getStride() const { return stride; }

  /// Returns size of vertex buffer for number of vertices.
  uint32_t getSize(uint32_t _num) const { return _num*stride; }

  /// Binds the vertex attributes according to the current shader program.
  void bindAttributes(const SoState * state);

  /// Unbinds the vertex attributes according to the current shader program.
  void unbindAttributes(const SoState * state);

  uint32_t hash;
  uint16_t stride;
  uint16_t offset[SoAttrib::Count];
  uint16_t attributes[SoAttrib::Count];

private:
  uint16_t indexes[SoAttrib::Count];
};

#endif // COIN_VERTEXLAYOUT_H
