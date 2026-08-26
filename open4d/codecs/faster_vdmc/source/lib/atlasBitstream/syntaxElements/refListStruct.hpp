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

namespace atlas {

// 8.3.6.12  Reference list structure syntax
class RefListStruct {
public:
  RefListStruct() {
    absDeltaAfocSt_.clear();
    afocLsbLt_.clear();
    stRefAtlasFrameFlag_.clear();
    strpfEntrySignFlag_.clear();
  }
  ~RefListStruct() {
    absDeltaAfocSt_.clear();
    afocLsbLt_.clear();
    stRefAtlasFrameFlag_.clear();
    strpfEntrySignFlag_.clear();
  }
  RefListStruct& operator=(const RefListStruct&) = default;

  void allocate(size_t numRefEntries = 0) {
    if (numRefEntries > 0) numRefEntries_ = numRefEntries;
    absDeltaAfocSt_.resize(numRefEntries_, 0);
    afocLsbLt_.resize(numRefEntries_, 0);
    stRefAtlasFrameFlag_.resize(numRefEntries_, false);
    strpfEntrySignFlag_.resize(numRefEntries_, false);
  }

  void addAbsDeltaAfocSt(uint8_t value) { absDeltaAfocSt_.push_back(value); }
  void addStrafEntrySignFlag(uint8_t value) {
    strpfEntrySignFlag_.push_back(value);
  }
  void addAfocLsbLt(uint8_t value) { afocLsbLt_.push_back(value); }
  void addStRefAtlasFrameFlag(uint8_t value) {
    stRefAtlasFrameFlag_.push_back(value);
  }

  auto getNumRefEntries() const { return numRefEntries_; }
  auto getAbsDeltaAfocSt(uint16_t index) const {
    return absDeltaAfocSt_[index];
  }
  auto getStrafEntrySignFlag(uint16_t index) const {
    return strpfEntrySignFlag_[index];
  }
  auto getAfocLsbLt(uint16_t index) const { return afocLsbLt_[index]; }
  auto getStRefAtlasFrameFlag(uint16_t index) const {
    return stRefAtlasFrameFlag_[index];
  }

  auto getNumRefEntries() { return numRefEntries_; }
  void setNumRefEntries(int32_t value) { numRefEntries_=value; }
  
  auto getAbsDeltaAfocSt(uint16_t index) { return absDeltaAfocSt_[index]; }
  auto getStrafEntrySignFlag(uint16_t index) {
    return strpfEntrySignFlag_[index];
  }
  auto getAfocLsbLt(uint16_t index) { return afocLsbLt_[index]; }
  auto getStRefAtlasFrameFlag(uint16_t index) {
    return stRefAtlasFrameFlag_[index];
  }

  void setAbsDeltaAfocSt(uint16_t index, uint8_t value) { absDeltaAfocSt_[index]=value; }
  void setStrafEntrySignFlag(uint16_t index, uint8_t value) {
     strpfEntrySignFlag_[index] = value;
  }
  void setAfocLsbLt(uint16_t index, uint8_t value) { afocLsbLt_[index]=value; }
  void setStRefAtlasFrameFlag(uint16_t index, uint8_t value) {
    stRefAtlasFrameFlag_[index]=value;
  }
  
private:
  uint8_t              numRefEntries_ = 0;
  std::vector<uint8_t> absDeltaAfocSt_;
  std::vector<uint8_t> afocLsbLt_;
  std::vector<uint8_t> stRefAtlasFrameFlag_;
  std::vector<uint8_t> strpfEntrySignFlag_;
};

};  // namespace vmesh
