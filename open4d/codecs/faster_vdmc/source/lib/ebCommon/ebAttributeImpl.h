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

#ifndef _EB_ATTRIBUTE_IMPL_H_
#define _EB_ATTRIBUTE_IMPL_H_

 //
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>
//
#include "ebCornerTable.h"
#include "ebAttribute.h"

namespace eb {

    class AttributeVertexBase : public Attribute {
    public:
        // a corner table if needed
        CornerTable ct;
        // auxiliary opposite table to handle seams
        // when separate index is used
        // this is not equal to ct.O, auxO does not 
        // contain degenerated triangles from aux index table
        std::vector<int> auxO;
        // information for deduplication when convertModelToCTMesh called
        // with keepTrackOfVertexDeduplications
        // associate groups of duplicated points to a
        // unique index from 0 to numSplitVertices minus 1
        std::map<int, int> duplicatesMap;
        
    public:
        AttributeVertexBase(
            int index,
            Domain attrDomain,
            Type   attrType,
            int             nbComp,
            int             mainAttrIdx,
            bool            hasAuxIndices = false,
            int             refAttrIdx = -1)
            : Attribute(index, attrDomain, attrType,
                nbComp, mainAttrIdx, hasAuxIndices, refAttrIdx) {};
    };

    template<class ValueType>
    class AttributeVertex : public AttributeVertexBase {
    public:
        // the per vertex attributes (values)
        // the values are dereferenced by ct.V if hasOwnIndices is true
        // otherwise they are dereferenced by the V table of the attribute of index mainAttrIdx
        std::vector<ValueType> values;
        ValueType minVal = ValueType(-1.0);
        ValueType maxVal = ValueType(+1.0);

    public:
        AttributeVertex(
            int index,
            Domain attrDomain,
            Type   attrType,
            int             nbComp,
            int             mainAttrIdx,
            bool            hasAuxIndices = false,
            int             refAttrIdx = -1)
            : AttributeVertexBase(index, attrDomain, attrType,
                nbComp, mainAttrIdx, hasAuxIndices, refAttrIdx) {};
    };

    class AttributeVertexPosition : 
        // vertex positions (x,y,z)
        public AttributeVertex<glm::vec3> {

    public:

        // we do not allow multiple positions per vertex,
        // each position table corresponds to an object
        // so mainAttrIdx is forced to -1
        // and hasOwnIndices is forced to true;
        AttributeVertexPosition(int index)
            : AttributeVertex(index,
                Attribute::Domain::PER_VERTEX,
                Attribute::Type::POSITION,
                glm::vec3::length(),
                -1,
                true,
                -1) {};

        inline int getPositionCount() const { return values.size(); }
        inline int getTriangleCount() const { return ct.getTriangleCount(); }
    };

    class AttributeVertexUVCoord : 
        // vertex uv coordinates (u,v)
        public AttributeVertex<glm::vec2> {

    public:
        AttributeVertexUVCoord(int index, int mainAttrIdx, bool hasOwnIndices, int refAttrIdx)
            : AttributeVertex(index,
                Attribute::Domain::PER_VERTEX,
                Attribute::Type::UVCOORD,
                glm::vec2::length(),
                mainAttrIdx,
                hasOwnIndices,
                refAttrIdx) {};
    };

    class AttributeVertexColor : 
        // vertex colors (r,g,b)
        public AttributeVertex< glm::vec3>{
    public:
        AttributeVertexColor(int index, int mainAttrIdx, bool hasOwnIndices, int refAttrIdx)
            : AttributeVertex(index,
                Attribute::Domain::PER_VERTEX,
                Attribute::Type::COLOR,
                glm::vec3::length(),
                mainAttrIdx,
                hasOwnIndices,
                refAttrIdx) {};
    };

    class AttributeVertexNormal : 
        // vertex normal vector (x,y,z)
        public AttributeVertex<glm::vec3> {
    public:
        AttributeVertexNormal(int index, int mainAttrIdx, bool hasOwnIndices, int refAttrIdx)
            : AttributeVertex(index,
                Attribute::Domain::PER_VERTEX,
                Attribute::Type::NORMAL,
                glm::vec3::length(),
                mainAttrIdx,
                hasOwnIndices,
                refAttrIdx) {};
    };

    template<int dim>
    class AttributeVertexGeneric :
        // vertex normal vector (x,y,z)
        public AttributeVertex<glm::vec<dim,float>> {
    public:

        using vecN = glm::vec<dim, float>;
        using dvecN = glm::vec<dim, double, glm::defaultp>;
        using ivecN = glm::vec<dim, int>;

        AttributeVertexGeneric(int index, int mainAttrIdx, bool hasOwnIndices, int refAttrIdx)
            : AttributeVertex<vecN>(index,
                Attribute::Domain::PER_VERTEX,
                Attribute::Type::GENERIC,
                dim,
                mainAttrIdx,
                hasOwnIndices,
                refAttrIdx) {};
    };

    template<class ValueType>
    class AttributeFace : public Attribute {
    public:
        // the per face attributes (values)
        // the values are dereferenced by ct.V if hasOwnIndices is true
        // otherwise they are dereference by the V table of the attribute of index mainAttrIdx
        std::vector<ValueType> values;

    public:
        AttributeFace(
            int index,
            Domain attrDomain,
            Type   attrType,
            int             nbComp,
            int             mainAttrIdx,
            bool            hasAuxIndices = false)
            : Attribute(index, attrDomain, attrType,
                nbComp, mainAttrIdx, hasAuxIndices) {};
    };

    // Per face material Id
    class AttributeFaceMaterialId :
        // per face material id
        public AttributeFace<int> {

    public:
        AttributeFaceMaterialId(int index, int mainAttrIdx) :
            AttributeFace(index, 
                Attribute::Domain::PER_FACE,
                Attribute::Type::MATERIAL_ID,
                1,
                mainAttrIdx,
                false) {};
    };

}  // namespace eb

#endif