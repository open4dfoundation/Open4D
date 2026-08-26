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
#include <vector>

namespace vmesh {

// 8.3.5 NAL unit syntax
class NalUnit {
public:
  NalUnit(uint8_t nalUnitType      = 0,
          uint8_t layerId          = 0,
          uint8_t temporalyIdPlus1 = 0,
          size_t  size             = 0)
    : type_(nalUnitType)
    , layerId_(layerId)
    , temporalyIdPlus1_(temporalyIdPlus1)
    , size_(size) {}

  ~NalUnit() { data_.clear(); }

  void    allocate() { data_.resize(size_ - 2, 0); }
  uint8_t getType() const { return type_; }
  uint8_t getLayerId() const { return layerId_; }
  uint8_t getTemporalyIdPlus1() const { return temporalyIdPlus1_; }
  size_t  getSize() const { return size_; }
  uint8_t getData(size_t index) const { return data_[index]; }
  std::vector<uint8_t>& getData() { return data_; }

  uint8_t& getType() { return type_; }
  uint8_t& getLayerId() { return layerId_; }
  uint8_t& getTemporalyIdPlus1() { return temporalyIdPlus1_; }
  size_t   getSize() { return size_; }
  void     setSize(size_t value) { size_ = value; }
  uint8_t& getData(size_t index) { return data_[index]; }

  void initialize(uint8_t  type,
                  uint8_t  layerId,
                  uint8_t  temporalyIdPlus1,
                  size_t   size = 0,
                  uint8_t* data = nullptr) {
    type_             = type;
    layerId_          = layerId;
    temporalyIdPlus1_ = temporalyIdPlus1;
    if (size_ > 0) {
      size_ = size;
      data_.resize(size_, 0);
      memcpy(data_.data(), data, size_ * sizeof(uint8_t));
    }
  }

private:
  uint8_t              type_             = 0;
  uint8_t              layerId_          = 0;
  uint8_t              temporalyIdPlus1_ = 0;
  size_t               size_             = 0;
  std::vector<uint8_t> data_;
};

};  // namespace vmesh
