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

#include "meshPositionCodingPayload.hpp"
#include "meshCodingHeader.hpp"
#include "meshAttributeCodingPayload.hpp"

namespace eb {

// 1.2.1	General Mesh coding syntax
class MeshCoding {
public:
  MeshCoding() {}
  ~MeshCoding() {}

  MeshCoding& operator=(const MeshCoding&) = default;

  auto& getMeshCodingHeader() const { return meshCodingHeader_; }
  auto& getMeshPositionCodingPayload() const {
    return meshPositionCodingPayload_;
  }
  auto& getMeshAttributeCodingPayload() const {
    return meshAttributeCodingPayload_;
  }

  auto& getMeshCodingHeader() { return meshCodingHeader_; }
  auto& getMeshPositionCodingPayload() { return meshPositionCodingPayload_; }
  auto& getMeshAttributeCodingPayload() {
    return meshAttributeCodingPayload_;
  }

// Global Parameters access
  auto& getNumHandles() {
      return numHandles_;
  }
  auto& getNumPositionStart() {
      return numPositionStart_;
  }
  auto& getNumPredictedFinePositions() {
      return numPredictedFinePositions;
  }
  auto& getNumPredictedCoarsePositions() {
      return numPredictedCoarsePositions;
  }
  auto& getNumAttributeStart() {
      return numAttributeStart_;
  }
  auto& getNumPositionIsDuplicateFlags() {
      return numPositionIsDuplicateFlags_;
  }
  auto& getNumSplitVertex() {
      return numSplitVertex_;
  }
  auto& getNumAddedDuplicatedVertex() {
      return numAddedDuplicatedVertex_;
  }
    auto& getNumAttributeIsDuplicateFlags()
  {
    return numAttributeIsDuplicateFlags_;
  }
  auto& getNumSplitAttribute() { return numSplitAttribute_; }
  auto& getNumAddedDuplicatedAttribute() { return numAddedDuplicatedAttribute_; }

private:
  MeshCodingHeader            meshCodingHeader_;
  MeshPositionCodingPayload   meshPositionCodingPayload_;
  MeshAttributeCodingPayload  meshAttributeCodingPayload_;

  // Global Parameters
  uint32_t              numHandles_                   = 0;
  uint32_t              numPositionStart_             = 0;
  uint32_t              numPredictedFinePositions     = 0;
  uint32_t              numPredictedCoarsePositions   = 0;
  std::vector<uint32_t> numAttributeStart_            = {};
  uint32_t              numPositionIsDuplicateFlags_  = 0;
  uint32_t              numSplitVertex_               = 0;
  uint32_t              numAddedDuplicatedVertex_     = 0;
  std::vector<uint32_t> numAttributeIsDuplicateFlags_ = {};
  std::vector<uint32_t> numSplitAttribute_            = {};
  std::vector<uint32_t> numAddedDuplicatedAttribute_  = {};

};

};  // namespace eb
