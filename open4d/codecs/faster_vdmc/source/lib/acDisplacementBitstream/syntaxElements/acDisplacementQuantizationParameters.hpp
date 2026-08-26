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

#include <vector>

namespace acdisplacement {

class DisplQuantizationParameters {
public:
  DisplQuantizationParameters() {}
  DisplQuantizationParameters(int paramIndex) {
    _displParamLevelIndex_ = paramIndex;
  }
  ~DisplQuantizationParameters() {}
  DisplQuantizationParameters&
  operator=(const DisplQuantizationParameters&) = default;

  void setDisplLodQuantizationFlag(bool value) {
    displLodQuantizationFlag_ = value;
  }
  bool getDisplLodQuantizationFlag() { return displLodQuantizationFlag_; }
  bool getDisplLodQuantizationFlag() const {
    return displLodQuantizationFlag_;
  }

  void setDisplBitDepthOffset(int32_t value) { displBitDepthOffset_ = value; }
  int32_t getDisplBitDepthOffset() { return displBitDepthOffset_; }
  int32_t getDisplBitDepthOffset() const { return displBitDepthOffset_; }

  void setDisplQuantizationParameters(uint32_t index, uint32_t value) {
    displQuantizationParameters_[index] = value;
  }
  std::vector<uint32_t>& getDisplQuantizationParameters() {
    return displQuantizationParameters_;
  }
  const std::vector<uint32_t>& getDisplQuantizationParameters() const {
    return displQuantizationParameters_;
  }

  void setDisplLog2LodInverseScale(uint32_t index, uint32_t value) {
    displLog2LodInverseScale_[index] = value;
  }
  std::vector<uint32_t>& getDisplLog2LodInverseScale() {
    return displLog2LodInverseScale_;
  }
  const std::vector<uint32_t>& getDisplLog2LodInverseScale() const {
    return displLog2LodInverseScale_;
  }

  void setDisplLodDeltaQPValue(int level, int axis, uint8_t value) {
    displLodDeltaQPValue_[level][axis] = value;
  }
  uint8_t getDisplLodDeltaQPValue(int level, int axis) const {
    return displLodDeltaQPValue_[level][axis];
  }
  uint8_t getDisplLodDeltaQPValue(int level, int axis) {
    return displLodDeltaQPValue_[level][axis];
  }
  std::vector<uint8_t>& getDisplLodDeltaQPValue(int level) {
    return displLodDeltaQPValue_[level];
  }
  std::vector<uint8_t> getDisplLodDeltaQPValue(int level) const {
    return displLodDeltaQPValue_[level];
  }
  std::vector<std::vector<uint8_t>>& getDisplLodDeltaQPValue() {
    return displLodDeltaQPValue_;
  }
  std::vector<std::vector<uint8_t>> getDisplLodDeltaQPValue() const {
    return displLodDeltaQPValue_;
  }

  void setDisplLodDeltaQPSign(int level, int axis, uint8_t value) {
    displLodDeltaQPSign_[level][axis] = value;
  }
  uint8_t getDisplLodDeltaQPSign(int level, int axis) const {
    return displLodDeltaQPSign_[level][axis];
  }
  uint8_t getDisplLodDeltaQPSign(int level, int axis) {
    return displLodDeltaQPSign_[level][axis];
  }
  std::vector<uint8_t>& getDisplLodDeltaQPSign(int level) {
    return displLodDeltaQPSign_[level];
  }
  std::vector<uint8_t> getDisplLodDeltaQPSign(int level) const {
    return displLodDeltaQPSign_[level];
  }
  std::vector<std::vector<uint8_t>>& getDisplLodDeltaQPSign() {
    return displLodDeltaQPSign_;
  }
  std::vector<std::vector<uint8_t>> getDisplLodDeltaQPSign() const {
    return displLodDeltaQPSign_;
  }

  void setNumComponents(uint32_t value) {
    numComponents_ = value;
    displQuantizationParameters_.resize(numComponents_);
    displLog2LodInverseScale_.resize(numComponents_);
  }
  void setNumLod(uint32_t value) {
    lodCount_ = value;
    displLodDeltaQPValue_.resize(lodCount_);
    displLodDeltaQPSign_.resize(lodCount_);
  }
  void allocComponents(size_t numComponents, size_t lodIdx) {
    displLodDeltaQPValue_[lodIdx].resize(numComponents);
    displLodDeltaQPSign_[lodIdx].resize(numComponents);
  }

private:
  int                               _displParamLevelIndex_    = 0;
  uint8_t                           numComponents_            = 0;
  uint8_t                           lodCount_                 = 0;
  bool                              displLodQuantizationFlag_ = false;
  int32_t                           displBitDepthOffset_      = 0;
  std::vector<uint32_t>             displQuantizationParameters_;
  std::vector<uint32_t>             displLog2LodInverseScale_;
  std::vector<std::vector<uint8_t>> displLodDeltaQPValue_;
  std::vector<std::vector<uint8_t>> displLodDeltaQPSign_;
};

};  // namespace vmesh
