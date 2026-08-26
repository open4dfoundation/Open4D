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

#include <list>
#include "bitstream.hpp"
#include "atlasWriter.hpp"
#include "writerCommon.hpp"
#include "util/mesh.hpp"

using namespace atlas;

AtlasWriter::AtlasWriter()  = default;
AtlasWriter::~AtlasWriter() = default;

// 8.2 Specification of syntax functions and descriptors
bool
AtlasWriter::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AtlasWriter::lengthAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AtlasWriter::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
AtlasWriter::moreRbspData(vmesh::Bitstream& bitstream) {
  return false;
}
bool
//bool
//V3CReader::nextBits(vmesh::Bitstream& bitstream, int n) {
//    TRACE_BITSTREAM_IN("%s", __func__);
//    TRACE_BITSTREAM_OUT("%s", __func__);
//    return false;
//}
AtlasWriter::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

// 8.3.2.4 Atlas sub-bitstream syntax
void
AtlasWriter::encode(AtlasBitstream&   atlasBitstream,
                    vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  vmesh::SampleStreamNalUnit ssnu;

  uint64_t maxNaluSize = 0;
  struct adNalUnit {
    uint64_t         naluTotSize;
    AtlasNalUnitType nuType;
    vmesh::Bitstream tempBitStream;
  };
  std::vector<adNalUnit> atlasNaluList;

  size_t   nalHeaderSize = 2;
  uint32_t nalUCounter   = 0;
  //=====
  // Atlas sequence parameter set
  for (auto& asps : atlasBitstream.getAtlasSequenceParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(ATLAS_NAL_ASPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    atlasSequenceParameterSetRbsp(asps, atlasBitstream, tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    atlasNaluList.push_back(
      adNalUnit({naluSize, ATLAS_NAL_ASPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
    nalUCounter++;
  }
  // Atlas frame parameter set
  for (auto& afps : atlasBitstream.getAtlasFrameParameterSetList()) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    vmesh::NalUnit nu(ATLAS_NAL_AFPS, 0, 1);
    nalUnitHeader(tempBitStream, nu);
    atlasFrameParameterSetRbsp(afps, atlasBitstream, tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    atlasNaluList.push_back(
      adNalUnit({naluSize, ATLAS_NAL_AFPS, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
    nalUCounter++;
  }

  int32_t frmOrderCntVal = -1;
  for (auto& atgl : atlasBitstream.getAtlasTileLayerList()) {
    // Prefix sei message
    for (auto& sei : atgl.getSEI().getSeiPrefix()) {
      vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
      tempBitStream.setTrace(true);
      tempBitStream.setLogger(bitstream.getLogger());
#endif
      vmesh::NalUnit nu(ATLAS_NAL_PREFIX_ESEI, 0, 1);
      nalUnitHeader(tempBitStream, nu);
      seiRbsp(atlasBitstream,
              tempBitStream,
              ATLAS_NAL_PREFIX_ESEI,
              *sei,
              atgl.getEncFrameIndex());
      uint64_t naluSize = (tempBitStream.size());
      atlasNaluList.push_back(
        adNalUnit({naluSize, ATLAS_NAL_PREFIX_ESEI, tempBitStream}));
      maxNaluSize = std::max(maxNaluSize, naluSize);
      nalUCounter++;
    }

    // Atlas tile layer data
    // checking the tile if the NAL unit can be skipped for ATTRIBUTE tiles
    bool skip = false;
    if (atgl.getHeader().getType() == I_TILE_ATTR) {
      auto tileId = atgl.getHeader().getAtlasTileHeaderId();
      auto afpsId = atgl.getHeader().getAtlasFrameParameterSetId();
      int afpsIdx = -1;
      for (int i = 0; i < atlasBitstream.getAtlasFrameParameterSetList().size(); i++) {
        if (afpsId == atlasBitstream.getAtlasFrameParameterSetList()[i].getAtlasFrameParameterSetId())
        {
          afpsIdx = i;
          break;
        }
      }
      auto& afps = atlasBitstream.getAtlasFrameParameterSetList()[afpsIdx];
      auto aspsId = afps.getAtlasSequenceParameterSetId();
      int aspsIdx = -1;
      for (int i = 0; i < atlasBitstream.getAtlasSequenceParameterSetList().size(); i++) {
        if (aspsId == atlasBitstream.getAtlasSequenceParameterSetList()[i].getAtlasSequenceParameterSetId())
        {
          aspsIdx = i;
          break;
        }
      }
      auto& asps = atlasBitstream.getAtlasSequenceParameterSetList()[aspsIdx];
      for (int attrIdx = 0; attrIdx < asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount(); attrIdx++) {
        auto& aftai = afps.getAfveExtension().getAtlasFrameTileAttributeInformation(attrIdx);
        if (aftai.getNumTilesInAtlasFrameMinus1() == 0 && aftai.getTileId(0) == tileId && !aftai.getNalTilePresentFlag()) {
          skip = true;
        }
      }
    }
    if (!skip) {
    vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
    tempBitStream.setTrace(true);
    tempBitStream.setLogger(bitstream.getLogger());
#endif
    AtlasNalUnitType naluType = ATLAS_NAL_SKIP_R;
    if (atgl.getHeader().getType() == I_TILE
        || atgl.getHeader().getType() == I_TILE_ATTR) {
      naluType = ATLAS_NAL_IDR_N_LP;
    }
    if (atgl.getHeader().getType() == P_TILE
        || atgl.getHeader().getType() == P_TILE_ATTR) {
      naluType = ATLAS_NAL_TRAIL_R;
    }

    vmesh::NalUnit nu(naluType, 0, 1);
    nalUnitHeader(tempBitStream, nu);

    atlasTileLayerRbsp(atgl, atlasBitstream, naluType, tempBitStream);
    uint64_t naluSize = (tempBitStream.size());
    atlasNaluList.push_back(adNalUnit({naluSize, naluType, tempBitStream}));
    maxNaluSize = std::max(maxNaluSize, naluSize);
    nalUCounter++;
    }
    // Suffix sei message
    for (auto& sei : atgl.getSEI().getSeiSuffix()) {
      for (size_t i = 0; i < atgl.getSEI().getSeiSuffix().size(); i++) {
        vmesh::Bitstream tempBitStream;
#if defined(BITSTREAM_TRACE)
        tempBitStream.setTrace(true);
        tempBitStream.setLogger(bitstream.getLogger());
#endif
        vmesh::NalUnit nu(ATLAS_NAL_SUFFIX_ESEI, 0, 1);
        nalUnitHeader(tempBitStream, nu);
        seiRbsp(atlasBitstream,
                tempBitStream,
                ATLAS_NAL_SUFFIX_ESEI,
                *sei,
                atgl.getEncFrameIndex());
        uint64_t naluSize = (tempBitStream.size());
        atlasNaluList.push_back(
          adNalUnit({naluSize, ATLAS_NAL_SUFFIX_ESEI, tempBitStream}));
        maxNaluSize = std::max(maxNaluSize, naluSize);
        nalUCounter++;
      }
    }
  }
  ssnu.getSizePrecisionBytesMinus1() =
    std::min(
      std::max(int(ceil(double(vmesh::ceilLog2(maxNaluSize + 1)) / 8.0)), 1),
      8)
    - 1;
  sampleStreamNalHeader(bitstream, ssnu);

  // Atlas sequence parameter set
  size_t index     = 0;
  size_t sizeIndex = 0;
  for (auto& nu : atlasNaluList) {
    TRACE_BITSTREAM("atlas nalu........%s\t%d\n",
                    toString((AtlasNalUnitType)nu.nuType).c_str(),
                    nu.naluTotSize);
    WRITE_CODE(nu.naluTotSize, 8 * (ssnu.getSizePrecisionBytesMinus1() + 1));
    bitstream.copyFrom(nu.tempBitStream, 0, nu.naluTotSize);
    auto type = toString(nu.nuType);
    //copyFrom changes the bytePosition of nu.tempBitstream.
    printf("atlas NalUnit Type = %-25s size = %llu \n",
           type.c_str(),
           nu.naluTotSize);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.3 Byte alignment syntax
void
AtlasWriter::byteAlignment(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5 NAL unit syntax
// 8.3.5.1 General NAL unit syntax
void
AtlasWriter::nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    WRITE_CODE(nalUnit.getData(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5.2 NAL unit header syntax
void
AtlasWriter::nalUnitHeader(vmesh::Bitstream& bitstream,
                           vmesh::NalUnit&   nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(zero, 1);                           // f(1)
  WRITE_CODE(nalUnit.getType(), 6);              // u(6)
  WRITE_CODE(nalUnit.getLayerId(), 6);           // u(6)
  WRITE_CODE(nalUnit.getTemporalyIdPlus1(), 3);  // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1 Atlas sequence parameter set Rbsp
// 8.3.6.1.1 General Atlas sequence parameter set Rbsp
void
AtlasWriter::atlasSequenceParameterSetRbsp(AtlasSequenceParameterSetRbsp& asps,
                                           AtlasBitstream&   atlasBitstream,
                                           vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(
    static_cast<uint32_t>(asps.getAtlasSequenceParameterSetId()));  // ue(v)
  WRITE_UVLC(asps.getFrameWidth());                                 // ue(v)
  WRITE_UVLC(asps.getFrameHeight());                                // ue(v)
  WRITE_CODE(asps.getGeometry3dBitdepthMinus1(), 5);                // u(5)
  WRITE_CODE(asps.getGeometry2dBitdepthMinus1(), 5);                // u(5)
  WRITE_UVLC(static_cast<uint32_t>(
    asps.getLog2MaxAtlasFrameOrderCntLsbMinus4()));  // ue(v)
  WRITE_UVLC(static_cast<uint32_t>(
    asps.getMaxDecAtlasFrameBufferingMinus1()));        // ue(v)
  WRITE_CODE(asps.getLongTermRefAtlasFramesFlag(), 1);  // u(1)
  WRITE_UVLC(
    static_cast<uint32_t>(asps.getNumRefAtlasFrameListsInAsps()));  // ue(v)
  for (size_t i = 0; i < asps.getNumRefAtlasFrameListsInAsps(); i++) {
    refListStruct(asps.getRefListStruct(i), asps, bitstream);
  }
  WRITE_CODE(asps.getUseEightOrientationsFlag(), 1);       // u(1)
  WRITE_CODE(asps.getExtendedProjectionEnabledFlag(), 1);  // u(1)
  if (asps.getExtendedProjectionEnabledFlag()) {
    WRITE_UVLC(
      static_cast<uint32_t>(asps.getMaxNumberProjectionsMinus1()));  // ue(v)
  }
  WRITE_CODE(asps.getNormalAxisLimitsQuantizationEnabledFlag(),
             1);                                                // u(1)
  WRITE_CODE(asps.getNormalAxisMaxDeltaValueEnabledFlag(), 1);  // u(1)
  WRITE_CODE(asps.getPatchPrecedenceOrderFlag(), 1);            // u(1)
  WRITE_CODE(asps.getLog2PatchPackingBlockSize(), 3);           // u(3)
  WRITE_CODE(asps.getPatchSizeQuantizerPresentFlag(), 1);       // u(1)
  WRITE_CODE(asps.getMapCountMinus1(), 4);                      // u(4)
  WRITE_CODE(asps.getPixelDeinterleavingFlag(), 1);             // u(1)
  if (asps.getPixelDeinterleavingFlag()) {
    for (size_t i = 0; i < asps.getMapCountMinus1() + 1; i++) {
      WRITE_CODE(asps.getPixelDeinterleavingMapFlag(i), 1);  // u(1)
    }
  }
  WRITE_CODE(asps.getRawPatchEnabledFlag(), 1);  // u(1)
  WRITE_CODE(asps.getEomPatchEnabledFlag(), 1);  // u(1)
  if (asps.getEomPatchEnabledFlag() && asps.getMapCountMinus1() == 0) {
    WRITE_CODE(asps.getEomFixBitCountMinus1(), 4);  // u(4)
  }
  if (asps.getRawPatchEnabledFlag() || asps.getEomPatchEnabledFlag()) {
    WRITE_CODE(asps.getAuxiliaryVideoEnabledFlag(), 1);  // u(1)
  }
  WRITE_CODE(asps.getPLREnabledFlag(), 1);  // u(1)
  if (asps.getPLREnabledFlag()) {
    assert(0);
    exit(-1);
  }
  WRITE_CODE(asps.getVuiParametersPresentFlag(), 1);  // u(1)
  if (asps.getVuiParametersPresentFlag()) {
    vuiParameters(bitstream, asps.getVuiParameters());
  }

  WRITE_CODE(asps.getExtensionFlag(), 1);  // u(1)
  if (asps.getExtensionFlag()) {
    WRITE_CODE(asps.getVpccExtensionFlag(), 1);  // u(1)
    WRITE_CODE(asps.getMivExtensionFlag(), 1);   // u(1)
    WRITE_CODE(asps.getVdmcExtensionFlag(), 1);  // u(1)
    WRITE_CODE(asps.getExtension5Bits(), 5);     // u(5)
    if (asps.getVpccExtensionFlag())
      aspsVpccExtension(bitstream, asps, asps.getAspsVpccExtension());
    if (asps.getVdmcExtensionFlag())
      aspsVdmcExtension(bitstream, asps, asps.getAsveExtension());
    if (asps.getExtension5Bits())
      while (moreRbspData(bitstream)) WRITE_CODE(0, 1);  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1.2 Point local reconstruction information syntax

// 8.3.6.1.3 Atlas sequence parameter set V-DMC extension syntax
void
AtlasWriter::aspsVdmcExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AspsVdmcExtension&             asve) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  WRITE_CODE(asve.getAsveSubdivisionIterationCount(), 3);
  if (asve.getAsveSubdivisionIterationCount() > 1) {
    WRITE_CODE(asve.getAsveLodAdaptiveSubdivisionFlag(), 1);
  }
  WRITE_CODE(asve.getAsveEdgeBasedSubdivisionFlag(), 1);
  for (int32_t it = 0; it < asve.getAsveSubdivisionIterationCount(); it++) {
    if (asve.getAsveLodAdaptiveSubdivisionFlag() || it == 0) {
      WRITE_CODE(asve.getAsveSubdivisionMethod()[it], 3);
    }
  }
  if (asve.getAsveEdgeBasedSubdivisionFlag() && (asve.getAsveSubdivisionIterationCount() != 0)) {
    WRITE_CODE(asve.getAsveSubdivisionMinEdgeLength(), 16);
  }
  WRITE_CODE(asve.getAsve1DDisplacementFlag(), 1)  //u1
  WRITE_CODE(asve.getAsveInterpolateSubdividedNormalsFlag(), 1);
  WRITE_CODE(asve.getAsveQuantizationParametersPresentFlag(), 1);
  if (asve.getAsveQuantizationParametersPresentFlag()) {
    WRITE_CODE(asve.getAsveInverseQuantizationOffsetPresentFlag(), 1);
    WRITE_SVLC(asve.getAsveDisplacementReferenceQPMinus49());
    uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();
    vdmcQuantizationParameters(bitstream,
                               asve.getAsveQuantizationParameters(),
                               asve.getAsveSubdivisionIterationCount(),
                               numComp);
  }

  if (asve.getAsveSubdivisionIterationCount() > 0) {
    WRITE_CODE(asve.getAsveTransformMethod(), 3);
  } else {
    asve.setAsveTransformMethod(0);
  }
  WRITE_CODE(asve.getAsveLiftingOffsetPresentFlag(), 1);
  WRITE_CODE(asve.getAsveDirectionalLiftingPresentFlag(), 1);
  if (asve.getAsveTransformMethod()
        == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING) {
    vdmcLiftingTransformParameters(bitstream,
                                   asve.getAspsExtLtpDisplacement(),
                                   0,
                                   asve.getAsveDirectionalLiftingPresentFlag(),
                                   asve.getAsveSubdivisionIterationCount(),
                                   NULL);
  }
  WRITE_CODE(asve.getAsveAttributeInformationPresentFlag(), 1);
  if (asve.getAsveAttributeInformationPresentFlag()) {
    WRITE_CODE(asve.getAsveConsistentAttributeFrameFlag(), 1);
    if (!asve.getAsveConsistentAttributeFrameFlag()) {
      WRITE_CODE(asve.getAsveAttributeFrameSizeCount(), 7);
    }
    for (int attrIdx = 0; attrIdx < asve.getAspsAttributeNominalFrameSizeCount();
         attrIdx++) {
      WRITE_UVLC(asve.getAsveAttributeFrameWidth()[attrIdx]);
      WRITE_UVLC(asve.getAsveAttributeFrameHeight()[attrIdx]);
      WRITE_CODE(asve.getAsveAttributeSubtextureEnabledFlag()[attrIdx], 1);
    }
  }
  WRITE_CODE(asve.getAsveDisplacementIdPresentFlag(), 1);  //u1
  WRITE_CODE(asve.getAsveLodPatchesEnableFlag(), 1);       //u1
  WRITE_CODE(asve.getAsvePackingMethod(), 1);     //u1
  // orthoAtlas
  WRITE_CODE(asve.getAsveProjectionTexcoordEnableFlag(), 1);  //u1
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    WRITE_CODE(asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag(), 1);  //u1
    if (asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag())
      WRITE_CODE(asve.getAsveProjectionTexcoordMappingAttributeIndex(),
                 7);                                                   //u(7)
    WRITE_CODE(asve.getAsveProjectionTexcoordOutputBitdepthMinus1(), 5);  //u(5)
    WRITE_CODE(asve.getAsveProjectionTexcoordBboxBiasEnableFlag(), 1);        //u1
    WRITE_CODE_40bits(asve.getAsveProjectionTexCoordUpscaleFactorMinus1(),
                      40);                                        //u(40)
    WRITE_CODE(asve.getAsveProjectionTexcoordLog2DownscaleFactor(), 6);  //u(6)
    WRITE_CODE(asve.getAsveProjectionRawTextcoordPresentFlag(), 1);
    if (asve.getAsveProjectionRawTextcoordPresentFlag()) {
      WRITE_CODE(asve.getAsveProjectionRawTextcoordBitdepthMinus1(), 4);  // u(4)
    }
  }
  // V-DMC VUI
  WRITE_CODE(asve.getAsveVdmcVuiParametersPresentFlag(), 1);  //u1
  VdmcVuiParameters vp = asve.getAsveVdmcVuiParameters();
  if (asve.getAsveVdmcVuiParametersPresentFlag() == true) {
    // It is assumed that vp has been properly allocated and populated elsewhere
    for (size_t d = 0; d < asve.getAspsAttributeNominalFrameSizeCount(); d++) {
      WRITE_CODE(vp.getVdmcVuiOneSubmeshPerIndependentUnitAttributeFlag(d),
                 1)  //u1
    }
    WRITE_CODE(vp.getVdmcVuiOneSubmeshPerIndependentUnitGeometryFlag(),
               1)  //u1
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1.4 Quantization parameters syntax
void
AtlasWriter::vdmcQuantizationParameters(vmesh::Bitstream&           bitstream,
                                        VdmcQuantizationParameters& qp,
                                        uint32_t subdivIterationCount,
                                        uint8_t  numComp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(qp.getVdmcLodQuantizationFlag(), 1);  // u(1)

  if (qp.getVdmcLodQuantizationFlag() == 0) {
    for (int i = 0; i < numComp; i++) {
      WRITE_CODE(qp.getVdmcQuantizationParameters()[i], 7);  // u(7)
      WRITE_CODE(qp.getVdmcLog2LodInverseScale()[i], 2);     // u(2)
    }
  } else {
    for (int i = 0; i < subdivIterationCount + 1; i++) {
      for (int j = 0; j < numComp; j++) {
        WRITE_UVLC(qp.getVdmcLodDeltaQPValue(i, j));  // ue(v)
        if (qp.getVdmcLodDeltaQPValue(i, j)) {
          WRITE_CODE(qp.getVdmcLodDeltaQPSign(i, j), 1);  // u(1)
        }
      }
    }
  }
  WRITE_CODE(qp.getVdmcDirectQuantizationEnabledFlag(), 1);  //u(1)
  if (!qp.getVdmcDirectQuantizationEnabledFlag())
    WRITE_SVLC(qp.getVdmcBitDepthOffset());  // se(v)

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1.4 Lifting transform parameters syntax
void
AtlasWriter::vdmcLiftingTransformParameters(
  vmesh::Bitstream&               bitstream,
  VdmcLiftingTransformParameters& vdmcLtp,
  int                             paramLevelIndex,
  bool                            dirLiftFlag,
  uint32_t                        subdivIterationCount,
  size_t                          patchMode) {
  TRACE_BITSTREAM_IN("%s", __func__);
  vdmcLtp.setNumLod(subdivIterationCount + 1);
  uint32_t value;

  WRITE_CODE(vdmcLtp.getSkipUpdateFlag(), 1);  // u(1)
  for (int i = 0; i < subdivIterationCount; i++) {
    if (vdmcLtp.getSkipUpdateFlag()) {
      // UpdateWeight[ ltpIndex ][ i ] = 0
    } else {
      if (i == 0) {
        WRITE_CODE(vdmcLtp.getAdaptiveUpdateWeightFlag(), 1);  //u(1)
        WRITE_CODE(vdmcLtp.getValenceUpdateWeightFlag(), 1);   //u(1)
      }
      if ((vdmcLtp.getAdaptiveUpdateWeightFlag() == 1) || (i == 0)) {
        WRITE_UVLC(vdmcLtp.getLiftingUpdateWeightNumerator()[i]);  // ue(v)
        WRITE_UVLC(
          vdmcLtp.getLiftingUpdateWeightDenominatorMinus1()[i]);  // ue(v)
          //UpdateWeight[ ltpIndex ][ i ] =
          //  vdmcLtp.getLiftingUpdateWeightNumerator(i) ?
          //  (vdmcLtp.setLiftingUpdateWeightDenominatorMinus1(i) + 1)
      } else {
        //  UpdateWeight[ ltpIndex ][ i ] = UpdateWeight[ ltpIndex ][ 0 ]
      }
    }
  }
  WRITE_CODE(vdmcLtp.getAdaptivePredictionWeightFlag(), 1);  //u(1)
  if (vdmcLtp.getAdaptivePredictionWeightFlag()) {
    for (int i = 0; i < subdivIterationCount; i++) {
      WRITE_UVLC(vdmcLtp.getLiftingPredictionWeightNumerator()[i]);  // ue(v)
      WRITE_UVLC(
        vdmcLtp.getLiftingPredictionWeightDenominatorMinus1()[i]);  // ue(v)
    }
  }

  if (dirLiftFlag) {
    WRITE_UVLC(vdmcLtp.getDirectionalLiftingScale1());
    WRITE_UVLC(vdmcLtp.getDirectionalLiftingDeltaScale2());
    WRITE_UVLC(vdmcLtp.getDirectionalLiftingDeltaScale3());
    WRITE_UVLC(vdmcLtp.getDirectionalLiftingScaleDenoMinus1());
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.6.2 Atlas frame parameter set Rbsp syntax
// 8.3.6.2.1 General atlas frame parameter set Rbsp syntax
void
AtlasWriter::atlasFrameParameterSetRbsp(AtlasFrameParameterSetRbsp& afps,
                                        AtlasBitstream&   atlasBitstream,
                                        vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(afps.getAtlasFrameParameterSetId());     // ue(v)
  WRITE_UVLC(afps.getAtlasSequenceParameterSetId());  // ue(v)
  auto& asps = atlasBitstream.getAtlasSequenceParameterSet(
    afps.getAtlasSequenceParameterSetId());
  atlasFrameTileInformation(
    afps.getAtlasFrameTileInformation(), asps, bitstream);
  WRITE_CODE(afps.getOutputFlagPresentFlag(), 1);                // u(1)
  WRITE_UVLC(afps.getNumRefIdxDefaultActiveMinus1());            // ue(v)
  WRITE_UVLC(afps.getAdditionalLtAfocLsbLen());                  // ue(v)
  WRITE_CODE(afps.getLodModeEnableFlag(), 1);                    // u(1)
  WRITE_CODE(afps.getRaw3dOffsetBitCountExplicitModeFlag(), 1);  // u(1)
  WRITE_CODE(afps.getExtensionFlag(), 1);                        // u(1)
  if (afps.getExtensionFlag()) {
    WRITE_CODE(afps.getMivExtensionFlag(), 1);  // u(1)
    WRITE_CODE(afps.getVmcExtensionFlag(), 1);  // u(1)
    WRITE_CODE(afps.getExtension6Bits(), 6);    // u(6)
  }
  if (afps.getVmcExtensionFlag()) {
    auto& afve = afps.getAfveExtension();
    afpsVdmcExtension(bitstream, asps, afps, afve);
  }
  if (afps.getExtension6Bits()) {
    while (moreRbspData(bitstream)) { WRITE_CODE(0, 1); }  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.2 Atlas frame tile information syntax
void
AtlasWriter::atlasFrameTileInformation(AtlasFrameTileInformation&     afti,
                                       AtlasSequenceParameterSetRbsp& asps,
                                       vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(afti.getSingleTileInAtlasFrameFlag(), 1);  // u(1)
  if (!afti.getSingleTileInAtlasFrameFlag()) {
    WRITE_CODE(afti.getUniformPartitionSpacingFlag(), 1);  // u(1)
    if (afti.getUniformPartitionSpacingFlag()) {
      WRITE_UVLC(afti.getPartitionColumnWidthMinus1(0));  //  ue(v)
      WRITE_UVLC(afti.getPartitionRowHeightMinus1(0));    //  ue(v)
      TRACE_BITSTREAM("numPartitionColumnMinus1 = %d\n",
                      afti.getNumPartitionColumnsMinus1());
      TRACE_BITSTREAM("numPartitionRowsMinus1   = %d\n",
                      afti.getNumPartitionRowsMinus1());
    } else {
      WRITE_UVLC(afti.getNumPartitionColumnsMinus1());  //  ue(v)
      WRITE_UVLC(afti.getNumPartitionRowsMinus1());     //  ue(v)
      for (size_t i = 0; i < afti.getNumPartitionColumnsMinus1(); i++) {
        WRITE_UVLC(afti.getPartitionColumnWidthMinus1(i));  //  ue(v)
      }
      for (size_t i = 0; i < afti.getNumPartitionRowsMinus1(); i++) {
        WRITE_UVLC(afti.getPartitionRowHeightMinus1(i));  //  ue(v)
      }
    }
    WRITE_CODE(afti.getSinglePartitionPerTileFlag(), 1);  //  u(1)
    if (afti.getSinglePartitionPerTileFlag() == 0u) {
      uint32_t NumPartitionsInAtlasFrame =
        (afti.getNumPartitionColumnsMinus1() + 1)
        * (afti.getNumPartitionRowsMinus1() + 1);
      WRITE_UVLC(afti.getNumTilesInAtlasFrameMinus1());  // ue(v)
      for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
        uint8_t bitCount = vmesh::ceilLog2(NumPartitionsInAtlasFrame);
        WRITE_CODE(afti.getTopLeftPartitionIdx(i), bitCount);     // u(v)
        WRITE_UVLC(afti.getBottomRightPartitionColumnOffset(i));  // ue(v)
        WRITE_UVLC(afti.getBottomRightPartitionRowOffset(i));     // ue(v)
      }
    }
  }
  if (asps.getAuxiliaryVideoEnabledFlag()) {
    WRITE_UVLC(afti.getAuxiliaryVideoTileRowWidthMinus1());  // ue(v)
    for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
      WRITE_UVLC(afti.getAuxiliaryVideoTileRowHeight(i));  // ue(v)
    }
  }
  WRITE_CODE(afti.getSignalledTileIdFlag(), 1);  // u(1)
  if (afti.getSignalledTileIdFlag()) {
    WRITE_UVLC(afti.getSignalledTileIdLengthMinus1());  // ue(v)
    for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
      uint8_t bitCount = afti.getSignalledTileIdLengthMinus1() + 1;
      WRITE_CODE(afti.getTileId(i), bitCount);  // u(v)
    }
  } else {
    TRACE_BITSTREAM("NumTilesInAtlasFrameMinus1 : %d\n",
                    afti.getNumTilesInAtlasFrameMinus1());
    TRACE_BITSTREAM("SignalledTileIdLengthMinus1: %d\n",
                    afti.getSignalledTileIdLengthMinus1());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.3 Atlas frame parameter set V-DMC extension syntax
void
AtlasWriter::afpsVdmcExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AtlasFrameParameterSetRbsp&    afps,
                               AfpsVdmcExtension&             afve) {
  TRACE_BITSTREAM_IN("%s", __func__);

  uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();
  WRITE_CODE(afve.getAfveOverridenFlag(), 1);
  if (afve.getAfveOverridenFlag()) {
    WRITE_CODE(afve.getAfveSubdivisionIterationCountPresentFlag(), 1);
    if (afve.getAfveSubdivisionIterationCountPresentFlag()) {
      WRITE_CODE(afve.getAfveSubdivisionIterationCount(), 3);
    }
    if (afve.getAfveSubdivisionIterationCount() > 0) {
      WRITE_CODE(afve.getAfveTransformMethodPresentFlag(), 1);
      }
    if (!afve.getAfveSubdivisionIterationCountPresentFlag()) {
      if (afve.getAfveSubdivisionIterationCount() != 0) {
        WRITE_CODE(afve.getAfveSubdivisionMethodPresentFlag(), 1);
    }
      if (asps.getAsveExtension().getAsveQuantizationParametersPresentFlag()) {
        WRITE_CODE(afve.getAfveQuantizationParametersPresentFlag(), 1);
    }
      if ((!afve.getAfveTransformMethodPresentFlag())
        && (afve.getAfveSubdivisionIterationCount() != 0)) {
        WRITE_CODE(afve.getAfveTransformParametersPresentFlag(), 1);
    }
  }
  }
  if (afve.getAfveSubdivisionMethodPresentFlag()) {
    if (afve.getAfveSubdivisionIterationCount() > 1) {
      WRITE_CODE(afve.getAfveLodAdaptiveSubdivisionFlag(), 1);
    }
    for (int32_t it = 0; it < afve.getAfveSubdivisionIterationCount(); it++) {
      if (afve.getAfveLodAdaptiveSubdivisionFlag() || it == 0) {
        WRITE_CODE(afve.getAfveSubdivisionMethod()[it], 3);
      }
    }
  }
  if ((asps.getAsveExtension().getAsveEdgeBasedSubdivisionFlag()) && (afve.getAfveSubdivisionIterationCount() != 0)) {
    WRITE_CODE(afve.getAfveEdgeBasedSubdivisionFlag(), 1);
    if (afve.getAfveEdgeBasedSubdivisionFlag()) {
      WRITE_CODE(afve.getAfveSubdivisionMinEdgeLength(), 16);
    }
  }
  //WRITE_CODE(afve.getAfveDisplacementCoordinateSystem(), 1);
  if (afve.getAfveQuantizationParametersPresentFlag())
    vdmcQuantizationParameters(bitstream,
                               afve.getAfveQuantizationParameters(),
                               afve.getAfveSubdivisionIterationCount(),
                               numComp);
  if (afve.getAfveTransformMethodPresentFlag()) {
    WRITE_CODE(afve.getAfveTransformMethod(), 3);
  }

  if (afve.getAfveTransformMethod()
        == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING
      && afve.getAfveTransformParametersPresentFlag()) {
    vdmcLiftingTransformParameters(
      bitstream,
      afve.getAfpsLtpDisplacement(),
      1,
      asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag(),
      afve.getAfveSubdivisionIterationCount(),
      NULL);
  }

  if (asps.getAsveExtension().getAsveAttributeInformationPresentFlag()) {
    if (asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount() > 1) {
      WRITE_CODE(afve.getAfveConsistentTilingAccrossAttributesFlag(), 1);
    }
    if (afve.getAfveConsistentTilingAccrossAttributesFlag()) {
      const auto refAttrIdx = afve.getAfveReferenceAttributeIdx();
      if (asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount() > 1) {
        WRITE_UVLC(refAttrIdx);
      }
      atlasFrameTileAttributeInformation(
        bitstream,
        afve.getAtlasFrameTileAttributeInformation(refAttrIdx),
        refAttrIdx,
        asps);
    } else {
      int aspsAttributeNominalFrameSizeCount =
        asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
      for (int i = 0; i < aspsAttributeNominalFrameSizeCount; i++) {
        atlasFrameTileAttributeInformation(
          bitstream, afve.getAtlasFrameTileAttributeInformation(i), i, asps);
      }
    }
  }

  atlasFrameMeshInformation(afve.getAtlasFrameMeshInformation(), bitstream);
  if (asps.getAsveExtension().getAsveProjectionTexcoordEnableFlag()) {
    auto numSubmeshes =
      afve.getAtlasFrameMeshInformation().getNumSubmeshesInAtlasFrameMinus1()
      + 1;
    for (int i = 0; i < numSubmeshes; i++) {
      WRITE_CODE(afve.getProjectionTextcoordPresentFlag(i), 1);
      if (afve.getProjectionTextcoordPresentFlag(i)) {
        WRITE_UVLC(afve.getProjectionTextcoordWidth(i));
        WRITE_UVLC(afve.getProjectionTextcoordHeight(i));
        WRITE_UVLC(afve.getProjectionTextcoordGutter(i));
      }
    }
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.4	Atlas frame attribute tile information syntax
void
AtlasWriter::atlasFrameTileAttributeInformation(
  vmesh::Bitstream&                   bitstream,
  AtlasFrameTileAttributeInformation& aftai,
  int32_t                             attrIdx,
  AtlasSequenceParameterSetRbsp&      asps) {
  TRACE_BITSTREAM_IN("%s", __func__);
  //vmesh::VERIFY_CONFORMANCE(attrIdx == 0);
  WRITE_CODE(aftai.getSingleTileInAtlasFrameFlag(), 1);  // u(1)
  if (!aftai.getSingleTileInAtlasFrameFlag()) {
    WRITE_CODE(aftai.getUniformPartitionSpacingFlag(), 1);  // u(1)
    if (aftai.getUniformPartitionSpacingFlag()) {
      WRITE_UVLC(aftai.getPartitionColumnWidthMinus1(0));  //  ue(v)
      WRITE_UVLC(aftai.getPartitionRowHeightMinus1(0));    //  ue(v)
      TRACE_BITSTREAM("numPartitionColumnMinus1 = %d\n",
                      aftai.getNumPartitionColumnsMinus1());
      TRACE_BITSTREAM("numPartitionRowsMinus1   = %d\n",
                      aftai.getNumPartitionRowsMinus1());
    } else {
      WRITE_UVLC(aftai.getNumPartitionColumnsMinus1());  //  ue(v)
      WRITE_UVLC(aftai.getNumPartitionRowsMinus1());     //  ue(v)
      for (size_t i = 0; i < aftai.getNumPartitionColumnsMinus1(); i++) {
        WRITE_UVLC(aftai.getPartitionColumnWidthMinus1(i));  //  ue(v)
      }
      for (size_t i = 0; i < aftai.getNumPartitionRowsMinus1(); i++) {
        WRITE_UVLC(aftai.getPartitionRowHeightMinus1(i));  //  ue(v)
      }
    }
    WRITE_CODE(aftai.getSinglePartitionPerTileFlag(), 1);  //  u(1)
    if (aftai.getSinglePartitionPerTileFlag() == 0u) {
      uint32_t NumPartitionsInAtlasFrame =
        (aftai.getNumPartitionColumnsMinus1() + 1)
        * (aftai.getNumPartitionRowsMinus1() + 1);
      WRITE_UVLC(aftai.getNumTilesInAtlasFrameMinus1());  // ue(v)
      for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
        uint8_t bitCount = vmesh::ceilLog2(NumPartitionsInAtlasFrame);
        WRITE_CODE(aftai.getTopLeftPartitionIdx(i), bitCount);     // u(v)
        WRITE_UVLC(aftai.getBottomRightPartitionColumnOffset(i));  // ue(v)
        WRITE_UVLC(aftai.getBottomRightPartitionRowOffset(i));     // ue(v)
      }
    }
  } else {
    WRITE_CODE(aftai.getNalTilePresentFlag(), 1);  // u(1)
  }
  WRITE_CODE(aftai.getSignalledTileIdFlag(), 1);  // u(1)
  if (aftai.getSignalledTileIdFlag()) {
    WRITE_UVLC(aftai.getSignalledTileIdLengthMinus1());  // ue(v)
    for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
      uint8_t bitCount = aftai.getSignalledTileIdLengthMinus1() + 1;
      WRITE_CODE(aftai.getTileId(i), bitCount);  // u(v)
    }
  } else {
    TRACE_BITSTREAM("NumTilesInAtlasFrameMinus1 : %d\n",
                    aftai.getNumTilesInAtlasFrameMinus1());
    TRACE_BITSTREAM("SignalledTileIdLengthMinus1: %d\n",
                    aftai.getSignalledTileIdLengthMinus1());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.5 Atlas frame mesh information syntax
void
AtlasWriter::atlasFrameMeshInformation(AtlasFrameMeshInformation& afmi,
                                       vmesh::Bitstream&          bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(afmi.getNumSubmeshesInAtlasFrameMinus1(), 6);  // u(6)
  WRITE_CODE(afmi.getSignalledSubmeshIdFlag(), 1);          // u(1)
  if (afmi.getSignalledSubmeshIdFlag()) {
    WRITE_UVLC(afmi.getSignalledSubmeshIdDeltaLength());  // ue(v)
    auto numSubmeshes =
      static_cast<size_t>(afmi.getNumSubmeshesInAtlasFrameMinus1()) + 1;
    uint32_t b = (uint32_t)ceil(log2(numSubmeshes));
    for (size_t i = 0; i < numSubmeshes; i++) {
      uint8_t bitCount = afmi.getSignalledSubmeshIdDeltaLength() + b;
      WRITE_CODE(afmi.getSubmeshId(i), bitCount);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.3	Atlas adaptation parameter set RBSP syntax
void
AtlasWriter::atlasAdaptationParameterSetRbsp(
  AtlasAdaptationParameterSetRbsp& aaps,
  vmesh::Bitstream&                bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(aaps.getAtlasAdaptationParameterSetId());  // ue(v)
  WRITE_CODE(aaps.getExtensionFlag(), 1);               // u(1)
  if (aaps.getExtensionFlag()) {
    WRITE_CODE(aaps.getVpccExtensionFlag(), 1);  // u(1)
    WRITE_CODE(aaps.getExtension7Bits(), 7);     // u(7)
    if (aaps.getVpccExtensionFlag())
      aapsVpccExtension(bitstream, aaps.getAapsVpccExtension());
    if (aaps.getExtension7Bits())
      while (moreRbspData(bitstream)) { WRITE_CODE(0, 1); }  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.4  Supplemental enhancement information Rbsp
void
AtlasWriter::seiRbsp(AtlasBitstream&   atlasBitstream,
                     vmesh::Bitstream& bitstream,
                     AtlasNalUnitType  nalUnitType,
                     SEI&              sei,
                     size_t            atglIndex) {
  TRACE_BITSTREAM_IN("%s", __func__);
  do {
    seiMessage(bitstream, atlasBitstream, nalUnitType, sei, atglIndex);
  } while (moreRbspData(bitstream));
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.5 Access unit delimiter RBSP syntax
void
AtlasWriter::accessUnitDelimiterRbsp(vmesh::AccessUnitDelimiterRbsp& aud,
                                     AtlasBitstream&   atlasBitstream,
                                     vmesh::Bitstream& bitstream) {
  WRITE_CODE(aud.getAframeType(), 3);  //	u(3)
  rbspTrailingBits(bitstream);
}
// 8.3.6.6 End of sequence RBSP syntax
void
AtlasWriter::endOfSequenceRbsp(vmesh::EndOfSequenceRbsp& eos,
                               AtlasBitstream&           atlasBitstream,
                               vmesh::Bitstream&         bitstream) {}

// 8.3.6.7 End of bitstream RBSP syntax
void
AtlasWriter::endOfAtlasSubBitstreamRbsp(
  vmesh::EndOfAtlasSubBitstreamRbsp& eoasb,
  AtlasBitstream&                    atlasBitstream,
  vmesh::Bitstream&                  bitstream) {}

// 8.3.6.8 Filler data RBSP syntax
void
AtlasWriter::fillerDataRbsp(vmesh::FillerDataRbsp& filler,
                            AtlasBitstream&        atlasBitstream,
                            vmesh::Bitstream&      bitstream) {
  // while ( next_bits( bitstream, 8 ) == 0xFF ) { uint32_t code; READ_CODE( code, 8 );  // f(8)
  rbspTrailingBits(bitstream);
}

// 8.3.6.9 Atlas tile group layer Rbsp syntax
void
AtlasWriter::atlasTileLayerRbsp(AtlasTileLayerRbsp& atgl,
                                AtlasBitstream&     atlasBitstream,
                                AtlasNalUnitType    nalUnitType,
                                vmesh::Bitstream&   bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  atlasTileHeader(atgl.getHeader(), atlasBitstream, nalUnitType, bitstream);
  // According to V3C 4th edition
  size_t afpsId = atgl.getHeader().getAtlasFrameParameterSetId();
  AtlasFrameParameterSetRbsp& afps =
    atlasBitstream.getAtlasFrameParameterSet(afpsId);
  size_t aspsId = afps.getAtlasSequenceParameterSetId();
  AtlasSequenceParameterSetRbsp& asps =
    atlasBitstream.getAtlasSequenceParameterSet(aspsId);
  if (!asps.getExtensionFlag() || asps.getVpccExtensionFlag() || asps.getMivExtensionFlag()) {
      //atlasTileDataUnit(atgl.getDataUnit(), atgl.getHeader(), atlasBitstream, bitstream);
  }
  if (asps.getVdmcExtensionFlag()) {
    vdmcAtlasTileDataUnit(
      atgl.getDataUnit(), atgl.getHeader(), atlasBitstream, bitstream);
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.10 RBSP trailing bit syntax
void
AtlasWriter::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.11  Atlas tile group header syntax
void
AtlasWriter::atlasTileHeader(AtlasTileHeader&  ath,
                             AtlasBitstream&   atlasBitstream,
                             AtlasNalUnitType  nalUnitType,
                             vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t                      afpsId = ath.getAtlasFrameParameterSetId();
  AtlasFrameParameterSetRbsp& afps =
    atlasBitstream.getAtlasFrameParameterSet(afpsId);
  size_t aspsId = afps.getAtlasSequenceParameterSetId();
  AtlasSequenceParameterSetRbsp& asps =
    atlasBitstream.getAtlasSequenceParameterSet(aspsId);
  AtlasFrameTileInformation& afti = afps.getAtlasFrameTileInformation();
  AfpsVdmcExtension&         afve = afps.getAfveExtension();
  // 8.4.6.11
  // The length of ath_id is max(AftiSignalledTileIDBitCount,
  // AftaiSignalledTileIDBitCount[0], .., AftaiSignalledTileIDBitCount[x]
  // for x in the range of 0 to(asve_num_attribute_video � 1)) bits.
  // avoid issue with automatic getAtlasFrameTileAttributeInformation resize
  // when using getAtlasFrameTileAttributeInformation(i)
  // otherwise when aftai is not present length minus1 is evaluated to 0
  // resulting in mismatch

  if (nalUnitType >= ATLAS_NAL_BLA_W_LP
      && nalUnitType <= ATLAS_NAL_RSV_IRAP_ACL_29) {
    WRITE_CODE(ath.getNoOutputOfPriorAtlasFramesFlag(), 1);  // u(1)
  }
  WRITE_UVLC(ath.getAtlasFrameParameterSetId());       // ue(v)
  WRITE_UVLC(ath.getAtlasAdaptationParameterSetId());  // ue(v)
  int64_t signalledTileIDBitCount = 0;
  if (afti.getSignalledTileIdFlag()) {
      signalledTileIDBitCount = afti.getSignalledTileIdLengthMinus1() + 1;
  }
  else {
      signalledTileIDBitCount = CeilLog2(afti.getNumTilesInAtlasFrameMinus1() + 1);
  }
  for (auto i = 0; i < asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount(); ++i) {
      int startId = 0;
      if (i == 0)
          startId = afti.getMaxTileId() + 1;
      else
          startId = afve.getAtlasFrameTileAttributeInformation(i - 1).getMaxTileId() + 1;
    AtlasFrameTileAttributeInformation& aftai = afve.getAtlasFrameTileAttributeInformation(i);
    int64_t signalledAttrTileIDBitCount = 0;
    if (aftai.getSignalledTileIdFlag()) {
        signalledAttrTileIDBitCount = aftai.getSignalledTileIdLengthMinus1() + 1;
    }
    else {
        signalledAttrTileIDBitCount = CeilLog2(aftai.getNumTilesInAtlasFrameMinus1() + 1 + startId);
    }
    signalledTileIDBitCount = std::max(signalledTileIDBitCount,
        signalledAttrTileIDBitCount);
  }
  if (signalledTileIDBitCount != 0) {
    WRITE_CODE(uint32_t(ath.getAtlasTileHeaderId()),
               signalledTileIDBitCount);  // u(v)
  }
  WRITE_UVLC(ath.getType());  // ue(v)
  if (afps.getOutputFlagPresentFlag()) {
    WRITE_CODE(ath.getAtlasOutputFlag(), 1);
  }
  WRITE_CODE(ath.getAtlasFrmOrderCntLsb(),
             asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4);  // u(v)
  if (asps.getNumRefAtlasFrameListsInAsps() > 0) {
    WRITE_CODE(ath.getRefAtlasFrameListSpsFlag(), 1);  // u(1)
  }
  if (static_cast<int>(ath.getRefAtlasFrameListSpsFlag()) == 0) {
    refListStruct(ath.getRefListStruct(), asps, bitstream);
  } else if (asps.getNumRefAtlasFrameListsInAsps() > 1) {
    size_t bitCount = vmesh::ceilLog2(asps.getNumRefAtlasFrameListsInAsps());
    WRITE_CODE(ath.getRefAtlasFrameListIdx(), bitCount);  // u(v)
  }
  uint8_t rlsIdx                = ath.getRefAtlasFrameListIdx();
  auto&   refList               = ath.getRefAtlasFrameListSpsFlag()
                                    ? asps.getRefListStruct(rlsIdx)
                                    : ath.getRefListStruct();
  size_t  numLtrAtlasFrmEntries = 0;
  for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
    if (!refList.getStRefAtlasFrameFlag(i)) { numLtrAtlasFrmEntries++; }
  }
  for (size_t j = 0; j < numLtrAtlasFrmEntries; j++) {
    WRITE_CODE(ath.getAdditionalAfocLsbPresentFlag(j), 1);  // u(1)
    if (ath.getAdditionalAfocLsbPresentFlag(j)) {
      uint8_t bitCount = afps.getAdditionalLtAfocLsbLen();
      WRITE_CODE(ath.getAdditionalAfocLsbVal(j), bitCount);  // u(v)
    }
  }
  if (ath.getType() != SKIP_TILE) {
    if (asps.getNormalAxisLimitsQuantizationEnabledFlag()) {
      WRITE_CODE(ath.getPosMinDQuantizer(), 5);  // u(5)
      if (asps.getNormalAxisMaxDeltaValueEnabledFlag()) {
        WRITE_CODE(ath.getPosDeltaMaxDQuantizer(), 5);  // u(5)
      }
    }
    if (asps.getPatchSizeQuantizerPresentFlag()) {
      WRITE_CODE(ath.getPatchSizeXinfoQuantizer(), 3);  // u(3)
      WRITE_CODE(ath.getPatchSizeYinfoQuantizer(), 3);  // u(3)
    }
    if (afps.getRaw3dOffsetBitCountExplicitModeFlag()) {
      size_t bitCount =
        vmesh::floorLog2(asps.getGeometry3dBitdepthMinus1() + 1);
      WRITE_CODE(ath.getRaw3dOffsetAxisBitCountMinus1(),
                 bitCount);  // u(v)
    }
    if (ath.getType() == P_TILE && refList.getNumRefEntries() > 1) {
      WRITE_CODE(ath.getNumRefIdxActiveOverrideFlag(), 1);  // u(1)
      if (ath.getNumRefIdxActiveOverrideFlag()) {
        WRITE_UVLC(ath.getNumRefIdxActiveMinus1());  // ue(v)
      }
    }
  }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.12  Reference list structure syntax
void
AtlasWriter::refListStruct(RefListStruct&                 rls,
                           AtlasSequenceParameterSetRbsp& asps,
                           vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(rls.getNumRefEntries());  // ue(v)
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (asps.getLongTermRefAtlasFramesFlag()) {
      WRITE_CODE(rls.getStRefAtlasFrameFlag(i), 1);  // u(1)
    }
    if (rls.getStRefAtlasFrameFlag(i)) {
      WRITE_UVLC(rls.getAbsDeltaAfocSt(i));  // ue(v)
      if (rls.getAbsDeltaAfocSt(i) > 0) {
        WRITE_CODE(rls.getStrafEntrySignFlag(i), 1);  // u(1)
      }
    } else {
      uint8_t bitCount = asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4;
      WRITE_CODE(rls.getAfocLsbLt(i), bitCount);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.13	Common atlas sequence parameter set RBSP syntax
// 8.3.6.14	Common atlas frame RBSP syntax

// 8.3.7	Atlas tile data unit syntax
// 8.3.7.1	General atlas tile data unit syntax
void
AtlasWriter::vdmcAtlasTileDataUnit(AtlasTileDataUnit& atdu,
                               AtlasTileHeader&   ath,
                               AtlasBitstream&    atlasBitstream,
                               vmesh::Bitstream&  bitstream) {
  TRACE_BITSTREAM_IN("%s(%s)", __func__, toString(ath.getType()).c_str());
  if (ath.getType() == SKIP_TILE) {
    skipMeshpatchDataUnit(bitstream);
  } else {
    for (size_t puCount = 0; puCount < atdu.getPatchCount(); puCount++) {
      WRITE_UVLC(uint32_t(atdu.getPatchMode(puCount)));  // ue(v)
      auto& pid           = atdu.getPatchInformationData(puCount);
      pid.getTileOrder()  = atdu.getTileOrder();
      pid.getPatchIndex() = puCount;
      patchInformationData(
        pid, atdu.getPatchMode(puCount), ath, atlasBitstream, bitstream);
    }
  }
  TRACE_BITSTREAM_OUT("%s(%s)", __func__, toString(ath.getType()).c_str());
}

// 8.3.7.2  Patch information data syntax
void
AtlasWriter::patchInformationData(PatchInformationData& pid,
                                  size_t                patchMode,
                                  AtlasTileHeader&      ath,
                                  AtlasBitstream&       atlasBitstream,
                                  vmesh::Bitstream&     bitstream) {
  if (ath.getType() == SKIP_TILE) {
    // skip mode: currently not supported but added it for convenience. Could
    // easily be removed
  } else if (ath.getType() == P_TILE) {
    VERIFY_CONFORMANCE((patchMode == P_SKIP || patchMode == P_INTRA
                        || patchMode == P_INTER || patchMode == P_MERGE
                        || patchMode == I_END));
    if (patchMode == P_SKIP) {
      skipMeshpatchDataUnit(bitstream);
    } else if (patchMode == P_INTRA) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlasBitstream, patchMode, bitstream);
    } else if (patchMode == P_INTER) {
      auto& imdu = pid.getInterMeshpatchDataUnit();
      imdu.setTileOrder(pid.getTileOrder());
      imdu.setCurrentPatchIndex(pid.getPatchIndex());
      interMeshpatchDataUnit(imdu, ath, atlasBitstream, patchMode, bitstream);
    } else if (patchMode == P_MERGE) {
      auto& mmdu           = pid.getMergeMeshpatchDataUnit();
      mmdu.getTileOrder()  = (pid.getTileOrder());
      mmdu.getPatchIndex() = (pid.getPatchIndex());
      mergeMeshpatchDataUnit(mmdu, ath, atlasBitstream, patchMode, bitstream);
    }
  } else if (ath.getType() == I_TILE) {
    VERIFY_CONFORMANCE(patchMode == I_INTRA || patchMode == I_END);
    if (patchMode == I_INTRA) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlasBitstream, patchMode, bitstream);
    }
  } else if (ath.getType() == P_TILE_ATTR) {
    VERIFY_CONFORMANCE((patchMode == I_INTRA_ATTR || patchMode == P_SKIP_ATTR
                        || patchMode == I_END_ATTR));
    if (patchMode == I_INTRA_ATTR) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlasBitstream, patchMode, bitstream);
    } else if (patchMode == P_SKIP) {
      skipMeshpatchDataUnit(bitstream);
    }
  } else if (ath.getType() == I_TILE_ATTR) {
    VERIFY_CONFORMANCE(patchMode == I_INTRA_ATTR || patchMode == I_END_ATTR);
    if (patchMode == I_INTRA_ATTR) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlasBitstream, patchMode, bitstream);
    }
  }
}

// 8.3.7.3  Patch data unit syntax
void
AtlasWriter::meshpatchDataUnit(MeshpatchDataUnit& mdu,
                               AtlasTileHeader&   ath,
                               AtlasBitstream&    atlasBitstream,
                               size_t             patchMode,
                               vmesh::Bitstream&  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t  afpsId     = ath.getAtlasFrameParameterSetId();
  auto&   afps       = atlasBitstream.getAtlasFrameParameterSet(afpsId);
  size_t  aspsId     = afps.getAtlasSequenceParameterSetId();
  auto&   asps       = atlasBitstream.getAtlasSequenceParameterSet(aspsId);
  uint8_t bitCountUV = asps.getGeometry3dBitdepthMinus1() + 1;
  uint8_t bitCountD =
    asps.getGeometry3dBitdepthMinus1() - ath.getPosMinDQuantizer() + 1;

  auto& asve = asps.getAsveExtension();
  auto& ltp  = mdu.getMduLiftingTransformParameters();
  auto& afve = afps.getAfveExtension();
  auto& afmi = afve.getAtlasFrameMeshInformation();

  uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();
  WRITE_UVLC(mdu.getMduSubmeshId());
  if (ath.getType() == I_TILE || ath.getType() == P_TILE) {
    if (asve.getAsveDisplacementIdPresentFlag()) {
      WRITE_UVLC(mdu.getMduDisplId());
    } else {
      if (asve.getAsveLodPatchesEnableFlag() == 1) {
        WRITE_UVLC(mdu.getMduLoDIdx());
      }
      //if (mdu.getMduLoDIdx() == 0) { WRITE_UVLC(mdu.getPduFaceCountMinus1()); }
      WRITE_UVLC(mdu.getMduGeometry2dPosX());
      WRITE_UVLC(mdu.getMduGeometry2dPosY());
      WRITE_UVLC(mdu.getMduGeometry2dSizeXMinus1());
      WRITE_UVLC(mdu.getMduGeometry2dSizeYMinus1());
    }
    if (mdu.getMduLoDIdx() == 0) {
      WRITE_CODE(mdu.getMduParametersOverrideFlag(), 1);
      if (mdu.getMduParametersOverrideFlag()) {
        WRITE_CODE(mdu.getMduSubdivisionIterationCountPresentFlag(), 1);
        if (mdu.getMduSubdivisionIterationCountPresentFlag())
          WRITE_CODE(mdu.getMduSubdivisionIterationCount(), 3);
        if (mdu.getMduSubdivisionIterationCount() != 0)
          WRITE_CODE(mdu.getMduTransformMethodPresentFlag(), 1);
        if ((!mdu.getMduSubdivisionIterationCountPresentFlag())
            && mdu.getMduSubdivisionIterationCount() != 0)
          WRITE_CODE(mdu.getMduSubdivisionMethodPresentFlag(), 1);
        if (asps.getAsveExtension()
              .getAsveQuantizationParametersPresentFlag()) {
          if (!mdu.getMduSubdivisionIterationCountPresentFlag()) {
            WRITE_CODE(mdu.getMduQuantizationPresentFlag(), 1);
          }
        }
        if ((!mdu.getMduTransformMethodPresentFlag())
            && (mdu.getMduSubdivisionIterationCount() != 0)) {
          WRITE_CODE(mdu.getMduTransformParametersPresentFlag(), 1);
        }
      }
      if (mdu.getMduSubdivisionMethodPresentFlag()) {
        if (mdu.getMduSubdivisionIterationCount() > 1) {
          WRITE_CODE(mdu.getMduLodAdaptiveSubdivisionFlag(), 1);
        }
        for (int32_t it = 0; it < mdu.getMduSubdivisionIterationCount();
             it++) {
          if (mdu.getMduLodAdaptiveSubdivisionFlag() || it == 0) {
            WRITE_CODE(mdu.getMduSubdivisionMethod()[it], 3);
          }
        }
      }
      if (asps.getAsveExtension().getAsveEdgeBasedSubdivisionFlag()
          && mdu.getMduSubdivisionIterationCount() != 0) {
        WRITE_CODE(mdu.getMduEdgeBasedSubdivisionFlag(), 1);  //u(1)
        if (mdu.getMduEdgeBasedSubdivisionFlag()) {

          WRITE_CODE(mdu.getMduSubdivisionMinEdgeLength(), 16);
        }
      }
      if (mdu.getMduQuantizationPresentFlag()) {
        vdmcQuantizationParameters(bitstream,
                                   mdu.getMduQuantizationParameters(),
                                   mdu.getMduSubdivisionIterationCount(),
                                   numComp);
      }

      if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
        WRITE_CODE(mdu.getMduInverseQuantizationOffsetEnableFlag(), 1);  //u(1)
        if (mdu.getMduInverseQuantizationOffsetEnableFlag()) {
          for (int i = 0; i < mdu.getMduSubdivisionIterationCount() + 1; i++) {
            for (int j = 0; j < numComp; j++) {
              for (
                int k = 0; k < 3;
                k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
                WRITE_CODE(mdu.getIQOffsetValues(i, j, k, 0), 1);  //sign
                WRITE_SVLC(
                  mdu.getIQOffsetValues(i, j, k, 1));  //first precision
                WRITE_SVLC(
                  mdu.getIQOffsetValues(i, j, k, 2));  //second precision
              }
            }
          }
        }
      }
      WRITE_CODE(mdu.getMduDisplacementCoordinateSystem(), 1);

      if (mdu.getMduTransformMethodPresentFlag()) {
        WRITE_CODE(mdu.getMduTransformMethod(), 3);
      }

      if (mdu.getMduTransformMethod()
          == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING) {
        if (asve.getAsveLiftingOffsetPresentFlag()) {
          WRITE_CODE(mdu.getMduLiftingOffsetPresentFlag(), 1);
          if (mdu.getMduLiftingOffsetPresentFlag()) {
            auto vdmcLtp = mdu.getMduLiftingTransformParameters();
            for (int i = 0; i < mdu.getMduSubdivisionIterationCount(); i++) {
              WRITE_SVLC(vdmcLtp.getLiftingOffsetValuesNumerator()[i]);
              WRITE_UVLC(vdmcLtp.getLiftingOffsetValuesDenominatorMinus1()[i]);
            }
          }
        }
        if (asve.getAsveDirectionalLiftingPresentFlag()) {
          WRITE_CODE(mdu.getMduDirectionalLiftingPresentFlag(), 1);
          if (mdu.getMduDirectionalLiftingPresentFlag()) {
            WRITE_SVLC(mdu.getMduDirectionalLiftingMeanNum());
            WRITE_UVLC(mdu.getMduDirectionalLiftingMeanDenoMinus1());
            WRITE_UVLC(mdu.getMduDirectionalLiftingStdNum());
            WRITE_UVLC(mdu.getMduDirectionalLiftingMeanDenoMinus1());
      }
        }
      }
      if (mdu.getMduTransformParametersPresentFlag()) {
        vdmcLiftingTransformParameters(
          bitstream,
          mdu.getMduLiftingTransformParameters(),
          2,
          asve.getAsveDirectionalLiftingPresentFlag(),
          mdu.getMduSubdivisionIterationCount(),
          patchMode);
      }
    }
    auto bitcount = asps.getLog2PatchPackingBlockSize() * 2;

    if (asve.getAsveDisplacementIdPresentFlag()
        || !asve.getAsveLodPatchesEnableFlag()) {
      for (int32_t i = 0; i <= mdu.getMduSubdivisionIterationCount(); i++) {
        WRITE_UVLC(mdu.getMduBlockCount()[i]);
        WRITE_CODE(mdu.getMduLastPosInBlock()[i], bitcount);
      }
    } else {
      WRITE_UVLC(mdu.getMduBlockCount()[mdu.getMduLoDIdx()]);
      WRITE_CODE(mdu.getMduLastPosInBlock()[mdu.getMduLoDIdx()], bitcount);
    }
    if (mdu.getMduLoDIdx() == 0) {
      auto& tpInfo = mdu.getTextureProjectionInformation();
      //orthoAtlas HLS
      if (asve.getAsveProjectionTexcoordEnableFlag()) {
        auto submeshId  = mdu.getMduSubmeshId();
        int  submeshIdx = afmi._submeshIDToIndex[submeshId];

        if (afve.getProjectionTextcoordPresentFlag(submeshIdx)) {
          textureProjectionInformation(
            mdu.getTextureProjectionInformation(), asps, bitstream);
        }
      }
    }
  } else if (ath.getType() == I_TILE_ATTR || ath.getType() == P_TILE_ATTR) {
    if (asve.getAsveAttributeSubtextureEnabledFlag()[0]) {
      WRITE_UVLC(mdu.getMduAttributes2dPosX());
      WRITE_UVLC(mdu.getMduAttributes2dPosY());
      WRITE_UVLC(mdu.getMduAttributes2dSizeXMinus1());
      WRITE_UVLC(mdu.getMduAttributes2dSizeYMinus1());
    }
  }
}

// 8.3.7.4  Skip patch data unit syntax
void
AtlasWriter::skipMeshpatchDataUnit(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.10  No patch data unit syntax
void
AtlasWriter::noPatchDataUnit(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.5  Merge patch data unit syntax
void
AtlasWriter::mergeMeshpatchDataUnit(MergeMeshpatchDataUnit& mmdu,
                                    AtlasTileHeader&        ath,
                                    AtlasBitstream&         atlasBitstream,
                                    size_t                  patchMode,
                                    vmesh::Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t afpsId = ath.getAtlasFrameParameterSetId();
  auto&  afps   = atlasBitstream.getAtlasFrameParameterSet(afpsId);
  auto&  afve   = afps.getAfveExtension();

  size_t  aspsId  = afps.getAtlasSequenceParameterSetId();
  auto&   asps    = atlasBitstream.getAtlasSequenceParameterSet(aspsId);
  auto&   asve    = asps.getAsveExtension();
  auto& ltp = mmdu.getMmduLiftingTransformParameters();
  uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();

  size_t numRefIdxActive = ath.getNumRefIdxActiveMinus1() + 1;
  if (numRefIdxActive > 1) {
    WRITE_UVLC(mmdu.getMmduRefIndex());  // ue(v)
  }
  if (asve.getAsveInverseQuantizationOffsetPresentFlag() || asve.getAsveLiftingOffsetPresentFlag()) {
    WRITE_CODE(mmdu.getMmduSubdivisionIterationCountPresentFlag(), 1);
    if (mmdu.getMmduSubdivisionIterationCountPresentFlag())
      WRITE_CODE(mmdu.getMmduSubdivisionIterationCount(), 3);
  }
  if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
    WRITE_CODE(mmdu.getMmduInverseQuantizationOffsetEnableFlag(), 1);
    if (mmdu.getMmduInverseQuantizationOffsetEnableFlag()) {
        for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount() + 1; i++) {
          for (int j = 0; j < numComp; j++) {
            for (
              int k = 0; k < 3;
              k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
              WRITE_CODE(mmdu.getIQOffsetValues(i, j, k, 0), 1);  //sign
              WRITE_SVLC(
                mmdu.getIQOffsetValues(i, j, k, 1));  //first precision
              WRITE_SVLC(
                mmdu.getIQOffsetValues(i, j, k, 2));  //second precision
            }
          }
        }
      }
    }
  if (asve.getAsveLiftingOffsetPresentFlag()) {
    WRITE_CODE(mmdu.getMmduLiftingOffsetPresentFlag(), 1);
    if (mmdu.getMmduLiftingOffsetPresentFlag()) {
      auto vdmcLtp = mmdu.getMmduLiftingTransformParameters();
        for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount(); i++) {
          WRITE_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesNumerator()[i]);
          WRITE_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesDenominator()[i]);
        }
      }
    }
  if (asve.getAsveDirectionalLiftingPresentFlag()) {
    WRITE_CODE(mmdu.getMmduDirectionalLiftingPresentFlag(), 1);
    if (mmdu.getMmduDirectionalLiftingPresentFlag()) {
      WRITE_SVLC(mmdu.getMmduDirectionalLiftingDeltaMeanNum());
      WRITE_SVLC(mmdu.getMmduDirectionalLiftingDeltaMeanDeno());
      WRITE_SVLC(mmdu.getMmduDirectionalLiftingDeltaStdNum());
      WRITE_SVLC(mmdu.getMmduDirectionalLiftingDeltaMeanDeno());
      }
    }
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    WRITE_CODE(mmdu.getMmduTextureProjectionPresentFlag(), 1);  //u(1)
    if (mmdu.getMmduTextureProjectionPresentFlag())
        textureProjectionMergeInformation(
          mmdu.getTextureProjectionMergeInformation(), asps, bitstream);
    }

  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.7.6  Inter patch data unit syntax
void
AtlasWriter::interMeshpatchDataUnit(InterMeshpatchDataUnit& imdu,
                                    AtlasTileHeader&        ath,
                                    AtlasBitstream&         atlasBitstream,
                                    size_t                  patchMode,
                                    vmesh::Bitstream&       bitstream) {
  size_t  afpsId     = ath.getAtlasFrameParameterSetId();
  auto&   afps       = atlasBitstream.getAtlasFrameParameterSet(afpsId);
  size_t  aspsId     = afps.getAtlasSequenceParameterSetId();
  auto&   asps       = atlasBitstream.getAtlasSequenceParameterSet(aspsId);
  uint8_t bitCountUV = asps.getGeometry3dBitdepthMinus1() + 1;
  uint8_t bitCountD =
    asps.getGeometry3dBitdepthMinus1() - ath.getPosMinDQuantizer() + 1;

  auto&   asve    = asps.getAsveExtension();
  auto&   afve    = afps.getAfveExtension();
  auto&   ltp     = imdu.getImduLiftingTransformParameters();
  uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();

  size_t numRefIdxActive = ath.getNumRefIdxActiveMinus1() + 1;
  if (numRefIdxActive > 1) {
    WRITE_UVLC(imdu.getImduRefIndex());  // ue(v)
  }
  WRITE_SVLC(imdu.getImduPatchIndex());

  if (!asve.getAsveDisplacementIdPresentFlag()) {
    if (asve.getAsveLodPatchesEnableFlag() == 1) {
      WRITE_UVLC(imdu.getImduLoDIdx());
    }
    WRITE_SVLC(imdu.getImdu2dDeltaPosX());
    WRITE_SVLC(imdu.getImdu2dDeltaPosY());
    WRITE_SVLC(imdu.getImdu2dDeltaSizeX());
    WRITE_SVLC(imdu.getImdu2dDeltaSizeY());
  }
  if (imdu.getImduLoDIdx() == 0) {
    WRITE_CODE(imdu.getImduSubdivisionIterationCountPresentFlag(), 1);
    if (imdu.getImduSubdivisionIterationCountPresentFlag())
      WRITE_CODE(imdu.getImduSubdivisionIterationCount(), 3);

    if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
      WRITE_CODE(imdu.getImduInverseQuantizationOffsetEnableFlag(), 1);
      if (imdu.getImduInverseQuantizationOffsetEnableFlag()) {
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount() + 1; i++) {
          for (int j = 0; j < numComp; j++) {
            for (
              int k = 0; k < 3;
              k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
              WRITE_CODE(imdu.getIQOffsetValues(i, j, k, 0), 1);  //sign
              WRITE_SVLC(
                imdu.getIQOffsetValues(i, j, k, 1));  //first precision
              WRITE_SVLC(
                imdu.getIQOffsetValues(i, j, k, 2));  //second precision
            }
          }
        }
      }
    }

    if (imdu.getImduSubdivisionIterationCount() > 0)
      WRITE_CODE(imdu.getImduTransformParametersPresentFlag(), 1);

    WRITE_CODE(imdu.getImduTransformMethodPresentFlag(), 1);
    if (imdu.getImduTransformMethodPresentFlag())
      WRITE_CODE(imdu.getImduTransformMethod(), 3);

    if (imdu.getImduTransformMethod()
          == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING
        && asve.getAsveLiftingOffsetPresentFlag()) {
      WRITE_CODE(imdu.getImduLiftingOffsetPresentFlag(), 1);
      if (imdu.getImduLiftingOffsetPresentFlag()) {
        auto vdmcLtp = imdu.getImduLiftingTransformParameters();
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount(); i++) {
          WRITE_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesNumerator()[i]);
          WRITE_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesDenominator()[i]);
        }
      }
    }
    if (imdu.getImduTransformMethod()
          == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING
        && asve.getAsveDirectionalLiftingPresentFlag()) {
      WRITE_CODE(imdu.getImduDirectionalLiftingPresentFlag(), 1);
      if (imdu.getImduDirectionalLiftingPresentFlag()) {
        WRITE_SVLC(imdu.getImduDirectionalLiftingDeltaMeanNum());
        WRITE_SVLC(imdu.getImduDirectionalLiftingDeltaMeanDeno());
        WRITE_SVLC(imdu.getImduDirectionalLiftingDeltaStdNum());
        WRITE_SVLC(imdu.getImduDirectionalLiftingDeltaMeanDeno());
      }
    }
    if (imdu.getImduTransformMethod()
          == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING
        && imdu.getImduTransformParametersPresentFlag()
        && (imdu.getImduSubdivisionIterationCount() != 0)) {
      vdmcLiftingTransformParameters(bitstream,
                                     imdu.getImduLiftingTransformParameters(),
                                     2,
                                     asve.getAsveDirectionalLiftingPresentFlag(),
                                     imdu.getImduSubdivisionIterationCount(),
                                     patchMode);
    }

    if (asve.getAsveProjectionTexcoordEnableFlag()) {
      WRITE_CODE(imdu.getImduTextureProjectionPresentFlag(), 1);  //u(1)
      if (imdu.getImduTextureProjectionPresentFlag())
        textureProjectionInterInformation(
          imdu.getTextureProjectionInterInformation(), asps, bitstream);
    }
  }
  auto vertexInfoCount = 1;
  if (asve.getAsveDisplacementIdPresentFlag()
      || !asve.getAsveLodPatchesEnableFlag())
    vertexInfoCount = imdu.getImduSubdivisionIterationCount() + 1;
  if (asve.getAsveLodPatchesEnableFlag() == 1) {
    int i = imdu.getImduLoDIdx();
    WRITE_SVLC(imdu.getImduDeltaBlockCount()[i]);
    WRITE_SVLC(imdu.getImduDeltaLastPosInBlock()[i]);
  } else {
    for (int32_t i = 0; i < vertexInfoCount; i++) {
      WRITE_SVLC(imdu.getImduDeltaBlockCount()[i]);
      WRITE_SVLC(imdu.getImduDeltaLastPosInBlock()[i]);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.7.7 raw patch data unit syntax

// 8.3.7.8 EOM patch data unit syntax

// 8.3.7.9 Point local reconstruction data syntax

// 8.3.7.11	Texture projection information syntax
void
AtlasWriter::textureProjectionInformation(TextureProjectionInformation&  tpi,
                                          AtlasSequenceParameterSetRbsp& asps,
                                          vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(tpi.getFaceIdPresentFlag(), 1);           //u(1)
  WRITE_CODE_40bits(tpi.getFrameUpscaleMinus1(), 40);  // u(40)
  WRITE_CODE(tpi.getFrameDownscale(), 6);
  WRITE_UVLC(tpi.getSubpatchCountMinus1());  // ue(v)
  if (asps.getAsveExtension().getAsveProjectionRawTextcoordPresentFlag()) {
    WRITE_CODE(tpi.getSubpatchRawEnableFlag(), 1);  //u(1)
  } else {
    tpi.getSubpatchRawEnableFlag() = 0;
  }
  for (int idx = 0; idx < tpi.getSubpatchCountMinus1() + 1; idx++) {
    if (tpi.getFaceIdPresentFlag())
      WRITE_UVLC(tpi.getFaceId2SubpatchIdx(idx));  // ue(v)
    if (tpi.getSubpatchRawEnableFlag()) {
      WRITE_CODE(tpi.getSubpatchRawPresentFlag(idx), 1);  //u(1)
    } else {
      tpi.getSubpatchRawPresentFlag(idx) = 0;
    }
    if (tpi.getSubpatchRawPresentFlag(idx)) {
      subpatchRawInformation(tpi.getSubpatchRaw(idx), asps, bitstream);
    } else {
      subpatchInformation(tpi.getSubpatch(idx), asps, bitstream);
    }
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.12	Sub-patch information syntax
void
AtlasWriter::subpatchInformation(SubpatchInformation&           si,
                                 AtlasSequenceParameterSetRbsp& asps,
                                 vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(si.getProjectionId(),
             asps.getExtendedProjectionEnabledFlag() ? 5 : 3);  // u(v)
  WRITE_UVLC(si.getOrientationId());                            // ue(v)
  WRITE_UVLC(si.get2dPosX());                                   // ue(v)
  WRITE_UVLC(si.get2dPosY());                                   // ue(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    WRITE_UVLC(si.getPosBiasX());       // ue(v)
    WRITE_UVLC(si.getPosBiasY());       // ue(v)
    WRITE_SVLC(si.get2dSizeXMinus1());  // se(v)
    WRITE_SVLC(si.get2dSizeYMinus1());  // se(v)
  }
  WRITE_CODE(si.getScalePresentFlag(), 1);  // u(1)
  if (si.getScalePresentFlag()) {
    WRITE_UVLC(si.getSubpatchScale());  // ue(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch raw information syntax
void
AtlasWriter::subpatchRawInformation(SubpatchRawInformation&        sri,
                                    AtlasSequenceParameterSetRbsp& asps,
                                    vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto qpTexCoord =
    asps.getAsveExtension().getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
  WRITE_UVLC(sri.getNumRawUVMinus1());  // ue(v)
  for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
    WRITE_CODE(sri.getUcoord(i), qpTexCoord);  // u(v)
    WRITE_CODE(sri.getVcoord(i), qpTexCoord);  // u(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Texture projection inter information syntax
void
AtlasWriter::textureProjectionInterInformation(
  TextureProjectionInterInformation& tpii,
  AtlasSequenceParameterSetRbsp&     asps,
  vmesh::Bitstream&                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(tpii.getFaceIdPresentFlag(), 1);           //u(1)
  WRITE_CODE_40bits(tpii.getFrameUpscaleMinus1(), 40);  // u(40)
  WRITE_CODE(tpii.getFrameDownscale(), 6);              // u(6)
  WRITE_UVLC(tpii.getSubpatchCountMinus1());            // ue(v)
  WRITE_CODE(tpii.getSubpatchInterEnableFlag(), 1);     //u(1)
  if (asps.getAsveExtension().getAsveProjectionRawTextcoordPresentFlag()) {
    WRITE_CODE(tpii.getSubpatchRawEnableFlag(), 1);  //u(1)
  } else {
    tpii.getSubpatchRawEnableFlag() = 0;
  }
  for (int idx = 0; idx < tpii.getSubpatchCountMinus1() + 1; idx++) {
    if (tpii.getFaceIdPresentFlag())
      WRITE_UVLC(tpii.getFaceId2SubpatchIdx(idx));  // ue(v)
    if (tpii.getSubpatchInterEnableFlag()) {
      WRITE_CODE(tpii.getUpdateFlag(idx), 1);  //u(1)
    } else {
      tpii.getUpdateFlag(idx) = 0;
    }
    if (tpii.getSubpatchRawEnableFlag()) {
      WRITE_CODE(tpii.getSubpatchRawPresentFlag(idx), 1);  //u(1)
    } else {
      tpii.getSubpatchRawPresentFlag(idx) = 0;
    }
    if (tpii.getUpdateFlag(idx)) {
      subpatchInterInformation(tpii.getSubpatchInter(idx), asps, bitstream);
    } else {
      if (tpii.getSubpatchRawPresentFlag(idx)) {
        subpatchRawInformation(tpii.getSubpatchRaw(idx), asps, bitstream);
      } else {
        subpatchInformation(tpii.getSubpatch(idx), asps, bitstream);
      }
    }
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch inter information syntax
void
AtlasWriter::subpatchInterInformation(SubpatchInterInformation&      sii,
                                      AtlasSequenceParameterSetRbsp& asps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_SVLC(sii.getSubpatchIdxDiff());  // ue(v)
  WRITE_SVLC(sii.get2dPosXDelta());      // se(v)
  WRITE_SVLC(sii.get2dPosYDelta());      // se(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    WRITE_SVLC(sii.getPosBiasXDelta());  // ue(v)
    WRITE_SVLC(sii.getPosBiasYDelta());  // ue(v)
    WRITE_SVLC(sii.get2dSizeXDelta());   // se(v)
    WRITE_SVLC(sii.get2dSizeYDelta());   // se(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Texture projection merge information syntax
void
AtlasWriter::textureProjectionMergeInformation(
  TextureProjectionMergeInformation& tpmi,
  AtlasSequenceParameterSetRbsp&     asps,
  vmesh::Bitstream&                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(tpmi.getSubpatchMergePresentFlag(), 1);  // u(1)
  if (tpmi.getSubpatchMergePresentFlag()) {
    WRITE_UVLC(tpmi.getSubpatchCountMinus1());  // ue(v)
    for (int idx = 0; idx < tpmi.getSubpatchCountMinus1() + 1; idx++)
      subpatchMergeInformation(tpmi.getSubpatchMerge(idx), asps, bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch merge information syntax
void
AtlasWriter::subpatchMergeInformation(SubpatchMergeInformation&      smi,
                                      AtlasSequenceParameterSetRbsp& asps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_UVLC(smi.getSubpatchIdx());  // ue(v)
  WRITE_SVLC(smi.get2dPosXDelta());  // se(v)
  WRITE_SVLC(smi.get2dPosYDelta());  // se(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    WRITE_SVLC(smi.getPosBiasXDelta());  // ue(v)
    WRITE_SVLC(smi.getPosBiasYDelta());  // ue(v)
    WRITE_SVLC(smi.get2dSizeXDelta());   // se(v)
    WRITE_SVLC(smi.get2dSizeYDelta());   // se(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.8 Supplemental enhancement information message syntax
void
AtlasWriter::seiMessage(vmesh::Bitstream& bitstream,
                        AtlasBitstream&   atlasBitstream,
                        AtlasNalUnitType  nalUnitType,
                        SEI&              sei,
                        size_t            atglIndex) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto payloadType = static_cast<int32_t>(sei.getPayloadType());
  for (; payloadType >= 0xff; payloadType -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadType, 8);  // u(8)

  // Note: calculating the size of the sei message before writing it into the bitstream
  vmesh::Bitstream tempbitstream;
  seiPayload(tempbitstream, atlasBitstream, sei, nalUnitType, atglIndex);
  sei.setPayloadSize(tempbitstream.size());

  auto payloadSize = static_cast<int32_t>(sei.getPayloadSize());
  for (; payloadSize >= 0xff; payloadSize -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadSize, 8);  // u(8)
  seiPayload(bitstream, atlasBitstream, sei, nalUnitType, atglIndex);
}

// D.2 Sample stream NAL unit syntax and semantics
// D.2.1 Sample stream NAL header syntax
void
AtlasWriter::sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                                   vmesh::SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  WRITE_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2  SEI payload syntax
// F.2.1  General SEI message syntax
void
AtlasWriter::seiPayload(vmesh::Bitstream& bitstream,
                        AtlasBitstream&   atlasBitstream,
                        SEI&              sei,
                        AtlasNalUnitType  nalUnitType,
                        size_t            atglIndex) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto payloadType = sei.getPayloadType();
  if (nalUnitType == ATLAS_NAL_PREFIX_ESEI
      || nalUnitType == ATLAS_NAL_PREFIX_NSEI) {
    if (payloadType == BUFFERING_PERIOD) {  // 0
      bufferingPeriod(bitstream, sei);
    } else if (payloadType == ATLAS_FRAME_TIMING) {  // 1
      assert(atlasBitstream //syntax.getAtlasDataStream()
               .getAtlasTileLayer(atglIndex)
               .getSEI()
               .seiIsPresent(ATLAS_NAL_PREFIX_NSEI, BUFFERING_PERIOD));

      auto& bpsei =
        *atlasBitstream.getAtlasTileLayer(atglIndex).getSEI().getLastSei(
          ATLAS_NAL_PREFIX_NSEI, BUFFERING_PERIOD);
      atlasFrameTiming(bitstream, sei, bpsei, false);
    } else if (payloadType == FILLER_PAYLOAD) {  // 2
      fillerPayload(bitstream, sei);
    } else if (payloadType == USER_DATAREGISTERED_ITUTT35) {  // 3
      userDataRegisteredItuTT35(bitstream, sei);
    } else if (payloadType == USER_DATA_UNREGISTERED) {  // 4
      userDataUnregistered(bitstream, sei);
    } else if (payloadType == RECOVERY_POINT) {  // 5
      recoveryPoint(bitstream, sei);
    } else if (payloadType == NO_RECONSTRUCTION) {  // 6
      noReconstruction(bitstream, sei);
    } else if (payloadType == TIME_CODE) {  // 7
      timeCode(bitstream, sei);
    } else if (payloadType == SEI_MANIFEST) {  // 8
      seiManifest(bitstream, sei);
    } else if (payloadType == SEI_PREFIX_INDICATION) {  // 9
      seiPrefixIndication(bitstream, sei);
    } else if (payloadType == ACTIVE_SUB_BITSTREAMS) {  // 10
      activeSubBitstreams(bitstream, sei);
    } else if (payloadType == COMPONENT_CODEC_MAPPING) {  // 11
      componentCodecMapping(bitstream, sei);
    } else if (payloadType == SCENE_OBJECT_INFORMATION) {  // 12
      sceneObjectInformation(bitstream, sei);
    } else if (payloadType == OBJECT_LABEL_INFORMATION) {  // 13
      objectLabelInformation(bitstream, sei);
    } else if (payloadType == PATCH_INFORMATION) {  // 14
      patchInformation(bitstream, sei);
    } else if (payloadType == VOLUMETRIC_RECTANGLE_INFORMATION) {  // 15
      volumetricRectangleInformation(bitstream, sei);
    } else if (payloadType == ATLAS_OBJECT_INFORMATION) {  // 16
      atlasObjectInformation(bitstream, sei);
    } else if (payloadType == VIEWPORT_CAMERA_PARAMETERS) {  // 17
      viewportCameraParameters(bitstream, sei);
    } else if (payloadType == VIEWPORT_POSITION) {  // 18
      viewportPosition(bitstream, sei);
    } else if (payloadType == ATTRIBUTE_TRANSFORMATION_PARAMS) {  // 64
      attributeTransformationParams(bitstream, sei);
    } else if (payloadType == OCCUPANCY_SYNTHESIS) {  // 65
      occupancySynthesis(bitstream, sei);
    } else if (payloadType == GEOMETRY_SMOOTHING) {  // 66
      geometrySmoothing(bitstream, sei);
    } else if (payloadType == ATTRIBUTE_SMOOTHING) {  // 67
      attributeSmoothing(bitstream, sei);
    } else if (payloadType == VPCC_REGISTERED) {  // 68
      vpccRegisteredSEI(bitstream, sei);
    } else if (payloadType == VIEWING_SPACE) {  // 128
                                                //viewingSpace(bitstream, sei);
    } else if (payloadType == VIEWING_SPACE_HANDLING) {  // 129
      //viewingSpaceHandling(bitstream, sei);
    } else if (payloadType == GEOMETRY_UPSCALING_PARAMETERS) {  // 130
      //geometryUpscalingParameters(bitstream, sei);
    } else if (payloadType == ATLAS_VIEW_ENABLED) {  // 131
      //atlasViewEnabled(bitstream, sei);
    } else if (payloadType == OMAF_V1_COMPATIBLE) {  // 132
      //omafV1Compatible(bitstream, sei);
    } else if (payloadType == GEOMETRY_ASSISTANCE) {  // 133
      //geometryAssitance(bitstream, sei);
    } else if (payloadType == EXTENDED_GEOMETRY_ASSISTANCE) {  // 134
      //extendedGeometryAssitance(bitstream, sei);
    } else if (payloadType == MIV_REGISTERED) {  // 135
      //mivRegisteredSEI(bitstream, sei);
    } else if (payloadType == VDMC_REGISTERED) {  // 192
      vdmcRegisteredSEI(bitstream, sei);
    } else {
      reservedMessage(bitstream, sei);
    }
  } else { /* nalUnitType  ==  NAL_SUFFIX_SEI  || nalUnitType  ==
             NAL_SUFFIX_NSEI */
    /*SEI& sei = syntax.addSeiSuffix( payloadType, nalUnitType == NAL_SUFFIX_ESEI );*/
    if (payloadType == FILLER_PAYLOAD) {  // 2
      fillerPayload(bitstream, sei);
    } else if (payloadType == USER_DATAREGISTERED_ITUTT35) {  // 3
      userDataRegisteredItuTT35(bitstream, sei);
    } else if (payloadType == USER_DATA_UNREGISTERED) {  // 4
      userDataUnregistered(bitstream, sei);
    } else if (payloadType == DECODED_ATLAS_INFORMATION_HASH) {  // 19
      decodedAtlasInformationHash(bitstream, sei);
    } else if (payloadType == VPCC_REGISTERED) {  // 68
      vpccRegisteredSEI(bitstream, sei);
    } else if (payloadType == GEOMETRY_ASSISTANCE) {  // 133
      //geometryAssitance(bitstream, sei);
    } else if (payloadType == EXTENDED_GEOMETRY_ASSISTANCE) {  // 134
      //extendedGeometryAssitance(bitstream, sei);
    } else if (payloadType == MIV_REGISTERED) {  // 135
      //mivRegisteredSEI(bitstream, sei);
    } else if (payloadType == VDMC_REGISTERED) {  // 192
      vdmcRegisteredSEI(bitstream, sei);
    } else {
      reservedMessage(bitstream, sei);
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

// ISO/IEC 23002-7  Filler payload SEI message syntax
void
AtlasWriter::fillerPayload(vmesh::Bitstream& bitstream, SEI& sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto payloadSize = sei.getPayloadSize();
  for (size_t k = 0; k < payloadSize; k++) {
    WRITE_CODE(0xFF, 8);  // f(8) equal to 0xFF
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  User data registered by Recommendation ITU-T T.35 SEI message syntax
void
AtlasWriter::userDataRegisteredItuTT35(vmesh::Bitstream& bitstream,
                                       SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei         = static_cast<SEIUserDataRegisteredItuTT35&>(seiAbstract);
  auto  payloadSize = sei.getPayloadSize();
  WRITE_CODE(sei.getCountryCode(), 8);  // b(8)
  payloadSize--;
  if (sei.getCountryCode() == 0xFF) {
    WRITE_CODE(sei.getCountryCodeExtensionByte(), 8);  // b(8)
    payloadSize--;
  }
  auto& payload = sei.getPayloadByte();
  for (auto& element : payload) {
    WRITE_CODE(element, 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  User data unregistered SEI message syntax
void
AtlasWriter::userDataUnregistered(vmesh::Bitstream& bitstream,
                                  SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei         = static_cast<SEIUserDataUnregistered&>(seiAbstract);
  auto  payloadSize = sei.getPayloadSize();
  for (size_t i = 0; i < 16; i++) {
    WRITE_CODE(sei.getUuidIsoIec11578(i), 8);  // u(128) <=> 16 * u(8)
  }
  payloadSize -= 16;
  for (size_t i = 0; i < payloadSize; i++) {
    WRITE_CODE(sei.getUserDataPayloadByte(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.2  Recovery point SEI message syntax
void
AtlasWriter::recoveryPoint(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIRecoveryPoint&>(seiAbstract);
  WRITE_SVLC(sei.getRecoveryAfocCnt());    // se(v)
  WRITE_CODE(sei.getExactMatchFlag(), 1);  // u(1)
  WRITE_CODE(sei.getBrokenLinkFlag(), 1);  // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.3  No reconstruction SEI message syntax
void
AtlasWriter::noReconstruction(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  Reserved SEI message syntax
void
AtlasWriter::reservedMessage(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei         = static_cast<SEIReservedMessage&>(seiAbstract);
  auto  payloadSize = sei.getPayloadSize();
  for (size_t i = 0; i < payloadSize; i++) {
    WRITE_CODE(sei.getPayloadByte(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.4  SEI manifest SEI message syntax
void
AtlasWriter::seiManifest(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIManifest&>(seiAbstract);
  WRITE_CODE(sei.getNumSeiMsgTypes(), 16);  // u(16)
  for (size_t i = 0; i < sei.getNumSeiMsgTypes(); i++) {
    WRITE_CODE(sei.getSeiPayloadType(i), 16);  // u(16)
    WRITE_CODE(sei.getSeiDescription(i), 8);   // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.5  SEI prefix indication SEI message syntax
void
AtlasWriter::seiPrefixIndication(vmesh::Bitstream& bitstream,
                                 SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIPrefixIndication&>(seiAbstract);
  WRITE_CODE(sei.getPrefixSeiPayloadType(), 16);          // u(16)
  WRITE_CODE(sei.getNumSeiPrefixIndicationsMinus1(), 8);  // u(8)
  for (size_t i = 0; i <= sei.getNumSeiPrefixIndicationsMinus1(); i++) {
    WRITE_CODE(sei.getNumBitsInPrefixIndicationMinus1(i), 16);  // u(16)
    for (size_t j = 0; j <= sei.getNumBitsInPrefixIndicationMinus1(i); j++) {
      WRITE_CODE(sei.getSeiPrefixDataBit(i, j), 1);  // u(1)
    }
    while (!bitstream.byteAligned()) {
      WRITE_CODE(1, 1);  // f(1): equal to 1
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.6  Active substreams SEI message syntax
void
AtlasWriter::activeSubBitstreams(vmesh::Bitstream& bitstream,
                                 SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIActiveSubBitstreams&>(seiAbstract);
  WRITE_CODE(sei.getActiveSubBitstreamsCancelFlag(), 1);  // u(1)
  if (!sei.getActiveSubBitstreamsCancelFlag()) {
    WRITE_CODE(sei.getActiveAttributesChangesFlag(), 1);    // u(1)
    WRITE_CODE(sei.getActiveMapsChangesFlag(), 1);          // u(1)
    WRITE_CODE(sei.getAuxiliarySubstreamsActiveFlag(), 1);  // u(1)
    if (sei.getActiveAttributesChangesFlag()) {
      WRITE_CODE(sei.getAllAttributesActiveFlag(), 1);  // u(1)
      if (!sei.getAllAttributesActiveFlag()) {
        WRITE_CODE(sei.getActiveAttributeCountMinus1(), 7);  // u(7)
        for (size_t i = 0; i <= sei.getActiveAttributeCountMinus1(); i++) {
          WRITE_CODE(sei.getActiveAttributeIdx(i), 7);  // u(7)
        }
      }
    }
    if (sei.getActiveMapsChangesFlag()) {
      WRITE_CODE(sei.getAllMapsActiveFlag(), 1);  // u(1)
      if (!sei.getAllMapsActiveFlag()) {
        WRITE_CODE(sei.getActiveMapCountMinus1(), 4);  // u(4)
        for (size_t i = 0; i <= sei.getActiveMapCountMinus1(); i++) {
          WRITE_CODE(sei.getActiveMapIdx(i), 4);  // u(4)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.7  Component codec mapping SEI message syntax
void
AtlasWriter::componentCodecMapping(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIComponentCodecMapping&>(seiAbstract);
  WRITE_CODE(sei.getComponentCodecCancelFlag(), 1);  // u(1)
  if (!sei.getComponentCodecCancelFlag()) {
    WRITE_CODE(sei.getCodecMappingsCountMinus1(), 8);  // u(8)
    sei.allocate();
    for (size_t i = 0; i <= sei.getCodecMappingsCountMinus1(); i++) {
      WRITE_CODE(sei.getCodecId(i), 8);                  // u(8)
      WRITE_STRING(sei.getCodec4cc(sei.getCodecId(i)));  // st(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.8  Volumetric annotation SEI message family syntax
// ISO/IEC 23002-5:F.2.8.1 Scene object information SEI message syntax
void
AtlasWriter::sceneObjectInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISceneObjectInformation&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  WRITE_CODE(sei.getResetFlag(), 1);        // u(1)
  WRITE_UVLC(sei.getNumObjectUpdates());    // ue(v)
  if (sei.getNumObjectUpdates() > 0) {
    WRITE_CODE(sei.getSimpleObjectsFlag(), 1);  // u(1)
    if (static_cast<int>(sei.getSimpleObjectsFlag()) == 0) {
      WRITE_CODE(sei.getObjectLabelPresentFlag(), 1);       // u(1)
      WRITE_CODE(sei.getPriorityPresentFlag(), 1);          // u(1)
      WRITE_CODE(sei.getObjectHiddenPresentFlag(), 1);      // u(1)
      WRITE_CODE(sei.getObjectDependencyPresentFlag(), 1);  // u(1)
      WRITE_CODE(sei.getVisibilityConesPresentFlag(), 1);   // u(1)
      WRITE_CODE(sei.get3dBoundingBoxPresentFlag(), 1);     // u(1)
      WRITE_CODE(sei.getCollisionShapePresentFlag(), 1);    // u(1)
      WRITE_CODE(sei.getPointStylePresentFlag(), 1);        // u(1)
      WRITE_CODE(sei.getMaterialIdPresentFlag(), 1);        // u(1)
      WRITE_CODE(sei.getExtensionPresentFlag(), 1);         // u(1)
    }
    if (sei.get3dBoundingBoxPresentFlag()) {
      WRITE_CODE(sei.get3dBoundingBoxScaleLog2(), 5);        // u(5)
      WRITE_CODE(sei.get3dBoundingBoxPrecisionMinus8(), 5);  // u(5)
    }
    WRITE_CODE(sei.getLog2MaxObjectIdxUpdated(), 5);  // u(5)
    if (sei.getObjectDependencyPresentFlag()) {
      WRITE_CODE(sei.getLog2MaxObjectDependencyIdx(), 5);  // u(5)
    }
    for (size_t i = 0; i <= sei.getNumObjectUpdates(); i++) {
      assert(sei.getObjectIdx(i) >= sei.getNumObjectUpdates());
      WRITE_CODE(sei.getObjectIdx(i),
                 sei.getLog2MaxObjectIdxUpdated());  // u(v)
      size_t k = sei.getObjectIdx(i);
      WRITE_CODE(sei.getObjectCancelFlag(k), 1);  // u(1)
      if (sei.getObjectCancelFlag(k)) {
        if (sei.getObjectLabelPresentFlag()) {
          WRITE_CODE(sei.getObjectLabelUpdateFlag(k), 1);  // u(1)
          if (sei.getObjectLabelUpdateFlag(k)) {
            WRITE_UVLC(sei.getObjectLabelIdx(k));  // ue(v)
          }
        }
        if (sei.getPriorityPresentFlag()) {
          WRITE_CODE(sei.getPriorityUpdateFlag(k), 1);  // u(1)
          if (sei.getPriorityUpdateFlag(k)) {
            WRITE_CODE(sei.getPriorityValue(k), 4);  // u(4)
          }
        }
        if (sei.getObjectHiddenPresentFlag()) {
          WRITE_CODE(sei.getObjectHiddenFlag(k), 1);  // u(1)
        }
        if (sei.getObjectDependencyPresentFlag()) {
          WRITE_CODE(sei.getObjectDependencyUpdateFlag(k), 1);  // u(1)
          if (sei.getObjectDependencyUpdateFlag(k)) {
            WRITE_CODE(sei.getObjectNumDependencies(k), 4);  // u(4)
            size_t bitCount =
              ceil(log2(sei.getObjectNumDependencies(k)) + 0.5);
            for (size_t j = 0; j < sei.getObjectNumDependencies(k); j++) {
              WRITE_CODE(sei.getObjectDependencyIdx(k, j),
                         bitCount);  // u(v)
            }
          }
        }
        if (sei.getVisibilityConesPresentFlag()) {
          WRITE_CODE(sei.getVisibilityConesUpdateFlag(k), 1);  // u(1)
          if (sei.getVisibilityConesUpdateFlag(k)) {
            WRITE_CODE(sei.getDirectionX(k), 16);  // u(16)
            WRITE_CODE(sei.getDirectionY(k), 16);  // u(16)
            WRITE_CODE(sei.getDirectionZ(k), 16);  // u(16)
            WRITE_CODE(sei.getAngle(k), 16);       // u(16)
          }
        }  // cones

        if (sei.get3dBoundingBoxPresentFlag()) {
          WRITE_CODE(sei.get3dBoundingBoxUpdateFlag(k), 1);  // u(1)
          if (sei.get3dBoundingBoxUpdateFlag(k)) {
            WRITE_UVLC(sei.get3dBoundingBoxX(k));       // ue(v)
            WRITE_UVLC(sei.get3dBoundingBoxY(k));       // ue(v)
            WRITE_UVLC(sei.get3dBoundingBoxZ(k));       // ue(v)
            WRITE_UVLC(sei.get3dBoundingBoxDeltaX(k));  // ue(v)
            WRITE_UVLC(sei.get3dBoundingBoxDeltaY(k));  // ue(v)
            WRITE_UVLC(sei.get3dBoundingBoxDeltaZ(k));  // ue(v)
          }
        }  // 3dBB

        if (sei.getCollisionShapePresentFlag()) {
          WRITE_CODE(sei.getCollisionShapeUpdateFlag(k), 1);  // u(1)
          if (sei.getCollisionShapeUpdateFlag(k)) {
            WRITE_CODE(sei.getCollisionShapeId(k), 16);  // u(16)
          }
        }  // collision
        if (sei.getPointStylePresentFlag()) {
          WRITE_CODE(sei.getPointStyleUpdateFlag(k), 1);  // u(1)
          if (sei.getPointStyleUpdateFlag(k)) {
            WRITE_CODE(sei.getPointShapeId(k), 8);  // u(8)
            WRITE_CODE(sei.getPointSize(k), 16);    // u(16)
          }
        }  // pointstyle
        if (sei.getMaterialIdPresentFlag()) {
          WRITE_CODE(sei.getMaterialIdUpdateFlag(k), 1);  // u(1)
          if (sei.getMaterialIdUpdateFlag(k)) {
            WRITE_CODE(sei.getMaterialId(k), 16);  // u(16)
          }
        }  // materialid
      }    // sei.getObjectCancelFlag(k)
    }      // for(size_t i=0; i<=sei.getNumObjectUpdates(); i++)
  }        // if( sei.getNumObjectUpdates() > 0 )
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.8.2 Object label information SEI message syntax
void
AtlasWriter::objectLabelInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    sei  = static_cast<SEIObjectLabelInformation&>(seiAbstract);
  uint32_t zero = 0;
  WRITE_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    WRITE_CODE(sei.getLabelLanguagePresentFlag(), 1);  // u(1)
    if (sei.getLabelLanguagePresentFlag()) {
      while (!bitstream.byteAligned()) {
        WRITE_CODE(zero, 1);  // u(1)
      }
      WRITE_STRING(sei.getLabelLanguage());  // st(v)
    }
    WRITE_UVLC(sei.getNumLabelUpdates());  // ue(v)
    for (size_t i = 0; i < sei.getNumLabelUpdates(); i++) {
      WRITE_UVLC(sei.getLabelIdx(i));           // ue(v)
      WRITE_CODE(sei.getLabelCancelFlag(), 1);  // u(1)
      if (!sei.getLabelCancelFlag()) {
        while (!bitstream.byteAligned()) {
          WRITE_CODE(zero, 1);  // u(1)
        }
        WRITE_STRING(sei.getLabel(sei.getLabelIdx(i)));  // st(v)
      }
    }
    WRITE_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.8.3 Patch information SEI message syntax
void
AtlasWriter::patchInformation(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIPatchInformation&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  WRITE_CODE(sei.getResetFlag(), 1);        // u(1)
  WRITE_UVLC(sei.getNumTileUpdates());      // ue(v)
  if (sei.getNumTileUpdates() > 0) {
    WRITE_CODE(sei.getLog2MaxObjectIdxTracked(), 5);  // u(5)
    WRITE_CODE(sei.getLog2MaxPatchIdxUpdated(), 4);   // u(4)
  }
  for (size_t i = 0; i < sei.getNumTileUpdates(); i++) {
    WRITE_UVLC(sei.getTileId(i));  // ue(v)
    size_t j = sei.getTileId(i);
    WRITE_CODE(sei.getTileCancelFlag(j), 1);  // u(1)
    WRITE_UVLC(sei.getNumPatchUpdates(j));    // ue(v)
    for (size_t k = 0; k < sei.getNumPatchUpdates(j); k++) {
      WRITE_CODE(sei.getPatchIdx(j, k),
                 sei.getLog2MaxPatchIdxUpdated());  // u(v)
      auto p = sei.getPatchIdx(j, k);
      WRITE_CODE(sei.getPatchCancelFlag(j, p), 1);  // u(1)
      if (!sei.getPatchCancelFlag(j, p)) {
        WRITE_UVLC(sei.getPatchNumberOfObjectsMinus1(j, p));  // ue(v)
        for (size_t n = 0; n < sei.getPatchNumberOfObjectsMinus1(j, p) + 1;
             n++) {
          WRITE_CODE(sei.getPatchObjectIdx(j, p, n),
                     sei.getLog2MaxObjectIdxTracked());  // u(v)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
};

// ISO/IEC 23002-5:F.2.8.4 Volumetric rectangle information SEI message syntax
void
AtlasWriter::volumetricRectangleInformation(vmesh::Bitstream& bitstream,
                                            SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIVolumetricRectangleInformation&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);    // u(1)
  WRITE_CODE(sei.getResetFlag(), 1);          // u(1)
  WRITE_UVLC(sei.getNumRectanglesUpdates());  // ue(v)
  if (sei.getNumRectanglesUpdates() > 0) {
    WRITE_CODE(sei.getLog2MaxObjectIdxTracked(), 5);     // u(5)
    WRITE_CODE(sei.getLog2MaxRectangleIdxUpdated(), 4);  // u(4)
  }
  for (size_t k = 0; k < sei.getNumRectanglesUpdates(); k++) {
    WRITE_CODE(sei.getRectangleIdx(k),
               sei.getLog2MaxRectangleIdxUpdated());  // u(v)
    auto p = sei.getRectangleIdx(k);
    WRITE_CODE(sei.getRectangleCancelFlag(p), 1);  // u(1)
    if (!sei.getRectangleCancelFlag(p)) {
      WRITE_CODE(sei.getBoundingBoxUpdateFlag(p), 1);  // u(1)
      if (sei.getBoundingBoxUpdateFlag(p)) {
        WRITE_UVLC(sei.getBoundingBoxTop(p));     // ue(v)
        WRITE_UVLC(sei.getBoundingBoxLeft(p));    // ue(v)
        WRITE_UVLC(sei.getBoundingBoxWidth(p));   // ue(v)
        WRITE_UVLC(sei.getBoundingBoxHeight(p));  // ue(v)
      }
      WRITE_UVLC(sei.getRectangleNumberOfObjectsMinus1(p));  // ue(v)
      for (size_t n = 0; n < sei.getRectangleNumberOfObjectsMinus1(p) + 1;
           n++) {
        WRITE_CODE(sei.getRectangleObjectIdx(p, n),
                   sei.getLog2MaxObjectIdxTracked());  // u(v)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
};

// ISO/IEC 23002-5:F.2.8.5 Atlas object information  SEI message syntax
void
AtlasWriter::atlasObjectInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAtlasObjectInformation&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);   //	u(1)
  WRITE_CODE(sei.getResetFlag(), 1);         // 	u(1)
  WRITE_CODE(sei.getNumAtlasesMinus1(), 6);  // 	u(6)
  WRITE_UVLC(sei.getNumUpdates());           // ue(v)
  if (sei.getNumUpdates() > 0) {
    WRITE_CODE(sei.getLog2MaxObjectIdxTracked(), 5);  //	u(5)
    for (size_t i = 0; i < sei.getNumAtlasesMinus1() + 1; i++) {
      WRITE_CODE(sei.getAtlasId(i), 5);  // 	u(6)
    }
    for (size_t i = 0; i < sei.getNumUpdates() + 1; i++) {
      WRITE_CODE(sei.getObjectIdx(i),
                 sei.getLog2MaxObjectIdxTracked());  // u(v)
      for (size_t j = 0; j < sei.getNumAtlasesMinus1() + 1; j++) {
        WRITE_CODE(sei.getObjectInAtlasPresentFlag(i, j), 1);  // u(1)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.9  Buffering period SEI message syntax
void
AtlasWriter::bufferingPeriod(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIBufferingPeriod&>(seiAbstract);
  WRITE_CODE(sei.getNalHrdParamsPresentFlag(), 1);             // u(1)
  WRITE_CODE(sei.getAclHrdParamsPresentFlag(), 1);             // u(1)
  WRITE_CODE(sei.getInitialCabRemovalDelayLengthMinus1(), 5);  // u(5)
  WRITE_CODE(sei.getAuCabRemovalDelayLengthMinus1(), 5);       // u(5)
  WRITE_CODE(sei.getDabOutputDelayLengthMinus1(), 5);          // u(5)
  WRITE_CODE(sei.getIrapCabParamsPresentFlag(), 1);            // u(1)
  if (sei.getIrapCabParamsPresentFlag()) {
    WRITE_CODE(sei.getCabDelayOffset(),
               sei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
    WRITE_CODE(sei.getDabDelayOffset(),
               sei.getDabOutputDelayLengthMinus1() + 1);  // u(v)
  }
  WRITE_CODE(sei.getConcatenationFlag(), 1);  // u(1)
  WRITE_CODE(sei.getAtlasCabRemovalDelayDeltaMinus1(),
             sei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
  WRITE_CODE(sei.getMaxSubLayersMinus1(), 3);              // u(3)
  int32_t bitCount = sei.getInitialCabRemovalDelayLengthMinus1() + 1;
  for (size_t i = 0; i <= sei.getMaxSubLayersMinus1(); i++) {
    WRITE_CODE(sei.getHrdCabCntMinus1(i), 3);  // u(3)
    if (sei.getNalHrdParamsPresentFlag()) {
      for (size_t j = 0; j < sei.getHrdCabCntMinus1(i) + 1; j++) {
        WRITE_CODE(sei.getNalInitialCabRemovalDelay(i, j),
                   bitCount);  // u(v)
        WRITE_CODE(sei.getNalInitialCabRemovalOffset(i, j),
                   bitCount);  // u(v)
        if (sei.getIrapCabParamsPresentFlag()) {
          WRITE_CODE(sei.getNalInitialAltCabRemovalDelay(i, j),
                     bitCount);  // u(v)
          WRITE_CODE(sei.getNalInitialAltCabRemovalOffset(i, j),
                     bitCount);  // u(v)
        }
      }
    }
    if (sei.getAclHrdParamsPresentFlag()) {
      for (size_t j = 0; j < sei.getHrdCabCntMinus1(i) + 1; j++) {
        WRITE_CODE(sei.getAclInitialCabRemovalDelay(i, j),
                   bitCount);  // u(v)
        WRITE_CODE(sei.getAclInitialCabRemovalOffset(i, j),
                   bitCount);  // u(v)
        if (sei.getIrapCabParamsPresentFlag()) {
          WRITE_CODE(sei.getAclInitialAltCabRemovalDelay(i, j),
                     bitCount);  // u(v)
          WRITE_CODE(sei.getAclInitialAltCabRemovalOffset(i, j),
                     bitCount);  // u(v)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.10  Atlas frame timing SEI message syntax
void
AtlasWriter::atlasFrameTiming(vmesh::Bitstream& bitstream,
                              SEI&              seiAbstract,
                              SEI&              seiBufferingPeriodAbstract,
                              bool              cabDabDelaysPresentFlag) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei   = static_cast<SEIAtlasFrameTiming&>(seiAbstract);
  auto& bpsei = static_cast<SEIBufferingPeriod&>(seiBufferingPeriodAbstract);
  if (cabDabDelaysPresentFlag) {
    for (uint32_t i = 0; i <= bpsei.getMaxSubLayersMinus1(); i++) {
      WRITE_CODE(sei.getAftCabRemovalDelayMinus1(i),
                 bpsei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
      WRITE_CODE(sei.getAftDabOutputDelay(i),
                 bpsei.getDabOutputDelayLengthMinus1() + 1);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.11	Viewport SEI messages family syntax
// ISO/IEC 23002-5:F.2.11.1	Viewport camera parameters SEI messages syntax
void
AtlasWriter::viewportCameraParameters(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIViewportCameraParameters&>(seiAbstract);
  WRITE_CODE(sei.getCameraId(), 10);   // u(10)
  WRITE_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (sei.getCameraId() > 0 && !sei.getCancelFlag()) {
    WRITE_CODE(sei.getPersistenceFlag(), 1);              // u(1)
    WRITE_CODE(sei.getCameraType(), 3);                   // u(3)
    if (sei.getCameraType() == 0) {                       // equirectangular
      WRITE_CODE(sei.getErpHorizontalFov(), 32);          // u(32)
      WRITE_CODE(sei.getErpVerticalFov(), 32);            // u(32)
    } else if (sei.getCameraType() == 1) {                // perspective
      WRITE_FLOAT(sei.getPerspectiveAspectRatio());       // fl(32)
      WRITE_CODE(sei.getPerspectiveHorizontalFov(), 32);  // u(32)
    } else if (sei.getCameraType() == 2) {                // orthographic
      WRITE_FLOAT(sei.getOrthoAspectRatio());             // fl(32)
      WRITE_FLOAT(sei.getOrthoHorizontalSize());          // fl(32)
    }
    WRITE_FLOAT(sei.getClippingNearPlane());  // fl(32)
    WRITE_FLOAT(sei.getClippingFarPlane());   // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.11.2	Viewport position SEI messages syntax
void
AtlasWriter::viewportPosition(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIViewportPosition&>(seiAbstract);
  WRITE_UVLC(sei.getViewportId());                      // ue(v
  WRITE_CODE(sei.getCameraParametersPresentFlag(), 1);  // u(1)
  if (sei.getCameraParametersPresentFlag()) {
    WRITE_CODE(sei.getViewportId(), 10);  //	u(10)
  }
  WRITE_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    WRITE_CODE(sei.getPersistenceFlag(), 1);  // u(1)
    for (size_t d = 0; d < 3; d++) {
      WRITE_FLOAT(sei.getPosition(d));  //	fl(32)
    }
    WRITE_CODE(sei.getRotationQX(), 16);     //	i(16)
    WRITE_CODE(sei.getRotationQY(), 16);     //	i(16)
    WRITE_CODE(sei.getRotationQZ(), 16);     //	i(16)
    WRITE_CODE(sei.getCenterViewFlag(), 1);  // 	u(1)
    if (!sei.getCenterViewFlag()) {
      WRITE_CODE(sei.getLeftViewFlag(), 1);  // u(1)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12 Decoded Atlas Information Hash SEI message syntax
void
AtlasWriter::decodedAtlasInformationHash(vmesh::Bitstream& bitstream,
                                         SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIDecodedAtlasInformationHash&>(seiAbstract);
  WRITE_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    WRITE_CODE(sei.getPersistenceFlag(), 1);                      // u(1)
    WRITE_CODE(sei.getHashType(), 8);                             // u(8)
    WRITE_CODE(sei.getDecodedHighLevelHashPresentFlag(), 1);      // u(1)
    WRITE_CODE(sei.getDecodedAtlasHashPresentFlag(), 1);          // u(1)
    WRITE_CODE(sei.getDecodedAtlasB2pHashPresentFlag(), 1);       // u(1)
    WRITE_CODE(sei.getDecodedAtlasTilesHashPresentFlag(), 1);     // u(1)
    WRITE_CODE(sei.getDecodedAtlasTilesB2pHashPresentFlag(), 1);  // u(1)
    WRITE_CODE(0, 1);                                             // u(1)
    if (sei.getDecodedHighLevelHashPresentFlag()) {
      decodedHighLevelHash(bitstream, sei);
    }
    if (sei.getDecodedAtlasHashPresentFlag()) {
      decodedAtlasHash(bitstream, sei);
    }
    if (sei.getDecodedAtlasB2pHashPresentFlag()) {
      decodedAtlasB2pHash(bitstream, sei);
    }
    if (sei.getDecodedAtlasTilesHashPresentFlag()
        || sei.getDecodedAtlasTilesB2pHashPresentFlag()) {
      WRITE_UVLC(sei.getNumTilesMinus1());   // ue(v)
      WRITE_UVLC(sei.getTileIdLenMinus1());  // ue(v)
      for (size_t t = 0; t <= sei.getNumTilesMinus1(); t++) {
        WRITE_CODE(sei.getTileId(t),
                   sei.getTileIdLenMinus1() + 1);  // u(v)
      }
      byteAlignment(bitstream);
      for (size_t t = 0; t <= sei.getNumTilesMinus1(); t++) {
        size_t j = sei.getTileId(t);
        if (sei.getDecodedAtlasTilesHashPresentFlag()) {
          decodedAtlasTilesHash(bitstream, sei, j);
        }
        if (sei.getDecodedAtlasTilesB2pHashPresentFlag()) {
          decodedAtlasTilesB2pHash(bitstream, sei, j);
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.1 Decoded high level hash unit syntax
void
AtlasWriter::decodedHighLevelHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      WRITE_CODE(sei.getHighLevelMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    WRITE_CODE(sei.getHighLevelCrc(), 16);  // u(16)
  } else if (hType == 2) {
    WRITE_CODE(sei.getHighLevelCheckSum(), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.2 Decoded atlas hash unit syntax
void
AtlasWriter::decodedAtlasHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      WRITE_CODE(sei.getAtlasMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    WRITE_CODE(sei.getAtlasCrc(), 16);  // u(16)
  } else if (hType == 2) {
    WRITE_CODE(sei.getAtlasCheckSum(), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.3 Decoded atlas b2p hash unit syntax
void
AtlasWriter::decodedAtlasB2pHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      WRITE_CODE(sei.getAtlasB2pMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    WRITE_CODE(sei.getAtlasB2pCrc(), 16);  // u(16)
  } else if (hType == 2) {
    WRITE_CODE(sei.getAtlasB2pCheckSum(), 32);  // u(32)
  }
}

// ISO/IEC 23002-5:F.2.12.4 Decoded atlas tile hash unit syntax
void
AtlasWriter::decodedAtlasTilesHash(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbs,
                                   size_t            id) {
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      WRITE_CODE(sei.getAtlasTilesMd5(id, i), 8);  // b(8)
    }
  } else if (hType == 1) {
    WRITE_CODE(sei.getAtlasTilesCrc(id), 16);  // u(16)
  } else if (hType == 2) {
    WRITE_CODE(sei.getAtlasTilesCheckSum(id), 32);  // u(32)
  }
}

// ISO/IEC 23002-5:F.2.12.5 Decoded atlas tile b2p hash unit syntax
void
AtlasWriter::decodedAtlasTilesB2pHash(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbs,
                                      size_t            id) {
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      WRITE_CODE(sei.getAtlasTilesB2pMd5(id, i), 8);  // b(8)
    }
  } else if (hType == 1) {
    WRITE_CODE(sei.getAtlasTilesB2pCrc(id), 16);  // u(16)
  } else if (hType == 2) {
    WRITE_CODE(sei.getAtlasTilesB2pCheckSum(id), 32);  // u(32)
  }
}

// ISO/IEC 23002-5:F.2.13 Time code SEI message syntax
void
AtlasWriter::timeCode(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  auto& sei = static_cast<SEITimeCode&>(seiAbstract);
  WRITE_CODE(sei.getNumUnitsInTick(), 32);    // u(32)
  WRITE_CODE(sei.getTimeScale(), 32);         // u(32)
  WRITE_CODE(sei.getCountingType(), 5);       // u(5)
  WRITE_CODE(sei.getFullTimestampFlag(), 1);  // u(1)
  WRITE_CODE(sei.getDiscontinuityFlag(), 1);  // u(1)
  WRITE_CODE(sei.getCntDroppedFlag(), 1);     // u(1)
  WRITE_CODE(sei.getNFrames(), 9);            // u(9)
  if (sei.getFullTimestampFlag()) {
    WRITE_CODE(sei.getSecondsValue(), 6);  // u(6)
    WRITE_CODE(sei.getMinutesValue(), 6);  // u(6)
    WRITE_CODE(sei.getHoursValue(), 5);    // u(5)
  } else {
    WRITE_CODE(sei.getSecondFlag(), 1);  // u(1)
    if (sei.getSecondFlag()) {
      WRITE_CODE(sei.getSecondsValue(), 6);  // u(6)
      WRITE_CODE(sei.getMinutesFlag(), 1);   // u(1)
      if (sei.getMinutesFlag()) {
        WRITE_CODE(sei.getMinutesValue(), 6);  // u(6)
        WRITE_CODE(sei.getHoursFlag(), 1);     // u(1)
        if (sei.getHoursFlag()) {
          WRITE_CODE(sei.getHoursValue(), 5);  // u(5)
        }
      }
    }
  }
  WRITE_CODE(sei.getTimeOffsetLength(), 5);  // u(5)
  if (sei.getTimeOffsetLength() > 0) {
    WRITE_CODES(sei.getTimeOffsetValue(),
                sei.getTimeOffsetLength());  // i(v)
  }
}

// ISO/IEC 23002-5:H.20.2.13 Attribute transformation parameters SEI message syntax
void
AtlasWriter::attributeTransformationParams(vmesh::Bitstream& bitstream,
                                           SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeTransformationParams&>(seiAbstract);
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

// ISO/IEC 23002-5:H.20.2.14 Occupancy synthesis SEI message syntax
void
AtlasWriter::occupancySynthesis(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIOccupancySynthesis&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);   //	u(1)
  WRITE_CODE(sei.getResetFlag(), 1);         //	u(1)
  WRITE_CODE(sei.getInstancesUpdated(), 8);  //	u(8)
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    WRITE_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    WRITE_CODE(sei.getInstanceCancelFlag(k), 1);  //	u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      WRITE_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        WRITE_CODE(sei.getPbfLog2ThresholdMinus1(k), 2);  //	u(2)
        WRITE_CODE(sei.getPbfPassesCountMinus1(k), 2);    //	u(2)
        WRITE_CODE(sei.getPbfFilterSizeMinus1(k), 3);     //	u(3)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.15 Geometry smoothing SEI message syntax
void
AtlasWriter::geometrySmoothing(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIGeometrySmoothing&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);   //	u(1)
  WRITE_CODE(sei.getResetFlag(), 1);         //	u(1)
  WRITE_CODE(sei.getInstancesUpdated(), 8);  //	u(8)
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    WRITE_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    WRITE_CODE(sei.getInstanceCancelFlag(k), 1);  //	u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      WRITE_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        WRITE_CODE(sei.getFilterEomPointsFlag(k), 1);  // u(1)
        WRITE_CODE(sei.getGridSizeMinus2(k), 7);       // u(7)
        WRITE_CODE(sei.getThreshold(k), 8);            // u(8)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.20.2.20 Attribute smoothing SEI message syntax
void
AtlasWriter::attributeSmoothing(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeSmoothing&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);    //	u(1)
  WRITE_CODE(sei.getResetFlag(), 1);          //	u(1)
  WRITE_UVLC(sei.getNumAttributesUpdated());  //	ue(v)
  for (size_t j = 0; j < sei.getNumAttributesUpdated(); j++) {
    WRITE_CODE(sei.getAttributeIdx(j), 7);  // u(7)
    size_t k = sei.getAttributeIdx(j);
    WRITE_CODE(sei.getAttributeSmoothingCancelFlag(k), 1);  // u(1)
    WRITE_CODE(sei.getInstancesUpdated(k), 8);              //	u(8)
    for (size_t i = 0; i < sei.getInstancesUpdated(k); i++) {
      WRITE_CODE(sei.getInstanceIndex(k, i), 8);  //	u(8)
      size_t m = sei.getInstanceIndex(k, i);
      WRITE_CODE(sei.getInstanceCancelFlag(k, m), 1);  // u(1)
      if (sei.getInstanceCancelFlag(k, m) != 1) {
        WRITE_UVLC(sei.getMethodType(k, m));  // ue(v)
        if (sei.getMethodType(k, m)) {
          WRITE_CODE(sei.getFilterEomPointsFlag(k, m), 1);  // u(1)
          WRITE_CODE(sei.getGridSizeMinus2(k, m), 5);       //	u(5)
          WRITE_CODE(sei.getThreshold(k, m), 8);            // u(8)
          WRITE_CODE(sei.getThresholdVariation(k, m), 8);   // u(8)
          WRITE_CODE(sei.getThresholdDifference(k, m), 8);  // u(8)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.17 V-PCC registered SEI message syntax
void
AtlasWriter::vpccRegisteredSEI(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  auto& sei         = static_cast<SEIVPCCRegistered&>(seiAbstract);
  auto  payloadType = static_cast<int32_t>(sei.getVpccPayloadType());
  for (; payloadType >= 0xff; payloadType -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadType, 8);  // u(8)
  vpccRegisteredSeiPayload(
    bitstream, static_cast<SeiPayloadType>(payloadType), seiAbstract);
}

// ISO/IEC 23002-5:H.20.2.18 V-PCC registered SEI payload syntax
void
AtlasWriter::vpccRegisteredSeiPayload(vmesh::Bitstream& bitstream,
                                      SeiPayloadType    vpccPayloadType,
                                      SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  reservedMessage(bitstream, seiAbstract);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.1 V-DMC registered SEI message syntax
void
AtlasWriter::vdmcRegisteredSEI(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  auto& sei         = static_cast<SEIVDMCRegistered&>(seiAbstract);
  auto  payloadType = static_cast<int32_t>(sei.getVdmcPayloadType());
  for (; payloadType >= 0xff; payloadType -= 0xff) {
    WRITE_CODE(0xff, 8);  // u(8)
  }
  WRITE_CODE(payloadType, 8);  // u(8)
  vdmcRegisteredSeiPayload(
    bitstream, static_cast<VdmcSeiPayloadType>(payloadType), seiAbstract);
}

// F.2.2 V-DMC registered SEI payload syntax
void
AtlasWriter::vdmcRegisteredSeiPayload(vmesh::Bitstream&  bitstream,
                                      VdmcSeiPayloadType vpccPayloadType,
                                      SEI&               seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  if (vpccPayloadType == ZIPPERING) {  // 0
    zippering(bitstream, seiAbstract);
  } else if (vpccPayloadType == SUBMESH_SOI_RELATIONSHIP_INDICATION) {  // 1
    submeshSOIIndicationRelationship(bitstream, seiAbstract);
  } else if (vpccPayloadType == SUBMESH_DISTORTION_INDICATION) {  // 2
    submeshDistortionIndication(bitstream, seiAbstract);
  } else if (vpccPayloadType == LOD_EXTRACTION_INFORMATION) {  // 3
    LoDExtractionInformation(bitstream, seiAbstract);
  } else if (vpccPayloadType == TILE_SUBMESH_MAPPING) {  // 4
    tileSubmeshMapping(bitstream, seiAbstract);
  } else if (vpccPayloadType == ATTRIBUTE_EXTRACTION_INFORMATION) {  // 5
    AttributeExtractionInformation(bitstream, seiAbstract);
  } else {
    reservedMessage(bitstream, seiAbstract);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.3 Zippering SEI message syntax
void
AtlasWriter::zippering(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIZippering&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceFlag(), 1);   // u(1)
  WRITE_CODE(sei.getResetFlag(), 1);         // u(1)
  WRITE_CODE(sei.getInstancesUpdated(), 8);  // u(8)
  sei.allocate();
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    WRITE_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    WRITE_CODE(sei.getInstanceCancelFlag(k), 1);  // u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      WRITE_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        WRITE_UVLC(sei.getZipperingMaxMatchDistance(k));  // ue(v)
        if (sei.getZipperingMaxMatchDistance(k) != 0) {
          WRITE_CODE(sei.getZipperingSendDistancePerSubmesh(k), 1);  // u(1)
          if (sei.getZipperingSendDistancePerSubmesh(k)) {
            WRITE_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
            auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
            auto bitCountMaxDistance =
              ceilLog2(sei.getZipperingMaxMatchDistance(k)
                       + 1);  // distance=[0,MaxDistance], MaxDistance+1
            for (int p = 0; p < numSubmeshes; p++) {
              WRITE_CODE(sei.getZipperingMaxMatchDistancePerSubmesh(k, p),
                         bitCountMaxDistance);  // u(v)
              if (sei.getZipperingMaxMatchDistancePerSubmesh(k, p) != 0) {
                WRITE_CODE(sei.getZipperingSendDistancePerBorderPoint(k, p),
                           1);  // u(1)
                if (sei.getZipperingSendDistancePerBorderPoint(k, p)) {
                  WRITE_UVLC(
                    sei.getZipperingNumberOfBorderPoints(k, p));  // ue(v)
                  auto numBorderPoints =
                    sei.getZipperingNumberOfBorderPoints(k, p);
                  auto bitCountMaxDistancePerSubmesh =
                    ceilLog2(sei.getZipperingMaxMatchDistancePerSubmesh(k, p)
                             + 1);  // distance=[0,MaxDistance], MaxDistance+1
                  for (int b = 0; b < numBorderPoints; b++) {
                    WRITE_CODE(sei.getZipperingDistancePerBorderPoint(k, p, b),
                               bitCountMaxDistancePerSubmesh);  // u(v)
                  }
                }
              }
            }
          } else {
            WRITE_CODE(sei.getZipperingSendDistancePerSubmeshPair(k),
                       1);  // u(1)
            if (sei.getZipperingSendDistancePerSubmeshPair(k)) {
              WRITE_CODE(sei.getZipperingLinearSegmentation(k), 1);
              WRITE_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k),
                         6);  // u(6)
              auto numSubmeshes =
                sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
              auto bitCountMaxDistance =
                ceilLog2(sei.getZipperingMaxMatchDistance(k) + 1);
              for (int p = 0; p < numSubmeshes - 1; p++) {
                if (sei.getZipperingLinearSegmentation(k)) {
                  WRITE_CODE(
                    sei.getZipperingMaxMatchDistancePerSubmeshPair(k, p, 0),
                    bitCountMaxDistance);
                } else {
                  for (int t = p + 1; t < numSubmeshes; t++) {
                    WRITE_CODE(sei.getZipperingMaxMatchDistancePerSubmeshPair(
                                 k, p, t - p - 1),
                               bitCountMaxDistance);
                  }
                }
              }
            }
          }
        }
      }
      if (sei.getMethodType(k) == 2) {
        WRITE_UVLC(sei.getMethodForUnmatchedLoDs(k));               // ue(v)
        WRITE_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
        WRITE_CODE(sei.getZipperingDeltaFlag(k), 1);                // u(1)
        auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
        auto useDelta     = static_cast<bool>(sei.getZipperingDeltaFlag(k));
        auto maxNumBitsSubmesh =
          ceilLog2(numSubmeshes
                   + 1);  // submeshIdx=[0,submeshCount-1,submeshCount(skip)]
        for (int p = 0; p < numSubmeshes; p++) {
          WRITE_UVLC(sei.getZipperingNumberOfBorderPoints(k, p));  // ue(v)
        }
        for (int p = 0; p < numSubmeshes; p++) {
          auto numBorderPoints = sei.getZipperingNumberOfBorderPoints(k, p);
          for (int b = 0; b < numBorderPoints; b++) {
            if (!sei.getZipperingBorderPointMatchIndexFlag(k, p, b)) {
              if (useDelta) {
                auto submeshIdx =
                  sei.getZipperingBorderPointMatchSubmeshIndex(k, p, b);
                auto borderPointIdx =
                  sei.getZipperingBorderPointMatchBorderIndex(k, p, b);
                auto submeshIdxDelta =
                  sei.getZipperingBorderPointMatchSubmeshIndexDelta(k, p, b);
                WRITE_SVLC(submeshIdxDelta);  // se(v)
                if (submeshIdx != numSubmeshes) {
                  auto borderPointIdxDelta =
                    sei.getZipperingBorderPointMatchBorderIndexDelta(k, p, b);
                  WRITE_SVLC(borderPointIdxDelta);  // se(v)
                  if (submeshIdx > p)
                    sei.getZipperingBorderPointMatchIndexFlag(
                      k, submeshIdx, borderPointIdx) = true;
                }
              } else {
                auto submeshIdx =
                  sei.getZipperingBorderPointMatchSubmeshIndex(k, p, b);
                auto borderPointIdx =
                  sei.getZipperingBorderPointMatchBorderIndex(k, p, b);
                WRITE_CODE(submeshIdx, maxNumBitsSubmesh);  // u(v)
                if (submeshIdx != numSubmeshes) {
                  size_t maxNumBitsBorderIdx =
                    ceilLog2(sei.getZipperingNumberOfBorderPoints(
                      k, submeshIdx));  //borderIdx=[0,numBorderIdx-1]
                  WRITE_CODE(borderPointIdx, maxNumBitsBorderIdx);  // u(v)
                  if (submeshIdx > p)
                    sei.getZipperingBorderPointMatchIndexFlag(
                      k, submeshIdx, borderPointIdx) = true;
                }
              }
            }
          }
        }
      }
      if (sei.getMethodType(k) == 3) {
        WRITE_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
        auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
        for (int p = 0; p < numSubmeshes; p++) {
          WRITE_UVLC(sei.getcrackCountPerSubmesh(k, p));
          auto numCrackCount = sei.getcrackCountPerSubmesh(k, p);
          for (int c = 0; c < numCrackCount; c++) {
            for (int b = 0; b < 3; b++) {
              WRITE_UVLC(sei.getboundaryIndex(k, p, c, b));
            }
          }
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.4 Submesh SOI relationship indication SEI message syntax
void
AtlasWriter::submeshSOIIndicationRelationship(vmesh::Bitstream& bitstream,
                                              SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISubmeshSOIIndicationRelationship&>(seiAbstract);
  WRITE_CODE(sei.getPersistenceAssociationFlag(), 1);
  WRITE_UVLC(sei.getNumberOfActiveSceneObjects());
  WRITE_UVLC(sei.getSubmeshIdLengthMinus1());
  uint32_t l = CeilLog2(sei.getNumberOfActiveSceneObjects());
  for (int i = 0; i < sei.getNumberOfActiveSceneObjects(); i++) {
      WRITE_CODE(sei.getSoiObjectIdx(i), l);
      WRITE_UVLC(sei.getNumberOfSubmeshIncluded(i));
      for (int j = 0; j < sei.getNumberOfSubmeshIncluded(i); j++) {
          WRITE_CODE(sei.getSubmeshId(i, j), sei.getSubmeshIdLengthMinus1() + 1);
          WRITE_CODE(sei.getCompletelyIncluded(i, j), 1);
      }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.5 Submesh distortion indication SEI message syntax
void
AtlasWriter::submeshDistortionIndication(vmesh::Bitstream& bitstream,
                                         SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISubmeshDistortionIndication&>(seiAbstract);
  WRITE_UVLC(sei.getNumberOfSubmeshIndicatedMinus1());
  WRITE_UVLC(sei.getSubmeshIdLengthMinus1());
  for (int i = 0; i < sei.getNumberOfSubmeshIndicatedMinus1() + 1; i++) {
      WRITE_CODE(sei.getSubmeshId(i), sei.getSubmeshIdLengthMinus1() + 1);
      WRITE_UVLC(sei.getNumberOfVerticesOfOriginalSubmesh(i));
      WRITE_CODE(sei.getSubdivisionIterationCount(i), 3);
      WRITE_UVLC(sei.getNumberOfDistortionIndicatedMinus1(i));
      for (int j = 0; j < sei.getNumberOfDistortionIndicatedMinus1(i) + 1; j++) {
          WRITE_CODE(sei.getDistortionMetricsType(i, j), 8);
          for (int k = 0; k < sei.getSubdivisionIterationCount(i); k++)
              WRITE_UVLC(sei.getDistortion(i, j, k));
      }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.6 LoD extraction information SEI message syntax
void
AtlasWriter::LoDExtractionInformation(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEILoDExtractionInformation&>(seiAbstract);
  WRITE_CODE(sei.getExtractableUnitTypeIdx(),
             2);  // extractable unit type 0:MCTS 1:subpicture
  WRITE_CODE(sei.getNumSubmeshCount(), 6);  // submeshcount u(6)
  sei.allocate();
  for (int submeshIdx = 0; submeshIdx <= sei.getNumSubmeshCount();
       submeshIdx++) {
    WRITE_UVLC(sei.getSubmeshId(submeshIdx));  // submeshId ue(v)
    WRITE_CODE(sei.getSubmeshSubdivisionIterationCount(submeshIdx), 3);
    sei.allocateSubdivisionIteractionCountPerSubmesh(
      submeshIdx, sei.getSubmeshSubdivisionIterationCount(submeshIdx));
    for (int32_t i = 0;
         i <= sei.getSubmeshSubdivisionIterationCount(submeshIdx);
         i++) {
      if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
        WRITE_UVLC(sei.getMCTSIdx(submeshIdx, i));
      } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
        WRITE_UVLC(sei.getsubpictureIdx(submeshIdx, i));
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// F.2.7 Tile submesh mapping SEI message syntax
void
AtlasWriter::tileSubmeshMapping(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei          = static_cast<SEITileSubmeshMapping&>(seiAbstract);
  uint8_t currCount[2] = {0, 0};
  WRITE_CODE(sei.getPersistenceMappingFlag(), 1);
  WRITE_UVLC(sei.getNumberTilesMinus1());
  WRITE_UVLC(sei.getTileIdLengthMinus1());
  WRITE_CODE(sei.getCodecTileSignalFlag(), 1);
  if (sei.getCodecTileSignalFlag()) {
    WRITE_CODE(sei.getGeoCodecTileAlignmentFlag(), 1);
    WRITE_CODE(sei.getAttrCodecTileAlignmentFlag(), 1);
    currCount[0] = 0;
    currCount[1] = 0;
  }
  for (int i = 0; i < sei.getNumberTilesMinus1() + 1; i++) {
    WRITE_CODE(sei.getTileId(i), sei.getTileIdLengthMinus1() + 1);
    WRITE_CODE(sei.getTileTypeFlag(i), 1);
    int numSubmeshes = 0;
    WRITE_CODE(sei.getNumSubmeshesMinus1(i), 6);
    numSubmeshes = sei.getNumSubmeshesMinus1(i) + 1;
    WRITE_UVLC(sei.getSubmeshIdLengthMinus1(i));
    for (int j = 0; j < numSubmeshes; j++) {
      WRITE_CODE(sei.getSubmeshId(i, j), sei.getSubmeshIdLengthMinus1(i) + 1);
    }
    if (sei.getCodecTileSignalFlag()) {
      if ((sei.getTileTypeFlag(i) == 0 && !sei.getGeoCodecTileAlignmentFlag())
          || sei.getTileTypeFlag(i) == 1
               && !sei.getAttrCodecTileAlignmentFlag()) {
        WRITE_UVLC(sei.getNumCodecTilesInTileMinus1(i));
        for (int j = 0; j < sei.getNumCodecTilesInTileMinus1(i) + 1; j++) {
          WRITE_UVLC(sei.getCodecTileIdx(i, j));
        }
      }
      if (sei.getGeoCodecTileAlignmentFlag()
          || sei.getAttrCodecTileAlignmentFlag()) {
        if (sei.getTileTypeFlag(i) == 1 && currCount[1] != 0) {
          WRITE_CODE(sei.getCodecTileIdxResetFlag(i), 1);
          if (sei.getCodecTileIdxResetFlag(i)) { currCount[1] = 0; }
        }
        sei.setCodecTileIdx(i, 0, currCount[sei.getTileTypeFlag(i)]);
        currCount[sei.getTileTypeFlag(i)] += 1;
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// F.2.8 Attribute extraction information SEI message syntax
void
AtlasWriter::AttributeExtractionInformation(vmesh::Bitstream& bitstream,
                                            SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeExtractionInformation&>(seiAbstract);
  WRITE_CODE(sei.getCancelFlag(), 1);  // cancel flag u(1)
  if (!sei.getCancelFlag()) {
    WRITE_CODE(sei.getExtractableUnitTypeIdx(),
               2);  // extractable unit type 0:MCTS 1:subpicture
    WRITE_CODE(sei.getNumSubmeshCount(), 6);  // submeshcount u(6)
    WRITE_CODE(sei.getAttributeCount(), 7);   // attributecount u(7)
    sei.allocate();
    for (int submeshIdx = 0; submeshIdx <= sei.getNumSubmeshCount();
         submeshIdx++) {
      WRITE_UVLC(sei.getSubmeshId(submeshIdx));  // submeshId ue(v)
      for (int32_t attributeIdx = 0; attributeIdx < sei.getAttributeCount();
           attributeIdx++) {
        WRITE_CODE(sei.getExtractionInfoPresentFlag(attributeIdx),
                   1);  // extraction infomation present flag u(1)
        if (sei.getExtractionInfoPresentFlag(attributeIdx)) {
          if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
            WRITE_UVLC(sei.getMCTSIdx(submeshIdx, attributeIdx));
          } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
            WRITE_UVLC(sei.getsubpictureIdx(submeshIdx, attributeIdx));
          }
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2 VUI syntax
// G.2.1 VUI parameters syntax
void
AtlasWriter::vuiParameters(vmesh::Bitstream& bitstream, VUIParameters& vp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
  if (vp.getTimingInfoPresentFlag()) {
    WRITE_CODE(vp.getNumUnitsInTick(), 32);              // u(32)
    WRITE_CODE(vp.getTimeScale(), 32);                   // u(32)
    WRITE_CODE(vp.getPocProportionalToTimingFlag(), 1);  // u(1)
    if (vp.getPocProportionalToTimingFlag()) {
      WRITE_UVLC(vp.getNumTicksPocDiffOneMinus1());  // ue(v)
    }
    WRITE_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
    if (vp.getHrdParametersPresentFlag()) {
      hrdParameters(bitstream, vp.getHrdParameters());
    }
  }
  WRITE_CODE(vp.getTileRestrictionsPresentFlag(), 1);  // u(1)
  if (vp.getTileRestrictionsPresentFlag()) {
    WRITE_CODE(vp.getFixedAtlasTileStructureFlag(), 1);          // u(1)
    WRITE_CODE(vp.getFixedVideoTileStructureFlag(), 1);          //	u(1)
    WRITE_UVLC(vp.getConstrainedTilesAcrossV3cComponentsIdc());  // ue(v)
    WRITE_UVLC(vp.getMaxNumTilesPerAtlasMinus1());               // 	ue(v)
  }
  WRITE_CODE(vp.getMaxCodedVideoResolutionPresentFlag(), 1);  // u(1)
  if (vp.getMaxCodedVideoResolutionPresentFlag()) {
    maxCodedVideoResolution(bitstream, vp.getMaxCodedVideoResolution());
  }
  WRITE_CODE(vp.getCoordinateSystemParametersPresentFlag(), 1);  // u(1)
  if (vp.getCoordinateSystemParametersPresentFlag()) {
    coordinateSystemParameters(bitstream, vp.getCoordinateSystemParameters());
  }
  WRITE_CODE(vp.getUnitInMetresFlag(), 1);           // u(1)
  WRITE_CODE(vp.getDisplayBoxInfoPresentFlag(), 1);  // u(1)
  if (vp.getDisplayBoxInfoPresentFlag()) {
    for (size_t d = 0; d < 3; d++) {
      WRITE_UVLC(vp.getDisplayBoxOrigin(d));  // ue(v)
      WRITE_UVLC(vp.getDisplayBoxSize(d));    // ue(v)
    }
    WRITE_CODE(vp.getAnchorPointPresentFlag(), 1);  // u(1)
    if (vp.getAnchorPointPresentFlag()) {
      for (size_t d = 0; d < 3; d++) {
        WRITE_UVLC(vp.getAnchorPoint(d));  // u(v)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.2  HRD parameters syntax
void
AtlasWriter::hrdParameters(vmesh::Bitstream& bitstream, HrdParameters& hp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(hp.getNalParametersPresentFlag(), 1);  // u(1)
  WRITE_CODE(hp.getAclParametersPresentFlag(), 1);  // u(1)
  if (hp.getNalParametersPresentFlag() || hp.getAclParametersPresentFlag()) {
    WRITE_CODE(hp.getBitRateScale(), 4);  // u(4)
    WRITE_CODE(hp.getCabSizeScale(), 4);  // u(4)
  }
  for (size_t i = 0; i <= hp.getMaxNumSubLayersMinus1(); i++) {
    WRITE_CODE(hp.getFixedAtlasRateGeneralFlag(i), 1);  // u(1)
    if (!hp.getFixedAtlasRateGeneralFlag(i)) {
      WRITE_CODE(hp.getFixedAtlasRateWithinCasFlag(i), 1);  // u(1)
    }
    if (hp.getFixedAtlasRateWithinCasFlag(i)) {
      WRITE_CODE(hp.getElementalDurationInTcMinus1(i), 1);  // ue(v)
    } else {
      WRITE_CODE(hp.getLowDelayFlag(i), 1);  // u(1)
    }
    if (!hp.getLowDelayFlag(i)) {
      WRITE_CODE(hp.getCabCntMinus1(i), 1);  // ue(v)
    }
    if (hp.getNalParametersPresentFlag()) {
      hrdSubLayerParameters(
        bitstream, hp.getHdrSubLayerParameters(0, i), hp.getCabCntMinus1(i));
    }
    if (hp.getAclParametersPresentFlag()) {
      hrdSubLayerParameters(
        bitstream, hp.getHdrSubLayerParameters(1, i), hp.getCabCntMinus1(i));
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.3  Sub-layer HRD parameters syntax
void
AtlasWriter::hrdSubLayerParameters(vmesh::Bitstream&      bitstream,
                                   HrdSubLayerParameters& hlsp,
                                   size_t                 cabCnt) {
  TRACE_BITSTREAM_IN("%s", __func__);
  for (size_t i = 0; i <= cabCnt; i++) {
    WRITE_UVLC(hlsp.getBitRateValueMinus1(i));  // ue(v)
    WRITE_UVLC(hlsp.getCabSizeValueMinus1(i));  // ue(v)
    WRITE_CODE(hlsp.getCbrFlag(i), 1);          // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.4 Maximum coded video resolution syntax
void
AtlasWriter::maxCodedVideoResolution(vmesh::Bitstream&        bitstream,
                                     MaxCodedVideoResolution& mcvr) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(mcvr.getOccupancyResolutionPresentFlag(), 1);  // u(1)
  WRITE_CODE(mcvr.getGeometryResolutionPresentFlag(), 1);   // u(1)
  WRITE_CODE(mcvr.getAttributeResolutionPresentFlag(), 1);  // u(1)
  if (mcvr.getOccupancyResolutionPresentFlag()) {
    WRITE_UVLC(mcvr.getOccupancyWidth());   // ue(v)
    WRITE_UVLC(mcvr.getOccupancyHeight());  // ue(v)
  }
  if (mcvr.getGeometryResolutionPresentFlag()) {
    WRITE_UVLC(mcvr.getGeometryWidth());   // ue(v)
    WRITE_UVLC(mcvr.getGeometryHeight());  // ue(v)
  }
  if (mcvr.getAttributeResolutionPresentFlag()) {
    WRITE_UVLC(mcvr.getAttributeWidth());   // ue(v)
    WRITE_UVLC(mcvr.getAttributeHeight());  // ue(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.5 Coordinate system parameters syntax
void
AtlasWriter::coordinateSystemParameters(vmesh::Bitstream&           bitstream,
                                        CoordinateSystemParameters& csp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(csp.getForwardAxis(), 2);    // u(2)
  WRITE_CODE(csp.getDeltaLeftAxis(), 1);  // u(1)
  WRITE_CODE(csp.getForwardSign(), 1);    // u(1)
  WRITE_CODE(csp.getLeftSign(), 1);       // u(1)
  WRITE_CODE(csp.getUpSign(), 1);         // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.6 V-DMC VUI parameters syntax
void
AtlasWriter::vdmcVuiParameters(vmesh::Bitstream&  bitstream,
                               VdmcVuiParameters& vp,
                               size_t             numAttributesVideo) {
  TRACE_BITSTREAM_IN("%s", __func__);
  for (size_t d = 0; d < numAttributesVideo; d++) {
    WRITE_CODE(vp.getVdmcVuiOneSubmeshPerIndependentUnitAttributeFlag(d),
               1);  // u(1)
  }
  WRITE_CODE(vp.getVdmcVuiOneSubmeshPerIndependentUnitGeometryFlag(),
             1);  // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.4.1	VPS V-PCC extension syntax
//void
//AtlasWriter::vpsVpccExtension(vmesh::Bitstream& bitstream,
//                              VpsVpccExtension& ext) {
//  TRACE_BITSTREAM_IN("%s", __func__);
//  TRACE_BITSTREAM_OUT("%s", __func__);
//}

// H.7.3.6.1.1	ASPS V-PCC extension syntax
void
AtlasWriter::aspsVpccExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AspsVpccExtension&             ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(ext.getRemoveDuplicatePointEnableFlag(), 1);  // u(1)
  if (asps.getPixelDeinterleavingFlag() || asps.getPLREnabledFlag()) {
    WRITE_UVLC(ext.getSurfaceThicknessMinus1());  // ue(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.1.2	ASPS MIV extension syntax
void
AtlasWriter::aspsMivExtension(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.1	AFPS V-PCC extension syntax
void
AtlasWriter::afpsVpccExtension(vmesh::Bitstream&  bitstream,
                               AfpsVpccExtension& ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.1	AAPS V-PCC extension syntax
void
AtlasWriter::aapsVpccExtension(vmesh::Bitstream&  bitstream,
                               AapsVpccExtension& ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(ext.getCameraParametersPresentFlag(), 1);  // u(1);
  if (ext.getCameraParametersPresentFlag()) {
    atlasCameraParameters(bitstream, ext.getAtlasCameraParameters());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.2	Atlas camera parameters syntax
void
AtlasWriter::atlasCameraParameters(vmesh::Bitstream&      bitstream,
                                   AtlasCameraParameters& acp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(acp.getCameraModel(), 8);  // u(8)
  if (acp.getCameraModel() == 1) {
    WRITE_CODE(acp.getScaleEnabledFlag(), 1);     // u(1)
    WRITE_CODE(acp.getOffsetEnabledFlag(), 1);    // u(1)
    WRITE_CODE(acp.getRotationEnabledFlag(), 1);  // u(1)
    if (acp.getScaleEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        WRITE_CODE(acp.getScaleOnAxis(i), 32);
      }  // u(32)
    }
    if (acp.getOffsetEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        WRITE_CODES(acp.getOffsetOnAxis(i), 32);
      }  // i(32)
    }
    if (acp.getRotationEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        WRITE_CODES(acp.getRotation(i), 16);
      }  // i(16)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
