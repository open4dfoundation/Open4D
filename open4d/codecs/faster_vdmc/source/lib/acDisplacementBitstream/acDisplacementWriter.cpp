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


#include "bitstream.hpp"
#include "acDisplacementWriter.hpp"
#include "writerCommon.hpp"
#include <list>

using namespace acdisplacement;

AcDisplacementWriter::AcDisplacementWriter()  = default;
AcDisplacementWriter::~AcDisplacementWriter() = default;

bool
AcDisplacementWriter::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AcDisplacementWriter::lengthAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AcDisplacementWriter::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
AcDisplacementWriter::moreRbspData(vmesh::Bitstream& bitstream) {
  return false;
}
bool

AcDisplacementWriter::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

void
AcDisplacementWriter::encode(AcDisplacementBitstream& acDisplacementBitstream,
                             vmesh::Bitstream&        bitstream,
                             AcDisplacementBitstreamGofStat&    bitstreamStat) {
  TRACE_BITSTREAM_IN("%s", __func__);
  vmesh::SampleStreamNalUnit ssnu;
  uint64_t            maxNaluSize = 0;
  struct bmNalUnit {
    uint64_t         naluTotSize;
    DisplNalUnitType nuType;
    vmesh::Bitstream tempBitStream;
  };
  std::vector<bmNalUnit> displNaluList;
  const size_t           nalHeaderSize = 2;
  for (auto& dsps :
       acDisplacementBitstream.getDisplSequenceParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(DISPLACEMENT_NAL_BMSPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    displSequenceParameterSetRbsp(dsps, tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    displNaluList.push_back(
      bmNalUnit({naluSize, DISPLACEMENT_NAL_BMSPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
  }
  int32_t frmOrderCntVal = -1;
  for (auto& dfps : acDisplacementBitstream.getDisplFrameParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(DISPLACEMENT_NAL_BMFPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    displFrameParameterSetRbsp(
      dfps,
      acDisplacementBitstream.getDisplSequenceParameterSetList(),
      tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    displNaluList.push_back(
      bmNalUnit({naluSize, DISPLACEMENT_NAL_BMFPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
  }
  for (auto& dsl : acDisplacementBitstream.getDisplacementLayerList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    auto& dsh = dsl.getDisplHeader();
    auto& dfp =
      acDisplacementBitstream
        .getDisplFrameParameterSetList()[dsh.getDisplFrameParameterSetId()];

    auto naluType = DISPLACEMENT_NAL_IDR_N_LP;
    if (dsh.getDisplType() == I_DISPL) {
      naluType = DISPLACEMENT_NAL_IDR_N_LP;
    }
    if (dsh.getDisplType() == P_DISPL) { naluType = DISPLACEMENT_NAL_TRAIL_N; }

    vmesh::NalUnit nu(naluType, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    TRACE_BITSTREAM("NALU(displacementLayer) naluHeader: %d\n",
                    (tempBitStream.size()));
    displacementLayerRbsp(
      dsl,
      naluType,
      acDisplacementBitstream.getDisplFrameParameterSetList(),
      acDisplacementBitstream.getDisplSequenceParameterSetList(),
      tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    TRACE_BITSTREAM("NALU(displacementLayer) naluSize: %d\n", naluSize);
    bitstreamStat.setDisplacement((DisplacementType)dsh.getDisplType(),
                                  naluSize - 2);  //2:header size

    displNaluList.push_back(bmNalUnit({naluSize, naluType, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
  }

  // Calculation of the max unit size done
  ssnu.getSizePrecisionBytesMinus1() =
    std::min(std::max(int(ceil(double(vmesh::ceilLog2(maxNaluSize + 1)) / 8.0)), 1),
             8)
    - 1;
  sampleStreamNalHeader(bitstream, ssnu);

  // Write sample streams
  for (auto& nu : displNaluList) {
    TRACE_BITSTREAM("displacement nalu........%s\t%d\n",
                    toString((DisplNalUnitType)nu.nuType).c_str(),
                    nu.naluTotSize);
    WRITE_CODE(nu.naluTotSize, 8 * (ssnu.getSizePrecisionBytesMinus1() + 1));
    bitstream.copyFrom(nu.tempBitStream, 0, nu.naluTotSize);
    auto type = toString(nu.nuType);
    //copyFrom changes the bytePosition of nu.tempBitstream.
    printf("displacement NalUnit Type = %-25s size = %zu \n",
           type.c_str(),
           (size_t)nu.naluTotSize);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
AcDisplacementWriter::displacementLayerRbsp(
  DisplacementLayer&                          dl,
  DisplNalUnitType                            nalUnitType,
  std::vector<DisplFrameParameterSetRbsp>&    dfpsList,
  std::vector<DisplSequenceParameterSetRbsp>& dspsList,
  vmesh::Bitstream&                           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    dh = dl.getDisplHeader();
  uint64_t dispHeaderSize =
    displacementHeader(dh, nalUnitType, dfpsList, dspsList, bitstream);
  uint64_t dispPayloadSize =
    displacementDataUnit(dl, dfpsList, dspsList, bitstream);
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM("displacementLayer Size : Header, Payload (%d, %d)\n",
                  dispHeaderSize,
                  dispPayloadSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint64_t
AcDisplacementWriter::displacementHeader(
  DisplacementHeader&                         dh,
  DisplNalUnitType                            nalUnitType,
  std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos = bitstream.getPosition().bytes_;
  //DisplFrameParameterSetRbsp& dfps =
  //  mfpsList[dh.getDisplFrameParameterSetId()];  //TODO: [PSIDX] order=index?
  //DisplSequenceParameterSetRbsp& dsps =
  //  mspsList[dfps.getDfpsDisplSequenceParameterSetId()];

  if (nalUnitType >= DISPLACEMENT_NAL_BLA_W_LP
      && nalUnitType <= DISPLACEMENT_NAL_RSV_BMCL_29) {
    WRITE_CODE(dh.getNoOutputOfPriorDisplFramesFlag(), 1);
  }
  WRITE_UVLC(dh.getDisplFrameParameterSetId());
  int dfpsIdx = -1;
  for (int i = 0; i < mfpsList.size(); i++) {
    if (mfpsList[i].getDfpsDisplFrameParameterSetId()
        == dh.getDisplFrameParameterSetId()) {
      dfpsIdx = i;
      break;
    }
  }
  if (dfpsIdx < 0)
    throw std::runtime_error(
      "No matching parameter set ID found in mfpsList.");
  DisplFrameParameterSetRbsp& dfps    = mfpsList[dfpsIdx];
  int                         dspsIdx = -1;
  for (int i = 0; i < mspsList.size(); i++) {
    if (mspsList[i].getDspsSequenceParameterSetId()
        == dfps.getDfpsDisplSequenceParameterSetId()) {
      dspsIdx = i;
      break;
    }
  }
  if (dspsIdx < 0)
    throw std::runtime_error(
      "No matching parameter set ID found in mspsList.");
  DisplSequenceParameterSetRbsp& dsps = mspsList[dspsIdx];
  uint32_t                       b    = (uint32_t)ceil(
    log2(dfps.getDisplInformation().getDiNumDisplsMinus2() + 2));
  uint32_t v =
    dfps.getDisplInformation().getDiSignalledDisplIdDeltaLength() + b;
  WRITE_CODE(dh.getDisplId(), v);  // u(v)
  WRITE_UVLC(dh.getDisplType());
  if (dfps.getDfpsOutputFlagPresentFlag())
    WRITE_CODE(dh.getDisplOutputFlag(), 1);
  WRITE_CODE(dh.getDisplFrmOrderCntLsb(),
             (dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4));
  if (dsps.getDspsNumRefDisplFrameListsInDsps() > 0)
    WRITE_CODE(dh.getRefDisplFrameListDspsFlag(), 1);
  if (dh.getRefDisplFrameListDspsFlag() == 0)
    refListStruct(dh.getDisplRefListStruct(), dsps, bitstream);
  else if (dsps.getDspsNumRefDisplFrameListsInDsps() > 1) {
    size_t bitCount = ceilLog2(dsps.getDspsNumRefDisplFrameListsInDsps());
    WRITE_CODE(dh.getRefDisplFrameListIdx(), bitCount);
  }

  uint8_t rlsIdx  = dh.getRefDisplFrameListIdx();
  auto&   refList = dh.getRefDisplFrameListDspsFlag()
                      ? dsps.getDisplRefListStruct()[rlsIdx]
                      : dh.getDisplRefListStruct();

  size_t NumLtrMeshFrmEntries = 0;
  for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
    if (!refList.getStRefdisplFrameFlag(i)) { NumLtrMeshFrmEntries++; }
  }
  for (size_t j = 0; j < NumLtrMeshFrmEntries; j++) {
    WRITE_CODE(dh.getAdditionalDfocLsbPresentFlag()[j], 1);
    if (dh.getAdditionalDfocLsbPresentFlag()[j])
      WRITE_CODE(dh.getAdditionalDfocLsbVal()[j],
                 dfps.getDfpsAdditionalLtDfocLsbLen());
  }

    uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
    WRITE_CODE(dh.getDhParametersOverrideFlag(), 1);
    if (dh.getDhParametersOverrideFlag()) {
      WRITE_CODE(dh.getDhSubdivisionOverrideFlag(), 1);
      WRITE_CODE(dh.getDhQuantizationOverrideFlag(), 1);
    }

    if (dh.getDhSubdivisionOverrideFlag()) {
      WRITE_CODE(dh.getDhSubdivisionIterationCount(), 3);
    }
    for (size_t i = 0; i < dh.getDhSubdivisionIterationCount() + 1; i++) {
      WRITE_UVLC(dh.getDhVertexCountLod()[i]);
      WRITE_CODE(dh.getDhLodSplitFlag(i), 1);
      if (dh.getDhLodSplitFlag(i)) {
        WRITE_CODE(dh.getDhNumSubblockLodMinus1()[i], 8);
      }
    }
    if (dh.getDhQuantizationOverrideFlag()) {
      displQuantizationParameters(bitstream,
                                  dh.getDhQuantizationParameters(),
                                  dh.getDhSubdivisionIterationCount(),
                                  numComp);
    }
    if (dsps.getDspsInverseQuantizationOffsetPresentFlag()) {
      WRITE_CODE(dh.getDisplIQOffsetFlag(), 1);  //u(1)
      if (dh.getDisplIQOffsetFlag()) {
        for (int i = 0; i < dh.getDhSubdivisionIterationCount() + 1; i++) {
          for (int j = 0; j < numComp; j++) {
            for (
              int k = 0; k < 3;
              k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
              WRITE_CODE(dh.getIQOffsetValues(i, j, k, 0), 1);  //sign
              WRITE_SVLC(dh.getIQOffsetValues(i, j, k, 1));  //first precision
              WRITE_SVLC(dh.getIQOffsetValues(i, j, k, 2));  //second precision
            }
          }
        }
      }
    }

    if (dh.getDisplType() == P_DISPL) {
      if (refList.getNumRefEntries() > 1) {
        WRITE_CODE(dh.getNumRefIdxActiveOverrideFlag(), 1);
        if (dh.getNumRefIdxActiveOverrideFlag())
          WRITE_UVLC(dh.getNumRefIdxActiveMinus1());
      }
      WRITE_CODE(dh.getDhLayerInterDisableFlag(), 1);
      if (!dh.getDhLayerInterDisableFlag()) {
        for (int lod = 0; lod < dh.getDhSubdivisionIterationCount() + 1;
             lod++) {
          WRITE_CODE(dh.getDhLoDInter(lod), 1);
        }
      }
    }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
AcDisplacementWriter::displacementDataUnit(
  DisplacementLayer&                          dl,
  std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto                        pos = bitstream.getPosition().bytes_;
  DisplacementHeader&         dh  = dl.getDisplHeader();
  DisplFrameParameterSetRbsp& dfps =
    mfpsList[dh.getDisplFrameParameterSetId()];  //TODO: [PSIDX] order=index?
  DisplSequenceParameterSetRbsp& dsps =
    mspsList[dfps.getDfpsDisplSequenceParameterSetId()];

  if (dh.getDisplType() == I_DISPL) {
    auto& du = dl.getDisplDataunitIntra();
    WRITE_UVLC(du.getPayloadSize());
    displacementDataUnitIntra(
      du,
      bitstream);  //TODO: does this correspond to J.7.1.2.6	Displacement data unit syntax
  } else if (dh.getDisplType() == P_DISPL) {
    auto& du = dl.getDisplDataunitInter();
    WRITE_UVLC(du.getPayloadSize());
    displacementDataUnitInter(
      du, bitstream);  //TODO: this is not present in the WD5.0
  }
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint64_t
AcDisplacementWriter::displacementDataUnitInter(
  DisplacementDataInter& displdata,
  vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos = bitstream.getPosition().bytes_;
  TRACE_BITSTREAM("displacementDataUnitInter CodedDisplDataSize(): %d\n",
                  displdata.getCodedDisplDataSize());
  bitstream.copyFromBits(displdata.getCodedDisplDataUnitBuffer(),
                         0,
                         displdata.getCodedDisplDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
AcDisplacementWriter::displacementDataUnitIntra(
  DisplacementDataIntra& displdata,
  vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos = bitstream.getPosition().bytes_;
  TRACE_BITSTREAM("displacementDataUnitIntra CodedDisplDataSize(): %d\n",
                  displdata.getCodedDisplDataSize());
  bitstream.copyFromBits(displdata.getCodedDisplDataUnitBuffer(),
                         0,
                         displdata.getCodedDisplDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

void
AcDisplacementWriter::displInformation(DisplInformation& di,
                                       vmesh::Bitstream&        bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(di.getDiUseSingleDisplFlag(), 1);  // u(1)
  if (!di.getDiUseSingleDisplFlag()) {
    WRITE_UVLC(di.getDiNumDisplsMinus2());  // ue(v)
  }
  WRITE_CODE(di.getDiSignalledDisplIdFlag(), 1);  // u(1)
  if (di.getDiSignalledDisplIdFlag()) {
    WRITE_UVLC(di.getDiSignalledDisplIdDeltaLength());  // ue(v)
    auto     numDispls = static_cast<size_t>(di.getDiNumDisplsMinus2() + 2);
    uint32_t b         = static_cast<uint32_t>(ceil(log2(numDispls)));
    for (size_t i = 0; i < numDispls; i++) {
      uint8_t bitCount = di.getDiSignalledDisplIdDeltaLength() + b;
      WRITE_CODE(di.getDiDisplId(i), bitCount);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
uint32_t
AcDisplacementWriter::displFrameParameterSetRbsp(
  DisplFrameParameterSetRbsp&                 dfps,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto                           pos0 = bitstream.size();
  DisplSequenceParameterSetRbsp& dsps =
    mspsList[dfps.getDfpsDisplSequenceParameterSetId()];

  WRITE_UVLC(dfps.getDfpsDisplSequenceParameterSetId());
  WRITE_UVLC(dfps.getDfpsDisplFrameParameterSetId());
  displInformation(dfps.getDisplInformation(), bitstream);
  WRITE_CODE(dfps.getDfpsOutputFlagPresentFlag(), 1);
  WRITE_UVLC(dfps.getDfpsNumRefIdxDefaultActiveMinus1());
  WRITE_UVLC(dfps.getDfpsAdditionalLtDfocLsbLen());
  uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
  WRITE_CODE(dfps.getDfpsOverridenFlag(), 1);
  if (dfps.getDfpsOverridenFlag()) {
    WRITE_CODE(dfps.getDfpsSubdivisionEnableFlag(), 1);
    WRITE_CODE(dfps.getDfpsQuantizationParametersEnableFlag(), 1);
  }

  if (dfps.getDfpsSubdivisionEnableFlag()) {
    WRITE_CODE(dfps.getDfpsSubdivisionIterationCount(), 3);
  }
  if (dfps.getDfpsQuantizationParametersEnableFlag())
    displQuantizationParameters(bitstream,
                                dfps.getDfpsQuantizationParameters(),
                                dfps.getDfpsSubdivisionIterationCount(),
                                numComp);
  WRITE_CODE(dfps.getDfpsExtensionPresentFlag(), 1);
  if (dfps.getDfpsExtensionPresentFlag())
    WRITE_CODE(dfps.getDfpsExtension8bits(), 8);
  if (dfps.getDfpsExtension8bits()) {
    // while (more_rbsp_data())
    //    WRITE_CODE( dfps.getDfpsExtensionDataFlag(), 1);
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}
void
AcDisplacementWriter::refListStruct(DisplRefListStruct&            rls,
                                    DisplSequenceParameterSetRbsp& dsps,
                                    vmesh::Bitstream&                     bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(rls.getNumRefEntries());  // ue(v)
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (dsps.getDspsLongTermRefDisplFramesFlag()) {
      WRITE_CODE(rls.getStRefdisplFrameFlag(i), 1);
    }  // u(1)

    if (rls.getStRefdisplFrameFlag(i)) {
      bitstream.writeUvlc(rls.getAbsDeltaDfocSt(i));  // ue(v)
      if (rls.getAbsDeltaDfocSt(i) > 0) {
        WRITE_CODE(rls.getStrafEntrySignFlag(i), 1);  // u(1)
      }
    } else {
      uint8_t bitCount = dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4;
      WRITE_CODE(rls.getDfocLsbLt(i), bitCount);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
AcDisplacementWriter::dispProfileToolsetConstraintsInformation(
  DisplProfileToolsetConstraintsInformation& dptci,
  vmesh::Bitstream&                                 bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(dptci.getDptcOneDisplacementFrameOnlyFlag(), 1);
  WRITE_CODE(dptci.getDptcIntraFramesOnlyFlag(), 1);
  WRITE_CODE(dptci.getDptcReservedZero6bits(),6);  
  WRITE_CODE(dptci.getDptcNumReservedConstraintBytes(), 8);
  for (size_t i = 0; i < dptci.getDptcNumReservedConstraintBytes(); i++) {
    WRITE_CODE(dptci.getDptcReservedConstraintByte()[i], 8);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint32_t
AcDisplacementWriter::displProfileTierLevel(DisplProfileTierLevel& dpftl,
                                            vmesh::Bitstream&             bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(dpftl.getDptlTierFlag(), 1);
  WRITE_CODE(dpftl.getDptlProfileToolsetIdc(), 8);
  WRITE_CODE(dpftl.getDptlReservedZero32bits(), 32);
  WRITE_CODE(dpftl.getDptlLevelIdc(), 8);
  WRITE_CODE(dpftl.getDptlNumSubProfiles(), 6);
  WRITE_CODE(dpftl.getDptlExtendedSubProfileFlag(), 1);
  int v = dpftl.getDptlExtendedSubProfileFlag() == 0 ? 32 : 64;
  for (size_t i = 0; i < dpftl.getDptlNumSubProfiles(); i++) {
    WRITE_CODE(dpftl.getDptlSubProfileIdc()[i], v);
  }
  WRITE_CODE(dpftl.getDptlToolsetConstraintsPresentFlag(), 1);
  if (dpftl.getDptlToolsetConstraintsPresentFlag())
    dispProfileToolsetConstraintsInformation(
      dpftl.getDptlProfileToolsetConstraintsInformation(), bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return 0;
}
uint32_t
AcDisplacementWriter::displSequenceParameterSetRbsp(
  DisplSequenceParameterSetRbsp& dsps,
  vmesh::Bitstream&                     bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();
  WRITE_CODE(dsps.getDspsSequenceParameterSetId(), 4);
  WRITE_CODE(dsps.getDspsMaxSubLayersMinus1(), 3);
  displProfileTierLevel(dsps.getDspsProfileTierLevel(), bitstream);
  WRITE_CODE(dsps.getDspsSingleDimensionFlag(), 1);
  WRITE_UVLC(dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4());
  for (size_t i = 0; i < dsps.getDspsMaxSubLayersMinus1() + 1; i++) {
    WRITE_UVLC(dsps.getDspsMaxDecDisplFrameBufferingMinus1(i));
    WRITE_UVLC(dsps.getDspsMaxNumReorderFrames(i));
    WRITE_UVLC(dsps.getDspsMaxLatencyIncreasePlus1(i));
  }
  WRITE_CODE(dsps.getDspsLongTermRefDisplFramesFlag(), 1);
  WRITE_UVLC(dsps.getDspsNumRefDisplFrameListsInDsps());
  for (size_t i = 0; i < dsps.getDspsNumRefDisplFrameListsInDsps(); i++) {
    refListStruct(dsps.getDisplRefListStruct()[i], dsps, bitstream);
  }
  WRITE_CODE(dsps.getDspsGeometry3dBitdepthMinus1(), 5);

  WRITE_CODE(dsps.getDspsSubdivisionPresentFlag(), 1);
  if (dsps.getDspsSubdivisionPresentFlag()) {
    WRITE_CODE(dsps.getDspsSubdivisionIterationCount(), 3);
  }
  WRITE_SVLC(dsps.getDspsDisplacementReferenceQPMinus49());  // se(v)
  uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
  WRITE_CODE(dsps.getDspsInverseQuantizationOffsetPresentFlag(), 1);
  displQuantizationParameters(bitstream,
                              dsps.getDspsQuantizationParameters(),
                              dsps.getDspsSubdivisionIterationCount(),
                              numComp);
  WRITE_CODE(dsps.getDspsVuiParametersPresentFlag(), 1);
  if (dsps.getDspsVuiParametersPresentFlag()) {
      displacementVuiParameters(bitstream, dsps, dsps.getVuiParameters());
  }
  WRITE_CODE(dsps.getDspsExtensionPresentFlag(), 1);
  if (dsps.getDspsExtensionPresentFlag())
    WRITE_CODE(dsps.getDspsExtensionCount(), 8);
  if (dsps.getDspsExtensionCount()) {
    WRITE_UVLC(dsps.getDspsExtensionsLengthMinus1());
    for (uint32_t i = 0; i < dsps.getDspsExtensionCount(); i++) {
      vmesh::Bitstream tempBitstream;
      //DspsExtension(dsps, i, tempBitstream);
      dsps.setDspsExtensionLength(i, tempBitstream.size());
      WRITE_CODE(dsps.getDspsExtensionType(i), 8);
      WRITE_CODE(dsps.getDspsExtensionLength(i), 16);
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

void
AcDisplacementWriter::displQuantizationParameters(
  vmesh::Bitstream&                   bitstream,
  DisplQuantizationParameters& qp,
  uint32_t                     subdivIterationCount,
  uint8_t                      numComp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(qp.getDisplLodQuantizationFlag(), 1);
  WRITE_SVLC(qp.getDisplBitDepthOffset());

  if (qp.getDisplLodQuantizationFlag() == 0) {
    for (int i = 0; i < numComp; i++) {
      WRITE_CODE(qp.getDisplQuantizationParameters()[i], 7);
      WRITE_CODE(qp.getDisplLog2LodInverseScale()[i], 2);
    }
  } else {
    for (int i = 0; i < subdivIterationCount + 1; i++) {
      for (int j = 0; j < numComp; j++) {
        WRITE_UVLC(qp.getDisplLodDeltaQPValue(i, j));
        if (qp.getDisplLodDeltaQPValue(i, j)) {
          WRITE_CODE(qp.getDisplLodDeltaQPSign(i, j), 1);
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// J.13  VUI syntax
// J.13.2.1  VUI parameters syntax
void
AcDisplacementWriter::displacementVuiParameters(
    vmesh::Bitstream& bitstream,
    DisplSequenceParameterSetRbsp displsps, 
    DisplVuiParameters& vp) {
    TRACE_BITSTREAM_IN("%s", __func__);
    WRITE_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
    if (vp.getTimingInfoPresentFlag()) {
        WRITE_CODE(vp.getNumUnitsInTick(), 32);              // u(32)
        WRITE_CODE(vp.getTimeScale(), 32);                   // u(32)
        WRITE_CODE(vp.getMfocProportionalToTimingFlag(), 1);  // u(1)
        if (vp.getMfocProportionalToTimingFlag()) {
            WRITE_UVLC(vp.getNumTicksMfocDiffOneMinus1());  // ue(v)
        }
        WRITE_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
        if (vp.getHrdParametersPresentFlag()) {
            displacementHrdParameters(bitstream, true, displsps.getDspsMaxSubLayersMinus1(), vp.getHrdParameters());
        }
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// J.13.2.2	AC displacement HRD parameters syntax x
void
AcDisplacementWriter::displacementHrdParameters(
    vmesh::Bitstream& bitstream,
    bool commonInPresentFlag,
    int maxSubLayersMinus1,
    DisplHrdParameters& hp) {
    TRACE_BITSTREAM_IN("%s", __func__);

    if (commonInPresentFlag) {
        WRITE_CODE(hp.getDisplNalParametersPresentFlag(), 1);  // u(1)
        WRITE_CODE(hp.getDisplClParametersPresentFlag(), 1);  // u(1)
        if (hp.getDisplNalParametersPresentFlag() || hp.getDisplClParametersPresentFlag()) {
            WRITE_CODE(hp.getDisplBitRateScale(), 4);  // u(4)
            WRITE_CODE(hp.getCdisplbSizeScale(), 4);  // u(4)
            WRITE_CODE(hp.getInitialCdisplbRemovalDelayLengthMinus1(), 5);  // u(5)
            WRITE_CODE(hp.getAuCdisplbRemovalDelayLengthMinus1(), 5);  // u(5)
            WRITE_CODE(hp.getDdisplbOutputDelayLengthMinus1(), 5);  // u(5)
        }
    }

    for (size_t i = 0; i <= maxSubLayersMinus1; i++) {
        WRITE_CODE(hp.getDisplFixedRateGeneralFlag(i), 1);  // u(1)
        if (!hp.getDisplFixedRateGeneralFlag(i)) {
            WRITE_CODE(hp.getDisplFixedRateWithinCbmsFlag(i), 1);  // u(1)
        }
        if (hp.getDisplFixedRateWithinCbmsFlag(i)) {
            WRITE_CODE(hp.getDisplElementalDurationInTcMinus1(i), 1);  // ue(v)
        }
        else {
            WRITE_CODE(hp.getDisplHrdReservedFlag(i), 1);  // u(1)
        }
        if (!hp.getDisplHrdReservedFlag(i)) {
            WRITE_CODE(hp.getCdispldCntMinus1(i), 1);  // ue(v)
        }
        if (hp.getDisplNalParametersPresentFlag()) {
            displacementHrdSubLayerParameters(
                bitstream, hp, hp.getDisplHdrSubLayerParameters(0, i), hp.getCdispldCntMinus1(i));
        }
        if (hp.getDisplClParametersPresentFlag()) {
            displacementHrdSubLayerParameters(
                bitstream, hp, hp.getDisplHdrSubLayerParameters(1, i), hp.getCdispldCntMinus1(i));
        }
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// J.13.2.3	AC displacement sub-layer HRD parameters syntax
void
AcDisplacementWriter::displacementHrdSubLayerParameters(
    vmesh::Bitstream& bitstream,
    DisplHrdParameters& hp,
    DisplHrdSubLayerParameters& hlsp,
    size_t                 cabCnt) {
    TRACE_BITSTREAM_IN("%s", __func__);
    hlsp.allocate(cabCnt + 1);
    for (size_t i = 0; i <= cabCnt; i++) {
        WRITE_UVLC(hlsp.getDisplBitRateValueMinus1(i));  // ue(v)
        WRITE_UVLC(hlsp.getCdisplSizeValueMinus1(i));  // ue(v)
        WRITE_CODE(hlsp.getDisplCbrFlag(i), 1);          // u(1)
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.3 Byte alignment syntax
void
AcDisplacementWriter::byteAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.10 Length alignment syntax
void
AcDisplacementWriter::lengthAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  while (!lengthAligned(bitstream)) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// D.2 Sample stream NAL unit syntax and semantics
// D.2.1 Sample stream NAL header syntax
void
AcDisplacementWriter::sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                                            SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  WRITE_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5 NAL unit syntax
// 8.3.5.1 General NAL unit syntax
void
AcDisplacementWriter::nalUnit(vmesh::Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    WRITE_CODE(nalUnit.getData(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5.2 NAL unit header syntax
void
AcDisplacementWriter::nalUnitHeader(vmesh::Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(zero, 1);                           // f(1)
  WRITE_CODE(nalUnit.getType(), 6);              // u(6)
  WRITE_CODE(nalUnit.getLayerId(), 6);           // u(6)
  WRITE_CODE(nalUnit.getTemporalyIdPlus1(), 3);  // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.10 RBSP trailing bit syntax
void
AcDisplacementWriter::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
