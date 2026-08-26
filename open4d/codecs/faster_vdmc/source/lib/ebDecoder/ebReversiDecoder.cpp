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
#include <vector>
#include <stack>
#include <climits>

// internal headers
#include "ebIO.h"
#include "ebChrono.h"
#include "ebModel.h"
#include "ebGeometry.h"

#include "ebModelConverter.h"
#include "ebVertexTraversal.h"
#include "ebReversiDecoder.h"

#include "ebEntropy.h"
#include "ebEntropyContext.h"

using namespace eb;

bool EBReversiDecoder::decode(Model& output)
{
    COUT << "  Decoding using reverse Edgebreaker method" << std::endl;

    switch (cfg.traversal) {
    case eb::EBConfig::Traversal::EB:     COUT << "    Using connectivity traversal for predictions" << std::endl; break;
    case eb::EBConfig::Traversal::DEGREE: COUT << "    Using vertex degree traversal for predictions" << std::endl; break;
    }
    COUT << "    Using entropy packets = " << cfg.useEntropyPacket << std::endl;

    auto t = now();

    // initializing the set of decoders
    for (auto dec : decoders) {
        dec->initialize(cfg);
    }

    COUT << "  Memory init time (ms) " << elapsed(t) << std::endl;

    // Work on all the primary decoders one by one,
    // those will cascade calls if needed
    for (auto dec : primaryDecoders)
    {
        dec->decodeConnectivity(cfg);

        dec->decodeAttributes(cfg);

        if (cfg.deduplicate) {
            auto t1 = now();
            dec->indexDeduplicatePerFan(cfg);
            // invokes auxiliary index deduplication
            for (auto auxDec : dec->auxiliaryDecoders) {
                auxDec->indexDeduplicatePerFan(cfg);
            }
            COUT << "  Fan index deduplicate time (ms) = " << elapsed(t1) << std::endl;
        }
    }

    COUT << "  Decoding time (ms) = " << elapsed(t) << std::endl;

    if (cfg.reindexOutput == EBConfig::Reindex::DEGREE) {
        for (auto dec : primaryDecoders) {
            auto t1 = now();
            dec->reindexAttributes(cfg);
            COUT << "  Post-reindex time (ms) = " << elapsed(t1) << std::endl;
        }
    }

    t = now();
    ModelConverter::convertCTMeshToModel(_ctMesh, output, cfg.deduplicate, cfg.verbose);
    COUT << "  Convert to IFS (ms) = " << elapsed(t) << std::endl;

   t = now();
   if (cfg.reverseUnification) {
      ReverseUnification(output);
      adjustRecMesh(output);
   }
   COUT << "  Reverse unification and adjust reconstrutcted mesh time (ms) = " << elapsed(t) << std::endl;

   if (!cfg.intAttr) {
       auto t1 = now();
       dequantize(output);
       COUT << "  Dequantize time (ms) = " << elapsed(t1) << std::endl;
   }

   return true;
}

void PositionVertexAttributeDecoder::initialize(EBConfig& cfg)
{
    attr->ct.V.assign(3 * nT, -1);
    attr->ct.O.assign(3 * nT, -1);
    attr->values.assign(nV, glm::vec3(NAN));
    FC.assign(nB, 0);
    MV.assign(nV, 0);
}

void PositionVertexAttributeDecoder::decodeConnectivity(EBConfig& cfg) {

    // verify that we are primary
    // shall be allways the case for the time beeing
    // but we could imagine to be a secondary pos attr
    // in this case the else statement defaults to 
    // aux connectivity decoding
    if (attr->isPrimary()) {

        connectivityPrePass();

        connectivityMainPass();

        // invokes auxiliary topologies decoding
        for (auto auxDec : auxiliaryDecoders) {
            auxDec->decodeConnectivity(cfg);
        }
    }
    else {
        // this statement never occurs
        // since position are allways 
        // primary for the time beeing
        decodeAuxiliaryConnectivity(cfg, attr);
    }
}

void PositionVertexAttributeDecoder::connectivityPrePass(void)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;
    // Reconstruct handles, filter between real handles and 
    // boundaries that are also coded using handles 
    // init each pair of handle
    for (auto i = 0; i < iHandles.size() / 2; ++i) {
        const int b = iHandles[i * 2 + 0];
        const int c = iHandles[i * 2 + 1];
        if (b > 0) // handle
        {
            O[b] = c;
            O[c] = b;
        }
        else       // boundary
        {
            O[-b] = -c;
            O[c] = b;
        }
    }
    // parse the CLERS string in forward order and construct the enriched table of symbols
    clersIndex = 0;                          // index to consume clers table by invoking readClers()
    _T = nC = 0;                             // set current triangle and number of components to 0
    ccStartCorners.clear();
    symb.clear();
    symb.reserve(iClers.size());             // reserve memory for the extended symbol table (incl P)

    int i = 0;                               // number of parentheses

    if (nT > 0) {   // m73834 : do not add a triangle if MeshTriangleCount is 0
        if (nB == 0) {                           // if the next comp has no boundary, add P symbol
            ccStartCorners.push_back(ov.p(0));
            symb.push_back('P');
            _T++;
        }
        else
            ccStartCorners.push_back(0);
    }

    while (_T < nT) {                        // loop on all the triangles 
        // but the first one if no boundary on first CC
        const char s = readClers();          // reads the next symbol
        const int c = 3 * _T;
        symb.push_back(s);                   // store the next symbol
        _T++;
        if (s == 'S' && ov.l(c) == -1) {     // on an S symbol not related to a handle
            i++;                             // increase the number of parentheses
        }
        if (s == 'E') {                      // on an E symbol
            if (i > 0) {                     // if the S are not all matched
                i--;
            }
            else {
                nC++;                        // else we increase the number of components 
                if (_T < nT) {                // record the id of the start corner
                    if (nC >= nB)            // on a P symbol 
                        ccStartCorners.push_back(ov.p(_T * 3));
                    else                     // not on a P symbol
                        ccStartCorners.push_back(_T * 3);
                }
                if (nC < nB && nC >= 0) {   // strict comp so we skip the last one 
                    // (first one is 0, already in table at init)
                    FC[nC] = _T;            // stores the end of previous component 
                }
                if (nC >= nB && _T < nT) {// if the next comp has no boundary, add P symbol if not last CC
                    symb.push_back('P');
                    _T++;
                }
            }
        }
    }
}

void PositionVertexAttributeDecoder::connectivityMainPass(void)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    // order of corners processing (for visu)
    processedCorners.clear();
    processedCorners.reserve(V.size());
    // order of corners writing (used by attribute pass)
    // a corner might not have reads but we reserve max possible memory
    readCorners.clear();
    readCorners.reserve(V.size());

    //
    _T = symb.size() - 1;
    _N = nV - 1;

    //
    std::stack<int> stack; // stack of corners

    while (nC > 0) {                                                   // reads the CLERS string backwards
        int c = -1;                                                    // initiates first corner
        while (_T >= 0 && (nC > nB || (nC >= 0 && _T >= FC[nC - 1])))   // filter components with boundary
        {
            bool isP = false;
            switch (symb[_T]) {                    // test the next symbol
            case 'C':
                match(c, 3 * _T + 1);              // matches the right gate
                closeStar(3 * _T + 2, _N);
                readCorners.push_back(ov.n(3 * _T + 2));
                _N--;
                break;
            case 'L':
                match(c, 3 * _T + 1);
                break;
            case 'R':
                match(c, 3 * _T + 2);
                break;
            case 'S':
                match(c, 3 * _T + 1);              // matches the right gate
                c = 3 * _T + 2;
                if (O[c] == -1) {                  // if it is not a handle
                    match(c, stack.top());          // match the left gate
                    stack.pop();
                }
                else if (O[c] < 0) {               // if it is an interior boundary
                    match(c, -O[c]);
                    c = ov.p(c);                   // JEM FIX: add to the paper 
                    while (ov.r(c) >= 0) {         // goes backward on the boundary 
                        c = ov.r(c);
                    }
                    readBoundary(ov.p(c), _N);     // decodes the interior boundary
                }
                break;
            case 'E':
                if (c > 0) {
                    stack.push(c);
                }
                break;
            case 'P':
                isP = true; // processed corner is adjusted on 'P' symbols
                match(c, 3 * _T);                    // matches the gate
                closeStar(3 * _T + 1, _N - 2);       // close star for next corner
                closeStar(3 * _T + 2, _N - 1);       // close star for previous corner
                closeStar(3 * _T + 0, _N);           // close the star of the corner
                readCorners.push_back(3 * _T + 1);
                readCorners.push_back(3 * _T + 0);
                readCorners.push_back(3 * _T + 2);
                _N -= 3; nC--; c = -1;               // update for new component, 
                break;
            }
            c = 3 * _T;
            if (isP)
                processedCorners.push_back(ov.p(c));
            else
                processedCorners.push_back(c);

            _T--;
        }
        if (nB != 0) {
            nC--;
            readBoundary(3 * FC[nC] + 1, _N);        // decodes exterior boundary
        }
    }
}

void PositionVertexAttributeDecoder::readBoundary(int b, int& N)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    auto& O = ov.O;

    while (ov.l(b) >= 0) {
        b = ov.l(b);                // go to the left toward the boundary
    }
    do {
        readCorners.push_back(b);
        MV[N] = 1;                  // mark vertex N as visited
        ov.setV(b, N);
        b = ov.p(b);
        ov.setO(b, -3); // marks the corner (set to -3 to discriminate from seams)
        while (ov.l(b) >= 0) {      // updates all incident corners to the vertex
            b = ov.l(b);
            ov.setV(ov.n(b), N);
        }
        N--;                        // JEM: added decrement N here  (not in paper)
    } while (ov.v(b) < 0);          // until closing the boundary

}

void PositionVertexAttributeDecoder::closeStar(int c, int N)
{
    auto& ov = attr->ct;
    auto& V = ov.V;
    MV[N] = 1;                              // mark vertex N as visited
    int b = c;
    while (ov.l(b) >= 0 && ov.l(b) != c) {  // assign incident corners to the new vertex
        ov.setV(ov.n(b), N);
        b = ov.l(b);
    }
    ov.setV(ov.n(b), N);
    match(ov.p(b), c);                      // closes the star
}

// decoding of auxiliary connectivity
template<class ValueType>
void VertexAttributeDecoder::decodeAuxiliaryConnectivity
(
    EBConfig& cfg,
    AttributeVertex<ValueType>* attr
)
{
    assert(attr->hasOwnIndices);

    const auto& ov = mainDec->attr->ct;
    auto& O = ov.O;
    auto& MC = mainDec->MC;
    auto& OTC = attr->ct.O;
    auto& TC = attr->ct.V;

    const auto altTraversal = (cfg.traversal != EBConfig::Traversal::EB);

    if (altTraversal) {
        // we will add UV seams cuts on top of this
        // so the alt traversal can use the corner table associated to UVs
        attr->ct.O = O;
        ccStartCorners = mainDec->ccStartCorners;  // extended set of start corners
    }

    // reset the corner marking table
    MC.assign(mainDec->nT * 3, 0);

    // use OTC to describe seams
    // at the end of the process attr->ct.O contains -1 (no alt traversal case) 
    // or positive values (alt traversal case) where there is no border or no seam
    // and -3 for border and -2 for seam
    // I.9.5 - auxilliary connectivity decoding first loop : seams regeneration
    for (int i = mainDec->processedCorners.size() - 1; i >= 0; --i) {
        const auto& corner = mainDec->processedCorners[i];
        const int   corners[3] = { corner, ov.n(corner), ov.p(corner) };
        const int   src_face_id = ov.t(corner);
        for (int c = 0; c < 3; ++c) {
            const int cur_corner = corners[c];
            const int opp_corner = O[cur_corner];
            // no explicit seams on boundary edges 
            // or skip since already marked 
            if ((opp_corner < 0) || MC[opp_corner] > 0)
                continue;
            const int opp_face_id = ov.t(opp_corner);
            // decode only one side
            if (opp_face_id < src_face_id)
                continue;
            MC[cur_corner] = 1;
            // use pre evaluated seam code
            const auto isSeam = iSeams[seamsIndex++];
            if (isSeam) {
                attr->ct.O[cur_corner] = -2;
                attr->ct.O[opp_corner] = -2;
                // we store seam corners as a potential start corners
                if (altTraversal) {
                    ccStartCorners.push_back(cur_corner);
                    ccStartCorners.push_back(opp_corner);
                }
            }
            else if (!altTraversal)
            {
                attr->ct.O[cur_corner] = -1;
                attr->ct.O[opp_corner] = -1;
            }
        }
    }

    // regenerate UV indices (TC) using seams information from OTC
    int NTC = 0;
    // I.9.5 - auxilliary connectivity decoding second loop : index regeneration
    for (int i = mainDec->processedCorners.size() - 1; i >= 0; --i) {
        const auto& corner = mainDec->processedCorners[i];
        const int   corners[3] = { corner, ov.n(corner), ov.p(corner) };
        for (int c = 0; c < 3; ++c) {
            const int cur_corner = corners[c];
            if (TC[cur_corner] >= 0)  // index already allocated
                continue;
            const auto newTC = NTC++;  // inc after assign
            TC[cur_corner] = newTC;
            // swing to right most extent (geo border -3 or uv seam -2)
            auto movC = cur_corner;
            while (O[ov.n(movC)] >= 0 && OTC[ov.n(movC)] >= -1) {
                movC = ov.n(O[ov.n(movC)]);
                TC[movC] = newTC;
                if (movC == cur_corner) break;
            }
            // then the other side (geo border or uv seam)
            movC = cur_corner;
            while (O[ov.p(movC)] >= 0 && OTC[ov.p(movC)] >= -1) {
                movC = ov.p(O[ov.p(movC)]);
                TC[movC] = newTC;
                if (movC == cur_corner) break;
            }
        }
    }
}

void PositionVertexAttributeDecoder::reindexAttributes(EBConfig& cfg)
{
    // if notprimary encoder we scip.
    if (attr->isPrimary()) {

        if (cfg.traversal != EBConfig::Traversal::DEGREE)
        {
            predVertexTraverser.traverse(&attr->ct, attr->getPositionCount(), attr->getTriangleCount(), ccStartCorners);
        }
        else {
            // no need to recompute traversal of predVertexTraverser
            // already done by decodeAttributes()
        }

        const auto posSize = attr->values.size();
        std::vector<int> newIdx(posSize);
        std::vector<glm::vec3> newPos(posSize);

        // reindex the position attributes
        for (auto i = 0; i < predVertexTraverser.visitedCorners.size(); ++i) {
            const auto vIdx = attr->ct.v(predVertexTraverser.visitedCorners[i]);
            newIdx[vIdx] = i;
            newPos[i] = attr->values[attr->ct.v(predVertexTraverser.visitedCorners[i])];
        }
        attr->values.swap(newPos);

        // we need to swap aux attributes also if using primary index
        for (auto auxDec : auxiliaryDecoders) {
            auxDec->reindexAttributes(cfg);
        }

        // finalize corner table update
        for (auto c = 0; c < attr->ct.V.size(); ++c) {
            attr->ct.V[c] = newIdx[attr->ct.V[c]];
        }
    }
    else {
        // this statement never occurs
        // since position are allways 
        // primary for the time beeing
        reindexAuxiliaryAttributes(cfg, mainDec, attr);
    }
}

// decoding of auxiliary connectivity
template<class ValueType>
void VertexAttributeDecoder::reindexAuxiliaryAttributes(
    EBConfig& cfg,
    PositionVertexAttributeDecoder* mainDec,
    AttributeVertex<ValueType>* auxAttr)
{
    if (!auxAttr->hasOwnIndices && auxAttr->values.size() != 0) {

        std::vector<ValueType> newValues(mainDec->attr->values.size());

        for (auto i = 0; i < mainDec->predVertexTraverser.visitedCorners.size(); ++i) {
            const auto vIdx = mainDec->attr->ct.v(mainDec->predVertexTraverser.visitedCorners[i]);
            newValues[i] = auxAttr->values[vIdx];
        }
        auxAttr->values.swap(newValues);
    }
}

void PositionVertexAttributeDecoder::decodeAttributes(EBConfig& cfg)
{
    // verify that we are primary
    // shall be allways the case for the time being
    // but we could imagine to be a secondary pos attr
    // in this case the else statement defaults to 
    // aux connectivity decoding
    if (attr->isPrimary()) {
        // I.9.6.1 invoking I.9.6.2 with reversed order when traversal is EB
        if (cfg.traversal == EBConfig::Traversal::DEGREE) {
            // computes a corner order using prediction degree traversal
            predVertexTraverser.traverse(&attr->ct, attr->getPositionCount(), attr->getTriangleCount(), ccStartCorners);
            // invoke I.9.6.2 with non reversed order when traversal is DEGREE
            decodePrimaryAttribute(cfg, predVertexTraverser.visitedCorners, false);
        }
        else { // otherwise uses the traversal order from connectivity encoding
            // invoke I.9.6.2 with reversed order when traversal is EB
            decodePrimaryAttribute(cfg, readCorners, true);
        }

        // invokes auxiliary attributes decoding
        for (auto auxDec : auxiliaryDecoders) {
            auxDec->decodeAttributes(cfg);
        }
    }
    // in a role of an auxiliary decoder
    else {
        // not available yet
        // we shall never reach this statement
        // decodeAuxiliaryPositionAttributes(cfg);
    }
}

void PositionVertexAttributeDecoder::decodePrimaryAttribute(
    EBConfig& cfg,
    std::vector<int>& traversalOrder,
    bool reverse)
{
    const auto& ov = attr->ct;
    const auto& V = ov.V;
    auto& G = attr->values;

    // prepare the skip table if deduplication is active
    if (cfg.deduplicate) {
        skippedCorners.assign(ov.V.size(), -1);
    }

    // reset the vertex marking table
    for (auto& v : MV) v = -1;

    // current duplicate index
    int currentDuplicate = 0;
    int currentVertex = 0;

    // I.9.6.2 reverse is set depending on mesh_vertex_traversal_method
    // goes through the corners, for the positions
    // go through traversal in forward or reverse order
    const int start = reverse ? traversalOrder.size() - 1 : 0;
    int inc = reverse ? -1 : 1;
    for (int i = start; i >= 0 && i < traversalOrder.size(); i += inc) {
        const auto& c = traversalOrder[i];

        bool predictPosition = true;
        // perform deduplication if needed
        if (cfg.deduplicate && isVertexDup.size() && isVertexDup[currentVertex++])
        {
            auto splitIdx = iDuplicateSplitVertexIdx[currentDuplicate++];
            auto dupVtx = processedDupIdxArray[splitIdx];
            if (dupVtx >=0)
            {
                duplicates.push_back(std::make_pair(c, dupVtx));
                G[V[c]] = G[dupVtx];
                predictPosition = false;
                // used by attributes using main index table
                skippedCorners[c] = dupVtx;
            }
            else
            {
                processedDupIdxArray[splitIdx] = V[c];
            }
        }
        // perform prediciton if needed
        if (predictPosition) {
            posDecodeWithPrediction(c);
        }
    }

}


template<class ValueType>
void VertexAttributeDecoder::applyIndexDeduplicatePerFan(
    EBConfig& cfg,
    AttributeVertex<ValueType>* auxAttr)
{
    const auto& ov = auxAttr->ct;
    auto& V = auxAttr->ct.V;
    auto& O = auxAttr->ct.O;

    for (auto& dup : duplicates)
    {
        auto curCorner = dup.first;
        auto splitVtxToVertex = dup.second;

        V[curCorner] = splitVtxToVertex;
        auto next_c = curCorner;
        while (next_c = ov.n(O[ov.n(next_c)]),
            next_c != curCorner && next_c >= 0) {
            V[next_c] = splitVtxToVertex;
        }
        if (next_c < 0)
        {
            next_c = curCorner;
            while (next_c = ov.p(O[ov.p(next_c)]),
                next_c != curCorner && next_c >= 0) {
                V[next_c] = splitVtxToVertex;
            }
        }
    }
}

template<class AuxDecoder>
void VertexAttributeDecoder::decodeAuxiliaryAttributes(
    EBConfig& cfg,
    PositionVertexAttributeDecoder* mainDec,
    AuxDecoder* auxDec)
{
    auto& posAttr = mainDec->attr;
    const auto& ov = posAttr->ct;
    auto& attr = auxDec->attr;

    // reset the vertex marking table
    for (auto& v : mainDec->MV) v = -1;

    if (!attr->hasOwnIndices) {
        // traversal orders to be rechecked in all conditions
        if (cfg.traversal == EBConfig::Traversal::DEGREE) {
            // reuse the main traverser already initialized
            // I.9.7 MESH_TRAVERSAL_DEGREE when hasOwnIndices is FALSE
            auto& traverser = mainDec->predVertexTraverser;
            for (auto c : traverser.visitedCorners)
            {
                const auto skipIdx = mainDec->skippedCorners.size() != 0 ? mainDec->skippedCorners[c] : -1;
                if (skipIdx < 0) {
                    auxDec->decodeWithPrediction(c, posAttr->ct.V);
                }
                else {
                    attr->values[posAttr->ct.V[c]] = attr->values[skipIdx];
                }
            }
        }
        else {

            // goes through the reversed corners, for the auxilliary attribute, using main index table V
            // I.9.7 MESH_TRAVERSAL_EB when hasOwnIndices is FALSE
            for (int i = mainDec->readCorners.size() - 1; i >= 0; --i) {
                const auto& c = mainDec->readCorners[i];
                const auto skipIdx = mainDec->skippedCorners.size() != 0 ? mainDec->skippedCorners[c] : -1;
                if (skipIdx < 0) {
                    auxDec->decodeWithPrediction(c, posAttr->ct.V);
                }
                else {
                    attr->values[posAttr->ct.V[c]] = attr->values[skipIdx];
                }
            }
        }

        // early return, we are done
        return;
    }

    // Otherwise we are in the case we have our own index table
    auto& G = attr->values;
    const auto& V = attr->ct.V;
    // current duplicate index
    int currentDuplicate = 0;
    int currentVertex = 0;

    auto& MC = mainDec->MC;
    //auto& O = mainDec->attr->auxO; // ??!
    auto& O = mainDec->attr->ct.O;
    auto& OTC = attr->ct.O;


    if (cfg.traversal == EBConfig::Traversal::DEGREE)
    {
        auto& traverser = mainDec->auxIndexTraverser;
        traverser.traverse(&attr->ct, attr->values.size(), ov.getTriangleCount(), ccStartCorners);
        // I.9.7 MESH_TRAVERSAL_DEGREE when hasOwnIndices is TRUE
        for (auto cur_corner : traverser.visitedCorners)
        {
            bool predictAttribute = true;
            // perform deduplication if needed
            if (cfg.deduplicate && isVertexDup.size() && isVertexDup[currentVertex++])
            {
                auto splitIdx = iDuplicateSplitVertexIdx[currentDuplicate++];
                auto dupVtx = processedDupIdxArray[splitIdx];
                if (dupVtx >= 0)
                {
                    duplicates.push_back(std::make_pair(cur_corner, dupVtx));
                    G[V[cur_corner]] = G[dupVtx];
                    predictAttribute = false;
                    // used by attributes using main index table
                    //skippedCorners[c] = dupVtx; // used for dependent ?
                }
                else
                {
                    processedDupIdxArray[splitIdx] = V[cur_corner];
                }
            }
            // perform prediciton if needed
            if (predictAttribute) {
                auxDec->decodeWithPrediction(cur_corner, attr->ct.V);
            }            
        }

        //regenerate OTC
        if (cfg.deduplicate && isVertexDup.size()) {
            MC.assign(mainDec->nT * 3, 0);
            for (auto corner : traverser.visitedCorners) {
                const int corners[3] = { corner, ov.n(corner), ov.p(corner) };
                for (int c = 0; c < 3; c++) {
                    const int cur_corner = corners[c];

                    if (MC[cur_corner])
                        continue;

                    MC[cur_corner] = true;

                    const auto oppc = O[cur_corner];
                    if (oppc >= 0) {
                        if (OTC[cur_corner] != -2) {
                            OTC[cur_corner] = oppc;
                            OTC[oppc] = cur_corner;
                        }
                        MC[oppc] = true;
                    }
                }
            }
        }
    }
    else { // otherwise use traversal order from connectivity
        // I.9.7 MESH_TRAVERSAL_EB when hasOwnIndices is TRUE
        for (int i = mainDec->processedCorners.size() - 1; i >= 0; --i){
            const auto& corner = mainDec->processedCorners[i];
            const int corners[3] = { corner, ov.n(corner), ov.p(corner) };
            for (int c = 0; c < 3; ++c) {
                const int cur_corner = corners[c];
                if (mainDec->MV[attr->ct.V[cur_corner]] > 0) continue;
                bool predictAttribute = true;
                // perform deduplication if needed
                if (cfg.deduplicate && isVertexDup.size() && isVertexDup[currentVertex++])
                {
                    auto splitIdx = iDuplicateSplitVertexIdx[currentDuplicate++];
                    auto dupVtx = processedDupIdxArray[splitIdx];
                    if (dupVtx >= 0)
                    {
                        duplicates.push_back(std::make_pair(cur_corner, dupVtx));
                        G[V[cur_corner]] = G[dupVtx];
                        predictAttribute = false;
                        // used by attributes using main index table
                        //skippedCorners[c] = dupVtx; // used for dependent ?
                    }
                    else
                    {
                        processedDupIdxArray[splitIdx] = V[cur_corner];
                    }
                }
                // perform prediciton if needed
                if (predictAttribute) {
                    auxDec->decodeWithPrediction(cur_corner, attr->ct.V);
                }
            }
        }

        //regenerate OTC
        if (cfg.deduplicate && isVertexDup.size()) {
            MC.assign(mainDec->nT * 3, 0);
            for (auto corner : mainDec->processedCorners) {
                const int corners[3] = { corner, ov.n(corner), ov.p(corner) };
                for (int c = 0; c < 3; c++) {
                    const int cur_corner = corners[c];

                    if (MC[cur_corner])
                        continue;

                    MC[cur_corner] = true;

                    const auto oppc = O[cur_corner];
                    if (oppc >= 0) {
                        if (OTC[cur_corner] != -2) {
                            OTC[cur_corner] = oppc;
                            OTC[oppc] = cur_corner;
                        }
                        MC[oppc] = true;
                    }
                }
            }
        }
    }
}

void PositionVertexAttributeDecoder::posDecodeWithPrediction(int c)
{
    const auto MAX_PARALLELOGRAMS = 4;
    const auto MAX_TRAPEZOIDS = 4;
    const auto& ov = attr->ct;
    const auto& V = ov.V;
    const auto& O = ov.O;

    auto& G = attr->values;
    const auto& v = ov.v(c);

    // is vertex already predicted ?
    if (MV[v] > 0)
        return;

    // search for some parallelogram estimations and some trapezoid estimations around the vertex of the corner
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
    // swing around the fan until we find a border
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c)
    {
        altC = nextC;
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    int startC = altC;
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

      // incomplete fan or full rotation or max predictions reached
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
            glm::ivec3 delta = readDeltaFine();
            glm::ivec3 pred(0, 0, 0);
            for (int i = 0; i < count; ++i) {
                pred += scale[count - 1][i] * predPos[i];
            }
            pred = (pred + glm::ivec3(GEO_SCALE / 2)) >> GEO_SHIFT;
            G[v] = delta + pred;
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
        glm::ivec3 delta = readDeltaFine();
        glm::ivec3 pred(0, 0, 0);
        for (int i = 0; i < countTra; ++i) {
            pred += scale[countTra - 1][i] * predPosTra[i];
        }
        const int frac_bits = GEO_SHIFT + 2;
        const int rounding_offset = 1 << (frac_bits - 1);
        pred = (pred + glm::ivec3(rounding_offset)) >> frac_bits;
        G[v] = delta + pred;
        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_v = ov.v(ov.p(c));
    const auto& c_n_v = ov.v(ov.n(c));

    if (c_p_v > -1 && MV[c_p_v] > -1) {
        G[v] = readDeltaCoarse() + G[c_p_v];
        return;
    }

    if (c_n_v > -1 && MV[c_n_v] > -1) {
        G[v] = readDeltaCoarse() + G[c_n_v];
        return;
    }

    // 3. or maybe we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_v = ov.v(b);
        auto marked = MV[b_v];
        if (marked > -1) {
            G[v] = readDeltaCoarse() + G[b_v];
            return;
        }
    }

    // 4. no more choices
    G[v] = readStart();

    return;

}

void UVCoordVertexAttributeDecoder::decodeWithPrediction(
    int c,
    const std::vector<int>& tcIndices
) {

    const auto MAX_STRETCH_PREDS = 8;
    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& OTC = attr->ct.O;
    auto& UV = attr->values;
    const auto& TC = tcIndices;
    const auto& tc = TC[c];
    auto& MV = mainDec->MV;

    // is vertex already predicted ?
    if (MV[tc] > 0)
        return;
    // we mark the vertex
    MV[tc] = 1;

    // search for valid min stretch estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be deifned due to boundaries
    // we use OV accessors and test to filter negative values
    int  altC = c;

    // loop through corners attached to the current vertex
    // swing around the fan until we find a border
    bool onSeam = (OTC.size() != 0 ? (OTC[ov.n(altC)] == -2) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OTC.size() != 0 ? (OTC[ov.n(altC)] == -2) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    int startC = altC;
    int  count = 0;             // number of valid strtech found
    glm::i64vec2 predUV(0, 0);  // the predicted uv
    do
    {
        const auto altV = TC[altC];
        const auto prevV = TC[ov.p(altC)];
        const auto nextV = TC[ov.n(altC)];
        if ((altV > -1 && prevV > -1 && nextV > -1) &&
            ((MV[altV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
        {
            predictUV(altC, TC, predUV);
            ++count;
        }
        onSeam = (OTC.size() != 0 ? (OTC[ov.p(altC)] == -2) : false);
        altC = ov.p(O[ov.p(altC)]);                         // swing around the triangle fan
       
      // incomplete fan or full rotation, or max predictions reached
    } while (altC >= 0 && altC != startC && !onSeam && count < MAX_STRETCH_PREDS);

    // 1. use min stretch prediction when possible
    if (count > 0) {
        glm::i64vec2 roundig;
        const int64_t fracBits = (int64_t)UVPRED_FRACBITS;
        roundig.x = (predUV.x >= 0) ? (int64_t)(1 << (fracBits - 1)): -1 * (int64_t)(1 << (fracBits - 1));
        roundig.y = (predUV.y >= 0) ? (int64_t)(1 << (fracBits - 1)): -1 * (int64_t)(1 << (fracBits - 1));
        predUV = (predUV / glm::i64vec2(count) + roundig) >> fracBits;
        UV[tc] = (glm::vec2)predUV + readUvDeltaFine();

        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_tc = TC[ov.p(c)];
    const auto& c_n_tc = TC[ov.n(c)];

    if (c_p_tc > -1 && MV[c_p_tc] > -1) {
        UV[tc] = readUvDeltaCoarse() + UV[c_p_tc];
        return;
    }

    if (c_n_tc > -1 && MV[c_n_tc] > -1) {
        UV[tc] = readUvDeltaCoarse() + UV[c_n_tc];
        return;
    }

    // 3. or maybe we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_tc = TC[b];
        if (MV[b_tc] > -1) {
            UV[tc] = readUvDeltaCoarse() + UV[b_tc];
            return;
        }
    }

    // 4. no more choices, it is a start
    UV[tc] = readUvStart();

    return;

}

//
void UVCoordVertexAttributeDecoder::predictUV(
    const int c, const std::vector<int>& tcIndices,
    glm::i64vec2& predUV
) {
    auto& ov = mainDec->attr->ct;
    const auto& G = mainDec->attr->values;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& OTC = attr->ct.O;
    auto& UV = attr->values;
    const auto& TC = tcIndices;
    const auto& tc = TC[c];
    auto& MV = mainDec->MV;

    // accumulate uv predictions 
    const glm::i64vec2 uvPrev = (glm::i64vec2)UV[TC[ov.p(c)]];
    const glm::i64vec2 uvNext = (glm::i64vec2)UV[TC[ov.n(c)]];
    const glm::i64vec3 gPrev = (glm::i64vec3)G[V[ov.p(c)]];
    const glm::i64vec3 gNext = (glm::i64vec3)G[V[ov.n(c)]];
    const glm::i64vec3 gCurr = (glm::i64vec3)G[V[c]];

    const glm::i64vec3 gNgP = (glm::i64vec3)(gPrev - gNext);
    const glm::i64vec3 gNgC = (glm::i64vec3)(gCurr - gNext);
    const glm::i64vec2 uvNuvP = (glm::i64vec2)(uvPrev - uvNext);
    const int64_t gNgP_dot_gNgC = gNgP.x * gNgC.x + gNgP.y * gNgC.y + gNgP.z * gNgC.z;
    const int64_t d2_gNgP = gNgP.x * gNgP.x + gNgP.y * gNgP.y + gNgP.z * gNgP.z;
    const int64_t fracBits = (int64_t)UVPRED_FRACBITS;
    if (d2_gNgP > 0)
    {
        const glm::i64vec2 uvProj = (uvNext << fracBits) + uvNuvP * ((gNgP_dot_gNgC << fracBits) / d2_gNgP);
        const glm::i64vec3 gProj = (gNext << fracBits) + gNgP * ((gNgP_dot_gNgC << fracBits) / d2_gNgP);
        const glm::i64vec3 diff_gProj_gCurr = (gCurr << fracBits) - gProj;
        const int64_t d2_gProj_gCurr = diff_gProj_gCurr.x * diff_gProj_gCurr.x + diff_gProj_gCurr.y * diff_gProj_gCurr.y + diff_gProj_gCurr.z * diff_gProj_gCurr.z;
        const glm::i64vec2 uvProjuvCurr = glm::i64vec2(uvNuvP.y, -uvNuvP.x) * (int64_t)(isqrt((d2_gProj_gCurr / d2_gNgP) << 2) >> 1);
        const glm::i64vec2 predUV0(uvProj + uvProjuvCurr);
        const glm::i64vec2 predUV1(uvProj - uvProjuvCurr);

        // the first estimation for this UV corner
        bool useOpp = false;
        // we cannot use the opposite if beyond a seam
        const bool onSeam = (OTC.size() != 0 ? (OTC[c] == -2) : false);
        // we cannot use the opposite if beyond a seam
        const bool checkOpposite = (!onSeam && O[c] >= 0 && TC[O[c]] >= 0 && MV[TC[O[c]]] > 0);

        if (checkOpposite) {
            // check that O not aligned with N and P (possible to disciminate sides
            const glm::i64vec2 uvOpp = UV[TC[O[c]]];
            // this test should be using 64b integers - this is ( vecNP ^ vec NO )
            const glm::i64vec2 NP = (uvPrev - uvNext);
            glm::i64vec2 NO(uvOpp - uvNext);
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
            const glm::i64vec2 predUVd = readOrientation() ? predUV0 : predUV1;
            predUV += predUVd;
        }
    }
    // else average the two predictions
    else
    {
        predUV += (((glm::i64vec2)UV[TC[ov.n(c)]] << fracBits) + ((glm::i64vec2)UV[TC[ov.p(c)]] << fracBits)) >> (int64_t)1;
    }
}

template <int dim>
void GenericVertexAttributeDecoder<dim>::decodeWithPrediction(
    int c,
    const std::vector<int>& attrIndices
) {
    const auto MAX_PARALLELOGRAMS = 4;

    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;
    const auto& V = ov.V;
    const auto& OAI = attr->ct.O;
    auto& AV = attr->values;
    const auto& AI = attrIndices;
    const auto& ai = AI[c];
    auto& MV = mainDec->MV;

    // is vertex already predicted ?
    if (MV[ai] > 0)
        return;
    // we mark the vertex
    MV[ai] = 1;

    // search for valid min stretch estimations around the vertex of the corner
    // the triangle fan might not be complete since we do not use dummy points,
    // but we know that a vertex is not non-manifold, so we have only one fan per vertex
    // also some opposite might not be deifned due to boundaries
    // we use OV accessors and test to filter negative values
    int  altC = c;

    // loop through corners attached to the current vertex
    // swing around the fan until we find a border
    bool onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -2) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -2) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

#if ENABLE_GENERIC_FXP
    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    int startC = altC;
    int  count = 0;                      // number of valid mpara found
    i64vecN predGen[MAX_PARALLELOGRAMS]; // predicted generic values
    // check if init required for portability? = { i64vecN(0) };

    // avoid this test in the loop using templates ?
    if (attr->predMethod == (int8_t)EBConfig::GenPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;
            // O is in geometric domain ... should we stick to attribute connectivity domain only
            const bool seamInPara = (OAI.size() && (OAI[altC] == -2));
            const auto oppoV = seamInPara ? -1 : ( (O[altC] >= 0) ? AI[O[altC]] : -1 );
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictGenPara(altC, AI, predGen[count++]);
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -2) : false);
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
        i64vecN delta = readGenDeltaFine();
        i64vecN pred(0);
        for (int i = 0; i < count; ++i) {
            pred += scale[count - 1][i] * predGen[i];
        }
        for (auto i = 0; i < dim; i++) {
            pred[i] = (pred[i] + (GEO_SCALE / 2)) >> GEO_SHIFT;
        }
        AV[ai] = pred + delta;
        return;
    }
#else // previous non FXP implementation to remove
    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    int startC = altC;
    int  count = 0;                 // number of valid mpara found
    vecN predGen(0);                // the predicted generic

    // avoid this test in the loop using templates ?
    if (attr->predMethod == (int8_t)EBConfig::GenPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;
            // O is in geometric domain ... should we stick to attribute connectivity domain only
            const bool seamInPara = (OAI.size() && (OAI[altC] == -2));
            const auto oppoV = seamInPara ? -1 : ((O[altC] >= 0) ? AI[O[altC]] : -1);
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictGenPara(altC, AI, predGen);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -2) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }
    // 1. use parallelogram prediction when possible
    if (count > 0) {
        predGen = glm::round(predGen / vecN(count));
        AV[ai] = predGen + readGenDeltaFine();
        return;
    }

#endif

    // 2. or fallback to delta with available values
    const auto& c_p_ai = AI[ov.p(c)];
    const auto& c_n_ai = AI[ov.n(c)];

    if (c_p_ai > -1 && MV[c_p_ai] > -1) {
        AV[ai] = readGenDeltaCoarse() + AV[c_p_ai];
        return;
    }

    if (c_n_ai > -1 && MV[c_n_ai] > -1) {
          AV[ai] = readGenDeltaCoarse() + AV[c_n_ai];
        return;
    }

    // 3. or maybe we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_ai = AI[b];
        if (MV[b_ai] > -1) {
            AV[ai] = readGenDeltaCoarse() + AV[b_ai];
            return;
        }
    }

    // 4. no more choices, it is a start
    AV[ai] = readGenStart();

    return;

}

#if ENABLE_GENERIC_FXP
template <int dim>
void GenericVertexAttributeDecoder<dim>::predictGenPara(
    const int c, const std::vector<int>& attrIndices,
    i64vecN& predGen
) {
    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;
    auto& AV = attr->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    vecN avOppo = AV[AI[O[c]]]; // // correction required to handle 32b qg
    vecN avPrev = AV[AI[ov.p(c)]];
    vecN avNext = AV[AI[ov.n(c)]];

    // parallelogram prediction = prevGen + nextGen - oppoGen
    predGen = avPrev + avNext - avOppo;
}
#else
template <int dim>
void GenericVertexAttributeDecoder<dim>::predictGenPara(
    const int c, const std::vector<int>& attrIndices,
    vecN& predGen
) {
    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;
    auto& AV = attr->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    vecN avOppo = AV[AI[O[c]]]; // recheck vs auxO
    vecN avPrev = AV[AI[ov.p(c)]];
    vecN avNext = AV[AI[ov.n(c)]];

    // parallelogram prediction estGen = prevGen + nextGen - oppoGen
    const vecN estGen = avPrev + avNext - avOppo;
    predGen += estGen;
}
#endif

void NormalVertexAttributeDecoder::decodeWithPrediction(
    int c,
    const std::vector<int>& attrIndices
) {
    const auto MAX_PARALLELOGRAMS = 4;

    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;               // pO
    const auto& V = ov.V;               // pV
    const auto& OAI = attr->ct.O;       // auxO
    auto& AV = attr->values;            // auxNorm 
    const auto& AI = attrIndices;       // auxV
    const auto& ai = AI[c];             // v
    auto& MV = mainDec->MV;             // mV
    const int& qn = this->attr->qp;

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
    // swing around the fan until we find a border
    bool onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -2) : false);
    int nextC = ov.n(O[ov.n(altC)]);
    while (nextC >= 0 && nextC != c && !onSeam)
    {
        altC = nextC;
        onSeam = (OAI.size() != 0 ? (OAI[ov.n(altC)] == -2) : false);
        nextC = ov.n(O[ov.n(altC)]);
    };
    bool isBoundary = (!onSeam && nextC != c);

    // now we are position on the right most corner sharing v
    // we turn left an evaluate the possible predictions
    int startC = altC;
    int  count = 0;                 // number of valid strtech found
    glm::vec3 predNorm(0, 0, 0);    // the predicted norm

    // avoid this test in the loop using templates ?
    if (attr->predMethod == (int8_t)EBConfig::NormPred::MPARA) {
        do
        {
            if (count >= MAX_PARALLELOGRAMS) break;

            const bool seamInPara = (OAI.size() && (OAI[altC] == -2));
            const auto oppoV = seamInPara ? -1 : ((O[altC] >= 0) ? AI[O[altC]] : -1);
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if ((oppoV > -1 && prevV > -1 && nextV > -1) &&
                ((MV[oppoV] > 0) && (MV[prevV] > 0) && (MV[nextV] > 0)))
            {
                predictNormPara(altC, AI, predNorm);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -2) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }
    else if (attr->predMethod == (int8_t)EBConfig::NormPred::CROSS) {
        do
        {
            const auto prevV = AI[ov.p(altC)];
            const auto nextV = AI[ov.n(altC)];
            if (prevV > -1 && nextV > -1) // no check on marked predictions as Geo only used
            {
                predictNormCross(altC, predNorm);
                ++count;
            }
            onSeam = (OAI.size() != 0 ? (OAI[ov.p(altC)] == -2) : false);
            altC = ov.p(O[ov.p(altC)]);                       // swing around the triangle fan
        } while (altC >= 0 && altC != startC && !onSeam);     // incomplete fan or full rotation
    }
    // 1. use MPARA or Cross
    if (count > 0 && !(predNorm == glm::vec3(0,0,0))) {
        const glm::i64vec3 predNormI64 = predNorm;
        int64_t  dot_predNorm = predNormI64.x * predNormI64.x + predNormI64.y * predNormI64.y + predNormI64.z * predNormI64.z;
        
        const int64_t irsqt = irsqrt(dot_predNorm);                                 // fxp:40 = NRM_SHIFT_1
        const glm::i64vec3 st1 = predNormI64 * irsqt;                               // fxp:NRM_SHIFT_1
        const glm::i64vec3 st2 = st1 + (int64_t)(1ULL << NRM_SHIFT_1);              // fxp:NRM_SHIFT_1
        
        const glm::i64vec3 st3 = st2 << (int64_t)(qn-1);                            // fxp:NRM_SHIFT_1
        const glm::i64vec3 st4 = ((st2 + (int64_t)1) >> (int64_t)(1));
        const glm::i64vec3 st5 = st3 - st4 + (int64_t)(1ULL << (NRM_SHIFT_1-1));    // fxp:NRM_SHIFT_1
        const glm::vec3 scaledPredNorm = st5 >> NRM_SHIFT_1;                        // fxp:0
        
        if (useOctahedral)
            decodeOctahedral(scaledPredNorm, AV[ai], 1);
        else
            AV[ai] = scaledPredNorm + readNrmDeltaFine();
        return;
    }

    // 2. or fallback to delta with available values
    const auto& c_p_ai = AI[ov.p(c)];
    const auto& c_n_ai = AI[ov.n(c)];

    if (c_p_ai > -1 && MV[c_p_ai] > -1) {
        if (useOctahedral)
            decodeOctahedral(AV[c_p_ai], AV[ai], 0);
        else
            AV[ai] = readNrmDeltaCoarse() + AV[c_p_ai];
        return;
    }

    if (c_n_ai > -1 && MV[c_n_ai] > -1) {
        if (useOctahedral)
            decodeOctahedral(AV[c_n_ai], AV[ai], 0);
        else
            AV[ai] = readNrmDeltaCoarse() + AV[c_n_ai];
        return;
    }

    // 3. or maybe we are on a boundary
    // then we may use deltas from previous vertex on the boundary
    if (isBoundary) {
        const auto b = ov.p(startC); // b is on boundary
        const auto b_ai = AI[b];
        if (MV[b_ai] > -1) {
            if (useOctahedral)
                decodeOctahedral(AV[b_ai], AV[ai], 0);
            else
                AV[ai] = readNrmDeltaCoarse() + AV[b_ai];
            return;
        }
    }

    // 4. no more choices, it is a start
    AV[ai] = readNrmStart();

    return;

}

//
void NormalVertexAttributeDecoder::predictNormPara(
    const int c, const std::vector<int>& attrIndices,
    glm::vec3& predNorm
) {
    const int& qn = this->attr->qp;
    auto& ov = mainDec->attr->ct;
    const auto& O = ov.O;
    auto& AV = attr->values;
    const auto& AI = attrIndices;  // texture coordinates indices

    glm::vec3 avOppo = AV[AI[O[c]]]; // recheck vs auxO
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

//
void NormalVertexAttributeDecoder::predictNormCross(
    const int c, glm::vec3& predNorm
) {
    auto& ov = mainDec->attr->ct;
    const auto& G = mainDec->attr->values;
    const auto& V = ov.V;

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

// Normal Octahedral Functions.
void NormalVertexAttributeDecoder::decodeOctahedral(const glm::vec3 pred, glm::vec3& rec, const bool fine) {

    glm::vec2 first2Dresidual(0, 0);
    if (fine)
        first2Dresidual = readNrmOctaFine();
    else
        first2Dresidual = readNrmOctaCoarse();

    glm::vec2 pred2D(0, 0);
    convert3Dto2Doctahedral(pred, pred2D);

    glm::vec2 rec2D(0 ,0);
    
    if (wrapAround) {
        const int32_t center = ( 1u << static_cast<uint32_t>( qpOcta-1 ) );
        for (int c = 0; c < 2; c++) {
            pred2D[c] = pred2D[c] - center;
        }
        rec2D = pred2D + first2Dresidual;
        
        int32_t maxNormalValueplusOne = ( 1u << static_cast<uint32_t>( qpOcta ) );
        for (int c = 0; c < 2; c++) {
            if (rec2D[c] < -center)
                rec2D[c] = rec2D[c] + maxNormalValueplusOne;
            else if (rec2D[c] > center-1)
                rec2D[c] = rec2D[c] - maxNormalValueplusOne;
        }

        for (int c = 0; c < 2; c++) {
            rec2D[c] = rec2D[c] + center;  // Make it back to unsigned integer
        }
    } else {
        rec2D = pred2D + first2Dresidual;
    }
    
    glm::vec3 reconstructed3D(0, 0, 0);
    convert2DoctahedralTo3D(rec2D, reconstructed3D);

    if (normalEncodeSecondResidual)
        rec = reconstructed3D + readOctsecondResiduals();
    else
        rec = reconstructed3D;

    return;
}

void NormalVertexAttributeDecoder::convert3Dto2Doctahedral(glm::vec3 input, glm::vec2& output) {
    const int& qn = this->attr->qp;
    // Center
    const int32_t center = ( 1u << static_cast<uint32_t>( qn-1 ) );
    for (int c = 0; c < 3; c++) {
        input[c] = input[c] - center;
    }
    
    const uint64_t divisor = std::abs(input.x) + std::abs(input.y) + std::abs(input.z);
    int32_t shift;
    const int64_t recipD = recipApprox(divisor, shift);                           // fxp:shift    
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

void NormalVertexAttributeDecoder::convert2DoctahedralTo3D(glm::vec2 input, glm::vec3& output) {
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
void MaterialIDFaceAttributeDecoder::decodeAttributes(EBConfig& cfg)
{
    const auto& ov = mainDec->attr->ct;
    auto& O = mainDec->attr->ct.O;
    auto& ID = attr->values;

    int prevFid = -1;
    int prevTri = -1;
    int fidNotPredictedIdx = 0;
    int fidIsRightIdx = 0;
    int fidIsLeftIdx = 0;
    int fidIsFacingIdx = 0;
    int fidIsDifferentIdx = 0;

    // triangle marking table 
    // NOTE: could use the one from mainDec ? maybe.
    std::vector<int> UF(mainDec->nT, 0);
    // I.9.8  decoding per face attributes
    for (int i = mainDec->processedCorners.size() - 1; i >= 0; --i)
    {
        const auto& corner = mainDec->processedCorners[i];
        const int corners[3] = { corner, ov.n(corner), ov.p(corner) };
        const int tri = ov.t(corner);

        const bool isSame = !iFidsIdIsDifferent[fidIsDifferentIdx++];
        if (isSame)
        {
            ID[tri] = prevFid;
            UF[tri] = 1;
            prevTri = tri;
            continue;
        }

        // use _UF and shift 
        // may be on a boundary, in this case the O is negative and t(O[]) to, hence the test
        const auto tFIdx = ov.t(ov.O[corners[0]]);
        const auto tRIdx = ov.t(ov.O[corners[1]]);
        const auto tLIdx = ov.t(ov.O[corners[2]]);
        bool decodedFacing = (tFIdx < 0 ? false : UF[tFIdx]) && !(tFIdx == prevTri);
        bool decodedRight = (tRIdx < 0 ? false : UF[tRIdx]) && !(tRIdx == prevTri);
        bool decodedLeft = (tLIdx < 0 ? false : UF[tLIdx]) && !(tLIdx == prevTri);
        int faceIdFacing = decodedFacing ? ID[ov.t(O[corners[0]])] : -1;
        int faceIdRight = decodedRight ? ID[ov.t(O[corners[1]])] : -1;
        int faceIdLeft = decodedLeft ? ID[ov.t(O[corners[2]])] : -1;

        for (auto k = 0; k < 3; k++)
        {
            const auto dec = (k == 0) ? decodedRight : ((k == 1) ? decodedLeft : decodedFacing);
            if (!dec)
            {
                // initialise a starting "rotating corner" as a corner of the non encoded
                // adjacent triangle (R for k==0, L if k==1, F if k==2) 
                auto rc = ov.o(corners[(k + 1) % 3]);
                // then start looping through adjacent triangles, swinging around a first 
                // corner shared with the triangle we are encoding
                rc = ov.o(ov.n(rc));
                // stopping if an encoded triangle is found, or if the analyzed face is one of 
                // the 3 original R,L,F triangles
                while (rc >= 0 && rc != ov.p(O[corners[(k) % 3]]) && !UF[ov.t(rc)])
                {
                    rc = O[ov.n(rc)];
                }
                if (rc < 0 || !UF[ov.t(rc)])
                {
                    // if no encoded face found, then swing around the other shared corners
                    rc = ov.o(corners[(k + 1) % 3]);
                    rc = ov.o(ov.p(rc));
                    while (rc >= 0 && rc != ov.n(O[corners[(k + 2) % 3]]) && !UF[ov.t(rc)])
                    {
                        rc = O[ov.p(rc)];
                    }
                }
                // negative if no new decoded was found
                auto newDecoded = -1; // negative if no new decoded was found
                if (rc >= 0 && UF[ov.t(rc)] && !(ov.t(rc) == prevTri)) {
                    newDecoded = ID[ov.t(rc)];
                }
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

        UF[tri] = 1;
        int fid = -1;
        if ((fid < 0) && decodedRight)
        {
            if (iFaceIdIsRight[fidIsRightIdx++])
                fid = faceIdRight;
        }
        if ((fid < 0) && decodedLeft && (faceIdLeft != faceIdRight))
        {
            if (iFaceIdIsLeft[fidIsLeftIdx++])
                fid = faceIdLeft;
        }
        if ((fid < 0) && decodedFacing && (faceIdFacing != faceIdRight) && (faceIdFacing != faceIdLeft))
        {
            if (iFaceIdIsFacing[fidIsFacingIdx++]) {
                fid = faceIdFacing;
            }

        }
        if (fid < 0)
            fid = iNotPredictedFaceId[fidNotPredictedIdx++];
        ID[tri] = fid;
        prevFid = fid;
        prevTri = tri;

    }
}

// Dequantize if applicable
void EBReversiDecoder::dequantize(Model& output) {

    auto attrIter = output.attributes.begin();
    for (auto dec : decoders) {
        // dequantize position attributes
        if (auto decPos = dynamic_cast<PositionVertexAttributeDecoder*>(dec))
        {
            if (decPos->attr->dequantize) // could be at dec level - getMeshPositionDequantizeFlag could also be used 
            {
                std::vector<float>* posValues = (*attrIter)->getData<float>();
                if (!posValues->empty()) {
                    const int32_t   maxPositionQuantizedValue = (1u << static_cast<uint32_t>(decPos->attr->qp)) - 1;
                    const glm::vec3 diag = decPos->maxPos - decPos->minPos;
                    const float     range = std::max(std::max(diag.x, diag.y), diag.z);
                    const float     scale = range / maxPositionQuantizedValue;
                    // loops could be templated using (*attrIter)->getDataCnt() (*attrIter)->getDim()
                    for (size_t i = 0; i < posValues->size() / 3; i++) {
                        for (size_t k = 0; k < 3; k++)
                            (*posValues)[3 * i + k] = (*posValues)[3 * i + k] * scale + decPos->minPos[k];
                    }
                }
            }
        }

        // dequantize UV coordinates
        if (auto decUv = dynamic_cast<UVCoordVertexAttributeDecoder*>(dec))
        {
            if (decUv->attr->dequantize) // could be at dec level - getMeshAttributeDequantizeFlag could also be used 
            {
                std::vector<float>* uvValues = (*attrIter)->getData<float>();
                if (!uvValues->empty()) {
                    const int32_t   maxUVcoordQuantizedValue = (1u << static_cast<uint32_t>(decUv->attr->qp)) - 1;
                    const glm::vec2 diag = decUv->maxUv - decUv->minUv;
                    const float     range = std::max(diag.x, diag.y);
                    float           scale = range / maxUVcoordQuantizedValue;
                    // loops could be templated using (*attrIter)->getDataCnt() (*attrIter)->getDim()
                    for (size_t i = 0; i < uvValues->size() / 2; i++) {
                        for (size_t k = 0; k < 2; k++)
                            (*uvValues)[2 * i + k] = (*uvValues)[2 * i + k] * scale + decUv->minUv[k];

                    }
                }
            }
        }

        // dequantize Normals
        if (auto decNrm = dynamic_cast<NormalVertexAttributeDecoder*>(dec))
        {
            if (decNrm->attr->dequantize) // could be at dec level - getMeshAttributeDequantizeFlag could also be used 
            {
                std::vector<float>* nrmValues = (*attrIter)->getData<float>();
                if (!nrmValues->empty()) {

                    const int32_t   maxNormalQuantizedValue = (1u << static_cast<uint32_t>(decNrm->attr->qp)) - 1;
                    const glm::vec3 diag = decNrm->maxNrm - decNrm->minNrm;
                    const float     range = std::max(std::max(diag.x, diag.y), diag.z);
                    const float     scale = range / maxNormalQuantizedValue;
                    // loops could be templated using (*attrIter)->getDataCnt() (*attrIter)->getDim()
                    for (size_t i = 0; i < nrmValues->size() / 3; i++) {
                        for (size_t k = 0; k < 3; k++)
                            (*nrmValues)[3 * i + k] = (*nrmValues)[3 * i + k] * scale + decNrm->minNrm[k];
                    }
                }
            }
        }

        // dequantize Generic
        if (dec && dec->attrType == eb::Attribute::Type::GENERIC)
        {

            auto processGen = [&](auto&& decGen) {
                if (decGen->attr->dequantize) // could be at dec level - getMeshAttributeDequantizeFlag could also be used 
                {
                    std::vector<float>* genValues = (*attrIter)->getData<float>();
                    if (!genValues->empty()) {

                        const int32_t   maxNormalQuantizedValue = (1u << static_cast<uint32_t>(decGen->attr->qp)) - 1;
         
                        const auto diag = decGen->maxGen - decGen->minGen;
                        float     range = diag[0];
                        for (size_t k = 1; k < decGen->nbComp; k++)
                            range = std::max(range, diag[k]);
                        const float     scale = range / maxNormalQuantizedValue;
                        // loops could be templated using (*attrIter)->getDataCnt() (*attrIter)->getDim()
                        for (size_t i = 0; i < genValues->size() / decGen->nbComp; i++) {
                            for (size_t k = 0; k < decGen->nbComp; k++)
                                (*genValues)[3 * i + k] = (*genValues)[decGen->nbComp * i + k] * scale + decGen->minGen[k];
                        }
                    }
                }
            };

            switch (dec->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeDecoder<1>*>(dec)); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeDecoder<2>*>(dec)); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeDecoder<3>*>(dec)); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeDecoder<4>*>(dec)); break;
            };


        }

        // material colors are not implemented in this version

        // dequantization of per face material values is not considered in this version
        if (auto decFaceMatId = dynamic_cast<MaterialIDFaceAttributeDecoder*>(dec))
        {
            if (decFaceMatId->attr->dequantize)
            {
                std::cerr << "Warning: dequantization of per face material values is not considered in this version" << std::endl;
            }
        }

        ++attrIter;
    }

}


bool EBReversiDecoder::unserialize(MeshCoding& meshCoding) {

    auto t = now();

    // Extracting Mesh Coding Header
    auto& mch = meshCoding.getMeshCodingHeader();

    auto& method = (uint8_t&)mch.getMeshCodecType();

    // vertex traversal method
    switch (mch.getMeshVertexTraversalMethod()) {
    case MeshVertexTraversalMethod::MESH_EB_TRAVERSAL: cfg.traversal = EBConfig::Traversal::EB; break;
    case MeshVertexTraversalMethod::MESH_DEGREE_TRAVERSAL: cfg.traversal = EBConfig::Traversal::DEGREE; break;
    }

    // creation of the position attribute
    const auto attrPosIdx = _ctMesh.createAttributeVertexPosition();
    AttributeVertexPosition* attrPos = static_cast<AttributeVertexPosition*>(_ctMesh.getAttribute(attrPosIdx));
    // creation of the associated position attribute decoder
    const auto decPosIdx = createPositionAttributeDecoder(attrPos);
    auto decPos = dynamic_cast<PositionVertexAttributeDecoder*>(decoders[decPosIdx]);

    // Position Decoding Parameters
    auto& mpep = mch.getMeshPositionEncodingParameters();
    auto qp = mpep.getMeshPositionBitDepthMinus1() + 1;
    cfg.reverseUnification = mpep.getMeshPositionReverseUnificationFlag();
    cfg.deduplicate = !(mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_NONE);
    // Position Dequantization 
    cfg.intAttr = !(mch.getMeshPositionDequantizeFlag()); // intAttr is used as an aggregate here, true if at least one attribute is dequantized

    // Output reindexing option enables to override bitstream defined values, it shall be kept to default when reverseUnification is active
    // Note : then reindexing scheme could be reformulated when deduplication is active
    if (cfg.reverseUnification || cfg.reindexOutput == EBConfig::Reindex::DEFAULT)
    {
        switch (mch.getMeshReindexOutput()) {
        case MeshVertexTraversalMethod::MESH_EB_TRAVERSAL: cfg.reindexOutput = EBConfig::Reindex::EB; break;
        case MeshVertexTraversalMethod::MESH_DEGREE_TRAVERSAL: cfg.reindexOutput = EBConfig::Reindex::DEGREE; break;
        }
    }

    attrPos->qp = qp;
    attrPos->dequantize = mch.getMeshPositionDequantizeFlag();

    if (mch.getMeshPositionDequantizeFlag())
    {
        auto& mpdp = mch.getMeshPositionDequantizeParameters();
        decPos->minPos.x = mpdp.getMeshPositionMin(0);
        decPos->minPos.y = mpdp.getMeshPositionMin(1);
        decPos->minPos.z = mpdp.getMeshPositionMin(2);
        decPos->maxPos.x = mpdp.getMeshPositionMax(0);
        decPos->maxPos.y = mpdp.getMeshPositionMax(1);
        decPos->maxPos.z = mpdp.getMeshPositionMax(2);
    }

    // Attributes
    for (auto i = 0; i < mch.getMeshAttributeCount(); i++)
    {
        auto& maep = mch.getMeshAttributesEncodingParameters(i);

        // TO BE MODIFIED -implement all attribute types
        if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_TEXCOORD)
        {
            auto qt = maep.getMeshAttributeBitDepthMinus1() + 1;

            bool hasSeparateUvIndex = false;
            int refAuxIdx = -1;
            if (!maep.getMeshAttributePerFaceFlag())
            {
                hasSeparateUvIndex = maep.getMeshAttributeSeparateIndexFlag();
                if (!maep.getMeshAttributeSeparateIndexFlag())
                    refAuxIdx = maep.getMeshAttributeReferenceIndexPlus1();
            }
            else
            {
                // per face texture coordinates attribute not implmemented in this version
                std::cerr << "Error: use of per face texture coordinates attribute is not allowed in this version" << std::endl;
                exit(0);
            }

            if (refAuxIdx > 0)
            {
                // reuse of connectivity from appropriate attribute not implemented in this version when refAuxIdx > 0
                std::cerr << "Error: reuse of connectivity from an attribute different from the primary attribute is not implemented in this version" << std::endl;
                exit(0);
            }
            // creation of the UVCoordinate attribute
            auto attrIdx = _ctMesh.createAttributeVertexUVCoord(attrPosIdx, hasSeparateUvIndex, refAuxIdx);
            // creation of the associated attribute decoder
            auto decUvIdx = createUVCoordAttributeDecoder(decPos,
                static_cast<AttributeVertexUVCoord*>(_ctMesh.getAttributes()[attrIdx]));
            auto decUV = dynamic_cast<UVCoordVertexAttributeDecoder*>(decoders[decUvIdx]);

            if (maep.getMeshAttributePredictionMethod() == (uint8_t)(MeshAttributePredictionMethod_TEXCOORD::MESH_TEXCOORD_STRETCH))
                decUV->attr->predMethod = (uint8_t)EBConfig::UvPred::STRETCH; // 

            decUV->attr->qp = qt; 
            decUV->attr->dequantize = mch.getMeshAttributeDequantizeFlag()[i];
            cfg.intAttr &= decUV->attr->dequantize; 
            // intAttr has aggregate meaning, overriven in encoder when reverse unification is active

            if (mch.getMeshAttributeDequantizeFlag()[i])
            {
                auto& madp = mch.getMeshAttributesDequantizeParameters()[i];
                decUV->minUv.x = madp.getMeshAttributeMin(0);
                decUV->minUv.y = madp.getMeshAttributeMin(1);
                decUV->maxUv.x = madp.getMeshAttributeMax(0);
                decUV->maxUv.y = madp.getMeshAttributeMax(1);
            }

        }
        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_NORMAL)
        {            
            auto qn = maep.getMeshAttributeBitDepthMinus1() + 1;

            bool hasSeparateNorIndex = false;
            int refAuxIdx = -1;
            if (!maep.getMeshAttributePerFaceFlag())
            {
                hasSeparateNorIndex = maep.getMeshAttributeSeparateIndexFlag();
                if (!maep.getMeshAttributeSeparateIndexFlag())
                    refAuxIdx = maep.getMeshAttributeReferenceIndexPlus1();
            }
            else
            {
                // per face normal attribute not implmemented in this version
                std::cerr << "Error: use of per face normal attribute is not implemented in this version" << std::endl;
                exit(0);
            }

            if (refAuxIdx > 0)
            {
                // reuse of connectivity from appropriate attribute not implemented in this version when refAuxIdx > 0
                std::cerr << "Error: reuse of connectivity from an attribute different from the primary attribute is not implemented in this version" << std::endl;
                exit(0);
            }
            // creation of the UVCoordinate attribute
            auto attrIdx = _ctMesh.createAttributeVertexNormal(attrPosIdx, hasSeparateNorIndex, refAuxIdx);
            // creation of the associated attribute decoder
            auto decNorIdx = createNormalAttributeDecoder(decPos,
                static_cast<AttributeVertexNormal*>(_ctMesh.getAttributes()[attrIdx]));
            auto decNor = dynamic_cast<NormalVertexAttributeDecoder*>(decoders[decNorIdx]);

            if (maep.getMeshAttributePredictionMethod() == (uint8_t)(MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_DELTA))
                decNor->attr->predMethod = (uint8_t)EBConfig::NormPred::DELTA; // 
            else if (maep.getMeshAttributePredictionMethod() == (uint8_t)MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_MPARA)
                decNor->attr->predMethod = (uint8_t)EBConfig::NormPred::MPARA; // 
            else if (maep.getMeshAttributePredictionMethod() == (uint8_t)MeshAttributePredictionMethod_NORMAL::MESH_NORMAL_CROSS)
                decNor->attr->predMethod = (uint8_t)EBConfig::NormPred::CROSS; //
            
            decNor->attr->qp = qn;
            decNor->attr->dequantize = mch.getMeshAttributeDequantizeFlag()[i];
            cfg.intAttr &= decNor->attr->dequantize;

            decNor->useOctahedral = mch.getMeshNormalOctahedralFlag()[i];

            if (mch.getMeshAttributeDequantizeFlag()[i])
            {
                // normal outputs always have dimension 3 in this version
                auto& madp = mch.getMeshAttributesDequantizeParameters()[i];

                decNor->minNrm.x = madp.getMeshAttributeMin(0);
                decNor->minNrm.y = madp.getMeshAttributeMin(1);
                decNor->minNrm.z = madp.getMeshAttributeMin(2);
                decNor->maxNrm.x = madp.getMeshAttributeMax(0);
                decNor->maxNrm.y = madp.getMeshAttributeMax(1);
                decNor->maxNrm.z = madp.getMeshAttributeMax(2);
            }
            else
            {
                // min / max values used in prediction require initialization - default value for intattr is +/-1.0
                decNor->minNrm = glm::vec3(-1.0);
                decNor->maxNrm = glm::vec3(+1.0);
            }
        }

        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_GENERIC)
        {
            auto qg = maep.getMeshAttributeBitDepthMinus1() + 1;

            bool hasSeparateGenIndex = false;
            int refAuxIdx = -1;
            if (!maep.getMeshAttributePerFaceFlag())
            {
                hasSeparateGenIndex = maep.getMeshAttributeSeparateIndexFlag();
                if (!maep.getMeshAttributeSeparateIndexFlag())
                    refAuxIdx = maep.getMeshAttributeReferenceIndexPlus1();
            }
            else
            {
                // per face generic attribute not implmemented in this version
                std::cerr << "Error: use of per face generic attribute is not allowed in this version" << std::endl;
                exit(0);
            }

            if (refAuxIdx > 0)
            {
                // reuse of connectivity from appropriate attribute not implemented in this version when refAuxIdx > 0
                std::cerr << "Error: reuse of connectivity from an attribute different from the primary attribute is not implemented in this version" << std::endl;
                exit(0);
            }

            // creation of the Generic attribute
            auto dim = mch.getMeshAttributeNumComponentsMinus1()[i] + 1;
            auto attrIdx = _ctMesh.createAttributeVertexGenericN(dim, attrPosIdx, hasSeparateGenIndex, refAuxIdx);
            // creation of the associated attribute decoder
            auto genAttrib = _ctMesh.getAttributes()[attrIdx];
            // float restriction in this version could be lifted by extending templating 
            auto decGenIdx = createGenericAttributeDecoderN(dim, decPos, static_cast<AttributeVertex<float>*>(_ctMesh.getAttributes()[attrIdx]));

            auto processGen = [&](auto&& decGen) {
                // !! something to revise to ba able to better write the following
                if (maep.getMeshAttributePredictionMethod() == (uint8_t)(MeshAttributePredictionMethod_GENERIC::MESH_GENERIC_DELTA))
                    decGen->attr->predMethod = (uint8_t)EBConfig::GenPred::DELTA; // 
                else if (maep.getMeshAttributePredictionMethod() == (uint8_t)MeshAttributePredictionMethod_GENERIC::MESH_GENERIC_MPARA)
                    decGen->attr->predMethod = (uint8_t)EBConfig::GenPred::MPARA; // 

                decGen->attr->qp = qg;
                decGen->attr->dequantize = mch.getMeshAttributeDequantizeFlag()[i];
                cfg.intAttr &= decGen->attr->dequantize;

                if (mch.getMeshAttributeDequantizeFlag()[i])
                {
                    auto& madp = mch.getMeshAttributesDequantizeParameters()[i];
                    for (auto i = 0; i < decGen->attr->nbComp; ++i)
                    {
                        decGen->minGen[i] = madp.getMeshAttributeMin(i);
                        decGen->maxGen[i] = madp.getMeshAttributeMax(i);
                    }
                }
            };

            switch (decoders[decGenIdx]->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeDecoder<1>*>(decoders[decGenIdx])); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeDecoder<2>*>(decoders[decGenIdx])); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeDecoder<3>*>(decoders[decGenIdx])); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeDecoder<4>*>(decoders[decGenIdx])); break;
            };
        }

        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_MATERIAL_ID)
        {
            // assuming faceId
            auto qm = maep.getMeshAttributeBitDepthMinus1() + 1;
           
            // creation of the FaceId attribute
            auto attrIdx = _ctMesh.createAttributeFaceMaterialId(attrPosIdx);
            // creation of the associated attribute decoder
            auto decFaceMatIdIdx = createFaceIDAttributeDecoder(decPos,
                static_cast<AttributeFaceMaterialId*>(_ctMesh.getAttributes()[attrIdx]));
            auto decFaceMatId = dynamic_cast<MaterialIDFaceAttributeDecoder*>(decoders[decFaceMatIdIdx]);
            decFaceMatId->attr->qp = qm;
            // TO CHECK dequantizing the id is not disabled, may rescale
            decFaceMatId->attr->dequantize = mch.getMeshAttributeDequantizeFlag()[i];
        }
        else {
            std::cerr << "Warning: unsupported attribute type=" << (int)mch.getMeshAttributeType()[i] << ", skipping" << std::endl;
        }
    }

    // Payload

    // Extracting Mesh Position Coding Payload
    auto& mpcp = meshCoding.getMeshPositionCodingPayload();

    cfg.useEntropyPacket = mch.getMeshEntropyPacketFlag();

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
    // Difference from tmmv9.0 where contexts where reused without reset 
    // When implementing the specification results are not better for C1/C2 
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

    EntropyDecoder   adE;

    
    
    auto byteCount = mch.getMeshGeoEntropyPacketBufferSize();
    if (byteCount > 0) { // I.8.2 : It is a requirement of MEB bitstream conformance that n is greater than 0
        const auto* const  bufferPtr0 = reinterpret_cast<const char*>(&mch.getMeshGeoEntropyPacketBuffer()[0]);
        adE.setBuffer(byteCount, bufferPtr0);
    }
    adE.start();

    decPos->iVerticesFine.resize(mpcp.getMeshPositionFineResidualsCount());
    decPos->iVerticesCoarse.resize(mpcp.getMeshPositionCoarseResidualsCount());

    decPos->nB = mpcp.getMeshCcWithBoundaryCount();

    // Handles
    auto NumHandles = mpcp.getMeshHandlesCount();
    decPos->iHandles.resize(NumHandles * 2);
    uint32_t curHandle1 = 0;
    uint32_t curHandle2 = 0;

    const int MinHandles = MIN_HANDLE; // handles always AC coded as MIN_HANDLE set to 0
    if (NumHandles)
    {
#if MIN_HANDLE
        if (NumHandles < MinHandles)
        {
            for (uint32_t i = 0; i < NumHandles; i++) {
                decPos->iHandles[2 * i + 0] = mpcp.getMeshHandleFirstDelta()[i] + (i ? decPos->iHandles[2 * i - 2] : 0);
                decPos->iHandles[2 * i + 1] = mpcp.getMeshHandleSecondDelta()[i] + (i ? decPos->iHandles[2 * i - 1] : 0);
            }
        }
        else
#endif
        {
            const auto capexp = 3;
            int prev1 = 0;
            int prev2 = 0;
            for (uint32_t i = 0; i < NumHandles; i++) {
                const bool h1sign = adE.decode(ctxTblOthers[CtxIndex::mesh_handle_first_sign]);
                const bool h2offset = adE.decode(ctxTblOthers[CtxIndex::mesh_handle_second_shift]);
                int nb = 1;
                int val = 0;
                bool isneg;
                for (auto i = 0; adE.decode(ctxTbl7_mesh_handle_first_vdl4m1[std::min(i, capexp)]); i++, nb++) {}
#if HANDLE_MOD
                int offset;
                for (auto i = 0; i < HGRP * nb; ++i)
                {
                    val = (val << 1) | adE.decode(ctxTbl8);
                }
                offset = 0;
                for (auto i = 2; i <= nb; ++i) offset = (offset << HGRP) + (1 << HGRP);
                val = val + offset;
#else
                for (auto i = 0; i < 4 * nb; ++i)
                {
                    val = (val << 1) | adE.decode(ctxTbl8);
                }                
#endif
                isneg = val & 1;
                val = (val + (isneg ? 1 : 0)) >> 1;
                val = isneg ? -val : val;
                auto cur1 = prev1 + val;
                decPos->iHandles[2 * i + 0] = (3 * cur1 + 2) * (h1sign ? 1 : -1);
                prev1 = cur1;
                nb = 1;
                val = 0;
                for (auto i = 0; adE.decode(ctxTbl9_mesh_handle_second_vdl4m1[std::min(i, capexp)]); i++, nb++) {}
#if HANDLE_MOD
                for (auto i = 0; i < HGRP * nb; ++i)
                {
                    val = (val << 1) | adE.decode(ctxTbl10);
                }
                offset = 0;
                for (auto i = 2; i <= nb; ++i) offset = (offset << HGRP) + (1 << HGRP);
                val = val + offset;
#else
                for (auto i = 0; i < 4 * nb; ++i)
                {
                    val = (val << 1) | adE.decode(ctxTbl10);
                }
#endif
                isneg = val & 1;
                val = (val + (isneg ? 1 : 0)) >> 1;
                val = isneg ? -val : val;
                auto cur2 = prev2 + val;
                decPos->iHandles[2 * i + 1] = (3 * cur2 + 1) + (h2offset ? 1 : 0);
                prev2 = cur2;

            }
        }
    }

    // Topology
    {
// build with and without--clersUpdate 
#define CDVERSION // CD version with 30 contexts
#define CDUPDATE // DIS version

#ifdef CDVERSION
#ifdef CDUPDATE
        const char codeToChar[9][5] =
        {
            { 'R','C','S','L','E' }, // 0
            { 'R','C','S','L','E' }, // 1
            { 'R','C','S','L','E' }, // 2
            { 'E','R','L','C','S' }, // 3
            { 'C','R','E','L','S' }, // 4
            { 'C','R','S','L','E' }, // 5
            { 'C','R','L','S','E' }, // 6
            { 'C','E','L','R','S' }, // 7
            { 'C','R','E','S','L' }, // 8
        };
#else
        const char codeToChar[16] = { 'C','R',0,'S',0,0,0,'L',0,0,0,0,0,0,0,'E' };
#endif
        AdaptiveBitModel ctx_clers[30]; // aligned with Table K-6
#ifdef CDUPDATE
        char ClersSymbol0 = 'R';
        char ClersSymbol1 = 'E';
#else
        char ClersSymbol1 = 'C';
        char ClersSymbol0 = 'R';
#endif

        int CtxIdxClers = 0;
        int CtxStateClers = 0; // added in update
        int Crun = 0;
        int BinIdxClers = 0;


        // REWRITTEN USING 1 2 or 3 BINS - avoiding use of a dummy bin 1 which could be confusing as nothing is encoded

        // I.10.3.4.1	Determination of CtxIdxClers for a bin of mesh_clers_symbol[]
        auto selectCtxIdxClers = [&]() {

            if (ClersSymbol0 == 'C')
                CtxIdxClers = (mpcp.getMeshClersCount() <= 3000) ? 0
                : (Crun < 2 ? 0
                    : (Crun < 7 ? 1 : 2));
            else if (ClersSymbol0 == 'S')
                CtxIdxClers = 3;
            else if (ClersSymbol0 == 'L')
                CtxIdxClers = 4;
            else if (ClersSymbol0 == 'R')
                CtxIdxClers = 5 + (ClersSymbol1 == 'C' ? 0
                    : (ClersSymbol1 == 'R' ? 1 : 2));
            else /* if ClersSymbol0 == CLERS_E */
                CtxIdxClers = 8;

            CtxStateClers = CtxIdxClers; // added in update

            CtxIdxClers += 9 * BinIdxClers;
            if (BinIdxClers > 1) CtxIdxClers -= 3 * (BinIdxClers - 1);
            };

        // K.4
        auto dec_aebin = [&]() {
            // context selection for the bin to decode
            selectCtxIdxClers();
            // decoding using selected context
            return adE.decode(ctx_clers[CtxIdxClers]);
            };
        decPos->iClers.resize(mpcp.getMeshClersCount());

        auto tc = now();

        for (uint32_t i = 0; i < mpcp.getMeshClersCount(); ++i) {
            // K.2.7	Parsing Mesh CLERS symbols
            BinIdxClers = 0;
            auto val = 0;
            auto bitIdx = 0;

            auto nbBins = 4;
            for (BinIdxClers = 0; BinIdxClers < nbBins; BinIdxClers++) {
                auto bitClers = dec_aebin();
                // 1 to 4 bins - stopping on first decoded 0 or on second bin is previous symbol is CLERS_C
                if ((bitClers == 0) || ( (BinIdxClers == 1) && (ClersSymbol0 == 'C')))
                {
                    nbBins = BinIdxClers + 1; 
                }
#ifdef CDUPDATE
                val += bitClers;
#else
                val += bitClers << BinIdxClers;
#endif
            }

#ifdef CDUPDATE
            decPos->iClers[i] = codeToChar[CtxStateClers][val];
#else
            decPos->iClers[i] = codeToChar[val];
#endif
            // I.10.3.3.1 update after parsing mesh_clers_symbol[i] 
            ClersSymbol1 = ClersSymbol0;
            ClersSymbol0 = decPos->iClers[i]; //  = mesh_clers_symbol[i];

            if (decPos->iClers[i] == 'C') //    if (mesh_clers_symbol[i] == CLERS_C)
                Crun += 1;
            else
                Crun = 0;
        }
#else // original InterDigital soluiton with 4x32 contexts
        const char codeToChar[8] = { 'C','S',0,'L',0,'R',0,'E' };
        AdaptiveBitModel ctx_isNotC[32]; // not all 32 contexts used - code to be rewitten
        AdaptiveBitModel ctx_bit1[32];
        AdaptiveBitModel ctx_bit02[32];
        AdaptiveBitModel ctx_bit12[32];
        decPos->iClers.resize(mpcp.getMeshClersCount());
        int pS = 0; //'C'
        bool useExtended = (mpcp.getMeshClersCount() > 3000); // fixed threshold is this basic variant

        auto tc = now();

        for (auto i = 0; i < mpcp.getMeshClersCount(); ++i) {
            int32_t value = adE.decode(ctx_isNotC[pS]);
            if (value)
            {
                const auto bit1 = adE.decode(ctx_bit1[pS]);
                const auto bit2 = adE.decode(bit1 ? ctx_bit12[pS] : ctx_bit02[pS]);
                value |= bit1 << 1;
                value |= bit2 << 2;

            }
            if (!useExtended)
                pS = value; // 5 contexts
            else
                pS = value + ((pS & 1) << 3) + ((pS & 4) << 2); // extended contexts (4*5)

            decPos->iClers[i] = codeToChar[value];
        }
#endif
        COUT << "  CLERS AC decoding time (ms) = " << elapsed(tc) << std::endl;
    }

    decPos->nT = mpcp.getMeshTriangleCount();

    // retrieve the NumPositionStart global
    const auto& NumPositionStart = meshCoding.getNumPositionStart();
    decPos->isVertices.resize(NumPositionStart);
    for (uint32_t i = 0; i < NumPositionStart; i++) {
        for (auto j = 0; j < 3; j++) {
            decPos->isVertices[i][j] = mpcp.getMeshPositionStart()[i][j];
        }
    }

    // extract difference information when reverse unification
    if (mpep.getMeshPositionReverseUnificationFlag()) {
      auto& mdi = mpcp.getMeshDifferenceInformation();
      addPointList.resize(mdi.getMeshAddPointCount());
      deletePointList.resize(mdi.getMeshDeletePointCount());
      modifyTriangles.resize(mdi.getModifyMeshPointCount());
      modifyIndex.resize(mdi.getModifyMeshPointCount());
      modifyPointID.resize(mdi.getModifyMeshPointCount());

      if (mdi.getMeshAddPointCount() > 0) {
        int shift = 0;
        for (auto i = 0u; i < mdi.getMeshAddPointCount(); i++) {
          shift += mdi.getAddPointIdxDelta()[i];
          addPointList[i] = shift;
        }
      }

      if (mdi.getMeshDeletePointCount() > 0) {
        int shift = 0;
        for (auto i = 0u; i < mdi.getMeshDeletePointCount(); i++) {
          shift += mdi.getDeletePointIdxDelta()[i];
          deletePointList[i] = shift;
        }
      }

      if (mdi.getModifyMeshPointCount() > 0) {
        int Tshift = 0;
        int Vshift = 0;
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          Tshift += mdi.getModifyTrianglesDelta()[i];
          modifyTriangles[i] = Tshift;
        }
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          modifyIndex[i] = mdi.getModifyIdx()[i];
        }
        for (auto i = 0u; i < mdi.getModifyMeshPointCount(); i++) {
          Vshift += mdi.getModifyPointIdxDelta()[i];
          modifyPointID[i] = Vshift;
        }
      }
    }


    // Decode position info
    if (decPos->iVerticesFine.size())
    {
        const auto maxOffset = 7;
        const auto egK = 2;
        const auto bctx = 2;
        for (int32_t k = 0; k < 3; ++k) {
            for (auto i = 0; i < decPos->iVerticesFine.size(); ++i) {
                decPos->iVerticesFine[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEO - 1, MAX_CTX_COEFF_REM_SUFFIX_GEO - 1);
            }
        }
    }
    if (decPos->iVerticesCoarse.size())
    {
        const auto maxOffset = 7;
        const auto egK = 2;
        const auto bctx = 3;
        for (int32_t k = 0; k < 3; ++k) {
            for (auto i = 0; i < decPos->iVerticesCoarse.size(); ++i) {
                decPos->iVerticesCoarse[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                    ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                    bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEO - 1, MAX_CTX_COEFF_REM_SUFFIX_GEO - 1);
            }
        }
    }

    // Decode deduplication info
    auto& mpdi = mpcp.getMeshPositionDeduplicateInformation();
    // retrieve flag count global
    const auto& NumPositionIsDuplicateFlags = meshCoding.getNumPositionIsDuplicateFlags();
    const auto& NumAddedDuplicatedVertex = meshCoding.getNumAddedDuplicatedVertex();
    // copy parsed split vertex index array if defined
    if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
        if (mpdi.getMeshPositionDeduplicateCount() > 0) {
            decPos->iDuplicateSplitVertexIdx.resize(mpdi.getMeshPositionDeduplicateCount());
            for (uint32_t i = 0; i < mpdi.getMeshPositionDeduplicateCount(); i++) {
                decPos->iDuplicateSplitVertexIdx[i] = mpdi.getMeshPositionDeduplicateIdx()[i];
            }
        }
    }

    if (NumPositionIsDuplicateFlags)
    {
        decPos->isVertexDup.resize(NumPositionIsDuplicateFlags);
        decPos->processedDupIdxArray.assign(meshCoding.getNumSplitVertex(), -1); // move to inits

        for (uint32_t i = 0; i < NumPositionIsDuplicateFlags; i++)
            decPos->isVertexDup[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_position_is_duplicate_flag]);
    }

    // Extracting and Decoding Mesh Attribute Coding Payload

    auto& macp = meshCoding.getMeshAttributeCodingPayload();
    auto& NumAttributeStart = meshCoding.getNumAttributeStart();
    for (auto i = 0; i < mch.getMeshAttributeCount(); i++) {

        if (cfg.useEntropyPacket)
        {
            //context reset for all tables from K-8  - not required for position only contexts
            // restart the ac decoding engine, switching coded buffer
            adE.stop();
            auto byteCount = macp.getMeshAttributeEntropyPacketSize()[i];
            const auto* const  bufferPtr0 = reinterpret_cast<const char*>(&macp.getMeshAttributeEntropyPacketBuffer()[i][0]);
            adE.setBuffer(byteCount, bufferPtr0);
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
            adE.start();
        }
        else
        {
// test context reset without stopping entropy coding
#if RESET_CONTEXTS_PER_ATTRIBUTE
            for (auto& ctx : ctxTbl1Trunc)
                ctx.reset();
            for (auto& ctx : ctxTbl1CoeffRemPrefix)
                ctx.reset();
            for (auto& ctx : ctxTbl1CoeffRemSuffix)
                ctx.reset();
#endif

// intermediate test
// ENFORCE_NORESET expected to be set
// to avoid bin context reset within an entropy packet
#if !ENFORCE_NORESET
            for (auto& ctx : ctxTblOthers)
                ctx.reset();
            for (auto& ctx : ctxTbl7_mesh_handle_first_vdl4m1)
                ctx.reset();
            for (auto& ctx : ctxTbl9_mesh_handle_second_vdl4m1)
                ctx.reset();

#endif
        }

        if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_TEXCOORD)
        {
            // get the attribute from the CTMesh
            // we know we created the attributes per
            // object in their enumeration order
            auto iDec = (decPos->attr->index + 1 + i < decoders.size()) ? decoders[decPos->attr->index + 1 + i] : 0;
            auto dec = dynamic_cast<UVCoordVertexAttributeDecoder*>(iDec);
            // sanity check (who knows ...)
            if (!dec)
            {
                std::cerr << "Error: invalid attribute type, expected a per vertex uv coordiante" << std::endl;
                exit(0);
            }

            dec->isUVCoords.resize(NumAttributeStart[i]);
            const auto numComponents = mch.getNumComponents(i);
            for (uint32_t j = 0; j < NumAttributeStart[i]; j++) {
                for (uint32_t k = 0; k < numComponents; k++) {
                    dec->isUVCoords[j][k] = macp.getMeshAttributeStart()[i][j][k];
                }
            }

            dec->iUVCoordsFine.resize(macp.getMeshAttributeFineResidualsCount()[i]);
            dec->iUVCoordsCoarse.resize(macp.getMeshAttributeCoarseResidualsCount()[i]);

            if (dec->iUVCoordsFine.size())
            {
                const auto maxOffset = 10;
                const auto egK = 1;
                const auto bctx = 2;
                for (auto i = 0; i < dec->iUVCoordsFine.size(); ++i) {
                    for (int32_t k = 0; k < 2; ++k) {
                        dec->iUVCoordsFine[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                            ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                            bctx - 1, MAX_CTX_COEFF_REM_PREFIX_UV - 1, MAX_CTX_COEFF_REM_SUFFIX_UV - 1);
                    }
                }
            }
            if (dec->iUVCoordsCoarse.size())
            {
                const auto maxOffset = 7;
                const auto egK = 2;
                const auto bctx = 3;
                for (auto i = 0; i < dec->iUVCoordsCoarse.size(); ++i) {
                    for (int32_t k = 0; k < 2; ++k) {
                        dec->iUVCoordsCoarse[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                            ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, 6, 6,
                            bctx - 1, MAX_CTX_COEFF_REM_PREFIX_UV - 1, MAX_CTX_COEFF_REM_SUFFIX_UV - 1);
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
                    auto& nbOrientations = mtced.getMeshTexCoordStretchOrientationsCount();
                    dec->iOrientations.resize(nbOrientations);
                    if (nbOrientations)
                    {
                        for (uint32_t j = 0; j < nbOrientations; j++)
                            dec->iOrientations[j] = adE.decode(ctxTblOthers[CtxIndex::mesh_texcoord_stretch_orientation]);

                    }
                }
            }

            // moved to after prediction residuals to align with coder side
            auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
            auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
            if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                auto& nbSeams = macp.getMeshAttributeSeamsCount()[i];
                dec->iSeams.resize(macp.getMeshAttributeSeamsCount()[i]);
                if (dec->iSeams.size()) {
                    for (uint32_t j = 0; j < nbSeams; j++)
                        // this CtxIndex::mesh_attribute_seam for ENFORCE_NORESET strict application
                        // could reuse CtxIndex::mesh_texcoord_stretch_orientation with reset in practice is reset per attribute
                        dec->iSeams[j] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                }
            }

            if (mesh_attribute_separate_index_flag) // else addedDuplicates from reference index 
            {
                auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];

                const auto& NumAttributeIsDuplicateFlags = meshCoding.getNumAttributeIsDuplicateFlags()[i];
                const auto& NumAddedDuplicatedAttribute = meshCoding.getNumAddedDuplicatedAttribute()[i];
                dec->addedDuplicates = NumAddedDuplicatedAttribute;
                if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) { 
                  auto AttributeDeduplicateCount = madi.getMeshAttributeDeduplicateCount();
                  dec->iDuplicateSplitVertexIdx.resize(madi.getMeshAttributeDeduplicateCount());
                
                  if (madi.getMeshAttributeDeduplicateCount()> 0) {
                    for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                      dec->iDuplicateSplitVertexIdx[j] = madi.getMeshAttributeDeduplicateIdx()[j];
                    }
                  } 
                } 

                // Decode uv deduplication info if uv has seperate index
                if (NumAttributeIsDuplicateFlags) {
                  dec->isVertexDup.resize(NumAttributeIsDuplicateFlags);
                  dec->processedDupIdxArray.assign(meshCoding.getNumSplitAttribute()[i], -1);// move to inits
                  for (uint32_t i = 0; i < NumAttributeIsDuplicateFlags; i++)
                    dec->isVertexDup[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                }
            }

        }
        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_GENERIC)
        {
            // get the attribute from the CTMesh
            // we know we created the attributes per
            // object in their enumeration order
            auto iDec = (decPos->attr->index + 1 + i < decoders.size()) ? decoders[decPos->attr->index + 1 + i] : 0;

            auto processGen = [&](auto&& dec) {

                dec->isGValues.resize(NumAttributeStart[i]);
                const auto numComponents = mch.getNumComponents(i);
                for (uint32_t j = 0; j < NumAttributeStart[i]; j++) {
                    for (uint32_t k = 0; k < numComponents; k++) {
                        dec->isGValues[j][k] = macp.getMeshAttributeStart()[i][j][k];
                    }
                }

                dec->iGValuesFine.resize(macp.getMeshAttributeFineResidualsCount()[i]);
                dec->iGValuesCoarse.resize(macp.getMeshAttributeCoarseResidualsCount()[i]);

// expected to be set to 1 to align code with specification
#if ENFORCE_BYPASS
                const auto ATT_GEN_NBPFXCTX = 12;
                const auto ATT_GEN_NBSFXCTX = 12;
#else
                const auto ATT_GEN_NBPFXCTX = INT_MAX;
                const auto ATT_GEN_NBSFXCTX = INT_MAX;
#endif
                if (dec->iGValuesFine.size())
                {
                    const auto maxOffset = 7;
                    const auto egK = 2;
                    const auto bctx = 2;
                    for (auto i = 0; i < dec->iGValuesFine.size(); ++i) {
                        for (auto k = 0u; k < numComponents; ++k) {
                            dec->iGValuesFine[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_GEN_NBPFXCTX, ATT_GEN_NBSFXCTX,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1);
                        }
                    }
                }
                if (dec->iGValuesCoarse.size())
                {
                    const auto maxOffset = 7;
                    const auto egK = 2;
                    const auto bctx = 3;
                    for (auto i = 0; i < dec->iGValuesCoarse.size(); ++i) {
                        for (auto k = 0u; k < numComponents; ++k) {
                            dec->iGValuesCoarse[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_GEN_NBPFXCTX, ATT_GEN_NBSFXCTX,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1, MAX_CTX_COEFF_REM_PREFIX_GEN - 1);
                        }
                    }
                }

                {
                    auto& maed = macp.getMeshAttributeExtraData()[i];
                    // no extra data for generic
                }

                // moved to after prediction residuals to align with coder side
                auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
                auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
                if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                    auto& nbSeams = macp.getMeshAttributeSeamsCount()[i];
                    dec->iSeams.resize(macp.getMeshAttributeSeamsCount()[i]);
                    if (dec->iSeams.size()) {
                        for (uint32_t j = 0; j < nbSeams; j++)
                            dec->iSeams[j] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                    }
                }

                if (mesh_attribute_separate_index_flag) // else addedDuplicates from reference index 
                {
                    auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];

                    const auto& NumAttributeIsDuplicateFlags = meshCoding.getNumAttributeIsDuplicateFlags()[i];
                    const auto& NumAddedDuplicatedAttribute = meshCoding.getNumAddedDuplicatedAttribute()[i];
                    dec->addedDuplicates = NumAddedDuplicatedAttribute;
                    if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
                        auto AttributeDeduplicateCount = madi.getMeshAttributeDeduplicateCount();
                        dec->iDuplicateSplitVertexIdx.resize(madi.getMeshAttributeDeduplicateCount());

                        if (madi.getMeshAttributeDeduplicateCount() > 0) {
                            for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                                dec->iDuplicateSplitVertexIdx[j] = madi.getMeshAttributeDeduplicateIdx()[j];
                            }
                        }
                    }

                    // Decode generic deduplication info if generic has seperate index
                    if (NumAttributeIsDuplicateFlags) {
                        dec->isVertexDup.resize(NumAttributeIsDuplicateFlags);
                        dec->processedDupIdxArray.assign(meshCoding.getNumSplitAttribute()[i], -1);// move to inits
                        for (uint32_t i = 0; i < NumAttributeIsDuplicateFlags; i++)
                            dec->isVertexDup[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                    }
                }

                };

            if (iDec)
            {
                switch (iDec->nbComp)
                {
                case 1: processGen(dynamic_cast<GenericVertexAttributeDecoder<1>*>(iDec)); break;
                case 2: processGen(dynamic_cast<GenericVertexAttributeDecoder<2>*>(iDec)); break;
                case 3: processGen(dynamic_cast<GenericVertexAttributeDecoder<3>*>(iDec)); break;
                case 4: processGen(dynamic_cast<GenericVertexAttributeDecoder<4>*>(iDec)); break;
                };
            }
            // sanity check (who knows ...)
            if (!iDec || iDec->nbComp>4 || iDec->nbComp < 1)
            {
                std::cerr << "Error: invalid attribute type, expected a per vertex generic with " << iDec->nbComp << " components" << std::endl;
                exit(0);
            }
        }
        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_NORMAL)
        {
            // get the attribute from the CTMesh
            // we know we created the attributes per
            // object in their enumeration order
            auto iDec = (decPos->attr->index + 1 + i < decoders.size()) ? decoders[decPos->attr->index + 1 + i] : 0;
            auto dec = dynamic_cast<NormalVertexAttributeDecoder*>(iDec);
            // sanity check (who knows ...)
            if (!dec)
            {
                std::cerr << "Error: invalid attribute type, expected a per vertex normals" << std::endl;
                exit(0);
            }

            dec->isNormals.resize(NumAttributeStart[i]);
            const auto numComponents = mch.getNumComponents(i);
            for (uint32_t j = 0; j < NumAttributeStart[i]; j++) {
                macp.getMeshAttributeStart()[i][j].resize(numComponents);
                for (uint32_t k = 0; k < numComponents; k++) {
                    dec->isNormals[j][k] = macp.getMeshAttributeStart()[i][j][k];
                }
            }

            if (dec->useOctahedral) {
                dec->iOctNormalsFine.resize(macp.getMeshAttributeFineResidualsCount()[i]);
                dec->iOctNormalsCoarse.resize(macp.getMeshAttributeCoarseResidualsCount()[i]);
                auto& maed = macp.getMeshAttributeExtraData()[i];
                auto& mnoed = maed.getMeshNormalOctrahedralExtraData();
                dec->qpOcta = mnoed.getMeshNormalOctrahedralBitDepthMinus1() + 1;
                cfg.normalEncodeSecondResidual = mnoed.getMeshNormalOctahedralSecondResidualFlag();
                dec->normalEncodeSecondResidual = mnoed.getMeshNormalOctahedralSecondResidualFlag();
                if (cfg.normalEncodeSecondResidual) {
                    dec->iOctSecondResiduals.resize(mnoed.getMeshNormalOctahedralSecondResidualCount());
                }
            }
            else {
                dec->iNormalsFine.resize(macp.getMeshAttributeFineResidualsCount()[i]);
                dec->iNormalsCoarse.resize(macp.getMeshAttributeCoarseResidualsCount()[i]);
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

            // using NumResidualsComponents from syntax
            // equals to NumComponents except for octahedral normal where it is 2
            int norm_dim = mch.getNumResidualsComponents(i);  // dec->useOctahedral ? 2 : numComponents;
            int norm_res_size_fine = dec->iNormalsFine.size();
            int norm_res_size_coarse = dec->iNormalsCoarse.size();

            if (dec->useOctahedral) {
                norm_res_size_fine = dec->iOctNormalsFine.size();
                norm_res_size_coarse = dec->iOctNormalsCoarse.size();
            }

            if (norm_res_size_fine > 0) {
                const auto maxOffset = 7;
                const auto egK = 5;
                const auto bctx = 2;
                for (int32_t k = 0; k < norm_dim; ++k) {
                    for (auto i = 0; i < norm_res_size_fine; ++i) {
                        if (dec->useOctahedral)
                            dec->iOctNormalsFine[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_F, ATT_NOR_NBSFXCTX_F,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_F - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                        else
                            dec->iNormalsFine[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_F, ATT_NOR_NBSFXCTX_F,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_F - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                    }
                }
            }
            if (norm_res_size_coarse > 0) {
                const auto maxOffset = 7;
                const auto egK = 1;
                const auto bctx = 1;
                for (int32_t k = 0; k < norm_dim; ++k) {  // 2D
                    for (auto i = 0; i < norm_res_size_coarse; ++i) {
                        if (dec->useOctahedral)
                            dec->iOctNormalsCoarse[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_C, ATT_NOR_NBSFXCTX_C,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_C - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                        else
                            dec->iNormalsCoarse[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                                ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_NBPFXCTX_C, ATT_NOR_NBSFXCTX_C,
                                bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_C - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR - 1);
                    }
                }
            }

            // extra data before seams and duplicates -> we could move seams and duplicates first
            // Second Residuals for Octahedral Case.
            if (dec->useOctahedral && cfg.normalEncodeSecondResidual) {
                auto& maed = macp.getMeshAttributeExtraData()[i];
                auto& mnoed = maed.getMeshNormalOctrahedralExtraData();
                const auto maxOffset = 7;
                const auto egK = 1;
                const auto bctx = 2;
                for (int32_t k = 0; k < 3; ++k) {
                    for (auto i = 0; i < dec->iOctSecondResiduals.size(); ++i) {
                        dec->iOctSecondResiduals[i][k] = adE.decodeTUExpGolombS(maxOffset, egK, ctxTbl1Sign,
                            ctxTbl1Trunc, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix, ATT_NOR_SEC_NBPFXCTX, ATT_NOR_SEC_NBSFXCTX,
                            bctx - 1, MAX_CTX_COEFF_REM_PREFIX_NOR_SEC - 1, MAX_CTX_COEFF_REM_SUFFIX_NOR_SEC - 1);
                    }
                }
            }

            // moved to after prediction residuals to align with coder side
            auto mesh_attribute_per_face_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributePerFaceFlag();
            auto mesh_attribute_separate_index_flag = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeSeparateIndexFlag();
            if (!mesh_attribute_per_face_flag && mesh_attribute_separate_index_flag) {
                auto& nbSeams = macp.getMeshAttributeSeamsCount()[i];
                dec->iSeams.resize(macp.getMeshAttributeSeamsCount()[i]);
                if (dec->iSeams.size()) {
                    for (uint32_t j = 0; j < nbSeams; j++)
                        dec->iSeams[j] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_seam]);
                }
            }

            if (mesh_attribute_separate_index_flag)
            {
                auto& madi = macp.getMeshAttributeDeduplicateInformation()[i]; // else addedDuplicates from reference index 

                const auto& NumAttributeIsDuplicateFlags = meshCoding.getNumAttributeIsDuplicateFlags()[i];
                const auto& NumAddedDuplicatedAttribute = meshCoding.getNumAddedDuplicatedAttribute()[i];
                dec->addedDuplicates = NumAddedDuplicatedAttribute;
                if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
                    auto AttributeDeduplicateCount = madi.getMeshAttributeDeduplicateCount();
                    dec->iDuplicateSplitVertexIdx.resize(madi.getMeshAttributeDeduplicateCount());

                    if (madi.getMeshAttributeDeduplicateCount() > 0) {
                        for (auto j = 0u; j < madi.getMeshAttributeDeduplicateCount(); ++j) {
                            dec->iDuplicateSplitVertexIdx[j] = madi.getMeshAttributeDeduplicateIdx()[j];
                        }
                    }
                }

                // Decode deduplication info if Nrm has seperate index
                if (NumAttributeIsDuplicateFlags) {
                    dec->isVertexDup.resize(NumAttributeIsDuplicateFlags);
                    dec->processedDupIdxArray.assign(meshCoding.getNumSplitAttribute()[i], -1);// move to inits
                    for (uint32_t i = 0; i < NumAttributeIsDuplicateFlags; i++)
                        dec->isVertexDup[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_attribute_is_duplicate_flag]);
                }
            }

        }
        else if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_MATERIAL_ID)
        {
            // get the attribute from the CTMesh
            // we know we created the attributes per
            // object in their enumeration order
            auto iDec = (decPos->attr->index + 1 + i < decoders.size()) ? decoders[decPos->attr->index + 1 + i] : 0;
            auto dec = dynamic_cast<MaterialIDFaceAttributeDecoder*>(iDec);
            // sanity check (who knows ...)
            if (!dec)
            {
                std::cerr << "Error: invalid attribute type, expected a per face material ID coordiante" << std::endl;
                exit(0);
            }
            //
            dec->iNotPredictedFaceId.resize(macp.getMeshAttributeFineResidualsCount()[i]);
            {
                auto& maed = macp.getMeshAttributeExtraData()[i];
                if ((mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_MATERIAL_ID)
                    && (mch.getMeshAttributesEncodingParameters()[i].getMeshAttributePredictionMethod()
                        == (uint8_t)MeshAttributePredictionMethod_MATERIALID::MESH_MATERIALID_DEFAULT))
                {
                    auto& mmied = maed.getMeshMaterialIDExtraData();
                    auto& nbisR = mmied.getMeshMaterialIDRCount();
                    dec->iFaceIdIsRight.resize(nbisR);

                    auto& nbisL = mmied.getMeshMaterialIDLCount();
                    dec->iFaceIdIsLeft.resize(nbisL);

                    auto& nbisF = mmied.getMeshMaterialIDFCount();
                    dec->iFaceIdIsFacing.resize(nbisF);

                    auto& nbisD = mmied.getMeshMaterialIDDCount();
                    dec->iFidsIdIsDifferent.resize(nbisD);

                    for (auto i = 0; i < dec->iNotPredictedFaceId.size(); ++i) {
                        dec->iNotPredictedFaceId[i] = adE.decodeExpGolomb(2, ctxTbl1CoeffRemPrefix, ctxTbl1CoeffRemSuffix,
                            MAX_CTX_COEFF_REM_PREFIX_MTL - 1, MAX_CTX_COEFF_REM_SUFFIX_MTL - 1);
                    }
                    // ordering must striclty follow syntax ordering when using common packet for multiple syntax elements
                    for (auto i = 0; i < dec->iFidsIdIsDifferent.size(); ++i) {
                        dec->iFidsIdIsDifferent[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_materialid_default_not_equal_flag]);
                    }
                    for (auto i = 0; i < dec->iFaceIdIsLeft.size(); ++i) {
                        dec->iFaceIdIsLeft[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_materialid_default_left_flag]);
                    }
                    for (auto i = 0; i < dec->iFaceIdIsRight.size(); ++i) {
                        dec->iFaceIdIsRight[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_materialid_default_right_flag]);
                    }
                    for (auto i = 0; i < dec->iFaceIdIsFacing.size(); ++i) {
                        dec->iFaceIdIsFacing[i] = adE.decode(ctxTblOthers[CtxIndex::mesh_materialid_default_facing_flag]);
                    }
                }
            }
        }
        else {
            std::cerr << "Warning: unsupported attribute type=" << (int)mch.getMeshAttributeType()[i] << ", skipping" << std::endl;
        }

    }

    adE.stop();

    // size info for tables init
    decPos->nV = decPos->iVerticesFine.size() + decPos->iVerticesCoarse.size() + decPos->isVertices.size() + NumAddedDuplicatedVertex; // use known added vert count
    // here we shall re-add a loop over uv attributes and do this for each
    for (auto lDec : decPos->auxiliaryDecoders) {     
        if (auto uvDec = dynamic_cast<UVCoordVertexAttributeDecoder*>(lDec)) {
            // when not hasOwnIndices, reference index is always from primary position in this version
            const auto recUv = uvDec->iUVCoordsFine.size() + uvDec->iUVCoordsCoarse.size() + uvDec->isUVCoords.size() + uvDec->addedDuplicates;
            uvDec->nUV = uvDec->attr->hasOwnIndices ? recUv : (recUv ? decPos->nV : 0);
        }
        else if (auto nrmDec = dynamic_cast<NormalVertexAttributeDecoder*>(lDec)) {
            // when not hasOwnIndices, reference index is always from primary position in this version
            const auto recNrm = nrmDec->iNormalsFine.size() + nrmDec->iNormalsCoarse.size() + nrmDec->iOctNormalsFine.size() + nrmDec->iOctNormalsCoarse.size() + nrmDec->isNormals.size() + nrmDec->addedDuplicates;
            nrmDec->nN = nrmDec->attr->hasOwnIndices ? recNrm : (recNrm ? decPos->nV : 0);
        }
        else if (lDec && lDec->attrType == eb::Attribute::Type::GENERIC) {
            auto processGen = [&](auto&& genDec) {
                // when not hasOwnIndices, reference index is always from primary position in this version
                const auto recGen = genDec->iGValuesFine.size() + genDec->iGValuesCoarse.size() + genDec->isGValues.size() + genDec->addedDuplicates;
                genDec->nGV = genDec->attr->hasOwnIndices ? recGen : (recGen ? decPos->nV : 0);
            };

            switch (lDec->nbComp)
            {
            case 1: processGen(dynamic_cast<GenericVertexAttributeDecoder<1>*>(lDec)); break;
            case 2: processGen(dynamic_cast<GenericVertexAttributeDecoder<2>*>(lDec)); break;
            case 3: processGen(dynamic_cast<GenericVertexAttributeDecoder<3>*>(lDec)); break;
            case 4: processGen(dynamic_cast<GenericVertexAttributeDecoder<4>*>(lDec)); break;
            };
        }
        // could be extended to colors when eb::Attribute::Type::COLOR when related prediction processes are defined
    }

    COUT << "  Syntax read and AC decoding time (ms) = " << elapsed(t) << std::endl;

    return true;
}

void EBReversiDecoder::ReverseUnification(Model& mesh)
{
    // TODO REWRITE for arbitrary number and type of attributes - here assumes that we have either pos+uv, pos+normal or (pos+normal)+uv !!

    std::vector<float> emptyFloat;
    std::vector<int32_t> emptyInt;

    // assuming float type
    std::vector<float>* vertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* uvcoords = nullptr;  // vertex uvcoords (x,y)
    std::vector<float>* normals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* triangles = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesuv = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesn = nullptr;  // triangle normal indices

    for (const auto& attrPtr : mesh.attributes)
    {
        // attribute values represented as 32b float in this version
        // the code should be extended to handle complete allowed quantization ranges
        // the code should also check the attribute domain
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!vertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            vertices = attr->getData<float>();
            triangles = attr->getIndices<int32_t>(); //     assumes valid - add checks
        }
        if (!uvcoords && (attr->getType() == eb::AttrType::TEXCOORD) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            uvcoords = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesuv = attr->getIndices<int32_t>();
        }
        if (!normals && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            normals = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesn = attr->getIndices<int32_t>();
        }
    }

    if (!vertices) vertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!uvcoords) uvcoords = &emptyFloat;  // vertex uvcoords (x,y
    if (!normals) normals = &emptyFloat;  // vertex normals (x,y,z)
    if (!triangles) triangles = &emptyInt;  // triangle position indices
    if (!trianglesn) trianglesn = &emptyInt;  // normal indices

  std::vector<int> posToTexture(mesh.getPositionCount(), -1);
  std::vector<int> texToPosition(uvcoords? uvcoords->size()/2: 0, -1);
  // assuming unification resulting in separate uv index
  if (!trianglesuv || ( (*trianglesuv).size() == 0 ) )
    return;
  // here assuming normals sharing position index to reproduce m67554_m67553
  const bool hasNormalsSharingPositionIndex = !normals->empty() && (trianglesn->empty() || (*trianglesn) == (*triangles));

  for (int i = 0; i < triangles->size(); i++) {
    if (posToTexture[(*triangles)[i]] == -1) {
      posToTexture[(*triangles)[i]] = (*trianglesuv)[i];
      texToPosition[(*trianglesuv)[i]] = (*triangles)[i];
    } else if (posToTexture[(*triangles)[i]] != (*trianglesuv)[i]) {
      if (texToPosition[(*trianglesuv)[i]] == -1) {
        // Build duplicated point
        int pt = (*triangles)[i];
        (*vertices).push_back((*vertices)[3 * pt + 0]);
        (*vertices).push_back((*vertices)[3 * pt + 1]);
        (*vertices).push_back((*vertices)[3 * pt + 2]);
        posToTexture.push_back((*trianglesuv)[i]);
        texToPosition[(*trianglesuv)[i]] = posToTexture.size() - 1;
        
        (*triangles)[i] = posToTexture.size() - 1;
        
        if (hasNormalsSharingPositionIndex) {
            (*normals).push_back((*normals)[3 * pt + 0]);
            (*normals).push_back((*normals)[3 * pt + 1]);
            (*normals).push_back((*normals)[3 * pt + 2]);
        }

      } else {
          bool checkNormalValue = true;
          if (hasNormalsSharingPositionIndex)
          {
              checkNormalValue = ((*normals)[3 * (*triangles)[i] + 0] == (*normals)[3 * texToPosition[(*trianglesuv)[i]] + 0]
                  && (*normals)[3 * (*triangles)[i] + 1] == (*normals)[3 * texToPosition[(*trianglesuv)[i]] + 1]
                  && (*normals)[3 * (*triangles)[i] + 2] == (*normals)[3 * texToPosition[(*trianglesuv)[i]] + 2]);
          }
        if (checkNormalValue
          && (*vertices)[3 * (*triangles)[i] + 0] == (*vertices)[3 * texToPosition[(*trianglesuv)[i]] + 0]
          && (*vertices)[3 * (*triangles)[i] + 1] == (*vertices)[3 * texToPosition[(*trianglesuv)[i]] + 1]
          && (*vertices)[3 * (*triangles)[i] + 2] == (*vertices)[3 * texToPosition[(*trianglesuv)[i]] + 2]) {
          (*triangles)[i] = texToPosition[(*trianglesuv)[i]];
        }
      }
    }
  }

  // adjust the data cnt in MeshAttribute objects (per face triplets count for position and normals)
  for (const auto& attrPtr : mesh.attributes)
  {
      eb::MeshAttribute* attr = attrPtr.get();
      if (attr->getType() == eb::AttrType::POSITION)
      {
          vertices = attr->getData<float>();
          attr->setDataCnt(vertices->size() / 3);
      }
      if (attr->getType() == eb::AttrType::NORMAL) 
      {
          normals = attr->getData<float>();
          attr->setDataCnt(normals->size() / 3);
      }
  }

}

void EBReversiDecoder::adjustRecMesh(Model& output)
{

    std::vector<float> emptyFloat;
    std::vector<int32_t> emptyInt;

    // assuming float type
    std::vector<float>* vertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* uvcoords = nullptr;  // vertex uvcoords (x,y)
    std::vector<float>* normals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* triangles = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesuv = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesn = nullptr;  // triangle normal indices

    for (const auto& attrPtr : output.attributes)
    {
        // attribute values represented as 32b float in this version
        // the code should be extended to handle complete allowed quantization ranges
        // the code should also check the attribute domain
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!vertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            vertices = attr->getData<float>();
            triangles = attr->getIndices<int32_t>(); //     assumes valid - add checks
        }
        if (!uvcoords && (attr->getType() == eb::AttrType::TEXCOORD) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            uvcoords = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesuv = attr->getIndices<int32_t>();
        }
        if (!normals && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            normals = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesn = attr->getIndices<int32_t>();
        }
    }

    if (!vertices) vertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!uvcoords) uvcoords = &emptyFloat;  // vertex uvcoords (x,y
    if (!normals) normals = &emptyFloat;  // vertex normals (x,y,z)
    if (!triangles) triangles = &emptyInt;  // triangle position indices
    if (!trianglesn) trianglesn = &emptyInt;  // normal indices

  // TODO - check decoding process with normals using position index
  // here assuming normals sharing position index to reproduce m67554_m67553
  const bool hasNormalsSharingPositionIndex = !(*normals).empty()
                && ((*trianglesn).empty() || (*trianglesn) == (*triangles));

  std::map<std::vector<int>, std::vector<int>> recPointMap;

  std::vector<int> point;
  for (int i = 0; i < output.getPositionCount(); i++) {
    if (!hasNormalsSharingPositionIndex)
      point = {(int)(*vertices)[3 * i + 0], (int)(*vertices)[3 * i + 1], (int)(*vertices)[3 * i + 2]};
    else
      point = { (int)(*vertices)[3 * i + 0], (int)(*vertices)[3 * i + 1], (int)(*vertices)[3 * i + 2],
                (int)(*normals)[3 * i + 0], (int)(*normals)[3 * i + 1], (int)(*normals)[3 * i + 2] };
    recPointMap[point].push_back(i);
  }

  std::vector<std::vector<int>> recVertexToTriangles(output.getPositionCount(), std::vector<int>(0));
  for (int i = 0; i < (*triangles).size(); i++) {
    recVertexToTriangles[(*triangles)[i]].push_back(i / 3);
  }

  //add point
  std::vector<int> tmpPoint;
  if (addPointList.size()) {
    for (int i = 0; i < addPointList.size(); i++) {
      int dupIdx = addPointList[i];
      if (!hasNormalsSharingPositionIndex)
        tmpPoint = {(int)(*vertices)[3 * dupIdx + 0], (int)(*vertices)[3 * dupIdx + 1], (int)(*vertices)[3 * dupIdx + 2]};
      else
          tmpPoint = { (int)(*vertices)[3 * dupIdx + 0], (int)(*vertices)[3 * dupIdx + 1], (int)(*vertices)[3 * dupIdx + 2],
                       (int)(*normals)[3 * dupIdx + 0], (int)(*normals)[3 * dupIdx + 1], (int)(*normals)[3 * dupIdx + 2] };
      (*vertices).push_back(tmpPoint[0]);
      (*vertices).push_back(tmpPoint[1]);
      (*vertices).push_back(tmpPoint[2]);
      if (hasNormalsSharingPositionIndex) // !!TODO this is missing in v80 -> bug check diff
      {
          (*normals).push_back(tmpPoint[3]);
          (*normals).push_back(tmpPoint[4]);
          (*normals).push_back(tmpPoint[5]);
      }
      recPointMap[tmpPoint].push_back(output.getPositionCount() - 1);
    }
  }
  //record delete point and update triangles
  if (deletePointList.size()) {
    for (int i = 0; i < deletePointList.size(); i++) {
      int delIdx = deletePointList[i];
      if (!hasNormalsSharingPositionIndex)
          tmpPoint = { (int)(*vertices)[3 * delIdx + 0], (int)(*vertices)[3 * delIdx + 1], (int)(*vertices)[3 * delIdx + 2] };
      else
          tmpPoint = { (int)(*vertices)[3 * delIdx + 0], (int)(*vertices)[3 * delIdx + 1], (int)(*vertices)[3 * delIdx + 2],
                       (int)(*normals)[3 * delIdx + 0], (int)(*normals)[3 * delIdx + 1], (int)(*normals)[3 * delIdx + 2] };
      int newPoint = recPointMap[tmpPoint][0];
      auto triangleList = recVertexToTriangles[delIdx];
      for (int j = 0; j < triangleList.size(); j++) {
        for (int k = 0; k < 3; k++) {
          if ((*triangles)[3 * triangleList[j] + k] == delIdx)
            (*triangles)[3 * triangleList[j] + k] = newPoint;
        }
      }
    }
  }

  //adjust connectivity
  if (modifyTriangles.size()) {
    for (int i = 0; i < modifyTriangles.size(); i++) {
      int mt = modifyTriangles[i];
      int mp = modifyIndex[i];
      int mi = modifyPointID[i];
      (*triangles)[3 * mt + mp] = mi;
    }
  }


  // adjust the data cnt in MeshAttribute objects (per face triplets count for position and normals)
  for (const auto& attrPtr : output.attributes)
  {
      eb::MeshAttribute* attr = attrPtr.get();
      if (attr->getType() == eb::AttrType::POSITION)
      {
          vertices = attr->getData<float>();
          attr->setDataCnt(vertices->size() / 3);
      }
      if (attr->getType() == eb::AttrType::NORMAL)
      {
          normals = attr->getData<float>();
          attr->setDataCnt(normals->size() / 3);
      }
  }
  //delete point actually
  if (deletePointList.size())
    deletePoints(output, deletePointList);

}

void EBReversiDecoder::deletePoints(Model& mesh, std::vector<int> deletePoints)
{


    std::vector<float> emptyFloat;
    std::vector<int32_t> emptyInt;

    // assuming float type
    std::vector<float>* vertices = nullptr;  // vertex positions (x,y,z)
    std::vector<float>* uvcoords = nullptr;  // vertex uvcoords (x,y)
    std::vector<float>* normals = nullptr;  // vertex normals (x,y,z)
    std::vector<int32_t>* triangles = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesuv = nullptr;  // triangle position indices
    std::vector<int32_t>* trianglesn = nullptr;  // triangle normal indices

    for (const auto& attrPtr : mesh.attributes)
    {
        // attribute values represented as 32b float in this version
        // the code should be extended to handle complete allowed quantization ranges
        // the code should also check the attribute domain
        const eb::MeshAttribute* attr = attrPtr.get();
        if (!vertices && (attr->getType() == eb::AttrType::POSITION) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            vertices = attr->getData<float>();
            triangles = attr->getIndices<int32_t>(); //     assumes valid - add checks
        }
        if (!uvcoords && (attr->getType() == eb::AttrType::TEXCOORD) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            uvcoords = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesuv = attr->getIndices<int32_t>();
        }
        if (!normals && (attr->getType() == eb::AttrType::NORMAL) && (attr->getDataType() == eb::DataType::FLOAT32))
        {
            normals = attr->getData<float>();
            if (attr->getIndexCnt())
                trianglesn = attr->getIndices<int32_t>();
        }
    }

    if (!vertices) vertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!uvcoords) uvcoords = &emptyFloat;  // vertex uvcoords (x,y
    if (!normals) normals = &emptyFloat;  // vertex normals (x,y,z)
    if (!triangles) triangles = &emptyInt;  // triangle position indices
    if (!trianglesn) trianglesn = &emptyInt;  // normal indices


  const bool hasNrm = (!(*normals).empty());
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
      if (hasNrm) {
          (*normals)[3 * slow + 0] = (*normals)[3 * i + 0];
          (*normals)[3 * slow + 1] = (*normals)[3 * i + 1];
          (*normals)[3 * slow + 2] = (*normals)[3 * i + 2];
      }
      
      slow++;
    } else {
      dp++;
    }
  }
  (*vertices).erase((*vertices).begin() + 3 * slow, (*vertices).end());
  
  if (hasNrm)
    (*normals).erase((*normals).begin() + 3 * slow, (*normals).end());
  for (int i = 0; i < (*triangles).size(); i++) {
    (*triangles)[i] = mapping[(*triangles)[i]];
  }

  // data cnt adjustment is weird this way but required for further processing MoshAttribute objects as those are currently defined
  for (const auto& attrPtr : mesh.attributes)
  {
      eb::MeshAttribute* attr = attrPtr.get();
      if (attr->getType() == eb::AttrType::POSITION)
      {
          vertices = attr->getData<float>();
          attr->setDataCnt(vertices->size() / 3);
      }
      if (attr->getType() == eb::AttrType::NORMAL)
      {
          normals = attr->getData<float>();
          attr->setDataCnt(normals->size() / 3);
      }
  }
}
