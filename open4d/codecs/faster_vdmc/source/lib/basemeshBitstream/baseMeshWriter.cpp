/* The copyright in this software is being made available under the BSD
* License, included below. This software may be subject to other third party
* and contributor rights, including patent rights, and no such rights are
* granted under this license.
*
* Copyright (c) 2025, ISO/IEC
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

//#include "v3cCommon.hpp"
#include <list>
#include "baseMeshWriter.hpp"
#include "writerCommon.hpp"
#include "bitstream.hpp"


using namespace basemesh;

BaseMeshWriter::BaseMeshWriter()  = default;
BaseMeshWriter::~BaseMeshWriter() = default;

bool
BaseMeshWriter::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}

bool
BaseMeshWriter::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
BaseMeshWriter::moreRbspData(vmesh::Bitstream& bitstream) {
  return false;
}
bool
BaseMeshWriter::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

std::pair<uint32_t, uint32_t>
BaseMeshWriter::calcMaxDataUnitSize(
  std::vector<BaseMeshSubmeshLayer>& smlList) {
  size_t maxSubmeshDataUnitIntraSize = 0;
  size_t maxSubmeshDataUnitInterSize = 0;
  for (auto& bmsl : smlList) {
    vmesh::Bitstream tempBitStream;
    if (bmsl.getSubmeshHeader().getSmhType() == I_BASEMESH) {
      auto&  bmsdu  = bmsl.getSubmeshDataunitIntra();
      size_t duSize = bmsdu.getCodedMeshDataSize();
      maxSubmeshDataUnitIntraSize =
        std::max(duSize, maxSubmeshDataUnitIntraSize);
      bmsdu.setPayloadSize(duSize);
    } else if (bmsl.getSubmeshHeader().getSmhType() == P_BASEMESH) {
      auto&  bmsdu  = bmsl.getSubmeshDataunitInter();
      size_t duSize = 0;
      maxSubmeshDataUnitInterSize =
        std::max(duSize, maxSubmeshDataUnitInterSize);
      bmsdu.setPayloadSize(duSize);
    }
  }
  uint32_t submeshDataUnitIntraBitCount =
    (uint32_t)ceil(log2((double)maxSubmeshDataUnitIntraSize));
  uint32_t submeshDataUnitInterBitCount =
    (uint32_t)ceil(log2((double)maxSubmeshDataUnitInterSize));

  return std::pair<uint32_t, uint32_t>(submeshDataUnitIntraBitCount,
                                       submeshDataUnitInterBitCount);
}

void
BaseMeshWriter::encode(BaseMeshBitstream& baseMesh, vmesh::Bitstream& bitstream, BaseMeshBitstreamGofStat& bistreamStat) {
  TRACE_BITSTREAM_IN("%s", __func__);
  vmesh::SampleStreamNalUnit ssnu;
  uint64_t            maxNaluSize = 0;
  struct bmNalUnit {
    uint64_t            naluTotSize;
    BaseMeshNalUnitType nuType;
    vmesh::Bitstream           tempBitStream;
  };
  std::vector<bmNalUnit> basemeshNaluList;
  const size_t           nalHeaderSize = 2;

  //calc max inter/intra submeshDataUnit size for byteCount
  std::pair<uint32_t, uint32_t> submeshDataUnitBitcount =
    calcMaxDataUnitSize(baseMesh.getBaseMeshSubmeshLayerList());

  for (auto& bmsl : baseMesh.getBaseMeshSubmeshLayerList()) {
    auto&  bmsh   = bmsl.getSubmeshHeader();
    size_t bmfpId = bmsh.getSmhSubmeshFrameParameterSetId();
    auto&  bmfp   = baseMesh.getBaseMeshFrameParameterSetList()
                   [bmfpId];  //TODO: [PSIDX] order=index?
  }

  uint32_t nalUCounter = 0;
  for (auto& bmsps : baseMesh.getBaseMeshSequenceParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(BASEMESH_NAL_BMSPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    baseMeshSequenceParameterSetRbsp(bmsps, tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    basemeshNaluList.push_back(
      bmNalUnit({naluSize, BASEMESH_NAL_BMSPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
    nalUCounter++;
    TRACE_BITSTREAM("NalUnit(basemesh) Index: %d, %s\n",
                    nalUCounter - 1,
                    toString((BaseMeshNalUnitType)nu.getType()).c_str());
  }
  for (auto& bmfps : baseMesh.getBaseMeshFrameParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(BASEMESH_NAL_BMFPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    baseMeshFrameParameterSetRbsp(
      bmfps, baseMesh.getBaseMeshSequenceParameterSetList(), tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    basemeshNaluList.push_back(
      bmNalUnit({naluSize, BASEMESH_NAL_BMFPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
    nalUCounter++;
    TRACE_BITSTREAM("NalUnit(basemesh) Index: %d, %s\n",
                    nalUCounter - 1,
                    toString((BaseMeshNalUnitType)nu.getType()).c_str());
  }

  baseMesh.reorderBaseMeshSubmeshLayer();
  int32_t frmOrderCntVal = -1;
  for (auto& bmsl : baseMesh.getBaseMeshSubmeshLayerList()) {
    // Prefix sei message
    for (auto& sei : bmsl.getSEI().getSeiPrefix()) {
      vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
      tempBitStream.setTrace(true);
      tempBitStream.setLogger(bitstream.getLogger());
#endif
      BaseMeshNalUnitType payloadType = BASEMESH_NAL_PREFIX_ESEI;
      if (sei->getPayloadType() == BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS) {
        payloadType = BASEMESH_NAL_PREFIX_NSEI;
      }
      vmesh::NalUnit nu(payloadType, 0, 1);
      nalUnitHeader(tempBitStream, nu);
      seiRbsp(tempBitStream, payloadType, *sei);
      uint64_t naluSize = (tempBitStream.size());
      basemeshNaluList.push_back(
        bmNalUnit({naluSize, payloadType, tempBitStream}));
      maxNaluSize = std::max(maxNaluSize, naluSize);
      nalUCounter++;
    }

    // Base mesh sub-stream layer data
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    auto& bmsh = bmsl.getSubmeshHeader();
    auto& bmfp = baseMesh.getBaseMeshFrameParameterSetList()
                   [bmsh.getSmhSubmeshFrameParameterSetId()];

    BaseMeshNalUnitType naluType = BASEMESH_NAL_IDR_N_LP;
    if (bmsh.getSmhType() == I_BASEMESH) { naluType = BASEMESH_NAL_IDR_N_LP; }
    if (bmsh.getSmhType() == P_BASEMESH) { naluType = BASEMESH_NAL_TRAIL_N; }

    vmesh::NalUnit nu(naluType, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    TRACE_BITSTREAM("NALU(submeshLayer) naluHeader: %d\n",
                    (tempBitStream.size()));
    baseMeshSubmeshLayerRbsp(bmsl,
                             naluType,
                             baseMesh.getBaseMeshFrameParameterSetList(),
                             baseMesh.getBaseMeshSequenceParameterSetList(),
                             tempBitStream);
    nalUCounter++;
    uint64_t naluSize     = (tempBitStream.size());
    bistreamStat.setBaseMesh(bmsh.getSmhType(), naluSize - 2);  // 2:header size
    TRACE_BITSTREAM("NalUnit(basemesh) Index: %d, %s\n",
                    nalUCounter - 1,
                    toString((BaseMeshNalUnitType)nu.getType()).c_str());
    TRACE_BITSTREAM("NALU(submeshLayer) naluSize: %d\n", naluSize);
    basemeshNaluList.push_back(bmNalUnit({naluSize, naluType, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);

    // Suffix sei message
    for (auto& sei : bmsl.getSEI().getSeiSuffix()) {
      for (size_t i = 0; i < bmsl.getSEI().getSeiSuffix().size(); i++) {
        vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
        tempBitStream.setTrace(true);
        tempBitStream.setLogger(bitstream.getLogger());
#endif
        vmesh::NalUnit nu(BASEMESH_NAL_SUFFIX_ESEI, 0, 1);
        nalUnitHeader(tempBitStream, nu);
        seiRbsp(tempBitStream,
                BASEMESH_NAL_SUFFIX_ESEI,
                *sei);
        uint64_t naluSize = (tempBitStream.size());
        basemeshNaluList.push_back(
          bmNalUnit({naluSize, BASEMESH_NAL_SUFFIX_ESEI, tempBitStream}));
        maxNaluSize = std::max(maxNaluSize, naluSize);
        nalUCounter++;
      }
    }
  }

  // Calculation of the max unit size done
  ssnu.getSizePrecisionBytesMinus1() =
    std::min(std::max(int(ceil(double(vmesh::ceilLog2(maxNaluSize + 1)) / 8.0)), 1),
             8)
    - 1;
  sampleStreamNalHeader(bitstream, ssnu);

  // Write sample streams
  size_t index     = 0;
  size_t sizeIndex = 0;

  for (auto& nu : basemeshNaluList) {
    TRACE_BITSTREAM("basmesh nalu........%s\t%d\n",
                    toString((BaseMeshNalUnitType)nu.nuType).c_str(),
                    nu.naluTotSize);
    WRITE_CODE(nu.naluTotSize, 8 * (ssnu.getSizePrecisionBytesMinus1() + 1));
    bitstream.copyFrom(nu.tempBitStream, 0, nu.naluTotSize);
    auto type = toString(nu.nuType);
    //copyFrom changes the bytePosition of nu.tempBitstream.
    printf("basemesh NalUnit Type = %-25s size = %llu \n",
           type.c_str(),
           nu.naluTotSize);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
void
BaseMeshWriter::baseMeshRefListStruct(BaseMeshRefListStruct&                rls,
                                      BaseMeshSequenceParameterSetRbsp& msps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(rls.getNumRefEntries());  // ue(v)
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (msps.getBmspsLongTermRefMeshFramesFlag()) {
      WRITE_CODE(rls.getStRefMeshFrameFlag(i), 1);
    }  // u(1)

    if (rls.getStRefMeshFrameFlag(i)) {
      WRITE_UVLC(rls.getAbsDeltaMfocSt(i));  // ue(v)
      if (rls.getAbsDeltaMfocSt(i) > 0) {
        WRITE_CODE(rls.getStrafEntrySignFlag(i), 1);  // u(1)
      }
    } else {
      uint8_t bitCount = msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4;
      WRITE_CODE(rls.getMfocLsbLt(i), bitCount);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

//ProfileToolsetConstraintsInformation
void
BaseMeshWriter::baseMeshProfileToolsetConstraintsInformation(
  BaseMeshProfileToolsetConstraintsInformation& mpftc,
  vmesh::Bitstream&                                bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(mpftc.getBmptcOneBeshFrameOnlyFlag(), 1);
  WRITE_CODE(mpftc.getBmptcIntraFrameOnlyFlag(), 1);
  WRITE_CODE(mpftc.getBmptcMotionVectorDerivationDisableFlag(), 1);
  WRITE_CODE(mpftc.getBmptcNoCraBlaFlag(), 1);
  WRITE_CODE(mpftc.getBmptcNoRaslFlag(), 1);
  WRITE_CODE(mpftc.getBmptcNoRadlFlag(), 1);
  WRITE_CODE(mpftc.getBmptcOneSubmeshPerFrameFlag(), 1);
  WRITE_CODE(mpftc.getBmptcNoTemporalScalabilityFlag(), 1);
  WRITE_CODE(mpftc.getBmptcNumReservedConstraintBytes(), 8);
  for (int i = 0; i < mpftc.getBmptcNumReservedConstraintBytes(); i++) {
    WRITE_CODE(mpftc.getBmptcReservedConstraintByte()[i], 8);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
//meshProfileTierLevel
uint32_t
BaseMeshWriter::baseMeshProfileTierLevel(BaseMeshProfileTierLevel& mpftl,
                                         vmesh::Bitstream&            bitstream,
                                         uint8_t maxNumBmeshSubLayersMinus1) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(mpftl.getBmptlTierFlag(), 1);
  WRITE_CODE(mpftl.getBmptlProfileIdc(), 7);
  WRITE_CODE(mpftl.getBmptlReservedZero32bits(), 32);
  WRITE_CODE(mpftl.getBmptlLevelIdc(), 8);
  for (size_t i = 0; i < maxNumBmeshSubLayersMinus1 + 1; i++) {
    WRITE_CODE(mpftl.getBmptlSubLayerProfilePresentFlag(i), 1);
    WRITE_CODE(mpftl.getBmptlSubLayerLevelPresentFlag(i), 1);
    if (mpftl.getBmptlSubLayerProfilePresentFlag(i)) {
      WRITE_CODE(mpftl.getBmptlSubLayerTierFlag(i), 1);
      WRITE_CODE(mpftl.getBmptlSubLayerProfileIdc(i), 7);
    }
    if (mpftl.getBmptlSubLayerLevelPresentFlag(i)) {
      WRITE_CODE(mpftl.getBmptlSubLayerLevelIdc(i), 8);
    }
  }
  WRITE_CODE(mpftl.getBmptlNumSubProfiles(), 6);
  WRITE_CODE(mpftl.getBmptlExtendedSubProfileFlag(), 1);
  for (size_t i = 0; i < mpftl.getBmptlNumSubProfiles(); i++) {
    uint32_t v = mpftl.getBmptlExtendedSubProfileFlag() == 0 ? 32 : 64;
    WRITE_CODE(mpftl.getBmptlSubProfileIdc(i), v);
  }
  WRITE_CODE(mpftl.getBmptlToolsetConstraintsPresentFlag(), 1);
  if (mpftl.getBmptlToolsetConstraintsPresentFlag()) {
    baseMeshProfileToolsetConstraintsInformation(
      mpftl.getBmptlProfileToolsetConstraintsInformation(), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
  return 0;
}
void
BaseMeshWriter::baseMeshSpsExtension(BaseMeshSequenceParameterSetRbsp& msps,
                                     uint32_t                          extIdx,
                                     vmesh::Bitstream& bitstream) {}
// Base mesh sequence parameter set Rbsp
uint32_t
BaseMeshWriter::baseMeshSequenceParameterSetRbsp(
  BaseMeshSequenceParameterSetRbsp& msps,
  vmesh::Bitstream&                        bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();
  WRITE_CODE(msps.getBmspsSequenceParameterSetId(), 4);
  WRITE_CODE(msps.getBmspsMaxSubLayersMinus1(), 3);
  WRITE_CODE(msps.getBmspsTemporalIdNestingFlag(), 1);
  baseMeshProfileTierLevel(msps.getBmeshProfileTierLevel(),
                           bitstream,
                           msps.getBmspsMaxSubLayersMinus1());
  WRITE_CODE(msps.getBmspsIntraMeshCodecId(), 8);
  WRITE_CODE(msps.getBmspsInterMeshCodecId(), 8);
  WRITE_CODE(msps.getBmspsGeometry3dBitDepthMinus1(), 5);
  WRITE_CODE(msps.getBmspsGeometryMsbAlignFlag(), 1);
  //WRITE_CODE(msps.getBmspsMaxNumMotionVectorPredictor() - 1, 2);
  WRITE_CODE(msps.getBmspsMeshAttributeCount(), 7);
  for (size_t i = 0; i < msps.getBmspsMeshAttributeCount(); i++) {
    WRITE_CODE(msps.getBmspsMeshAttributeIndex(i), 7);
    WRITE_CODE(msps.getBmspsMeshAttributeTypeId(i), 4);
    WRITE_CODE(msps.getBmspsMeshAttributeDimensionMinus1(i), 6);
    WRITE_CODE(msps.getBmspsAttributeBitDepthMinus1(i), 5);
    WRITE_CODE(msps.getBmspsAttributeMsbAlignFlag(i), 1);
  }
  //WRITE_UVLC(msps.getBmspsIntraMeshPostReindexMethod());
  WRITE_UVLC(msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4());
  WRITE_CODE(msps.getBmspsSubLayerOrderingInfoPresentFlag(), 1);
  uint32_t index = msps.getBmspsMaxSubLayersMinus1();
  if (msps.getBmspsSubLayerOrderingInfoPresentFlag()) { index = 0; }
  for (size_t i = index; i < msps.getBmspsMaxSubLayersMinus1() + 1; i++) {
    WRITE_UVLC(msps.getBmspsMaxDecMeshFrameBufferingMinus1(i));
    WRITE_UVLC(msps.getBmspsMaxNumReorderFrames(i));
    WRITE_UVLC(msps.getBmspsMaxLatencyIncreasePlus1(i));
  }
  WRITE_CODE(msps.getBmspsLongTermRefMeshFramesFlag(), 1);
  WRITE_UVLC(msps.getBmspsNumRefMeshFrameListsInBmsps());
  for (size_t i = 0; i < msps.getBmspsNumRefMeshFrameListsInBmsps(); i++) {
    baseMeshRefListStruct(msps.getBmeshRefListStruct(i), msps, bitstream);
  }
  //WRITE_CODE(msps.getBmspsInterMeshLog2MotionGroupSize(), 3);
  WRITE_CODE(msps.getBmspsInterMeshMaxNumNeighboursMinus1(), 3);
  WRITE_CODE(msps.getBmspsCodecSpecificParametersPresentFlag(), 1);
  if (msps.getBmspsCodecSpecificParametersPresentFlag()) {
    WRITE_CODE(msps.getBmspsMeshCodecPrefixLengthMinus1(), 8);
    for (uint32_t i = 0; i < (msps.getBmspsMeshCodecPrefixLengthMinus1() + 1);
         i++) {
      WRITE_CODE(msps.getBmspsMeshCodecPrefixData()[i], 8);
    }
  }
  WRITE_CODE(msps.getBmspsMotionCodecSpecificParametersPresentFlag(), 1);
  if (msps.getBmspsMotionCodecSpecificParametersPresentFlag()) {
    WRITE_CODE(msps.getBmspsMotionCodecPrefixLengthMinus1(), 8);
    for (uint32_t i = 0;
         i < (msps.getBmspsMotionCodecPrefixLengthMinus1() + 1);
         i++) {
      WRITE_CODE(msps.getBmspsMotionCodecPrefixData()[i], 8);
    }
  }
  WRITE_CODE(msps.getBmspsVuiParametersPresentFlag(), 1);
  if (msps.getBmspsVuiParametersPresentFlag()) {
    basemeshVuiParameters(bitstream, msps, msps.getVuiParameters());
  }
  WRITE_CODE(msps.getBmspsExtensionPresentFlag(), 1);
  if (msps.getBmspsExtensionPresentFlag())
    WRITE_CODE(msps.getBmspsExtensionCount(), 8);
  if (msps.getBmspsExtensionCount()) {
    WRITE_UVLC(msps.getBmspsExtensionsLengthMinus1());
    for (uint32_t i = 0; i < msps.getBmspsExtensionCount(); i++) {
      vmesh::Bitstream tempBitstream;
      baseMeshSpsExtension(msps, i, tempBitstream);
      msps.setBmspsExtensionLength(i, tempBitstream.size());
      WRITE_CODE(msps.getBmspsExtensionType(i), 8);
      WRITE_CODE(msps.getBmspsExtensionLength(i), 16);
#if defined(BITSTREAM_TRACE)
      tempBitstream.setTrace(true);
      tempBitstream.setLogger(bitstream.getLogger());
#endif
      //      bitstream.copyFromBits(tempBitstream, 0, tempBitstream.size());
    }
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}

// Base mesh frame parameter set Rbsp
uint32_t
BaseMeshWriter::baseMeshFrameParameterSetRbsp(
  BaseMeshFrameParameterSetRbsp& mfps,
  std::vector<BaseMeshSequenceParameterSetRbsp>& /*mspsList*/,
  vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();

  WRITE_UVLC(mfps.getBfpsMeshSequenceParameterSetId());
  WRITE_UVLC(mfps.getBfpsMeshFrameParameterSetId());
  baseMeshSubmeshInformation(mfps.getSubmeshInformation(), bitstream);
  WRITE_CODE(mfps.getBfpsOutputFlagPresentFlag(), 1);
  WRITE_UVLC(mfps.getBfpsNumRefIdxDefaultActiveMinus1());
  WRITE_UVLC(mfps.getBfpsAdditionalLtMfocLsbLen());
  //TODO : confirm these two values are needed
  //WRITE_UVLC         ( mfps.getBfpsSubmeshIntraUnitSizeBitCount() );
  //WRITE_UVLC         ( mfps.getBfpsSubmeshInterUnitSizeBitCount() );
  WRITE_CODE(mfps.getBfpsExtensionPresentFlag(), 1);
  if (mfps.getBfpsExtensionPresentFlag()) {
    //
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}

// Base mesh frame tile information
uint32_t
BaseMeshWriter::baseMeshSubmeshInformation(BaseMeshSubmeshInformation& msmi,
                                           vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();
  WRITE_CODE(msmi.getBmsiNumSubmeshesMinus1(), 6);  // u(6)
  WRITE_CODE(msmi.getBmsiSignalledSubmeshIdFlag(), 1);
  if (msmi.getBmsiSignalledSubmeshIdFlag()) {
    WRITE_UVLC(msmi.getBmsiSignalledSubmeshIdDeltaLength());
    auto numSubmeshes =
      static_cast<size_t>(msmi.getBmsiNumSubmeshesMinus1() + 1);
    uint32_t b = (uint32_t)ceil(log2(numSubmeshes));
    for (size_t i = 0; i < numSubmeshes; i++) {
      WRITE_CODE(msmi._submeshIndexToID[i],
                 msmi.getBmsiSignalledSubmeshIdDeltaLength()
                   + b);  //bugfix getBmsiSignalledSubmeshIdFlag
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}

void
BaseMeshWriter::baseMeshSubmeshLayerRbsp(
  BaseMeshSubmeshLayer&                          bmsl,
  BaseMeshNalUnitType                            nalUnitType,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                                     bitstream,
  int                                            layerCount) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    bmsh = bmsl.getSubmeshHeader();
  uint64_t submeshHeaderSize =
    baseMeshSubmeshHeader(bmsh, nalUnitType, mfpsList, mspsList, bitstream);
  uint64_t submeshPayloadSize =
    baseMeshSubmeshDataUnit(bmsl, mfpsList, mspsList, bitstream);
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM("submeshLayer Size : Header, Payload(%s) (%d, %d)\n",
                  bmsh.getSmhType() == I_BASEMESH ? "I" : "P",
                  submeshHeaderSize,
                  submeshPayloadSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint64_t
BaseMeshWriter::baseMeshSubmeshHeader(
  BaseMeshSubmeshHeader&                         smh,
  BaseMeshNalUnitType                            nalUnitType,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                                     bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos      = bitstream.getPosition().bytes_;
  int  bmfpsIdx = -1;
  for (int i = 0; i < mfpsList.size(); i++) {
    if (mfpsList[i].getBfpsMeshFrameParameterSetId()
        == smh.getSmhSubmeshFrameParameterSetId()) {
      bmfpsIdx = i;
      break;
    }
  }
  if (bmfpsIdx < 0)
    throw std::runtime_error(
      "No matching parameter set ID found in mfpsList.");
  BaseMeshFrameParameterSetRbsp& mfps     = mfpsList[bmfpsIdx];
  int                            bmspsIdx = -1;
  for (int i = 0; i < mspsList.size(); i++) {
    if (mspsList[i].getBmspsSequenceParameterSetId()
        == mfps.getBfpsMeshSequenceParameterSetId()) {
      bmspsIdx = i;
      break;
    }
  }
  if (bmspsIdx < 0)
    throw std::runtime_error(
      "No matching parameter set ID found in mspsList.");
  BaseMeshSequenceParameterSetRbsp& msps = mspsList[bmspsIdx];
  if (nalUnitType >= BASEMESH_NAL_BLA_W_LP
      && nalUnitType <= BASEMESH_NAL_RSV_BMCL_29) {
    WRITE_CODE(smh.getSmhNoOutputOfPriorSubmeshFramesFlag(), 1);  // u(1)
  }

  WRITE_UVLC(smh.getSmhSubmeshFrameParameterSetId());
  uint32_t b = (uint32_t)ceil(
    log2(mfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1() + 1));
  uint32_t v =
    mfps.getSubmeshInformation().getBmsiSignalledSubmeshIdDeltaLength() + b;
  if (mfps.getSubmeshInformation().getBmsiSignalledSubmeshIdFlag()
      || mfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1() != 0)
    WRITE_CODE(smh.getSmhId(), v);
  WRITE_UVLC(smh.getSmhType());
  if (mfps.getBfpsOutputFlagPresentFlag())
    WRITE_CODE(smh.getSmhMeshOutputFlag(), 1);

  WRITE_CODE(smh.getSmhBasemeshFrmOrderCntLsb(),
             (msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4));

  if (msps.getBmspsNumRefMeshFrameListsInBmsps() > 0)
    WRITE_CODE(smh.getSmhRefBasemeshFrameListMspsFlag(), 1);
  if (smh.getSmhRefBasemeshFrameListMspsFlag() == 0) {
    baseMeshRefListStruct(smh.getSmhRefListStruct(), msps, bitstream);
  } else if (msps.getBmspsNumRefMeshFrameListsInBmsps() > 1) {
    size_t bitCount = vmesh::ceilLog2(msps.getBmspsNumRefMeshFrameListsInBmsps());
    WRITE_CODE(smh.getSmhRefMeshFrameListIdx(), bitCount);
  }
  uint8_t rlsIdx  = smh.getSmhRefMeshFrameListIdx();
  auto&   refList = smh.getSmhRefBasemeshFrameListMspsFlag()
                      ? msps.getBmeshRefListStruct(rlsIdx)
                      : smh.getSmhRefListStruct();

  size_t NumLtrMeshFrmEntries = 0;
  for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
    if (!refList.getStRefMeshFrameFlag(i)) { NumLtrMeshFrmEntries++; }
  }
  for (size_t j = 0; j < NumLtrMeshFrmEntries; j++) {
    WRITE_CODE(smh.getMshAdditionalMfocLsbPresentFlag(j), 1);
    if (smh.getMshAdditionalMfocLsbPresentFlag(j)) {
      uint32_t v = mfps.getBfpsAdditionalLtMfocLsbLen();
      WRITE_CODE(smh.getMshAdditionalMfocLsbVal(j), v);
    }
  }

  if (smh.getSmhType() != SKIP_BASEMESH) {
    if (smh.getSmhType() == P_BASEMESH && refList.getNumRefEntries() > 1) {
      WRITE_CODE(smh.getMshNumRefIdxActiveOverrideFlag(), 1);
      if (smh.getMshNumRefIdxActiveOverrideFlag())
        WRITE_UVLC(smh.getMshNumRefIdxActiveMinus1());
    }
  }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
BaseMeshWriter::baseMeshSubmeshDataUnit(
  BaseMeshSubmeshLayer&                          bmsl,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                                     bitstream,
  int                                            layerCount) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto                           pos  = bitstream.getPosition().bytes_;
  BaseMeshSubmeshHeader&         bmsh = bmsl.getSubmeshHeader();
  BaseMeshFrameParameterSetRbsp& mfps = mfpsList
    [bmsh.getSmhSubmeshFrameParameterSetId()];  //TODO: [PSIDX] order=index?
  BaseMeshSequenceParameterSetRbsp& msps =
    mspsList[mfps.getBfpsMeshSequenceParameterSetId()];

  if (bmsh.getSmhType() == I_BASEMESH) {
    auto& bmsdu = bmsl.getSubmeshDataunitIntra();
    baseMeshSubmeshDataUnitIntra(bmsdu, bitstream);
  } else if (bmsh.getSmhType() == P_BASEMESH) {
    auto& bmsdu = bmsl.getSubmeshDataunitInter();
    baseMeshSubmeshDataUnitInter(bmsdu, bmsh, bitstream, layerCount);
  } else if (bmsh.getSmhType() == SKIP_BASEMESH) {
    auto& bmsdu = bmsl.getSubmeshDataunitInter();
    baseMeshSubmeshDataUnitSkip(bmsdu, bitstream);
  }
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
  TRACE_BITSTREAM_OUT("%s", __func__);
}
uint64_t
BaseMeshWriter::baseMeshSubmeshDataUnitIntra(
  BaseMeshSubmeshDataUnitIntra& meshdata,
  vmesh::Bitstream&                    bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos = bitstream.getPosition().bytes_;
  TRACE_BITSTREAM(
    "baseMeshSubmeshDataUnitIntra meshdata.getMeshDataSize(): %d\n",
    meshdata.getCodedMeshDataSize());
  bitstream.copyFromBits(
    meshdata.getCodedMeshDataUnitBuffer(), 0, meshdata.getCodedMeshDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
BaseMeshWriter::baseMeshSubmeshDataUnitInter(
  BaseMeshSubmeshDataUnitInter& meshdata,
  BaseMeshSubmeshHeader&        smh,
  vmesh::Bitstream&                    bitstream,
  int                           layerCount) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t numRefIdxActive = smh.getMshNumRefIdxActiveMinus1() + 1;
  if (numRefIdxActive > 1) {
    WRITE_UVLC(meshdata.getReferenceFrameIndex());  // ue(v)
    byteAlignment(bitstream);
  }
  auto pos            = bitstream.getPosition().bytes_;
  auto motionDataSize = meshdata.getCodedMeshDataSize();
  TRACE_BITSTREAM("basemeshSubmeshDataUnitInter motion data size(): %d\n",
                  meshdata.getCodedMeshDataSize());
  bitstream.copyFromBits(
    meshdata.getCodedMeshDataUnitBuffer(), 0, meshdata.getCodedMeshDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
BaseMeshWriter::baseMeshSubmeshDataUnitSkip(
  BaseMeshSubmeshDataUnitInter& meshdata,
  vmesh::Bitstream&                    bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return 0;
}

// 8.3.3 Byte alignment
void
BaseMeshWriter::byteAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5.1 General NAL unit
void
BaseMeshWriter::nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    WRITE_CODE(nalUnit.getData(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5.2 NAL unit header
void
BaseMeshWriter::nalUnitHeader(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(zero, 1);                           // f(1)
  WRITE_CODE(nalUnit.getType(), 6);              // u(6)
  WRITE_CODE(nalUnit.getLayerId(), 6);           // u(6)
  WRITE_CODE(nalUnit.getTemporalyIdPlus1(), 3);  // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
BaseMeshWriter::seiRbsp(vmesh::Bitstream&          bitstream,
                        BaseMeshNalUnitType nalUnitType,
                        basemesh::SEI&                sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  do {
    seiMessage(bitstream, nalUnitType, sei);
  } while (moreRbspData(bitstream));
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.10 RBSP trailing bit
void
BaseMeshWriter::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
BaseMeshWriter::seiMessage(vmesh::Bitstream&          bitstream,
                           BaseMeshNalUnitType nalUnitType,
                           basemesh::SEI&                sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto payloadType = static_cast<int32_t>(sei.getPayloadType());
  for (; payloadType >= 0xff; payloadType -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadType, 8);  // u(8)

  // Note: calculating the size of the sei message before writing it into the bitstream
  vmesh::Bitstream tempbitstream;
  seiPayload(tempbitstream, sei, nalUnitType);
  sei.setPayloadSize(tempbitstream.size());

  auto payloadSize = static_cast<int32_t>(sei.getPayloadSize());
  for (; payloadSize >= 0xff; payloadSize -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadSize, 8);  // u(8)
  seiPayload(bitstream, sei, nalUnitType);
}

// D.2 Sample stream NAL unit
// D.2.1 Sample stream NAL header
void
BaseMeshWriter::sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                                      vmesh::SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  WRITE_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23090-29:H.14.1.1 General SEI message
void
BaseMeshWriter::seiPayload(vmesh::Bitstream&          bitstream,
                           basemesh::SEI&                sei,
                           BaseMeshNalUnitType nalUnitType) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto payloadType = sei.getPayloadType();
  if (nalUnitType == BASEMESH_NAL_PREFIX_ESEI
      || nalUnitType == BASEMESH_NAL_PREFIX_NSEI) {
    if (payloadType == BASEMESH_BUFFERING_PERIOD) {                    // 0
    } else if (payloadType == BASEMESH_FRAME_TIMING) {                 // 1
    } else if (payloadType == BASEMESH_FILLER_PAYLOAD) {               // 2
    } else if (payloadType == BASEMESH_USER_DATAREGISTERED_ITUTT35) {  // 3
    } else if (payloadType == BASEMESH_USER_DATA_UNREGISTERED) {       // 4
    } else if (payloadType == BASEMESH_RECOVERY_POINT) {               // 5
    } else if (payloadType == BASEMESH_NO_RECONSTRUCTION) {            // 6
    } else if (payloadType == BASEMESH_TIME_CODE) {                    // 7
    } else if (payloadType == BASEMESH_SEI_MANIFEST) {                 // 8
    } else if (payloadType == BASEMESH_SEI_PREFIX_INDICATION) {        // 9
    } else if (payloadType == BASEMESH_COMPONENT_CODEC_MAPPING) {      // 10
    } else if (payloadType
               == BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS) {  // 11
      baseMeshAttributeTransformationParams(bitstream, sei);
    } else {
      // TODO reservedMessage(bitstream, sei);
    }
  } else { /* nalUnitType  ==  NAL_SUFFIX_SEI  || nalUnitType  ==
             NAL_SUFFIX_NSEI */
    if (payloadType == BASEMESH_FILLER_PAYLOAD) {                      // 2
    } else if (payloadType == BASEMESH_USER_DATAREGISTERED_ITUTT35) {  // 3
    } else if (payloadType == BASEMESH_USER_DATA_UNREGISTERED) {       // 4
    } else {
      // TODO reservedMessage(bitstream, sei);
    }
  }
  if (moreDataInPayload(bitstream)) {
    if (payloadExtensionPresent(bitstream)) {
      WRITE_CODE(1, 1);  // u(v)
    }
    byteAlignment(bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}


// H.15  VUI
// H.15.2.1  VUI parameters
void
BaseMeshWriter::basemeshVuiParameters(vmesh::Bitstream& bitstream,
                                      BaseMeshSequenceParameterSetRbsp bmsps,
                                      BasemeshVuiParameters&           vp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
  if (vp.getTimingInfoPresentFlag()) {
    WRITE_CODE(vp.getNumUnitsInTick(), 32);               // u(32)
    WRITE_CODE(vp.getTimeScale(), 32);                    // u(32)
    WRITE_CODE(vp.getMfocProportionalToTimingFlag(), 1);  // u(1)
    if (vp.getMfocProportionalToTimingFlag()) {
      WRITE_UVLC(vp.getNumTicksMfocDiffOneMinus1());  // ue(v)
    }
    WRITE_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
    if (vp.getHrdParametersPresentFlag()) {
      basemeshHrdParameters(bitstream,
                            true,
                            bmsps.getBmspsMaxSubLayersMinus1(),
                            vp.getHrdParameters());
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.15.2.2	Basemesh HRD parameters
void
BaseMeshWriter::basemeshHrdParameters(vmesh::Bitstream&       bitstream,
                                      bool             commonInPresentFlag,
                                      int              maxSubLayersMinus1,
                                      BaseMeshHrdParameters& hp) {
  TRACE_BITSTREAM_IN("%s", __func__);

  if (commonInPresentFlag) {
    WRITE_CODE(hp.getBmNalParametersPresentFlag(), 1);  // u(1)
    WRITE_CODE(hp.getBmClParametersPresentFlag(), 1);   // u(1)
    if (hp.getBmNalParametersPresentFlag()
        || hp.getBmClParametersPresentFlag()) {
      WRITE_CODE(hp.getSubmeshHrdParametersPresentFlag(), 1);  // u(1)
      if (hp.getSubmeshHrdParametersPresentFlag()) {
        WRITE_CODE(hp.getBmTickDivisorMinus2(), 8);  // u(8)
        WRITE_CODE(hp.getDuCbmbRemovalDelayIncrementLengthMinus1(),
                   5);                                              // u(5)
        WRITE_CODE(hp.getSubmeshCbmbParamsInBmTimingSeiFlag(), 1);  // u(1)
        WRITE_CODE(hp.getDbmbOutputDuDelayLengthMinus1(), 5);       // u(5)
      }
      WRITE_CODE(hp.getBmBitRateScale(), 4);  // u(4)
      WRITE_CODE(hp.getCbmbSizeScale(), 4);   // u(4)
      if (hp.getSubmeshHrdParametersPresentFlag())
        WRITE_CODE(hp.getCbmbSizeDuScale(), 4);                    // u(4)
      WRITE_CODE(hp.getInitialCbmbRemovalDelayLengthMinus1(), 5);  // u(5)
      WRITE_CODE(hp.getAuCbmbRemovalDelayLengthMinus1(), 5);       // u(5)
      WRITE_CODE(hp.getdbmbOutputDelayLengthMinus1(), 5);          // u(5)
    }
  }

  for (size_t i = 0; i <= maxSubLayersMinus1; i++) {
    WRITE_CODE(hp.getBmFixedRateGeneralFlag(i), 1);  // u(1)
    if (!hp.getBmFixedRateGeneralFlag(i)) {
      WRITE_CODE(hp.getBmFixedRateWithinCbmsFlag(i), 1);  // u(1)
    }
    if (hp.getBmFixedRateWithinCbmsFlag(i)) {
      WRITE_CODE(hp.getBmElementalDurationInTcMinus1(i), 1);  // ue(v)
    } else {
      WRITE_CODE(hp.getBmLowDelayFlag(i), 1);  // u(1)
    }
    if (!hp.getBmLowDelayFlag(i)) {
      WRITE_CODE(hp.getCbmdCntMinus1(i), 1);  // ue(v)
    }
    if (hp.getBmNalParametersPresentFlag()) {
      basemeshHrdSubLayerParameters(bitstream,
                                    hp,
                                    hp.getBmHdrSubLayerParameters(0, i),
                                    hp.getCbmdCntMinus1(i));
    }
    if (hp.getBmClParametersPresentFlag()) {
      basemeshHrdSubLayerParameters(bitstream,
                                    hp,
                                    hp.getBmHdrSubLayerParameters(1, i),
                                    hp.getCbmdCntMinus1(i));
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.15.2.3	Basemesh sub-layer HRD parameters
void
BaseMeshWriter::basemeshHrdSubLayerParameters(vmesh::Bitstream&       bitstream,
                                              BaseMeshHrdParameters& hp,
  BaseMeshHrdSubLayerParameters& hlsp,
                                              size_t cabCnt) {
  TRACE_BITSTREAM_IN("%s", __func__);
  hlsp.allocate(cabCnt + 1);
  for (size_t i = 0; i <= cabCnt; i++) {
    WRITE_UVLC(hlsp.getBmBitRateValueMinus1(i));  // ue(v)
    WRITE_UVLC(hlsp.getCmbmSizeValueMinus1(i));   // ue(v)
    if (hp.getSubmeshHrdParametersPresentFlag()) {
      WRITE_UVLC(hlsp.getCmbmSizeDuValueMinus1(i));   // ue(v)
      WRITE_UVLC(hlsp.getBmBitRateDuValueMinus1(i));  // ue(v)
    }
    WRITE_CODE(hlsp.getBmCbrFlag(i), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23090-29:H.14.1.10 Basemesh Attribute transformation parameters SEI message
void
BaseMeshWriter::baseMeshAttributeTransformationParams(vmesh::Bitstream& bitstream,
                                                      basemesh::SEI&       seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<basemesh::BMSEIAttributeTransformationParams&>(seiAbstract);
  WRITE_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    WRITE_UVLC(sei.getNumAttributeUpdates());  // ue(v)
    for (size_t j = 0; j < sei.getNumAttributeUpdates(); j++) {
      WRITE_CODE(sei.getAttributeIdx(j), 8);  // u(8)
      size_t index = sei.getAttributeIdx(j);
      WRITE_CODE(sei.getDimensionMinus1(index), 8);  // u(8)
      for (size_t i = 0; i <= sei.getDimensionMinus1(index); i++) {
        WRITE_CODE(sei.getScaleParamsEnabledFlag(index, i), 1);   // u(1)
        WRITE_CODE(sei.getOffsetParamsEnabledFlag(index, i), 1);  // u(1)
        if (sei.getScaleParamsEnabledFlag(index, i)) {
          WRITE_CODE(sei.getAttributeScale(index, i), 32);  // u(32)
        }
        if (sei.getOffsetParamsEnabledFlag(index, i)) {
          WRITE_CODES(sei.getAttributeOffset(index, i), 32);  // i(32)
        }
      }
    }
    WRITE_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
