/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
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

#include "meshCodingHeader.hpp"

#include "meshAttributeExtraData.hpp"
#include "meshAttributeDeduplicateInformation.hpp"

namespace eb {

// 1.2.9	Mesh attribute coding payload syntax
class MeshAttributeCodingPayload {
public:
  MeshAttributeCodingPayload() {}
  ~MeshAttributeCodingPayload() {}

  MeshAttributeCodingPayload&
  operator=(const MeshAttributeCodingPayload&) = default;

  auto& getMeshAttributeStartCount() const { return meshAttributeStartCount_; }
  auto& getMeshAttributeFineResidualsCount() const {
      return meshAttributeFineResidualsCount_;
  }
  auto& getMeshAttributeCoarseResidualsCount() const {
      return meshAttributeCoarseResidualsCount_;
  }
  auto& getMeshAttributeEntropyPacketSize() const {
      return meshAttributeEntropyPacketSize_;
  }
  auto& getMeshAttributeEntropyPacketBuffer() const {
      return meshAttributeEntropyPacketBuffer_;
  }
  auto& getMeshAttributeSeamsCount() const { return meshAttributeSeamsCount_; }
  auto& getMeshAttributeStart() const { return meshAttributeStart_; }
  auto& getMeshAttributeExtraData() const { return meshAttributeExtraData_; }
  auto& getMeshAttributeDeduplicateInformation() const {
      return meshAttributeDeduplicateInformation_;
  }

  auto& getMeshAttributeStartCount() { return meshAttributeStartCount_; }
  auto& getMeshAttributeFineResidualsCount() {
      return meshAttributeFineResidualsCount_;
  }
  auto& getMeshAttributeCoarseResidualsCount() {
      return meshAttributeCoarseResidualsCount_;
  }
  auto& getMeshAttributeEntropyPacketSize() {
      return meshAttributeEntropyPacketSize_;
  }
  auto& getMeshAttributeEntropyPacketBuffer() {
      return meshAttributeEntropyPacketBuffer_;
  }
  auto& getMeshAttributeSeamsCount() { return meshAttributeSeamsCount_; }
  auto& getMeshAttributeStart() { return meshAttributeStart_; }
  auto& getMeshAttributeExtraData() { return meshAttributeExtraData_; }
  auto& getMeshAttributeDeduplicateInformation() {
    return meshAttributeDeduplicateInformation_;
  }

  void allocate(size_t attributeCount) {      
    meshAttributeStartCount_.resize(attributeCount);
    meshAttributeFineResidualsCount_.resize(attributeCount);
    meshAttributeCoarseResidualsCount_.resize(attributeCount);
    meshAttributeEntropyPacketSize_.resize(attributeCount);
    meshAttributeEntropyPacketBuffer_.resize(attributeCount);
    meshAttributeSeamsCount_.resize(attributeCount);
    meshAttributeStart_.resize(attributeCount);
    meshAttributeExtraData_.resize(attributeCount);
    meshAttributeDeduplicateInformation_.resize(attributeCount);
  }

private:
  std::vector<uint32_t>              meshAttributeStartCount_;
  std::vector<uint32_t>              meshAttributeFineResidualsCount_;
  std::vector<uint32_t>              meshAttributeCoarseResidualsCount_;
  std::vector<uint32_t >             meshAttributeEntropyPacketSize_;
  std::vector<std::vector<uint8_t>> meshAttributeEntropyPacketBuffer_;
  std::vector<uint32_t>              meshAttributeSeamsCount_;
  std::vector<std::vector<std::vector<uint32_t>>> meshAttributeStart_;
  std::vector<MeshAttributeExtraData> meshAttributeExtraData_;
  std::vector<MeshAttributeDeduplicateInformation>
    meshAttributeDeduplicateInformation_;
};

};  // namespace eb
