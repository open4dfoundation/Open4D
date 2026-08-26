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
#include "../acDisplacementCommon.hpp"
#include "acDisplacementRefListStruct.hpp"
#include "acDisplacementQuantizationParameters.hpp"

namespace acdisplacement {

//disp header
class DisplacementHeader {
public:
 DisplacementHeader() {
   _frameIndex=0;
   _dhdisplFrmOrderCntMsb=0;
   _dhdisplFrmOrderCntVal=0;
 }
 DisplacementHeader(int32_t frameIndex, int32_t refFrameIndex, DisplacementType type) {
   _frameIndex=frameIndex;
   displType_=type;
 }
 DisplacementHeader(DisplacementType type) { _frameIndex=0; displType_=type;}
 ~DisplacementHeader() {}
 DisplacementHeader& operator=( const DisplacementHeader& ) = default;

 int32_t     getFrameIndex()             { return _frameIndex; }
 void        setFrameIndex(uint32_t value){ _frameIndex = value; }
 int32_t     getDisplTileIndex()             { return _dispTileIndex; }
 void        setDisplTileIndex(uint32_t value){ _dispTileIndex = value; }

 int32_t     getReferenceFrameIndex() { return _refFrameIndex;}
 void        setReferenceFrameIndex(int32_t value) { _refFrameIndex=value; }

 size_t      getDhdisplFrmOrderCntMsb()        { return _dhdisplFrmOrderCntMsb; }
 size_t      getDhdisplFrmOrderCntVal()        { return _dhdisplFrmOrderCntVal; }
 void        setDhdisplFrmOrderCntMsb(size_t value)        { _dhdisplFrmOrderCntMsb=value; }
 void        setDhdisplFrmOrderCntVal(size_t value)        { _dhdisplFrmOrderCntVal=value; }


 void                       setNoOutputOfPriorDisplFramesFlag(bool  value) { noOutputOfPriorDisplFramesFlag_ = value; }
 void                       setDisplFrameParameterSetId(uint8_t  value) { displFrameParameterSetId_ = value; }
 void                       setDisplId(uint8_t  value) { displId_ = value; }
 void                       setDisplType(uint32_t  value) { displType_ = value; }
 void                       setDisplOutputFlag(bool  value) { displOutputFlag_ = value; }
 void                       setDisplFrmOrderCntLsb(uint8_t  value) { displFrmOrderCntLsb_ = value; }
 void                       setRefDisplFrameListDspsFlag(bool  value) { refDisplFrameListDspsFlag_ = value; }
 void                       setDisplRefListStruct(DisplRefListStruct&  value) { displRefListStruct_ = value; }
 void                       setRefDisplFrameListIdx(uint8_t  value) { refDisplFrameListIdx_ = value; }
 void                       setAdditionalDfocLsbPresentFlag(std::vector<bool>& value) { additionalDfocLsbPresentFlag_ = value; }
 void                       setAdditionalDfocLsbPresentFlag(size_t idx, uint8_t value) { additionalDfocLsbPresentFlag_[idx] = value; }
 void                       setAdditionalDfocLsbVal(std::vector<uint8_t>& value) { additionalDfocLsbVal_ = value; }
 void                       setAdditionalDfocLsbVal(size_t idx, uint8_t value) { additionalDfocLsbVal_[idx] = value; }
 void                       setNumRefIdxActiveOverrideFlag(bool  value) { numRefIdxActiveOverrideFlag_ = value; }
 void                       setNumRefIdxActiveMinus1(uint32_t  value) { numRefIdxActiveMinus1_ = value; }
 void                       setDhdisplReferenceList(int32_t idx, int32_t value) {_dhdisplReferenceList[idx]=value;}
 void                       setDhParametersOverrideFlag(bool value) { dhParametersOverrideFlag_ = value; }
 void                       setDhSubdivisionOverrideFlag(bool value) { dhSubdivisionOverrideFlag_ = value; }
 void                       setDhQuantizationOverrideFlag(bool value) { dhQuantizationOverrideFlag_ = value; }
 void                       setDhSubdivisionIterationCount(uint32_t value) {
   dhSubdivisionIterationCount_ = value;
   const auto lodCount          = value + 1;
   dhVertexCountLod_.resize(lodCount);
   dhLodSplitFlag_.resize(lodCount);
   dhNumSubblockLodMinus1_.resize(lodCount);
 }
 void                       setDisplIQOffsetFlag(bool value) { displIQOffsetFlag_ = value; }
 void                       setIQOffsetValuesSize(int lod_count,int numComponents) {
   displIQOffsetValues_.resize(lod_count);
   for (int i = 0; i < lod_count; i++) {
     displIQOffsetValues_[i].resize(numComponents,{{0,0,0},{0,0,0},{0,0,0}});
   }
 }
 void                       setDhLoDInter(int8_t lod, int8_t value) { dhLoDInter_[lod] = value; }
 void setDhLayerInterDisableFlag( uint8_t value) { dhLayerInterDisableFlag_ = value; }

 bool                       getNoOutputOfPriorDisplFramesFlag() { return noOutputOfPriorDisplFramesFlag_; }
 uint8_t                    getDisplFrameParameterSetId() { return displFrameParameterSetId_; }
 uint8_t                    getDisplId() { return displId_; }
 uint32_t                   getDisplType() { return displType_; }
 bool                       getDisplOutputFlag() { return displOutputFlag_; }
 uint8_t                    getDisplFrmOrderCntLsb() { return displFrmOrderCntLsb_; }
 bool                       getRefDisplFrameListDspsFlag() { return refDisplFrameListDspsFlag_; }
 DisplRefListStruct&        getDisplRefListStruct() { return displRefListStruct_; }
 uint8_t                    getRefDisplFrameListIdx() { return refDisplFrameListIdx_; }
 std::vector<bool>&         getAdditionalDfocLsbPresentFlag() { return additionalDfocLsbPresentFlag_; }
 std::vector<uint8_t>&      getAdditionalDfocLsbVal() { return additionalDfocLsbVal_; }
 bool                       getNumRefIdxActiveOverrideFlag() { return numRefIdxActiveOverrideFlag_; }
 uint32_t                   getNumRefIdxActiveMinus1() { return numRefIdxActiveMinus1_; }
 std::vector<int32_t>&      getDhdisplReferenceList() { return _dhdisplReferenceList; }
 bool                       getDhParametersOverrideFlag() { return dhParametersOverrideFlag_; }
 bool                       getDhSubdivisionOverrideFlag() { return dhSubdivisionOverrideFlag_; }
 bool                       getDhQuantizationOverrideFlag() { return dhQuantizationOverrideFlag_; }
 uint32_t                   getDhSubdivisionIterationCount() { return dhSubdivisionIterationCount_; }
 DisplQuantizationParameters& getDhQuantizationParameters() { return  dhQuantizationParameters_; }
 bool&                      getDisplIQOffsetFlag() { return displIQOffsetFlag_; }
 std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() { return displIQOffsetValues_; }
 auto&                      getIQOffsetValues(int lod) { return displIQOffsetValues_[lod]; }
 auto&                      getIQOffsetValues(int lod, int numComponents) { return displIQOffsetValues_[lod][numComponents]; }
 auto&                      getIQOffsetValues(int lod, int numComponents, int deadzone) { return displIQOffsetValues_[lod][numComponents][deadzone]; }
 auto&                      getIQOffsetValues(int lod, int numComponents, int deadzone,int value) { return displIQOffsetValues_[lod][numComponents][deadzone][value]; }

 int32_t                    getDhLoDInter(int8_t lod)  { return dhLoDInter_[lod]; }
 uint8_t getDhLayerInterDisableFlag() { return dhLayerInterDisableFlag_; }

 const bool                       getNoOutputOfPriorDisplFramesFlag() const { return noOutputOfPriorDisplFramesFlag_; }
 const uint8_t                    getDisplFrameParameterSetId()const{ return displFrameParameterSetId_; }
 const uint8_t                    getDisplId() const{ return displId_; }
 const uint32_t                   getDisplType() const{ return displType_; }
 const bool                       getDisplOutputFlag() const{ return displOutputFlag_; }
 const uint8_t                    getDisplFrmOrderCntLsb()const { return displFrmOrderCntLsb_; }
 const bool                       getRefDisplFrameListDspsFlag() const{ return refDisplFrameListDspsFlag_; }
 const DisplRefListStruct&        getDisplRefListStruct() const{ return displRefListStruct_; }
 const uint8_t                    getRefDisplFrameListIdx() const{ return refDisplFrameListIdx_; }
 const std::vector<bool>&         getAdditionalDfocLsbPresentFlag() const{ return additionalDfocLsbPresentFlag_; }
 const std::vector<uint8_t>&      getAdditionalDfocLsbVal() const{ return additionalDfocLsbVal_; }
 const bool                       getNumRefIdxActiveOverrideFlag() const{ return numRefIdxActiveOverrideFlag_; }
 const uint32_t                   getNumRefIdxActiveMinus1() const{ return numRefIdxActiveMinus1_; }
 const std::vector<int32_t>&      getDhdisplReferenceList() const{ return _dhdisplReferenceList; }
 const bool                       getDhParametersOverrideFlag() const{ return dhParametersOverrideFlag_; }
 const bool                       getDhSubdivisionOverrideFlag() const{ return dhSubdivisionOverrideFlag_; }
 const bool                       getDhQuantizationOverrideFlag() const{ return dhQuantizationOverrideFlag_; }
 const uint32_t                   getDhSubdivisionIterationCount() const{ return dhSubdivisionIterationCount_; }
 const DisplQuantizationParameters& getDhQuantizationParameters() const{ return  dhQuantizationParameters_; }
 const bool getDisplIQOffsetFlag() const { return displIQOffsetFlag_; }
 const std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() const { return displIQOffsetValues_; }
 const auto&                      getIQOffsetValues(int lod) const { return displIQOffsetValues_[lod]; }
 const auto&                      getIQOffsetValues(int lod, int numComponents) const { return displIQOffsetValues_[lod][numComponents]; }
 const auto&                      getIQOffsetValues(int lod, int numComponents, int deadzone) const { return displIQOffsetValues_[lod][numComponents][deadzone]; }
 const auto&                      getIQOffsetValues(int lod, int numComponents, int deadzone,int value) const { return displIQOffsetValues_[lod][numComponents][deadzone][value]; }
 const int32_t                    getDhLoDInter(int8_t lod) const { return dhLoDInter_[lod]; }
 const uint8_t getDhLayerInterDisableFlag() const{ return dhLayerInterDisableFlag_; }
 const std::vector<uint32_t>&     getDhVertexCountLod() const { return dhVertexCountLod_;}
 void                             setDhVertexCountLod(uint8_t idx, uint32_t value) {  dhVertexCountLod_[idx] = value;}
 const bool                       getDhLodSplitFlag(uint8_t idx) const { return dhLodSplitFlag_[idx]; }
 void                             setDhLodSplitFlag(uint8_t idx, bool value) { dhLodSplitFlag_[idx] = value; }
 const std::vector<uint32_t>&     getDhNumSubblockLodMinus1() const { return dhNumSubblockLodMinus1_; }
 void                             setDhNumSubblockLodMinus1(uint8_t idx, uint32_t value) { dhNumSubblockLodMinus1_[idx] = value; }
 void   setNumLoDInter(uint32_t value) {
   dhLoDInter_.resize(value);
 }
private:
 bool                     noOutputOfPriorDisplFramesFlag_ = 0;
 uint8_t                  displFrameParameterSetId_ = 0;
 uint8_t                  displId_ = 0;
 uint32_t                 displType_ = 0;
 bool                     displOutputFlag_ = 0;
 uint8_t                  displFrmOrderCntLsb_ = 0;
 bool                     refDisplFrameListDspsFlag_ = 0;
 DisplRefListStruct       displRefListStruct_;
 uint8_t                  refDisplFrameListIdx_ = 0;
 std::vector<bool>        additionalDfocLsbPresentFlag_;
 std::vector<uint8_t>     additionalDfocLsbVal_;
 bool                     numRefIdxActiveOverrideFlag_ = 0;
 uint32_t                 numRefIdxActiveMinus1_ = 0;
 std::vector<uint32_t>    dhNumSubblockLodMinus1_;
 std::vector<bool>        dhLodSplitFlag_;
 uint32_t            _frameIndex;
 uint32_t            _dispTileIndex;
 size_t              _dhdisplFrmOrderCntMsb;
 size_t              _dhdisplFrmOrderCntVal;
 std::vector<int32_t>_dhdisplReferenceList;
 int32_t            _refFrameIndex; //-1~31 (-1:intra)
 std::vector<uint32_t>     dhVertexCountLod_;
 //decoder
 bool             decoded_;
 bool             dhParametersOverrideFlag_= 0;
 bool             dhSubdivisionOverrideFlag_= 0;
 bool             dhQuantizationOverrideFlag_ = 0;
 uint32_t         dhSubdivisionIterationCount_= 0;
 DisplQuantizationParameters dhQuantizationParameters_;
 bool             displIQOffsetFlag_ = 0;
 std::vector<std::vector<std::vector<std::vector<int8_t>>>> displIQOffsetValues_;
 std::vector<uint8_t>     dhLoDInter_;
 uint8_t                  dhLayerInterDisableFlag_ = 0;
};


};  // namespace vdispl
