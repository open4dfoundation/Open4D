/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2023, ISO/IEC
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
#include "ebBitstream.h"
#include "ebReader.hpp"
#include "meshCoding.hpp"
#include "meshCodingHeader.hpp"
#include "meshPositionEncodingParameters.hpp"
#include "meshPositionDequantizeParameters.hpp"
#include "meshAttributesEncodingParameters.hpp"
#include "meshAttributesDequantizeParameters.hpp"
#include "meshPositionCodingPayload.hpp"
#include "meshPositionDeduplicateInformation.hpp"
#include "meshAttributeCodingPayload.hpp"
#include "meshAttributeExtraData.hpp"
#include "meshTexCoordStretchExtraData.hpp"
#include "meshMaterialIDExtraData.hpp"
#include "meshAttributeDeduplicateInformation.hpp"
#include "meshNormalOctrahedralExtraData.hpp"

using namespace eb;

#if defined(BITSTREAM_TRACE)
static std::string
format(const std::string& var) {
  std::string substr = var;
  auto        npos   = substr.find("get");
  if (npos != std::string::npos) substr = substr.substr(npos + 3);
  npos = substr.find("(");
  if (npos != std::string::npos) substr = substr.substr(0, npos);
  return substr;
}

#  define READ_CODE(VAR, LEN) \
    { \
      VAR = bitstream.read(LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define READ_CODE_CAST(VAR, LEN, CAST) \
    { \
      VAR = static_cast<CAST>(bitstream.read(LEN)); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define READ_CODES(VAR, LEN) \
    { \
      VAR = bitstream.readS(LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "s(" + std::to_string(LEN) + ")"); \
    }

#  define READ_UVLC(VAR) \
    { \
      VAR = bitstream.readUvlc(); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }
#  define READ_UVLC_CAST(VAR, CAST) \
    { \
      VAR = static_cast<CAST>(bitstream.readUvlc()); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }

#  define READ_SVLC(VAR) \
    { \
      VAR = bitstream.readSvlc(); \
      bitstream.traceBit(format(#VAR), VAR, "se(v)"); \
    }

#  define READ_FLOAT(VAR) \
    { \
      VAR = bitstream.readFloat(); \
      bitstream.traceBitFloat(format(#VAR), VAR, "f(32)"); \
    }

#  define READ_STRING(VAR) \
    { \
      VAR = bitstream.readString(); \
      bitstream.traceBitStr(format(#VAR), VAR, "str()"); \
    }

#  define READ_VU(VAR) \
    { \
      VAR = bitstream.readVu(); \
      bitstream.traceBit(format(#VAR), VAR, "vu(v)"); \
    }

#  define READ_VI(VAR) \
    { \
      VAR = bitstream.readVi(); \
      bitstream.traceBit(format(#VAR), VAR, "vi(v)"); \
    }

#  define READ_VECTOR(VAR, SIZE) \
    { \
      bitstream.read(VAR, SIZE); \
      bitstream.trace( \
        "Data stream: %s size = %zu \n", format(#VAR).c_str(), VAR.size()); \
    }
#  define READ_VIDEO(VAR, SIZE) \
    { \
      bitstream.read(VAR, SIZE); \
      bitstream.trace("Video stream: type = %s size = %zu \n", \
                      toString(VAR.type()).c_str(), \
                      SIZE); \
    }
#else
#  define READ_CODE(VAR, LEN) VAR = bitstream.read(LEN);
#  define READ_CODE_CAST(VAR, LEN, CAST) \
    VAR = static_cast<CAST>(bitstream.read(LEN));
#  define READ_CODES(VAR, LEN) VAR = bitstream.readS(LEN);
#  define READ_UVLC(VAR) VAR = bitstream.readUvlc();
#  define READ_UVLC_CAST(VAR, CAST) \
    VAR = static_cast<CAST>(bitstream.readUvlc());
#  define READ_SVLC(VAR) VAR = bitstream.readSvlc();
#  define READ_FLOAT(VAR) VAR = bitstream.readFloat();
#  define READ_STRING(VAR) VAR = bitstream.readString();
#  define READ_VU(VAR) VAR = bitstream.readVu();
#  define READ_VI(VAR) VAR = bitstream.readVi();
#  define READ_VECTOR(VAR, SIZE) bitstream.read(VAR, SIZE);
#  define READ_VIDEO(VAR, SIZE) bitstream.read(VAR, SIZE);
#endif

EbReader::EbReader() {}
EbReader::~EbReader() = default;

void
EbReader::read(Bitstream& bitstream, MeshCoding& mc) {
  TRACE_BITSTREAM_IN("%s", __func__);
  setGlobal(mc);
  meshCoding(bitstream, mc);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.1 General Mesh coding syntax
void
EbReader::meshCoding(Bitstream& bitstream, MeshCoding& mc) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& mch  = mc.getMeshCodingHeader();
  auto& macp = mc.getMeshAttributeCodingPayload();
  auto& mpcp = mc.getMeshPositionCodingPayload();
  meshCodingHeader(bitstream, mch);
  meshPositionCodingPayload(bitstream, mpcp, mch);
  meshAttributeCodingPayload(bitstream, macp, mch);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.2 Mesh coding header syntax
void
EbReader::meshCodingHeader(Bitstream& bitstream, MeshCodingHeader& mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE_CAST(mch.getMeshCodecType(), 2, MeshCodecType);  // u(2)
  READ_CODE_CAST(mch.getMeshVertexTraversalMethod(), 2, MeshVertexTraversalMethod);  // u(2)
  meshPositionEncodingParameters(bitstream,
                                 mch.getMeshPositionEncodingParameters());
  READ_CODE(mch.getMeshPositionDequantizeFlag(), 1);  // u(1)

  if (mch.getMeshPositionDequantizeFlag()) {
    meshPositionDequantizeParameters(
      bitstream, mch.getMeshPositionDequantizeParameters());
  }
  READ_CODE(mch.getMeshEntropyPacketFlag(), 1);  // u(1)
  READ_CODE(mch.getMeshAttributeCount(), 5);  // u(5)

  mch.allocateAttributes(mch.getMeshAttributeCount());
  for (uint32_t i = 0; i < mch.getMeshAttributeCount(); i++) {
    READ_CODE_CAST(mch.getMeshAttributeType()[i], 3, MeshAttributeType);  // u(3)
    if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_NORMAL) {
      READ_CODE(mch.getMeshNormalOctahedralFlag()[i], 1);  // u(1) 
    }
    if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_GENERIC) {
      READ_CODE(mch.getMeshAttributeNumComponentsMinus1()[i], 2);  // u(2)
    }
    meshAttributesEncodingParameters(
      bitstream, mch.getMeshAttributesEncodingParameters()[i]);
    READ_CODE(mch.getMeshAttributeDequantizeFlag()[i], 1);  // u(1)
    if (mch.getMeshAttributeDequantizeFlag()[i])
      meshAttributesDequantizeParameters(bitstream, mch, i);
  }
  
  READ_UVLC_CAST(mch.getMeshDeduplicateMethod(), MeshDeduplicateMethod);  // ue(v)
  READ_CODE_CAST(mch.getMeshReindexOutput(), 2, MeshVertexTraversalMethod); //u(1)
  padding_to_byte_alignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.3 Mesh position encoding parameters syntax
void
EbReader::meshPositionEncodingParameters(
  Bitstream&                      bitstream,
  MeshPositionEncodingParameters& mpep) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(mpep.getMeshPositionBitDepthMinus1(), 5);           // u(5)
  READ_UVLC_CAST(mpep.getMeshPositionPredictionMethod(),        MeshPositionPredictionMethod);         // ue(v)
  READ_CODE(mpep.getMeshPositionReverseUnificationFlag(), 1);  // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.4 Mesh position dequantize parameters syntax
void
EbReader::meshPositionDequantizeParameters(
  Bitstream&                        bitstream,
  MeshPositionDequantizeParameters& mpdp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  for (uint32_t i = 0; i < 3; i++) {
    READ_FLOAT(mpdp.getMeshPositionMin(i));  // fl(32)
    READ_FLOAT(mpdp.getMeshPositionMax(i));  // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.5 Mesh attributes encoding parameters syntax
void
EbReader::meshAttributesEncodingParameters(
  Bitstream&                        bitstream,
  MeshAttributesEncodingParameters& maep) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(maep.getMeshAttributeBitDepthMinus1(), 5);        // u(5)
  READ_CODE(maep.getMeshAttributePerFaceFlag(), 1);           // u(1)
  if (!maep.getMeshAttributePerFaceFlag()) {
    READ_CODE(maep.getMeshAttributeSeparateIndexFlag(), 1);   // u(1)
    if (!maep.getMeshAttributeSeparateIndexFlag())
      READ_UVLC(maep.getMeshAttributeReferenceIndexPlus1());  // ue(v)
  }
  READ_UVLC(maep.getMeshAttributePredictionMethod());         // ue(v)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.6 Mesh attributes dequantize parameters syntax
void
EbReader::meshAttributesDequantizeParameters(Bitstream&        bitstream,
                                             MeshCodingHeader& mch,
                                             uint32_t          index) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& madp = mch.getMeshAttributesDequantizeParameters(index);
  for (uint32_t i = 0; i < mch.getNumComponents(index); i++) {
    READ_FLOAT(madp.getMeshAttributeMin(i));  // fl(32)
    READ_FLOAT(madp.getMeshAttributeMax(i));  // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.7 Mesh position coding payload syntax
void
EbReader::meshPositionCodingPayload(Bitstream&                 bitstream,
                                    MeshPositionCodingPayload& mpcp,
                                    MeshCodingHeader&          mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& NumHandles =       getGlobal().getNumHandles();
  auto& NumPositionStart = getGlobal().getNumPositionStart();

  READ_VU(mpcp.getMeshTriangleCount());                      // vu(v)
  READ_VU(mpcp.getMeshPositionStartCount());                 // vu(v)
  READ_VU(mpcp.getMeshPositionFineResidualsCount());         // vu(v)
  READ_VU(mpcp.getMeshPositionCoarseResidualsCount());       // vu(v)
  READ_VU(mpcp.getMeshClersCount());                         // vu(v)
  READ_VU(mpcp.getMeshCcWithBoundaryCount());                // vu(v)
  READ_VU(mpcp.getMeshHandlesCount());                       // vu(v)

  // rename to first entropy packet size -> unique, or is geo and others are 1 per attrib
  READ_VU(mch.getMeshGeoEntropyPacketBufferSize());    // vu(v)
  READ_VECTOR(mch.getMeshGeoEntropyPacketBuffer(),     // ae(v) packet - byte aligned
        mch.getMeshGeoEntropyPacketBufferSize());

  const int MinHandles = MIN_HANDLE; // MinHandles = 10 in syntax
  NumHandles = mpcp.getMeshHandlesCount();
  if (NumHandles < MinHandles) {
      auto& meshHandleFirstDelta = mpcp.getMeshHandleFirstDelta();
      auto& meshHandleSecondDelta = mpcp.getMeshHandleSecondDelta();
      meshHandleFirstDelta.resize(NumHandles);
      meshHandleSecondDelta.resize(NumHandles);
      for (size_t i = 0; i < NumHandles; i++) {
          READ_VI(meshHandleFirstDelta[i]);     // vi(v)
          READ_VI(meshHandleSecondDelta[i]);    // vi(v)
      }
  }

  // mesh_clers_symbol ae(v) in entropy packet

  NumPositionStart = mpcp.getMeshPositionStartCount();
  auto& mpep = mch.getMeshPositionEncodingParameters();
  auto& meshPositionStart = mpcp.getMeshPositionStart();
  meshPositionStart.resize(NumPositionStart);
  for (uint32_t i = 0; i < NumPositionStart; i++) {
      meshPositionStart[i].resize(3);
      for (uint32_t j = 0; j < 3; j++) {
          READ_CODE(meshPositionStart[i][j],
                mpep.getMeshPositionBitDepthMinus1() + 1);      // u(v)
      }
  }
  padding_to_byte_alignment(bitstream);

  auto& NumPredictedFinePositions = getGlobal().getNumPredictedFinePositions();
  NumPredictedFinePositions = mpcp.getMeshPositionFineResidualsCount();

  // mesh_position_fine_residual ae(v) in entropy packet

  auto& NumPredictedCoarsePositions = getGlobal().getNumPredictedCoarsePositions();
  NumPredictedCoarsePositions = mpcp.getMeshPositionCoarseResidualsCount();

  // mesh_position_coarse_residual ae(v) in entropy packet

  auto& mpdi = mpcp.getMeshPositionDeduplicateInformation();
  meshPositionDeduplicateInformation(bitstream, mpdi, mch);

  if (mpep.getMeshPositionReverseUnificationFlag()) {
    auto& mdi = mpcp.getMeshDifferenceInformation();
    meshDifferenceInformation(bitstream, mdi);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.8 Mesh position deduplicate information syntax
void
EbReader::meshPositionDeduplicateInformation(
  Bitstream&                          bitstream,
  MeshPositionDeduplicateInformation& mpdi,
  MeshCodingHeader&                   mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& NumPositionStart            = getGlobal().getNumPositionStart();
  auto& NumPredictedFinePositions   = getGlobal().getNumPredictedFinePositions();
  auto& NumPredictedCoarsePositions = getGlobal().getNumPredictedCoarsePositions();
  if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
    READ_VU(mpdi.getMeshPositionDeduplicateCount());  //vu(v)
    if( mpdi.getMeshPositionDeduplicateCount() > 0 ){
      auto& NumPositionIsDuplicateFlags = getGlobal().getNumPositionIsDuplicateFlags();
      auto& NumSplitVertex = getGlobal().getNumSplitVertex();
      auto& NumAddedDuplicatedVertex = getGlobal().getNumAddedDuplicatedVertex();

      auto& meshPositionDeduplicateIdx = mpdi.getMeshPositionDeduplicateIdx();
      meshPositionDeduplicateIdx.resize(mpdi.getMeshPositionDeduplicateCount());
      for (uint32_t i = 0; i < mpdi.getMeshPositionDeduplicateCount(); i++) {
        READ_VU(meshPositionDeduplicateIdx[i]);  //vu(v)
        NumSplitVertex = std::max(NumSplitVertex, meshPositionDeduplicateIdx[i] + 1);
      }
      NumAddedDuplicatedVertex = mpdi.getMeshPositionDeduplicateCount() - NumSplitVertex; // is required as global ?
      NumPositionIsDuplicateFlags = NumPositionStart + NumPredictedFinePositions
          + NumPredictedCoarsePositions + NumAddedDuplicatedVertex;
      // mesh_position_is_duplicate_flag ae(v) in entropy packet
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.9 Mesh difference information syntax
void EbReader::meshDifferenceInformation(Bitstream& bitstream, MeshDifferenceInformation& mdi)
{
    TRACE_BITSTREAM_IN("%s", __func__);
    READ_VU(mdi.getMeshAddPointCount());  //	vu(v)
    if (mdi.getMeshAddPointCount() > 0) {
        auto& AddPointIdxDelta = mdi.getAddPointIdxDelta();
        AddPointIdxDelta.resize(mdi.getMeshAddPointCount());
        for (uint32_t i = 0; i < mdi.getMeshAddPointCount(); i++) {
            READ_VI(AddPointIdxDelta[i]);  //vi(v)
        }
    }

    READ_VU(mdi.getMeshDeletePointCount());  //	vu(v)
    if (mdi.getMeshDeletePointCount() > 0) {
        auto& DeletePointIdxDelta = mdi.getDeletePointIdxDelta();
        DeletePointIdxDelta.resize(mdi.getMeshDeletePointCount());
        for (uint32_t i = 0; i < mdi.getMeshDeletePointCount(); i++) {
            READ_VI(DeletePointIdxDelta[i]);  //vi(v)
        }
    }

    READ_VU(mdi.getModifyMeshPointCount());  //	vu(v)
    if (mdi.getModifyMeshPointCount() > 0) {
        auto& ModifyTriangleIdxDelta = mdi.getModifyTrianglesDelta();
        ModifyTriangleIdxDelta.resize(mdi.getModifyMeshPointCount());
        for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
            READ_VI(ModifyTriangleIdxDelta[i]);  //vi(v)
        }

        auto& ModifyIdx = mdi.getModifyIdx();
        ModifyIdx.resize(mdi.getModifyMeshPointCount());
        for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
            READ_VU(ModifyIdx[i]);  //vu(v)
        }

        auto& ModifyPointIdxDelta = mdi.getModifyPointIdxDelta();
        ModifyPointIdxDelta.resize(mdi.getModifyMeshPointCount());
        for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
            READ_VI(ModifyPointIdxDelta[i]);  //vi(v)
        }
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.10 Mesh attribute coding payload syntax
void
EbReader::meshAttributeCodingPayload(Bitstream&                  bitstream,
                                     MeshAttributeCodingPayload& macp,
                                     MeshCodingHeader&           mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& NumPositionStart = getGlobal().getNumPositionStart();
  auto& NumAttributeStart = getGlobal().getNumAttributeStart();
  NumAttributeStart.resize(mch.getMeshAttributeCount());

  auto& NumAttributeIsDuplicateFlags = getGlobal().getNumAttributeIsDuplicateFlags();
  auto& NumSplitAttribute = getGlobal().getNumSplitAttribute();
  auto& NumAddedDuplicatedAttribute = getGlobal().getNumAddedDuplicatedAttribute();

  NumAttributeIsDuplicateFlags.resize(mch.getMeshAttributeCount()); 
  NumSplitAttribute.resize(mch.getMeshAttributeCount());
  NumAddedDuplicatedAttribute.resize(mch.getMeshAttributeCount());

  macp.allocate(mch.getMeshAttributeCount());
  for (uint32_t i = 0; i < mch.getMeshAttributeCount(); i++) {

    READ_VU(macp.getMeshAttributeStartCount()[i]);           // vu(v)
    READ_VU(macp.getMeshAttributeFineResidualsCount()[i]);   // vu(v)
    READ_VU(macp.getMeshAttributeCoarseResidualsCount()[i]); // vu(v)
    if (mch.getMeshEntropyPacketFlag()) { // use a global here ?
        READ_VU(macp.getMeshAttributeEntropyPacketSize()[i]);       // vu(v)
        READ_VECTOR(macp.getMeshAttributeEntropyPacketBuffer()[i],  // ae(v)
            macp.getMeshAttributeEntropyPacketSize()[i]);
    }
    auto& maep          = mch.getMeshAttributesEncodingParameters(i);
    auto  numComponents = mch.getNumComponents(i);
    auto  bitDepth      = maep.getMeshAttributeBitDepthMinus1() + 1;
    NumAttributeStart[i] = macp.getMeshAttributeStartCount()[i];
    auto& meshAttributeStart = macp.getMeshAttributeStart()[i];
    meshAttributeStart.resize(NumAttributeStart[i]); 
    for (uint32_t j = 0; j < NumAttributeStart[i]; j++) {
      meshAttributeStart[j].resize(numComponents);
      for (uint32_t k = 0; k < numComponents; k++) {
        READ_CODE(meshAttributeStart[j][k], bitDepth)  // u(v)
      }
    }
    padding_to_byte_alignment(bitstream);

    /*	extra data dependent on the selected prediction scheme	*/
    auto& maed = macp.getMeshAttributeExtraData()[i];
    meshAttributeExtraData(bitstream, maed, mch, maep, i);

    if (maep.getMeshAttributeSeparateIndexFlag()) {
      READ_VU(macp.getMeshAttributeSeamsCount()[i]);         // vu(v)
      auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];
      meshAttributeDeduplicateInformation(bitstream, madi, mch, i);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.11 Mesh extra attribute data syntax
void
EbReader::meshAttributeExtraData(Bitstream&                        bitstream,
                                 MeshAttributeExtraData&           maed,
                                 MeshCodingHeader&                 mch,
                                 MeshAttributesEncodingParameters& maep,
                                 uint32_t                          index) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto type = mch.getMeshAttributeType()[index];
  if (type == MeshAttributeType::MESH_ATTR_TEXCOORD) {
      if (maep.getMeshAttributePredictionMethod() == (uint8_t)MeshAttributePredictionMethod_TEXCOORD::MESH_TEXCOORD_STRETCH) {
          meshTexCoordStretchExtraData(bitstream,
              maed.getMeshTexCoordStretchExtraData());
      }
  } else if (type == MeshAttributeType::MESH_ATTR_NORMAL) {
      if (mch.getMeshNormalOctahedralFlag()[index]) {
          meshNormalOctrahedralExtraData(bitstream, maed.getMeshNormalOctrahedralExtraData());
      }
  } else if (type == MeshAttributeType::MESH_ATTR_COLOR) {
      /* No extra data defined for specified prediction methods applied on colors */
  } else if (type == MeshAttributeType::MESH_ATTR_MATERIAL_ID) {
      if (maep.getMeshAttributePredictionMethod() == (uint8_t)MeshAttributePredictionMethod_MATERIALID::MESH_MATERIALID_DEFAULT) {
          meshMaterialIDExtraData(bitstream,
              maed.getMeshMaterialIDExtraData());
      }
  } else if (type == MeshAttributeType::MESH_ATTR_GENERIC) {
      /* No extra data defined for specified prediction methods applied on generic*/
  }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.12 Mesh texcoord stretch extra data syntax
void
EbReader::meshTexCoordStretchExtraData(Bitstream&                    bitstream,
                                       MeshTexCoordStretchExtraData& mtcsed) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_VU(mtcsed.getMeshTexCoordStretchOrientationsCount());  // vu(v)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.13 Mesh materialid default extra data syntax
void
EbReader::meshMaterialIDExtraData(Bitstream& bitstream,
                                  MeshMaterialIDExtraData& mmied) {
    TRACE_BITSTREAM_IN("%s", __func__);
    READ_VU(mmied.getMeshMaterialIDDCount());  // vu(v)
    READ_VU(mmied.getMeshMaterialIDLCount());  // vu(v)
    READ_VU(mmied.getMeshMaterialIDRCount());  // vu(v)
    READ_VU(mmied.getMeshMaterialIDFCount());  // vu(v)
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.14 Mesh normal octahedral extra data syntax
void
EbReader::meshNormalOctrahedralExtraData(Bitstream& bitstream, MeshNormalOctrahedralExtraData& mnoed) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(mnoed.getMeshNormalOctrahedralBitDepthMinus1(), 5);        // u(5)
  READ_CODE(mnoed.getMeshNormalOctahedralSecondResidualFlag(), 1);     // u(1)
  padding_to_byte_alignment(bitstream);
  if (mnoed.getMeshNormalOctahedralSecondResidualFlag()) {
    READ_VU(mnoed.getMeshNormalOctahedralSecondResidualCount());         // vu(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.15 Mesh attribute deduplicate information syntax
void
EbReader::meshAttributeDeduplicateInformation(
  Bitstream&                           bitstream,
  MeshAttributeDeduplicateInformation& madi,
  MeshCodingHeader&                    mch,
  uint32_t                             index)
{
  TRACE_BITSTREAM_IN("%s", __func__);
  if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
    READ_VU(madi.getMeshAttributeDeduplicateCount());  //vu(v)
    if (madi.getMeshAttributeDeduplicateCount() > 0) {
      auto& NumAttributeStart = getGlobal().getNumAttributeStart()[index];
      auto& NumAttributeIsDuplicateFlags = getGlobal().getNumAttributeIsDuplicateFlags()[index];
      auto& NumSplitAttribute = getGlobal().getNumSplitAttribute()[index];
      auto& NumAddedDuplicatedAttribute = getGlobal().getNumAddedDuplicatedAttribute()[index];

      auto& meshAttributeDeduplicateIdx = madi.getMeshAttributeDeduplicateIdx();
      meshAttributeDeduplicateIdx.resize(madi.getMeshAttributeDeduplicateCount());
      for (uint32_t i = 0; i < madi.getMeshAttributeDeduplicateCount(); i++) {
        READ_VU(meshAttributeDeduplicateIdx[i]);  //vu(v)
        NumSplitAttribute = std::max(NumSplitAttribute, madi.getMeshAttributeDeduplicateIdx()[i] + 1);
      }
      NumAddedDuplicatedAttribute = madi.getMeshAttributeDeduplicateCount() - NumSplitAttribute; // not required as global ?
      NumAttributeIsDuplicateFlags = getGlobal().getMeshAttributeCodingPayload().getMeshAttributeFineResidualsCount()[index]
        + getGlobal().getMeshAttributeCodingPayload().getMeshAttributeCoarseResidualsCount()[index]
        + NumAttributeStart + NumAddedDuplicatedAttribute;
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.16 Padding to byte alignment syntax
void
EbReader::padding_to_byte_alignment(Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    uint32_t zero = 0;
    while (!bitstream.byteAligned()) {
        READ_CODE(zero, 1);  // f(1): equal to 0
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}
