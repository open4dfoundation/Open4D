/* The copyright in this software is being made available under the BSD
* Licence, included below.  This software may be subject to other third
* party and contributor rights, including patent rights, and no such
* rights are granted under this licence.
*
* Copyright (c) 2025, ISO/IEC
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
*
* * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
* * Neither the name of the ISO/IEC nor the names of its contributors
*   may be used to endorse or promote products derived from this
*   software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <cmath>
#include <array>
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "atlasEncoder.hpp"

namespace atlas {

//============================================================================
std::vector<double>
fracPackingScale(double val) {
  std::cout << "OrthoAtlas Fixed Point Signaling activated"
            << "\n";
  int    _iteration = 0;
  int    _bitDepth  = 40;
  double _fs        = val;
  while (_fs < std::pow(2, _bitDepth - 1)) {
    _fs *= 2;
    _iteration++;
  }
  _fs = (double)(int64_t)(_fs + 0.5);

  std::vector<double> _frac;
  _frac.push_back(_fs);
  _frac.push_back((double)_iteration);
  return _frac;
}

//============================================================================
int32_t
AtlasEncoder::initializeAtlasParameterSets(
  AtlasBitstream&               adStream,
  const AtlasEncoderParameters& params) {
  const uint8_t aspsId = 0;
  const uint8_t afpsId = 0;

  // Atlas sequence parameter set
  auto& asps = adStream.addAtlasSequenceParameterSet();
  asps.setFrameWidth(0);
  asps.setFrameHeight(0);
  asps.getGeometry3dBitdepthMinus1()  = params.bitDepthPosition - 1;
  asps.getGeometry2dBitdepthMinus1()  = params.bitDepthTexCoord - 1;
  asps.getLog2PatchPackingBlockSize() = params.log2GeometryVideoBlockSize;
  asps.getExtensionFlag()             = 1;
  asps.getVdmcExtensionFlag()         = 1;

  // ASPS extension
  auto& asve = asps.getAsveExtension();
  asve.setAsveSubdivisionIterationCount(
    uint32_t(params.subdivisionIterationCount));
  if (params.interpolateSubdividedNormalsFlag > 0)
    asve.setAsveInterpolateSubdividedNormalsFlag(1);
  else asve.setAsveInterpolateSubdividedNormalsFlag(0);

  if (params.subdivisionIterationCount > 0) {
    asve.setAsveLodAdaptiveSubdivisionFlag(
      params.intraGeoParams.lodAdaptiveSubdivisionFlag);
  }
  for (int32_t it = 0; it < params.subdivisionIterationCount; it++) {
    if (params.intraGeoParams.lodAdaptiveSubdivisionFlag || it == 0) {
      asve.setAsveSubdivisionMethod(
        it, uint32_t(params.intraGeoParams.subdivisionMethod[it]));
    } else {
      asve.setAsveSubdivisionMethod(
        it, uint32_t(params.intraGeoParams.subdivisionMethod[0]));
    }
  }
  asve.setAsveEdgeBasedSubdivisionFlag(
    ((params.subdivisionEdgeLengthThreshold > 0)&&(params.subdivisionIterationCount > 0)));
  if (asve.getAsveEdgeBasedSubdivisionFlag()) {
    asve.setAsveSubdivisionMinEdgeLength(
      params.subdivisionEdgeLengthThreshold);
  }
  if (params.transformMethod == 0
      || params.subdivisionIterationCount == 0) {  //m68642
    asve.setAsveTransformMethod((uint8_t)vmesh::TransformMethod::NONE_TRANSFORM);
    std::cout << "Transform: None Transform selected." << std::endl;
  } else {
    asve.setAsveTransformMethod((uint8_t)vmesh::TransformMethod::LINEAR_LIFTING);
    std::cout << "Transform: Linear Lifting selected." << std::endl;
  }

  //  uint32_t attributeNominalFrameCount =
  //    vps.getVpsAttributeNominalFrameCount(0);
  // TODO as parameter to encoder but not needed for 1st edition
  auto attributeNominalFrameCount = params.encodeTextureVideo ? 1: 0;
  //  asve.setAsveConsistentAttributeFrameFlag(
  //    vps.getVpsVdmcExtension().getVpsExtConsistentAttributeFrameFlag(0));
  auto consistentAttributeFrameFlag = true;

  if (attributeNominalFrameCount == 0) {
    // bitstream conformance requirement
    asve.setAsveAttributeInformationPresentFlag(false);
  } else {
    asve.setAsveAttributeInformationPresentFlag(true);
  asve.setAsveConsistentAttributeFrameFlag(consistentAttributeFrameFlag);
    asve.setAsveAttributeFrameSizeCount(attributeNominalFrameCount);
  }
  auto& qp       = asve.getAsveQuantizationParameters();
  auto  lodCount = asve.getAsveSubdivisionIterationCount() + 1;
  qp.setNumLod(lodCount);
  auto dimCount = params.applyOneDimensionalDisplacement ? 1 : 3;
  qp.setNumComponents(dimCount);
  qp.setVdmcLodQuantizationFlag(params.lodDisplacementQuantizationFlag);
  qp.setVdmcBitDepthOffset(params.bitDepthOffset);
  if (params.lodDisplacementQuantizationFlag == 0) {
    for (int d = 0; d < dimCount; d++) {
      qp.setVdmcQuantizationParameters(d, params.liftingQP[d]);
      qp.setVdmcLog2LodInverseScale(d,
                                    params.log2LevelOfDetailInverseScale[d]);
    }
  } else {
    for (int l = 0; l < lodCount; l++) {
      qp.allocComponents(dimCount, l);
      for (int d = 0; d < dimCount; d++) {
        int8_t value = params.qpPerLevelOfDetails[l][d]
                       - (asve.getAsveDisplacementReferenceQPMinus49() + 49);
        qp.setVdmcLodDeltaQPValue(l, d, std::abs(value));
        qp.setVdmcLodDeltaQPSign(l, d, (value < 0) ? 1 : 0);
      }
    }
  }
  qp.setVdmcDirectQuantizationEnabledFlag(
    params.displacementQuantizationType
    == vmesh::DisplacementQuantizationType::ADAPTIVE);
  asve.setAsveInverseQuantizationOffsetPresentFlag(
    params.IQSkipFlag ? false : params.InverseQuantizationOffsetFlag);

  auto& ltp = asve.getAspsExtLtpDisplacement();
  if (params.transformMethod == 1) {
    ltp.setNumLod(lodCount - 1);
    ltp.setSkipUpdateFlag(params.liftingSkipUpdate);
    bool lodAdaptivUpdateWeight = params.liftingAdaptiveUpdateWeightFlag;
    ltp.setAdaptiveUpdateWeightFlag(params.liftingAdaptiveUpdateWeightFlag);
    ltp.setValenceUpdateWeightFlag(params.liftingValenceUpdateWeightFlag);
    for (int l = 0; l < lodCount - 1 && !params.liftingSkipUpdate; l++) {
      if (lodAdaptivUpdateWeight || (l == 0)) {
        ltp.setLiftingUpdateWeightNumerator(
          l, params.liftingUpdateWeightNumerator[l]);
        ltp.setLiftingUpdateWeightDenominatorMinus1(
          l, params.liftingUpdateWeightDenominatorMinus1[l]);
      } else {
        // constant update weights per LoD
      }
      ltp.setAdaptivePredictionWeightFlag(
        params.liftingAdaptivePredictionWeightFlag);
      if (ltp.getAdaptivePredictionWeightFlag()) {
        ltp.setLiftingPredictionWeightNumerator(
          l, params.liftingPredictionWeightNumerator[l]);
        ltp.setLiftingPredictionWeightDenominatorMinus1(
          l, params.liftingPredictionWeightDenominatorMinus1[l]);
      } else {
        ltp.setLiftingPredictionWeightNumerator(
          l, params.liftingPredictionWeightNumeratorDefault[l]);
        ltp.setLiftingPredictionWeightDenominatorMinus1(
          l, params.liftingPredictionWeightDenominatorMinus1Default[l]);
      }
    }
  }

  asve.setAsve1DDisplacementFlag(params.applyOneDimensionalDisplacement);
  asve.setAsveDisplacementIdPresentFlag(
    (1 == params.encodeDisplacementType));
  if (params.transformMethod == 1) {
    asve.setAsveLiftingOffsetPresentFlag(params.applyLiftingOffset);
    asve.setAsveDirectionalLiftingPresentFlag(params.dirlift);
  }

  ltp.setDirectionalLiftingScale1(params.dirliftScale1);
  ltp.setDirectionalLiftingDeltaScale2(params.dirliftDeltaScale2);
  ltp.setDirectionalLiftingDeltaScale3(params.dirliftDeltaScale3);
  ltp.setDirectionalLiftingScaleDenoMinus1(params.dirliftScaleDenoMinus1);

  asve.setAsvePackingMethod(params.displacementReversePacking);
  if (params.encodeDisplacementType != 2) {
    asve.setAsveQuantizationParametersPresentFlag(false);
  } else {
    asve.setAsveQuantizationParametersPresentFlag(true);
  }
  //  asve.setAsveQuantizationParametersPresentFlag(
  //    syntax.getDisplacementCodecType() != VideoCodecGroupIdc::AC_CODED ? 1 : 0);
  if (params.IQSkipFlag) {
    asve.setAsveQuantizationParametersPresentFlag(false);
  } else {
    asve.setAsveQuantizationParametersPresentFlag(true);
  }
  if (params.encodeDisplacementType == 0)  // no displacement
  {
    asve.setAsveQuantizationParametersPresentFlag(false);
    asve.setAsveInverseQuantizationOffsetPresentFlag(false);
  }

  // orthoAtlas parameters
  asve.setAsveProjectionTexcoordEnableFlag(
    (params.iDeriveTextCoordFromPos >= 1));
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    asve.setAsveProjectionTexcoordMappingAttributeIndexPresentFlag(
      (params.iDeriveTextCoordFromPos != 3));
    if (asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag()) {
      asve.setAsveProjectionTexcoordMappingAttributeIndex(0);  // always using the single attribute, could be changed in the future if using multiple attributes
    }
    asve.setAsveProjectionTexcoordOutputBitdepthMinus1(params.bitDepthTexCoord - 1);
    std::vector<double> packScale = fracPackingScale(params.packingScaling);
    asve.setAsveProjectionTexCoordUpscaleFactorMinus1(packScale[0] - 1);
    asve.setAsveProjectionTexcoordLog2DownscaleFactor(packScale[1]);
    asve.setAsveProjectionTexcoordBboxBiasEnableFlag(params.biasEnableFlag);
    if (params.useRawUV) asve.setAsveProjectionRawTextcoordPresentFlag(true);
    if (asve.getAsveProjectionRawTextcoordPresentFlag())
      asve.setAsveProjectionRawTextcoordBitdepthMinus1(params.rawTextcoordBitdepth - 1);
  }

  // Atlas frame parameter set
  auto& afps                            = adStream.addAtlasFrameParameterSet();
  afps.getAtlasSequenceParameterSetId() = aspsId;
  afps.getAtlasFrameParameterSetId()    = afpsId;
  afps.getExtensionFlag()               = true;
  afps.getVmcExtensionFlag()            = true;
  auto& afve                            = afps.getAfveExtension();
  auto& meshInfo                        = afve.getAtlasFrameMeshInformation();
  meshInfo.getSubmeshIds().resize(params.submeshIdList.size(), 0);
  meshInfo.getNumSubmeshesInAtlasFrameMinus1() = params.numSubmesh - 1;
  meshInfo.getSignalledSubmeshIdFlag()         = params.enableSignalledIds;
  meshInfo.getSubmeshIds().resize(params.submeshIdList.size());
  if (meshInfo.getSignalledSubmeshIdFlag()) {
    meshInfo._submeshIndexToID.resize(params.numSubmesh);
    uint32_t maxVal = 0;
    for (int i = 0; i < params.submeshIdList.size(); i++) {
      maxVal = std::max(maxVal, params.submeshIdList[i]);
      meshInfo.setSubmeshId(i, params.submeshIdList[i]);
    }
    meshInfo._submeshIDToIndex.resize(maxVal + 1);
    for (int i = 0; i < params.submeshIdList.size(); i++) {
      meshInfo._submeshIDToIndex[params.submeshIdList[i]] = i;
    }
    uint32_t v = (maxVal < 2 ? 1 : vmesh::CeilLog2(maxVal + 1));
    uint32_t b = (uint32_t)ceil(log2(params.numSubmesh));
    meshInfo.getSignalledSubmeshIdDeltaLength() = v - b;
  } else {
    for (int i = 0; i < params.numSubmesh; i++) {
      meshInfo.setSubmeshId(i, i);
    }
  }
  if (!meshInfo.getSignalledSubmeshIdFlag()) {
    meshInfo._submeshIDToIndex.resize(params.numSubmesh);
    meshInfo._submeshIndexToID.resize(params.numSubmesh);
    auto numSubmeshes =
      static_cast<uint32_t>(meshInfo.getNumSubmeshesInAtlasFrameMinus1());
    for (size_t i = 0; i < numSubmeshes; i++) {
      meshInfo._submeshIDToIndex[i] = (uint32_t)i;
      meshInfo._submeshIndexToID[i] = (uint32_t)i;
    }
  }

  afve.setAfveOverridenFlag(false);
  afve.setAfveSubdivisionIterationCount(
    asve.getAsveSubdivisionIterationCount());
  for (int32_t it = 0; it < params.subdivisionIterationCount; it++) {
    afve.setAfveSubdivisionMethod(it, asve.getAsveSubdivisionMethod()[it]);
  }
  if (asve.getAsveEdgeBasedSubdivisionFlag()) {
    // change this if AFPS value is different from ASPS value, for now they are the same value
    afve.setAfveEdgeBasedSubdivisionFlag(false);
    afve.setAfveSubdivisionMinEdgeLength(
      params.subdivisionEdgeLengthThreshold);
  } else {
    afve.setAfveEdgeBasedSubdivisionFlag(false);
  }
  afve.setAfveTransformMethod(asve.getAsveTransformMethod());
  afve.getAfveQuantizationParameters() = asve.getAsveQuantizationParameters();
  afve.getAfpsLtpDisplacement()        = asve.getAspsExtLtpDisplacement();
  if (asve.getAsveProjectionTexcoordEnableFlag()) {
    auto numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
    for (int i = 0; i < numSubmeshes; i++) {
      afve.getProjectionTextcoordPresentFlag(i) = true;
      if (afve.getProjectionTextcoordPresentFlag(i)) {
        //texturePackingSize.first  = params.texturePackingWidth;
        //texturePackingSize.second = params.texturePackingHeight;
        afve.getProjectionTextcoordWidth(i)  = params.texturePackingWidth;
        afve.getProjectionTextcoordHeight(i) = params.texturePackingHeight;
        afve.getProjectionTextcoordGutter(i) = params.gutter;
      }
    }
  } else {
    auto numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
    for (int i = 0; i < numSubmeshes; i++) {
      afve.getProjectionTextcoordPresentFlag(i) = false;
    }
  }

  // Atlas frame tile information
  auto& afti = afps.getAtlasFrameTileInformation();
  afti.setSingleTileInAtlasFrameFlag(params.numTilesGeometry == 1);
  afti.setSignalledTileIdFlag() = params.enableSignalledIds;
  if (afti.getSignalledTileIdFlag()) {
    for (int i = 0; i < params.numTilesGeometry; i++) {
      afti.setTileId(params.tileIdList[i]);
    }
    auto v =
      (int32_t)(afti.getMaxTileId() < 2 ? 1
                                        : vmesh::CeilLog2(afti.getMaxTileId() + 1));
    afti.setSignalledTileIdLengthMinus1() = v - 1;
  } else {
    for (int i = 0; i < params.numTilesGeometry; i++) { afti.setTileId(i); }
    int32_t v = 0;
    if (afti.getMaxTileId() > 0) {
      v = (int32_t)ceil(log2(afti.getMaxTileId()) + 1);
    }
    afti.setSignalledTileIdLengthMinus1() = (v - 1);
  }

  // Atlas frame tile attribute information
  afve.getAtlasFrameTileAttributeInformation().resize(1);
  auto  maxTileIndexInAfti = afti.getMaxTileIndex();
  auto& aftai              = afve.getAtlasFrameTileAttributeInformation()[0];
  aftai.setTileIndexOffset(maxTileIndexInAfti + 1);
  aftai.setSingleTileInAtlasFrameFlag(true);
  aftai.setSignalledTileIdFlag() = params.enableSignalledIds;
  if (aftai.getSignalledTileIdFlag()) {
    for (int i = params.numTilesGeometry;
         i < params.numTilesGeometry + params.numTilesAttribute;
         i++) {
      aftai.setTileId(params.tileIdList[i]);
    }
    auto v =
      (int32_t)(aftai.getMaxTileId() < 2 ? 1
                                         : vmesh::CeilLog2(aftai.getMaxTileId() + 1));
    aftai.setSignalledTileIdLengthMinus1() = v - 1;
  } else {
    for (int i = 0; i < params.numTilesAttribute; i++) {
      aftai.setTileId(afti.getMaxTileId() + i + 1);
    }
    int32_t v = 0; // align evaluation of SignalledTileIdLengthMinus1 as in afti
    if (aftai.getMaxTileId() > 0) {
      v = (int32_t)ceil(log2(aftai.getMaxTileId()) + 1);
    }
    aftai.setSignalledTileIdLengthMinus1() = (v - 1);
  }

  // Atlas tile layer
  //NOTE: One atlas tile can have multiple patch data units that corresponding to submeshes
  // LK i move this to atlas compress function under initializeAtlasTileLayer
  //  for (size_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
  //    // geometry
  //    for (int ti = 0; ti < afti.getTileCount(); ti++) {
  //      auto& atl = adStream.addAtlasTileLayer();
  //      auto& ath = atl.getHeader();
  //      ath.setFrameIndex(frameIdx);
  //
  //      ath.setAtlasFrmOrderCntVal(frameIdx);
  //      ath.setAtlasFrmOrderCntLsb(frameIdx);
  //      ath.getAtlasFrameParameterSetId() = afpsId;
  //      ath.setNumRefIdxActiveOverrideFlag(false);
  //      ath.setRefAtlasFrameListSpsFlag(false);
  //      ath.setRefAtlasFrameListIdx(0);
  //      if (afti.getSignalledTileIdFlag()) {
  //        ath.setAtlasTileHeaderId(params.tileIdList[ti]);
  //      } else {
  //        ath.setAtlasTileHeaderId(ti);
  //      }
  //      ath.setType(I_TILE);
  //
  //      int patchCount0 = params.submeshIdsInTile[ti].size();
  //      if (params.lodPatchesEnable == 1) {
  //        patchCount0 = patchCount0 * (params.subdivisionIterationCount + 1);
  //      }
  //      for (int32_t pi = 0; pi < patchCount0; pi++) {
  //        atl.getDataUnit().addPatchInformationData(I_INTRA);
  //      }
  //
  //      // init ref list;
  //      //refFrameDiff:1,2,3,4...
  //      size_t numRefList          = params.maxNumRefBmeshList;
  //      size_t numActiveRefEntries = params.maxNumRefBmeshFrame;
  //      for (size_t list = 0; list < numRefList; list++) {
  //        RefListStruct refList;
  //        refList.setNumRefEntries((int32_t)numActiveRefEntries);
  //        refList.allocate();
  //        for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
  //          int afocDiff = -1;
  //          if (i == 0)
  //            afocDiff =
  //              params.refFrameDiff
  //                [0];  //the absolute difference between the foc values of current & ref
  //          else
  //            afocDiff =
  //              params.refFrameDiff[i]
  //              - params.refFrameDiff
  //                  [i
  //                   - 1];  //the absolute difference between the foc values ref[i-1] & ref[i]
  //          refList.setAbsDeltaAfocSt(i, std::abs(afocDiff));
  //          refList.setStrafEntrySignFlag(
  //            i, afocDiff < 0 ? false : true);  //0.minus 1.plus
  //          refList.setStRefAtlasFrameFlag(i, true);
  //        }
  //        asps.addRefListStruct(refList);
  //      }
  //      auto& refList = ath.getRefListStruct();
  //      refList.setNumRefEntries(0);
  //      refList.allocate();
  //      for (size_t j = 0; j < refList.getNumRefEntries(); j++) {
  //        int32_t afocDiff = j == 0 ? -1 : 1;
  //        refList.setAbsDeltaAfocSt(j, std::abs(afocDiff));
  //        refList.setStrafEntrySignFlag(j, afocDiff < 0 ? false : true);
  //        refList.setStRefAtlasFrameFlag(j, true);
  //      }
  //    }
  //
  //    // attribute
  //    for (int ti = 0; ti < aftai.getTileCount(); ti++) {
  //      auto& atl = adStream.addAtlasTileLayer();
  //      auto& ath = atl.getHeader();
  //      ath.setFrameIndex(frameIdx);
  //
  //      ath.setAtlasFrmOrderCntVal(frameIdx);
  //      ath.setAtlasFrmOrderCntLsb(frameIdx);
  //      ath.getAtlasFrameParameterSetId() = afpsId;
  //      ath.setNumRefIdxActiveOverrideFlag(false);
  //      ath.setRefAtlasFrameListSpsFlag(false);
  //      ath.setRefAtlasFrameListIdx(0);
  //      ath.setAtlasTileHeaderId(aftai.getTileId(ti));
  //      ath.setType(I_TILE_ATTR);
  //
  //      int patchCount0 =
  //        params.submeshIdsInTile[ti + params.numTilesGeometry].size();
  //      for (int32_t pi = 0; pi < patchCount0; pi++) {
  //        atl.getDataUnit().addPatchInformationData(I_INTRA_ATTR);
  //      }
  //
  //      size_t numRefList          = params.maxNumRefBmeshList;
  //      size_t numActiveRefEntries = params.maxNumRefBmeshFrame;
  //      for (size_t list = 0; list < numRefList; list++) {
  //        RefListStruct refList;
  //        refList.setNumRefEntries((int32_t)numActiveRefEntries);
  //        refList.allocate();
  //        for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
  //          int afocDiff = -1;
  //          if (i == 0)
  //            afocDiff =
  //              params.refFrameDiff
  //                [0];  //the absolute difference between the foc values of current & ref
  //          else
  //            afocDiff =
  //              params.refFrameDiff[i]
  //              - params.refFrameDiff
  //                  [i
  //                   - 1];  //the absolute difference between the foc values ref[i-1] & ref[i]
  //          refList.setAbsDeltaAfocSt(i, std::abs(afocDiff));
  //          refList.setStrafEntrySignFlag(
  //            i, afocDiff < 0 ? false : true);  //0.minus 1.plus
  //          refList.setStRefAtlasFrameFlag(i, true);
  //        }
  //        asps.addRefListStruct(refList);
  //      }
  //      auto& refList = ath.getRefListStruct();
  //      refList.setNumRefEntries(0);
  //      refList.allocate();
  //      for (size_t j = 0; j < refList.getNumRefEntries(); j++) {
  //        int32_t afocDiff = j == 0 ? -1 : 1;
  //        refList.setAbsDeltaAfocSt(j, std::abs(afocDiff));
  //        refList.setStrafEntrySignFlag(j, afocDiff < 0 ? false : true);
  //        refList.setStRefAtlasFrameFlag(j, true);
  //      }
  //    }
  //  }

  return 0;
}

void
AtlasEncoder::initializeAtlasTileLayer(AtlasBitstream& adStream,
                                       int32_t         frameCount,
                                       const AtlasEncoderParameters& params) {
  auto& asps = adStream.getAtlasSequenceParameterSet(0);
  auto& afps =
    adStream.getAtlasFrameParameterSet(asps.getAtlasSequenceParameterSetId());
  auto  afpsId = afps.getAtlasFrameParameterSetId();
  auto& afti   = afps.getAtlasFrameTileInformation();
  auto& aftai =
    afps.getAfveExtension().getAtlasFrameTileAttributeInformation()[0];

  for (size_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    // geometry
    for (int ti = 0; ti < afti.getTileCount(); ti++) {
      auto& atl = adStream.addAtlasTileLayer();
      auto& ath = atl.getHeader();
      ath.setFrameIndex(frameIdx);

      ath.setAtlasFrmOrderCntVal(frameIdx);
      ath.setAtlasFrmOrderCntLsb(frameIdx);
      ath.getAtlasFrameParameterSetId() = afpsId;
      ath.setNumRefIdxActiveOverrideFlag(false);
      ath.setRefAtlasFrameListSpsFlag(false);
      ath.setRefAtlasFrameListIdx(0);
      if (afti.getSignalledTileIdFlag()) {
        ath.setAtlasTileHeaderId(params.tileIdList[ti]);
      } else {
        ath.setAtlasTileHeaderId(ti);
      }
      ath.setType(I_TILE);

      int patchCount0 = params.submeshIdsInTile[ti].size();
      if (params.lodPatchesEnable == 1) {
        patchCount0 = patchCount0 * (params.subdivisionIterationCount + 1);
      }
      for (int32_t pi = 0; pi < patchCount0; pi++) {
        atl.getDataUnit().addPatchInformationData(I_INTRA);
      }

      // init ref list;
      //refFrameDiff:1,2,3,4...
      size_t numRefList          = params.maxNumRefBmeshList;
      size_t numActiveRefEntries = params.maxNumRefBmeshFrame;
      for (size_t list = 0; list < numRefList; list++) {
        RefListStruct refList;
        refList.setNumRefEntries((int32_t)numActiveRefEntries);
        refList.allocate();
        for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
          int afocDiff = -1;
          if (i == 0)
            afocDiff =
              params.refFrameDiff
                [0];  //the absolute difference between the foc values of current & ref
          else
            afocDiff =
              params.refFrameDiff[i]
              - params.refFrameDiff
                  [i
                   - 1];  //the absolute difference between the foc values ref[i-1] & ref[i]
          refList.setAbsDeltaAfocSt(i, std::abs(afocDiff));
          refList.setStrafEntrySignFlag(
            i, afocDiff < 0 ? false : true);  //0.minus 1.plus
          refList.setStRefAtlasFrameFlag(i, true);
        }
        asps.addRefListStruct(refList);
      }
      auto& refList = ath.getRefListStruct();
      refList.setNumRefEntries(0);
      refList.allocate();
      for (size_t j = 0; j < refList.getNumRefEntries(); j++) {
        int32_t afocDiff = j == 0 ? -1 : 1;
        refList.setAbsDeltaAfocSt(j, std::abs(afocDiff));
        refList.setStrafEntrySignFlag(j, afocDiff < 0 ? false : true);
        refList.setStRefAtlasFrameFlag(j, true);
      }
    }

    // attribute
    for (int ti = 0; ti < aftai.getTileCount(); ti++) {
      auto& atl = adStream.addAtlasTileLayer();
      auto& ath = atl.getHeader();
      ath.setFrameIndex(frameIdx);

      ath.setAtlasFrmOrderCntVal(frameIdx);
      ath.setAtlasFrmOrderCntLsb(frameIdx);
      ath.getAtlasFrameParameterSetId() = afpsId;
      ath.setNumRefIdxActiveOverrideFlag(false);
      ath.setRefAtlasFrameListSpsFlag(false);
      ath.setRefAtlasFrameListIdx(0);
      ath.setAtlasTileHeaderId(aftai.getTileId(ti));
      ath.setType(I_TILE_ATTR);

      int patchCount0 =
        params.submeshIdsInTile[ti + params.numTilesGeometry].size();
      for (int32_t pi = 0; pi < patchCount0; pi++) {
        atl.getDataUnit().addPatchInformationData(I_INTRA_ATTR);
      }

      size_t numRefList          = params.maxNumRefBmeshList;
      size_t numActiveRefEntries = params.maxNumRefBmeshFrame;
      for (size_t list = 0; list < numRefList; list++) {
        RefListStruct refList;
        refList.setNumRefEntries((int32_t)numActiveRefEntries);
        refList.allocate();
        for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
          int afocDiff = -1;
          if (i == 0)
            afocDiff =
              params.refFrameDiff
                [0];  //the absolute difference between the foc values of current & ref
          else
            afocDiff =
              params.refFrameDiff[i]
              - params.refFrameDiff
                  [i
                   - 1];  //the absolute difference between the foc values ref[i-1] & ref[i]
          refList.setAbsDeltaAfocSt(i, std::abs(afocDiff));
          refList.setStrafEntrySignFlag(
            i, afocDiff < 0 ? false : true);  //0.minus 1.plus
          refList.setStRefAtlasFrameFlag(i, true);
        }
        asps.addRefListStruct(refList);
      }
      auto& refList = ath.getRefListStruct();
      refList.setNumRefEntries(0);
      refList.allocate();
      for (size_t j = 0; j < refList.getNumRefEntries(); j++) {
        int32_t afocDiff = j == 0 ? -1 : 1;
        refList.setAbsDeltaAfocSt(j, std::abs(afocDiff));
        refList.setStrafEntrySignFlag(j, afocDiff < 0 ? false : true);
        refList.setStRefAtlasFrameFlag(j, true);
      }
    }
  }


  return;
}

//============================================================================
void
AtlasEncoder::compressAtlas(
  AtlasBitstream&                             adStream,
  std::vector<std::vector<AtlasTile>>&        tileAreasInVideo_,
  std::vector<std::vector<AtlasTile>>&        reconAtlasTiles_,
  std::vector<std::vector<vmesh::VMCSubmesh>>&       encFrames,
  const AtlasEncoderParameters&               params,
  std::pair<int32_t, int32_t>&                geometryVideoSize,
  std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_) {
  int32_t frameCountSubmesh0 = (int32_t)tileAreasInVideo_[0].size();

  this->initializeAtlasTileLayer(adStream, frameCountSubmesh0, params);

  reconAtlasTiles_.resize(tileAreasInVideo_.size());
  for (size_t i = 0; i < reconAtlasTiles_.size(); i++) {
    reconAtlasTiles_[i].resize(tileAreasInVideo_[i].size());  //frameCount
  }

  size_t                         aspsId = 0;
  AtlasSequenceParameterSetRbsp& asps = adStream.getAtlasSequenceParameterSet(
    aspsId);  //TODO: [PSIDX] aspsId=index?
  asps.setFrameWidth(params.encodeDisplacementType != 2
                       ? 0
                       : uint32_t(geometryVideoSize.first));
  asps.setFrameHeight(params.encodeDisplacementType != 2
                        ? 0
                        : uint32_t(geometryVideoSize.second));
  asps.getAsveExtension().setAsveLodPatchesEnableFlag(params.lodPatchesEnable);
  if (params.use45DegreeProjection) {
    asps.getExtendedProjectionEnabledFlag() = true;
  }
  //TODO: [Magenta Syntax] it may need to be done fo multiple attributeIndices
  AspsVdmcExtension& asve = asps.getAsveExtension();
  for (int attrIdx = 0; attrIdx < asve.getAspsAttributeNominalFrameSizeCount();
       attrIdx++) {
    asve.setAsveAttributeFrameWidth(attrIdx,
                                    attributeVideoSize_[attrIdx].first);
    asve.setAsveAttributeFrameHeight(attrIdx,
                                     attributeVideoSize_[attrIdx].second);
    // checking number of submeshes
    if (encFrames.size() > 1) {
      asve.setAsveAttributeSubtextureEnabledFlag(
        attrIdx, params.texturePlacementPerPatch);
    }
  }

  //==========
  //encode and decode tile information
  int attributeCount =
    asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
  AtlasFrameParameterSetRbsp& afps = adStream.getAtlasFrameParameterSet(0);
  this->setAtlasTileInformation(
    afps, asps, tileAreasInVideo_, geometryVideoSize, params);
  afps.getAfveExtension().getAtlasFrameTileAttributeInformation().resize(
    attributeCount);
  //VERIFY_SOFTWARE_LIMITATION(attributeCount < 2);
  for (int attrIdx = 0; attrIdx < attributeCount; attrIdx++) {
    this->setAtlasTileAttributeInformation(
      attrIdx, afps, asps, tileAreasInVideo_, attributeVideoSize_, params);
  }

  checkAttributeTilesConsistency(afps.getAfveExtension(),
                                 asps,
                                 params.referenceAttributeIdx,
                                 tileAreasInVideo_,
                                 attributeVideoSize_);
  //}
    // It is a requirement of bitstream conformance that
    // afve_consistent_tiling_across_attribute_video_flag be equal to 1
    // when vpve_consistent_attribute_frame_flag[ j ] equal to 1 where j
    // is the ID of the current atlas. (8.4.6.2.3)
    // TODO, Lukasz: this cannot be here Olivier,
    //  somehow on the VDMC encoder application we should check this
//    const auto& afve = afps.getAfveExtension();
//    for (auto i = 0; i <= vps.getAtlasCountMinus1(); ++i) {
//      const auto j = vps.getAtlasId(i);
//      if (vmcExt.getVpsExtConsistentAttributeFrameFlag(j)) {
//        VERIFY_CONFORMANCE(
//          afve.getAfveConsistentTilingAccrossAttributesFlag() == 1
//        );
//      }
//    }

  std::vector<imageArea> decodedGeometryTileAreas;
  this->reconstructTileInformation(decodedGeometryTileAreas, -1, afps, asps);

  std::vector<std::vector<imageArea>> decodedTextureTileAreas(attributeCount);
  for (int attrIdx = 0; attrIdx < attributeCount; attrIdx++) {
    this->reconstructTileInformation(
      decodedTextureTileAreas[attrIdx], attrIdx, afps, asps);
    for (int ti = 0; ti < decodedTextureTileAreas[attrIdx].size(); ti++)
      printf("ti[%d]\t%d,%d\t%dx%d\n",
             ti,
             decodedTextureTileAreas[attrIdx][ti].LTx,
             decodedTextureTileAreas[attrIdx][ti].LTy,
             decodedTextureTileAreas[attrIdx][ti].sizeX,
             decodedTextureTileAreas[attrIdx][ti].sizeY);
  }

  // geometry and attribute tiles and submeshIds in Tile
  auto numTilesGeometry  = params.numTilesGeometry;
  auto numTilesAttribute = params.numTilesAttribute;
  auto numTilesTotal     = numTilesGeometry + numTilesAttribute;

  for (int32_t frameIdx = 0; frameIdx < frameCountSubmesh0; frameIdx++) {
    for (uint32_t tileIdx = 0; tileIdx < numTilesGeometry; tileIdx++) {
      auto                atlPos = frameIdx * numTilesTotal + tileIdx;
      AtlasTileLayerRbsp& atl    = adStream.getAtlasTileLayerList()[atlPos];
      AtlasTileHeader&    ath    = atl.getHeader();

      uint32_t numSubmeshesInTile =
        (uint32_t)(numTilesGeometry == 1
                     ? encFrames.size()
                     : params.submeshIdsInTile[tileIdx].size());

      atl.getHeader().getPatchSizeXinfoQuantizer() =
        params.log2GeometryVideoBlockSize;
      atl.getHeader().getPatchSizeYinfoQuantizer() =
        params.log2GeometryVideoBlockSize;
      //create Atlas Patch Information
      atl.getHeader().setType(
        (uint8_t)I_TILE);  //currently we only have one INTRA frame/tile

      int32_t  predictorIdx      = 0;
      uint32_t patchCount0       = numSubmeshesInTile;
      uint32_t patchesPerSubmesh = 1;
      if (params.lodPatchesEnable == 1) {
        patchCount0 = patchCount0 * (params.subdivisionIterationCount + 1);
        patchesPerSubmesh =
          patchesPerSubmesh * (params.subdivisionIterationCount + 1);
      }

      for (uint32_t pi = 0; pi < patchCount0; pi++) {
        //mapping between patches and submeshes
        uint32_t si           = pi / patchesPerSubmesh;
        uint32_t submeshId    = params.submeshIdsInTile[tileIdx][si];
        int32_t  submeshIndex = submeshIdtoIndex_[submeshId];
        auto&    submesh      = encFrames[submeshIndex][frameIdx];
        auto&    pid          = atl.getDataUnit().getPatchInformationData(pi);
        submesh.atlPos        = atlPos;
        printf("Create AtlasPatchInfo: frame %zu\ttileIndex %d\tpatchIndex "
               "%d\tatlPos %d\ttileId %d\tsubmeshIndex %d\tsubmeshId "
               "%d\tvertex: %d\tfaces: %d\n",
               atl.getHeader().getFrameIndex(),
               tileIdx,
               pi,
               atlPos,
               atl.getHeader().getAtlasTileHeaderId(),
               submeshIndex,
               submeshId,
               submesh.subdiv.pointCount(),
               submesh.subdiv.triangleCount());
        int32_t referenceFrameIndex = frameIdx - 1;
        if (submesh.submeshType != basemesh::I_BASEMESH) {
          referenceFrameIndex = submesh.referenceFrameIndex;
        }
        int32_t referencePatchIndex = pi;
        predictorIdx =
          this->createAtlasPatchInformation(true,
                                            submesh,
                                            reconAtlasTiles_[tileIdx],
                                            frameIdx,
                                            tileIdx,
                                            pi,
                                            submeshId,
                                            referenceFrameIndex,
                                            referencePatchIndex,
                                            predictorIdx,
                                            asps,
                                            afps,
                                            atl,
                                            pid,
                                            tileAreasInVideo_,
                                            params);
      }
      //construct Atlas Header Ref List : setRefIndex() is done here
      this->constructAtlasHeaderRefListStruct(adStream, atl, frameIdx);
      //reconstruct Atlas Patches
      reconAtlasTiles_[tileIdx][frameIdx].patches_.resize(
        atl.getDataUnit().getPatchInformationData().size());
      for (auto& p : reconAtlasTiles_[tileIdx][frameIdx].patches_)
        p.attributePatchArea.resize(attributeCount);
      reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_ =
        decodedGeometryTileAreas[tileIdx];
//      reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_.resize(
//        attributeCount);
//      for (int attrIndex = 0; attrIndex < attributeCount; attrIndex++)
//        reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_[attrIndex] =
//          decodedTextureTileAreas[attrIndex][tileIdx];

      printf(
        "Reconstructed AtlasTileInfo: tileIndex %d\tLT(%d,%d)\tSize(%dx%d)\n",
        tileIdx,
        reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx,
        reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy,
        reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.sizeX,
        reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.sizeY);

      predictorIdx = 0;
      for (uint32_t patchIdx = 0; patchIdx < patchCount0; patchIdx++) {
        auto& pi     = atl.getDataUnit().getPatchInformationData()[patchIdx];
        predictorIdx = this->reconstructAtlasPatch(frameIdx,
                                                   tileIdx,
                                                   patchIdx,
                                                   pi,
                                                   atl,
                                                   asps,
                                                   afps,
                                                   reconAtlasTiles_,
                                                   predictorIdx);
        printf(
          "Reconstructed AtlasPatchInfo: frame %d\ttileIndex %d\tpatchIndex "
          "%d\tatlPos %d\ttileId %d\t(%d,%d)\t(%dx%d)\tsubmeshId %d\tvertex: "
          "%d\n",
          frameIdx,
          tileIdx,
          patchIdx,
          atlPos,
          reconAtlasTiles_[tileIdx][frameIdx].tileId_,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTx,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTy,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.sizeX,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.sizeY,
          reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_,
          reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].vertexCount);
      }

      auto patchType = static_cast<uint8_t>(
        (atl.getHeader().getType() == I_TILE) ? I_END : P_END);
      atl.getDataUnit().addPatchInformationData(patchType);
    }  //tile geometry

    // TODO: only one tile for attributes supported now
    for (uint32_t tileIdx = numTilesGeometry; tileIdx < numTilesTotal;
         tileIdx++) {
      auto atlPos = frameIdx * numTilesTotal + tileIdx;

      AtlasTileLayerRbsp& atl = adStream.getAtlasTileLayerList()[atlPos];
      AtlasTileHeader&    ath = atl.getHeader();
      // TODO: same parameters should be defined for attributes
      atl.getHeader().getPatchSizeXinfoQuantizer() =
        params.log2GeometryVideoBlockSize;
      atl.getHeader().getPatchSizeYinfoQuantizer() =
        params.log2GeometryVideoBlockSize;
      //create Atlas Patch Information
      atl.getHeader().setType((uint8_t)I_TILE_ATTR);
      int32_t  predictorIdx = 0;
      uint32_t patchCount0  = params.numSubmesh;

      for (uint32_t pi = 0; pi < patchCount0; pi++) {
        // mapping between patches and submeshes
        uint32_t si           = pi;
        uint32_t submeshId    = params.submeshIdsInTile[tileIdx][si];
        int32_t  submeshIndex = submeshIdtoIndex_[submeshId];
        auto&    submesh      = encFrames[submeshIndex][frameIdx];
        auto&    pid          = atl.getDataUnit().getPatchInformationData(pi);
        printf("Create Attribute AtlasPatchInfo: frame %zu\ttileIndex "
               "%d\tpatchIndex "
               "%d\tatlPos %d\ttileId %d\tsubmeshIndex %d\tsubmeshId %d\n",
               atl.getHeader().getFrameIndex(),
               tileIdx,
               pi,
               atlPos,
               atl.getHeader().getAtlasTileHeaderId(),
               submeshIndex,
               submeshId);
        // only intra for attribute patches
        int32_t referenceFrameIndex = frameIdx - 1;
        int32_t referencePatchIndex = pi;
        predictorIdx =
          this->createAtlasPatchInformation(false,
                                            submesh,
                                            reconAtlasTiles_[tileIdx],
                                            frameIdx,
                                            tileIdx,
                                            pi,
                                            submeshId,
                                            referenceFrameIndex,
                                            referencePatchIndex,
                                            predictorIdx,
                                            asps,
                                            afps,
                                            atl,
                                            pid,
                                            tileAreasInVideo_,
                                            params);
      }
      //construct Atlas Header Ref List : setRefIndex() is done here
      this->constructAtlasHeaderRefListStruct(adStream, atl, frameIdx);
      //reconstruct Atlas Patches
      reconAtlasTiles_[tileIdx][frameIdx].patches_.resize(
        atl.getDataUnit().getPatchInformationData().size());
      for (auto& p : reconAtlasTiles_[tileIdx][frameIdx].patches_) {
        p.attributePatchArea.resize(attributeCount);
      }
      reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_.resize(
        attributeCount);
      for (int attrIndex = 0; attrIndex < attributeCount; attrIndex++) {
        reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_[attrIndex] =
          decodedTextureTileAreas[attrIndex]
                                 [tileIdx - params.numTilesGeometry];
      }

      predictorIdx = 0;
      for (uint32_t patchIdx = 0; patchIdx < patchCount0; patchIdx++) {
        auto& pi     = atl.getDataUnit().getPatchInformationData()[patchIdx];
        predictorIdx = this->reconstructAtlasPatch(frameIdx,
                                                   tileIdx,
                                                   patchIdx,
                                                   pi,
                                                   atl,
                                                   asps,
                                                   afps,
                                                   reconAtlasTiles_,
                                                   predictorIdx);
        printf(
          "Reconstructed Attribute AtlasPatchInfo: frame %d\ttileIndex "
          "%d\tpatchIndex "
          "%d\tatlPos %d\ttileId %d\t(%d,%d)\t(%dx%d)\tsubmeshId %d\n",
          frameIdx,
          tileIdx,
          patchIdx,
          atlPos,
          reconAtlasTiles_[tileIdx][frameIdx].tileId_,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .LTx,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .LTy,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .sizeX,
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .sizeY,
          reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_);
      }

      auto patchType = static_cast<uint8_t>(
        (atl.getHeader().getType() == I_TILE_ATTR) ? I_END_ATTR : P_END_ATTR);
      atl.getDataUnit().addPatchInformationData(patchType);
    }  //tile attribute
  }    //frame
}

//============================================================================
int32_t
AtlasEncoder::createAtlasPatchInformation(
  bool                                 geometryPatchFlag,
  vmesh::VMCSubmesh&                          submesh,
  std::vector<AtlasTile>&              referenceFrames,
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  int32_t                              submeshId,
  int32_t                              referenceFrameIndex,
  int32_t                              referencePatchIndex,
  int32_t                              predictorPatchIndex,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  AtlasTileLayerRbsp&                  atl,
  PatchInformationData&                pid,
  std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
  const AtlasEncoderParameters&        params) {
  int32_t updatedPredictorPatchIndex = 0;

  if (
    frameIndex == 0 || params.analyzeGof == false
    || (!params.basemeshGOPList.empty())
    || geometryPatchFlag
         == false)  //first frame or all-intra or basemesh-ra.cfg or attribute patch
  {
    updatedPredictorPatchIndex =
      createAtlasMeshPatchInformation(submesh,
                                      frameIndex,
                                      tileIndex,
                                      patchIndex,
                                      submeshId,
                                      referencePatchIndex,
                                      predictorPatchIndex,
                                      asps,
                                      afps,
                                      atl,
                                      pid,
                                      tileAreasInVideo,
                                      params);

  } else if ((submesh.submeshType != basemesh::I_BASEMESH)
             && (params.iDeriveTextCoordFromPos != 3)
             && (params.subdivisionEdgeLengthThreshold == 0)
             && (params.lodPatchesEnable != 1)) {
    updatedPredictorPatchIndex =
      createAtlasMergePatchInformation(submesh,
                                       referenceFrames,
                                       frameIndex,
                                       tileIndex,
                                       patchIndex,
                                       submeshId,
                                       referenceFrameIndex,
                                       referencePatchIndex,
                                       predictorPatchIndex,
                                       asps,
                                       afps,
                                       atl,
                                       pid,
                                       tileAreasInVideo,
                                       params);

  } else {
    updatedPredictorPatchIndex =
      createAtlasInterPatchInformation(submesh,
                                       referenceFrames,
                                       frameIndex,
                                       tileIndex,
                                       patchIndex,
                                       submeshId,
                                       referenceFrameIndex,
                                       referencePatchIndex,
                                       predictorPatchIndex,
                                       asps,
                                       afps,
                                       atl,
                                       pid,
                                       tileAreasInVideo,
                                       params);
  }
  return updatedPredictorPatchIndex;
}

//============================================================================
int32_t
AtlasEncoder::createAtlasMeshPatchInformation(
  vmesh::VMCSubmesh&                          submesh,
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  int32_t                              submeshId,
  int32_t                              referenceFrameIndex,
  int32_t                              predictorPatchIndex,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  AtlasTileLayerRbsp&                  atl,
  PatchInformationData&                pid,
  std::vector<std::vector<AtlasTile>>& tileAreasInVideo_,
  const AtlasEncoderParameters&        params) {
  int32_t  updatedPredictorPatchIndex = predictorPatchIndex;
  int32_t  submeshPos                 = submeshIdtoIndex_[submeshId];
  auto&    ath                        = atl.getHeader();
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();
  uint32_t patchSizeXQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeXinfoQuantizer())
                                   : patchBlockSize;
  uint32_t patchSizeYQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeYinfoQuantizer())
                                   : patchBlockSize;

  printf("\t%s:: frameIndex[%d] tileIndex[%d] patchIndex[%d] submeshPos[%d] "
         "Id=%d\treferenceFrameIndex %d\n",
         toString((PatchModeITile)pid.getPatchMode()).c_str(),
         frameIndex,
         tileIndex,
         patchIndex,
         submeshPos,
         submeshId,
         referenceFrameIndex);

  MeshpatchDataUnit& mdu = pid.getMeshpatchDataUnit();
  mdu.setMduSubmeshId(submeshId);
  if (ath.getType() == I_TILE || ath.getType() == P_TILE) {
    mdu.setMduDisplId(submeshId);
    uint32_t pixelsPerBlock = (1 << params.log2GeometryVideoBlockSize)
                              * (1 << params.log2GeometryVideoBlockSize);
    uint32_t lodCount = submesh.subdivInfoLevelOfDetails.size();
    mdu.getMduBlockCount().resize(lodCount);
    mdu.getMduLastPosInBlock().resize(lodCount);
    for (size_t level = 0; level < lodCount; level++) {
      uint32_t pointPerLod =
        submesh.subdivInfoLevelOfDetails[level].pointCount;
      if (level != 0)
        pointPerLod -= submesh.subdivInfoLevelOfDetails[level - 1].pointCount;
      uint32_t posInBlock = pointPerLod % pixelsPerBlock;
      uint32_t blockPerLod =
        (pointPerLod + pixelsPerBlock - 1) / pixelsPerBlock;
      mdu.setMduBlockCount(level, blockPerLod);
      mdu.setMduLastPosInBlock(level, posInBlock);
    }

    //mdu.setPduFaceCountMinus1(submesh.subdiv.triangleCount() - 1);
    mdu.setMduSubdivisionIterationCountPresentFlag(false);
    mdu.setMduSubdivisionMethodPresentFlag(false);
    mdu.setMduTransformMethodPresentFlag(false);
    mdu.setMduTransformParametersPresentFlag(false);

    if (params.transformMethod == 1 && params.applyLiftingOffset) {
      mdu.setMduLiftingOffsetPresentFlag(true);
    }
    if (params.transformMethod == 1 && params.dirlift) {
      mdu.setMduDirectionalLiftingPresentFlag(true);
    }
    mdu.setMduSubdivisionIterationCount(
      asps.getAsveExtension().getAsveSubdivisionIterationCount());
    if (mdu.getMduSubdivisionIterationCount() > 0) {
      mdu.setMduLodAdaptiveSubdivisionFlag(
        asps.getAsveExtension().getAsveLodAdaptiveSubdivisionFlag());
    }
    for (int32_t it = 0;
         it < asps.getAsveExtension().getAsveSubdivisionIterationCount();
         it++) {
      if (mdu.getMduLodAdaptiveSubdivisionFlag() || it == 0)
        mdu.setMduSubdivisionMethod(
          it, asps.getAsveExtension().getAsveSubdivisionMethod()[it]);
      else
        mdu.setMduSubdivisionMethod(
          it, asps.getAsveExtension().getAsveSubdivisionMethod()[0]);
    }
    if (asps.getAsveExtension().getAsveEdgeBasedSubdivisionFlag()) {
      // change this if PDU value is different from AFPS value, for now they are the same value
      mdu.setMduEdgeBasedSubdivisionFlag(false);
      mdu.setMduSubdivisionMinEdgeLength(
        params.subdivisionEdgeLengthThreshold);

    } else {
      mdu.setMduEdgeBasedSubdivisionFlag(false);
    }
    mdu.getMduLiftingTransformParameters() =
      asps.getAsveExtension().getAspsExtLtpDisplacement();
    mdu.setMduDisplacementCoordinateSystem(
      (int)params.displacementCoordinateSystem);
    mdu.setMduTransformMethod(
      asps.getAsveExtension().getAsveTransformMethod());
    //pdu.getMduLiftingTransformParameters().copy( asps.getAsveExtension().getAspsExtLtpDisplacement() );
    if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
      mdu.setMduParametersOverrideFlag(true);
      mdu.setMduSubdivisionIterationCountPresentFlag(true);
      mdu.setMduSubdivisionMethodPresentFlag(true);
      mdu.setMduSubdivisionIterationCount(
        params.subdivisionIterationCount ? params.subdivisionIterationCount + 1
                                         : params.subdivisionIterationCount);
      if (params.subdivisionIterationCount)
        mdu.setMduSubdivisionMethod(
          params.subdivisionIterationCount,
          mdu.getMduSubdivisionMethod()
            [params.subdivisionIterationCount
             - 1]);  //copying the last iteration subdivision
      if (params.IQSkipFlag) {
        mdu.setMduQuantizationPresentFlag(false);
      } else {
        mdu.setMduQuantizationPresentFlag(
          true);  // When increaseTopSubmeshSubdivisionCount=1, the PduQuantizationOverrideFlag need to be enabled
        auto& qp       = mdu.getMduQuantizationParameters();
        auto  lodCount = mdu.getMduSubdivisionIterationCount() + 1;
        qp.setNumLod(lodCount);
        auto dimCount = params.applyOneDimensionalDisplacement ? 1 : 3;
        qp.setNumComponents(dimCount);
        qp.setVdmcLodQuantizationFlag(params.lodDisplacementQuantizationFlag);
        qp.setVdmcBitDepthOffset(params.bitDepthOffset);
        if (params.lodDisplacementQuantizationFlag == 0) {
          for (int d = 0; d < dimCount; d++) {
            qp.setVdmcQuantizationParameters(d, params.liftingQP[d]);
            qp.setVdmcLog2LodInverseScale(
              d, params.log2LevelOfDetailInverseScale[d]);
          }
        } else {
          for (int l = 0; l < lodCount; l++) {
            qp.allocComponents(dimCount, l);
            for (int d = 0; d < dimCount; d++) {
              int8_t value = params.qpPerLevelOfDetails[l][d]
                             - (asps.getAsveExtension()
                                  .getAsveDisplacementReferenceQPMinus49()
                                + 49);
              qp.setVdmcLodDeltaQPValue(l, d, std::abs(value));
              qp.setVdmcLodDeltaQPSign(l, d, (value < 0) ? 1 : 0);
            }
          }
        }
        qp.setVdmcDirectQuantizationEnabledFlag(
          params.displacementQuantizationType
          == vmesh::DisplacementQuantizationType::ADAPTIVE);
      }
      if (mdu.getMduTransformMethod() == 1) {
        mdu.setMduTransformParametersPresentFlag(true);
        auto& mduLtp = mdu.getMduLiftingTransformParameters();
        mduLtp.setNumLod(mdu.getMduSubdivisionIterationCount());
        mduLtp.setAdaptiveUpdateWeightFlag(
          params.liftingAdaptiveUpdateWeightFlag);
        mduLtp.setValenceUpdateWeightFlag(
          params.liftingValenceUpdateWeightFlag);
        for (int l = 0; l < mdu.getMduSubdivisionIterationCount()
                        && !params.liftingSkipUpdate;
             l++) {
          if (params.liftingAdaptiveUpdateWeightFlag || (l == 0)) {
            mduLtp.setLiftingUpdateWeightNumerator(
              l, params.liftingUpdateWeightNumerator[l]);
            mduLtp.setLiftingUpdateWeightDenominatorMinus1(
              l, params.liftingUpdateWeightDenominatorMinus1[l]);
          }
        }
        mduLtp.setAdaptivePredictionWeightFlag(
          params.liftingAdaptivePredictionWeightFlag);
        if (params.liftingAdaptivePredictionWeightFlag) {
          for (int l = 0; l < mdu.getMduSubdivisionIterationCount(); l++) {
            mduLtp.setLiftingPredictionWeightNumerator(
              l, params.liftingPredictionWeightNumerator[l]);
            mduLtp.setLiftingPredictionWeightDenominatorMinus1(
              l, params.liftingPredictionWeightDenominatorMinus1[l]);
          }
        } else {
          for (int l = 0; l < mdu.getMduSubdivisionIterationCount(); l++) {
            mduLtp.setLiftingPredictionWeightNumerator(
              l, params.liftingPredictionWeightNumeratorDefault[l]);
            mduLtp.setLiftingPredictionWeightDenominatorMinus1(
              l, params.liftingPredictionWeightDenominatorMinus1Default[l]);
          }
        }
        mduLtp.setDirectionalLiftingScale1(params.dirliftScale1);
        mduLtp.setDirectionalLiftingDeltaScale2(params.dirliftDeltaScale2);
        mduLtp.setDirectionalLiftingDeltaScale3(params.dirliftDeltaScale3);
        mduLtp.setDirectionalLiftingScaleDenoMinus1(
          params.dirliftScaleDenoMinus1);
      }
    }

    if (params.encodeDisplacementType == 2) {
      mdu.setMduLoDIdx(
        tileAreasInVideo_[tileIndex][frameIndex].patches_[patchIndex].lodIdx_);
      mdu.setMduGeometry2dPosX((tileAreasInVideo_[tileIndex][frameIndex]
                                  .patches_[patchIndex]
                                  .geometryPatchArea.LTx)
                               / patchBlockSize);
      mdu.setMduGeometry2dPosY((tileAreasInVideo_[tileIndex][frameIndex]
                                  .patches_[patchIndex]
                                  .geometryPatchArea.LTy)
                               / patchBlockSize);
      mdu.setMduGeometry2dSizeXMinus1((tileAreasInVideo_[tileIndex][frameIndex]
                                         .patches_[patchIndex]
                                         .geometryPatchArea.sizeX
                                       + patchBlockSize - 1)
                                        / patchBlockSize
                                      - 1);
      mdu.setMduGeometry2dSizeYMinus1((tileAreasInVideo_[tileIndex][frameIndex]
                                         .patches_[patchIndex]
                                         .geometryPatchArea.sizeY
                                       + patchBlockSize - 1)
                                        / patchBlockSize
                                      - 1);
    } else {
      mdu.setMduLoDIdx(0);
      mdu.setMduGeometry2dPosX(0);
      mdu.setMduGeometry2dPosY(0);
      mdu.setMduGeometry2dSizeXMinus1(0);
      mdu.setMduGeometry2dSizeYMinus1(0);
    }

    if (params.iDeriveTextCoordFromPos >= 1) {
      createAtlasMeshSubPatch(submesh,
                              submeshPos,
                              frameIndex,
                              mdu.getTextureProjectionInformation(),
                              params.bitDepthTexCoord,
                              params.packingScaling,
                              params.use45DegreeProjection,
                              params.bFaceIdPresentFlag);
    }
    if (params.transformMethod == 1) {
      if (params.applyLiftingOffset) {
        if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
          for (int i = 0; i < params.subdivisionIterationCount + 1; i++) {
            mdu.getMduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
              i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0));
            mdu.getMduLiftingTransformParameters()
              .setLiftingOffsetValuesDenominatorMinus1(i, 99);
          }
        } else {
          for (int i = 0; i < params.subdivisionIterationCount; i++) {
            mdu.getMduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
              i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0));
            mdu.getMduLiftingTransformParameters()
              .setLiftingOffsetValuesDenominatorMinus1(i, 99);
          }
        }
      }

      if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
        mdu.setMduDirectionalLiftingMeanNum(
          (int32_t)(submesh.liftingdispstat[0] * 100.0));
        mdu.setMduDirectionalLiftingMeanDenoMinus1(99);
        mdu.setMduDirectionalLiftingStdNum(
          (int32_t)(submesh.liftingdispstat[1] * 100.0));
        mdu.setMduDirectionalLiftingStdDenoMinus1(99);
      }
    }
  } else if (ath.getType() == I_TILE_ATTR || ath.getType() == P_TILE_ATTR) {
      for (int attrIdx = 0;
           attrIdx
           < asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
           attrIdx++) {
        mdu.setMduAttributes2dPosX(tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[attrIdx]
                                     .LTx
                                   / patchBlockSize);
        mdu.setMduAttributes2dPosY(tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[attrIdx]
                                     .LTy
                                   / patchBlockSize);
        mdu.setMduAttributes2dSizeXMinus1(
          (tileAreasInVideo_[tileIndex][frameIndex]
             .patches_[patchIndex]
             .attributePatchArea[attrIdx]
             .sizeX
           + patchSizeXQuantizer - 1)
            / patchSizeXQuantizer
          - 1);
        mdu.setMduAttributes2dSizeYMinus1(
          (tileAreasInVideo_[tileIndex][frameIndex]
             .patches_[patchIndex]
             .attributePatchArea[attrIdx]
             .sizeY
           + patchSizeYQuantizer - 1)
            / patchSizeYQuantizer
          - 1);
      }
    }
  return updatedPredictorPatchIndex;
}

//============================================================================
int32_t
AtlasEncoder::createAtlasMergePatchInformation(
  vmesh::VMCSubmesh&                          submesh,
  std::vector<AtlasTile>&              referenceFrames,
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  int32_t                              submeshId,
  int32_t                              referenceFrameIndex,
  int32_t                              referencePatchIndex,
  int32_t                              predictorPatchIndex,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  AtlasTileLayerRbsp&                  atl,
  PatchInformationData&                pid,
  std::vector<std::vector<AtlasTile>>& tileAreasInVideo_,
  const AtlasEncoderParameters&        params) {
  int32_t  updatedPredictorPatchIndex = predictorPatchIndex;
  int32_t  submeshPos                 = submeshIdtoIndex_[submeshId];
  auto&    ath                        = atl.getHeader();
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  atl.getHeader().setType((uint8_t)P_TILE);
  pid.setPatchMode(static_cast<uint8_t>(P_MERGE));
  printf("\t%s:: frameIndex[%d] tileIndex[%d] patchIndex[%d] submeshPos[%d] "
         "Id=%d\treferenceFrameIndex %d\n",
         toString((PatchModePTile)pid.getPatchMode()).c_str(),
         frameIndex,
         tileIndex,
         patchIndex,
         submeshPos,
         submeshId,
         referenceFrameIndex);

  auto& mmdu = pid.getMergeMeshpatchDataUnit();
  mmdu.setRefereceFrameIndex(referenceFrameIndex);
  updatedPredictorPatchIndex = referencePatchIndex + 1;

  if (params.iDeriveTextCoordFromPos >= 1) {
    mmdu.setMmduTextureProjectionPresentFlag(true);
    auto& refSubmesh =
      referenceFrames[referenceFrameIndex].patches_[referencePatchIndex];
    createAtlasMeshSubPatchMerge(submesh,
                                 submeshPos,
                                 frameIndex,
                                 mmdu.getTextureProjectionMergeInformation(),
                                 params.packingScaling,
                                 refSubmesh);
  }
  mmdu.setMmduSubdivisionIterationCountPresentFlag(false);
  mmdu.setMmduSubdivisionIterationCount(
    asps.getAsveExtension().getAsveSubdivisionIterationCount());
  if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
    mmdu.setMmduSubdivisionIterationCountPresentFlag(true);
    mmdu.setMmduSubdivisionIterationCount(
      params.subdivisionIterationCount ? params.subdivisionIterationCount + 1
                                       : params.subdivisionIterationCount);
  }

  mmdu.getMmduLiftingTransformParameters() =
    asps.getAsveExtension().getAspsExtLtpDisplacement();
  if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
    mmdu.getMmduLiftingTransformParameters() =
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter;
  }
  if (params.applyLiftingOffset) {
    mmdu.setMmduLiftingOffsetPresentFlag(true);
    if (referenceFrames[referenceFrameIndex]
          .patches_[referencePatchIndex]
          .subdivIteration
        == 0) {
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter.setLiftingOffsetValuesNumerator(0, 0);
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter.setLiftingOffsetValuesDenominatorMinus1(0, 0);
    }

    if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
      for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount() + 1; i++) {
        int32_t refNum =
          referenceFrames[referenceFrameIndex]
            .patches_[referencePatchIndex]
            .transformParameter.getLiftingOffsetValuesNumerator()[i];
        int32_t refDeno =
          referenceFrames[referenceFrameIndex]
            .patches_[referencePatchIndex]
            .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[i];
        mmdu.getMmduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
          i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
        mmdu.getMmduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
          i, 99 - refDeno);
      }
    }
    else {
      for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount(); i++) {
        int32_t refNum =
          referenceFrames[referenceFrameIndex]
            .patches_[referencePatchIndex]
            .transformParameter.getLiftingOffsetValuesNumerator()[i];
        int32_t refDeno =
          referenceFrames[referenceFrameIndex]
            .patches_[referencePatchIndex]
            .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[i];
        mmdu.getMmduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
          i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
        mmdu.getMmduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
          i, 99 - refDeno);
      }
    }
  }

  if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
    mmdu.setMmduDirectionalLiftingPresentFlag(true);
    int32_t refMeanNum = referenceFrames[referenceFrameIndex]
                           .patches_[referencePatchIndex]
                           .directionalLiftParameterss[0];
    int32_t refMeanDeno = referenceFrames[referenceFrameIndex]
                            .patches_[referencePatchIndex]
                            .directionalLiftParameterss[1];
    int32_t refStdNum = referenceFrames[referenceFrameIndex]
                          .patches_[referencePatchIndex]
                          .directionalLiftParameterss[2];
    int32_t refStdDeno = referenceFrames[referenceFrameIndex]
                           .patches_[referencePatchIndex]
                           .directionalLiftParameterss[3];
    mmdu.setMmduDirectionalLiftingDeltaMeanNum(
      (int32_t)(submesh.liftingdispstat[0] * 100.0) - refMeanNum);
    mmdu.setMmduDirectionalLiftingDeltaMeanDeno(99 - refMeanDeno);
    mmdu.setMmduDirectionalLiftingDeltaStdNum(
      (int32_t)(submesh.liftingdispstat[1] * 100.0) - refStdNum);
    mmdu.setMmduDirectionalLiftingDeltaStdDeno(99 - refStdDeno);
  }

  return updatedPredictorPatchIndex;
}

//============================================================================
int32_t
AtlasEncoder::createAtlasInterPatchInformation(
  vmesh::VMCSubmesh&                          submesh,
  std::vector<AtlasTile>&              referenceFrames,
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  int32_t                              submeshId,
  int32_t                              referenceFrameIndex,
  int32_t                              referencePatchIndex,
  int32_t                              predictorPatchIndex,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  AtlasTileLayerRbsp&                  atl,
  PatchInformationData&                pid,
  std::vector<std::vector<AtlasTile>>& tileAreasInVideo_,
  const AtlasEncoderParameters&        params) {
  int32_t  updatedPredictorPatchIndex = predictorPatchIndex;
  int32_t  submeshPos                 = submeshIdtoIndex_[submeshId];
  auto&    ath                        = atl.getHeader();
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  //I_BASEMESH -> P_INTER atlas patch
  atl.getHeader().setType((uint8_t)P_TILE);
  pid.setPatchMode(static_cast<uint8_t>(P_INTER));
  printf("\t%s:: frameIndex[%d] submeshPos[%d] Id=%d\treferenceFrameIndex "
         "%d, referencePatchIndex %d, predictor %d\n",
         toString((PatchModePTile)pid.getPatchMode()).c_str(),
         frameIndex,
         submeshPos,
         submeshId,
         referenceFrameIndex,
         referencePatchIndex,
         predictorPatchIndex);

  auto& imdu = pid.getInterMeshpatchDataUnit();
  //imdu.setImduSubmeshId(submeshId);
  imdu.setRefereceFrameIndex(referenceFrameIndex);
  imdu.setImduPatchIndex(referencePatchIndex - predictorPatchIndex);
  updatedPredictorPatchIndex = referencePatchIndex + 1;
  imdu.setImduSubdivisionIterationCountPresentFlag(false);
  imdu.setImduSubdivisionIterationCount(
    asps.getAsveExtension().getAsveSubdivisionIterationCount());
  imdu.setImduTransformMethod(
    asps.getAsveExtension().getAsveTransformMethod());
  if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
    imdu.setImduSubdivisionIterationCountPresentFlag(true);
    imdu.setImduSubdivisionIterationCount(
      params.subdivisionIterationCount ? params.subdivisionIterationCount + 1
                                       : params.subdivisionIterationCount);
  }

  uint32_t pixelsPerBlock = (1 << params.log2GeometryVideoBlockSize)
                            * (1 << params.log2GeometryVideoBlockSize);
  uint32_t locCount = submesh.subdivInfoLevelOfDetails.size();
  imdu.getImduDeltaBlockCount().resize(locCount);
  imdu.getImduDeltaLastPosInBlock().resize(locCount);
  for (size_t level = 0; level < locCount; level++) {
    uint32_t pointPerLod = submesh.subdivInfoLevelOfDetails[level].pointCount;
    if (level != 0)
      pointPerLod -= submesh.subdivInfoLevelOfDetails[level - 1].pointCount;
    uint32_t posInBlock  = pointPerLod % pixelsPerBlock;
    uint32_t blockPerLod = (pointPerLod + pixelsPerBlock - 1) / pixelsPerBlock;
    imdu.setImduDeltaBlockCount(level,
                                      blockPerLod
                                        - referenceFrames[referenceFrameIndex]
                                            .patches_[referencePatchIndex]
                                            .blockCount[level]);
    imdu.setImduDeltaLastPosInBlock(level,
                                    posInBlock
                                      - referenceFrames[referenceFrameIndex]
                                          .patches_[referencePatchIndex]
                                          .lastPosInBlock[level]);
  }
  if (params.encodeDisplacementType == 2) {
    imdu.setImduLoDIdx(
      tileAreasInVideo_[tileIndex][frameIndex].patches_[patchIndex].lodIdx_);
    int32_t refTLx = referenceFrames[referenceFrameIndex]
                       .patches_[referencePatchIndex]
                       .geometryPatchArea.LTx;
    int32_t refTLy = referenceFrames[referenceFrameIndex]
                       .patches_[referencePatchIndex]
                       .geometryPatchArea.LTy;
    int32_t refSizeX = referenceFrames[referenceFrameIndex]
                         .patches_[referencePatchIndex]
                         .geometryPatchArea.sizeX;
    int32_t refSizeY = referenceFrames[referenceFrameIndex]
                         .patches_[referencePatchIndex]
                         .geometryPatchArea.sizeY;
    imdu.setImdu2dDeltaPosX((tileAreasInVideo_[tileIndex][frameIndex]
                                       .patches_[patchIndex]
                                       .geometryPatchArea.LTx
                                     - refTLx + patchBlockSize - 1)
                                    / patchBlockSize);
    imdu.setImdu2dDeltaPosY((tileAreasInVideo_[tileIndex][frameIndex]
                                       .patches_[patchIndex]
                                       .geometryPatchArea.LTy
                                     - refTLy + patchBlockSize - 1)
                                    / patchBlockSize);
    imdu.setImdu2dDeltaSizeX((tileAreasInVideo_[tileIndex][frameIndex]
                                        .patches_[patchIndex]
                                        .geometryPatchArea.sizeX
                                      - refSizeX + patchBlockSize - 1)
                                     / patchBlockSize);
    imdu.setImdu2dDeltaSizeY((tileAreasInVideo_[tileIndex][frameIndex]
                                        .patches_[patchIndex]
                                        .geometryPatchArea.sizeY
                                      - refSizeY + patchBlockSize - 1)
                                     / patchBlockSize);

  } else {
    imdu.setImduLoDIdx(0);
    imdu.setImdu2dDeltaPosX(0);
    imdu.setImdu2dDeltaPosY(0);
    imdu.setImdu2dDeltaSizeX(0);
    imdu.setImdu2dDeltaSizeY(0);
  }

  if (params.iDeriveTextCoordFromPos >= 1) {
    // will use the INTRA parameters for default packing, but we can use some other criteria
    imdu.setImduTextureProjectionPresentFlag(true);
    auto& refSubmesh =
      referenceFrames[referenceFrameIndex].patches_[referencePatchIndex];
    bool enableSubpatchInter = (params.packingType > 0);
    if ((submesh.submeshType != basemesh::I_BASEMESH)
        && ((params.iDeriveTextCoordFromPos == 3)))
      enableSubpatchInter = true;
    createAtlasMeshSubPatchInter(
      submesh,
      submeshPos,
      frameIndex,
      imdu.getTextureProjectionInterInformation(),
      params.packingScaling,
      refSubmesh,
      enableSubpatchInter,
      params.use45DegreeProjection,
      params
        .bFaceIdPresentFlag);  // for default packing, it will use only INTRA sub-patches, for other packing methods, it decides based on a sub-patch basis
  }
  imdu.getImduLiftingTransformParameters() =
    asps.getAsveExtension().getAspsExtLtpDisplacement();
  imdu.setImduTransformParametersPresentFlag(false);
  if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
    imdu.getImduLiftingTransformParameters() =
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter;  //temporal bugfix for params.increaseTopSubmeshSubdivisionCount is enabled, parse the missing lifting parameter from the reference patch to avoid encoder&decoder mismatches on lifting parameter set and bitstream
  }
  if (params.applyLiftingOffset) {
    imdu.setImduLiftingOffsetPresentFlag(true);
    if (referenceFrames[referenceFrameIndex]
          .patches_[referencePatchIndex]
          .subdivIteration
        == 0) {
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter.setLiftingOffsetValuesNumerator(0, 0);
      referenceFrames[referenceFrameIndex]
        .patches_[referencePatchIndex]
        .transformParameter.setLiftingOffsetValuesDenominatorMinus1(0, 0);
    }
    if (params.increaseTopSubmeshSubdivisionCount && submeshId == 0) {
      if (imdu.getImduSubdivisionIterationCount()
          > referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .subdivIteration) {
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount() + 1; i++) {
          int32_t refNum =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesNumerator()[0];
          int32_t refDeno =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[0];
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
            i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
            i, 99 - refDeno);
        }
      } else {
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount() + 1; i++) {
          int32_t refNum =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesNumerator()[i];
          int32_t refDeno =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[i];
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
            i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
            i, 99 - refDeno);
        }
      }
    } else {
      if (imdu.getImduSubdivisionIterationCount()
          > referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .subdivIteration) {
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount(); i++) {
          int32_t refNum =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesNumerator()[0];
          int32_t refDeno =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[0];
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
            i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
            i, 99 - refDeno);
        }
      } else {
        for (int i = 0; i < imdu.getImduSubdivisionIterationCount(); i++) {
          int32_t refNum =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesNumerator()[i];
          int32_t refDeno =
            referenceFrames[referenceFrameIndex]
              .patches_[referencePatchIndex]
              .transformParameter.getLiftingOffsetValuesDenominatorMinus1()[i];
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesNumerator(
            i, (int32_t)(submesh.liftingdisplodmean[i] * 100.0) - refNum);
          imdu.getImduLiftingTransformParameters().setLiftingOffsetDeltaValuesDenominator(
            i, 99 - refDeno);
        }
      }
    }
  }

  if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
    imdu.setImduDirectionalLiftingPresentFlag(true);
    int32_t refMeanNum = referenceFrames[referenceFrameIndex]
                           .patches_[referencePatchIndex]
                           .directionalLiftParameterss[0];
    int32_t refMeanDeno = referenceFrames[referenceFrameIndex]
                            .patches_[referencePatchIndex]
                            .directionalLiftParameterss[1];
    int32_t refStdNum = referenceFrames[referenceFrameIndex]
                          .patches_[referencePatchIndex]
                          .directionalLiftParameterss[2];
    int32_t refStdDeno = referenceFrames[referenceFrameIndex]
                           .patches_[referencePatchIndex]
                           .directionalLiftParameterss[3];
    imdu.setImduDirectionalLiftingDeltaMeanNum(
      (int32_t)(submesh.liftingdispstat[0] * 100.0) - refMeanNum);
    imdu.setImduDirectionalLiftingDeltaMeanDeno(99 - refMeanDeno);
    imdu.setImduDirectionalLiftingDeltaStdNum(
      (int32_t)(submesh.liftingdispstat[1] * 100.0) - refStdNum);
    imdu.setImduDirectionalLiftingDeltaStdDeno(99 - refStdDeno);
  }

  return updatedPredictorPatchIndex;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatchIntra(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  printf("\tI/P_INTRA\n");
  auto& ath            = atl.getHeader();
  auto& atdu           = atl.getDataUnit();
  auto& aspsVmcExt     = asps.getAsveExtension();
  auto& reconAtlasTile = reconAtlasTiles_[tileIndex][frameIndex];
  auto& reconAtlasPatch =
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  auto& pdu                  = pi.getMeshpatchDataUnit();
  reconAtlasPatch.submeshId_ = pdu.getMduSubmeshId();
  reconAtlasPatch.lodIdx_    = pdu.getMduLoDIdx();
  reconAtlasPatch.geometryPatchArea.sizeX =
    (int)(pdu.getMduGeometry2dSizeXMinus1() + 1) * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.sizeY =
    (int)(pdu.getMduGeometry2dSizeYMinus1() + 1) * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.LTx =
    (int)pdu.getMduGeometry2dPosX() * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.LTy =
    (int)pdu.getMduGeometry2dPosY() * patchBlockSize;

  reconAtlasPatch.directionalLiftParameterss.resize(4);
  reconAtlasPatch.directionalLiftParameterss[0] =
    pdu.getMduDirectionalLiftingMeanNum();
  reconAtlasPatch.directionalLiftParameterss[1] =
    pdu.getMduDirectionalLiftingMeanDenoMinus1();
  reconAtlasPatch.directionalLiftParameterss[2] =
    pdu.getMduDirectionalLiftingStdNum();
  reconAtlasPatch.directionalLiftParameterss[3] =
    pdu.getMduDirectionalLiftingStdDenoMinus1();

  reconAtlasPatch.subdivIteration = pdu.getMduSubdivisionIterationCount();
  if (reconAtlasPatch.subdivIteration > 0) {
    reconAtlasPatch.subdivMethod.resize(reconAtlasPatch.subdivIteration);
    for (int it = 0; it < reconAtlasPatch.subdivIteration; it++)
      reconAtlasPatch.subdivMethod[it] = pdu.getMduSubdivisionMethod()[it];
  }
  reconAtlasPatch.subdivMinEdgeLength = pdu.getMduSubdivisionMinEdgeLength();
  reconAtlasPatch.displacementCoordinateSystem =
    (vmesh::DisplacementCoordinateSystem)pdu.getMduDisplacementCoordinateSystem();
  reconAtlasPatch.transformMethod    = (int)pdu.getMduTransformMethod();
  reconAtlasPatch.transformParameter = pdu.getMduLiftingTransformParameters();
  uint32_t lodCount                  = reconAtlasPatch.subdivIteration + 1;
  reconAtlasPatch.blockCount.resize(lodCount);
  reconAtlasPatch.lastPosInBlock.resize(lodCount);
  for (int level = 0; level < lodCount; level++) {
    reconAtlasPatch.blockCount[level] =
      pdu.getMduBlockCount()[level];
    reconAtlasPatch.lastPosInBlock[level] = pdu.getMduLastPosInBlock()[level];
  }

  if (asps.getAsveExtension().getAsveLodPatchesEnableFlag()) {
    int32_t blockSize = 1 << asps.getLog2PatchPackingBlockSize();
    int32_t lodIdx    = pdu.getMduLoDIdx();
    reconAtlasPatch.vertexCount =
      ((reconAtlasPatch.blockCount[lodIdx] > 0) ? (reconAtlasPatch.blockCount[lodIdx] - 1) * blockSize * blockSize : 0)
      + reconAtlasPatch.lastPosInBlock[lodIdx];
  }

  if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
    int smIdx = afps.getAfveExtension()
                  .getAtlasFrameMeshInformation()
                  ._submeshIDToIndex[reconAtlasPatch.submeshId_];
    if (afps.getAfveExtension().getProjectionTextcoordPresentFlag(smIdx)) {
      printf("reconstructing orthoAtlas parameters (INTRA)\n");
      auto qpTexCoord =
        aspsVmcExt.getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
      reconstructTextureProjectionInformation(
        pdu.getTextureProjectionInformation(),
        aspsVmcExt.getAspsProjectionTexcoordUpscaleFactor()
          / aspsVmcExt.getAspsProjectionTexcoordDownscaleFactor(),
        qpTexCoord,
        asps.getExtendedProjectionEnabledFlag(),
        reconAtlasPatch);
    }
  }
  return predictorIdx;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatchInter(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  auto& ath            = atl.getHeader();
  auto& atdu           = atl.getDataUnit();
  auto& aspsVmcExt     = asps.getAsveExtension();
  auto& reconAtlasTile = reconAtlasTiles_[tileIndex][frameIndex];
  auto& reconAtlasPatch =
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  auto&   imdu                = pi.getInterMeshpatchDataUnit();
  int32_t referenceFrameIndex = imdu.getRefereceFrameIndex();
  auto    referencePatchIndex = predictorIdx + imdu.getImduPatchIndex();
  printf("\tP_INTER sigRefIndex: %d(refFrame:%d) referencePatchIndex: "
         "%d(%d+%d)\n",
         imdu.getImduRefIndex(),
         imdu.getRefereceFrameIndex(),
         referencePatchIndex,
         predictorIdx,
         imdu.getImduPatchIndex());

  auto& refPatch = reconAtlasTiles_[tileIndex][referenceFrameIndex]
                     .patches_[referencePatchIndex];
  reconAtlasPatch   = refPatch;
  uint32_t lodCount = reconAtlasPatch.subdivIteration + 1;
  reconAtlasPatch.blockCount.resize(lodCount);
  reconAtlasPatch.lastPosInBlock.resize(lodCount);
  for (int level = 0; level < lodCount; level++) {
    reconAtlasPatch.blockCount[level] =
      refPatch.blockCount[level]
      + imdu.getImduDeltaBlockCount()[level];
    reconAtlasPatch.lastPosInBlock[level] =
      refPatch.lastPosInBlock[level]
      + imdu.getImduDeltaLastPosInBlock()[level];
  }
  reconAtlasPatch.geometryPatchArea.sizeX =
    refPatch.geometryPatchArea.sizeX
    + (int)imdu.getImdu2dDeltaSizeX() * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.sizeY =
    refPatch.geometryPatchArea.sizeY
    + (int)imdu.getImdu2dDeltaSizeY() * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.LTx =
    refPatch.geometryPatchArea.LTx
    + (int)imdu.getImdu2dDeltaPosX() * patchBlockSize;
  reconAtlasPatch.geometryPatchArea.LTy =
    refPatch.geometryPatchArea.LTy
    + (int)imdu.getImdu2dDeltaPosY() * patchBlockSize;

  reconAtlasPatch.subdivIteration = refPatch.subdivIteration;
  if (reconAtlasPatch.subdivIteration > 0) {
    reconAtlasPatch.subdivMethod.resize(reconAtlasPatch.subdivIteration);
    for (int it = 0; it < reconAtlasPatch.subdivIteration; it++)
      reconAtlasPatch.subdivMethod[it] = refPatch.subdivMethod[it];
  }
  reconAtlasPatch.subdivMinEdgeLength = refPatch.subdivMinEdgeLength;
  reconAtlasPatch.displacementCoordinateSystem =
    refPatch.displacementCoordinateSystem;
  reconAtlasPatch.transformMethod    = refPatch.transformMethod;
  reconAtlasPatch.transformParameter = refPatch.transformParameter;

  if (aspsVmcExt.getAsveLiftingOffsetPresentFlag()) {
    if (reconAtlasPatch.subdivIteration > refPatch.subdivIteration) {
      for (int i = 0; i < reconAtlasPatch.subdivIteration; i++) {
        reconAtlasPatch.transformParameter.setLiftingOffsetValuesNumerator(
          i,
          refPatch.transformParameter.getLiftingOffsetValuesNumerator()[0]
            + (int)imdu.getImduLiftingTransformParameters()
                .getLiftingOffsetDeltaValuesNumerator()[i]);
        reconAtlasPatch.transformParameter
          .setLiftingOffsetValuesDenominatorMinus1(
            i,
            refPatch.transformParameter
                .getLiftingOffsetValuesDenominatorMinus1()[0]
              + (int)imdu.getImduLiftingTransformParameters()
                  .getLiftingOffsetDeltaValuesDenominator()[i]);
      }
    } else {
      for (int i = 0; i < reconAtlasPatch.subdivIteration; i++) {
        reconAtlasPatch.transformParameter.setLiftingOffsetValuesNumerator(
          i,
          refPatch.transformParameter.getLiftingOffsetValuesNumerator()[i]
            + (int)imdu.getImduLiftingTransformParameters()
                .getLiftingOffsetDeltaValuesNumerator()[i]);
        reconAtlasPatch.transformParameter
          .setLiftingOffsetValuesDenominatorMinus1(
            i,
            refPatch.transformParameter
                .getLiftingOffsetValuesDenominatorMinus1()[i]
              + (int)imdu.getImduLiftingTransformParameters()
                  .getLiftingOffsetDeltaValuesDenominator()[i]);
      }
    }
  }
  reconAtlasPatch.directionalLiftParameterss.resize(4);
  reconAtlasPatch.directionalLiftParameterss[0] =
    refPatch.directionalLiftParameterss[0]
    + imdu.getImduDirectionalLiftingDeltaMeanNum();
  reconAtlasPatch.directionalLiftParameterss[1] =
    refPatch.directionalLiftParameterss[1]
    + imdu.getImduDirectionalLiftingDeltaMeanDeno();
  reconAtlasPatch.directionalLiftParameterss[2] =
    refPatch.directionalLiftParameterss[2]
    + imdu.getImduDirectionalLiftingDeltaStdNum();
  reconAtlasPatch.directionalLiftParameterss[3] =
    refPatch.directionalLiftParameterss[3]
    + imdu.getImduDirectionalLiftingDeltaStdDeno();

  if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
    if (imdu.getImduTextureProjectionPresentFlag()) {
      printf("reconstructing orthoAtlas parameters (INTER)\n");
      auto qpTexCoord =
        aspsVmcExt.getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
      reconstructTextureProjectionInterInformation(
        imdu.getTextureProjectionInterInformation(),
        aspsVmcExt.getAspsProjectionTexcoordUpscaleFactor()
          / aspsVmcExt.getAspsProjectionTexcoordDownscaleFactor(),
        qpTexCoord,
        asps.getExtendedProjectionEnabledFlag(),
        reconAtlasPatch,
        refPatch);
    }
  }
  return referencePatchIndex + 1;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatchMerge(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  auto& ath            = atl.getHeader();
  auto& atdu           = atl.getDataUnit();
  auto& aspsVmcExt     = asps.getAsveExtension();
  auto& reconAtlasTile = reconAtlasTiles_[tileIndex][frameIndex];
  auto& reconAtlasPatch =
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  auto& mmdu                = pi.getMergeMeshpatchDataUnit();
  auto  refIdx              = mmdu.getMmduRefIndex();
  auto  referenceFrameIndex = ath.getReferenceList()[refIdx];
  auto  referencePatchIndex = predictorIdx;
  auto& referencePatch      = reconAtlasTiles_[tileIndex][referenceFrameIndex]
                           .patches_[referencePatchIndex];
  printf("\tP_MERGE sigRefIndex: %d(refFrame:%d) referencePatchIndex: %d\n",
         mmdu.getMmduRefIndex(),
         referenceFrameIndex,
         referencePatchIndex);

  reconAtlasPatch.submeshId_ =
    referencePatch.submeshId_;  //(int32_t) dpdu.getSubmeshId();
  reconAtlasPatch.lodIdx_           = referencePatch.lodIdx_;
  reconAtlasPatch.geometryPatchArea = referencePatch.geometryPatchArea;
  if (aspsVmcExt.getAspsAttributeNominalFrameSizeCount() > 0) {
    reconAtlasPatch.attributePatchArea = referencePatch.attributePatchArea;
  }
  reconAtlasPatch.subdivIteration = referencePatch.subdivIteration;
  if (reconAtlasPatch.subdivIteration > 0) {
    reconAtlasPatch.subdivMethod.resize(reconAtlasPatch.subdivIteration);
    for (int it = 0; it < reconAtlasPatch.subdivIteration; it++)
      reconAtlasPatch.subdivMethod[it] = referencePatch.subdivMethod[it];
  }
  reconAtlasPatch.subdivMinEdgeLength = referencePatch.subdivMinEdgeLength;
  reconAtlasPatch.displacementCoordinateSystem =
    referencePatch.displacementCoordinateSystem;
  reconAtlasPatch.transformMethod    = referencePatch.transformMethod;
  reconAtlasPatch.transformParameter = referencePatch.transformParameter;

  if (aspsVmcExt.getAsveLiftingOffsetPresentFlag()) {
    if (reconAtlasPatch.subdivIteration > referencePatch.subdivIteration) {
      for (int i = 0; i < reconAtlasPatch.subdivIteration; i++) {
        reconAtlasPatch.transformParameter.setLiftingOffsetValuesNumerator(
          i,
          referencePatch.transformParameter
              .getLiftingOffsetValuesNumerator()[0]
            + (int)mmdu.getMmduLiftingTransformParameters()
                .getLiftingOffsetDeltaValuesNumerator()[i]);
        reconAtlasPatch.transformParameter
          .setLiftingOffsetValuesDenominatorMinus1(
            i,
            referencePatch.transformParameter
                .getLiftingOffsetValuesDenominatorMinus1()[0]
              + (int)mmdu.getMmduLiftingTransformParameters()
                  .getLiftingOffsetDeltaValuesDenominator()[i]);
      }
    } else {
      for (int i = 0; i < reconAtlasPatch.subdivIteration; i++) {
        reconAtlasPatch.transformParameter.setLiftingOffsetValuesNumerator(
          i,
          referencePatch.transformParameter
              .getLiftingOffsetValuesNumerator()[i]
            + (int)mmdu.getMmduLiftingTransformParameters()
                .getLiftingOffsetDeltaValuesNumerator()[i]);
        reconAtlasPatch.transformParameter
          .setLiftingOffsetValuesDenominatorMinus1(
            i,
            referencePatch.transformParameter
                .getLiftingOffsetValuesDenominatorMinus1()[i]
              + (int)mmdu.getMmduLiftingTransformParameters()
                  .getLiftingOffsetDeltaValuesDenominator()[i]);
      }
    }
  }
  reconAtlasPatch.directionalLiftParameterss.resize(4);
  reconAtlasPatch.directionalLiftParameterss[0] =
    referencePatch.directionalLiftParameterss[0]
    + mmdu.getMmduDirectionalLiftingDeltaMeanNum();
  reconAtlasPatch.directionalLiftParameterss[1] =
    referencePatch.directionalLiftParameterss[1]
    + mmdu.getMmduDirectionalLiftingDeltaMeanDeno();
  reconAtlasPatch.directionalLiftParameterss[2] =
    referencePatch.directionalLiftParameterss[2]
    + mmdu.getMmduDirectionalLiftingDeltaStdNum();
  reconAtlasPatch.directionalLiftParameterss[3] =
    referencePatch.directionalLiftParameterss[3]
    + mmdu.getMmduDirectionalLiftingDeltaStdDeno();

  uint32_t lodCount = reconAtlasPatch.subdivIteration + 1;
  reconAtlasPatch.blockCount.resize(lodCount);
  reconAtlasPatch.lastPosInBlock.resize(lodCount);
  for (int level = 0; level < lodCount; level++) {
    reconAtlasPatch.blockCount[level] =
      referencePatch.blockCount[level];
    reconAtlasPatch.lastPosInBlock[level] =
      referencePatch.lastPosInBlock[level];
  }
  if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
    if (mmdu.getMmduTextureProjectionPresentFlag()) {
      printf("reconstructing orthoAtlas parameters (MERGE)\n");
      reconstructTextureProjectionMergeInformation(
        mmdu.getTextureProjectionMergeInformation(),
        aspsVmcExt.getAspsProjectionTexcoordUpscaleFactor()
          / aspsVmcExt.getAspsProjectionTexcoordDownscaleFactor(),
        reconAtlasPatch,
        referencePatch);
    }
  }
  return (int)referencePatchIndex + 1;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatchSkip(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  auto& ath            = atl.getHeader();
  auto& atdu           = atl.getDataUnit();
  auto& aspsVmcExt     = asps.getAsveExtension();
  auto& reconAtlasTile = reconAtlasTiles_[tileIndex][frameIndex];
  auto& reconAtlasPatch =
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();

  auto  refIdx              = 0;
  auto  referenceFrameIndex = ath.getReferenceList()[refIdx];
  auto  referencePatchIndex = predictorIdx;
  auto& referencePatch      = reconAtlasTiles_[tileIndex][referenceFrameIndex]
                           .patches_[referencePatchIndex];
  printf("\tP_SKIP sigRefIndex: %d(refFrame:%d) referencePatchIndex: %d\n",
         refIdx,
         referenceFrameIndex,
         referencePatchIndex);

  reconAtlasPatch.submeshId_        = referencePatch.submeshId_;
  reconAtlasPatch.geometryPatchArea = referencePatch.geometryPatchArea;
  if (aspsVmcExt.getAspsAttributeNominalFrameSizeCount() > 0) {
    reconAtlasPatch.attributePatchArea = referencePatch.attributePatchArea;
  }
  reconAtlasPatch.subdivIteration = referencePatch.subdivIteration;
  if (reconAtlasPatch.subdivIteration > 0) {
    reconAtlasPatch.subdivMethod.resize(reconAtlasPatch.subdivIteration);
    for (int it = 0; it < reconAtlasPatch.subdivIteration; it++)
      reconAtlasPatch.subdivMethod[it] = referencePatch.subdivMethod[it];
  }
  reconAtlasPatch.subdivMinEdgeLength = referencePatch.subdivMinEdgeLength;
  reconAtlasPatch.displacementCoordinateSystem =
    referencePatch.displacementCoordinateSystem;
  reconAtlasPatch.transformMethod    = referencePatch.transformMethod;
  reconAtlasPatch.transformParameter = referencePatch.transformParameter;
  if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
    int smIdx = afps.getAfveExtension()
                  .getAtlasFrameMeshInformation()
                  ._submeshIDToIndex[reconAtlasPatch.submeshId_];
    if (afps.getAfveExtension().getProjectionTextcoordPresentFlag(smIdx)) {
      reconAtlasPatch.frameScale   = referencePatch.frameScale;
      reconAtlasPatch.packedCCList = referencePatch.packedCCList;
    }
  }
  return (int)referencePatchIndex + 1;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatchAttIntra(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  printf("\tI_INTRA_ATTR\n");

  auto& ath            = atl.getHeader();
  auto& atdu           = atl.getDataUnit();
  auto& aspsVmcExt     = asps.getAsveExtension();
  auto& reconAtlasTile = reconAtlasTiles_[tileIndex][frameIndex];
  auto& reconAtlasPatch =
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();
  uint32_t patchSizeXQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeXinfoQuantizer())
                                   : patchBlockSize;
  uint32_t patchSizeYQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeYinfoQuantizer())
                                   : patchBlockSize;

  auto& pdu                  = pi.getMeshpatchDataUnit();
  reconAtlasPatch.submeshId_ = pdu.getMduSubmeshId();
  reconAtlasPatch.tileType_  = ath.getType();
  if (aspsVmcExt.getAspsAttributeNominalFrameSizeCount() > 0) {
    for (int attIdx = 0;
         attIdx < aspsVmcExt.getAspsAttributeNominalFrameSizeCount();
         attIdx++) {
      reconAtlasPatch.attributePatchArea[attIdx].sizeX =
        (int)(pdu.getMduAttributes2dSizeXMinus1() + 1) * patchSizeXQuantizer;
      reconAtlasPatch.attributePatchArea[attIdx].sizeY =
        (int)(pdu.getMduAttributes2dSizeYMinus1() + 1) * patchSizeYQuantizer;
      reconAtlasPatch.attributePatchArea[attIdx].LTx =
        (int)pdu.getMduAttributes2dPosX() * patchBlockSize;
      reconAtlasPatch.attributePatchArea[attIdx].LTy =
        (int)pdu.getMduAttributes2dPosY() * patchBlockSize;
    }
  }
  return predictorIdx;
}

//============================================================================
int32_t
AtlasEncoder::reconstructAtlasPatch(
  int32_t                              frameIndex,
  int32_t                              tileIndex,
  int32_t                              patchIndex,
  PatchInformationData&                pi,
  AtlasTileLayerRbsp&                  atl,
  AtlasSequenceParameterSetRbsp&       asps,
  AtlasFrameParameterSetRbsp&          afps,
  std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
  int32_t                              predictorIdx) {
  printf("->Reconstruct atlas patch: Frame = %d Tile = %d Patch = %d\n",
         frameIndex,
         tileIndex,
         patchIndex);
  fflush(stdout);
  auto& ath = atl.getHeader();

  if ((ath.getType() == I_TILE && pi.getPatchMode() == I_INTRA)
      || (ath.getType() == P_TILE && pi.getPatchMode() == P_INTRA)) {
    return reconstructAtlasPatchIntra(frameIndex,
                                      tileIndex,
                                      patchIndex,
                                      pi,
                                      atl,
                                      asps,
                                      afps,
                                      reconAtlasTiles_,
                                      predictorIdx);
  } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_MERGE)) {
    return reconstructAtlasPatchMerge(frameIndex,
                                      tileIndex,
                                      patchIndex,
                                      pi,
                                      atl,
                                      asps,
                                      afps,
                                      reconAtlasTiles_,
                                      predictorIdx);
  } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_INTER)) {
    return reconstructAtlasPatchInter(frameIndex,
                                      tileIndex,
                                      patchIndex,
                                      pi,
                                      atl,
                                      asps,
                                      afps,
                                      reconAtlasTiles_,
                                      predictorIdx);
  } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_SKIP)) {
    return reconstructAtlasPatchSkip(frameIndex,
                                     tileIndex,
                                     patchIndex,
                                     pi,
                                     atl,
                                     asps,
                                     afps,
                                     reconAtlasTiles_,
                                     predictorIdx);
  } else if ((ath.getType() == I_TILE_ATTR
              && pi.getPatchMode() == I_INTRA_ATTR)
             || (ath.getType() == P_TILE_ATTR
                 && pi.getPatchMode() == I_INTRA_ATTR)) {
    return reconstructAtlasPatchAttIntra(frameIndex,
                                         tileIndex,
                                         patchIndex,
                                         pi,
                                         atl,
                                         asps,
                                         afps,
                                         reconAtlasTiles_,
                                         predictorIdx);
  }

  return -1;
}

//============================================================================
void
AtlasEncoder::reconstructTileInformation(
  std::vector<imageArea>&              tileArea,
  int32_t                              attrIndex,
  AtlasFrameParameterSetRbsp&          afps,
  const AtlasSequenceParameterSetRbsp& asps) {
  auto& afti =
    attrIndex == -1
      ? afps.getAtlasFrameTileInformation()
      : afps.getAfveExtension().getAtlasFrameTileAttributeInformation(
          attrIndex);
  size_t frameWidth =
    attrIndex == -1
      ? asps.getFrameWidth()
      : asps.getAsveExtension().getAsveAttributeFrameWidth()[attrIndex];
  size_t frameHeight =
    attrIndex == -1
      ? asps.getFrameHeight()
      : asps.getAsveExtension().getAsveAttributeFrameHeight()[attrIndex];
  size_t numPartitionCols = afti.getNumPartitionColumnsMinus1() + 1;
  size_t numPartitionRows = afti.getNumPartitionRowsMinus1() + 1;
  auto&  partitionWidth   = afti.getPartitionWidth();
  auto&  partitionHeight  = afti.getPartitionHeight();
  auto&  partitionPosX    = afti.getPartitionPosX();
  auto&  partitionPosY    = afti.getPartitionPosY();
  partitionWidth.resize(numPartitionCols);
  partitionHeight.resize(numPartitionRows);
  partitionPosX.resize(numPartitionCols);
  partitionPosY.resize(numPartitionRows);

  uint32_t numTiles = 1;
  if (afti.getSingleTileInAtlasFrameFlag()) {
    partitionWidth[0]  = asps.getFrameWidth();
    partitionHeight[0] = asps.getFrameHeight();
    partitionPosX[0]   = 0;
    partitionPosY[0]   = 0;
    numTiles           = 1;
  } else {
    numTiles = afti.getNumTilesInAtlasFrameMinus1() + 1;
    if (afti.getUniformPartitionSpacingFlag()) {
      if (attrIndex == -1 && frameWidth == 0 && frameHeight == 0) {
        partitionWidth[0]  = 0;
        partitionHeight[0] = 0;
        partitionPosX[0]   = 0;
        partitionPosY[0]   = 0;
      } else {
        size_t uniformPatitionWidth =
          64 * (afti.getPartitionColumnWidthMinus1(0) + 1);
        size_t uniformPatitionHeight =
          64 * (afti.getPartitionRowHeightMinus1(0) + 1);
        partitionPosX[0]  = 0;
        partitionWidth[0] = uniformPatitionWidth;
        for (size_t col = 1; col < numPartitionCols - 1; col++) {
          partitionPosX[col] =
            partitionPosX[col - 1] + partitionWidth[col - 1];
          partitionWidth[col] = uniformPatitionWidth;
        }
        if (numPartitionCols > 1) {
          partitionPosX[numPartitionCols - 1] =
            partitionPosX[numPartitionCols - 2]
            + partitionWidth[numPartitionCols - 2];
          partitionWidth[numPartitionCols - 1] =
            frameWidth - partitionPosX[numPartitionCols - 1];
        }

        partitionPosY[0]   = 0;
        partitionHeight[0] = uniformPatitionHeight;
        for (size_t row = 1; row < numPartitionRows - 1; row++) {
          partitionPosY[row] =
            partitionPosY[row - 1] + partitionHeight[row - 1];
          partitionHeight[row] = uniformPatitionHeight;
        }
        if (numPartitionRows > 1) {
          partitionPosY[numPartitionRows - 1] =
            partitionPosY[numPartitionRows - 2]
            + partitionHeight[numPartitionRows - 2];
          partitionHeight[numPartitionRows - 1] =
            frameHeight - partitionPosY[numPartitionRows - 1];
        }
      }
    } else {
      partitionPosX[0]  = 0;
      partitionWidth[0] = 64 * (afti.getPartitionColumnWidthMinus1(0) + 1);
      for (size_t col = 1; col < numPartitionCols - 1; col++) {
        partitionPosX[col] = partitionPosX[col - 1] + partitionWidth[col - 1];
        partitionWidth[col] =
          64 * (afti.getPartitionColumnWidthMinus1(col) + 1);
      }
      if (numPartitionCols > 1) {
        partitionPosX[numPartitionCols - 1] =
          partitionPosX[numPartitionCols - 2]
          + partitionWidth[numPartitionCols - 2];
        partitionWidth[numPartitionCols - 1] =
          frameWidth - partitionPosX[numPartitionCols - 1];
      }

      partitionPosY[0]   = 0;
      partitionHeight[0] = 64 * (afti.getPartitionRowHeightMinus1(0) + 1);
      for (size_t row = 1; row < numPartitionRows - 1; row++) {
        partitionPosY[row] = partitionPosY[row - 1] + partitionHeight[row - 1];
        partitionHeight[row] =
          64 * (afti.getPartitionRowHeightMinus1(row) + 1);
      }
      if (numPartitionRows > 1) {
        partitionPosY[numPartitionRows - 1] =
          partitionPosY[numPartitionRows - 2]
          + partitionHeight[numPartitionRows - 2];
        partitionHeight[numPartitionRows - 1] =
          frameHeight - partitionPosY[numPartitionRows - 1];
      }
    }
  }

  tileArea.resize(numTiles);
  if (numTiles == 1) {
    tileArea[0].LTx   = 0;
    tileArea[0].LTy   = 0;
    tileArea[0].sizeX = frameWidth;
    tileArea[0].sizeY = frameHeight;

  } else {
    for (size_t ti = 0; ti < numTiles; ti++) {
      size_t TopLeftPartitionColumn =
        afti.getTopLeftPartitionIdx(ti)
        % (afti.getNumPartitionColumnsMinus1() + 1);
      size_t TopLeftPartitionRow = afti.getTopLeftPartitionIdx(ti)
                                   / (afti.getNumPartitionColumnsMinus1() + 1);
      size_t BottomRightPartitionColumn =
        TopLeftPartitionColumn + afti.getBottomRightPartitionColumnOffset(ti);
      size_t BottomRightPartitionRow =
        TopLeftPartitionRow + afti.getBottomRightPartitionRowOffset(ti);

      size_t tileStartX = afti.getPartitionPosX(TopLeftPartitionColumn);
      size_t tileStartY = afti.getPartitionPosY(TopLeftPartitionRow);
      size_t tileWidth  = 0;
      size_t tileHeight = 0;
      for (size_t j = TopLeftPartitionColumn; j <= BottomRightPartitionColumn;
           j++) {
        tileWidth += (afti.getPartitionWidth(j));
      }
      for (size_t j = TopLeftPartitionRow; j <= BottomRightPartitionRow; j++) {
        tileHeight += (afti.getPartitionHeight(j));
      }
      tileArea[ti].LTx = (uint32_t)tileStartX;
      tileArea[ti].LTy = (uint32_t)tileStartY;

      if ((tileArea[ti].LTx + tileWidth) > frameWidth)
        tileWidth = asps.getFrameWidth() - tileArea[ti].LTx;
      if ((tileArea[ti].LTy + tileHeight) > frameHeight)
        tileHeight = asps.getFrameHeight() - tileArea[ti].LTy;

      tileArea[ti].sizeX = (uint32_t)tileWidth;
      tileArea[ti].sizeY = (uint32_t)tileHeight;

      assert(tileArea[ti].LTx < frameWidth);
      assert(tileArea[ti].LTy < frameHeight);
    }
  }
}

//============================================================================
void
AtlasEncoder::setAtlasTileInformation(
  AtlasFrameParameterSetRbsp&          afps,
  AtlasSequenceParameterSetRbsp&       asps,
  std::vector<std::vector<AtlasTile>>& tileAreasInVideo_,
  std::pair<int32_t, int32_t>&         geometryVideoSize,
  const AtlasEncoderParameters&        params) {
  // TODO: Assumption that tiles do not change over GoP so AFPS does not have to be updated.
  std::vector<imageArea> tileArea(params.numTilesGeometry);
  uint32_t               videoWidth  = 0;
  uint32_t               videoHeight = 0;

  videoWidth  = geometryVideoSize.first;
  videoHeight = geometryVideoSize.second;
  for (uint32_t ti = 0; ti < params.numTilesGeometry; ti++) {
    tileArea[ti] = tileAreasInVideo_[ti][0].tileGeometryArea_;
  }

  printf("(setAtlasTileInformation) TileCount: %d\n", params.numTilesGeometry);
  printf("\ttGeometryVideo : %dx%d\n", videoWidth, videoHeight);
  for (int ti = 0; ti < tileArea.size(); ti++) {
    printf("\t\ttile[%d]\tsize: %dx%d\tpos: (%d,%d)\n",
           ti,
           tileArea[ti].sizeX,
           tileArea[ti].sizeY,
           tileArea[ti].LTx,
           tileArea[ti].LTy);
  }

  AtlasFrameTileInformation& afti = afps.getAtlasFrameTileInformation();
  uint32_t                   partitionWidthIn  = 64;
  uint32_t                   partitionHeightIn = 64;

  if (params.numTilesGeometry == 1) {
    afti.setSingleTileInAtlasFrameFlag(true);
  } else {
    afti.getUniformPartitionSpacingFlag() = true;
    afti.setNumTilesInAtlasFrameMinus1(params.numTilesGeometry - 1);
    if (params.encodeDisplacementType == 1) { return; }
    uint32_t partitionWidth  = partitionWidthIn;
    uint32_t partitionHeight = partitionHeightIn;
    uint32_t NumPartitionColumns =
      std::max((uint32_t)1, videoWidth / partitionWidth);
    uint32_t NumPartitionRows =
      std::max((uint32_t)1, videoHeight / partitionHeight);

    std::vector<uint32_t> partitionWidthArray(NumPartitionColumns,
                                              partitionWidth);
    std::vector<uint32_t> partitionHeightArray(NumPartitionRows,
                                               partitionHeight);

    afti.setNumPartitionColumnsMinus1(NumPartitionColumns - 1);
    afti.setNumPartitionRowsMinus1(NumPartitionRows - 1);
    afti.setPartitionColumnWidthMinus1(0, partitionWidth / 64 - 1);
    afti.setPartitionRowHeightMinus1(0, partitionHeight / 64 - 1);
    afti.getSinglePartitionPerTileFlag() = false;

    afti.allocate();
    for (size_t ti = 0; ti <= afti.getNumTilesInAtlasFrameMinus1(); ti++) {
      int    leftTopX          = tileArea[ti].LTx;
      int    leftTopY          = tileArea[ti].LTy;
      int    bottomRightDeltaX = tileArea[ti].sizeX;
      int    bottomRightDeltaY = tileArea[ti].sizeY;
      size_t numPartLeftX = 0, numPartLeftY = 0;
      while (leftTopX > 0) {
        leftTopX -= partitionWidthArray[numPartLeftX];
        numPartLeftX += 1;
      }
      while (leftTopY > 0) {
        leftTopY -= partitionHeightArray[numPartLeftY];
        numPartLeftY += 1;
      }
      uint32_t topLeftIdx = numPartLeftX + numPartLeftY * NumPartitionColumns;
      afti.setTopLeftPartitionIdx((size_t)ti, topLeftIdx);

      size_t numPartBottomX = 0, numPartBottomY = 0;
      while (bottomRightDeltaX > 0) {
        bottomRightDeltaX -=
          partitionWidthArray[numPartLeftX + numPartBottomX];
        numPartBottomX += 1;
      }
      while (bottomRightDeltaY > 0) {
        bottomRightDeltaY -=
          partitionHeightArray[numPartLeftY + numPartBottomY];
        numPartBottomY += 1;
      }

      afti.setBottomRightPartitionColumnOffset((size_t)ti, numPartBottomX - 1);
      afti.setBottomRightPartitionRowOffset((size_t)ti, numPartBottomY - 1);
    }
  }  //numTiles>1
}

//============================================================================
void
AtlasEncoder::setAtlasTileAttributeInformation(
  int32_t                                     attrIndex,
  AtlasFrameParameterSetRbsp&                 afps,
  AtlasSequenceParameterSetRbsp&              asps,
  std::vector<std::vector<AtlasTile>>&        tileAreasInVideo_,
  std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_,
  const AtlasEncoderParameters&               params) {
  // TODO: Assumption that tiles do not change over GoP so AFPS does not have to be updated.
  std::vector<imageArea> tileArea(params.numTilesAttribute);
  uint32_t               videoWidth  = 0;
  uint32_t               videoHeight = 0;
  videoWidth                         = attributeVideoSize_[attrIndex].first;
  videoHeight                        = attributeVideoSize_[attrIndex].second;
  // TODO works only for one attribute tile
  //VERIFY_SOFTWARE_LIMITATION(params.numTilesAttribute == 1);
  tileArea[0] = tileAreasInVideo_[params.numTilesGeometry][0]
                  .tileAttributeAreas_[attrIndex];

  printf("(setAtlasTileAttributeInformation) TileCount: %d\n",
         params.numTilesAttribute);
  printf("\tAttributeVideo : %dx%d\n", videoWidth, videoHeight);
  for (int ti = 0; ti < tileArea.size(); ti++) {
    printf("\t\ttile[%d]\tsize: %dx%d\tpos: (%d,%d)\n",
           ti,
           tileArea[ti].sizeX,
           tileArea[ti].sizeY,
           tileArea[ti].LTx,
           tileArea[ti].LTy);
  }

  AtlasFrameTileAttributeInformation& aftai =
    afps.getAfveExtension().getAtlasFrameTileAttributeInformation(attrIndex);
  //VERIFY_SOFTWARE_LIMITATION(aftai.getNumTilesInAtlasFrameMinus1() == 0);
  uint32_t partitionWidthIn  = 256;
  uint32_t partitionHeightIn = 256;
  if (params.numTilesAttribute == 1) {
    aftai.setSingleTileInAtlasFrameFlag(true);
    aftai.setNalTilePresentFlag(params.numSubmesh > 1); // if we only have one submesh, we don't need to send the attribute tile
  } else {
    aftai.getUniformPartitionSpacingFlag() = true;
    aftai.setNumTilesInAtlasFrameMinus1(params.numTilesAttribute - 1);
    uint32_t partitionWidth  = partitionWidthIn;
    uint32_t partitionHeight = partitionHeightIn;
    uint32_t NumPartitionColumns =
      std::max((uint32_t)1, videoWidth / partitionWidth);
    uint32_t NumPartitionRows =
      std::max((uint32_t)1, videoHeight / partitionHeight);

    std::vector<uint32_t> partitionWidthArray(NumPartitionColumns,
                                              partitionWidth);
    std::vector<uint32_t> partitionHeightArray(NumPartitionRows,
                                               partitionHeight);

    aftai.setNumPartitionColumnsMinus1(NumPartitionColumns - 1);
    aftai.setNumPartitionRowsMinus1(NumPartitionRows - 1);
    aftai.setPartitionColumnWidthMinus1(
      0, partitionWidth / 64 - 1);  //partition width = 128
    aftai.setPartitionRowHeightMinus1(
      0, partitionHeight / 64 - 1);  //partition height=64
    aftai.getSinglePartitionPerTileFlag() = false;

    aftai.allocate();
    for (size_t ti = 0; ti <= aftai.getNumTilesInAtlasFrameMinus1(); ti++) {
      uint32_t leftTopX          = tileArea[ti].LTx;
      uint32_t leftTopY          = tileArea[ti].LTy;
      uint32_t bottomRightDeltaX = tileArea[ti].sizeX;
      uint32_t bottomRightDeltaY = tileArea[ti].sizeY;
      uint32_t numPartLeftX      = 0;
      uint32_t numPartLeftY      = 0;

      while (leftTopX > 0) {
        leftTopX -= partitionWidthArray[numPartLeftX];
        numPartLeftX += 1;
      }

      while (leftTopY > 0) {
        leftTopY -= partitionHeightArray[numPartLeftY];
        numPartLeftY += 1;
      }

      uint32_t topLeftIdx = numPartLeftX + numPartLeftY * NumPartitionColumns;
      aftai.setTopLeftPartitionIdx((size_t)ti, topLeftIdx);

      size_t numPartBottomX = 0, numPartBottomY = 0;
      while (bottomRightDeltaX > 0) {
        bottomRightDeltaX -=
          partitionWidthArray[numPartLeftX + numPartBottomX];
        numPartBottomX += 1;
      }
      while (bottomRightDeltaY > 0) {
        bottomRightDeltaY -=
          partitionHeightArray[numPartLeftY + numPartBottomY];
        numPartBottomY += 1;
      }

      aftai.setBottomRightPartitionColumnOffset((size_t)ti,
                                                numPartBottomX - 1);
      aftai.setBottomRightPartitionRowOffset((size_t)ti, numPartBottomY - 1);
    }
  }
}

//============================================================================
std::pair<int32_t, int32_t>
AtlasEncoder::tilePatchOfSubmesh(int32_t                       submeshIndex,
                                 const AtlasEncoderParameters& encParams) {
  auto    submeshId  = encParams.submeshIdList[submeshIndex];
  int32_t tileIndex  = -1;
  int32_t patchIndex = -1;
  auto    numTilesTotal =
    encParams.numTilesGeometry + encParams.numTilesAttribute;
  for (int32_t ti = 0; ti < numTilesTotal; ti++) {
    for (int32_t pi = 0; pi < encParams.submeshIdsInTile[ti].size(); pi++) {
      if (encParams.submeshIdsInTile[ti][pi] == submeshId) {
        tileIndex  = ti;
        patchIndex = pi;
        break;
      }
    }
  }
  return std::pair<uint32_t, uint32_t>((uint32_t)tileIndex,
                                       (uint32_t)patchIndex);
}

void
AtlasEncoder::constructAtlasHeaderRefListStruct(AtlasBitstream&     adStream,
                                                AtlasTileLayerRbsp& atl,
                                                int32_t frameIndex) {
  int32_t          numRefFrame = 1;
  AtlasTileHeader& ath         = atl.getHeader();
  //referenceFrameIndex = 0,1,2,3...31
  if (frameIndex == 0 || atl.getHeader().getType() == I_TILE) {
    numRefFrame = 0;
    ath.setRefAtlasFrameListSpsFlag(true);
    ath.setRefAtlasFrameListIdx(0);
    return;
  } else {
  }

  size_t afpsId = ath.getAtlasFrameParameterSetId();
  auto&  afps   = adStream.getAtlasFrameParameterSet(afpsId);
  size_t aspsId = afps.getAtlasSequenceParameterSetId();
  auto&  asps   = adStream.getAtlasSequenceParameterSet(aspsId);

  //check if reference frame is in the asps
  //1. asps refstruct to reference list
  std::vector<std::vector<int32_t>> aspsReferenceFrameList(
    asps.getNumRefAtlasFrameListsInAsps());
  for (size_t refListIdx = 0;
       refListIdx < asps.getNumRefAtlasFrameListsInAsps();
       refListIdx++) {
    auto&   refList  = asps.getRefListStruct(refListIdx);
    int32_t afocBase = frameIndex;
    for (size_t refIdx = 0; refIdx < refList.getNumRefEntries(); refIdx++) {
      int16_t deltaAfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                            * refList.getAbsDeltaAfocSt(refIdx);
      auto refFrameIndex = afocBase - deltaAfocSt;
      afocBase           = refFrameIndex;
      if (refFrameIndex >= 0)
        aspsReferenceFrameList[refListIdx].push_back(
          refFrameIndex);  //absolute frame index : {0,-1,-2,-3} {28, 27, 26, 25}
    }
  }
  //2. check the presence of the reference frame in the asps reference list
  bool    allInAsps      = true;
  int32_t prevListIndex  = -1;
  int32_t foundListIndex = 0;
  for (auto& piData : atl.getDataUnit().getPatchInformationData()) {
    int32_t referenceFrameIndex = -1;
    if (piData.getPatchMode() == P_MERGE && ath.getType() == P_TILE)
      referenceFrameIndex =
        piData.getMergeMeshpatchDataUnit().getRefereceFrameIndex();
    if (piData.getPatchMode() == P_INTER && ath.getType() == P_TILE)
      referenceFrameIndex =
        piData.getInterMeshpatchDataUnit().getRefereceFrameIndex();
    if (referenceFrameIndex < 0) continue;
    //search
    bool isInAsps = false;
    for (int32_t refListIdx = 0;
         refListIdx < (int32_t)aspsReferenceFrameList.size();
         refListIdx++) {
      for (int32_t refIdx = 0;
           refIdx < (int32_t)aspsReferenceFrameList[refListIdx].size();
           refIdx++) {
        if (aspsReferenceFrameList[refListIdx][refIdx]
            == referenceFrameIndex) {
          isInAsps       = true;
          foundListIndex = refListIdx;
          break;
        }
      }
    }  //refList
    if (isInAsps) {
      if (prevListIndex == -1) prevListIndex = foundListIndex;
      else if (foundListIndex != prevListIndex) allInAsps = false;
    } else allInAsps = false;
    if (ath.getReferenceList().size() == 0)
      ath.getReferenceList().push_back(referenceFrameIndex);
    for (auto refFrameIdx : ath.getReferenceList()) {
      if (refFrameIdx != referenceFrameIndex) {
        ath.getReferenceList().push_back(referenceFrameIndex);
        break;
      }
    }
  }
  //3. create an ath reference list if needed
  if (allInAsps) {
    ath.setRefAtlasFrameListSpsFlag(true);
    ath.setRefAtlasFrameListIdx(foundListIndex);
  } else {
    //new reference structure
    ath.setRefAtlasFrameListSpsFlag(false);
    RefListStruct refList;
    refList.setNumRefEntries(ath.getReferenceList().size());
    refList.allocate();
    for (size_t i = 0; i < ath.getReferenceList().size(); i++) {
      auto referenceFrameIndex = ath.getReferenceList()[i];
      int  mfocDiff            = -1;
      if (i == 0) {
        mfocDiff = frameIndex - referenceFrameIndex;
      } else {
        mfocDiff = ath.getReferenceList()[i - 1] - referenceFrameIndex;
      }
      refList.setAbsDeltaAfocSt(i, std::abs(mfocDiff));
      refList.setStrafEntrySignFlag(i, mfocDiff < 0 ? false : true);
      refList.setStRefAtlasFrameFlag(i, true);
    }

    ath.setRefListStruct(refList);
  }

  //4. assigning refIdx
  auto& refList = ath.getRefAtlasFrameListSpsFlag()
                    ? aspsReferenceFrameList[ath.getRefAtlasFrameListIdx()]
                    : ath.getReferenceList();
  numRefFrame   = (int32_t)refList.size();
  for (auto& piData : atl.getDataUnit().getPatchInformationData()) {
    int32_t referenceFrameIndex = -1;
    if (piData.getPatchMode() == P_MERGE && ath.getType() == P_TILE)
      referenceFrameIndex =
        piData.getMergeMeshpatchDataUnit().getRefereceFrameIndex();
    if (piData.getPatchMode() == P_INTER && ath.getType() == P_TILE)
      referenceFrameIndex =
        piData.getInterMeshpatchDataUnit().getRefereceFrameIndex();
    int32_t foundRefIndex = -1;
    for (int32_t refIdx = 0; refIdx < refList.size(); refIdx++) {
      if (refList[refIdx] == referenceFrameIndex) {
        foundRefIndex = refIdx;
        break;
      }
    }
    if (piData.getPatchMode() == P_MERGE && ath.getType() == P_TILE)
      piData.getMergeMeshpatchDataUnit().setMmduRefIndex(foundRefIndex);
    if (piData.getPatchMode() == P_INTER && ath.getType() == P_TILE)
      piData.getInterMeshpatchDataUnit().setImduRefIndex(foundRefIndex);
  }

  //size_t numActiveRefEntries=1;
  //refoc = afocbase - diff
  if (numRefFrame > 0) {
    bool bNumRefIdxActiveOverrideFlag = false;
    if ((uint32_t)numRefFrame >= (afps.getNumRefIdxDefaultActiveMinus1())) {
      bNumRefIdxActiveOverrideFlag =
        ((uint32_t)numRefFrame
         != (afps.getNumRefIdxDefaultActiveMinus1() + 1));
    } else {
      bNumRefIdxActiveOverrideFlag =
        numRefFrame != ath.getRefListStruct().getNumRefEntries();
    }
    ath.setNumRefIdxActiveOverrideFlag(bNumRefIdxActiveOverrideFlag);
    if (ath.getNumRefIdxActiveOverrideFlag())
      ath.setNumRefIdxActiveMinus1(numRefFrame - 1);
  }
}

bool
AtlasEncoder::createAtlasMeshSubPatch(vmesh::VMCSubmesh&                   encFrame,
                                      int32_t                       submeshPos,
                                      int32_t                       frameIndex,
                                      TextureProjectionInformation& tpi,
                                      int32_t                       qpTexCoord,
                                      double packingScaling,
                                      bool   use45DegreeProjection,
                                      bool   bFaceIdPresentFlag) {
  auto& subpatches            = encFrame.packedCCList;
  tpi.getFrameUpscaleMinus1() = (int64_t)(subpatches[0].getFrameUpscale()) - 1;
  tpi.getFrameDownscale()     = (int)(subpatches[0].getFrameDownscale());
  tpi.getFaceIdPresentFlag()  = bFaceIdPresentFlag;
  tpi.allocateSubPatches(subpatches.size());
  bool subpatchRawEnableFlag = 0;
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int i = 0; i < subpatches.size(); i++) {
    auto& subpatch = subpatches[i];
    if (bFaceIdPresentFlag)
      tpi.getFaceId2SubpatchIdx(i) = subpatch.getFaceId();
    else tpi.getFaceId2SubpatchIdx(i) = i;
    int rawUvId = use45DegreeProjection ? 18 : 6;
    if (subpatch.getProjection() == rawUvId) {
      subpatchRawEnableFlag            = true;
      tpi.getSubpatchRawPresentFlag(i) = true;
      auto& sri                        = tpi.getSubpatchRaw(i);
      sri.getNumRawUVMinus1()          = subpatch.getNumRawUVMinus1();
      for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
        sri.getUcoord(i) = subpatch.getUcoordQuant(i);
        sri.getVcoord(i) = subpatch.getVcoordQuant(i);
      }
    } else {
      auto& si              = tpi.getSubpatch(i);
      si.getProjectionId()  = subpatch.getProjection();
      si.getOrientationId() = subpatch.getOrientation();
      int sizeU             = subpatch.getSizeU() - 1;
      int sizeV             = subpatch.getSizeV() - 1;
      if (i > 0) {
        int diffSizeU = sizeU - subpatches[prevOrthoPatchId].getSizeU() + 1;
        int diffSizeV = sizeV - subpatches[prevOrthoPatchId].getSizeV() + 1;
        sizeU         = diffSizeU;
        sizeV         = diffSizeV;
      }
      si.get2dPosX()        = subpatch.getU0();
      si.get2dPosY()        = subpatch.getV0();
      si.getPosBiasX()      = subpatch.getU1();
      si.getPosBiasY()      = subpatch.getV1();
      si.get2dSizeXMinus1() = sizeU;
      si.get2dSizeYMinus1() = sizeV;
      if (subpatch.getScale() != subpatch.getFrameScale()) {
        si.getScalePresentFlag() = true;
        uint8_t n                = 0;
        auto    ratio         = subpatch.getScale() / subpatch.getFrameScale();
        double  num           = log(ratio);
        double  den           = log(packingScaling);
        double  nDbl          = num / den;
        n                     = round(nDbl) - 1;
        si.getSubpatchScale() = n;
      } else si.getScalePresentFlag() = false;
      prevOrthoPatchId = i;
    }
  }
  tpi.getSubpatchRawEnableFlag() = subpatchRawEnableFlag;
  return true;
}

bool
AtlasEncoder::createAtlasMeshSubPatchInter(
  vmesh::VMCSubmesh&                        encFrame,
  int32_t                            submeshPos,
  int32_t                            frameIndex,
  TextureProjectionInterInformation& tpii,
  double                             packingScaling,
  AtlasPatch&                        refFrame,
  bool                               enableSubpatchInter,
  bool                               use45DegreeProjection,
  bool                               bFaceIdPresentFlag) {
  auto& subpatches    = encFrame.packedCCList;
  auto& refSubpatches = refFrame.packedCCList;
  tpii.getFrameUpscaleMinus1() =
    (int64_t)(subpatches[0].getFrameUpscale()) - 1;
  tpii.getFrameDownscale() = (int)(subpatches[0].getFrameDownscale());
  tpii.allocateSubPatches(subpatches.size());
  bool subpatchRawEnableFlag = 0;
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int i = 0; i < subpatches.size(); i++) {
    auto& subpatch = subpatches[i];
    if (bFaceIdPresentFlag)
      tpii.getFaceId2SubpatchIdx(i) =
        subpatch
          .getFaceId();  // this is just for testing the mapping functionality
    else tpii.getFaceId2SubpatchIdx(i) = i;
    bool sendDelta = true;
    if (!enableSubpatchInter || i >= refSubpatches.size()) {
      sendDelta = false;
    } else {
      auto& refSubpatch = refSubpatches[i];
      // check if the sub-patches have the same values
      sendDelta =
        sendDelta && (subpatch.getProjection() == refSubpatch.getProjection());
      sendDelta =
        sendDelta
        && (subpatch.getOrientation() == refSubpatch.getOrientation());

      bool scalePresentFlag = false;
      int  subpatchScale    = 0;
      if (subpatch.getScale() != subpatch.getFrameScale()) {
        scalePresentFlag = true;
        uint8_t n        = 0;
        auto    ratio    = subpatch.getScale() / subpatch.getFrameScale();
        double  num      = log(ratio);
        double  den      = log(packingScaling);
        double  nDbl     = num / den;
        n                = round(nDbl) - 1;
        subpatchScale    = n;
      }

      bool refScalePresentFlag = false;
      int  refSubpatchScale    = 0;
      if (refSubpatch.getScale() != refSubpatch.getFrameScale()) {
        refScalePresentFlag = true;
        uint8_t n           = 0;
        auto    ratio = refSubpatch.getScale() / refSubpatch.getFrameScale();
        double  num   = log(ratio);
        double  den   = log(packingScaling);
        double  nDbl  = num / den;
        n             = round(nDbl) - 1;
        refSubpatchScale = n;
      }
      sendDelta = sendDelta && (scalePresentFlag == refScalePresentFlag);
      sendDelta = sendDelta && (subpatchScale == refSubpatchScale);
    }
    tpii.getUpdateFlag(i) = sendDelta;
    if (sendDelta) {
      enableSubpatchInter = true;
      auto& refSubpatch   = refSubpatches[i];
      auto& sdi           = tpii.getSubpatchInter(i);
      sdi.getSubpatchIdxDiff() =
        0;  // for now the patches are just matched with their direct index, but we could do a search here as well...
      sdi.get2dPosXDelta() = (int)subpatch.getU0() - (int)refSubpatch.getU0();
      sdi.get2dPosYDelta() = (int)subpatch.getV0() - (int)refSubpatch.getV0();
      sdi.getPosBiasXDelta() =
        (int)subpatch.getU1() - (int)refSubpatch.getU1();
      sdi.getPosBiasYDelta() =
        (int)subpatch.getV1() - (int)refSubpatch.getV1();
      sdi.get2dSizeXDelta() =
        (int)subpatch.getSizeU() - (int)refSubpatch.getSizeU();
      sdi.get2dSizeYDelta() =
        (int)subpatch.getSizeV() - (int)refSubpatch.getSizeV();
    } else {
      int rawUvId = use45DegreeProjection ? 18 : 6;
      if (subpatch.getProjection() == rawUvId) {
        subpatchRawEnableFlag             = true;
        tpii.getSubpatchRawPresentFlag(i) = true;
        auto& sri                         = tpii.getSubpatchRaw(i);
        sri.getNumRawUVMinus1()           = subpatch.getNumRawUVMinus1();
        for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
          sri.getUcoord(i) = subpatch.getUcoordQuant(i);
          sri.getVcoord(i) = subpatch.getVcoordQuant(i);
        }
      } else {
        auto& si              = tpii.getSubpatch(i);
        si.getProjectionId()  = subpatch.getProjection();
        si.getOrientationId() = subpatch.getOrientation();
        int sizeU             = subpatch.getSizeU() - 1;
        int sizeV             = subpatch.getSizeV() - 1;
        if (i > 0) {
          int diffSizeU = sizeU - subpatches[prevOrthoPatchId].getSizeU() + 1;
          int diffSizeV = sizeV - subpatches[prevOrthoPatchId].getSizeV() + 1;
          sizeU         = diffSizeU;
          sizeV         = diffSizeV;
        }
        si.get2dPosX()        = subpatch.getU0();
        si.get2dPosY()        = subpatch.getV0();
        si.getPosBiasX()      = subpatch.getU1();
        si.getPosBiasY()      = subpatch.getV1();
        si.get2dSizeXMinus1() = sizeU;
        si.get2dSizeYMinus1() = sizeV;
        if (subpatch.getScale() != subpatch.getFrameScale()) {
          si.getScalePresentFlag() = true;
          uint8_t n                = 0;
          auto    ratio = subpatch.getScale() / subpatch.getFrameScale();
          double  num   = log(ratio);
          double  den   = log(packingScaling);
          double  nDbl  = num / den;
          n             = round(nDbl) - 1;
          si.getSubpatchScale() = n;
        } else si.getScalePresentFlag() = false;
        prevOrthoPatchId = i;
      }
    }
  }
  tpii.getSubpatchInterEnableFlag() = enableSubpatchInter;
  tpii.getSubpatchRawEnableFlag()   = subpatchRawEnableFlag;
  return true;
}

bool
AtlasEncoder::createAtlasMeshSubPatchMerge(
  vmesh::VMCSubmesh&                        encFrame,
  int32_t                            submeshPos,
  int32_t                            frameIndex,
  TextureProjectionMergeInformation& tpmi,
  double                             packingScaling,
  AtlasPatch&                        refFrame) {
  auto&            subpatches    = encFrame.packedCCList;
  auto&            refSubpatches = refFrame.packedCCList;
  std::vector<int> indexToUpdateList;
  indexToUpdateList.clear();
  for (int i = 0; i < subpatches.size(); i++) {
    int32_t deltaU0 =
      (int32_t)subpatches[i].getU0() - (int32_t)refSubpatches[i].getU0();
    int32_t deltaV0 =
      (int32_t)subpatches[i].getV0() - (int32_t)refSubpatches[i].getV0();
    int32_t deltaU1 =
      (int32_t)subpatches[i].getU1() - (int32_t)refSubpatches[i].getU1();
    int32_t deltaV1 =
      (int32_t)subpatches[i].getV1() - (int32_t)refSubpatches[i].getV1();
    int32_t deltaSizeU =
      (int32_t)subpatches[i].getSizeU() - (int32_t)refSubpatches[i].getSizeU();
    int32_t deltaSizeV =
      (int32_t)subpatches[i].getSizeV() - (int32_t)refSubpatches[i].getSizeV();

    if ((deltaU0 != (int32_t)0) || (deltaV0 != (int32_t)0)
        || (deltaU1 != (int32_t)0) || (deltaV1 != (int32_t)0)
        || (deltaSizeU != (int32_t)0) || (deltaSizeV != (int32_t)0)) {
      indexToUpdateList.push_back(i);
    }
  }
  if (indexToUpdateList.size() > 0) {
    tpmi.getSubpatchMergePresentFlag() = true;
    tpmi.getSubpatchCountMinus1()      = indexToUpdateList.size() - 1;
    for (int i = 0; i < tpmi.getSubpatchCountMinus1() + 1; i++) {
      auto&   smi = tpmi.getSubpatchMerge(i);
      int     idx = indexToUpdateList[i];
      int32_t deltaU0 =
        (int32_t)subpatches[idx].getU0() - (int32_t)refSubpatches[idx].getU0();
      int32_t deltaV0 =
        (int32_t)subpatches[idx].getV0() - (int32_t)refSubpatches[idx].getV0();
      int32_t deltaU1 =
        (int32_t)subpatches[i].getU1() - (int32_t)refSubpatches[i].getU1();
      int32_t deltaV1 =
        (int32_t)subpatches[i].getV1() - (int32_t)refSubpatches[i].getV1();
      int32_t deltaSizeU = (int32_t)subpatches[idx].getSizeU()
                           - (int32_t)refSubpatches[idx].getSizeU();
      int32_t deltaSizeV = (int32_t)subpatches[idx].getSizeV()
                           - (int32_t)refSubpatches[idx].getSizeV();
      smi.getSubpatchIdx()   = idx;
      smi.get2dPosXDelta()   = deltaU0;
      smi.get2dPosYDelta()   = deltaV0;
      smi.getPosBiasXDelta() = deltaU1;
      smi.getPosBiasYDelta() = deltaV1;
      smi.get2dSizeXDelta()  = deltaSizeU;
      smi.get2dSizeYDelta()  = deltaSizeV;
    }
  } else {
    tpmi.getSubpatchMergePresentFlag() = false;
  }
  return true;
}

bool
AtlasEncoder::reconstructTextureProjectionInformation(
  const TextureProjectionInformation& tpi,
  double                              aspsScale,
  int32_t                             qpTexCoord,
  bool                                extendedProjectionEnabledFlag,
  AtlasPatch&                         reconAtlasPatch) {
  int64_t frameUpscale   = tpi.getFrameUpscaleMinus1() + 1;
  int     frameDownscale = tpi.getFrameDownscale();
  reconAtlasPatch.frameScale =
    (double)frameUpscale / (double)std::pow(2, frameDownscale);
  reconAtlasPatch.packedCCList.resize(tpi.getSubpatchCountMinus1() + 1);
  auto rawUvId = extendedProjectionEnabledFlag ? 18 : 6;
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int i = 0; i < tpi.getSubpatchCountMinus1() + 1; i++) {
    auto& subpatch = reconAtlasPatch.packedCCList[i];
    subpatch.setFaceId(tpi.getFaceId2SubpatchIdx(i));
    if (tpi.getSubpatchRawEnableFlag() && tpi.getSubpatchRawPresentFlag(i)) {
      subpatch.setProjection(
        rawUvId);  //convert flag info to special projection id
      auto sri = tpi.getSubpatchRaw(i);
      subpatch.setNumRawUVMinus1(sri.getNumRawUVMinus1());
      subpatch.getUcoords().resize(sri.getNumRawUVMinus1() + 1);
      subpatch.getVcoords().resize(sri.getNumRawUVMinus1() + 1);
      const auto scaleTexCoord  = std::pow(2.0, qpTexCoord) - 1.0;
      const auto iscaleTexCoord = 1.0 / scaleTexCoord;
      for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
        subpatch.setUcoord(i, sri.getUcoord(i) * iscaleTexCoord);
        subpatch.setVcoord(i, sri.getVcoord(i) * iscaleTexCoord);
      }
    } else {
      auto si = tpi.getSubpatch(i);
      subpatch.setProjection(si.getProjectionId());
      subpatch.setOrientation(si.getOrientationId());
      int sizeU = si.get2dSizeXMinus1();
      int sizeV = si.get2dSizeYMinus1();
      if (i > 0) {
        sizeU += reconAtlasPatch.packedCCList[i - 1].getSizeU() - 1;
        sizeV += reconAtlasPatch.packedCCList[i - 1].getSizeV() - 1;
      }
      subpatch.setU0(si.get2dPosX());
      subpatch.setV0(si.get2dPosY());
      subpatch.setU1(si.getPosBiasX());
      subpatch.setV1(si.getPosBiasY());
      subpatch.setSizeU(sizeU + 1);
      subpatch.setSizeV(sizeV + 1);
      subpatch.setFrameScale(reconAtlasPatch.frameScale);
      double scale = reconAtlasPatch.frameScale;
      if (si.getScalePresentFlag()) {
        for (int j = 0; j <= si.getSubpatchScale(); j++) {
          scale *= aspsScale;
        }
      }
      subpatch.setScale(scale);
      prevOrthoPatchId = i;
    }
  }
  return 0;
}

bool
AtlasEncoder::reconstructTextureProjectionInterInformation(
  const TextureProjectionInterInformation& tpii,
  double                                   aspsScale,
  int32_t                                  qpTexCoord,
  bool                                     extendedProjectionEnabledFlag,
  AtlasPatch&                              reconAtlasPatch,
  AtlasPatch&                              reconAtlasPatchRef) {
  int64_t frameUpscale   = tpii.getFrameUpscaleMinus1() + 1;
  int     frameDownscale = tpii.getFrameDownscale();
  reconAtlasPatch.frameScale =
    (double)frameUpscale / (double)std::pow(2, frameDownscale);
  reconAtlasPatch.packedCCList.clear();
  reconAtlasPatch.packedCCList.resize(tpii.getSubpatchCountMinus1() + 1);
  // copy the projection parameters
  auto rawUvId = extendedProjectionEnabledFlag ? 18 : 6;
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int i = 0; i < tpii.getSubpatchCountMinus1() + 1; i++) {
    if (tpii.getSubpatchInterEnableFlag() && tpii.getUpdateFlag(i)) {
      auto& subpatch = reconAtlasPatch.packedCCList[i];
      subpatch.setFaceId(tpii.getFaceId2SubpatchIdx(i));
      auto  sid = tpii.getSubpatchInter(i);
      auto& subpatchRef =
        reconAtlasPatchRef.packedCCList[i + sid.getSubpatchIdxDiff()];
      subpatch.setProjection(subpatchRef.getProjection());
      subpatch.setOrientation(subpatchRef.getOrientation());
      subpatch.setU0(subpatchRef.getU0() + sid.get2dPosXDelta());
      subpatch.setV0(subpatchRef.getV0() + sid.get2dPosYDelta());
      subpatch.setU1(subpatchRef.getU1() + sid.getPosBiasXDelta());
      subpatch.setV1(subpatchRef.getV1() + sid.getPosBiasYDelta());
      subpatch.setSizeU(subpatchRef.getSizeU() + sid.get2dSizeXDelta());
      subpatch.setSizeV(subpatchRef.getSizeV() + sid.get2dSizeYDelta());
      subpatch.setFrameScale(reconAtlasPatch.frameScale);
      if (subpatchRef.getFrameScale() == subpatchRef.getScale())
        subpatch.setScale(reconAtlasPatch.frameScale);
      else {
        // uses the same number of scale factors
        uint8_t n     = 0;
        auto    ratio = subpatchRef.getScale() / subpatchRef.getFrameScale();
        double  num   = log(ratio);
        double  den   = log(aspsScale);
        double  nDbl  = num / den;
        n             = round(nDbl) - 1;
        double scale  = reconAtlasPatch.frameScale;
        for (int i = 0; i <= n; i++) { scale *= aspsScale; }
        subpatch.setScale(scale);
      }
    } else if (tpii.getSubpatchRawEnableFlag()
               && tpii.getSubpatchRawPresentFlag(i)) {
      auto& subpatch = reconAtlasPatch.packedCCList[i];
      subpatch.setFaceId(tpii.getFaceId2SubpatchIdx(i));
      subpatch.setProjection(
        rawUvId);  //convert flag info to special projection id
      auto sri = tpii.getSubpatchRaw(i);
      subpatch.setNumRawUVMinus1(sri.getNumRawUVMinus1());
      subpatch.getUcoords().resize(sri.getNumRawUVMinus1() + 1);
      subpatch.getVcoords().resize(sri.getNumRawUVMinus1() + 1);
      const auto scaleTexCoord  = std::pow(2.0, qpTexCoord) - 1.0;
      const auto iscaleTexCoord = 1.0 / scaleTexCoord;
      for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
        subpatch.setUcoord(i, sri.getUcoord(i) * iscaleTexCoord);
        subpatch.setVcoord(i, sri.getVcoord(i) * iscaleTexCoord);
      }
    } else {
      auto& subpatch = reconAtlasPatch.packedCCList[i];
      subpatch.setFaceId(tpii.getFaceId2SubpatchIdx(i));
      auto si = tpii.getSubpatch(i);
      subpatch.setProjection(si.getProjectionId());
      subpatch.setOrientation(si.getOrientationId());
      int sizeU = si.get2dSizeXMinus1();
      int sizeV = si.get2dSizeYMinus1();
      if (i > 0) {
        sizeU += reconAtlasPatch.packedCCList[prevOrthoPatchId].getSizeU() - 1;
        sizeV += reconAtlasPatch.packedCCList[prevOrthoPatchId].getSizeV() - 1;
      }
      subpatch.setU0(si.get2dPosX());
      subpatch.setV0(si.get2dPosY());
      subpatch.setU1(si.getPosBiasX());
      subpatch.setV1(si.getPosBiasY());
      subpatch.setSizeU(sizeU + 1);
      subpatch.setSizeV(sizeV + 1);
      subpatch.setFrameScale(reconAtlasPatch.frameScale);
      double scale = reconAtlasPatch.frameScale;
      if (si.getScalePresentFlag()) {
        for (int i = 0; i <= si.getSubpatchScale(); i++) {
          scale *= aspsScale;
        }
      }
      subpatch.setScale(scale);
      prevOrthoPatchId = i;
    }
  }
  return 0;
}

bool
AtlasEncoder::reconstructTextureProjectionMergeInformation(
  const TextureProjectionMergeInformation& tpmi,
  double                                   aspsScale,
  AtlasPatch&                              reconAtlasPatch,
  AtlasPatch&                              reconAtlasPatchRef) {
  reconAtlasPatch.frameScale   = reconAtlasPatchRef.frameScale;
  reconAtlasPatch.packedCCList = reconAtlasPatchRef.packedCCList;
  if (tpmi.getSubpatchMergePresentFlag()) {
    for (int i = 0; i < tpmi.getSubpatchCountMinus1() + 1; i++) {
      auto smi = tpmi.getSubpatchMerge(i);
      int  idx = smi.getSubpatchIdx();
      reconAtlasPatch.packedCCList[idx].setU0(
        reconAtlasPatchRef.packedCCList[idx].getU0() + smi.get2dPosXDelta());
      reconAtlasPatch.packedCCList[idx].setV0(
        reconAtlasPatchRef.packedCCList[idx].getV0() + smi.get2dPosYDelta());
      reconAtlasPatch.packedCCList[idx].setU1(
        reconAtlasPatchRef.packedCCList[idx].getU1() + smi.getPosBiasXDelta());
      reconAtlasPatch.packedCCList[idx].setV1(
        reconAtlasPatchRef.packedCCList[idx].getV1() + smi.getPosBiasYDelta());
      reconAtlasPatch.packedCCList[idx].setSizeU(
        reconAtlasPatchRef.packedCCList[idx].getSizeU()
        + smi.get2dSizeXDelta());
      reconAtlasPatch.packedCCList[idx].setSizeV(
        reconAtlasPatchRef.packedCCList[idx].getSizeV()
        + smi.get2dSizeYDelta());
    }
  }
  return 0;
}

//============================================================================
void
AtlasEncoder::checkAttributeTilesConsistency(
  AfpsVdmcExtension&                          afve,
  AtlasSequenceParameterSetRbsp&              asps,
  int32_t                                     refAttrIdx,
  std::vector<std::vector<AtlasTile>>&        tileAreasInVideo_,
  std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_) {
  bool                 consistent = true;
  static constexpr int shift      = 14;
  auto                 scale      = [](auto num, auto den) {
    return ((num << shift) + (den >> 1)) / den;
  };
  auto invscale = [](auto s) { return (s + (1 << (shift - 1))) >> shift; };
  auto div64    = [](auto s) {
    auto ss = s / 64;
    return ss == 0 ? 1 : ss;
  };

  int attributeCount =
    asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
  if (attributeCount > 0) {
    // refAttrIdx should be smaller than attributeCount
    const auto& ref = afve.getAtlasFrameTileAttributeInformation(refAttrIdx);
    for (int attrIdx = 0; attrIdx < attributeCount; attrIdx++) {
      if (attrIdx == refAttrIdx) { continue; }
      auto& other = afve.getAtlasFrameTileAttributeInformation(attrIdx);
      if (ref.getNumTilesInAtlasFrameMinus1()
            != other.getNumTilesInAtlasFrameMinus1()
          || ref.getSignalledTileIdFlag() != other.getSignalledTileIdFlag()) {
        consistent = false;
        break;
      }
      if (ref.getSignalledTileIdFlag()) {
        if (ref.getSignalledTileIdLengthMinus1()
            != other.getSignalledTileIdLengthMinus1()) {
          consistent = false;
          break;
        }
        for (int i = 0; i <= ref.getNumTilesInAtlasFrameMinus1(); i++) {
          if (ref.getTileId(i) != other.getTileId(i)) {
            consistent = false;
            break;
          }
        }
      }
      if (ref.getNumTilesInAtlasFrameMinus1() == 0) { consistent = true; }
      const auto rW = scale(attributeVideoSize_[attrIdx].first,
                            attributeVideoSize_[refAttrIdx].first);
      const auto rH = scale(attributeVideoSize_[attrIdx].second,
                            attributeVideoSize_[refAttrIdx].second);

      //TODO: Nael.Ouedraogo@crf.canon.fr, ikai.tomohiro@sharp.co.jp, hong.sujun@sharp.co.jp, tokumo.yasuaki@sharp.co.jp, Franck.Denoual@crf.canon.fr
      //check this one to see if it makes sense
      for (int ti = 0; ti <= ref.getNumTilesInAtlasFrameMinus1(); ti++) {
        const auto& tileAreaRef =
          tileAreasInVideo_[ti][0].tileAttributeAreas_[refAttrIdx];
        const auto& tileArea =
          tileAreasInVideo_[ti][0].tileAttributeAreas_[attrIdx];
        if (invscale(tileAreaRef.LTx * rW) != tileArea.LTx
            || invscale(tileAreaRef.LTy * rH) != tileArea.LTy
            || invscale(tileAreaRef.sizeX * rW) != tileArea.sizeX
            || invscale(tileAreaRef.sizeY * rH) != tileArea.sizeY) {
          consistent = false;
          break;
        }
      }
    }
  }
  if (attributeCount > 1) {
    afve.setAfveConsistentTilingAccrossAttributesFlag(consistent);
  } else afve.setAfveConsistentTilingAccrossAttributesFlag(true);
}

}  // namespace vmesh
