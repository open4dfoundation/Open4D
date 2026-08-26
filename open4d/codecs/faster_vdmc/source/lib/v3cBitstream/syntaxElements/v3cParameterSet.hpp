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
#include "profileTierLevel.hpp"
#include "geometryInformation.hpp"
#include "occupancyInformation.hpp"
#include "attributeInformation.hpp"
#include "packingInformation.hpp"
#include "vpccExtension.hpp"
#include "vdmcExtension.hpp"

#include <optional>
#include <cstddef>
namespace vmesh {

// 8.3.4.1  General V3C Sequence parameter set syntax
class V3CParameterSet {
public:
  V3CParameterSet()
    : v3cParameterSetId_(0)
    , atlasCountMinus1_(0)
    , extensionPresentFlag_(false)
    , extensionCount_(0)
    , extensionLengthMinus1_(0) {}
  ~V3CParameterSet() {
    for (auto& data : mapAbsoluteCodingEnableFlag_) { data.clear(); }
    mapAbsoluteCodingEnableFlag_.clear();
    for (auto& data : mapPredictorIndexDiff_) { data.clear(); }
    mapPredictorIndexDiff_.clear();
    frameWidth_.clear();
    frameHeight_.clear();
    mapCountMinus1_.clear();
    multipleMapStreamsPresentFlag_.clear();
    auxiliaryVideoPresentFlag_.clear();
    occupancyVideoPresentFlag_.clear();
    geometryVideoPresentFlag_.clear();
    attributeVideoPresentFlag_.clear();
    for (auto& data : extensionDataByte_) { data.clear(); }
    extensionDataByte_.clear();
  }

  V3CParameterSet& operator=(const V3CParameterSet&) = default;
  void             init( uint32_t v3cParameterSetId,
                         int32_t  atlasCount,
                         uint16_t atlasId,
                         uint16_t frameWidth,
                         uint16_t frameHeight,
                         uint16_t avgFrameRate,
                         uint32_t mapCountMinus1,
                         bool     multipleMapStreamsPresentFlag,
                         bool     auxiliaryVideoPresentFlag,
                         bool     occupancyVideoPresentFlag,
                         bool     geometryVideoPresentFlag,
                         bool     attributeVideoPresentFlag,
                         bool     packedVideoPresentFlag) {
    if(packedVideoPresentFlag){
      geometryVideoPresentFlag  = false;
      attributeVideoPresentFlag = false;
      vpsPackedVideoExtension_.setAtlasCount(atlasCount);
    }
    v3cParameterSetId_ = v3cParameterSetId;
    atlasCountMinus1_  = atlasCount-1;
    allocateAtlas(atlasCount);
    atlasId_[0]                       = atlasId;
    frameWidth_[0]                    = frameWidth;
    frameHeight_[0]                   = frameHeight;
    mapCountMinus1_[0]                = mapCountMinus1;
    multipleMapStreamsPresentFlag_[0] = multipleMapStreamsPresentFlag;
    auxiliaryVideoPresentFlag_[0]     = auxiliaryVideoPresentFlag;
    occupancyVideoPresentFlag_[0]     = occupancyVideoPresentFlag;
    geometryVideoPresentFlag_[0]      = geometryVideoPresentFlag;
    attributeVideoPresentFlag_[0]     = attributeVideoPresentFlag;
  }
  void allocateAtlas(size_t number) {
    atlasCountMinus1_ = number - 1;
    atlasId_.resize(atlasCountMinus1_ + 1, 0);
    frameWidth_.resize(atlasCountMinus1_ + 1, 0);
    frameHeight_.resize(atlasCountMinus1_ + 1, 0);
    mapCountMinus1_.resize(atlasCountMinus1_ + 1, 0);
    multipleMapStreamsPresentFlag_.resize(atlasCountMinus1_ + 1, 0);
    mapAbsoluteCodingEnableFlag_.resize(atlasCountMinus1_ + 1);
    mapPredictorIndexDiff_.resize(atlasCountMinus1_ + 1);
    auxiliaryVideoPresentFlag_.resize(atlasCountMinus1_ + 1, 0);
    geometryVideoPresentFlag_.resize(atlasCountMinus1_ + 1, 0);
    occupancyVideoPresentFlag_.resize(atlasCountMinus1_ + 1, 0);
    attributeVideoPresentFlag_.resize(atlasCountMinus1_ + 1, 0);
    geometryInformation_.resize(atlasCountMinus1_ + 1);
    occupancyInformation_.resize(atlasCountMinus1_ + 1);
    attributeInformation_.resize(atlasCountMinus1_ + 1);
    vpsPackedVideoExtension_.setAtlasCount(number);
  }
  void allocateMap(size_t index) {
    mapAbsoluteCodingEnableFlag_[index].resize(mapCountMinus1_[index] + 1, 1);
    mapPredictorIndexDiff_[index].resize(mapCountMinus1_[index] + 1);
  }
  uint32_t getV3CParameterSetId() const { return v3cParameterSetId_; }
  uint32_t getAtlasCountMinus1() const { return atlasCountMinus1_; }
  uint16_t getAtlasId(size_t index) const { return atlasId_[index]; }
  uint16_t getFrameWidth(size_t index) const { return frameWidth_[index]; }
  uint16_t getFrameHeight(size_t index) const { return frameHeight_[index]; }
  uint32_t getMapCountMinus1(size_t index) const {
    return mapCountMinus1_[index];
  }
  bool getMultipleMapStreamsPresentFlag(size_t index) const {
    return multipleMapStreamsPresentFlag_[index];
  }
  bool getAuxiliaryVideoPresentFlag(size_t index) const {
    return auxiliaryVideoPresentFlag_[index];
  }
  bool getOccupancyVideoPresentFlag(size_t index) const {
    return occupancyVideoPresentFlag_[index];
  }
  bool getGeometryVideoPresentFlag(size_t index) const {
    return geometryVideoPresentFlag_[index];
  }
  bool getAttributeVideoPresentFlag(size_t index) const {
    return attributeVideoPresentFlag_[index];
  }
  size_t getMapPredictorIndexDiff(size_t i, size_t j) const {
    return mapPredictorIndexDiff_[i][j];
  }
  bool getMapAbsoluteCodingEnableFlag(size_t i, size_t j) const {
    return mapAbsoluteCodingEnableFlag_[i][j];
  }
  bool    getExtensionPresentFlag() const { return extensionPresentFlag_; }
  uint8_t getExtensionCount() const { return extensionCount_; }
  size_t  getExtensionLengthMinus1() const { return extensionLengthMinus1_; }
  uint8_t getExtensionType(size_t index) const {
    return extensionType_[index];
  }
  uint16_t getExtensionLength(size_t index) const {
    return extensionLength_[index];
  }
  std::vector<uint8_t>& getExtensionDataByte(size_t index) {
    return extensionDataByte_[index];
  }
  uint8_t getExtensionDataByte(size_t i, size_t j) const {
    return extensionDataByte_[i][j];
  }

  uint32_t& getV3CParameterSetId() { return v3cParameterSetId_; }
  uint32_t& getAtlasCountMinus1() { return atlasCountMinus1_; }
  uint16_t& getAtlasId(size_t index) { return atlasId_[index]; }
  uint16_t& getFrameWidth(size_t index) { return frameWidth_[index]; }
  uint16_t& getFrameHeight(size_t index) { return frameHeight_[index]; }
  uint8_t&  getMapCountMinus1(size_t index) { return mapCountMinus1_[index]; }

  uint8_t& getMultipleMapStreamsPresentFlag(size_t index) {
    return multipleMapStreamsPresentFlag_[index];
  }
  uint8_t& getAuxiliaryVideoPresentFlag(size_t index) {
    return auxiliaryVideoPresentFlag_[index];
  }
  uint8_t& getOccupancyVideoPresentFlag(size_t index) {
    return occupancyVideoPresentFlag_[index];
  }
  uint8_t getGeometryVideoPresentFlag(size_t index) {
    return geometryVideoPresentFlag_[index];
  }
  uint8_t& getAttributeVideoPresentFlag(size_t index) {
    return attributeVideoPresentFlag_[index];
  }
  uint8_t& getMapAbsoluteCodingEnableFlag(size_t i, size_t j) {
    if (mapAbsoluteCodingEnableFlag_[i].size() < j + 1) {
      mapAbsoluteCodingEnableFlag_[i].resize(j + 1, 1);
    }
    return mapAbsoluteCodingEnableFlag_[i][j];
  }
  size_t& getMapPredictorIndexDiff(size_t i, size_t j) {
    if (mapPredictorIndexDiff_[i].size() < j + 1) {
      mapPredictorIndexDiff_[i].resize(j + 1, 1);
    }
    return mapPredictorIndexDiff_[i][j];
  }
  bool     getExtensionPresentFlag() { return extensionPresentFlag_; }
  uint8_t  getExtensionCount() { return extensionCount_; }
  size_t   getExtensionLengthMinus1() { return extensionLengthMinus1_; }
  uint8_t  getExtensionType(size_t index) { return extensionType_[index]; }
  uint16_t getExtensionLength(size_t index) {
    return extensionLength_[index];
  }
  uint8_t& getExtensionDataByte(size_t i, size_t j) {
    return extensionDataByte_[i][j];
  }

  ProfileTierLevel&    getProfileTierLevel() { return profileTierLevel_; }
  const ProfileTierLevel&    getProfileTierLevel() const { return profileTierLevel_; }
  GeometryInformation& getGeometryInformation(size_t index) {
    return geometryInformation_[index];
  }
  OccupancyInformation& getOccupancyInformation(size_t index) {
    return occupancyInformation_[index];
  }
  AttributeInformation& getAttributeInformation(size_t index) {
    return attributeInformation_[index];
  }
  const GeometryInformation& getGeometryInformation(size_t index) const {
    return geometryInformation_[index];
  }
  const AttributeInformation& getAttributeInformation(size_t index) const {
    return attributeInformation_[index];
  }

  VpsVpccExtension&       getVpsVpccExtension() { return vpsVpccExtension_; }
  VpsVdmcExtension&       getVpsVdmcExtension() { return vpsVdmcExtension_; }
  VpsPackedVideoExtension&getVpsPackedVideoExtension() { return vpsPackedVideoExtension_; }
  const VpsVpccExtension& getVpsVpccExtension() const {
    return vpsVpccExtension_;
  }
  const VpsVdmcExtension& getVpsVdmcExtension() const {
    return vpsVdmcExtension_;
  }
  const VpsPackedVideoExtension& getVpsPackedVideoExtension() const {
    return vpsPackedVideoExtension_;
  }
  
  void addMapAbsoluteCodingEnableFlag(size_t index, bool value) {
    mapAbsoluteCodingEnableFlag_[index].push_back(value);
  }
  void addMapPredictorIndexDiff(size_t index, bool value) {
    mapPredictorIndexDiff_[index].push_back(value);
  }
  
  void setV3CParameterSetId( uint32_t value ) { v3cParameterSetId_ = value; }
  void setAtlasCountMinus1( uint32_t value ) { atlasCountMinus1_ = value; }
  void setAtlasId( size_t index, uint16_t value ) { atlasId_[index] = value; }
  void setFrameWidth( size_t index, uint16_t value ) { frameWidth_[index] = value; }
  void setFrameHeight( size_t index, uint16_t value ) { frameHeight_[index] = value; }
  void setMapCountMinus1( size_t index, uint32_t value ) { mapCountMinus1_[index] = value; }
  void setMultipleMapStreamsPresentFlag( size_t index, bool value ) { multipleMapStreamsPresentFlag_[index] = value; }
  void setAuxiliaryVideoPresentFlag( size_t index, bool value ) { auxiliaryVideoPresentFlag_[index] = value; }
  void setOccupancyVideoPresentFlag( size_t index, bool value ) { occupancyVideoPresentFlag_[index] = value; }
  void setGeometryVideoPresentFlag( size_t index, bool value ) { geometryVideoPresentFlag_[index] = value; }
  void setAttributeVideoPresentFlag( size_t index, bool value ) { attributeVideoPresentFlag_[index] = value; }
  void setGeometryInformation( size_t index, GeometryInformation value ) { geometryInformation_[index] = value; }
  void setOccupancyInformation( size_t index, OccupancyInformation value ) { occupancyInformation_[index] = value; }
  void setMapAbsoluteCodingEnableFlag( size_t i, size_t j, bool value ) {
    if ( mapAbsoluteCodingEnableFlag_[i].size() < j + 1 ) { mapAbsoluteCodingEnableFlag_[i].resize( j + 1, 1 ); }
    mapAbsoluteCodingEnableFlag_[i][j] = value;
  }
  void setMapPredictorIndexDiff( size_t i, size_t j, bool value ) {
    if ( mapPredictorIndexDiff_[i].size() < j + 1 ) { mapPredictorIndexDiff_[i].resize( j + 1 ); }
    mapPredictorIndexDiff_[i][j] = value;
  }
  
  void setExtensionDataByte(size_t i, size_t j, uint8_t value) {
    extensionDataByte_[i][j] = value;
  }
  void setExtensionDataByte(size_t index, std::vector<uint8_t>& value) {
    extensionDataByte_[index] = value;
  }

  void setExtensionPresentFlag(bool value) { extensionPresentFlag_ = value; }
  void setExtensionLengthMinus1(size_t value) {
    extensionLengthMinus1_ = value;
  }
  void setExtensionCount(uint8_t value) {
    extensionCount_ = value;
    extensionType_.resize(extensionCount_, 0);
    extensionLength_.resize(extensionCount_, (uint16_t)0);
    extensionDataByte_.resize(extensionCount_);
  }
  void setExtensionType(int index, uint8_t value) {
    extensionType_[index] = value;
  }
  void setExtensionLength(int index, uint8_t value) {
    extensionLength_[index] = value;
  }

  uint32_t getVpsAttributeNominalFrameSizeCount(int index) const {
    uint32_t attributeNominalFrameSizeCount = 0;

    // TODO all the function should make the index check

    if (this->getAttributeVideoPresentFlag(index) == 1 ) {
      attributeNominalFrameSizeCount = this->getAttributeInformation(index).getAttributeCount();
    }else if(vpsPackedVideoExtension_.getPackedVideoPresentFlag(index) &&
       vpsPackedVideoExtension_.getPackingInformation(index).pin_attribute_present_flag()){
      attributeNominalFrameSizeCount = vpsPackedVideoExtension_.getPackingInformation(index).pin_attribute_count();
    }
    if(vpsVdmcExtension_.getVpsExtConsistentAttributeFrameFlag(index) == 1){
      if (attributeNominalFrameSizeCount > 1 ) { // from 8.4.4.11 vpve_consistent_attribute_frame_flag semantics
        attributeNominalFrameSizeCount = 1;
      }
    }

    return attributeNominalFrameSizeCount;
  }

private:
  ProfileTierLevel                  profileTierLevel_;
  uint32_t                          v3cParameterSetId_ = 0;
  uint32_t                          atlasCountMinus1_  = 0;
  std::vector<uint16_t>             atlasId_;
  std::vector<uint16_t>             frameWidth_;
  std::vector<uint16_t>             frameHeight_;
  std::vector<uint8_t>              mapCountMinus1_;
  std::vector<uint8_t>              multipleMapStreamsPresentFlag_;
  std::vector<std::vector<uint8_t>> mapAbsoluteCodingEnableFlag_;
  std::vector<std::vector<size_t>>  mapPredictorIndexDiff_;
  std::vector<uint8_t>              auxiliaryVideoPresentFlag_;
  std::vector<uint8_t>              occupancyVideoPresentFlag_;
  std::vector<uint8_t>              geometryVideoPresentFlag_;
  std::vector<uint8_t>              attributeVideoPresentFlag_;
//  std::vector<uint8_t>              packedVideoPresentFlag_;
  std::vector<OccupancyInformation> occupancyInformation_;
  std::vector<GeometryInformation>  geometryInformation_;
  std::vector<AttributeInformation> attributeInformation_;
//  std::vector<PackingInformation>   packingInformation_;
  bool                              extensionPresentFlag_  = false;
  uint8_t                           extensionCount_        = 0;
  size_t                            extensionLengthMinus1_ = 0;
  std::vector<uint8_t>              extensionType_;
  std::vector<uint16_t>             extensionLength_;
  std::vector<std::vector<uint8_t>> extensionDataByte_;
  VpsVpccExtension                  vpsVpccExtension_;
  VpsVdmcExtension                  vpsVdmcExtension_;
  VpsPackedVideoExtension           vpsPackedVideoExtension_;
};

};  // namespace vmesh
