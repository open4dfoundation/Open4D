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
#include "vdmcLiftingTransformParameters.hpp"
#include "vdmcQuantizationParameters.hpp"
#include "vuiParameters.hpp"

namespace atlas {

class AspsVdmcExtension {
public:
  AspsVdmcExtension() {}
  ~AspsVdmcExtension() {}
  AspsVdmcExtension& operator=(const AspsVdmcExtension&) = default;

  // subdivision
  void setAsveSubdivisionIterationCount(uint32_t value) {
    asveSubdivisionIterationCount_ = value;
  }
  uint32_t getAsveSubdivisionIterationCount() const {
    return asveSubdivisionIterationCount_;
  }
  void setAsveLodAdaptiveSubdivisionFlag(bool value) {
    asveLodAdaptiveSubdivisionFlag_ = value;
  }
  bool getAsveLodAdaptiveSubdivisionFlag() const {
    return asveLodAdaptiveSubdivisionFlag_;
  }
  void setAsveSubdivisionMethod(int subdivIterIdx, uint8_t value) {
    if (subdivIterIdx >= asveSubdivisionMethod_.size())
      asveSubdivisionMethod_.resize(asveSubdivisionMethod_.size() + 1);
    asveSubdivisionMethod_[subdivIterIdx] = value;
  }
  std::vector<uint8_t>& getAsveSubdivisionMethod() {
    return asveSubdivisionMethod_;
  }
  void setAsveEdgeBasedSubdivisionFlag(bool value) {
    asveEdgeBasedSubdivisionFlag_ = value;
  }
  bool getAsveEdgeBasedSubdivisionFlag() const {
    return asveEdgeBasedSubdivisionFlag_;
  }
  void setAsveSubdivisionMinEdgeLength(uint32_t value) {
    asveSubdivisionMinEdgeLength_ = value;
  }
  uint32_t getAsveSubdivisionMinEdgeLength() const {
    return asveSubdivisionMinEdgeLength_;
  }
  void setAsve1DDisplacementFlag(bool value) {
    asve1dDisplacementFlag_ = value;
    aspsDispComponents = asve1dDisplacementFlag_ ? 1 : 3;
  }
  bool getAsve1DDisplacementFlag() const { return asve1dDisplacementFlag_; }
  uint8_t getAspsDispComponents() const { return aspsDispComponents; }
  void setAsveInterpolateSubdividedNormalsFlag(bool value) {
    asveInterpolateSubdividedNormalsFlag_ = value;
  }
  bool getAsveInterpolateSubdividedNormalsFlag() const {
    return asveInterpolateSubdividedNormalsFlag_;
  }
  // quantization
  void setAsveQuantizationParametersPresentFlag(bool value) {
    asveQuantizationParametersPresentFlag_ = value;
  }
  bool getAsveQuantizationParametersPresentFlag() const {
    return asveQuantizationParametersPresentFlag_;
  }
  void  setAsveInverseQuantizationOffsetPresentFlag(bool value) { asveInverseQuantizationOffsetPresentFlag_ = value; }
  bool  getAsveInverseQuantizationOffsetPresentFlag() const { return asveInverseQuantizationOffsetPresentFlag_; }
  void setAsveDisplacementReferenceQPMinus49(int value) {
    asveDisplacementReferenceQPMinus49_ = value;
  }
  int8_t getAsveDisplacementReferenceQPMinus49() const {
    return asveDisplacementReferenceQPMinus49_;
  }
  VdmcQuantizationParameters& getAsveQuantizationParameters() {
    return aspsQuantizationParameters_;
  }
  const VdmcQuantizationParameters& getAsveQuantizationParameters() const {
    return aspsQuantizationParameters_;
  }
  //lifting parameters
  void setAsveTransformMethod(uint8_t value) { asveTransformMethod_ = value; }
  uint8_t getAsveTransformMethod() const { return asveTransformMethod_; }
  void setAsveLiftingOffsetPresentFlag(bool value) {
    asveLiftingOffsetPresentFlag_ = value;
  }
  bool getAsveLiftingOffsetPresentFlag() const { return asveLiftingOffsetPresentFlag_; }
  void setAsveDirectionalLiftingPresentFlag(bool value) {
    asveDirectionalLiftingPresentFlag_ = value;
  }
  bool getAsveDirectionalLiftingPresentFlag() const {
    return asveDirectionalLiftingPresentFlag_;
  }
  VdmcLiftingTransformParameters& getAspsExtLtpDisplacement() {
    return aspsExtLtpDisplacement_;
  }
  const VdmcLiftingTransformParameters& getAspsExtLtpDisplacement() const {
    return aspsExtLtpDisplacement_;
  }
  //attribute
  void setAsveAttributeInformationPresentFlag(bool value) {
    asveAttributeInformationPresentFlag_ = value;
  }
  bool getAsveAttributeInformationPresentFlag() const {
    return asveAttributeInformationPresentFlag_;
  }
  void setAsveConsistentAttributeFrameFlag(bool value) {
    asveConsistentAttributeFrameFlag_ = value;
  }
  bool getAsveConsistentAttributeFrameFlag() const {
    return asveConsistentAttributeFrameFlag_;
  }
  void setAsveAttributeFrameSizeCount(uint8_t value) {
    asveAttributeFrameSizeCount_ = value;
    asveAttributeFrameWidth_.resize(asveAttributeFrameSizeCount_, 0);
    asveAttributeFrameHeight_.resize(asveAttributeFrameSizeCount_, 0);
    asveAttributeSubtextureEnabledFlag_.resize(asveAttributeFrameSizeCount_,
      0);
    aspsAttributeNominalFrameSizeCount = asveAttributeFrameSizeCount_;
  }
  uint8_t getAsveAttributeFrameSizeCount() const {
    return asveAttributeFrameSizeCount_;
  }
  uint8_t getAspsAttributeNominalFrameSizeCount() const {
    return aspsAttributeNominalFrameSizeCount;
  }
  void setAsveAttributeFrameWidth(int attrIdx, uint32_t value) {
    asveAttributeFrameWidth_[attrIdx] = value;
  }
  std::vector<uint32_t> getAsveAttributeFrameWidth() const {
    return asveAttributeFrameWidth_;
  }
  void setAsveAttributeFrameHeight(int attrIdx, uint32_t value) {
    asveAttributeFrameHeight_[attrIdx] = value;
  }
  std::vector<uint32_t> getAsveAttributeFrameHeight() const {
    return asveAttributeFrameHeight_;
  }
  void setAsveAttributeSubtextureEnabledFlag(int attrIdx, bool value) {
    asveAttributeSubtextureEnabledFlag_[attrIdx] = value;
  }
  const std::vector<bool>& getAsveAttributeSubtextureEnabledFlag() const {
    return asveAttributeSubtextureEnabledFlag_;
  }
  // displacement
  void setAsveDisplacementIdPresentFlag(bool value) {
    asveDisplacementIdPresentFlag_ = value;
  }

  bool getAsveDisplacementIdPresentFlag() const {
    return asveDisplacementIdPresentFlag_;
  }
  void setAsveLodPatchesEnableFlag(bool value) { 
    asveLodPatchesEnableFlag_ = value; 
  }
  bool getAsveLodPatchesEnableFlag() const {
    return asveLodPatchesEnableFlag_;
  }
  void setAsvePackingMethod(bool value) {
    asvePackingMethod_ = value;
  }
  bool getAsvePackingMethod() const { return asvePackingMethod_; }
  //orthoAtlas
  void setAsveProjectionTexcoordEnableFlag(bool value) {
    asveProjectionTexcoordEnableFlag_ = value;
  }
  bool getAsveProjectionTexcoordEnableFlag() const {
    return asveProjectionTexcoordEnableFlag_;
  }
  void setAsveProjectionTexcoordMappingAttributeIndexPresentFlag(bool value) {
    asveProjectionTexcoordMappingAttributeIndexPresentFlag_ = value;
  }
  bool getAsveProjectionTexcoordMappingAttributeIndexPresentFlag() const {
    return asveProjectionTexcoordMappingAttributeIndexPresentFlag_;
  }
  void setAsveProjectionTexcoordMappingAttributeIndex(uint8_t value) {
    asveProjectionTexcoordMappingAttributeIndex_ = value;
  }
  uint8_t getAsveProjectionTexcoordMappingAttributeIndex() const {
    return asveProjectionTexcoordMappingAttributeIndex_;
  }
  void setAsveProjectionTexcoordOutputBitdepthMinus1(uint8_t value) {
    asveProjectionTexcoordOutputBitdepthMinus1_ = value;
  }
  uint8_t getAsveProjectionTexcoordOutputBitdepthMinus1() const {
    return asveProjectionTexcoordOutputBitdepthMinus1_;
  }
  void setAsveProjectionTexcoordBboxBiasEnableFlag(bool value) {
    asveProjectionTexcoordBboxBiasEnableFlag_ = value;
  }
  bool getAsveProjectionTexcoordBboxBiasEnableFlag() const{
    return asveProjectionTexcoordBboxBiasEnableFlag_;
  }
  void setAsveProjectionTexCoordUpscaleFactorMinus1(uint64_t value){
    asveProjectionTexcoordUpscaleFactorMinus1_ = value;
    aspsProjectionTexcoordUpscaleFactor = asveProjectionTexcoordUpscaleFactorMinus1_ + 1;
  }
  uint64_t getAsveProjectionTexCoordUpscaleFactorMinus1() const {
    return asveProjectionTexcoordUpscaleFactorMinus1_;
  }
  double getAspsProjectionTexcoordUpscaleFactor() const {
    return aspsProjectionTexcoordUpscaleFactor;
  }
  void setAsveProjectionTexcoordLog2DownscaleFactor(uint64_t value) {
    asveProjectionTexcoordLog2DownscaleFactor_ = value;
    aspsProjectionTexcoordDownscaleFactor = pow(2, asveProjectionTexcoordLog2DownscaleFactor_);
  }
  uint64_t getAsveProjectionTexcoordLog2DownscaleFactor() const {
    return asveProjectionTexcoordLog2DownscaleFactor_;
  }
  double getAspsProjectionTexcoordDownscaleFactor() const {
    return aspsProjectionTexcoordDownscaleFactor;
  }
  void setAsveProjectionRawTextcoordPresentFlag(bool value) {
    asveProjectionRawTextcoordPresentFlag_ = value;
  }
  bool getAsveProjectionRawTextcoordPresentFlag() const {
    return asveProjectionRawTextcoordPresentFlag_;
  }
  void setAsveProjectionRawTextcoordBitdepthMinus1(uint32_t value) {
    asveProjectionRawTextcoordBitdepthMinus1_ = value;
  }
  uint32_t getAsveProjectionRawTextcoordBitdepthMinus1() const {
    return asveProjectionRawTextcoordBitdepthMinus1_;
  }
  // vui
  void setAsveVdmcVuiParametersPresentFlag(bool value) {
    asveVdmcVuiParameterPresentFlag_ = value;
  }
  bool getAsveVdmcVuiParametersPresentFlag() const {
    return asveVdmcVuiParameterPresentFlag_;
  }
  VdmcVuiParameters& getAsveVdmcVuiParameters() {
    return asveVdmcVuiParameters_;
  }
  const VdmcVuiParameters& getAsveVdmcVuiParameters() const {
    return asveVdmcVuiParameters_;
  }

private:
  uint32_t             asveSubdivisionIterationCount_  = 0;
  bool                 asveLodAdaptiveSubdivisionFlag_       = false;
  std::vector<uint8_t> asveSubdivisionMethod_;
  bool                 asveEdgeBasedSubdivisionFlag_         = false;
  uint32_t             asveSubdivisionMinEdgeLength_         = 0;
  bool                 asve1dDisplacementFlag_               = false;
  uint8_t              aspsDispComponents                    = 0; // variable NOT syntax element
  bool                 asveInterpolateSubdividedNormalsFlag_ = false;
  // quantization
  bool                       asveQuantizationParametersPresentFlag_    = true;  
  bool                       asveInverseQuantizationOffsetPresentFlag_ = false;
  int8_t                     asveDisplacementReferenceQPMinus49_       = 0;
  VdmcQuantizationParameters aspsQuantizationParameters_;
  //lifting parameters
  uint8_t                        asveTransformMethod_   = 0;
  bool                           asveLiftingOffsetPresentFlag_      = false;
  bool                           asveDirectionalLiftingPresentFlag_ = false;
  VdmcLiftingTransformParameters aspsExtLtpDisplacement_;
  //attribute
  bool                  asveAttributeInformationPresentFlag_ = false;
  bool                  asveConsistentAttributeFrameFlag_    = true; 
  uint8_t               asveAttributeFrameSizeCount_         = 1;
  uint8_t               aspsAttributeNominalFrameSizeCount   = 0; // variable NOT syntax element
  std::vector<uint32_t> asveAttributeFrameWidth_;
  std::vector<uint32_t> asveAttributeFrameHeight_;
  std::vector<bool>     asveAttributeSubtextureEnabledFlag_;  
  // displacement
  bool asveDisplacementIdPresentFlag_ = false;
  bool asveLodPatchesEnableFlag_      = false;
  bool asvePackingMethod_             = false;
  // orthoAtlas
  bool    asveProjectionTexcoordEnableFlag_            = false;
  bool    asveProjectionTexcoordMappingAttributeIndexPresentFlag_ = true;
  uint8_t asveProjectionTexcoordMappingAttributeIndex_ = 0;
  uint8_t asveProjectionTexcoordOutputBitdepthMinus1_  = 0;
  bool    asveProjectionTexcoordBboxBiasEnableFlag_    = false;
  uint64_t asveProjectionTexcoordUpscaleFactorMinus1_  = 0;
  double  aspsProjectionTexcoordUpscaleFactor         = 1.0; // variable NOT syntax element
  uint8_t asveProjectionTexcoordLog2DownscaleFactor_  = 0;
  double  aspsProjectionTexcoordDownscaleFactor       = 1.0; // variable NOT syntax element
  bool    asveProjectionRawTextcoordPresentFlag_      = false;
  uint32_t asveProjectionRawTextcoordBitdepthMinus1_   = 0;
  // vui
  bool              asveVdmcVuiParameterPresentFlag_ = false;
  VdmcVuiParameters asveVdmcVuiParameters_;
};

};  // namespace atlas
