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

namespace acdisplacement {

// disp info
class DisplInformation {
public:
  DisplInformation()
    : diUseSingleDisplFlag_(1)
    , diNumDisplsMinus2_(-1)
    , diSignalledDisplIdFlag_(0)
    , diSignalledDisplIdDeltaLength_(0)
    , diDisplId_(0) {
    diDisplId_.resize(1);
  };
  ~DisplInformation() { diDisplId_.clear(); };

  DisplInformation& operator=(const DisplInformation&) = default;
  bool              operator==(const DisplInformation& other) const {
    if (diUseSingleDisplFlag_ != other.diUseSingleDisplFlag_) {
      return false;
    } else if (!diUseSingleDisplFlag_) {
      if (diNumDisplsMinus2_ != other.diNumDisplsMinus2_) { return false; }
    }
    return true;
  }
  auto getDiUseSingleDisplFlag() const { return diUseSingleDisplFlag_; }
  auto getDiNumDisplsMinus2() const { return diNumDisplsMinus2_; }
  auto getDiSignalledDisplIdFlag() const { return diSignalledDisplIdFlag_; }
  auto getDiSignalledDisplIdDeltaLength() const {
    return diSignalledDisplIdDeltaLength_;
  }
  auto getDiDisplId(size_t index) const { return diDisplId_[index]; }

  auto& getDiUseSingleDisplFlag() { return diUseSingleDisplFlag_; }
  auto& getDiNumDisplsMinus2() { return diNumDisplsMinus2_; }
  auto& getDiSignalledDisplIdFlag() { return diSignalledDisplIdFlag_; }
  auto& getDiSignalledDisplIdDeltaLength() {
    return diSignalledDisplIdDeltaLength_;
  }
  auto& getDiDisplId(size_t index) {
    if (index >= diDisplId_.size()) diDisplId_.resize(diDisplId_.size() + 1);
    return diDisplId_[index];
  }
  auto& getDiDisplIds() { return diDisplId_; }
  void  setDiDisplId(size_t index, uint32_t value) {
    if (index >= diDisplId_.size()) diDisplId_.resize(index + 1);
    diDisplId_[index] = value;
  }

  std::vector<uint32_t> _displIDToIndex;
  std::vector<uint32_t> _displIndexToID;

private:
  bool                  diUseSingleDisplFlag_          = false;
  int32_t               diNumDisplsMinus2_             = -1;
  bool                  diSignalledDisplIdFlag_        = false;
  uint32_t              diSignalledDisplIdDeltaLength_ = 0;
  std::vector<uint32_t> diDisplId_;
};

};  // namespace vmesh
