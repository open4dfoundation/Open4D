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
#include "acDisplacementReader.hpp"
#include "acDisplacementBitstream.hpp"
#include "acDisplacementStat.hpp"
#include "readerCommon.hpp"

using namespace acdisplacement;

AcDisplacementReader::AcDisplacementReader() {}
AcDisplacementReader::~AcDisplacementReader() = default;

void
AcDisplacementReader::sampleStreamNalHeader(vmesh::Bitstream& bitstream,
                                            vmesh::SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  READ_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.2 Specification of syntax functions and descriptors
bool
AcDisplacementReader::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AcDisplacementReader::lengthAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AcDisplacementReader::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
AcDisplacementReader::moreRbspData(vmesh::Bitstream& bitstream) {
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
//bool
//AcDisplacementReader::nextBits(vmesh::Bitstream& bitstream, int n) {
//    TRACE_BITSTREAM_IN("%s", __func__);
//    TRACE_BITSTREAM_OUT("%s", __func__);
//    return false;
//}
bool
AcDisplacementReader::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

void
AcDisplacementReader::decode(AcDisplacementBitstream& acd,
                             vmesh::Bitstream&        bitstream,
                             AcDisplacementBitstreamGofStat&    bistreamStat) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t                     sizeBitstream = bitstream.capacity();
  vmesh::SampleStreamNalUnit ssnu;
  // LK: it was not used PCCSEI                     prefixSEI;
  sampleStreamNalHeader(bitstream, ssnu);
  while (bitstream.size() < sizeBitstream) {
    ssnu.addNalUnit();
    //        sampleStreamNalUnit(
    //          syntax, false, bitstream, ssnu, ssnu.getNalUnitCount() - 1, sei);
    //    (V3cBitstream&        syntax,
    //                                   bool                 isAtlas,
    //                                   Bitstream&           bitstream,
    //                                   SampleStreamNalUnit& ssnu,
    //                                   size_t               index,
    //                                   PCCSEI&              prefixSEI)
    auto  index = ssnu.getNalUnitCount() - 1;
    auto& nalu  = ssnu.getNalUnit(index);
    //    READ_CODE(nalu.getSize(), 8 * (ssnu.getSizePrecisionBytesMinus1() + 1));  // u(v)
    size_t naluSize = 0;
    READ_CODE_DESC(naluSize,
                   8 * (ssnu.getSizePrecisionBytesMinus1() + 1),
                   "DISPL_NALUSIZE");
    nalu.setSize(naluSize);
    nalu.allocate();
    vmesh::Bitstream ssnuBitstream;
#if defined(BITSTREAM_TRACE)
    ssnuBitstream.setTrace(true);
    ssnuBitstream.setLogger(*logger_);
#endif
    bitstream.copyTo(ssnuBitstream, nalu.getSize());
    nalUnitHeader(ssnuBitstream, nalu);
    auto  naluType     = DisplNalUnitType(nalu.getType());
    switch (naluType) {
    case DISPLACEMENT_NAL_BMSPS:
      displSequenceParameterSetRbsp(acd.addDisplSequenceParameterSet(),
                                    ssnuBitstream);
      break;
    case DISPLACEMENT_NAL_BMFPS:
      displFrameParameterSetRbsp(acd.addDisplFrameParameterSet(),
                                 acd.getDisplSequenceParameterSetList(),
                                 ssnuBitstream);
      break;
    case DISPLACEMENT_NAL_TRAIL_N:
    case DISPLACEMENT_NAL_TRAIL_R:
    case DISPLACEMENT_NAL_TSA_N:
    case DISPLACEMENT_NAL_TSA_R:
    case DISPLACEMENT_NAL_STSA_N:
    case DISPLACEMENT_NAL_STSA_R:
    case DISPLACEMENT_NAL_RADL_N:
    case DISPLACEMENT_NAL_RADL_R:
    case DISPLACEMENT_NAL_RASL_N:
    case DISPLACEMENT_NAL_RASL_R:
    case DISPLACEMENT_NAL_SKIP_N:
    case DISPLACEMENT_NAL_SKIP_R:
    case DISPLACEMENT_NAL_IDR_N_LP:
      //nalu.getSize(),
      displacementLayerRbsp(acd.addDisplacementLayer(),
                            naluType,
                            acd.getDisplFrameParameterSetList(),
                            acd.getDisplSequenceParameterSetList(),
                            ssnuBitstream);
      bistreamStat.setDisplacement(
        (DisplacementType)acd.getDisplacementLayerList()
          .back()
          .getDisplHeader()
          .getDisplType(),
        naluSize - 2);
      break;
    default:
      fprintf(stderr,
              "Displacement Substream type = %d not supported\n",
              static_cast<int32_t>(nalu.getType()));
    }
  }
  for (auto& nu : ssnu.getNalUnit()) {
    TRACE_BITSTREAM("displacement nalu........%s\t%d\n",
                    toString((DisplNalUnitType)nu.getType()).c_str(),
                    nu.getSize());
    printf("displacement NalUnit Type = %-25s size = %zu \n",
           toString((DisplNalUnitType)nu.getType()).c_str(),
           nu.getSize());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
AcDisplacementReader::displacementLayerRbsp(
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

// 8.3.6.10 RBSP trailing bit syntax
void
AcDisplacementReader::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint64_t
AcDisplacementReader::displacementHeader(
  DisplacementHeader&                         dh,
  DisplNalUnitType                            nalUnitType,
  std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos = bitstream.getPosition().bytes_;
  //DisplFrameParameterSetRbsp& dfps = mfpsList[ dh.getDisplFrameParameterSetId() ]; //TODO: [PSIDX] order=index?
  //DisplSequenceParameterSetRbsp& dsps = mspsList[dfps.getDfpsDisplSequenceParameterSetId()];

  uint64_t value;
  if (nalUnitType >= DISPLACEMENT_NAL_BLA_W_LP
      && nalUnitType <= DISPLACEMENT_NAL_RSV_BMCL_29) {
    READ_CODE_DESC(value, 1, "NoOutputOfPriorDisplFramesFlag");
    dh.setNoOutputOfPriorDisplFramesFlag(value);
  }
  READ_UVLC_DESC(value, "DisplFrameParameterSetId");
  assert(value >= 0 && value <= 63);
  dh.setDisplFrameParameterSetId(value);
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

  auto&    dfti = dfps.getDisplInformation();
  uint32_t b    = (uint32_t)ceil(log2(dfti.getDiNumDisplsMinus2() + 2));
  uint32_t v    = dfti.getDiSignalledDisplIdDeltaLength() + b;
  READ_CODE_DESC(value, v, "DisplId");
  dh.setDisplId(value);  // u(v)
  READ_UVLC_DESC(value, "DisplType");
  dh.setDisplType((uint32_t)value);
  if (dfps.getDfpsOutputFlagPresentFlag()) {
    READ_CODE_DESC(value, 1, "DisplOutputFlag");
    dh.setDisplOutputFlag(value);
  }
  READ_CODE_DESC(value,
                 (dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4),
                 "DisplFrmOrderCntLsb");
  dh.setDisplFrmOrderCntLsb(value);
  if (dsps.getDspsNumRefDisplFrameListsInDsps() > 0) {
    READ_CODE_DESC(value, 1, "RefDisplFrameListDspsFlag");
    dh.setRefDisplFrameListDspsFlag(value);
  } else {
    dh.setRefDisplFrameListDspsFlag(false);
  }
  if (dh.getRefDisplFrameListDspsFlag() == 0) {
    refListStruct(dh.getDisplRefListStruct(), dsps, bitstream);
  } else if (dsps.getDspsNumRefDisplFrameListsInDsps() > 1) {
    size_t bitCount = vmesh::ceilLog2(dsps.getDspsNumRefDisplFrameListsInDsps());
    READ_CODE_DESC(value, bitCount, "RefDisplFrameListIdx");
    dh.setRefDisplFrameListIdx(value);
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
    READ_CODE_DESC(value, 1, "AdditionalDfocLsbPresentFlag");
    dh.setAdditionalDfocLsbPresentFlag(j, value);
    if (dh.getAdditionalDfocLsbPresentFlag()[j]) {
      READ_CODE_DESC(
        value, dfps.getDfpsAdditionalLtDfocLsbLen(), "AdditionalDfocLsbVal");
      dh.setAdditionalDfocLsbVal(j, value);  //u(v)
    }
  }
  uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
  READ_CODE_DESC(value, 1, "DhParametersOverrideFlag");
  dh.setDhParametersOverrideFlag(value);
  if (dh.getDhParametersOverrideFlag()) {
    READ_CODE_DESC(value, 1, "DhSubdivisionOverrideFlag");
    dh.setDhSubdivisionOverrideFlag(value);
    READ_CODE_DESC(value, 1, "DhQuantizationOverrideFlag");
    dh.setDhQuantizationOverrideFlag(value);
  } else {
    value = 0;
    dh.setDhSubdivisionOverrideFlag(value);
    dh.setDhQuantizationOverrideFlag(value);
  }
  if (dh.getDhSubdivisionOverrideFlag()) {
    READ_CODE_DESC(value, 3, "DhSubdivisionIterationCount");
    dh.setDhSubdivisionIterationCount(value);
  } else {
    dh.setDhSubdivisionIterationCount(dfps.getDfpsSubdivisionIterationCount());
  }
  for (size_t i = 0; i < dh.getDhSubdivisionIterationCount() + 1; i++) {
    READ_UVLC_DESC(value, "DhVertexCountLod");
    dh.setDhVertexCountLod(i, (uint32_t)value);
    READ_CODE_DESC(value, 1, "DhLodSplitFlag");
    dh.setDhLodSplitFlag(i, (bool)value);
    if (dh.getDhLodSplitFlag(i)) {
      READ_CODE_DESC(value, 8, "DhNumSubblockLodMinus1");
      dh.setDhNumSubblockLodMinus1(i, (uint32_t)value);
    } else {
      dh.setDhNumSubblockLodMinus1(i, 0u);
    }
  }
  if (dh.getDhQuantizationOverrideFlag()) {
    displQuantizationParameters(bitstream,
                                dh.getDhQuantizationParameters(),
                                dh.getDhSubdivisionIterationCount(),
                                numComp);
  } else {
    dh.getDhQuantizationParameters() = dfps.getDfpsQuantizationParameters();
  }

  if (dsps.getDspsInverseQuantizationOffsetPresentFlag()) {
    READ_CODE_DESC(value, 1, "DisplIQOffsetFlag");
    dh.setDisplIQOffsetFlag(value);
    if (dh.getDisplIQOffsetFlag()) {
      dh.setIQOffsetValuesSize(dh.getDhSubdivisionIterationCount() + 1,
                               numComp);
      for (int i = 0; i < dh.getDhSubdivisionIterationCount() + 1; i++) {
        for (int j = 0; j < numComp; j++) {
          for (
            int k = 0; k < 3;
            k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
            READ_CODE_DESC(value, 1, "IQOffsetValues0");
            dh.getIQOffsetValues(i, j, k, 0) = value;  //sign
            READ_SVLC_DESC(value, "IQOffsetValues1");
            dh.getIQOffsetValues(i, j, k, 1) = value;  //first precision
            READ_SVLC_DESC(value, "IQOffsetValues2");
            dh.getIQOffsetValues(i, j, k, 2) = value;  //second precision
          }
        }
      }
    }
  }

  if (dh.getDisplType() == P_DISPL) {
    if (refList.getNumRefEntries() > 1) {
      READ_CODE_DESC(value, 1, "NumRefIdxActiveOverrideFlag");
      dh.setNumRefIdxActiveOverrideFlag(value);
      if (dh.getNumRefIdxActiveOverrideFlag()) {
        READ_UVLC_DESC(value, "NumRefIdxActiveMinus1");
        dh.setNumRefIdxActiveMinus1((uint32_t)value);
      }
    }
    READ_CODE_DESC(value, 1, "DhLayerInterDisableFlag");
    dh.setDhLayerInterDisableFlag(value);
    dh.setNumLoDInter(dh.getDhSubdivisionIterationCount() + 1);
    if (!dh.getDhLayerInterDisableFlag()) {
      for (int lod = 0; lod < dh.getDhSubdivisionIterationCount() + 1; lod++) {
        uint8_t value;
        READ_CODE_DESC(value, 1, "setDhLoDInter");
        dh.setDhLoDInter(lod, value);
      }
    }
  }

  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

uint64_t
AcDisplacementReader::displacementDataUnit(
  DisplacementLayer&                          dl,
  std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto                        pos = bitstream.getPosition().bytes_;
  DisplacementHeader&         dh  = dl.getDisplHeader();
  DisplFrameParameterSetRbsp& dfps =
    mfpsList[dh.getDisplFrameParameterSetId()];  //TODO: [PSIDX] order=index?
  DisplSequenceParameterSetRbsp& dsps =
    mspsList[dfps.getDfpsDisplSequenceParameterSetId()];

  if (dh.getDisplType() == I_DISPL) {
    auto&    du = dl.getDisplDataunitIntra();
    uint32_t duSize;
    READ_UVLC_DESC(duSize, "Displacement Intra PayloadSize");
    du.setPayloadSize(duSize);
    displacementDataUnitIntra(
      du,
      duSize,
      bitstream);  //TODO: does this correspond to J.7.1.2.6	Displacement data unit syntax
  } else if (dh.getDisplType() == P_DISPL) {
    auto&    du = dl.getDisplDataunitInter();
    uint32_t duSize;
    READ_UVLC_DESC(duSize, "Displacement Inter PayloadSize");
    du.setPayloadSize(duSize);
    displacementDataUnitInter(
      du, duSize, bitstream);  //TODO: this is not present in the WD5.0
  }
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
  TRACE_BITSTREAM_OUT("%s", __func__);
}
uint64_t
AcDisplacementReader::displacementDataUnitInter(
  DisplacementDataInter& displdata,
  uint64_t               duSize,
  vmesh::Bitstream&      bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  displdata.allocateDisplDataBuffer(duSize);
  //pos=bmesh_nalu_header + smesh_header
  auto pos = bitstream.getPosition().bytes_;
  bitstream.copyToBits(displdata.getCodedDisplDataUnitBuffer(), duSize);
  TRACE_BITSTREAM("displacementDataUnitInter CodedDisplDataSize(): %d\n",
                  displdata.getCodedDisplDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}
uint64_t
AcDisplacementReader::displacementDataUnitIntra(
  DisplacementDataIntra& displdata,
  uint64_t               duSize,
  vmesh::Bitstream&      bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  displdata.allocateDisplDataBuffer(duSize);
  //pos=bmesh_nalu_header + smesh_header
  auto pos = bitstream.getPosition().bytes_;
  bitstream.copyToBits(displdata.getCodedDisplDataUnitBuffer(), duSize);
  TRACE_BITSTREAM("displacementDataUnitIntra CodedDisplDataSize(): %d\n",
                  displdata.getCodedDisplDataSize());
  TRACE_BITSTREAM_OUT("%s", __func__);
  auto pos2 = bitstream.getPosition().bytes_;
  return pos2 - pos;
}

void
AcDisplacementReader::displacementNoDataUnit(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return;
}

//uint64_t AcDisplacementReader::displacementDataUnitSkip( displacementDataIntra& displdata,
//                                             vmesh::Bitstream& bitstream){
//  TRACE_BITSTREAM_IN( "%s", __func__ );
//  TRACE_BITSTREAM_OUT( "%s", __func__ );
//  return 0;
//}
void
AcDisplacementReader::displInformation(DisplInformation& di,
                                       vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE_DESC(
    di.getDiUseSingleDisplFlag(), 1, "DiUseSingleDisplFlag ");  // u(1)
  if (!di.getDiUseSingleDisplFlag()) {
    READ_UVLC_DESC(di.getDiNumDisplsMinus2(), "DiNumDisplsMinus2 ");  // ue(v)
  } else {
    di.getDiNumDisplsMinus2() = -1;
  }
  auto numDispls = static_cast<size_t>(di.getDiNumDisplsMinus2() + 2);
  READ_CODE_DESC(
    di.getDiSignalledDisplIdFlag(), 1, "DiSignalledDisplIdFlag ");  // u(1)
  if (di.getDiSignalledDisplIdFlag()) {
    di._displIndexToID.resize(numDispls);
    READ_UVLC_DESC(di.getDiSignalledDisplIdDeltaLength(),
                   "DiSignalledDisplIdDeltaLength ");  // ue(v)
    uint32_t value = 0;
    uint32_t b     = static_cast<uint32_t>(ceil(log2(numDispls)));
    for (size_t i = 0; i < numDispls; i++) {
      uint8_t bitCount = di.getDiSignalledDisplIdDeltaLength() + b;
      READ_CODE_DESC(value, bitCount, "DiDisplId(i)");  // u(v)
      di.setDiDisplId(i, value);
      di._displIndexToID[i] = di.getDiDisplId(i);
      if (di._displIDToIndex.size() <= di._displIndexToID[i]) {
        di._displIDToIndex.resize(di._displIndexToID[i] + 1, -1);
      }
      di._displIDToIndex[di._displIndexToID[i]] = static_cast<uint32_t>(i);
    }
  } else {
    di.getDiSignalledDisplIdDeltaLength() = 0;
    di._displIDToIndex.resize(numDispls);
    di._displIndexToID.resize(numDispls);
    for (size_t i = 0; i < numDispls; i++) {
      di.setDiDisplId(i, i);
      di._displIDToIndex[i] = static_cast<uint32_t>(i);
      di._displIndexToID[i] = static_cast<uint32_t>(i);
      TRACE_BITSTREAM("\t%d-th ID %d\n", i, di._displIndexToID[i]);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
uint32_t
AcDisplacementReader::displFrameParameterSetRbsp(
  DisplFrameParameterSetRbsp&                 dfps,
  std::vector<DisplSequenceParameterSetRbsp>& mspsList,
  vmesh::Bitstream&                           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();
  //DisplSequenceParameterSetRbsp& dsps = mspsList[dfps.getDfpsDisplSequenceParameterSetId()];
  int dspsIdx = -1;
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

  uint32_t value;
  READ_UVLC_DESC(value, "DfpsDisplSequenceParameterSetId");
  assert(value >= 0 && value <= 15);
  dfps.setDfpsDisplSequenceParameterSetId(value);
  READ_UVLC_DESC(value, "DfpsDisplFrameParameterSetId    ");
  assert(value >= 0 && value <= 63);
  dfps.setDfpsDisplFrameParameterSetId(value);
  displInformation(dfps.getDisplInformation(), bitstream);
  READ_CODE_DESC(value, 1, "DfpsOutputFlagPresentFlag");
  dfps.setDfpsOutputFlagPresentFlag(value);
  READ_UVLC_DESC(value, "DfpsNumRefIdxDefaultActiveMinus1");
  dfps.setDfpsNumRefIdxDefaultActiveMinus1(value);
  READ_UVLC_DESC(value, "DfpsAdditionalLtDfocLsbLen");
  dfps.setDfpsAdditionalLtDfocLsbLen(value);
  uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
  READ_CODE_DESC(value, 1, "DfpsOverridenFlag");
  dfps.setDfpsOverridenFlag(value);
  if (dfps.getDfpsOverridenFlag()) {
    READ_CODE_DESC(value, 1, "DfpsSubdivisionEnableFlag");
    dfps.setDfpsSubdivisionEnableFlag(value);
    READ_CODE_DESC(value, 1, "DfpsQuantizationParametersEnableFlag");
    dfps.setDfpsQuantizationParametersEnableFlag(value);
  } else {
    value = 0;
    dfps.setDfpsSubdivisionEnableFlag(value);
    dfps.setDfpsQuantizationParametersEnableFlag(value);
  }
  if (dfps.getDfpsSubdivisionEnableFlag()) {
    READ_CODE_DESC(value, 3, "DfpsSubdivisionIterationCount");
    dfps.setDfpsSubdivisionIterationCount(value);
  } else {
    dfps.setDfpsSubdivisionIterationCount(
      dsps.getDspsSubdivisionIterationCount());
  }
  if (dfps.getDfpsQuantizationParametersEnableFlag())
    displQuantizationParameters(bitstream,
                                dfps.getDfpsQuantizationParameters(),
                                dfps.getDfpsSubdivisionIterationCount(),
                                numComp);
  else
    dfps.getDfpsQuantizationParameters() =
      dsps.getDspsQuantizationParameters();
  READ_CODE_DESC(value, 1, "DfpsExtensionPresentFlag");
  dfps.setDfpsExtensionPresentFlag(value);
  if (dfps.getDfpsExtensionPresentFlag())
    READ_CODE_DESC(value, 8, "DfpsExtension8bits");
  dfps.setDfpsExtension8bits(value);
  if (dfps.getDfpsExtension8bits()) {
    // while (more_rbsp_data())
    //   READ_CODE_DESC(value, 1, "DfpsExtensionDataFlag");  dfps.setDfpsExtensionDataFlag( value );
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}
void
AcDisplacementReader::refListStruct(DisplRefListStruct&            rls,
                                    DisplSequenceParameterSetRbsp& dsps,
                                    vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_UVLC_DESC(value, "NumRefEntries");
  rls.setNumRefEntries(value);  // ue(v)
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (dsps.getDspsLongTermRefDisplFramesFlag()) {
      READ_CODE_DESC(value, 1, "rls.getStRefdisplFrameFlag( i )");
      rls.setStRefdisplFrameFlag(i, value);  // u(1)
    } else rls.setStRefdisplFrameFlag(i, true);
    if (rls.getStRefdisplFrameFlag(i)) {
      value = bitstream.readUvlc();  // ue(v)
      rls.setAbsDeltaDfocSt(i, value);
      if (rls.getAbsDeltaDfocSt(i) > 0) {
        READ_CODE_DESC(value, 1, "StrafEntrySignFlag");
        rls.setStrafEntrySignFlag(i, value);  // u(1)
      }
    } else {
      uint8_t bitCount = dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4;
      READ_CODE_DESC(value, bitCount, "DfocLsbLt");  // u(v)
      rls.setDfocLsbLt(i, value);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

void
AcDisplacementReader::dispProfileToolsetConstraintsInformation(
  DisplProfileToolsetConstraintsInformation& dptci,
  vmesh::Bitstream&                          bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_CODE_DESC(value, 1, "DptcOneDisplacementFrameOnlyFlag");
  dptci.setDptcOneDisplacementFrameOnlyFlag(value);
  READ_CODE_DESC(value, 1, "DptcIntraFramesOnlyFlag");
  dptci.setDptcIntraFramesOnlyFlag(value);
  READ_CODE_DESC(value, 6, "DptcReservedZero7bits");
  dptci.setDptcReservedZero6bits(
    value);  //TODO: should be rename to reserved_6bits here and in the WD 5.0
  READ_CODE_DESC(value, 8, "DptcNumReservedConstraintBytes");
  dptci.setDptcNumReservedConstraintBytes(value);
  for (size_t i = 0; i < dptci.getDptcNumReservedConstraintBytes(); i++) {
    READ_CODE_DESC(value, 8, "DptcReservedConstraintByte");
    dptci.setDptcReservedConstraintByte(i, value);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

uint32_t
AcDisplacementReader::displProfileTierLevel(DisplProfileTierLevel& dpftl,
                                            vmesh::Bitstream&      bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);

  uint32_t value;
  READ_CODE_DESC(value, 1, "DptlTierFlag");
  dpftl.setDptlTierFlag(value);
  READ_CODE_DESC(value, 8, "DptlProfileToolsetIdc");
  dpftl.setDptlProfileToolsetIdc(value);
  READ_CODE_DESC(value, 32, "DptlReservedZero32bits");
  dpftl.setDptlReservedZero32bits(value);
  READ_CODE_DESC(value, 8, "DptlLevelIdc");
  dpftl.setDptlLevelIdc(value);
  READ_CODE_DESC(value, 6, "DptlNumSubProfiles");
  dpftl.setDptlNumSubProfiles(value);
  READ_CODE_DESC(value, 1, "DptlExtendedSubProfileFlag");
  dpftl.setDptlExtendedSubProfileFlag(value);
  int v = dpftl.getDptlExtendedSubProfileFlag() == 0 ? 32 : 64;
  for (size_t i = 0; i < dpftl.getDptlNumSubProfiles(); i++) {
    READ_CODE_DESC(value, v, "DptlSubProfileIdc");
    dpftl.setDptlSubProfileIdc(i, value);  //u(v)
  }
  READ_CODE_DESC(value, 1, "DptlToolsetConstraintsPresentFlag");
  dpftl.setDptlToolsetConstraintsPresentFlag(value);
  if (dpftl.getDptlToolsetConstraintsPresentFlag()) {
    dispProfileToolsetConstraintsInformation(
      dpftl.getDptlProfileToolsetConstraintsInformation(), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
  return 0;
}
uint32_t
AcDisplacementReader::displSequenceParameterSetRbsp(
  DisplSequenceParameterSetRbsp& dsps,
  vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto pos0 = bitstream.size();

  uint32_t value;
  READ_CODE_DESC(value, 4, "DspsSequenceParameterSetId");
  dsps.setDspsSequenceParameterSetId(value);
  READ_CODE_DESC(value, 3, "DspsMaxSubLayersMinus1");
  dsps.setDspsMaxSubLayersMinus1(value);
  displProfileTierLevel(dsps.getDspsProfileTierLevel(), bitstream);
  READ_CODE_DESC(value, 1, "DspsSingleDimensionFlag");
  dsps.setDspsSingleDimensionFlag(value);
  READ_UVLC_DESC(value, "DspsLog2MaxDisplFrameOrderCntLsbMinus4");
  dsps.setDspsLog2MaxDisplFrameOrderCntLsbMinus4(value);
  for (size_t i = 0; i < dsps.getDspsMaxSubLayersMinus1() + 1; i++) {
    READ_UVLC_DESC(value, "DspsMaxDecDisplFrameBufferingMinus1");
    dsps.setDspsMaxDecDisplFrameBufferingMinus1(value, i);
    READ_UVLC_DESC(value, "DspsMaxNumReorderFrames");
    dsps.setDspsMaxNumReorderFrames(value, i);
    READ_UVLC_DESC(value, "DspsMaxLatencyIncreasePlus1");
    dsps.setDspsMaxLatencyIncreasePlus1(value, i);
  }
  READ_CODE_DESC(value, 1, "DspsLongTermRefDisplFramesFlag");
  dsps.setDspsLongTermRefDisplFramesFlag(value);
  READ_UVLC_DESC(value, "DspsNumRefDisplFrameListsInDsps");
  dsps.setDspsNumRefDisplFrameListsInDsps(value);

  dsps.getDisplRefListStruct().resize(
    dsps.getDspsNumRefDisplFrameListsInDsps());
  for (size_t i = 0; i < dsps.getDspsNumRefDisplFrameListsInDsps(); i++) {
    refListStruct(dsps.getDisplRefListStruct()[i], dsps, bitstream);
  }
  READ_CODE(value, 5);
  dsps.setDspsGeometry3dBitdepthMinus1(value);

  READ_CODE_DESC(value, 1, "DspsSubdivisionPresentFlag");
  dsps.setDspsSubdivisionPresentFlag(value);
  if (dsps.getDspsSubdivisionPresentFlag()) {
    READ_CODE_DESC(value, 3, "DspsSubdivisionIterationCount");
    dsps.setDspsSubdivisionIterationCount(value);
  } else {
    dsps.setDspsSubdivisionIterationCount(0);
  }
  READ_SVLC_DESC(value, "DspsDisplacementReferenceQPMinus49");
  dsps.setDspsDisplacementReferenceQPMinus49(value);  //se(v)
  uint8_t numComp = dsps.getDspsSingleDimensionFlag() ? 1 : 3;
  READ_CODE_DESC(value, 1, "DspsInverseQuantizationOffsetPresentFlag");
  dsps.setDspsInverseQuantizationOffsetPresentFlag(value);
  displQuantizationParameters(bitstream,
                              dsps.getDspsQuantizationParameters(),
                              dsps.getDspsSubdivisionIterationCount(),
                              numComp);
  READ_CODE_DESC(value, 1, "DspsVuiParametersPresentFlag");
  dsps.setDspsVuiParametersPresentFlag(value);
  if (dsps.getDspsVuiParametersPresentFlag()) {
      displacementVuiParameters(bitstream, dsps, dsps.getVuiParameters());
  }
  READ_CODE_DESC(value, 1, "DspsExtensionPresentFlag");
  dsps.setDspsExtensionPresentFlag(value);
  if (dsps.getDspsExtensionPresentFlag()) {
    READ_CODE_DESC(value, 8, "DspsExtensionCount");
    dsps.setDspsExtensionCount(value);
  }
  if (dsps.getDspsExtensionCount()) {
    READ_UVLC_DESC(value, "ExtensionLengthMinus1");
    dsps.setDspsExtensionsLengthMinus1(value);  // ue(v)
    for (uint32_t i = 0; i < dsps.getDspsExtensionCount(); i++) {
      READ_CODE_DESC(value, 8, "ExtensionType");
      dsps.setDspsExtensionType(i, value);  // u(8)
      READ_CODE_DESC(value, 16, "ExtensionLength");
      dsps.setDspsExtensionLength(i, value);  // u(16)
      vmesh::Bitstream tempBitstream;
#if defined(BITSTREAM_TRACE)
      tempBitstream.setTrace(true);
      tempBitstream.setLogger(bitstream.getLogger());
#endif
      bitstream.copyToBits(tempBitstream, dsps.getDspsExtensionLength(i));
      //DspsExtension(msps, i, tempBitstream);
    }
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return (uint32_t)(bitstream.size() - pos0);
}

void
AcDisplacementReader::displQuantizationParameters(
  vmesh::Bitstream&            bitstream,
  DisplQuantizationParameters& qp,
  uint32_t                     subdivIterationCount,
  uint8_t                      numComp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  qp.setNumComponents(numComp);
  READ_CODE_DESC(value, 1, "DisplLodQuantizationFlag");
  qp.setDisplLodQuantizationFlag(value);
  READ_SVLC_DESC(value, "DisplBitDepthOffset");
  qp.setDisplBitDepthOffset(value);
  if (qp.getDisplLodQuantizationFlag() == 0) {
    for (int k = 0; k < numComp; k++) {
      READ_CODE_DESC(value, 7, "DisplQuantizationParameters");
      qp.setDisplQuantizationParameters(k, value);
      READ_CODE_DESC(value, 2, "DisplLog2LodInverseScale");
      qp.setDisplLog2LodInverseScale(k, value);
    }
  } else {
    qp.setNumLod(subdivIterationCount + 1);
    for (int LoDIdx = 0; LoDIdx < subdivIterationCount + 1; LoDIdx++) {
      qp.allocComponents(numComp, LoDIdx);
      for (int compIdx = 0; compIdx < numComp; compIdx++) {
        READ_UVLC_DESC(value, "DisplLodDeltaQPValue");
        qp.setDisplLodDeltaQPValue(LoDIdx, compIdx, value);
        value = 0;
        if (qp.getDisplLodDeltaQPValue(LoDIdx, compIdx))
          READ_CODE_DESC(value, 1, "DisplLodDeltaQPSign");
        qp.setDisplLodDeltaQPSign(LoDIdx, compIdx, value);
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// J.13  VUI syntax
// J.13.2.1  VUI parameters syntax
void
AcDisplacementReader::displacementVuiParameters(
    vmesh::Bitstream& bitstream,
    DisplSequenceParameterSetRbsp displsps, 
    DisplVuiParameters& vp) {
    TRACE_BITSTREAM_IN("%s", __func__);
    READ_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
    if (vp.getTimingInfoPresentFlag()) {
        READ_CODE(vp.getNumUnitsInTick(), 32);              // u(32)
        READ_CODE(vp.getTimeScale(), 32);                   // u(32)
        READ_CODE(vp.getMfocProportionalToTimingFlag(), 1);  // u(1)
        if (vp.getMfocProportionalToTimingFlag()) {
            READ_UVLC(vp.getNumTicksMfocDiffOneMinus1());  // ue(v)
        }
        READ_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
        if (vp.getHrdParametersPresentFlag()) {
            displacementHrdParameters(bitstream, true, displsps.getDspsMaxSubLayersMinus1(), vp.getHrdParameters());
        }
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// J.13.2.2	Basemesh HRD parameters syntax x
void
AcDisplacementReader::displacementHrdParameters(
    vmesh::Bitstream& bitstream,
    bool commonInPresentFlag,
    int maxSubLayersMinus1,
    DisplHrdParameters& hp) {
    TRACE_BITSTREAM_IN("%s", __func__);

    if (commonInPresentFlag) {
        READ_CODE(hp.getDisplNalParametersPresentFlag(), 1);  // u(1)
        READ_CODE(hp.getDisplClParametersPresentFlag(), 1);  // u(1)
        if (hp.getDisplNalParametersPresentFlag() || hp.getDisplClParametersPresentFlag()) {
            READ_CODE(hp.getDisplBitRateScale(), 4);  // u(4)
            READ_CODE(hp.getCdisplbSizeScale(), 4);  // u(4)
            READ_CODE(hp.getInitialCdisplbRemovalDelayLengthMinus1(), 5);  // u(5)
            READ_CODE(hp.getAuCdisplbRemovalDelayLengthMinus1(), 5);  // u(5)
            READ_CODE(hp.getDdisplbOutputDelayLengthMinus1(), 5);  // u(5)
        }
    }

    for (size_t i = 0; i <= maxSubLayersMinus1; i++) {
        READ_CODE(hp.getDisplFixedRateGeneralFlag(i), 1);  // u(1)
        if (!hp.getDisplFixedRateGeneralFlag(i)) {
            READ_CODE(hp.getDisplFixedRateWithinCbmsFlag(i), 1);  // u(1)
        }
        if (hp.getDisplFixedRateWithinCbmsFlag(i)) {
            READ_CODE(hp.getDisplElementalDurationInTcMinus1(i), 1);  // ue(v)
        }
        else {
            READ_CODE(hp.getDisplHrdReservedFlag(i), 1);  // u(1)
        }
        if (!hp.getDisplHrdReservedFlag(i)) {
            READ_CODE(hp.getCdispldCntMinus1(i), 1);  // ue(v)
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

// J.13.2.3	Basemesh sub-layer HRD parameters syntax
void
AcDisplacementReader::displacementHrdSubLayerParameters(
    vmesh::Bitstream& bitstream,
    DisplHrdParameters& hp,
    DisplHrdSubLayerParameters& hlsp,
    size_t                 cabCnt) {
    TRACE_BITSTREAM_IN("%s", __func__);
    hlsp.allocate(cabCnt + 1);
    for (size_t i = 0; i <= cabCnt; i++) {
        READ_UVLC(hlsp.getDisplBitRateValueMinus1(i));  // ue(v)
        READ_UVLC(hlsp.getCdisplSizeValueMinus1(i));  // ue(v)
        READ_CODE(hlsp.getDisplCbrFlag(i), 1);          // u(1)
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5 NAL unit syntax
// 8.3.5.1 General NAL unit syntax
void
AcDisplacementReader::nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    READ_CODE(nalUnit.getData(i), 8);  // b(8)
  }
}

// 8.3.5.2 NAL unit header syntax
void
AcDisplacementReader::nalUnitHeader(vmesh::Bitstream& bitstream,
                                    vmesh::NalUnit&   nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(zero, 1);                                      // f(1)
  READ_CODE_CAST(nalUnit.getType(), 6, DisplNalUnitType);  // u(6)
  READ_CODE(nalUnit.getLayerId(), 6);                      // u(6)
  READ_CODE(nalUnit.getTemporalyIdPlus1(), 3);             // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.3 Byte alignment syntax
void
AcDisplacementReader::byteAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}