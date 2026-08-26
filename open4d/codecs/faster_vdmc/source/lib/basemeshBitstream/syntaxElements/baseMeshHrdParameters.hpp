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

#include "baseMeshHrdSubLayerParameters.hpp"

namespace basemesh {

// H.15.2.2	Basemesh HRD parameters syntax
class BaseMeshHrdParameters {
public:
  BaseMeshHrdParameters() {
    subLayerParameters_[0].resize(maxNumSubLayersMinus1_);
    subLayerParameters_[1].resize(maxNumSubLayersMinus1_);
  }
  ~BaseMeshHrdParameters() {
    subLayerParameters_[0].clear();
    subLayerParameters_[1].clear();
  }
  BaseMeshHrdParameters& operator=(const BaseMeshHrdParameters&) = default;

  auto getMaxNumSubLayersMinus1() { return maxNumSubLayersMinus1_; }

  auto getBmNalParametersPresentFlag() const {
    return bmNalParametersPresentFlag_;
  }
  auto& getBmNalParametersPresentFlag() { return bmNalParametersPresentFlag_; }
  auto  getBmClParametersPresentFlag() const {
    return bmClParametersPresentFlag_;
  }
  auto& getBmClParametersPresentFlag() { return bmClParametersPresentFlag_; }
  auto  getSubmeshHrdParametersPresentFlag() const {
    return submeshHrdParametersPresentFlag_;
  }
  auto& getSubmeshHrdParametersPresentFlag() {
    return submeshHrdParametersPresentFlag_;
  }
  auto  getBmTickDivisorMinus2() const { return bmTickDivisorMinus2_; }
  auto& getBmTickDivisorMinus2() { return bmTickDivisorMinus2_; }
  auto  getDuCbmbRemovalDelayIncrementLengthMinus1() const {
    return duCbmbRemovalDelayIncrementLengthMinus1_;
  }
  auto& getDuCbmbRemovalDelayIncrementLengthMinus1() {
    return duCbmbRemovalDelayIncrementLengthMinus1_;
  }
  auto getSubmeshCbmbParamsInBmTimingSeiFlag() const {
    return submeshCbmbParamsInBmTimingSeiFlag_;
  }
  auto& getSubmeshCbmbParamsInBmTimingSeiFlag() {
    return submeshCbmbParamsInBmTimingSeiFlag_;
  }
  auto getDbmbOutputDuDelayLengthMinus1() const {
    return dbmbOutputDuDelayLengthMinus1_;
  }
  auto& getDbmbOutputDuDelayLengthMinus1() {
    return dbmbOutputDuDelayLengthMinus1_;
  }
  auto  getBmBitRateScale() const { return bmBitRateScale_; }
  auto& getBmBitRateScale() { return bmBitRateScale_; }
  auto  getCbmbSizeScale() const { return cbmbSizeScale_; }
  auto& getCbmbSizeScale() { return cbmbSizeScale_; }
  auto  getCbmbSizeDuScale() const { return cbmbSizeDuScale_; }
  auto& getCbmbSizeDuScale() { return cbmbSizeDuScale_; }
  auto  getInitialCbmbRemovalDelayLengthMinus1() const {
    return initialCbmbRemovalDelayLengthMinus1_;
  }
  auto& getInitialCbmbRemovalDelayLengthMinus1() {
    return initialCbmbRemovalDelayLengthMinus1_;
  }
  auto getAuCbmbRemovalDelayLengthMinus1() const {
    return auCbmbRemovalDelayLengthMinus1_;
  }
  auto& getAuCbmbRemovalDelayLengthMinus1() {
    return auCbmbRemovalDelayLengthMinus1_;
  }
  auto getdbmbOutputDelayLengthMinus1() const {
    return dbmbOutputDelayLengthMinus1_;
  }
  auto& getdbmbOutputDelayLengthMinus1() {
    return dbmbOutputDelayLengthMinus1_;
  }
  auto getBmFixedRateGeneralFlag_(size_t i) const {
    return bmFixedRateGeneralFlag_[i];
  }
  auto& getBmFixedRateGeneralFlag(size_t i) {
    return bmFixedRateGeneralFlag_[i];
  }
  auto getBmFixedRateWithinCbmsFlag(size_t i) const {
    return bmFixedRateWithinCbmsFlag_[i];
  }
  auto& getBmFixedRateWithinCbmsFlag(size_t i) {
    return bmFixedRateWithinCbmsFlag_[i];
  }
  auto getBmElementalDurationInTcMinus1(size_t i) const {
    return bmElementalDurationInTcMinus1_[i];
  }
  auto& getBmElementalDurationInTcMinus1(size_t i) {
    return bmElementalDurationInTcMinus1_[i];
  }
  auto  getBmLowDelayFlag(size_t i) const { return bmLowDelayFlag_[i]; }
  auto& getBmLowDelayFlag(size_t i) { return bmLowDelayFlag_[i]; }
  auto  getCbmdCntMinus1(size_t i) const { return cbmdCntMinus1_[i]; }
  auto& getCbmdCntMinus1(size_t i) { return cbmdCntMinus1_[i]; }
  auto& getBmHdrSubLayerParameters(size_t i, size_t j) const {
    return subLayerParameters_[i][j];
  }
  auto& getBmHdrSubLayerParameters(size_t i, size_t j) {
    return subLayerParameters_[i][j];
  }

private:
  //TODO: check if the default values are correct
  static const int32_t  maxNumSubLayersMinus1_                   = 0;
  bool                  bmNalParametersPresentFlag_              = 0;
  bool                  bmClParametersPresentFlag_               = 0;
  bool                  submeshHrdParametersPresentFlag_         = 0;
  uint8_t               bmTickDivisorMinus2_                     = 0;
  uint8_t               duCbmbRemovalDelayIncrementLengthMinus1_ = 0;
  uint8_t               submeshCbmbParamsInBmTimingSeiFlag_      = 0;
  uint8_t               dbmbOutputDuDelayLengthMinus1_           = 0;
  uint8_t               bmBitRateScale_                          = 0;
  uint8_t               cbmbSizeScale_                           = 0;
  uint8_t               cbmbSizeDuScale_                         = 0;
  uint8_t               initialCbmbRemovalDelayLengthMinus1_     = 0;
  uint8_t               auCbmbRemovalDelayLengthMinus1_          = 0;
  uint8_t               dbmbOutputDelayLengthMinus1_             = 0;
  std::vector<uint8_t>  bmFixedRateGeneralFlag_;
  std::vector<uint8_t>  bmFixedRateWithinCbmsFlag_;
  std::vector<uint32_t> bmElementalDurationInTcMinus1_;
  std::vector<uint8_t>  bmLowDelayFlag_;
  std::vector<uint32_t> cbmdCntMinus1_;
  std::vector<BaseMeshHrdSubLayerParameters> subLayerParameters_[2];
};

};  // namespace basemesh
