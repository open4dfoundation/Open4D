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

#ifndef _EB_CTMESH_EXT_H_
#define _EB_CTMESH_EXT_H_

 //
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
// mathematics
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
//
#include "ebAttribute.h"
#include "ebAttributeImpl.h"

namespace eb {

    // Extensible representation of a mesh
    // based on Corner Table data structure
    // Specifically designed for the EdgeBreaker codec
    class CTMeshExt {
    private:
        // the attributes of the mesh
        std::vector<Attribute*> attributes;
        // dereference the primary position table of each object
        std::vector<int> objects;

    public:
        CTMeshExt() {}

        ~CTMeshExt() {
            // Caution : attribute destructors are invoked in the destroy
            // function, there is no  virtual inheritance defined
            for (auto attr : attributes) { destroy(attr); }
            attributes.clear();
        }

        inline bool isPrimaryAttribute(Attribute* attr) const {
            if (attr->domain == Attribute::Domain::PER_VERTEX
                && attr->type == Attribute::Type::POSITION
                && attr->mainAttrIdx == -1) {
                return true;
            }
            return false;
        }

        inline void exitIfNotPrimaryAttribute(int attrIdx) const {
            if (attrIdx < 0 || attrIdx >= attributes.size()) {
                std::cerr << "Error: invalid mainAttrIdx not in attribuute indices range"
                    << std::endl;
                exit(0);
            }
            const auto attr = getAttribute(attrIdx);
            if (!isPrimaryAttribute(attr)) {
                std::cerr << "Error: mainAttrIdx is not a primary index attribute"
                    << std::endl;
                exit(0);
            }
        }

        // return the list of attributes
        inline const std::vector<Attribute*>& getAttributes() const {
            return attributes;
        }

        // dereferences an attribute using its index
        // warning: no sanity check
        inline Attribute* getAttribute(int index) const { return attributes[index]; }

        // dereference the primary position table of each object
        // an object is defined as:
        //   per vertex position attributes
        //   not referencing any other attribute
        // this is used for iterations over objects
        inline const std::vector<int>& getObjects() const { return objects; }

        // create position atttibute, return its index
        inline int createAttributeVertexPosition() {
            const auto attrIdx = attributes.size();
            attributes.push_back(new AttributeVertexPosition(attrIdx));
            objects.push_back(attrIdx);
            return attrIdx;
        }

        // create AttributeVertexUVCoord atttibute, return its index
        // performs sanity checks, exit(0) if not ok
        // mainAttrIdx has to be >= 0, and the attribute of index mainAttrIdx
        // must be a primary position attribute.
        inline int createAttributeVertexUVCoord(int  mainAttrIdx,
            bool hasOwnIndices, int refAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            attributes.push_back(
                new AttributeVertexUVCoord(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx));
            return attrIdx;
        }

        // create AttributeVertexColor atttibute, return its index
        // performs sanity checks, exit(0) if not ok
        // mainAttrIdx has to be >= 0, and the attribute of index mainAttrIdx
        // must be a primary position attribute.
        inline int createAttributeVertexColor(int mainAttrIdx, bool hasOwnIndices, int refAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            attributes.push_back(new AttributeVertexColor(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx));
            return attrIdx;
        }

        // create AttributeVertexGeneric atttibute, return its index
        // performs sanity checks, exit(0) if not ok
        // mainAttrIdx has to be >= 0, and the attribute of index mainAttrIdx
        // must be a primary position attribute.
        template<int dim>
        inline int createAttributeVertexGeneric(int mainAttrIdx, bool hasOwnIndices, int refAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            attributes.push_back(new AttributeVertexGeneric<dim>(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx));
            return attrIdx;
        }

        inline int createAttributeVertexGenericN(int dim, int mainAttrIdx, bool hasOwnIndices, int refAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            switch (dim)
            {
            case 1: attributes.push_back(new AttributeVertexGeneric<1>(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx)); break;
            case 2: attributes.push_back(new AttributeVertexGeneric<2>(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx)); break;
            case 3: attributes.push_back(new AttributeVertexGeneric<3>(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx)); break;
            case 4: attributes.push_back(new AttributeVertexGeneric<4>(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx)); break;
            default: std::cout << "Error : generic dimension beyond current code capability" << std::endl; exit(0);
            }
            return attrIdx;
        }

        // create AttributeVertexNormal atttibute, return its index
        // performs sanity checks, exit(0) if not ok
        // mainAttrIdx has to be >= 0, and the attribute of index mainAttrIdx
        // must be a primary position attribute.
        inline int createAttributeVertexNormal(int mainAttrIdx, bool hasOwnIndices, int refAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            attributes.push_back(new AttributeVertexNormal(attrIdx, mainAttrIdx, hasOwnIndices, refAttrIdx));
            return attrIdx;
        }

        // create AttributeFaceMaterialId atttibute, return its index
        // performs sanity checks, exit(0) if not ok
        // mainAttrIdx has to be >= 0, and the attribute of index mainAttrIdx
        // must be a primary position attribute.
        inline int createAttributeFaceMaterialId(int  mainAttrIdx) {
            // sanity check
            exitIfNotPrimaryAttribute(mainAttrIdx);
            //
            const auto attrIdx = attributes.size();
            attributes.push_back(new AttributeFaceMaterialId(attrIdx, mainAttrIdx));
            return attrIdx;
        }

    private:
        // we do that to prevent the need
        // for polymorphic objects
        // for performance reasons.
        inline void destroy(Attribute* attr) {
            if (attr->domain == Attribute::Domain::PER_VERTEX) {
                switch ( attr->type ){
                case Attribute::Type::POSITION:
                    delete static_cast<AttributeVertexPosition*>(attr);
                    break;
                case Attribute::Type::UVCOORD:
                    delete static_cast<AttributeVertexUVCoord*>(attr);
                    break;
                case Attribute::Type::NORMAL:
                    delete static_cast<AttributeVertexNormal*>(attr);
                    break;
                case Attribute::Type::GENERIC:
                {
                    switch (attr->nbComp) {
                    case 1:delete static_cast<AttributeVertexGeneric<1>*>(attr); break;
                    case 2:delete static_cast<AttributeVertexGeneric<2>*>(attr); break;
                    case 3:delete static_cast<AttributeVertexGeneric<3>*>(attr); break;
                    case 4:delete static_cast<AttributeVertexGeneric<4>*>(attr); break;
                    }
                    break;
                }
                default:
                    std::cout << "Error: type not properly deleted" << std::endl;
                }
            }
            else if (attr->domain == Attribute::Domain::PER_FACE) {
                switch (attr->type) {
                case Attribute::Type::MATERIAL_ID:
                    delete static_cast<AttributeFaceMaterialId*>(attr);
                    break;
                default:
                    std::cout << "Error: type not properly deleted" << std::endl;
                }
            }
            else {
                std::cout << "Error: type not properly deleted" << std::endl;
            }
        }
    };
}  // namespace eb

#endif