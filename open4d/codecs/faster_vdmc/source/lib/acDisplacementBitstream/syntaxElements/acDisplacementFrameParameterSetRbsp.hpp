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
#include <vector>
#include "acDisplacementInformation.hpp"
#include "acDisplacementQuantizationParameters.hpp"

namespace acdisplacement {

//disp FPS
class DisplFrameParameterSetRbsp {
public:
 DisplFrameParameterSetRbsp() {}
 ~DisplFrameParameterSetRbsp(){}
 DisplFrameParameterSetRbsp& operator=( const DisplFrameParameterSetRbsp& ) = default;
 void                       setDfpsDisplSequenceParameterSetId(uint8_t  value) { dfpsDisplSequenceParameterSetId_ = value; }
 void                       setDfpsDisplFrameParameterSetId(uint8_t  value) { dfpsDisplFrameParameterSetId_ = value; }
 void                       setDfpsOutputFlagPresentFlag(bool  value) { dfpsOutputFlagPresentFlag_ = value; }
 void                       setDfpsNumRefIdxDefaultActiveMinus1(uint32_t  value) { dfpsNumRefIdxDefaultActiveMinus1_ = value; }
 void                       setDfpsAdditionalLtDfocLsbLen(uint32_t  value) { dfpsAdditionalLtDfocLsbLen_ = value; }
 void                       setDfpsExtensionPresentFlag(bool  value) { dfpsExtensionPresentFlag_ = value; }
 void                       setDfpsExtension8bits(uint32_t  value) { dfpsExtension8bits_ = value; }
 void                       setDfpsExtensionDataFlag(bool  value) { dfpsExtensionDataFlag_ = value; }
 void                       setDfpsOverridenFlag(bool  value) { dfpsOverridenFlag_ = value; }
 void                       setDfpsSubdivisionEnableFlag(bool  value) { dfpsSubdivisionEnableFlag_ = value; }
 void                       setDfpsQuantizationParametersEnableFlag(bool  value) { dfpsQuantizationParametersEnableFlag_ = value; }
 void                       setDfpsSubdivisionIterationCount(uint32_t  value) { dfpsSubdivisionIterationCount_ = value; }

 uint8_t                    getDfpsDisplSequenceParameterSetId() { return dfpsDisplSequenceParameterSetId_; }
 uint8_t                    getDfpsDisplFrameParameterSetId() { return dfpsDisplFrameParameterSetId_; }
 bool                       getDfpsOutputFlagPresentFlag() { return dfpsOutputFlagPresentFlag_; }
 uint32_t                   getDfpsNumRefIdxDefaultActiveMinus1() { return dfpsNumRefIdxDefaultActiveMinus1_; }
 uint32_t                   getDfpsAdditionalLtDfocLsbLen() { return dfpsAdditionalLtDfocLsbLen_; }
 bool                       getDfpsExtensionPresentFlag() { return dfpsExtensionPresentFlag_; }
 uint32_t                   getDfpsExtension8bits() { return dfpsExtension8bits_; }
 bool                       getDfpsExtensionDataFlag() { return dfpsExtensionDataFlag_; }
 DisplInformation&          getDisplInformation()  { return displInformation_; }
 bool                       getDfpsOverridenFlag() { return dfpsOverridenFlag_; }
 bool                       getDfpsSubdivisionEnableFlag() { return dfpsSubdivisionEnableFlag_; }
 bool                       getDfpsQuantizationParametersEnableFlag() { return dfpsQuantizationParametersEnableFlag_; }
 uint32_t                   getDfpsSubdivisionIterationCount() { return dfpsSubdivisionIterationCount_; }
 DisplQuantizationParameters& getDfpsQuantizationParameters() { return dfpsQuantizationParameters_; }

 const uint8_t              getDfpsDisplSequenceParameterSetId() const { return dfpsDisplSequenceParameterSetId_; }
 const uint8_t              getDfpsDisplFrameParameterSetId() const{ return dfpsDisplFrameParameterSetId_; }
 const bool                 getDfpsOutputFlagPresentFlag() const{ return dfpsOutputFlagPresentFlag_; }
 const uint32_t             getDfpsNumRefIdxDefaultActiveMinus1() const{ return dfpsNumRefIdxDefaultActiveMinus1_; }
 const uint32_t             getDfpsAdditionalLtDfocLsbLen() const{ return dfpsAdditionalLtDfocLsbLen_; }
 const bool                 getDfpsExtensionPresentFlag() const{ return dfpsExtensionPresentFlag_; }
 const uint32_t             getDfpsExtension8bits() const{ return dfpsExtension8bits_; }
 const bool                 getDfpsExtensionDataFlag() const{ return dfpsExtensionDataFlag_; }
 const DisplInformation&    getDisplInformation()const { return displInformation_; }
 const bool                 getDfpsOverridenFlag() const{ return dfpsOverridenFlag_; }
 const bool                 getDfpsSubdivisionEnableFlag() const{ return dfpsSubdivisionEnableFlag_; }
 const bool                 getDfpsQuantizationParametersEnableFlag () const{ return dfpsQuantizationParametersEnableFlag_; }
 const uint32_t             getDfpsSubdivisionIterationCount() const{ return dfpsSubdivisionIterationCount_; }
 const DisplQuantizationParameters& getDfpsQuantizationParameters() const { return dfpsQuantizationParameters_; }

private:
 uint8_t                  dfpsDisplSequenceParameterSetId_ = 0;
 uint8_t                  dfpsDisplFrameParameterSetId_ = 0;
 DisplInformation         displInformation_;
 bool                     dfpsOutputFlagPresentFlag_ = 0;
 uint32_t                 dfpsNumRefIdxDefaultActiveMinus1_ = 0;
 uint32_t                 dfpsAdditionalLtDfocLsbLen_ = 0;
 bool                     dfpsExtensionPresentFlag_ = 0;
 uint32_t                 dfpsExtension8bits_ = 0;
 bool                     dfpsExtensionDataFlag_ = 0;
 bool                     dfpsOverridenFlag_ = 0;
 bool                     dfpsSubdivisionEnableFlag_ = 0;
 bool                     dfpsQuantizationParametersEnableFlag_ = 0;
 uint32_t                 dfpsSubdivisionIterationCount_ = 0;
 DisplQuantizationParameters dfpsQuantizationParameters_;

};

};  // namespace vdispl
