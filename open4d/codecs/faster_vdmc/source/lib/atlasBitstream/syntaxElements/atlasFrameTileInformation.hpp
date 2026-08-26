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

#include <map>

namespace atlas {

// 8.3.6.2.2 Atlas frame tile information syntax
class AtlasFrameTileInformation {
public:
  AtlasFrameTileInformation() {
    partitionColumnWidthMinus1_.resize(1, 0);
    partitionRowHeightMinus1_.resize(1, 0);
    topLeftPartitionIdx_.resize(1, 0);
    bottomRightPartitionColumnOffset_.resize(1, 0);
    bottomRightPartitionRowOffset_.resize(1, 0);
    auxiliaryVideoTileRowHeight_.clear();
  };
  ~AtlasFrameTileInformation() {
    partitionColumnWidthMinus1_.clear();
    partitionRowHeightMinus1_.clear();
    topLeftPartitionIdx_.clear();
    bottomRightPartitionColumnOffset_.clear();
    bottomRightPartitionRowOffset_.clear();
    auxiliaryVideoTileRowHeight_.clear();
    tileIndexToID_.clear();
  };

  AtlasFrameTileInformation&
       operator=(const AtlasFrameTileInformation&) = default;
  bool operator==(const AtlasFrameTileInformation& other) const {
    if (singleTileInAtlasFrameFlag_ != other.singleTileInAtlasFrameFlag_) {
      return false;
    } else if (!singleTileInAtlasFrameFlag_) {
      if (uniformPartitionSpacingFlag_ != other.uniformPartitionSpacingFlag_) {
        return false;
      }
      if (uniformPartitionSpacingFlag_) {
        if (partitionColumnWidthMinus1_[0]
            != other.partitionColumnWidthMinus1_[0]) {
          return false;
        }
        if (partitionRowHeightMinus1_[0]
            != other.partitionRowHeightMinus1_[0]) {
          return false;
        }
      } else {
        if (numPartitionColumnsMinus1_ != other.numPartitionColumnsMinus1_) {
          return false;
        }
        if (numPartitionRowsMinus1_ != other.numPartitionRowsMinus1_) {
          return false;
        }

        for (size_t i = 0; i <= numPartitionColumnsMinus1_; i++) {
          if (partitionColumnWidthMinus1_[i]
              != other.partitionColumnWidthMinus1_[i]) {
            return false;
          }
        }
        for (size_t i = 0; i <= numPartitionRowsMinus1_; i++) {
          if (partitionRowHeightMinus1_[i]
              != other.partitionRowHeightMinus1_[i]) {
            return false;
          }
        }
      }
      if (singlePartitionPerTileFlag_ != other.singlePartitionPerTileFlag_) {
        return false;
      }
      if (numTilesInAtlasFrameMinus1_ != other.numTilesInAtlasFrameMinus1_) {
        return false;
      }
      if (!singleTileInAtlasFrameFlag_) {
        for (uint32_t i = 0; i < numTilesInAtlasFrameMinus1_ + 1; i++) {
          if (topLeftPartitionIdx_[i] != other.topLeftPartitionIdx_[i]) {
            return false;
          }
          if (bottomRightPartitionColumnOffset_[i]
              != other.bottomRightPartitionColumnOffset_[i]) {
            return false;
          }
          if (bottomRightPartitionRowOffset_[i]
              != other.bottomRightPartitionRowOffset_[i]) {
            return false;
          }
        }
      }
      if (auxiliaryVideoTileRowHeight_.size()
          != other.auxiliaryVideoTileRowHeight_.size()) {
        return false;
      }
      if (auxiliaryVideoTileRowHeight_.size() != 0) {
        if (auxiliaryVideoTileRowWidthMinus1_
            != other.auxiliaryVideoTileRowWidthMinus1_) {
          return false;
        }
        for (size_t ti = 0; ti < (numTilesInAtlasFrameMinus1_ + 1); ti++) {
          if (auxiliaryVideoTileRowHeight_[ti]
              != other.auxiliaryVideoTileRowHeight_[ti]) {
            return false;
          }
        }
      }
    }
    return true;
  }
  auto getSingleTileInAtlasFrameFlag() const {
    return singleTileInAtlasFrameFlag_;
  }
  auto getUniformPartitionSpacingFlag() const {
    return uniformPartitionSpacingFlag_;
  }
  auto getNumPartitionColumnsMinus1() const {
    return numPartitionColumnsMinus1_;
  }
  auto getNumPartitionRowsMinus1() const { return numPartitionRowsMinus1_; }
  auto getSinglePartitionPerTileFlag() const {
    return singlePartitionPerTileFlag_;
  }
  auto getNumTilesInAtlasFrameMinus1() const {
    return numTilesInAtlasFrameMinus1_;
  }
  auto getSignalledTileIdFlag() const { return signalledTileIdFlag_; }
  auto getSignalledTileIdLengthMinus1() const {
    return signalledTileIdLengthMinus1_;
  }
  auto getPartitionColumnWidthMinus1(size_t index) const {
    return partitionColumnWidthMinus1_[index];
  }
  auto getPartitionRowHeightMinus1(size_t index) const {
    return partitionRowHeightMinus1_[index];
  }
  auto getTopLeftPartitionIdx(size_t index) const {
    return topLeftPartitionIdx_[index];
  }
  auto getBottomRightPartitionColumnOffset(size_t index) const {
    return bottomRightPartitionColumnOffset_[index];
  }
  auto getBottomRightPartitionRowOffset(size_t index) const {
    return bottomRightPartitionRowOffset_[index];
  }
  auto getAuxiliaryVideoTileRowWidthMinus1() const {
    return auxiliaryVideoTileRowWidthMinus1_;
  }
  auto getAuxiliaryVideoTileRowHeight(size_t index) const {
    return auxiliaryVideoTileRowHeight_.size() == 0
             ? 0
             : auxiliaryVideoTileRowHeight_[index];
  }

  auto getSingleTileInAtlasFrameFlag() { return singleTileInAtlasFrameFlag_; }
  void setSingleTileInAtlasFrameFlag(bool value) {
    singleTileInAtlasFrameFlag_=value;
    if(value) setNumTilesInAtlasFrameMinus1(0);
  }
  auto& getUniformPartitionSpacingFlag() {
    return uniformPartitionSpacingFlag_;
  }
  auto getNumPartitionColumnsMinus1() { return numPartitionColumnsMinus1_; }
  auto getNumPartitionRowsMinus1() { return numPartitionRowsMinus1_; }
  void setNumPartitionColumnsMinus1(uint32_t value) { numPartitionColumnsMinus1_ = value; partitionColumnWidthMinus1_.resize(value); }
  void setNumPartitionRowsMinus1   (uint32_t value) { numPartitionRowsMinus1_ = value; partitionRowHeightMinus1_.resize(value); }

  auto& getSinglePartitionPerTileFlag() { return singlePartitionPerTileFlag_; }
  auto getNumTilesInAtlasFrameMinus1() { return numTilesInAtlasFrameMinus1_; }
  void setNumTilesInAtlasFrameMinus1(uint32_t value) {
    numTilesInAtlasFrameMinus1_=value;
    topLeftPartitionIdx_.resize(value+1);
    bottomRightPartitionColumnOffset_.resize(value+1);
    bottomRightPartitionRowOffset_.resize(value+1);
    //tileId_.resize(value+1);
  }
  auto getSignalledTileIdFlag() { return signalledTileIdFlag_; }
  auto getSignalledTileIdLengthMinus1() {
    return signalledTileIdLengthMinus1_;
  }
  auto& setSignalledTileIdFlag() { return signalledTileIdFlag_; }
  auto& setSignalledTileIdLengthMinus1() {
    return signalledTileIdLengthMinus1_;
  }

  auto& getAuxiliaryVideoTileRowWidthMinus1() {
    return auxiliaryVideoTileRowWidthMinus1_;
  }

  void allocate(){
    topLeftPartitionIdx_.resize(numTilesInAtlasFrameMinus1_ + 1);
    bottomRightPartitionColumnOffset_.resize(numTilesInAtlasFrameMinus1_ + 1);
    bottomRightPartitionRowOffset_.resize(numTilesInAtlasFrameMinus1_ + 1);
    //tileId_.resize(numTilesInAtlasFrameMinus1_ + 1);
  }
  void setPartitionColumnWidthMinus1(size_t index, uint32_t value) {
    if (index >= partitionColumnWidthMinus1_.size())
      partitionColumnWidthMinus1_.resize(index+1);
    partitionColumnWidthMinus1_[index] = value;
  }
  void setPartitionRowHeightMinus1(size_t index, uint32_t value) {
    if (index >= partitionRowHeightMinus1_.size())
      partitionRowHeightMinus1_.resize(index+1);
    partitionRowHeightMinus1_[index] = value;
  }
  void setTopLeftPartitionIdx(size_t index, uint32_t value) {
     topLeftPartitionIdx_[index] = value;
  }
  void setBottomRightPartitionColumnOffset(size_t index, uint32_t value) {
     bottomRightPartitionColumnOffset_[index]= value;
  }
  void setBottomRightPartitionRowOffset(size_t index, uint32_t value) {
    bottomRightPartitionRowOffset_[index] = value;
  }
//  void setTileId(size_t index, uint32_t value) {
//    tileId_[index] = value;
//  }
  auto getPartitionColumnWidthMinus1(size_t index) {
    return partitionColumnWidthMinus1_[index];
  }
  auto getPartitionRowHeightMinus1(size_t index) {
    return partitionRowHeightMinus1_[index];
  }
  auto getTopLeftPartitionIdx(size_t index) {
    return topLeftPartitionIdx_[index];
  }
  auto getBottomRightPartitionColumnOffset(size_t index) {
    return bottomRightPartitionColumnOffset_[index];
  }
  auto getBottomRightPartitionRowOffset(size_t index) {
    return bottomRightPartitionRowOffset_[index];
  }
//  auto getTileId(size_t index) {
//    return tileId_[index];
//  }
  auto& getAuxiliaryVideoTileRowHeight(size_t index) {
    if (index <= auxiliaryVideoTileRowHeight_.size())
      auxiliaryVideoTileRowHeight_.resize(
        auxiliaryVideoTileRowHeight_.size() + 1, 0);
    return auxiliaryVideoTileRowHeight_[index];
  }

  std::vector<size_t>& getPartitionWidth() { return colWidth_; }
  std::vector<size_t>& getPartitionHeight() { return rowHeight_; }
  std::vector<size_t>& getPartitionPosX() { return partitionPosX_; }
  std::vector<size_t>& getPartitionPosY() { return partitionPosY_; }
  size_t getPartitionPosX(size_t index) { return partitionPosX_[index]; }
  size_t getPartitionPosY(size_t index) { return partitionPosY_[index]; }
  size_t getPartitionWidth(size_t index) { return colWidth_[index]; }
  size_t getPartitionHeight(size_t index) { return rowHeight_[index]; }

  void setTileIndexOffset(uint32_t value) {
    startTileIndexOffset_ = value;
  }

  void setTileId(uint32_t tileID) {
    bool tileIdIntTileIndexToID = false;
    for (auto & it : tileIndexToID_) {
      if (it.second == tileID) { tileIdIntTileIndexToID = true; }
    }

    if (!tileIdIntTileIndexToID) {
      tileIndexToID_[lastTileIndex_] = tileID;
      lastTileIndex_++;
    } else {
      std::cerr << "Error: tile ID " << tileID
                << " already in tileIndexToID_\n";
    }
  }

  uint32_t getTileId(uint32_t tileIndex) const {
    uint32_t tileID             = 0;
    bool     tileIndexIntTileIndexToID = false;
    for (auto& it : tileIndexToID_) {
      if (it.first == tileIndex) {
        tileID              = it.second;
        tileIndexIntTileIndexToID = true;
      }
    }

    if (!tileIndexIntTileIndexToID) {
      std::cerr << "Error: tile index " << tileIndex
                << " not in tileIndexToID_\n";
    }
    return tileID;
  }

  uint32_t getTileIndex(uint32_t tileID) const {
    uint32_t tileIndex              = 0;
    bool     tileIdIntTileIndexToID = false;
    for (auto& it : tileIndexToID_) {
      if (it.second == tileID) {
        tileIndex              = it.first;
        tileIdIntTileIndexToID = true;
      }
    }

    if (!tileIdIntTileIndexToID) {
      std::cerr << "Error: tile ID " << tileIndex
                << " not in tileIndexToID_\n";
    }
    return tileIndex;
  }

  uint32_t getTileIndexWithOffset(uint32_t tileID) const {
    uint32_t tileIndex = getTileIndex( tileID);
    return tileIndex + this->startTileIndexOffset_;
  }

  uint32_t getMaxTileId() {
    uint32_t maxTileID                 = 0;
    bool     tileIndexIntTileIndexToID = false;
    for (auto& it : tileIndexToID_) {
      if (it.second > maxTileID) { maxTileID = it.second; }
    }

    return maxTileID;
  }

  uint32_t getMaxTileIndex() {
    uint32_t maxTileIndex                = 0;
    bool     tileIndexIntTileIndexToID = false;
    for (auto& it : tileIndexToID_) {
      if (it.first > maxTileIndex) { maxTileIndex = it.second; }
    }

    return maxTileIndex;
  }

  std::map<uint32_t, uint32_t> getTileIndexToIDMap() const
  {
    return tileIndexToID_;
  }

  void setTileIndexToIDMap(std::map<uint32_t, uint32_t> map) {
    tileIndexToID_ = std::move(map);
  }

  uint32_t getTileCount(){
    return tileIndexToID_.size();
  }

private:
  bool                  singleTileInAtlasFrameFlag_  = false;
  bool                  uniformPartitionSpacingFlag_ = true;
  uint32_t              numPartitionColumnsMinus1_   = 0;
  uint32_t              numPartitionRowsMinus1_      = 0;
  uint32_t              singlePartitionPerTileFlag_  = 0;
  uint32_t              numTilesInAtlasFrameMinus1_  = 0;
  bool                  signalledTileIdFlag_         = false;
  uint32_t              signalledTileIdLengthMinus1_ = 0;
  std::vector<uint32_t> partitionColumnWidthMinus1_;
  std::vector<uint32_t> partitionRowHeightMinus1_;
  std::vector<uint32_t> topLeftPartitionIdx_;
  std::vector<uint32_t> bottomRightPartitionColumnOffset_;
  std::vector<uint32_t> bottomRightPartitionRowOffset_;
  //std::vector<uint32_t> tileId_;
  uint32_t              auxiliaryVideoTileRowWidthMinus1_;
  std::vector<uint32_t> auxiliaryVideoTileRowHeight_;
  std::vector<size_t>   colWidth_;
  std::vector<size_t>   rowHeight_;
  std::vector<size_t>   partitionPosX_;
  std::vector<size_t>   partitionPosY_;

  uint32_t                     lastTileIndex_ = 0;
  std::map<uint32_t, uint32_t> tileIndexToID_;

  uint32_t startTileIndexOffset_ = 0;

};

};  // namespace vmesh
