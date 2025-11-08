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

/*!
  \class SoVertexLayout
  \brief The SoVertexLayout class is used to specify vertex layouts.

*/

#include <Inventor/elements/SoGLShaderProgramElement.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include "rendering/SoGL.h"
#include "rendering/SoVertexLayout.h"
#include "rendering/SoVBO.h"
#include "shaders/SoGLShaderProgram.h"
#include "glue/glp.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>

// This code was adapted from BGFX's vertex layout code.

static const uint8_t attribTypeSizeGl[SoAttribType::Count][4] = {
  {1, 2, 4, 4},  // Uint8
  {2, 4, 6, 8},  // Int16
  {2, 4, 6, 8},  // Half
  {4, 8, 12, 16},// Float
};

SoVertexLayout::SoVertexLayout()
    : stride(0)
{
}

SoVertexLayout& SoVertexLayout::begin()
{
  hash = 0; // use hash to store renderer type while building VertexLayout.
  stride = 0;
  memset(attributes, 0xff, sizeof(attributes));
  memset(offset, 0, sizeof(offset));
  memset(indexes, 0xff, sizeof(indexes));

  return *this;
}

void SoVertexLayout::end()
{

}

SoVertexLayout& SoVertexLayout::add(SoAttrib::Enum _attrib, uint8_t _num,
  SoAttribType::Enum _type, bool _normalized, bool _asInt)
{
  const uint16_t encodedNorm = (_normalized&1)<<7;
  const uint16_t encodedType = (_type&7)<<3;
  const uint16_t encodedNum  = (_num-1)&3;
  const uint16_t encodeAsInt = (_asInt&(!!"\x1\x1\x1\x0\x0"[_type]) )<<8;
  attributes[_attrib] = encodedNorm|encodedType|encodedNum|encodeAsInt;

  offset[_attrib] = stride;
  stride += attribTypeSizeGl[_type][_num-1];

  return *this;
}

void SoVertexLayout::decode(SoAttrib::Enum _attrib, uint8_t& _num,
  SoAttribType::Enum& _type, bool& _normalized, bool& _asInt) const
{
  uint16_t val = attributes[_attrib];
  _num        = (val&3)+1;
  _type       = SoAttribType::Enum( (val>>3)&7);
  _normalized = !!(val&(1<<7) );
  _asInt      = !!(val&(1<<8) );
}

static const char* attrName[] = {
  "a_position",
  "a_normal",
  "a_tangent",
  "a_bitangent",
  "a_color0",
  "a_color1",
  "a_color2",
  "a_color3",
  "a_indices",
  "a_weights",
  "a_texCoord0",
  "a_texCoord1",
  "a_texCoord2",
  "a_texCoord3",
  "a_texCoord4",
  "a_texCoord5",
  "a_texCoord6",
  "a_texCoord7",
};

static const GLenum attribType[] =
{
  GL_UNSIGNED_BYTE,            // Uint8
  GL_SHORT,                    // Int16
  GL_HALF_FLOAT,               // Half
  GL_FLOAT,                    // Float
};

void SoVertexLayout::bindAttributes(const SoState * state)
{
  const cc_glglue * glue = sogl_glue_instance(state);

  SoGLShaderProgram * shaderprogram =
    static_cast<SoGLShaderProgram *>(SoGLShaderProgramElement::get((SoState *)state));

  if (!shaderprogram) {
    SoDebugError::post("SoVertexLayout::GLRender",
                       "SoShaderProgram node not found in scene");
    return;
  }

  if (shaderprogram && shaderprogram->glslShaderProgramLinked()) {
    uint32_t shaderobj = shaderprogram->getGLSLShaderProgramHandle((SoState *)state);

    for (int i = 0; i < SoAttrib::Count; i++) {
      if (attributes[i] == UINT16_MAX) { continue; }

      SoAttrib::Enum attrib = (SoAttrib::Enum) i;
      uint8_t num;
      SoAttribType::Enum type;
      bool normalized;
      bool asInt;
      decode(attrib, num, type, normalized, asInt);

      // query the location for this in the shader
      const char* name = attrName[i];
      GLint index = glGetAttribLocation((COIN_GLhandle)shaderobj, (COIN_GLchar*)name);
      if (index < 0) {
#if COIN_DEBUG
          SoDebugError::postWarning("SoVertexLayout::bindAttributes",
                                    "vertex attribute '%s' not used in shader", name);
#endif // COIN_DEBUG
        continue;
      }

      auto gltype = attribType[type];
      uint32_t _baseVertex = 0;
      uint32_t baseVertex = _baseVertex* stride + offset[i];

      glEnableVertexAttribArray(index);
      glVertexAttribPointer(index, num, gltype, normalized, stride, (void*)(uintptr_t)baseVertex);

      indexes[i] = index;
    }
  }
}

void SoVertexLayout::unbindAttributes(const SoState * state)
{
    for (int i = 0; i < SoAttrib::Count; i++) {
      if (attributes[i] == UINT16_MAX) { continue; }

      SoAttrib::Enum attrib = (SoAttrib::Enum) attributes[i];
      uint8_t num;
      SoAttribType::Enum type;
      bool normalized;
      bool asInt;
      decode(attrib, num, type, normalized, asInt);

      uint16_t index = indexes[type];
      glDisableVertexAttribArray(index);
    }
}