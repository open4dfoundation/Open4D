/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
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

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "baseMeshSequenceParameterSetRbsp.hpp"
#include "baseMeshFrameParameterSetRbsp.hpp"
#include "baseMeshSubmeshLayer.hpp"

namespace basemesh {

// used for syntax handling
class BaseMeshBitstream {
public:
  BaseMeshBitstream() {}
  ~BaseMeshBitstream() {}

  size_t calcMaxSubmeshCount() {
    size_t maxSbmeshCount = 1;
    for (auto& bmfps : baseMeshFrameParameterSet_) {
      maxSbmeshCount = std::max(
        maxSbmeshCount,
        (size_t)(bmfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1()
                 + 1));
    }
    return maxSbmeshCount;
  }

  size_t calcTotalMeshFrameCount() {
    auto&  mslList         = baseMeshSubmeshLayer_;
    int    prevMspsIndex   = -1;
    size_t totalFrameCount = 0;
    size_t frameCountInMSPS =
      0;  //for the case when this atllist refers multiple MSPS not the overall MSPS
    for (size_t i = 0; i < mslList.size(); i++) {
      auto& mfps = baseMeshFrameParameterSet_
        [mslList[i].getSubmeshHeader().getSmhSubmeshFrameParameterSetId()];
      int mspsIndex = mfps.getBfpsMeshSequenceParameterSetId();
      if (prevMspsIndex != mspsIndex) {  //new msps
        totalFrameCount += frameCountInMSPS;
        prevMspsIndex    = mspsIndex;
        frameCountInMSPS = 0;
      }
      size_t mfocVal = calculateBmFocVal(mslList, i);
      mslList[i].getSubmeshHeader().setFrameIndex(
        (uint32_t)(mfocVal + totalFrameCount));
      frameCountInMSPS = std::max(frameCountInMSPS, mfocVal + 1);
    }
    totalFrameCount += frameCountInMSPS;  //is it right?

    return totalFrameCount;
  }

  // BMSPS related functions
  auto& getBaseMeshSequenceParameterSet(size_t setId) {
    return baseMeshSequenceParameterSet_[setId];
  }
  auto& getBaseMeshSequenceParameterSet(size_t setId) const {
    return baseMeshSequenceParameterSet_[setId];
  }
  auto& addBaseMeshSequenceParameterSet() {
    BaseMeshSequenceParameterSetRbsp msps;
    msps.setBmspsSequenceParameterSetId(baseMeshSequenceParameterSet_.size());
    baseMeshSequenceParameterSet_.push_back(msps);
    return baseMeshSequenceParameterSet_.back();
  }
  auto& addBaseMeshSequenceParameterSet(uint8_t setId) {
    BaseMeshSequenceParameterSetRbsp msps;
    //TODO: [PSIDX] what if setId is already occupied?
    msps.setBmspsSequenceParameterSetId(setId);
    if (baseMeshSequenceParameterSet_.size() < setId + 1) {
      baseMeshSequenceParameterSet_.resize(setId + 1);
    }
    baseMeshSequenceParameterSet_[setId] = msps;
    return baseMeshSequenceParameterSet_[setId];
  }
  auto& getBaseMeshSequenceParameterSetList() {
    return baseMeshSequenceParameterSet_;
  }
  const auto& getBaseMeshSequenceParameterSetList() const {
    return baseMeshSequenceParameterSet_;
  }
  // reference list, defined in BMSPS
  void setNumOfRefBaseMeshFrameList(size_t value) {
    refBaseMeshFrameList_.resize(value);
  }
  void setSizeOfRefBaseMeshFrameList(size_t listIndex, size_t listSize) {
    refBaseMeshFrameList_[listIndex].resize(listSize);
  }
  void setRefBaseMeshFrame(size_t listIndex, size_t refIndex, int32_t value) {
    refBaseMeshFrameList_[listIndex][refIndex] = value;
  }
  void setRefBaseMeshFrameList(std::vector<std::vector<int32_t>>& list) {
    size_t listSize = (std::min)(refBaseMeshFrameList_.size(), list.size());
    for (size_t i = 0; i < listSize; i++) {
      size_t refSize =
        (std::min)(refBaseMeshFrameList_[i].size(), list[i].size());
      for (size_t j = 0; j < refSize; j++)
        refBaseMeshFrameList_[i][j] = list[i][j];
    }
  }
  auto getNumOfRefBaseMeshFrameList() { return refBaseMeshFrameList_.size(); }
  auto getSizeOfRefBaseMeshFrameList(size_t listIndex) {
    return refBaseMeshFrameList_[listIndex].size();
  }
  auto getRefBaseMeshFrame(size_t listIndex, size_t refIndex) {
    return refBaseMeshFrameList_[listIndex][refIndex];
  }
  auto& getRefBaseMeshFrameList(size_t listIndex) {
    return refBaseMeshFrameList_[listIndex];
  }
  auto getMaxNumRefBaseMeshFrame(size_t listIndex) {
    return maxNumRefBaseMeshFrame_;
  }

  // BMFPS related functions
  auto& getBaseMeshFrameParameterSet(size_t setId) {
    return baseMeshFrameParameterSet_[setId];
  }
  auto& getBaseMeshFrameParameterSet(size_t setId) const {
    return baseMeshFrameParameterSet_[setId];
  }
  auto& getBaseMeshFrameParameterSetList() {
    return baseMeshFrameParameterSet_;
  }
  auto& getBaseMeshFrameParameterSetList() const {
    return baseMeshFrameParameterSet_;
  }
  auto& addBaseMeshFrameParameterSet() {
    BaseMeshFrameParameterSetRbsp mfps;
    mfps.setBfpsMeshFrameParameterSetId(baseMeshFrameParameterSet_.size());
    baseMeshFrameParameterSet_.push_back(mfps);
    return baseMeshFrameParameterSet_.back();
  }
  //  auto& addBaseMeshFrameParameterSet(BaseMeshFrameParameterSetRbsp& refmfps) {
  //    size_t                        setId = baseMeshFrameParameterSet_.size();
  //    BaseMeshFrameParameterSetRbsp mfps;
  //    mfps.copyFrom(refmfps);
  //    mfps.setBfpsMeshFrameParameterSetId() = setId;
  //    baseMeshFrameParameterSet_.push_back(mfps);
  //    return baseMeshFrameParameterSet_.back();
  //  }
  auto& addBaseMeshFrameParameterSet(uint8_t setId) {
    BaseMeshFrameParameterSetRbsp mfps;
    mfps.setBfpsMeshFrameParameterSetId(setId);
    if (baseMeshFrameParameterSet_.size() < setId + 1) {
      baseMeshFrameParameterSet_.resize(setId + 1);
    }
    baseMeshFrameParameterSet_[setId] = mfps;
    return baseMeshFrameParameterSet_[setId];
  }

  // Mesh data
  auto&       getBaseMeshSubmeshLayerList() { return baseMeshSubmeshLayer_; }
  const auto& getBaseMeshSubmeshLayerList() const {
    return baseMeshSubmeshLayer_;
  }
  BaseMeshSubmeshLayer& getBaseMeshSubmeshLayer(size_t index) {
    return baseMeshSubmeshLayer_[index];
  }
  const BaseMeshSubmeshLayer& getBaseMeshSubmeshLayer(size_t index) const {
    return baseMeshSubmeshLayer_[index];
  }
  auto& addBaseMeshSubmeshLayer() {
    BaseMeshSubmeshLayer bmsl;
    baseMeshSubmeshLayer_.push_back(bmsl);
    return baseMeshSubmeshLayer_.back();
  }

  size_t calculateBmFocVal(std::vector<BaseMeshSubmeshLayer>& smlList,
                          size_t                             smlOrder) {

    if (smlOrder == 0) {
      smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntMsb(0);
      smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntVal(
        smlList[smlOrder].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb());
      return smlList[smlOrder]
        .getSubmeshHeader()
        .getSmhBasemeshFrmOrderCntLsb();
    }

    size_t prevMeshFrmOrderCntMsb =
      smlList[smlOrder - 1].getSubmeshHeader().getSmhBasemeshFrmOrderCntMsb();
    size_t bmFrmOrderCntMsb   = 0;
    auto&  bmfps              = getBaseMeshFrameParameterSetList()
      [smlList[smlOrder]
         .getSubmeshHeader()
         .getSmhSubmeshFrameParameterSetId()];
    auto& bmsps = getBaseMeshSequenceParameterSetList()
      [bmfps.getBfpsMeshSequenceParameterSetId()];

    size_t maxMeshFrmOrderCntLsb =
      size_t(1) << (bmsps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4);
    size_t bmFrmOrderCntLsb =
      smlList[smlOrder].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb();
    size_t prevMeshFrmOrderCntLsb =
      smlList[smlOrder - 1].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb();
    if ((bmFrmOrderCntLsb < prevMeshFrmOrderCntLsb)
        && ((prevMeshFrmOrderCntLsb - bmFrmOrderCntLsb) >= (maxMeshFrmOrderCntLsb / 2)))
      bmFrmOrderCntMsb = prevMeshFrmOrderCntMsb + maxMeshFrmOrderCntLsb;
    else if ((bmFrmOrderCntLsb > prevMeshFrmOrderCntLsb)
             && ((bmFrmOrderCntLsb - prevMeshFrmOrderCntLsb)
                 > (maxMeshFrmOrderCntLsb / 2)))
      bmFrmOrderCntMsb = prevMeshFrmOrderCntMsb - maxMeshFrmOrderCntLsb;
    else bmFrmOrderCntMsb = prevMeshFrmOrderCntMsb;

    smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntMsb(
      bmFrmOrderCntMsb);
    smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntVal(
      bmFrmOrderCntMsb + bmFrmOrderCntLsb);
#if 0
    printf("atlPos: %zu\t afocVal: %zu\t bmFrmOrderCntLsb: %zu\t maxCntLsb:(%zu)\t prevCntLsb:%zu atlasFrmOrderCntMsb: %zu\n", atglOrder, atlasFrmOrderCntMsb + bmFrmOrderCntLsb, bmFrmOrderCntLsb, maxAtlasFrmOrderCntLsb,
             prevAtlasFrmOrderCntLsb, atlasFrmOrderCntMsb);
#endif
    return bmFrmOrderCntMsb + bmFrmOrderCntLsb;
  }

  void setV3CParameterSetId(uint32_t value) { vpsIndex_ = value; }
  void setAtlasId(uint32_t value) { atlasIndex_ = value; }
  void setSizePrecisionBytesMinus1(uint32_t value) {
    assert(value >= 0);
    precisionMinus1_ = value;
  }
  uint32_t       getV3CParameterSetId() { return vpsIndex_; }
  uint32_t       getAtlasId() { return atlasIndex_; }
  const uint32_t getAtlasId() const { return atlasIndex_; }
  uint32_t       getSizePrecisionBytesMinus1() { return precisionMinus1_; }

  void reorderBaseMeshSubmeshLayer() {
    std::unordered_map<int32_t, size_t> frameOrder;
    for (size_t i = 0; i < baseMeshSubmeshLayer_.size(); ++i) {
      const int32_t frame = baseMeshSubmeshLayer_[i].getFrameIndex();
      if (frameOrder.find(frame) == frameOrder.end()) {
        frameOrder[frame] = i;
      }
    }
    std::sort(baseMeshSubmeshLayer_.begin(),
              baseMeshSubmeshLayer_.end(),
              [&frameOrder](BaseMeshSubmeshLayer& a, BaseMeshSubmeshLayer& b) {
                return frameOrder[a.getFrameIndex()]
                         < frameOrder[b.getFrameIndex()]
                       || (a.getFrameIndex() == b.getFrameIndex()
                           && a.getSubmeshIndex() < b.getSubmeshIndex());
              });
  }

private:
  uint32_t                                      vpsIndex_ = 0;
  uint32_t                                      atlasIndex_ = 0;
  uint32_t                                      precisionMinus1_ = 0;
  std::vector<BaseMeshSequenceParameterSetRbsp> baseMeshSequenceParameterSet_;
  std::vector<std::vector<int32_t>>             refBaseMeshFrameList_;
  size_t                                        maxNumRefBaseMeshFrame_ = 0;
  std::vector<BaseMeshFrameParameterSetRbsp>    baseMeshFrameParameterSet_;
  std::vector<BaseMeshSubmeshLayer>             baseMeshSubmeshLayer_;
};

}; 
