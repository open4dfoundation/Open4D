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
#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include "atlasCommon.hpp"
#include "atlasTileHeader.hpp"
#include "atlasTileLayerRbsp.hpp"
#include "atlasSequenceParameterSetRbsp.hpp"
#include "atlasFrameParameterSetRbsp.hpp"
#if defined (CONFORMANCE_LOG)
#include "atlasTileStruct.hpp"
#endif
namespace atlas {

class BitstreamStat;

// used for syntax handling
class AtlasBitstream {
public:
  AtlasBitstream() {}
  ~AtlasBitstream() {}

  size_t calculateAFOCval( std::vector<AtlasTileLayerRbsp>& atglList, size_t atglOrder ) {
    // 8.2.3.1 Atals frame order count derivation process
    if ( atglOrder == 0 ) {
      atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb( 0 );
      atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal( atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb() );
      return atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb();
    }

    size_t prevAtlasFrmOrderCntMsb = atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntMsb();
    size_t atlasFrmOrderCntMsb     = 0;
    auto&  atgh                    = atglList[atglOrder].getHeader();
    auto&  afps                    = getAtlasFrameParameterSet( atgh.getAtlasFrameParameterSetId() );
    auto&  asps                    = getAtlasSequenceParameterSet( afps.getAtlasSequenceParameterSetId() );

    size_t maxAtlasFrmOrderCntLsb  = size_t( 1 ) << ( asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4 );
    size_t afocLsb                 = atgh.getAtlasFrmOrderCntLsb();
    size_t prevAtlasFrmOrderCntLsb = atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntLsb();
    if ( ( afocLsb < prevAtlasFrmOrderCntLsb ) &&
        ( ( prevAtlasFrmOrderCntLsb - afocLsb ) >= ( maxAtlasFrmOrderCntLsb / 2 ) ) )
      atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb + maxAtlasFrmOrderCntLsb;
    else if ( ( afocLsb > prevAtlasFrmOrderCntLsb ) &&
             ( ( afocLsb - prevAtlasFrmOrderCntLsb ) > ( maxAtlasFrmOrderCntLsb / 2 ) ) )
      atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb - maxAtlasFrmOrderCntLsb;
    else
      atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb;

    atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb( atlasFrmOrderCntMsb );
    atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal( atlasFrmOrderCntMsb + afocLsb );
#if 0
    printf("atlPos: %zu\t afocVal: %zu\t afocLsb: %zu\t maxCntLsb:(%zu)\t prevCntLsb:%zu atlasFrmOrderCntMsb: %zu\n", atglOrder, atlasFrmOrderCntMsb + afocLsb, afocLsb, maxAtlasFrmOrderCntLsb,
             prevAtlasFrmOrderCntLsb, atlasFrmOrderCntMsb);
#endif
    return atlasFrmOrderCntMsb + afocLsb;
  }

  size_t calcTotalAtlasFrameCount(){

    int prevAspsIndex=-1;
    size_t totalFrameCount = 0;
    size_t frameCountInASPS=0; //for the case when this atllist refers multiple MSPS not the overall MSPS
    for ( size_t i = 0; i < atlasTileLayer_.size(); i++ ) {
      auto& afps =
        getAtlasFrameParameterSet( atlasTileLayer_[i].getHeader().getAtlasFrameParameterSetId() );
      int aspsIndex = afps.getAtlasSequenceParameterSetId();
      if(prevAspsIndex!=aspsIndex){ //new msps
        totalFrameCount += frameCountInASPS;
        prevAspsIndex = aspsIndex;
        frameCountInASPS = 0;
      }
      size_t afocVal = calculateAFOCval( atlasTileLayer_, i );
      atlasTileLayer_[i].getHeader().setFrameIndex( (uint32_t) (afocVal + totalFrameCount));
      frameCountInASPS = std::max(frameCountInASPS, afocVal+1);
    }
    totalFrameCount+=frameCountInASPS; //is it right?

    return totalFrameCount;
  }

  auto  getOccupancyPrecision() const { return occupancyPrecision_; }
  auto  getLog2PatchQuantizerSizeX() const { return log2PatchQuantizerSizeX_; }
  auto  getLog2PatchQuantizerSizeY() const { return log2PatchQuantizerSizeY_; }
  auto  getEnablePatchSizeQuantization() const {
    return enablePatchSizeQuantization_;
  }
  auto getPrefilterLossyOM() const { return prefilterLossyOM_; }
  auto getOffsetLossyOM() const { return offsetLossyOM_; }
  auto getGeometry3dCoordinatesBitdepth() const {
    return geometry3dCoordinatesBitdepth_;
  }
  auto getSingleLayerMode() const { return singleLayerMode_; }
  auto& getOccupancyPrecision() { return occupancyPrecision_; }
  auto& getLog2PatchQuantizerSizeX() { return log2PatchQuantizerSizeX_; }
  auto& getLog2PatchQuantizerSizeY() { return log2PatchQuantizerSizeY_; }
  auto& getEnablePatchSizeQuantization() {
    return enablePatchSizeQuantization_;
  }
  auto& getPrefilterLossyOM() { return prefilterLossyOM_; }
  auto& getOffsetLossyOM() { return offsetLossyOM_; }
  auto& getGeometry3dCoordinatesBitdepth() {
    return geometry3dCoordinatesBitdepth_;
  }
  auto& getSingleLayerMode() { return singleLayerMode_; }

  // ASPS related functions
  AtlasSequenceParameterSetRbsp& getAtlasSequenceParameterSet(size_t setId) {
    return atlasSequenceParameterSet_[setId];
  }
  const AtlasSequenceParameterSetRbsp&
  getAtlasSequenceParameterSet(size_t setId) const {
    return atlasSequenceParameterSet_[setId];
  }
  const std::vector<AtlasSequenceParameterSetRbsp>&
  getAtlasSequenceParameterSetList() const {
    return atlasSequenceParameterSet_;
  }
  AtlasSequenceParameterSetRbsp& addAtlasSequenceParameterSet() {
    AtlasSequenceParameterSetRbsp asps;
    asps.getAtlasSequenceParameterSetId() = atlasSequenceParameterSet_.size();
    atlasSequenceParameterSet_.push_back(asps);
    return atlasSequenceParameterSet_.back();
  }
  AtlasSequenceParameterSetRbsp& addAtlasSequenceParameterSet(uint8_t setId) {
    AtlasSequenceParameterSetRbsp asps;
    asps.getAtlasSequenceParameterSetId() = setId;
    if (atlasSequenceParameterSet_.size() < setId + 1) {
      atlasSequenceParameterSet_.resize(setId + 1);
    }
    atlasSequenceParameterSet_[setId] = asps;
    return atlasSequenceParameterSet_[setId];
  }
  std::vector<AtlasSequenceParameterSetRbsp>&
  getAtlasSequenceParameterSetList() {
    return atlasSequenceParameterSet_;
  }

  // reference list, defined in ASPS
  void setNumOfRefAtlasFrameList(size_t value) {
    refAtlasFrameList_.resize(value);
  }
  void setSizeOfRefAtlasFrameList(size_t listIndex, size_t listSize) {
    refAtlasFrameList_[listIndex].resize(listSize);
  }
  void setRefAtlasFrame(size_t listIndex, size_t refIndex, int32_t value) {
    refAtlasFrameList_[listIndex][refIndex] = value;
  }
  void setRefAtlasFrameList(std::vector<std::vector<int32_t>>& list) {
    size_t listSize = (std::min)(refAtlasFrameList_.size(), list.size());
    for (size_t i = 0; i < listSize; i++) {
      size_t refSize =
        (std::min)(refAtlasFrameList_[i].size(), list[i].size());
      for (size_t j = 0; j < refSize; j++)
        refAtlasFrameList_[i][j] = list[i][j];
    }
  }
  size_t getNumOfRefAtlasFrameList() { return refAtlasFrameList_.size(); }
  size_t getSizeOfRefAtlasFrameList(size_t listIndex) {
    return refAtlasFrameList_[listIndex].size();
  }
  int32_t getRefAtlasFrame(size_t listIndex, size_t refIndex) {
    return refAtlasFrameList_[listIndex][refIndex];
  }
  std::vector<int32_t>& getRefAtlasFrameList(size_t listIndex) {
    return refAtlasFrameList_[listIndex];
  }
  size_t getNumRefIdxActive(AtlasTileHeader& ath) {
    size_t afpsId          = ath.getAtlasFrameParameterSetId();
    auto&  afps            = getAtlasFrameParameterSet(afpsId);
    size_t numRefIdxActive = 0;
    if (ath.getType() == P_TILE || ath.getType() == SKIP_TILE) {
      if (ath.getNumRefIdxActiveOverrideFlag()) {
        numRefIdxActive = ath.getNumRefIdxActiveMinus1() + 1;
      } else {
        auto& asps =
          getAtlasSequenceParameterSet(afps.getAtlasSequenceParameterSetId());
        auto& refList =
          ath.getRefAtlasFrameListSpsFlag()
            ? asps.getRefListStruct(ath.getRefAtlasFrameListIdx())
            : ath.getRefListStruct();
        numRefIdxActive = static_cast<size_t>((std::min)(
          static_cast<int>(refList.getNumRefEntries()),
          static_cast<int>(afps.getNumRefIdxDefaultActiveMinus1()) + 1));
      }
    }
    return numRefIdxActive;
  }
  size_t getMaxNumRefAtlasFrame(size_t listIndex) {
    return maxNumRefAtlasFrame_;
  }
  void setMaxNumRefAtlasFrame(size_t value) { maxNumRefAtlasFrame_ = value; }

  // AFPS related functions
  AtlasFrameParameterSetRbsp& getAtlasFrameParameterSet(size_t setId) {
    return atlasFrameParameterSet_[setId];
  }
  const AtlasFrameParameterSetRbsp&
  getAtlasFrameParameterSet(size_t setId) const {
    return atlasFrameParameterSet_[setId];
  }
  std::vector<AtlasFrameParameterSetRbsp>& getAtlasFrameParameterSetList() {
    return atlasFrameParameterSet_;
  }
  const std::vector<AtlasFrameParameterSetRbsp>& getAtlasFrameParameterSetList() const {
    return atlasFrameParameterSet_;
  }
  AtlasFrameParameterSetRbsp& addAtlasFrameParameterSet() {
    AtlasFrameParameterSetRbsp afps;
    afps.getAtlasFrameParameterSetId() = atlasFrameParameterSet_.size();
    atlasFrameParameterSet_.push_back(afps);
    return atlasFrameParameterSet_.back();
  }
  AtlasFrameParameterSetRbsp&
  addAtlasFrameParameterSet(AtlasFrameParameterSetRbsp& refAfps) {
    size_t                     setId = atlasFrameParameterSet_.size();
    AtlasFrameParameterSetRbsp afps;
    afps.copyFrom(refAfps);
    afps.getAtlasFrameParameterSetId() = setId;
    atlasFrameParameterSet_.push_back(afps);
    return atlasFrameParameterSet_.back();
  }
  AtlasFrameParameterSetRbsp& addAtlasFrameParameterSet(uint8_t setId) {
    AtlasFrameParameterSetRbsp afps;
    afps.getAtlasFrameParameterSetId() = setId;
    if (atlasFrameParameterSet_.size() < setId + 1) {
      atlasFrameParameterSet_.resize(setId + 1);
    }
    atlasFrameParameterSet_[setId] = afps;
    return atlasFrameParameterSet_[setId];
  }

  // ATGL related functions
  AtlasTileLayerRbsp& addAtlasTileLayer() {
    AtlasTileLayerRbsp atgl;
    atgl.getTileOrder()               = atlasTileLayer_.size();
    atgl.getDataUnit().getTileOrder() = atlasTileLayer_.size();
    atgl.getEncFrameIndex()           = (std::numeric_limits<size_t>::max)();
    atgl.getEncTileIndex()            = (std::numeric_limits<size_t>::max)();
    atlasTileLayer_.push_back(atgl);
    return atlasTileLayer_.back();
  }
  AtlasTileLayerRbsp& addAtlasTileLayer(
    size_t frameIdx,
    size_t
      tileIdx) {  // ajt::how is tileIdx is used, should it also do setTileOrder(tileIdx)?
    AtlasTileLayerRbsp atgl;
    atgl.getEncFrameIndex()       = frameIdx;
    atgl.getEncTileIndex()        = tileIdx;
    atgl.getHeader().setAtlasFrmOrderCntVal(frameIdx);
    atlasTileLayer_.push_back(atgl);
    return atlasTileLayer_.back();
  }
  void traceAtlasTileLayer() {
    printf("traceAtlasTileLayer: atlasTileLayer_.size() = %zu \n",
           atlasTileLayer_.size());
    for (size_t atglIndex = 0; atglIndex < atlasTileLayer_.size();
         atglIndex++) {
      printf("  atgl %3zu: EncFrameIndex = %zu EncTileIndex = %zu \n",
             atglIndex,
             atlasTileLayer_[atglIndex].getEncFrameIndex(),
             atlasTileLayer_[atglIndex].getEncTileIndex());
    }
    fflush(stdout);
  }
  size_t getAtlasTileLayerIndex(size_t frameIndex, size_t tileIndex) {
    for (size_t atglIndex = 0; atglIndex < atlasTileLayer_.size();
         atglIndex++) {
      if (atlasTileLayer_[atglIndex].getEncFrameIndex() == frameIndex
          && atlasTileLayer_[atglIndex].getEncTileIndex() == tileIndex) {
        return atglIndex;
      }
    }
    return 0;
  }
  const std::vector<AtlasTileLayerRbsp>& getAtlasTileLayerList() const {
    return atlasTileLayer_;
  }
  std::vector<AtlasTileLayerRbsp>& getAtlasTileLayerList() {
    return atlasTileLayer_;
  }
  AtlasTileLayerRbsp& getAtlasTileLayer(size_t atglIndex) {
    return atlasTileLayer_[atglIndex];
  }
  AtlasTileLayerRbsp& getAtlasTileLayer(size_t frameIndex, size_t tileIndex) {
    return atlasTileLayer_[getAtlasTileLayerIndex(frameIndex, tileIndex)];
  }
  const AtlasTileLayerRbsp& getAtlasTileLayer(size_t atglIndex) const {
      return atlasTileLayer_[atglIndex];
  }
  size_t getFrameCount(const AtlasSequenceParameterSetRbsp& asps,
                       const AtlasFrameParameterSetRbsp&    afps) const {
    size_t frameCount          = 0;
    size_t atlasFrmOrderCntMsb = 0;
    size_t atlasFrmOrderCntVal = 0;
    for (size_t i = 0; i < atlasTileLayer_.size(); i++) {
      size_t afocVal = calculateAFOCval(
        asps, afps, i, atlasFrmOrderCntMsb, atlasFrmOrderCntVal);
      frameCount = std::max(frameCount, (afocVal + 1));
    }
    return frameCount;
  }

  // 8.2.3.1 Atlas frame order count derivation process
  size_t calculateAFOCval(const AtlasSequenceParameterSetRbsp& asps,
                          const AtlasFrameParameterSetRbsp&    afps,
                          size_t                               atglIndex,
                          size_t& atlasFrmOrderCntMsb,
                          size_t& atlasFrmOrderCntVal) const {
    auto& atgh = atlasTileLayer_[atglIndex].getHeader();
    if (atglIndex == 0) {
      // atlasTileLayer_[atglIndex].setAtlasFrmOrderCntMsb(0);
      // atlasTileLayer_[atglIndex].setAtlasFrmOrderCntVal(
      //   atlh.getAtlasFrmOrderCntLsb());
      return atgh.getAtlasFrmOrderCntLsb();
    }

    size_t prevAtlasFrmOrderCntMsb = atlasFrmOrderCntMsb;
    size_t newAtlasFrmOrderCntMsb  = 0;
    // auto& afps = getAtlasFrameParameterSet(atgh.getAtlasFrameParameterSetId());
    // auto& asps =
    //  getAtlasSequenceParameterSet(afps.getAtlasSequenceParameterSetId());
    size_t maxAtlasFrmOrderCntLsb =
      size_t(1) << (asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4);
    size_t afocLsb = atgh.getAtlasFrmOrderCntLsb();
    size_t prevAtlasFrmOrderCntLsb =
      atlasTileLayer_[atglIndex - 1].getHeader().getAtlasFrmOrderCntLsb();
    if ((afocLsb < prevAtlasFrmOrderCntLsb)
        && ((prevAtlasFrmOrderCntLsb - afocLsb)
            >= (maxAtlasFrmOrderCntLsb / 2))) {
      newAtlasFrmOrderCntMsb =
        prevAtlasFrmOrderCntMsb + maxAtlasFrmOrderCntLsb;
    } else if ((afocLsb > prevAtlasFrmOrderCntLsb)
               && ((afocLsb - prevAtlasFrmOrderCntLsb)
                   > (maxAtlasFrmOrderCntLsb / 2))) {
      newAtlasFrmOrderCntMsb =
        prevAtlasFrmOrderCntMsb - maxAtlasFrmOrderCntLsb;
    } else {
      newAtlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb;
    }
    atlasFrmOrderCntMsb = newAtlasFrmOrderCntMsb;
    atlasFrmOrderCntVal = newAtlasFrmOrderCntMsb + afocLsb;
    return atlasFrmOrderCntMsb + afocLsb;
  }

  void     setV3CParameterSetId(uint32_t value)        { vpsId_ = value; }
  void     setAtlasId(uint32_t value)                  { atlasId_ = value; }
  void     setSizePrecisionBytesMinus1(uint32_t value ){ assert(value>=0); precisionMinus1_=value; }
  uint32_t getV3CParameterSetId()       { return vpsId_; }
  uint32_t getAtlasId()                 { return atlasId_; }
  const uint32_t getAtlasId()           const { return atlasId_; }
  uint32_t getSizePrecisionBytesMinus1(){ return precisionMinus1_; }
  
#if defined (CONFORMANCE_LOG)
  void aspsCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps) {
      uint8_t val = asps.getFrameWidth() & 0xFF;
      stringByte.push_back(val);
      val = (size_t(asps.getFrameWidth()) >> 8) & 0xFF;
      stringByte.push_back(val);
      val = (size_t(asps.getFrameWidth()) >> 16) & 0xFF;
      stringByte.push_back(val);
      val = (size_t(asps.getFrameWidth()) >> 24) & 0xFF;
      stringByte.push_back(val);
      val = asps.getFrameHeight() & 0xFF;
      stringByte.push_back(val);
      val = (asps.getFrameHeight() >> 8) & 0xFF;
      stringByte.push_back(val);
      val = (size_t(asps.getFrameHeight()) >> 16) & 0xFF;
      stringByte.push_back(val);
      val = (size_t(asps.getFrameHeight()) >> 24) & 0xFF;
      stringByte.push_back(val);
      val = asps.getGeometry3dBitdepthMinus1() & 0xFF;
      stringByte.push_back(val);
      val = asps.getGeometry2dBitdepthMinus1() & 0xFF;
      stringByte.push_back(val);
      val = asps.getMapCountMinus1() & 0xFF;
      stringByte.push_back(val);
      val = asps.getMaxNumberProjectionsMinus1() & 0xFF;
      stringByte.push_back(val);
      val = uint8_t(asps.getPatchPrecedenceOrderFlag()) & 0xFF;
      stringByte.push_back(val);
  }

  void deriveQuantizationParameters(
      VdmcQuantizationParameters qp,
      int subdivIterCount,
      int refSubdivIterCount,
      int numComp,
      int refQPMinus49,
      std::vector<std::vector<uint32_t>>& QuantizationParameters,
      std::vector<std::vector<uint32_t>>& refQuantizationParameters,
      int qpIndex) {
      if (qp.getVdmcLodQuantizationFlag() == 0)
      {
          for (int i = 0; i < subdivIterCount + 1; i++) {
              for (int j = 0; j < numComp; j++) {
                  QuantizationParameters[i][j] = qp.getVdmcQuantizationParameters()[j];
              }
          }
      }
      else {
          for (int i = 0; i < subdivIterCount + 1; i++) {
              for (int j = 0; j < numComp; j++) {
                  if (qpIndex == 0) {
                      QuantizationParameters[i][j] =
                          refQPMinus49 + 49 +
                          (1 - 2 * qp.getVdmcLodDeltaQPSign(i, j)) * qp.getVdmcLodDeltaQPValue(i, j);
                  } else {
                      if (subdivIterCount <= refSubdivIterCount) {
                          QuantizationParameters[i][j] =
                              refQuantizationParameters[i][j] +
                              (1 - 2 * qp.getVdmcLodDeltaQPSign(i, j)) * qp.getVdmcLodDeltaQPValue(i, j);
                      } else {
                          QuantizationParameters[i][j] =
                              QuantizationParameters[0][j] +
                              (1 - 2 * qp.getVdmcLodDeltaQPSign(i, j)) * qp.getVdmcLodDeltaQPValue(i, j);
                      }
                  }
              }
          }
      }
  }

  void deriveLiftingTransformParameters(VdmcLiftingTransformParameters ltp, int subdivCount, std::vector<float>& updateWeight, std::vector<float>& predictionWeight) {
      for (int i = 0; i < subdivCount; i++) {
          if (ltp.getSkipUpdateFlag()) {
              updateWeight[i] = 0;
          }
          else {
              for (int l = 0; l < subdivCount; l++) {
                  if ((ltp.getAdaptiveUpdateWeightFlag() == 1) || (l == 0)) {
                      updateWeight[l] =
                          (double)ltp.getLiftingUpdateWeightNumerator()[l]
                          / (double)(ltp.getLiftingUpdateWeightDenominatorMinus1()[l] + 1);
                  }
                  else {
                      updateWeight[l] = updateWeight[0];
                  }
              }
          }
          if (ltp.getAdaptivePredictionWeightFlag()) {
              predictionWeight[i] =
                  (double)ltp.getLiftingPredictionWeightNumerator()[i]
                  / (double)(ltp.getLiftingPredictionWeightDenominatorMinus1()[i] + 1);
          } else {
              predictionWeight[i] = 1.0;
          }
      }
  }

  void aspsApplicationByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps) {
      auto& asve = asps.getAsveExtension();
      uint8_t val = asve.getAsveSubdivisionIterationCount() & 0xFF;
      stringByte.push_back(val);
      for (int i = 0; i < asve.getAsveSubdivisionIterationCount(); i++) {
          val = asve.getAsveSubdivisionMethod()[i] & 0xFF;
          stringByte.push_back(val);
      }
      val = asve.getAsveSubdivisionMinEdgeLength() & 0xFF;
      stringByte.push_back(val);
      val = (asve.getAsveSubdivisionMinEdgeLength() >> 8) & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsve1DDisplacementFlag() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsveInterpolateSubdividedNormalsFlag() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsveQuantizationParametersPresentFlag() & 0xFF;
      stringByte.push_back(val);
      if (asve.getAsveQuantizationParametersPresentFlag()) {
          auto& qp = asve.getAsveQuantizationParameters();
          int subdivIterCount = asve.getAsveSubdivisionIterationCount();
          int numComp = asve.getAspsDispComponents();
          int refQPMinus49 = asve.getAsveDisplacementReferenceQPMinus49();
          std::vector<std::vector<uint32_t>> refQuantizationParameters;
          std::vector<std::vector<uint32_t>> QuantizationParameters;
          QuantizationParameters.resize(subdivIterCount + 1);
          for (int i = 0; i < subdivIterCount + 1; i++) {
              QuantizationParameters[i].resize(numComp);
          }
          deriveQuantizationParameters(qp, subdivIterCount, subdivIterCount, numComp,
              refQPMinus49, QuantizationParameters, refQuantizationParameters, 0);
          for (int j = 0; j < numComp; j++) {
              for (int i = 0; i < asve.getAsveSubdivisionIterationCount(); i++) {
                  val = QuantizationParameters[i][j] & 0xFF;
                  stringByte.push_back(val);
              }
              val = qp.getVdmcLog2LodInverseScale()[j] & 0xFF;
              stringByte.push_back(val);
          }
          val = (uint32_t)qp.getVdmcDirectQuantizationEnabledFlag() & 0xFF;
          stringByte.push_back(val);
          if (!qp.getVdmcDirectQuantizationEnabledFlag()) {
              val = qp.getVdmcBitDepthOffset() & 0xFF;
              stringByte.push_back(val);
              val = (qp.getVdmcBitDepthOffset() >> 8) & 0xFF;
              stringByte.push_back(val);
          }
      }
      if (asve.getAsveSubdivisionIterationCount() != 0) {
          val = asve.getAsveTransformMethod() & 0xFF;
          stringByte.push_back(val);
      }
      val = asve.getAsveLiftingOffsetPresentFlag() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsveDirectionalLiftingPresentFlag() & 0xFF;
      stringByte.push_back(val);
      if (asve.getAsveSubdivisionIterationCount() != 0 && asve.getAsveTransformMethod() == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING ) {
          auto& ltp = asve.getAspsExtLtpDisplacement(); 
          std::vector<float> predWeight;
          std::vector<float> updateWeight;
          predWeight.resize(asve.getAsveSubdivisionIterationCount(), 0);
          updateWeight.resize(asve.getAsveSubdivisionIterationCount(), 0);
          deriveLiftingTransformParameters(ltp, asve.getAsveSubdivisionIterationCount(), updateWeight, predWeight);
          val = ltp.getValenceUpdateWeightFlag() & 0xFF;
          stringByte.push_back(val);
          for (int i = 0; i < asve.getAsveSubdivisionIterationCount(); i++) {
              uint32_t buffer;
              float value = predWeight[i];
              memcpy(&buffer, &value, sizeof(float));
              val = buffer & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 24) & 0xFF;
              stringByte.push_back(val); 
              value = updateWeight[i];
              memcpy(&buffer, &value, sizeof(float));
              val = buffer & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 24) & 0xFF;
              stringByte.push_back(val);
          }
      }
      val = asve.getAsveAttributeInformationPresentFlag() & 0xFF;
      stringByte.push_back(val);
      if (asve.getAsveAttributeInformationPresentFlag()) {
          val = asve.getAsveAttributeFrameSizeCount() & 0xFF;
          stringByte.push_back(val);
          for (int attrIdx = 0; attrIdx < asve.getAspsAttributeNominalFrameSizeCount(); attrIdx++) {
              val = asve.getAsveAttributeFrameWidth()[attrIdx] & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameWidth()[attrIdx]>>8) & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameWidth()[attrIdx] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameWidth()[attrIdx] >> 24) & 0xFF;
              stringByte.push_back(val);
              val = asve.getAsveAttributeFrameHeight()[attrIdx] & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameHeight()[attrIdx] >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameHeight()[attrIdx] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (asve.getAsveAttributeFrameHeight()[attrIdx] >> 24) & 0xFF;
              stringByte.push_back(val);
              val = asve.getAsveAttributeSubtextureEnabledFlag()[attrIdx] & 0xFF;
              stringByte.push_back(val);
          }
      }
      val = asve.getAsveDisplacementIdPresentFlag() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsveLodPatchesEnableFlag() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsvePackingMethod() & 0xFF;
      stringByte.push_back(val);
      val = asve.getAsveProjectionTexcoordEnableFlag() & 0xFF;
      stringByte.push_back(val);
      if (asve.getAsveProjectionTexcoordEnableFlag()) {
          val = asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag() & 0xFF;
          stringByte.push_back(val);
          if (asve.getAsveProjectionTexcoordMappingAttributeIndexPresentFlag()) {
              val = asve.getAsveProjectionTexcoordMappingAttributeIndex() & 0xFF;
              stringByte.push_back(val);
          }
          val = asve.getAsveProjectionTexcoordOutputBitdepthMinus1() & 0xFF;
          stringByte.push_back(val);
          val = asve.getAsveProjectionTexcoordBboxBiasEnableFlag() & 0xFF;
          stringByte.push_back(val);
          val = asve.getAsveProjectionTexCoordUpscaleFactorMinus1() & 0xFF;
          stringByte.push_back(val);
          val = (asve.getAsveProjectionTexCoordUpscaleFactorMinus1() >> 8) & 0xFF;
          stringByte.push_back(val);
          val = (asve.getAsveProjectionTexCoordUpscaleFactorMinus1() >> 16) & 0xFF;
          stringByte.push_back(val);
          val = (asve.getAsveProjectionTexCoordUpscaleFactorMinus1() >> 24) & 0xFF;
          stringByte.push_back(val);
          val = (asve.getAsveProjectionTexCoordUpscaleFactorMinus1() >> 32) & 0xFF;
          stringByte.push_back(val);
          val = asve.getAsveProjectionTexcoordLog2DownscaleFactor() & 0xFF;
          stringByte.push_back(val);
          val = asve.getAsveProjectionRawTextcoordPresentFlag() & 0xFF;
          stringByte.push_back(val);
          if (asve.getAsveProjectionRawTextcoordPresentFlag()) {
              val = asve.getAsveProjectionRawTextcoordBitdepthMinus1() & 0xFF;
              stringByte.push_back(val);
          }
      }
  }

  void afpsCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AtlasFrameParameterSetRbsp& afps) {
      auto    afti = afps.getAtlasFrameTileInformation();
      uint8_t val = afti.getNumTilesInAtlasFrameMinus1() & 0xFF;
      stringByte.push_back(val);
      std::vector<size_t> hashAuxTileHeight;
      size_t              prevAuxTileOffset = 0;
      if (asps.getAuxiliaryVideoEnabledFlag()) {
          hashAuxTileHeight.resize(afti.getNumTilesInAtlasFrameMinus1() + 1, 0);
          for (size_t ti = 0; ti <= afti.getNumTilesInAtlasFrameMinus1(); ti++) {
              hashAuxTileHeight[ti] = afti.getAuxiliaryVideoTileRowHeight(ti) * 64;
          }
      }
      else {
          hashAuxTileHeight.resize(afti.getNumTilesInAtlasFrameMinus1() + 1, 0);
      }

      for (uint32_t i = 0; i < afti.getNumTilesInAtlasFrameMinus1() + 1; i++) {
          size_t topLeftColumn = afti.getTopLeftPartitionIdx(i) % (afti.getNumPartitionColumnsMinus1() + 1);
          size_t topLeftRow = afti.getTopLeftPartitionIdx(i) / (afti.getNumPartitionColumnsMinus1() + 1);
          size_t bottomRightColumn = topLeftColumn + afti.getBottomRightPartitionColumnOffset(i);
          size_t bottomRightRow = topLeftRow + afti.getBottomRightPartitionRowOffset(i);
          size_t tileWidth = 0;
          size_t tileHeight = 0;
          size_t tileOffsetX = afti.getPartitionPosX()[topLeftColumn];
          size_t tileOffsetY = afti.getPartitionPosY()[topLeftRow];
          for (int j = topLeftColumn; j <= bottomRightColumn; j++) {
              tileWidth += afti.getPartitionWidth()[j];
          }
          for (int j = topLeftRow; j <= bottomRightRow; j++) { tileHeight += afti.getPartitionHeight()[j]; }
          size_t auxTileHeight = hashAuxTileHeight[i];
          size_t auxTileOffset = prevAuxTileOffset + auxTileHeight;
          prevAuxTileOffset = auxTileOffset;
          val = tileOffsetX & 0xFF;
          stringByte.push_back(val);
          val = (tileOffsetX >> 8) & 0xFF;
          stringByte.push_back(val);
          val = tileOffsetY & 0xFF;
          stringByte.push_back(val);
          val = (tileOffsetY >> 8) & 0xFF;
          stringByte.push_back(val);
          val = tileWidth & 0xFF;
          stringByte.push_back(val);
          val = (tileWidth >> 8) & 0xFF;
          stringByte.push_back(val);
          val = tileHeight & 0xFF;
          stringByte.push_back(val);
          val = (tileHeight >> 8) & 0xFF;
          stringByte.push_back(val);
          val = auxTileOffset & 0xFF;
          stringByte.push_back(val);
          val = (auxTileOffset >> 8) & 0xFF;
          stringByte.push_back(val);
          val = auxTileHeight & 0xFF;
          stringByte.push_back(val);
          val = (auxTileHeight >> 8) & 0xFF;
          stringByte.push_back(val);
          if (afps.getAtlasFrameTileInformation().getSignalledTileIdFlag()) {
              val = afps.getAtlasFrameTileInformation().getTileId(i) & 0xFF;  //
              stringByte.push_back(val);
              val = (afps.getAtlasFrameTileInformation().getTileId(i) >> 8) & 0xFF;
              stringByte.push_back(val);
          }
          else {
              val = i & 0xFF;
              stringByte.push_back(val);
              val = (i >> 8) & 0xFF;
              stringByte.push_back(val);
          }
      }
  }

  void attributeTileInformationByteString(std::vector<uint8_t>& stringByte,
      AtlasFrameTileInformation& afti) {
      uint8_t val = afti.getTileCount() & 0xFF;
      stringByte.push_back(val);
      for (int i = 0; i < afti.getTileCount(); i++) {
          val = afti.getTileId(i) & 0xFF;
          stringByte.push_back(val);
          val = (afti.getTileId(i) >> 8) & 0xFF;
          stringByte.push_back(val);
      }
  }

  void atlasFrameMeshInformationByteString(std::vector<uint8_t>& stringByte,
      AtlasFrameMeshInformation& afmi) {
      uint8_t val = (afmi.getNumSubmeshesInAtlasFrameMinus1() + 1) & 0xFF;
      stringByte.push_back(val);
      for (int i = 0; i < afmi.getNumSubmeshesInAtlasFrameMinus1() + 1; i++) {
          val = afmi.getSubmeshIds()[i] & 0xFF;
          stringByte.push_back(val);
          val = (afmi.getSubmeshIds()[i] >> 8) & 0xFF;
          stringByte.push_back(val);
      }
  }

  void afpsApplicationByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AtlasFrameParameterSetRbsp& afps) {
      auto& asve = asps.getAsveExtension();
      auto& afve = afps.getAfveExtension();
      uint8_t val = afve.getAfveSubdivisionIterationCount() & 0xFF;
      stringByte.push_back(val);
      for (int i = 0; i < afve.getAfveSubdivisionIterationCount(); i++) {
          val = afve.getAfveSubdivisionMethod()[i] & 0xFF;
          stringByte.push_back(val);
      }
      val = afve.getAfveSubdivisionMinEdgeLength() & 0xFF;
      stringByte.push_back(val);
      val = (afve.getAfveSubdivisionMinEdgeLength() >> 8) & 0xFF;
      stringByte.push_back(val);
      if (afve.getAfveQuantizationParametersPresentFlag()) {
          auto& qp = afve.getAfveQuantizationParameters();
          auto& qpAsps = asve.getAsveQuantizationParameters();
          int subdivIterCount = afve.getAfveSubdivisionIterationCount();
          int refSubdivIterCount = asve.getAsveSubdivisionIterationCount();
          int numComp = asve.getAspsDispComponents();
          int refQPMinus49 = asve.getAsveDisplacementReferenceQPMinus49();
          std::vector<std::vector<uint32_t>> QuantizationParameters;
          QuantizationParameters.resize(subdivIterCount + 1);
          for (int i = 0; i < subdivIterCount + 1; i++) {
              QuantizationParameters[i].resize(numComp);
          }
          std::vector<std::vector<uint32_t>> refQuantizationParameters;
          refQuantizationParameters.resize(refSubdivIterCount + 1);
          for (int i = 0; i < refSubdivIterCount + 1; i++) {
              refQuantizationParameters[i].resize(numComp);
          }
          // deriving QP for ASPS
          deriveQuantizationParameters(qp, refSubdivIterCount, refSubdivIterCount, numComp,
              refQPMinus49, refQuantizationParameters, refQuantizationParameters, 0);
          // deriving QP for AFPS
          deriveQuantizationParameters(qp, subdivIterCount, subdivIterCount, numComp,
              refQPMinus49, QuantizationParameters, refQuantizationParameters, 1);
          for (int j = 0; j < numComp; j++) {
              for (int i = 0; i < afve.getAfveSubdivisionIterationCount(); i++) {
                  val = QuantizationParameters[i][j] & 0xFF;
                  stringByte.push_back(val);
              }
              val = qp.getVdmcLog2LodInverseScale()[j] & 0xFF;
              stringByte.push_back(val);
          }
          val = (uint32_t)qp.getVdmcDirectQuantizationEnabledFlag() & 0xFF;
          stringByte.push_back(val);
          if (!qp.getVdmcDirectQuantizationEnabledFlag()) {
              val = qp.getVdmcBitDepthOffset() & 0xFF;
              stringByte.push_back(val);
              val = (qp.getVdmcBitDepthOffset() >> 8) & 0xFF;
              stringByte.push_back(val);
          }
      }
      val = afve.getAfveTransformMethod() & 0xFF;
      stringByte.push_back(val);
      if (afve.getAfveTransformMethod() == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING && afve.getAfveTransformMethodPresentFlag()) {
          auto& ltp = afve.getAfpsLtpDisplacement();
          std::vector<float> predWeight;
          std::vector<float> updateWeight;
          predWeight.resize(afve.getAfveSubdivisionIterationCount(), 0);
          updateWeight.resize(afve.getAfveSubdivisionIterationCount(), 0);
          deriveLiftingTransformParameters(ltp, afve.getAfveSubdivisionIterationCount(), updateWeight, predWeight);
          val = ltp.getValenceUpdateWeightFlag() & 0xFF;
          stringByte.push_back(val);
          for (int i = 0; i < asve.getAsveSubdivisionIterationCount(); i++) {
              uint32_t buffer;
              float value = predWeight[i];
              memcpy(&buffer, &value, sizeof(float));
              val = buffer & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 24) & 0xFF;
              stringByte.push_back(val);
              value = updateWeight[i];
              memcpy(&buffer, &value, sizeof(float));
              val = buffer & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (buffer >> 24) & 0xFF;
              stringByte.push_back(val);
          }
      }
      if (asve.getAsveAttributeInformationPresentFlag()) {
          auto& afati = afve.getAtlasFrameTileAttributeInformation();
          val = afve.getAfveConsistentTilingAccrossAttributesFlag() & 0xFF;
          stringByte.push_back(val);
          if (afve.getAfveConsistentTilingAccrossAttributesFlag()) {
              val = afve.getAfveReferenceAttributeIdx() & 0xFF;
              stringByte.push_back(val);
              attributeTileInformationByteString(stringByte, afati[afve.getAfveReferenceAttributeIdx()]);
          } else {
              for (int i = 0; i < afati.size(); i++) {
                  attributeTileInformationByteString(stringByte, afati[i]);
              }
          }
      }
      auto& afmi = afve.getAtlasFrameMeshInformation();
      atlasFrameMeshInformationByteString(stringByte, afmi);
      if (asve.getAsveProjectionTexcoordEnableFlag()) {
          for (int i = 0; i < afmi.getNumSubmeshesInAtlasFrameMinus1() + 1; i++) {
              val = afve.getProjectionTextcoordPresentFlag(i) & 0xFF;
              stringByte.push_back(val);
              if (afve.getProjectionTextcoordPresentFlag(i)) {
                  val = afve.getProjectionTextcoordWidth(i) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordWidth(i) >> 8) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordWidth(i) >> 16) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordWidth(i) >> 24) & 0xFF;
                  stringByte.push_back(val);
                  val = afve.getProjectionTextcoordHeight(i) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordHeight(i) >> 8) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordHeight(i) >> 16) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordHeight(i) >> 24) & 0xFF;
                  stringByte.push_back(val);
                  val = afve.getProjectionTextcoordGutter(i) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordGutter(i) >> 8) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordGutter(i) >> 16) & 0xFF;
                  stringByte.push_back(val);
                  val = (afve.getProjectionTextcoordGutter(i) >> 24) & 0xFF;
                  stringByte.push_back(val);
              }
          }
      }
  }

  void atlasMeshpatchCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AspsVdmcExtension& asve,
      AfpsVdmcExtension& afve,
      AtlasPatch& atlasMeshpatch, bool isGeo, int attrIdx = 0) {
      // ID
      uint8_t val = atlasMeshpatch.submeshId_ & 0xFF;
      stringByte.push_back(val);
      val = (atlasMeshpatch.submeshId_ >> 8) & 0xFF;
      stringByte.push_back(val);
      if (isGeo) {
          if (asve.getAsveDisplacementIdPresentFlag()) {
              val = atlasMeshpatch.displId_ & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.displId_ >> 8) & 0xFF;
              stringByte.push_back(val);
          }
          else {
              val = atlasMeshpatch.geometryPatchArea.LTx & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.LTx >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.LTy & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.LTy >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.sizeX & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.sizeX >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.sizeY & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.sizeY >> 8) & 0xFF;
              stringByte.push_back(val);
          }
          val = atlasMeshpatch.lodIdx_ & 0xFF;
          stringByte.push_back(val);
          if (atlasMeshpatch.lodIdx_ == 0) {
              // subdivision
              val = atlasMeshpatch.subdivIteration & 0xFF;
              stringByte.push_back(val);
              for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                  val = atlasMeshpatch.subdivMethod[i] & 0xFF;
                  stringByte.push_back(val);
              }
              val = atlasMeshpatch.subdivMinEdgeLength & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.subdivMinEdgeLength >> 8) & 0xFF;
              stringByte.push_back(val);
              // quantization
              val = atlasMeshpatch.IQ_skip & 0xFF;
              stringByte.push_back(val);
              if (!atlasMeshpatch.IQ_skip) {
                  val = atlasMeshpatch.quantizationParameters.lodQuantFlag & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.quantizationParameters.BitDepthOffset & 0xFF;
                  stringByte.push_back(val);
                  val = (atlasMeshpatch.quantizationParameters.BitDepthOffset >> 8) & 0xFF;
                  stringByte.push_back(val);
                  for (int k = 0; k < asve.getAspsDispComponents(); k++) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration + 1; i++) {
                          val = (uint32_t)atlasMeshpatch.quantizationParameters.lodQp[i][k] & 0xFF;
                          stringByte.push_back(val);
                      }
                      val = atlasMeshpatch.quantizationParameters.log2InverseScale[k] & 0xFF;
                      stringByte.push_back(val);
                  }
                  val = atlasMeshpatch.quantizationParameters.directQuantFlag & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.iqOffsetFlag & 0xFF;
                  stringByte.push_back(val);
                  if (atlasMeshpatch.iqOffsetFlag) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration + 1; i++) {
                          for (int j = 0; j < asve.getAspsDispComponents(); j++) {
                              for (int k = 0; k < 3; k++) {
                                  val = atlasMeshpatch.iqOffsets[i][j][k][0] & 0xFF;
                                  stringByte.push_back(val);
                                  val = atlasMeshpatch.iqOffsets[i][j][k][1] & 0xFF;
                                  stringByte.push_back(val);
                                  val = atlasMeshpatch.iqOffsets[i][j][k][2] & 0xFF;
                                  stringByte.push_back(val);
                              }
                          }
                      }
                  }
              }
              // coordinate system
              val = (uint8_t)atlasMeshpatch.displacementCoordinateSystem & 0xFF;
              stringByte.push_back(val);
              // transform
              val = atlasMeshpatch.transformMethod & 0xFF;
              stringByte.push_back(val);
              if (atlasMeshpatch.transformMethod == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING) {
                  if (asve.getAsveLiftingOffsetPresentFlag()) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                          val = atlasMeshpatch.transformParameters.liftingOffsetValuesNumerator_[i] & 0xFF;
                          stringByte.push_back(val);
                          val = atlasMeshpatch.transformParameters.liftingOffsetValuesDenominator_[i] & 0xFF;
                          stringByte.push_back(val);
                      }
                  }
                  if (asve.getAsveDirectionalLiftingPresentFlag()) {
                      val = atlasMeshpatch.dirLiftParams.MeanNumerator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.MeanDenominator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.StdNumerator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.StdDenominator_ & 0xFF;
                      stringByte.push_back(val);
                  }
                  val = atlasMeshpatch.transformParameters.skipUpdateFlag_ & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.transformParameters.valenceUpdateWeightFlag_ & 0xFF;
                  stringByte.push_back(val);
                  for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                      uint32_t buffer;
                      float weight = atlasMeshpatch.transformParameters.updateWeight_[i];
                      std::memcpy(&buffer, &weight, sizeof(float));
                      val = buffer & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 24) & 0xFF;
                      stringByte.push_back(val);
                      weight = atlasMeshpatch.transformParameters.predWeight_[i];
                      std::memcpy(&buffer, &weight, sizeof(float));
                      val = buffer & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 24) & 0xFF;
                      stringByte.push_back(val);
                  }
              }
          }
          // vertex count
          int numLodInPatch = (!asve.getAsveLodPatchesEnableFlag() || asve.getAsveDisplacementIdPresentFlag()) ?
              (atlasMeshpatch.subdivIteration + 1) : 1;
          for (int i = 0; i < numLodInPatch; i++) {
              val = atlasMeshpatch.blockCount[i] & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 24) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.lastPosInBlock[i] & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 24) & 0xFF;
          }
          // orthoAtlas
          int smIdx = afve.getAtlasFrameMeshInformation()._submeshIDToIndex[atlasMeshpatch.submeshId_];
          if (atlasMeshpatch.lodIdx_ == 0 && afve.getProjectionTextcoordPresentFlag(smIdx)) {
              val = (atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1) & 0xFF;
              stringByte.push_back(val);
              val = ((atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1) >> 8) & 0xFF;
              stringByte.push_back(val);
              for (int spIdx = 0; spIdx < atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1; spIdx++) {
                  auto& subpatch = atlasMeshpatch.projectionTextcoordSubpatches_[spIdx];
                  // load subpatch variables
                  val = subpatch.projectionTextcoordFaceId_ & 0xFF;
                  stringByte.push_back(val);
                  val = (subpatch.projectionTextcoordFaceId_ >> 8) & 0xFF;
                  stringByte.push_back(val);
                  int rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
                  if (subpatch.projectionTextcoordProjectionId_ == rawUvId) {
                      val = (subpatch.numRawUVMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.numRawUVMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      if ((subpatch.numRawUVMinus1_ + 1) > 0) {
                          for (int i = 0; i < subpatch.numRawUVMinus1_ + 1; i++) {
                              int32_t uVal = (subpatch.uCoords_[i] * asve.getAsveProjectionRawTextcoordBitdepthMinus1());
                              int32_t vVal = (subpatch.vCoords_[i] * asve.getAsveProjectionRawTextcoordBitdepthMinus1());
                              val = uVal & 0xFF;
                              stringByte.push_back(val);
                              val = (uVal >> 8) & 0xFF;
                              stringByte.push_back(val);
                              val = vVal & 0xFF;
                              stringByte.push_back(val);
                              val = (vVal >> 8) & 0xFF;
                              stringByte.push_back(val);
                          }
                      }
                  }
                  else {
                      int numRawUV = 0;
                      val = (numRawUV) & 0xFF;
                      stringByte.push_back(val);
                      val = ((numRawUV) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoordProjectionId_ & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoordOrientationId_ & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoord2dPosX_ & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dPosX_ >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoord2dPosY_ & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dPosY_ >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dSizeXMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.projectionTextcoord2dSizeXMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dSizeYMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.projectionTextcoord2dSizeYMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      if (asve.getAsveProjectionTexcoordBboxBiasEnableFlag()) {
                          val = subpatch.projectionTextcoord2dBiasX_ & 0xFF;
                          stringByte.push_back(val);
                          val = (subpatch.projectionTextcoord2dBiasX_ >> 8) & 0xFF;
                          stringByte.push_back(val);
                          val = subpatch.projectionTextcoord2dBiasY_ & 0xFF;
                          stringByte.push_back(val);
                          val = (subpatch.projectionTextcoord2dBiasY_ >> 8) & 0xFF;
                          stringByte.push_back(val);
                      }
                      float scale = 1.0;
                      if (subpatch.projectionTextcoordScalePresentFlag_) {
                          scale = atlasMeshpatch.projectionTextcoordFrameScale_;
                          float patchScale = asve.getAspsProjectionTexcoordUpscaleFactor() / asve.getAspsProjectionTexcoordDownscaleFactor();
                          for (int i = 0; i < subpatch.projectionTextcoordSubpatchScale_; i++)
                              scale *= patchScale;
                      }
                      val = (uint32_t)scale & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 24) & 0xFF;
                      stringByte.push_back(val);
                  }
              }
          }
      }
      else
      {
          val = atlasMeshpatch.attributePatchArea[attrIdx].LTx & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].LTx >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].LTy & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].LTy >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].sizeX & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].sizeX >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].sizeY & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].sizeY >> 8) & 0xFF;
          stringByte.push_back(val);
      }
  };

  void atlasMeshpatchCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AspsVdmcExtension& asve,
      AfpsVdmcExtension& afve,
      VMCPatch& atlasMeshpatch, bool isGeo, int attrIdx = 0) {
      // ID
      uint8_t val = atlasMeshpatch.submeshId_ & 0xFF;
      stringByte.push_back(val);
      val = (atlasMeshpatch.submeshId_ >> 8 ) & 0xFF;
      stringByte.push_back(val);
      if (isGeo) {
          if (asve.getAsveDisplacementIdPresentFlag()) {
              val = atlasMeshpatch.displId_ & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.displId_ >> 8 ) & 0xFF;
              stringByte.push_back(val);
          } else {
              val = atlasMeshpatch.geometryPatchArea.LTx & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.LTx >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.LTy & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.LTy >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.sizeX & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.sizeX >> 8) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.geometryPatchArea.sizeY & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.geometryPatchArea.sizeY >> 8) & 0xFF;
              stringByte.push_back(val);
          }
          val = atlasMeshpatch.lodIdx_ & 0xFF;
          stringByte.push_back(val);
          if (atlasMeshpatch.lodIdx_ == 0) {
              // subdivision
              val = atlasMeshpatch.subdivIteration & 0xFF;
              stringByte.push_back(val);
              for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                  val = atlasMeshpatch.subdivMethod[i] & 0xFF;
                  stringByte.push_back(val);
              }
              val = atlasMeshpatch.subdivMinEdgeLength& 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.subdivMinEdgeLength >> 8 ) & 0xFF; 
              stringByte.push_back(val);
              // quantization
              val = atlasMeshpatch.IQ_skip & 0xFF;
              stringByte.push_back(val);
              if (!atlasMeshpatch.IQ_skip) {
                  val = atlasMeshpatch.quantizationParameters.lodQuantFlag & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.quantizationParameters.BitDepthOffset & 0xFF;
                  stringByte.push_back(val);
                  val = (atlasMeshpatch.quantizationParameters.BitDepthOffset >> 8) & 0xFF;
                  stringByte.push_back(val);
                  for (int k = 0; k < asve.getAspsDispComponents(); k++) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration + 1; i++) {
                          val = (uint32_t)atlasMeshpatch.quantizationParameters.lodQp[i][k] & 0xFF;
                          stringByte.push_back(val);
                      }
                      val = atlasMeshpatch.quantizationParameters.log2InverseScale[k] & 0xFF;
                      stringByte.push_back(val);
                  }
                  val = atlasMeshpatch.quantizationParameters.directQuantFlag & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.iqOffsetFlag & 0xFF;
                  stringByte.push_back(val);
                  if (atlasMeshpatch.iqOffsetFlag) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration + 1; i++) {
                          for (int j = 0; j < asve.getAspsDispComponents(); j++) {
                              for (int k = 0; k < 3; k++) {
                                  val = atlasMeshpatch.iqOffsets[i][j][k][0] & 0xFF;
                                  stringByte.push_back(val);
                                  val = atlasMeshpatch.iqOffsets[i][j][k][1] & 0xFF;
                                  stringByte.push_back(val);
                                  val = atlasMeshpatch.iqOffsets[i][j][k][2] & 0xFF;
                                  stringByte.push_back(val);
                              }
                          }
                      }
                  }
              }
              // coordinate system
              val = (uint8_t)atlasMeshpatch.displacementCoordinateSystem & 0xFF;
              stringByte.push_back(val);
              // transform
              val = atlasMeshpatch.transformMethod & 0xFF;
              stringByte.push_back(val);
              if (atlasMeshpatch.transformMethod == (uint8_t)vmesh::TransformMethod::LINEAR_LIFTING) {
                  if (asve.getAsveLiftingOffsetPresentFlag()) {
                      for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                          val = atlasMeshpatch.transformParameters.liftingOffsetValuesNumerator_[i] & 0xFF;
                          stringByte.push_back(val);
                          val = atlasMeshpatch.transformParameters.liftingOffsetValuesDenominator_[i] & 0xFF;
                          stringByte.push_back(val);
                      }
                  }
                  if (asve.getAsveDirectionalLiftingPresentFlag()) {
                      val = atlasMeshpatch.dirLiftParams.MeanNumerator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.MeanDenominator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.StdNumerator_ & 0xFF;
                      stringByte.push_back(val);
                      val = atlasMeshpatch.dirLiftParams.StdDenominator_ & 0xFF;
                      stringByte.push_back(val);
                  }
                  val = atlasMeshpatch.transformParameters.skipUpdateFlag_ & 0xFF;
                  stringByte.push_back(val);
                  val = atlasMeshpatch.transformParameters.valenceUpdateWeightFlag_ & 0xFF;
                  stringByte.push_back(val);
                  for (int i = 0; i < atlasMeshpatch.subdivIteration; i++) {
                      uint32_t buffer;
                      float weight = atlasMeshpatch.transformParameters.updateWeight_[i];
                      std::memcpy(&buffer, &weight, sizeof(float));
                      val = buffer& 0xFF; 
                      stringByte.push_back(val);
                      val = (buffer >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 24) & 0xFF;
                      stringByte.push_back(val);
                      weight = atlasMeshpatch.transformParameters.predWeight_[i];
                      std::memcpy(&buffer, &weight, sizeof(float));
                      val = buffer & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 8 ) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = (buffer >> 24) & 0xFF;
                      stringByte.push_back(val);
                  }
              }
          }
          // vertex count
          int numLodInPatch = (!asve.getAsveLodPatchesEnableFlag() || asve.getAsveDisplacementIdPresentFlag() ) ? 
              (atlasMeshpatch.subdivIteration + 1) : 1;
          for (int i = 0; i < numLodInPatch; i++) {
              val = atlasMeshpatch.blockCount[i] & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.blockCount[i] >> 24) & 0xFF;
              stringByte.push_back(val);
              val = atlasMeshpatch.lastPosInBlock[i] & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 8) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 16) & 0xFF;
              stringByte.push_back(val);
              val = (atlasMeshpatch.lastPosInBlock[i] >> 24) & 0xFF;
          }
          // orthoAtlas
          int smIdx = afve.getAtlasFrameMeshInformation()._submeshIDToIndex[atlasMeshpatch.submeshId_];
          if (atlasMeshpatch.lodIdx_ == 0 && afve.getProjectionTextcoordPresentFlag(smIdx)) {
              val = (atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1) & 0xFF;
              stringByte.push_back(val);
              val = ((atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1) >> 8) & 0xFF;
              stringByte.push_back(val);
              for (int spIdx = 0; spIdx < atlasMeshpatch.projectionTextcoordSubpatchCountMinus1_ + 1; spIdx++) {
                  auto& subpatch = atlasMeshpatch.projectionTextcoordSubpatches_[spIdx];
                  // load subpatch variables
                  val = subpatch.projectionTextcoordFaceId_ & 0xFF;
                  stringByte.push_back(val);
                  val = (subpatch.projectionTextcoordFaceId_ >> 8) & 0xFF;
                  stringByte.push_back(val);
                  int rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
                  if (subpatch.projectionTextcoordProjectionId_ == rawUvId) {
                      val = (subpatch.numRawUVMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.numRawUVMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      if ((subpatch.numRawUVMinus1_ + 1) > 0) {
                          for (int i = 0; i < subpatch.numRawUVMinus1_ + 1; i++) {
                              int32_t uVal = (subpatch.uCoords_[i] * asve.getAsveProjectionRawTextcoordBitdepthMinus1());
                              int32_t vVal = (subpatch.vCoords_[i] * asve.getAsveProjectionRawTextcoordBitdepthMinus1());
                              val = uVal & 0xFF;
                              stringByte.push_back(val);
                              val = (uVal >> 8) & 0xFF;
                              stringByte.push_back(val);
                              val = vVal & 0xFF;
                              stringByte.push_back(val);
                              val = (vVal >> 8) & 0xFF;
                              stringByte.push_back(val);
                          }
                      }
                  } else {
                      int numRawUV = 0;
                      val = (numRawUV) & 0xFF;
                      stringByte.push_back(val);
                      val = ((numRawUV) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoordProjectionId_ & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoordOrientationId_ & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoord2dPosX_ & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dPosX_ >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = subpatch.projectionTextcoord2dPosY_ & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dPosY_ >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dSizeXMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.projectionTextcoord2dSizeXMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = (subpatch.projectionTextcoord2dSizeYMinus1_ + 1) & 0xFF;
                      stringByte.push_back(val);
                      val = ((subpatch.projectionTextcoord2dSizeYMinus1_ + 1) >> 8) & 0xFF;
                      stringByte.push_back(val);
                      if (asve.getAsveProjectionTexcoordBboxBiasEnableFlag()) {
                          val = subpatch.projectionTextcoord2dBiasX_ & 0xFF;
                          stringByte.push_back(val);
                          val = (subpatch.projectionTextcoord2dBiasX_ >> 8) & 0xFF;
                          stringByte.push_back(val);
                          val = subpatch.projectionTextcoord2dBiasY_ & 0xFF;
                          stringByte.push_back(val);
                          val = (subpatch.projectionTextcoord2dBiasY_ >> 8) & 0xFF;
                          stringByte.push_back(val);
                      }
                      float scale = 1.0;
                      if (subpatch.projectionTextcoordScalePresentFlag_) {
                          scale = atlasMeshpatch.projectionTextcoordFrameScale_;
                          float patchScale = asve.getAspsProjectionTexcoordUpscaleFactor() / asve.getAspsProjectionTexcoordDownscaleFactor();
                          for (int i = 0; i < subpatch.projectionTextcoordSubpatchScale_; i++)
                              scale *= patchScale;
                      }
                      val = (uint32_t)scale & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 8) & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 16) & 0xFF;
                      stringByte.push_back(val);
                      val = ((uint32_t)scale >> 24) & 0xFF;
                      stringByte.push_back(val);
                  }
              }
          }
      }
      else
      {
          val = atlasMeshpatch.attributePatchArea[attrIdx].LTx & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].LTx >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].LTy & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].LTy >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].sizeX & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].sizeX >> 8) & 0xFF;
          stringByte.push_back(val);
          val = atlasMeshpatch.attributePatchArea[attrIdx].sizeY & 0xFF;
          stringByte.push_back(val);
          val = (atlasMeshpatch.attributePatchArea[attrIdx].sizeY>> 8) & 0xFF;
          stringByte.push_back(val);
      }
  };

  void tileMeshpatchCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AspsVdmcExtension& asve,
      AfpsVdmcExtension& afve,
      AtlasPatch& tileMeshpatch,
      AtlasTileHeader ath) {
      // tile type
      uint8_t val = ath.getType() & 0xFF;
      stringByte.push_back(val);

      if ((ath.getType() == SKIP_TILE) || (ath.getType() == SKIP_TILE_ATTR)) {
          // do nothing
      }
      else if ((ath.getType() == I_TILE_ATTR) || (ath.getType() == P_TILE_ATTR)) {
          auto& afati = afve.getAtlasFrameTileAttributeInformation();
          int attrIdx = 0;
          for (int i = 0; i < afati.size(); i++) {
              for (int j = 0; j < afati[i].getTileCount(); j++) {
                  if (ath.getAtlasTileHeaderId() == afati[i].getTileId(j))
                  {
                      attrIdx = i;
                      break;
                  }
              }
          }
          atlasMeshpatchCommonByteString(stringByte, asps, asve, afve, tileMeshpatch, false, attrIdx);
      }
      else {
          atlasMeshpatchCommonByteString(stringByte, asps, asve, afve, tileMeshpatch, true);
      }
  }

  void tileMeshpatchCommonByteString(std::vector<uint8_t>& stringByte,
      AtlasSequenceParameterSetRbsp& asps,
      AspsVdmcExtension& asve,
      AfpsVdmcExtension& afve,
      VMCPatch& tileMeshpatch,
      AtlasTileHeader ath) {
      // tile type
      uint8_t val = ath.getType() & 0xFF;
      stringByte.push_back(val);

      if ((ath.getType() == SKIP_TILE) || (ath.getType() == SKIP_TILE_ATTR)) {
        // do nothing
      } else if ((ath.getType() == I_TILE_ATTR) || (ath.getType() == P_TILE_ATTR)) {
          auto& afati = afve.getAtlasFrameTileAttributeInformation();
          int attrIdx = 0;
          for (int i = 0; i < afati.size(); i++) {
              for (int j = 0; j < afati[i].getTileCount(); j++) {
                  if (ath.getAtlasTileHeaderId() == afati[i].getTileId(j))
                  {
                      attrIdx = i;
                      break;
                  }
              }
          }
          atlasMeshpatchCommonByteString(stringByte, asps, asve, afve, tileMeshpatch, false, attrIdx);
      }
      else {
          atlasMeshpatchCommonByteString(stringByte, asps, asve, afve, tileMeshpatch, true);
      }
  }

#endif
private:
  uint32_t                                   vpsId_               = 0;
  uint32_t                                   atlasId_             = 0;
  uint32_t                                   precisionMinus1_= 0;
  size_t                                     maxNumRefAtlasFrame_ = 4;
  std::vector<AtlasSequenceParameterSetRbsp> atlasSequenceParameterSet_;
  std::vector<std::vector<int32_t>>          refAtlasFrameList_;
  std::vector<AtlasFrameParameterSetRbsp>    atlasFrameParameterSet_;
  std::vector<AtlasTileLayerRbsp>            atlasTileLayer_;
  std::vector<int32_t>                       refAFOCList_;
  
  uint8_t                           occupancyPrecision_            = 0;
  uint8_t                           log2PatchQuantizerSizeX_       = 0;
  uint8_t                           log2PatchQuantizerSizeY_       = 0;
  bool                              enablePatchSizeQuantization_   = 0;
  bool                              prefilterLossyOM_              = 0;
  size_t                            offsetLossyOM_                 = 0;
  size_t                            geometry3dCoordinatesBitdepth_ = 0;
  bool                              singleLayerMode_               = 0;
};

};  // namespace vmesh
