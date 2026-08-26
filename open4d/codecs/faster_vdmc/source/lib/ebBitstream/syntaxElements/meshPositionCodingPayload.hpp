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

#include "meshPositionDeduplicateInformation.hpp"
#include "meshDifferenceInformation.hpp"

namespace eb {

// 1.2.7	Mesh position coding payload syntax
class MeshPositionCodingPayload {
public:
  MeshPositionCodingPayload() {}
  ~MeshPositionCodingPayload() {}

  MeshPositionCodingPayload&
  operator=(const MeshPositionCodingPayload&) = default;

  auto& getMeshTriangleCount() const { return meshTriangleCount_; }
  auto& getMeshPositionStartCount() const { return meshPositionStartCount_; }
  auto& getMeshPositionFineResidualsCount() const {
      return meshPositionFineResidualsCount_;
  }
  auto& getMeshPositionCoarseResidualsCount() const {
      return meshPositionCoarseResidualsCount_;
  }
  auto& getMeshClersCount() const { return meshClersCount_; }
  auto& getMeshCcWithBoundaryCount() const { return meshCcWithBoundaryCount_; }
  auto& getMeshHandlesCount() const { return meshHandlesCount_; }
  auto& getMeshHandleFirstDelta() const {
    return meshHandleFirstDelta_;
  }
  auto& getMeshHandleSecondDelta() const {
    return meshHandleSecondDelta_;
  }
  auto& getMeshPositionStart() const { return meshPositionStart_; }
  auto& getMeshPositionDeduplicateInformation() const {
    return meshPositionDeduplicateInformation_;
  }
  auto& getMeshDifferenceInformation() const { return meshDifferenceInformation_; }


  auto& getMeshTriangleCount() { return meshTriangleCount_; }
  auto& getMeshPositionStartCount() { return meshPositionStartCount_; }
  auto& getMeshPositionFineResidualsCount() {
      return meshPositionFineResidualsCount_;
  }
  auto& getMeshPositionCoarseResidualsCount() {
      return meshPositionCoarseResidualsCount_;
  }
  auto& getMeshClersCount() { return meshClersCount_; }
  auto& getMeshCcWithBoundaryCount() { return meshCcWithBoundaryCount_; }
  auto& getMeshHandlesCount() { return meshHandlesCount_; }

  auto& getMeshHandleFirstDelta() {
      return meshHandleFirstDelta_;
  }
  auto& getMeshHandleSecondDelta() {
      return meshHandleSecondDelta_;
  }
  auto& getMeshPositionStart() { return meshPositionStart_; }
  auto& getMeshPositionDeduplicateInformation() {
      return meshPositionDeduplicateInformation_;
  }

  auto& getMeshDifferenceInformation() { return meshDifferenceInformation_; }

private:
  uint32_t                           meshTriangleCount_       = 0;
  uint32_t                           meshPositionStartCount_  = 0;
  uint32_t                           meshPositionFineResidualsCount_ = 0;
  uint32_t                           meshPositionCoarseResidualsCount_ = 0;
  uint32_t                           meshClersCount_          = 0;
  uint32_t                           meshCcWithBoundaryCount_ = 0;
  uint32_t                           meshHandlesCount_        = 0;
  std::vector<int32_t>               meshHandleFirstDelta_;
  std::vector<int32_t>               meshHandleSecondDelta_;
  std::vector<std::vector<uint32_t>> meshPositionStart_;
  MeshPositionDeduplicateInformation meshPositionDeduplicateInformation_;
  MeshDifferenceInformation meshDifferenceInformation_;
};

};  // namespace eb
