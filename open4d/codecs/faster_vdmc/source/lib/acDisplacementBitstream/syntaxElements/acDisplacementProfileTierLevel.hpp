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
#include "acDisplacementProfileToolsetConstraintsInformation.hpp"

namespace acdisplacement {

//H.8.1.3.1.2  displ Profile, tier, and level syntax
class DisplProfileTierLevel {
public:
  DisplProfileTierLevel() {}
  ~DisplProfileTierLevel() {}
  DisplProfileTierLevel& operator=(const DisplProfileTierLevel&) = default;
  void                   setDptlTierFlag(bool value) { dptlTierFlag_ = value; }
  void                   setDptlProfileToolsetIdc(uint32_t value) {
    dptlProfileToolsetIdc_ = value;
  }
  void setDptlReservedZero32bits(uint8_t value) {
    dptlReservedZero32bits_ = value;
  }
  void setDptlLevelIdc(uint32_t value) { dptlLevelIdc_ = value; }
  void setDptlNumSubProfiles(uint8_t value) { dptlNumSubProfiles_ = value; }
  void setDptlExtendedSubProfileFlag(bool value) {
    dptlExtendedSubProfileFlag_ = value;
  }
  void setDptlSubProfileIdc(std::vector<uint8_t>& value) {
    dptlSubProfileIdc_ = value;
  }
  void setDptlSubProfileIdc(size_t idx, uint8_t value) {
    dptlSubProfileIdc_[idx] = value;
  }
  void setDptlToolsetConstraintsPresentFlag(bool value) {
    dptlToolsetConstraintsPresentFlag_ = value;
  }
  void setDptlProfileToolsetConstraintsInformation(
    DisplProfileToolsetConstraintsInformation& value) {
    dptlProfileToolsetConstraintsInformation_ = value;
  }

  bool     getDptlTierFlag() { return dptlTierFlag_; }
  uint32_t getDptlProfileToolsetIdc() { return dptlProfileToolsetIdc_; }
  uint8_t  getDptlReservedZero32bits() { return dptlReservedZero32bits_; }
  uint32_t getDptlLevelIdc() { return dptlLevelIdc_; }
  uint8_t  getDptlNumSubProfiles() { return dptlNumSubProfiles_; }
  bool getDptlExtendedSubProfileFlag() { return dptlExtendedSubProfileFlag_; }
  std::vector<uint8_t>& getDptlSubProfileIdc() { return dptlSubProfileIdc_; }
  bool                  getDptlToolsetConstraintsPresentFlag() {
    return dptlToolsetConstraintsPresentFlag_;
  }
  DisplProfileToolsetConstraintsInformation&
  getDptlProfileToolsetConstraintsInformation() {
    return dptlProfileToolsetConstraintsInformation_;
  }

private:
  bool                 dptlTierFlag_               = 0;
  uint32_t             dptlProfileToolsetIdc_      = 0;
  uint8_t              dptlReservedZero32bits_     = 0;
  uint32_t             dptlLevelIdc_               = 0;
  uint8_t              dptlNumSubProfiles_         = 0;
  bool                 dptlExtendedSubProfileFlag_ = 0;
  std::vector<uint8_t> dptlSubProfileIdc_;
  bool                 dptlToolsetConstraintsPresentFlag_ = 0;
  DisplProfileToolsetConstraintsInformation
    dptlProfileToolsetConstraintsInformation_;
};

};  // namespace vmesh
