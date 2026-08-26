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

//
#include <set>
#include <map>
#include <list>
#include <stack>
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <time.h>
#include <unordered_map>
// mathematics
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
// internal
#include "ebChrono.h"
#include "ebModelConverter.h"

using namespace eb;

#define COUT if ( verbose ) std::cout << "EB"

// output shall be pristine
bool
ModelConverter::convertCTMeshToModel(
    const CTMeshExt& input,
    Model& output,
    bool deduplicate,
    bool verbose)
{
    if (input.getObjects().size() == 0)
        return false;

    auto attrPos = static_cast<AttributeVertexPosition*>(
        input.getAttribute(input.getObjects()[0]));

    /* we construct an array of MeshAttribute whose values are copied from CTMeshExt attributes */
    /* the first is assumed to be the primary position attribute */

    // data type usage could be updated to enable up to 32 bits integer values in CTMeshExt attribute values
    // then final conversion to the expected output format could be deferred to just before the potential dequantization
    // in the current implementation attribute values are 32b float and indices 32b integers in CTMeshExt attributes
    // which limits the maximal quantization value

    output.attributes.emplace_back(std::make_shared<MeshAttribute>(
        AttrType::POSITION,
        DataType::FLOAT32,
        3,
        AttrDomain::PER_VERTEX,
        new std::vector<float>((float*)attrPos->values.data(), (float*)attrPos->values.data()+ attrPos->values.size()*3),
        attrPos->ct.V));

    MeshAttribute* mattrPos = output.attributes.back().get();

    /* then we loop over CTMeshExt attributes and construct a MeshAttribute objects for each decoded attribute */

    // the same data type restriction as above applies to all those attributes

    std::vector<std::tuple< bool, size_t, std::vector<float>*, std::vector<float>*, std::vector<int32_t>*>> attrVec;
    std::vector<size_t> attrIdx;

    for (auto attr : input.getAttributes()) {
        if (attr->mainAttrIdx != attrPos->index)
            continue;
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::UVCOORD)
        {
            auto attrUv = static_cast<AttributeVertexUVCoord*>(attr);

            output.attributes.emplace_back(std::make_shared<MeshAttribute>(
                AttrType::TEXCOORD,
                DataType::FLOAT32,
                2,
                AttrDomain::PER_VERTEX,
                new std::vector<float>((float*)attrUv->values.data(), (float*)attrUv->values.data()+ 2* attrUv->values.size()),
                attrUv->ct.V,
                attrUv->hasOwnIndices ? -1 : attrUv->refAuxIdx
                ));

            const auto& mattrUv = output.attributes.back().get();

            if (deduplicate)
            {
                attrVec.emplace_back(std::make_tuple(attrUv->refAuxIdx == 0, mattrUv->getDim(), nullptr, mattrUv->getData<float>(), mattrUv->getIndices<int32_t>()));
                attrIdx.push_back(output.attributes.size() - 1);
            }
            continue;
        }
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::NORMAL)
        {
            auto attrNrm = static_cast<AttributeVertexNormal*>(attr);

            output.attributes.emplace_back(std::make_shared<MeshAttribute>(
                AttrType::NORMAL,
                DataType::FLOAT32,
                3, // normal input / outputs always have 3 components - could be extended with a 2 component quantized octahedral
                AttrDomain::PER_VERTEX,
                new std::vector<float>((float*)attrNrm->values.data(), (float*)attrNrm->values.data() + 3 * attrNrm->values.size()),
                attrNrm->ct.V,
                attrNrm->hasOwnIndices ? -1 : attrNrm->refAuxIdx
            ));

            const auto& mattrNrm = output.attributes.back().get();

            if (deduplicate)
            {
                attrVec.emplace_back(std::make_tuple(attrNrm->refAuxIdx == 0, mattrNrm->getDim(), nullptr, mattrNrm->getData<float>(), mattrNrm->getIndices<int32_t>()));
                attrIdx.push_back(output.attributes.size() - 1);
            }
            continue;
        }
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::GENERIC)
        {
            auto processGen = [&](auto* attrGen) {
                output.attributes.emplace_back(std::make_shared<MeshAttribute>(
                    AttrType::GENERIC,
                    DataType::FLOAT32,
                    attrGen->nbComp,
                    AttrDomain::PER_VERTEX,
                    new std::vector<float>((float*)attrGen->values.data(), (float*)attrGen->values.data() + attrGen->nbComp * attrGen->values.size()),
                    attrGen->ct.V,
                    attrGen->hasOwnIndices ? -1 : attrGen->refAuxIdx
                ));

                const auto& mattrGen = output.attributes.back().get();

                if (deduplicate)
                {
                    attrVec.emplace_back(std::make_tuple(attrGen->refAuxIdx == 0, mattrGen->getDim(), nullptr, mattrGen->getData<float>(), mattrGen->getIndices<int32_t>()));
                    attrIdx.push_back(output.attributes.size() - 1);
                }
            };
            switch (attr->nbComp)
            {
            case 1: processGen(static_cast<AttributeVertexGeneric<1>*>(attr)); break;
            case 2: processGen(static_cast<AttributeVertexGeneric<2>*>(attr)); break;
            case 3: processGen(static_cast<AttributeVertexGeneric<3>*>(attr)); break;
            case 4: processGen(static_cast<AttributeVertexGeneric<4>*>(attr)); break;
            };
            continue;
        }
        if (attr->domain == Attribute::Domain::PER_FACE
            && attr->type == Attribute::Type::MATERIAL_ID)
        {
            auto attrFid = static_cast<AttributeFaceMaterialId*>(attr);
            // the current implmementation assumes per face material id values have an
            // implicit indexing [0..(nbfaces-1)]
            output.attributes.emplace_back(std::make_shared<MeshAttribute>(
                AttrType::MATERIAL_ID,
                DataType::INT32,
                1,
                AttrDomain::PER_FACE,
                new std::vector<int32_t>(
                    (int32_t*)attrFid->values.data(),
                    (int32_t*)attrFid->values.data() + 1 * attrFid->values.size())
            ));
            const auto & mattrFid = output.attributes.back().get();
            continue;
        }
    }

    // non manifold deduplication, remove isolated vertices
    if (deduplicate)
    {
        const std::vector<int> emptyInt;
        const std::vector<int> emptyFloat;

        const std::vector<float>* vertices = mattrPos->getData<float>();
        std::vector<int32_t>* triangles = mattrPos->getIndices<int32_t>();

        // remove duplicate vertices
        int idx = 0;
        std::vector<int> mapping(vertices->size() / 3, -1);
        for (int i = 0; i < triangles->size(); ++i) {
            if (mapping[(*triangles)[i]] < 0) {
                mapping[(*triangles)[i]] = idx++;
            }
        }
        for (int i = 0; i < triangles->size(); ++i) {
            (*triangles)[i] = mapping[(*triangles)[i]];
        }
        std::vector<float>* newVertices = new std::vector<float>(3 * idx);
        std::vector<float>* newUVCoords = nullptr;
        for (auto& attr : attrVec)
        {
            auto useMainIdx = std::get<0>(attr);
            if (useMainIdx)
            {
                const auto dim = std::get<1>(attr);
                auto& newValues = std::get<2>(attr);
                newValues = new std::vector<float>(dim * idx);
            }
        }

        for (int i = 0; i < mapping.size(); ++i) {
            if (mapping[i] >= 0)
            {
                (*newVertices)[3 * mapping[i] + 0] = (*vertices)[3 * i + 0];
                (*newVertices)[3 * mapping[i] + 1] = (*vertices)[3 * i + 1];
                (*newVertices)[3 * mapping[i] + 2] = (*vertices)[3 * i + 2];
                for (const auto& attr : attrVec)
                {
                    auto useMainIdx = std::get<0>(attr);                    
                    if (useMainIdx)
                    {
                        const auto dim = std::get<1>(attr);
                        auto& newValues = std::get<2>(attr);
                        const auto oldValues = std::get<3>(attr);
                        for (auto k=0;k<dim;++k)
                            (*newValues)[dim * mapping[i] + k] = (*oldValues)[dim * i + k];
                    }
                }
            }
        }
        mattrPos->setData(newVertices);

        for (auto i = 0; i < attrVec.size(); ++i)
        {
            auto& attrV = attrVec[i];
            auto& attr = output.attributes[attrIdx[i]];
            auto useMainIdx = std::get<0>(attrV);
            if (useMainIdx)
            {
                auto& newValues = std::get<2>(attrV);
                attr->setData(newValues);
            }
            else {  //remove duplicate attribute when attribute uses auxiliary index table

                const auto dim = std::get<1>(attrV);
                auto& newValues = std::get<2>(attrV);
                const auto oldValues = std::get<3>(attrV);
                const auto indices = std::get<4>(attrV);

                int idx = 0;
                std::vector<int> AttrMapping(oldValues->size() / dim, -1);
                for (int i = 0; i < indices->size(); ++i) {
                    if (AttrMapping[(*indices)[i]] < 0) {
                        AttrMapping[(*indices)[i]] = idx++;
                    }
                }
                for (int i = 0; i < indices->size(); ++i) {
                    (*indices)[i] = AttrMapping[(*indices)[i]];
                }
                newValues = new std::vector<float>(dim * idx);
                for (int i = 0; i < AttrMapping.size(); ++i) {
                    if (AttrMapping[i] >= 0) {
                        for (auto k = 0; k < dim; ++k)
                            (*newValues)[dim * AttrMapping[i] + k] = (*oldValues)[dim * i + k];
                    }
                }
                attr->setData(newValues);
            }
        }
    }

    return true;
}

// output shall be pristine
bool
ModelConverter::convertModelToCTMesh(const Model& input_orig,
    CTMeshExt& output,
    bool keepTrackOfVertexDeduplications,
    bool useMainIndexIfEqual,
    bool unify,
    bool unifyPositionsOnly,
    bool verbose) {
    auto t = eb::now();

    // optionnaly unify positions and attributes
    eb::Model unified;
    if (unify)
    {
        eb::unify(input_orig, unified, unifyPositionsOnly);
        COUT << "   IFStoCT Time on unification pre-processing: " << eb::elapsed(t) << " ms." << std::endl;
        t = eb::now();
    }

    // use either original input or unified mesh depending on unify option
    const Model& input = (unify) ? unified : input_orig;

    int i, j, c;

    int attrPosIdx = 0; // should always be 0 with current restriction that position is first and there is a single position attribute
    MeshAttribute* mattrPos = nullptr;// input.attributes[0].get();

    for (auto attr : input.attributes) {
        if (!mattrPos&& attr->getType() != AttrType::POSITION)
        {
            std::cerr << "Error: position attribute expected fist" << std::endl;
            exit(0); // could be fixed later
        }
        else if (attr->getType() == AttrType::POSITION)
        {
            if (mattrPos || attr->getDomain() != AttrDomain::PER_VERTEX) {
                std::cerr << "Error: a single per vertex position attribute is expected in this version" << std::endl;
                exit(0); // could be extended later                
            }
            mattrPos = attr.get();
            const auto nverts = mattrPos->getDataCnt();
            const auto ntrigs = mattrPos->getIndexCnt(); // the getIndexCnt function returns the number of index triplets(triangle faces)

            attrPosIdx = output.createAttributeVertexPosition();
            AttributeVertexPosition* attrPos = static_cast<AttributeVertexPosition*>(output.getAttribute(attrPosIdx));

            const std::vector<float>* vertices = mattrPos->getData<float>();
            std::vector<int32_t>* triangles = mattrPos->getIndices<int32_t>();

            attrPos->values.resize(nverts);
            attrPos->ct.O.resize(3 * ntrigs, -2);
            attrPos->ct.V = *triangles;
            attrPos->ct.V.resize(3 * ntrigs);

            // we use memcpy since vectors are not of same nature - this should be avoided
            memcpy(attrPos->values.data(),
                (float*)vertices->data(),
                vertices->size() * sizeof(float));
        }
        else if (attr->getType() == AttrType::TEXCOORD)
        {
            if (!attr->getDataCnt() || attr->getDomain() != AttrDomain::PER_VERTEX) {
                std::cerr << "Error: non empty Texture Coordinates are expected to be per vertex/corner" << std::endl;
                exit(0); // could be extended later                
            }
            bool hasSeparateIndex = (attr->getIndexRef() < 0) && (attr->getIndexCnt() != 0);
            bool forceMainIfEqual = false;
            if (hasSeparateIndex && useMainIndexIfEqual)
            {
                hasSeparateIndex = (attr->getIndexCnt() != mattrPos->getIndexCnt()) || (*attr->getIndices<uint32_t>() != *mattrPos->getIndices<uint32_t>());
                // if (!hasSeparateIndex) attr->setIndexRef(0); // no need to alter input model
                forceMainIfEqual = !hasSeparateIndex;
            }
            if (hasSeparateIndex)
            {
                COUT << "   IFStoCT Using separate index table for UVs" << std::endl;
            }
            else
            {
                if (forceMainIfEqual || attr->getIndexRef() == 0)
                {
                    COUT << "   IFStoCT Using primary index table for UVs" << std::endl;
                }
                else
                {
                    COUT << "   IFStoCT Using auxilliary index table " << attr->getIndexRef() << " for UVs" << std::endl;
                }
            }

            AttributeVertexUVCoord* attrUv = static_cast<AttributeVertexUVCoord*>(output.getAttribute(
                output.createAttributeVertexUVCoord(attrPosIdx, hasSeparateIndex, forceMainIfEqual ? 0 :  attr->getIndexRef())));
            attrUv->values.resize(attr->getDataCnt()); // CAUTION : no size checks
            
            const auto ntrigs = mattrPos->getIndexCnt(); // the getIndexCnt function returns the number of index triplets(triangle faces)
            const std::vector<float>* vertices = attr->getData<float>(); // CAUTION : implementation currently limited to float attribute values         

            if (hasSeparateIndex) {  // V used instead of TC if not hasSeparateIndex
                // attrUv->ct.O will be set by auxTopoDecode only if using degree traversal
                std::vector<int32_t>* triangles = attr->getIndices<int32_t>();
                attrUv->ct.V = *triangles;
                attrUv->ct.V.resize(3 * ntrigs);
                attrUv->auxO.resize(3 * ntrigs, -2);
            }
            else {
                attrUv->ct.V.resize(3 * ntrigs, -1);
                // ct.O is potentially generated multiple times when degree traversal
            }

            // we use memcpy since vectors are not of same nature - this should be avoided
            memcpy(attrUv->values.data(),
                (float*)vertices->data(),
                vertices->size() * sizeof(float));
        }
        else if (attr->getType() == AttrType::NORMAL)
        {
            if (!attr->getDataCnt() || attr->getDomain() != AttrDomain::PER_VERTEX) {
                std::cerr << "Error: non empty Normals are expected to be per vertex/corner" << std::endl;
                exit(0); // could be extended later                
            }
            bool hasSeparateIndex = (attr->getIndexRef() < 0) && (attr->getIndexCnt() != 0);
            bool forceMainIfEqual = false;
            if (hasSeparateIndex && useMainIndexIfEqual)
            {
                hasSeparateIndex = (attr->getIndexCnt() != mattrPos->getIndexCnt()) || (*attr->getIndices<uint32_t>() != *mattrPos->getIndices<uint32_t>());
                // if (!hasSeparateIndex) attr->setIndexRef(0); // no need to alter input model
                forceMainIfEqual = !hasSeparateIndex;
            }
            if (hasSeparateIndex)
            {
                COUT << "   IFStoCT Using separate index table for Normals" << std::endl;
            }
            else
            {
                if (forceMainIfEqual || attr->getIndexRef() == 0)
                {
                    COUT << "   IFStoCT Using primary index table for Normals" << std::endl;
                }
                else
                {
                    COUT << "   IFStoCT Using auxilliary index table " << attr->getIndexRef() << " for Normals" << std::endl;
                }
            }

            AttributeVertexNormal* attrNrm = static_cast<AttributeVertexNormal*>(output.getAttribute(
                output.createAttributeVertexNormal(attrPosIdx, hasSeparateIndex, forceMainIfEqual ? 0 : attr->getIndexRef())));
            attrNrm->values.resize(attr->getDataCnt()); 

            const auto ntrigs = mattrPos->getIndexCnt();
            const std::vector<float>* vertices = attr->getData<float>(); // CAUTION : implementation currently limited to float attribute values 

            if (hasSeparateIndex) {  // V used instead of TC if not hasSeparateattrIndex
                // attrUv->ct.O will be set by auxTopoDecode only if using degree traversal
                std::vector<int32_t>* triangles = attr->getIndices<int32_t>();
                attrNrm->ct.V = *triangles;
                attrNrm->ct.V.resize(3 * ntrigs);
                attrNrm->auxO.resize(3 * ntrigs, -2);
            }
            else {
                attrNrm->ct.V.resize(3 * ntrigs, -1);
                // ct.O is potentially generated multiple times when degree traversal
            }

            // we use memcpy since vectors are not of same nature - this should be avoided
            memcpy(attrNrm->values.data(),
                (float*)vertices->data(),
                vertices->size() * sizeof(float));
        }
        else if (attr->getType() == AttrType::GENERIC)
        {
            if (!attr->getDataCnt() || attr->getDomain() != AttrDomain::PER_VERTEX) {
                std::cerr << "Error: non empty Generic are expected to be per vertex/corner" << std::endl;
                exit(0); // could be extended later                
            }
            bool hasSeparateIndex = (attr->getIndexRef() < 0) && (attr->getIndexCnt() != 0);
            bool forceMainIfEqual = false;
            if (hasSeparateIndex && useMainIndexIfEqual)
            {
                hasSeparateIndex = (attr->getIndexCnt() != mattrPos->getIndexCnt()) || (*attr->getIndices<uint32_t>() != *mattrPos->getIndices<uint32_t>());
                // if (!hasSeparateIndex) attr->setIndexRef(0); // no need to alter input model
                forceMainIfEqual = !hasSeparateIndex;
            }
            if (hasSeparateIndex)
            {
                COUT << "   IFStoCT Using separate index table for Generic" << std::endl;
            }
            else
            {
                if (forceMainIfEqual || attr->getIndexRef() == 0)
                {
                    COUT << "   IFStoCT Using primary index table for Generic" << std::endl;
                }
                else
                {
                    COUT << "   IFStoCT Using auxilliary index table " << attr->getIndexRef() << " for Generic" << std::endl;
                }
            }

            auto* attrGenN = output.getAttribute(
                output.createAttributeVertexGenericN(attr->getDim(), attrPosIdx, hasSeparateIndex, forceMainIfEqual ? 0 : attr->getIndexRef()));

            auto processGen = [&](auto* attrGen) {
                attrGen->values.resize(attr->getDataCnt());

                const auto ntrigs = mattrPos->getIndexCnt();
                const std::vector<float>* vertices = attr->getData<float>(); // CAUTION : implementation currently limited to float attribute values 

                if (hasSeparateIndex) {  // V used instead of TC if not hasSeparateIndex
                    // attrUv->ct.O will be set by auxTopoDecode only if using degree traversal
                    std::vector<int32_t>* triangles = attr->getIndices<int32_t>();
                    attrGen->ct.V = *triangles;
                    attrGen->ct.V.resize(3 * ntrigs);
                    attrGen->auxO.resize(3 * ntrigs, -2);
                }
                else {
                    attrGen->ct.V.resize(3 * ntrigs, -1);
                    // ct.O is generated multiple times potentially when degree traversal
                }

                // we use memcpy since vectors are not of same nature - this should be avoided
                memcpy(attrGen->values.data(),
                    (float*)vertices->data(),
                    vertices->size() * sizeof(float));
            };
            switch (attrGenN->nbComp) 
            {
            case 1: processGen(static_cast<AttributeVertexGeneric<1>*>(attrGenN)); break;
            case 2: processGen(static_cast<AttributeVertexGeneric<2>*>(attrGenN)); break;
            case 3: processGen(static_cast<AttributeVertexGeneric<3>*>(attrGenN)); break;
            case 4: processGen(static_cast<AttributeVertexGeneric<4>*>(attrGenN)); break;
            };
            continue;
        }
        else if (attr->getType() == AttrType::MATERIAL_ID)
        {
            if (!attr->getDataCnt() || attr->getDomain() != AttrDomain::PER_FACE) {
                std::cerr << "Error: non empty meterial id values are expected to be per face" << std::endl;
                exit(0); // could be extended later                
            }
            const auto ntrigs = mattrPos->getIndexCnt(); // the getIndexCnt function returns the number of index triplets(triangle faces)
            // it is assumed there is one value per face in the data array
            if (attr->getDataCnt() != ntrigs) {
                std::cerr << "Error: expecting as many ids as faces" << std::endl;
                exit(0); // could be extended later, using indices                 
            }
            AttributeFaceMaterialId* attrFid = static_cast<AttributeFaceMaterialId*>(
                output.getAttribute(output.createAttributeFaceMaterialId(attrPosIdx)));
            attrFid->values.resize(attr->getDataCnt());

            const std::vector<int>* vertices = attr->getData<int>(); // CAUTION : implementation currently limited to int MATERIAL_ID values 
            attrFid->values = *(vertices);
        }

    }


    COUT << "   IFStoCT Time on init tables: " << eb::elapsed(t) << " ms." << std::endl;
    t = eb::now();

    // Get vertices' stars
    struct HashPair {
        std::size_t operator()(const std::pair<int, int> pair) const {
            return std::hash<int>()(pair.first);  // always faster ?
        }
    };

    // should always be at pos 0 for now
    AttributeVertexPosition* attrPos = static_cast<AttributeVertexPosition*>(output.getAttribute(attrPosIdx));
    const auto ntrigs = mattrPos ? mattrPos->getIndexCnt() : 0;

    //  unordered_multimap preferred to std::multimap for perfs
#if defined FAST_MEB_ENCODE
    std::unordered_multimap<std::pair<int, int>, int, HashPair> adjacency;
#else
    std::multimap<std::pair<int, int>, int> adjacency;
#endif

    int nbDegenerateTri = 0;

    // attempt to pair matching half edges
    for (c = 0; c < 3 * ntrigs; c++) {
        // rewrite to avoid /3 +1/2 mod
        i = attrPos->ct.V[attrPos->ct.n(c)];
        j = attrPos->ct.V[attrPos->ct.p(c)];
        const auto k = attrPos->ct.V[c];
        // skip degenerated // but should be disconsidered later also !!? ... or will result in separate coding of degenerate tri
        if ((i == j) || (i == k) || (j == k))
        {
            ++nbDegenerateTri;
            continue;
        }
        // if ordered edges do match, set opposite - as done in draco - but wrong (non optimal) pairing could result
        // should we break all and reconnect later ?
        auto pos = adjacency.find({ j, i }); // should have opposite winding
        // matching corner ?
        if (pos != adjacency.end())
        {
            const auto mc = pos->second;
            // skip mirror case
            if (k == attrPos->ct.V[mc])
                continue;
            attrPos->ct.O[c] = mc;
            attrPos->ct.O[mc] = c;
            adjacency.erase(pos);  // would be faster with big vector ?
        }
        else {
            // multi map requird here for completeness - or vector of half edge data
            adjacency.insert({ {i, j}, c });
        }
    }
    adjacency.clear();

    COUT << "   IFStoCT Time on init opposites: " << eb::elapsed(t) << " ms." << std::endl;
    t = eb::now();

    if (nbDegenerateTri != 0) {
        std::cout << "Warning: " << nbDegenerateTri << " degenerate triangles were found" << std::endl;
    }

    //////////////////////////////////////////////////////
    // FOLLOWING PORTION OF CODE IS ADAPTED AND EXTENDED FROM GOOGLE DRACO
    // https://github.com/google/draco/blob/master/LICENSE
    // related license fro draco is replicated at the end of this file

    // break remaining manifold by analysing vertex indices in 1-rings around corners
    std::vector<int> MC(attrPos->ct.V.size(), false); // Corners marking array
    if (true) {
        std::vector<std::pair<int, int>> sink_vertices; // vert , corner
        bool mesh_connectivity_updated = false;
        do {
            mesh_connectivity_updated = false;
            for (c = 0; c < 3 * ntrigs; c++) {
                if (MC[c]) continue;
                sink_vertices.clear();
                int first_c = c;
                int current_c = c;
                int next_c;
                while (next_c = attrPos->ct.n(attrPos->ct.O[attrPos->ct.n(current_c)]),
                    next_c != first_c && next_c >= 0 && !MC[next_c]) {
                    current_c = next_c;
                }
                first_c = current_c;
                do {
                    MC[current_c] = true;
                    const int sink_c = attrPos->ct.n(current_c);
                    const int sink_v = attrPos->ct.V[sink_c];

                    const int edge_corner = attrPos->ct.p(current_c);
                    bool vertex_connectivity_updated = false;
                    for (auto&& attached_sink_vertex : sink_vertices) {
                        if (attached_sink_vertex.first == sink_v) {
                            const int other_edge_corner = attached_sink_vertex.second;
                            const int opp_edge_corner = attrPos->ct.O[edge_corner];
                            if (opp_edge_corner == other_edge_corner) continue;

                            const int opp_other_edge_corner =
                                attrPos->ct.O[other_edge_corner];
                            if (opp_edge_corner >= 0) attrPos->ct.O[opp_edge_corner] = -2;
                            if (opp_other_edge_corner >= 0)
                                attrPos->ct.O[opp_other_edge_corner] = -2;
                            if (edge_corner >= 0) attrPos->ct.O[edge_corner] = -2;
                            if (other_edge_corner >= 0)
                                attrPos->ct.O[other_edge_corner] = -2;
                            vertex_connectivity_updated = true;
                            break;
                        }
                    }
                    if (vertex_connectivity_updated) {
                        mesh_connectivity_updated = true;
                        break;
                    }
                    std::pair<int, int> new_sink_vert;
                    new_sink_vert.first = attrPos->ct.V[attrPos->ct.p(current_c)];
                    new_sink_vert.second = sink_c;
                    sink_vertices.push_back(new_sink_vert);
                    current_c = attrPos->ct.p(attrPos->ct.O[attrPos->ct.p(current_c)]);
                } while (current_c != first_c && current_c >= 0);
            }
        } while (mesh_connectivity_updated);
    }

    COUT << "   IFStoCT Time on analysis: " << eb::elapsed(t) << " ms." << std::endl;
    t = eb::now();


    // helper funcion to duplicate one value for any attribute type
    auto duplicateValue = [&](auto* attr, int index) {
        attr->values.push_back(attr->values[index]);
        };


    // build a list of attributes sharing the main index to replicate duplication
    std::vector<Attribute*> sameIndexAttrVec;
    for (auto* attrib : output.getAttributes())
    {
        if (attrib->type == eb::Attribute::Type::POSITION)
            continue;
        if (attrib->domain != eb::Attribute::Domain::PER_VERTEX)
            continue;
        if (attrib->refAuxIdx == 0)
        {
            sameIndexAttrVec.push_back(attrib);
        }
    }

    // duplicate points where needed
    std::vector<bool> M(attrPos->values.size(), false);              // Vertex marking array
    MC.assign(attrPos->ct.V.size(), false);  // resets corners marked
    std::vector<std::pair<int, int>> matchedOpposites;
    int                              pairedAfterManifoldSplit = 0;
    auto                             numVertex = attrPos->values.size();
    int                              addedVertices = 0;
    int                              numSplitVertices = 0;
    attrPos->duplicatesMap.clear();
    for (c = 0; c < 3 * ntrigs; c++) {
        bool manifoldVertex = false;
        if (MC[c]) continue;
        auto vertexIndex = attrPos->ct.V[c];
        if (M[vertexIndex]) {
            manifoldVertex = true;
            M.push_back(false);
            attrPos->values.push_back(attrPos->values[vertexIndex]);
            // loop through the list -> would be easier to store the duplication list and replicate per attribute later
            for (auto* attr : sameIndexAttrVec)
            {
                switch (attr->type)
                {
                case eb::Attribute::Type::UVCOORD: duplicateValue(static_cast<AttributeVertexUVCoord*>(attr), vertexIndex); break;
                case eb::Attribute::Type::NORMAL: duplicateValue(static_cast<AttributeVertexNormal*>(attr), vertexIndex); break;
                case eb::Attribute::Type::GENERIC: 
                {
                    switch (attr->nbComp)
                    {
                    case 1:duplicateValue(static_cast<AttributeVertexGeneric<1>*>(attr), vertexIndex); break;
                    case 2:duplicateValue(static_cast<AttributeVertexGeneric<2>*>(attr), vertexIndex); break;
                    case 3:duplicateValue(static_cast<AttributeVertexGeneric<3>*>(attr), vertexIndex); break;
                    case 4:duplicateValue(static_cast<AttributeVertexGeneric<4>*>(attr), vertexIndex); break;
                    default: std::cout << "Error : unexpected or unimplemented generic dim" << std::endl; exit(0);
                    }
                }
                default: std::cout << "Error : unexpected or unimplemented type" << std::endl; exit(0);
                }
            }
            vertexIndex = numVertex++;
            // THIS IS ADDED FOR VERTEX DEDUPLICATION
            if (keepTrackOfVertexDeduplications) {
                const auto firstadd = attrPos->duplicatesMap.find(attrPos->ct.V[c]);
                const auto dupIdx = (firstadd == attrPos->duplicatesMap.end()) ? numSplitVertices : firstadd->second;
                if (firstadd == attrPos->duplicatesMap.end())
                {
                    attrPos->duplicatesMap.insert({ attrPos->ct.V[c], dupIdx });
                    numSplitVertices++;
                }
                attrPos->duplicatesMap.insert({ vertexIndex, dupIdx });
            }
            addedVertices++;
        }
        M[vertexIndex] = true;
        auto curc = c;
        int  lefm = curc;
        int  rigm = curc;
        while (curc >= 0) {
            MC[curc] = true;
            if (manifoldVertex && curc >= 0) attrPos->ct.V[curc] = vertexIndex;
            if (curc >= 0 && attrPos->ct.O[attrPos->ct.n(curc)] < 0) lefm = curc;
            curc = attrPos->ct.n(attrPos->ct.O[attrPos->ct.n(curc)]);
            if (curc == c) break;
        }
        if (curc < 0) {
            curc = attrPos->ct.p(attrPos->ct.O[attrPos->ct.p(c)]);
            while (curc >= 0) {
                MC[curc] = true;
                if (manifoldVertex && curc >= 0) attrPos->ct.V[curc] = vertexIndex;
                if (curc >= 0 && attrPos->ct.O[attrPos->ct.p(curc)] < 0) rigm = curc;
                curc = attrPos->ct.p(attrPos->ct.O[attrPos->ct.p(curc)]);
                if (curc == c) break;
            }
        }
        // THIS IS ADDED FOR EDGE MERGING
        // test appairing directly
        if (lefm != rigm && lefm > 0 && rigm > 0)
        {
            auto oln = attrPos->ct.O[attrPos->ct.n(lefm)];
            auto orp = attrPos->ct.O[attrPos->ct.p(rigm)];
            auto vl = attrPos->ct.V[attrPos->ct.p(lefm)];
            auto vr = attrPos->ct.V[attrPos->ct.n(rigm)];
            // same fan center V[c] same other edge vertex means opposites should be registered
            if ((vl == vr) && (oln < 0) && (orp < 0))
                // TO CHECK when could we could reconnect in the loop partly ?
                matchedOpposites.push_back(std::pair<int, int>(attrPos->ct.n(lefm), attrPos->ct.p(rigm)));
        }
    }

    COUT << "   IFStoCT Time on duplicate: " << eb::elapsed(t) << " ms." << std::endl;
    t = eb::now();

    // END OF CODE INSPIRED FROM DRACO
    //////////////////////////////////

    // THIS CODE IS ADDED FOR EDGE MERGING
    for (const auto& opp : matchedOpposites) {
        auto oln = attrPos->ct.O[opp.first];
        auto orp = attrPos->ct.O[opp.second];
        auto vl = attrPos->ct.V[attrPos->ct.n(opp.first)];
        auto vr = attrPos->ct.V[attrPos->ct.p(opp.second)];
        if ((vl == vr) && (oln < 0) && (orp < 0)) {
            attrPos->ct.O[opp.first] = opp.second;
            attrPos->ct.O[opp.second] = opp.first;
            pairedAfterManifoldSplit++;
        }
    }

    COUT << "   IFStoCT Time on merging: " << eb::elapsed(t) << " ms." << std::endl;
    t = eb::now();

    if (keepTrackOfVertexDeduplications) {
        COUT << "   IFStoCT Manifold handling, split vtx= " << numSplitVertices << ", added vtx = " << addedVertices << ", paired = " << pairedAfterManifoldSplit << std::endl;
    }
    else {
        COUT << "   IFStoCT Manifold handling, added vtx = " << addedVertices << ", paired = " << pairedAfterManifoldSplit << std::endl;
    }

    for (auto& attrib : output.getAttributes())
    {
        if (attrib->type == eb::Attribute::Type::POSITION)
            continue;
        if (attrib->domain != eb::Attribute::Domain::PER_VERTEX)
            continue;
        if (attrib->refAuxIdx != -1) // when indices are reused from a reference attribute
        {
            // the current implementation duplicates index related information to enable simple processing in encodeAuxiliaryConnectivity encodeAuxiliaryAttributes
            if (attrib->refAuxIdx > 0) // primary attribute as the reference (refAuxIdx==0) already taken care of in the code
            {
                // attrib->refAuxIdx > 0 is not allowed in the current specification
                std::cerr << "Warning: mesh_attribute_reference_index_plus1 > 0 " << std::endl;
                // when attrib->refAuxIdx > 0, then this code is not valid when alignment changed (discarded attribute)
                auto attribBase = static_cast<AttributeVertexBase*>(attrib);
                auto refBase = static_cast<AttributeVertexBase*>(output.getAttributes()[attrib->refAuxIdx]);
                attribBase->ct = refBase->ct;
                attribBase->auxO = refBase->auxO;
                attribBase->duplicatesMap = refBase->duplicatesMap;
            }
            continue;
        }

        // secondary attributes analysis now, based on correct O table

        // process here any attribute type (related to type of data only)
        auto processAttribute = [&](auto* attribute) {
            // NOTE processing is duplicated when attributes do share indices with a non primary reference attribute

            int nbDegenerateAttr = 0;
            MC.assign(attrPos->ct.V.size(), false);  // resets corners marked
            // NOK we should replicate O from geo to split components and add index test for seams - then second pass to add missing indices
            for (c = 0; c < 3 * ntrigs; c++) {
                if (MC[c]) continue;
                const auto oppc = attrPos->ct.O[c];
                MC[c] = true;
                if (oppc >= 0) {
                    const auto& i = attribute->ct.V[attrPos->ct.n(c)];
                    const auto& j = attribute->ct.V[attrPos->ct.p(c)];
                    const auto& k = attribute->ct.V[c];
                    if ((i == j) || (i == k) || (j == k)) {
                        // degenerated // but should be disconsidered later also !!?
                        ++nbDegenerateAttr;
                    }
                    const auto& io = attribute->ct.V[attrPos->ct.n(oppc)];
                    const auto& jo = attribute->ct.V[attrPos->ct.p(oppc)];  // pb if opp is denegerated no ?!!
                    const bool isNotSeam = (i == jo) && (j == io);
                    if (isNotSeam) {
                        attribute->auxO[c] = oppc;
                        attribute->auxO[oppc] = c;
                    }
                    MC[oppc] = true;
                }
            }

            if (nbDegenerateAttr != 0) {
                std::cout << "Warning: " << nbDegenerateAttr << " degenerate " << attribute->type << " triangles found" << std::endl;
            }

            MC.assign(attrPos->ct.V.size(), false);  // resets corners marked
            M.assign(attribute->values.size(), false);
            matchedOpposites.clear();
            pairedAfterManifoldSplit = 0;

            auto numAttrValues = attribute->values.size();
            int addedAttrVertices = 0;
            int numSplitAttrVertices = 0;
            for (c = 0; c < 3 * ntrigs; c++) {
                bool manifoldAttr = false;
                if (MC[c]) continue;
                auto attrIndex = attribute->ct.V[c];
                if (M[attrIndex]) {
                    // already visited - is manifold
                    manifoldAttr = true;
                    M.push_back(false);
                    // duplicate the attribute vertex value
                    attribute->values.push_back(attribute->values[attrIndex]);
                    attrIndex = numAttrValues++;

                    if (keepTrackOfVertexDeduplications) {
                        const auto firstAttradd = attribute->duplicatesMap.find(attribute->ct.V[c]);
                        const auto dupAttrIdx = (firstAttradd == attribute->duplicatesMap.end()) ? numSplitAttrVertices : firstAttradd->second;
                        if (firstAttradd == attribute->duplicatesMap.end()) {
                            attribute->duplicatesMap.insert({ attribute->ct.V[c], dupAttrIdx });
                            numSplitAttrVertices++;
                        }
                        attribute->duplicatesMap.insert({ attrIndex, dupAttrIdx });
                    }
                    addedAttrVertices++;
                }
                M[attrIndex] = true;
                auto curc = c;
                int lefm = curc;
                int rigm = curc;
                while (curc >= 0) {
                    MC[curc] = true;
                    if (manifoldAttr && curc >= 0) attribute->ct.V[curc] = attrIndex;
                    if (curc >= 0 && attribute->auxO[attribute->ct.n(curc)] < 0)
                        lefm = curc;
                    curc = attribute->ct.n(attribute->auxO[attribute->ct.n(curc)]);
                    if (curc == c) break;
                }
                if (curc < 0) {
                    curc = attribute->ct.p(attribute->auxO[attribute->ct.p(c)]);
                    while (curc >= 0) {
                        MC[curc] = true;
                        if (manifoldAttr && curc >= 0) attribute->ct.V[curc] = attrIndex;
                        if (curc >= 0 && attribute->auxO[attribute->ct.p(curc)] < 0)
                            rigm = curc;
                        curc = attribute->ct.p(attribute->auxO[attribute->ct.p(curc)]);
                        if (curc == c) break;
                    }
                }
                if (lefm != rigm && lefm > 0 && rigm > 0)
                {
                    auto oln = attribute->auxO[attrPos->ct.n(lefm)];
                    auto orp = attribute->auxO[attrPos->ct.p(rigm)];
                    auto vl = attribute->ct.V[attrPos->ct.p(lefm)];
                    auto vr = attribute->ct.V[attrPos->ct.n(rigm)];
                    // same fan center V[c] same other edge vertex means opposites should be registered 
                    if ((vl == vr) && (oln < 0) && (orp < 0))
                        // TO CHECK when could we could reconnect in the loop partly ?
                        matchedOpposites.push_back(std::pair<int, int>(attribute->ct.n(lefm), attribute->ct.p(rigm)));
                }
            }

            for (const auto& opp : matchedOpposites)
            {
                auto oln = attribute->auxO[opp.first];
                auto orp = attribute->auxO[opp.second];
                auto vl = attribute->ct.V[attribute->ct.n(opp.first)];
                auto vr = attribute->ct.V[attribute->ct.p(opp.second)];
                if ((vl == vr) && (oln < 0) && (orp < 0))
                {
                    attribute->auxO[opp.first] = opp.second;
                    attribute->auxO[opp.second] = opp.first;
                    pairedAfterManifoldSplit++;
                }
            }
            COUT << "   IFStoCT Time on " << attribute->type << " analysis: " << eb::elapsed(t) << " ms."
                << std::endl;
            t = eb::now();
            if (keepTrackOfVertexDeduplications) {
                COUT << "   IFStoCT Manifold handling, split " << attribute->type << " = " << numSplitAttrVertices << ", added " << attribute->type << " vtx = " << addedAttrVertices << ", paired " << attribute->type << " = " << pairedAfterManifoldSplit << std::endl;
            }
            else {
                COUT << "   IFStoCT Manifold handling, added " << attribute->type << " = " << addedAttrVertices << ", paired " << attribute->type << " = " << pairedAfterManifoldSplit << std::endl;
            }
        };

        // invoke attribute processing using relevant AttributeVertex classes
        switch (attrib->type) {
        case eb::Attribute::Type::UVCOORD:
            processAttribute(static_cast<AttributeVertexUVCoord*>(attrib));
            break;
        case eb::Attribute::Type::NORMAL:
            processAttribute(static_cast<AttributeVertexNormal*>(attrib));
            break;
        case eb::Attribute::Type::GENERIC:
            switch (attrib->nbComp)
            {
            case 1: processAttribute(static_cast<AttributeVertexGeneric<1>*>(attrib)); break;
            case 2: processAttribute(static_cast<AttributeVertexGeneric<2>*>(attrib)); break;
            case 3: processAttribute(static_cast<AttributeVertexGeneric<3>*>(attrib)); break;
            case 4: processAttribute(static_cast<AttributeVertexGeneric<4>*>(attrib)); break;
            }
            break;
            // others to add
        }
    }
    return true;
}

// License related to Google Draco inspired code
//Apache License
//Version 2.0, January 2004
//http://www.apache.org/licenses/
//
//TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION
//
//1. Definitions.
//
//"License" shall mean the terms and conditions for use, reproduction,
//and distribution as defined by Sections 1 through 9 of this document.
//
//"Licensor" shall mean the copyright owner or entity authorized by
//the copyright owner that is granting the License.
//
//"Legal Entity" shall mean the union of the acting entity and all
//other entities that control, are controlled by, or are under common
//control with that entity.For the purposes of this definition,
//"control" means(i) the power, direct or indirect, to cause the
//direction or management of such entity, whether by contract or
//otherwise, or (ii)ownership of fifty percent(50 %) or more of the
//outstanding shares, or (iii)beneficial ownership of such entity.
//
//"You" (or "Your") shall mean an individual or Legal Entity
//exercising permissions granted by this License.
//
//"Source" form shall mean the preferred form for making modifications,
//including but not limited to software source code, documentation
//source, and configuration files.
//
//"Object" form shall mean any form resulting from mechanical
//transformation or translation of a Source form, including but
//not limited to compiled object code, generated documentation,
//and conversions to other media types.
//
//"Work" shall mean the work of authorship, whether in Source or
//Object form, made available under the License, as indicated by a
//copyright notice that is included in or attached to the work
//(an example is provided in the Appendix below).
//
//"Derivative Works" shall mean any work, whether in Source or Object
//form, that is based on(or derived from) the Workand for which the
//editorial revisions, annotations, elaborations, or other modifications
//represent, as a whole, an original work of authorship.For the purposes
//of this License, Derivative Works shall not include works that remain
//separable from, or merely link(or bind by name) to the interfaces of,
//the Workand Derivative Works thereof.
//
//"Contribution" shall mean any work of authorship, including
//the original version of the Workand any modifications or additions
//to that Work or Derivative Works thereof, that is intentionally
//submitted to Licensor for inclusion in the Work by the copyright owner
//or by an individual or Legal Entity authorized to submit on behalf of
//the copyright owner.For the purposes of this definition, "submitted"
//means any form of electronic, verbal, or written communication sent
//to the Licensor or its representatives, including but not limited to
//communication on electronic mailing lists, source code control systems,
//and issue tracking systems that are managed by, or on behalf of, the
//Licensor for the purpose of discussingand improving the Work, but
//excluding communication that is conspicuously marked or otherwise
//designated in writing by the copyright owner as "Not a Contribution."
//
//"Contributor" shall mean Licensor and any individual or Legal Entity
//on behalf of whom a Contribution has been received by Licensor and
//subsequently incorporated within the Work.
//
//2. Grant of Copyright License.Subject to the terms and conditions of
//this License, each Contributor hereby grants to You a perpetual,
//worldwide, non - exclusive, no - charge, royalty - free, irrevocable
//copyright license to reproduce, prepare Derivative Works of,
//publicly display, publicly perform, sublicense, and distribute the
//Workand such Derivative Works in Source or Object form.
//
//3. Grant of Patent License.Subject to the terms and conditions of
//this License, each Contributor hereby grants to You a perpetual,
//worldwide, non - exclusive, no - charge, royalty - free, irrevocable
//(except as stated in this section) patent license to make, have made,
//use, offer to sell, sell, import, and otherwise transfer the Work,
//where such license applies only to those patent claims licensable
//by such Contributor that are necessarily infringed by their
//Contribution(s) alone or by combination of their Contribution(s)
//with the Work to which such Contribution(s) was submitted.If You
//institute patent litigation against any entity(including a
//    cross - claim or counterclaim in a lawsuit) alleging that the Work
//    or a Contribution incorporated within the Work constitutes direct
//    or contributory patent infringement, then any patent licenses
//    granted to You under this License for that Work shall terminate
//    as of the date such litigation is filed.
//
//    4. Redistribution.You may reproduce and distribute copies of the
//    Work or Derivative Works thereof in any medium, with or without
//    modifications, and in Source or Object form, provided that You
//    meet the following conditions :
//
//(a)You must give any other recipients of the Work or
//Derivative Works a copy of this License; and
//
//(b)You must cause any modified files to carry prominent notices
//stating that You changed the files; and
//
//(c)You must retain, in the Source form of any Derivative Works
//that You distribute, all copyright, patent, trademark, and
//attribution notices from the Source form of the Work,
//excluding those notices that do not pertain to any part of
//the Derivative Works; and
//
//(d)If the Work includes a "NOTICE" text file as part of its
//distribution, then any Derivative Works that You distribute must
//include a readable copy of the attribution notices contained
//within such NOTICE file, excluding those notices that do not
//pertain to any part of the Derivative Works, in at least one
//of the following places : within a NOTICE text file distributed
//as part of the Derivative Works; within the Source form or
//documentation, if provided along with the Derivative Works; or ,
//within a display generated by the Derivative Works, ifand
//wherever such third - party notices normally appear.The contents
//of the NOTICE file are for informational purposes onlyand
//do not modify the License.You may add Your own attribution
//notices within Derivative Works that You distribute, alongside
//or as an addendum to the NOTICE text from the Work, provided
//that such additional attribution notices cannot be construed
//as modifying the License.
//
//You may add Your own copyright statement to Your modificationsand
//may provide additional or different license terms and conditions
//for use, reproduction, or distribution of Your modifications, or
//for any such Derivative Works as a whole, provided Your use,
//reproduction, and distribution of the Work otherwise complies with
//the conditions stated in this License.
//
//5. Submission of Contributions.Unless You explicitly state otherwise,
//any Contribution intentionally submitted for inclusion in the Work
//by You to the Licensor shall be under the termsand conditions of
//this License, without any additional terms or conditions.
//Notwithstanding the above, nothing herein shall supersede or modify
//the terms of any separate license agreement you may have executed
//with Licensor regarding such Contributions.
//
//6. Trademarks.This License does not grant permission to use the trade
//names, trademarks, service marks, or product names of the Licensor,
//except as required for reasonableand customary use in describing the
//origin of the Workand reproducing the content of the NOTICE file.
//
//7. Disclaimer of Warranty.Unless required by applicable law or
//agreed to in writing, Licensor provides the Work(and each
//    Contributor provides its Contributions) on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//    implied, including, without limitation, any warranties or conditions
//    of TITLE, NON - INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
//    PARTICULAR PURPOSE.You are solely responsible for determining the
//    appropriateness of using or redistributing the Work and assume any
//    risks associated with Your exercise of permissions under this License.
//
//    8. Limitation of Liability.In no event and under no legal theory,
//    whether in tort(including negligence), contract, or otherwise,
//    unless required by applicable law(such as deliberateand grossly
//        negligent acts) or agreed to in writing, shall any Contributor be
//    liable to You for damages, including any direct, indirect, special,
//    incidental, or consequential damages of any character arising as a
//    result of this License or out of the use or inability to use the
//    Work(including but not limited to damages for loss of goodwill,
//        work stoppage, computer failure or malfunction, or any and all
//        other commercial damages or losses), even if such Contributor
//    has been advised of the possibility of such damages.
//
//    9. Accepting Warranty or Additional Liability.While redistributing
//    the Work or Derivative Works thereof, You may choose to offer,
//    and charge a fee for, acceptance of support, warranty, indemnity,
//    or other liability obligations and /or rights consistent with this
//    License.However, in accepting such obligations, You may act only
//    on Your own behalfand on Your sole responsibility, not on behalf
//    of any other Contributor, and only if You agree to indemnify,
//    defend, and hold each Contributor harmless for any liability
//    incurred by, or claims asserted against, such Contributor by reason
//    of your accepting any such warranty or additional liability.
//
//    END OF TERMS AND CONDITIONS
//
//    APPENDIX : How to apply the Apache License to your work.
//
//    To apply the Apache License to your work, attach the following
//    boilerplate notice, with the fields enclosed by brackets "[]"
//    replaced with your own identifying information. (Don't include
//        the brackets!)  The text should be enclosed in the appropriate
//    comment syntax for the file format.We also recommend that a
//    file or class nameand description of purpose be included on the
//    same "printed page" as the copyright notice for easier
//    identification within third - party archives.
//
//    Copyright[yyyy][name of copyright owner]
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//you may not use this file except in compliance with the License.
//You may obtain a copy of the License at
//
//http ://www.apache.org/licenses/LICENSE-2.0
//
//Unless required by applicable law or agreed to in writing, software
//distributed under the License is distributed on an "AS IS" BASIS,
//WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//See the License for the specific language governing permissions and
//limitations under the License.
