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

//#include "v3cCommon.hpp"
#include "nalUnit.hpp"

namespace vmesh {

// C.2 Sample stream NAL unit syntax and semantics
class SampleStreamNalUnit {
public:
  SampleStreamNalUnit() {}
  ~SampleStreamNalUnit() { nalUnit_.clear(); }
  NalUnit& addNalUnit() {
    nalUnit_.resize(nalUnit_.size() + 1);
    return nalUnit_.back();
  }

  void  popFront() { nalUnit_.erase(nalUnit_.begin(), nalUnit_.begin() + 1); }
  auto& front() { return *(nalUnit_.begin()); }
  auto& getNalUnit() { return nalUnit_; }
  auto& getNalUnit(size_t index) { return nalUnit_[index]; }
  auto  getNalUnitCount() { return nalUnit_.size(); }
  auto  getSizePrecisionBytesMinus1() const {
    return sizePrecisionBytesMinus1_;
  }
  auto& getSizePrecisionBytesMinus1() { return sizePrecisionBytesMinus1_; }
  auto& getLastNalUnit() { return nalUnit_.back(); }

private:
  uint8_t              sizePrecisionBytesMinus1_ = 0;
  std::vector<NalUnit> nalUnit_;
};

};  // namespace vmesh
