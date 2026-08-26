/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
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

#include "util/checksum.hpp"
#include "atlasDecoder.hpp"
using namespace atlas;
void
AtlasDataDecoder::decompressTileInformation(
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
//----------------------------------------------------------------------------
void
AtlasDataDecoder::decodeLiftingTransformParameters(
  vmesh::LiftingTransformParameters&           trParam,
  int                                   subdivIterationCount,
  bool                                  enableLiftingOffsetFlag,
  const VdmcLiftingTransformParameters& ltp,
  const VdmcLiftingTransformParameters& higherLevelLtp) {
#if 1
  printf("enableLiftingOffsetFlag: "
         "%d\tltp.getLiftingOffsetValuesNumerator().size(): %d\n",
         (int)enableLiftingOffsetFlag,
         ltp.getLiftingOffsetValuesNumerator().size());
  for (int i = 0; i < trParam.liftingOffsetValues_.size(); i++)
    printf("trParam.liftingOffsetValues_[%d] %f\n",
           i,
           trParam.liftingOffsetValues_[i]);
#endif
  trParam.setSkipUpdateFlag(ltp.getSkipUpdateFlag());
  trParam.predWeight_.resize(subdivIterationCount, 0);
  trParam.updateWeight_.resize(subdivIterationCount, 0);
  if (trParam.getSkipUpdateFlag()) {
    // trParam.updateWeight_[l] = 0
  } else {
    trParam.valenceUpdateWeightFlag_ = ltp.getValenceUpdateWeightFlag();
    for (int l = 0; l < subdivIterationCount; l++) {
      if ((ltp.getAdaptiveUpdateWeightFlag() == 1) || (l == 0)) {
        trParam.updateWeight_[l] =
          (double)ltp.getLiftingUpdateWeightNumerator()[l]
          / (double)(ltp.getLiftingUpdateWeightDenominatorMinus1()[l] + 1);
      } else {
        trParam.updateWeight_[l] = trParam.updateWeight_[0];
      }
    }
  }

  for (int l = 0; l < subdivIterationCount; l++) {
    trParam.predWeight_[l] =
      (double)ltp.getLiftingPredictionWeightNumerator()[l]
      / (double)(ltp.getLiftingPredictionWeightDenominatorMinus1()[l] + 1);
  }
  /*} else { // When main params not present at patch or AFPS, inherit from higher level i.e, AFPS or ASPS respectively
    assert(higherLevelLtp.getLodCount() == subdivIterationCount + 1);
    trParam.setSkipUpdateFlag(higherLevelLtp.getSkipUpdateFlag());
    trParam.predWeight_.resize(subdivIterationCount, 0);
    trParam.updateWeight_.resize(subdivIterationCount, 0);
    if (trParam.getSkipUpdateFlag()) {
      //
    } else {
      trParam.valenceUpdateWeightFlag_ = higherLevelLtp.getValenceUpdateWeightFlag();
      for (int l = 0; l < subdivIterationCount; l++) {
        if ((higherLevelLtp.getAdaptiveUpdateWeightFlag() == 1) || (l == 0)) {
            trParam.updateWeight_[l] =
              (double)higherLevelLtp.getLiftingUpdateWeightNumerator()[l]
              / (double)(higherLevelLtp.getLiftingUpdateWeightDenominatorMinus1()[l] + 1);
        } else {
          trParam.updateWeight_[l] = trParam.updateWeight_[0];
        }
      }
    }

    for (int l = 0; l < subdivIterationCount; l++) {
      trParam.predWeight_[l] =
        (double)higherLevelLtp.getLiftingPredictionWeightNumerator()[l]
        / (double)(higherLevelLtp.getLiftingPredictionWeightDenominatorMinus1()[l] + 1);
    }
  }*/
}

void
AtlasDataDecoder::decodeQuantizationParameters(
  vmesh::QuantizationParameters&              qParameters,
  int                                  qpIdx,
  const VdmcQuantizationParameters&    vdmcQp,
  int32_t                              lodCount,
  bool                                 qpEnable,
  const AtlasSequenceParameterSetRbsp& asps,
  const AtlasFrameParameterSetRbsp&    afps) {
  int  aspsIdx          = 0;
  int  afpsIdx          = 0;
  auto bitDepthPosition = asps.getGeometry3dBitdepthMinus1() + 1;
  //  // determining the inverse quantization scale factor
  int dispDimension =
    asps.getAsveExtension().getAspsDispComponents();
  //  //int lodCount = frame.subdivInfoLevelOfDetails.size();
  std::vector<std::vector<double>>& iscale = qParameters.iscale;
  std::vector<std::vector<double>>& lodQp  = qParameters.lodQp;
  std::vector<uint32_t>& log2InverseScale = qParameters.log2InverseScale;
  log2InverseScale.resize(dispDimension, 0);
  if (!vdmcQp.getVdmcLodQuantizationFlag())
      log2InverseScale = vdmcQp.getVdmcLog2LodInverseScale();
  uint8_t& lodQuantFlag = qParameters.lodQuantFlag;
  lodQuantFlag = vdmcQp.getVdmcLodQuantizationFlag();
  uint8_t& directQuantFlag = qParameters.directQuantFlag;
  directQuantFlag = vdmcQp.getVdmcDirectQuantizationEnabledFlag();
  uint16_t& BitDepthOffset = qParameters.BitDepthOffset;
  BitDepthOffset = vdmcQp.getVdmcBitDepthOffset();

  //QuantizationParameter[i].resize(lodCount); 0<=i<3 - asps, afps, patch
  std::vector<std::vector<double>> InverseScale;
  //initialization
  iscale.resize(lodCount);
  InverseScale.resize(lodCount);
  lodQp.resize(lodCount);
  for (int i = 0; i < lodCount; i++) {
    iscale[i].resize(dispDimension, 0);
    lodQp[i].resize(dispDimension, 0);
    InverseScale[i].resize(dispDimension, 0);
  }

  if (vdmcQp.getVdmcLodQuantizationFlag() == 0) {
    for (int d = 0; d < dispDimension; d++)
      for (int l = 0; l < lodCount; l++)
        lodQp[l][d] = vdmcQp.getVdmcQuantizationParameters()[d];
  } else {
    for (int l = 0; l < lodCount; l++)
      for (int d = 0; d < dispDimension; d++) {
        int    refLodQP = 49;
        int8_t delta    = 0;
        if (qpEnable)
          delta = (1 - 2 * vdmcQp.getVdmcLodDeltaQPSign()[l][d])
                  * vdmcQp.getVdmcLodDeltaQPValue()[l][d];

        if (qpIdx == 0) {  //asps
          refLodQP =
            asps.getAsveExtension().getAsveDisplacementReferenceQPMinus49()
            + 49;
        } else if (qpIdx == 1) {
          if (lodCount
              <= asps.getAsveExtension().getAsveSubdivisionIterationCount()
                   + 1)
            refLodQP = aspsLodQuantizationParametersList_[aspsIdx].lodQp[l][d];
          else
            refLodQP = aspsLodQuantizationParametersList_[aspsIdx].lodQp[0][d];
        } else {
          // qpIdx == 2
          if (lodCount
              <= afps.getAfveExtension().getAfveSubdivisionIterationCount()
                   + 1)
            refLodQP = afpsLodQuantizationParametersList_[afpsIdx].lodQp[l][d];
          else
            refLodQP = afpsLodQuantizationParametersList_[afpsIdx].lodQp[0][d];
        }

        lodQp[l][d] = refLodQP + delta;
      }
  }

  //InverseScale
  for (int l = 0; l < lodCount; l++)
    for (int d = 0; d < dispDimension; d++) {
      if (vdmcQp.getVdmcDirectQuantizationEnabledFlag()) {
        InverseScale[l][d] =
          1.0 / (std::max)(1.0, (std::min)(lodQp[l][d], 100.0));
        ;
      } else {
        InverseScale[l][d] =
          pow(0.5,
              16 + vdmcQp.getVdmcBitDepthOffset() - bitDepthPosition
                + (4 - lodQp[l][d]) / 6.0);
      }
    }
  // iscale
  if (vdmcQp.getVdmcLodQuantizationFlag()) {
    for (int l = 0; l < lodCount; l++)
      for (int d = 0; d < dispDimension; d++)
        iscale[l][d] = InverseScale[l][d];
  } else {
    std::vector<double> levelOfDetailInverseScale;
    levelOfDetailInverseScale.resize(dispDimension, 0);
    for (int d = 0; d < dispDimension; d++) {
      iscale[0][d] = InverseScale[0][d];
      levelOfDetailInverseScale[d] =
        std::pow(2, vdmcQp.getVdmcLog2LodInverseScale()[d]);
    }
    for (int l = 1; l < lodCount; l++)
      for (int d = 0; d < dispDimension; d++)
        iscale[l][d] = iscale[l - 1][d] * levelOfDetailInverseScale[d];
  }
}

bool
AtlasDataDecoder::decompressAtlasPatch(
  uint32_t                 atlPos,
  const AtlasBitstream& adStream,
  int32_t                  frameIndex,
  int32_t                  tileIndex,
  AtlasTileLayerRbsp&      atl,
  std::vector<AtlasTile>&    referenceFrameList) {  //frameCount
  fflush(stdout);
  int32_t  atlasId             = adStream.getAtlasId();
  auto&    ath                 = atl.getHeader();
  auto&    atdu                = atl.getDataUnit();
  auto     afpsId              = ath.getAtlasFrameParameterSetId();
  auto     afps                = adStream.getAtlasFrameParameterSet(afpsId);
  auto     aspsId              = afps.getAtlasSequenceParameterSetId();
  auto&    asps                = adStream.getAtlasSequenceParameterSet(aspsId);
  auto&    aspsVmcExt          = asps.getAsveExtension();
  uint32_t patchBlockSize      = 1 << asps.getLog2PatchPackingBlockSize();
  uint32_t patchSizeXQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeXinfoQuantizer())
                                   : patchBlockSize;
  uint32_t patchSizeYQuantizer = asps.getPatchSizeQuantizerPresentFlag()
                                   ? (1 << ath.getPatchSizeYinfoQuantizer())
                                   : patchBlockSize;

  printf("Decompress atlas patch: Frame = %d Tile = %d TileId = %d\n",
         frameIndex,
         tileIndex,
         ath.getAtlasTileHeaderId());
  uint32_t patchIndex   = 0;
  int32_t  predictorIdx = 0;
  auto&    decodedTile  = decodedTiles_[tileIndex][frameIndex];
  decodedTile.tileId_   = ath.getAtlasTileHeaderId();
  decodedTile.tileType_ = ath.getType();
  for (auto& pi : atdu.getPatchInformationData()) {
    AtlasPatch decodedPatch;
    decodedPatch.attributePatchArea.resize(
      aspsVmcExt.getAspsAttributeNominalFrameSizeCount());
    if ((ath.getType() == I_TILE && pi.getPatchMode() == I_INTRA)
        || (ath.getType() == P_TILE && pi.getPatchMode() == P_INTRA)) {
      auto& pdu               = pi.getMeshpatchDataUnit();
      decodedPatch.submeshId_ = pdu.getMduSubmeshId();
      decodedPatch.displId_   = pdu.getMduDisplId();
      if (asps.getAsveExtension().getAsveLodPatchesEnableFlag() == 1) {
        decodedPatch.lodIdx_ = pdu.getMduLoDIdx();
      } else {
        decodedPatch.lodIdx_ = 0;
      }

      if (!aspsVmcExt.getAsveDisplacementIdPresentFlag()) {
        decodedPatch.geometryPatchArea.sizeX =
          (int)(pdu.getMduGeometry2dSizeXMinus1() + 1) * patchBlockSize;
        decodedPatch.geometryPatchArea.sizeY =
          (int)(pdu.getMduGeometry2dSizeYMinus1() + 1) * patchBlockSize;
        decodedPatch.geometryPatchArea.LTx =
          (int)pdu.getMduGeometry2dPosX() * patchBlockSize;
        decodedPatch.geometryPatchArea.LTy =
          (int)pdu.getMduGeometry2dPosY() * patchBlockSize;
      }

      if (decodedPatch.lodIdx_ == 0) {
        decodedPatch.subdivIteration = pdu.getMduSubdivisionIterationCount();
        if (decodedPatch.subdivIteration > 0) {
          decodedPatch.subdivMethod.resize(decodedPatch.subdivIteration);
          for (int32_t it = 0; it < decodedPatch.subdivIteration; it++) {
            decodedPatch.subdivMethod[it] = pdu.getMduSubdivisionMethod()[it];
          }
        }
        decodedPatch.subdivMinEdgeLength = pdu.getMduSubdivisionMinEdgeLength();
        if (decodedPatch.lodIdx_ == 0) {
          decodedPatch.displacementCoordinateSystem =
            (vmesh::DisplacementCoordinateSystem)
              pdu.getMduDisplacementCoordinateSystem();
          decodedPatch.transformMethod = (int)pdu.getMduTransformMethod();

          if (decodedPatch.transformMethod > 0) {
            if (pdu.getMduLiftingOffsetPresentFlag()) {
              auto& trParam = decodedPatch.transformParameters;
              VdmcLiftingTransformParameters& ltp =
                pdu.getMduLiftingTransformParameters();
              trParam.liftingOffsetValues_.resize(
                ltp.getLiftingOffsetValuesNumerator().size(), 0);
              trParam.liftingOffsetValuesNumerator_.resize(
                ltp.getLiftingOffsetValuesNumerator().size(), 0);
              trParam.liftingOffsetValuesDenominator_.resize(
                ltp.getLiftingOffsetValuesNumerator().size(), 0);
              for (int i = 0; i < pdu.getMduSubdivisionIterationCount(); i++) {
                trParam.liftingOffsetValuesNumerator_[i] =
                  ltp.getLiftingOffsetValuesNumerator()[i];
                trParam.liftingOffsetValuesDenominator_[i] =
                  ltp.getLiftingOffsetValuesDenominatorMinus1()[i];
                trParam.liftingOffsetValues_[i] =
                  ((double)(trParam.liftingOffsetValuesNumerator_[i])
                   / (double)(trParam.liftingOffsetValuesDenominator_[i] + 1));
              }
            }
            if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
              VdmcLiftingTransformParameters& ltp =
                pdu.getMduLiftingTransformParameters();
              auto& trParam = decodedPatch.transformParameters;
              trParam.dirlift_ =
                asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag();				
			  trParam.dirliftScale1_ = (double)ltp.getDirectionalLiftingScale1()/(double)(ltp.getDirectionalLiftingScaleDenoMinus1()+1);
              trParam.dirliftScale2_ = trParam.dirliftScale1_ + (double)ltp.getDirectionalLiftingDeltaScale2()/(double)(ltp.getDirectionalLiftingScaleDenoMinus1()+1);
              trParam.dirliftScale3_ = trParam.dirliftScale2_ + (double)ltp.getDirectionalLiftingDeltaScale3()/(double)(ltp.getDirectionalLiftingScaleDenoMinus1()+1);
              trParam.liftingScaleValues_.resize(2, 0);
              trParam.liftingScaleValues_[0] =
                (double)(pdu.getMduDirectionalLiftingMeanNum())
                / (double)(pdu.getMduDirectionalLiftingMeanDenoMinus1()
                           + 1);  //precision
              trParam.liftingScaleValues_[1] =
                (double)(pdu.getMduDirectionalLiftingStdNum())
                / (double)(pdu.getMduDirectionalLiftingStdDenoMinus1()
                           + 1);  //precision
              decodedPatch.dirLiftParams.MeanNumerator_ =
                pdu.getMduDirectionalLiftingMeanNum();
              decodedPatch.dirLiftParams.MeanDenominator_ =
                pdu.getMduDirectionalLiftingMeanDenoMinus1() + 1;
              decodedPatch.dirLiftParams.StdNumerator_ =
                pdu.getMduDirectionalLiftingStdNum();
              decodedPatch.dirLiftParams.StdDenominator_ =
                pdu.getMduDirectionalLiftingStdDenoMinus1() + 1;
              // printf("enableDirLiftFlag: %d\t pdu.getPduLiftingDirectionalScaleNumerator().size(): %d\n",
              //   (int)asps.getAsveExtension().getEnableDirectionalLiftingFlag(), pdu.getPduLiftingDirectionalScaleNumerator().size());
              // for (int i = 0; i < trParam.liftingScaleValues_.size(); i++)
              //   printf("trParam.liftingScaleValues_[%d] %f\n", i, trParam.liftingScaleValues_[i]);
            } else {
              auto& trParam = decodedPatch.transformParameters;
              trParam.liftingScaleValues_.resize(2, 0);
            }
            if (pdu.getMduTransformParametersPresentFlag()) {
              decodeLiftingTransformParameters(
                decodedPatch.transformParameters,
                pdu.getMduSubdivisionIterationCount(),
                0,
                pdu.getMduLiftingTransformParameters(),
                afps.getAfveExtension().getAfpsLtpDisplacement());
            } else if (afps.getAfveExtension()
                         .getAfveTransformParametersPresentFlag()) {
              decodeLiftingTransformParameters(
                decodedPatch.transformParameters,
                afps.getAfveExtension().getAfveSubdivisionIterationCount(),
                0,
                afps.getAfveExtension().getAfpsLtpDisplacement(),
                asps.getAsveExtension().getAspsExtLtpDisplacement());
            } else {
              decodeLiftingTransformParameters(
                decodedPatch.transformParameters,
                asps.getAsveExtension().getAsveSubdivisionIterationCount(),
                0,
                asps.getAsveExtension().getAspsExtLtpDisplacement(),
                asps.getAsveExtension().getAspsExtLtpDisplacement());
            }
          }
          auto qpIdx =
            pdu.getMduQuantizationPresentFlag() ? 2
            : afps.getAfveExtension().getAfveQuantizationParametersPresentFlag()
              ? 1
              : 0;
          auto& vdmcQp =
            qpIdx == 2 ? pdu.getMduQuantizationParameters()
            : qpIdx == 1
              ? afps.getAfveExtension().getAfveQuantizationParameters()
              : asps.getAsveExtension().getAsveQuantizationParameters();
          decodedPatch.IQ_skip =
            (!pdu.getMduQuantizationPresentFlag())
            && (!afps.getAfveExtension()
                   .getAfveQuantizationParametersPresentFlag())
            && (!asps.getAsveExtension()
                   .getAsveQuantizationParametersPresentFlag());

          if (qpIdx == 2) {
            decodeQuantizationParameters(decodedPatch.quantizationParameters,
                                         qpIdx,
                                         vdmcQp,
                                         decodedPatch.subdivIteration + 1,
                                         pdu.getMduQuantizationPresentFlag(),
                                         asps,
                                         afps);
          } else {
            decodedPatch.quantizationParameters =
              (qpIdx == 1) ? afpsLodQuantizationParametersList_[afpsId]
                           : aspsLodQuantizationParametersList_[aspsId];
          }
          if (asps.getAsveExtension().getAsveInverseQuantizationOffsetPresentFlag()
              && pdu.getMduInverseQuantizationOffsetEnableFlag()) {
            decodedPatch.iqOffsetFlag = pdu.getMduInverseQuantizationOffsetEnableFlag();
            decodedPatch.iqOffsets    = pdu.getIQOffsetValues();
          } else {
            decodedPatch.iqOffsetFlag = false;
          }
        }
      }

      if (!asps.getAsveExtension().getAsveLodPatchesEnableFlag()) {
        uint32_t lodCount = decodedPatch.subdivIteration + 1;
        decodedPatch.blockCount.resize(lodCount);
        decodedPatch.lastPosInBlock.resize(lodCount);
        for (int level = 0; level < lodCount; level++) {
          decodedPatch.blockCount[level] =
            pdu.getMduBlockCount()[level];
          decodedPatch.lastPosInBlock[level] =
            pdu.getMduLastPosInBlock()[level];
        }
      } else {
        decodedPatch.blockCount.resize((1));
        decodedPatch.lastPosInBlock.resize((1));
        decodedPatch.blockCount[0] = pdu.getMduBlockCount()[0];
        decodedPatch.lastPosInBlock[0]   = pdu.getMduLastPosInBlock()[0];
      }

      if (decodedPatch.lodIdx_ == 0) {
        if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
          int32_t smIdx = -1;
          for (size_t i = 0; i < afps.getAfveExtension()
                                   .getAtlasFrameMeshInformation()
                                   .getSubmeshIds()
                                   .size();
               i++) {
            if (afps.getAfveExtension()
                  .getAtlasFrameMeshInformation()
                  .getSubmeshId(i)
                == decodedPatch.submeshId_) {
              smIdx = i;
              break;
            }
          }
          if (smIdx < 0) { exit(-1); }
          if (afps.getAfveExtension().getProjectionTextcoordPresentFlag(
                smIdx)) {
            decodedPatch.projectionTextcoordMode = 1;
            auto& ath                            = atl.getHeader();
            auto& pid  = atl.getDataUnit().getPatchInformationData(patchIndex);
            auto& mmdu = pid.getMeshpatchDataUnit();
            auto& tpi  = mmdu.getTextureProjectionInformation();
            decodeTextureProjectionInformation(
              tpi, asps, patchIndex, frameIndex, decodedPatch);
          }
        }
      }

      printf("\tPI_INTRA:: patchIndex[%d] submeshId %d\n",
             patchIndex,
             decodedPatch.submeshId_);
      printf("\t\tdisplacement: current (%4d, %4d), (%4dx%4d) : signalled "
             "(%d,%d %dx%d)\n",
             decodedPatch.geometryPatchArea.LTx,
             decodedPatch.geometryPatchArea.LTy,
             decodedPatch.geometryPatchArea.sizeX,
             decodedPatch.geometryPatchArea.sizeY,
             pdu.getMduGeometry2dPosX(),
             pdu.getMduGeometry2dPosY(),
             pdu.getMduGeometry2dSizeXMinus1(),
             pdu.getMduGeometry2dSizeYMinus1());
    } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_INTER)) {
      auto& imdu                = pi.getInterMeshpatchDataUnit();
      auto& ltp                 = imdu.getImduLiftingTransformParameters();
      auto  referenceFrameIndex = ath.getReferenceList()[imdu.getImduRefIndex()];
      auto  referencePatchIndex = imdu.getImduPatchIndex() + predictorIdx;
      printf("\tP_INTER:: patchIndex[%d] submeshId %d\t",
             patchIndex,
             decodedPatch.submeshId_);
      printf("sigRefIndex: %d(refFrame:%d) referencePatchIndex: %d(%d+%d)\n",
             imdu.getImduRefIndex(),
             referenceFrameIndex,
             referencePatchIndex,
             predictorIdx,
             imdu.getImduPatchIndex());
      predictorIdx = referencePatchIndex + 1;

      auto& refPatches =
        referenceFrameList[referenceFrameIndex].patches_[referencePatchIndex];
      decodedPatch = refPatches;
      decodedPatch.submeshId_ =
        refPatches.submeshId_;  // imdu.getImduSubmeshId();
      if (!aspsVmcExt.getAsveDisplacementIdPresentFlag()) {
        decodedPatch.geometryPatchArea.sizeX =
          refPatches.geometryPatchArea.sizeX
          + (int)imdu.getImdu2dDeltaSizeX() * patchBlockSize;
        decodedPatch.geometryPatchArea.sizeY =
          refPatches.geometryPatchArea.sizeY
          + (int)imdu.getImdu2dDeltaSizeY() * patchBlockSize;
        decodedPatch.geometryPatchArea.LTx =
          refPatches.geometryPatchArea.LTx
          + (int)imdu.getImdu2dDeltaPosX() * patchBlockSize;
        decodedPatch.geometryPatchArea.LTy =
          refPatches.geometryPatchArea.LTy
          + (int)imdu.getImdu2dDeltaPosY() * patchBlockSize;
      }

      if (imdu.getImduLoDIdx() == 0) {
        decodedPatch.subdivIteration = refPatches.subdivIteration;
        if (decodedPatch.subdivIteration > 0) {
          decodedPatch.subdivMethod.resize(decodedPatch.subdivIteration);
          for (int32_t it = 0; it < decodedPatch.subdivIteration; it++) {
            decodedPatch.subdivMethod[it] = refPatches.subdivMethod[it];
          }
        }
        decodedPatch.displacementCoordinateSystem =
          refPatches.displacementCoordinateSystem;
        decodedPatch.transformMethod = refPatches.transformMethod;

        if (decodedPatch.transformMethod > 0) {
          //decodedPatch.transformParameter           = refPatches.transformParameter
          if (asps.getAsveExtension().getAsveLiftingOffsetPresentFlag()) {
            imdu.getImduLiftingTransformParameters()
              .getLiftingOffsetValuesNumerator()
              .resize(imdu.getImduSubdivisionIterationCount());
            imdu.getImduLiftingTransformParameters()
              .getLiftingOffsetValuesDenominatorMinus1()
              .resize(imdu.getImduSubdivisionIterationCount());
            if (refPatches.subdivIteration == 0) {
              refPatches.transformParameters.liftingOffsetValuesNumerator_[0] =
                0;
              refPatches.transformParameters
                .liftingOffsetValuesDenominator_[0] = 0;
            }

            if (imdu.getImduSubdivisionIterationCount()
                > refPatches.subdivIteration) {
              for (int i = 0; i < imdu.getImduSubdivisionIterationCount();
                   i++) {
                imdu.getImduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
                  i,
                  refPatches.transformParameters
                      .liftingOffsetValuesNumerator_[0]
                    + (int)imdu.getImduLiftingTransformParameters()
                        .getLiftingOffsetDeltaValuesNumerator()[i]);
                imdu.getImduLiftingTransformParameters()
                  .setLiftingOffsetValuesDenominatorMinus1(
                    i,
                    refPatches.transformParameters
                        .liftingOffsetValuesDenominator_[0]
                      + (int)imdu.getImduLiftingTransformParameters()
                          .getLiftingOffsetDeltaValuesDenominator()[i]);
              }
            } else {
              for (int i = 0; i < imdu.getImduSubdivisionIterationCount();
                   i++) {
                imdu.getImduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
                  i,
                  refPatches.transformParameters
                      .liftingOffsetValuesNumerator_[i]
                    + (int)imdu.getImduLiftingTransformParameters()
                        .getLiftingOffsetDeltaValuesNumerator()[i]);
                imdu.getImduLiftingTransformParameters()
                  .setLiftingOffsetValuesDenominatorMinus1(
                    i,
                    refPatches.transformParameters
                        .liftingOffsetValuesDenominator_[i]
                      + (int)imdu.getImduLiftingTransformParameters()
                          .getLiftingOffsetDeltaValuesDenominator()[i]);
              }
            }
          }

          if (imdu.getImduLiftingOffsetPresentFlag()) {
            auto& trParam = decodedPatch.transformParameters;
            VdmcLiftingTransformParameters& ltp =
              imdu.getImduLiftingTransformParameters();
            trParam.liftingOffsetValues_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            trParam.liftingOffsetValuesNumerator_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            trParam.liftingOffsetValuesDenominator_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            for (int i = 0; i < imdu.getImduSubdivisionIterationCount(); i++) {
              trParam.liftingOffsetValuesNumerator_[i] =
                ltp.getLiftingOffsetValuesNumerator()[i];
              trParam.liftingOffsetValuesDenominator_[i] =
                ltp.getLiftingOffsetValuesDenominatorMinus1()[i];
              if ((trParam.liftingOffsetValuesDenominator_[i] + 1) != 0) {
                trParam.liftingOffsetValues_[i] =
                  ((double)(trParam.liftingOffsetValuesNumerator_[i])
                   / (double)(trParam.liftingOffsetValuesDenominator_[i] + 1));
              } else {
                trParam.liftingOffsetValues_[i] = 0;
              }
            }
          }
          if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
            VdmcLiftingTransformParameters& ltp =
              imdu.getImduLiftingTransformParameters();
            auto& trParam = decodedPatch.transformParameters;
            trParam.dirlift_ =
              asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag();
            trParam.liftingScaleValues_.resize(2, 0);
            decodedPatch.dirLiftParams.MeanNumerator_ =
              refPatches.dirLiftParams.MeanNumerator_
              + imdu.getImduDirectionalLiftingDeltaMeanNum();
            decodedPatch.dirLiftParams.MeanDenominator_ =
              refPatches.dirLiftParams.MeanDenominator_
              + imdu.getImduDirectionalLiftingDeltaMeanDeno();
            decodedPatch.dirLiftParams.StdNumerator_ =
              refPatches.dirLiftParams.StdNumerator_
              + imdu.getImduDirectionalLiftingDeltaStdNum();
            decodedPatch.dirLiftParams.StdDenominator_ =
              refPatches.dirLiftParams.StdDenominator_
              + imdu.getImduDirectionalLiftingDeltaStdDeno();
            if (decodedPatch.dirLiftParams.MeanDenominator_ != 0) {
              trParam.liftingScaleValues_[0] =
                (double)(decodedPatch.dirLiftParams.MeanNumerator_)
                / (double)(decodedPatch.dirLiftParams.MeanDenominator_);
            } else {
              trParam.liftingScaleValues_[0] = 0;
            }
            if (decodedPatch.dirLiftParams.StdDenominator_ != 0) {
              trParam.liftingScaleValues_[1] =
                (double)(decodedPatch.dirLiftParams.StdNumerator_)
                / (double)(decodedPatch.dirLiftParams.StdDenominator_);
            } else {
              trParam.liftingScaleValues_[1] = 0;
            }
            // printf("enableDirLiftFlag: %d\t imdu.getImduLiftingDirectionalScaleNumerator().size(): %d\n",
            //   (int)asps.getAsveExtension().getEnableDirectionalLiftingFlag(), imdu.getImduLiftingDirectionalScaleNumerator().size());
            // for (int i = 0; i < trParam.liftingScaleValues_.size(); i++)
            //   printf("trParam.liftingScaleValues_[%d] %f\n", i, trParam.liftingScaleValues_[i]);
          } else {
            auto& trParam = decodedPatch.transformParameters;
            trParam.liftingScaleValues_.resize(2, 0);
          }

          if ((imdu.getImduTransformMethod()
               == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING)
              && (imdu.getImduTransformMethodPresentFlag()
                  || imdu.getImduTransformParametersPresentFlag()
                  || imdu.getImduSubdivisionIterationCountPresentFlag())) {
            decodeLiftingTransformParameters(
              decodedPatch.transformParameters,
              imdu.getImduSubdivisionIterationCount(),
              0,
              imdu.getImduLiftingTransformParameters(),
              afps.getAfveExtension().getAfpsLtpDisplacement());
          } else if (afps.getAfveExtension()
                       .getAfveTransformParametersPresentFlag()) {
            decodeLiftingTransformParameters(
              decodedPatch.transformParameters,
              afps.getAfveExtension().getAfveSubdivisionIterationCount(),
              0,
              afps.getAfveExtension().getAfpsLtpDisplacement(),
              asps.getAsveExtension().getAspsExtLtpDisplacement());
          } else {
            decodeLiftingTransformParameters(
              decodedPatch.transformParameters,
              asps.getAsveExtension().getAsveSubdivisionIterationCount(),
              0,
              asps.getAsveExtension().getAspsExtLtpDisplacement(),
              asps.getAsveExtension().getAspsExtLtpDisplacement());
          }
        }
        decodedPatch.quantizationParameters =
          refPatches.quantizationParameters;
        if (asps.getAsveExtension().getAsveInverseQuantizationOffsetPresentFlag()
            && imdu.getImduInverseQuantizationOffsetEnableFlag()) {
          decodedPatch.iqOffsetFlag = imdu.getImduInverseQuantizationOffsetEnableFlag();
          decodedPatch.iqOffsets    = imdu.getIQOffsetValues();
        } else {
          decodedPatch.iqOffsetFlag = false;
        }
      }

      uint32_t lodCount = decodedPatch.subdivIteration + 1;
      if (asps.getAsveExtension().getAsveLodPatchesEnableFlag()) {
        lodCount = 1;
      }
      decodedPatch.blockCount.resize(lodCount);
      decodedPatch.lastPosInBlock.resize(lodCount);
      //      std::vector<uint32_t> refPatchBlockCount =refPatches.blockCount;
      //      std::vector<uint32_t> refPatchLastPosInBlock = refPatches.lastPosInBlock;
      std::vector<uint32_t> refPatchBlockCount(lodCount, 0);
      std::vector<uint32_t> refPatchLastPosInBlock(lodCount, 0);
      for (int level = 0; level < refPatches.blockCount.size();
           level++) {
        refPatchBlockCount[level] = refPatches.blockCount[level];
        refPatchLastPosInBlock[level]   = refPatches.lastPosInBlock[level];
      }
      for (int level = 0; level < lodCount; level++) {
        decodedPatch.blockCount[level] =
          refPatches.blockCount[level]
          + imdu.getImduDeltaBlockCount()[level];
        decodedPatch.lastPosInBlock[level] =
          refPatches.lastPosInBlock[level]
          + imdu.getImduDeltaLastPosInBlock()[level];
      }

      if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()
          && imdu.getImduLoDIdx() == 0) {
        if (imdu.getImduTextureProjectionPresentFlag()) {
          decodedPatch.projectionTextcoordMode = 1;
          auto& ath                            = atl.getHeader();
          auto& pid  = atl.getDataUnit().getPatchInformationData(patchIndex);
          auto& imdu = pid.getInterMeshpatchDataUnit();
          auto& tpii = imdu.getTextureProjectionInterInformation();
          decodeTextureProjectionInterInformation(
            tpii, asps, patchIndex, frameIndex, decodedPatch, refPatches);
        }
      }
    }  //P_INTER
    else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_MERGE)) {
      auto& mmdu                = pi.getMergeMeshpatchDataUnit();
      auto& ltp = mmdu.getMmduLiftingTransformParameters();
      auto  referenceFrameIndex = ath.getReferenceList()[mmdu.getMmduRefIndex()];
      auto  referencePatchIndex = predictorIdx;
      predictorIdx              = referencePatchIndex + 1;
      auto& refPatch =
        referenceFrameList[referenceFrameIndex].patches_[referencePatchIndex];
      decodedPatch = refPatch;

      printf("\tP_MERGE:: patchIndex[%d] submeshId %d\t",
             patchIndex,
             decodedPatch.submeshId_);
      printf("sigRefIndex: %d(refFrame:%lld) referencePatchIndex: %d\n",
             referenceFrameIndex,
        mmdu.getMmduRefIndex(),
             referencePatchIndex);
      decodedPatch.transformParameters = refPatch.transformParameters;
        if (decodedPatch.transformMethod > 0) {
        if (asps.getAsveExtension().getAsveLiftingOffsetPresentFlag()) {
          mmdu.getMmduLiftingTransformParameters()
              .getLiftingOffsetValuesNumerator()
              .resize(mmdu.getMmduSubdivisionIterationCount());
          mmdu.getMmduLiftingTransformParameters()
              .getLiftingOffsetValuesDenominatorMinus1()
              .resize(mmdu.getMmduSubdivisionIterationCount());
            if (refPatch.subdivIteration == 0) {
              refPatch.transformParameters.liftingOffsetValuesNumerator_[0] =
                0;
              refPatch.transformParameters.liftingOffsetValuesDenominator_[0] =
                0;
            }

            if (mmdu.getMmduSubdivisionIterationCount()
                > refPatch.subdivIteration) {
              for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount();
                   i++) {
              mmdu.getMmduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
                  i,
                  refPatch.transformParameters.liftingOffsetValuesNumerator_[0]
                + (int)mmdu.getMmduLiftingTransformParameters()
                        .getLiftingOffsetDeltaValuesNumerator()[i]);
              mmdu.getMmduLiftingTransformParameters()
                  .setLiftingOffsetValuesDenominatorMinus1(
                    i,
                    refPatch.transformParameters
                        .liftingOffsetValuesDenominator_[0]
                  + (int)mmdu.getMmduLiftingTransformParameters()
                          .getLiftingOffsetDeltaValuesDenominator()[i]);
              }
          }
          else {
              for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount();
                   i++) {
              mmdu.getMmduLiftingTransformParameters().setLiftingOffsetValuesNumerator(
                  i,
                  refPatch.transformParameters.liftingOffsetValuesNumerator_[i]
                + (int)mmdu.getMmduLiftingTransformParameters()
                        .getLiftingOffsetDeltaValuesNumerator()[i]);
              mmdu.getMmduLiftingTransformParameters()
                  .setLiftingOffsetValuesDenominatorMinus1(
                    i,
                    refPatch.transformParameters
                        .liftingOffsetValuesDenominator_[i]
                  + (int)mmdu.getMmduLiftingTransformParameters()
                          .getLiftingOffsetDeltaValuesDenominator()[i]);
              }
            }
          }

        if (mmdu.getMmduLiftingOffsetPresentFlag()) {
            auto& trParam = decodedPatch.transformParameters;
            VdmcLiftingTransformParameters& ltp =
            mmdu.getMmduLiftingTransformParameters();
            trParam.liftingOffsetValues_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            trParam.liftingOffsetValuesNumerator_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            trParam.liftingOffsetValuesDenominator_.resize(
              ltp.getLiftingOffsetValuesNumerator().size(), 0);
            for (int i = 0; i < mmdu.getMmduSubdivisionIterationCount(); i++) {
              trParam.liftingOffsetValuesNumerator_[i] =
                ltp.getLiftingOffsetValuesNumerator()[i];
              trParam.liftingOffsetValuesDenominator_[i] =
                ltp.getLiftingOffsetValuesDenominatorMinus1()[i];
              if ((trParam.liftingOffsetValuesDenominator_[i] + 1) != 0) {
                trParam.liftingOffsetValues_[i] =
                  ((double)(trParam.liftingOffsetValuesNumerator_[i])
                   / (double)(trParam.liftingOffsetValuesDenominator_[i] + 1));
            }
            else {
                trParam.liftingOffsetValues_[i] = 0;
              }
            }
          }
        if (asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag()) {
            VdmcLiftingTransformParameters& ltp =
            mmdu.getMmduLiftingTransformParameters();
            auto& trParam = decodedPatch.transformParameters;
            trParam.dirlift_ =
            asps.getAsveExtension().getAsveDirectionalLiftingPresentFlag();
            trParam.liftingScaleValues_.resize(2, 0);
            decodedPatch.dirLiftParams.MeanNumerator_ =
              refPatch.dirLiftParams.MeanNumerator_
            + mmdu.getMmduDirectionalLiftingDeltaMeanNum();
            decodedPatch.dirLiftParams.MeanDenominator_ =
              refPatch.dirLiftParams.MeanDenominator_
            + mmdu.getMmduDirectionalLiftingDeltaMeanDeno();
            decodedPatch.dirLiftParams.StdNumerator_ =
              refPatch.dirLiftParams.StdNumerator_
            + mmdu.getMmduDirectionalLiftingDeltaStdNum();
            decodedPatch.dirLiftParams.StdDenominator_ =
              refPatch.dirLiftParams.StdDenominator_
            + mmdu.getMmduDirectionalLiftingDeltaStdDeno();
            if (decodedPatch.dirLiftParams.MeanDenominator_ != 0) {
              trParam.liftingScaleValues_[0] =
                (double)(decodedPatch.dirLiftParams.MeanNumerator_)
                / (double)(decodedPatch.dirLiftParams.MeanDenominator_);
          }
          else {
              trParam.liftingScaleValues_[0] = 0;
            }
            if (decodedPatch.dirLiftParams.StdDenominator_ != 0) {
              trParam.liftingScaleValues_[1] =
                (double)(decodedPatch.dirLiftParams.StdNumerator_)
                / (double)(decodedPatch.dirLiftParams.StdDenominator_);  //precision
            }
            else {
              trParam.liftingScaleValues_[1] = 0;
            }
            // printf("enableDirLiftFlag: %d\t mmdu.getMmduLiftingDirectionalScaleNumerator().size(): %d\n",
            //   (int)asps.getAsveExtension().getEnableDirectionalLiftingFlag(), mmdu.getMmduLiftingDirectionalScaleNumerator().size());
            // for (int i = 0; i < trParam.liftingScaleValues_.size(); i++)
            //   printf("trParam.liftingScaleValues_[%d] %f\n", i, trParam.liftingScaleValues_[i]);
        }
        else {
            auto& trParam = decodedPatch.transformParameters;
            trParam.liftingScaleValues_.resize(2, 0);
          }
        if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
          if (mmdu.getMmduTextureProjectionPresentFlag()) {
              decodedPatch.projectionTextcoordMode = 1;
              auto& ath                            = atl.getHeader();
              auto& pid =
                atl.getDataUnit().getPatchInformationData(patchIndex);
              auto& mmdu = pid.getMergeMeshpatchDataUnit();
              auto& tpmi = mmdu.getTextureProjectionMergeInformation();
              decodeTextureProjectionMergeInformation(
                tpmi, patchIndex, frameIndex, decodedPatch, refPatch);
            }
          }
        }
      if (asps.getAsveExtension().getAsveInverseQuantizationOffsetPresentFlag()
        && mmdu.getMmduInverseQuantizationOffsetEnableFlag()) {
        decodedPatch.iqOffsetFlag = mmdu.getMmduInverseQuantizationOffsetEnableFlag();
          decodedPatch.iqOffsets    = mmdu.getIQOffsetValues();
      }
      else {
          decodedPatch.iqOffsetFlag = false;
        }
      }
    else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_SKIP)) {
      int  refIdx              = 0;
      auto referenceFrameIndex = ath.getReferenceList()[refIdx];
      auto referencePatchIndex = predictorIdx;
      predictorIdx             = referencePatchIndex + 1;
      auto& refPatch =
        referenceFrameList[referenceFrameIndex].patches_[referencePatchIndex];
      decodedPatch = refPatch;

      printf("\tP_SKIP:: patchIndex[%d] submeshId %d\t",
             patchIndex,
             decodedPatch.submeshId_);
      printf("sigRefIndex: %d(refFrame:%lld) referencePatchIndex: %d\n",
             referenceFrameIndex,
             refIdx,
             referencePatchIndex);
      if (aspsVmcExt.getAsveProjectionTexcoordEnableFlag()) {
        int32_t smIdx = -1;
        for (size_t i = 0; i < afps.getAfveExtension()
                                 .getAtlasFrameMeshInformation()
                                 .getSubmeshIds()
                                 .size();
             i++) {
          if (afps.getAfveExtension()
                .getAtlasFrameMeshInformation()
                .getSubmeshId(i)
              == decodedPatch.submeshId_) {
            smIdx = (int32_t)i;
            break;
          }
        }
        if (smIdx < 0) { exit(-1); }
        if (afps.getAfveExtension().getProjectionTextcoordPresentFlag(smIdx)) {
          decodedPatch.projectionTextcoordMode = 1;
          auto& ath                            = atl.getHeader();
          //          auto& afps = adStream.getAtlasFrameParameterSet(decodedPatch.projectionAfpsId_);
          //          auto& asps = adStream.getAtlasSequenceParameterSet(decodedPatch.projectionAspsId_);
          auto& pid = atl.getDataUnit().getPatchInformationData(patchIndex);
          decodedPatch.projectionTextcoordSubpatches_ =
            refPatch.projectionTextcoordSubpatches_;
          decodedPatch.projectionTextcoordFrameScale_=
              refPatch.projectionTextcoordFrameScale_;
          decodedPatch.projectionTextcoordFrameUpscale_ =
            refPatch.projectionTextcoordFrameUpscale_;
          decodedPatch.projectionTextcoordFrameDownscale_ =
            refPatch.projectionTextcoordFrameDownscale_;
          decodedPatch.projectionTextcoordSubpatchCountMinus1_ =
            refPatch.projectionTextcoordSubpatchCountMinus1_;
        }
      }
    } else if (ath.getType() == I_TILE_ATTR
               && pi.getPatchMode() == I_INTRA_ATTR) {
      auto& pdu               = pi.getMeshpatchDataUnit();
      decodedPatch.submeshId_ = pdu.getMduSubmeshId();
      if (!decodedPatch.attributePatchArea.empty()) {
        for (int attrIdx = 0;
             attrIdx < aspsVmcExt.getAspsAttributeNominalFrameSizeCount();
             attrIdx++) {
          if (aspsVmcExt.getAsveAttributeSubtextureEnabledFlag()[attrIdx]) {
            decodedPatch.attributePatchArea[attrIdx].sizeX =
              (int)(pdu.getMduAttributes2dSizeXMinus1() + 1)
              * patchSizeXQuantizer;
            decodedPatch.attributePatchArea[attrIdx].sizeY =
              (int)(pdu.getMduAttributes2dSizeYMinus1() + 1)
              * patchSizeXQuantizer;
            decodedPatch.attributePatchArea[attrIdx].LTx =
              (int)pdu.getMduAttributes2dPosX() * patchBlockSize;
            decodedPatch.attributePatchArea[attrIdx].LTy =
              (int)pdu.getMduAttributes2dPosY() * patchBlockSize;
          } else {
                decodedPatch.attributePatchArea[attrIdx].sizeX =
                    decodedTile.tileAttributeAreas_[attrIdx].sizeX;
                decodedPatch.attributePatchArea[attrIdx].sizeY =
                    decodedTile.tileAttributeAreas_[attrIdx].sizeY;
                decodedPatch.attributePatchArea[attrIdx].LTx = 0;
                decodedPatch.attributePatchArea[attrIdx].LTy = 0;
          }
        }
        printf("\t\ttexture     : current (%4d, %4d), (%4dx%4d)\n",
               decodedPatch.attributePatchArea[0].LTx,
               decodedPatch.attributePatchArea[0].LTy,
               decodedPatch.attributePatchArea[0].sizeX,
               decodedPatch.attributePatchArea[0].sizeY);
      }
    } else if ((ath.getType() == P_TILE_ATTR
                && pi.getPatchMode() == I_INTRA_ATTR)
               || (ath.getType() == P_TILE_ATTR
                   && pi.getPatchMode() == P_SKIP_ATTR)) {
      //VERIFY_SOFTWARE_LIMITATION(ath.getType() == P_TILE_ATTR);
    }
    patchIndex++;
    decodedTiles_[tileIndex][frameIndex].patches_.push_back(decodedPatch);
  }  //pi
  return true;
}
//----------------------------------------------------------------------------
int32_t
AtlasDataDecoder::createAspsReferenceLists(
  const AtlasSequenceParameterSetRbsp& asps,
  std::vector<std::vector<int32_t>>&   aspsRefDiffList) {
  auto numRefList = asps.getNumRefAtlasFrameListsInAsps();
  aspsRefDiffList.resize(numRefList);
  //refFrameDiff:1,2,3,4...
  for (size_t listIdx = 0; listIdx < numRefList; listIdx++) {
    auto&  refList             = asps.getRefListStruct(listIdx);
    size_t numActiveRefEntries = refList.getNumRefEntries();
    aspsRefDiffList[listIdx].resize(numActiveRefEntries);
    for (size_t refIdx = 0; refIdx < numActiveRefEntries; refIdx++) {
      int32_t deltaAfocSt = 0;
      if (refList.getStRefAtlasFrameFlag(refIdx))
        deltaAfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                      * refList.getAbsDeltaAfocSt(refIdx);
      aspsRefDiffList[listIdx][refIdx] = deltaAfocSt;
    }  //refIdx
  }    //listIdx
  return 0;
}
int32_t
AtlasDataDecoder::createAthReferenceList(
  const AtlasSequenceParameterSetRbsp& asps,
  AtlasTileLayerRbsp&                  tile,
  std::vector<int32_t>&                referenceList) {
  auto& tileHeader = tile.getHeader();

  RefListStruct refListStruct;
  if (tileHeader.getRefAtlasFrameListSpsFlag()) {
    // choosing the list form the ASPS structure
    size_t refListIdx = 0;
    if (asps.getNumRefAtlasFrameListsInAsps() > 1)
      refListIdx = tileHeader.getRefAtlasFrameListIdx();
    refListStruct = asps.getRefListStruct(refListIdx);
    //    refListIdx=atlasHeader.getRefAtlasFrameListIdx();
    //  atlasHeader.setRefListStruct( asps.getRefListStruct(refListIdx));
  } else {
    // do nothing, use the list from the header itself
    refListStruct = tileHeader.getRefListStruct();
  }

  referenceList.clear();
  size_t listSize = refListStruct.getNumRefEntries();
  auto   afocBase = tileHeader.getFrameIndex();
  for (size_t idx = 0; idx < listSize; idx++) {
    int deltaAfocSt = 0;
    if (refListStruct.getStRefAtlasFrameFlag(idx))
      deltaAfocSt = (2 * refListStruct.getStrafEntrySignFlag(idx) - 1)
                    * refListStruct.getAbsDeltaAfocSt(idx);  // Eq.26
    int refPOC = afocBase - deltaAfocSt;
    if (refPOC >= 0) referenceList.push_back(refPOC);
    afocBase = refPOC;
  }

#if 1
  printf("(createAthReferenceList) referenceList:");
  for (size_t idx = 0; idx < referenceList.size(); idx++) {
    printf("%d\t", (int)referenceList[idx]);
  }
  printf("\n");
#endif

  return referenceList.size() == 0 ? -1 : referenceList[0];
}
//----------------------------------------------------------------------------
void
AtlasDataDecoder::decodeAtlasDataSubbitstream(AtlasBitstream& adStream,
                                              bool               checksum) {
  //  const auto& asps            = adStream.getAtlasSequenceParameterSet(0);
  //  const auto& afps            = adStream.getAtlasFrameParameterSet(0);
  atlasFrameCount = (int32_t)calcTotalAtlasFrameCount(adStream);
  maxTileCount    = 0;
  for (auto& afps : adStream.getAtlasFrameParameterSetList()) {
    auto& asps = adStream.getAtlasSequenceParameterSet(
      afps.getAtlasSequenceParameterSetId());
    auto attributeNominalFrameCount =
      asps.getAsveExtension().getAspsAttributeNominalFrameSizeCount();
    auto tileCount =
      afps.getAtlasFrameTileInformation().getNumTilesInAtlasFrameMinus1() + 1;
    for (int i = 0; i < attributeNominalFrameCount; i++) {
      tileCount += afps.getAfveExtension()
                     .getAtlasFrameTileAttributeInformation()[i]
                     .getNumTilesInAtlasFrameMinus1()
                   + 1;
    }
    maxTileCount = std::max(maxTileCount, tileCount);
  }

  auto     atList    = adStream.getAtlasTileLayerList();  //to avoid const
  auto&    afpsList  = adStream.getAtlasFrameParameterSetList();
  auto&    aspsList  = adStream.getAtlasSequenceParameterSetList();
  uint32_t aspsIndex = 0;
  uint32_t afpsIndex = 0;
  std::vector<imageArea> decodedGeometryTileAreas;
  decompressTileInformation(
    decodedGeometryTileAreas, -1, afpsList[afpsIndex], aspsList[aspsIndex]);
  uint32_t attributeCount =
    aspsList[aspsIndex].getAsveExtension().getAspsAttributeNominalFrameSizeCount();
  std::vector<std::vector<imageArea>> decodedTextureTileAreas(attributeCount);

  auto afps = afpsList[afpsIndex];
  for (int attrIdx = 0; attrIdx < attributeCount; attrIdx++) {
    if (afps.getAfveExtension()
          .getAfveConsistentTilingAccrossAttributesFlag()) {
      auto refIdx = afps.getAfveExtension().getAfveReferenceAttributeIdx();
      if (attrIdx != refIdx) {
        decompressConsistentAttributeTile(
          attrIdx, refIdx, afps, aspsList[aspsIndex]);
      }
    }
    decompressTileInformation(decodedTextureTileAreas[attrIdx],
                              attrIdx,
                              afpsList[afpsIndex],
                              aspsList[aspsIndex]);
    for (int ti = 0; ti < decodedTextureTileAreas[attrIdx].size(); ti++)
      printf("ti[%d]\t%d,%d\t%dx%d\n",
             ti,
             decodedTextureTileAreas[attrIdx][ti].LTx,
             decodedTextureTileAreas[attrIdx][ti].LTy,
             decodedTextureTileAreas[attrIdx][ti].sizeX,
             decodedTextureTileAreas[attrIdx][ti].sizeY);
  }
  //bugfix : decodedPatches's size needs to be calcuated from afps
  decodedTiles_.resize(maxTileCount);
  for (int32_t tileIdx = 0; tileIdx < maxTileCount; tileIdx++) {
    decodedTiles_[tileIdx].resize(atlasFrameCount);
    for (int32_t fi = 0; fi < atlasFrameCount; fi++) {
      decodedTiles_[tileIdx][fi].tileAttributeAreas_.resize(attributeCount);
    }
  }
  aspsSequenceReferenceFrameIndexDiffList_.resize(aspsList.size());
  aspsLodQuantizationParametersList_.resize(aspsList.size());
  uint32_t aspsPos = 0;
  afpsLodQuantizationParametersList_.resize(afpsList.size());
  aspsSequenceReferenceFrameIndexDiffList_.resize(aspsList.size());
  aspsPos = 0;
  for (auto& asps : aspsList) {
    createAspsReferenceLists(
      asps, aspsSequenceReferenceFrameIndexDiffList_[aspsPos]);
    if (asps.getAsveExtension().getAsveQuantizationParametersPresentFlag()) {
      decodeQuantizationParameters(
        aspsLodQuantizationParametersList_[aspsPos],
        0,
        asps.getAsveExtension().getAsveQuantizationParameters(),
        asps.getAsveExtension().getAsveSubdivisionIterationCount() + 1,
        asps.getAsveExtension().getAsveQuantizationParametersPresentFlag(),
        asps,
        afps);
    }
    aspsPos++;
  }
  uint32_t afpsPos = 0;
  afpsLodQuantizationParametersList_.resize(afpsList.size());
  for (auto& afps : afpsList) {
    auto& asps = aspsList[afps.getAtlasSequenceParameterSetId()];
    if (asps.getAsveExtension().getAsveQuantizationParametersPresentFlag()) {
      decodeQuantizationParameters(
        afpsLodQuantizationParametersList_[afpsPos],
        1,
        afps.getAfveExtension().getAfveQuantizationParameters(),
        afps.getAfveExtension().getAfveSubdivisionIterationCount() + 1,
        afps.getAfveExtension().getAfveQuantizationParametersPresentFlag(),
        asps,
        afps);
    }
    afpsPos++;
  }
  aspsSequenceReferenceFrameIndexDiffList_.resize(aspsList.size());
  aspsPos = 0;
  for (auto& asps : aspsList) {
    createAspsReferenceLists(
      asps, aspsSequenceReferenceFrameIndexDiffList_[aspsPos]);
    aspsPos++;
  }
  auto& afti =
    adStream.getAtlasFrameParameterSet(0).getAtlasFrameTileInformation();

  for (int32_t atlIdx = 0; atlIdx < (int32_t)atList.size(); atlIdx++) {
    auto& afps = adStream.getAtlasFrameParameterSet(
      atList[atlIdx].getHeader().getAtlasFrameParameterSetId());
    auto    aspsId = afps.getAtlasSequenceParameterSetId();
    auto&   asps   = adStream.getAtlasSequenceParameterSet(aspsId);
    int32_t frameIndexCalc =
      (int32_t)calculateAFOCval(adStream, atList, atlIdx);
    bool mappingSEIPresent = atList[atlIdx].getSEI().seiIsPresent(
      ATLAS_NAL_PREFIX_ESEI, TILE_SUBMESH_MAPPING);
    if (mappingSEIPresent) {
      auto& sei =
        static_cast<SEITileSubmeshMapping&>(*atList[atlIdx].getSEI().getSei(
          ATLAS_NAL_PREFIX_ESEI, TILE_SUBMESH_MAPPING));
      std::cout << "TILE_SUBMESH_SEI::Tile Submesh Mapping SEI in atlas"
                << std::endl;
      std::cout << "TILE_SUBMESH_SEI::" << sei.getNumberTilesMinus1() + 1
                << " tiles" << std::endl;
      for (int tileIdx = 0; tileIdx < sei.getNumberTilesMinus1() + 1;
           tileIdx++) {
        int numSubmeshes = sei.getNumSubmeshesMinus1(tileIdx) + 1;
        std::cout << "TILE_SUBMESH_SEI::Tile[" << tileIdx
                  << "] ID = " << sei.getTileId(tileIdx);
        if (sei.getTileTypeFlag(tileIdx)) {
          std::cout << " (Attribute Tile)";
        } else {
          std::cout << " (Geometry Tile)";
        }
        std::cout << " Number of submesh = " << numSubmeshes << std::endl;
        for (int submeshIdx = 0; submeshIdx < numSubmeshes; submeshIdx++) {
          std::cout << "TILE_SUBMESH_SEI:: |-Submesh[" << submeshIdx
                    << "] = " << sei.getSubmeshId(tileIdx, submeshIdx)
                    << std::endl;
        }
      }
    }

    createAthReferenceList(
      asps, atList[atlIdx], atList[atlIdx].getHeader().getReferenceList());
    auto tileIndex = 0;
    std::cout << "atList[atlIdx].getHeader().getType() "
              << atList[atlIdx].getHeader().getType() << "/n";
    if (atList[atlIdx].getHeader().getType() == I_TILE
        || atList[atlIdx].getHeader().getType() == P_TILE
        || atList[atlIdx].getHeader().getType() == SKIP_TILE) {
      tileIndex = afps.getAtlasFrameTileInformation().getTileIndex(
        atList[atlIdx].getHeader().getAtlasTileHeaderId());
      decodedTiles_[tileIndex][frameIndexCalc].tileType_ =
        atList[atlIdx].getHeader().getType();
      decodedTiles_[tileIndex][frameIndexCalc].tileId_ =
        atList[atlIdx].getHeader().getAtlasTileHeaderId();
      decodedTiles_[tileIndex][frameIndexCalc].tileAfpsId_ =
        atList[atlIdx].getHeader().getAtlasFrameParameterSetId();
      decodedTiles_[tileIndex][frameIndexCalc].tileAspsId_ = aspsId;
      decodedTiles_[tileIndex][frameIndexCalc].tileGeometryArea_ =
        decodedGeometryTileAreas[tileIndex];
    } else if (atList[atlIdx].getHeader().getType() == I_TILE_ATTR
               || atList[atlIdx].getHeader().getType() == P_TILE_ATTR
               || atList[atlIdx].getHeader().getType() == SKIP_TILE_ATTR)
    {
      tileIndex = afps.getAfveExtension()
                    .getAtlasFrameTileAttributeInformation()[0]
                    .getTileIndexWithOffset(
                      atList[atlIdx].getHeader().getAtlasTileHeaderId());
      decodedTiles_[tileIndex][frameIndexCalc].tileType_ =
        atList[atlIdx].getHeader().getType();
      decodedTiles_[tileIndex][frameIndexCalc].tileId_ =
        atList[atlIdx].getHeader().getAtlasTileHeaderId();
      decodedTiles_[tileIndex][frameIndexCalc].tileAfpsId_ =
        atList[atlIdx].getHeader().getAtlasFrameParameterSetId();
      decodedTiles_[tileIndex][frameIndexCalc].tileAspsId_ = aspsId;
      // TODO: now we have only one atlas attribute nominal frame but here would be
      // mapping of tile to attribute index using tile ID
      //VERIFY_SOFTWARE_LIMITATION(
      //  asps.getAsveExtension().getAsveAttributeNominalFrameCount() > 0);
      auto ti =
        afps.getAfveExtension()
          .getAtlasFrameTileAttributeInformation()[0]
          .getTileIndex(atList[atlIdx].getHeader().getAtlasTileHeaderId());
      decodedTiles_[tileIndex][frameIndexCalc].tileAttributeAreas_[0].sizeX =
        decodedTextureTileAreas[0][ti].sizeX;
      decodedTiles_[tileIndex][frameIndexCalc].tileAttributeAreas_[0].sizeY =
        decodedTextureTileAreas[0][ti].sizeY;
      decodedTiles_[tileIndex][frameIndexCalc].tileAttributeAreas_[0].LTx =
        decodedTextureTileAreas[0][ti].LTx;
      decodedTiles_[tileIndex][frameIndexCalc].tileAttributeAreas_[0].LTy =
        decodedTextureTileAreas[0][ti].LTy;
    } else {
        std::cerr << "Error: Unknown Tile Type\n";
        exit(0);
    }

    decompressAtlasPatch(atlIdx,
                         adStream,
                         frameIndexCalc,
                         tileIndex,
                         atList[atlIdx],
                         decodedTiles_[tileIndex]);
  }
}

size_t
AtlasDataDecoder::calculateAFOCval(AtlasBitstream&               amStream,
                                   std::vector<AtlasTileLayerRbsp>& atglList,
                                   size_t atglOrder) {
  // 8.2.3.1 Atals frame order count derivation process
  if (atglOrder == 0) {
    atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb(0);
    atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal(
      atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb());
    return atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb();
  }

  size_t prevAtlasFrmOrderCntMsb =
    atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntMsb();
  size_t atlasFrmOrderCntMsb = 0;
  auto&  atgh                = atglList[atglOrder].getHeader();
  auto&  afps =
    amStream.getAtlasFrameParameterSet(atgh.getAtlasFrameParameterSetId());
  auto& asps = amStream.getAtlasSequenceParameterSet(
    afps.getAtlasSequenceParameterSetId());

  size_t maxAtlasFrmOrderCntLsb =
    size_t(1) << (asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4);
  size_t afocLsb = atgh.getAtlasFrmOrderCntLsb();
  size_t prevAtlasFrmOrderCntLsb =
    atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntLsb();
  if ((afocLsb < prevAtlasFrmOrderCntLsb)
      && ((prevAtlasFrmOrderCntLsb - afocLsb) >= (maxAtlasFrmOrderCntLsb / 2)))
    atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb + maxAtlasFrmOrderCntLsb;
  else if ((afocLsb > prevAtlasFrmOrderCntLsb)
           && ((afocLsb - prevAtlasFrmOrderCntLsb)
               > (maxAtlasFrmOrderCntLsb / 2)))
    atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb - maxAtlasFrmOrderCntLsb;
  else atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb;

  atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb(atlasFrmOrderCntMsb);
  atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal(atlasFrmOrderCntMsb
                                                         + afocLsb);
#if 0
  printf("atlPos: %zu\t afocVal: %zu\t afocLsb: %zu\t maxCntLsb:(%zu)\t prevCntLsb:%zu atlasFrmOrderCntMsb: %zu\n", atglOrder, atlasFrmOrderCntMsb + afocLsb, afocLsb, maxAtlasFrmOrderCntLsb,
           prevAtlasFrmOrderCntLsb, atlasFrmOrderCntMsb);
#endif
  return atlasFrmOrderCntMsb + afocLsb;
}
size_t
AtlasDataDecoder::calcTotalAtlasFrameCount(AtlasBitstream& adStream) {
  auto&  atlList         = adStream.getAtlasTileLayerList();
  int    prevAspsIndex   = -1;
  size_t totalFrameCount = 0;
  size_t frameCountInASPS =
    0;  //for the case when this atllist refers multiple MSPS not the overall MSPS
  for (size_t i = 0; i < atlList.size(); i++) {
    auto& afps = adStream.getAtlasFrameParameterSet(
      atlList[i].getHeader().getAtlasFrameParameterSetId());
    int aspsIndex = afps.getAtlasSequenceParameterSetId();
    if (prevAspsIndex != aspsIndex) {  //new msps
      totalFrameCount += frameCountInASPS;
      prevAspsIndex    = aspsIndex;
      frameCountInASPS = 0;
    }
    size_t afocVal = calculateAFOCval(adStream, atlList, i);
    atlList[i].getHeader().setFrameIndex(
      (uint32_t)(afocVal + totalFrameCount));
    frameCountInASPS = std::max(frameCountInASPS, afocVal + 1);
  }
  totalFrameCount += frameCountInASPS;  //is it right?

  return totalFrameCount;
}

void
AtlasDataDecoder::decompressConsistentAttributeTile(
  int32_t                              attrIndex,
  int32_t                              refIndex,
  AtlasFrameParameterSetRbsp&          afps,
  const AtlasSequenceParameterSetRbsp& asps) {
  auto& curr =
    afps.getAfveExtension().getAtlasFrameTileAttributeInformation(attrIndex);
  const auto& ref =
    afps.getAfveExtension().getAtlasFrameTileAttributeInformation(refIndex);
  static constexpr int shift = 14;
  auto                 scale = [](auto num, auto den) {
    return ((num << shift) + (den >> 1)) / den;
  };
  auto invscale = [](auto s) { return (s + (1 << (shift - 1))) >> shift; };
  auto div64    = [](auto s) {
    auto ss = s / 64;
    return ss == 0 ? 1 : ss;
  };

  curr.setSingleTileInAtlasFrameFlag(ref.getSingleTileInAtlasFrameFlag());
  if (!ref.getSingleTileInAtlasFrameFlag()) {
    curr.getUniformPartitionSpacingFlag() =
      ref.getUniformPartitionSpacingFlag();
    if (curr.getUniformPartitionSpacingFlag()) {
      curr.setNumPartitionColumnsMinus1(ref.getNumPartitionColumnsMinus1());
      auto widthPartition =
        asps.getAsveExtension().getAsveAttributeFrameWidth()[attrIndex]
        / (ref.getNumPartitionColumnsMinus1() + 1);

      for (size_t i = 0; i < ref.getNumPartitionColumnsMinus1(); i++) {
        curr.setPartitionColumnWidthMinus1(i, div64(widthPartition) - 1);
      }

      curr.setNumPartitionRowsMinus1(ref.getNumPartitionRowsMinus1());
      auto heightPartition =
        asps.getAsveExtension().getAsveAttributeFrameHeight()[attrIndex]
        / (ref.getNumPartitionColumnsMinus1() + 1);
      for (size_t i = 0; i < ref.getNumPartitionRowsMinus1(); i++) {
        curr.setPartitionRowHeightMinus1(i, div64(heightPartition) - 1);
      }
    } else {
      const auto rW =
        scale(asps.getAsveExtension().getAsveAttributeFrameWidth()[attrIndex],
              asps.getAsveExtension().getAsveAttributeFrameWidth()[refIndex]);
      curr.setNumPartitionColumnsMinus1(ref.getNumPartitionColumnsMinus1());
      for (size_t i = 0; i < ref.getNumPartitionColumnsMinus1(); i++) {
        auto partitionWidth =
          (ref.getPartitionColumnWidthMinus1(i) + 1) * 64 * rW;
        curr.setPartitionColumnWidthMinus1(
          i, div64(invscale(partitionWidth)) - 1);
      }

      const auto rH =
        scale(asps.getAsveExtension().getAsveAttributeFrameHeight()[attrIndex],
              asps.getAsveExtension().getAsveAttributeFrameHeight()[refIndex]);
      curr.setNumPartitionRowsMinus1(ref.getNumPartitionRowsMinus1());
      for (size_t i = 0; i < ref.getNumPartitionRowsMinus1(); i++) {
        auto partitionHeight =
          (ref.getPartitionRowHeightMinus1(i) + 1) * 64 * rH;
        curr.setPartitionRowHeightMinus1(i,
                                         div64(invscale(partitionHeight)) - 1);
      }
    }

    curr.getSinglePartitionPerTileFlag() = ref.getSinglePartitionPerTileFlag();
    if (curr.getSinglePartitionPerTileFlag() > 0) {
      curr.setNumTilesInAtlasFrameMinus1(
        (curr.getNumPartitionColumnsMinus1() + 1)
          * (curr.getNumPartitionRowsMinus1() + 1)
        - 1);
      for (uint32_t i = 0; i <= curr.getNumTilesInAtlasFrameMinus1(); i++) {
        curr.setTopLeftPartitionIdx(i, i);
        curr.setBottomRightPartitionColumnOffset(i, 0);
        curr.setBottomRightPartitionRowOffset(i, 0);
      }
    }
    curr.setNumTilesInAtlasFrameMinus1(ref.getNumTilesInAtlasFrameMinus1());
    for (size_t i = 0; i <= ref.getNumTilesInAtlasFrameMinus1(); i++) {
      curr.setTopLeftPartitionIdx(i, ref.getTopLeftPartitionIdx(i));
      curr.setBottomRightPartitionColumnOffset(
        i, ref.getBottomRightPartitionColumnOffset(i));
      curr.setBottomRightPartitionRowOffset(
        i, ref.getBottomRightPartitionRowOffset(i));
    }

  } else {
    curr.setNumTilesInAtlasFrameMinus1(0);
  }
  curr.setSignalledTileIdFlag()         = ref.getSignalledTileIdFlag();
  curr.setSignalledTileIdLengthMinus1() = ref.getSignalledTileIdLengthMinus1();
  curr.setTileIndexToIDMap(ref.getTileIndexToIDMap());
}

bool
AtlasDataDecoder::decodeTextureProjectionInformation(
  const TextureProjectionInformation&  tpi,
  const AtlasSequenceParameterSetRbsp& asps,
  int32_t                              submeshIndex,
  int32_t                              frameIndex,
  AtlasPatch&                            patch) {
  patch.projectionTextcoordFrameUpscale_   = tpi.getFrameUpscaleMinus1() + 1;
  patch.projectionTextcoordFrameDownscale_ = tpi.getFrameDownscale();
  patch.projectionTextcoordFrameScale_ = patch.projectionTextcoordFrameUpscale_ / (double)std::pow(2, patch.projectionTextcoordFrameDownscale_);
  int numCC                                = tpi.getSubpatchCountMinus1() + 1;
  patch.projectionTextcoordSubpatchCountMinus1_ = numCC - 1;
  patch.projectionTextcoordSubpatches_.resize(numCC);
  int  rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
  auto qpTexCoord =
    asps.getAsveExtension().getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
  // copy the projection parameters
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int idxCC = 0; idxCC < numCC; idxCC++) {
    auto& subpatch = patch.projectionTextcoordSubpatches_[idxCC];
    subpatch.projectionTextcoordFaceId_ = tpi.getFaceId2SubpatchIdx(idxCC);
    if (tpi.getSubpatchRawEnableFlag()
        && tpi.getSubpatchRawPresentFlag(idxCC)) {
      subpatch.projectionTextcoordProjectionId_ =
        rawUvId;  //convert flag info to special projection id
      auto sri                 = tpi.getSubpatchRaw(idxCC);
      subpatch.numRawUVMinus1_ = sri.getNumRawUVMinus1();
      subpatch.uCoords_.resize(sri.getNumRawUVMinus1() + 1);
      subpatch.vCoords_.resize(sri.getNumRawUVMinus1() + 1);
      const auto scaleTexCoord  = std::pow(2.0, qpTexCoord) - 1.0;
      const auto iscaleTexCoord = 1.0 / scaleTexCoord;
      for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
        subpatch.uCoords_[i] = sri.getUcoord(i) * iscaleTexCoord;
        subpatch.vCoords_[i] = sri.getVcoord(i) * iscaleTexCoord;
      }
    } else {
      auto si                                    = tpi.getSubpatch(idxCC);
      subpatch.projectionTextcoordProjectionId_  = si.getProjectionId();
      subpatch.projectionTextcoordOrientationId_ = si.getOrientationId();
      int sizeU                                  = si.get2dSizeXMinus1();
      int sizeV                                  = si.get2dSizeYMinus1();
      if (idxCC > 0) {
        sizeU += patch.projectionTextcoordSubpatches_[prevOrthoPatchId]
                   .projectionTextcoord2dSizeXMinus1_;
        sizeV += patch.projectionTextcoordSubpatches_[prevOrthoPatchId]
                   .projectionTextcoord2dSizeYMinus1_;
      }
      subpatch.projectionTextcoord2dPosX_           = si.get2dPosX();
      subpatch.projectionTextcoord2dPosY_           = si.get2dPosY();
      subpatch.projectionTextcoord2dBiasX_          = si.getPosBiasX();
      subpatch.projectionTextcoord2dBiasY_          = si.getPosBiasY();
      subpatch.projectionTextcoord2dSizeXMinus1_    = sizeU;
      subpatch.projectionTextcoord2dSizeYMinus1_    = sizeV;
      subpatch.projectionTextcoordScalePresentFlag_ = si.getScalePresentFlag();
      if (subpatch.projectionTextcoordScalePresentFlag_)
        subpatch.projectionTextcoordSubpatchScale_ = si.getSubpatchScale();
      prevOrthoPatchId = idxCC;
    }
  }
  return 0;
}

bool
AtlasDataDecoder::decodeTextureProjectionInterInformation(
  const TextureProjectionInterInformation& tpii,
  const AtlasSequenceParameterSetRbsp&     asps,
  int32_t                                  submeshIndex,
  int32_t                                  frameIndex,
  AtlasPatch&                                patch,
  AtlasPatch&                                refPatch) {
  patch.projectionTextcoordFrameUpscale_   = tpii.getFrameUpscaleMinus1() + 1;
  patch.projectionTextcoordFrameDownscale_ = tpii.getFrameDownscale();
  patch.projectionTextcoordFrameScale_ = patch.projectionTextcoordFrameUpscale_ / (double)std::pow(2, patch.projectionTextcoordFrameDownscale_);
  int numCC                                = tpii.getSubpatchCountMinus1() + 1;
  patch.projectionTextcoordSubpatchCountMinus1_ = numCC - 1;
  patch.projectionTextcoordSubpatches_.resize(numCC);
  int  rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
  auto qpTexCoord =
    asps.getAsveExtension().getAsveProjectionRawTextcoordBitdepthMinus1() + 1;
  // copy the projection parameters
  //Calculate diff from previous projection subpatch (not from raw subpatch)
  int prevOrthoPatchId = 0;
  for (int idxCC = 0; idxCC < numCC; idxCC++) {
    auto& subpatch = patch.projectionTextcoordSubpatches_[idxCC];
    subpatch.projectionTextcoordFaceId_ = tpii.getFaceId2SubpatchIdx(idxCC);
    if (tpii.getSubpatchInterEnableFlag() && tpii.getUpdateFlag(idxCC)) {
      auto& subpatch = patch.projectionTextcoordSubpatches_[idxCC];
      auto  sid      = tpii.getSubpatchInter(idxCC);
      auto& subpatchRef =
        refPatch
          .projectionTextcoordSubpatches_[idxCC + sid.getSubpatchIdxDiff()];
      subpatch.projectionTextcoordProjectionId_ =
        subpatchRef.projectionTextcoordProjectionId_;
      subpatch.projectionTextcoordOrientationId_ =
        subpatchRef.projectionTextcoordOrientationId_;
      subpatch.projectionTextcoord2dPosX_ =
        subpatchRef.projectionTextcoord2dPosX_ + sid.get2dPosXDelta();
      subpatch.projectionTextcoord2dPosY_ =
        subpatchRef.projectionTextcoord2dPosY_ + sid.get2dPosYDelta();
      subpatch.projectionTextcoord2dBiasX_ =
        subpatchRef.projectionTextcoord2dBiasX_ + sid.getPosBiasXDelta();
      subpatch.projectionTextcoord2dBiasY_ =
        subpatchRef.projectionTextcoord2dBiasY_ + sid.getPosBiasYDelta();
      subpatch.projectionTextcoord2dSizeXMinus1_ =
        subpatchRef.projectionTextcoord2dSizeXMinus1_ + sid.get2dSizeXDelta();
      subpatch.projectionTextcoord2dSizeYMinus1_ =
        subpatchRef.projectionTextcoord2dSizeYMinus1_ + sid.get2dSizeYDelta();
      subpatch.projectionTextcoordScalePresentFlag_ =
        subpatchRef.projectionTextcoordScalePresentFlag_;
      if (subpatch.projectionTextcoordScalePresentFlag_)
        subpatch.projectionTextcoordSubpatchScale_ =
          subpatchRef.projectionTextcoordSubpatchScale_;
    } else if (tpii.getSubpatchRawEnableFlag()
               && tpii.getSubpatchRawPresentFlag(idxCC)) {
      subpatch.projectionTextcoordProjectionId_ =
        rawUvId;  //convert flag info to special projection id
      auto sri                 = tpii.getSubpatchRaw(idxCC);
      subpatch.numRawUVMinus1_ = sri.getNumRawUVMinus1();
      subpatch.uCoords_.resize(sri.getNumRawUVMinus1() + 1);
      subpatch.vCoords_.resize(sri.getNumRawUVMinus1() + 1);
      const auto scaleTexCoord  = std::pow(2.0, qpTexCoord) - 1.0;
      const auto iscaleTexCoord = 1.0 / scaleTexCoord;
      for (uint32_t i = 0; i < sri.getNumRawUVMinus1() + 1; i++) {
        subpatch.uCoords_[i] = sri.getUcoord(i) * iscaleTexCoord;
        subpatch.vCoords_[i] = sri.getVcoord(i) * iscaleTexCoord;
      }
    } else {
      auto si                                    = tpii.getSubpatch(idxCC);
      subpatch.projectionTextcoordProjectionId_  = si.getProjectionId();
      subpatch.projectionTextcoordOrientationId_ = si.getOrientationId();
      int sizeU                                  = si.get2dSizeXMinus1();
      int sizeV                                  = si.get2dSizeYMinus1();
      if (idxCC > 0) {
        sizeU += patch.projectionTextcoordSubpatches_[prevOrthoPatchId]
                   .projectionTextcoord2dSizeXMinus1_;
        sizeV += patch.projectionTextcoordSubpatches_[prevOrthoPatchId]
                   .projectionTextcoord2dSizeYMinus1_;
      }
      subpatch.projectionTextcoord2dPosX_           = si.get2dPosX();
      subpatch.projectionTextcoord2dPosY_           = si.get2dPosY();
      subpatch.projectionTextcoord2dBiasX_          = si.getPosBiasX();
      subpatch.projectionTextcoord2dBiasY_          = si.getPosBiasY();
      subpatch.projectionTextcoord2dSizeXMinus1_    = sizeU;
      subpatch.projectionTextcoord2dSizeYMinus1_    = sizeV;
      subpatch.projectionTextcoordScalePresentFlag_ = si.getScalePresentFlag();
      if (subpatch.projectionTextcoordScalePresentFlag_)
        subpatch.projectionTextcoordSubpatchScale_ = si.getSubpatchScale();
      prevOrthoPatchId = idxCC;
    }
  }
  return 0;
}

bool
AtlasDataDecoder::decodeTextureProjectionMergeInformation(
  const TextureProjectionMergeInformation& tpmi,
  int32_t                                  submeshIndex,
  int32_t                                  frameIndex,
  AtlasPatch&                                patch,
  AtlasPatch&                                refPatch) {
  patch.projectionTextcoordFrameUpscale_ =
    refPatch.projectionTextcoordFrameUpscale_;
  patch.projectionTextcoordFrameDownscale_ =
    refPatch.projectionTextcoordFrameDownscale_;
  patch.projectionTextcoordFrameScale_ = refPatch.projectionTextcoordFrameScale_;
  // copy the projection parameters
  patch.projectionTextcoordSubpatches_ =
    refPatch.projectionTextcoordSubpatches_;
  patch.projectionTextcoordSubpatchCountMinus1_ =
    refPatch.projectionTextcoordSubpatchCountMinus1_;
  // update projection parameters, if indicated
  if (tpmi.getSubpatchMergePresentFlag()) {
    for (int i = 0; i < tpmi.getSubpatchCountMinus1() + 1; i++) {
      auto smi    = tpmi.getSubpatchMerge(i);
      int  subIdx = smi.getSubpatchIdx();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dPosX_ += smi.get2dPosXDelta();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dPosY_ += smi.get2dPosYDelta();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dBiasX_ += smi.getPosBiasXDelta();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dBiasY_ += smi.getPosBiasYDelta();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dSizeXMinus1_ += smi.get2dSizeXDelta();
      patch.projectionTextcoordSubpatches_[subIdx]
        .projectionTextcoord2dSizeYMinus1_ += smi.get2dSizeYDelta();
    }
  }
  return 0;
}