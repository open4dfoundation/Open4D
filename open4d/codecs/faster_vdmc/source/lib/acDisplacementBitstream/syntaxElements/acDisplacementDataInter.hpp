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

namespace acdisplacement {

class DisplacementDataInter {
public:
  DisplacementDataInter() {
    _frameIndex          = 0;
    _referenceFrameIndex = 0;
  }
  ~DisplacementDataInter() { displ_data_unit_.clear(); }
  DisplacementDataInter& operator=(const DisplacementDataInter&) = default;

  int32_t getReferenceFrameIndex() { return _referenceFrameIndex; }
  int32_t getFrameIndex() { return _frameIndex; }
  void    setFrameIndex(uint32_t value) { _frameIndex = value; }
  void setReferenceFrameIndex(uint32_t value) { _referenceFrameIndex = value; }

  bool readTodisplDataUnit(uint8_t* encodedDataBuffer, uint32_t byteCount) {
    displ_data_unit_.resize(byteCount, 0);
    std::copy(encodedDataBuffer,
              encodedDataBuffer + byteCount,
              reinterpret_cast<char*>(getCodedDisplDataUnitBuffer()));
    //setSize(byteCount);
    return false;
  }
  void setPayloadSize(size_t size) { _displ_data_unint_payload_size = size; }
  void allocateDisplDataBuffer(size_t size) {
    displ_data_unit_.resize(size, 0);
  }

  size_t getPayloadSize() { return _displ_data_unint_payload_size; }
  std::vector<uint8_t>& getCodedDisplDataUnit() { return displ_data_unit_; }
  size_t   getCodedDisplDataSize() { return displ_data_unit_.size(); }
  uint8_t* getCodedDisplDataUnitBuffer() { return displ_data_unit_.data(); }

  const size_t getPayloadSize() const {
    return _displ_data_unint_payload_size;
  }
  const std::vector<uint8_t>& getCodedDisplDataUnit() const {
    return displ_data_unit_;
  }
  const size_t getCodedDisplDataSize() const {
    return displ_data_unit_.size();
  }
  const uint8_t* getCodedDisplDataUnitBuffer() const {
    return displ_data_unit_.data();
  }

private:
  int32_t _frameIndex;
  size_t  _displ_data_unint_payload_size;
  int32_t _referenceFrameIndex;  //absolute frame Index

  std::vector<uint8_t> displ_data_unit_;
};

};  // namespace vmesh
