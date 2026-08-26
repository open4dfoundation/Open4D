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

namespace vmesh {

// 8.3.4.6	Profile toolset constraints information syntax
class ProfileToolsetConstraintsInformation {
public:
  ProfileToolsetConstraintsInformation() {}
  ~ProfileToolsetConstraintsInformation() { reservedConstraintByte_.clear(); }
  ProfileToolsetConstraintsInformation&
  operator=(const ProfileToolsetConstraintsInformation&) = default;

  void allocate() {
    reservedConstraintByte_.resize(NumReservedConstraintBytes_, 0);
  }

  auto getOneFrameOnlyFlag() const { return oneFrameOnlyFlag_; }
  auto getEOMContraintFlag() const { return EOMContraintFlag_; }
  auto getMaxMapCountMinus1() const { return maxMapCountMinus1_; }
  auto getMaxAtlasCountMinus1() const { return maxAtlasCountMinus1_; }
  auto getMultipleMapStreamsConstraintFlag() const {
    return multipleMapStreamsConstraintFlag_;
  }
  auto getPLRConstraintFlag() const { return PLRConstraintFlag_; }
  auto getAttributeMaxDimensionMinus1() const {
    return attributeMaxDimensionMinus1_;
  }
  auto getAttributeMaxDimensionPartitionsMinus1() const {
    return attributeMaxDimensionPartitionsMinus1_;
  }
  auto getNoEightOrientationsConstraintFlag() const {
    return noEightOrientationsConstraintFlag_;
  }
  auto getNo45DegreeProjectionPatchConstraintFlag() const {
    return No45DegreeProjectionPatchConstraintFlag_;
  }
  auto getRestrictedGeometryFlag() const {
      return restrictedGeometryFlag_;
  }
  auto getNumReservedConstraintBytes() const {
    return NumReservedConstraintBytes_;
  }
  auto getReservedConstraintByte(size_t index) const {
    return reservedConstraintByte_[index];
  }
  auto getNoSubdivisionFlag() const { return noSubdivisionFlag_; }
  auto getNoDisplacementFlag() const { return noDisplacementFlag_; }
  auto getDisplacementAscendingFlag() const { return displacementAscendingFlag_; }
  auto getDisplacementDimensionConstraintFlag() const { return displacementDimensionConstraintFlag_; }
  auto getDisplacementVideoFlag() const { return displacementVideoFlag_; }
  auto getNoPackedVideoFlag() const { return noPackedVideoFlag_; }

  auto& getOneFrameOnlyFlag() { return oneFrameOnlyFlag_; }
  auto& getEOMContraintFlag() { return EOMContraintFlag_; }
  auto& getMaxMapCountMinus1() { return maxMapCountMinus1_; }
  auto& getMaxAtlasCountMinus1() { return maxAtlasCountMinus1_; }
  auto& getMultipleMapStreamsConstraintFlag() {
    return multipleMapStreamsConstraintFlag_;
  }
  auto& getPLRConstraintFlag() { return PLRConstraintFlag_; }
  auto& getAttributeMaxDimensionMinus1() {
    return attributeMaxDimensionMinus1_;
  }
  auto& getAttributeMaxDimensionPartitionsMinus1() {
    return attributeMaxDimensionPartitionsMinus1_;
  }
  auto& getNoEightOrientationsConstraintFlag() {
    return noEightOrientationsConstraintFlag_;
  }
  auto& getNo45DegreeProjectionPatchConstraintFlag() {
    return No45DegreeProjectionPatchConstraintFlag_;
  }
  auto& getRestrictedGeometryFlag() {
      return restrictedGeometryFlag_;
  }
  auto& getNumReservedConstraintBytes() { return NumReservedConstraintBytes_; }
  auto& getReservedConstraintByte(size_t index) {
    return reservedConstraintByte_[index];
  }
  auto& getNoSubdivisionFlag()  {  return noSubdivisionFlag_; }
  auto& getNoDisplacementFlag()  { return noDisplacementFlag_; }
  auto& getDisplacementAscendingFlag()  { return displacementAscendingFlag_; }
  auto& getDisplacementDimensionConstraintFlag()  { return displacementDimensionConstraintFlag_; }
  auto& getDisplacementVideoFlag()  { return displacementVideoFlag_; }
  auto& getNoPackedVideoFlag()  { return noPackedVideoFlag_; }

private:
  bool                 oneFrameOnlyFlag_                        = false;
  bool                 EOMContraintFlag_                        = false;
  uint8_t              maxMapCountMinus1_                       = 0;
  uint8_t              maxAtlasCountMinus1_                     = 0;
  bool                 multipleMapStreamsConstraintFlag_        = false;
  bool                 PLRConstraintFlag_                       = false;
  uint8_t              attributeMaxDimensionMinus1_             = 2;
  uint8_t              attributeMaxDimensionPartitionsMinus1_   = 0;
  bool                 noEightOrientationsConstraintFlag_       = true;
  bool                 No45DegreeProjectionPatchConstraintFlag_ = true;
  bool                 restrictedGeometryFlag_                  = false;
  bool                  noSubdivisionFlag_                      = false;
  bool                  noDisplacementFlag_                     = false;
  bool                  displacementAscendingFlag_              = false;
  bool                  displacementDimensionConstraintFlag_    = false;
  bool                  displacementVideoFlag_                  = false;
  bool                  noPackedVideoFlag_                      = false;
  uint8_t              NumReservedConstraintBytes_              = 1;
  std::vector<uint8_t> reservedConstraintByte_;
};

};  // namespace vmesh
