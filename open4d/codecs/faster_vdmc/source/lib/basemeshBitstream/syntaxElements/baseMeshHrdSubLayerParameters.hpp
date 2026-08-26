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

namespace basemesh {

// H.15.2 VUI syntyax
// H.15.2.3	Basemesh sub-layer HRD parameters syntax
class BaseMeshHrdSubLayerParameters {
public:
  BaseMeshHrdSubLayerParameters() {}
  ~BaseMeshHrdSubLayerParameters() {
    bmBitRateValueMinus1_.clear();
    cbmbSizeValueMinus1_.clear();
    cbmbSizeDuValueMinus1_.clear();
    bmBitRateDuValueMinus1_.clear();
    bmCbrFlag_.clear();
  }
  BaseMeshHrdSubLayerParameters& operator=(const BaseMeshHrdSubLayerParameters&) = default;
  void                     allocate(size_t size) {
    bmBitRateValueMinus1_.resize(size, 0);
    cbmbSizeValueMinus1_.resize(size, 0);
    cbmbSizeDuValueMinus1_.resize(size, 0);
    bmBitRateDuValueMinus1_.resize(size, 0);
    bmCbrFlag_.resize(size, false);
  }
  auto size() { return bmBitRateValueMinus1_.size(); }

  auto getBmBitRateValueMinus1(size_t i) const {
    return bmBitRateValueMinus1_[i];
  }
  auto& getBmBitRateValueMinus1(size_t i) { return bmBitRateValueMinus1_[i]; }
  auto  getCmbmSizeValueMinus1(size_t i) const {
    return cbmbSizeValueMinus1_[i];
  }
  auto& getCmbmSizeValueMinus1(size_t i) { return cbmbSizeValueMinus1_[i]; }
  auto  getCmbmSizeDuValueMinus1(size_t i) const {
    return cbmbSizeDuValueMinus1_[i];
  }
  auto& getCmbmSizeDuValueMinus1(size_t i) {
    return cbmbSizeDuValueMinus1_[i];
  }
  auto getBmBitRateDuValueMinus1(size_t i) const {
    return bmBitRateDuValueMinus1_[i];
  }
  auto& getBmBitRateDuValueMinus1(size_t i) {
    return bmBitRateDuValueMinus1_[i];
  }
  auto  getBmCbrFlag(size_t i) const { return bmCbrFlag_[i]; }
  auto& getBmCbrFlag(size_t i) { return bmCbrFlag_[i]; }

private:
  std::vector<uint32_t> bmBitRateValueMinus1_;
  std::vector<uint32_t> cbmbSizeValueMinus1_;
  std::vector<uint32_t> cbmbSizeDuValueMinus1_;
  std::vector<uint32_t> bmBitRateDuValueMinus1_;
  std::vector<uint8_t>  bmCbrFlag_;
};

};  // namespace basemesh
