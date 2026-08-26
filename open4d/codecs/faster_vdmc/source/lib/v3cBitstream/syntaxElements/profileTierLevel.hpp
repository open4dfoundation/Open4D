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
#include "profileToolsetConstraintsInformation.hpp"

namespace vmesh {

// 8.3.4.2 Profile, Tier and Level Syntax
class ProfileTierLevel {
public:
  ProfileTierLevel() {}
  ~ProfileTierLevel() { subProfileIdc_.clear(); }
  ProfileTierLevel& operator=(const ProfileTierLevel&) = default;

  void allocate() { subProfileIdc_.resize(numSubProfiles_, 0); }

  uint8_t getProfileCodecGroupIdc() const {
    return uint8_t((ptlNonVideoCodecGroupIdc_ << 4) | (ptlVideoCodecGroupIdc_ & 0x0F));
  }
  uint8_t getProfileCodecGroupIdc() {
    return uint8_t((ptlNonVideoCodecGroupIdc_ << 4) | (ptlVideoCodecGroupIdc_ & 0x0F));
  }
  auto getPtlVideoCodecGroupIdc() const {
    return ptlVideoCodecGroupIdc_;
  }
  auto getPtlNonVideoCodecGroupIdc() const {
    return ptlNonVideoCodecGroupIdc_;
  }
  auto getProfileToolsetIdc() const { return profileToolsetIdc_; }
  auto getProfileReconstructionIdc() const {
    return profileReconstructionIdc_;
  }
  auto getMaxDecodesIdc() const { return maxDecodesIdc_; }
  auto getLevelIdc() const { return levelIdc_; }
  auto getTierFlag() const { return tierFlag_; }
  auto getAttributeLevelIdc() const { return attributeLevelIdc_; }
  auto getAttributeTierFlag() const { return attributeTierFlag_; }
  auto getNumSubProfiles() const { return numSubProfiles_; }
  auto getExtendedSubProfileFlag() const { return extendedSubProfileFlag_; }
  auto getToolConstraintsPresentFlag() const {
    return toolConstraintsPresentFlag_;
  }
  auto  getSubProfileIdc(size_t index) const { return subProfileIdc_[index]; }
  auto& getProfileToolsetConstraintsInformation() {
    return profileToolsetConstraintsInformation_;
  }


  auto& getProfileToolsetIdc() { return profileToolsetIdc_; }
  auto& getProfileReconstructionIdc() { return profileReconstructionIdc_; }
  auto& getMaxDecodesIdc() { return maxDecodesIdc_; }
  auto& getLevelIdc() { return levelIdc_; }
  auto& getTierFlag() { return tierFlag_; }
  auto& getAttributeLevelIdc() { return attributeLevelIdc_; }
  auto& getAttributeTierFlag() { return attributeTierFlag_; }
  auto& getNumSubProfiles() { return numSubProfiles_; }
  auto& getExtendedSubProfileFlag() { return extendedSubProfileFlag_; }
  auto& getToolConstraintsPresentFlag() { return toolConstraintsPresentFlag_; }
  auto& getSubProfileIdc(size_t index) { return subProfileIdc_[index]; }
  void setPtlCodecGroupIdc(uint8_t value) {
    ptlVideoCodecGroupIdc_ = value & 0x0F;
    ptlNonVideoCodecGroupIdc_ = value >> 4;
  }

  void setPtlVideoCodecGroupIdc(uint8_t value) {
    ptlVideoCodecGroupIdc_ = value;
  }
  void setPtlNonVideoCodecGroupIdc(uint8_t value) {
    ptlNonVideoCodecGroupIdc_ = value; }
  
private:
  uint8_t                              ptlVideoCodecGroupIdc_      = 1;
  uint8_t                              ptlNonVideoCodecGroupIdc_   = 0;
  uint8_t                              profileToolsetIdc_          = 0;
  uint8_t                              profileReconstructionIdc_   = 0;
  uint8_t                              maxDecodesIdc_              = 15;
  uint8_t                              levelIdc_                   = 0;
  bool                                 tierFlag_                   = false;
  uint8_t                              attributeLevelIdc_          = 0;
  bool                                 attributeTierFlag_          = false;
  uint8_t                              numSubProfiles_             = 0;
  bool                                 extendedSubProfileFlag_     = false;
  bool                                 toolConstraintsPresentFlag_ = false;
  std::vector<uint8_t>                 subProfileIdc_;
  ProfileToolsetConstraintsInformation profileToolsetConstraintsInformation_;
};

};  // namespace vmesh
