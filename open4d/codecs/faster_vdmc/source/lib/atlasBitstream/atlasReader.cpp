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

#include "util/mesh.hpp"
#include "atlasReader.hpp"
#include "bitstream.hpp"
#include "readerCommon.hpp"

using namespace atlas;

AtlasReader::AtlasReader() {}
AtlasReader::~AtlasReader() = default;

// 8.2 Specification of syntax functions and descriptors
bool
AtlasReader::byteAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AtlasReader::lengthAligned(vmesh::Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
AtlasReader::moreDataInPayload(vmesh::Bitstream& bitstream) {
  return !bitstream.byteAligned();
}
bool
AtlasReader::moreRbspData(vmesh::Bitstream& bitstream) {
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
AtlasReader::payloadExtensionPresent(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

// 8.3.2.4 Atlas sub-bitstream syntax
void
AtlasReader::decode(AtlasBitstream& atlas, vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t                     sizeBitstream = bitstream.capacity();
  PCCSEI                     sei;
  vmesh::SampleStreamNalUnit ssnu;
  sampleStreamNalHeader(bitstream, ssnu);
  while (bitstream.size() < sizeBitstream) {
    ssnu.addNalUnit();
    //sampleStreamNalUnit(syntax, true, bitstream, ssnu, ssnu.getNalUnit().size() - 1, sei);
    //READ_CODE(nalu.getSize(), 8 * (ssnu.getSizePrecisionBytesMinus1() + 1));  // u(v)
    auto&  nalu     = ssnu.getNalUnit(ssnu.getNalUnit().size() - 1);
    size_t naluSize = 0;
    READ_CODE_DESC(
      naluSize, 8 * (ssnu.getSizePrecisionBytesMinus1() + 1), "AD_NALUSIZE");
    nalu.setSize(naluSize);
    nalu.allocate();
    vmesh::Bitstream ssnuBitstream;
#if defined(BITSTREAM_TRACE)
    ssnuBitstream.setTrace(true);
    ssnuBitstream.setLogger(*logger_);
#endif
    bitstream.copyTo(ssnuBitstream, nalu.getSize());
    nalUnitHeader(ssnuBitstream, nalu);
    auto naluType = AtlasNalUnitType(nalu.getType());
    //auto& atlas    = syntax.getAtlasDataStream();
    switch (naluType) {
    case ATLAS_NAL_ASPS:
      atlasSequenceParameterSetRbsp(atlas.addAtlasSequenceParameterSet(),
                                    ssnuBitstream);
      break;
    case ATLAS_NAL_AFPS:
      atlasFrameParameterSetRbsp(
        atlas.addAtlasFrameParameterSet(), atlas, ssnuBitstream);
      break;
    case ATLAS_NAL_TRAIL_N:
    case ATLAS_NAL_TRAIL_R:
    case ATLAS_NAL_TSA_N:
    case ATLAS_NAL_TSA_R:
    case ATLAS_NAL_STSA_N:
    case ATLAS_NAL_STSA_R:
    case ATLAS_NAL_RADL_N:
    case ATLAS_NAL_RADL_R:
    case ATLAS_NAL_RASL_N:
    case ATLAS_NAL_RASL_R:
    case ATLAS_NAL_SKIP_N:
    case ATLAS_NAL_SKIP_R:
    case ATLAS_NAL_IDR_N_LP:
      atlasTileLayerRbsp(
        atlas.addAtlasTileLayer(), atlas, naluType, ssnuBitstream);
      atlas.getAtlasTileLayerList().back().getSEI().getSeiPrefix() =
        sei.getSeiPrefix();
      sei.getSeiPrefix().clear();
      break;
    case ATLAS_NAL_PREFIX_ESEI:
    case ATLAS_NAL_PREFIX_NSEI:
      seiRbsp(atlas, ssnuBitstream, naluType, sei);
      break;
    case ATLAS_NAL_SUFFIX_ESEI:
    case ATLAS_NAL_SUFFIX_NSEI:
      seiRbsp(atlas,
              ssnuBitstream,
              naluType,
              atlas.getAtlasTileLayerList().back().getSEI());
      break;
    default:
      fprintf(stderr,
              "atlasSubStream type = %d not supported\n",
              static_cast<int32_t>(nalu.getType()));
    }
  }
  for (auto& nu : ssnu.getNalUnit()) {
    TRACE_BITSTREAM("atlas nalu........%s\t%d\n",
                    toString((AtlasNalUnitType)nu.getType()).c_str(),
                    nu.getSize());
    printf("atlas NalUnit Type = %-25s size = %zu \n",
           toString((AtlasNalUnitType)nu.getType()).c_str(),
           nu.getSize());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.3 Byte alignment syntax
void
AtlasReader::byteAlignment(Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.5 NAL unit syntax
// 8.3.5.1 General NAL unit syntax
void
AtlasReader::nalUnit(Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  nalUnitHeader(bitstream, nalUnit);
  for (size_t i = 2; i < nalUnit.getSize(); i++) {
    READ_CODE(nalUnit.getData(i), 8);  // b(8)
  }
}

// 8.3.5.2 NAL unit header syntax
void
AtlasReader::nalUnitHeader(Bitstream& bitstream, NalUnit& nalUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(zero, 1);                                      // f(1)
  READ_CODE_CAST(nalUnit.getType(), 6, AtlasNalUnitType);  // u(6)
  READ_CODE(nalUnit.getLayerId(), 6);                      // u(6)
  READ_CODE(nalUnit.getTemporalyIdPlus1(), 3);             // u(3)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1 Atlas sequence parameter set Rbsp
// 8.3.6.1.1 General Atlas sequence parameter set Rbsp
void
AtlasReader::atlasSequenceParameterSetRbsp(AtlasSequenceParameterSetRbsp& asps,
                                           vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_UVLC(asps.getAtlasSequenceParameterSetId());  // ue(v)
  uint16_t FrameWidth, FrameHeight;
  READ_UVLC(FrameWidth);
  asps.setFrameWidth(FrameWidth);
  READ_UVLC(FrameHeight);
  asps.setFrameHeight(FrameHeight);
  //  READ_UVLC(asps.getFrameWidth());                          // ue(v)
  //  READ_UVLC(asps.getFrameHeight());                         // ue(v)
  READ_CODE(asps.getGeometry3dBitdepthMinus1(), 5);         // u(5)
  READ_CODE(asps.getGeometry2dBitdepthMinus1(), 5);         // u(5)
  READ_UVLC(asps.getLog2MaxAtlasFrameOrderCntLsbMinus4());  // ue(v)
  READ_UVLC(asps.getMaxDecAtlasFrameBufferingMinus1());     // ue(v)
  READ_CODE(asps.getLongTermRefAtlasFramesFlag(), 1);       // u(1)
  READ_UVLC(asps.getNumRefAtlasFrameListsInAsps());         // ue(v)
  asps.allocateRefListStruct();
  for (size_t i = 0; i < asps.getNumRefAtlasFrameListsInAsps(); i++) {
    refListStruct(asps.getRefListStruct(i), asps, bitstream);
  }
  READ_CODE(asps.getUseEightOrientationsFlag(), 1);       // u(1)
  READ_CODE(asps.getExtendedProjectionEnabledFlag(), 1);  // u(1)
  if (asps.getExtendedProjectionEnabledFlag()) {
    READ_UVLC(asps.getMaxNumberProjectionsMinus1());  // ue(v)
  }
  READ_CODE(asps.getNormalAxisLimitsQuantizationEnabledFlag(), 1);  // u(1)
  READ_CODE(asps.getNormalAxisMaxDeltaValueEnabledFlag(), 1);       // u(1)
  READ_CODE(asps.getPatchPrecedenceOrderFlag(), 1);                 // u(1)
  READ_CODE(asps.getLog2PatchPackingBlockSize(), 3);                // u(3)
  READ_CODE(asps.getPatchSizeQuantizerPresentFlag(), 1);            // u(1)
  READ_CODE(asps.getMapCountMinus1(), 4);                           // u(4)
  READ_CODE(asps.getPixelDeinterleavingFlag(), 1);                  // u(1)
  if (asps.getPixelDeinterleavingFlag()) {
    asps.allocatePixelDeinterleavingMapFlag();
    for (size_t i = 0; i < asps.getMapCountMinus1() + 1; i++) {
      READ_CODE(asps.getPixelDeinterleavingMapFlag(i), 1);  // u(1)
    }
  }
  READ_CODE(asps.getRawPatchEnabledFlag(), 1);  // u(1)
  READ_CODE(asps.getEomPatchEnabledFlag(), 1);  // u(1)
  if (asps.getEomPatchEnabledFlag() && asps.getMapCountMinus1() == 0) {
    READ_CODE(asps.getEomFixBitCountMinus1(), 4);  // u(4)
  }
  if (asps.getRawPatchEnabledFlag() || asps.getEomPatchEnabledFlag()) {
    READ_CODE(asps.getAuxiliaryVideoEnabledFlag(), 1);  // u(1)
  }
  READ_CODE(asps.getPLREnabledFlag(), 1);  // u(1)
  if (asps.getPLREnabledFlag()) { exit(-1); }
  READ_CODE(asps.getVuiParametersPresentFlag(), 1);  // u(1)
  if (asps.getVuiParametersPresentFlag()) {
    vuiParameters(bitstream, asps.getVuiParameters());
  }

  READ_CODE(asps.getExtensionFlag(), 1);  // u(1)
  if (asps.getExtensionFlag()) {
    READ_CODE(asps.getVpccExtensionFlag(), 1);  // u(1)
    READ_CODE(asps.getMivExtensionFlag(), 1);   // u(1)
    READ_CODE(asps.getVdmcExtensionFlag(), 1);  // u(1)
    READ_CODE(asps.getExtension5Bits(), 5);     // u(5)
    if (asps.getVpccExtensionFlag())
      aspsVpccExtension(bitstream, asps, asps.getAspsVpccExtension());
    if (asps.getVdmcExtensionFlag())
      aspsVdmcExtension(bitstream, asps, asps.getAsveExtension());
    if (asps.getExtension5Bits())
      while (moreRbspData(bitstream)) READ_CODE(zero, 1);  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1.2 Point local reconstruction information syntax

// 8.3.6.1.3 Atlas sequence parameter set V-DMC extension syntax
void
AtlasReader::aspsVdmcExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AspsVdmcExtension&             asve) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_CODE_DESC(value, 3, "AsveSubdivisionIterationCount");
  asve.setAsveSubdivisionIterationCount(value);
  if (asve.getAsveSubdivisionIterationCount() > 1) {
    READ_CODE_DESC(value, 1, "AsveLodAdaptiveSubdivisionFlag");
    asve.setAsveLodAdaptiveSubdivisionFlag(value);
  }
  READ_CODE_DESC(value, 1, "AsveEdgeBasedSubdivisionFlag");
  asve.setAsveEdgeBasedSubdivisionFlag(value);

  for (int it = 0; it < asve.getAsveSubdivisionIterationCount(); it++) {
    if (asve.getAsveLodAdaptiveSubdivisionFlag() || it == 0) {
      READ_CODE_DESC(value, 3, "AsveSubdivisionMethod");
      asve.setAsveSubdivisionMethod(it, value);
    } else {
      asve.setAsveSubdivisionMethod(it, asve.getAsveSubdivisionMethod()[0]);
    }
  }
  if (asve.getAsveEdgeBasedSubdivisionFlag() && (asve.getAsveSubdivisionIterationCount() != 0)) {
    READ_CODE_DESC(value, 16, "AsveSubdivisionMinEdgeLength");
    asve.setAsveSubdivisionMinEdgeLength(value);
  } else {
    asve.setAsveSubdivisionMinEdgeLength(0);
  }
  READ_CODE_DESC(value, 1,"Asve1DDisplacementFlag");  //u1
  asve.setAsve1DDisplacementFlag(value);
  READ_CODE_DESC(value, 1, "AsveInterpolateSubdividedNormalsFlag");
  asve.setAsveInterpolateSubdividedNormalsFlag(value);
  READ_CODE_DESC(value, 1, "AsveQuantizationParametersPresentFlag");
  asve.setAsveQuantizationParametersPresentFlag(value);
  if (asve.getAsveQuantizationParametersPresentFlag()) {
    READ_CODE_DESC(value, 1, "AsveInverseQuantizationOffsetPresentFlag");
    asve.setAsveInverseQuantizationOffsetPresentFlag(value);
    READ_SVLC_DESC(value, "AsveDisplacementReferenceQPMinus49");
    asve.setAsveDisplacementReferenceQPMinus49(value);
    uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();
    vdmcQuantizationParameters(bitstream,
                               asve.getAsveQuantizationParameters(),
                               asve.getAsveSubdivisionIterationCount(),
                               numComp);
  }

  if (asve.getAsveSubdivisionIterationCount() != 0) {
    READ_CODE_DESC(value, 3, "AsveTransformMethod");
    asve.setAsveTransformMethod(value);
  } else {
    asve.setAsveTransformMethod(0);
  }
  READ_CODE_DESC(value, 1, "AsveLiftingOffsetPresentFlag");
  asve.setAsveLiftingOffsetPresentFlag(value);
  READ_CODE_DESC(value, 1, "AsveDirectionalLiftingPresentFlag");
  asve.setAsveDirectionalLiftingPresentFlag(value);
  if (asve.getAsveTransformMethod() == (uint8_t)TransformMethod::LINEAR_LIFTING) {
    vdmcLiftingTransformParameters(bitstream,
                                   asve.getAspsExtLtpDisplacement(),
                                   0,
                                   asve.getAsveDirectionalLiftingPresentFlag(),
                                   asve.getAsveSubdivisionIterationCount(),
                                   NULL);
  }
  READ_CODE_DESC(value, 1, "AsveAttributeInformationPresentFlag");
  asve.setAsveAttributeInformationPresentFlag(value);
  if (asve.getAsveAttributeInformationPresentFlag()) {
    READ_CODE_DESC(value, 1, "AsveConsistentAttributeFrameFlag");
    asve.setAsveConsistentAttributeFrameFlag(value);
    if (!asve.getAsveConsistentAttributeFrameFlag()) {
      READ_CODE_DESC(value, 7, "AsveAttributeFrameSizeCount");
      asve.setAsveAttributeFrameSizeCount(value);
    } else {
      asve.setAsveAttributeFrameSizeCount(1);
    }

    for (int attrIdx = 0; attrIdx < asve.getAspsAttributeNominalFrameSizeCount();
         attrIdx++) {
      READ_UVLC_DESC(value, "AsveAttributeFrameWidth");
      asve.setAsveAttributeFrameWidth(attrIdx, value);
      READ_UVLC_DESC(value, "AsveAttributeFrameHeight");
      asve.setAsveAttributeFrameHeight(attrIdx, value);
      READ_CODE_DESC(value, 1, "AsveAttributeSubtextureEnabledFlag");
      asve.setAsveAttributeSubtextureEnabledFlag(attrIdx, value);
    }
  }
  READ_CODE_DESC(value, 1, "AsveDisplacementIdPresentFlag");  //u1
  asve.setAsveDisplacementIdPresentFlag(value);
  READ_CODE_DESC(value, 1, "AsveLodPatchesEnableFlag");       //u1
  asve.setAsveLodPatchesEnableFlag(value);
  READ_CODE_DESC(value, 1, "AsvePackingMethod");     //u1
  asve.setAsvePackingMethod(value);
  // orthoAtlas
  READ_CODE_DESC(value, 1, "AsveProjectionTexcoordEnableFlag");  //u1
  asve.setAsveProjectionTexcoordEnableFlag(value);
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    READ_CODE_DESC(value, 1, "AsveProjectionTexcoordMappingAttributeIndexPresentFlag");  //u1
    asve.setAsveProjectionTexcoordMappingAttributeIndexPresentFlag(value);
    if (asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag())
      READ_CODE_DESC(value, 7, "AsveProjectionTexcoordMappingAttributeIndex"); // u(7)
    asve.setAsveProjectionTexcoordMappingAttributeIndex(value);
    READ_CODE_DESC(value, 5, "AsveProjectionTexcoordOutputBitdepthMinus1");  // u(5)
    asve.setAsveProjectionTexcoordOutputBitdepthMinus1(value);
    READ_CODE_DESC(value, 1,"AsveProjectionTexcoordBboxBiasEnableFlag");        // u(1)
    asve.setAsveProjectionTexcoordBboxBiasEnableFlag(value);
    uint64_t ProjectionTextCoordUpscaleFactorMinus1 = 0;
    READ_CODE_40bits(ProjectionTextCoordUpscaleFactorMinus1, 40);                                        // u(40)
    asve.setAsveProjectionTexCoordUpscaleFactorMinus1(ProjectionTextCoordUpscaleFactorMinus1);
    READ_CODE_DESC(value, 6, "AsveProjectionTexcoordLog2DownscaleFactor");  // u(6)
    asve.setAsveProjectionTexcoordLog2DownscaleFactor(value);
    READ_CODE_DESC(value, 1, "AsveProjectionRawTextcoordPresentFlag");   // u(1)
    asve.setAsveProjectionRawTextcoordPresentFlag(value);
    if (asve.getAsveProjectionRawTextcoordPresentFlag()) {
      READ_CODE_DESC(value, 4, "AsveProjectionRawTextcoordBitdepthMinus1");  // u(4)
      asve.setAsveProjectionRawTextcoordBitdepthMinus1(value);
    }
  }
  READ_CODE_DESC(value, 1, "AsveVdmcVuiParametersPresentFlag");
  asve.setAsveVdmcVuiParametersPresentFlag(value);
  VdmcVuiParameters vp = asve.getAsveVdmcVuiParameters();
  vp.allocateStorage(asve.getAspsAttributeNominalFrameSizeCount());
  if (asve.getAsveVdmcVuiParametersPresentFlag() == true) {
    vdmcVuiParameters(bitstream, vp, asve.getAspsAttributeNominalFrameSizeCount());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.6.1.4 Quantization parameters syntax
void
AtlasReader::vdmcQuantizationParameters(vmesh::Bitstream&           bitstream,
                                        VdmcQuantizationParameters& qp,
                                        uint32_t subdivIterationCount,
                                        uint8_t  numComp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  qp.setNumComponents(numComp);
  READ_CODE_DESC(value, 1, "VdmcLodQuantizationFlag");
  qp.setVdmcLodQuantizationFlag(value);
  if (qp.getVdmcLodQuantizationFlag() == 0) {
    for (int k = 0; k < numComp; k++) {
      READ_CODE_DESC(value, 7, "VdmcQuantizationParameters");
      qp.setVdmcQuantizationParameters(k, value);
      READ_CODE_DESC(value, 2, "VdmcLog2LodInverseScale");
      qp.setVdmcLog2LodInverseScale(k, value);
      //for (i = 0; i < subdivisionCount + 1; i++)
      //    QuantizationParameter[qpIndex][i][k] =  qp.setVdmcQuantizationParameters(k)
    }
  } else {
    qp.setNumLod(subdivIterationCount + 1);
    for (int LoDIdx = 0; LoDIdx < subdivIterationCount + 1; LoDIdx++) {
      qp.allocComponents(numComp, LoDIdx);
      for (int compIdx = 0; compIdx < numComp; compIdx++) {
        READ_UVLC_DESC(value, "VdmcLodDeltaQPValue");
        qp.setVdmcLodDeltaQPValue(LoDIdx, compIdx, value);
        value = 0;
        if (qp.getVdmcLodDeltaQPValue(LoDIdx, compIdx))
          READ_CODE_DESC(value, 1, "VdmcLodDeltaQPSign");
        qp.setVdmcLodDeltaQPSign(LoDIdx, compIdx, value);
        //if (qpIndex = 0)
        //    QuantizationParameter[qpIndex][i][k] =
        //    asve_displacement_reference_qp + (1 ?2 *
        //        vqp_lod_delta_quantization_parameter_sign[qpIndex][i][k]) *
        //    vqp_lod_delta_quantization_parameter_value[qpIndex][i][k]
        //else
        //    QuantizationParameter[qpIndex][i][k] =
        //    QuantizationParameter[qpIndex ?1][i][k] + (1 ?2 *
        //        vqp_lod_delta_quantization_parameter_sign[qpIndex][i][k]) *
        //    vqp_lod_delta_quantization_parameter_value[qpIndex][i][k]
      }
    }
  }
  READ_CODE_DESC(value, 1, "VdmcDirectQuantizationEnabledFlag");
  qp.setVdmcDirectQuantizationEnabledFlag(value);
  value = 0;
  if (!qp.getVdmcDirectQuantizationEnabledFlag())
    READ_SVLC_DESC(value, "VdmcBitDepthOffset");
  qp.setVdmcBitDepthOffset(value);

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.1.5 Lifting transform parameters syntax
void
AtlasReader::vdmcLiftingTransformParameters(
  vmesh::Bitstream&               bitstream,
  VdmcLiftingTransformParameters& vdmcLtp,
  int                             paramLevelIndex,
  bool                            dirLiftFlag,
  uint32_t                        subdivIterationCount,
  size_t                          patchMode) {
  TRACE_BITSTREAM_IN("%s", __func__);
  vdmcLtp.setNumLod(subdivIterationCount + 1);
  uint32_t value;
  READ_CODE_DESC(value, 1, "SkipUpdateFlag");
  vdmcLtp.setSkipUpdateFlag(value);
  for (int i = 0; i < subdivIterationCount; i++) {
    if (vdmcLtp.getSkipUpdateFlag()) {
      // UpdateWeight[ ltpIndex ][ i ] = 0
    } else {
      if (i == 0) {
        READ_CODE_DESC(value, 1, "AdaptiveUpdateWeightFlag");
        vdmcLtp.setAdaptiveUpdateWeightFlag(value);
        READ_CODE_DESC(value, 1, "ValenceUpdateWeightFlag");
        vdmcLtp.setValenceUpdateWeightFlag(value);
      }
      if ((vdmcLtp.getAdaptiveUpdateWeightFlag() == 1) || (i == 0)) {
        READ_UVLC_DESC(value, "LiftingUpdateWeightNumerator");
        vdmcLtp.setLiftingUpdateWeightNumerator(i, value);
        READ_UVLC_DESC(value, "LiftingUpdateWeightDenominatorMinus1");
        vdmcLtp.setLiftingUpdateWeightDenominatorMinus1(i, value);
        //UpdateWeight[ ltpIndex ][ i ] =
        //  vdmcLtp.getLiftingUpdateWeightNumerator(i) ?
        //  (vdmcLtp.setLiftingUpdateWeightDenominatorMinus1(i) + 1)
      } else {
        //  UpdateWeight[ ltpIndex ][ i ] = UpdateWeight[ ltpIndex ][ 0 ]
        //vdmcLtp.setLog2LiftingUpdateWeight(i, vdmcLtp.getLog2LiftingUpdateWeight()[0]);
      }
    }
  }
  READ_CODE_DESC(value, 1, "AdaptivePredictionWeightFlag");
  vdmcLtp.setAdaptivePredictionWeightFlag(value);
  if (vdmcLtp.getAdaptivePredictionWeightFlag()) {
    for (int i = 0; i < subdivIterationCount; i++) {
      READ_UVLC_DESC(value, "LiftingPredictionWeightNumerator");
      vdmcLtp.setLiftingPredictionWeightNumerator(i, value);
      READ_UVLC_DESC(value, "LiftingPredictionWeightDenominatorMinus1");
      vdmcLtp.setLiftingPredictionWeightDenominatorMinus1(i, value);
    }
  } else {
    for (int i = 0; i < subdivIterationCount; i++) {
      vdmcLtp.setLiftingPredictionWeightNumerator(i, 1);
      vdmcLtp.setLiftingPredictionWeightDenominatorMinus1(i, 1);
    }
  }

  if (dirLiftFlag) {
    READ_UVLC_DESC(value, "dirliftScale1");
    vdmcLtp.setDirectionalLiftingScale1(value);
    READ_UVLC_DESC(value, "dirliftDeltaScale2");
    vdmcLtp.setDirectionalLiftingDeltaScale2(value);
    READ_UVLC_DESC(value, "dirliftDeltaScale3");
    vdmcLtp.setDirectionalLiftingDeltaScale3(value);
    READ_UVLC_DESC(value, "dirliftScaleDeno");
    vdmcLtp.setDirectionalLiftingScaleDenoMinus1(value);
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.6.2 Atlas frame parameter set Rbsp syntax
// 8.3.6.2.1 General atlas frame parameter set Rbsp syntax
void
AtlasReader::atlasFrameParameterSetRbsp(AtlasFrameParameterSetRbsp& afps,
                                        AtlasBitstream&             atlas,
                                        vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_UVLC(afps.getAtlasFrameParameterSetId());     // ue(v)
  READ_UVLC(afps.getAtlasSequenceParameterSetId());  // ue(v)
  auto& asps =
    atlas.getAtlasSequenceParameterSet(afps.getAtlasSequenceParameterSetId());
  atlasFrameTileInformation(
    afps.getAtlasFrameTileInformation(), asps, bitstream);
  READ_CODE(afps.getOutputFlagPresentFlag(), 1);                // u(1)
  READ_UVLC(afps.getNumRefIdxDefaultActiveMinus1());            // ue(v)
  READ_UVLC(afps.getAdditionalLtAfocLsbLen());                  // ue(v)
  READ_CODE(afps.getLodModeEnableFlag(), 1);                    // u(1)
  READ_CODE(afps.getRaw3dOffsetBitCountExplicitModeFlag(), 1);  // u(1)
  READ_CODE(afps.getExtensionFlag(), 1);                        // u(1)
  if (afps.getExtensionFlag()) {
    READ_CODE(afps.getMivExtensionFlag(), 1);  // u(1)
    READ_CODE(afps.getVmcExtensionFlag(), 1);  // u(1)
    READ_CODE(afps.getExtension6Bits(), 6);    // u(6)
  }
  if (afps.getVmcExtensionFlag()) {
    auto& afve = afps.getAfveExtension();
    afpsVdmcExtension(bitstream, asps, afps, afve);
  }
  if (afps.getExtension6Bits()) {
    while (moreRbspData(bitstream)) { READ_CODE(zero, 1); }  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.2 Atlas frame tile information syntax
void
AtlasReader::atlasFrameTileInformation(AtlasFrameTileInformation&     afti,
                                       AtlasSequenceParameterSetRbsp& asps,
                                       vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_CODE_DESC(value, 1, "SingleTileInAtlasFrameFlag");
  afti.setSingleTileInAtlasFrameFlag(value);  // u(1)
  auto frameWidth  = asps.getFrameWidth();
  auto frameHeight = asps.getFrameHeight();

  if (!afti.getSingleTileInAtlasFrameFlag()) {
    READ_CODE(afti.getUniformPartitionSpacingFlag(), 1);  // u(1)
    if (afti.getUniformPartitionSpacingFlag()) {
      uint32_t partitionColumnWidthMinus1, partitionRowHeightMinus1;
      READ_UVLC(partitionColumnWidthMinus1);
      afti.setPartitionColumnWidthMinus1(
        0, partitionColumnWidthMinus1);  //  ue(v)
      READ_UVLC(partitionRowHeightMinus1);
      afti.setPartitionRowHeightMinus1(0, partitionRowHeightMinus1);  //  ue(v)
      afti.setNumPartitionColumnsMinus1(
        (frameWidth)
          ? ceil(frameWidth
                 / ((afti.getPartitionColumnWidthMinus1(0) + 1) * 64.0))
              - 1
          : 0);
      afti.setNumPartitionRowsMinus1(
        (frameHeight)
          ? ceil(frameHeight
                 / ((afti.getPartitionRowHeightMinus1(0) + 1) * 64.0))
              - 1
          : 0);
      TRACE_BITSTREAM("numPartitionColumnMinus1 = %d\n",
                      afti.getNumPartitionColumnsMinus1());
      TRACE_BITSTREAM("numPartitionRowsMinus1   = %d\n",
                      afti.getNumPartitionRowsMinus1());
    } else {
      READ_UVLC_DESC(value, "getNumPartitionColumnsMinus1()");
      afti.setNumPartitionColumnsMinus1(value);
      READ_UVLC_DESC(value, "getNumPartitionRowsMinus1()");
      afti.setNumPartitionRowsMinus1(value);  //  ue(v)
      uint32_t partitionColumnWidthMinus1, partitionRowHeightMinus1;
      for (size_t i = 0; i < afti.getNumPartitionColumnsMinus1(); i++) {
        READ_UVLC(partitionColumnWidthMinus1);
        afti.setPartitionColumnWidthMinus1(
          i, partitionColumnWidthMinus1);  //  ue(v)
      }
      for (size_t i = 0; i < afti.getNumPartitionRowsMinus1(); i++) {
        READ_UVLC(partitionRowHeightMinus1);
        afti.setPartitionRowHeightMinus1(i,
                                         partitionRowHeightMinus1);  //  ue(v)
      }
    }
    READ_CODE(afti.getSinglePartitionPerTileFlag(), 1);  //  u(1)
    if (afti.getSinglePartitionPerTileFlag() == 0U) {
      uint32_t NumPartitionsInAtlasFrame =
        (afti.getNumPartitionColumnsMinus1() + 1)
        * (afti.getNumPartitionRowsMinus1() + 1);
      READ_UVLC_DESC(value, "NumTilesInAtlasFrameMinus1()");
      afti.setNumTilesInAtlasFrameMinus1(value);  // ue(v)
      for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
        uint8_t  bitCount = ceilLog2(NumPartitionsInAtlasFrame);
        uint32_t topLeft, bottomRightColumn, bottomRightRow;
        READ_CODE(topLeft, bitCount);
        afti.setTopLeftPartitionIdx(i, topLeft);  // u(v)
        READ_UVLC(bottomRightColumn);
        afti.setBottomRightPartitionColumnOffset(i,
                                                 bottomRightColumn);  // ue(v)
        READ_UVLC(bottomRightRow);
        afti.setBottomRightPartitionRowOffset(i, bottomRightRow);  // ue(v)
      }
    } else {
      afti.setNumTilesInAtlasFrameMinus1(
        (afti.getNumPartitionColumnsMinus1() + 1)
          * (afti.getNumPartitionRowsMinus1() + 1)
        - 1);
      for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
        afti.setTopLeftPartitionIdx(i, (uint32_t)i);
        afti.setBottomRightPartitionColumnOffset(i, 0);
        afti.setBottomRightPartitionRowOffset(i, 0);
      }
    }
  } else {
    afti.setNumTilesInAtlasFrameMinus1(0);
  }
  if (asps.getAuxiliaryVideoEnabledFlag()) {
    READ_UVLC(afti.getAuxiliaryVideoTileRowWidthMinus1());  // ue(v)
    for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
      READ_UVLC(afti.getAuxiliaryVideoTileRowHeight(i));  // ue(v)
    }
  }
  READ_CODE_DESC(value, 1, "SignalledTileIdFlag");  // u(1)
  afti.setSignalledTileIdFlag() = value;
  if (afti.getSignalledTileIdFlag()) {
    READ_UVLC(afti.setSignalledTileIdLengthMinus1());  // ue(v)
    uint8_t bitCount = afti.getSignalledTileIdLengthMinus1() + 1;

    uint32_t maxVal = 0;
    for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
      READ_CODE_DESC(value, bitCount, "_tilIndexToID[ i ]");  // u(v)
      afti.setTileId(value);
      maxVal = std::max(maxVal, value);
    }
  } else {
    for (size_t i = 0; i <= afti.getNumTilesInAtlasFrameMinus1(); i++) {
      afti.setTileId(i);
    }
    afti.setSignalledTileIdLengthMinus1() =
      ceil(log2(afti.getNumTilesInAtlasFrameMinus1() + 1)) - 1;
    TRACE_BITSTREAM("NumTilesInAtlasFrameMinus1   : %d\n",
                    afti.getNumTilesInAtlasFrameMinus1());
    TRACE_BITSTREAM("SignalledTileIdLengthMinus1  : %d\n",
                    afti.getSignalledTileIdLengthMinus1());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.3 Atlas frame parameter set V-DMC extension syntax
void
AtlasReader::afpsVdmcExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AtlasFrameParameterSetRbsp&    afps,
                               AfpsVdmcExtension&             afve) {
  TRACE_BITSTREAM_IN("%s", __func__);

  uint32_t value;
  uint8_t  numComp = asps.getAsveExtension().getAspsDispComponents();
  afve.setAfveSubdivisionIterationCount(
    asps.getAsveExtension().getAsveSubdivisionIterationCount());
  READ_CODE_DESC(value, 1, "AfveOverridenFlag");
  afve.setAfveOverridenFlag(value);
  if (afve.getAfveOverridenFlag()) {
    READ_CODE_DESC(value, 1, "AfveSubdivisionIterationCountPresentFlag");
    afve.setAfveSubdivisionIterationCountPresentFlag(value);
    if (afve.getAfveSubdivisionIterationCountPresentFlag()) {
      READ_CODE_DESC(value, 3, "afveSubdivisionIterationCount");
    afve.setAfveSubdivisionIterationCount(value);
    }
    if (afve.getAfveSubdivisionIterationCount() > 0) {
      READ_CODE_DESC(value, 1, "AfveTransformMethodPresentFlag");
      afve.setAfveTransformMethodPresentFlag(value);
    }
    else {
      afve.setAfveTransformMethodPresentFlag(false);
    }
    if ((!afve.getAfveSubdivisionIterationCountPresentFlag())
        && afve.getAfveSubdivisionIterationCount() > 0) {
      READ_CODE_DESC(value, 1, "AfveSubdivisionMethodPresentFlag");
      afve.setAfveSubdivisionMethodPresentFlag(value);
    } else if (afve.getAfveSubdivisionIterationCount() == 0) {
      afve.setAfveSubdivisionMethodPresentFlag(false);
    } else {
      afve.setAfveSubdivisionMethodPresentFlag(true);
    }
    if (asps.getAsveExtension().getAsveQuantizationParametersPresentFlag()) {
      if (!afve.getAfveSubdivisionIterationCountPresentFlag()) {
        READ_CODE_DESC(value, 1, "AfveQuantizationParametersPresentFlag");
        afve.setAfveQuantizationParametersPresentFlag(value);
      } else {
        afve.setAfveQuantizationParametersPresentFlag(true);
      }
    }
    if ((!afve.getAfveTransformMethodPresentFlag())
        && (!afve.getAfveSubdivisionIterationCountPresentFlag())
        && (afve.getAfveSubdivisionIterationCount() > 0)) {
      READ_CODE_DESC(value, 1, "AfveTransformParametersPresentFlag");
      afve.setAfveTransformParametersPresentFlag(value);
    } else if (afve.getAfveSubdivisionIterationCount() == 0) {
      afve.setAfveTransformParametersPresentFlag(false);
    } else {
      afve.setAfveTransformParametersPresentFlag(true);
    }
  } else {
    value = 0;
    afve.setAfveSubdivisionIterationCountPresentFlag(value);
    afve.setAfveSubdivisionMethodPresentFlag(value);
    if (asps.getAsveExtension().getAsveQuantizationParametersPresentFlag()) {
      afve.setAfveQuantizationParametersPresentFlag(value);
    }
    afve.setAfveTransformMethodPresentFlag(value);
    afve.setAfveTransformParametersPresentFlag(value);
  }
  if (afve.getAfveSubdivisionMethodPresentFlag()) {
    if (afve.getAfveSubdivisionIterationCount() > 1) {
      READ_CODE_DESC(value, 1, "afveLodAdaptiveSubdivisionFlag");
      afve.setAfveLodAdaptiveSubdivisionFlag(value);
    }
    for (int32_t it = 0; it < afve.getAfveSubdivisionIterationCount(); it++) {
      if (afve.getAfveLodAdaptiveSubdivisionFlag() || it == 0) {
        READ_CODE_DESC(value, 3, "afveSubdivisionMethod");
        afve.setAfveSubdivisionMethod(it, value);
      } else {
        afve.setAfveSubdivisionMethod(it, afve.getAfveSubdivisionMethod()[0]);
      }
    }
  } else {
    for (int32_t it = 0; it < afve.getAfveSubdivisionIterationCount(); it++) {
      afve.setAfveSubdivisionMethod(
        it, asps.getAsveExtension().getAsveSubdivisionMethod()[it]);
    }
  }
  if ((asps.getAsveExtension().getAsveEdgeBasedSubdivisionFlag()) && (afve.getAfveSubdivisionIterationCount() != 0)) {
    READ_CODE_DESC(value, 1, "AfveEdgeBasedSubdivisionFlag");
    afve.setAfveEdgeBasedSubdivisionFlag(value);
    if (afve.getAfveEdgeBasedSubdivisionFlag()) {
      READ_CODE_DESC(value, 16, "AfveSubdivisionMinEdgeLength");
      afve.setAfveSubdivisionMinEdgeLength(value);
    } else {
      afve.setAfveSubdivisionMinEdgeLength(
        asps.getAsveExtension().getAsveSubdivisionMinEdgeLength());
    }
  } else {
    afve.setAfveSubdivisionMinEdgeLength(0);
  }
  if (afve.getAfveQuantizationParametersPresentFlag())
    vdmcQuantizationParameters(bitstream,
                               afve.getAfveQuantizationParameters(),
                               afve.getAfveSubdivisionIterationCount(),
                               numComp);

  else
    afve.getAfveQuantizationParameters() =
      asps.getAsveExtension().getAsveQuantizationParameters();

  //READ_CODE_DESC(value, 1, "afveDisplacementCoordinateSystem");  afve.setAfveDisplacementCoordinateSystem(value);

  if (afve.getAfveTransformMethodPresentFlag()) {
    READ_CODE_DESC(value, 3, "afveTransformMethod");
    afve.setAfveTransformMethod(value);
  } else {
    afve.setAfveTransformMethod(
      asps.getAsveExtension().getAsveTransformMethod());
  }
  if (afve.getAfveTransformMethod() == (uint8_t)TransformMethod::LINEAR_LIFTING
      && afve.getAfveTransformParametersPresentFlag()) {
    vdmcLiftingTransformParameters(
      bitstream,
      afve.getAfpsLtpDisplacement(),
      1,
      asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag(),
      afve.getAfveSubdivisionIterationCount(),
      NULL);
  } else {
    afve.getAfpsLtpDisplacement() =
      asps.getAsveExtension().getAspsExtLtpDisplacement();
  }

  if (asps.getAsveExtension().getAsveAttributeInformationPresentFlag()) {
    int aspsAttributeNominalFrameSizeCount =
      asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
    if (aspsAttributeNominalFrameSizeCount > 1) {
      READ_CODE_DESC(value, 1, "afveConsistentTilingAttributeFlag");
      afve.setAfveConsistentTilingAccrossAttributesFlag(value);
    } else {
      afve.setAfveConsistentTilingAccrossAttributesFlag(true);
    }

    afve.getAtlasFrameTileAttributeInformation().resize(
      aspsAttributeNominalFrameSizeCount);
    if (afve.getAfveConsistentTilingAccrossAttributesFlag()) {
      auto refAttrIdx = 0;
      if (aspsAttributeNominalFrameSizeCount > 1) { READ_UVLC(refAttrIdx); }
      afve.setAfveReferenceAttributeIdx(refAttrIdx);

      uint32_t startId = afps.getAtlasFrameTileInformation().getMaxTileId() + 1;
      uint32_t startIdx = afps.getAtlasFrameTileInformation().getNumTilesInAtlasFrameMinus1() + 1;
      atlasFrameTileAttributeInformation(
        bitstream,
        startId,startIdx,
        afve.getAtlasFrameTileAttributeInformation(refAttrIdx),
        refAttrIdx,
        asps);
    } else {
      for (int i = 0; i < aspsAttributeNominalFrameSizeCount; i++) {
        uint32_t startId = 0;
        uint32_t startIdx = 0;
        if (i == 0) {
            startId = afps.getAtlasFrameTileInformation().getMaxTileId() + 1;
            startIdx = afps.getAtlasFrameTileInformation().getNumTilesInAtlasFrameMinus1() + 1;
        }
        else {
            startId = afve.getAtlasFrameTileAttributeInformation(i-1).getMaxTileId() + 1;
            startIdx = afve.getAtlasFrameTileAttributeInformation(i-1).getNumTilesInAtlasFrameMinus1() + 1;
        }
        atlasFrameTileAttributeInformation(
          bitstream,
          startId, startIdx,
          afve.getAtlasFrameTileAttributeInformation(i),
          i,
          asps);
      }
    }
  }

  atlasFrameMeshInformation(afve.getAtlasFrameMeshInformation(), bitstream);
  if (asps.getAsveExtension().getAsveProjectionTexcoordEnableFlag()) {
    auto numSubmeshes =
      afve.getAtlasFrameMeshInformation().getNumSubmeshesInAtlasFrameMinus1()
      + 1;
    for (int i = 0; i < numSubmeshes; i++) {
      READ_CODE(afve.getProjectionTextcoordPresentFlag(i), 1);
      if (afve.getProjectionTextcoordPresentFlag(i)) {
        READ_UVLC(afve.getProjectionTextcoordWidth(i));
        READ_UVLC(afve.getProjectionTextcoordHeight(i));
        READ_UVLC(afve.getProjectionTextcoordGutter(i));
      }
    }
  } else {
    auto numSubmeshes =
      afve.getAtlasFrameMeshInformation().getNumSubmeshesInAtlasFrameMinus1()
      + 1;
    for (int i = 0; i < numSubmeshes; i++)
      afve.getProjectionTextcoordPresentFlag(i) = 0;
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.4	Atlas frame attribute tile information syntax
void
AtlasReader::atlasFrameTileAttributeInformation(
  vmesh::Bitstream&                   bitstream,
  uint32_t startId, uint32_t startIdx,
  AtlasFrameTileAttributeInformation& aftai,
  int32_t                             attrIdx,
  AtlasSequenceParameterSetRbsp&      asps) {
  TRACE_BITSTREAM_IN("%s", __func__);
  VERIFY_CONFORMANCE(attrIdx == 0);
  uint32_t value;
  READ_CODE_DESC(value, 1, "SingleTileInAtlasFrameFlag");
  aftai.setSingleTileInAtlasFrameFlag(value);  // u(1)
  auto frameWidth =
    asps.getAsveExtension().getAsveAttributeFrameWidth()[attrIdx];
  auto frameHeight =
    asps.getAsveExtension().getAsveAttributeFrameHeight()[attrIdx];
  aftai.setTileIndexOffset(startIdx);
  if (!aftai.getSingleTileInAtlasFrameFlag()) {
    READ_CODE(aftai.getUniformPartitionSpacingFlag(), 1);  // u(1)
    if (aftai.getUniformPartitionSpacingFlag()) {
      uint32_t partitionColumnWidthMinus1, partitionRowHeightMinus1;
      READ_UVLC(partitionColumnWidthMinus1);
      aftai.setPartitionColumnWidthMinus1(
        0, partitionColumnWidthMinus1);  //  ue(v)
      READ_UVLC(partitionRowHeightMinus1);
      aftai.setPartitionRowHeightMinus1(0,
                                        partitionRowHeightMinus1);  //  ue(v)
      aftai.setNumPartitionColumnsMinus1(
        (frameWidth)
          ? ceil(frameWidth
                 / ((aftai.getPartitionColumnWidthMinus1(0) + 1) * 64.0))
              - 1
          : 0);
      aftai.setNumPartitionRowsMinus1(
        (frameHeight)
          ? ceil(frameHeight
                 / ((aftai.getPartitionRowHeightMinus1(0) + 1) * 64.0))
              - 1
          : 0);
      TRACE_BITSTREAM("numPartitionColumnMinus1 = %d\n",
                      aftai.getNumPartitionColumnsMinus1());
      TRACE_BITSTREAM("numPartitionRowsMinus1   = %d\n",
                      aftai.getNumPartitionRowsMinus1());
    } else {
      READ_UVLC_DESC(value, "getNumPartitionColumnsMinus1()");
      aftai.setNumPartitionColumnsMinus1(value);
      READ_UVLC_DESC(value, "getNumPartitionRowsMinus1()");
      aftai.setNumPartitionRowsMinus1(value);  //  ue(v)
      uint32_t partitionColumnWidthMinus1, partitionRowHeightMinus1;
      for (size_t i = 0; i < aftai.getNumPartitionColumnsMinus1(); i++) {
        READ_UVLC(partitionColumnWidthMinus1);
        aftai.setPartitionColumnWidthMinus1(
          i, partitionColumnWidthMinus1);  //  ue(v)
      }
      for (size_t i = 0; i < aftai.getNumPartitionRowsMinus1(); i++) {
        READ_UVLC(partitionRowHeightMinus1);
        aftai.setPartitionRowHeightMinus1(i,
                                          partitionRowHeightMinus1);  //  ue(v)
      }
    }
    READ_CODE(aftai.getSinglePartitionPerTileFlag(), 1);  //  u(1)
    if (aftai.getSinglePartitionPerTileFlag() == 0U) {
      uint32_t NumPartitionsInAtlasFrame =
        (aftai.getNumPartitionColumnsMinus1() + 1)
        * (aftai.getNumPartitionRowsMinus1() + 1);
      READ_UVLC_DESC(value, "NumTilesInAtlasFrameMinus1()");
      aftai.setNumTilesInAtlasFrameMinus1(value);  // ue(v)
      for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
        uint8_t  bitCount = ceilLog2(NumPartitionsInAtlasFrame);
        uint32_t topLeft, bottomRightColumn, bottomRightRow;
        READ_CODE(topLeft, bitCount);
        aftai.setTopLeftPartitionIdx(i, topLeft);  // u(v)
        READ_UVLC(bottomRightColumn);
        aftai.setBottomRightPartitionColumnOffset(i,
                                                  bottomRightColumn);  // ue(v)
        READ_UVLC(bottomRightRow);
        aftai.setBottomRightPartitionRowOffset(i, bottomRightRow);  // ue(v)
      }
    } else {
      aftai.setNumTilesInAtlasFrameMinus1(
        (aftai.getNumPartitionColumnsMinus1() + 1)
          * (aftai.getNumPartitionRowsMinus1() + 1)
        - 1);
      for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
        aftai.setTopLeftPartitionIdx(i, (uint32_t)i);
        aftai.setBottomRightPartitionColumnOffset(i, 0);
        aftai.setBottomRightPartitionRowOffset(i, 0);
      }
    }
  } else {
    aftai.setNumTilesInAtlasFrameMinus1(0);
    READ_CODE_DESC(value, 1, "NalTilePresentFlag");  // u(1)
    aftai.setNalTilePresentFlag(value);
  }
  READ_CODE_DESC(value, 1, "SignalledTileIdFlag");  // u(1)
  aftai.setSignalledTileIdFlag() = value;
  if (aftai.getSignalledTileIdFlag()) {
    READ_UVLC(aftai.setSignalledTileIdLengthMinus1());  // ue(v)
    uint8_t bitCount = aftai.getSignalledTileIdLengthMinus1() + 1;

    uint32_t maxVal = 0;
    for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
      READ_CODE_DESC(value, bitCount, "_tilIndexToID[ i ]");  // u(v)
      aftai.setTileId(value);
      maxVal = std::max(maxVal, value);
    }
  } else {
    for (size_t i = 0; i <= aftai.getNumTilesInAtlasFrameMinus1(); i++) {
      aftai.setTileId(i + startId);
    }
    //aftai.setSignalledTileIdLengthMinus1() = ceil(log2(aftai.getMaxTileId()));
    aftai.setSignalledTileIdLengthMinus1() = ceil(log2(aftai.getNumTilesInAtlasFrameMinus1()+1+startId)) - 1;
    TRACE_BITSTREAM("NumTilesInAtlasFrameMinus1   : %d\n",
                    aftai.getNumTilesInAtlasFrameMinus1());
    TRACE_BITSTREAM("SignalledTileIdLengthMinus1  : %d\n",
                    aftai.getSignalledTileIdLengthMinus1());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.2.5 Atlas frame mesh information syntax
void
AtlasReader::atlasFrameMeshInformation(AtlasFrameMeshInformation& afmi,
                                       vmesh::Bitstream&          bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(afmi.getNumSubmeshesInAtlasFrameMinus1(), 6);  // u(6)
  auto numSubmeshes =
    static_cast<size_t>(afmi.getNumSubmeshesInAtlasFrameMinus1()) + 1;
  READ_CODE(afmi.getSignalledSubmeshIdFlag(), 1);  // u(1)
  if (afmi.getSignalledSubmeshIdFlag()) {
    afmi._submeshIndexToID.resize(numSubmeshes);
    READ_UVLC(afmi.getSignalledSubmeshIdDeltaLength());  // ue(v)
    uint32_t value = 0;
    uint32_t b     = (uint32_t)ceil(log2(numSubmeshes));
    for (size_t i = 0; i < numSubmeshes; i++) {
      uint8_t bitCount = afmi.getSignalledSubmeshIdDeltaLength() + b;
      READ_CODE_DESC(value, bitCount, "SubmeshId(i)");  // u(v)
      afmi.setSubmeshId(i, value);
      afmi._submeshIndexToID[i] = afmi.getSubmeshId(i);
      if (afmi._submeshIDToIndex.size() <= afmi._submeshIndexToID[i])
        afmi._submeshIDToIndex.resize(afmi._submeshIndexToID[i] + 1, -1);
      afmi._submeshIDToIndex[afmi._submeshIndexToID[i]] = (uint32_t)i;
    }
  } else {
    afmi.getSignalledSubmeshIdDeltaLength() = 0;
    afmi._submeshIDToIndex.resize(numSubmeshes);
    afmi._submeshIndexToID.resize(numSubmeshes);
    for (size_t i = 0; i < numSubmeshes; i++) {
      afmi.setSubmeshId(i, i);
      afmi._submeshIDToIndex[i] = (uint32_t)i;
      afmi._submeshIndexToID[i] = (uint32_t)i;
      TRACE_BITSTREAM("\t%d-th ID %d\n", i, afmi._submeshIndexToID[i]);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.3 Atlas adaptation parameter set RBSP syntax
void
AtlasReader::atlasAdaptationParameterSetRbsp(
  AtlasAdaptationParameterSetRbsp& aaps,
  vmesh::Bitstream&                bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_UVLC(aaps.getAtlasAdaptationParameterSetId());  // ue(v)
  READ_CODE(aaps.getExtensionFlag(), 1);               // u(1)
  if (aaps.getExtensionFlag()) {
    READ_CODE(aaps.getVpccExtensionFlag(), 1);  // u(1)
    READ_CODE(aaps.getExtension7Bits(), 7);     // u(7)
    if (aaps.getVpccExtensionFlag())
      aapsVpccExtension(bitstream, aaps.getAapsVpccExtension());
    if (aaps.getExtension7Bits())
      while (moreRbspData(bitstream)) { READ_CODE(zero, 1); }  // u(1)
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.4  Supplemental enhancement information Rbsp
void
AtlasReader::seiRbsp(AtlasBitstream&       atlas,
                     vmesh::Bitstream& bitstream,
                     AtlasNalUnitType  nalUnitType,
                     PCCSEI&           sei) {
  TRACE_BITSTREAM_IN("%s", __func__);
  do {
    seiMessage(bitstream, atlas, nalUnitType, sei);
  } while (moreRbspData(bitstream));
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.5 Access unit delimiter RBSP syntax
void
AtlasReader::accessUnitDelimiterRbsp(AccessUnitDelimiterRbsp& aud,
                                     AtlasBitstream&       atlas,
                                     vmesh::Bitstream&        bitstream) {
  READ_CODE(aud.getAframeType(), 3);  //	u(3)
  rbspTrailingBits(bitstream);
}
// 8.3.6.6 End of sequence RBSP syntax
void
AtlasReader::endOfSequenceRbsp(EndOfSequenceRbsp& eosbsp,
                               AtlasBitstream&       atlas,
                               vmesh::Bitstream&  bitstream) {}

// 8.3.6.7 End of bitstream RBSP syntax
void
AtlasReader::endOfAtlasSubBitstreamRbsp(EndOfAtlasSubBitstreamRbsp& eoasb,
                                        AtlasBitstream&       atlas,
                                        vmesh::Bitstream& bitstream) {}

// 8.3.6.8 Filler data RBSP syntax
void
AtlasReader::fillerDataRbsp(FillerDataRbsp&   fdrbsp,
                            AtlasBitstream&       atlas,
                            vmesh::Bitstream& bitstream) {
  // while ( next_bits( bitstream, 8 ) == 0xFF ) { uint32_t code; READ_CODE( code, 8 );  // f(8)
  rbspTrailingBits(bitstream);
}

// 8.3.6.9  Atlas tile group layer Rbsp syntax
void
AtlasReader::atlasTileLayerRbsp(AtlasTileLayerRbsp& atgl,
                                AtlasBitstream&     atlas,
                                AtlasNalUnitType    nalUnitType,
                                vmesh::Bitstream&   bitstream) {
  // setFrameIndex
  TRACE_BITSTREAM_IN("%s", __func__);
  atlasTileHeader(atgl.getHeader(), atlas, nalUnitType, bitstream);
  // According to V3C 4th edition
  size_t afpsId = atgl.getHeader().getAtlasFrameParameterSetId();
  AtlasFrameParameterSetRbsp& afps   = atlas.getAtlasFrameParameterSet(afpsId);
  size_t                      aspsId = afps.getAtlasSequenceParameterSetId();
  AtlasSequenceParameterSetRbsp& asps =
    atlas.getAtlasSequenceParameterSet(aspsId);
  if (!asps.getExtensionFlag() || asps.getVpccExtensionFlag() || asps.getMivExtensionFlag()) {
      //atlasTileDataUnit(atgl.getDataUnit(), atgl.getHeader(), atlas, bitstream);
  }
  if (asps.getVdmcExtensionFlag()) {
    vdmcAtlasTileDataUnit(atgl.getDataUnit(), atgl.getHeader(), atlas, bitstream);
  }
  rbspTrailingBits(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.10 RBSP trailing bit syntax
void
AtlasReader::rbspTrailingBits(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.11  Atlas tile group header syntax
void
AtlasReader::atlasTileHeader(AtlasTileHeader&  ath,
                             AtlasBitstream&   atlas,
                             AtlasNalUnitType  nalUnitType,
                             vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  if (nalUnitType >= ATLAS_NAL_BLA_W_LP
      && nalUnitType <= ATLAS_NAL_RSV_IRAP_ACL_29) {
    READ_CODE(ath.getNoOutputOfPriorAtlasFramesFlag(), 1);  // u(1)
  }
  if (nalUnitType == ATLAS_NAL_TRAIL_R) { ath.getTileNaluTypeInfo() = 1; }
  if (nalUnitType == ATLAS_NAL_TRAIL_N) { ath.getTileNaluTypeInfo() = 2; }
  READ_UVLC(ath.getAtlasFrameParameterSetId());       // ue(v)
  READ_UVLC(ath.getAtlasAdaptationParameterSetId());  // ue(v)
  size_t afpsId = ath.getAtlasFrameParameterSetId();
  auto&  afps   = atlas.getAtlasFrameParameterSet(afpsId);
  size_t aspsId = afps.getAtlasSequenceParameterSetId();
  auto&  asps   = atlas.getAtlasSequenceParameterSet(aspsId);
  auto&  afti   = afps.getAtlasFrameTileInformation();
  auto&  afve   = afps.getAfveExtension();
  // 8.4.6.11
  // The length of ath_id is max(AftiSignalledTileIDBitCount,
  // AftaiSignalledTileIDBitCount[0], .., AftaiSignalledTileIDBitCount[x]
  // for x in the range of 0 to(asve_num_attribute_video � 1)) bits.
  // avoid issue with automatic getAtlasFrameTileAttributeInformation resize
  // when using getAtlasFrameTileAttributeInformation(i)
  // otherwise when aftai is not present length minus1 is evaluated to 0
  // resulting in mismatch


  uint32_t value;
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
    READ_CODE_DESC(value,
                   signalledTileIDBitCount,
                   "AtlasTileHeaderId");  // u(v)
    ath.setAtlasTileHeaderId(value);
  } else {
    ath.setAtlasTileHeaderId(0);
  }

  READ_UVLC_CAST(ath.getType(), TileType);  // ue(v)
  if (afps.getOutputFlagPresentFlag()) {
    READ_CODE(ath.getAtlasOutputFlag(), 1);  // u(1)
  } else {
    ath.getAtlasOutputFlag() = false;
  }

  READ_CODE_DESC(value,
                 asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4,
                 "AtlasFrmOrderCntLsb");  // u(v)
  ath.setAtlasFrmOrderCntLsb(value);
  if (asps.getNumRefAtlasFrameListsInAsps() > 0) {
    READ_CODE_DESC(value, 1, "RefAtlasFrameListSpsFlag");  // u(1)
    ath.setRefAtlasFrameListSpsFlag(value);
  } else {
    ath.setRefAtlasFrameListSpsFlag(false);
  }
  ath.setRefAtlasFrameListIdx(0);
  if (static_cast<int>(ath.getRefAtlasFrameListSpsFlag()) == 0) {
    refListStruct(ath.getRefListStruct(), asps, bitstream);
  } else if (asps.getNumRefAtlasFrameListsInAsps() > 1) {
    size_t bitCount = ceilLog2(asps.getNumRefAtlasFrameListsInAsps());
    READ_CODE_DESC(value, bitCount, "RefAtlasFrameListIdx");  // u(v)
    ath.setRefAtlasFrameListIdx(value);
  }
  if (ath.getRefAtlasFrameListSpsFlag()) {
    ath.getRefListStruct() =
      asps.getRefListStruct(ath.getRefAtlasFrameListIdx());
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
    READ_CODE(ath.getAdditionalAfocLsbPresentFlag(j), 1);  // u(1)
    if (ath.getAdditionalAfocLsbPresentFlag(j)) {
      uint8_t bitCount = afps.getAdditionalLtAfocLsbLen();
      READ_CODE(ath.getAdditionalAfocLsbVal(j), bitCount);  // u(v)
    }
  }
  if (ath.getType() != SKIP_TILE) {
    if (asps.getNormalAxisLimitsQuantizationEnabledFlag()) {
      READ_CODE(ath.getPosMinDQuantizer(), 5);  // u(5)
      if (asps.getNormalAxisMaxDeltaValueEnabledFlag()) {
        READ_CODE(ath.getPosDeltaMaxDQuantizer(), 5);  // u(5)
      }
    }
    if (asps.getPatchSizeQuantizerPresentFlag()) {
      READ_CODE(ath.getPatchSizeXinfoQuantizer(), 3);  // u(3)
      READ_CODE(ath.getPatchSizeYinfoQuantizer(), 3);  // u(3)
    } else {
      ath.getPatchSizeXinfoQuantizer() = asps.getLog2PatchPackingBlockSize();
      ath.getPatchSizeYinfoQuantizer() = asps.getLog2PatchPackingBlockSize();
    }
    if (afps.getRaw3dOffsetBitCountExplicitModeFlag()) {
      size_t bitCount = floorLog2(asps.getGeometry3dBitdepthMinus1() + 1);
      READ_CODE(ath.getRaw3dOffsetAxisBitCountMinus1(), bitCount);  // u(v)
    } else {
      ath.getRaw3dOffsetAxisBitCountMinus1() =
        std::max(0,
                 asps.getGeometry3dBitdepthMinus1()
                   - asps.getGeometry2dBitdepthMinus1())
        - 1;
    }
    if (ath.getType() == P_TILE && refList.getNumRefEntries() > 1) {
      READ_CODE_DESC(value, 1, "NumRefIdxActiveOverrideFlag");  // u(1)
      ath.setNumRefIdxActiveOverrideFlag(value);
      if (ath.getNumRefIdxActiveOverrideFlag()) {
        READ_UVLC_DESC(value, "NumRefIdxActiveMinus1()");  // ue(v)
        ath.setNumRefIdxActiveMinus1(value);
      }
    }
  }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.12  Reference list structure syntax
void
AtlasReader::refListStruct(RefListStruct&                 rls,
                           AtlasSequenceParameterSetRbsp& asps,
                           vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t value;
  READ_UVLC_DESC(value, "NumRefEntries()");
  rls.setNumRefEntries(value);  // ue(v)
  rls.allocate();
  for (size_t i = 0; i < rls.getNumRefEntries(); i++) {
    if (asps.getLongTermRefAtlasFramesFlag()) {
      READ_CODE_DESC(value, 1, "getStRefAtlasFrameFlag");  // u(1)
      rls.setStRefAtlasFrameFlag(i, value);
    } else {
      rls.setStRefAtlasFrameFlag(i, true);
    }
    if (rls.getStRefAtlasFrameFlag(i)) {
      READ_UVLC_DESC(value, "rls.getAbsDeltaAfocSt(i)");  // ue(v)
      rls.setAbsDeltaAfocSt(i, value);
      if (rls.getAbsDeltaAfocSt(i) > 0) {
        READ_CODE_DESC(value, 1, "rls.getStrafEntrySignFlag(i)");  // u(1)
        rls.setStrafEntrySignFlag(i, value);
      } else {
        rls.setStrafEntrySignFlag(i, true);
      }
    } else {
      uint8_t bitCount = asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4;
      READ_CODE_DESC(value, bitCount, "rls.getAfocLsbLt(i)");  // u(v)
      rls.setAfocLsbLt(i, value);
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.6.13	Common atlas sequence parameter set RBSP syntax
// 8.3.6.14	Common atlas frame RBSP syntax

// 8.3.7	Atlas tile data unit syntax
// 8.3.7.1	General atlas tile data unit syntax
void
AtlasReader::vdmcAtlasTileDataUnit(AtlasTileDataUnit& atdu,
                               AtlasTileHeader&   ath,
                               AtlasBitstream&       atlas,
                               vmesh::Bitstream&  bitstream) {
  TRACE_BITSTREAM_IN("%s(%s)", __func__, toString(ath.getType()).c_str());
  if (ath.getType() == SKIP_TILE) {
    skipMeshpatchDataUnit(bitstream);
  } else {
    atdu.init();
    size_t patchIndex = 0;
    prevPatchSizeU_   = 0;
    prevPatchSizeV_   = 0;
    predPatchIndex_   = 0;
    uint8_t PatchMode = 0;
    READ_UVLC(PatchMode);  // ue(v)
    while ((PatchMode != I_END) && (PatchMode != P_END)) {
      auto& pid           = atdu.addPatchInformationData(PatchMode);
      pid.getTileOrder()  = atdu.getTileOrder();
      pid.getPatchIndex() = patchIndex;
      patchIndex++;
      patchInformationData(pid, PatchMode, ath, atlas, bitstream);
      READ_UVLC(PatchMode);  // ue(v)
    }
    prevFrameIndex_ = atdu.getTileOrder();
  }
  TRACE_BITSTREAM_OUT("%s(%s)", __func__, toString(ath.getType()).c_str());
}

// 8.3.7.2  Patch information data syntax
void
AtlasReader::patchInformationData(PatchInformationData& pid,
                                  size_t                patchMode,
                                  AtlasTileHeader&      ath,
                                  AtlasBitstream&       atlas,
                                  vmesh::Bitstream&     bitstream) {
  if (ath.getType() == SKIP_TILE) {
    // skip mode: currently not supported but added it for convenience. Could
    // easily be removed
  } else if (ath.getType() == P_TILE) {
    if (patchMode == P_SKIP) {
      skipMeshpatchDataUnit(bitstream);
    } else if (patchMode == P_INTRA) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlas, patchMode, bitstream);
    } else if (patchMode == P_INTER) {
      auto& imdu = pid.getInterMeshpatchDataUnit();
      imdu.setTileOrder(pid.getTileOrder());
      imdu.setCurrentPatchIndex(pid.getPatchIndex());
      interMeshpatchDataUnit(imdu, ath, atlas, patchMode, bitstream);
    } else if (patchMode == P_MERGE) {
      auto& mmdu           = pid.getMergeMeshpatchDataUnit();
      mmdu.getTileOrder()  = (pid.getTileOrder());
      mmdu.getPatchIndex() = (pid.getPatchIndex());
      mergeMeshpatchDataUnit(mmdu, ath, atlas, patchMode, bitstream);
    }
  } else if (ath.getType() == I_TILE) {
    if (patchMode == I_INTRA) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlas, patchMode, bitstream);
    }
  } else if (ath.getType() == P_TILE_ATTR) {
    VERIFY_CONFORMANCE((patchMode == I_INTRA_ATTR || patchMode == P_SKIP_ATTR
                        || patchMode == I_END_ATTR));
    if (patchMode == I_INTRA_ATTR) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlas, patchMode, bitstream);
    } else if (patchMode == P_SKIP) {
      skipMeshpatchDataUnit(bitstream);
    }
  } else if (ath.getType() == I_TILE_ATTR) {
    VERIFY_CONFORMANCE(patchMode == I_INTRA_ATTR || patchMode == I_END_ATTR);
    if (patchMode == I_INTRA_ATTR) {
      auto& mdu = pid.getMeshpatchDataUnit();
      mdu.setTileOrder(pid.getTileOrder());
      mdu.setPatchIndex(pid.getPatchIndex());
      meshpatchDataUnit(mdu, ath, atlas, patchMode, bitstream);
    }
  }
}

// 8.3.7.3  Patch data unit syntax
void
AtlasReader::meshpatchDataUnit(MeshpatchDataUnit& mdu,
                               AtlasTileHeader&   ath,
                               AtlasBitstream&    atlas,
                               size_t             patchMode,
                               vmesh::Bitstream&  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t  afpsId     = ath.getAtlasFrameParameterSetId();
  auto&   afps       = atlas.getAtlasFrameParameterSet(afpsId);
  size_t  aspsId     = afps.getAtlasSequenceParameterSetId();
  auto&   asps       = atlas.getAtlasSequenceParameterSet(aspsId);
  uint8_t bitCountUV = asps.getGeometry3dBitdepthMinus1() + 1;
  uint8_t bitCountD =
    asps.getGeometry3dBitdepthMinus1() - ath.getPosMinDQuantizer() + 1;
  uint32_t value;
  auto&    asve = asps.getAsveExtension();
  auto&    afve = afps.getAfveExtension();

  auto&   afmi    = afve.getAtlasFrameMeshInformation();
  uint8_t numComp = asps.getAsveExtension().getAspsDispComponents();

  READ_UVLC_DESC(value, "MduSubmeshId");
  mdu.setMduSubmeshId(value); // ue(v)

  if (ath.getType() == I_TILE || ath.getType() == P_TILE) {
    if (asve.getAsveDisplacementIdPresentFlag()) {
      READ_UVLC_DESC(value, "MduDisplId");
      mdu.setMduDisplId(value);
    }
    else {
      if (asve.getAsveLodPatchesEnableFlag()) {
        READ_UVLC_DESC(value, "MduLoDIdx");
        mdu.setMduLoDIdx(value);
      }
      else {
        mdu.setMduLoDIdx(0);
      }
      READ_UVLC_DESC(value, "MduGeometry2dPosX");
      mdu.setMduGeometry2dPosX(value);
      READ_UVLC_DESC(value, "MduGeometry2dPosY");
      mdu.setMduGeometry2dPosY(value);
      READ_UVLC_DESC(value, "MduGeometry2dSizeXMinus1");
      mdu.setMduGeometry2dSizeXMinus1(value);
      READ_UVLC_DESC(value, "MduGeometry2dSizeYMinus1");
      mdu.setMduGeometry2dSizeYMinus1(value);
    }
    if (mdu.getMduLoDIdx() == 0) {
      mdu.setMduSubdivisionIterationCount(
        afve.getAfveSubdivisionIterationCount());
      READ_CODE_DESC(value, 1, "MduParametersOverrideFlag");
      mdu.setMduParametersOverrideFlag(value);
      if (mdu.getMduParametersOverrideFlag()) {
        READ_CODE_DESC(value, 1, "MduSubdivisionIterationCountPresentFlag");
        mdu.setMduSubdivisionIterationCountPresentFlag(value);
        if (mdu.getMduSubdivisionIterationCountPresentFlag()) {
          READ_CODE_DESC(value, 3, "MduSubdivisionIterationCount");
          mdu.setMduSubdivisionIterationCount(value);
        }
        if (mdu.getMduSubdivisionIterationCount() != 0) {
          READ_CODE_DESC(value, 1, "MduTransformMethodPresentFlag");
          mdu.setMduTransformMethodPresentFlag(value);
        }
        else {
          mdu.setMduTransformMethodPresentFlag(false);
        }
        if ((!mdu.getMduSubdivisionIterationCountPresentFlag())
          && mdu.getMduSubdivisionIterationCount() != 0) {
          READ_CODE_DESC(value, 1, "MduSubdivisionMethodPresentFlag");
          mdu.setMduSubdivisionMethodPresentFlag(value);
        }
        else if (mdu.getMduSubdivisionIterationCount() == 0) {
          mdu.setMduSubdivisionMethodPresentFlag(false);
        }
        else {
          mdu.setMduSubdivisionMethodPresentFlag(true);
        }
        if (asps.getAsveExtension()
              .getAsveQuantizationParametersPresentFlag()) {
          if (!mdu.getMduSubdivisionIterationCountPresentFlag()) {
            READ_CODE_DESC(value, 1, "MduQuantizationPresentFlag");
            mdu.setMduQuantizationPresentFlag(value);
          }
          else {
            mdu.setMduQuantizationPresentFlag(true);
        }
        }
        if ((!mdu.getMduTransformMethodPresentFlag())
          && (mdu.getMduSubdivisionIterationCount() != 0)) {
          READ_CODE_DESC(value, 1, "MduTransformParametersPresentFlag");
          mdu.setMduTransformParametersPresentFlag(value);
        }
        else if (mdu.getMduSubdivisionIterationCount() == 0) {
          mdu.setMduTransformParametersPresentFlag(false);
        }
        else {
          mdu.setMduTransformParametersPresentFlag(true);
        }
      }
      else {
        value = 0;
        mdu.setMduSubdivisionIterationCountPresentFlag(value);
        mdu.setMduSubdivisionMethodPresentFlag(value);
        if (asps.getAsveExtension()
              .getAsveQuantizationParametersPresentFlag()) {
          mdu.setMduQuantizationPresentFlag(value);
        }
        mdu.setMduTransformMethodPresentFlag(value);
        mdu.setMduTransformParametersPresentFlag(value);
      }
      if (mdu.getMduSubdivisionMethodPresentFlag()) {
        if (mdu.getMduSubdivisionIterationCount() > 1) {
          READ_CODE_DESC(value, 1, "MduLodAdaptiveSubdivisionFlag");
          mdu.setMduLodAdaptiveSubdivisionFlag(value);
        }
        for (int32_t it = 0; it < mdu.getMduSubdivisionIterationCount();
             it++) {
          if (mdu.getMduLodAdaptiveSubdivisionFlag() || it == 0) {
            READ_CODE_DESC(value, 3, "MduSubdivisionMethod");
            mdu.setMduSubdivisionMethod(it, value);
          }
          else {
            mdu.setMduSubdivisionMethod(it, mdu.getMduSubdivisionMethod()[0]);
          }
        }
      }
      else {
        for (int32_t it = 0; it < mdu.getMduSubdivisionIterationCount();
             it++) {
          mdu.setMduSubdivisionMethod(it, afve.getAfveSubdivisionMethod()[it]);
        }
      }
      if (asps.getAsveExtension().getAsveEdgeBasedSubdivisionFlag()
        && mdu.getMduSubdivisionIterationCount() != 0) {
        READ_CODE_DESC(value, 1, "MduEdgeBasedSubdivisionFlag");
        mdu.setMduEdgeBasedSubdivisionFlag(value);
        if (mdu.getMduEdgeBasedSubdivisionFlag()) {
          READ_CODE_DESC(value, 16, "MduSubdivisionMinEdgeLength");
          mdu.setMduSubdivisionMinEdgeLength(value);
        }
        else {
          mdu.setMduSubdivisionMinEdgeLength(
            afve.getAfveSubdivisionMinEdgeLength());
        }
      }
      else {
        mdu.setMduSubdivisionMinEdgeLength(0);
      }
      if (mdu.getMduQuantizationPresentFlag()) {
        vdmcQuantizationParameters(bitstream,
          mdu.getMduQuantizationParameters(),
          mdu.getMduSubdivisionIterationCount(),
                                   numComp);
      }
      else {
        mdu.getMduQuantizationParameters() =
          afve.getAfveQuantizationParameters();
      }
      if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
        READ_CODE_DESC(value, 1, "MduInverseQuantizationOffsetEnableFlag");
        mdu.setMduInverseQuantizationOffsetEnableFlag(value);
        if (mdu.getMduInverseQuantizationOffsetEnableFlag()) {
          mdu.setIQOffsetValuesSize(mdu.getMduSubdivisionIterationCount() + 1,
                                    numComp);
          for (int i = 0; i < mdu.getMduSubdivisionIterationCount() + 1; i++) {
            for (int j = 0; j < numComp; j++) {
              for (
                int k = 0; k < 3;
                k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
                READ_CODE_DESC(value, 1, "IQOffsetValues");
                mdu.getIQOffsetValues(i, j, k, 0) = value;  //sign
                READ_SVLC_DESC(value, "IQOffsetValues");
                mdu.getIQOffsetValues(i, j, k, 1) = value;  //first precision
                READ_SVLC_DESC(value, "IQOffsetValues");
                mdu.getIQOffsetValues(i, j, k, 2) = value;  //second precision
              }
            }
          }
        }
      }
      READ_CODE_DESC(value, 1, "MduDisplacementCoordinateSystem");
      mdu.setMduDisplacementCoordinateSystem(value);
      if (mdu.getMduTransformMethodPresentFlag()) {
        READ_CODE_DESC(value, 3, "MduTransformMethod");
        mdu.setMduTransformMethod(value);
      }
      else {
        mdu.setMduTransformMethod(afve.getAfveTransformMethod());
      }

      if (mdu.getMduTransformMethod()
          == (uint8_t)TransformMethod::LINEAR_LIFTING) {
        mdu.getMduLiftingTransformParameters() = afve.getAfpsLtpDisplacement();
        if (asve.getAsveLiftingOffsetPresentFlag()) {
          READ_CODE_DESC(value, 1, "MduLiftingOffsetPresentFlag");
          mdu.setMduLiftingOffsetPresentFlag(value);
          if (mdu.getMduLiftingOffsetPresentFlag()) {
            auto& vdmcLtp = mdu.getMduLiftingTransformParameters();
            vdmcLtp.getLiftingOffsetValuesNumerator().resize(
              mdu.getMduSubdivisionIterationCount());
            vdmcLtp.getLiftingOffsetValuesDenominatorMinus1().resize(
              mdu.getMduSubdivisionIterationCount());
            for (int i = 0; i < mdu.getMduSubdivisionIterationCount(); i++) {
              READ_SVLC(vdmcLtp.getLiftingOffsetValuesNumerator()[i]);
              READ_UVLC(vdmcLtp.getLiftingOffsetValuesDenominatorMinus1()[i]);
            }
          }
        }
        if (asve.getAsveDirectionalLiftingPresentFlag()) {
          READ_CODE_DESC(value, 1, "MduDirectionalLiftingPresentFlag");
          mdu.setMduDirectionalLiftingPresentFlag(value);
          if (mdu.getMduDirectionalLiftingPresentFlag()) {
            READ_SVLC(mdu.getMduDirectionalLiftingMeanNum());
            READ_UVLC(mdu.getMduDirectionalLiftingMeanDenoMinus1());
            READ_UVLC(mdu.getMduDirectionalLiftingStdNum());
            READ_UVLC(mdu.getMduDirectionalLiftingStdDenoMinus1());
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
    }
    auto bitcount = asps.getLog2PatchPackingBlockSize() * 2;

    if (asve.getAsveDisplacementIdPresentFlag()
        || !asve.getAsveLodPatchesEnableFlag()) {
      mdu.getMduBlockCount().resize(mdu.getMduSubdivisionIterationCount()
                                          + 1);
      mdu.getMduLastPosInBlock().resize(mdu.getMduSubdivisionIterationCount()
                                        + 1);
      for (int32_t i = 0; i <= mdu.getMduSubdivisionIterationCount(); i++) {
        READ_UVLC_DESC(value, "MduBlockCount");
        mdu.setMduBlockCount(i, value);
        READ_CODE_DESC(value, bitcount, "MduLastPosInBlock");
        mdu.setMduLastPosInBlock(i, value);
      }
    } else {
      mdu.getMduBlockCount().resize(1);
      mdu.getMduLastPosInBlock().resize(1);
      READ_UVLC_DESC(value, "MduBlockCount");
      mdu.setMduBlockCount(0, value);
      READ_CODE_DESC(value, bitcount, "MduLastPosInBlock");
      mdu.setMduLastPosInBlock(0, value);
    }
    if (mdu.getMduLoDIdx() == 0) {
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
      READ_UVLC_DESC(value, "MduAttributes2dPosX");
      mdu.setMduAttributes2dPosX(value);
      READ_UVLC_DESC(value, "MduAttributes2dPosY");
      mdu.setMduAttributes2dPosY(value);
      READ_UVLC_DESC(value, "MduAttributes2dSizeXMinus1");
      mdu.setMduAttributes2dSizeXMinus1(value);
      READ_UVLC_DESC(value, "MduAttributes2dSizeYMinus1");
      mdu.setMduAttributes2dSizeYMinus1(value);
    }
  }
}

// 8.3.7.4  Skip patch data unit syntax
void
AtlasReader::skipMeshpatchDataUnit(vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.7.5  Merge patch data unit syntax
void
AtlasReader::mergeMeshpatchDataUnit(MergeMeshpatchDataUnit& mmdu,
                                    AtlasTileHeader&        ath,
                                    AtlasBitstream&         atlas,
                                    size_t                  patchMode,
                                    vmesh::Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t   afpsId          = ath.getAtlasFrameParameterSetId();
  auto&    afps            = atlas.getAtlasFrameParameterSet(afpsId);
  auto&    afve            = afps.getAfveExtension();
  size_t   aspsId          = afps.getAtlasSequenceParameterSetId();
  auto&    asps            = atlas.getAtlasSequenceParameterSet(aspsId);
  auto&    asve            = asps.getAsveExtension();
  size_t   numRefIdxActive = ath.getNumRefIdxActiveMinus1() + 1;
  uint32_t value;
  uint8_t  numComp = asps.getAsveExtension().getAspsDispComponents();

  if (numRefIdxActive > 1) {
    READ_UVLC_DESC(value, "MmduRefIndex");
    mmdu.setMmduRefIndex(value);
  }
  else {
    mmdu.setMmduRefIndex(0);
  }
  if (asve.getAsveInverseQuantizationOffsetPresentFlag() || asve.getAsveLiftingOffsetPresentFlag()) {
    // reading the subdivision count for parsing purposes, should be the same as the reference meshpatch
    // it is only needed for the inverse quantization offset and lifting offset
    READ_CODE_DESC(value, 1, "MmduSubdivisionIterationCountPresentFlag");
    mmdu.setMmduSubdivisionIterationCountPresentFlag(value);
    if (mmdu.getMmduSubdivisionIterationCountPresentFlag()) {
      READ_CODE_DESC(value, 3, "MmduSubdivisionIterationCount");
      mmdu.setMmduSubdivisionIterationCount(value);
    }
    else {
      mmdu.setMmduSubdivisionIterationCount(
        afve.getAfveSubdivisionIterationCount());
    }
  }
  if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
    READ_CODE_DESC(value, 1, "MmduInverseQuantizationOffsetEnableFlag");
    mmdu.setMmduInverseQuantizationOffsetEnableFlag(value);
    if (mmdu.getMmduInverseQuantizationOffsetEnableFlag()) {
        mmdu.setIQOffsetValuesSize(mmdu.getMmduSubdivisionIterationCount() + 1,
                                   numComp);
        for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount() + 1; i++) {
          for (int j = 0; j < numComp; j++) {
            for (
              int k = 0; k < 3;
              k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
              READ_CODE_DESC(value, 1, "IQOffsetValues0");
              mmdu.getIQOffsetValues(i, j, k, 0) = value;  //sign
              READ_SVLC_DESC(value, "IQOffsetValues1");
              mmdu.getIQOffsetValues(i, j, k, 1) = value;  //first precision
              READ_SVLC_DESC(value, "IQOffsetValues2");
              mmdu.getIQOffsetValues(i, j, k, 2) = value;  //second precision
            }
          }
        }
      }
    }
  if (asve.getAsveLiftingOffsetPresentFlag()) {
    mmdu.getMmduLiftingTransformParameters() = asve.getAspsExtLtpDisplacement();
    READ_CODE_DESC(value, 1, "MmduLiftingOffsetPresentFlag");
    mmdu.setMmduLiftingOffsetPresentFlag(value);
    if (mmdu.getMmduLiftingOffsetPresentFlag()) {
      auto& vdmcLtp = mmdu.getMmduLiftingTransformParameters();
        vdmcLtp.getLiftingOffsetDeltaValuesNumerator().resize(
          mmdu.getMmduSubdivisionIterationCount());
        vdmcLtp.getLiftingOffsetDeltaValuesDenominator().resize(
          mmdu.getMmduSubdivisionIterationCount());
        for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount(); i++) {
          READ_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesNumerator()[i]);
          READ_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesDenominator()[i]);
        }
      }
    }
  if (asve.getAsveDirectionalLiftingPresentFlag()) {
      READ_CODE_DESC(value, 1, "MmduEnableDirectionalLiftingFlag");
    mmdu.setMmduDirectionalLiftingPresentFlag(value);
    if (mmdu.getMmduDirectionalLiftingPresentFlag()) {
      READ_SVLC(mmdu.getMmduDirectionalLiftingDeltaMeanNum());
      READ_SVLC(mmdu.getMmduDirectionalLiftingDeltaMeanDeno());
      READ_SVLC(mmdu.getMmduDirectionalLiftingDeltaStdNum());
      READ_SVLC(mmdu.getMmduDirectionalLiftingDeltaStdDeno());
      }
    }
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    READ_CODE_DESC(value, 1, "MmduTextureProjectionPresentFlag");
    mmdu.setMmduTextureProjectionPresentFlag(value);
    if (mmdu.getMmduTextureProjectionPresentFlag())
        textureProjectionMergeInformation(
          mmdu.getTextureProjectionMergeInformation(), asps, bitstream);
    }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.7.6  Inter patch data unit syntax
void
AtlasReader::interMeshpatchDataUnit(InterMeshpatchDataUnit& imdu,
                                    AtlasTileHeader&        ath,
                                    AtlasBitstream&         atlas,
                                    size_t                  patchMode,
                                    vmesh::Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t   afpsId = ath.getAtlasFrameParameterSetId();
  auto&    afps   = atlas.getAtlasFrameParameterSet(afpsId);
  size_t   aspsId = afps.getAtlasSequenceParameterSetId();
  auto&    asps   = atlas.getAtlasSequenceParameterSet(aspsId);
  uint32_t value;
  auto&    asve            = asps.getAsveExtension();
  auto&    afve            = afps.getAfveExtension();
  size_t   numRefIdxActive = ath.getNumRefIdxActiveMinus1() + 1;
  uint8_t  numComp = asps.getAsveExtension().getAspsDispComponents();

  if (numRefIdxActive > 1) {
    READ_UVLC_DESC(value, "ImduRefIndex");
    imdu.setImduRefIndex(value);
  } else {
    imdu.setImduRefIndex(0);
  }
  READ_SVLC_DESC(value, "ImduPatchIndex");
  imdu.setImduPatchIndex(value);

  if (!asve.getAsveDisplacementIdPresentFlag()) {
    if (asve.getAsveLodPatchesEnableFlag()) {
      READ_UVLC_DESC(value, "ImduLoDIdx");
      imdu.setImduLoDIdx(value);
    } else {
      imdu.setImduLoDIdx(0);
    }
    READ_SVLC_DESC(value, "Imdu2dDeltaPosX");
    imdu.setImdu2dDeltaPosX(value);
    READ_SVLC_DESC(value, "Imdu2dDeltaPosY");
    imdu.setImdu2dDeltaPosY(value);
    READ_SVLC_DESC(value, "Imdu2dDeltaSizeX");
    imdu.setImdu2dDeltaSizeX(value);
    READ_SVLC_DESC(value, "Imdu2dDeltaSizeY");
    imdu.setImdu2dDeltaSizeY(value);
  }
  if (imdu.getImduLoDIdx() == 0) {
    READ_CODE_DESC(value, 1, "ImduSubdivisionIterationCountPresentFlag");
    imdu.setImduSubdivisionIterationCountPresentFlag(value);
    if (imdu.getImduSubdivisionIterationCountPresentFlag()) {
      READ_CODE_DESC(value, 3, "ImduSubdivisionIterationCount");
      imdu.setImduSubdivisionIterationCount(value);
    } else {
      imdu.setImduSubdivisionIterationCount(
        afve.getAfveSubdivisionIterationCount());
    }
    if (asve.getAsveInverseQuantizationOffsetPresentFlag()) {
      READ_CODE_DESC(value, 1, "ImduInverseQuantizationOffsetEnableFlag");
      imdu.setImduInverseQuantizationOffsetEnableFlag(value);
      if (imdu.getImduInverseQuantizationOffsetEnableFlag()) {
        imdu.setIQOffsetValuesSize(imdu.getImduSubdivisionIterationCount() + 1,
                                   numComp);
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount() + 1; i++) {
          for (int j = 0; j < numComp; j++) {
            for (
              int k = 0; k < 3;
              k++) {  // k represents the zones where 0 is deadzone, 1 is positive non-deadzone and 2 is negative non-deadzone
              READ_CODE_DESC(value, 1, "IQOffsetValues");
              imdu.getIQOffsetValues(i, j, k, 0) = value;  //sign
              READ_SVLC_DESC(value, "IQOffsetValues");
              imdu.getIQOffsetValues(i, j, k, 1) = value;  //first precision
              READ_SVLC_DESC(value, "IQOffsetValues");
              imdu.getIQOffsetValues(i, j, k, 2) = value;  //second precision
            }
          }
        }
      }
    }

    if (imdu.getImduSubdivisionIterationCount() != 0) {
      READ_CODE_DESC(value, 1, "ImduTransformParametersPresentFlag");
      imdu.setImduTransformParametersPresentFlag(value);
    }
    READ_CODE_DESC(value, 1, "ImduTransformMethodPresentFlag");
    imdu.setImduTransformMethodPresentFlag(value);
    if (imdu.getImduTransformMethodPresentFlag()) {
      READ_CODE_DESC(value, 3, "ImduTransformMethod");
      imdu.setImduTransformMethod(value);
    } else {
      imdu.setImduTransformMethod(afve.getAfveTransformMethod());
    }

    if (imdu.getImduTransformMethod()
          == (uint8_t)TransformMethod::LINEAR_LIFTING
        && asve.getAsveLiftingOffsetPresentFlag()) {
      READ_CODE_DESC(value, 1, "ImduLiftingOffsetPresentFlag");
      imdu.setImduLiftingOffsetPresentFlag(value);
      if (imdu.getImduLiftingOffsetPresentFlag()) {
        auto& vdmcLtp = imdu.getImduLiftingTransformParameters();
        vdmcLtp.getLiftingOffsetDeltaValuesNumerator().resize(
          imdu.getImduSubdivisionIterationCount());
        vdmcLtp.getLiftingOffsetDeltaValuesDenominator().resize(
          imdu.getImduSubdivisionIterationCount());
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount(); i++) {
          READ_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesNumerator()[i]);
          READ_SVLC(vdmcLtp.getLiftingOffsetDeltaValuesDenominator()[i]);
        }
      }
    }
    if (imdu.getImduTransformMethod()
          == (uint8_t)TransformMethod::LINEAR_LIFTING
        && asve.getAsveDirectionalLiftingPresentFlag()) {
      READ_CODE_DESC(value, 1, "ImduDirectionalLiftingPresentFlag");
      imdu.setImduDirectionalLiftingPresentFlag(value);
      if (imdu.getImduDirectionalLiftingPresentFlag()) {
        READ_SVLC(imdu.getImduDirectionalLiftingDeltaMeanNum());
        READ_SVLC(imdu.getImduDirectionalLiftingDeltaMeanDeno());
        READ_SVLC(imdu.getImduDirectionalLiftingDeltaStdNum());
        READ_SVLC(imdu.getImduDirectionalLiftingDeltaStdDeno());
      }
    }
    if (imdu.getImduTransformMethod()
          == (uint8_t)TransformMethod::LINEAR_LIFTING
        && imdu.getImduTransformParametersPresentFlag()
        && (imdu.getImduSubdivisionIterationCount() != 0)) {
      vdmcLiftingTransformParameters(bitstream,
                                     imdu.getImduLiftingTransformParameters(),
                                     2,
                                     asve.getAsveDirectionalLiftingPresentFlag(),
                                     imdu.getImduSubdivisionIterationCount(),
                                     patchMode);
    }
    //orthoAtlas HLS
    if (asve.getAsveProjectionTexcoordEnableFlag()) {
      READ_CODE_DESC(value, 1, "ProjectionTextcoordPresentFlag");
      imdu.setImduTextureProjectionPresentFlag(value);
      if (imdu.getImduTextureProjectionPresentFlag())
        textureProjectionInterInformation(
          imdu.getTextureProjectionInterInformation(), asps, bitstream);
    }
  }
  auto vertexInfoCount = 1;
  if (asve.getAsveDisplacementIdPresentFlag()
      || !asve.getAsveLodPatchesEnableFlag())
    vertexInfoCount = imdu.getImduSubdivisionIterationCount() + 1;
  imdu.getImduDeltaBlockCount().resize(vertexInfoCount);
  imdu.getImduDeltaLastPosInBlock().resize(vertexInfoCount);
  for (int32_t i = 0; i < vertexInfoCount; i++) {
    READ_SVLC(imdu.getImduDeltaBlockCount()[i]);
    READ_SVLC(imdu.getImduDeltaLastPosInBlock()[i]);
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.3.7.7  Raw patch data unit syntax

// 8.3.7.8 EOM patch data unit syntax

// 8.3.7.9 Point local reconstruction data syntax

// 8.3.7.11	Texture projection information syntax
void
AtlasReader::textureProjectionInformation(TextureProjectionInformation&  tpi,
                                          AtlasSequenceParameterSetRbsp& asps,
                                          vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(tpi.getFaceIdPresentFlag(), 1);           //u(1)
  READ_CODE_40bits(tpi.getFrameUpscaleMinus1(), 40);  // u(40)
  READ_CODE(tpi.getFrameDownscale(), 6);              // u(6)
  READ_UVLC(tpi.getSubpatchCountMinus1());            // ue(v)
  if (asps.getAsveExtension().getAsveProjectionRawTextcoordPresentFlag()) {
    READ_CODE(tpi.getSubpatchRawEnableFlag(), 1);  //u(1)
  } else tpi.getSubpatchRawEnableFlag() = 0;
  for (int idx = 0; idx < tpi.getSubpatchCountMinus1() + 1; idx++) {
    if (tpi.getFaceIdPresentFlag()) {
      READ_UVLC(tpi.getFaceId2SubpatchIdx(idx));  // ue(v)
    } else tpi.getFaceId2SubpatchIdx(idx) = idx;
    if (tpi.getSubpatchRawEnableFlag()) {
      READ_CODE(tpi.getSubpatchRawPresentFlag(idx), 1);  //u(1)
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
AtlasReader::subpatchInformation(SubpatchInformation&           si,
                                 AtlasSequenceParameterSetRbsp& asps,
                                 vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(si.getProjectionId(),
            asps.getExtendedProjectionEnabledFlag() ? 5 : 3);  // u(v)
  READ_UVLC(si.getOrientationId());                            // ue(v)
  READ_UVLC(si.get2dPosX());                                   // ue(v)
  READ_UVLC(si.get2dPosY());                                   // ue(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    READ_UVLC(si.getPosBiasX());       // ue(v)
    READ_UVLC(si.getPosBiasY());       // ue(v)
    READ_SVLC(si.get2dSizeXMinus1());  // se(v)
    READ_SVLC(si.get2dSizeYMinus1());  // se(v)
  } else {
    si.getPosBiasX()      = 0;
    si.getPosBiasY()      = 0;
    si.get2dSizeXMinus1() = 0;
    si.get2dSizeYMinus1() = 0;
  }
  READ_CODE(si.getScalePresentFlag(), 1);  // u(1)
  if (si.getScalePresentFlag()) {
    READ_UVLC(si.getSubpatchScale());  // ue(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch raw information syntax
void
AtlasReader::subpatchRawInformation(SubpatchRawInformation&        sri,
                                    AtlasSequenceParameterSetRbsp& asps,
                                    vmesh::Bitstream&              bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto qpTexCoord =
    asps.getAsveExtension().getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
  READ_UVLC(sri.getNumRawUVMinus1());  // ue(v)
  for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
    READ_CODE(sri.getUcoord(i), qpTexCoord);  // u(v)
    READ_CODE(sri.getVcoord(i), qpTexCoord);  // u(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Texture projection inter information syntax
void
AtlasReader::textureProjectionInterInformation(
  TextureProjectionInterInformation& tpii,
  AtlasSequenceParameterSetRbsp&     asps,
  vmesh::Bitstream&                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(tpii.getFaceIdPresentFlag(), 1);           //u(1)
  READ_CODE_40bits(tpii.getFrameUpscaleMinus1(), 40);  // u(40)
  READ_CODE(tpii.getFrameDownscale(), 6);              // u(6)
  READ_UVLC(tpii.getSubpatchCountMinus1());            // ue(v)
  READ_CODE(tpii.getSubpatchInterEnableFlag(), 1);     //u(1)
  if (asps.getAsveExtension().getAsveProjectionRawTextcoordPresentFlag()) {
    READ_CODE(tpii.getSubpatchRawEnableFlag(), 1);  //u(1)
  } else {
    tpii.getSubpatchRawEnableFlag() = 0;
  }
  for (int idx = 0; idx < tpii.getSubpatchCountMinus1() + 1; idx++) {
    if (tpii.getFaceIdPresentFlag()) {
      READ_UVLC(tpii.getFaceId2SubpatchIdx(idx));  // ue(v)
    } else tpii.getFaceId2SubpatchIdx(idx) = idx;
    if (tpii.getSubpatchInterEnableFlag()) {
      READ_CODE(tpii.getUpdateFlag(idx), 1);  //u(1)
    } else {
      tpii.getUpdateFlag(idx) = 0;
    }
    if (tpii.getSubpatchRawEnableFlag()) {
      READ_CODE(tpii.getSubpatchRawPresentFlag(idx), 1);  //u(1)
    } else {
      tpii.getSubpatchRawPresentFlag(idx) = 0;
    }
    if (tpii.getUpdateFlag(idx)) {
      subpatchInterInformation(tpii.getSubpatchInter(idx), asps, bitstream);
    } else if (tpii.getSubpatchRawPresentFlag(idx)) {
      subpatchRawInformation(tpii.getSubpatchRaw(idx), asps, bitstream);
    } else {
      subpatchInformation(tpii.getSubpatch(idx), asps, bitstream);
    }
  }

  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch inter information syntax
void
AtlasReader::subpatchInterInformation(SubpatchInterInformation&      sii,
                                      AtlasSequenceParameterSetRbsp& asps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_SVLC(sii.getSubpatchIdxDiff());  // ue(v)
  READ_SVLC(sii.get2dPosXDelta());      // se(v)
  READ_SVLC(sii.get2dPosYDelta());      // se(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    READ_SVLC(sii.getPosBiasXDelta());  // ue(v)
    READ_SVLC(sii.getPosBiasYDelta());  // ue(v)
    READ_SVLC(sii.get2dSizeXDelta());   // se(v)
    READ_SVLC(sii.get2dSizeYDelta());   // se(v)
  } else {
    sii.getPosBiasXDelta() = 0;
    sii.getPosBiasYDelta() = 0;
    sii.get2dSizeXDelta()  = 0;
    sii.get2dSizeYDelta()  = 0;
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Texture projection merge information syntax
void
AtlasReader::textureProjectionMergeInformation(
  TextureProjectionMergeInformation& tpmi,
  AtlasSequenceParameterSetRbsp&     asps,
  vmesh::Bitstream&                  bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(tpmi.getSubpatchMergePresentFlag(), 1);  // u(1)
  if (tpmi.getSubpatchMergePresentFlag()) {
    READ_UVLC(tpmi.getSubpatchCountMinus1());  // ue(v)
    for (int idx = 0; idx < tpmi.getSubpatchCountMinus1() + 1; idx++)
      subpatchMergeInformation(tpmi.getSubpatchMerge(idx), asps, bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.7.XX	Sub-patch merge information syntax
void
AtlasReader::subpatchMergeInformation(SubpatchMergeInformation&      smi,
                                      AtlasSequenceParameterSetRbsp& asps,
                                      vmesh::Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_UVLC(smi.getSubpatchIdx());  // ue(v)
  READ_SVLC(smi.get2dPosXDelta());  // se(v)
  READ_SVLC(smi.get2dPosYDelta());  // se(v)
  if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
    READ_SVLC(smi.getPosBiasXDelta());  // ue(v)
    READ_SVLC(smi.getPosBiasYDelta());  // ue(v)
    READ_SVLC(smi.get2dSizeXDelta());   // se(v)
    READ_SVLC(smi.get2dSizeYDelta());   // se(v)
  } else {
    smi.getPosBiasXDelta() = 0;
    smi.getPosBiasYDelta() = 0;
    smi.get2dSizeXDelta()  = 0;
    smi.get2dSizeYDelta()  = 0;
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.8 Supplemental enhancement information message syntax
void
AtlasReader::seiMessage(vmesh::Bitstream& bitstream,
                        AtlasBitstream&       atlas,
                        AtlasNalUnitType  nalUnitType,
                        PCCSEI&           sei) {
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
             atlas,
             nalUnitType,
             static_cast<SeiPayloadType>(payloadType),
             payloadSize,
             sei);
  // if ( static_cast<SeiPayloadType>( payloadType ) == DECODED_ATLAS_INFORMATION_HASH ) {
  //   syntax.getSeiHash().push_back(
  //       static_cast<SEIDecodedAtlasInformationHash&>( *( sei.getSeiSuffix().back().get() ) ) );
  // }
}

// D.2 Sample stream NAL unit syntax and semantics
// D.2.1 Sample stream NAL header syntax
void
AtlasReader::sampleStreamNalHeader(vmesh::Bitstream&    bitstream,
                                   SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  READ_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2  SEI payload syntax
// ISO/IEC 23002-5:F.2.1  General SEI message syntax
void
AtlasReader::seiPayload(vmesh::Bitstream& bitstream,
                        AtlasBitstream&       atlas,
                        AtlasNalUnitType  nalUnitType,
                        SeiPayloadType    payloadType,
                        size_t            payloadSize,
                        PCCSEI&           seiList) {
  TRACE_BITSTREAM_IN("%s", __func__);
  VpccSeiPayloadType vpccPayloadType;
  if (payloadType == VPCC_REGISTERED) {
    int32_t vpccPayloadTypeVal = 0;
    int32_t headerSize         = 0;
    int32_t byte               = 0;
    do {
      READ_CODE(byte, 8);  // u(8)
      vpccPayloadTypeVal += byte;
      headerSize++;
    } while (byte == 0xff);
    vpccPayloadType = static_cast<VpccSeiPayloadType>(vpccPayloadTypeVal);
    payloadSize -= headerSize;
  }
  VdmcSeiPayloadType vdmcPayloadType;
  if (payloadType == VDMC_REGISTERED) {
    int32_t vdmcPayloadTypeVal = 0;
    int32_t headerSize         = 0;
    int32_t byte               = 0;
    do {
      READ_CODE(byte, 8);  // u(8)
      vdmcPayloadTypeVal += byte;
      headerSize++;
    } while (byte == 0xff);
    vdmcPayloadType = static_cast<VdmcSeiPayloadType>(vdmcPayloadTypeVal);
    payloadSize -= headerSize;
  }
  SEI& sei = payloadType == VPCC_REGISTERED
               ? seiList.addSei(nalUnitType, vpccPayloadType)
             : payloadType == VDMC_REGISTERED
               ? seiList.addSei(nalUnitType, vdmcPayloadType)
               : seiList.addSei(nalUnitType, payloadType);
  printf("        seiMessage: type = %d %s payloadSize = %zu \n",
         payloadType,
         toString(payloadType).c_str(),
         payloadSize);
  fflush(stdout);
  if (nalUnitType == ATLAS_NAL_PREFIX_ESEI
      || nalUnitType == ATLAS_NAL_PREFIX_NSEI) {
    if (payloadType == BUFFERING_PERIOD) {  // 0
      bufferingPeriod(bitstream, sei);
    } else if (payloadType == ATLAS_FRAME_TIMING) {  // 1
      assert(seiList.seiIsPresent(ATLAS_NAL_PREFIX_NSEI, BUFFERING_PERIOD));
      auto& bpsei =
        *seiList.getLastSei(ATLAS_NAL_PREFIX_NSEI, BUFFERING_PERIOD);
      atlasFrameTiming(bitstream, sei, bpsei, false);
    } else if (payloadType == FILLER_PAYLOAD) {  // 2
      fillerPayload(bitstream, sei, payloadSize);
    } else if (payloadType == USER_DATAREGISTERED_ITUTT35) {  // 3
      userDataRegisteredItuTT35(bitstream, sei, payloadSize);
    } else if (payloadType == USER_DATA_UNREGISTERED) {  // 4
      userDataUnregistered(bitstream, sei, payloadSize);
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
      vpccRegisteredSEI(bitstream, sei, vpccPayloadType, payloadSize);
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
      vdmcRegisteredSEI(bitstream, sei, vdmcPayloadType, payloadSize);
    } else {
      reservedMessage(bitstream, sei, payloadSize);
    }
  } else {
    if (payloadType == FILLER_PAYLOAD) {  // 2
      fillerPayload(bitstream, sei, payloadSize);
    } else if (payloadType == USER_DATAREGISTERED_ITUTT35) {  // 3
      userDataRegisteredItuTT35(bitstream, sei, payloadSize);
    } else if (payloadType == USER_DATA_UNREGISTERED) {  // 4
      userDataUnregistered(bitstream, sei, payloadSize);
    } else if (payloadType == DECODED_ATLAS_INFORMATION_HASH) {  // 19
      decodedAtlasInformationHash(bitstream, sei);
    } else if (payloadType == VPCC_REGISTERED) {  // 68
      vpccRegisteredSEI(bitstream, sei, vpccPayloadType, payloadSize);
    } else if (payloadType == GEOMETRY_ASSISTANCE) {  // 133
      //geometryAssitance(bitstream, sei);
    } else if (payloadType == EXTENDED_GEOMETRY_ASSISTANCE) {  // 134
      //extendedGeometryAssitance(bitstream, sei);
    } else if (payloadType == MIV_REGISTERED) {  // 135
      //mivRegisteredSEI(bitstream, sei);
    } else if (payloadType == VDMC_REGISTERED) {  // 192
      vdmcRegisteredSEI(bitstream, sei, vdmcPayloadType, payloadSize);
    } else {
      reservedMessage(bitstream, sei, payloadSize);
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

// ISO/IEC 23002-7  Filler payload SEI message syntax
void
AtlasReader::fillerPayload(vmesh::Bitstream& bitstream,
                           SEI&              sei,
                           size_t            payloadSize) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t code = 0xFF;
  for (size_t k = 0; k < payloadSize; k++) {
    READ_CODE(code, 8);  // f(8) equal to 0xFF
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  User data registered by Recommendation ITU-T T.35 SEI message syntax
void
AtlasReader::userDataRegisteredItuTT35(vmesh::Bitstream& bitstream,
                                       SEI&              seiAbstract,
                                       size_t            payloadSize) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIUserDataRegisteredItuTT35&>(seiAbstract);
  READ_CODE(sei.getCountryCode(), 8);  // b(8)
  payloadSize--;
  if (sei.getCountryCode() == 0xFF) {
    READ_CODE(sei.getCountryCodeExtensionByte(), 8);  // b(8)
    payloadSize--;
  }
  auto& payload = sei.getPayloadByte();
  payload.resize(payloadSize);
  for (auto& element : payload) {
    READ_CODE(element, 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  User data unregistered SEI message syntax
void
AtlasReader::userDataUnregistered(vmesh::Bitstream& bitstream,
                                  SEI&              seiAbstract,
                                  size_t            payloadSize) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIUserDataUnregistered&>(seiAbstract);
  for (size_t i = 0; i < 16; i++) {
    READ_CODE(sei.getUuidIsoIec11578(i), 8);  // u(128) <=> 16 * u(8)
  }
  payloadSize -= 16;
  sei.getUserDataPayloadByte().resize(payloadSize);
  for (size_t i = 0; i < payloadSize; i++) {
    READ_CODE(sei.getUserDataPayloadByte(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.2  Recovery point SEI message syntax
void
AtlasReader::recoveryPoint(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIRecoveryPoint&>(seiAbstract);
  READ_SVLC(sei.getRecoveryAfocCnt());    // se(v)
  READ_CODE(sei.getExactMatchFlag(), 1);  // u(1)
  READ_CODE(sei.getBrokenLinkFlag(), 1);  // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.3  No reconstruction SEI message syntax
void
AtlasReader::noReconstruction(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-7  Reserved SEI message syntax
void
AtlasReader::reservedMessage(vmesh::Bitstream& bitstream,
                             SEI&              seiAbstract,
                             size_t            payloadSize) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIReservedMessage&>(seiAbstract);
  sei.getPayloadByte().resize(payloadSize);
  for (size_t i = 0; i < payloadSize; i++) {
    READ_CODE(sei.getPayloadByte(i), 8);  // b(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.4  SEI manifest SEI message syntax
void
AtlasReader::seiManifest(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIManifest&>(seiAbstract);
  READ_CODE(sei.getNumSeiMsgTypes(), 16);  // u(16)
  sei.allocate();
  for (size_t i = 0; i < sei.getNumSeiMsgTypes(); i++) {
    READ_CODE(sei.getSeiPayloadType(i), 16);  // u(16)
    READ_CODE(sei.getSeiDescription(i), 8);   // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.5  SEI prefix indication SEI message syntax
void
AtlasReader::seiPrefixIndication(vmesh::Bitstream& bitstream,
                                 SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIPrefixIndication&>(seiAbstract);
  READ_CODE(sei.getPrefixSeiPayloadType(), 16);          // u(16)
  READ_CODE(sei.getNumSeiPrefixIndicationsMinus1(), 8);  // u(8)
  sei.getNumBitsInPrefixIndicationMinus1().resize(
    sei.getNumSeiPrefixIndicationsMinus1() + 1, 0);
  sei.getSeiPrefixDataBit().resize(sei.getNumSeiPrefixIndicationsMinus1() + 1);
  for (size_t i = 0; i <= sei.getNumSeiPrefixIndicationsMinus1(); i++) {
    READ_CODE(sei.getNumBitsInPrefixIndicationMinus1(i), 16);  // u(16)
    sei.getSeiPrefixDataBit(i).resize(
      sei.getNumBitsInPrefixIndicationMinus1(i), false);
    for (size_t j = 0; j <= sei.getNumBitsInPrefixIndicationMinus1(i); j++) {
      READ_CODE(sei.getSeiPrefixDataBit(i, j), 1);  // u(1)
    }
    while (!bitstream.byteAligned()) {
      uint32_t one = 0;
      READ_CODE(one, 1);  // f(1): equal to 1
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.6  Active substreams SEI message syntax
void
AtlasReader::activeSubBitstreams(vmesh::Bitstream& bitstream,
                                 SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIActiveSubBitstreams&>(seiAbstract);
  READ_CODE(sei.getActiveSubBitstreamsCancelFlag(), 1);  // u(1)
  if (!sei.getActiveSubBitstreamsCancelFlag()) {
    READ_CODE(sei.getActiveAttributesChangesFlag(), 1);    // u(1)
    READ_CODE(sei.getActiveMapsChangesFlag(), 1);          // u(1)
    READ_CODE(sei.getAuxiliarySubstreamsActiveFlag(), 1);  // u(1)
    if (sei.getActiveAttributesChangesFlag()) {
      READ_CODE(sei.getAllAttributesActiveFlag(), 1);  // u(1)
      if (!sei.getAllAttributesActiveFlag()) {
        READ_CODE(sei.getActiveAttributeCountMinus1(), 7);  // u(7)
        sei.getActiveAttributeIdx().resize(
          sei.getActiveAttributeCountMinus1() + 1, 0);
        for (size_t i = 0; i <= sei.getActiveAttributeCountMinus1(); i++) {
          READ_CODE(sei.getActiveAttributeIdx(i), 7);  // u(7)
        }
      }
    }
    if (sei.getActiveMapsChangesFlag()) {
      READ_CODE(sei.getAllMapsActiveFlag(), 1);  // u(1)
      if (!sei.getAllMapsActiveFlag()) {
        READ_CODE(sei.getActiveMapCountMinus1(), 4);  // u(4)
        sei.getActiveMapIdx().resize(sei.getActiveMapCountMinus1() + 1, 0);
        for (size_t i = 0; i <= sei.getActiveMapCountMinus1(); i++) {
          READ_CODE(sei.getActiveMapIdx(i), 4);  // u(4)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.7  Component codec mapping SEI message syntax
void
AtlasReader::componentCodecMapping(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIComponentCodecMapping&>(seiAbstract);
  READ_CODE(sei.getComponentCodecCancelFlag(), 1);  // u(1)
  if (!sei.getComponentCodecCancelFlag()) {
    READ_CODE(sei.getCodecMappingsCountMinus1(), 8);  // u(8)
    sei.allocate();
    for (size_t i = 0; i <= sei.getCodecMappingsCountMinus1(); i++) {
      READ_CODE(sei.getCodecId(i), 8);                  // u(8)
      READ_STRING(sei.getCodec4cc(sei.getCodecId(i)));  // st(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.8  Volumetric annotation SEI message family syntax
// ISO/IEC 23002-5:F.2.8.1 Scene object information SEI message syntax
void
AtlasReader::sceneObjectInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISceneObjectInformation&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  READ_CODE(sei.getResetFlag(), 1);        // u(1)
  READ_UVLC(sei.getNumObjectUpdates());    // ue(v)
  sei.allocateObjectIdx();
  if (sei.getNumObjectUpdates() > 0) {
    READ_CODE(sei.getSimpleObjectsFlag(), 1);  // u(1)
    if (static_cast<int>(sei.getSimpleObjectsFlag()) == 0) {
      READ_CODE(sei.getObjectLabelPresentFlag(), 1);       // u(1)
      READ_CODE(sei.getPriorityPresentFlag(), 1);          // u(1)
      READ_CODE(sei.getObjectHiddenPresentFlag(), 1);      // u(1)
      READ_CODE(sei.getObjectDependencyPresentFlag(), 1);  // u(1)
      READ_CODE(sei.getVisibilityConesPresentFlag(), 1);   // u(1)
      READ_CODE(sei.get3dBoundingBoxPresentFlag(), 1);     // u(1)
      READ_CODE(sei.getCollisionShapePresentFlag(), 1);    // u(1)
      READ_CODE(sei.getPointStylePresentFlag(), 1);        // u(1)
      READ_CODE(sei.getMaterialIdPresentFlag(), 1);        // u(1)
      READ_CODE(sei.getExtensionPresentFlag(), 1);         // u(1)
    } else {
      sei.getObjectLabelPresentFlag()      = false;
      sei.getPriorityPresentFlag()         = false;
      sei.getObjectHiddenPresentFlag()     = false;
      sei.getObjectDependencyPresentFlag() = false;
      sei.getVisibilityConesPresentFlag()  = false;
      sei.get3dBoundingBoxPresentFlag()    = false;
      sei.getCollisionShapePresentFlag()   = false;
      sei.getPointStylePresentFlag()       = false;
      sei.getMaterialIdPresentFlag()       = false;
      sei.getExtensionPresentFlag()        = false;
    }
    if (sei.get3dBoundingBoxPresentFlag()) {
      READ_CODE(sei.get3dBoundingBoxScaleLog2(), 5);        // u(5)
      READ_CODE(sei.get3dBoundingBoxPrecisionMinus8(), 5);  // u(5)
    }
    READ_CODE(sei.getLog2MaxObjectIdxUpdated(), 5);  // u(5)
    if (sei.getObjectDependencyPresentFlag()) {
      READ_CODE(sei.getLog2MaxObjectDependencyIdx(), 5);  // u(5)
    }
    for (size_t i = 0; i <= sei.getNumObjectUpdates(); i++) {
      assert(sei.getObjectIdx().size() >= sei.getNumObjectUpdates());
      READ_CODE(sei.getObjectIdx(i),
                sei.getLog2MaxObjectIdxUpdated());  // u(v)
      size_t k = sei.getObjectIdx(i);
      sei.allocate(k + 1);
      READ_CODE(sei.getObjectCancelFlag(k), 1);  // u(1)
      if (sei.getObjectCancelFlag(k)) {
        if (sei.getObjectLabelPresentFlag()) {
          READ_CODE(sei.getObjectLabelUpdateFlag(k), 1);  // u(1)
          if (sei.getObjectLabelUpdateFlag(k)) {
            READ_UVLC(sei.getObjectLabelIdx(k));  // ue(v)
          }
        }
        if (sei.getPriorityPresentFlag()) {
          READ_CODE(sei.getPriorityUpdateFlag(k), 1);  // u(1)
          if (sei.getPriorityUpdateFlag(k)) {
            READ_CODE(sei.getPriorityValue(k), 4);  // u(4)
          }
        }
        if (sei.getObjectHiddenPresentFlag()) {
          READ_CODE(sei.getObjectHiddenFlag(k), 1);  // u(1)
        }
        if (sei.getObjectDependencyPresentFlag()) {
          READ_CODE(sei.getObjectDependencyUpdateFlag(k), 1);  // u(1)
          if (sei.getObjectDependencyUpdateFlag(k)) {
            READ_CODE(sei.getObjectNumDependencies(k), 4);  // u(4)
            sei.allocateObjectNumDependencies(k,
                                              sei.getObjectNumDependencies(k));
            size_t bitCount =
              ceil(log2(sei.getObjectNumDependencies(k)) + 0.5);
            for (size_t j = 0; j < sei.getObjectNumDependencies(k); j++) {
              READ_CODE(sei.getObjectDependencyIdx(k, j), bitCount);  // u(v)
            }
          }
        }
        if (sei.getVisibilityConesPresentFlag()) {
          READ_CODE(sei.getVisibilityConesUpdateFlag(k), 1);  // u(1)
          if (sei.getVisibilityConesUpdateFlag(k)) {
            READ_CODE(sei.getDirectionX(k), 16);  // u(16)
            READ_CODE(sei.getDirectionY(k), 16);  // u(16)
            READ_CODE(sei.getDirectionZ(k), 16);  // u(16)
            READ_CODE(sei.getAngle(k), 16);       // u(16)
          }
        }  // cones

        if (sei.get3dBoundingBoxPresentFlag()) {
          READ_CODE(sei.get3dBoundingBoxUpdateFlag(k), 1);  // u(1)
          if (sei.get3dBoundingBoxUpdateFlag(k)) {
            READ_UVLC(sei.get3dBoundingBoxX(k));       // ue(v)
            READ_UVLC(sei.get3dBoundingBoxY(k));       // ue(v)
            READ_UVLC(sei.get3dBoundingBoxZ(k));       // ue(v)
            READ_UVLC(sei.get3dBoundingBoxDeltaX(k));  // ue(v)
            READ_UVLC(sei.get3dBoundingBoxDeltaY(k));  // ue(v)
            READ_UVLC(sei.get3dBoundingBoxDeltaZ(k));  // ue(v)
          }
        }  // 3dBB

        if (sei.getCollisionShapePresentFlag()) {
          READ_CODE(sei.getCollisionShapeUpdateFlag(k), 1);  // u(1)
          if (sei.getCollisionShapeUpdateFlag(k)) {
            READ_CODE(sei.getCollisionShapeId(k), 16);  // u(16)
          }
        }  // collision
        if (sei.getPointStylePresentFlag()) {
          READ_CODE(sei.getPointStyleUpdateFlag(k), 1);  // u(1)
          if (sei.getPointStyleUpdateFlag(k)) {
            READ_CODE(sei.getPointShapeId(k), 8);  // u(8)
            READ_CODE(sei.getPointSize(k), 16);    // u(16)
          }
        }  // pointstyle
        if (sei.getMaterialIdPresentFlag()) {
          READ_CODE(sei.getMaterialIdUpdateFlag(k), 1);  // u(1)
          if (sei.getMaterialIdUpdateFlag(k)) {
            READ_CODE(sei.getMaterialId(k), 16);  // u(16)
          }
        }  // materialid
      }    // sei.getObjectCancelFlag(k)
    }      // for(size_t i=0; i<=sei.getNumObjectUpdates(); i++)
  }        // if( sei.getNumObjectUpdates() > 0 )
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.8.2 Object label information SEI message syntax
void
AtlasReader::objectLabelInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    sei  = static_cast<SEIObjectLabelInformation&>(seiAbstract);
  uint32_t zero = 0;
  READ_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    READ_CODE(sei.getLabelLanguagePresentFlag(), 1);  // u(1)
    if (sei.getLabelLanguagePresentFlag()) {
      while (!bitstream.byteAligned()) {
        READ_CODE(zero, 1);  // u(1)
      }
      READ_STRING(sei.getLabelLanguage());  // st(v)
    }
    READ_UVLC(sei.getNumLabelUpdates());  // ue(v)
    sei.allocate();
    for (size_t i = 0; i < sei.getNumLabelUpdates(); i++) {
      READ_UVLC(sei.getLabelIdx(i));           // ue(v)
      READ_CODE(sei.getLabelCancelFlag(), 1);  // u(1)
      if (!sei.getLabelCancelFlag()) {
        while (!bitstream.byteAligned()) {
          READ_CODE(zero, 1);  // u(1)
        }
        READ_STRING(sei.getLabel(sei.getLabelIdx(i)));  // st(v)
      }
    }
    READ_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
};

// ISO/IEC 23002-5:F.2.8.3 Patch information SEI message syntax
void
AtlasReader::patchInformation(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIPatchInformation&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);  // u(1)
  READ_CODE(sei.getResetFlag(), 1);        // u(1)
  READ_UVLC(sei.getNumTileUpdates());      // ue(v)
  if (sei.getNumTileUpdates() > 0) {
    READ_CODE(sei.getLog2MaxObjectIdxTracked(), 5);  // u(5)
    READ_CODE(sei.getLog2MaxPatchIdxUpdated(), 4);   // u(4)
  }
  for (size_t i = 0; i < sei.getNumTileUpdates(); i++) {
    READ_UVLC(sei.getTileId(i));  // ue(v)
    size_t j = sei.getTileId(i);
    READ_CODE(sei.getTileCancelFlag(j), 1);  // u(1)
    READ_UVLC(sei.getNumPatchUpdates(j));    // ue(v)
    for (size_t k = 0; k < sei.getNumPatchUpdates(j); k++) {
      READ_CODE(sei.getPatchIdx(j, k),
                sei.getLog2MaxPatchIdxUpdated());  // u(v)
      auto p = sei.getPatchIdx(j, k);
      READ_CODE(sei.getPatchCancelFlag(j, p), 1);  // u(1)
      if (!sei.getPatchCancelFlag(j, p)) {
        READ_UVLC(sei.getPatchNumberOfObjectsMinus1(j, p));  // ue(v)
        for (size_t n = 0; n < sei.getPatchNumberOfObjectsMinus1(j, p) + 1;
             n++) {
          READ_CODE(sei.getPatchObjectIdx(j, p, n),
                    sei.getLog2MaxObjectIdxTracked());  // u(v)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
};

// ISO/IEC 23002-5:F.2.8.4 Volumetric rectangle information SEI message syntax
void
AtlasReader::volumetricRectangleInformation(vmesh::Bitstream& bitstream,
                                            SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIVolumetricRectangleInformation&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);    // u(1)
  READ_CODE(sei.getResetFlag(), 1);          // u(1)
  READ_UVLC(sei.getNumRectanglesUpdates());  // ue(v)
  if (sei.getNumRectanglesUpdates() > 0) {
    READ_CODE(sei.getLog2MaxObjectIdxTracked(), 5);     // u(5)
    READ_CODE(sei.getLog2MaxRectangleIdxUpdated(), 4);  // u(4)
  }
  for (size_t k = 0; k < sei.getNumRectanglesUpdates(); k++) {
    READ_CODE(sei.getRectangleIdx(k),
              sei.getLog2MaxRectangleIdxUpdated());  // u(v)
    auto p = sei.getRectangleIdx(k);
    READ_CODE(sei.getRectangleCancelFlag(p), 1);  // u(1)
    if (!sei.getRectangleCancelFlag(p)) {
      sei.allocate(p + 1);
      READ_CODE(sei.getBoundingBoxUpdateFlag(p), 1);  // u(1)
      if (sei.getBoundingBoxUpdateFlag(p)) {
        READ_UVLC(sei.getBoundingBoxTop(p));     // ue(v)
        READ_UVLC(sei.getBoundingBoxLeft(p));    // ue(v)
        READ_UVLC(sei.getBoundingBoxWidth(p));   // ue(v)
        READ_UVLC(sei.getBoundingBoxHeight(p));  // ue(v)
      }
      READ_UVLC(sei.getRectangleNumberOfObjectsMinus1(p));  // ue(v)
      sei.allocateRectangleObjectIdx(
        p, sei.getRectangleNumberOfObjectsMinus1(p) + 1);
      for (size_t n = 0; n < sei.getRectangleNumberOfObjectsMinus1(p) + 1;
           n++) {
        READ_CODE(sei.getRectangleObjectIdx(p, n),
                  sei.getLog2MaxObjectIdxTracked());  // u(v)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
};

// ISO/IEC 23002-5:F.2.8.5 Atlas object information  SEI message syntax
void
AtlasReader::atlasObjectInformation(vmesh::Bitstream& bitstream,
                                    SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAtlasObjectInformation&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);   // u(1)
  READ_CODE(sei.getResetFlag(), 1);         // u(1)
  READ_CODE(sei.getNumAtlasesMinus1(), 6);  // u(6)
  READ_UVLC(sei.getNumUpdates());           // ue(v)
  sei.allocateAltasId();
  sei.allocateObjectIdx();
  if (sei.getNumUpdates() > 0) {
    READ_CODE(sei.getLog2MaxObjectIdxTracked(), 5);  // u(5)
    for (size_t i = 0; i < sei.getNumAtlasesMinus1() + 1; i++) {
      READ_CODE(sei.getAtlasId(i), 5);  // u(6)
    }
    for (size_t i = 0; i < sei.getNumUpdates() + 1; i++) {
      READ_CODE(sei.getObjectIdx(i),
                sei.getLog2MaxObjectIdxTracked());  // u(v)
      for (size_t j = 0; j < sei.getNumAtlasesMinus1() + 1; j++) {
        READ_CODE(sei.getObjectInAtlasPresentFlag(i, j), 1);  // u(1)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.9  Buffering period SEI message syntax
void
AtlasReader::bufferingPeriod(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIBufferingPeriod&>(seiAbstract);
  READ_CODE(sei.getNalHrdParamsPresentFlag(), 1);             // u(1)
  READ_CODE(sei.getAclHrdParamsPresentFlag(), 1);             // u(1)
  READ_CODE(sei.getInitialCabRemovalDelayLengthMinus1(), 5);  // u(5)
  READ_CODE(sei.getAuCabRemovalDelayLengthMinus1(), 5);       // u(5)
  READ_CODE(sei.getDabOutputDelayLengthMinus1(), 5);          // u(5)
  READ_CODE(sei.getIrapCabParamsPresentFlag(), 1);            // u(1)
  if (sei.getIrapCabParamsPresentFlag()) {
    READ_CODE(sei.getCabDelayOffset(),
              sei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
    READ_CODE(sei.getDabDelayOffset(),
              sei.getDabOutputDelayLengthMinus1() + 1);  // u(v)
  }
  READ_CODE(sei.getConcatenationFlag(), 1);  // u(1)
  READ_CODE(sei.getAtlasCabRemovalDelayDeltaMinus1(),
            sei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
  READ_CODE(sei.getMaxSubLayersMinus1(), 3);              // u(3)
  sei.allocate();
  int32_t bitCount = sei.getInitialCabRemovalDelayLengthMinus1() + 1;
  for (size_t i = 0; i <= sei.getMaxSubLayersMinus1(); i++) {
    READ_CODE(sei.getHrdCabCntMinus1(i), 3);  // u(3)
    if (sei.getNalHrdParamsPresentFlag()) {
      for (size_t j = 0; j < sei.getHrdCabCntMinus1(i) + 1; j++) {
        READ_CODE(sei.getNalInitialCabRemovalDelay(i, j), bitCount);   // u(v)
        READ_CODE(sei.getNalInitialCabRemovalOffset(i, j), bitCount);  // u(v)
        if (sei.getIrapCabParamsPresentFlag()) {
          READ_CODE(sei.getNalInitialAltCabRemovalDelay(i, j),
                    bitCount);  // u(v)
          READ_CODE(sei.getNalInitialAltCabRemovalOffset(i, j),
                    bitCount);  // u(v)
        }
      }
    }
    if (sei.getAclHrdParamsPresentFlag()) {
      for (size_t j = 0; j < sei.getHrdCabCntMinus1(i) + 1; j++) {
        READ_CODE(sei.getAclInitialCabRemovalDelay(i, j), bitCount);   // u(v)
        READ_CODE(sei.getAclInitialCabRemovalOffset(i, j), bitCount);  // u(v)
        if (sei.getIrapCabParamsPresentFlag()) {
          READ_CODE(sei.getAclInitialAltCabRemovalDelay(i, j),
                    bitCount);  // u(v)
          READ_CODE(sei.getAclInitialAltCabRemovalOffset(i, j),
                    bitCount);  // u(v)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.10  Atlas frame timing SEI message syntax
void
AtlasReader::atlasFrameTiming(vmesh::Bitstream& bitstream,
                              SEI&              seiAbstract,
                              SEI&              seiBufferingPeriodAbstract,
                              bool              cabDabDelaysPresentFlag) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei   = static_cast<SEIAtlasFrameTiming&>(seiAbstract);
  auto& bpsei = static_cast<SEIBufferingPeriod&>(seiBufferingPeriodAbstract);
  if (cabDabDelaysPresentFlag) {
    for (uint32_t i = 0; i <= bpsei.getMaxSubLayersMinus1(); i++) {
      READ_CODE(sei.getAftCabRemovalDelayMinus1(i),
                bpsei.getAuCabRemovalDelayLengthMinus1() + 1);  // u(v)
      READ_CODE(sei.getAftDabOutputDelay(i),
                bpsei.getDabOutputDelayLengthMinus1() + 1);  // u(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.11   Viewport SEI messages family syntax
// ISO/IEC 23002-5:F.2.11.1 Viewport camera parameters SEI messages syntax
void
AtlasReader::viewportCameraParameters(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIViewportCameraParameters&>(seiAbstract);
  READ_CODE(sei.getCameraId(), 10);   // u(10)
  READ_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (sei.getCameraId() > 0 && !sei.getCancelFlag()) {
    READ_CODE(sei.getPersistenceFlag(), 1);              // u(1)
    READ_CODE(sei.getCameraType(), 3);                   // u(3)
    if (sei.getCameraType() == 0) {                      // equirectangular
      READ_CODE(sei.getErpHorizontalFov(), 32);          // u(32)
      READ_CODE(sei.getErpVerticalFov(), 32);            // u(32)
    } else if (sei.getCameraType() == 1) {               // perspective
      READ_FLOAT(sei.getPerspectiveAspectRatio());       // fl(32)
      READ_CODE(sei.getPerspectiveHorizontalFov(), 32);  // u(32)
    } else if (sei.getCameraType() == 2) {               /* orthographic */
      READ_FLOAT(sei.getOrthoAspectRatio());             // fl(32)
      READ_FLOAT(sei.getOrthoHorizontalSize());          // fl(32)
    }
    READ_FLOAT(sei.getClippingNearPlane());  // fl(32)
    READ_FLOAT(sei.getClippingFarPlane());   // fl(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.11.2 Viewport position SEI messages syntax
void
AtlasReader::viewportPosition(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIViewportPosition&>(seiAbstract);
  READ_UVLC(sei.getViewportId());                      // ue(v)
  READ_CODE(sei.getCameraParametersPresentFlag(), 1);  // u(1)
  if (sei.getCameraParametersPresentFlag()) {
    READ_CODE(sei.getViewportId(), 10);  // u(10)
  }
  READ_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    READ_CODE(sei.getPersistenceFlag(), 1);  // u(1)
    for (size_t d = 0; d < 3; d++) {
      READ_FLOAT(sei.getPosition(d));  // fl(32)
    }
    READ_CODE(sei.getRotationQX(), 16);     // i(16)
    READ_CODE(sei.getRotationQY(), 16);     // i(16)
    READ_CODE(sei.getRotationQZ(), 16);     // i(16)
    READ_CODE(sei.getCenterViewFlag(), 1);  //  u(1)
    if (!sei.getCenterViewFlag()) {
      READ_CODE(sei.getLeftViewFlag(), 1);  // u(1)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12 Decoded Atlas Information Hash SEI message syntax
void
AtlasReader::decodedAtlasInformationHash(vmesh::Bitstream& bitstream,
                                         SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    sei  = static_cast<SEIDecodedAtlasInformationHash&>(seiAbstract);
  uint32_t zero = 0;
  READ_CODE(sei.getCancelFlag(), 1);  // u(1)
  if (!sei.getCancelFlag()) {
    READ_CODE(sei.getPersistenceFlag(), 1);                      // u(1)
    READ_CODE(sei.getHashType(), 8);                             // u(8)
    READ_CODE(sei.getDecodedHighLevelHashPresentFlag(), 1);      // u(1)
    READ_CODE(sei.getDecodedAtlasHashPresentFlag(), 1);          // u(1)
    READ_CODE(sei.getDecodedAtlasB2pHashPresentFlag(), 1);       // u(1)
    READ_CODE(sei.getDecodedAtlasTilesHashPresentFlag(), 1);     // u(1)
    READ_CODE(sei.getDecodedAtlasTilesB2pHashPresentFlag(), 1);  // u(1)
    READ_CODE(zero, 1);                                          // u(1)
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
      READ_UVLC(sei.getNumTilesMinus1());   // ue(v)
      READ_UVLC(sei.getTileIdLenMinus1());  // ue(v)
      sei.allocateAtlasTilesHash(sei.getNumTilesMinus1() + 1);
      for (size_t t = 0; t <= sei.getNumTilesMinus1(); t++) {
        READ_CODE(sei.getTileId(t), sei.getTileIdLenMinus1() + 1);  // u(v)
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
AtlasReader::decodedHighLevelHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      READ_CODE(sei.getHighLevelMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    READ_CODE(sei.getHighLevelCrc(), 16);  // u(16)
  } else if (hType == 2) {
    READ_CODE(sei.getHighLevelCheckSum(), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.2 Decoded atlas hash unit syntax
void
AtlasReader::decodedAtlasHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      READ_CODE(sei.getAtlasMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    READ_CODE(sei.getAtlasCrc(), 16);  // u(16)
  } else if (hType == 2) {
    READ_CODE(sei.getAtlasCheckSum(), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.3 Decoded atlas b2p hash unit syntax
void
AtlasReader::decodedAtlasB2pHash(vmesh::Bitstream& bitstream, SEI& seiAbs) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      READ_CODE(sei.getAtlasB2pMd5(i), 8);  // b(8)
    }
  } else if (hType == 1) {
    READ_CODE(sei.getAtlasB2pCrc(), 16);  // u(16)
  } else if (hType == 2) {
    READ_CODE(sei.getAtlasB2pCheckSum(), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.4 Decoded atlas tile hash unit syntax
void
AtlasReader::decodedAtlasTilesHash(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbs,
                                   size_t            id) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      READ_CODE(sei.getAtlasTilesMd5(id, i), 8);  // b(8)
    }
  } else if (hType == 1) {
    READ_CODE(sei.getAtlasTilesCrc(id), 16);  // u(16)
  } else if (hType == 2) {
    READ_CODE(sei.getAtlasTilesCheckSum(id), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.12.5 Decoded atlas tile b2p hash unit syntax
void
AtlasReader::decodedAtlasTilesB2pHash(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbs,
                                      size_t            id) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&   sei   = static_cast<SEIDecodedAtlasInformationHash&>(seiAbs);
  uint8_t hType = sei.getHashType();
  if (hType == 0) {
    for (size_t i = 0; i < 16; i++) {
      READ_CODE(sei.getAtlasTilesB2pMd5(id, i), 8);  // b(8)
    }
  } else if (hType == 1) {
    READ_CODE(sei.getAtlasTilesB2pCrc(id), 16);  // u(16)
  } else if (hType == 2) {
    READ_CODE(sei.getAtlasTilesB2pCheckSum(id), 32);  // u(32)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:F.2.13 Time code SEI message syntax
void
AtlasReader::timeCode(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEITimeCode&>(seiAbstract);
  READ_CODE(sei.getNumUnitsInTick(), 32);    // u(32)
  READ_CODE(sei.getTimeScale(), 32);         // u(32)
  READ_CODE(sei.getCountingType(), 5);       // u(5)
  READ_CODE(sei.getFullTimestampFlag(), 1);  // u(1)
  READ_CODE(sei.getDiscontinuityFlag(), 1);  // u(1)
  READ_CODE(sei.getCntDroppedFlag(), 1);     // u(1)
  READ_CODE(sei.getNFrames(), 9);            // u(9)
  if (sei.getFullTimestampFlag()) {
    READ_CODE(sei.getSecondsValue(), 6);  // u(6)
    READ_CODE(sei.getMinutesValue(), 6);  // u(6)
    READ_CODE(sei.getHoursValue(), 5);    // u(5)
  } else {
    READ_CODE(sei.getSecondFlag(), 1);  // u(1)
    if (sei.getSecondFlag()) {
      READ_CODE(sei.getSecondsValue(), 6);  // u(6)
      READ_CODE(sei.getMinutesFlag(), 1);   // u(1)
      if (sei.getMinutesFlag()) {
        READ_CODE(sei.getMinutesValue(), 6);  // u(6)
        READ_CODE(sei.getHoursFlag(), 1);     // u(1)
        if (sei.getHoursFlag()) {
          READ_CODE(sei.getHoursValue(), 5);  // u(5)
        }
      }
    }
  }
  READ_CODE(sei.getTimeOffsetLength(), 5);  // u(5)
  if (sei.getTimeOffsetLength() > 0) {
    READ_CODES(sei.getTimeOffsetValue(), sei.getTimeOffsetLength());  // i(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.13 Attribute transformation parameters SEI message syntax
void
AtlasReader::attributeTransformationParams(vmesh::Bitstream& bitstream,
                                           SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeTransformationParams&>(seiAbstract);
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

// ISO/IEC 23002-5:H.20.2.14 Occupancy synthesis SEI message syntax
void
AtlasReader::occupancySynthesis(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIOccupancySynthesis&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);   // u(1)
  READ_CODE(sei.getResetFlag(), 1);         // u(1)
  READ_CODE(sei.getInstancesUpdated(), 8);  // u(8)
  sei.allocate();
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    READ_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    READ_CODE(sei.getInstanceCancelFlag(k), 1);  // u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      READ_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        READ_CODE(sei.getPbfLog2ThresholdMinus1(k), 2);  // u(2)
        READ_CODE(sei.getPbfPassesCountMinus1(k), 2);    // u(2)
        READ_CODE(sei.getPbfFilterSizeMinus1(k), 3);     // u(3)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.15 Geometry smoothing SEI message syntax
void
AtlasReader::geometrySmoothing(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIGeometrySmoothing&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);   // u(1)
  READ_CODE(sei.getResetFlag(), 1);         // u(1)
  READ_CODE(sei.getInstancesUpdated(), 8);  // u(8)
  sei.allocate();
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    READ_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    READ_CODE(sei.getInstanceCancelFlag(k), 1);  // u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      READ_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        READ_CODE(sei.getFilterEomPointsFlag(k), 1);  // u(1)
        READ_CODE(sei.getGridSizeMinus2(k), 7);       // u(7)
        READ_CODE(sei.getThreshold(k), 8);            // u(8)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.16 Attribute smoothing SEI message syntax
void
AtlasReader::attributeSmoothing(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeSmoothing&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);    // u(1)
  READ_CODE(sei.getResetFlag(), 1);          // u(1)
  READ_UVLC(sei.getNumAttributesUpdated());  // ue(v)
  sei.allocate();
  for (size_t j = 0; j < sei.getNumAttributesUpdated(); j++) {
    READ_CODE(sei.getAttributeIdx(j), 7);  // u(7)
    size_t k = sei.getAttributeIdx(j);
    READ_CODE(sei.getAttributeSmoothingCancelFlag(k), 1);  // u(1)
    READ_CODE(sei.getInstancesUpdated(k), 8);              // u(8)
    for (size_t i = 0; i < sei.getInstancesUpdated(k); i++) {
      size_t m = 0;
      READ_CODE(m, 8);  // u(8)
      sei.allocate(k + 1, m + 1);
      sei.getInstanceIndex(k, i) = m;
      READ_CODE(sei.getInstanceCancelFlag(k, m), 1);  // u(1)
      if (sei.getInstanceCancelFlag(k, m) != 1) {
        READ_UVLC(sei.getMethodType(k, m));  // ue(v)
        if (sei.getMethodType(k, m)) {
          READ_CODE(sei.getFilterEomPointsFlag(k, m), 1);  // u(1)
          READ_CODE(sei.getGridSizeMinus2(k, m), 5);       // u(5)
          READ_CODE(sei.getThreshold(k, m), 8);            // u(8)
          READ_CODE(sei.getThresholdVariation(k, m), 8);   // u(8)
          READ_CODE(sei.getThresholdDifference(k, m), 8);  // u(8)
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// ISO/IEC 23002-5:H.20.2.17 V-PCC registered SEI message syntax
void
AtlasReader::vpccRegisteredSEI(vmesh::Bitstream&  bitstream,
                               SEI&               seiAbstract,
                               VpccSeiPayloadType vpccPayloadType,
                               int32_t            payloadSize) {
  vpccRegisteredSeiPayload(
    bitstream, vpccPayloadType, payloadSize, seiAbstract);
}

// ISO/IEC 23002-5:H.20.2.18 V-PCC registered SEI payload syntax
void
AtlasReader::vpccRegisteredSeiPayload(vmesh::Bitstream&  bitstream,
                                      VpccSeiPayloadType vpccPayloadType,
                                      size_t             payloadSize,
                                      SEI&               seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  reservedMessage(bitstream, seiAbstract, payloadSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.1 V-DMC registered SEI message syntax
void
AtlasReader::vdmcRegisteredSEI(vmesh::Bitstream&  bitstream,
                               SEI&               seiAbstract,
                               VdmcSeiPayloadType vdmcPayloadType,
                               int32_t            payloadSize) {
  vdmcRegisteredSeiPayload(
    bitstream, vdmcPayloadType, payloadSize, seiAbstract);
}

// F.2.2 V-DMC registered SEI payload syntax
void
AtlasReader::vdmcRegisteredSeiPayload(vmesh::Bitstream&  bitstream,
                                      VdmcSeiPayloadType vpccPayloadType,
                                      size_t             payloadSize,
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
    reservedMessage(bitstream, seiAbstract, payloadSize);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.3 Zippering SEI message syntax
void
AtlasReader::zippering(vmesh::Bitstream& bitstream, SEI& seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIZippering&>(seiAbstract);
  READ_CODE(sei.getPersistenceFlag(), 1);   // u(1)
  READ_CODE(sei.getResetFlag(), 1);         // u(1)
  READ_CODE(sei.getInstancesUpdated(), 8);  // u(8)
  sei.allocate();
  for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
    READ_CODE(sei.getInstanceIndex(i), 8);  // u(8)
    size_t k = sei.getInstanceIndex(i);
    READ_CODE(sei.getInstanceCancelFlag(k), 1);  // u(1)
    if (!sei.getInstanceCancelFlag(k)) {
      READ_UVLC(sei.getMethodType(k));  // ue(v)
      if (sei.getMethodType(k) == 1) {
        READ_UVLC(sei.getZipperingMaxMatchDistance(k));  // ue(v)
        if (sei.getZipperingMaxMatchDistance(k) != 0) {
          READ_CODE(sei.getZipperingSendDistancePerSubmesh(k), 1);  // u(1)
          if (sei.getZipperingSendDistancePerSubmesh(k)) {
            READ_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
            auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
            auto bitCountMaxDistance =
              ceilLog2(sei.getZipperingMaxMatchDistance(k) + 1);
            sei.allocateDistancePerSubmesh(k, numSubmeshes);
            for (int p = 0; p < numSubmeshes; p++) {
              READ_CODE(sei.getZipperingMaxMatchDistancePerSubmesh(k, p),
                        bitCountMaxDistance);  // u(v)
              if (sei.getZipperingMaxMatchDistancePerSubmesh(k, p) != 0) {
                READ_CODE(sei.getZipperingSendDistancePerBorderPoint(k, p),
                          1);  // u(1)
                if (sei.getZipperingSendDistancePerBorderPoint(k, p)) {
                  READ_UVLC(
                    sei.getZipperingNumberOfBorderPoints(k, p));  // ue(v)
                  auto numBorderPoints =
                    sei.getZipperingNumberOfBorderPoints(k, p);
                  auto bitCountMaxDistancePerSubmesh = ceilLog2(
                    sei.getZipperingMaxMatchDistancePerSubmesh(k, p) + 1);
                  sei.allocateDistancePerBorderPoint(k, p, numBorderPoints);
                  for (int b = 0; b < numBorderPoints; b++) {
                    READ_CODE(sei.getZipperingDistancePerBorderPoint(k, p, b),
                              bitCountMaxDistancePerSubmesh);  // u(v)
                  }
                }
              }
            }
          } else {
            READ_CODE(sei.getZipperingSendDistancePerSubmeshPair(k),
                      1);  // u(1)
            if (sei.getZipperingSendDistancePerSubmeshPair(k)) {
              READ_CODE(sei.getZipperingLinearSegmentation(k), 1);
              READ_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k),
                        6);  // u(6)
              auto numSubmeshes =
                sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
              auto bitCountMaxDistance =
                ceilLog2(sei.getZipperingMaxMatchDistance(k) + 1);
              sei.allocateDistancePerSubmeshPair(k, numSubmeshes);
              for (int p = 0; p < numSubmeshes - 1; p++) {
                if (sei.getZipperingLinearSegmentation(k)) {
                  READ_CODE(
                    sei.getZipperingMaxMatchDistancePerSubmeshPair(k, p, 0),
                    bitCountMaxDistance);
                } else {
                  for (int t = p + 1; t < numSubmeshes; t++) {
                    READ_CODE(sei.getZipperingMaxMatchDistancePerSubmeshPair(
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
        READ_UVLC(sei.getMethodForUnmatchedLoDs(k));               // ue(v)
        READ_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
        READ_CODE(sei.getZipperingDeltaFlag(k), 1);                // u(1)
        auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
        auto useDelta     = static_cast<bool>(sei.getZipperingDeltaFlag(k));
        auto maxNumBitsSubmesh = ceilLog2(numSubmeshes + 1);
        sei.allocateMatchPerSubmesh(k, numSubmeshes);
        for (int p = 0; p < numSubmeshes; p++) {
          READ_UVLC(sei.getZipperingNumberOfBorderPoints(k, p));  // ue(v)
          auto numBorderPoints = sei.getZipperingNumberOfBorderPoints(k, p);
          sei.allocateMatchPerBorderPoint(k, p, numBorderPoints);
        }
        for (int p = 0; p < numSubmeshes; p++) {
          auto    numBorderPoints = sei.getZipperingNumberOfBorderPoints(k, p);
          int64_t prevSubmeshIdx  = 0;
          int64_t prevBorderPointIdx = 0;
          for (int b = 0; b < numBorderPoints; b++) {
            if (!sei.getZipperingBorderPointMatchIndexFlag(k, p, b)) {
              if (useDelta) {
                auto& submeshIdxDelta =
                  sei.getZipperingBorderPointMatchSubmeshIndexDelta(k, p, b);
                auto& borderPointIdxDelta =
                  sei.getZipperingBorderPointMatchBorderIndexDelta(k, p, b);
                READ_SVLC(submeshIdxDelta);  // se(v)
                auto& submeshIdx =
                  sei.getZipperingBorderPointMatchSubmeshIndex(k, p, b);
                submeshIdx     = prevSubmeshIdx + submeshIdxDelta;
                prevSubmeshIdx = submeshIdx;
                if (submeshIdx != numSubmeshes) {
                  READ_SVLC(borderPointIdxDelta);  // se(v)
                  auto& borderPointIdx =
                    sei.getZipperingBorderPointMatchBorderIndex(k, p, b);
                  borderPointIdx = prevBorderPointIdx + borderPointIdxDelta;
                  prevBorderPointIdx = borderPointIdx;
                  if (submeshIdx > p)
                    sei.getZipperingBorderPointMatchIndexFlag(
                      k, submeshIdx, borderPointIdx) = true;
                }
              } else {
                auto& submeshIdx =
                  sei.getZipperingBorderPointMatchSubmeshIndex(k, p, b);
                auto& borderPointIdx =
                  sei.getZipperingBorderPointMatchBorderIndex(k, p, b);
                READ_CODE(submeshIdx, maxNumBitsSubmesh);  // u(v)
                if (submeshIdx != numSubmeshes) {
                  size_t maxNumBitsBorderIdx = ceilLog2(
                    sei.getZipperingNumberOfBorderPoints(k, submeshIdx));
                  READ_CODE(borderPointIdx, maxNumBitsBorderIdx);  // u(v)
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
        READ_CODE(sei.getZipperingNumberOfSubmeshesMinus1(k), 6);  // u(6)
        auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
        sei.allocateboundaryPerSubmesh(k, numSubmeshes);
        for (int p = 0; p < numSubmeshes; p++) {
          READ_UVLC(sei.getcrackCountPerSubmesh(k, p));
          auto numCrackCount = sei.getcrackCountPerSubmesh(k, p);
          for (int c = 0; c < numCrackCount; c++) {
            sei.allocateboundarycrack(k, p, numCrackCount);
            for (int b = 0; b < 3; b++) {
              READ_UVLC(sei.getboundaryIndex(k, p, c, b));
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
AtlasReader::submeshSOIIndicationRelationship(vmesh::Bitstream& bitstream,
                                              SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISubmeshSOIIndicationRelationship&>(seiAbstract);
  READ_CODE(sei.getPersistenceAssociationFlag(), 1);
  READ_UVLC(sei.getNumberOfActiveSceneObjects());
  READ_UVLC(sei.getSubmeshIdLengthMinus1());
  sei.allocateNumberOfAciveScenes();
  uint32_t l = CeilLog2(sei.getNumberOfActiveSceneObjects());
  for (int i = 0; i < sei.getNumberOfActiveSceneObjects(); i++) {
      READ_CODE(sei.getSoiObjectIdx(i), l);
      READ_UVLC(sei.getNumberOfSubmeshIncluded(i));
      sei.allocateNumberOfSubmeshes(i);
      for (int j = 0; j < sei.getNumberOfSubmeshIncluded(i); j++) {
          READ_CODE(sei.getSubmeshId(i, j), sei.getSubmeshIdLengthMinus1() + 1);
          READ_CODE(sei.getCompletelyIncluded(i, j), 1);
      }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.5 Submesh distortion indication SEI message syntax
void
AtlasReader::submeshDistortionIndication(vmesh::Bitstream& bitstream,
                                         SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEISubmeshDistortionIndication&>(seiAbstract);
  READ_UVLC(sei.getNumberOfSubmeshIndicatedMinus1());
  READ_UVLC(sei.getSubmeshIdLengthMinus1());
  sei.allocateNumberOfSubmeshesIndicated();
  for (int i = 0; i < sei.getNumberOfSubmeshIndicatedMinus1() + 1; i++) {
      READ_CODE(sei.getSubmeshId(i), sei.getSubmeshIdLengthMinus1() + 1);
      READ_UVLC(sei.getNumberOfVerticesOfOriginalSubmesh(i));
      READ_CODE(sei.getSubdivisionIterationCount(i), 3);
      READ_UVLC(sei.getNumberOfDistortionIndicatedMinus1(i));
      sei.allocateDistortion(i);
      for (int j = 0; j < sei.getNumberOfDistortionIndicatedMinus1(i) + 1; j++) {
          READ_CODE(sei.getDistortionMetricsType(i, j), 8);
          for (int k = 0; k < sei.getSubdivisionIterationCount(i); k++)
              READ_UVLC(sei.getDistortion(i, j, k));
      }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// F.2.6 LoD extraction information SEI message syntax
void
AtlasReader::LoDExtractionInformation(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEILoDExtractionInformation&>(seiAbstract);
  READ_CODE(sei.getExtractableUnitTypeIdx(),
            2);  // extractable unit type 0:MCTS 1:subpicture
  READ_CODE(sei.getNumSubmeshCount(), 6);  // submeshCount u(8)
  sei.allocate();
  for (int submeshIdx = 0; submeshIdx <= sei.getNumSubmeshCount();
       submeshIdx++) {
    READ_UVLC(sei.getSubmeshId(submeshIdx));  // submeshId ue(v)
    READ_CODE(sei.getSubmeshSubdivisionIterationCount(submeshIdx), 3);
    sei.allocateSubdivisionIteractionCountPerSubmesh(
      submeshIdx,
      sei.getSubmeshSubdivisionIterationCount(
        submeshIdx));  // subdivisionIterationCount
    for (int32_t i = 0;
         i <= sei.getSubmeshSubdivisionIterationCount(submeshIdx);
         i++) {
      if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
        READ_UVLC(sei.getMCTSIdx(submeshIdx, i));
      } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
        READ_UVLC(sei.getsubpictureIdx(submeshIdx, i));
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// F.2.7 Tile submesh mapping SEI message syntax
void
AtlasReader::tileSubmeshMapping(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto&    sei = static_cast<SEITileSubmeshMapping&>(seiAbstract);
  uint32_t val;
  uint8_t  currCount[2] = {0, 0};
  READ_CODE_DESC(val, 1, "PersistenceMappingFlag");
  sei.setPersistenceMappingFlag(val);
  READ_UVLC_DESC(val, "NumberTilesMinus1");
  sei.setNumberTilesMinus1(val);
  READ_UVLC_DESC(val, "TileIdLengthMinus1");
  sei.setTileIdLengthMinus1(val);
  READ_CODE_DESC(val, 1, "CodecTileSignalFlag");
  sei.setCodecTileSignalFlag(val);
  if (sei.getCodecTileSignalFlag()) {
    READ_CODE_DESC(val, 1, "GeoCodecTileAlignmentFlag");
    sei.setGeoCodecTileAlignmentFlag(val);
    READ_CODE_DESC(val, 1, "AttrCodecTileAlignmentFlag");
    sei.setAttrCodecTileAlignmentFlag(val);
    currCount[0] = 0;
    currCount[1] = 0;
  }
  for (int i = 0; i < sei.getNumberTilesMinus1() + 1; i++) {
    READ_CODE_DESC(val, sei.getTileIdLengthMinus1() + 1, "TileId");
    sei.setTileId(i, val);
    READ_CODE_DESC(val, 1, "TileTypeFlag");
    sei.setTileTypeFlag(i, val);
    int numSubmeshes = 0;
    READ_CODE_DESC(val, 6, "NumSubmeshesMinus1");
    sei.setNumSubmeshesMinus1(i, val);
    numSubmeshes = sei.getNumSubmeshesMinus1(i) + 1;
    READ_UVLC_DESC(val, "SubmeshIdLengthMinus1");
    sei.setSubmeshIdLengthMinus1(i, val);
    for (int j = 0; j < numSubmeshes; j++) {
      READ_CODE_DESC(val, sei.getSubmeshIdLengthMinus1(i) + 1, "SubmeshId");
      sei.setSubmeshId(i, j, val);
    }
    if (sei.getCodecTileSignalFlag()) {
      if ((sei.getTileTypeFlag(i) == 0 && !sei.getGeoCodecTileAlignmentFlag())
          || sei.getTileTypeFlag(i) == 1
               && !sei.getAttrCodecTileAlignmentFlag()) {
        READ_UVLC_DESC(val, "NumCodecTilesInTileMinus1");
        sei.setNumCodecTilesInTileMinus1(i, val);
        for (int j = 0; j < sei.getNumCodecTilesInTileMinus1(i) + 1; j++) {
          READ_UVLC_DESC(val, "CodecTileIdx");
          sei.setCodecTileIdx(i, j, val);
        }
      }
      if (sei.getGeoCodecTileAlignmentFlag()
          || sei.getAttrCodecTileAlignmentFlag()) {
        if (sei.getTileTypeFlag(i) == 1 && currCount[1] != 0) {
          READ_CODE_DESC(val, 1, "CodecTileIdxResetFlag");
          sei.setCodecTileIdxResetFlag(i, val);
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
AtlasReader::AttributeExtractionInformation(vmesh::Bitstream& bitstream,
                                            SEI&              seiAbstract) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& sei = static_cast<SEIAttributeExtractionInformation&>(seiAbstract);
  READ_CODE(sei.getCancelFlag(), 1);  // cancel flag u(1)
  if (!sei.getCancelFlag()) {
    READ_CODE(sei.getExtractableUnitTypeIdx(),
              2);  // extractable unit type 0:MCTS 1:subpicture
    READ_CODE(sei.getNumSubmeshCount(), 6);  // submeshCount u(6)
    READ_CODE(sei.getAttributeCount(), 7);   // attributeCount u(7)
    sei.allocate();
    for (int submeshIdx = 0; submeshIdx <= sei.getNumSubmeshCount();
         submeshIdx++) {
      READ_UVLC(sei.getSubmeshId(submeshIdx));  // submeshId ue(v)
      for (int attributeIdx = 0; attributeIdx < sei.getAttributeCount();
           attributeIdx++) {
        READ_CODE(sei.getExtractionInfoPresentFlag(attributeIdx),
                  1);  // extraction infomation present flag u(1)
        if (sei.getExtractionInfoPresentFlag(attributeIdx)) {
          if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
            READ_UVLC(sei.getMCTSIdx(submeshIdx, attributeIdx));
          } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
            READ_UVLC(sei.getsubpictureIdx(submeshIdx, attributeIdx));
          }
        }
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2  VUI syntax
// G.2.1  VUI parameters syntax
void
AtlasReader::vuiParameters(vmesh::Bitstream& bitstream, VUIParameters& vp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(vp.getTimingInfoPresentFlag(), 1);  // u(1)
  if (vp.getTimingInfoPresentFlag()) {
    READ_CODE(vp.getNumUnitsInTick(), 32);              // u(32)
    READ_CODE(vp.getTimeScale(), 32);                   // u(32)
    READ_CODE(vp.getPocProportionalToTimingFlag(), 1);  // u(1)
    if (vp.getPocProportionalToTimingFlag()) {
      READ_UVLC(vp.getNumTicksPocDiffOneMinus1());  // ue(v)
    }
    READ_CODE(vp.getHrdParametersPresentFlag(), 1);  // u(1)
    if (vp.getHrdParametersPresentFlag()) {
      hrdParameters(bitstream, vp.getHrdParameters());
    }
  }
  READ_CODE(vp.getTileRestrictionsPresentFlag(), 1);  // u(1)
  if (vp.getTileRestrictionsPresentFlag()) {
    READ_CODE(vp.getFixedAtlasTileStructureFlag(), 1);          // u(1)
    READ_CODE(vp.getFixedVideoTileStructureFlag(), 1);          //	u(1)
    READ_UVLC(vp.getConstrainedTilesAcrossV3cComponentsIdc());  // ue(v)
    READ_UVLC(vp.getMaxNumTilesPerAtlasMinus1());               // ue(v)
  }
  READ_CODE(vp.getCoordinateSystemParametersPresentFlag(), 1);  // u(1)
  if (vp.getCoordinateSystemParametersPresentFlag()) {
    coordinateSystemParameters(bitstream, vp.getCoordinateSystemParameters());
  }
  READ_CODE(vp.getUnitInMetresFlag(), 1);           // u(1)
  READ_CODE(vp.getDisplayBoxInfoPresentFlag(), 1);  // u(1)
  if (vp.getDisplayBoxInfoPresentFlag()) {
    for (size_t d = 0; d < 3; d++) {
      READ_UVLC(vp.getDisplayBoxOrigin(d));  // ue(v)
      READ_UVLC(vp.getDisplayBoxSize(d));    // ue(v)
    }
    READ_CODE(vp.getAnchorPointPresentFlag(), 1);  // u(1)
    if (vp.getAnchorPointPresentFlag()) {
      for (size_t d = 0; d < 3; d++) {
        READ_UVLC(vp.getAnchorPoint(d));  // u(v)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.2  HRD parameters syntax
void
AtlasReader::hrdParameters(vmesh::Bitstream& bitstream, HrdParameters& hp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(hp.getNalParametersPresentFlag(), 1);  // u(1)
  READ_CODE(hp.getAclParametersPresentFlag(), 1);  // u(1)
  if (hp.getNalParametersPresentFlag() || hp.getAclParametersPresentFlag()) {
    READ_CODE(hp.getBitRateScale(), 4);  // u(4)
    READ_CODE(hp.getCabSizeScale(), 4);  // u(4)
  }
  for (size_t i = 0; i <= hp.getMaxNumSubLayersMinus1(); i++) {
    READ_CODE(hp.getFixedAtlasRateGeneralFlag(i), 1);  // u(1)
    if (!hp.getFixedAtlasRateGeneralFlag(i)) {
      READ_CODE(hp.getFixedAtlasRateWithinCasFlag(i), 1);  // u(1)
    }
    if (hp.getFixedAtlasRateWithinCasFlag(i)) {
      READ_CODE(hp.getElementalDurationInTcMinus1(i), 1);  // ue(v)
    } else {
      READ_CODE(hp.getLowDelayFlag(i), 1);  // u(1)
    }
    if (!hp.getLowDelayFlag(i)) {
      READ_CODE(hp.getCabCntMinus1(i), 1);  // ue(v)
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
AtlasReader::hrdSubLayerParameters(vmesh::Bitstream&      bitstream,
                                   HrdSubLayerParameters& hlsp,
                                   size_t                 cabCnt) {
  TRACE_BITSTREAM_IN("%s", __func__);
  hlsp.allocate(cabCnt + 1);
  for (size_t i = 0; i <= cabCnt; i++) {
    READ_UVLC(hlsp.getBitRateValueMinus1(i));  // ue(v)
    READ_UVLC(hlsp.getCabSizeValueMinus1(i));  // ue(v)
    READ_CODE(hlsp.getCbrFlag(i), 1);          // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.4 Maximum coded video resolution syntax
void
AtlasReader::maxCodedVideoResolution(vmesh::Bitstream&        bitstream,
                                     MaxCodedVideoResolution& mcvr) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(mcvr.getOccupancyResolutionPresentFlag(), 1);  // u(1)
  READ_CODE(mcvr.getGeometryResolutionPresentFlag(), 1);   // u(1)
  READ_CODE(mcvr.getAttributeResolutionPresentFlag(), 1);  // u(1)
  if (mcvr.getOccupancyResolutionPresentFlag()) {
    READ_UVLC(mcvr.getOccupancyWidth());   // ue(v)
    READ_UVLC(mcvr.getOccupancyHeight());  // ue(v)
  }
  if (mcvr.getGeometryResolutionPresentFlag()) {
    READ_UVLC(mcvr.getGeometryWidth());   // ue(v)
    READ_UVLC(mcvr.getGeometryHeight());  // ue(v)
  }
  if (mcvr.getAttributeResolutionPresentFlag()) {
    READ_UVLC(mcvr.getAttributeWidth());   // ue(v)
    READ_UVLC(mcvr.getAttributeHeight());  // ue(v)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G.2.5 Coordinate system parameters syntax
void
AtlasReader::coordinateSystemParameters(vmesh::Bitstream&           bitstream,
                                        CoordinateSystemParameters& csp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(csp.getForwardAxis(), 2);    // u(2)
  READ_CODE(csp.getDeltaLeftAxis(), 1);  // u(1)
  READ_CODE(csp.getForwardSign(), 1);    // u(1)
  READ_CODE(csp.getLeftSign(), 1);       // u(1)
  READ_CODE(csp.getUpSign(), 1);         // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// G2.6 V-DMC VUI extension
void
AtlasReader::vdmcVuiParameters(vmesh::Bitstream&  bitstream,
                               VdmcVuiParameters& vp,
                               size_t             numAttrVideo) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t value;
  for (size_t d = 0; d < numAttrVideo; d++) {
    READ_CODE(value, 1);
    vp.setVdmcVuiOneSubmeshPerIndependentUnitAttributeFlag(d, value);
  }
  READ_CODE(vp.getVdmcVuiOneSubmeshPerIndependentUnitGeometryFlag(), 1);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.1.1 ASPS V-PCC extension syntax
void
AtlasReader::aspsVpccExtension(vmesh::Bitstream&              bitstream,
                               AtlasSequenceParameterSetRbsp& asps,
                               AspsVpccExtension&             ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(ext.getRemoveDuplicatePointEnableFlag(), 1);
  if (asps.getPixelDeinterleavingFlag() || asps.getPLREnabledFlag()) {
    READ_UVLC(ext.getSurfaceThicknessMinus1());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.1 AFPS V-PCC extension syntax
void
AtlasReader::afpsVpccExtension(vmesh::Bitstream&  bitstream,
                               AfpsVpccExtension& ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.1 AAPS V-PCC extension syntax
void
AtlasReader::aapsVpccExtension(vmesh::Bitstream&  bitstream,
                               AapsVpccExtension& ext) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(ext.getCameraParametersPresentFlag(), 1);  // u(1);
  if (ext.getCameraParametersPresentFlag()) {
    atlasCameraParameters(bitstream, ext.getAtlasCameraParameters());
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// H.7.3.6.2.2 Atlas camera parameters syntax
void
AtlasReader::atlasCameraParameters(vmesh::Bitstream&      bitstream,
                                   AtlasCameraParameters& acp) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(acp.getCameraModel(), 8);  // u(8)
  if (acp.getCameraModel() == 1) {
    READ_CODE(acp.getScaleEnabledFlag(), 1);     // u(1)
    READ_CODE(acp.getOffsetEnabledFlag(), 1);    // u(1)
    READ_CODE(acp.getRotationEnabledFlag(), 1);  // u(1)
    if (acp.getScaleEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        READ_CODE(acp.getScaleOnAxis(i), 32);  // u(32)
      }
    }
    if (acp.getOffsetEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        READ_CODES(acp.getOffsetOnAxis(i), 32);  // i(32)
      }
    }
    if (acp.getRotationEnabledFlag()) {
      for (size_t i = 0; i < 3; i++) {
        READ_CODES(acp.getRotation(i), 16);  // i(16)
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
