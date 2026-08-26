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

// internal headers
#include "ebIO.h"
#include "ebModel.h"
#include "ebGeometry.h"
#include "ebEncoder.h"
#include "ebAttributeImpl.h"

using namespace eb;

// quantize the attributes of the OV table
bool EBEncoder::quantize(bool intAttr) {

    for (auto attr : _ctMesh.getAttributes()) {

        // note per face attributes quantization is not considered in this version
        // this simple implementation could be updated to use integral types for quantized values

        // quantize position attributes
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::POSITION)
        {
            auto attrPos = static_cast<AttributeVertexPosition*>(attr);

            auto& positions = attrPos->values;
            if (!positions.empty() && qp >= 0) {

                if (qp > 0 || intAttr)
                {
                    Geometry::computeBBox(
                        positions, attrPos->minVal, attrPos->maxVal, positions.size());
                }

                if (qp > 0 && !intAttr)
                {
                    const int32_t maxPositionQuantizedValue =
                        (1u << static_cast<uint32_t>(qp)) - 1;
                    const glm::vec3 diag = attrPos->maxVal - attrPos->minVal;
                    const double    range = std::max(std::max(diag.x, diag.y), diag.z);
                    double          scale = (double)range / maxPositionQuantizedValue;

                    for (size_t i = 0; i < positions.size(); i++) {
                        glm::ivec3 pos =
                            glm::floor((glm::dvec3(positions[i] - attrPos->minVal) / scale) + 0.5);
                        positions[i] = pos;
                    }
                    attrPos->qp = qp;
                }
                else if (qp >= 0 && intAttr) // there only for intAttr auto 
                {
                    // we should separate intAttr case and check value type
                    auto minv = std::numeric_limits<int>::max();
                    auto maxv = std::numeric_limits<int>::min();
                    for (auto i = 0; i < attrPos->minVal.length(); ++i)
                    {
                        minv = std::min((int)attrPos->minVal[i], minv);
                        maxv = std::max((int)attrPos->maxVal[i], maxv);
                    }
                    if (minv < 0) // // not handled in this version
                    {
                        attrPos->qp = -1; // do not encode
                        std::cout << "ERROR : negative quantized position values present" << std::endl;
                        exit(0);
                    }
                    else if (qp == 0)
                    {
                        attrPos->qp = std::ceil(std::log2(maxv + 1));
                        COUT << "  qp = " << (int)attrPos->qp << " (auto)" << std::endl;
                    }
                    else
                    {
                        attrPos->qp = qp;
                    }
                }
                else if (qp == 0 && !intAttr)
                {
                    // could be used for connectivity only
                    std::cout << "ERROR : not handled qp=0 without intAttr" << std::endl;
                    exit(0);
                }
            }
            else
                attrPos->qp = -1;
        }

        // quantize UV coordinates
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::UVCOORD)
        {
            auto  attrUv = static_cast<AttributeVertexUVCoord*>(attr);
            auto& uvcoords = attrUv->values;
            if (!uvcoords.empty() && qt >= 0) {

                if (qp > 0 || intAttr)
                {
                    Geometry::computeBBox(
                        uvcoords, attrUv->minVal, attrUv->maxVal, uvcoords.size());
                }
                if (qt > 0 && !intAttr)
                {
                    const int32_t maxUVcoordQuantizedValue =
                        (1u << static_cast<uint32_t>(qt)) - 1;
                    const glm::vec2 diag = attrUv->maxVal - attrUv->minVal;
                    const double    range = std::max(diag.x, diag.y);
                    double          scale = (double)range / maxUVcoordQuantizedValue;

                    for (size_t i = 0; i < uvcoords.size(); i++) {
                        glm::ivec2 uv =
                            glm::floor((glm::dvec2(uvcoords[i] - attrUv->minVal) / scale) + 0.5);
                        uvcoords[i] = uv;
                    }
                    attrUv->qp = qt;
                }
                else if (qt >= 0 && intAttr) // there only for intAttr auto 
                {
                    auto minv = std::numeric_limits<int>::max();
                    auto maxv = std::numeric_limits<int>::min();
                    for (auto i = 0; i < attrUv->minVal.length(); ++i)
                    {
                        minv = std::min((int)attrUv->minVal[i], minv);
                        maxv = std::max((int)attrUv->maxVal[i], maxv);
                    }
                    if (minv < 0) // // not handled in this version
                    {
                        attrUv->qp = -1; // do not encode
                        std::cout << "ERROR : negative quantized texture coordinate values present" << std::endl;
                        exit(0);
                    }
                    else if (qt == 0)
                    {
                        attrUv->qp = std::ceil(std::log2(maxv + 1));
                        COUT << "  qt = " << (int)attrUv->qp << " (auto)" << std::endl;
                    }
                    else
                    {
                        attrUv->qp = qt;
                    }
                }
                else if (qt == 0 && !intAttr)
                {
                    // could be used for connectivity only
                    std::cout << "ERROR : not handled yet qt=0 without intAttr" << std::endl;
                    exit(0);
                }
            }
            else
                attrUv->qp = -1;
        }

        // quantize normals
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::NORMAL)
        {
            // quantize normals
            // shall not be performed this way
            // rather use octahedron mechanism
            // but the ingest might be 3 comp, TBD find a way to convert here if needed
            auto  attrNrm = static_cast<AttributeVertexNormal*>(attr);
            auto& normals = attrNrm->values;
            // code below is incorrect - checking only partially quantized value limits
            // min and max vals should be better controlled through a dedicated api ?
            if (!normals.empty() && qn >=0) {

                if (qn > 0 || intAttr)
                {
                    Geometry::computeBBox(normals, attrNrm->minVal, attrNrm->maxVal, normals.size());
                }
                if (qn > 0 && !intAttr)
                {
                    // normal prediction process assumes values are normalized
                    // issue warning when any min/max value is beyond [-1;+1]
                    // consider additionnal tests
                    bool inrange = true;
                    for (size_t i = 0; i < attrNrm->nbComp; i++) {
                        inrange &= attrNrm->minVal[i] >= -1;
                        inrange &= attrNrm->maxVal[i] <= +1;
                    }
                    if (!inrange)
                    {
                        std::cout << "ERROR : expecting normal values in the [-1;+1] range" << std::endl;
                        exit(0);
                    }
                    attrNrm->minVal = glm::vec3(-1.0);
                    attrNrm->maxVal = glm::vec3(+1.0);
                    const int32_t maxNormalQuantizedValue =
                        (1u << static_cast<uint32_t>(qn)) - 1;
                    const glm::vec3 diag = attrNrm->maxVal - attrNrm->minVal;
                    const double    range = std::max(std::max(diag.x, diag.y), diag.z);
                    double          scale = (double)range / maxNormalQuantizedValue;

                    for (size_t i = 0; i < normals.size(); i++) {
                        glm::ivec3 nrm =
                            glm::floor((glm::dvec3(normals[i] - attrNrm->minVal) / scale) + 0.5);
                        normals[i] = nrm;
                    }
                    attrNrm->qp = qn;
                }
                else if (qn >= 0 && intAttr) // there only for intAttr auto 
                {
                    auto minv = std::numeric_limits<int>::max();
                    auto maxv = std::numeric_limits<int>::min();
                    for (auto i = 0; i < attrNrm->minVal.length(); ++i)
                    {
                        minv = std::min((int)attrNrm->minVal[i], minv);
                        maxv = std::max((int)attrNrm->maxVal[i], maxv);
                    }
                    if (minv < 0) // // not handled in this version
                    {
                        attrNrm->qp = -1; // do not encode
                        std::cout << "ERROR : negative quantized normal values present" << std::endl;
                        exit(0);
                    }
                    else if (qn == 0)
                    {
                        attrNrm->qp = std::ceil(std::log2(maxv + 1));
                        COUT << "  qn = " << (int)attrNrm->qp << " (auto)" << std::endl;
                    }
                    else
                    {
                        attrNrm->qp = qn;
                    }
                    // we need to ensure this is properly initialized when input is pre quantized
                    attrNrm->minVal = glm::vec3(-1.0);
                    attrNrm->maxVal = glm::vec3(+1.0);
                }
                else if (qn == 0 && !intAttr)
                {
                    // could be used for connectivity only
                    std::cout << "ERROR : not handled yet qn=0 without intAttr" << std::endl;
                    exit(0);
                }
            }
            else
                attrNrm->qp = -1;
        }
        // quantize generic
        if (attr->domain == Attribute::Domain::PER_VERTEX
            && attr->type == Attribute::Type::GENERIC)
        {
            auto processGen = [&](auto* attrGen) {
                using AttGenT = typename std::remove_pointer<decltype(attrGen)>::type;
                auto& generic = attrGen->values;
                if (!generic.empty() && qg >= 0) {
                    if (qg > 0 || intAttr)
                    {
                        typename AttGenT::vecN minGen(FLT_MAX);
                        typename AttGenT::vecN maxGen(-FLT_MAX);
                        for (size_t i = 0; i < generic.size(); i++) {
                            minGen = glm::min(generic[i], minGen);
                            maxGen = glm::max(generic[i], maxGen);
                        }
                        attrGen->minVal = minGen;
                        attrGen->maxVal = maxGen;
                    }

                    if (qg > 0 && !intAttr)
                    {
                        const int32_t maxGenericQuantizedValue =
                            (1u << static_cast<uint32_t>(qg)) - 1;
                        const typename AttGenT::vecN diag = attrGen->maxVal - attrGen->minVal;

                        float range = diag[0];
                        for (auto i = 1; i < diag.length(); ++i)
                            range = std::max(range, diag[i]);

                        double scale = (double)range / maxGenericQuantizedValue;

                        for (size_t i = 0; i < generic.size(); i++) {
                            typename AttGenT::ivecN gen =
                                glm::floor((typename AttGenT::dvecN(generic[i] - attrGen->minVal) / scale) + 0.5);
                            generic[i] = gen;
                        }
                        attrGen->qp = qg;
                    }
                    else if (qg >= 0 && intAttr) // there only for intAttr auto 
                    {
                        auto minv = std::numeric_limits<int>::max();
                        auto maxv = std::numeric_limits<int>::min();
                        for (auto i = 0; i < attrGen->minVal.length(); ++i)
                        {
                            minv = std::min((int)attrGen->minVal[i], minv);
                            maxv = std::max((int)attrGen->maxVal[i], maxv);
                        }
                        if (minv < 0) // // not handled in this version
                        {
                            attrGen->qp = -1; // do not encode
                            std::cout << "ERROR : negative quantized generic values present" << std::endl;
                            exit(0);
                        }
                        else if (qg== 0)
                        {
                            attrGen->qp = std::ceil(std::log2(maxv + 1));
                            COUT << "  qg = " << (int)attrGen->qp << " (auto)" << std::endl;
                        }
                        else
                        {
                            attrGen->qp = qg;
                        }
                    }
                    else if (qg == 0 && !intAttr)
                    {
                        // could be used for connectivity only
                        std::cout << "ERROR : not handled yet qg=0 without intAttr" << std::endl;
                        exit(0);
                    }
                }
                else
                    attrGen->qp = -1;
            };

            switch (attr->nbComp)
            {
            case 1: processGen(static_cast<AttributeVertexGeneric<1>*>(attr)); break;
            case 2: processGen(static_cast<AttributeVertexGeneric<2>*>(attr)); break;
            case 3: processGen(static_cast<AttributeVertexGeneric<3>*>(attr)); break;
            case 4: processGen(static_cast<AttributeVertexGeneric<4>*>(attr)); break;
            };

        }
        // could be extended to quantize colors when attr->type == Attribute::Type::COLOR when related prediction processes are defined
    }

    return true;
}
