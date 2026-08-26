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

#include "../baseMeshCommon.hpp"

namespace basemesh {

// Annex E: Supplemental enhancement information
// ISO/IEC 23002-5:F.2.1	General SEI message syntax  <=> 7.3.8	Supplemental enhancement
// information message syntax
class SEI {
public:
  SEI() {
    payloadSize_ = 0;
    byteStrData_.clear();
  }
  virtual ~SEI() { byteStrData_.clear(); }
  virtual BaseMeshSeiPayloadType getPayloadType() = 0;

  virtual std::vector<uint8_t>& getMD5ByteStrData() { return byteStrData_; }

  size_t getPayloadSize() { return payloadSize_; }
  void   setPayloadSize(size_t value) { payloadSize_ = value; }

protected:
  std::vector<uint8_t> byteStrData_;

private:
  size_t payloadSize_;
};

// ISO/IEC 23090-29:H.14.1.10  Basemesh Attribute transformation parameters SEI message syntax
class BMSEIAttributeTransformationParams : public SEI {
public:
  BMSEIAttributeTransformationParams() {}

  ~BMSEIAttributeTransformationParams() {
    attributeIdx_.clear();
    dimensionMinus1_.clear();
    for (auto& element : scaleParamsEnabledFlag_) { element.clear(); }
    for (auto& element : offsetParamsEnabledFlag_) { element.clear(); }
    for (auto& element : attributeScale_) { element.clear(); }
    for (auto& element : attributeOffset_) { element.clear(); }
    scaleParamsEnabledFlag_.clear();
    offsetParamsEnabledFlag_.clear();
    attributeScale_.clear();
    attributeOffset_.clear();
  }
  BMSEIAttributeTransformationParams&
  operator=(const BMSEIAttributeTransformationParams&) = default;

  BaseMeshSeiPayloadType getPayloadType() { return (BaseMeshSeiPayloadType)BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS; }
  void           allocate() {
    attributeIdx_.resize(numAttributeUpdates_ + 1, 0);
    dimensionMinus1_.resize(256 + 1, 0);
    scaleParamsEnabledFlag_.resize(256);
    offsetParamsEnabledFlag_.resize(256);
    attributeScale_.resize(256);
    attributeOffset_.resize(256);
  }
  void allocate(size_t i) {
    scaleParamsEnabledFlag_[i].resize(dimensionMinus1_[i] + 1, false);
    offsetParamsEnabledFlag_[i].resize(dimensionMinus1_[i] + 1, false);
    attributeScale_[i].resize(dimensionMinus1_[i] + 1, 0);
    attributeOffset_[i].resize(dimensionMinus1_[i] + 1, 0);
  }

  auto  getCancelFlag() const { return cancelFlag_; }
  auto  getNumAttributeUpdates() const { return numAttributeUpdates_; }
  auto& AttributeIdx() const { return attributeIdx_; }
  auto& getDimensionMinus() const { return dimensionMinus1_; }
  auto& getScaleParamsEnabledFlag() const { return scaleParamsEnabledFlag_; }
  auto& getOffsetParamsEnabledFlag() const { return offsetParamsEnabledFlag_; }
  auto& getAttributeScale() const { return attributeScale_; }
  auto& getAttributeOffset() const { return attributeOffset_; }
  auto  getPersistenceFlag() const { return persistenceFlag_; }
  auto  getAttributeIdx(size_t i) const { return attributeIdx_[i]; }
  auto  getDimensionMinus1(size_t i) const { return dimensionMinus1_[i]; }
  auto  getScaleParamsEnabledFlag(size_t i, size_t j) const {
    return scaleParamsEnabledFlag_[i][j];
  }
  auto getOffsetParamsEnabledFlag(size_t i, size_t j) const {
    return offsetParamsEnabledFlag_[i][j];
  }
  auto getAttributeScale(size_t i, size_t j) const {
    return attributeScale_[i][j];
  }
  auto getAttributeOffset(size_t i, size_t j) const {
    return attributeOffset_[i][j];
  }

  auto& getCancelFlag() { return cancelFlag_; }
  auto& getNumAttributeUpdates() { return numAttributeUpdates_; }
  auto& AttributeIdx() { return attributeIdx_; }
  auto& getDimensionMinus() { return dimensionMinus1_; }
  auto& getScaleParamsEnabledFlag() { return scaleParamsEnabledFlag_; }
  auto& getOffsetParamsEnabledFlag() { return offsetParamsEnabledFlag_; }
  auto& getAttributeScale() { return attributeScale_; }
  auto& getAttributeOffset() { return attributeOffset_; }
  auto& getPersistenceFlag() { return persistenceFlag_; }
  auto& getAttributeIdx(size_t i) { return attributeIdx_[i]; }
  auto& getDimensionMinus1(size_t i) { return dimensionMinus1_[i]; }
  auto& getScaleParamsEnabledFlag(size_t i, size_t j) {
    return scaleParamsEnabledFlag_[i][j];
  }
  auto& getOffsetParamsEnabledFlag(size_t i, size_t j) {
    return offsetParamsEnabledFlag_[i][j];
  }
  auto& getAttributeScale(size_t i, size_t j) { return attributeScale_[i][j]; }
  auto& getAttributeOffset(size_t i, size_t j) {
    return attributeOffset_[i][j];
  }

private:
  bool                               cancelFlag_          = false;
  int32_t                            numAttributeUpdates_ = 0;
  std::vector<uint8_t>               attributeIdx_;
  std::vector<uint8_t>               dimensionMinus1_;
  std::vector<std::vector<uint8_t>>  scaleParamsEnabledFlag_;
  std::vector<std::vector<uint8_t>>  offsetParamsEnabledFlag_;
  std::vector<std::vector<uint32_t>> attributeScale_;
  std::vector<std::vector<int32_t>>  attributeOffset_;
  bool                               persistenceFlag_ = true;
};

class BaseMeshSei {
public:
  BaseMeshSei() {
    seiPrefix_.clear();
    seiSuffix_.clear();
  }
  ~BaseMeshSei() {
    seiPrefix_.clear();
    seiSuffix_.clear();
  }

  SEI& addSei(BaseMeshNalUnitType nalUnitType, BaseMeshSeiPayloadType payloadType) {
    std::shared_ptr<SEI> sharedPtr;
    switch (payloadType) {
    case BASEMESH_BUFFERING_PERIOD:
      break;
    case BASEMESH_FRAME_TIMING:
      break;
    case BASEMESH_FILLER_PAYLOAD:
      break;
    case BASEMESH_USER_DATAREGISTERED_ITUTT35:
      break;
    case BASEMESH_USER_DATA_UNREGISTERED:
      break;
    case BASEMESH_RECOVERY_POINT:
      break;
    case BASEMESH_NO_RECONSTRUCTION:
      break;
    case BASEMESH_TIME_CODE:
      break;
    case BASEMESH_SEI_MANIFEST:
      break;
    case BASEMESH_SEI_PREFIX_INDICATION:
      break;
    case BASEMESH_COMPONENT_CODEC_MAPPING:
      break;
    case BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS:
      sharedPtr = std::make_shared<BMSEIAttributeTransformationParams>();
      break;
    default:
      fprintf(stderr, "basemesh SEI payload type not supported \n");
      exit(-1);
      break;
    }
    if (nalUnitType == BASEMESH_NAL_PREFIX_ESEI
        || nalUnitType == BASEMESH_NAL_PREFIX_NSEI) {
      seiPrefix_.push_back(sharedPtr);
      return *(seiPrefix_.back().get());
    } else if (nalUnitType == BASEMESH_NAL_SUFFIX_ESEI
               || nalUnitType == BASEMESH_NAL_SUFFIX_NSEI) {
      seiSuffix_.push_back(sharedPtr);
      return *(seiSuffix_.back().get());
    } else {
      fprintf(stderr, "Nal unit type of basemesh SEI not correct\n");
      exit(-1);
    }
    return *(sharedPtr);
  }

  SEI* getSei(BaseMeshNalUnitType nalUnitType, BaseMeshSeiPayloadType payloadType) {
    auto& seis = (nalUnitType == BASEMESH_NAL_PREFIX_ESEI
                  || nalUnitType == BASEMESH_NAL_PREFIX_NSEI)
                   ? seiPrefix_
                   : seiSuffix_;
    for (auto& sei : seis) {
      if (sei->getPayloadType() == payloadType) { return sei.get(); }
    }
    assert(0);
    return (SEI*)nullptr;
  }

  SEI& addSeiPrefix(BaseMeshSeiPayloadType payloadType, bool essensial) {
    return addSei(essensial ? BASEMESH_NAL_PREFIX_ESEI : BASEMESH_NAL_PREFIX_NSEI,
                  payloadType);
  }

  SEI& addSeiSuffix(BaseMeshSeiPayloadType payloadType, bool essensial) {
    return addSei(essensial ? BASEMESH_NAL_SUFFIX_ESEI : BASEMESH_NAL_SUFFIX_NSEI,
                  payloadType);
  }

  std::vector<std::shared_ptr<basemesh::SEI>>& getSeiPrefix() { return seiPrefix_; }
  std::vector<std::shared_ptr<basemesh::SEI>>& getSeiSuffix() { return seiSuffix_; }
  basemesh::SEI& getSeiPrefix(size_t index) { return *(seiPrefix_[index]); }
  basemesh::SEI& getSeiSuffix(size_t index) { return *(seiSuffix_[index]); }

private:
  std::vector<std::shared_ptr<basemesh::SEI>> seiPrefix_;
  std::vector<std::shared_ptr<basemesh::SEI>> seiSuffix_;
};

};  // namespace vmesh
