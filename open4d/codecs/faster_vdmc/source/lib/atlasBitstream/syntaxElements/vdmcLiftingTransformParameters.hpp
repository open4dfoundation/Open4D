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
#include <vector>

namespace atlas {
class VdmcLiftingTransformParameters {
public:
  VdmcLiftingTransformParameters() {}
  ~VdmcLiftingTransformParameters() {}
  VdmcLiftingTransformParameters&
  operator=(const VdmcLiftingTransformParameters&) = default;

  int32_t getLodCount() { return lodCount_; }
  int32_t getLodCount() const { return lodCount_; }
  void    setSkipUpdateFlag(bool value) { skipUpdateFlag_ = value; }
  bool    getSkipUpdateFlag() { return skipUpdateFlag_; }
  bool    getSkipUpdateFlag() const { return skipUpdateFlag_; }

  void setAdaptivePredictionWeightFlag(bool value) {
    adaptivePredictionWeightFlag_ = value;
  }
  bool getAdaptivePredictionWeightFlag() {
    return adaptivePredictionWeightFlag_;
  }
  bool getAdaptivePredictionWeightFlag() const {
    return adaptivePredictionWeightFlag_;
  }

  void setAdaptiveUpdateWeightFlag(bool value) {
    adaptiveUpdateWeightFlag_ = value;
  }
  bool getAdaptiveUpdateWeightFlag() { return adaptiveUpdateWeightFlag_; }
  bool getAdaptiveUpdateWeightFlag() const {
    return adaptiveUpdateWeightFlag_;
  }

  void setValenceUpdateWeightFlag(bool value) {
    valenceUpdateWeightFlag_ = value;
  }
  bool getValenceUpdateWeightFlag() { return valenceUpdateWeightFlag_; }
  bool getValenceUpdateWeightFlag() const { return valenceUpdateWeightFlag_; }

  void setLiftingUpdateWeightNumerator(uint32_t index, uint32_t value) {
    liftingUpdateWeightNumerator_[index] = value;
  }
  std::vector<uint32_t>& getLiftingUpdateWeightNumerator() {
    return liftingUpdateWeightNumerator_;
  }
  uint32_t getLiftingUpdateWeightNumerator(size_t index) {
    return liftingUpdateWeightNumerator_[index];
  }
  const std::vector<uint32_t>& getLiftingUpdateWeightNumerator() const {
    return liftingUpdateWeightNumerator_;
  }

  void setLiftingUpdateWeightDenominatorMinus1(uint32_t index,
                                               uint32_t value) {
    liftingUpdateWeightDenominatorMinus1_[index] = value;
  }
  std::vector<uint32_t>& getLiftingUpdateWeightDenominatorMinus1() {
    return liftingUpdateWeightDenominatorMinus1_;
  }
  uint32_t getLiftingUpdateWeightDenominatorMinus1(size_t index) {
    return liftingUpdateWeightDenominatorMinus1_[index];
  }
  const std::vector<uint32_t>&
  getLiftingUpdateWeightDenominatorMinus1() const {
    return liftingUpdateWeightDenominatorMinus1_;
  }

  void setLiftingPredictionWeightNumerator(uint32_t index, uint32_t value) {
    liftingPredictionWeightNumerator_[index] = value;
  }
  std::vector<uint32_t>& getLiftingPredictionWeightNumerator() {
    return liftingPredictionWeightNumerator_;
  }
  uint32_t getLiftingPredictionWeightNumerator(size_t index) {
    return liftingPredictionWeightNumerator_[index];
  }
  const std::vector<uint32_t>& getLiftingPredictionWeightNumerator() const {
    return liftingPredictionWeightNumerator_;
  }

  void setLiftingPredictionWeightDenominatorMinus1(uint32_t index,
                                                   uint32_t value) {
    liftingPredictionWeightDenominatorMinus1_[index] = value;
  }
  std::vector<uint32_t>& getLiftingPredictionWeightDenominatorMinus1() {
    return liftingPredictionWeightDenominatorMinus1_;
  }
  uint32_t getLiftingPredictionWeightDenominatorMinus1(size_t index) {
    return liftingPredictionWeightDenominatorMinus1_[index];
  }
  const std::vector<uint32_t>&
  getLiftingPredictionWeightDenominatorMinus1() const {
    return liftingPredictionWeightDenominatorMinus1_;
  }

  void setLiftingDirectionalLiftingLODFlag(int32_t lod, bool value) {
    liftingDirectionalLiftingLODFlag_[lod] = value;
  }
  std::vector<bool>& getLiftingDirectionalLiftingLODFlag() {
    return liftingDirectionalLiftingLODFlag_;
  }
  const std::vector<bool>& getLiftingDirectionalLiftingLODFlag() const {
    return liftingDirectionalLiftingLODFlag_;
  }

  void setDirectionalLiftingScale1(uint32_t value) { dirliftScale1_ = value; }
  uint32_t&       getDirectionalLiftingScale1() { return dirliftScale1_; }
  const uint32_t& getDirectionalLiftingScale1() const {
    return dirliftScale1_;
  }

  void setDirectionalLiftingDeltaScale2(uint32_t value) {
    dirliftDeltaScale2_ = value;
  }
  uint32_t& getDirectionalLiftingDeltaScale2() { return dirliftDeltaScale2_; }
  const uint32_t& getDirectionalLiftingDeltaScale2() const {
    return dirliftDeltaScale2_;
  }

  void setDirectionalLiftingDeltaScale3(uint32_t value) {
    dirliftDeltaScale3_ = value;
  }
  uint32_t& getDirectionalLiftingDeltaScale3() { return dirliftDeltaScale3_; }
  const uint32_t& getDirectionalLiftingDeltaScale3() const {
    return dirliftDeltaScale3_;
  }

  void setDirectionalLiftingScaleDenoMinus1(uint32_t value) {
    dirliftScaleDenoMinus1_ = value;
  }
  uint32_t& getDirectionalLiftingScaleDenoMinus1() {
    return dirliftScaleDenoMinus1_;
  }
  const uint32_t& getDirectionalLiftingScaleDenoMinus1() const {
    return dirliftScaleDenoMinus1_;
  }

  void setNumLod(uint32_t value) {
    lodCount_ = value;
    liftingUpdateWeightNumerator_.resize(lodCount_, 0);
    liftingUpdateWeightDenominatorMinus1_.resize(lodCount_, 0);
    liftingPredictionWeightNumerator_.resize(lodCount_, 0);
    liftingPredictionWeightDenominatorMinus1_.resize(lodCount_, 0);
    //adaptiveUpdateWeightFlag_.resize(lodCount_, 0);
    //valenceUpdateWeightFlag_.resize(lodCount_, 0);
    liftingOffsetValuesNumerator_.resize(lodCount_, 0);
    liftingOffsetValuesDenominatorMinus1_.resize(lodCount_, 0);
    liftingOffsetDeltaValuesNumerator_.resize(lodCount_, 0);
    liftingOffsetDeltaValuesDenominator_.resize(lodCount_, 0);
  }

  void setLiftingOffsetValuesNumerator(int32_t lod, int32_t value) {
    liftingOffsetValuesNumerator_[lod] = value;
  }
  std::vector<int32_t>& getLiftingOffsetValuesNumerator() {
    return liftingOffsetValuesNumerator_;
  }
  const std::vector<int32_t>& getLiftingOffsetValuesNumerator() const {
    return liftingOffsetValuesNumerator_;
  }

  void setLiftingOffsetValuesDenominatorMinus1(int32_t lod, int32_t value) {
    liftingOffsetValuesDenominatorMinus1_[lod] = value;
  }
  std::vector<int32_t>& getLiftingOffsetValuesDenominatorMinus1() {
    return liftingOffsetValuesDenominatorMinus1_;
  }
  const std::vector<int32_t>& getLiftingOffsetValuesDenominatorMinus1() const {
    return liftingOffsetValuesDenominatorMinus1_;
  }

  void setLiftingOffsetDeltaValuesNumerator(int32_t lod, int32_t value) {
    liftingOffsetDeltaValuesNumerator_[lod] = value;
  }
  std::vector<int32_t>& getLiftingOffsetDeltaValuesNumerator() {
    return liftingOffsetDeltaValuesNumerator_;
  }
  const std::vector<int32_t>& getLiftingOffsetDeltaValuesNumerator() const {
    return liftingOffsetDeltaValuesNumerator_;
  }

  void setLiftingOffsetDeltaValuesDenominator(int32_t lod, int32_t value) {
    liftingOffsetDeltaValuesDenominator_[lod] = value;
  }
  std::vector<int32_t>& getLiftingOffsetDeltaValuesDenominator() {
    return liftingOffsetDeltaValuesDenominator_;
  }
  const std::vector<int32_t>& getLiftingOffsetDeltaValuesDenominator() const {
    return liftingOffsetDeltaValuesDenominator_;
  }

  bool compareMainParameters(VdmcLiftingTransformParameters& rhs) {
    if (lodCount_ != rhs.getLodCount()) return false;

    bool compFlag =
      skipUpdateFlag_ == rhs.getSkipUpdateFlag()
      && adaptivePredictionWeightFlag_ == rhs.getAdaptivePredictionWeightFlag()
      && adaptiveUpdateWeightFlag_ == rhs.getAdaptiveUpdateWeightFlag()
      && valenceUpdateWeightFlag_ == rhs.getValenceUpdateWeightFlag();

    if (!compFlag) return false;
    bool compFlagLod = true;
    for (int i = 0; i < lodCount_; i++) {
      bool compFlagLod =
        liftingUpdateWeightNumerator_[i]
          == rhs.getLiftingUpdateWeightNumerator(i)
        && liftingUpdateWeightDenominatorMinus1_[i]
             == rhs.getLiftingUpdateWeightDenominatorMinus1(i)
        && liftingPredictionWeightNumerator_[i]
             == rhs.getLiftingPredictionWeightNumerator(i)
        && liftingPredictionWeightDenominatorMinus1_[i]
             == rhs.getLiftingPredictionWeightDenominatorMinus1(i);
      if (!compFlagLod) return false;
    }
    return true;
  }

private:
  bool                  skipUpdateFlag_               = 0;
  bool                  adaptivePredictionWeightFlag_ = 1;
  bool                  adaptiveUpdateWeightFlag_     = 1;
  bool                  valenceUpdateWeightFlag_      = 1;
  std::vector<uint32_t> liftingUpdateWeightNumerator_;
  std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1_;
  std::vector<uint32_t> liftingPredictionWeightNumerator_;
  std::vector<uint32_t> liftingPredictionWeightDenominatorMinus1_;
  uint8_t               lodCount_ = 0;
  std::vector<int32_t>  liftingOffsetValuesNumerator_;
  std::vector<int32_t>  liftingOffsetValuesDenominatorMinus1_;
  std::vector<int32_t>  liftingOffsetDeltaValuesNumerator_;
  std::vector<int32_t>  liftingOffsetDeltaValuesDenominator_;
  std::vector<bool>     liftingDirectionalLiftingLODFlag_;
  uint32_t              dirliftScale1_          = 0;
  uint32_t              dirliftDeltaScale2_     = 0;
  uint32_t              dirliftDeltaScale3_     = 0;
  uint32_t              dirliftScaleDenoMinus1_ = 0;
};

};  // namespace atlas
