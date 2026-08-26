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
#include "atlasFrameMeshInformation.hpp"
#include "atlasFrameTileInformation.hpp"
#include "atlasFrameTileAttributeInformation.hpp"
#include "vdmcLiftingTransformParameters.hpp"
#include "vdmcQuantizationParameters.hpp"

namespace atlas {

class AfpsVdmcExtension {
public:
  AfpsVdmcExtension() {}
  ~AfpsVdmcExtension() {
    atlasFrameTileAttributeInformation_.clear();
    projectionTextcoordPresentFlag_.clear();
    projectionTextcoordWidth_.clear();
    projectionTextcoordHeight_.clear();
    projectionTextcoordGutter_.clear();
  }
  AfpsVdmcExtension& operator=(const AfpsVdmcExtension&) = default;

  //get function
  bool getAfveOverridenFlag() { return afveOverridenFlag_; }
  bool getAfveSubdivisionIterationCountPresentFlag() {
    return afveSubdivisionIterationCountPresentFlag_;
  }
  bool getAfveSubdivisionMethodPresentFlag() {
    return AfveSubdivisionMethodPresentFlag_;
  }
  bool getAfveQuantizationParametersPresentFlag() {
    return afveQuantizationParametersPresentFlag_;
  }
  bool getAfveQuantizationParametersPresentFlag() const {
    return afveQuantizationParametersPresentFlag_;
  }
  bool getAfveTransformMethodPresentFlag() {
    return AfveTransformMethodPresentFlag_;
  }
  bool getAfveTransformParametersPresentFlag() {
    return afveTransformParametersPresentFlag_;
  }
  std::vector<uint8_t>& getAfveSubdivisionMethod() {
    return afveSubdivisionMethod_;
  }
  bool getAfveLodAdaptiveSubdivisionFlag() {
    return afveLodAdaptiveSubdivisionFlag_;
  }
  uint32_t getAfveSubdivisionIterationCount() {
    return afveSubdivisionIterationCount_;
  }
  uint32_t getAfveSubdivisionIterationCount() const {
    return afveSubdivisionIterationCount_;
  }
  bool getAfveEdgeBasedSubdivisionFlag() const {
    return afveEdgeBasedSubdivisionFlag_;
  }
  uint32_t getAfveSubdivisionMinEdgeLength() {
    return afveSubdivisionMinEdgeLength_;
  }
  //bool                       getAfveDisplacementCoordinateSystem() { return afveDisplacementCoordinateSystem_; }
  const VdmcQuantizationParameters& getAfveQuantizationParameters() const {
    return afveQuantizationParameters_;
  }
  uint8_t getAfveTransformMethod() { return afveTransformMethod_; }
  bool    getAfveConsistentTilingAccrossAttributesFlag() const {
    return afveConsistentTilingAccrosAttributesVideoFlag_;
  }
  int32_t getAfveReferenceAttributeIdx() const {
    return afveReferenceAttributeIdx_;
  }
  auto& getAfpsLtpDisplacement() { return afpsExtLtpDisplacement_; }
  auto& getAtlasFrameTileAttributeInformation(int attrIdx) {
    if (attrIdx >= atlasFrameTileAttributeInformation_.size())
      atlasFrameTileAttributeInformation_.resize(
        atlasFrameTileAttributeInformation_.size() + 1);
    return atlasFrameTileAttributeInformation_[attrIdx];
  }
  auto& getAtlasFrameTileAttributeInformation() {
    return atlasFrameTileAttributeInformation_;
  }

  const auto& getAtlasFrameTileAttributeInformation(int attrIdx) const {
    return atlasFrameTileAttributeInformation_[attrIdx];
  }
  auto& getAtlasFrameMeshInformation() { return atlasFrameMeshInformation_; }
  const auto& getAtlasFrameMeshInformation() const {
    return atlasFrameMeshInformation_;
  }
  //set function
  void setAfveOverridenFlag(bool value) { afveOverridenFlag_ = value; }
  void setAfveSubdivisionIterationCountPresentFlag(bool value) {
    afveSubdivisionIterationCountPresentFlag_ = value;
  }
  void setAfveSubdivisionMethodPresentFlag(bool value) {
    AfveSubdivisionMethodPresentFlag_ = value;
  }
  void setAfveQuantizationParametersPresentFlag(bool value) {
    afveQuantizationParametersPresentFlag_ = value;
  }
  void setAfveTransformMethodPresentFlag(bool value) {
    AfveTransformMethodPresentFlag_ = value;
  }
  void setAfveTransformParametersPresentFlag(bool value) {
    afveTransformParametersPresentFlag_ = value;
  }
  void setAfveSubdivisionMethod(int SubdivInterIdx, uint8_t value) {
    if (SubdivInterIdx >= afveSubdivisionMethod_.size())
      afveSubdivisionMethod_.resize(afveSubdivisionMethod_.size() + 1);
    afveSubdivisionMethod_[SubdivInterIdx] = value;
  }
  void setAfveLodAdaptiveSubdivisionFlag(bool value) {
    afveLodAdaptiveSubdivisionFlag_ = value;
  }
  void setAfveSubdivisionIterationCount(uint32_t value) {
    afveSubdivisionIterationCount_ = value;
  }
  void setAfveEdgeBasedSubdivisionFlag(bool value) {
    afveEdgeBasedSubdivisionFlag_ = value;
  }
  void setAfveSubdivisionMinEdgeLength(uint32_t value) {
    afveSubdivisionMinEdgeLength_ = value;
  }
  //void                       setAfveDisplacementCoordinateSystem(bool  value) { afveDisplacementCoordinateSystem_ = value; }
  VdmcQuantizationParameters& getAfveQuantizationParameters() {
    return afveQuantizationParameters_;
  }
  void setAfveTransformMethod(uint8_t value) { afveTransformMethod_ = value; }
  void setAfveConsistentTilingAccrossAttributesFlag(bool value) {
    afveConsistentTilingAccrosAttributesVideoFlag_ = value;
  }
  void setAfveReferenceAttributeIdx(int32_t i) {
    afveReferenceAttributeIdx_ = i;
  }
  //orthoAtlas
  auto& getProjectionTextcoordPresentFlag(int index) {
    if (index >= projectionTextcoordPresentFlag_.size())
      projectionTextcoordPresentFlag_.resize(
        projectionTextcoordPresentFlag_.size() + 1);
    return projectionTextcoordPresentFlag_[index];
  }
  auto& getProjectionTextcoordWidth(int index) {
    if (index >= projectionTextcoordWidth_.size())
      projectionTextcoordWidth_.resize(projectionTextcoordWidth_.size() + 1);
    return projectionTextcoordWidth_[index];
  }
  auto& getProjectionTextcoordHeight(int index) {
    if (index >= projectionTextcoordHeight_.size())
      projectionTextcoordHeight_.resize(projectionTextcoordHeight_.size() + 1);
    return projectionTextcoordHeight_[index];
  }
  auto& getProjectionTextcoordGutter(int index) {
    if (index >= projectionTextcoordGutter_.size())
      projectionTextcoordGutter_.resize(projectionTextcoordGutter_.size() + 1);
    return projectionTextcoordGutter_[index];
  }
  const auto& getProjectionTextcoordPresentFlag(int index) const {
    return projectionTextcoordPresentFlag_[index];
  }
  const auto& getProjectionTextcoordWidth(int index) const {
    return projectionTextcoordWidth_[index];
  }
  const auto& getProjectionTextcoordHeight(int index) const {
    return projectionTextcoordHeight_[index];
  }
  const auto& getProjectionTextcoordGutter(int index) const {
    return projectionTextcoordGutter_[index];
  }
  void allocateStructures(int size) {
    projectionTextcoordPresentFlag_.resize(size);
    projectionTextcoordWidth_.resize(size);
    projectionTextcoordHeight_.resize(size);
    projectionTextcoordGutter_.resize(size);
  }

private:
  bool                 afveOverridenFlag_                       = 0;
  bool                 afveSubdivisionIterationCountPresentFlag_ = 0;
  uint32_t             afveSubdivisionIterationCount_ = 0;
  bool                 AfveTransformMethodPresentFlag_ = 0;
  bool                 AfveSubdivisionMethodPresentFlag_         = 0;
  bool                 afveQuantizationParametersPresentFlag_    = 0;
  bool                 afveTransformParametersPresentFlag_       = 0;
  bool                 afveLodAdaptiveSubdivisionFlag_ = 0;
  std::vector<uint8_t> afveSubdivisionMethod_;
  bool                 afveEdgeBasedSubdivisionFlag_   = 0;
  uint32_t             afveSubdivisionMinEdgeLength_   = 0;
  //bool                     afveDisplacementCoordinateSystem_ = 0;
  VdmcQuantizationParameters     afveQuantizationParameters_;
  uint8_t                        afveTransformMethod_ = 0;
  VdmcLiftingTransformParameters afpsExtLtpDisplacement_;
  bool    afveConsistentTilingAccrosAttributesVideoFlag_ = false;
  int32_t afveReferenceAttributeIdx_                     = 0;
  std::vector<AtlasFrameTileAttributeInformation>
                            atlasFrameTileAttributeInformation_;
  AtlasFrameMeshInformation atlasFrameMeshInformation_;
  //orthoAtlas
  std::vector<uint8_t>  projectionTextcoordPresentFlag_;
  std::vector<uint32_t> projectionTextcoordWidth_;
  std::vector<uint32_t> projectionTextcoordHeight_;
  std::vector<uint32_t> projectionTextcoordGutter_;
};

};  // namespace vmesh
