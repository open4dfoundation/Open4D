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
#include "acDisplacementProfileTierLevel.hpp"
#include "acDisplacementRefListStruct.hpp"
#include "acDisplacementQuantizationParameters.hpp"
#include "acDisplacementVuiParameters.hpp"

namespace acdisplacement {

//H.8.1.3.1.1  General displ sequence parameter set RBSP syntax
class DisplSequenceParameterSetRbsp {
public:
  DisplSequenceParameterSetRbsp() : dspsMaxSubLayersMinus1_(0) {
    dspsMaxDecDisplFrameBufferingMinus1_.resize(dspsMaxSubLayersMinus1_ + 1,
                                                0);
    dspsMaxNumReorderFrames_.resize(dspsMaxSubLayersMinus1_ + 1, 0);
    dspsMaxLatencyIncreasePlus1_.resize(dspsMaxSubLayersMinus1_ + 1, 1);
  }
  ~DisplSequenceParameterSetRbsp() {}

  DisplSequenceParameterSetRbsp&
       operator=(const DisplSequenceParameterSetRbsp&) = default;
  void setDspsSequenceParameterSetId(uint8_t value) {
    dspsSequenceParameterSetId_ = value;
  }
  void setDspsMaxSubLayersMinus1(uint8_t value) {
    dspsMaxSubLayersMinus1_ = value;
    dspsMaxDecDisplFrameBufferingMinus1_.resize(dspsMaxSubLayersMinus1_ + 1,
                                                0);
    dspsMaxNumReorderFrames_.resize(dspsMaxSubLayersMinus1_ + 1, 0);
    dspsMaxLatencyIncreasePlus1_.resize(dspsMaxSubLayersMinus1_ + 1, 1);
  }
  void setDspsProfileTierLevel(DisplProfileTierLevel& value) {
    dspsProfileTierLevel_ = value;
  }
  void setDspsSingleDimensionFlag(bool value) {
    dspsSingleDimensionFlag_ = value;
  }
  void setDspsLog2MaxDisplFrameOrderCntLsbMinus4(uint32_t value) {
    dspsLog2MaxDisplFrameOrderCntLsbMinus4_ = value;
  }
  void setDspsMaxDecDisplFrameBufferingMinus1(uint32_t value, uint8_t index) {
    dspsMaxDecDisplFrameBufferingMinus1_[index] = value;
  }
  void setDspsMaxNumReorderFrames(uint32_t value, uint8_t index) {
    dspsMaxNumReorderFrames_[index] = value;
  }
  void setDspsMaxLatencyIncreasePlus1(uint32_t value, uint8_t index) {
    dspsMaxLatencyIncreasePlus1_[index] = value;
  }
  void setDspsLongTermRefDisplFramesFlag(bool value) {
    dspsLongTermRefDisplFramesFlag_ = value;
  }
  void setDspsNumRefDisplFrameListsInDsps(uint32_t value) {
    dspsNumRefDisplFrameListsInDsps_ = value;
  }
  void addDisplRefListStruct(DisplRefListStruct& value) {
    displRefListStruct_.push_back(value);
  }
  void setDspsExtensionPresentFlag(bool value) {
    dspsExtensionPresentFlag_ = value;
  }
  void setDspsVuiParametersPresentFlag(bool value) {
    dspsVuiParametersPresentFlag_ = value;
  }
  void setDspsExtensionCount(uint8_t value) {
    dsps_extension_count_ = value;
    dsps_extension_type_.resize(dsps_extension_count_, 0);
    dsps_extension_length_.resize(dsps_extension_count_, (uint16_t)0);
    dsps_extension_dataByte_.resize(dsps_extension_count_);
  }
  void setDspsExtensionType(int index, uint8_t value) {
    dsps_extension_type_[index] = value;
  }
  void setDspsExtensionLength(int index, uint8_t value) {
    dsps_extension_length_[index] = value;
  }
  void setDspsExtensionsLengthMinus1(uint32_t value) {
    dspsExtensionsLengthMinus1_ = value;
  }
  void setDspsExtensionDataByte(bool value) { dspsExtensionDataByte_ = value; }
  void setDspsGeometry3dBitdepthMinus1(uint8_t value) {
    dspsGeometry3dBitdepthMinus1_ = value;
  }
  void setDspsSubdivisionPresentFlag(bool value) {
    dspsSubdivisionPresentFlag_ = value;
  }
  void setDspsSubdivisionIterationCount(uint32_t value) {
    dspsSubdivisionIterationCount_ = value;
  }
  void setDspsDisplacementReferenceQPMinus49(int value) {
    displacementReferenceQPMinus49_ = value;
  }
  void setDspsInverseQuantizationOffsetPresentFlag(bool value) {
    displInverseQuantizationOffsetPresentFlag_ = value;
  }

  uint8_t getDspsSequenceParameterSetId() {
    return dspsSequenceParameterSetId_;
  }
  uint8_t getDspsMaxSubLayersMinus1() { return dspsMaxSubLayersMinus1_; }
  DisplProfileTierLevel& getDspsProfileTierLevel() {
    return dspsProfileTierLevel_;
  }
  bool     getDspsSingleDimensionFlag() { return dspsSingleDimensionFlag_; }
  uint32_t getDspsLog2MaxDisplFrameOrderCntLsbMinus4() {
    return dspsLog2MaxDisplFrameOrderCntLsbMinus4_;
  }
  uint32_t getDspsMaxDecDisplFrameBufferingMinus1(uint8_t index) {
    return dspsMaxDecDisplFrameBufferingMinus1_[index];
  }
  uint32_t getDspsMaxNumReorderFrames(uint8_t index) {
    return dspsMaxNumReorderFrames_[index];
  }
  uint32_t getDspsMaxLatencyIncreasePlus1(uint8_t index) {
    return dspsMaxLatencyIncreasePlus1_[index];
  }
  bool getDspsLongTermRefDisplFramesFlag() {
    return dspsLongTermRefDisplFramesFlag_;
  }
  uint32_t getDspsNumRefDisplFrameListsInDsps() {
    return dspsNumRefDisplFrameListsInDsps_;
  }
  std::vector<DisplRefListStruct>& getDisplRefListStruct() {
    return displRefListStruct_;
  }
  bool getDspsExtensionPresentFlag() { return dspsExtensionPresentFlag_; }
  bool getDspsVuiParametersPresentFlag() {
    return dspsVuiParametersPresentFlag_;
  }
  DisplVuiParameters& getVuiParameters() { return vp; }
  uint8_t getDspsExtensionCount() { return dsps_extension_count_; }
  uint8_t getDspsExtensionType(int index) {
    return dsps_extension_type_[index];
  }
  uint8_t getDspsExtensionLength(int index) {
    return dsps_extension_length_[index];
  }
  uint32_t getDspsExtensionsLengthMinus1() {
    return dspsExtensionsLengthMinus1_;
  }
  bool    getDspsExtensionDataByte() { return dspsExtensionDataByte_; }
  uint8_t getDspsGeometry3dBitdepthMinus1() {
    return dspsGeometry3dBitdepthMinus1_;
  }
  bool getDspsSubdivisionPresentFlag() { return dspsSubdivisionPresentFlag_; }
  uint32_t getDspsSubdivisionIterationCount() {
    return dspsSubdivisionIterationCount_;
  }
  uint8_t getDspsDisplacementReferenceQPMinus49() {
    return displacementReferenceQPMinus49_;
  }
  DisplQuantizationParameters& getDspsQuantizationParameters() {
    return dspsQuantizationParameters_;
  }
  bool getDspsInverseQuantizationOffsetPresentFlag() {
    return displInverseQuantizationOffsetPresentFlag_;
  }

  const uint8_t getDspsSequenceParameterSetId() const {
    return dspsSequenceParameterSetId_;
  }
  const uint8_t getDspsMaxSubLayersMinus1() const {
    return dspsMaxSubLayersMinus1_;
  }
  const DisplProfileTierLevel& getDspsProfileTierLevel() const {
    return dspsProfileTierLevel_;
  }
  const bool getDspsSingleDimensionFlag() const {
    return dspsSingleDimensionFlag_;
  }
  const uint32_t getDspsLog2MaxDisplFrameOrderCntLsbMinus4() const {
    return dspsLog2MaxDisplFrameOrderCntLsbMinus4_;
  }
  const uint32_t getDspsMaxDecDisplFrameBufferingMinus1(uint8_t index) const {
    return dspsMaxDecDisplFrameBufferingMinus1_[index];
  }
  const uint32_t getDspsMaxNumReorderFrames(uint8_t index) const {
    return dspsMaxNumReorderFrames_[index];
  }
  const uint32_t getDspsMaxLatencyIncreasePlus1(uint8_t index) const {
    return dspsMaxLatencyIncreasePlus1_[index];
  }
  const bool getDspsLongTermRefDisplFramesFlag() const {
    return dspsLongTermRefDisplFramesFlag_;
  }
  const uint32_t getDspsNumRefDisplFrameListsInDsps() const {
    return dspsNumRefDisplFrameListsInDsps_;
  }
  const std::vector<DisplRefListStruct>& getDisplRefListStruct() const {
    return displRefListStruct_;
  }
  const bool getDspsExtensionPresentFlag() const {
    return dspsExtensionPresentFlag_;
  }
  const bool getDspsVuiParametersPresentFlag() const {
    return dspsVuiParametersPresentFlag_;
  }
  const DisplVuiParameters& getVuiParameters() const { return vp; }
  const uint8_t getDspsExtensionCount() const { return dsps_extension_count_; }
  const uint8_t getDspsExtensionType(int index) const {
    return dsps_extension_type_[index];
  }
  const uint8_t getDspsExtensionLength(int index) const {
    return dsps_extension_length_[index];
  }
  const uint32_t getDspsExtensionsLengthMinus1() const {
    return dspsExtensionsLengthMinus1_;
  }
  const bool getDspsExtensionDataByte() const {
    return dspsExtensionDataByte_;
  }
  const uint8_t getDspsGeometry3dBitdepthMinus1() const {
    return dspsGeometry3dBitdepthMinus1_;
  }
  const bool getDspsSubdivisionPresentFlag() const {
    return dspsSubdivisionPresentFlag_;
  }
  const uint32_t getDspsSubdivisionIterationCount() const {
    return dspsSubdivisionIterationCount_;
  }
  const uint8_t getDspsDisplacementReferenceQPMinus49() const {
    return displacementReferenceQPMinus49_;
  }
  const DisplQuantizationParameters& getDspsQuantizationParameters() const {
    return dspsQuantizationParameters_;
  }
  const bool getDspsInverseQuantizationOffsetPresentFlag() const {
    return displInverseQuantizationOffsetPresentFlag_;
  }

private:
  uint8_t                         dspsSequenceParameterSetId_ = 0;
  uint8_t                         dspsMaxSubLayersMinus1_     = 0;
  DisplProfileTierLevel           dspsProfileTierLevel_;
  bool                            dspsSingleDimensionFlag_                = 0;
  uint32_t                        dspsLog2MaxDisplFrameOrderCntLsbMinus4_ = 0;
  std::vector<uint32_t>           dspsMaxDecDisplFrameBufferingMinus1_;
  std::vector<uint32_t>           dspsMaxNumReorderFrames_;
  std::vector<uint32_t>           dspsMaxLatencyIncreasePlus1_;
  bool                            dspsLongTermRefDisplFramesFlag_  = 0;
  uint32_t                        dspsNumRefDisplFrameListsInDsps_ = 0;
  std::vector<DisplRefListStruct> displRefListStruct_;
  bool                            dspsExtensionPresentFlag_     = 0;
  bool                            dspsVuiParametersPresentFlag_ = 0;
  DisplVuiParameters vp;
  uint8_t                         dsps_extension_count_         = 0;
  std::vector<uint8_t>            dsps_extension_type_;
  std::vector<uint16_t>           dsps_extension_length_;
  std::vector<std::vector<uint8_t>> dsps_extension_dataByte_;
  uint32_t                          dspsExtensionsLengthMinus1_     = 0;
  bool                              dspsExtensionDataByte_          = 0;
  uint8_t                           dspsGeometry3dBitdepthMinus1_   = 0;
  bool                              dspsSubdivisionPresentFlag_     = 0;
  uint32_t                          dspsSubdivisionIterationCount_  = 0;
  uint8_t                           displacementReferenceQPMinus49_ = 0;
  DisplQuantizationParameters       dspsQuantizationParameters_;
  bool displInverseQuantizationOffsetPresentFlag_ = 0;
};

};  // namespace vmesh
