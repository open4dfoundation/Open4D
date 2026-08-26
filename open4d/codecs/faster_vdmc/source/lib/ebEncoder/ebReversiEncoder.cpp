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

#include <iostream>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <time.h>
#include <math.h>
#include <tuple>
#include <bitset>
#include <climits>

// internal headers
#include "ebIO.h"
#include "ebModel.h"
#include "ebGeometry.h"
#include "ebModelConverter.h"
#include "ebVertexTraversal.h"
#include "ebReversiEncoder.h"
#include "ebBitstream.h"
#include "ebEntropy.h"
#include "ebEntropyContext.h"

#include "ebBitstream.h"
#include "ebWriter.hpp"
#include "syntaxElements/meshCoding.hpp"

// requires decoder !! should be included in project for link no ?
#include "ebReversiDecoder.h" // required for reverse unification

using namespace eb;

void EBReversiEncoder::encode(const Model& input) {

    COUT << "  Encoding using reverse Edgebreaker method" << std::endl;

    numOfInputTriangles = input.getTriangleCount(); // store number of triangles of the input mesh
    numOfInputVertices = input.getPositionCount();  // store number of vertices of the input mesh

    if (cfg.reverseUnification) {
        cfg.unify = true; // should this be done at the wrapper level- review requirements on joins flags values
        cfg.unifyPositionsOnly = true; // reverse unification applicable only when input mesh has a common index
        // ... but decoding does not reorder positions to reconstruct common index then
    }
    // A - first convert input mesh into an extensible corner table mesh
    auto t = now();

    // conversion could be updated to consider removal of non encoded attributes (eg those with q <0) 
    ModelConverter::convertModelToCTMesh(input, _ctMesh, cfg.deduplicate, cfg.useMainIndexIfEqual, cfg.unify, cfg.unifyPositionsOnly, cfg.verbose);
    COUT << "  Convert time (ms) = " << elapsed(t) << std::endl;

    // create an encoder for each attribute
    createAttributeEncoders();

    // initializing the set of decoders
    for (auto enc : encoders) {
        enc->initialize(cfg);
    }

    // Quantize the CT mesh if needed .. or define bit depth if auto , potentially check ranges
    if (true /* || !cfg.intAttr */) { // always go in to define auto levels - using specified bitdepth with unchecked values may results in decoded output beyond the range
        t = now();
        quantize(cfg.intAttr);
        COUT << "  Quantize time (ms) = " << elapsed(t) << " ms" << std::endl;
    }

    t = now();

    // Work on all the primary decoders one by one,
    // those will cascade calls if needed
    for (auto enc : primaryEncoders)
    {
        // encode the connectivity using edge breaker
        enc->encodeConnectivity(cfg);

        enc->encodeAttributes(cfg);

    }

    //reconstruct
    if (cfg.reverseUnification) {

        // serialize to bitstream to prepare decoding
        // this could be optimized to avoid AC coding/decoding
        // here complete process to ensure matching
        t = now();
        eb::Bitstream bs;
        const auto verbosity = cfg.verbose;
        cfg.verbose = false;
        serialize(bs);
        cfg.verbose = verbosity;
        bs.beginning();
        COUT << "  Serialization time (ms) = " << elapsed(t) << std::endl;

        // parsing and deserialization
        t = now();
        eb::MeshCoding meshCoding;  // Top level syntax element
        eb::EbReader   ebReader;
        ebReader.read(bs, meshCoding);
        eb::EBReversiDecoder decoder;

        // replicate output reindex option
        decoder.cfg.reindexOutput = cfg.reindexOutput;
        decoder.cfg.verbose = false;
         // deserialize the bitstream
        decoder.unserialize(meshCoding);
        COUT << "  Reverse unification De-serialization time (ms) = " << elapsed(t) << std::endl;
        
        // decode to model
        eb::Model recModel;
        decoder.cfg.intAttr = true; // bypass dequantization
        decoder.cfg.wrapAround = cfg.wrapAround;
        decoder.decode(recModel);        
        COUT << "  Reverse unification Decoding time (ms) = " << elapsed(t) << std::endl;

        for (auto enc : primaryEncoders)
        {
            // TO CLARIFY WHAT TO DO with multiple Encoders
            for (int i = 0; i < enc->processedCorners.size(); i++) {
                faceOrder.push_back(enc->processedCorners[i] / 3);
            }
        }
        
        // compare difference
        // NOTE that currently the comparison assumes no unreferences vertices are present in the input mesh
        // lossless normals using ReverseUnificationNorm in decode and compareMeshDifferenceNrm
        // ReverseUnificationNorm not in decoding process
        t = now();
        if (cfg.intAttr)
            compareMeshDifference(input, recModel);
        else
        {
          // mesh difference analysis requires a quantized input model
          eb::Model qModel = Model(input);
            for (auto idx = 0; idx < input.attributes.size();++idx) {
                auto* attr = input.attributes[idx].get();
                auto* qattr = qModel.attributes[idx].get();
                
                // quantize values ( note that per face attributes quantization is not considered in this version )
                // this simple implementation could be updated to use integral types for quantized values
                if ((attr->getDomain() == eb::AttrDomain::PER_VERTEX)  &&  (attr->getDataType() == DataType::FLOAT32)) 
                {
                    auto& values = *attr->getData<float>();
                    auto& qvalues = *qattr->getData<float>();
                    auto dim = attr->getDim();
                    std::vector<float> minV(dim, std::numeric_limits<float>::infinity()), maxV(dim ,-std::numeric_limits<float>::infinity());
                    for (size_t i = 0; i < values.size(); i+=dim) {
                        for (size_t j = 0; j < dim; j++) {
                            minV[j] = std::min(values[i + j], minV[j]);
                            maxV[j] = std::max(values[i + j], maxV[j]);
                        }
                    }
 
                    uint32_t q; // need for an api to define per attrib quantization when multiple of the same type
                    switch (attr->getType())
                    {
                    case eb::AttrType::POSITION: q = qp; break;
                    case eb::AttrType::TEXCOORD : q = qt; break;
                    case eb::AttrType::NORMAL: q = qn; break;
                    case eb::AttrType::GENERIC: q = qg; break;
                    case eb::AttrType::COLOR: q = qc; break;
                    }


                    const int32_t maxQuantizedValue = (1u << q) - 1;
                    std::vector<float> diag(dim);
                    float range = maxV[0]-minV[0];
                    for (size_t j = 1; j < dim; j++) {
                        range = std::max(range, maxV[j] - minV[j]);
                    }

                    double scale = (double)range / maxQuantizedValue;

                    for (size_t i = 0; i < values.size(); i += dim) {
                        for (size_t j = 0; j < dim; j++) {
                            qvalues[i+j] = std::floor((double(values[i+j] - minV[j]) / scale) + 0.5);
                        }
                    }

                }
            }
            compareMeshDifference(qModel, recModel);
        }
        COUT << "  Compare difference time (ms) = " << elapsed(t) << std::endl;
    }

    COUT << "  Encoding time (ms) = " << elapsed(t) << std::endl;
}

void PositionVertexAttributeEncoder::initialize(EBConfig& cfg)
{
    ioVertices.reserve(attr->values.size());
    nT = attr->getTriangleCount();     // number of triangles
    int nv = attr->getPositionCount(); // number of vertices
    // Allocate memory for tables
    MV.assign(nv, 0);
    MT.assign(nT, 0);
    processedCorners.reserve(attr->ct.V.size());
}

void PositionVertexAttributeEncoder::encodeConnectivity(EBConfig& cfg) {

    // verify that we are primary
    // shall be allways the case for the time beeing
    // but we could imagine to be a secondary pos attr
    // in this case the else statement defaults to 
    // aux connectivity decoding
    if (attr->isPrimary()) {

        encodePrimaryConnectivity(cfg);

        // invokes auxiliary topologies encoding
        for (auto auxEnc : auxiliaryEncoders) {
            auxEnc->encodeConnectivity(cfg);
        }
    }
    else {
        // this statement never occurs
        // since position are allways 
        // primary for the time beeing
        encodeAuxiliaryConnectivity(cfg, attr);
    }
}

void PositionVertexAttributeEncoder::encodePrimaryConnectivity(EBConfig& cfg)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    _T = 0;                               // last triangle
    nB = 0;                               // number of comps with boundary

    bool  hasBoundary = false;            // this is to be lazy if no boundary
    for (auto c = 0; c < 3 * nT; ++c) {   // tests all corners for a boundary
        if (O[c] < 0) {                   // looks for edges on the boundary
            hasBoundary = true;
            MV[V[ov.p(c)]] = 1;           // mark vertices
            MV[V[ov.n(c)]] = 1;
            O[c] = -1;                    // reset the boundary ID (why -2 ? -1 in the paper)
        }
    }

    if (hasBoundary)                              // if it has a boundary, process first all the components with boundary
    {
        for (auto c = 0; c < 3 * nT; ++c) {      // compress components with boundary
            if (O[c] < 0 && !MT[ov.t(c)]) {      // starts from an unvisited border triangle
                writeBoundary(ov.n(c)); // encodes the geometry of this boundary
                _T--;
                ccStartCorners.push_back(c);
                compressComponent(c);   // starts the compression of this component
                nB++; _T++;
            }
        }
    }

    // test all triangles
    if (_T != ov.ntri())  // is this test for lazy or error check ?
    {
        for (auto c = 0; c < 3 * nT; ++c) {     // compress component without boundary
            if (!MT[ov.t(c)]) {                 // starts with an unvisited triangle
                MT[ov.t(c)] = 1;                // marks triangle as visited
                MV[V[c]] = 1;                   // marks vertices as visited
                MV[V[ov.n(c)]] = 1;
                MV[V[ov.p(c)]] = 1;

                ccStartCorners.push_back(c);
                processedCorners.push_back(c); // record corner processing order
                writeVertex(c);                // encodes the geometry of this triangle (c, c.n, c.p)
                writeVertex(ov.n(c));
                writeVertex(ov.p(c));
                compressComponent(ov.r(c));    // starts the compression of this component
                _T++;
            }
        }
    }
}

void PositionVertexAttributeEncoder::compressComponent(int _c)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    std::stack<int> stack; // stack of corners
    int c = _c;

    while (true) {                 // visits the triangle - spanning tree
        // notations for correspondence with paper
        const auto c_r = ov.r(c);
        const auto c_l = ov.l(c);
        const auto c_t = ov.t(c);
        const auto c_r_t = ov.t(c_r);
        const auto c_l_t = ov.t(c_l);
        const auto c_v = ov.v(c);
        processedCorners.push_back(c);
        MT[c_t] = 1; _T++;                      // marks current triangle as visited
        checkHandle(c);                         // checks for handles (paper)

        if (MV[c_v] != 1) {                     // tests whether the tip vertex was visited
            ioClers.push_back('C');             // case C: unvisited tip vertex
            writeVertex(c);                     // encodes the vertex of the corner and marks it
            MV[c_v] = 1;
            c = c_r;                            // continues with the right neighbor
        }
        else if (c_r < 0 || MT[c_r_t] != 0) {   // tests whether right triangle was visited
            if (c_l < 0 || MT[c_l_t] != 0) {    // tests whether left triangle was visited
                ioClers.push_back('E');         // case E: both visited
                do {
                    if (stack.empty())          // if the stack is empty
                        return;
                    else {
                        c = stack.top();        // otherwise, pops stack pushed by S
                        stack.pop();
                    }
                } while (MT[ov.t(c)] != 0);     // pops until finding a non�handle corner
            }
            else {                              // case R: right visited, left not; moves left
                ioClers.push_back('R');
                c = c_l;
            }
        }
        else if (c_l < 0 || MT[c_l_t] != 0) {   // tests whether left triangle was visited
            ioClers.push_back('L');             // case L: left visited, right not; 
            c = c_r;                            // moves right
        }
        else {
            ioClers.push_back('S');             // case S: both unvisited
            if (ov.isOnBoundary(c) == -1) {     // if the corner is on an unvisited boundary 
                writeBoundary(c);               // encodes the internal boundary
                MT[c_t] = -3 * _T - 2;          // stores corner number (potential boundary)
            }
            else {                              // else stores corner number (potential handle)
                MT[c_t] = 3 * _T + 2;
            }
            stack.push(c_l);                    // pushes the left corner on the stack
            c = c_r;                            // moves right
        }
    }
}

//
void PositionVertexAttributeEncoder::checkHandle(const int h
) {

    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    const auto h_r = ov.r(h);
    const auto h_l = ov.l(h);
    const auto h_r_t = ov.t(h_r);
    const auto h_l_t = ov.t(h_l);

    const int br = (h_r >= 0) ? MT[h_r_t] : -1;
    if (br != 0 && br != 1 && br != -1) {    // checks for handle from the rigth
        ioHandles.push_back(br);              // encodes pair of corners to be glued as a handle
        ioHandles.push_back(_T * 3 + 1);
    }

    const int bl = (h_l >= 0) ? MT[h_l_t] : -1;
    if (bl != 0 && bl != 1 && bl != -1) {     // checks for handle from the left
        ioHandles.push_back(bl);               // encodes pair of corners to be glued as a handle
        ioHandles.push_back(_T * 3 + 2);
    }
}

// Goes through the boundary of b and writes each vertex once in order
// b is the corner which vertex is on the boundary
void PositionVertexAttributeEncoder::writeBoundary(int _b)
{

    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    nBounds = nBounds + 1;               // increase the boundary Id

    auto b = ov.n(_b);                   // goes to the next triangle, 
    while (ov.r(b) >= 0) {
        b = ov.r(b);                     // goes to right towards the boundary
    }
    int nb = 0;
    do {                                 // starts the boundary encoding 
        b = ov.n(b);                     // starts from the next triangle 
        O[b] = -(nBounds + 2);           // and marks the boundary
        while (ov.r(b) >= 0) {
            b = ov.r(b);                 // goes to the rightmost triangle
        }
        const auto& b_p = ov.p(b);       // intermediate just for debug
        writeVertex(b_p);                // encodes the vertex of the corner
        nb++;
    } while (ov.r(b) != -(nBounds + 2)); // stops when all the boundary is marked 

}

template<class ValueType>
void VertexAttributeEncoder::encodeAuxiliaryConnectivity(
    EBConfig& cfg, AttributeVertex<ValueType>* attr)
{
    assert(attr->hasOwnIndices || attr->refAuxIdx > 0); // attrib->refAuxIdx > 0 is not allowed in the current specification.

    auto& ov = mainEnc->attr->ct;

    // are we using deafult EB traversal ?
    const auto altTraversal = (cfg.traversal != EBConfig::Traversal::EB);

    // for alternative traversals
    // reconstruct the complete corner table for the attribute
    // we will add UV seams cuts on top of the main corner table
    if (altTraversal) {
        attr->ct.O = mainEnc->attr->ct.O;
        ccStartCorners = mainEnc->ccStartCorners;
    }

    // define seams
    // I.9.5 - dual to auxilliary connectivity decoding first loop : seams regeneration
    for (auto corner : mainEnc->processedCorners) {
        const int corners[3] = { corner, ov.n(corner), ov.p(corner) };
        for (int i = 0; i < 3; ++i) {
            const int cur_corner = corners[i];
            const int opp_corner = ov.O[cur_corner];
            // no explicit seams on boundary edges opp < 0,
            // also skip if already set to -3 (i.e. < -2)
            if (opp_corner < 0 || attr->auxO[opp_corner] < -2) { continue; }
            const bool isSeam = (attr->auxO[cur_corner] == -2);
            ioSeams.push_back(isSeam);
            const int updval = isSeam ? -3 : -4;
            attr->auxO[cur_corner] = updval;
            attr->auxO[opp_corner] = updval;
            // special work for alternative traversals
            if (altTraversal && isSeam) {
                attr->ct.O[cur_corner] = -3;
                attr->ct.O[opp_corner] = -3;
                // we store seam corners as a potential start corners
                ccStartCorners.push_back(cur_corner);
                ccStartCorners.push_back(opp_corner);
            }
        }
    }
}

void PositionVertexAttributeEncoder::encodeAttributes(EBConfig& cfg)
{
    // verify that we are primary
    // shall be allways the case for the time being
    // but we could imagine to be a secondary pos attr
    // in this case the else statement defaults to 
    // aux connectivity decoding
    if (attr->isPrimary()) {

        if (cfg.traversal == EBConfig::Traversal::DEGREE) {

            // computes a corner order using prediction degree traversal
            predVertexTraverser.traverse(&(attr->ct),
                attr->getPositionCount(),
                attr->ct.getTriangleCount(),
                ccStartCorners);

            // perform the predicitons using the generated corner order
            encodePositionsWithPredition(cfg, predVertexTraverser.visitedCorners, false);

        }
        else { // otherwise uses the traversal order from connectivity encoding
            // !!!revert so that both for loop 0..N in decode
            encodePositionsWithPredition(cfg, writtenCorners, false);
        }

        // invokes auxiliary attributes decoding
        for (auto auxEnc : auxiliaryEncoders) {
            auxEnc->encodeAttributes(cfg);
        }
    }
    // in a role of an auxiliary decoder
    else {
        // not available yet
        // we shall never reach this statement
    }
}

void PositionVertexAttributeEncoder::encodePositionsWithPredition(
    EBConfig& cfg,
    std::vector<int>& traversalOrder,
    bool reverse)
{
    const auto& ov = attr->ct;

    // prepare the skip table if deduplication is active
    if (cfg.deduplicate) {
        predictCorner.assign(ov.V.size(), true);
    }

    // reset the vertex marking table
    for (auto& v : MV) v = -1;
    
    // go through traversal in forward or reverse order ?
    const int start = reverse ? traversalOrder.size() - 1 : 0;
    int inc = reverse ? -1 : 1;

    // goes through the corners, for the positions
    for (int i = start; i >= 0 && i < traversalOrder.size(); i += inc) {
        const auto& c = traversalOrder[i];

        bool predictPosition = true;
        if (cfg.deduplicate)
        {
            // check for duplicated positions
            const auto dupIt = attr->duplicatesMap.find(ov.V[c]);
            if (dupIt != attr->duplicatesMap.end())
            {
                isVertexDup.push_back(true);
                ioDuplicateSplitVertexIdx.push_back(dupIt->second);
            }
            else
                isVertexDup.push_back(false);

            // early return if duplicate already coded
            if (dupIt != attr->duplicatesMap.end())
            {
                if (processedDupIdx.find(dupIt->second)
                    != processedDupIdx.end())
                {
                    // no need to encode as already processed
                    predictPosition = false;
                    // used by attributes using main index table
                    predictCorner[c] = false;
                }
                else {
                    processedDupIdx.insert(dupIt->second);
                }
            }
        }
        if (predictPosition) {
            posEncodeWithPrediction(c);
        }
    }
}

template<class AuxEncoder>
void VertexAttributeEncoder::encodeAuxiliaryAttributes(EBConfig& cfg,
    PositionVertexAttributeEncoder* mainEnc,
    AuxEncoder* auxEnc)
{
    auto& posAttr = mainEnc->attr;
    const auto& ov = posAttr->ct;
    auto& attr = auxEnc->attr;

    // reset the vertex marking table
    for (auto& v : mainEnc->MV) v = -1;

    if (!attr->hasOwnIndices && attr->refAuxIdx == 0 ) {
        if (cfg.traversal == EBConfig::Traversal::DEGREE) {
            // I.9.7 MESH_TRAVERSAL_DEGREE when hasOwnIndices is FALSE
            // reuse the main traverser already initialized
            auto& traverser = mainEnc->predVertexTraverser;
            for (auto c : traverser.visitedCorners) {
                if (mainEnc->predictCorner.size() != 0 ? mainEnc->predictCorner[c] : true) {
                    auxEnc->encodeWithPrediction(c, attr, posAttr->ct.V);
                }
            }
        }
        else {
            // otherwise uses the traversal order from connectivity encoding
             // I.9.7 MESH_TRAVERSAL_EB when hasOwnIndices is FALSE
            for (int i = 0; i<mainEnc->writtenCorners.size();++i)
            {
                const auto& c = mainEnc->writtenCorners[i];
                if (mainEnc->predictCorner.size() != 0 ? mainEnc->predictCorner[c] : true) {
                    auxEnc->encodeWithPrediction(c, attr, posAttr->ct.V);
                }
            }
        }
    }
    else {

        // when (!attr->hasOwnIndices && attr->refAuxIdx > 0) we need to access ct or result of previous traversal
        // for simplicity we use a ct copy in model converter, and ct.O when needed is computed several times in encodeAuxiliaryConnectivity
        // Note that attrib->refAuxIdx > 0 is not allowed in the current specification.
        const auto& V = attr->ct.V; 

        // do the traversal and predict
        if (cfg.traversal == EBConfig::Traversal::DEGREE) {
            PredictionVertexTraverser traverser;
            // use the aux attr ext start corners
            traverser.traverse(&(attr->ct), 
                attr->values.size(), 
                posAttr->getTriangleCount(),
                ccStartCorners);
            // then we compute the predictions
            // I.9.7 MESH_TRAVERSAL_DEGREE when hasOwnIndices is TRUE
            for (auto c : traverser.visitedCorners) {

                bool predictAttribute = true;
                if (cfg.deduplicate)
                {
                    // check for duplicated attribute
                    const auto dupIt = attr->duplicatesMap.find(V[c]);
                    if (dupIt != attr->duplicatesMap.end())
                    {
                        isVertexDup.push_back(true);
                        ioDuplicateSplitVertexIdx.push_back(dupIt->second);
                    }
                    else
                        isVertexDup.push_back(false);

                    // early return if duplicate already coded
                    if (dupIt != attr->duplicatesMap.end())
                    {
                        if (processedDupIdx.find(dupIt->second)
                            != processedDupIdx.end())
                        {
                            // no need to encode as already processed
                            predictAttribute = false;
                            // used by attributes using main index table - are there other dependent
                            //predictCorner[c] = false;
                        }
                        else {
                            processedDupIdx.insert(dupIt->second);
                        }
                    }
                }

                if (predictAttribute) {
                    auxEnc->encodeWithPrediction(c, attr, attr->ct.V);
                }
            }
        }
        else {
            // I.9.7 MESH_TRAVERSAL_EB when hasOwnIndices is TRUE
            for (auto corner : mainEnc->processedCorners) {
                const int corners[3] = {
                  corner, ov.n(corner), ov.p(corner) };
                for (int i = 0; i < 3; ++i) {
                    const int c = corners[i];
                    if (mainEnc->MV[attr->ct.V[c]] > 0) continue;
                    bool predictAttribute = true;
                    if (cfg.deduplicate)
                    {
                        // check for duplicated attribute
                        const auto dupIt = attr->duplicatesMap.find(V[c]);
                        if (dupIt != attr->duplicatesMap.end())
                        {
                            isVertexDup.push_back(true);
                            ioDuplicateSplitVertexIdx.push_back(dupIt->second);
                        }
                        else
                            isVertexDup.push_back(false);

                        // early return if duplicate already coded
                        if (dupIt != attr->duplicatesMap.end())
                        {
                            if (processedDupIdx.find(dupIt->second)
                                != processedDupIdx.end())
                            {
                                // no need to encode as already processed
                                predictAttribute = false;
                                // used by attributes using main index table - are there other dependent
                                //predictCorner[c] = false;
                            }
                            else {
                                processedDupIdx.insert(dupIt->second);
                            }
                        }
                    }

                    if (predictAttribute) {
                        auxEnc->encodeWithPrediction(c, attr, attr->ct.V);
                    }
                }
            }
        }
    }
}

void PositionVertexAttributeEncoder::posEncodeWithPrediction(int c) {

    const auto MAX_PARALLELOGRAMS = 4;
    const auto MAX_TRAPEZOIDS = 4;
    // we only work on the first object for the time being
    auto& ov = attr->ct;
    const auto& V = ov.V;
    const auto& O = ov.O;
    const auto& G = attr->values;
    const auto& v = ov.v(c);

    // is vertex already predicted ?
    if (MV[v] > 0)
        return;

    // search for some parallelogram estimations and trapezoid estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be deifned due to boundaries
    // we use OV accessors and test to filter negative values

    glm::ivec3 predPos[MAX_PARALLELOGRAMS];             // the predicted position
    int  count = 0;                         // number of valid parallelograms found
    int  altC = c;

    glm::ivec3 predPosTra[MAX_TRAPEZOIDS];             // the trapezoid predicted position
    int  countTra = 0;                         // number of valid trapezoids found

    // loop through corners attached to the current vertex
    // swing right around the fan
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c)
    {
        altC = nextC;
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    const int startC = altC;
    do
    {
        const auto& oppoV = ov.v(O[altC]);
        const auto& prevV = ov.v(ov.p(altC));
        const auto& nextV = ov.v(ov.n(altC));

        if (count < MAX_PARALLELOGRAMS) {
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                // parallelogram prediction estG = prevG + nextG - oppoGd
                glm::vec3 estG = G[prevV] + G[nextV] - G[oppoV];
                predPos[count] = estG;                      // accumulate parallelogram predictions
                ++count;
            }
        }
        if (countTra < MAX_TRAPEZOIDS) {
            if (oppoV > -1 && MV[oppoV] > 0) {
                if (prevV > -1 && MV[prevV] > 0) {
                    const auto& traNextV = ov.v(O[ov.p(altC)]);
                    if (traNextV > -1 && MV[traNextV] > 0) {
                        glm::vec3 estGTra = glm::vec3(4) * G[prevV] + glm::vec3(2) * (G[traNextV] - G[oppoV]);
                        predPosTra[countTra] = estGTra;  // accumulate trapezoid predictions
                        ++countTra;
                    }
                    else if (traNextV > -1 && nextV > -1 && MV[nextV] > 0)
                    {
                        const auto extOppoC = O[ov.p(O[ov.p(altC)])];
                        if (ov.n(extOppoC) > -1 && ov.p(extOppoC) > -1) {
                            const auto& extNextV = ov.v(O[ov.n(extOppoC)]);
                            const auto& extPrevV = ov.v(O[ov.p(extOppoC)]);
                            if ((extNextV > -1 && extPrevV > -1) && (MV[extNextV] > 0 && MV[extPrevV] > 0))
                            {
                                glm::vec3 estNextG = glm::vec3(2) * G[nextV] + G[extNextV] - G[extPrevV];
                                glm::vec3 estGTra = glm::vec3(4) * G[prevV] + estNextG - glm::vec3(2) * G[oppoV];
                                predPosTra[countTra] = estGTra;  // accumulate trapezoid predictions
                                ++countTra;
                            }
                        }
                    }
                }
                else if (prevV > -1 && nextV > -1 && MV[nextV] > 0)
                {
                    const auto& traNextV = ov.v(O[ov.p(altC)]);
                    if (traNextV > -1 && MV[traNextV] > 0 && ov.n(O[altC]) > -1 && ov.p(O[altC]) > -1) {
                        const auto& extNextV = ov.v(O[ov.n(O[altC])]);
                        const auto& extPrevV = ov.v(O[ov.p(O[altC])]);
                        if ((extNextV > -1 && extPrevV > -1) && (MV[extNextV] > 0 && MV[extPrevV] > 0))
                        {
                            glm::vec3 estPrevG = glm::vec3(2) * G[nextV] + G[extPrevV] - G[extNextV];
                            glm::vec3 estGTra = glm::vec3(2) * estPrevG + glm::vec3(2) * (G[traNextV] - G[oppoV]);
                            predPosTra[countTra] = estGTra;  // accumulate trapezoid predictions
                            ++countTra;
                        }
                    }
                }
            }
        }
        altC = ov.p(O[ov.p(altC)]);              // swing around the triangle fan
    } while (altC >= 0 && altC != startC);       // incomplete fan or full rotation

    // we mark the vertex
    MV[v] = 1;

    // 1. use parallelogram prediction and trapezoid prediction when possible
    if (count > 0) {
        if (count > countTra * 2 - 1) {
            const int scale[MAX_PARALLELOGRAMS][MAX_PARALLELOGRAMS] = {
                {GEO_SCALE, 0, 0, 0}, // 1.0, 0, 0, 0
                {GEO_SCALE / 2, GEO_SCALE / 2, 0, 0}, // 0.5, 0.5, 0, 0
                {(GEO_SCALE + 1) / 3, GEO_SCALE - 2 * (GEO_SCALE + 1) / 3, (GEO_SCALE + 1) / 3, 0}, // 1/3, 1/3, 1/3, 0 - approximation
                {GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4}, // 0.25, 0.25, 0.25, 0.25
            };
            glm::ivec3 predError;
            glm::ivec3 Gv = G[v];
            glm::ivec3 pred(0, 0, 0);
            for (int i = 0; i < count; ++i) {
                pred += scale[count - 1][i] * predPos[i];
            }
            pred = (pred + glm::ivec3(GEO_SCALE / 2)) >> GEO_SHIFT;
            predError = Gv - pred;
            glm::vec3 fPredError = predError;
            ioVertices.push_back(fPredError);
            ioAttrFine.push_back(true);
            return;
        }
    }

    if (countTra > 0) {
        const int scale[MAX_TRAPEZOIDS][MAX_TRAPEZOIDS] = {
            {GEO_SCALE, 0, 0, 0}, // 1.0, 0, 0, 0
            {GEO_SCALE / 2, GEO_SCALE / 2, 0, 0}, // 0.5, 0.5, 0, 0
            {(GEO_SCALE + 1) / 3, GEO_SCALE - 2 * (GEO_SCALE + 1) / 3, (GEO_SCALE + 1) / 3, 0}, // 1/3, 1/3, 1/3, 0 - approximation
            {GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4}, // 0.25, 0.25, 0.25, 0.25
        };
        glm::ivec3 predError;
        glm::ivec3 Gv = G[v];
        glm::ivec3 pred(0, 0, 0);
        for (int i = 0; i < countTra; ++i) {
            pred += scale[countTra - 1][i] * predPosTra[i];
        }
        const int frac_bits = GEO_SHIFT + 2;
        const int rounding_offset = 1 << (frac_bits - 1);
        pred = (pred + glm::ivec3(rounding_offset)) >> frac_bits;
        predError = Gv - pred;
        glm::vec3 fPredError = predError;
        ioVertices.push_back(fPredError);
        ioAttrFine.push_back(true);
        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_v = ov.v(ov.p(c));
    const auto& c_n_v = ov.v(ov.n(c));

    if (c_p_v > -1 && MV[c_p_v] > -1) {
        ioVertices.push_back(G[v] - G[c_p_v]);
        ioAttrFine.push_back(false);
        return;
    }

    if (c_n_v > -1 && MV[c_n_v] > -1) {
        ioVertices.push_back(G[v] - G[c_n_v]);
        ioAttrFine.push_back(false);
        return;
    }

    // 3. if we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_v = ov.v(b);
        auto marked = MV[b_v];
        if (marked > -1) {
            ioVertices.push_back(G[v] - G[b_v]);
            ioAttrFine.push_back(false);
            return;
        }
    }

    // 4. no other choice
    // global value (it is a start, pushed in separate table)
    iosVertices.push_back(G[v]);

    return;
}

void UVCoordVertexAttributeEncoder::encodeWithPrediction(
    int c,
    AttributeVertexUVCoord* attrUv,
    const std::vector<int>& tcIndices)
{
    const auto MAX_STRETCH_PREDS = 8;

    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& OTC = attrUv->auxO;
    const auto& UV = attrUv->values;
    const auto& TC = tcIndices;    // texture coordinate index table
    const auto& tc = TC[c];        // texture coordinate index for corner c
    auto& MV = mainEnc->MV;

    // is vertex already predicted ?
    if (MV[tc] > 0)
        return;
    // we mark the vertex
    MV[tc] = 1;

    // search for some stretch estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be deifned due to boundaries
    // we use OV accessors and test to filter negative values

    int  altC = c;

    // loop through corners attached to the current vertex
    // swing right around the fan untill full loop, border or UV seam
    bool onSeam = (OTC.size() != 0 ? (OTC[ov.n(altC)] == -3) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OTC.size() != 0 ? (OTC[ov.n(altC)] == -3) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions 
    const int startC = altC;
    int  count = 0;                         // number of valid stretch found
    glm::i64vec2 predUV(0, 0);
    do
    {
        if (count >= MAX_STRETCH_PREDS) break;

        const auto altV = TC[altC];
        const auto prevV = TC[ov.p(altC)];
        const auto nextV = TC[ov.n(altC)];
        if ((altV > -1 && prevV > -1 && nextV > -1) &&
            ((MV[altV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
        {
            predictUV(altC, attrUv, TC, predUV);
            ++count;
        }
        onSeam = (OTC.size() != 0 ? (OTC[ov.p(altC)] == -3) : false);
        altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
    } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation

    // 1. use stretch prediction when possible
    if (count > 0) {
        glm::i64vec2 roundig;
        const int64_t fracBits = (int64_t)UVPRED_FRACBITS;
        roundig.x = (predUV.x >= 0) ? (int64_t)(1 << (fracBits - 1)): -1 * (int64_t)(1 << (fracBits - 1));
        roundig.y = (predUV.y >= 0) ? (int64_t)(1 << (fracBits - 1)): -1 * (int64_t)(1 << (fracBits - 1));
        predUV = (predUV / glm::i64vec2(count) + roundig) >> fracBits;
        ioUVCoords.push_back(UV[tc] - (glm::vec2)predUV);
        ioAttrFine.push_back(true);
        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_tc = TC[ov.p(c)];
    const auto& c_n_tc = TC[ov.n(c)];

    if (c_p_tc > -1 && MV[c_p_tc] > -1) {
        ioUVCoords.push_back(UV[tc] - UV[c_p_tc]);
        ioAttrFine.push_back(false);
        return;
    }

    if (c_n_tc > -1 && MV[c_n_tc] > -1) {
        ioUVCoords.push_back(UV[tc] - UV[c_n_tc]);
        ioAttrFine.push_back(false);
        return;
    }

    // 3. if we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_tc = TC[b];
        if (MV[b_tc] > -1) {
            ioUVCoords.push_back(UV[tc] - UV[b_tc]);
            ioAttrFine.push_back(false);
            return;
        }
    }

    // 4. no other choice, it is a global start, pushed in separate table
    iosUVCoords.push_back(UV[tc]);

    return;
}

void UVCoordVertexAttributeEncoder::predictUV(
    const int c,
    AttributeVertexUVCoord* attrUv,
    const std::vector<int>& tcIndices,
    glm::i64vec2& predUV)
{

    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& G = mainEnc->attr->values;
    auto& MV = mainEnc->MV;

    const auto& OTC = attrUv->auxO;
    const auto& UV = attrUv->values;
    const auto& TC = tcIndices;  // texture coordinates indices

    glm::i64vec2 uvPrev = (glm::i64vec2)UV[TC[ov.p(c)]];
    glm::i64vec2 uvNext = (glm::i64vec2)UV[TC[ov.n(c)]];
    glm::i64vec2 uvCurr = (glm::i64vec2)UV[TC[c]];
    glm::i64vec3 gPrev = (glm::i64vec3)G[V[ov.p(c)]];
    glm::i64vec3 gNext = (glm::i64vec3)G[V[ov.n(c)]];
    glm::i64vec3 gCurr = (glm::i64vec3)G[V[c]];
    glm::i64vec3 gNgP = (glm::i64vec3)(gPrev - gNext);
    glm::i64vec3 gNgC = (glm::i64vec3)(gCurr - gNext);
    glm::i64vec2 uvNuvP = (glm::i64vec2)(uvPrev - uvNext);
    int64_t gNgP_dot_gNgC = gNgP.x * gNgC.x + gNgP.y * gNgC.y + gNgP.z * gNgC.z;
    int64_t d2_gNgP = gNgP.x * gNgP.x + gNgP.y * gNgP.y + gNgP.z * gNgP.z;
    const int64_t fracBits = (int64_t)UVPRED_FRACBITS;
    //
    if (d2_gNgP > 0) {
        glm::i64vec2 uvProj = (uvNext << fracBits) + uvNuvP * ((gNgP_dot_gNgC << fracBits) / d2_gNgP);
        glm::i64vec3 gProj = (gNext << fracBits) + gNgP * ((gNgP_dot_gNgC << fracBits) / d2_gNgP);
        glm::i64vec3 diff_gProj_gCurr = (gCurr << fracBits) - gProj;
        int64_t d2_gProj_gCurr = diff_gProj_gCurr.x * diff_gProj_gCurr.x + diff_gProj_gCurr.y * diff_gProj_gCurr.y + diff_gProj_gCurr.z * diff_gProj_gCurr.z;
        const glm::i64vec2 uvProjuvCurr = glm::i64vec2(uvNuvP.y, -uvNuvP.x) * (int64_t)(isqrt((d2_gProj_gCurr / d2_gNgP) << 2) >> 1);
        glm::i64vec2 predUV0(uvProj + uvProjuvCurr);
        glm::i64vec2 predUV1(uvProj - uvProjuvCurr);

        // the first estimation for this UV corner
        bool       useOpp = false;
        const bool onSeam = (OTC.size() != 0 ? (OTC[c] == -3) : false);
        // we cannot use the opposite if beyond a seam
        const bool checkOpposite =
            (!onSeam && O[c] >= 0 && TC[O[c]] >= 0 && MV[TC[O[c]]] > 0);

        if (checkOpposite) {
            // check that O not aligned with N and P (possible to disciminate sides
            const glm::i64vec2 uvOpp = UV[TC[O[c]]];
            // this test should be using 64b integers - this is ( vecNP ^ vec NO )
            const glm::i64vec2 NP = (uvPrev - uvNext);
            glm::i64vec2       NO(uvOpp - uvNext);
            // evaluate cross product
            const int64_t NPxNO = NP.x * NO.y - NP.y * NO.x;
            // in current implementation NPxNO is integer and check can be to strict zero
            useOpp = (NPxNO != 0);
        }

        if (useOpp) {
            const glm::i64vec2 uvOpp = UV[TC[O[c]]];
            const auto OUV0 = (uvOpp << fracBits) - predUV0;
            const auto OUV1 = (uvOpp << fracBits) - predUV1;
            predUV += ((OUV0.x * OUV0.x + OUV0.y * OUV0.y) < (OUV1.x * OUV1.x + OUV1.y * OUV1.y)) ? predUV1 : predUV0;
        }
        else {
            const auto CPr0 = (uvCurr << fracBits) - predUV0;
            const auto CPr1 = (uvCurr << fracBits) - predUV1;
            const bool orientation = (CPr0.x * CPr0.x + CPr0.y * CPr0.y) < (CPr1.x * CPr1.x + CPr1.y * CPr1.y);
            predUV += orientation ? predUV0 : predUV1;
            ioOrientations.push_back(orientation);
        }
    }
    // else average the two predictions
    else {
        predUV += (((glm::i64vec2)UV[TC[ov.n(c)]] << fracBits) + ((glm::i64vec2)UV[TC[ov.p(c)]] << fracBits)) >> (int64_t)1;
    }
}

void MaterialIDFaceAttributeEncoder::encodeAttributes(EBConfig& cfg)
{

    const auto& ov = mainEnc->attr->ct;

    // reset the face marking table
    UF.assign(mainEnc->nT, 0);
    // contains the previously travered face id value
    int prevFaceId = -1;
    // contains the previously travered tri
    int prevTri = -1;

    // I.9.8  decoding per face attributes
    for (auto corner : mainEnc->processedCorners)
    {
        const int corners[3] = { corner, ov.n(corner), ov.p(corner) };

        const int tri = ov.t(corner);
        auto      curFaceID = attr->values[tri];
        // store a boolean values checking for equallity between successive values
        ioFidsIdIsDifferent.push_back(curFaceID != prevFaceId);
        // if the id associated to the current face is equal
        // to the previously encoded value, nothing to do
        if (curFaceID != prevFaceId)
        {
            // check if the 3 adjacent faces (through opposites) have already been encoded
            // may be on a boundary, in this case the O is negative and t(O[]) to, hence the test
            const auto tFIdx = ov.t(ov.O[corners[0]]);
            const auto tRIdx = ov.t(ov.O[corners[1]]);
            const auto tLIdx = ov.t(ov.O[corners[2]]);
            bool decodedFacing = (tFIdx < 0 ? false : UF[tFIdx]) && !(tFIdx == prevTri);
            bool decodedRight = (tRIdx < 0 ? false : UF[tRIdx]) && !(tRIdx == prevTri);
            bool decodedLeft = (tLIdx < 0 ? false : UF[tLIdx]) && !(tLIdx == prevTri);
            // if already encoded, retreive corresponding ids
            int faceIdFacing = decodedFacing ? attr->values[ov.t(ov.O[corners[0]])] : -1;
            int faceIdRight = decodedRight ? attr->values[ov.t(ov.O[corners[1]])] : -1;
            int faceIdLeft = decodedLeft ? attr->values[ov.t(ov.O[corners[2]])] : -1;
            // starting from each of the adjacent triangles
            // (sharing one edge with the triangle we are encoding)
            for (auto k = 0; k < 3; k++)
            {
                // we will potentially update the "decoded" (decodable) status and related id
                // for each of the 3 reference triangles
                auto dec = (k == 0) ? decodedRight : ((k == 1) ? decodedLeft : decodedFacing);
                if (!dec)
                {
                    // initialise a starting "rotating corner" as a corner of the non encoded
                    // adjacent triangle (R for k==0, L if k==1, F if k==2) 
                    // rc is negative if we reach a boundary, we use accessor ov.o() that tests 
                    // range and returns 0 if so
                    auto rc = ov.o(corners[(k + 1) % 3]);
                    // then start looping through adjacent triangles, swinging around a first 
                    // corner shared with the triangle we are encoding
                    rc = ov.o(ov.n(rc));
                    // stopping if an encoded triangle is found, or if the analyzed face is one of 
                    // the 3 original R,L,F triangles
                    // rc is negative if we reach a boundary
                    while (rc >= 0 && rc != ov.p(ov.O[corners[(k) % 3]]) && !UF[ov.t(rc)])
                    {
                        rc = ov.O[ov.n(rc)];
                    }
                    if (rc < 0 || !UF[ov.t(rc)])
                    {
                        // if no encoded face found, then swing around the other shared corners
                        // rc is negative if we reach a boundary, we use accessor ov.o() that tests 
                        // range and returns 0 if so
                        rc = ov.o(corners[(k + 1) % 3]);
                        rc = ov.o(ov.p(rc));
                        while (rc >= 0 && rc != ov.n(ov.O[corners[(k + 2) % 3]]) && !UF[ov.t(rc)])
                        {
                            rc = ov.O[ov.p(rc)];
                        }
                    }
                    // negative if no new decoded was found
                    auto newDecoded = ((rc >= 0) && UF[ov.t(rc)] && !(ov.t(rc) == prevTri)) ? attr->values[ov.t(rc)] : -1;
                    // if an already encoded face is found during the procedure, then update
                    // the corresponding "decoded" (can be decoded) status, and Id value 
                    if (newDecoded >= 0) {
                        if (k == 0) {
                            decodedRight = true;
                            faceIdRight = newDecoded;
                        }
                        else if (k == 1) {
                            decodedLeft = true;
                            faceIdLeft = newDecoded;
                        }
                        else
                        {
                            decodedFacing = true;
                            faceIdFacing = newDecoded;
                        }
                    }
                }

            }

            if (decodedRight)
                ioFaceIdIsRight.push_back(faceIdRight == curFaceID);
            if (!decodedRight || (faceIdRight != curFaceID))
            {
                if ((decodedLeft) && (faceIdLeft != faceIdRight))
                    ioFaceIdIsLeft.push_back(faceIdLeft == curFaceID);
                if (!decodedLeft || (faceIdLeft != curFaceID))
                {
                    if ((decodedFacing) && (faceIdFacing != faceIdRight) && (faceIdFacing != faceIdLeft))
                        ioFaceIdIsFacing.push_back(faceIdFacing == curFaceID);
                    if (!decodedFacing || (faceIdFacing != curFaceID))
                        ioNotPredictedFaceId.push_back(curFaceID);
                }
            }
        }
        UF[tri] = 1; // mark the face as encoded
        prevFaceId = curFaceID;
        prevTri = tri;
    }
}

template <int dim>
void GenericVertexAttributeEncoder<dim>::encodeWithPrediction(
    int c,
    AttributeVertexGeneric<dim>* attrV,
    const std::vector<int>& attrIndices)
{
    const auto MAX_PARALLELOGRAMS = 4;

    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& OAI = attrV->auxO; // OTC => OAI
    const auto& AV = attrV->values; // UV => AV  attr value , AI attr index
    const auto& AI = attrIndices;    // TC => AI texture coordinate index table
    const auto& ai = AI[c];        // texture coordinate index for corner c
    auto& MV = mainEnc->MV;
    // mainEnc->attr->values 
    // is vertex already predicted ?
    if (MV[ai] > 0)
        return;
    // we mark the vertex
    MV[ai] = 1;

    // search for some stretch estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be deifned due to boundaries
    // we use OV accessors and test to filter negative values

    int  altC = c;

    // loop through corners attached to the current vertex
    // swing right around the fan untill full loop, border or UV seam
    bool onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -3) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -3) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

#if ENABLE_GENERIC_FXP
    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions 
    const int startC = altC;
    int  count = 0;                      // number of valid prediction found
    i64vecN predGen[MAX_PARALLELOGRAMS]; // predicted generic values
    // check if init required for portability? = { i64vecN(0) };

    // avoid this test in the loop using templates ?
    if (genPred == EBConfig::GenPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;

            const bool seamInPara = (OAI.size() && (OAI[altC] == -3));
            const auto oppoV = seamInPara ? -1 : ( (O[altC] >= 0) ? AI[O[altC]] : -1 );
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictGenPara(altC, attrV, AI, predGen[count++]);
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -3) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }

    // 1. use parallelogram prediction when possible
    if (count > 0) {
        // fixed point implementation identical to multiple parallelograms for position
        const int64_t scale[MAX_PARALLELOGRAMS][MAX_PARALLELOGRAMS] = {
            {GEO_SCALE, 0, 0, 0}, // 1.0, 0, 0, 0
            {GEO_SCALE / 2, GEO_SCALE / 2, 0, 0}, // 0.5, 0.5, 0, 0
            {(GEO_SCALE + 1) / 3, GEO_SCALE - 2 * (GEO_SCALE + 1) / 3, (GEO_SCALE + 1) / 3, 0}, // 1/3, 1/3, 1/3, 0 - approximation
            {GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4, GEO_SCALE / 4}, // 0.25, 0.25, 0.25, 0.25
        };
        i64vecN predError;
        const i64vecN& Gv = AV[ai];
        i64vecN pred(0);
        for (int i = 0; i < count; ++i) {
            pred += scale[count - 1][i] * predGen[i];
        }
        for (auto i = 0; i < dim; i++) {
            pred[i] = (pred[i] + (GEO_SCALE / 2)) >> GEO_SHIFT;
        }
        predError = Gv - pred;
        vecN fPredError = predError; // to be fixed for large qn values
        ioValues.push_back(fPredError);
        ioAttrFine.push_back(true);
        return;
    }
#else // previous non FXP implementation to remove
    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions 
    const int startC = altC;
    int  count = 0;                         // number of valid prediction found
    vecN predGen(0);

    // avoid this test in the loop using templates ?
    if (genPred == EBConfig::GenPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;

            const bool seamInPara = (OAI.size() && (OAI[altC] == -3));
            const auto oppoV = seamInPara ? -1 : ((O[altC] >= 0) ? AI[O[altC]] : -1);
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictGenPara(altC, attrV, AI, predGen);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -3) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }

    // 1. use parallelogram prediction when possible
    if (count > 0) {
        predGen = glm::round(predGen / vecN(count));
        ioValues.push_back(AV[ai] - predGen);
        ioAttrFine.push_back(true);
        return;
    }
#endif
    // 2. or fallback to delta with available values
    const auto& c_p_ai = AI[ov.p(c)];
    const auto& c_n_ai = AI[ov.n(c)];

    if (c_p_ai > -1 && MV[c_p_ai] > -1) {
        ioValues.push_back(AV[ai] - AV[c_p_ai]);
        ioAttrFine.push_back(false);
        return;
    }

    if (c_n_ai > -1 && MV[c_n_ai] > -1) {
        ioValues.push_back(AV[ai] - AV[c_n_ai]);
        ioAttrFine.push_back(false);
        return;
    }

    // 3. if we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_ai = AI[b];
        if (MV[b_ai] > -1) {
            ioValues.push_back(AV[ai] - AV[b_ai]);
            ioAttrFine.push_back(false);
            return;
        }
    }

    // 4. no other choice, it is a global start, pushed in separate table
    iosValues.push_back(AV[ai]);

    return;
}

#if ENABLE_GENERIC_FXP
template <int dim>
void GenericVertexAttributeEncoder<dim>::predictGenPara(
    int c,
    AttributeVertexGeneric<dim>* attrV,
    const std::vector<int>& attrIndices,
    i64vecN& predGen)
{
    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;

    const auto& AV = attrV->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    vecN avOppo = AV[AI[O[c]]];    // correction required to handle 32b qg
    vecN avPrev = AV[AI[ov.p(c)]];
    vecN avNext = AV[AI[ov.n(c)]];

    // parallelogram prediction = prevGen + nextGen - oppoGen
    predGen = avPrev + avNext - avOppo;
}
#else
template <int dim>
void GenericVertexAttributeEncoder<dim>::predictGenPara(
    int c,
    AttributeVertexGeneric<dim>* attrV,
    const std::vector<int>& attrIndices,
    vecN& predGen)
{
    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;

    const auto& AV = attrV->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    vecN avOppo = AV[AI[O[c]]];
    vecN avPrev = AV[AI[ov.p(c)]];
    vecN avNext = AV[AI[ov.n(c)]];

    // parallelogram prediction estGen = prevGen + nextGen - oppoGen
    const vecN estGen = avPrev + avNext - avOppo;
    predGen += estGen;
}
#endif

void NormalVertexAttributeEncoder::encodeWithPrediction(
    int c,
    AttributeVertexNormal* attrV,
    const std::vector<int>& attrIndices)
{
    const auto MAX_PARALLELOGRAMS = 4;

    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& OAI = attrV->auxO; // OTC => OAI
    const auto& AV = attrV->values; // UV => AV  attr value , AI attr index
    const auto& AI = attrIndices;    // TC => AI texture coordinate index table
    const auto& ai = AI[c];        // texture coordinate index for corner c
    auto& MV = mainEnc->MV;
    const int& qn = this->attr->qp;
    // mainEnc->attr->values 
    // is vertex already predicted ?
    if (MV[ai] > 0)
        return;
    // we mark the vertex
    MV[ai] = 1;

    // search for some estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be defined due to boundaries

    int  altC = c;

    // loop through corners attached to the current vertex
    // swing right around the fan untill full loop, border or UV seam
    bool onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -3) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -3) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions 
    const int startC = altC;
    int  count = 0;                         // number of valid prediction found
    glm::vec3 predNorm(0, 0, 0);

    // avoid this test in the loop using templates ?
    if (normPred == EBConfig::NormPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;

            const bool seamInPara = (OAI.size() && (OAI[altC] == -3));
            const auto oppoV = seamInPara ? -1 : ((O[altC] >= 0) ? AI[O[altC]] : -1);
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictNormPara(altC, attrV, AI, predNorm);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -3) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }
    else if (normPred == EBConfig::NormPred::CROSS){
        do
        {
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if (prevV > -1 && nextV > -1) // no check on marked predictions as Geo only used
            {
                predictNormCross(altC, predNorm);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -3) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation

    }
    // 1. use MPARA or Cross
    if (count > 0 && !(predNorm == glm::vec3(0,0,0))) {
        // Normalize and Scale
        const glm::i64vec3 predNormI64 = predNorm;
        int64_t  dot_predNorm = predNormI64.x * predNormI64.x + predNormI64.y * predNormI64.y + predNormI64.z * predNormI64.z;
        
        const int64_t irsqt = irsqrt(dot_predNorm);                                 // fxp:40 = NRM_SHIFT_1
        const glm::i64vec3 st1 = predNormI64 * irsqt;
        const glm::i64vec3 st2 = st1 + (int64_t)(1ULL << NRM_SHIFT_1);
        
        const glm::i64vec3 st3 = st2 << (int64_t)(qn-1);
        const glm::i64vec3 st4 = (st2 + (int64_t)1) >> (int64_t)1;
        const glm::i64vec3 st5 = st3 - st4 + (int64_t)(1ULL << (NRM_SHIFT_1-1));
        const glm::vec3 scaledPredNorm = st5 >> NRM_SHIFT_1;
        
        if (useOctahedral) {
            calculate2DResiduals(AV[ai], scaledPredNorm);
        }
        else {
            ioNormals.push_back(AV[ai] - scaledPredNorm);
        }
        ioAttrFine.push_back(true);
        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_ai = AI[ov.p(c)];
    const auto& c_n_ai = AI[ov.n(c)];

    if (c_p_ai > -1 && MV[c_p_ai] > -1) {
        if (useOctahedral) {
            calculate2DResiduals(AV[ai], AV[c_p_ai]);
        }
        else {
            ioNormals.push_back(AV[ai] - AV[c_p_ai]);            
        }
        ioAttrFine.push_back(false);
        return;
    }

    if (c_n_ai > -1 && MV[c_n_ai] > -1) {
        if (useOctahedral) {
            calculate2DResiduals(AV[ai], AV[c_n_ai]);
        }
        else {
            ioNormals.push_back(AV[ai] - AV[c_n_ai]);            
        }
        ioAttrFine.push_back(false);
        return;
    }

    // 3. if we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_ai = AI[b];
        if (MV[b_ai] > -1) {
            if (useOctahedral) {
                calculate2DResiduals(AV[ai], AV[b_ai]);
            }
            else {
                ioNormals.push_back(AV[ai] - AV[b_ai]);                
            }
            ioAttrFine.push_back(false);
            return;
        }
    }

    // 4. no other choice, it is a global start, pushed in separate table
    iosNormals.push_back(AV[ai]);

    return;
}

void NormalVertexAttributeEncoder::predictNormPara(
    const int c,
    AttributeVertexNormal* attrV,
    const std::vector<int>& attrIndices,
    glm::vec3& predNorm)
{
    const int& qn = this->attr->qp;
    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& G = mainEnc->attr->values;
    auto& MV = mainEnc->MV;

    const auto& OAI = attrV->auxO;
    const auto& AV = attrV->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    glm::vec3 avOppo = AV[AI[O[c]]];
    glm::vec3 avPrev = AV[AI[ov.p(c)]];
    glm::vec3 avNext = AV[AI[ov.n(c)]];

    // parallelogram prediction estNorm = prevNrm + nextNrm - oppoNrm
    glm::i32vec3 estNorm = avPrev + avNext - avOppo;
    
    const int32_t center = (1u << static_cast<uint32_t>(qn - 1));
    for (int c = 0; c < 3; c++) {
        estNorm[c] = estNorm[c] - center;
    }
    
    predNorm += estNorm;
}

void NormalVertexAttributeEncoder::predictNormCross(
    const int c, glm::vec3& predNorm)
{

    const auto& ov = mainEnc->attr->ct;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& G = mainEnc->attr->values;
    auto& MV = mainEnc->MV;

    glm::i64vec3 gPrev = G[V[ov.p(c)]];
    glm::i64vec3 gNext = G[V[ov.n(c)]];
    glm::i64vec3 gCurr = G[V[c]];

    const glm::i64vec3 gCgP = gPrev - gCurr;
    const glm::i64vec3 gCgN = gNext - gCurr;
    glm::vec3 estNorm;
    estNorm[0] = gCgN.y * gCgP.z - gCgP.y * gCgN.z;
	estNorm[1] = gCgN.z * gCgP.x - gCgP.z * gCgN.x;
	estNorm[2] = gCgN.x * gCgP.y - gCgP.x * gCgN.y;

    predNorm += estNorm;
}
// End of Normals 3D Predictions Functions.

// Octahedral 2D Functions.
void NormalVertexAttributeEncoder::calculate2DResiduals(const glm::vec3 original, const glm::vec3 pred)
{
    glm::vec2 orig2D(0, 0);
    glm::vec2 pred2D(0, 0);
    convert3Dto2Doctahedral(original, orig2D);
    convert3Dto2Doctahedral(pred, pred2D);

    glm::vec2 residual2D(0, 0);
    if (wrapAround) {
        const int32_t center = ( 1u << static_cast<uint32_t>( qpOcta-1 ) );
        for (int c = 0; c < 2; c++) {
            orig2D[c] = orig2D[c] - center;  // Convert it to signed integer
            pred2D[c] = pred2D[c] - center;  // Convert it to signed integer
        }
        residual2D = orig2D - pred2D;
        
        const int32_t   maxNormalValueplusOne = ( 1u << static_cast<uint32_t>( qpOcta ) );
        for (int c = 0; c < 2; c++) {
            // Wrap around at Encoder.
            if (residual2D[c] < -center){
                residual2D[c] = residual2D[c] + maxNormalValueplusOne;
            }
            else if (residual2D[c] > center-1){
                residual2D[c] = residual2D[c] - maxNormalValueplusOne;
            }
        }
    } else {
        residual2D = orig2D - pred2D;
    }
    ioOctNormals.push_back(residual2D);

    // Decoding
    glm::vec2 rec2D = pred2D + residual2D;

    if (wrapAround) {
        const int32_t center = ( 1u << static_cast<uint32_t>( qpOcta-1 ) );
        const int32_t  maxNormalValueplusOne = ( 1u << static_cast<uint32_t>( qpOcta ) );
        
        for (int c = 0; c < 2; c++) {
            if (rec2D[c] < -center)
                rec2D[c] = rec2D[c] + maxNormalValueplusOne;
            else if (rec2D[c] > center-1)
                rec2D[c] = rec2D[c] - maxNormalValueplusOne;
        }
        
        for (int c = 0; c < 2; c++) {
            rec2D[c] = rec2D[c] + center;  // Make it back to unsigned integer
        }
    }
    
    glm::vec3 reconstructed(0, 0, 0);
    convert2DoctahedralTo3D(rec2D, reconstructed);

    glm::vec3 secondResiduals = original - reconstructed;
    ioNormals.push_back(secondResiduals);      // Saving 2nd Residuals
}


void NormalVertexAttributeEncoder::convert3Dto2Doctahedral(glm::vec3 input, glm::vec2& output) {
    const int& qn = this->attr->qp;
    
    const int32_t center = ( 1u << static_cast<uint32_t>( qn-1 ) );
    for (int c = 0; c < 3; c++) {
        input[c] = input[c] - center;
    }

    const uint64_t divisor = std::abs(input.x) + std::abs(input.y) + std::abs(input.z);
    int32_t shift;
    const int64_t recipD = recipApprox(divisor, shift);                      // fxp:shift    
    glm::i64vec3 st0 = input;
    glm::i64vec3 normalized = st0 * recipD;
    
    glm::i64vec2 octahedral;
    if (normalized.z >= 0) {
        octahedral.x = normalized.x;
        octahedral.y = normalized.y;
    } else {
        octahedral.x = ((1ULL<<shift) - std::abs(normalized.y)) * std::copysign(1.f, normalized.x);
        octahedral.y = ((1ULL<<shift) - std::abs(normalized.x)) * std::copysign(1.f, normalized.y);
    }
    
    // Scale signed to unsigned with proper qp values.
    const glm::i64vec2 step1 = (octahedral + (int64_t)(1ULL << shift));           // fxp:shift
    const glm::i64vec2 step2 = step1 << (int64_t)(qpOcta-1);
    const glm::i64vec2 step3 = (step1 + (int64_t)1) >> (int64_t)1;
    const glm::i64vec2 step4 = step2 - step3 + (int64_t)(1ULL << (shift-1));
    output = step4 >> (int64_t)shift;
    
    return;
}

void NormalVertexAttributeEncoder::convert2DoctahedralTo3D(glm::vec2 input, glm::vec3& output) {
    const int& qn = this->attr->qp;
    
    const glm::i64vec2 inputI64 = input;
    const glm::i64vec2 inputI64_centered = (inputI64<<(int64_t)1) - (int64_t)((1<<qpOcta)-1);
    glm::i64vec3 threeDvec;
    threeDvec.x = inputI64_centered.x;
    threeDvec.y = inputI64_centered.y;
    threeDvec.z = (1<<qpOcta) - 1 - std::abs(threeDvec.x) - std::abs(threeDvec.y);
    
    if (threeDvec.z < 0) {
        const float x_t = threeDvec.x;
        threeDvec.x = (((1<<qpOcta)-1) - std::abs(threeDvec.y)) * std::copysign(1.f, x_t);
        threeDvec.y = (((1<<qpOcta)-1) - std::abs(x_t))   * std::copysign(1.f, threeDvec.y);
    }
    
    int64_t dot_2DI = threeDvec.x * threeDvec.x + threeDvec.y * threeDvec.y + threeDvec.z * threeDvec.z;
    const int64_t irsqt = irsqrt(dot_2DI);                                                   // fxp:40 = NRM_SHIFT_1
    
    const glm::i64vec3 st1 = threeDvec * irsqt + (int64_t)(1ULL << NRM_SHIFT_1);
    const glm::i64vec3 st2 = (st1 << (int64_t)(qn-1)) - ((st1+(int64_t)1) >> (int64_t)(1));
    output = (st2 + (int64_t)(1ULL << (NRM_SHIFT_1-1))) >> NRM_SHIFT_1;      // fxp:0
    
    return;
}

// End of Octahedral Functions.


//
bool EBReversiEncoder::save(std::string fileName) {

    Bitstream bitstream;
#if defined(MEB_BITSTREAM_TRACE)
    eb::Logger loggerMeb;
    loggerMeb.initilalize(fileName + "_meb", true);
    bitstream.setLogger(loggerMeb);
    bitstream.setTrace(true);
#endif

    if (!serialize(bitstream))
        return false;

    auto t = now();
    if (!bitstream.save(fileName)) {
        std::cerr << "Error: can't save compressed bitstream!\n";
        return false;
    }

    COUT << "  Bitstream write to file time (ms) = " << elapsed(t) << std::endl;

    COUT << "  Saved bitstream byte size = " << bitstream.size() << std::endl;

    return true;
}

// entropy encoding and serialization into a bitstream
bool EBReversiEncoder::serialize(Bitstream& bitstream) {

    auto timeAll = now();
    auto t = now();

    if (primaryEncoders.empty()) {
        std::cerr << "Error: no mesh to serialize" << std::endl;
        exit(0);
    }

    // First pass on encoders to check and potentially set qp values when intAttr is set
    for (auto iEnc : encoders) {
        if (auto encPos = dynamic_cast<PositionVertexAttributeEncoder*>(iEnc))
        {
            encPos->qp = encPos->attr->qp;
            if (encPos->attr->values.empty()) {
                encPos->qp = -1; // do not encode
                std::cerr << "Error: no vertex values in position attribute" << std::endl;
                exit(0);
            }
            else if (cfg.intAttr && (encPos->qp >= 0))
            {
                auto minv = std::numeric_limits<int>::max();
                auto maxv = std::numeric_limits<int>::min();
                for (auto& v : encPos->iosVertices)
                {
                    for (auto i = 0; i < v.length(); ++i)
                    {
                        minv = std::min((int)v[i], minv);
                        maxv = std::max((int)v[i], maxv);
                    }
                }
                if (minv < 0) // // not handled in this version
                {
                    encPos->qp = -1; // do not encode
                    std::cerr << "ERROR : negative quantized position values present" << std::endl;
                    exit(0);
                }

                if ((encPos->qp == 0) && encPos->iosVertices.size()) // no auto quantize done before on full set of data
                {
                    encPos->qp = std::ceil(std::log2(maxv + 1));
                    COUT << "  qp = " << encPos->qp << " (auto)" << std::endl;
                }

                if (maxv > ((1 << encPos->qp) - 1))
                {
                    encPos->qp = -1; // do not encode
                    std::cerr << "ERROR : maximal position values beyond quantized range" << std::endl;
                    exit(0);
                }
            }
        }
        if (auto encFid = dynamic_cast<MaterialIDFaceAttributeEncoder*>(iEnc))
        {
            encFid->qp = qm;
            //  exit if no attribute values
            if (encFid->attr->values.empty()) {
                encFid->qp = -1; // do not encode
                std::cerr << "Error: no face values in material id attribute" << std::endl;
                exit(0);
            }
            else if (qm >= 0) // assumes int values no test on cfg.intAttr
            {
                auto minv = std::numeric_limits<int>::max();
                auto maxv = std::numeric_limits<int>::min();
                for (auto v : encFid->ioNotPredictedFaceId) // coding/decoding possible, but still may not correspond to quantized range for the complete mesh
                {
                    minv = std::min(v, minv);
                    maxv = std::max(v, maxv);
                }
                if (minv < 0) // not handled in this version
                {
                    encFid->qp = -1; // do not encode
                    std::cerr << "ERROR : negative face Id values present" << std::endl;
                    exit(0);
                }

                if ((qm == 0) && encFid->ioNotPredictedFaceId.size()) // ioNotPredictedFaceId should always be > 0
                {
                    encFid->qp = std::ceil(std::log2(maxv + 1));
                    COUT << "  qm = " << encFid->qp << " (auto)" << std::endl;
                }

                if (maxv > ((1 << encFid->qp) - 1))
                {
                    encFid->qp = -1; // do not encode
                    std::cerr << "ERROR : maximal face Id values beyond quantized range" << std::endl;
                    exit(0);
                }
            }
        }
        if (auto encUv = dynamic_cast<UVCoordVertexAttributeEncoder*>(iEnc))
        {
            encUv->qp = encUv->attr->qp;
            //  exit if no attribute values
            if (encUv->attr->values.empty()) {
                encUv->qp = -1; // do not encode
                std::cerr << "Error: no vertex values in texture coordinate attribute" << std::endl;
                exit(0);
            }
            else if (cfg.intAttr && (qt >= 0))
            {
                auto minv = std::numeric_limits<int>::max();
                auto maxv = std::numeric_limits<int>::min();
                for (auto& v : encUv->iosUVCoords) // coding/decoding possible, but still may not correspond to quantized range for the complete mesh
                {
                    for (auto i = 0; i < v.length(); ++i)
                    {
                        minv = std::min((int)v[i], minv);
                        maxv = std::max((int)v[i], maxv);
                    }
                }
                if (minv < 0) // not handled in this version
                {
                    encUv->qp = -1; // do not encode ?? then need to reshuffle reference indices !!
                    std::cerr << "ERROR : negative quantized texture coordinates values present" << std::endl;
                    exit(0);
                }

                if ((qt == 0) && encUv->iosUVCoords.size()) // no auto quantize done before on full set of data
                {
                    encUv->qp = std::ceil(std::log2(maxv + 1));
                    COUT << "  qt = " << encUv->qp << " (auto)" << std::endl;
                }

                if (maxv > ((1 << encUv->qp) - 1))
                {
                    encUv->qp = -1; // do not encode
                    std::cerr << "ERROR : maximal texture coordinates values beyond quantized range" << std::endl;
                    exit(0);
                }
            }
        }
        if (auto encNor = dynamic_cast<NormalVertexAttributeEncoder*>(iEnc))
        {
            encNor->qp = encNor->attr->qp;
            //  exit if no attribute values
            if (encNor->attr->values.empty()) {
                encNor->qp = -1; // do not encode
                std::cerr << "Error: no vertex values in normal attribute" << std::endl;
                exit(0);
            }
            else if (cfg.intAttr && (qn >= 0))
            {
                auto minv = std::numeric_limits<int>::max();
                auto maxv = std::numeric_limits<int>::min();
                for (auto& v : encNor->iosNormals) // coding/decoding possible, but still may not correspond to quantized range for the complete mesh
                {
                    for (auto i = 0; i < v.length(); ++i)
                    {
                        minv = std::min((int)v[i], minv);
                        maxv = std::max((int)v[i], maxv);
                    }
                }
                if (minv < 0) // not handled in this version
                {
                    encNor->qp = -1; // do not encode ?? then need to reshuffle reference indices !!
                    std::cerr << "ERROR : negative quantized normal values present" << std::endl;
                    exit(0);
                }

                if ((qn == 0) && encNor->iosNormals.size()) // no auto quantize done before on full set of data
                {
                    encNor->qp = std::ceil(std::log2(maxv + 1));
                    COUT << "  qn = " << encNor->qp << " (auto)" << std::endl;
                }

                if (maxv > ((1 << encNor->qp) - 1))
                {
                    encNor->qp = -1; // do not encode
                    std::cerr << "ERROR : maximal normal values beyond quantized range" << std::endl;
                    exit(0);
                }
            }
        }
        if (iEnc->attrType == eb::Attribute::Type::GENERIC)
        {
            auto processGen = [&](auto* encGen) {

                encGen->qp = encGen->attr->qp;
                //  exit if no attribute values
                if (encGen->attr->values.empty()) {
                    encGen->qp = -1; // do not encode 
                    std::cerr << "Error: no vertex values in generic attribute" << std::endl;
                    exit(0);
                }
                else if (cfg.intAttr && (qg >= 0))
                {
                    auto minv = std::numeric_limits<int>::max();
                    auto maxv = std::numeric_limits<int>::min();
                    for (auto& v : encGen->iosValues) // coding/decoding possible, but still may not correspond to quantized range for the complete mesh
                    {
                        for (auto i = 0; i < v.length(); ++i)
                        {
                            minv = std::min((int)v[i], minv);
                            maxv = std::max((int)v[i], maxv);
                        }
                    }
                    if (minv < 0) // not handled in this version
                    {
                        encGen->qp = -1; // do not encode ?? then need to reshuffle reference indices !!
                        std::cerr << "ERROR : negative quantized generic values present" << std::endl;
                        exit(0);
                    }

                    if ((qg == 0) && encGen->iosValues.size()) // no auto quantize done before on full set of data
                    {
                        encGen->qp = std::ceil(std::log2(maxv + 1));
                        COUT << "  qg = " << encGen->qp << " (auto)" << std::endl;
                    }

                    if (maxv > ((1 << encGen->qp) - 1))
                    {
                        encGen->qp = -1; // do not encode
                        std::cerr << "ERROR : maximal generic values beyond quantized range" << std::endl;
                        exit(0);
                    }
                }
            };

            switch (iEnc->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeEncoder<1>*>(iEnc)); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeEncoder<2>*>(iEnc)); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeEncoder<3>*>(iEnc)); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeEncoder<4>*>(iEnc)); break;
            };

            // terminate is number of components nots in the 1..4 range
            if (iEnc->nbComp > 4 || iEnc->nbComp < 1)
            {
                std::cerr << "Error: invalid attribute type, expected a per vertex generic with " << iEnc->nbComp << " components" << std::endl;
                exit(0);
            }
        }
    }

    // first position attribute encoder
    PositionVertexAttributeEncoder* encPos = primaryEncoders[0];
    if (encPos->qp < 0)
    {
        std::cerr << "Error: invalid data for mesh primary encoder" << std::endl;
        exit(0);
    }


    COUT << "  AC encoding useEntropyPacket = " << cfg.useEntropyPacket << std::endl;

    // K-8 CtxTbl=1 shared contexts
    //      mesh_position_fine_residual[][]
    //      mesh_position_coarse_residual[][]
    //      mesh_attribute_fine_residual[][][]
    //      mesh_attribute_coarse_residual[][][]
    //      mesh_normal_octahedral_second_residual[][][]

    AdaptiveBitModel ctxTbl1Trunc[3];
    AdaptiveBitModel ctxTbl1CoeffRemPrefix[12];
    AdaptiveBitModel ctxTbl1CoeffRemSuffix[12];
    StaticBitModel   ctxTbl1Sign;

    // K-8 CtxTbl=2
    //      mesh_clers_symbol[]
    // defined later in the code

    // K-8 CtxTbl 7..10
    AdaptiveBitModel ctxTbl7_mesh_handle_first_vdl4m1[4]; // mesh_handle_first_variable_delta_length4_minus1[ ]
    StaticBitModel   ctxTbl8; // mesh_handle_first_variable_delta
    AdaptiveBitModel ctxTbl9_mesh_handle_second_vdl4m1[4]; // mesh_handle_second_variable_delta_length4_minus1[ ]
    StaticBitModel   ctxTbl10; // mesh_handle_second_variable_delta

    // K-8 CtxTbl=others

    AdaptiveBitModel ctxTblOthers[16 - 3 - 4 + 1];
    enum CtxIndex {
        mesh_attribute_seam = 0,
        mesh_texcoord_stretch_orientation,
        mesh_handle_first_sign,
        mesh_handle_second_shift,
        mesh_position_is_duplicate_flag,
        mesh_attribute_is_duplicate_flag,
        mesh_materialid_default_not_equal_flag,
        mesh_materialid_default_left_flag,
        mesh_materialid_default_right_flag,
        mesh_materialid_default_facing_flag,
    };

    EntropyEncoder     acE;
    const auto maxAcBufLen = primaryEncoders[0]->ioVertices.size() * 3 * 4 * 20 + 1024;
    // buffer sizing to revise, function of nb attributes and attribute types
    acE.setBuffer(maxAcBufLen, nullptr);
    acE.start();

    // Attributes
    uint8_t attributeCount = 0;
    uint8_t positionCount = 0;


    const int MinHandles = MIN_HANDLE; // handles always AC coded as MIN_HANDLE set to 0

    // second pass on encoders to perform AC coding
    size_t iEncIdx = 0;
    std::vector<uint8_t> cAttrIdx(encoders.size());
    std::vector<uint8_t> iAttrIdx;

    for (auto iEncIdx = 0; iEncIdx < encoders.size(); ++iEncIdx){

        auto& iEnc = encoders[iEncIdx];

        if (iEnc->qp < 0) // at this level autoqp has been done  when == 0 all values are 0 - very special case - should we handle topo only 
        {
            COUT << "  Attribute encoding bypassed ( " << iEncIdx << ", "<<iEnc->attrType << " )"<< std::endl;
        }
        else if (auto encPos = dynamic_cast<PositionVertexAttributeEncoder*>(iEnc))
        {
            ++positionCount;
            if (positionCount > 1)
            {
                std::cerr << "Error: current code cannot handle more than one position attribute" << std::endl;
                exit(0);
            }
            if (attributeCount > 0)
            {
                std::cerr << "Error: current code requires a primary position attribute" << std::endl;
                exit(0);
            }

            // Handles
            const auto NumHandles = encPos->ioHandles.size() / 2;
            COUT << "  Handles count = " << NumHandles << std::endl;

            if (NumHandles >= MinHandles) // AC coding only if handles are not to few
            {
                int prev1 = 0;
                int prev2 = 0;
                const auto nexp = 8;
                const auto capexp = 3;
                auto acStart = acE.pos();
                int fixed = 0;
                for (auto i = 0; i < NumHandles; i++) {
                    int cur1 = (abs(encPos->ioHandles[2 * i + 0]) - 2) / 3;
                    acE.encode(encPos->ioHandles[2 * i + 0] > 0, ctxTblOthers[CtxIndex::mesh_handle_first_sign]);       // AC encode the sign of h1, using ctx1
                    int cur2 = (encPos->ioHandles[2 * i + 1] - 1) / 3;
                    acE.encode((encPos->ioHandles[2 * i + 1] - 1) % 3, ctxTblOthers[CtxIndex::mesh_handle_second_shift]); // AC encode the shift of h2, using ctx2
                    // AC encode the first signed delta value per blocks of 4 bits using multiple contexts cexp[]
                    unsigned int val;
                    val = 2 * abs(cur1 - prev1) - ((cur1 - prev1) < 0 ? 1 : 0);
                    int nb;
#if HANDLE_MOD
                    unsigned int offset;
                    unsigned int prevOffset;
                    nb = 1;
                    offset = (1 << HGRP);
                    prevOffset = 0;
                    while (val >= offset) {
                        nb++;
                        prevOffset = offset;
                        offset = (offset << HGRP) + (1 << HGRP);
                    }
                    val = val - prevOffset;
#else
                    nb = std::ceil(log2(val + 1) / 4);
                    if (nb == 0) nb = 1;
#endif
                    for (auto i = 1; i < nb; ++i)
                    {
                        acE.encode(1, ctxTbl7_mesh_handle_first_vdl4m1[std::min(i - 1, capexp)]);
                    }
                    acE.encode(0, ctxTbl7_mesh_handle_first_vdl4m1[std::min(nb - 1, capexp)]);
#if HANDLE_MOD
                    fixed += HGRP * nb;
                    for (auto i = HGRP * nb - 1; i >= 0; --i)
#else
                    fixed += 4 * nb;
                    for (auto i = 4 * nb - 1; i >= 0; --i)
#endif
                    {
                        acE.encode(val & (1 << i), ctxTbl8);
                    }
                    // AC encode the second signed delta value per blocks of 4 bits using multiple contexts cexp[]
                    val = 2 * abs(cur2 - prev2) - ((cur2 - prev2) < 0 ? 1 : 0);
#if HANDLE_MOD
                    nb = 1;
                    offset = (1 << HGRP);
                    prevOffset = 0;
                    while (val >= offset) {
                        nb++;
                        prevOffset = offset;
                        offset = (offset << HGRP) + (1 << HGRP);
                    }
                    val = val - prevOffset;
#else
                    nb = std::ceil(log2(val + 1) / 4);
                    if (nb == 0) nb = 1;
#endif
                    for (auto i = 1; i < nb; ++i)
                    {
                        acE.encode(1, ctxTbl9_mesh_handle_second_vdl4m1[std::min(i - 1, capexp)]);
                    }
                    acE.encode(0, ctxTbl9_mesh_handle_second_vdl4m1[std::min(nb - 1, capexp)]);
#if HANDLE_MOD
                    fixed += HGRP * nb;
                    for (auto i = HGRP * nb - 1; i >= 0; --i)
#else
                    fixed += 4 * nb;
                    for (auto i = 4 * nb - 1; i >= 0; --i)
#endif
                    {
                        acE.encode(val & (1 << i), ctxTbl10);
                    }
                    prev1 = cur1;
                    prev2 = cur2;
                }
                auto handleOrientationLength = acE.pos() - acStart;
                const auto byteCount = uint32_t(handleOrientationLength);
                COUT << "  Handles auxiliary orientation selection bytes = " << handleOrientationLength
                    << " , bph = " << 8.0 * (float)handleOrientationLength / encPos->ioHandles.size() / 2 << std::endl;
            }

            // build with and without--clersUpdate 
#define CDVERSION // CD version with 30 contexts
#define CDUPDATE // DIS version

            // CLERS table convert "letters" to integers

#ifdef CDUPDATE // update CD version
            // convert "letters" to enums in 0..4
            typedef enum {
                CLERS_C = 0,
                CLERS_L = 1,
                CLERS_E = 2,
                CLERS_R = 3,
                CLERS_S = 4,
            } CLERS;

            const std::map<char, CLERS> clersToInt = { {'C',CLERS_C}, {'L',CLERS_L}, {'E',CLERS_E}, {'R',CLERS_R},{'S',CLERS_S} };

            // per state code to truncted unary coded value 
            const uint8_t clersToVal[9][5] =
            {
                //   C  L  E  R  S
                    {1, 3, 4, 0, 2}, // RCSLE
                    {1, 3, 4, 0, 2}, // RCSLE
                    {1, 3, 4, 0, 2}, // RCSLE
                    {3, 2, 0, 1, 4}, // ERLCS
                    {0, 3, 2, 1, 4}, // CRELS
                    {0, 3, 4, 1, 2}, // CRSLE
                    {0, 2, 4, 1, 3}, // CRLSE
                    {0, 2, 1, 3, 4}, // CELRS
                    {0, 4, 2, 1, 3}  // CRESL
            };

            std::vector<CLERS> clersEnums;
            clersEnums.reserve(encPos->ioClers.size());
            for (auto i = 0; i < encPos->ioClers.size(); ++i) {
                const auto symb = clersToInt.find(encPos->ioClers[i])->second;
                clersEnums.push_back(symb);
            }
#else
#ifdef CDVERSION
            // convert "letters" to 0..4 unary encoded 
            const std::map<char, uint32_t> clersToInt = { {'C',0}, {'R',1}, {'S',3}, {'L',7},{'E',15} };
#else
            const std::map<char, uint32_t> clersToInt = { {'C',0}, {'S',1}, {'L',3}, {'R',5},{'E',7} };
#endif
            std::vector<uint32_t> clersUI32;
            clersUI32.reserve(encPos->ioClers.size());
            for (auto i = 0; i < encPos->ioClers.size(); ++i) {
                const auto symb = clersToInt.find(encPos->ioClers[i])->second;
                clersUI32.push_back(symb);
            }
#endif


            auto tc = now();
            size_t topoLength = 0;
            {
                auto acStart = acE.pos();
#ifdef CDVERSION
                // alternative using binary coding for clers symbols and three bit contexts
                // adding context selection based on previous symbol
                const auto maxAcBufLent = encPos->ioClers.size() + 1024;

#ifdef CDUPDATE
                int Crun = 0;
                auto prevC = CLERS_R;
                auto prevprevC = CLERS_E;
                bool prevNotC = true;

                AdaptiveBitModel ctxStateBinArray[36]; // 3x2 unused - only as a coding simplification
#else
                int prev = 1; // align with initial pS = 5 ( 'R' )
                int prevprev = 0;
                int Crun = 0;

                AdaptiveBitModel ctx_isNotC[9];
                AdaptiveBitModel ctx_isNotR[9];
                AdaptiveBitModel ctx_isNotS[6];
                AdaptiveBitModel ctx_bit12[6];
#endif

                int count = 0;
#ifdef CDUPDATE
                int pS = 7; // 'R' => state 7 ('R' (not 'C' not 'R')  'C' is most frequent start followd by E
                bool useExtended = (clersEnums.size() > 3000); // fixed threshold is this basic variant
                for (const auto& curSymbol : clersEnums) {
                    auto value = clersToVal[pS][curSymbol];
                    auto* ctxBinArray = &ctxStateBinArray[pS << 2]; // [pS * 4] 3x2 ununsed - but simpler to write as this

                    const bool bit0 = (value > 0);
                    acE.encode(bit0, ctxBinArray[0]);
                    count++;
                    if (bit0)
                    {
                        const bool bit1 = (value > 1);
                        acE.encode(bit1, ctxBinArray[1]);
                        count++;
                        if (prevNotC) {
                            if (bit1)
                            {
                                const bool bit2 = (value > 2);
                                acE.encode(bit2, ctxBinArray[2]);
                                count++;
                                if (bit2)
                                {
                                    const bool bit3 = (value > 3);
                                    acE.encode(bit3, ctxBinArray[3]);
                                    count++;
                                }
            }
                        }
                    }

                    prevprevC = prevC;
                    prevC = curSymbol;
                    prevNotC = prevC != CLERS_C;
                    Crun = prevNotC ? 0 : Crun + 1;
                    switch (prevC) {
                    case CLERS_C:// prev is C
                        pS = !useExtended ? 0 : Crun < 2 ? 0 : Crun < 7 ? 1 : 2;
                        break;
                    case CLERS_S:// prev is S
                        pS = 3;
                        break;
                    case CLERS_L:// prev is L
                        pS = 4;
                        break;
                    case CLERS_R:// prev is R
                        pS = 5 + (prevprevC == CLERS_C /*C*/ ? 0 : prevprevC == CLERS_R/*R*/ ? 1 : 2);
                        break;
                    case CLERS_E:// prev is E
                        pS = 8;
                        break;
                    }
                }
#else
                int pS = 5; // 'R'
                bool useExtended = (clersUI32.size() > 3000); // fixed threshold is this basic variant
                for (auto i = 0; i < clersUI32.size(); ++i) {
                    const auto value = clersUI32[i];
                    const bool isNotC = (value & 1);
                    acE.encode(isNotC, ctx_isNotC[pS]);
                    count++;
                    if (isNotC)
                    {
                        const bool isNotR = (value & 2);
                        acE.encode(isNotR, ctx_isNotR[pS]);
                        count++;
                        if (prev > 0 || i == 0) {
                            if (isNotR)
                            {
                                const bool isNotS = (value & 4);
                                const bool bit2 = (value & 8);
                                acE.encode(isNotS, ctx_isNotS[pS - 3]);
                                count++;
                                if (isNotS) {
                                    acE.encode(bit2, ctx_bit12[pS - 3]);
                                    count++;
                                }
                            }
                        }
                    }

                    prevprev = prev;
                    prev = value;

                    Crun = isNotC ? 0 : Crun + 1;
                    switch (prev) {
                    case 0:// prev is C
                        pS = !useExtended ? 0 : Crun < 2 ? 0 : Crun < 7 ? 1 : 2;
                        break;
                    case 3:// prev is S
                        pS = 3;
                        break;
                    case 7:// prev is L
                        pS = 4;
                        break;
                    case 1:// prev is R
                        pS = 5 + (prevprev == 0/*C*/ ? 0 : prevprev == 1/*R*/ ? 1 : 2);
                        break;
                    case 15:// prev is E
                        pS = 8;
                        break;
                    }
                }
#endif
                topoLength = acE.pos() - acStart;
                const auto byteCount = uint32_t(topoLength);
                COUT << "  Connectivity bytes = " << topoLength << " , bins = " << count << " , bpf = " << 8.0 * (float)topoLength / encPos->ioClers.size() << std::endl;
#else // original InterDigital soluiton with 4x32 contexts
                // alternative using binary coding for clers symbols and three bit contexts
                // adding context selection based on previous symbol
                AdaptiveBitModel ctx_isNotC[32]; // not all 32 contexts used - code to be rewitten
                AdaptiveBitModel ctx_bit1[32];
                AdaptiveBitModel ctx_bit02[32];
                AdaptiveBitModel ctx_bit12[32];

                int pS = 0; // 'C'
                bool useExtended = (clersUI32.size() > 3000); // fixed threshold is this basic variant
                for (auto i = 0; i < clersUI32.size(); ++i) {
                    const auto value = clersUI32[i];
                    const bool isNotC = (value & 1);
                    acE.encode(isNotC, ctx_isNotC[pS]);
                    count++;
                    if (isNotC)
                    {
                        const bool bit1 = (value & 2);
                        const bool bit2 = (value & 4);
                        acE.encode(bit1, ctx_bit1[pS]);
                        count++;
                        if (bit1)
                            acE.encode(bit2, ctx_bit12[pS]);
                        else
                            acE.encode(bit2, ctx_bit02[pS]);
                        count++;
                    }
                    if (!useExtended)
                        pS = value; // 5 contexts
                    else
                        pS = value + ((pS & 1) << 3) + ((pS & 4) << 2); // extended contexts (4*5)
                }
                topoLength = acE.pos() --acStart;
                const auto byteCount = uint32_t(topoLength);
                COUT << "  Connectivity bytes = " << topoLength << " , bins = " << count << ", bpf = " << 8.0 * (float)topoLength / encPos->ioClers.size() << std::endl;
#endif
            }
            COUT << "  CLERS AC coding time (ms) = " << elapsed(tc) << std::endl;

            size_t posLengthC = 0;
            size_t posLengthF = 0;
            size_t posCoarseNum = 0;
            size_t posFineNum = 0;
            // encode fine then coarse residuals
            int count = 0;
            {
                const auto maxOffset = 7;
                const auto egK = 2;
                const auto bctx = 2;
                auto acStart = acE.pos();
                for (int32_t k = 0; k < 3; ++k) {
                    for (auto i = 0; i < encPos->ioVertices.size(); ++i) {
                        if (!encPos->ioAttrFine[i]) continue;
                        count++;
                        acE.encodeTUExpGolombS((int)(encPos->ioVertices[i][k]), maxOffset, egK, ctxTbl1Sign,
                            ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                            bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEO - 1, MAX_CTX_COEFF_REM_SUFFIX_GEO - 1);
                    }
                }
                posLengthF = acE.pos() - acStart;
                const auto byteCount = uint32_t(posLengthF);
            }
            posFineNum = count / 3;
            count = 0;
            {
                const auto maxOffset = 7;
                const auto egK = 2;
                const auto bctx = 3;
                auto acStart = acE.pos();
                for (int32_t k = 0; k < 3; ++k) {
                    for (auto i = 0; i < encPos->ioVertices.size(); ++i) {
                        if (encPos->ioAttrFine[i]) continue;
                        count++;
                        acE.encodeTUExpGolombS((int)(encPos->ioVertices[i][k]), maxOffset, egK, ctxTbl1Sign,
                            ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                            bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEO - 1, MAX_CTX_COEFF_REM_SUFFIX_GEO - 1);
                    }
                }
                posLengthC = acE.pos() - acStart;
                const auto byteCount = uint32_t(posLengthC);
            }
            posCoarseNum = count / 3;

            auto posLength = posLengthC + posLengthF;
            COUT << "  Positions count = " << posCoarseNum << " " << (encPos->ioVertices.size() - posCoarseNum) << " ,qp= " << encPos->qp << std::endl;
            COUT << "  Positions bytes = " << posLength << " ( " << posLengthC << " , " << posLengthF << " ), bpv = " << 8.0 * (float)posLength / encPos->ioVertices.size() << std::endl;

            // store in the encoder for later use when filling the syntax
            encPos->attrCoarseNum = posCoarseNum;

            if (cfg.deduplicate)
            {
                if (encPos->ioDuplicateSplitVertexIdx.size())
                {
                    const auto acStart = acE.pos();
                    for (auto i = 0; i < encPos->isVertexDup.size(); ++i) {
                        acE.encode(encPos->isVertexDup[i], ctxTblOthers[CtxIndex::mesh_position_is_duplicate_flag]);
                    }
                    auto dupLength = acE.pos() - acStart;
                    const auto byteCount = uint32_t(dupLength);
                    COUT << "  Binary signaling of duplicate indices = " << byteCount << std::endl;
                }
            }

            if (cfg.useEntropyPacket)
            {
                auto byteCount = acE.stop();
                encPos->meshAttributeEntropyPacketBuffer.assign(acE.buffer(), acE.buffer() + byteCount);
            }
        }
        else
        {
            // count the number of non positionnal attributes counted and prepare mapping when some are bypassed

            // add some bypass conditions
            // TO ADD restricts also combinations with per face ...  
            const bool bypass = !(
                (iEnc->attrType == eb::Attribute::Type::UVCOORD) ||
                //(iEnc->attrType == eb::Attribute::Type::COLOR) ||             // bypassed as corresponding prediction process not defined in this version
                (iEnc->attrType == eb::Attribute::Type::NORMAL) ||
                (iEnc->attrType == eb::Attribute::Type::GENERIC) ||
                (iEnc->attrType == eb::Attribute::Type::MATERIAL_ID));

            if (bypass) continue;

            iAttrIdx.push_back(iEncIdx);
            cAttrIdx[iEncIdx] = ++attributeCount;

            if (cfg.useEntropyPacket)
            {
                // restart the ac engine, reusing the same buffer - buffer sizing should be reviewed
                acE.setBuffer(maxAcBufLen, (uint8_t*)acE.buffer());
                acE.start();
                // reset all contexts in K-8 table 1
                for (auto& ctx : ctxTbl1Trunc)
                    ctx.reset();
                for (auto& ctx : ctxTbl1CoeffRemPrefix)
                    ctx.reset();
                for (auto& ctx : ctxTbl1CoeffRemSuffix)
                    ctx.reset();
                for (auto& ctx : ctxTblOthers)
                    ctx.reset();
                for (auto& ctx : ctxTbl7_mesh_handle_first_vdl4m1)
                    ctx.reset();
                for (auto& ctx : ctxTbl9_mesh_handle_second_vdl4m1)
                    ctx.reset();
            }
            else
            {
// intermediate test
// RESET_CONTEXTS_PER_ATTRIBUTE expected to be set to 0
// to avoid context reset within an entropy packet
#if RESET_CONTEXTS_PER_ATTRIBUTE
                for (auto& ctx : ctxTbl1Trunc)
                    ctx.reset();
                for (auto& ctx : ctxTbl1CoeffRemPrefix)
                    ctx.reset();
                for (auto& ctx : ctxTbl1CoeffRemSuffix)
                    ctx.reset();
#endif

// intermediate test
// ENFORCE_NORESET expected to be set to 1
// to avoid bin context reset within an entropy packet
#if !ENFORCE_NORESET
                // non regression test
                for (auto& ctx : ctxTblOthers)
                    ctx.reset();
                for (auto& ctx : ctxTbl7_mesh_handle_first_vdl4m1)
                    ctx.reset();
                for (auto& ctx : ctxTbl9_mesh_handle_second_vdl4m1)
                    ctx.reset();
#endif
            }

            if (auto encFid = dynamic_cast<MaterialIDFaceAttributeEncoder*>(iEnc))
            {
                auto acStart = acE.pos();
                for (auto fidRes : encFid->ioNotPredictedFaceId) {
                    acE.encodeExpGolomb(fidRes, 2, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 
                        MAX_CTX_COEFF_REM_PREFIX_MTL - 1, MAX_CTX_COEFF_REM_SUFFIX_MTL - 1);
                }
                auto resLength = acE.pos() - acStart;
                acStart = acE.pos();
                // ordering must striclty follow syntax ordering when using common packet for multiple syntax elements
                for (auto fidD : encFid->ioFidsIdIsDifferent) {
                    acE.encode(fidD, ctxTblOthers[CtxIndex::mesh_materialid_default_not_equal_flag]);
                }
                auto resDLength = acE.pos() - acStart;
                acStart = acE.pos();

                for (auto fidL : encFid->ioFaceIdIsLeft) {
                    acE.encode(fidL, ctxTblOthers[CtxIndex::mesh_materialid_default_left_flag]);
                }
                auto resLLength = acE.pos() - acStart;
                acStart = acE.pos();

                for (auto fidR : encFid->ioFaceIdIsRight) {
                    acE.encode(fidR, ctxTblOthers[CtxIndex::mesh_materialid_default_right_flag]);
                }
                auto resRLength = acE.pos() - acStart;
                acStart = acE.pos();

                for (auto fidF : encFid->ioFaceIdIsFacing) {
                    acE.encode(fidF, ctxTblOthers[CtxIndex::mesh_materialid_default_facing_flag]);
                }
                auto resFLength = acE.pos() - acStart;

                auto totalLength = resLength + resRLength + resLLength + resFLength + resDLength;

                COUT << "  MaterialID bytes = " << totalLength << " ( " << resLength << " , " << resDLength << " , " << resLLength << " , " << resRLength << " , " << resFLength << " ), bpf = " << 8.0 * (float)totalLength / encFid->ioFidsIdIsDifferent.size() << std::endl;
            }
            else if (auto encUv = dynamic_cast<UVCoordVertexAttributeEncoder*>(iEnc))
            {

                size_t uvLengthC = 0;
                size_t uvLengthF = 0;
                size_t uvCoarseNum = 0;

                // encode fine then coarse residuals

                int count = 0;
                {
                    const auto maxOffset = 10;
                    const auto egK = 1;
                    const auto bctx = 2;
                    auto acStart = acE.pos();
                    for (auto i = 0; i < encUv->ioUVCoords.size(); ++i) {
                        for (int32_t k = 0; k < 2; ++k) {
                            if (!encUv->ioAttrFine[i]) continue;
                            count++;
                            acE.encodeTUExpGolombS((int)(encUv->ioUVCoords[i][k]), maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_UV - 1, MAX_CTX_COEFF_REM_SUFFIX_UV - 1);
                        }
                    }
                    uvLengthF = acE.pos() - acStart;
                    const auto byteCount = uint32_t(uvLengthF);
                }
                count = 0;
                {
                    const auto maxOffset = 7;
                    const auto egK = 2;
                    const auto bctx = 3;
                    auto acStart = acE.pos();
                    for (auto i = 0; i < encUv->ioUVCoords.size(); ++i) {
                        for (int32_t k = 0; k < 2; ++k) {
                            if (encUv->ioAttrFine[i]) continue;
                            count++;
                            acE.encodeTUExpGolombS((int)(encUv->ioUVCoords[i][k]), maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_UV - 1, MAX_CTX_COEFF_REM_SUFFIX_UV - 1);
                        }
                    }
                    uvLengthC = acE.pos() - acStart;
                    const auto byteCount = uint32_t(uvLengthC);
                }
                uvCoarseNum = count >> 1;
                // store in the encoder for later use when filling the syntax
                encUv->attrCoarseNum = uvCoarseNum;


                auto uvLength = uvLengthC + uvLengthF;
                COUT << "  UVCoords count = " << uvCoarseNum << " " << (encUv->ioUVCoords.size() - uvCoarseNum) << " ,qt= " << encUv->qp << std::endl;
                COUT << "  UVCoords bytes = " << uvLength << " ( " << uvLengthC << " , " << uvLengthF << " ), bpv = " << 8.0 * (float)uvLength / encUv->ioUVCoords.size() << std::endl;

                // extra data for mpara 
                if (cfg.uvPred == EBConfig::UvPred::STRETCH)
                {
                    size_t uvOrientationLength = 0;
                    {
                        auto acStart = acE.pos();
                        for (auto it = encUv->ioOrientations.begin(); it != encUv->ioOrientations.end(); ++it) {
                            acE.encode(*it, ctxTblOthers[CtxIndex::mesh_texcoord_stretch_orientation]);
                        }
                        uvOrientationLength = acE.pos() - acStart;
                    }
                    COUT << "  UVCoords auxiliary orientation selection bytes = " << uvOrientationLength << " bpv = " << 8.0 * (float)uvOrientationLength / encUv->ioOrientations.size() << std::endl;
                }

                // extra data for uv coord seams when separte uv indices - should be replicated for all non geo attributes
                if (encUv->attr->hasOwnIndices)
                {
                    if (encUv->ioSeams.size()) {
                        size_t uvSeamsLength = 0;
                        {
                            auto acStart = acE.pos();
                            for (auto i = 0; i < encUv->ioSeams.size(); ++i) {
                                acE.encode(encUv->ioSeams[i], ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                            }
                            uvSeamsLength = acE.pos() - acStart;
                            const auto byteCount = uint32_t(uvSeamsLength);
                            const auto nbSeams = uint32_t(encUv->ioSeams.size());
                        }
                        COUT << "  UVCoords auxiliary seam bytes = " << uvSeamsLength << ", bpv = " << 8.0 * (float)uvSeamsLength / encUv->ioUVCoords.size() << std::endl;
                    }

                    if (cfg.deduplicate)
                    {
                        if (encUv->ioDuplicateSplitVertexIdx.size()) {
                            auto acStart = acE.pos();
                            for (auto i = 0; i < encUv->isVertexDup.size(); ++i) {
                                acE.encode(encUv->isVertexDup[i], ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                            }
                            auto dupLength = acE.pos() - acStart;
                            const auto byteCount = uint32_t(dupLength);
                            COUT << "  Binary signaling of duplicate UV indices = " << byteCount << std::endl;
                        }
                    }
                }
            }
            else if (iEnc->attrType == eb::Attribute::Type::GENERIC)
            {
// expected to be set to 1 to align code with specification
#if ENFORCE_BYPASS
                const auto ATT_GEN_NBPFXCTX = 12;
                const auto ATT_GEN_NBSFXCTX = 12;
#else
                const auto ATT_GEN_NBPFXCTX = INT_MAX;
                const auto ATT_GEN_NBSFXCTX = INT_MAX;
#endif
                auto processGen = [&](auto&& encGen) {

                    size_t genLengthC = 0;
                    size_t genLengthF = 0;
                    size_t genCoarseNum = 0;
                    const auto nbComp = encGen->nbComp;
                    int count = 0;
                    {
                        const auto maxOffset = 7;
                        const auto egK = 2;
                        const auto bctx = 2;
                        auto acStart = acE.pos();

                        for (auto i = 0; i < encGen->ioValues.size(); ++i) {
                            for (int32_t k = 0; k < nbComp; ++k) {
                                if (!encGen->ioAttrFine[i]) continue;
                                count++;
                                acE.encodeTUExpGolombS((int)(encGen->ioValues[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_GEN_NBPFXCTX, ATT_GEN_NBSFXCTX,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1, MAX_CTX_COEFF_REM_SUFFIX_GEN - 1);
                            }
                        }
                        genLengthF = acE.pos() - acStart;
                        const auto byteCount = uint32_t(genLengthF);

                    }
                    count = 0;
                    {
                        const auto maxOffset = 7;
                        const auto egK = 2;
                        const auto bctx = 3;
                        auto acStart = acE.pos();
                        for (auto i = 0; i < encGen->ioValues.size(); ++i) {
                            for (int32_t k = 0; k < nbComp; ++k) {
                                if (encGen->ioAttrFine[i]) continue;
                                count++;
                                acE.encodeTUExpGolombS((int)(encGen->ioValues[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_GEN_NBPFXCTX, ATT_GEN_NBSFXCTX,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1, MAX_CTX_COEFF_REM_SUFFIX_GEN - 1);
                            }
                        }
                        genLengthC = acE.pos() - acStart;
                        const auto byteCount = uint32_t(genLengthC);

                    }
                    genCoarseNum = count / nbComp;
                    // store in the encoder for later use when filling the syntax
                    encGen->attrCoarseNum = genCoarseNum;

                    auto genLength = genLengthC + genLengthF;
                    COUT << "  Generic count = " << genCoarseNum << " " << (encGen->ioValues.size() - genCoarseNum) << " ,qg= " << encGen->qp << std::endl;
                    COUT << "  Generic bytes = " << genLength << " ( " << genLengthC << " , " << genLengthF << " ), bpv = " << 8.0 * (float)genLength / encGen->ioValues.size() << std::endl;

                    // extra data for generic seams when separate indices 
                    if (encGen->attr->hasOwnIndices)
                    {
                        if (encGen->ioSeams.size()) {
                            size_t genSeamsLength = 0;
                            {
                                auto acStart = acE.pos();
                                for (auto i = 0; i < encGen->ioSeams.size(); ++i) {
                                    acE.encode(encGen->ioSeams[i], ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                                }
                                genSeamsLength = acE.pos() - acStart;
                                const auto byteCount = uint32_t(genSeamsLength);
                                const auto nbSeams = uint32_t(encGen->ioSeams.size());
                            }
                            COUT << "  Generic auxiliary seam bytes = " << genSeamsLength << ", bpv = " << 8.0 * (float)genSeamsLength / encGen->ioValues.size() << std::endl;
                        }

                        if (cfg.deduplicate)
                        {
                            if (encGen->ioDuplicateSplitVertexIdx.size()) {
                                auto acStart = acE.pos();
                                for (auto i = 0; i < encGen->isVertexDup.size(); ++i) {
                                    acE.encode(encGen->isVertexDup[i], ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                                }
                                auto dupLength = acE.pos() - acStart;
                                const auto byteCount = uint32_t(dupLength);
                                COUT << "  Binary signaling of duplicate UV indices = " << byteCount << std::endl;
                            }
                        }
                    }
                };

                switch (iEnc->nbComp)
                {
                case 1: processGen(dynamic_cast<GenericVertexAttributeEncoder<1>*>(iEnc)); break;
                case 2: processGen(dynamic_cast<GenericVertexAttributeEncoder<2>*>(iEnc)); break;
                case 3: processGen(dynamic_cast<GenericVertexAttributeEncoder<3>*>(iEnc)); break;
                case 4: processGen(dynamic_cast<GenericVertexAttributeEncoder<4>*>(iEnc)); break;
                };
                // terminate is number of components nots in the 1..4 range
                if (iEnc->nbComp > 4 || iEnc->nbComp < 1)
                {
                    std::cerr << "Error: invalid attribute type, expected a per vertex generic with " << iEnc->nbComp << " components" << std::endl;
                    exit(0);
                }
            }
            else if (auto encNor = dynamic_cast<NormalVertexAttributeEncoder*>(iEnc))
            {

                // Entropy Encoding Normals
                size_t normLengthC = 0;
                size_t normLengthF = 0;
                size_t normCoarseNum = 0;
                size_t normSecondRLength = 0;


                int norm_dimensions = 2;
                int norm_residual_size = encNor->ioOctNormals.size();
                if (!encNor->useOctahedral) {
                    norm_dimensions = 3;
                    norm_residual_size = encNor->ioNormals.size();
                }
// expected to be set to 1 to align code with specification
#if ENFORCE_BYPASS
                const auto ATT_NOR_NBPFXCTX_F = 10;
                const auto ATT_NOR_NBSFXCTX_F = 1;
                const auto ATT_NOR_NBPFXCTX_C = 5;
                const auto ATT_NOR_NBSFXCTX_C = 1;
                const auto ATT_NOR_SEC_NBPFXCTX = 1;
                const auto ATT_NOR_SEC_NBSFXCTX = 1;
#else
                const auto ATT_NOR_NBPFXCTX_F = INT_MAX;
                const auto ATT_NOR_NBSFXCTX_F = INT_MAX;
                const auto ATT_NOR_NBPFXCTX_C = INT_MAX;
                const auto ATT_NOR_NBSFXCTX_C = INT_MAX;
                const auto ATT_NOR_SEC_NBPFXCTX = INT_MAX;
                const auto ATT_NOR_SEC_NBSFXCTX = INT_MAX;
#endif                
                int count = 0;
                {
                    const auto maxOffset = 7;
                    const auto egK = 5;
                    const auto bctx = 2;
                    auto acStart = acE.pos();
                    for (int32_t k = 0; k < norm_dimensions; ++k) {
                        for (auto i = 0; i < norm_residual_size; ++i) {
                            if (!encNor->ioAttrFine[i]) continue;
                            count++;
                            if (cfg.useOctahedral)
                                acE.encodeTUExpGolombS((int)(encNor->ioOctNormals[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_F, ATT_NOR_NBSFXCTX_F,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_F - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                            else
                                acE.encodeTUExpGolombS((int)(encNor->ioNormals[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_F, ATT_NOR_NBSFXCTX_F,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_F - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                        }
                    }
                    normLengthF = acE.pos() - acStart;
                    const auto byteCount = uint32_t(normLengthF);
                }

                count = 0;
                {
                    const auto maxOffset = 7;
                    const auto egK = 1;
                    const auto bctx = 1;
                    auto acStart = acE.pos();
                    for (int32_t k = 0; k < norm_dimensions; ++k) {
                        for (auto i = 0; i < norm_residual_size; ++i) {
                            if (encNor->ioAttrFine[i]) continue;
                            count++;
                            if (cfg.useOctahedral)
                                acE.encodeTUExpGolombS((int)(encNor->ioOctNormals[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_C, ATT_NOR_NBSFXCTX_C,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_C - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                            else
                                acE.encodeTUExpGolombS((int)(encNor->ioNormals[i][k]), maxOffset, egK, ctxTbl1Sign,
                                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_C, ATT_NOR_NBSFXCTX_C,
                                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_C - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                        }
                    }
                    normLengthC = acE.pos() - acStart;
                    const auto byteCount = uint32_t(normLengthC);
                }
                normCoarseNum = count / norm_dimensions;
                // store in the encoder for later use when filling the syntax
                encNor->attrCoarseNum = normCoarseNum;

                auto normLength = normLengthC + normLengthF;
                COUT << "  Normal count = " << normCoarseNum << " " << (norm_residual_size - normCoarseNum) << " , qn= " << qn << std::endl;
                COUT << "  Normal bytes = " << normLength << " ( " << normLengthC << " , " << normLengthF << " ), bpv = " << 8.0 * (float)normLength / norm_residual_size << std::endl;

                // Second Residuals in case of Octahedral        
                if (cfg.useOctahedral && cfg.normalEncodeSecondResidual) {
                    const auto maxOffset = 7;
                    const auto egK = 1;
                    const auto bctx = 2;
                    auto acStart = acE.pos();
                    for (int32_t k = 0; k < 3; ++k) {
                        for (auto i = 0; i < encNor->ioNormals.size(); ++i) {
                            acE.encodeTUExpGolombS((int)(encNor->ioNormals[i][k]), maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_SEC_NBPFXCTX, ATT_NOR_SEC_NBSFXCTX,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_SEC - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR_SEC - 1);
                        }
                    }
                    normSecondRLength = acE.pos() - acStart;
                    const auto byteCount = uint32_t(normSecondRLength);

                    COUT << "  Normal Second Residual Count = " << encNor->ioNormals.size() << std::endl;
                    COUT << "  Normal Second Residual Bytes = " << normSecondRLength << " , bpv = " << 8.0 * (float)normSecondRLength / encNor->ioNormals.size() << std::endl;
                }

                // extra data for uv normal seams when separte indices
                if (encNor->attr->hasOwnIndices)
                {
                    if (encNor->ioSeams.size()) {
                        size_t attrSeamsLength = 0;
                        {
                            auto acStart = acE.pos();
                            for (auto i = 0; i < encNor->ioSeams.size(); ++i) {
                                acE.encode(encNor->ioSeams[i], ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                            }
                            attrSeamsLength = acE.pos() - acStart;
                            const auto byteCount = uint32_t(attrSeamsLength);
                            const auto nbSeams = uint32_t(encNor->ioSeams.size());
                        }
                        COUT << "  Normals auxiliary seam bytes = " << attrSeamsLength << ", bpv = " << 8.0 * (float)attrSeamsLength / encNor->ioNormals.size() << std::endl;
                    }

                    if (cfg.deduplicate)
                    {
                        if (encNor->ioDuplicateSplitVertexIdx.size()) {
                            auto acStart = acE.pos();
                            for (auto i = 0; i < encNor->isVertexDup.size(); ++i) {
                                acE.encode(encNor->isVertexDup[i], ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                            }
                            auto dupLength = acE.pos() - acStart;
                            const auto byteCount = uint32_t(dupLength);
                            COUT << "  Binary signaling of duplicate Normal indices = " << byteCount << std::endl;
                        }
                    }
                }
            }

            if (cfg.useEntropyPacket)
            {
                auto byteCount = acE.stop();
                iEnc->meshAttributeEntropyPacketBuffer.assign(acE.buffer(), acE.buffer() + byteCount);
            }
        }
    }

    if (!cfg.useEntropyPacket)
    {
        auto byteCount = acE.stop();
        encPos->meshAttributeEntropyPacketBuffer.assign(acE.buffer(), acE.buffer() + byteCount);
    }


    COUT << "  AC encoding time (ms) = " << elapsed(t) << std::endl;

    t = now();

    // Using bitstream writer to fill syntax tables 
    MeshCoding meshCoding; // Top level syntax element
    EbWriter   ebWriter;

    // Filling Mesh Coding Header
    auto& mch = meshCoding.getMeshCodingHeader();
    // Codec Variant
    mch.getMeshCodecType() = MeshCodecType::CODEC_TYPE_REVERSE;
    // vertex traversal method
    switch (cfg.traversal) {
    case EBConfig::Traversal::EB: mch.getMeshVertexTraversalMethod() = MeshVertexTraversalMethod::MESH_EB_TRAVERSAL; break;
    case EBConfig::Traversal::DEGREE: mch.getMeshVertexTraversalMethod() = MeshVertexTraversalMethod::MESH_DEGREE_TRAVERSAL; break;
    }
    mch.getMeshEntropyPacketFlag() = cfg.useEntropyPacket;
    //Deduplicate Method
    mch.getMeshDeduplicateMethod() = cfg.deduplicate ? MeshDeduplicateMethod::MESH_DEDUP_DEFAULT : MeshDeduplicateMethod::MESH_DEDUP_NONE;
    switch (cfg.reindexOutput) {
    case EBConfig::Reindex::EB: mch.getMeshReindexOutput() = MeshVertexTraversalMethod::MESH_EB_TRAVERSAL; break;
    case EBConfig::Reindex::DEGREE: mch.getMeshReindexOutput() = MeshVertexTraversalMethod::MESH_DEGREE_TRAVERSAL; break;
    }
    // Position Encoding Parameters
    auto& mpep = mch.getMeshPositionEncodingParameters();
    mpep.getMeshPositionBitDepthMinus1() = encPos->qp - 1;
    mpep.getMeshPositionPredictionMethod() = MeshPositionPredictionMethod::MESH_POSITION_MPARA;
    mpep.getMeshPositionReverseUnificationFlag() = cfg.reverseUnification;
    // Position Dequantization
    mch.getMeshPositionDequantizeFlag() = !cfg.intAttr; // this condition to be modified through a more flexible ebEncode interface
    if (mch.getMeshPositionDequantizeFlag())
    {
        auto& mpdp = mch.getMeshPositionDequantizeParameters();
        auto& minPos = encPos->attr->minVal;
        auto& maxPos = encPos->attr->maxVal;
        mpdp.getMeshPositionMin(0) = minPos.x;
        mpdp.getMeshPositionMin(1) = minPos.y;
        mpdp.getMeshPositionMin(2) = minPos.z;
        mpdp.getMeshPositionMax(0) = maxPos.x;
        mpdp.getMeshPositionMax(1) = maxPos.y;
        mpdp.getMeshPositionMax(2) = maxPos.z;
    }

    mch.allocateAttributes(attributeCount);
    mch.getMeshAttributeCount() = attributeCount;

    for (auto i = 0; i < mch.getMeshAttributeCount(); i++) {

        auto iEnc = encoders[iAttrIdx[i]];
        if (auto encFid = dynamic_cast<MaterialIDFaceAttributeEncoder*>(iEnc))
        {
            mch.getMeshAttributeType()[i] = MeshAttributeType::MESH_ATTR_MATERIAL_ID;
            auto& maep = mch.getMeshAttributesEncodingParameters(i);
            maep.getMeshAttributeBitDepthMinus1() = encFid->qp - 1;
            maep.getMeshAttributePerFaceFlag() = true;
            maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_MATERIALID::MESH_MATERIALID_DEFAULT; // only one method for face IDs
            mch.getMeshAttributeDequantizeFlag()[i] = false;
        }
        else if (auto encUv = dynamic_cast<UVCoordVertexAttributeEncoder*>(iEnc))
        {
            mch.getMeshAttributeType()[i] = MeshAttributeType::MESH_ATTR_TEXCOORD; 
            auto& maep = mch.getMeshAttributesEncodingParameters(i);

            maep.getMeshAttributeBitDepthMinus1() = encUv->qp - 1; 
            maep.getMeshAttributePerFaceFlag() = false;
            if (!maep.getMeshAttributePerFaceFlag())
            {
                maep.getMeshAttributeSeparateIndexFlag() = encUv->attr->hasOwnIndices; 
                if (!maep.getMeshAttributeSeparateIndexFlag())
                {
                    if (encUv->attr->refAuxIdx <0) // should not occur using only refAuxIdx and not hasOwnIndices
                        std::cout << "ERROR : reference attribute error" << std::endl;
                    // current code not using cAttrIdx array as refAuxIdx restricted to be 0 in this version
                    maep.getMeshAttributeReferenceIndexPlus1() = encUv->attr->refAuxIdx; // we always use position as the principal attibute for now
                }
            }

            maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_TEXCOORD::MESH_TEXCOORD_STRETCH;
            // this condition to be modified through a more flexible ebEncode interface for per attribute selection
            mch.getMeshAttributeDequantizeFlag()[i] = !cfg.intAttr;
            if (mch.getMeshAttributeDequantizeFlag()[i])
            {
                auto& madp = mch.getMeshAttributesDequantizeParameters()[i];
                auto& minUv = encUv->attr->minVal;
                auto& maxUv = encUv->attr->maxVal;
                madp.getMeshAttributeMin(0) = minUv.x;
                madp.getMeshAttributeMin(1) = minUv.y;
                madp.getMeshAttributeMax(0) = maxUv.x;
                madp.getMeshAttributeMax(1) = maxUv.y;
            }
        }
        else if (auto encNor = dynamic_cast<NormalVertexAttributeEncoder*>(iEnc))
        {
            mch.getMeshAttributeType()[i] = MeshAttributeType::MESH_ATTR_NORMAL;
            mch.getMeshNormalOctahedralFlag()[i] = encNor->useOctahedral;
            auto& maep = mch.getMeshAttributesEncodingParameters(i);
            maep.getMeshAttributeBitDepthMinus1() = encNor->qp - 1;
            maep.getMeshAttributePerFaceFlag() = false;
            if (!maep.getMeshAttributePerFaceFlag())
            {
                maep.getMeshAttributeSeparateIndexFlag() = encNor->attr->hasOwnIndices;
                if (!maep.getMeshAttributeSeparateIndexFlag())
                {
                    if (encNor->attr->refAuxIdx < 0) // should not occur using only refAuxIdx and not hasOwnIndices
                        std::cout << "ERROR : reference attribute error" << std::endl;
                    // current code not using cAttrIdx array as refAuxIdx restricted to be 0 in this version
                    maep.getMeshAttributeReferenceIndexPlus1() = encNor->attr->refAuxIdx; // we always use position as the principal attibute for now
                }
            }

            if (cfg.normPred == EBConfig::NormPred::DELTA)
                maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_DELTA;
            else if (cfg.normPred == EBConfig::NormPred::MPARA)
                maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_MPARA;
            else if (cfg.normPred == EBConfig::NormPred::CROSS)
                maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_CROSS;

            mch.getMeshAttributeDequantizeFlag()[i] = !cfg.intAttr;
            if (mch.getMeshAttributeDequantizeFlag()[i])
            {
                auto& madp = mch.getMeshAttributesDequantizeParameters()[i];
                auto& minNrm = encNor->attr->minVal;
                auto& maxNrm = encNor->attr->maxVal;
                madp.getMeshAttributeMin(0) = minNrm.x;
                madp.getMeshAttributeMin(1) = minNrm.y;
                madp.getMeshAttributeMin(2) = minNrm.z;
                madp.getMeshAttributeMax(0) = maxNrm.x;
                madp.getMeshAttributeMax(1) = maxNrm.y;
                madp.getMeshAttributeMax(2) = maxNrm.z;
            }
        }
        else if (iEnc->attrType == eb::Attribute::Type::GENERIC)
        {
            
            mch.getMeshAttributeType()[i] = MeshAttributeType::MESH_ATTR_GENERIC;
            mch.getMeshAttributeNumComponentsMinus1()[i] = iEnc->nbComp -1;

            auto& maep = mch.getMeshAttributesEncodingParameters(i);

            auto processGen = [&](auto* encGen) {
                using EncGenT = typename std::remove_pointer<decltype(encGen)>::type;
                maep.getMeshAttributeBitDepthMinus1() = encGen->qp - 1;
                maep.getMeshAttributePerFaceFlag() = false;
                if (!maep.getMeshAttributePerFaceFlag())
                {
                    maep.getMeshAttributeSeparateIndexFlag() = encGen->attr->hasOwnIndices;
                    if (!maep.getMeshAttributeSeparateIndexFlag())
                    {
                        if (encGen->attr->refAuxIdx < 0) // should not occur using only refAuxIdx and not hasOwnIndices
                            std::cout << "ERROR : reference attribute error" << std::endl;
                        // current code not using cAttrIdx array as refAuxIdx restricted to be 0 in this version
                        maep.getMeshAttributeReferenceIndexPlus1() = encGen->attr->refAuxIdx; // we always use position as the principal attibute for now
                    }
                }

                if (cfg.genPred == EBConfig::GenPred::DELTA)
                    maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_GENERIC::MESH_GENERIC_DELTA;
                else if (cfg.genPred == EBConfig::GenPred::MPARA)
                    maep.getMeshAttributePredictionMethod() = (uint8_t)MeshAttributePredictionMethod_GENERIC::MESH_GENERIC_MPARA;

                mch.getMeshAttributeDequantizeFlag()[i] = !cfg.intAttr;
                if (mch.getMeshAttributeDequantizeFlag()[i])
                {
                    auto& madp = mch.getMeshAttributesDequantizeParameters()[i];
                    auto& minGen = encGen->attr->minVal;
                    auto& maxGen = encGen->attr->maxVal;
                    for (auto i = 0; i < encGen->attr->nbComp; ++i)
                    {
                        madp.getMeshAttributeMin(i) = minGen[i];
                        madp.getMeshAttributeMax(i) = maxGen[i];
                    }
                }
            };

            switch (iEnc->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeEncoder<1>*>(iEnc)); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeEncoder<2>*>(iEnc)); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeEncoder<3>*>(iEnc)); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeEncoder<4>*>(iEnc)); break;
            };
            // terminate is number of components nots in the 1..4 range
            if (iEnc->nbComp > 4 || iEnc->nbComp < 1)
            {
                std::cerr << "Error: invalid attribute type, expected a per vertex generic with " << iEnc->nbComp << " components" << std::endl;
                exit(0);
            }
        }
    }

    // Payload

    // Filling Mesh Position Coding Payload
    auto& mpcp = meshCoding.getMeshPositionCodingPayload();

    mpcp.getMeshTriangleCount() = encPos->nT;
    mpcp.getMeshClersCount() = encPos->ioClers.size();
    mpcp.getMeshCcWithBoundaryCount() = encPos->nB; // we only write the number of CC with boundaries
    const auto NumHandles = encPos->ioHandles.size() / 2;
    mpcp.getMeshHandlesCount() = NumHandles;
#if MIN_HANDLE
    if (NumHandles < MinHandles)
    { // store deltas
        mpcp.getMeshHandleFirstDelta().resize(NumHandles);
        mpcp.getMeshHandleSecondDelta().resize(NumHandles);
        for (auto i = 0; i < NumHandles; i++) {
            mpcp.getMeshHandleFirstDelta()[i] = (int)encPos->ioHandles[2 * i + 0] - (i ? encPos->ioHandles[2 * i - 2] : 0);
            mpcp.getMeshHandleSecondDelta()[i] = (int)encPos->ioHandles[2 * i + 1] - (i ? encPos->ioHandles[2 * i - 1] : 0);
        }
    }
#endif
    auto NumPositionStart = 0;
    auto& mpdi = mpcp.getMeshPositionDeduplicateInformation();

    if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
        mpdi.getMeshPositionDeduplicateCount() = encPos->ioDuplicateSplitVertexIdx.size();
        unsigned int NumSplitVertex = 0;
        if (mpdi.getMeshPositionDeduplicateCount() > 0)
        {
            mpdi.getMeshPositionDeduplicateIdx().resize(mpdi.getMeshPositionDeduplicateCount());
            for (uint32_t i = 0; i < mpdi.getMeshPositionDeduplicateCount(); ++i) {
                mpdi.getMeshPositionDeduplicateIdx()[i] = encPos->ioDuplicateSplitVertexIdx[i];
            }
//            mpdi.getMeshPositionIsDuplicateSize() = encPos->meshAttributeIsDuplicateFlagBuffer.size();
//            mpdi.getMeshPositionIsDuplicateFlag() = encPos->meshAttributeIsDuplicateFlagBuffer;

        }
    }


    NumPositionStart = encPos->iosVertices.size();
    mpcp.getMeshPositionStartCount() = NumPositionStart;
    mpcp.getMeshPositionStart().resize(NumPositionStart);
    for (auto i = 0; i < NumPositionStart; i++) {
        mpcp.getMeshPositionStart()[i].resize(3);
        for (auto j = 0; j < 3; j++) {
            mpcp.getMeshPositionStart()[i][j] = encPos->iosVertices[i][j];
        }
    }

    mch.getMeshGeoEntropyPacketBufferSize() = encPos->meshAttributeEntropyPacketBuffer.size();
    mch.getMeshGeoEntropyPacketBuffer() = encPos->meshAttributeEntropyPacketBuffer;


    mpcp.getMeshPositionFineResidualsCount() = encPos->ioVertices.size() - encPos->attrCoarseNum;
    mpcp.getMeshPositionCoarseResidualsCount() = encPos->attrCoarseNum;
    //Differnce information
    if (mpep.getMeshPositionReverseUnificationFlag()) {
      auto& mdi = mpcp.getMeshDifferenceInformation();
      mdi.getMeshAddPointCount() = addPointList.size();
      if (mdi.getMeshAddPointCount() > 0) {
        mdi.getAddPointIdxDelta().resize(mdi.getMeshAddPointCount());
        for (auto i = 0u; i < mdi.getMeshAddPointCount(); i++) {
          mdi.getAddPointIdxDelta()[i] = (i > 0) ? addPointList[i] - addPointList[i - 1] : addPointList[0];
        }
      }

      mdi.getMeshDeletePointCount() = deletePointList.size();
      if (mdi.getMeshDeletePointCount() > 0) {
        mdi.getDeletePointIdxDelta().resize(mdi.getMeshDeletePointCount());
        for (auto i = 0u; i < mdi.getMeshDeletePointCount(); i++) {
          mdi.getDeletePointIdxDelta()[i] = (i > 0) ? deletePointList[i] - deletePointList[i - 1] : deletePointList[0];
        }
      }

      mdi.getModifyMeshPointCount() = modifyTriangles.size();
      if (mdi.getModifyMeshPointCount() > 0) {
        mdi.getModifyTrianglesDelta().resize(mdi.getModifyMeshPointCount());
        mdi.getModifyIdx().resize(mdi.getModifyMeshPointCount());
        mdi.getModifyPointIdxDelta().resize(mdi.getModifyMeshPointCount());
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          mdi.getModifyTrianglesDelta()[i] = (i > 0) ? modifyTriangles[i] - modifyTriangles[i - 1] : modifyTriangles[0];
        }
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          mdi.getModifyIdx()[i] = modifyIndex[i];
        }
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          mdi.getModifyPointIdxDelta()[i] = (i > 0) ? modifyPointID[i] - modifyPointID[i - 1] : modifyPointID[0];
        }
      }
    }

    // Filling Mesh Attribute Coding Payload
    auto& macp = meshCoding.getMeshAttributeCodingPayload();
    macp.allocate(mch.getMeshAttributeCount());
    for (auto i = 0; i < mch.getMeshAttributeCount(); i++) {

        auto iEnc = encoders[iAttrIdx[i]];

        macp.getMeshAttributeEntropyPacketSize()[i] = iEnc->meshAttributeEntropyPacketBuffer.size();
        macp.getMeshAttributeEntropyPacketBuffer()[i] = iEnc->meshAttributeEntropyPacketBuffer;

        if (auto encFid = dynamic_cast<MaterialIDFaceAttributeEncoder*>(iEnc))
        {
            // we assume that the only other attribute is id for now
            auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
            // we assume we have a separte index - enabling to set start size to 0
            macp.getMeshAttributeStartCount()[i] = 0; // no start ini per face material id coding in tis implementation
            macp.getMeshAttributeFineResidualsCount()[i] = encFid->ioNotPredictedFaceId.size();

            {
                auto& maed = macp.getMeshAttributeExtraData()[i];
                if ((mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_MATERIAL_ID)
                    && (mch.getMeshAttributesEncodingParameters()[i].getMeshAttributePredictionMethod()
                        == (uint8_t)MeshAttributePredictionMethod_MATERIALID::MESH_MATERIALID_DEFAULT))
                {
                    auto& mmied = maed.getMeshMaterialIDExtraData();
                    mmied.getMeshMaterialIDRCount() = encFid->ioFaceIdIsRight.size();
                    mmied.getMeshMaterialIDLCount() = encFid->ioFaceIdIsLeft.size();
                    mmied.getMeshMaterialIDFCount() = encFid->ioFaceIdIsFacing.size();
                    mmied.getMeshMaterialIDDCount() = encFid->ioFidsIdIsDifferent.size();
                }
            }
        }
        else if (auto encUv = dynamic_cast<UVCoordVertexAttributeEncoder*>(iEnc))
        {
            //macp.get
            auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
            auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
            if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                macp.getMeshAttributeSeamsCount()[i] = encUv->ioSeams.size();
            }

            macp.getMeshAttributeStartCount()[i] = encUv->iosUVCoords.size();

            macp.getMeshAttributeStart()[i].resize(encUv->iosUVCoords.size());
            const auto numComponents = mch.getNumComponents(i);
            for (auto j = 0; j < encUv->iosUVCoords.size(); j++) {
                macp.getMeshAttributeStart()[i][j].resize(numComponents);
                for (uint32_t k = 0; k < numComponents; k++) {
                    macp.getMeshAttributeStart()[i][j][k] = encUv->iosUVCoords[j][k];
                }
            }

            macp.getMeshAttributeFineResidualsCount()[i] = encUv->ioUVCoords.size() - encUv->attrCoarseNum; 
            //macp.getMeshCodedAttributeFineResidualsSize()[i] = encUv->meshAttributeResidualBufferFine.size();
            //macp.getMeshAttributeFineResidualBuffer()[i] = encUv->meshAttributeResidualBufferFine;

            macp.getMeshAttributeCoarseResidualsCount()[i] = encUv->attrCoarseNum;
            //macp.getMeshCodedAttributeCoarseResidualsSize()[i] = encUv->meshAttributeResidualBufferCoarse.size();
            //macp.getMeshAttributeCoarseResidualBuffer()[i] = encUv->meshAttributeResidualBufferCoarse;

            if (mesh_attribute_separate_index_flag)
            {
                auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];
                if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT)
                {
                    madi.getMeshAttributeDeduplicateCount() = encUv->ioDuplicateSplitVertexIdx.size();
                    if (madi.getMeshAttributeDeduplicateCount() > 0) {
                        madi.getMeshAttributeDeduplicateIdx().resize(madi.getMeshAttributeDeduplicateCount());
                        for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                            madi.getMeshAttributeDeduplicateIdx()[j] = encUv->ioDuplicateSplitVertexIdx[j];
                        }

                        //madi.getMeshAttributeIsDuplicateSize() = encUv->meshAttributeIsDuplicateFlagBuffer.size();
                        //madi.getMeshAttributeIsDuplicateFlag() = encUv->meshAttributeIsDuplicateFlagBuffer;
                    }
                }
            }
            {
                auto& maed = macp.getMeshAttributeExtraData()[i];
                if ((mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_TEXCOORD)
                    && (mch.getMeshAttributesEncodingParameters()[i].getMeshAttributePredictionMethod()
                        == (uint8_t)MeshAttributePredictionMethod_TEXCOORD::MESH_TEXCOORD_STRETCH))
                {
                    auto& mtced = maed.getMeshTexCoordStretchExtraData();

                    mtced.getMeshTexCoordStretchOrientationsCount() = encUv->ioOrientations.size();
                    //mtced.getMeshCodedTexCoordStretchOrientationsSize() = encUv->meshTexCoordStretchOrientationBuffer.size();
                    //mtced.getMeshTexCoordStretchOrientation() = encUv->meshTexCoordStretchOrientationBuffer;
                }
            }
        }
        else if (auto encNor = dynamic_cast<NormalVertexAttributeEncoder*>(iEnc))
        {
            auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
            auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
            if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                macp.getMeshAttributeSeamsCount()[i] = encNor->ioSeams.size();
                if (macp.getMeshAttributeSeamsCount()[i]) {
                    //macp.getMeshCodedAttributeSeamsSize()[i] = encNor->meshAttributeSeamBuffer.size();
                    //macp.getMeshAttributeSeamBuffer()[i] = encNor->meshAttributeSeamBuffer;
                }
            }

            macp.getMeshAttributeStartCount()[i] = encNor->iosNormals.size();

            macp.getMeshAttributeStart()[i].resize(encNor->iosNormals.size());
            const auto numComponents = mch.getNumComponents(i);
            for (auto j = 0; j < encNor->iosNormals.size(); j++) {
                macp.getMeshAttributeStart()[i][j].resize(numComponents);
                for (uint32_t k = 0; k < numComponents; k++) {
                    macp.getMeshAttributeStart()[i][j][k] = encNor->iosNormals[j][k];
                }
            }

            int norm_residual_size = encNor->useOctahedral ? encNor->ioOctNormals.size() : encNor->ioNormals.size();

            macp.getMeshAttributeFineResidualsCount()[i] = norm_residual_size - encNor->attrCoarseNum;
            //macp.getMeshCodedAttributeFineResidualsSize()[i] = encNor->meshAttributeResidualBufferFine.size();
            //macp.getMeshAttributeFineResidualBuffer()[i] = encNor->meshAttributeResidualBufferFine;

            macp.getMeshAttributeCoarseResidualsCount()[i] = encNor->attrCoarseNum;
            //macp.getMeshCodedAttributeCoarseResidualsSize()[i] = encNor->meshAttributeResidualBufferCoarse.size();
            //macp.getMeshAttributeCoarseResidualBuffer()[i] = encNor->meshAttributeResidualBufferCoarse;

            if (mesh_attribute_separate_index_flag)
            {
                auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];
                if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT)
                {
                    madi.getMeshAttributeDeduplicateCount() = encNor->ioDuplicateSplitVertexIdx.size();
                    if (madi.getMeshAttributeDeduplicateCount() > 0) {
                        madi.getMeshAttributeDeduplicateIdx().resize(madi.getMeshAttributeDeduplicateCount());
                        for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                            madi.getMeshAttributeDeduplicateIdx()[j] = encNor->ioDuplicateSplitVertexIdx[j];
                        }

                        //madi.getMeshAttributeIsDuplicateSize() = encNor->meshAttributeIsDuplicateFlagBuffer.size();
                        //madi.getMeshAttributeIsDuplicateFlag() = encNor->meshAttributeIsDuplicateFlagBuffer;
                    }
                }
            }
            // Normal Extra Data
            if (encNor->useOctahedral)
            {
                auto& maed = macp.getMeshAttributeExtraData()[i];
                
                auto& mnoed = maed.getMeshNormalOctrahedralExtraData();

                mnoed.getMeshNormalOctrahedralBitDepthMinus1() = encNor->qpOcta - 1;
                mnoed.getMeshNormalOctahedralSecondResidualFlag() = cfg.normalEncodeSecondResidual;
                mnoed.getMeshNormalOctahedralSecondResidualCount() = cfg.normalEncodeSecondResidual ? encNor->ioNormals.size() : 0;
                //mnoed.getMeshNormalOctahedralSecondResidualSize() = encNor->meshNormalSecondResidualsBuffer.size();
                //mnoed.getMeshNormalOctahedralSecondResidual() = encNor->meshNormalSecondResidualsBuffer;
            }
        }
        else if (iEnc && iEnc->attrType == eb::Attribute::Type::GENERIC)
        {
            auto processGen = [&](auto&& encGen) {
                //macp.get
                auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
                auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
                if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                    macp.getMeshAttributeSeamsCount()[i] = encGen->ioSeams.size();
                    if (macp.getMeshAttributeSeamsCount()[i]) {
                        //macp.getMeshCodedAttributeSeamsSize()[i] = encGen->meshAttributeSeamBuffer.size();
                        //macp.getMeshAttributeSeamBuffer()[i] = encGen->meshAttributeSeamBuffer;
                    }
                }

                macp.getMeshAttributeStartCount()[i] = encGen->iosValues.size();

                macp.getMeshAttributeStart()[i].resize(encGen->iosValues.size());
                const auto numComponents = mch.getNumComponents(i);
                for (auto j = 0; j < encGen->iosValues.size(); j++) {
                    macp.getMeshAttributeStart()[i][j].resize(numComponents);
                    for (uint32_t k = 0; k < numComponents; k++) {
                        macp.getMeshAttributeStart()[i][j][k] = encGen->iosValues[j][k];
                    }
                }

                macp.getMeshAttributeFineResidualsCount()[i] = encGen->ioValues.size() - encGen->attrCoarseNum;
                //macp.getMeshCodedAttributeFineResidualsSize()[i] = encGen->meshAttributeResidualBufferFine.size();
                //macp.getMeshAttributeFineResidualBuffer()[i] = encGen->meshAttributeResidualBufferFine;

                macp.getMeshAttributeCoarseResidualsCount()[i] = encGen->attrCoarseNum;
                //macp.getMeshCodedAttributeCoarseResidualsSize()[i] = encGen->meshAttributeResidualBufferCoarse.size();
                //macp.getMeshAttributeCoarseResidualBuffer()[i] = encGen->meshAttributeResidualBufferCoarse;

                if (mesh_attribute_separate_index_flag)
                {
                    auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];
                    if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT)
                    {
                        madi.getMeshAttributeDeduplicateCount() = encGen->ioDuplicateSplitVertexIdx.size();
                        if (madi.getMeshAttributeDeduplicateCount() > 0) {
                            madi.getMeshAttributeDeduplicateIdx().resize(madi.getMeshAttributeDeduplicateCount());
                            for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                                madi.getMeshAttributeDeduplicateIdx()[j] = encGen->ioDuplicateSplitVertexIdx[j];
                            }

                            //madi.getMeshAttributeIsDuplicateSize() = encGen->meshAttributeIsDuplicateFlagBuffer.size();
                            //madi.getMeshAttributeIsDuplicateFlag() = encGen->meshAttributeIsDuplicateFlagBuffer;
                        }
                    }
                }
                // no extra data
                {
                    auto& maed = macp.getMeshAttributeExtraData()[i];
                }
            };

            switch (iEnc->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeEncoder<1>*>(iEnc)); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeEncoder<2>*>(iEnc)); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeEncoder<3>*>(iEnc)); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeEncoder<4>*>(iEnc)); break;
            };

        }
    }

    COUT << "  Syntax fill time (ms) = " << elapsed(t) << std::endl;

    t = now();

    //serialize to bitstream
    auto totalSize = bitstream.size();
    ebWriter.write(bitstream, meshCoding);
    totalSize = bitstream.size() - totalSize;

    //
    headerLengthInBytes = ebWriter.getMeshCodingHeaderLength();

    COUT << "  Serialize syntax to bitstream time (ms) = " << elapsed(t) << std::endl;

    COUT << "  Total payload bytes = " << totalSize << std::endl;

    return true;
}

void EBReversiEncoder::compareMeshDifference(const Model& input, Model& rec)
{
    // ! SHOULD BE REWRITEN FOR ARBIRARY ATTRIBUTE LIST
    // ! assuming either POSITION+TEXCOORD, or POSITION+NORMAL+TEXCOORD with POS and NORMAL with shared (combined) index 

    std::vector<float> emptyFloat;
    std::vector<int32_t> emptyInt;

    // assuming float type, current code could be extending to handle other types
    std::vector<float>* vertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* normals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* triangles = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesn = nullptr;  // triangle normal indices

    for (const auto& attrPtr : input.attributes)
    {
        // !! no domain check
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!vertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            vertices = attr->getData<float>();
            triangles = attr->getIndices<int32_t>();
        }
        // consider case where normals are not encoded using here qn>=0
        if (!normals && (qn>=0) && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            normals = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesn = attr->getIndices<int32_t>();
        }
    }

    if (!vertices) vertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!normals) normals = &emptyFloat;  // vertex normals (x,y,z)
    if (!triangles) triangles = &emptyInt;  // triangle position indices
    if (!trianglesn) trianglesn = &emptyInt;  // normal indices

    // assuming float type, current code could be extending to handle other types
    std::vector<float>* rvertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* rnormals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* rtriangles = nullptr;  // triangle position indices
    std::vector<int32_t>* rtrianglesn = nullptr;  // triangle normal indices

    for (const auto& attrPtr : rec.attributes)
    {
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!rvertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            rvertices = attr->getData<float>();
            rtriangles = attr->getIndices<int32_t>();
        }
        if (!rnormals && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            rnormals = attr->getData<float>();
            if (attr->getIndexCnt())
                rtrianglesn = attr->getIndices<int32_t>();
        }
    }

    if (!rvertices) rvertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!rnormals) rnormals = &emptyFloat;  // vertex normals (x,y,z)
    if (!rtriangles) rtriangles = &emptyInt;  // triangle position indices
    if (!rtrianglesn) rtrianglesn = &emptyInt;  // normal indices


    // here assuming normals sharing position index to reproduce m67554_m67553
    const bool hasNormalsSharingPositionIndex = !normals->empty()
        && (trianglesn->empty() || (*trianglesn) == (*triangles));

    std::map<std::array<int, 3>, std::vector<int>> inputPointMap;
    std::map<std::array<int, 3>, std::vector<int>> recPointMap;

    std::map<std::array<int, 6>, std::vector<int>> inputPointNrmMap;
    std::map<std::array<int, 6>, std::vector<int>> recPointNrmMap;

    if (!hasNormalsSharingPositionIndex)
    {
        for (int i = 0; i < input.getPositionCount(); i++) {
            std::array<int, 3> point = { (int)(*vertices)[3 * i + 0], (int)(*vertices)[3 * i + 1],(int)(*vertices)[3 * i + 2] };
            inputPointMap[point].push_back(i);
        }
        for (int i = 0; i < rec.getPositionCount(); i++) {
            std::array<int, 3> point = { (int)(*rvertices)[3 * i + 0], (int)(*rvertices)[3 * i + 1], (int)(*rvertices)[3 * i + 2] };
            recPointMap[point].push_back(i);
        }
    }
    else
    {
        for (int i = 0; i < input.getPositionCount(); i++) {
            std::array<int, 6> pointNrm = { (int)(*vertices)[3 * i + 0], (int)(*vertices)[3 * i + 1],(int)(*vertices)[3 * i + 2],
                                           (int)(*normals)[3 * i + 0], (int)(*normals)[3 * i + 1],(int)(*normals)[3 * i + 2] };
            inputPointNrmMap[pointNrm].push_back(i);
        }
        for (int i = 0; i < rec.getPositionCount(); i++) {
            std::array<int, 6> pointNrm = { (int)(*rvertices)[3 * i + 0], (int)(*rvertices)[3 * i + 1], (int)(*rvertices)[3 * i + 2],
                                           (int)(*rnormals)[3 * i + 0], (int)(*rnormals)[3 * i + 1], (int)(*rnormals)[3 * i + 2] };
            recPointNrmMap[pointNrm].push_back(i);
        }
    }

    for (int i = 0; i < faceOrder.size();) {
        if (faceOrder[i] < triangles->size() / 3)
            ++i;
        else
            faceOrder.erase(faceOrder.begin() + i);
    }
    std::vector<int> inputFaceToRecFace(triangles->size() / 3, -1);
    for (int i = 0; i < faceOrder.size(); i++) {
        inputFaceToRecFace[faceOrder[i]] = i;
    }
    std::vector<std::vector<int>> inputVertexToTriangles(input.getPositionCount(), std::vector<int>(0));
    std::vector<std::vector<int>> recVertexToTriangles(rec.getPositionCount(), std::vector<int>(0));
    for (int i = 0; i < triangles->size(); i++) {
        inputVertexToTriangles[(*triangles)[i]].push_back(i / 3);
        recVertexToTriangles[(*rtriangles)[i]].push_back(i / 3);
    }

    auto adjustConnectivity = [&](auto& inputMap, auto& recMap) {
        for (auto& tmpInputPoint : inputMap) {
            auto& tempRecPoints = recMap[tmpInputPoint.first];
            if (tempRecPoints.empty())
            {
                // not handled - occurs when a vertex from the input has no match in the reconstruction
                COUT << "ERROR: mismatch between input and decoded mesh" << std::endl;
                exit(0);
            }
            if (tmpInputPoint.second.size() > 1 || tempRecPoints.size() > 1) {
                //add points
                if (tmpInputPoint.second.size() > tempRecPoints.size()) {
                    int i = tempRecPoints.size();
                    int addPoint = tempRecPoints[0];
                    for (; i < tmpInputPoint.second.size(); i++) {
                        addPointList.push_back(addPoint);
                        (*rvertices).push_back(tmpInputPoint.first[0]);
                        (*rvertices).push_back(tmpInputPoint.first[1]);
                        (*rvertices).push_back(tmpInputPoint.first[2]);
                        tempRecPoints.push_back(rec.getPositionCount() - 1);
                        if (hasNormalsSharingPositionIndex) {
                            (*rnormals).push_back(tmpInputPoint.first[3]);
                            (*rnormals).push_back(tmpInputPoint.first[4]);
                            (*rnormals).push_back(tmpInputPoint.first[5]);
                        }
                    }
                }
                else if (tmpInputPoint.second.size() < tempRecPoints.size()) {
                    //delete points and update tIdx
                    int newPoint = tempRecPoints[0];
                    for (int i = tmpInputPoint.second.size(); i < tempRecPoints.size(); i++) {
                        int deletePoint = tempRecPoints[i];
                        deletePointList.push_back(deletePoint);
                        auto& triangleList = recVertexToTriangles[deletePoint];
                        for (int j = 0; j < triangleList.size(); j++) {
                            for (int k = 0; k < 3; k++) {
                                if ((*rtriangles)[3 * triangleList[j] + k] == deletePoint)
                                    (*rtriangles)[3 * triangleList[j] + k] = newPoint;
                            }
                        }
                    }
                }

                //find matched point
                std::vector<bool> recPointFlag(tempRecPoints.size(), false);
                std::vector<bool> recTrianlgesFlag((*rtriangles).size(), false);
                std::map<int32_t, int32_t> matchedRecPoints;
                for (auto point : tmpInputPoint.second) {
                    auto& triangleList = inputVertexToTriangles[point];
                    // firstly, find matched point
                    bool found = false;
                    matchedRecPoints[point] = -1;
                    int matchedRecPoint = -1;
                    for (auto& tid : triangleList) {
                        int recTid = inputFaceToRecFace[tid];
                        for (int i = 0; i < 3; i++) {
                            int tmpPoint = (*rtriangles)[3 * recTid + i];
                            bool checkNormal = true;
                            if (hasNormalsSharingPositionIndex) {
                                checkNormal = (*rnormals)[3 * tmpPoint + 0] == tmpInputPoint.first[3]
                                    && (*rnormals)[3 * tmpPoint + 1] == tmpInputPoint.first[4]
                                    && (*rnormals)[3 * tmpPoint + 2] == tmpInputPoint.first[5];
                            }
                            if (checkNormal
                                && (*rvertices)[3 * tmpPoint + 0] == tmpInputPoint.first[0]
                                && (*rvertices)[3 * tmpPoint + 1] == tmpInputPoint.first[1]
                                && (*rvertices)[3 * tmpPoint + 2] == tmpInputPoint.first[2]) {
                                for (int j = 0; j < tempRecPoints.size(); j++) {
                                    if (tempRecPoints[j] == tmpPoint && (!recPointFlag[j])) {
                                        found = true;
                                        matchedRecPoints[point] = tempRecPoints[j];
                                        recPointFlag[j] = true;
                                        break;
                                    }
                                }
                            }
                            if (found)
                                break;
                        }
                        if (found)
                            break;
                    }
                }
                std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> mismatchedPoints;
                std::map<std::pair<int32_t, int32_t>, bool>  mismatched;
                for (auto point : tmpInputPoint.second) {
                    //find matched point
                    if (matchedRecPoints[point] < 0) {
                        for (int i = tempRecPoints.size() - 1; i >= 0; i--) {
                            if (!recPointFlag[i] && find(deletePointList.begin(), deletePointList.end(), tempRecPoints[i]) == deletePointList.end()) {
                                matchedRecPoints[point] = tempRecPoints[i];
                                recPointFlag[i] = true;
                                break;
                            }
                        }
                    }
                    // judge whether need to adjust connectivity
                    auto& triangleList = inputVertexToTriangles[point];
                    for (auto& tid : triangleList) {
                        int recTid = inputFaceToRecFace[tid];
                        mismatched[{point, recTid}] = true;
                        for (int i = 0; i < 3; i++) {
                            int tmpPoint = (*rtriangles)[3 * recTid + i];
                            if (recTrianlgesFlag[3 * recTid + i])
                                continue;
                            bool checkNormal = true;
                            if (hasNormalsSharingPositionIndex) {
                                checkNormal = (*rnormals)[3 * tmpPoint + 0] == tmpInputPoint.first[3]
                                    && (*rnormals)[3 * tmpPoint + 1] == tmpInputPoint.first[4]
                                    && (*rnormals)[3 * tmpPoint + 2] == tmpInputPoint.first[5];
                            }
                            if (checkNormal
                                && (*rvertices)[3 * tmpPoint + 0] == tmpInputPoint.first[0]
                                && (*rvertices)[3 * tmpPoint + 1] == tmpInputPoint.first[1]
                                && (*rvertices)[3 * tmpPoint + 2] == tmpInputPoint.first[2]) {
                                if (tmpPoint != matchedRecPoints[point]) {
                                    mismatchedPoints[{point, recTid}].push_back(i);
                                    continue;
                                }
                                mismatched[{point, recTid}] = false;
                                recTrianlgesFlag[3 * recTid + i] = true;
                                break;
                            }
                        }

                    }
                }
                // adjust connectivity if needed
                for (auto point : tmpInputPoint.second) {
                    auto& triangleList = inputVertexToTriangles[point];
                    for (auto& tid : triangleList) {
                        int recTid = inputFaceToRecFace[tid];
                        if (mismatched[{point, recTid}]) {
                            if (mismatchedPoints.find({ point, recTid }) != mismatchedPoints.end()) {
                                for (auto recVertexT : mismatchedPoints[{point, recTid}]) {
                                    if (recTrianlgesFlag[3 * recTid + recVertexT])
                                        continue;
                                    (*rtriangles)[3 * recTid + recVertexT] = matchedRecPoints[point];
                                    modifyTriangles.push_back(recTid);
                                    modifyIndex.push_back(recVertexT);
                                    modifyPointID.push_back(matchedRecPoints[point]);
                                    recTrianlgesFlag[3 * recTid + recVertexT] = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    if (!hasNormalsSharingPositionIndex)
    {
        adjustConnectivity(inputPointMap, recPointMap);
    }
    else
    {
        adjustConnectivity(inputPointNrmMap, recPointNrmMap);
    }

    // code to be refactored to avoid this adjustement
    for (const auto& attrPtr : rec.attributes)
    {
        eb::MeshAttribute* attr = attrPtr.get();
        if (attr->getDomain() == eb::AttrDomain::PER_VERTEX)
        {
            attr->setDataCnt(attr->getData<float>()->size() / attr->getDim());
        }
    }

    COUT << "  MeshDifference - addPoints num: " << addPointList.size() << std::endl;
    COUT << "  MeshDifference - deletePoints num: " << deletePointList.size() << std::endl;
    COUT << "  MeshDifference - modify num: " << modifyTriangles.size() << std::endl;

    //delet points if needed
    if (deletePointList.size() > 0)
        deletePoints(rec, deletePointList);

}

//
void EBReversiEncoder::deletePoints(Model& mesh, std::vector<int> deletePoints)
{
    // assuming float type
    std::vector<float>* vertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* normals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* triangles = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesn = nullptr;  // triangle normal indices

    // assuming float type, current code could be extending to handle other types
    for (const auto& attrPtr : mesh.attributes)
    {
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!vertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            vertices = attr->getData<float>();
            triangles = attr->getIndices<int32_t>();
        }
        if (!normals && (qn >= 0) && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            normals = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesn = attr->getIndices<int32_t>();
        }
    }

    // here assuming normals sharing position index to reproduce m67554_m67553
    const bool hasNormalsSharingPositionIndex = normals && !normals->empty()
        && (!trianglesn || trianglesn->empty() || (*trianglesn) == (*triangles));

    std::sort(deletePoints.begin(), deletePoints.end());
    int dp = 0;
    std::vector<int> mapping(mesh.getPositionCount(), -1);
    int slow = 0;
    for (int i = 0; i < mesh.getPositionCount(); i++) {
        if (dp >= deletePoints.size())
            dp = 0;
        if (i != deletePoints[dp]) {
            mapping[i] = slow;
            (*vertices)[3 * slow + 0] = (*vertices)[3 * i + 0];
            (*vertices)[3 * slow + 1] = (*vertices)[3 * i + 1];
            (*vertices)[3 * slow + 2] = (*vertices)[3 * i + 2];

            if (hasNormalsSharingPositionIndex) {
                (*normals)[3 * slow + 0] = (*normals)[3 * i + 0];
                (*normals)[3 * slow + 1] = (*normals)[3 * i + 1];
                (*normals)[3 * slow + 2] = (*normals)[3 * i + 2];
            }

            slow++;
        }
        else {
            dp++;
        }
    }
    vertices->erase(vertices->begin() + 3 * slow, vertices->end());

    if (hasNormalsSharingPositionIndex)
        normals->erase(normals->begin() + 3 * slow, normals->end());
    for (int i = 0; i < triangles->size(); i++) {
        (*triangles)[i] = mapping[(*triangles)[i]];
        if (trianglesn && !trianglesn->empty() && hasNormalsSharingPositionIndex)
        {
            // usage of reference index should be systematic to avoid confusion with such cases
            (*trianglesn)[i] = (*triangles)[i];
        }

    }

    // code to be refactored to avoid this adjustement
    for (const auto& attrPtr : mesh.attributes)
    {
        eb::MeshAttribute* attr = attrPtr.get();
        if (attr->getDomain() == eb::AttrDomain::PER_VERTEX)
        {
            attr->setDataCnt(attr->getData<float>()->size() / attr->getDim());
        }
    }
}

/*
void EBReversiEncoder::EBBasicEncoderToDec()
{
    // used to avoid complete encoding / decoding before mesh difference evaluation
    // needs to be adapted to latest encoding / decoding process
    // temporarily using complete encoding / decoding 
}
*/
