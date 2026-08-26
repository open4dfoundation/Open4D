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
#include "baseMeshReader.hpp"
#include "bitstream.hpp"
//#include "bitstreamStat.hpp"
#include "sampleStreamNalUnit.hpp"
#include "readerCommon.hpp"

using namespace basemesh;

BaseMeshReader::BaseMeshReader() {}
BaseMeshReader::~BaseMeshReader() = default;

// 8.2 Specification functions and descriptors
bool
BaseMeshReader::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
BaseMeshReader::lengthAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
BaseMeshReader::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
BaseMeshReader::moreRbspData(vmesh::Bitstream& bitstream) {
  uint32_t value = 0;
  // Return false if there is no more data.
  if (!bitstream.moreData()) { return false; }
  // Store bitstream state.
  auto position = bitstream.getPosition();
#if defined(BITSTREAM_TRACE)
  bitstream.setTrace(false);
#endif
  // Skip first bit. It may be part of a RBSP or a rbsp_one_stop_bit.
  READ_CODE(value, 1);  // u(1)
  while (bitstream.moreData()) {
    READ_CODE(value, 1);  // u(1)
    if (value) {
      // We found a one bit beyond the first bit. Restore bitstream state and return true.
      bitstream.setPosition(position);
#if defined(BITSTREAM_TRACE)
      bitstream.setTrace(true);
#endif
      return true;
    }
  }
  // We did not found a one bit beyond the first bit. Restore bitstream state and return false.
  bitstream.setPosition(position);
#if defined(BITSTREAM_TRACE)
  bitstream.setTrace(true);
#endif
  return false;
}

bool
BaseMeshReader::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

void
BaseMeshReader::decode(BaseMeshBitstream&    baseMesh,
                       vmesh::Bitstream&     bitstream,
                       BaseMeshBitstreamGofStat& bistreamStat) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t                     sizeBitstream = bitstream.capacity();
  vmesh::SampleStreamNalUnit ssnu;
  basemesh::BaseMeshSei      sei;
  sampleStreamNalHeader(bitstream, ssnu);
  while (bitstream.size() < sizeBitstream) {
    ssnu.addNalUnit();
    size_t                 index     = ssnu.getNalUnitCount() - 1;
    basemesh::BaseMeshSei& prefixSEI = sei;
    auto&                  nalu      = ssnu.getNalUnit(index);
    size_t                 naluSize  = 0;
    READ_CODE_DESC(
      naluSize, 8 * (ssnu.getSizePrecisionBytesMinus1() + 1), "BM_NALUSIZE");
    nalu.setSize(naluSize);
    nalu.allocate();
    Bitstream ssnuBitstream;
    // no need to signal the SODB length as parsing from the end of the RBSP
    // is possible from rbsp_trailing_bits
    // if the SODB is known to have a length that is a multiple of 8
    // then the RBSP ends with 80 and potentially a number of 00 until
    // alignment with precision bytes is reached

#if defined(BITSTREAM_TRACE)
    ssnuBitstream.setTrace(true);
    ssnuBitstream.setLogger(*logger_);
#endif
    bitstream.copyTo(ssnuBitstream, nalu.getSize());
    nalUnitHeader(ssnuBitstream, nalu);
    {
      size_t duSize   = 0;
      auto   naluType = BaseMeshNalUnitType(nalu.getType());
      switch (naluType) {
      case BASEMESH_NAL_BMSPS:
        baseMeshSequenceParameterSetRbsp(
          baseMesh.addBaseMeshSequenceParameterSet(), ssnuBitstream);
        break;
      case BASEMESH_NAL_BMFPS:
        baseMeshFrameParameterSetRbsp(
          baseMesh.addBaseMeshFrameParameterSet(),
          baseMesh.getBaseMeshSequenceParameterSetList(),
          ssnuBitstream);
        break;
      case BASEMESH_NAL_TRAIL_N:
      case BASEMESH_NAL_TRAIL_R:
      case BASEMESH_NAL_TSA_N:
      case BASEMESH_NAL_TSA_R:
      case BASEMESH_NAL_STSA_N:
      case BASEMESH_NAL_STSA_R:
      case BASEMESH_NAL_RADL_N:
      case BASEMESH_NAL_RADL_R:
      case BASEMESH_NAL_RASL_N:
      case BASEMESH_NAL_RASL_R:
      case BASEMESH_NAL_SKIP_N:
      case BASEMESH_NAL_SKIP_R:
      case BASEMESH_NAL_IDR_N_LP:
        duSize = baseMeshSubmeshLayerRbsp(
          baseMesh.addBaseMeshSubmeshLayer(),
          naluType,
          nalu.getSize(),
          baseMesh.getBaseMeshFrameParameterSetList(),
          baseMesh.getBaseMeshSequenceParameterSetList(),
          ssnuBitstream);
        baseMesh.getBaseMeshSubmeshLayerList().back().getSEI().getSeiPrefix() =
          sei.getSeiPrefix();
        sei.getSeiPrefix().clear();
        break;
      case BASEMESH_NAL_PREFIX_ESEI:
      case BASEMESH_NAL_PREFIX_NSEI:
        seiRbsp(ssnuBitstream, naluType, sei);
        break;
      case BASEMESH_NAL_SUFFIX_ESEI:
      case BASEMESH_NAL_SUFFIX_NSEI:
        seiRbsp(ssnuBitstream,
                naluType,
                baseMesh.getBaseMeshSubmeshLayerList().back().getSEI());
        break;
      default:
        fprintf(stderr,
                "sampleStreamNalUnit type = %d not supported\n",
                static_cast<int32_t>(nalu.getType()));
      }
      if (duSize != 0) {
        bistreamStat.setBaseMesh(baseMesh.getBaseMeshSubmeshLayerList()
                                   .back()
                                   .getSubmeshHeader()
                                   .getSmhType(),
                                 duSize);
      }
    }
    TRACE_BITSTREAM("NalUnit(basemesh) Index: %d, %s\n",
                    ssnu.getNalUnitCount() - 1,
                    toString((BaseMeshNalUnitType)ssnu
                               .getNalUnit()[ssnu.getNalUnitCount() - 1]
                               .getType())
                      .c_str());
    if (BaseMeshNalUnitType(nalu.getType()) != BASEMESH_NAL_BMSPS
        && BaseMeshNalUnitType(nalu.getType()) != BASEMESH_NAL_BMFPS)
      TRACE_BITSTREAM("NALU(submeshLayer) naluSize: %d\n", naluSize);
  }
  for (auto& nu : ssnu.getNalUnit()) {
    TRACE_BITSTREAM("basmesh nalu........%s\t%d\n",
                    toString((BaseMeshNalUnitType)nu.getType()).c_str(),
                    nu.getSize());
    printf("basemesh NalUnit Type = %-25s size = %zu \n",
           toString((BaseMeshNalUnitType)nu.getType()).c_str(),
           nu.getSize());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
void
BaseMeshReader::baseMeshRefListStruct(BaseMeshRefListStruct&            rls,
                                      BaseMeshSequenceParameterSetRbsp& msps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value = 0;
  READ_UVLC_DESC(value, "NumRefEntries");
  rls.setNumRefEntries(value);  // ue(v)

  rls.allocate();
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (msps.getBmspsLongTermRefMeshFramesFlag()) {
      READ_CODE_DESC(value, 1,  "StRefMeshFrameFlag");
      rls.setStRefMeshFrameFlag(i, value);  // u(1)
    } else {
      rls.setStRefMeshFrameFlag(i, true);
    }
    if (rls.getStRefMeshFrameFlag(i)) {
      READ_UVLC_DESC(value, "AbsDeltaMfocSt");
      rls.setAbsDeltaMfocSt(i, value);  // ue(v)
      if (rls.getAbsDeltaMfocSt(i) > 0) {
        READ_CODE_DESC(value, 1, "StrafEntrySignFlag");
        rls.setStrafEntrySignFlag(i, value);  // u(1)
      } else {
        rls.setStrafEntrySignFlag(i, true);
      }
    } else {
      uint8_t bitCount = msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4;
      READ_CODE_DESC(value,bitCount, "MfocLsbLt");
      rls.setMfocLsbLt(i, bitstream.read(bitCount));  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

//ProfileToolsetConstraintsInformation
void
BaseMeshReader::baseMeshProfileToolsetConstraintsInformation(
  BaseMeshProfileToolsetConstraintsInformation& mpftc,
  vmesh::Bitstream&                             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  mpftc.setBmptcOneMeshFrameOnlyFlag(bitstream.read(1));
  mpftc.setBmptcIntraFrameOnlyFlag(bitstream.read(1));
  mpftc.setBmptcMotionVectorDerivationDisableFlag(bitstream.read(1));
  mpftc.setBmptcNoCraBlaFlag(bitstream.read(1));
  mpftc.setBmptcNoRaslFlag(bitstream.read(1));
  mpftc.setBmptcNoRadlFlag(bitstream.read(1));
  mpftc.setBmptcOneSubmeshPerFrameFlag(bitstream.read(1));
  mpftc.setBmptcNoTemporalScalabilityFlag(bitstream.read(1));
  mpftc.setBmptcNumReservedConstraintBytes(bitstream.read(8));
  for (int i = 0; i < mpftc.getBmptcNumReservedConstraintBytes(); i++) {
    mpftc.setBmptcReservedConstraintByte(i, bitstream.read(8));
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
//meshProfileTierLevel
void
BaseMeshReader::baseMeshProfileTierLevel(BaseMeshProfileTierLevel& mpftl,
                                         vmesh::Bitstream&         bitstream,
                                         uint8_t maxNumBmeshSubLayersMinus1) {
  TRACE_BITSTREAM_IN("%s", __func__);
  mpftl.setBmptlTierFlag(bitstream.read(1));
  TRACE_BITSTREAM("  BmptlTierFlag ..................................... = %d      u(1)\n",
    mpftl.getBmptlTierFlag());
  mpftl.setBmptlProfileIdc(bitstream.read(7));
  TRACE_BITSTREAM("  BmptlProfileIdc ................................... = %d      u(7)\n",
    mpftl.getBmptlProfileIdc());
  mpftl.setBmptlReservedZero32bits(bitstream.read(32));
  TRACE_BITSTREAM("  BmptlReservedZero32bits ........................... = %d      u(32)\n",
    mpftl.getBmptlReservedZero32bits());
  mpftl.setBmptlLevelIdc(bitstream.read(8));
  TRACE_BITSTREAM("  BmptlLevelIdc ..................................... = %d      u(8)\n",
    mpftl.getBmptlLevelIdc());
  for (size_t i = 0; i < maxNumBmeshSubLayersMinus1 + 1; i++) {
    mpftl.setBmptlSubLayerProfilePresentFlag(bitstream.read(1), i);
    TRACE_BITSTREAM("  BmptlSubLayerProfilePresentFlag ................... = %d      u(1)\n",
      mpftl.getBmptlSubLayerProfilePresentFlag(i));
    mpftl.setBmptlSubLayerLevelPresentFlag(bitstream.read(1), i);
    TRACE_BITSTREAM("  BmptlSubLayerLevelPresentFlag ..................... = %d      u(1)\n",
      mpftl.getBmptlSubLayerLevelPresentFlag(i));
    if (mpftl.getBmptlSubLayerProfilePresentFlag(i)) {
      mpftl.setBmptlSubLayerTierFlag(bitstream.read(1), i);
      TRACE_BITSTREAM("  BmptlSubLayerTierFlag ............................. = %d      u(1)\n",
        mpftl.getBmptlSubLayerTierFlag()[i]);
      mpftl.setBmptlSubLayerProfileIdc(bitstream.read(7), i);
      TRACE_BITSTREAM("  BmptlSubLayerProfileIdc ........................... = %d      u(7)\n",
        mpftl.getBmptlSubLayerProfileIdc()[i]);
    }
    if (mpftl.getBmptlSubLayerLevelPresentFlag(i)) {
      mpftl.setBmptlSubLayerLevelIdc(bitstream.read(8), i);
      TRACE_BITSTREAM("  BmptlSubLayerLevelPresentFlag ..................... = %d      u(8)\n",
        mpftl.getBmptlSubLayerLevelPresentFlag()[i]);
    }
  }
  mpftl.setBmptlNumSubProfiles(bitstream.read(6));
  TRACE_BITSTREAM("  BmptlNumSubProfiles ............................... = %d      u(6)\n",
    mpftl.getBmptlNumSubProfiles());
  mpftl.setBmptlExtendedSubProfileFlag(bitstream.read(1));
  TRACE_BITSTREAM("  BmptlExtendedSubProfileFlag ....................... = %d      u(1)\n",
    mpftl.getBmptlExtendedSubProfileFlag());
  for (size_t i = 0; i < mpftl.getBmptlNumSubProfiles(); i++) {
    uint32_t v = mpftl.getBmptlExtendedSubProfileFlag() == 0 ? 32 : 64;
    mpftl.setBmptlSubProfileIdc(bitstream.read(v), i);
    TRACE_BITSTREAM("  BmptlSubProfileIdc ................................ = %d      u(v)\n",
      mpftl.getBmptlSubProfileIdc()[i]);
  }
  mpftl.setBmptlToolsetConstraintsPresentFlag(bitstream.read(1));
  TRACE_BITSTREAM("  BmptlToolsetConstraintsPresentFlag ................ = %d      u(1)\n",
                  mpftl.getBmptlToolsetConstraintsPresentFlag());
  if (mpftl.getBmptlToolsetConstraintsPresentFlag()) {
    baseMeshProfileToolsetConstraintsInformation(
      mpftl.getBmptlProfileToolsetConstraintsInformation(), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
void
BaseMeshReader::baseMeshSpsExtension(BaseMeshSequenceParameterSetRbsp& msps,
                                     uint32_t                          extIdx,
                                     vmesh::Bitstream& bitstream) {}
// Base mesh sequence parameter set Rbsp
void
BaseMeshReader::baseMeshSequenceParameterSetRbsp(
  BaseMeshSequenceParameterSetRbsp& msps,
  vmesh::Bitstream&                 bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  int32_t value;

  READ_CODE_DESC(value, 4, "BmspsSequenceParameterSetId");
  msps.setBmspsSequenceParameterSetId(value);
  READ_CODE_DESC(value, 3, "BmspsMaxSubLayersMinus1");
  msps.setBmspsMaxSubLayersMinus1(value);
  READ_CODE_DESC(value, 1, "BmspsTemporalIdNestingFlag");
  msps.setBmspsTemporalIdNestingFlag(value);
  baseMeshProfileTierLevel(msps.getBmeshProfileTierLevel(),
                           bitstream,
                           msps.getBmspsMaxSubLayersMinus1());
  READ_CODE_DESC(value, 8, "BmspsIntraMeshCodecId           ");
  msps.setBmspsIntraMeshCodecId(value);
  READ_CODE_DESC(value, 8, "BmspsInterMeshCodecId           ");
  msps.setBmspsInterMeshCodecId(value);
  READ_CODE_DESC(value, 5, "BmspsGeometry3dBitDepthMinus1   ");
  msps.setBmspsGeometry3dBitDepthMinus1(value);
  READ_CODE_DESC(value, 1, "BmspsGeometryMsbAlignFlag       ");
  msps.setBmspsGeometryMsbAlignFlag(value);
  //READ_CODE_DESC(value, 2, "BmspsMaxNumMotionVectorPredictorMinus1"); msps.setMaxNumMotionVectorPredictor(value + 1);
  READ_CODE_DESC(value, 7, "BmspsMeshAttributeCount         ");
  msps.setBmspsMeshAttributeCount(value);
  for (size_t i = 0; i < msps.getBmspsMeshAttributeCount(); i++) {
    READ_CODE_DESC(value, 7, "BmspsMeshAttributeIndex    ");
    msps.setBmspsMeshAttributeIndex(value, i);
    READ_CODE_DESC(value, 4, "BmspsMeshAttributeTypeId    ");
    msps.setBmspsMeshAttributeTypeId(value, i);
    READ_CODE_DESC(value, 6, "BmspsMeshAttributeDimensionMinus1");
    msps.setBmspsMeshAttributeDimensionMinus1(value, i);
    READ_CODE_DESC(value, 5, "BmspsAttributeBitDepthMinus1");
    msps.setBmspsAttributeBitDepthMinus1(value, i);
    READ_CODE_DESC(value, 1, "BmspsAttributeMsbAlignFlag  ");
    msps.setBmspsAttributeMsbAlignFlag(value, i);
  }

  //READ_UVLC_DESC(value, "BmspsIntraMeshPostReindexMethod ");  msps.setBmspsIntraMeshPostReindexMethod(value);
  READ_UVLC_DESC(value, "BmspsLog2MaxMeshFrameOrderCntLsbMinus4");
  msps.setBmspsLog2MaxMeshFrameOrderCntLsbMinus4(value);
  READ_CODE_DESC(value, 1, "BmspsSubLayerOrderingInfoPresentFlag()");
  msps.setBmspsSubLayerOrderingInfoPresentFlag(value);
  uint32_t index = msps.getBmspsMaxSubLayersMinus1();
  if (msps.getBmspsSubLayerOrderingInfoPresentFlag()) { index = 0; }
  for (size_t i = index; i < msps.getBmspsMaxSubLayersMinus1() + 1; i++) {
    READ_UVLC_DESC(value, "BmspsMaxDecMeshFrameBufferingMinus1   ");
    msps.setBmspsMaxDecMeshFrameBufferingMinus1(value, i);
    READ_UVLC_DESC(value, "BmspsMaxNumReorderFrames   ");
    msps.setBmspsMaxNumReorderFrames(value, i);
    READ_UVLC_DESC(value, "BmspsMaxLatencyIncreasePlus1   ");
    msps.setBmspsMaxLatencyIncreasePlus1(value, i);
  }
  READ_CODE_DESC(value, 1, "BmspsLongTermRefMeshFramesFlag");
  msps.setBmspsLongTermRefMeshFramesFlag(value);
  READ_UVLC_DESC(value, "BmspsNumRefMeshFrameListsInBmsps");
  msps.setBmspsNumRefMeshFrameListsInBmsps(value);
  for (size_t i = 0; i < msps.getBmspsNumRefMeshFrameListsInBmsps(); i++) {
    baseMeshRefListStruct(msps.getBmeshRefListStruct(i), msps, bitstream);
  }
  //READ_CODE_DESC(value, 3, "BmspsInterMeshLog2MotionGroupSize");       msps.setBmspsInterMeshLog2MotionGroupSize(value);
  READ_CODE_DESC(value, 3, "BmspsInterMeshMaxNumNeighboursMinus1");
  msps.setBmspsInterMeshMaxNumNeighboursMinus1(value);
  READ_CODE_DESC(value, 1, "BmspsCodecSpecificParametersPresentFlag");
  msps.setBmspsCodecSpecificParametersPresentFlag(value);
  if (msps.getBmspsCodecSpecificParametersPresentFlag()) {
    READ_CODE_DESC(value, 8, "BmspsMeshCodecPrefixLengthMinus1");
    msps.setBmspsMeshCodecPrefixLengthMinus1(value);
    std::vector<uint8_t> codecPrefixData;
    for (uint32_t i = 0; i < (msps.getBmspsMeshCodecPrefixLengthMinus1() + 1);
         i++) {
      READ_CODE_DESC(value, 8, "BmspsMeshCodecPrefixData");
      codecPrefixData.push_back(value);
    }
    msps.setBmspsMeshCodecPrefixData(codecPrefixData);
  }
  READ_CODE_DESC(value, 1, "BmspsMotionCodecSpecificParametersPresentFlag");
  msps.setBmspsMotionCodecSpecificParametersPresentFlag(value);
  if (msps.getBmspsMotionCodecSpecificParametersPresentFlag()) {
    READ_CODE_DESC(value, 8, "BmspsMotionCodecPrefixLengthMinus1");
    msps.setBmspsMotionCodecPrefixLengthMinus1(value);
    std::vector<uint8_t> codecPrefixData;
    for (uint32_t i = 0;
         i < (msps.getBmspsMotionCodecPrefixLengthMinus1() + 1);
         i++) {
      READ_CODE_DESC(value, 8, "BmspsMotionCodecPrefixData");
      codecPrefixData.push_back(value);
    }
    msps.setBmspsMotionCodecPrefixData(codecPrefixData);
  }
  READ_CODE_DESC(value, 1, "BmspsVuiParametersPresentFlag");
  msps.setBmspsVuiParametersPresentFlag(value);
  if (msps.getBmspsVuiParametersPresentFlag()) {
    basemeshVuiParameters(bitstream, msps, msps.getVuiParameters());
  }

  READ_CODE_DESC(value, 1, "BmspsExtensionPresentFlag");
  msps.setBmspsExtensionPresentFlag(value);
  if (msps.getBmspsExtensionPresentFlag()) {
    READ_CODE_DESC(value, 8, "BmspsExtensionCount");
    msps.setBmspsExtensionCount(value);
  }
  if (msps.getBmspsExtensionCount()) {
    READ_UVLC_DESC(value, "ExtensionLengthMinus1");
    msps.setBmspsExtensionsLengthMinus1(value);  // ue(v)
    for (uint32_t i = 0; i < msps.getBmspsExtensionCount(); i++) {
      READ_CODE_DESC(value, 8, "ExtensionType");
      msps.setBmspsExtensionType(i, value);  // u(8)
      READ_CODE_DESC(value, 16, "ExtensionLength");
      msps.setBmspsExtensionLength(i, value);  // u(16)
      Bitstream tempBitstream;
#if defined(BITSTREAM_TRACE)
      tempBitstream.setTrace(true);
      tempBitstream.setLogger(bitstream.getLogger());
#endif
      bitstream.copyToBits(tempBitstream, msps.getBmspsExtensionLength(i));
      //baseMeshSpsExtension(msps, i, tempBitstream);
    }
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// Base mesh frame parameter set Rbsp
void
BaseMeshReader::baseMeshFrameParameterSetRbsp(
  BaseMeshFrameParameterSetRbsp& bmfps,
  std::vector<BaseMeshSequenceParameterSetRbsp>& /*bmsps*/,
  vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero  = 0;
  int32_t  value = 0;
  //auto pos0 = bitstream.size();
  READ_UVLC_DESC(value, "BfpsMeshSequenceParameterSetId ");
  assert(value >= 0 && value <= 15);
  bmfps.setBfpsMeshSequenceParameterSetId(value);
  READ_UVLC_DESC(value, "BfpsMeshFrameParameterSetId    ");
  assert(value >= 0 && value <= 63);
  bmfps.setBfpsMeshFrameParameterSetId(value);
  baseMeshSubmeshInformation(bmfps.getSubmeshInformation(), bitstream);

  READ_CODE_DESC(value, 1, "BfpsOutputFlagPresentFlag");
  bmfps.setBfpsOutputFlagPresentFlag(value);
  READ_UVLC_DESC(value, "BfpsNumRefIdxDefaultActiveMinus1");
  bmfps.setBfpsNumRefIdxDefaultActiveMinus1(value);
  READ_UVLC_DESC(value, "BfpsAdditionalLtMfocLsbLen");
  bmfps.setBfpsAdditionalLtMfocLsbLen(value);

  READ_CODE_DESC(value, 1, "BfpsExtensionPresentFlag");
  bmfps.setBfpsExtensionPresentFlag(value);
  if (bmfps.getBfpsExtensionPresentFlag()) {
    READ_CODE_DESC(value, 8, "BfpsExtension8Bits");
    bmfps.setBfpsExtension8Bits(value);
  }
  if (bmfps.getBfpsExtension8Bits()) {
    //while (moreRbspData(bitstream)) { READ_CODE_DESC(value, 1, "BfpsExtensionDataFlag");  ; bmfps.setBfpsExtensionDataFlag(value);}
  }
  rbspTrailingBits(bitstream);

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// Base mesh frame tile information
void
BaseMeshReader::baseMeshSubmeshInformation(BaseMeshSubmeshInformation& bmfti,
                                           vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_CODE_DESC(value, 6, "BmsiNumSubmeshesMinus1 ");  // u(6)
  bmfti.setBmsiNumSubmeshesMinus1(value);
  auto numSubmeshes =
    static_cast<size_t>(bmfti.getBmsiNumSubmeshesMinus1() + 1);
  READ_CODE_DESC(value, 1, "BmsiSignalledSubmeshIdFlag ");
  bmfti.setBmsiSignalledSubmeshIdFlag(value);
  if (bmfti.getBmsiSignalledSubmeshIdFlag()) {
    bmfti._submeshIndexToID.resize(numSubmeshes);
    READ_UVLC_DESC(value, "BmsiSignalledSubmeshIdDeltaLength ");
    bmfti.setBmsiSignalledSubmeshIdDeltaLength(value);
    uint32_t b = (uint32_t)ceil(log2(bmfti.getBmsiNumSubmeshesMinus1() + 1));
    for (size_t i = 0; i < numSubmeshes; i++) {
      READ_CODE_DESC(bmfti._submeshIndexToID[i],
                     bmfti.getBmsiSignalledSubmeshIdDeltaLength() + b,
                     "_submeshIndexToID[i]");
      if (bmfti._submeshIDToIndex.size() <= bmfti._submeshIndexToID[i])
        bmfti._submeshIDToIndex.resize(bmfti._submeshIndexToID[i] + 1, -1);
      bmfti._submeshIDToIndex[bmfti._submeshIndexToID[i]] = (uint32_t)i;
    }
  } else {
    bmfti.setBmsiSignalledSubmeshIdDeltaLength(0);
    bmfti._submeshIDToIndex.resize(numSubmeshes);
    bmfti._submeshIndexToID.resize(numSubmeshes);
    for (size_t i = 0; i < numSubmeshes; i++) {
      //bmfti.setBmsiSubmeshId( (uint32_t)i, i );
      bmfti._submeshIDToIndex[i] = (uint32_t)i;
      bmfti._submeshIndexToID[i] = (uint32_t)i;
      TRACE_BITSTREAM("\t%d-th ID %d\n", i, bmfti._submeshIndexToID[i]);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
size_t
BaseMeshReader::baseMeshSubmeshLayerRbsp(
  BaseMeshSubmeshLayer&                          bmsl,
  BaseMeshNalUnitType                            nalUnitType,
  uint64_t                                       nalUnitSize,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    bmsh        = bmsl.getSubmeshHeader();
  uint64_t smlRbspSize = nalUnitSize - 2;
  uint64_t smhSize =
    baseMeshSubmeshHeader(bmsh, nalUnitType, mfpsList, mspsList, bitstream);
  size_t duSize = baseMeshSubmeshDataUnit(
    bmsl, smlRbspSize, smhSize, mfpsList, mspsList, bitstream);
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM("submeshLayer Size : Header, Payload(%s) (%d, %d)\n",
                  bmsh.getSmhType() == I_BASEMESH ? "I" : "P",
                  smhSize,
                  duSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return smlRbspSize;
}
uint64_t
BaseMeshReader::baseMeshSubmeshHeader(
  BaseMeshSubmeshHeader&                         bmsh,
  BaseMeshNalUnitType                            nalUnitType,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  int32_t value;
  if (nalUnitType >= BASEMESH_NAL_BLA_W_LP
      && nalUnitType <= BASEMESH_NAL_RSV_BMCL_29) {
    READ_CODE_DESC(value, 1, "SmhNoOutputOfPriorSubmeshFramesFlag");
    bmsh.setSmhNoOutputOfPriorSubmeshFramesFlag(value);  // u(1)
  }

  auto pos = bitstream.getPosition().bytes_;
  //BaseMeshFrameParameterSetRbsp& mfps = mfpsList[ bmsh.getSmhSubmeshFrameParameterSetId() ]; //TODO: [PSIDX] order=index?
  //BaseMeshSequenceParameterSetRbsp& msps = mspsList[mfps.getBfpsMeshSequenceParameterSetId()];
  READ_UVLC_DESC(value, "SmhSubmeshFrameParameterSetId");
  assert(value >= 0 && value <= 63);
  bmsh.setSmhSubmeshFrameParameterSetId(value);
  int bmfpsIdx = -1;
  for (int i = 0; i < mfpsList.size(); i++) {
    if (mfpsList[i].getBfpsMeshFrameParameterSetId()
        == bmsh.getSmhSubmeshFrameParameterSetId()) {
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

  auto&    bmfti = mfps.getSubmeshInformation();
  uint32_t b     = (uint32_t)ceil(log2(bmfti.getBmsiNumSubmeshesMinus1() + 1));
  uint32_t v     = bmfti.getBmsiSignalledSubmeshIdDeltaLength() + b;
  if (bmfti.getBmsiSignalledSubmeshIdFlag()) {
    READ_CODE_DESC(value, v, "SmhId");
    bmsh.setSmhId(value);  //u(v)
  } else {
    if (bmfti.getBmsiNumSubmeshesMinus1() != 0) {
      READ_CODE_DESC(value, v, "SmhId");
      bmsh.setSmhId(value);  //u(v)
    } else {
      bmsh.setSmhId(0);  //u(v)
    }
  }

  READ_UVLC_DESC(value, "SmhType");
  bmsh.setSmhType((BaseMeshType)value);
  if (mfps.getBfpsOutputFlagPresentFlag()) {
    READ_CODE_DESC(value, 1, "SmhMeshOutputFlag");
    bmsh.setSmhMeshOutputFlag(value);
  } else {
    bmsh.setSmhMeshOutputFlag(1);
  }
  READ_CODE_DESC(value,
                 (msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4),
                 "SmhBasemeshFrmOrderCntLsb");
  bmsh.setSmhBasemeshFrmOrderCntLsb(value);  //u(v)

  if (msps.getBmspsNumRefMeshFrameListsInBmsps() > 0) {
    READ_CODE_DESC(value, 1, "SmhRefBasemeshFrameListMspsFlag");
    bmsh.setSmhRefBasemeshFrameListMspsFlag(value);
  } else bmsh.setSmhRefBasemeshFrameListMspsFlag(0);

  if (bmsh.getSmhRefBasemeshFrameListMspsFlag() == 0) {
    baseMeshRefListStruct(bmsh.getSmhRefListStruct(), msps, bitstream);
  } else if (msps.getBmspsNumRefMeshFrameListsInBmsps() > 1) {
    size_t bitCount = ceilLog2(msps.getBmspsNumRefMeshFrameListsInBmsps());
    //READ_CODE_DESC(value, bitCount, "SmhRefMeshFrameListIdx"); bmsh.setSmhRefMeshFrameListIdx(value );
    bmsh.setSmhRefMeshFrameListIdx(bitstream.read(bitCount));
  } else bmsh.setSmhRefMeshFrameListIdx(0);  //u(v)

  uint8_t rlsIdx  = bmsh.getSmhRefMeshFrameListIdx();
  auto&   refList = bmsh.getSmhRefBasemeshFrameListMspsFlag()
                      ? msps.getBmeshRefListStruct(rlsIdx)
                      : bmsh.getSmhRefListStruct();

  size_t NumLtrMeshFrmEntries = 0;
  for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
    if (!refList.getStRefMeshFrameFlag(i)) { NumLtrMeshFrmEntries++; }
  }
  for (size_t j = 0; j < NumLtrMeshFrmEntries; j++) {
    bmsh.setSmhAdditionalMfocLsbPresentFlag(bitstream.read(1), j);
    if (bmsh.getMshAdditionalMfocLsbPresentFlag(j)) {
      uint32_t v = mfps.getBfpsAdditionalLtMfocLsbLen();
      bmsh.setSmhAdditionalMfocLsbVal(bitstream.read(v), j);  //u(v)
    }
  }

  if (bmsh.getSmhType() != SKIP_BASEMESH) {
    if (bmsh.getSmhType() == P_BASEMESH && refList.getNumRefEntries() > 1) {
      bmsh.setSmhNumRefIdxActiveOverrideFlag(bitstream.read(1));
      if (bmsh.getMshNumRefIdxActiveOverrideFlag())
        bmsh.setSmhNumRefIdxActiveMinus1(bitstream.readUvlc());
    }
  }
  byteAlignment(bitstream);

  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

size_t
BaseMeshReader::baseMeshSubmeshDataUnit(
  BaseMeshSubmeshLayer&                          bmsl,
  uint64_t                                       smlRbspSize,
  uint64_t                                       smhSize,
  std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);

  BaseMeshSubmeshHeader&         bmsh = bmsl.getSubmeshHeader();
  BaseMeshFrameParameterSetRbsp& mfps = mfpsList
    [bmsh.getSmhSubmeshFrameParameterSetId()];  //TODO: [PSIDX] order=index?
  BaseMeshSequenceParameterSetRbsp& msps =
    mspsList[mfps.getBfpsMeshSequenceParameterSetId()];

  // simple extraction of the remaining part of the SODB from the RBSP
  // this enables to avoid explicitly signalling its size
  // which is only useful to ensure the subsequent linear parsing of rbspTrailingBits
  // using the current code

  // the implemented case assumes that the RBSP bit length is a multiple of 8 (length_alignement used)
  // and that the smh header is also aligned on byte boundaries
  // this could be extended to a generic SODB extraction where the count in bits in the last byte
  // is obtained by counting the number of left bit shift to nonNullByte till its value is 0x00

  const auto NalHeaderSize = 2;
  auto       trailingBytes = 0;
  auto       nonNullByte   = 0x00;
  do {
    nonNullByte =
      bitstream.peekByteAt(NalHeaderSize + smlRbspSize - ++trailingBytes);
  } while (nonNullByte
           == 0x00);  // security  would add '&& trailingBytes< smlRbspSize'
  // but a conformant stream would have less that precision bytes set to 0x00
  // we can check that the first non null value is 80 - for other cases TODO implement the generic parsing
  assert(nonNullByte == 0x80);  // bmsdu SODB is not aligned on byte boundary

  size_t duSize = smlRbspSize - smhSize - trailingBytes;

  if (bmsh.getSmhType() == I_BASEMESH) {
    BaseMeshSubmeshDataUnitIntra& bmsdu = bmsl.getSubmeshDataunitIntra();
    baseMeshSubmeshDataUnitIntra(bmsdu, duSize, bitstream);
    if (msps.getBmspsCodecSpecificParametersPresentFlag()) {
      bmsdu.setCodedMeshHeaderDataSize(
        (msps.getBmspsMeshCodecPrefixLengthMinus1() + 1));
      auto&                cdu          = bmsdu.getCodedMeshDataUnit();
      std::vector<uint8_t> mcPrefixData = msps.getBmspsMeshCodecPrefixData();
      cdu.insert(cdu.begin(), mcPrefixData.begin(), mcPrefixData.end());
      duSize += (msps.getBmspsMeshCodecPrefixLengthMinus1() + 1);
    }
  } else if (bmsh.getSmhType() == P_BASEMESH) {
    BaseMeshSubmeshDataUnitInter& bmsdu = bmsl.getSubmeshDataunitInter();
    baseMeshSubmeshDataUnitInter(bmsdu, bmsh, duSize, bitstream);
    if (msps.getBmspsMotionCodecSpecificParametersPresentFlag()) {
      bmsdu.setCodedMotionHeaderDataSize(
        (msps.getBmspsMotionCodecPrefixLengthMinus1() + 1));
      auto&                cdu          = bmsdu.getCodedMeshDataUnit();
      std::vector<uint8_t> mcPrefixData = msps.getBmspsMotionCodecPrefixData();
      cdu.insert(cdu.begin(), mcPrefixData.begin(), mcPrefixData.end());
      duSize += (msps.getBmspsMotionCodecPrefixLengthMinus1() + 1);
    }
  } else if (bmsh.getSmhType() == SKIP_BASEMESH) {
    BaseMeshSubmeshDataUnitInter& bmsdu = bmsl.getSubmeshDataunitInter();
    baseMeshSubmeshDataUnitSkip(bmsdu, duSize, bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
  return duSize;
}
uint64_t
BaseMeshReader::baseMeshSubmeshDataUnitIntra(
  BaseMeshSubmeshDataUnitIntra& meshdata,
  uint64_t                      smduSize,
  vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  meshdata.allocateMeshDataBuffer(smduSize);
  auto pos =
    bitstream.getPosition().bytes_;  //pos=bmesh_nalu_header + smesh_header
  bitstream.copyToBits(meshdata.getCodedMeshDataUnitBuffer(), smduSize);
  TRACE_BITSTREAM(
    "baseMeshSubmeshDataUnitIntra meshdata.getMeshDataSize(): %d(%d)\n",
    meshdata.getCodedMeshDataSize(),
    smduSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
BaseMeshReader::baseMeshSubmeshDataUnitInter(
  BaseMeshSubmeshDataUnitInter& meshdata,
  BaseMeshSubmeshHeader&        smh,
  uint64_t                      smduSize,
  vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM("%s \n", __func__);
  uint32_t sismu_inter_vertex_count = 0;
  auto     pos                      = bitstream.getPosition().bytes_;
  uint32_t value;
  size_t   numRefIdxActive = smh.getMshNumRefIdxActiveMinus1() + 1;
  if (numRefIdxActive > 1) {
    READ_UVLC_DESC(value, "ReferenceFrameIndex");
    meshdata.setReferenceFrameIndex(value);  // ue(v)
    byteAlignment(bitstream);
  }
  auto motionInfoSize = bitstream.getPosition().bytes_ - pos;
  pos                 = bitstream.getPosition().bytes_;
  auto motionDataSize = smduSize - motionInfoSize;
  TRACE_BITSTREAM("basemeshSubmeshDataUnitInter motion data size(): %d\n",
                  motionDataSize);
  meshdata.allocateCodedMeshDataUnit(motionDataSize);
  bitstream.copyToBits(meshdata.getCodedMeshDataUnitBuffer(), motionDataSize);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
BaseMeshReader::baseMeshSubmeshDataUnitSkip(
  BaseMeshSubmeshDataUnitInter& meshdata,
  uint64_t                      smduSize,
  vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos  = bitstream.getPosition().bytes_;
  auto pos2 = bitstream.getPosition().bytes_;
  TRACE_BITSTREAM_OUT("%s", __func__);
  return pos2 - pos;
}

// 8.3.3 Byte alignment
void
BaseMeshReader::byteAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5 NAL unit
// 8.3.5.1 General NAL unit
void
BaseMeshReader::nalUnit(vmesh::Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    READ_CODE(nalUnit.getData(i), 8);  // b(8)
  }
}

// 8.3.5.2 NAL unit header
void
BaseMeshReader::nalUnitHeader(vmesh::Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(zero, 1);                                         // f(1)
  READ_CODE_CAST(nalUnit.getType(), 6, BaseMeshNalUnitType);  // u(6)
  READ_CODE(nalUnit.getLayerId(), 6);                         // u(6)
  READ_CODE(nalUnit.getTemporalyIdPlus1(), 3);                // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
BaseMeshReader::seiRbsp(vmesh::Bitstream&      bitstream,
                        BaseMeshNalUnitType    nalUnitType,
                        basemesh::BaseMeshSei& sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  do {
    seiMessage(bitstream, nalUnitType, sei);
  } while (moreRbspData(bitstream));
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.5 Access unit delimiter RBSP
void
BaseMeshReader::accessUnitDelimiterRbsp(AccessUnitDelimiterRbsp& aud,
                                        vmesh::Bitstream&        bitstream) {
  READ_CODE(aud.getAframeType(), 3);  //	u(3)
  rbspTrailingBits(bitstream);
}
// 8.3.6.6 End of sequence RBSP
void
BaseMeshReader::endOfSequenceRbsp(EndOfSequenceRbsp& eosbsp,
                                  vmesh::Bitstream&  bitstream) {}

// 8.3.6.7 End of bitstream RBSP
void
BaseMeshReader::endOfAtlasSubBitstreamRbsp(EndOfAtlasSubBitstreamRbsp& eoasb,
                                           vmesh::Bitstream& bitstream) {}

// 8.3.6.8 Filler data RBSP syntax
void
BaseMeshReader::fillerDataRbsp(FillerDataRbsp&   fdrbsp,
                               vmesh::Bitstream& bitstream) {
  // while ( next_bits( bitstream, 8 ) == 0xFF ) { uint32_t code; READ_CODE( code, 8 );  // f(8)
  rbspTrailingBits(bitstream);
}

// 8.3.6.10 RBSP trailing bit syntax
void
BaseMeshReader::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
BaseMeshReader::seiMessage(vmesh::Bitstream&      bitstream,
                           BaseMeshNalUnitType    nalUnitType,
                           basemesh::BaseMeshSei& sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  int32_t payloadType = 0;
  int32_t payloadSize = 0;
  int32_t byte        = 0;
  do {
    READ_CODE(byte, 8);  // u(8)
    payloadType += byte;
  } while (byte == 0xff);
  do {
    READ_CODE(byte, 8);  // u(8)
    payloadSize += byte;
  } while (byte == 0xff);
  seiPayload(bitstream,
             nalUnitType,
             static_cast<BaseMeshSeiPayloadType>(payloadType),
             payloadSize,
             sei);
}

// D.2.1 Sample stream NAL header
void
BaseMeshReader::sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                                      vmesh::SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  READ_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23090-29:H.14.1.1 General SEI message
void
BaseMeshReader::seiPayload(vmesh::Bitstream&      bitstream,
                           BaseMeshNalUnitType    nalUnitType,
                           BaseMeshSeiPayloadType payloadType,
                           size_t                 payloadSize,
                           basemesh::BaseMeshSei& seiList) {
  TRACE_BITSTREAM_IN("%s", __func__);
  basemesh::SEI& sei = seiList.addSei(nalUnitType, payloadType);
  printf("        seiMessage: type = %d %s payloadSize = %zu \n",
         payloadType,
         toString(payloadType).c_str(),
         payloadSize);
  fflush(stdout);
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
      // TODO: reservedMessage(bitstream, sei, payloadSize);
    }
  } else {
    if (payloadType == BASEMESH_FILLER_PAYLOAD) {                      // 2
    } else if (payloadType == BASEMESH_USER_DATAREGISTERED_ITUTT35) {  // 3
    } else if (payloadType == BASEMESH_USER_DATA_UNREGISTERED) {       // 4
    } else {
      // TODO: reservedMessage(bitstream, sei, payloadSize);
    }
  }
  if (moreDataInPayload(bitstream)) {
    if (payloadExtensionPresent(bitstream)) {
      uint32_t zero = 0;
      READ_CODE(zero, 1);  // u(v)
    }
    byteAlignment(bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.15.2.1  VUI parameters
void
BaseMeshReader::basemeshVuiParameters(vmesh::Bitstream& bitstream,
                                      BaseMeshSequenceParameterSetRbsp bmsps,
                                      BasemeshVuiParameters&           vp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
  if (vp.getTimingInfoPresentFlag()) {
    READ_CODE(vp.getNumUnitsInTick(), 32);               // u(32)
    READ_CODE(vp.getTimeScale(), 32);                    // u(32)
    READ_CODE(vp.getMfocProportionalToTimingFlag(), 1);  // u(1)
    if (vp.getMfocProportionalToTimingFlag()) {
      READ_UVLC(vp.getNumTicksMfocDiffOneMinus1());  // ue(v)
    }
    READ_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
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
BaseMeshReader::basemeshHrdParameters(vmesh::Bitstream& bitstream,
                                      bool              commonInPresentFlag,
                                      int               maxSubLayersMinus1,
                                      BaseMeshHrdParameters& hp) {
  TRACE_BITSTREAM_IN("%s", __func__);

  if (commonInPresentFlag) {
    READ_CODE(hp.getBmNalParametersPresentFlag(), 1);  // u(1)
    READ_CODE(hp.getBmClParametersPresentFlag(), 1);   // u(1)
    if (hp.getBmNalParametersPresentFlag()
        || hp.getBmClParametersPresentFlag()) {
      READ_CODE(hp.getSubmeshHrdParametersPresentFlag(), 1);  // u(1)
      if (hp.getSubmeshHrdParametersPresentFlag()) {
        READ_CODE(hp.getBmTickDivisorMinus2(), 8);                      // u(8)
        READ_CODE(hp.getDuCbmbRemovalDelayIncrementLengthMinus1(), 5);  // u(5)
        READ_CODE(hp.getSubmeshCbmbParamsInBmTimingSeiFlag(), 1);       // u(1)
        READ_CODE(hp.getDbmbOutputDuDelayLengthMinus1(), 5);            // u(5)
      }
      READ_CODE(hp.getBmBitRateScale(), 4);  // u(4)
      READ_CODE(hp.getCbmbSizeScale(), 4);   // u(4)
      if (hp.getSubmeshHrdParametersPresentFlag())
        READ_CODE(hp.getCbmbSizeDuScale(), 4);                    // u(4)
      READ_CODE(hp.getInitialCbmbRemovalDelayLengthMinus1(), 5);  // u(5)
      READ_CODE(hp.getAuCbmbRemovalDelayLengthMinus1(), 5);       // u(5)
      READ_CODE(hp.getdbmbOutputDelayLengthMinus1(), 5);          // u(5)
    }
  }

  for (size_t i = 0; i <= maxSubLayersMinus1; i++) {
    READ_CODE(hp.getBmFixedRateGeneralFlag(i), 1);  // u(1)
    if (!hp.getBmFixedRateGeneralFlag(i)) {
      READ_CODE(hp.getBmFixedRateWithinCbmsFlag(i), 1);  // u(1)
    }
    if (hp.getBmFixedRateWithinCbmsFlag(i)) {
      READ_CODE(hp.getBmElementalDurationInTcMinus1(i), 1);  // ue(v)
    } else {
      READ_CODE(hp.getBmLowDelayFlag(i), 1);  // u(1)
    }
    if (!hp.getBmLowDelayFlag(i)) {
      READ_CODE(hp.getCbmdCntMinus1(i), 1);  // ue(v)
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
BaseMeshReader::basemeshHrdSubLayerParameters(
  vmesh::Bitstream&              bitstream,
  BaseMeshHrdParameters&         hp,
  BaseMeshHrdSubLayerParameters& hlsp,
  size_t                         cabCnt) {
  TRACE_BITSTREAM_IN("%s", __func__);
  hlsp.allocate(cabCnt + 1);
  for (size_t i = 0; i <= cabCnt; i++) {
    READ_UVLC(hlsp.getBmBitRateValueMinus1(i));  // ue(v)
    READ_UVLC(hlsp.getCmbmSizeValueMinus1(i));   // ue(v)
    if (hp.getSubmeshHrdParametersPresentFlag()) {
      READ_UVLC(hlsp.getCmbmSizeDuValueMinus1(i));   // ue(v)
      READ_UVLC(hlsp.getBmBitRateDuValueMinus1(i));  // ue(v)
    }
    READ_CODE(hlsp.getBmCbrFlag(i), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23090-29:H.14.1.10 Basemesh Attribute transformation parameters SEI message
void
BaseMeshReader::baseMeshAttributeTransformationParams(
  vmesh::Bitstream& bitstream,
  basemesh::SEI&    seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei =
    static_cast<basemesh::BMSEIAttributeTransformationParams&>(seiAbstract);
  READ_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    READ_UVLC(sei.getNumAttributeUpdates());  // ue(v)
    sei.allocate();
    for (size_t j = 0; j < sei.getNumAttributeUpdates(); j++) {
      READ_CODE(sei.getAttributeIdx(j), 8);  // u(8)
      size_t index = sei.getAttributeIdx(j);
      READ_CODE(sei.getDimensionMinus1(index), 8);  // u(8)
      sei.allocate(index);
      for (size_t i = 0; i <= sei.getDimensionMinus1(index); i++) {
        READ_CODE(sei.getScaleParamsEnabledFlag(index, i), 1);   // u(1)
        READ_CODE(sei.getOffsetParamsEnabledFlag(index, i), 1);  // u(1)
        if (sei.getScaleParamsEnabledFlag(index, i)) {
          READ_CODE(sei.getAttributeScale(index, i), 32);  // u(32)
        } else {
          sei.getAttributeScale(index, i) = (1 << 16);
        }
        if (sei.getOffsetParamsEnabledFlag(index, i)) {
          READ_CODES(sei.getAttributeOffset(index, i), 32);  // i(32)
        } else {
          sei.getAttributeOffset(index, i) = 0;
        }
      }
    }
    READ_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
