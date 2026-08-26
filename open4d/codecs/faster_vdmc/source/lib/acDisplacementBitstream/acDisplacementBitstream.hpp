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

#include "acDisplacementSequenceParameterSetRbsp.hpp"
#include "acDisplacementFrameParameterSetRbsp.hpp"
#include "acDisplacementLayer.hpp"

namespace acdisplacement {

class AcDisplacementBitstream {
public:
  AcDisplacementBitstream() {}
  ~AcDisplacementBitstream() {}

  size_t calcMaxDisplCount() {
    size_t maxDisplCount = 1;
    for (auto& dfps : displFrameParameterSet_) {
      if (!dfps.getDisplInformation().getDiUseSingleDisplFlag())
        maxDisplCount = std::max(
          maxDisplCount,
          (size_t)(dfps.getDisplInformation().getDiNumDisplsMinus2() + 2));
    }
    return maxDisplCount;
  }

  size_t calcTotalDisplFrameCount() {
    auto&  displList       = displacementLayer_;
    int    prevMspsIndex   = -1;
    size_t totalFrameCount = 0;
    size_t frameCountInDSPS =
      0;  //for the case when this atllist refers multiple DSPS not the overall DSPS
    for (size_t i = 0; i < displList.size(); i++) {
      auto& dfps = displFrameParameterSet_
        [displList[i].getDisplHeader().getDisplFrameParameterSetId()];
      int dspsIndex = dfps.getDfpsDisplSequenceParameterSetId();
      if (prevMspsIndex != dspsIndex) {  //new msps
        totalFrameCount += frameCountInDSPS;
        prevMspsIndex    = dspsIndex;
        frameCountInDSPS = 0;
      }
      size_t mfocVal = calculateDFOCval(i);
      displList[i].getDisplHeader().setFrameIndex(
        (uint32_t)(mfocVal + totalFrameCount));
      frameCountInDSPS = std::max(frameCountInDSPS, mfocVal + 1);
    }
    totalFrameCount += frameCountInDSPS;  //is it right?

    return totalFrameCount;
  }

  auto& getDisplSequenceParameterSet(size_t setId) {
    return displSequenceParameterSet_[setId];
  }
  const auto& getDisplSequenceParameterSet(size_t setId) const {
    return displSequenceParameterSet_[setId];
  }
  auto& addDisplSequenceParameterSet() {
    DisplSequenceParameterSetRbsp msps;
    msps.setDspsSequenceParameterSetId(displSequenceParameterSet_.size());
    displSequenceParameterSet_.push_back(msps);
    return displSequenceParameterSet_.back();
  }
  auto& addDisplSequenceParameterSet(uint8_t setId) {
    DisplSequenceParameterSetRbsp msps;
    //TODO: [PSIDX] what if setId is already occupied?
    msps.setDspsSequenceParameterSetId(setId);
    if (displSequenceParameterSet_.size() < setId + 1) {
      displSequenceParameterSet_.resize(setId + 1);
    }
    displSequenceParameterSet_[setId] = msps;
    return displSequenceParameterSet_[setId];
  }
  auto& getDisplSequenceParameterSetList() {
    return displSequenceParameterSet_;
  }
  const auto& getDisplSequenceParameterSetList() const {
    return displSequenceParameterSet_;
  }

  void setNumOfRefDisplFrameList(size_t value) {
    refDisplFrameList_.resize(value);
  }
  void setSizeOfRefDisplFrameList(size_t listIndex, size_t listSize) {
    refDisplFrameList_[listIndex].resize(listSize);
  }
  void setRefDisplFrame(size_t listIndex, size_t refIndex, int32_t value) {
    refDisplFrameList_[listIndex][refIndex] = value;
  }
  void setRefDisplFrameList(std::vector<std::vector<int32_t>>& list) {
    size_t listSize = (std::min)(refDisplFrameList_.size(), list.size());
    for (size_t i = 0; i < listSize; i++) {
      size_t refSize =
        (std::min)(refDisplFrameList_[i].size(), list[i].size());
      for (size_t j = 0; j < refSize; j++)
        refDisplFrameList_[i][j] = list[i][j];
    }
  }
  auto getNumOfRefDisplFrameList() { return refDisplFrameList_.size(); }
  auto getSizeOfRefDisplFrameList(size_t listIndex) {
    return refDisplFrameList_[listIndex].size();
  }
  auto getRefDisplFrame(size_t listIndex, size_t refIndex) {
    return refDisplFrameList_[listIndex][refIndex];
  }
  auto& getRefDisplFrameList(size_t listIndex) {
    return refDisplFrameList_[listIndex];
  }
  auto getMaxNumRefDisplFrame(size_t listIndex) {
    return maxNumRefDisplFrame_;
  }


  auto& getDisplFrameParameterSet(size_t setId) {
    return displFrameParameterSet_[setId];
  }
  auto& getDisplFrameParameterSet(size_t setId) const {
    return displFrameParameterSet_[setId];
  }
  auto& getDisplFrameParameterSetList() { return displFrameParameterSet_; }
  auto& getDisplFrameParameterSetList() const {
    return displFrameParameterSet_;
  }
  auto& addDisplFrameParameterSet() {
    DisplFrameParameterSetRbsp mfps;
    mfps.setDfpsDisplFrameParameterSetId(displFrameParameterSet_.size());
    displFrameParameterSet_.push_back(mfps);
    return displFrameParameterSet_.back();
  }
  //  auto& addDisplFrameParameterSet(DisplFrameParameterSetRbsp& refmfps) {
  //    size_t                        setId = DisplFrameParameterSet_.size();
  //    DisplFrameParameterSetRbsp mfps;
  //    mfps.copyFrom(refmfps);
  //    mfps.setBfpsMeshFrameParameterSetId() = setId;
  //    DisplFrameParameterSet_.push_back(mfps);
  //    return DisplFrameParameterSet_.back();
  //  }
  auto& addDisplFrameParameterSet(uint8_t setId) {
    DisplFrameParameterSetRbsp mfps;
    mfps.setDfpsDisplFrameParameterSetId(setId);
    if (displFrameParameterSet_.size() < setId + 1) {
      displFrameParameterSet_.resize(setId + 1);
    }
    displFrameParameterSet_[setId] = mfps;
    return displFrameParameterSet_[setId];
  }

  auto&       getDisplacementLayerList() { return displacementLayer_; }
  const auto& getDisplacementLayerList() const { return displacementLayer_; }
  DisplacementLayer& getDisplacementLayerList(size_t index) {
    return displacementLayer_[index];
  }
  const DisplacementLayer& getDisplacementLayerList(size_t index) const {
    return displacementLayer_[index];
  }
  auto& addDisplacementLayer() {
    DisplacementLayer bmsl;
    displacementLayer_.push_back(bmsl);
    return displacementLayer_.back();
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

  size_t calculateDFOCval(size_t dlOrder) {
    // 8.2.3.1 Atals frame order count derivation process

    if (dlOrder == 0) {
      displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntMsb(0);
      displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntVal(
        displacementLayer_[dlOrder].getDisplHeader().getDisplFrmOrderCntLsb());
      return displacementLayer_[dlOrder]
        .getDisplHeader()
        .getDisplFrmOrderCntLsb();
    }

    size_t prevDispFrmOrderCntMsb = displacementLayer_[dlOrder - 1]
                                      .getDisplHeader()
                                      .getDhdisplFrmOrderCntMsb();
    size_t dispFrmOrderCntMsb = 0;
    auto&  dfps = getDisplFrameParameterSet(displacementLayer_[dlOrder]
                                             .getDisplHeader()
                                             .getDisplFrameParameterSetId());
    auto&  dsps =
      getDisplSequenceParameterSet(dfps.getDfpsDisplSequenceParameterSetId());

    size_t maxDispFrmOrderCntLsb =
      size_t(1) << (dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4);
    size_t dfocLsb =
      displacementLayer_[dlOrder].getDisplHeader().getDisplFrmOrderCntLsb();
    size_t prevDispFrmOrderCntLsb = displacementLayer_[dlOrder - 1]
                                      .getDisplHeader()
                                      .getDisplFrmOrderCntLsb();
    if ((dfocLsb < prevDispFrmOrderCntLsb)
        && ((prevDispFrmOrderCntMsb - dfocLsb) >= (maxDispFrmOrderCntLsb / 2)))
      dispFrmOrderCntMsb = prevDispFrmOrderCntMsb + maxDispFrmOrderCntLsb;
    else if ((dfocLsb > prevDispFrmOrderCntLsb)
             && ((dfocLsb - prevDispFrmOrderCntLsb)
                 > (maxDispFrmOrderCntLsb / 2)))
      dispFrmOrderCntMsb = prevDispFrmOrderCntMsb - maxDispFrmOrderCntLsb;
    else dispFrmOrderCntMsb = prevDispFrmOrderCntMsb;

    displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntMsb(
      dispFrmOrderCntMsb);
    displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntVal(
      dispFrmOrderCntMsb + dfocLsb);

    return dispFrmOrderCntMsb + dfocLsb;
  }

private:
  uint32_t                                   vpsIndex_        = 0;
  uint32_t                                   atlasIndex_      = 0;
  uint32_t                                   precisionMinus1_ = 0;
  std::vector<DisplSequenceParameterSetRbsp> displSequenceParameterSet_;
  std::vector<std::vector<int32_t>>          refDisplFrameList_;
  size_t                                     maxNumRefDisplFrame_ = 0;
  std::vector<DisplFrameParameterSetRbsp>    displFrameParameterSet_;
  std::vector<DisplacementLayer>             displacementLayer_;
};

};  // namespace vmesh
