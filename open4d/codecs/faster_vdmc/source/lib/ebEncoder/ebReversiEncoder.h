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

#ifndef _EB_REVERSI_ENCODER_H_
#define _EB_REVERSI_ENCODER_H_

// internal headers
#include "ebModel.h"
#include "ebCTMesh.h"
#include "ebChrono.h"

#include "ebEncoder.h"
#include "ebBitstream.h"
#include "ebVertexTraversal.h"

#include <fstream>
#include <iomanip>
#include <stack>

namespace eb {

    class PositionVertexAttributeEncoder;

    // this class exposes the generic encoding 
    // methods, those are kind of callbacks
    // invoked during the decoding process
    // we use a simple virtual function call 
    // mechanism since those methods are not 
    // intensively invoked
    class AttributeEncoder {
    public:
        // reference to the encoder of
        // the primary attribute
        // if null the current encoder is a 
        // primary attribute encoder 
        PositionVertexAttributeEncoder* const mainEnc = 0;

        Attribute::Type   attrType = Attribute::Type::UNDEFINED;
        uint8_t nbComp = 0;

        // quantization parameter (-1 do not encode, 0 will be adjusted automatically, >1 nb bits)
        int qp = 0;

        std::vector<uint8_t> meshAttributeEntropyPacketBuffer;

        AttributeEncoder(PositionVertexAttributeEncoder* mainEnc, Attribute* attr)
            : mainEnc(mainEnc), attrType(attr->type), nbComp(attr->nbComp) {
        };
        virtual ~AttributeEncoder() {};
        // decoding pipeline methods are invoked
        // in their order of description hereafter
        // an attribute that does not need any work
        // for the step will not specialize the method

        // invoked in a first pass: initialization
        virtual void initialize(EBConfig&) {}
        // invoked in a second pass: encoding of the connectivity
        virtual void encodeConnectivity(EBConfig&) {}
        // invoked in a third pass: encoding of the attributes
        virtual void encodeAttributes(EBConfig&) {}

    protected:
        // default constructor is not authorized
        AttributeEncoder() {}
    };

    class VertexAttributeEncoder :
        public AttributeEncoder
    {
    public:

        VertexAttributeEncoder(PositionVertexAttributeEncoder* mainEnc, Attribute* attr)
            : AttributeEncoder(mainEnc, attr) {};

        // decompression variables if attribute is auxiliary

        // start corner for each connected component
        std::vector<int> ccStartCorners;

        // for separate indices table generation
        std::vector<bool> ioSeams;

        // from which unique split vertex is the duplicated point originating added
        std::vector<int>  ioDuplicateSplitVertexIdx;

        // set of processed duplicate group indices
        std::set<int>    processedDupIdx;
        // per processed vertex duplicate status
        std::vector<bool> isVertexDup;

        // contains coarse of fine indication
        std::vector<bool> ioAttrFine;

        // temporary variable when bitstream writing
        size_t attrCoarseNum = 0;


    protected:

        // implements the generic connectivity encoding 
        // for auxiliary attributes, meant to be
        // invoked by per vertex encoders
        template<class ValueType>
        void encodeAuxiliaryConnectivity(
            EBConfig& cfg, 
            AttributeVertex<ValueType>* attr);

        // implements the generic attributes encoding
        // for auxiliary attributes, meant to be
        // invoked by per vertex encoders
        template<class AuxEncoder>
        void encodeAuxiliaryAttributes(EBConfig& cfg,
            PositionVertexAttributeEncoder* mainEnc,
            AuxEncoder* auxEnc);

        // default constructor is not authorized
        VertexAttributeEncoder() {}
    };

    class FaceAttributeEncoder :
        public AttributeEncoder
    {
    public:
        FaceAttributeEncoder(PositionVertexAttributeEncoder* mainEnc, Attribute* attr)
            : AttributeEncoder(mainEnc, attr) {};
    };

    class PositionVertexAttributeEncoder :
        public VertexAttributeEncoder
    {
    public:
        // the related attribute to encode
        AttributeVertexPosition* const attr;
        // set of auxiliary attribute encoders linked 
        // to this primary attribute encoder
        std::vector<AttributeEncoder*> auxiliaryEncoders;
        // traversal object on main connectivity
        // only the main encoder shall compute the traversal
        PredictionVertexTraverser predVertexTraverser;
        // reusable traversal object for
        // auxiliary attributes having sep index table
        // can be re-computed at will
        // this makes economy of some memmory allocations
        PredictionVertexTraverser auxIndexTraverser;

        // corners in their order of visit by edgebreaker
        std::vector<int> processedCorners;
        // written corners in their order of visit by edgebreaker
        std::vector<int> writtenCorners;

        // prediction status table for aux attributes if deduplication is active
        std::vector<bool> predictCorner;

        // Vertex marking array
        std::vector<int> MV;
        // Triangles marking array
        std::vector<int> MT;

        int _T;   // curent triangle id
        int nB;   // nb of comps with boundary
        int nT;   // nb of triangles
        int nBounds = 0;  // current number of boundaries

        // CLERS table
        std::vector<char> ioClers;
        // handle corners array
        std::vector<int>  ioHandles;
        // start vertices contains absolute values
        std::vector<glm::vec3> iosVertices;
        // contains deltas or parallelogram preds
        std::vector<glm::vec3> ioVertices;

        // constructor
        PositionVertexAttributeEncoder(
            AttributeVertexPosition* attr
        ) : VertexAttributeEncoder(0, attr), attr(attr) {}

        // encoder entry points
        void initialize(EBConfig&);
        void encodeConnectivity(EBConfig&);
        void encodeAttributes(EBConfig&);

    private: // internal encoding methods

        void encodePrimaryConnectivity(EBConfig& cfg);
        void compressComponent(int _c);
        void checkHandle(const int c);
        void writeBoundary(int b);
        inline void writeVertex(const int c) {
            writtenCorners.push_back(c);
        }

        void encodePositionsWithPredition(
            EBConfig& cfg,
            std::vector<int>& traversalOrder,
            bool reverse);
        void posEncodeWithPrediction(int c);

    };

    class UVCoordVertexAttributeEncoder :
        public VertexAttributeEncoder
    {
    public:
        // the related attribute to be decoded
        AttributeVertexUVCoord* const attr;
        //
        UVCoordVertexAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeVertexUVCoord* attr
        ) :VertexAttributeEncoder(mainEnc, attr), attr(attr) {}

        // start uvcoords contains absolute values
        std::vector<glm::vec2> iosUVCoords;
        // contains deltas or min stretch
        std::vector<glm::vec2> ioUVCoords;
        // contains orientations associated to uv predictions
        std::vector<bool> ioOrientations;


        // 
        void initialize(EBConfig&)
        {
            // increase number of vertices if needed (used for MV table allocation)
            int nv = std::max(mainEnc->attr->values.size(), mainEnc->MV.size());
            // resize mutualized MV to larger size if needed
            mainEnc->MV.resize(std::max(nv, (int)(attr->values.size())));
            // reserve some memory for performance
            ioUVCoords.reserve(std::max(nv, (int)(attr->values.size())));
        }

        // invoke the templated version 
        void encodeConnectivity(EBConfig& cfg)
        {
            if (attr->hasOwnIndices || attr->refAuxIdx > 0) // could be combined with reference attribute pass when attr->refAuxIdx > 0, here possible as ct.V and auxO copied in model converter
                encodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void encodeAttributes(EBConfig& cfg) 
        {
            encodeAuxiliaryAttributes(cfg, mainEnc, this);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        void encodeWithPrediction(
            const int c,
            AttributeVertexUVCoord* attrUv,
            const std::vector<int>& tcIndices);

    private: 

        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        // the predicted UV is ADDED to the predUV parameter
        void predictUV(
            const int c,
            AttributeVertexUVCoord* attrUv,
            const std::vector<int>& tcIndices,
            glm::i64vec2& predUV);

    };

    template<int dim>
    class GenericVertexAttributeEncoder :
        public VertexAttributeEncoder
    {
    public:

        using vecN = glm::vec<dim, float>;
        using i64vecN = glm::vec<dim, int64_t>;

        // the related attribute to be decoded
        AttributeVertexGeneric<dim>* const attr;
        //
        GenericVertexAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeVertexGeneric<dim>* attr
        ) :VertexAttributeEncoder(mainEnc, attr), attr(attr) {}

        // start uvcoords contains absolute values
        std::vector<vecN> iosValues;
        // contains deltas or min stretch
        std::vector<vecN> ioValues;

        // 
        void initialize(EBConfig& cfg)
        {
            // increase number of vertices if needed (used for MV table allocation)
            int nv = std::max(mainEnc->attr->values.size(), mainEnc->MV.size());
            // resize mutualized MV to larger size if needed
            mainEnc->MV.resize(std::max(nv, (int)(attr->values.size())));
            // reserve some memory for performance
            ioValues.reserve(std::max(nv, (int)(attr->values.size())));
            // set selected prediction scheme
            genPred = cfg.genPred;
        }

        // invoke the templated version 
        void encodeConnectivity(EBConfig& cfg)
        {
            if (attr->hasOwnIndices || attr->refAuxIdx > 0) // could be combined with reference attribute pass when attr->refAuxIdx > 0, here possible as ct.V and auxO copied in model converter
                encodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void encodeAttributes(EBConfig& cfg)
        {
            encodeAuxiliaryAttributes(cfg, mainEnc, this);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        void encodeWithPrediction(
            const int c,
            AttributeVertexGeneric<dim>* attrUv,
            const std::vector<int>& tcIndices);

    private:
        EBConfig::GenPred genPred;
        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        // the predicted UV is ADDED to the predUV parameter
        void predictGenPara(
            const int c,
            AttributeVertexGeneric<dim>* attrV,
            const std::vector<int>& tcIndices,
#if ENABLE_GENERIC_FXP
            i64vecN& predGen);
#else
            vecN& predGen);
#endif
    };

    class NormalVertexAttributeEncoder :
        public VertexAttributeEncoder
    {
    public:
        // the related attribute to be decoded
        AttributeVertexNormal* const attr;
        //
        NormalVertexAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeVertexNormal* attr
        ) :VertexAttributeEncoder(mainEnc, attr), attr(attr) {}

        // start values contains absolute values (3D)
        std::vector<glm::vec3> iosNormals;
        // contains 3D prediction residuals
        std::vector<glm::vec3> ioNormals;

        // Octahedral variables
        bool useOctahedral = false;
        int qpOcta = 0; // initialized from cfg.qpOcta
        std::vector<glm::vec2> ioOctNormals;
        bool wrapAround = true;

        // 
        void initialize(EBConfig& cfg)
        {
            // increase number of vertices if needed (used for MV table allocation)
            int nv = std::max(mainEnc->attr->values.size(), mainEnc->MV.size());
            // resize mutualized MV to larger size if needed
            mainEnc->MV.resize(std::max(nv, (int)(attr->values.size())));
            // reserve some memory for performance
            iosNormals.reserve(std::max(nv, (int)(attr->values.size())));
            
            normPred = cfg.normPred;
            qpOcta = cfg.qpOcta;
            useOctahedral = cfg.useOctahedral;
            wrapAround = cfg.wrapAround;
        }

        // invoke the templated version 
        void encodeConnectivity(EBConfig& cfg)
        {
            if (attr->hasOwnIndices)
                encodeAuxiliaryConnectivity(cfg, attr);
        }

        // invoke the templated version
        void encodeAttributes(EBConfig& cfg)
        {
            encodeAuxiliaryAttributes(cfg, mainEnc, this);
        }

        // invoked by the templated method encodeAuxiliaryAttributes
        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        void encodeWithPrediction(
            const int c,
            AttributeVertexNormal* attrV,
            const std::vector<int>& attrIndices);

    private:
        EBConfig::NormPred normPred;

        // specific prediction helper
        // tcIndices might be attrPos.ct.V when using main index or attrUv.ct.V when using aux index
        // tcIndices is given as parameter to prevent additional tests inside the method
        // the predicted UV is ADDED to the predUV parameter
        void predictNormPara(
            const int c,
            AttributeVertexNormal* attrV,
            const std::vector<int>& attrIndices,
            glm::vec3& predNorm);
        void predictNormCross(
            const int c,
            glm::vec3& predNorm);

    // Octahedral Functions
    private:        
        void calculate2DResiduals(const glm::vec3 original, const glm::vec3 pred);
        void convert3Dto2Doctahedral(glm::vec3 input, glm::vec2& output);
        void convert2DoctahedralTo3D(glm::vec2 input, glm::vec3& output);
    };

    class MaterialIDFaceAttributeEncoder :
        public FaceAttributeEncoder
    {
    public:
        // the related attribute to be decoded
        AttributeFaceMaterialId* const attr;
        //
        MaterialIDFaceAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeFaceMaterialId* attr
        ) :FaceAttributeEncoder(mainEnc, attr), attr(attr) {}

        // Triangles marking array for FaceID encoding 
        // (could reuse MT form position ?)
        // we may want to have one single table for all the 
        // objects but it would prevent from runing parallel coding of objects
        std::vector<int> UF;

        // contains the face id sequence
        std::vector<int> ioFids;
        // contains non predicted face id sequence
        std::vector<int> ioNotPredictedFaceId;
        // contains face id Difference boolean sequence
        std::vector<bool> ioFidsIdIsDifferent;
        // contains face id Right prediction boolean sequence
        std::vector<bool> ioFaceIdIsRight;
        // contains face id Left prediction boolean sequence
        std::vector<bool> ioFaceIdIsLeft;
        // contains face id Facing prediction boolean sequence
        std::vector<bool> ioFaceIdIsFacing;

        // nothing to do
        // virtual void initialize(EBConfig&);
        
        //
        virtual void encodeAttributes(EBConfig&);
    };

    class EBReversiEncoder : public EBEncoder {
    public:  // methods
        EBReversiEncoder() {};
        ~EBReversiEncoder() {
            for (auto enc : encoders) {
                delete enc;
            }
            encoders.clear();
        }
        virtual void encode(const Model& input);
        virtual bool save(std::string fileName);
        virtual bool serialize(Bitstream& bs);

        virtual int getMeshCodingHeaderLength() { return headerLengthInBytes; }
        void compareMeshDifference(const Model& input, Model& rec);

    private:

        int numOfInputTriangles=0;  // tri count before any pre-processing, for bpf statistics
        int numOfInputVertices=0;   // vert count before any pre-processing, for bpv statistics

    private: // could be relocated
        // postprocessing data
        std::vector<int> faceOrder;

        // difference information between rec and original mesh
        std::vector<int> addPointList;
        std::vector<int> deletePointList;
        std::vector<int> modifyTriangles;
        std::vector<int> modifyIndex;
        std::vector<int> modifyPointID;
        int headerLengthInBytes = 0;

    private: // could be relocated
        //delete points
        void deletePoints(Model& mesh, std::vector<int> deletePoints);
        // EBBasicEncoderToDec could be defined to avoid complete encoding / decoding before mesh difference evaluation
        // previous definition would need to be adapted to latest encoding / decoding process
        // current suboptimal implementation using complete encoding / decoding
        //void EBBasicEncoderToDec();

    private: // managment of subdencoders

        // flat list of encoders, each can be primary or auxiliary
        std::vector<AttributeEncoder*> encoders;
        // subset of encoders that are primary
        std::vector<PositionVertexAttributeEncoder*> primaryEncoders;

        inline bool isPrimaryEncoder(AttributeEncoder* enc) {
            return (bool)(enc->mainEnc); // False if null pointer
        }
        
        // passive encoder does nothing
        // used to preserve alignment sync between attr and enc lists
        // for unsupported attrbutes types
        // mainEnc can be null if attribute is a primary
        int createPassiveAttributeEncoder(Attribute* attr)
        {
            PositionVertexAttributeEncoder* mainEnc = 
                attr->mainAttrIdx >= 0 ? 
                dynamic_cast<PositionVertexAttributeEncoder*>( encoders[attr->mainAttrIdx] ) : 0;
            auto enc = new AttributeEncoder(mainEnc, attr);
            encoders.push_back(enc);
            if (mainEnc) {
                mainEnc->auxiliaryEncoders.push_back(enc);
            }
            return encoders.size() - 1;
        }

        // vertex positions
        int createPositionAttributeEncoder(AttributeVertexPosition* attr) {
            primaryEncoders.push_back(new PositionVertexAttributeEncoder(attr));
            encoders.push_back(primaryEncoders.back());
            return encoders.size() - 1;
        }

        // vertex uvcoords
        int createUVCoordAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeVertexUVCoord* attr)
        {
            auto enc = new UVCoordVertexAttributeEncoder(mainEnc, attr);
            encoders.push_back(enc);
            mainEnc->auxiliaryEncoders.push_back(enc);
            return encoders.size() - 1;
        }

        // vertex normals
        int createNormalAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeVertexNormal* attr)
        {
            auto enc = new NormalVertexAttributeEncoder(mainEnc, attr);
            encoders.push_back(enc);
            mainEnc->auxiliaryEncoders.push_back(enc);
            return encoders.size() - 1;
        }

        // generic
        template<int dim>
        int createGenericAttributeEncoder(
            PositionVertexAttributeEncoder* mainDec,
            AttributeVertexGeneric<dim>* attr)
        {
            auto enc = new GenericVertexAttributeEncoder<dim>(mainDec, attr);
            encoders.push_back(enc);
            mainDec->auxiliaryEncoders.push_back(enc);
            return encoders.size() - 1;
        }

        // face ids
        int createFaceIDAttributeEncoder(
            PositionVertexAttributeEncoder* mainEnc,
            AttributeFaceMaterialId* attr)
        {
            auto enc = new MaterialIDFaceAttributeEncoder(mainEnc, attr);
            encoders.push_back(enc);
            mainEnc->auxiliaryEncoders.push_back(enc);
            return encoders.size() - 1;
        }

        // 
        void createAttributeEncoders( void ) {
            for (auto attr : _ctMesh.getAttributes()) {
                if (attr->domain == Attribute::Domain::PER_VERTEX) {
                    switch (attr->type) {
                    case Attribute::Type::POSITION: {
                        auto attrPos = static_cast<AttributeVertexPosition*>(attr);
                        createPositionAttributeEncoder(attrPos);
                    } break;
                    case Attribute::Type::UVCOORD: {
                        auto attrUv = static_cast<AttributeVertexUVCoord*>(attr);
                        createUVCoordAttributeEncoder(
                            static_cast<PositionVertexAttributeEncoder*>(encoders[attrUv->mainAttrIdx]),
                            attrUv
                        );
                    } break;
                    case Attribute::Type::NORMAL: {
                        auto attrNrm = static_cast<AttributeVertexNormal*>(attr);
                        createNormalAttributeEncoder(
                            static_cast<PositionVertexAttributeEncoder*>(encoders[attrNrm->mainAttrIdx]),
                            attrNrm
                        );
                    } break;
                    case Attribute::Type::GENERIC: {
                        const auto& dim = attr->nbComp;
                        switch (dim)
                        {
                        case 1: {
                            auto attrGen = static_cast<AttributeVertexGeneric<1>*>(attr); // make generic
                            createGenericAttributeEncoder(
                                static_cast<PositionVertexAttributeEncoder*>(encoders[attrGen->mainAttrIdx]),
                                attrGen);
                        }; break;
                        case 2: {
                            auto attrGen = static_cast<AttributeVertexGeneric<2>*>(attr); // make generic
                            createGenericAttributeEncoder(
                                static_cast<PositionVertexAttributeEncoder*>(encoders[attrGen->mainAttrIdx]),
                                attrGen);
                        }; break;
                        case 3: {
                            auto attrGen = static_cast<AttributeVertexGeneric<3>*>(attr); // make generic
                            createGenericAttributeEncoder(
                                static_cast<PositionVertexAttributeEncoder*>(encoders[attrGen->mainAttrIdx]),
                                attrGen);
                        }; break;
                        case 4: {
                            auto attrGen = static_cast<AttributeVertexGeneric<4>*>(attr); // make generic
                            createGenericAttributeEncoder(
                                static_cast<PositionVertexAttributeEncoder*>(encoders[attrGen->mainAttrIdx]),
                                attrGen);
                        }; break;
                        };
                    } break;
                    case Attribute::Type::COLOR:
                        // auto attrCol = static_cast<AttributeVertexColor*>(attr);
                        // createColorAttributeEncoder();
                        // break;
                    default:
                        // unsupported type
                        createPassiveAttributeEncoder(attr);
                        break;
                    }
                }
                else if (attr->domain == Attribute::Domain::PER_FACE) {
                    switch (attr->type) {
                    case Attribute::Type::MATERIAL_ID: {
                        auto attrMat = static_cast<AttributeFaceMaterialId*>(attr);
                        createFaceIDAttributeEncoder(
                            static_cast<PositionVertexAttributeEncoder*>(encoders[attrMat->mainAttrIdx]),
                            attrMat
                        );
                    }break;
                    default:
                        // unsupported type
                        createPassiveAttributeEncoder(attr);
                        break;
                    }
                }
                else {
                    // unsupported domain 
                    createPassiveAttributeEncoder(attr);
                }
            }
        }
    };
};  // namespace eb

#endif
