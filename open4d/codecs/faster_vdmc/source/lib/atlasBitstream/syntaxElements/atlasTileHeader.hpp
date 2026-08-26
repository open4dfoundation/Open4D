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

#include "atlasCommon.hpp"
#include "refListStruct.hpp"

namespace atlas {

// 8.3.6.11  Atlas tile header syntax
class AtlasTileHeader {
public:
  AtlasTileHeader() {
    additionalAfocLsbPresentFlag_.resize(1, 0);
    additionalAfocLsbVal_.resize(1, 0);
  };
  ~AtlasTileHeader() {
    additionalAfocLsbPresentFlag_.clear();
    additionalAfocLsbVal_.clear();
  };
  AtlasTileHeader& operator=(const AtlasTileHeader&) = default;

  auto& getRefListStruct() { return refListStruct_; }
  void   setRefListStruct(RefListStruct& value)   { refListStruct_=value; }
  void   setRefListStruct(const RefListStruct& value)   { refListStruct_=value; }

  bool getRefAtlasFrameListSpsFlag() { return refAtlasFrameListSpsFlag_; }
  auto getRefAtlasFrameListIdx() { return refAtlasFrameListIdx_; }
  void setRefAtlasFrameListSpsFlag(bool value)    { refAtlasFrameListSpsFlag_=value; }
  void setRefAtlasFrameListIdx    (uint8_t value) { refAtlasFrameListIdx_=value; }
  
  auto  getNoOutputOfPriorAtlasFramesFlag() const {
    return noOutputOfPriorAtlasFramesFlag_;
  }
  auto getAtlasFrameParameterSetId() const {
    return atlasFrameParameterSetId_;
  }
  auto getAtlasAdaptationParameterSetId() const {
    return atlasAdaptationParameterSetId_;
  }
  //auto getId() const { return id_; }
  auto getAtlasTileHeaderId() const { return id_; }
  auto getType() const { return type_; }
  auto getAtlasOutputFlag() const { return atlasOutputFlag_; }
  auto getAtlasFrmOrderCntLsb() const { return atlasFrmOrderCntLsb_; }
  auto getRefAtlasFrameListSpsFlag() const {
    return refAtlasFrameListSpsFlag_;
  }
  auto getRefAtlasFrameListIdx() const { return refAtlasFrameListIdx_; }
  auto getPosMinDQuantizer() const { return posMinDQuantizer_; }
  auto getPosDeltaMaxDQuantizer() const { return posDeltaMaxDQuantizer_; }
  auto getPatchSizeXinfoQuantizer() const { return patchSizeXinfoQuantizer_; }
  auto getPatchSizeYinfoQuantizer() const { return patchSizeYinfoQuantizer_; }
  auto getRaw3dOffsetAxisBitCountMinus1() const {
    return raw3dOffsetAxisBitCountMinus1_;
  }
  bool getNumRefIdxActiveOverrideFlag() const {
    return numRefIdxActiveOverrideFlag_;
  }
  
  void setNumRefIdxActiveOverrideFlag(bool value)  {
    numRefIdxActiveOverrideFlag_=value;
  }
  auto  getNumRefIdxActiveMinus1() const { return numRefIdxActiveMinus1_; }
  auto& getAdditionalAfocLsbPresentFlag() {
    return additionalAfocLsbPresentFlag_;
  }
  auto& getAdditionalAfocLsbVal() const { return additionalAfocLsbVal_; }
  auto  getAdditionalAfocLsbPresentFlag(size_t idx) const {
    return additionalAfocLsbPresentFlag_[idx];
  }
  auto getAdditionalAfocLsbVal(size_t idx) const {
    return additionalAfocLsbVal_[idx];
  }
  auto getTileNaluTypeInfo() const { return tileNaluTypeInfo_; }

  size_t getFrameIndex() { return _frameIndex; }
  void   setFrameIndex(size_t value) { _frameIndex=value; }
  
  auto& getNoOutputOfPriorAtlasFramesFlag() {
    return noOutputOfPriorAtlasFramesFlag_;
  }
  auto& getAtlasFrameParameterSetId() { return atlasFrameParameterSetId_; }
  auto& getAtlasAdaptationParameterSetId() {
    return atlasAdaptationParameterSetId_;
  }
  //auto getId() { return id_; }
  auto getAtlasTileHeaderId() { return id_; }
  void setAtlasTileHeaderId(uint32_t value) {  id_=value; }
  auto& getType() { return type_; }
  void setType(uint8_t value) { type_=(TileType)value; }
  auto& getAtlasOutputFlag() { return atlasOutputFlag_; }
  auto   getAtlasFrmOrderCntLsb() { return atlasFrmOrderCntLsb_; }
  size_t getAtlasFrmOrderCntMsb() { return _atlasFrmOrderCntMsb; }
  size_t getAtlasFrmOrderCntVal() { return _atlasFrmOrderCntVal; }
  void   setAtlasFrmOrderCntLsb(size_t value) {  atlasFrmOrderCntLsb_=value; }
  void   setAtlasFrmOrderCntMsb(size_t value) {  _atlasFrmOrderCntMsb=value; }
  void   setAtlasFrmOrderCntVal(size_t value) {  _atlasFrmOrderCntVal=value; }
  
  
  auto& getPosMinDQuantizer() { return posMinDQuantizer_; }
  auto& getPosDeltaMaxDQuantizer() { return posDeltaMaxDQuantizer_; }
  auto& getPatchSizeXinfoQuantizer() { return patchSizeXinfoQuantizer_; }
  auto& getPatchSizeYinfoQuantizer() { return patchSizeYinfoQuantizer_; }
  auto& getRaw3dOffsetAxisBitCountMinus1() {
    return raw3dOffsetAxisBitCountMinus1_;
  }
//  void etNumRefIdxActiveOverrideFlag(bool value) { numRefIdxActiveOverrideFlag_=value; }
  void setNumRefIdxActiveMinus1(uint8_t value) { numRefIdxActiveMinus1_=value; }
  
  auto& getAdditionalAfocLsbPresentFlag(size_t idx) {
    return additionalAfocLsbPresentFlag_[idx];
  }
  auto& getAdditionalAfocLsbVal(size_t idx) {
    return additionalAfocLsbVal_[idx];
  }
  auto& getTileNaluTypeInfo() { return tileNaluTypeInfo_; }

  const std::vector<int32_t>&   getReferenceList()      const  { return _athReferenceList;}
  std::vector<int32_t>&   getReferenceList()                   { return _athReferenceList;}
  std::vector<int32_t>&   setReferenceList()                   { return _athReferenceList;}
private:
  bool                 noOutputOfPriorAtlasFramesFlag_ = false;
  uint8_t              atlasFrameParameterSetId_       = 0;
  uint8_t              atlasAdaptationParameterSetId_  = 0;
  uint32_t             id_                             = 0;
  TileType             type_                           = TileType(0);
  bool                 atlasOutputFlag_                = false;
  size_t               atlasFrmOrderCntLsb_            = 0;
  bool                 refAtlasFrameListSpsFlag_       = false;
  uint8_t              refAtlasFrameListIdx_           = 0;
  std::vector<uint8_t> additionalAfocLsbPresentFlag_;
  std::vector<uint8_t> additionalAfocLsbVal_;
  uint8_t              posMinDQuantizer_              = 0;
  uint8_t              posDeltaMaxDQuantizer_         = 0;
  uint8_t              patchSizeXinfoQuantizer_       = 0;
  uint8_t              patchSizeYinfoQuantizer_       = 0;
  uint8_t              raw3dOffsetAxisBitCountMinus1_ = 0;
  bool                 numRefIdxActiveOverrideFlag_   = false;
  uint8_t              numRefIdxActiveMinus1_         = 0;
  RefListStruct        refListStruct_;
  uint8_t              tileNaluTypeInfo_;
  
  size_t              _frameIndex = 0;
  size_t              _atlasFrmOrderCntMsb = 0;
  size_t              _atlasFrmOrderCntVal = 0;
  std::vector<int32_t> _athReferenceList;
  uint32_t _tileWidth = 0;
  uint32_t _tileHeight = 0;
  uint32_t  getTileWidth(){return _tileWidth;}
  uint32_t  getTileHeight(){return _tileHeight;}
  void setTileWidth(uint32_t value) { _tileWidth=value;}
  void setTileHeight(uint32_t value) { _tileHeight=value;}
};

};  // namespace vmesh
