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

#ifndef _EB_REVERSI_DECODER_H_
#define _EB_REVERSI_DECODER_H_

// internal headers
#include "ebModel.h"
#include "ebCTMeshExt.h"
#include "ebDecoderBase.h"
#include "ebVertexTraversal.h"

#include <fstream>
#include <iomanip>
#include <stack>

namespace eb {

    class PositionVertexAttributeDecoder;

    // this class exposes the generic encoding 
    // methods, those are kind of callbacks
    // invoked during the decoding process
    // we use a simple virtual function call 
    // mechanism since those methods are not 
    // intensively invoked
    class AttributeDecoder {
    public:
        // reference to the decoder of
        // the primary attribute
        // if null the current decoder is a 
        // primary attribute decoder 
        PositionVertexAttributeDecoder* const mainDec = 0;

        Attribute::Type   attrType =Attribute::Type::UNDEFINED;
        uint8_t nbComp = 0;

        AttributeDecoder(PositionVertexAttributeDecoder* mainDec, Attribute* attr)
            : mainDec(mainDec), attrType(attr->type), nbComp(attr->nbComp) {
            int i = 0;
        };

        virtual ~AttributeDecoder() {}

        // decoding pipeline methods are invoked
        // in their order of description hereafter
        // an attribute that does not need any work
        // for the step will not specialize the method

        // invoked in a first pass: initialization
        virtual void initialize(EBConfig&) {}
        // invoked in a second pass: decoding of the connectivity
        virtual void decodeConnectivity(EBConfig&) {}
        // invoked in a third pass: decoding of the attributes
        virtual void decodeAttributes(EBConfig&) {}
        // invoked in an optional fourth pass: deduplicating attributes indices
        virtual void indexDeduplicatePerFan(EBConfig&) {}
        // invoked in an optional fifth pass: reindexing of the attributes
        virtual void reindexAttributes(EBConfig&) {}

    protected:
        // default constructor is not authorized
        AttributeDecoder() {}
    };

    class VertexAttributeDecoder :
        public AttributeDecoder
    {
    public:

        VertexAttributeDecoder(PositionVertexAttributeDecoder* mainDec, Attribute* attr)
            : AttributeDecoder(mainDec, attr) {
        };
        ~VertexAttributeDecoder() = default;

        // decompression variables if attribute is auxiliary

        // start corner for each connected component
        std::vector<int> ccStartCorners;

        // mesh_texcoord_stretch_orientation array
        std::vector<bool> iOrientations;
        // mesh_attribute_seam array
        std::vector<bool> iSeams;

        // index to read iOrientations
        int orientationIndex = 0;
        // index to read iSeams
        int seamsIndex = 0;

        // used for vertex deduplication
        // from which unique split vertex is the duplicated 
        // point originating added duplicateCornerIdx
        std::vector<int> iDuplicateSplitVertexIdx;
        // associates a unique duplicate index with a vertex index on 
        // first occurence of the duplicate - used to replace indices - could be vector
        std::vector<int> processedDupIdxArray;
        // list of corners for which indexing 
        // has to be fixed and the corresponding fixed vertex index
        std::vector<std::pair<int, int>> duplicates;
        // per vertex duplicated status
        std::vector<bool> isVertexDup;
        // helper from syntax 
        int addedDuplicates = 0;

    protected:

        // low level data accessors
        inline const bool readOrientation() {
            return iOrientations[orientationIndex++];  // inc after dereference
        }

        inline const bool readSeam() {
            return iSeams[seamsIndex++];  // inc after dereference
        }

        // implements the generic connectivity decoding 
        // for auxiliary attributes, meant to be
        // invoked by per vertex decoders
        template<class ValueType>
        void decodeAuxiliaryConnectivity(EBConfig& cfg, 
            AttributeVertex<ValueType>* attr);

        // implements the generic attributes decoding
        // for auxiliary attributes, meant to be
        // invoked by per vertex decoders
        template<class AuxDecoder>
        void decodeAuxiliaryAttributes(EBConfig& cfg,
            PositionVertexAttributeDecoder* mainDec,
            AuxDecoder* auxDec);

        // implements the generic post reindex 
        // for auxiliary attributes, meant to be
        // invoked by per vertex decoders
        template<class ValueType>
        void reindexAuxiliaryAttributes(EBConfig& cfg, 
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertex<ValueType>* auxAttr);

        // implements the generic index deduplication 
        // for auxiliary attributes, meant to be
        // invoked by per vertex decoders
        template<class ValueType>
        void applyIndexDeduplicatePerFan(
            EBConfig& cfg,
            AttributeVertex<ValueType>* auxAttr);

        // default constructor is not authorized
        VertexAttributeDecoder() {}
    };

    class FaceAttributeDecoder :
        public AttributeDecoder
    {
    public:
        FaceAttributeDecoder(PositionVertexAttributeDecoder* mainDec, Attribute* attr)
            : AttributeDecoder(mainDec, attr) {};
        ~FaceAttributeDecoder() = default;
    };

    class PositionVertexAttributeDecoder :
        public VertexAttributeDecoder
    {
    public:
        // the related attribute where to store 
        // to output of the decoding
        AttributeVertexPosition* const attr;
        // set of auxiliary attribute decoders linked 
        // to this primary attribute decoder
        std::vector<AttributeDecoder*> auxiliaryDecoders;

        // traversal object on main connectivity
        // only the main decoder shall compute the traversal
        PredictionVertexTraverser predVertexTraverser;
        // reusable traversal object for
        // auxiliary attributes having sep index table
        // can be re-computed at will
        // this makes economy of some memmory allocations
        PredictionVertexTraverser auxIndexTraverser;

        // data extracted from syntax
        // and accessors

        // coordinates extents
        glm::vec3 minPos={0.0,0.0,0.0};
        glm::vec3 maxPos={0.0,0.0,0.0};
        // input clers symbols
        std::vector<char> iClers;
        // handle corners array
        std::vector<int>  iHandles;
        // start vertices contains absolute values
        std::vector<glm::vec3> isVertices;
        // contains deltas or parallelogram preds
        std::vector<glm::vec3> iVerticesFine;
        // contains deltas or parallelogram preds
        std::vector<glm::vec3> iVerticesCoarse;

        // index to consume clers table
        int clersIndex = 0;
        // index to read isVertices 
        int startIndex = 0;
        // index to read iVertices 
        int deltaIndexCoarse = 0;
        // index to read iVertices 
        int deltaIndexFine = 0;

        // Decompression variables

        // reverse reconstructed clers symbols, incl. 'P'
        std::vector<char> symb;
        // current symbol
        int _T = 0;
        // current vertex
        int _N = 0;
        // Vetex marking array
        // This table can be used and enlarged by 
        // linked attributes but never shrinked
        std::vector<int> MV;
        // Corner marking array
        // can be reused by linked attributes
        std::vector<int> MC;
        // First corner of each component with boundary
        std::vector<int> FC;
        // nb of comps 
        int nC = 0 ;
        // nb of comps with boundary
        int nB = 0;
        // nb of triangles
        int nT = 0;
        // nb of vertices
        int nV = 0;
        // current number of boundaries (for easier debuging)
        int nBounds = 0;
        // corners in their order of visit
        std::vector<int> processedCorners;
        // read corners in their order of visit
        std::vector<int> readCorners;

        // skip table for attributes using main index if deduplication is active
        std::vector<int> skippedCorners;

        // constructor
        PositionVertexAttributeDecoder(
            AttributeVertexPosition* attr
        ) : VertexAttributeDecoder(0, attr), attr(attr) {}
        ~PositionVertexAttributeDecoder() = default;

        // decoder entry points

        void initialize(EBConfig&);
        void decodeConnectivity(EBConfig&);
        void decodeAttributes(EBConfig&);
        void reindexAttributes(EBConfig&);

        // invoke the templated version
        void indexDeduplicatePerFan(EBConfig& cfg)
        {
            applyIndexDeduplicatePerFan(cfg, attr);
        }

    private: // internal decoding methods

        void connectivityPrePass();
        void connectivityMainPass();
        void readBoundary(int b, int& N);
        void closeStar(int c, int N);
        // associates opposite corners
        inline void match(int c0, int c1) {
            auto& ov = attr->ct;
            ov.setO(c0, c1);
            ov.setO(c1, c0);
        }

        void decodePrimaryAttribute(
            EBConfig& cfg,
            std::vector<int>& traversalOrder,
            bool reverse); // reverse for EB traversal

        void posDecodeWithPrediction(int c);

    private: // low level data accessors

        inline char readClers() {
            return iClers[clersIndex++];   // inc after dereference
        }
        inline const glm::vec3& readStart() {
            return isVertices[startIndex++]; // inc after dereference
        }
        inline const glm::vec3& readDeltaCoarse() {
            return iVerticesCoarse[deltaIndexCoarse++];  // inc after dereference
        }
        inline const glm::vec3& readDeltaFine() {
            return iVerticesFine[deltaIndexFine++];  // inc after dereference
        }
    };

    class UVCoordVertexAttributeDecoder :
        public VertexAttributeDecoder
    {
    public:
        // the related attribute to be decoded
        AttributeVertexUVCoord* const attr;
        
        // data extracted from syntax
        // and accessors

        // bool hasSeparateUvIndex = false; // replaced by attr->hasOwnIndex()
        // coordinates extents
        glm::vec2 minUv, maxUv;
        // start uvcoords contains absolute values
        std::vector<glm::vec2> isUVCoords;
        // contains values or deltas
        std::vector<glm::vec2> iUVCoordsFine;
        // contains values or deltas
        std::vector<glm::vec2> iUVCoordsCoarse;

        // index to read isUVCoords
        int startUvIndex = 0;
        // index to read iUVCoords Coarse
        int deltaUvIndexCoarse = 0;
        // index to read iUVCoords 
        int deltaUvIndexFine = 0;

        // Decompression variables

        // nb of uv coordinates
        int nUV;

        //
        UVCoordVertexAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexUVCoord* attr
        ) :VertexAttributeDecoder(mainDec, attr), attr(attr) {
        }

        ~UVCoordVertexAttributeDecoder() = default;
 
        void initialize(EBConfig&)
        {
            attr->values.assign(nUV, glm::vec2(NAN));
            if (attr->hasOwnIndices) {
                attr->ct.V.assign(3 * mainDec->nT, -1);
                attr->ct.O.assign(3 * mainDec->nT, -3);
            }
            // We add a bit more if needed
            auto& MV = mainDec->MV;
            MV.resize(std::max(MV.size(), (size_t)nUV), 0);
        }

        // invoke the templated version
        void decodeConnectivity(EBConfig& cfg)
        {
            if ( attr->hasOwnIndices )
                decodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void decodeAttributes(EBConfig& cfg)
        {
            decodeAuxiliaryAttributes(cfg, mainDec, this);
        }

        // invoke the templated version
        void reindexAttributes(EBConfig& cfg)
        {
            reindexAuxiliaryAttributes(cfg, mainDec, attr );
        }

        // invoke the templated version
        void indexDeduplicatePerFan(EBConfig& cfg)
        {
            applyIndexDeduplicatePerFan(cfg, attr);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be _ov.V when using main index or ov.TC when using aux index
        void decodeWithPrediction(int c, const std::vector<int>& tcIndices);

    private:

        // tcIndices might be _ov.V when using main index or ov.TC when using aux index
        // the predicted UV is added to the predUV parameter
        void predictUV(const int c, const std::vector<int>& tcIndices, glm::i64vec2& predUV);

    private: // low level data accessors

        inline const glm::vec2& readUvStart() {
            return isUVCoords[startUvIndex++]; // inc after dereference
        }
        inline const glm::vec2& readUvDeltaCoarse() {
            return iUVCoordsCoarse[deltaUvIndexCoarse++];  // inc after dereference
        }
        inline const glm::vec2& readUvDeltaFine() {
            return iUVCoordsFine[deltaUvIndexFine++];  // inc after dereference
        }
    };

    template<int dim>
    class GenericVertexAttributeDecoder :
        public VertexAttributeDecoder
    {
    public:

        using vecN = glm::vec<dim, float>;
        using i64vecN = glm::vec<dim, int64_t>;

        // the related attribute to be decoded
        AttributeVertexGeneric<dim>* const attr;

        // data extracted from syntax
        // and accessors

        // bool hasSeparateUvIndex = false; // replaced by attr->hasOwnIndex()
        // coordinates extents
        vecN minGen, maxGen;
        // start uvcoords contains absolute values
        std::vector<vecN> isGValues;
        // contains values or deltas
        std::vector<vecN> iGValuesFine;
        // contains values or deltas
        std::vector<vecN> iGValuesCoarse;

        // index to read isUVCoords
        int startGenIndex = 0;
        // index to read iUVCoords Coarse
        int deltaGenIndexCoarse = 0;
        // index to read iUVCoords 
        int deltaGenIndexFine = 0;

        // Decompression variables

        // nb of generic values
        int nGV;

        //
        GenericVertexAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexGeneric<dim>* attr
        ) :VertexAttributeDecoder(mainDec, attr), attr(attr) {
        }
        ~GenericVertexAttributeDecoder() = default;

        // 
        void initialize(EBConfig&)
        {
            attr->values.assign(nGV, vecN(NAN));
            if (attr->hasOwnIndices) {
                attr->ct.V.assign(3 * mainDec->nT, -1);
                attr->ct.O.assign(3 * mainDec->nT, -3);
            }
            // We add a bit more if needed
            auto& MV = mainDec->MV;
            MV.resize(std::max(MV.size(), (size_t)nGV), 0);
        }

        // invoke the templated version
        void decodeConnectivity(EBConfig& cfg)
        {
            if (attr->hasOwnIndices)
                decodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void decodeAttributes(EBConfig& cfg)
        {
            decodeAuxiliaryAttributes(cfg, mainDec, this);
        }

        // invoke the templated version
        void reindexAttributes(EBConfig& cfg)
        {
            reindexAuxiliaryAttributes(cfg, mainDec, attr);
        }

        // invoke the templated version
        void indexDeduplicatePerFan(EBConfig& cfg)
        {
            applyIndexDeduplicatePerFan(cfg, attr);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be _ov.V when using main index or ov.TC when using aux index
        void decodeWithPrediction(int c, const std::vector<int>& tcIndices);

    private:

        // tcIndices might be _ov.V when using main index or ov.TC when using aux index
        // the predicted generic value is added to the predGen parameter
#if ENABLE_GENERIC_FXP
        void predictGenPara(const int c, const std::vector<int>& tcIndices, i64vecN& predGen);
#else
        void predictGenPara(const int c, const std::vector<int>& tcIndices, vecN& predGen);
#endif

    private: // low level data accessors

        inline const vecN& readGenStart() {
            return isGValues[startGenIndex++]; // inc after dereference
        }
        inline const vecN& readGenDeltaCoarse() {
            return iGValuesCoarse[deltaGenIndexCoarse++];  // inc after dereference
        }
        inline const vecN& readGenDeltaFine() {
            return iGValuesFine[deltaGenIndexFine++];  // inc after dereference
        }
    };

    class NormalVertexAttributeDecoder :
        public VertexAttributeDecoder
    {
    public:
        // the related attribute to be decoded
        AttributeVertexNormal* const attr;

        // data extracted from syntax
        // and accessors

        // bool hasSeparateUvIndex = false; // replaced by attr->hasOwnIndex()
        // coordinates extents
        glm::vec3 minNrm, maxNrm;
        // start uvcoords contains absolute values
        std::vector<glm::vec3> isNormals;

        // contains values or deltas
        std::vector<glm::vec3> iNormalsFine;
        // contains values or deltas
        std::vector<glm::vec3> iNormalsCoarse;

        // index to read isUVCoords
        int startNrmIndex = 0;
        // index to read iUVCoords Coarse
        int deltaNrmIndexCoarse = 0;
        // index to read iUVCoords 
        int deltaNrmIndexFine = 0;

        // Octahedral Encoder
        bool useOctahedral = false;
        bool normalEncodeSecondResidual = true;
        bool wrapAround = true;
        // contains values or deltas
        std::vector<glm::vec3> iOctSecondResiduals;
        std::vector<glm::vec2> iOctNormalsCoarse;
        std::vector<glm::vec2> iOctNormalsFine;
        int qpOcta = 0; // initialised from mnoed.getMeshNormalOctrahedralBitDepthMinus1() + 1

        // Decompression variables

        // nb of nrm coordinates
        int nN;

        //
        NormalVertexAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexNormal* attr
        ) :VertexAttributeDecoder(mainDec, attr), attr(attr) {
        }

        ~NormalVertexAttributeDecoder() = default;
        
        void initialize(EBConfig& cfg)
        {
            attr->values.assign(nN, glm::vec3(NAN));
            if (attr->hasOwnIndices) {
                attr->ct.V.assign(3 * mainDec->nT, -1);
                attr->ct.O.assign(3 * mainDec->nT, -3);
            }
            // We add a bit more if needed
            auto& MV = mainDec->MV;
            MV.resize(std::max(MV.size(), (size_t)nN), 0);
            wrapAround = cfg.wrapAround;
        }

        // invoke the templated version
        void decodeConnectivity(EBConfig& cfg)
        {
            if (attr->hasOwnIndices)
                decodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void decodeAttributes(EBConfig& cfg)
        {
            decodeAuxiliaryAttributes(cfg, mainDec, this);
        }

        // invoke the templated version
        void reindexAttributes(EBConfig& cfg)
        {
            reindexAuxiliaryAttributes(cfg, mainDec, attr);
        }

        // invoke the templated version
        void indexDeduplicatePerFan(EBConfig& cfg)
        {
            //if (!attr->hasOwnIndices)
            //    mainDec
            applyIndexDeduplicatePerFan(cfg, attr);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be _ov.V when using main index or ov.TC when using aux index
        void decodeWithPrediction(int c, const std::vector<int>& tcIndices);

    private:

        // attrIndices might be _ov.V when using main index or ov.TC when using aux index
        // the predicted Normal is added to the predNorm parameter
        void predictNormPara(const int c, const std::vector<int>& attrIndices, glm::vec3& predNorm);
        void predictNormCross(const int c, glm::vec3& predNorm);

    private: // low level data accessors

        inline const glm::vec3& readNrmStart() {
            return isNormals[startNrmIndex++]; // inc after dereference
        }
        inline const glm::vec3& readNrmDeltaCoarse() {
            return iNormalsCoarse[deltaNrmIndexCoarse++];  // inc after dereference
        }
        inline const glm::vec3& readNrmDeltaFine() {
            return iNormalsFine[deltaNrmIndexFine++];  // inc after dereference
        }

        // use a dedicated encoder ?
    private:
        // index to read octo residuals Coarse
        int octaNrmIndexCoarse = 0;
        // index to read  octo residuals Fine 
        int octaNrmIndexFine = 0;
        void decodeOctahedral(const glm::vec3 pred, glm::vec3& rec, const bool fine);
        void convert3Dto2Doctahedral(glm::vec3 input, glm::vec2& output);
        void convert2DoctahedralTo3D(glm::vec2 input, glm::vec3& output);
        inline const glm::vec2& readNrmOctaCoarse() {
            return iOctNormalsCoarse[octaNrmIndexCoarse++];  // inc after dereference
        }
        inline const glm::vec2& readNrmOctaFine() {
            return iOctNormalsFine[octaNrmIndexFine++];  // inc after dereference
        }
        int curNrmOctSecRes = 0;                               // index to read secondResiduals
        inline const glm::vec3& readOctsecondResiduals() {
            return iOctSecondResiduals[curNrmOctSecRes++];     // inc after dereference
        }
    };

    class MaterialIDFaceAttributeDecoder :
        public FaceAttributeDecoder
    {
    public:
        // the related attribute to be decoded
        AttributeFaceMaterialId* const attr;
        //
        MaterialIDFaceAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeFaceMaterialId* attr
        ) :FaceAttributeDecoder(mainDec, attr), attr(attr) {}
        ~MaterialIDFaceAttributeDecoder() = default;

        // data extracted from syntax
        // and accessors

        // contains non predicted face id sequence
        std::vector<int>  iNotPredictedFaceId;
        // contains face id Right prediction boolean sequence
        std::vector<bool> iFaceIdIsRight;
        // contains face id Left prediction boolean sequence
        std::vector<bool> iFaceIdIsLeft;
        // contains face id Facing prediction boolean sequence
        std::vector<bool> iFaceIdIsFacing;
        // contains face id Difference boolean sequence
        std::vector<bool> iFidsIdIsDifferent;

        // 
        virtual void initialize(EBConfig&)
        {
            attr->values.assign(mainDec->nT, -1);
        }
        //
        virtual void decodeAttributes(EBConfig&);
    };

    class EBReversiDecoder : public EBDecoderBase {

    public: // methods

        EBReversiDecoder() {};
        ~EBReversiDecoder() {
            for (auto dec : decoders) {
                delete dec;
            }
            decoders.clear();
        };

        virtual bool unserialize(MeshCoding& meshCoding);
        virtual bool decode(Model& output);

    private: // internal processing methods

        // dequantizes the CTModel
        void dequantize(Model& output);

    private:
        // TO RELOCATE ??
        void ReverseUnification(Model& decoded);
        void adjustRecMesh(Model& output);
        void deletePoints(Model& mesh, std::vector<int> deletePoints);

    private: // managment of subdecoders

        // flat list of decoders, each can be primary or auxiliary
        std::vector<AttributeDecoder*> decoders;
        // subset of decoders that are primary
        std::vector<PositionVertexAttributeDecoder*> primaryDecoders;

        inline bool isPrimaryDecoder(AttributeDecoder* dec) {
            return (bool)(dec->mainDec); // False if null pointer
        }

        // passive decoder does nothing
        // used to preserve alignment sync between attr and enc lists
        // for unsupported attrbutes types
        // mainEnc can be null if attribute is a primary
        int createPassiveAttributeDecoder(Attribute* attr)
        {
            PositionVertexAttributeDecoder* mainDec =
                attr->mainAttrIdx >= 0 ?
                dynamic_cast<PositionVertexAttributeDecoder*>(decoders[attr->mainAttrIdx]) : 0;
            auto enc = new AttributeDecoder(mainDec, attr);
            decoders.push_back(enc);
            if (mainDec) {
                mainDec->auxiliaryDecoders.push_back(enc);
            }
            return decoders.size() - 1;
        }

        // vertex positions
        int createPositionAttributeDecoder(AttributeVertexPosition* attr) {
            primaryDecoders.push_back(new PositionVertexAttributeDecoder(attr));
            decoders.push_back(primaryDecoders.back());
            return decoders.size() - 1;
        }

        // vertex uvcoords
        int createUVCoordAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexUVCoord* attr)
        {
            auto dec = new UVCoordVertexAttributeDecoder(mainDec, attr);
            decoders.push_back(dec);
            mainDec->auxiliaryDecoders.push_back(dec);
            return decoders.size() - 1;
        }

        // vertex normals
        int createNormalAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexNormal* attr)
        {
            auto dec = new NormalVertexAttributeDecoder(mainDec, attr);
            decoders.push_back(dec);
            mainDec->auxiliaryDecoders.push_back(dec);
            return decoders.size() - 1;
        }

        // generic
        template<int dim>
        int createGenericAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertexGeneric<dim>* attr)
        {
            auto dec = new GenericVertexAttributeDecoder<dim>(mainDec, attr);
            decoders.push_back(dec);
            mainDec->auxiliaryDecoders.push_back(dec);
            return decoders.size() - 1;
        }

        inline int createGenericAttributeDecoderN(int dim,
            PositionVertexAttributeDecoder* mainDec,
            AttributeVertex<float>* attr) // restricted to float values in current implementation, could be extended to other data types
        {
            switch (dim)
            {
            case 1: return createGenericAttributeDecoder<1>(mainDec, reinterpret_cast<AttributeVertexGeneric<1>*>(attr)); break;
            case 2: return createGenericAttributeDecoder<2>(mainDec, reinterpret_cast<AttributeVertexGeneric<2>*>(attr)); break;
            case 3: return createGenericAttributeDecoder<3>(mainDec, reinterpret_cast<AttributeVertexGeneric<3>*>(attr)); break;
            case 4: return createGenericAttributeDecoder<4>(mainDec, reinterpret_cast<AttributeVertexGeneric<4>*>(attr)); break;
            default: std::cout << "Error : generic dimension beyond current code capability" << std::endl; exit(0);
            }
            return -1; // never occurs
        }

        // face ids
        int createFaceIDAttributeDecoder(
            PositionVertexAttributeDecoder* mainDec,
            AttributeFaceMaterialId* attr)
        {
            auto dec = new MaterialIDFaceAttributeDecoder(mainDec, attr);
            decoders.push_back(dec);
            mainDec->auxiliaryDecoders.push_back(dec);
            return decoders.size() - 1;
        }

    private:
        // diffrence information between rec and original mesh - TO RELOCATE ??!
        std::vector<int> addPointList;
        std::vector<int> deletePointList;
        std::vector<int> modifyTriangles;
        std::vector<int> modifyIndex;
        std::vector<int> modifyPointID;
    };
};  // namespace mm

#endif
