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

#include "v3cCommon.hpp"

namespace vmesh {

// 8.3.4.5 Attribute information Syntax
class AttributeInformation {
public:
  AttributeInformation() {}
  ~AttributeInformation() {
    attributeTypeId_.clear();
    attributeCodecId_.clear();
    auxiliaryAttributeCodecId_.clear();
    attributeDimensionMinus1_.clear();
    attributeDimensionPartitionsMinus1_.clear();
    Attribute2dBitdepthMinus1_.clear();
    for (auto& value : attributePartitionChannelsMinus1_) { value.clear(); }
    attributePartitionChannelsMinus1_.clear();
    attributeMapAbsoluteCodingPersistenceFlagList_.clear();
    attributeMSBAlignFlag_.clear();
  }
  AttributeInformation& operator=(const AttributeInformation&) = default;

  void allocate(size_t attributeCount) {
    attributeCount_ = attributeCount;
    attributeTypeId_.resize(attributeCount_, 0);
    attributeCodecId_.resize(attributeCount_, 0);
    auxiliaryAttributeCodecId_.resize(attributeCount_, 0);
    attributeDimensionMinus1_.resize(attributeCount_, 0);
    attributeDimensionPartitionsMinus1_.resize(attributeCount_, 0);
    Attribute2dBitdepthMinus1_.resize(attributeCount_, 0);
    attributePartitionChannelsMinus1_.resize(attributeCount_);
    attributeMapAbsoluteCodingPersistenceFlagList_.resize(attributeCount_);
    attributeMSBAlignFlag_.resize(attributeCount_, 0);
  }

  auto getAttributeCount() const { return attributeCount_; }
  auto getAttributeMSBAlignFlag(uint32_t index) const {
    return attributeMSBAlignFlag_[index];
  }
  auto getAttributeTypeId(uint32_t index) const {
    return attributeTypeId_[index];
  }
  auto getAttributeCodecId(uint32_t index) const {
    return attributeCodecId_[index];
  };
  auto getAuxiliaryAttributeCodecId(uint32_t index) const {
    return auxiliaryAttributeCodecId_[index];
  }
  auto getAttributeDimensionMinus1(uint32_t index) const {
    return attributeDimensionMinus1_[index];
  }
  auto getAttributeDimensionPartitionsMinus1(uint32_t index) const {
    return attributeDimensionPartitionsMinus1_[index];
  }
  auto getAttribute2dBitdepthMinus1(uint32_t index) const {
    return Attribute2dBitdepthMinus1_[index];
  }
  auto getAttributePartitionChannelsMinus1(uint32_t index, uint32_t j) const {
    return attributePartitionChannelsMinus1_[index][j];
  }
  auto& getAttributeMapAbsoluteCodingPersistenceFlagList() {
    return attributeMapAbsoluteCodingPersistenceFlagList_;
  }
  auto getAttributeMapAbsoluteCodingPersistenceFlag(size_t attIdx) const {
    return attributeMapAbsoluteCodingPersistenceFlagList_[attIdx];
  }

  auto& getAttributeCount() { return attributeCount_; }
  auto& getAttributeMSBAlignFlag(uint32_t index) {
    return attributeMSBAlignFlag_[index];
  }
  auto& getAttributeTypeId(uint32_t index) { return attributeTypeId_[index]; }
  auto& getAttributeCodecId(uint32_t index) {
    return attributeCodecId_[index];
  };
  auto& getAuxiliaryAttributeCodecId(uint32_t index) {
    return auxiliaryAttributeCodecId_[index];
  }
  auto& getAttributeDimensionMinus1(uint32_t index) {
    return attributeDimensionMinus1_[index];
  }
  auto& getAttributeDimensionPartitionsMinus1(uint32_t index) {
    return attributeDimensionPartitionsMinus1_[index];
  }
  auto& getAttribute2dBitdepthMinus1(uint32_t index) {
    return Attribute2dBitdepthMinus1_[index];
  }
  auto& getAttributePartitionChannelsMinus1(uint32_t index, uint32_t j) {
    if (j >= attributePartitionChannelsMinus1_[index].size()) {
      attributePartitionChannelsMinus1_[index].resize(j + 1, 0);
    }
    return attributePartitionChannelsMinus1_[index][j];
  }
  auto& getAttributeMapAbsoluteCodingPersistenceFlag(size_t attIdx) {
    return attributeMapAbsoluteCodingPersistenceFlagList_[attIdx];
  }

private:
  uint8_t              attributeCount_ = 0;
  std::vector<uint8_t> attributeTypeId_;
  std::vector<uint8_t> attributeCodecId_;
  std::vector<uint8_t> auxiliaryAttributeCodecId_;
  std::vector<uint8_t> attributeMapAbsoluteCodingPersistenceFlagList_;
  std::vector<uint8_t> attributeDimensionMinus1_;
  std::vector<uint8_t> attributeDimensionPartitionsMinus1_;
  std::vector<std::vector<uint8_t>> attributePartitionChannelsMinus1_;
  std::vector<uint8_t>              Attribute2dBitdepthMinus1_;
  std::vector<uint8_t>              attributeMSBAlignFlag_;
};

};  // namespace vmesh
