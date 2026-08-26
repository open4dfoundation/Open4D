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
#include "acDisplacementCommon.hpp"

namespace acdisplacement {

class AcDisplacementBitstreamGofStat {
public:
  AcDisplacementBitstreamGofStat() {
    displacementBinSize_.resize(acdisplacement::RESERVED_DISPL + 1, 0);
  }
  ~AcDisplacementBitstreamGofStat() {}

  void resetDisplacementBinSize() {
    for (auto& el : displacementBinSize_) el = 0;
  }

  void setDisplacement(acdisplacement::DisplacementType type, size_t size) {
    displacementBinSize_ [type] += size;
  }

  AcDisplacementBitstreamGofStat& operator+=(const AcDisplacementBitstreamGofStat& other) {
    for (int i = 0; i < acdisplacement::RESERVED_DISPL + 1; i++) {
      displacementBinSize_[i] += other.displacementBinSize_[i];
    }
    return *this;
  }

  size_t getTotalDisplacement() {
    return displacementBinSize_[acdisplacement::I_DISPL] + displacementBinSize_[acdisplacement::P_DISPL];
  }
  size_t getDisplacementIntra() { return displacementBinSize_[acdisplacement::I_DISPL]; }
  size_t getDisplacementInter() { return displacementBinSize_[acdisplacement::P_DISPL]; }

private:
  std::vector<size_t> displacementBinSize_;
};

//class AcDisplacementBitstreamStat {
//public:
//  AcDisplacementBitstreamStat() {}
//  ~AcDisplacementBitstreamStat() { bitstreamGofStat_.clear(); }
//  void newGOF() {
//    AcDisplacementBitstreamGofStat element;
//    bitstreamGofStat_.push_back(element);
//  }
//
//  void setDisplacement(acdisplacement::DisplacementType type, size_t size) {
//    bitstreamGofStat_.back().setDisplacement(type, size);
//  }
//
//private:
//  std::vector<AcDisplacementBitstreamGofStat> bitstreamGofStat_;
//};

}  // namespace vmesh
