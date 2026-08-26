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

#include <vector>

#include "ebCommon.hpp"
#include "meshPositionEncodingParameters.hpp"
#include "meshPositionDequantizeParameters.hpp"
#include "meshAttributesEncodingParameters.hpp"
#include "meshAttributesDequantizeParameters.hpp"

namespace eb {

// 1.2.2  Mesh coding header syntax
class MeshCodingHeader {
public:
  MeshCodingHeader() {}
  ~MeshCodingHeader() {}
  MeshCodingHeader& operator=(const MeshCodingHeader&) = default;

  auto getMeshCodecType() const { return meshCodecType_; }
  auto getMeshVertexTraversalMethod() const {
    return meshVertexTraversalMethod_;
  }
  auto getMeshPositionDequantizeFlag() const {
      return meshPositionDequantizeFlag_;
  }
  auto  getMeshAttributeCount() const { return meshAttributeCount_; }
  auto  getMeshAttributeType() const { return meshAttributeType_; }
  auto& getMeshAttributeNumComponentsMinus1() const {
    return meshAttributeNumComponentsMinus1_;
  }
  auto& getMeshAttributeDequantizeFlag() const {
    return meshAttributeDequantizeFlag_;
  }
  auto& getMeshPositionEncodingParameters() const {
    return meshPositionEncodingParameters_;
  }
  auto& getMeshPositionDequantizeParameters() const {
    return meshPositionDequantizeParameters_;
  }
  auto& getMeshAttributesEncodingParameters() const {
    return meshAttributesEncodingParameters_;
  }
  auto& getMeshAttributesDequantizeParameters() const {
    return meshAttributesDequantizeParameters_;
  }
  auto& getMeshAttributesDequantizeParameters(uint32_t index) const {
    return meshAttributesDequantizeParameters_[index];
  }
  auto& getMeshAttributesEncodingParameters(uint32_t index) const {
    return meshAttributesEncodingParameters_[index];
  }
  auto& getMeshDeduplicateMethod() const { return meshDeduplicateMethod_; }
  auto& getMeshReindexOutput() const { return meshReindexOutput_; }

  auto& getMeshCodecType() { return meshCodecType_; }
  auto& getMeshVertexTraversalMethod() { return meshVertexTraversalMethod_; }
  auto& getMeshPositionDequantizeFlag() { return meshPositionDequantizeFlag_; }
  auto& getMeshAttributeCount() { return meshAttributeCount_; }
  auto& getMeshAttributeType() { return meshAttributeType_; }
  auto& getMeshAttributeNumComponentsMinus1() {
    return meshAttributeNumComponentsMinus1_;
  }
  auto& getMeshAttributeDequantizeFlag() {
    return meshAttributeDequantizeFlag_;
  }

  auto& getMeshPositionEncodingParameters() {
    return meshPositionEncodingParameters_;
  }
  auto& getMeshPositionDequantizeParameters() {
    return meshPositionDequantizeParameters_;
  }
  auto& getMeshAttributesEncodingParameters() {
    return meshAttributesEncodingParameters_;
  }
  auto& getMeshAttributesDequantizeParameters() {
    return meshAttributesDequantizeParameters_;
  }
  auto& getMeshAttributesEncodingParameters(uint32_t index) {
    return meshAttributesEncodingParameters_[index];
  }
  auto& getMeshAttributesDequantizeParameters(uint32_t index) {
    return meshAttributesDequantizeParameters_[index];
  }
  auto& getMeshDeduplicateMethod() { return meshDeduplicateMethod_; }
  auto& getMeshReindexOutput() { return meshReindexOutput_; }


  auto getMeshNormalOctahedralFlag() const {
      return meshNormalOctahedralFlag_;
  }
  auto& getMeshNormalOctahedralFlag() { return meshNormalOctahedralFlag_; }

  auto getMeshEntropyPacketFlag() const { return meshEntropyPacketFlag_; }
  auto& getMeshEntropyPacketFlag() { return meshEntropyPacketFlag_; }

  auto& getMeshGeoEntropyPacketBufferSize() { return meshGeoEntropyPacketBufferSize_; }
  auto& getMeshGeoEntropyPacketBufferSize() const { return meshGeoEntropyPacketBufferSize_; }
  auto& getMeshGeoEntropyPacketBuffer() { return meshGeoEntropyPacketBuffer_; }
  auto& getMeshGeoEntropyPacketBuffer() const { return meshGeoEntropyPacketBuffer_; }

  auto& getMeshUvEntropyPacketBufferSize() { return meshUvEntropyPacketBufferSize_; }
  auto& getMeshUvEntropyPacketBufferSize() const { return meshUvEntropyPacketBufferSize_; }
  auto& getMeshUvEntropyPacketBuffer() { return meshUvEntropyPacketBuffer_; }
  auto& getMeshUvEntropyPacketBuffer() const { return meshUvEntropyPacketBuffer_; }

  auto& getMeshNrmEntropyPacketBufferSize() { return meshNrmEntropyPacketBufferSize_; }
  auto& getMeshNrmEntropyPacketBufferSize() const { return meshNrmEntropyPacketBufferSize_; }
  auto& getMeshNrmEntropyPacketBuffer() { return meshNrmEntropyPacketBuffer_; }
  auto& getMeshNrmEntropyPacketBuffer() const { return meshNrmEntropyPacketBuffer_; }

  auto& getMeshMtlEntropyPacketBufferSize() { return meshMtlEntropyPacketBufferSize_; }
  auto& getMeshMtlEntropyPacketBufferSize() const { return meshMtlEntropyPacketBufferSize_; }
  auto& getMeshMtlEntropyPacketBuffer() { return meshMtlEntropyPacketBuffer_; }
  auto& getMeshMtlEntropyPacketBuffer() const { return meshMtlEntropyPacketBuffer_; }

  void allocateAttributes(uint32_t attributeCount) {
    meshAttributesEncodingParameters_.resize(attributeCount);
    meshAttributesDequantizeParameters_.resize(attributeCount);
    meshAttributeType_.resize(attributeCount);
    meshAttributeNumComponentsMinus1_.resize(attributeCount);
    meshAttributeDequantizeFlag_.resize(attributeCount);
    meshNormalOctahedralFlag_.resize(attributeCount);
  }
  inline uint32_t getNumComponents(uint32_t index) {
    switch (meshAttributeType_[index]) {
    case MeshAttributeType::MESH_ATTR_TEXCOORD: return 2; break;
    case MeshAttributeType::MESH_ATTR_NORMAL: return 3; break;
    case MeshAttributeType::MESH_ATTR_COLOR: return 3; break;
    case MeshAttributeType::MESH_ATTR_MATERIAL_ID: return 1; break;
    case MeshAttributeType::MESH_ATTR_GENERIC:
      return meshAttributeNumComponentsMinus1_[index] + 1;
      break;
    default: return 0; break;
    }
    return 0;
  }
  inline uint32_t getNumResidualsComponents(uint32_t index) {
      return (meshNormalOctahedralFlag_[index]) ? 
          2 : getNumComponents(index);
  }
private:
  MeshCodecType                    meshCodecType_              = MeshCodecType::CODEC_TYPE_REVERSE;
  MeshVertexTraversalMethod        meshVertexTraversalMethod_  = MeshVertexTraversalMethod::MESH_EB_TRAVERSAL;
  bool                             meshPositionDequantizeFlag_ = 0;
  uint8_t                          meshAttributeCount_         = 0;
  std::vector<MeshAttributeType>   meshAttributeType_;
  std::vector<uint8_t>             meshAttributeNumComponentsMinus1_;
  std::vector<bool>                meshAttributeDequantizeFlag_;
  std::vector<bool>                meshNormalOctahedralFlag_;
  MeshPositionEncodingParameters   meshPositionEncodingParameters_;
  MeshPositionDequantizeParameters meshPositionDequantizeParameters_;
  std::vector<MeshAttributesEncodingParameters>
    meshAttributesEncodingParameters_;
 
  std::vector<MeshAttributesDequantizeParameters>
    meshAttributesDequantizeParameters_;
    MeshDeduplicateMethod meshDeduplicateMethod_ = MeshDeduplicateMethod::MESH_DEDUP_NONE;
    MeshVertexTraversalMethod meshReindexOutput_ = MeshVertexTraversalMethod::MESH_EB_TRAVERSAL;

  bool                             meshEntropyPacketFlag_ = 0;

  uint32_t                         meshGeoEntropyPacketBufferSize_ = 0;
  std::vector<uint8_t>             meshGeoEntropyPacketBuffer_;
  uint32_t                         meshUvEntropyPacketBufferSize_ = 0;
  std::vector<uint8_t>             meshUvEntropyPacketBuffer_;
  uint32_t                         meshNrmEntropyPacketBufferSize_ = 0;
  std::vector<uint8_t>             meshNrmEntropyPacketBuffer_;
  uint32_t                         meshMtlEntropyPacketBufferSize_ = 0;
  std::vector<uint8_t>             meshMtlEntropyPacketBuffer_;
};

};  // namespace eb
