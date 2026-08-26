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

#include "aspsVdmcExtension.hpp"
#include "aspsVpccExtension.hpp"
#include "refListStruct.hpp"
#include "vuiParameters.hpp"

namespace atlas {

// 8.3.6.1.1 General Atlas sequence parameter set Rbsp
class AtlasSequenceParameterSetRbsp {
public:
  AtlasSequenceParameterSetRbsp() {}
  ~AtlasSequenceParameterSetRbsp() {
    refListStruct_.clear();
    pixelDeinterleavingMapFlag_.clear();
  }

  AtlasSequenceParameterSetRbsp&
  operator=(const AtlasSequenceParameterSetRbsp&) = default;

  void allocateRefListStruct() {
    refListStruct_.resize(numRefAtlasFrameListsInAsps_);
  }
  void allocatePixelDeinterleavingMapFlag() {
    pixelDeinterleavingMapFlag_.resize(mapCountMinus1_ + 1, false);
  }

  uint8_t getAtlasSequenceParameterSetId() const {
    return atlasSequenceParameterSetId_;
  }
  uint16_t getFrameWidth() const { return frameWidth_; }
  uint16_t getFrameHeight() const { return frameHeight_; }
  uint16_t getGeometry2dBitdepthMinus1() const {
    return geometry2dBitdepthMinus1_;
  }
  uint16_t getGeometry3dBitdepthMinus1() const {
    return geometry3dBitdepthMinus1_;
  }
  uint8_t getLog2MaxAtlasFrameOrderCntLsbMinus4() const {
    return log2MaxAtlasFrameOrderCntLsbMinus4_;
  }
  uint8_t getMaxDecAtlasFrameBufferingMinus1() const {
    return maxDecAtlasFrameBufferingMinus1_;
  }
  bool getLongTermRefAtlasFramesFlag() const {
    return longTermRefAtlasFramesFlag_;
  }
  uint8_t getNumRefAtlasFrameListsInAsps() const {
    return numRefAtlasFrameListsInAsps_;
  }
  bool getUseEightOrientationsFlag() const {
    return useEightOrientationsFlag_;
  }
  bool getExtendedProjectionEnabledFlag() const {
    return extendedProjectionEnabledFlag_;
  }
  size_t getMaxNumberProjectionsMinus1() const {
    return maxNumberProjectionsMinus1_;
  }
  bool getNormalAxisLimitsQuantizationEnabledFlag() const {
    return normalAxisLimitsQuantizationEnabledFlag_;
  }
  bool getNormalAxisMaxDeltaValueEnabledFlag() const {
    return normalAxisMaxDeltaValueEnabledFlag_;
  }
  bool getPatchPrecedenceOrderFlag() const {
    return patchPrecedenceOrderFlag_;
  }
  uint8_t getLog2PatchPackingBlockSize() const {
    return log2PatchPackingBlockSize_;
  }
  bool getPatchSizeQuantizerPresentFlag() const {
    return patchSizeQuantizerPresentFlag_;
  }
  uint8_t getMapCountMinus1() const { return mapCountMinus1_; }
  bool getPixelDeinterleavingFlag() const { return pixelDeinterleavingFlag_; }
  bool getPixelDeinterleavingMapFlag(size_t index) const {
    return pixelDeinterleavingMapFlag_[index];
  }
  bool    getEomPatchEnabledFlag() const { return eomPatchEnabledFlag_; }
  uint8_t getEomFixBitCountMinus1() const { return eomFixBitCountMinus1_; }
  bool    getRawPatchEnabledFlag() const { return rawPatchEnabledFlag_; }
  bool    getAuxiliaryVideoEnabledFlag() const {
    return auxiliaryVideoEnabledFlag_;
  }
  bool getPLREnabledFlag() const { return PLREnabledFlag_; }
  bool getVuiParametersPresentFlag() const {
    return vuiParametersPresentFlag_;
  }
  bool    getExtensionFlag() const { return extensionFlag_; }
  bool    getVpccExtensionFlag() const { return vpccExtensionFlag_; }
  bool    getMivExtensionFlag() const { return mivExtensionFlag_; }
  bool    getVdmcExtensionFlag() const { return vdmcExtensionFlag_; }
  uint8_t getExtension5Bits() const { return extension5Bits_; }

  uint8_t& getAtlasSequenceParameterSetId() {
    return atlasSequenceParameterSetId_;
  }
  void setFrameWidth(uint16_t value) { frameWidth_=value; }
  void setFrameHeight(uint16_t value) { frameHeight_=value; }
  uint16_t getFrameWidth() { return frameWidth_; }
  uint16_t getFrameHeight() { return frameHeight_; }
  uint8_t&  getGeometry2dBitdepthMinus1() { return geometry2dBitdepthMinus1_; }
  uint8_t&  getGeometry3dBitdepthMinus1() { return geometry3dBitdepthMinus1_; }
  uint8_t&  getLog2MaxAtlasFrameOrderCntLsbMinus4() {
    return log2MaxAtlasFrameOrderCntLsbMinus4_;
  }
  uint8_t& getMaxDecAtlasFrameBufferingMinus1() {
    return maxDecAtlasFrameBufferingMinus1_;
  }
  bool& getLongTermRefAtlasFramesFlag() { return longTermRefAtlasFramesFlag_; }
  uint8_t& getNumRefAtlasFrameListsInAsps() {
    return numRefAtlasFrameListsInAsps_;
  }
  bool& getUseEightOrientationsFlag() { return useEightOrientationsFlag_; }
  bool& getExtendedProjectionEnabledFlag() {
    return extendedProjectionEnabledFlag_;
  }
  size_t& getMaxNumberProjectionsMinus1() {
    return maxNumberProjectionsMinus1_;
  }
  bool& getNormalAxisLimitsQuantizationEnabledFlag() {
    return normalAxisLimitsQuantizationEnabledFlag_;
  }
  bool& getNormalAxisMaxDeltaValueEnabledFlag() {
    return normalAxisMaxDeltaValueEnabledFlag_;
  }
  bool&    getPatchPrecedenceOrderFlag() { return patchPrecedenceOrderFlag_; }
  uint8_t& getLog2PatchPackingBlockSize() {
    return log2PatchPackingBlockSize_;
  }
  bool& getPatchSizeQuantizerPresentFlag() {
    return patchSizeQuantizerPresentFlag_;
  }
  uint8_t& getMapCountMinus1() { return mapCountMinus1_; }
  bool&    getPixelDeinterleavingFlag() { return pixelDeinterleavingFlag_; }
  uint8_t& getPixelDeinterleavingMapFlag(size_t index) {
    return pixelDeinterleavingMapFlag_[index];
  }
  bool&    getEomPatchEnabledFlag() { return eomPatchEnabledFlag_; }
  uint8_t& getEomFixBitCountMinus1() { return eomFixBitCountMinus1_; }
  bool&    getRawPatchEnabledFlag() { return rawPatchEnabledFlag_; }
  bool& getAuxiliaryVideoEnabledFlag() { return auxiliaryVideoEnabledFlag_; }
  bool& getPLREnabledFlag() { return PLREnabledFlag_; }
  bool& getVuiParametersPresentFlag() { return vuiParametersPresentFlag_; }
  bool& getExtensionFlag() { return extensionFlag_; }
  bool& getVpccExtensionFlag() { return vpccExtensionFlag_; }
  bool& getMivExtensionFlag() { return mivExtensionFlag_; }
  bool& getVdmcExtensionFlag() { return vdmcExtensionFlag_; }
  uint8_t& getExtension5Bits() { return extension5Bits_; }

  VUIParameters&     getVuiParameters() { return vuiParameters_; }
  AspsVpccExtension& getAspsVpccExtension() { return aspsVpccExtension_; }
  AspsVdmcExtension& getAsveExtension() { return aspsVdmcExtension_; }
  const AspsVdmcExtension& getAsveExtension() const {
    return aspsVdmcExtension_;
  }

  RefListStruct& getRefListStruct(uint8_t index) {
    return refListStruct_[index];
  }
  const RefListStruct& getRefListStruct(uint8_t index) const {
    return refListStruct_[index];
  }
  void addRefListStruct(RefListStruct value) {
    refListStruct_.push_back(value);
  }
  RefListStruct& addRefListStruct() {
    RefListStruct refListStruct;
    refListStruct_.push_back(refListStruct);
    return refListStruct_.back();
  }


private:
  uint8_t                     atlasSequenceParameterSetId_        = 0;
  uint16_t                    frameWidth_                         = 0;
  uint16_t                    frameHeight_                        = 0;
  uint8_t                     geometry2dBitdepthMinus1_           = 0;
  uint8_t                     geometry3dBitdepthMinus1_           = 0;
  uint8_t                     log2MaxAtlasFrameOrderCntLsbMinus4_ = 4;
  uint8_t                     maxDecAtlasFrameBufferingMinus1_    = 0;
  bool                        longTermRefAtlasFramesFlag_         = false;
  uint8_t                     numRefAtlasFrameListsInAsps_        = 1;
  std::vector<RefListStruct>  refListStruct_;
  bool                        useEightOrientationsFlag_                = false;
  bool                        extendedProjectionEnabledFlag_           = false;
  size_t                      maxNumberProjectionsMinus1_              = 5;
  bool                        normalAxisLimitsQuantizationEnabledFlag_ = true;
  bool                        normalAxisMaxDeltaValueEnabledFlag_      = false;
  bool                        patchPrecedenceOrderFlag_                = false;
  uint8_t                     log2PatchPackingBlockSize_               = 0;
  bool                        patchSizeQuantizerPresentFlag_           = false;
  uint8_t                     mapCountMinus1_                          = 0;
  bool                        pixelDeinterleavingFlag_                 = false;
  std::vector<uint8_t>        pixelDeinterleavingMapFlag_;
  bool                        eomPatchEnabledFlag_       = false;
  uint8_t                     eomFixBitCountMinus1_      = 0;
  bool                        rawPatchEnabledFlag_       = false;
  bool                        auxiliaryVideoEnabledFlag_ = false;
  bool                        PLREnabledFlag_            = false;
  bool                        vuiParametersPresentFlag_ = false;
  bool                        extensionFlag_            = false;
  bool                        vpccExtensionFlag_        = false;
  bool                        mivExtensionFlag_         = false;
  bool                        vdmcExtensionFlag_        = false;
  uint8_t                     extension5Bits_           = 0;
  VUIParameters               vuiParameters_;
  AspsVpccExtension           aspsVpccExtension_;
  AspsVdmcExtension           aspsVdmcExtension_;
};

};  // namespace vmesh
