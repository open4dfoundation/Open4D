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
#include "ebWriter.hpp"
#include "ebBitstream.h"
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

#  define WRITE_CODE(VAR, LEN) \
    { \
      bitstream.write(VAR, LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define WRITE_CODES(VAR, LEN) \
    { \
      bitstream.writeS(VAR, LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "s(" + std::to_string(LEN) + ")"); \
    }

#  define WRITE_UVLC(VAR) \
    { \
      bitstream.writeUvlc(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }

#  define WRITE_SVLC(VAR) \
    { \
      bitstream.writeSvlc(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "se(v)"); \
    }

#  define WRITE_FLOAT(VAR) \
    { \
      bitstream.writeFloat(VAR); \
      bitstream.traceBitFloat(format(#VAR), VAR, "f(32)"); \
    }
#  define WRITE_STRING(VAR) \
    { \
      bitstream.writeString(VAR); \
      bitstream.traceBitStr(format(#VAR), VAR, "f(32)"); \
    }

#  define WRITE_VU(VAR) \
    { \
      bitstream.writeVu(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "vu(v)"); \
    }

#  define WRITE_VI(VAR) \
    { \
      bitstream.writeVi(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "vi(v)"); \
    }

#  define WRITE_VECTOR(VAR) \
    { \
      bitstream.write(VAR); \
      bitstream.trace( \
        "Data stream: %s size = %zu \n", format(#VAR).c_str(), VAR.size()); \
    }

#  define WRITE_VIDEO(VAR) \
    { \
      bitstream.write(VAR); \
      bitstream.trace("Video stream: type = %s size = %zu \n", \
                      toString(VAR.type()).c_str(), \
                      VAR.size()); \
    }
#else
#  define WRITE_CODE(VAR, LEN) bitstream.write(VAR, LEN);
#  define WRITE_CODES(VAR, LEN) bitstream.writeS(VAR, LEN);
#  define WRITE_UVLC(VAR) bitstream.writeUvlc(VAR);
#  define WRITE_SVLC(VAR) bitstream.writeSvlc(VAR);
#  define WRITE_FLOAT(VAR) bitstream.writeFloat(VAR);
#  define WRITE_STRING(VAR) bitstream.writeString(VAR);
#  define WRITE_VU(VAR) bitstream.writeVu(VAR);
#  define WRITE_VI(VAR) bitstream.writeVi(VAR);
#  define WRITE_VECTOR(VAR) bitstream.write(VAR);
#  define WRITE_VIDEO(VAR) bitstream.write(VAR);
#endif

EbWriter::EbWriter()  = default;
EbWriter::~EbWriter() = default;

void
EbWriter::write(Bitstream& bitstream, MeshCoding& mc) {
  TRACE_BITSTREAM_IN("%s", __func__);
  setGlobal(mc);
  meshCoding( bitstream, mc );
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.1 General Mesh coding syntax
void
EbWriter::meshCoding(Bitstream& bitstream, MeshCoding& mc) {
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
EbWriter::meshCodingHeader(Bitstream& bitstream, MeshCodingHeader& mch) {
  auto startPosition = bitstream.getPosition().bytes_*8 + bitstream.getPosition().bits_;
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(mch.getMeshCodecType(), 2);     // u(2)
  WRITE_CODE(mch.getMeshVertexTraversalMethod(),2);  // u(2)
  meshPositionEncodingParameters(bitstream,
                                 mch.getMeshPositionEncodingParameters());
  WRITE_CODE(mch.getMeshPositionDequantizeFlag(), 1);  // u(1)

  if (mch.getMeshPositionDequantizeFlag()) {
    meshPositionDequantizeParameters(
      bitstream, mch.getMeshPositionDequantizeParameters());
  }
  WRITE_CODE(mch.getMeshEntropyPacketFlag(), 1);  // u(1)
  WRITE_CODE(mch.getMeshAttributeCount(), 5);  // u(5)

  for (uint32_t i = 0; i < mch.getMeshAttributeCount(); i++) {
    WRITE_CODE(mch.getMeshAttributeType()[i], 3);  // u(3)
    if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_NORMAL) {
      WRITE_CODE(mch.getMeshNormalOctahedralFlag()[i], 1);  // u(1)
    }
    if (mch.getMeshAttributeType()[i] == MeshAttributeType::MESH_ATTR_GENERIC) {
      WRITE_CODE(mch.getMeshAttributeNumComponentsMinus1()[i], 2);  // u(2)
    }
    meshAttributesEncodingParameters(bitstream, mch.getMeshAttributesEncodingParameters()[i]);
    WRITE_CODE(mch.getMeshAttributeDequantizeFlag()[i], 1);  // u(1)
    if (mch.getMeshAttributeDequantizeFlag()[i])
      meshAttributesDequantizeParameters(bitstream, mch, i);
  }
  WRITE_UVLC(mch.getMeshDeduplicateMethod());  // ue(v)
  WRITE_CODE(mch.getMeshReindexOutput(), 2); // u(1)
  padding_to_byte_alignment(bitstream);
  auto endPosition = bitstream.getPosition().bytes_*8 + bitstream.getPosition().bits_;
  headerLengthInBytes_ = (endPosition - startPosition)/8;
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.3 Mesh position encoding parameters syntax
void
EbWriter::meshPositionEncodingParameters(
  Bitstream&                      bitstream,
  MeshPositionEncodingParameters& mpep) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(mpep.getMeshPositionBitDepthMinus1(), 5);        // u(5)
  WRITE_UVLC(mpep.getMeshPositionPredictionMethod());         // ue(v)
  WRITE_CODE(mpep.getMeshPositionReverseUnificationFlag(), 1);  // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.4 Mesh position dequantize parameters syntax
void
EbWriter::meshPositionDequantizeParameters(
  Bitstream&                        bitstream,
  MeshPositionDequantizeParameters& mpdp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  for (uint32_t i = 0; i < 3; i++) {
    WRITE_FLOAT(mpdp.getMeshPositionMin(i));  // fl(32)
    WRITE_FLOAT(mpdp.getMeshPositionMax(i));  // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.5 Mesh attributes encoding parameters syntax
void
EbWriter::meshAttributesEncodingParameters(
  Bitstream&                        bitstream,
  MeshAttributesEncodingParameters& maep) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(maep.getMeshAttributeBitDepthMinus1(), 5);        // u(5)
  WRITE_CODE(maep.getMeshAttributePerFaceFlag(), 1);           // u(1)
  if (!maep.getMeshAttributePerFaceFlag()) {
    WRITE_CODE(maep.getMeshAttributeSeparateIndexFlag(), 1);   // u(1)
    if (!maep.getMeshAttributeSeparateIndexFlag())
      WRITE_UVLC(maep.getMeshAttributeReferenceIndexPlus1());  // ue(v)
  }
  WRITE_UVLC(maep.getMeshAttributePredictionMethod());         // ue(v)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.6 Mesh attributes dequantize parameters syntax
void
EbWriter::meshAttributesDequantizeParameters(Bitstream&        bitstream,
                                             MeshCodingHeader& mch,
                                             uint32_t          index) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& madp = mch.getMeshAttributesDequantizeParameters(index);
  for (uint32_t i = 0; i < mch.getNumComponents(index); i++) {
    WRITE_FLOAT(madp.getMeshAttributeMin(i));  // fl(32)
    WRITE_FLOAT(madp.getMeshAttributeMax(i));  // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.7 Mesh position coding payload syntax
void
EbWriter::meshPositionCodingPayload(Bitstream&                 bitstream,
                                    MeshPositionCodingPayload& mpcp,
                                    MeshCodingHeader&          mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& NumHandles = getGlobal().getNumHandles();
  auto& NumPositionStart = getGlobal().getNumPositionStart();

  WRITE_VU(mpcp.getMeshTriangleCount());                      // vu(v)
  WRITE_VU(mpcp.getMeshPositionStartCount());                 // vu(v)
  WRITE_VU(mpcp.getMeshPositionFineResidualsCount());         // vu(v)
  WRITE_VU(mpcp.getMeshPositionCoarseResidualsCount());       // vu(v)
  WRITE_VU(mpcp.getMeshClersCount());                         // vu(v)
  WRITE_VU(mpcp.getMeshCcWithBoundaryCount());                // vu(v)
  WRITE_VU(mpcp.getMeshHandlesCount());                       // vu(v)

  WRITE_VU(mch.getMeshGeoEntropyPacketBufferSize());   // vu(v)
  WRITE_VECTOR(mch.getMeshGeoEntropyPacketBuffer());   // ae(v) packet - byte aligned

  const int MinHandles = MIN_HANDLE; // MinHandles = 10 in syntax
  NumHandles = mpcp.getMeshHandlesCount();
  if (NumHandles < MinHandles) {
      const auto& meshHandleIndexFirstDelta = mpcp.getMeshHandleFirstDelta();
      const auto& meshHandleIndexSecondDelta = mpcp.getMeshHandleSecondDelta();
      for (size_t i = 0; i < NumHandles; i++) {
          WRITE_VI(meshHandleIndexFirstDelta[i]);   // vi(v)
          WRITE_VI(meshHandleIndexSecondDelta[i]);  // vi(v)
      }
  }

  NumPositionStart = mpcp.getMeshPositionStartCount();
  auto& mpep = mch.getMeshPositionEncodingParameters();
  auto& meshPositionStart = mpcp.getMeshPositionStart();
  for (uint32_t i = 0; i < NumPositionStart; i++) {
    for (uint32_t j = 0; j < 3; j++) {
      WRITE_CODE(meshPositionStart[i][j],
                 mpep.getMeshPositionBitDepthMinus1() + 1);     // u(v)
    }
  }
  padding_to_byte_alignment(bitstream);

  // mesh_position_fine_residual ae(v) in entropy packet

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
EbWriter::meshPositionDeduplicateInformation(
  Bitstream&                          bitstream,
  MeshPositionDeduplicateInformation& mpdi,
  MeshCodingHeader&                   mch){
  TRACE_BITSTREAM_IN("%s", __func__);
  if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
    WRITE_VU(mpdi.getMeshPositionDeduplicateCount());       //vu(v)
    if (mpdi.getMeshPositionDeduplicateCount() > 0) {
      auto& meshPositionDeduplicateIdx = mpdi.getMeshPositionDeduplicateIdx();
      for (uint32_t i = 0; i < mpdi.getMeshPositionDeduplicateCount(); i++) {
        WRITE_VU(meshPositionDeduplicateIdx[i]);            //vu(v)
      }
      // mesh_position_is_duplicate_flag ae(v) in entropy packet
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.9 Mesh difference information syntax
void EbWriter::meshDifferenceInformation(
    Bitstream& bitstream, MeshDifferenceInformation& mdi)
{
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_VU(mdi.getMeshAddPointCount());  //	vu(v)
  if (mdi.getMeshAddPointCount() > 0) {
    auto& AddPointIdxDelta = mdi.getAddPointIdxDelta();
    for (uint32_t i = 0; i < mdi.getMeshAddPointCount(); i++) {
      WRITE_VI(AddPointIdxDelta[i]);  //vi(v)
    }
  }

  WRITE_VU(mdi.getMeshDeletePointCount());  //	vu(v)
  if (mdi.getMeshDeletePointCount() > 0) {
    auto& DeletePointIdxDelta = mdi.getDeletePointIdxDelta();
    for (uint32_t i = 0; i < mdi.getMeshDeletePointCount(); i++) {
      WRITE_VI(DeletePointIdxDelta[i]);  //vi(v)
    }
  }

  WRITE_VU(mdi.getModifyMeshPointCount());  //	vu(v)
  if (mdi.getModifyMeshPointCount() > 0) {
    auto& ModifyTriangleIdxDelta = mdi.getModifyTrianglesDelta();
    for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
      WRITE_VI(ModifyTriangleIdxDelta[i]);  //vi(v)
    }

    auto& ModifyIdx = mdi.getModifyIdx();
    for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
      WRITE_VU(ModifyIdx[i]);  //vu(v)
    }

    auto& ModifyPointIdxDelta = mdi.getModifyPointIdxDelta();
    for (uint32_t i = 0; i < mdi.getModifyMeshPointCount(); i++) {
      WRITE_VI(ModifyPointIdxDelta[i]);  //vi(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.10 Mesh attribute coding payload syntax
void
EbWriter::meshAttributeCodingPayload(Bitstream&                  bitstream,
                                     MeshAttributeCodingPayload& macp,
                                     MeshCodingHeader&           mch) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& NumPositionStart = getGlobal().getNumPositionStart();
  auto& NumAttributeStart = getGlobal().getNumAttributeStart();
  NumAttributeStart.resize(mch.getMeshAttributeCount());
  for (uint32_t i = 0; i < mch.getMeshAttributeCount(); i++) {

    WRITE_VU(macp.getMeshAttributeStartCount()[i]);            // vu(v)
    WRITE_VU(macp.getMeshAttributeFineResidualsCount()[i]);    // vu(v)
    WRITE_VU(macp.getMeshAttributeCoarseResidualsCount()[i]);  // vu(v)
    if (mch.getMeshEntropyPacketFlag()) {
        WRITE_VU(macp.getMeshAttributeEntropyPacketSize()[i]);        // vu(v)
        WRITE_VECTOR(macp.getMeshAttributeEntropyPacketBuffer()[i]);  // ae(v)
    }
    auto& maep          = mch.getMeshAttributesEncodingParameters(i);
    auto  numComponents = mch.getNumComponents(i);
    auto  bitDepth      = maep.getMeshAttributeBitDepthMinus1() + 1;
    NumAttributeStart[i] = macp.getMeshAttributeStartCount()[i];
    auto& meshAttributeStart = macp.getMeshAttributeStart()[i];
    for (uint32_t j = 0; j < NumAttributeStart[i]; j++) {
      for (uint32_t k = 0; k < numComponents; k++) {
        WRITE_CODE(meshAttributeStart[j][k], bitDepth)                // u(v)
      }
    }
    padding_to_byte_alignment(bitstream);

    /*	extra data dependent on the selected prediction scheme	*/
    auto& maed = macp.getMeshAttributeExtraData()[i];
    meshAttributeExtraData(bitstream, maed, mch, maep, i);

    if (maep.getMeshAttributeSeparateIndexFlag()) {
      WRITE_VU(macp.getMeshAttributeSeamsCount()[i]);                 // vu(v)
      auto& madi = macp.getMeshAttributeDeduplicateInformation()[i];
      meshAttributeDeduplicateInformation(bitstream, madi, mch, i);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.11 Mesh extra attribute data syntax
void
EbWriter::meshAttributeExtraData(Bitstream&                        bitstream,
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
  }
  else if (type == MeshAttributeType::MESH_ATTR_NORMAL) {
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
EbWriter::meshTexCoordStretchExtraData(Bitstream&                    bitstream,
                                       MeshTexCoordStretchExtraData& mtcsed) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_VU(mtcsed.getMeshTexCoordStretchOrientationsCount());  // vu(v)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.13 Mesh materialid default extra data syntax
void
EbWriter::meshMaterialIDExtraData(Bitstream& bitstream,
    MeshMaterialIDExtraData& mmied) {
    TRACE_BITSTREAM_IN("%s", __func__);
    WRITE_VU(mmied.getMeshMaterialIDDCount());  // vu(v)
    WRITE_VU(mmied.getMeshMaterialIDLCount());  // vu(v)
    WRITE_VU(mmied.getMeshMaterialIDRCount());  // vu(v)
    WRITE_VU(mmied.getMeshMaterialIDFCount());  // vu(v)
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.14 Mesh normal octahedral extra data syntax
void EbWriter::meshNormalOctrahedralExtraData(Bitstream& bitstream, MeshNormalOctrahedralExtraData& mnoed) {
    TRACE_BITSTREAM_IN("%s", __func__);
    WRITE_CODE(mnoed.getMeshNormalOctrahedralBitDepthMinus1(), 5);        // u(5)
    WRITE_CODE(mnoed.getMeshNormalOctahedralSecondResidualFlag(), 1);     // u(1)
    padding_to_byte_alignment(bitstream);
    if (mnoed.getMeshNormalOctahedralSecondResidualFlag()) {
      WRITE_VU(mnoed.getMeshNormalOctahedralSecondResidualCount());         // vu(v)
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.15 Mesh attribute deduplicate information syntax
void
EbWriter::meshAttributeDeduplicateInformation(
  Bitstream&                           bitstream,
  MeshAttributeDeduplicateInformation& madi,
  MeshCodingHeader&                    mch,
  uint32_t                             index){
  TRACE_BITSTREAM_IN("%s", __func__);
  if (mch.getMeshDeduplicateMethod() == MeshDeduplicateMethod::MESH_DEDUP_DEFAULT) {
     WRITE_VU(madi.getMeshAttributeDeduplicateCount());  //vu(v)
    if (madi.getMeshAttributeDeduplicateCount() > 0) {
       auto& meshAttributeDeduplicateIdx = madi.getMeshAttributeDeduplicateIdx();
      for (uint32_t i = 0; i < madi.getMeshAttributeDeduplicateCount(); i++) {
         WRITE_VU(meshAttributeDeduplicateIdx[i]);  //vu(v)
      }
    } 
  } 
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// I.8.3.16 Padding to byte alignment syntax
void
EbWriter::padding_to_byte_alignment(Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    uint32_t zero = 0;
    while (!bitstream.byteAligned()) {
        WRITE_CODE(zero, 1);  // f(1): equal to 0
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}
