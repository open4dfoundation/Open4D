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
#include "v3cUnit.hpp"

namespace vmesh {

// B.2 Sample stream V3C unit syntax and semantics
// 6.1 V3C bistreams format
class SampleStreamV3CUnit {
public:
  SampleStreamV3CUnit() : ssvhUnitSizePrecisionBytesMinus1_(0) {}
  ~SampleStreamV3CUnit() { v3cUnits_.clear(); }
  V3CUnit& addV3CUnit() {
    v3cUnits_.resize(v3cUnits_.size() + 1);
    return v3cUnits_.back();
  }
  void popFront() {
    v3cUnits_.erase(v3cUnits_.begin(), v3cUnits_.begin() + 1);
  }
  auto& front() { return *(v3cUnits_.begin()); }
  auto& getV3CUnit() { return v3cUnits_; }
  auto  getV3CUnitCount() { return v3cUnits_.size(); }
  auto& getLastV3CUnit() { return v3cUnits_.back(); }
  auto  getSsvhUnitSizePrecisionBytesMinus1() const {
    return ssvhUnitSizePrecisionBytesMinus1_;
  }
  auto& getSsvhUnitSizePrecisionBytesMinus1() {
    return ssvhUnitSizePrecisionBytesMinus1_;
  }

  size_t getSize() {
    size_t size = 0;
    for (auto& v3cUnit : v3cUnits_) size += v3cUnit.getSize();
    return size;
  }

private:
  std::vector<V3CUnit> v3cUnits_;
  uint32_t             ssvhUnitSizePrecisionBytesMinus1_;
};

};  // namespace vmesh
