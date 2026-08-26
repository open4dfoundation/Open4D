/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _EB_ATTRIBUTE_H_
#define _EB_ATTRIBUTE_H_

//
#include <vector>
#include <algorithm>
#include "glm/glm.hpp"

namespace eb {

class Attribute {
public:
  enum class Domain {
    PER_VERTEX,
    PER_FACE,
    PER_OBJECT
  };
  enum class Type {
    POSITION,
    UVCOORD,
    COLOR,
    NORMAL,
    GENERIC,
    MATERIAL_ID,
    UNDEFINED
  };

public:
  // index of the attribute in the CTMesh table of attributes
  const int index;
  // to which primitive does the attribute applies
  const Domain domain;
  // type of the values
  const Type type;
  // number of components per value
  const int nbComp;
  // index of the reference attribute if any,
  // if set to -1 is a primary attribute,
  // if set >= 0 it means
  // the current attribute is expressed function
  // of a primary attribute which index dereferences
  // the attributes table of the CTMeshExt
  const int mainAttrIdx;  
  // set to true if attribute has its own index table
  // set to false otherwise or if attribute do not need
  // indices (Object attr)
  const bool hasOwnIndices;
  // index of the reference attribute for connectivity,
  // if set to -1 then has own indices,
  // if set >= 0 it means
  // if 0 the reference is the primary attribute
  // if >0 it is the index of the auxilliary attribute plus 1
  const int refAuxIdx;
  
  // some parameters are copied in the attribute object for convenience

  // prediction method 
  uint8_t predMethod;
  // quantization level
  int8_t  qp;
  // dequantization flag
  bool    dequantize;

public:
    Attribute(
        int index,
        Domain domain,
        Type   type,
        int    nbComp,
        int    mainAttrIdx,
        bool   hasAuxIndices = false,
        int    refAuxIdx = -1
        ) :
      index(index),
      domain(domain), 
      type(type), 
      nbComp(nbComp), 
      mainAttrIdx(mainAttrIdx), 
      hasOwnIndices(hasAuxIndices),
      refAuxIdx(refAuxIdx)
  {};

  inline bool isPrimary() const {
      return mainAttrIdx == -1;
  }
  inline bool isPerVertex() const {
      return domain == Domain::PER_VERTEX;
  }
  inline bool isPerFace() const {
      return domain == Domain::PER_FACE;
  }
  inline bool isPerObject() const {
      return domain == Domain::PER_OBJECT;
  }
  inline bool isPerVertexPosition() const {
      return (domain == Domain::PER_VERTEX 
          && type == Type::POSITION);
  }
  inline bool isPerVertexUvCoord() const {
      return (domain == Domain::PER_VERTEX
          && type == Type::UVCOORD);
  }
  inline bool isPerVertexUvCoord(int mainAttrIndex) const {
      return (mainAttrIdx == mainAttrIndex) &&
          isPerVertexUvCoord();
  }
  inline bool isPerVertexNormal() const {
      return (domain == Domain::PER_VERTEX
          && type == Type::NORMAL);
  }
  inline bool isPerVertexNormal(int mainAttrIndex) const {
      return (mainAttrIdx == mainAttrIndex) &&
          isPerVertexNormal();
  }
  inline bool isPerVertexColor() const {
      return (domain == Domain::PER_VERTEX
          && type == Type::COLOR);
  }
  inline bool isPerVertexColor(int mainAttrIndex) const {
      return (mainAttrIdx == mainAttrIndex) &&
          isPerVertexColor();
  }
};

static std::ostream&
operator<<(std::ostream& out, Attribute::Type val) {
    switch (val) {
    case Attribute::Type::POSITION:    out << "position"; break;
    case Attribute::Type::UVCOORD:     out << "uv"; break;
    case Attribute::Type::COLOR:       out << "color"; break;
    case Attribute::Type::NORMAL:      out << "normal"; break;
    case Attribute::Type::MATERIAL_ID: out << "material id"; break;
    case Attribute::Type::GENERIC:     out << "generic"; break;
    }
    return out;
}

}  // namespace eb

#endif