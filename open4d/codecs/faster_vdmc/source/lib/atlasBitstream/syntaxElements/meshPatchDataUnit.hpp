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

#include "textureProjectionInformation.hpp"
#include "vdmcQuantizationParameters.hpp"
#include "vdmcLiftingTransformParameters.hpp"

namespace atlas {

class MeshpatchDataUnit {
public:
  MeshpatchDataUnit() {}
  ~MeshpatchDataUnit() {}

  size_t getPatchIndex() { return _patchIndex; }
  size_t getFrameIndex() { return _frameIndex; }
  size_t getTileOrder() { return _tileOrder; }
  void   setPatchIndex(size_t value) { _patchIndex = value; }
  void   setFrameIndex(size_t value) { _frameIndex = value; }
  void   setTileOrder(size_t value) { _tileOrder = value; }

  MeshpatchDataUnit& operator=(const MeshpatchDataUnit&) = default;

  void setMduSubmeshId(uint8_t value) { mduSubmeshId_ = value; }
  void setMduDisplId(uint16_t value) { mduDisplId_ = value; }
  void setMduLoDIdx(uint8_t value) { mduLodIdx_ = value; }
  void setMduGeometry2dPosX(uint32_t value) { mduGeometry2dPosX_ = value; }
  void setMduGeometry2dPosY(uint32_t value) { mduGeometry2dPosY_ = value; }
  void setMduGeometry2dSizeXMinus1(uint32_t value) {
    mduGeometry2dSizeXMinus1_ = value;
  }
  void setMduGeometry2dSizeYMinus1(uint32_t value) {
    mduGeometry2dSizeYMinus1_ = value;
  }
  void setMduParametersOverrideFlag(bool value) {
    mduParametersOverrideFlag_ = value;
  }
  void setMduSubdivisionIterationCountPresentFlag(bool value) {
    mduSubdivisionIterationCountPresentFlag_ = value;
  }
  void setMduSubdivisionIterationCount(uint32_t value) {
    mduSubdivisionIterationCount_ = value;
  }
  void setMduTransformMethodPresentFlag(bool value) {
    mduTransformMethodPresentFlag_ = value;
  }
  void setMduSubdivisionMethodPresentFlag(bool value) {
    mduSubdivisionMethodPresentFlag_ = value;
  }
  void setMduQuantizationPresentFlag(bool value) {
    mduQuantizationPresentFlag_ = value;
  }
  void setMduTransformParametersPresentFlag(bool value) {
    mduTransformParametersPresentFlag_ = value;
  }
  void setMduLodAdaptiveSubdivisionFlag(bool value) {
    mduLodAdaptiveSubdivisionFlag_ = value;
  }
  void setMduSubdivisionMethod(int SubdivIterIdx, uint8_t value) {
    if (SubdivIterIdx >= mduSubdivisionMethod_.size())
      mduSubdivisionMethod_.resize(mduSubdivisionMethod_.size() + 1);
    mduSubdivisionMethod_[SubdivIterIdx] = value;
  }
  void setMduEdgeBasedSubdivisionFlag(bool value) {
    mduEdgeBasedSubdivisionFlag_ = value;
  }
  void setMduSubdivisionMinEdgeLength(uint32_t value) {
    mduSubdivisionMinEdgeLength_ = value;
  }
  VdmcQuantizationParameters& getMduQuantizationParameters() {
    return mduQuantizationParameters_;
  }
  void setMduInverseQuantizationOffsetEnableFlag(bool  value) { 
    mduInverseQuantizationOffsetEnableFlag_ = value; 
  }
  bool& getMduInverseQuantizationOffsetEnableFlag() { return mduInverseQuantizationOffsetEnableFlag_; }
  void                       setIQOffsetValuesSize(int lod_count,int numComponents){
    mduIQOffsetValues_.resize(lod_count);
      for(int i=0;i<lod_count;i++){
      mduIQOffsetValues_[i].resize(numComponents, { {0,0,0},{0,0,0},{0,0,0} });
    }
  }
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() { return mduIQOffsetValues_; }
  auto& getIQOffsetValues(int lod) { return mduIQOffsetValues_[lod]; }
  auto& getIQOffsetValues(int lod, int numComponents) { return mduIQOffsetValues_[lod][numComponents]; }
  auto& getIQOffsetValues(int lod, int numComponents, int deadzone) { return mduIQOffsetValues_[lod][numComponents][deadzone]; }
  auto& getIQOffsetValues(int lod, int numComponents, int deadzone, int value) { return mduIQOffsetValues_[lod][numComponents][deadzone][value]; }
  void setMduDisplacementCoordinateSystem(bool value) {
    mduDisplacementCoordinateSystem_ = value;
  }
  void setMduTransformMethod(uint8_t value) { mduTransformMethod_ = value; }
  void setMduLiftingOffsetPresentFlag(bool value) {
    mduLiftingOffsetPresentFlag_ = value;
  }
  // set function for lifting offset values num --> in lifting structure
  // set function for lifting offset values deno_minus 1 --> in lifting structure
  void setMduDirectionalLiftingPresentFlag(bool value) {
    mduDirectionalLiftingPresentFlag_ = value;
  }
  void setMduDirectionalLiftingMeanNum(int32_t value) { mduDirectionalLiftingMeanNum_ = value; }
  int32_t& getMduDirectionalLiftingMeanNum() { return mduDirectionalLiftingMeanNum_; }
  void setMduDirectionalLiftingMeanDenoMinus1(int32_t value) { mduDirectionalLiftingMeanDenoMinus1_ = value; }
  int32_t& getMduDirectionalLiftingMeanDenoMinus1() { return mduDirectionalLiftingMeanDenoMinus1_; }
  void setMduDirectionalLiftingStdNum(int32_t value) { mduDirectionalLiftingStdNum_ = value; }
  int32_t& getMduDirectionalLiftingStdNum() { return mduDirectionalLiftingStdNum_; }
  void setMduDirectionalLiftingStdDenoMinus1(int32_t value) { mduDirectionalLiftingStdDenoMinus1_ = value; }
  int32_t& getMduDirectionalLiftingStdDenoMinus1() { return mduDirectionalLiftingStdDenoMinus1_; }
  VdmcLiftingTransformParameters& getMduLiftingTransformParameters() {
    return mduLiftingTranformParameters_;
  }
  void setMduBlockCount(size_t idx, uint32_t value) {
    mduBlockCount_[idx] = value;
  }
  void setMduLastPosInBlock(size_t idx, uint32_t value) {
    mduLastPosInBlock_[idx] = value;
  }
  std::vector<uint32_t>& getMduBlockCount() {
    return mduBlockCount_;
  }
  std::vector<uint32_t>& getMduLastPosInBlock() {
    return mduLastPosInBlock_;
  }
  TextureProjectionInformation& getTextureProjectionInformation() {
    return orthoAtlasProjection_;
  }
  void setMduAttributes2dPosX(uint32_t value) {
    mduAttributes2dPosX_ = value;
  }
  void setMduAttributes2dPosY(uint32_t value) {
    mduAttributes2dPosY_ = value;
  }
  void setMduAttributes2dSizeXMinus1(uint32_t value) {
    mduAttributes2dSizeXMinus1_ = value;
  }
  void setMduAttributes2dSizeYMinus1(uint32_t value) {
    mduAttributes2dSizeYMinus1_ = value;
  }

  const uint8_t  getMduSubmeshId() const { return mduSubmeshId_; }
  const uint16_t getMduDisplId() const { return mduDisplId_; }
  const uint8_t  getMduLoDIdx() const { return mduLodIdx_; }
  const uint32_t getMduGeometry2dPosX() const { return mduGeometry2dPosX_; }
  const uint32_t getMduGeometry2dPosY() { return mduGeometry2dPosY_; }
  const uint32_t getMduGeometry2dSizeXMinus1() { return mduGeometry2dSizeXMinus1_; }
  const uint32_t getMduGeometry2dSizeYMinus1() { return mduGeometry2dSizeYMinus1_; }
  const bool getMduParametersOverrideFlag() const { return mduParametersOverrideFlag_; }
  const bool getMduSubdivisionIterationCountPresentFlag() const { return mduSubdivisionIterationCountPresentFlag_; }
  const uint32_t getMduSubdivisionIterationCount() const {
    return mduSubdivisionIterationCount_;
  }
  const bool getMduTransformMethodPresentFlag() const {
    return mduTransformMethodPresentFlag_;
  }
  const bool getMduSubdivisionMethodPresentFlag() const { return mduSubdivisionMethodPresentFlag_; }
  const bool getMduQuantizationPresentFlag() const {
    return mduQuantizationPresentFlag_;
  }
  const bool getMduTransformParametersPresentFlag() const {
    return mduTransformParametersPresentFlag_;
  }
  const bool getMduLodAdaptiveSubdivisionFlag() const {
    return mduLodAdaptiveSubdivisionFlag_;
  }
  const std::vector<uint8_t> getMduSubdivisionMethod() const {
    return mduSubdivisionMethod_;
  }
  const bool getMduEdgeBasedSubdivisionFlag() const { return mduEdgeBasedSubdivisionFlag_; }
  const uint32_t getMduSubdivisionMinEdgeLength() const {
    return mduSubdivisionMinEdgeLength_;
  }
  const VdmcQuantizationParameters& getMduQuantizationParameters() const {
    return mduQuantizationParameters_;
  }
  const bool getMduInverseQuantizationOffsetEnableFlag() const { return mduInverseQuantizationOffsetEnableFlag_; }
  const std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() const { return mduIQOffsetValues_; }
  const auto& getIQOffsetValues(int lod) const { return mduIQOffsetValues_[lod]; }
  const auto& getIQOffsetValues(int lod, int numComponents) const { return mduIQOffsetValues_[lod][numComponents]; }
  const auto& getIQOffsetValues(int lod, int numComponents, int deadzone) const { return mduIQOffsetValues_[lod][numComponents][deadzone]; }
  const auto& getIQOffsetValues(int lod, int numComponents, int deadzone, int value) const { return mduIQOffsetValues_[lod][numComponents][deadzone][value]; }
  const bool getMduDisplacementCoordinateSystem() const {
    return mduDisplacementCoordinateSystem_;
  }
  const uint8_t getMduTransformMethod() const { return mduTransformMethod_; }
  const bool getMduLiftingOffsetPresentFlag() const { return mduLiftingOffsetPresentFlag_; }
  // get function for lifting offset values num --> in lifting structure
  // get function for lifting offset values deno_minus 1 --> in lifting structure
  const bool getMduDirectionalLiftingPresentFlag() const { return mduDirectionalLiftingPresentFlag_; }
  const int32_t& getMduDirectionalLiftingMeanNum() const { return mduDirectionalLiftingMeanNum_; }
  const int32_t& getMduDirectionalLiftingMeanDenoMinus1() const { return mduDirectionalLiftingMeanDenoMinus1_; }
  const int32_t& getMduDirectionalLiftingStdNum() const { return mduDirectionalLiftingStdNum_; }
  const int32_t& getMduDirectionalLiftingStdDenoMinus1() const { return mduDirectionalLiftingStdDenoMinus1_; }
  const VdmcLiftingTransformParameters& getMduLiftingTransformParameters() const {
    return mduLiftingTranformParameters_;
  }
  const std::vector<uint32_t>& getMduBlockCount() const {
    return mduBlockCount_;
  }
  const std::vector<uint32_t>& getMduLastPosInBlock() const {
    return mduLastPosInBlock_;
  }
  const TextureProjectionInformation& getTextureProjectionInformation() const {
    return orthoAtlasProjection_;
  }
  const uint32_t getMduAttributes2dPosX() const {
    return mduAttributes2dPosX_;
  }
  const uint32_t getMduAttributes2dPosY() const {
    return mduAttributes2dPosY_;
  }
  const uint32_t getMduAttributes2dSizeXMinus1() const {
    return mduAttributes2dSizeXMinus1_;
      }
  const uint32_t getMduAttributes2dSizeYMinus1() const {
    return mduAttributes2dSizeYMinus1_;
  }

private:
  uint8_t               mduSubmeshId_ = 0;
  uint16_t              mduDisplId_ = 0;
  uint8_t               mduLodIdx_ = 0;
  uint32_t              mduGeometry2dPosX_ = 0;
  uint32_t              mduGeometry2dPosY_ = 0;
  uint32_t              mduGeometry2dSizeXMinus1_ = 0;
  uint32_t              mduGeometry2dSizeYMinus1_ = 0;
  bool                  mduParametersOverrideFlag_ = 0;
  bool                  mduSubdivisionIterationCountPresentFlag_ = 0;
  uint32_t              mduSubdivisionIterationCount_ = 0;
  bool                  mduTransformMethodPresentFlag_ = 0;
  bool                  mduSubdivisionMethodPresentFlag_ = 0;
  bool                  mduQuantizationPresentFlag_ = 0;
  bool                  mduTransformParametersPresentFlag_ = 0;
  bool                  mduLodAdaptiveSubdivisionFlag_ = 0;
  std::vector<uint8_t>  mduSubdivisionMethod_;
  bool                  mduEdgeBasedSubdivisionFlag_ = 0;
  uint32_t              mduSubdivisionMinEdgeLength_ = 0;
  VdmcQuantizationParameters     mduQuantizationParameters_;
  bool                  mduInverseQuantizationOffsetEnableFlag_ = 0;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>      mduIQOffsetValues_;
  bool                  mduDisplacementCoordinateSystem_ = 0;
  uint8_t               mduTransformMethod_ = 0;
  bool                  mduLiftingOffsetPresentFlag_ = 0;
  // lifting_offset_values_num --> in the lifting structure
  // lifting_offset_values_deno_minus1 --> in the lifting structure
  bool                  mduDirectionalLiftingPresentFlag_ = 0;
  int32_t               mduDirectionalLiftingMeanNum_ = 0;
  int32_t               mduDirectionalLiftingMeanDenoMinus1_ = 0;
  int32_t               mduDirectionalLiftingStdNum_ = 0;
  int32_t               mduDirectionalLiftingStdDenoMinus1_ = 0;
  VdmcLiftingTransformParameters mduLiftingTranformParameters_;
  std::vector<uint32_t> mduBlockCount_;
  std::vector<uint32_t> mduLastPosInBlock_;
  TextureProjectionInformation orthoAtlasProjection_;
  uint32_t              mduAttributes2dPosX_ = 0;
  uint32_t              mduAttributes2dPosY_ = 0;
  uint32_t              mduAttributes2dSizeXMinus1_ = 0;
  uint32_t              mduAttributes2dSizeYMinus1_ = 0;

  size_t _patchIndex = 0;
  size_t _frameIndex = 0;
  size_t _tileOrder  = 0;
};

class InterMeshpatchDataUnit {
public:
  InterMeshpatchDataUnit() {}
  ~InterMeshpatchDataUnit() {}
  InterMeshpatchDataUnit& operator=(const InterMeshpatchDataUnit&) = default;

  int32_t getRefereceFrameIndex() { return _referenceFrameIndex; }
  size_t                  getPatchIndex() { return _patchIndex; }
  size_t                  getFrameIndex() { return _frameIndex; }
  size_t                  getTileOrder() { return _tileOrder; }
  //uint8_t getImduSubmeshId(){ return imduSubmeshId_; }
  //uint8_t getImduSubmeshId() const { return imduSubmeshId_; }
  VdmcQuantizationParameters& getImduQuantizationParameters() {
    return imduQuantizationParameters_;
  }

  void setRefereceFrameIndex(int32_t value) { _referenceFrameIndex = value; }
  void setCurrentPatchIndex(size_t value) { _patchIndex = value; }
  void setFrameIndex(size_t value) { _frameIndex = value; }
  void setTileOrder(size_t value) { _tileOrder = value; }
  //void setImduSubmeshId(uint8_t value) { imduSubmeshId_ = value; }
  const VdmcQuantizationParameters& getImduQuantizationParameters() const {
    return imduQuantizationParameters_;
  }

  // get/set functions for the syntax elements
  void setImduRefIndex(int32_t value) { imduRefIndex_ = value; }
  int32_t getImduRefIndex() { return imduRefIndex_; }  //frame
  int32_t getImduRefIndex() const { return imduRefIndex_; }
  void setImduPatchIndex(int32_t value) { imduPatchIndex_ = value; }
  int32_t getImduPatchIndex() { return imduPatchIndex_; }
  auto    getImduPatchIndex() const { return imduPatchIndex_; }
  void setImduLoDIdx(uint8_t value) { imduLodIdx_ = value; }
  uint8_t  getImduLoDIdx() { return imduLodIdx_; }
  void setImdu2dDeltaPosX(int32_t value) {
    imdu2dDeltaPosX_ = value;
  }
  int32_t getImdu2dDeltaPosX() { return imdu2dDeltaPosX_; }
  int32_t getImdu2dDeltaPosX() const {
    return imdu2dDeltaPosX_;
  }
  void setImdu2dDeltaPosY(int32_t value) {
    imdu2dDeltaPosY_ = value;
  }
  int32_t getImdu2dDeltaPosY() { return imdu2dDeltaPosY_; }
  int32_t getImdu2dDeltaPosY() const {
    return imdu2dDeltaPosY_;
  }
  void setImdu2dDeltaSizeX(int32_t value) {
    imdu2dDeltaSizeX_ = value;
  }
  int32_t getImdu2dDeltaSizeX() { return imdu2dDeltaSizeX_; }
  int32_t getImdu2dDeltaSizeX() const {
    return imdu2dDeltaSizeX_;
  }
  void setImdu2dDeltaSizeY(int32_t value) {
    imdu2dDeltaSizeY_ = value;
  }
  int32_t getImdu2dDeltaSizeY() { return imdu2dDeltaSizeY_; }
  int32_t getImdu2dDeltaSizeY() const {
    return imdu2dDeltaSizeY_;
  }
  void setImduSubdivisionIterationCountPresentFlag(bool value) {
    imduSubdivisionIterationCountPresentFlag_ = value;
  }
  bool getImduSubdivisionIterationCountPresentFlag() {
    return imduSubdivisionIterationCountPresentFlag_;
  }
  void setImduSubdivisionIterationCount(uint32_t value) {
    imduSubdivisionIterationCount_ = value;
  }
  uint32_t getImduSubdivisionIterationCount() {
    return imduSubdivisionIterationCount_;
  }
  void  setImduInverseQuantizationOffsetEnableFlag(bool  value) { imduInverseQuantizationOffsetEnableFlag_ = value; }
  bool& getImduInverseQuantizationOffsetEnableFlag() { return imduInverseQuantizationOffsetEnableFlag_; }
  bool  getImduInverseQuantizationOffsetEnableFlag() const { return imduInverseQuantizationOffsetEnableFlag_; }
  void  setIQOffsetValuesSize(int lod_count, int numComponents) {
    imduIQOffsetValues_.resize(lod_count);
    for (int i = 0; i < lod_count; i++) {
      imduIQOffsetValues_[i].resize(numComponents, { {0,0,0},{0,0,0},{0,0,0} });
  }
  }
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() { return imduIQOffsetValues_; }
  const std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() const { return imduIQOffsetValues_; }
  auto& getIQOffsetValues(int lod) { return imduIQOffsetValues_[lod]; }
  auto& getIQOffsetValues(int lod, int numComponents) { return imduIQOffsetValues_[lod][numComponents]; }
  auto& getIQOffsetValues(int lod, int numComponents, int deadzone) { return imduIQOffsetValues_[lod][numComponents][deadzone]; }
  auto& getIQOffsetValues(int lod, int numComponents, int deadzone, int value) { return imduIQOffsetValues_[lod][numComponents][deadzone][value]; }
  const auto& getIQOffsetValues(int lod) const { return imduIQOffsetValues_[lod]; }
  const auto& getIQOffsetValues(int lod, int numComponents) const { return imduIQOffsetValues_[lod][numComponents]; }
  const auto& getIQOffsetValues(int lod, int numComponents, int deadzone) const { return imduIQOffsetValues_[lod][numComponents][deadzone]; }
  const auto& getIQOffsetValues(int lod, int numComponents, int deadzone, int value) const { return imduIQOffsetValues_[lod][numComponents][deadzone][value]; }
  bool getImduTransformParametersPresentFlag() {
    return imduTransformParametersPresentFlag_;
  }
  void setImduTransformParametersPresentFlag(bool value) {
    imduTransformParametersPresentFlag_ = value;
  }
  void setImduTransformMethodPresentFlag(bool value) {
    imduTransformMethodPresentFlag_ = value;
  }
  bool getImduTransformMethodPresentFlag() {
    return imduTransformMethodPresentFlag_;
  }
  void setImduTransformMethod(uint8_t value) { imduTransformMethod_ = value; }
  uint8_t getImduTransformMethod() { return imduTransformMethod_; }
  void setImduLiftingOffsetPresentFlag(bool value) { imduLiftingOffsetPresentFlag_ = value; }
  bool getImduLiftingOffsetPresentFlag() const { return imduLiftingOffsetPresentFlag_; }
  // set/get functions for imdu_lifting_offset_delta_values_num --> in lifting structure
  // set/get for imdu_lifting_offset_delta_values_deno --> in lifting structure
  void setImduDirectionalLiftingPresentFlag(bool value) { imduDirectionalLiftingPresentFlag_ = value; }
  bool getImduDirectionalLiftingPresentFlag() const { return imduDirectionalLiftingPresentFlag_; }
  void setImduDirectionalLiftingDeltaMeanNum(int32_t value) { imduDirectionalLiftingDeltaMeanNum_ = value; }
  int32_t& getImduDirectionalLiftingDeltaMeanNum() { return imduDirectionalLiftingDeltaMeanNum_; }
  const int32_t& getImduDirectionalLiftingDeltaMeanNum() const { return imduDirectionalLiftingDeltaMeanNum_; }
  void setImduDirectionalLiftingDeltaMeanDeno(int32_t value) { imduDirectionalLiftingDeltaMeanDeno_ = value; }
  int32_t& getImduDirectionalLiftingDeltaMeanDeno() { return imduDirectionalLiftingDeltaMeanDeno_; }
  const int32_t& getImduDirectionalLiftingDeltaMeanDeno() const { return imduDirectionalLiftingDeltaMeanDeno_; }
  void setImduDirectionalLiftingDeltaStdNum(int32_t value) { imduDirectionalLiftingDeltaStdNum_ = value; }
  int32_t& getImduDirectionalLiftingDeltaStdNum() { return imduDirectionalLiftingDeltaStdNum_; }
  const int32_t& getImduDirectionalLiftingDeltaStdNum() const { return imduDirectionalLiftingDeltaStdNum_; }
  void setImduDirectionalLiftingDeltaStdDeno(int32_t value) { imduDirectionalLiftingDeltaStdDeno_ = value; }
  int32_t& getImduDirectionalLiftingDeltaStdDeno() { return imduDirectionalLiftingDeltaStdDeno_; }
  const int32_t& getImduDirectionalLiftingDeltaStdDeno() const { return imduDirectionalLiftingDeltaStdDeno_; }
  VdmcLiftingTransformParameters& getImduLiftingTransformParameters() {
    return imduLiftingTransformParameters_;
  }
  const VdmcLiftingTransformParameters& getImduLiftingTransformParameters() const {
    return imduLiftingTransformParameters_;
  }
  int getImduTextureProjectionPresentFlag() {
    return imduTextureProjectionPresentFlag_;
  }
  void setImduTextureProjectionPresentFlag(int value) {
    imduTextureProjectionPresentFlag_ = value;
  }
  TextureProjectionInterInformation& getTextureProjectionInterInformation() {
    return orthoAtlasProjectionInter_;
  }
  const TextureProjectionInterInformation& getTextureProjectionInterInformation() const {
    return orthoAtlasProjectionInter_;
  }
  void setImduDeltaBlockCount(size_t idx, int32_t value) {
    imduDeltaBlockCount_[idx] = value;
  }
  std::vector<int32_t>& getImduDeltaBlockCount() {
    return imduDeltaBlockCount_;
  }
  void setImduDeltaLastPosInBlock(size_t idx, int32_t value) {
    imduDeltaLastPosInBlock_[idx] = value;
      }
  std::vector<int32_t>& getImduDeltaLastPosInBlock() {
    return imduDeltaLastPosInBlock_;
  }


private:

  int32_t _referenceFrameIndex;
  size_t  _patchIndex = 0;
  size_t  _frameIndex = 0;
  size_t  _tileOrder  = 0;
  //uint8_t imduSubmeshId_ = 0; // should be the same one as the reference
  VdmcQuantizationParameters imduQuantizationParameters_; // should be the same as the reference one
  //syntax elements
  int32_t                        imduRefIndex_ = 0;
  int32_t                        imduPatchIndex_ = 0;
  uint8_t                        imduLodIdx_    = 0;
  int32_t                        imdu2dDeltaPosX_ = 0;
  int32_t                        imdu2dDeltaPosY_ = 0;
  int32_t                        imdu2dDeltaSizeX_ = 0;
  int32_t                        imdu2dDeltaSizeY_ = 0;
  bool                           imduSubdivisionIterationCountPresentFlag_ = 0;
  uint32_t                       imduSubdivisionIterationCount_ = 0;
  bool                           imduInverseQuantizationOffsetEnableFlag_ = 0;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>      imduIQOffsetValues_;
  bool                           imduTransformParametersPresentFlag_ = 0;
  bool                           imduTransformMethodPresentFlag_ = 0;
  uint8_t                        imduTransformMethod_ = 0;
  bool                           imduLiftingOffsetPresentFlag_ = 0;
  //imdu_lifting_offset_delta_values_num --> in lifting structure
  //imdu_lifting_offset_delta_values_deno --> in lifting structure
  bool                           imduDirectionalLiftingPresentFlag_ = 0;
  int32_t                        imduDirectionalLiftingDeltaMeanNum_ = 0;
  int32_t                        imduDirectionalLiftingDeltaMeanDeno_ = 0;
  int32_t                        imduDirectionalLiftingDeltaStdNum_ = 0;
  int32_t                        imduDirectionalLiftingDeltaStdDeno_ = 0;
  VdmcLiftingTransformParameters imduLiftingTransformParameters_;
  int                            imduTextureProjectionPresentFlag_ = 0;
  TextureProjectionInterInformation orthoAtlasProjectionInter_;
  std::vector<int32_t>           imduDeltaBlockCount_;
  std::vector<int32_t>           imduDeltaLastPosInBlock_;

};

class MergeMeshpatchDataUnit {
public:
  MergeMeshpatchDataUnit() {}
  ~MergeMeshpatchDataUnit() {}

  MergeMeshpatchDataUnit& operator=(const MergeMeshpatchDataUnit&) = default;

  // get/set functions for the syntax elements
  auto getMmduRefIndex() const { return mmduRefIndex_; }
  auto getMmduRefIndex() { return mmduRefIndex_; }
  void setMmduRefIndex(uint32_t value) { mmduRefIndex_ = value; }
  bool getMmduSubdivisionIterationCountPresentFlag() {
    return mmduSubdivisionIterationCountPresentFlag_;
  }
  void setMmduSubdivisionIterationCountPresentFlag(bool value) {
    mmduSubdivisionIterationCountPresentFlag_ = value;
  }

  void setMmduSubdivisionIterationCount(uint32_t value) {
    mmduSubdivisionIterationCount_ = value;
  }
  uint32_t getMmduSubdivisionIterationCount() {
    return mmduSubdivisionIterationCount_;
  }
  void setMmduInverseQuantizationOffsetEnableFlag(bool  value) { mmduInverseQuantizationOffsetEnableFlag_ = value; }
  bool& getMmduInverseQuantizationOffsetEnableFlag() { return mmduInverseQuantizationOffsetEnableFlag_; }
  bool getMmduInverseQuantizationOffsetEnableFlag() const { return mmduInverseQuantizationOffsetEnableFlag_; }
  void                       setIQOffsetValuesSize(int lod_count,int numComponents){
      mmduIQOffsetValues_.resize(lod_count);
      for(int i=0;i<lod_count;i++){
      mmduIQOffsetValues_[i].resize(numComponents,{{0,0,0},{0,0,0},{0,0,0}});
      }
  }
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>&       getIQOffsetValues(){ return mmduIQOffsetValues_; }
  const std::vector<std::vector<std::vector<std::vector<int8_t>>>>& getIQOffsetValues() const { return mmduIQOffsetValues_; }
  auto&       getIQOffsetValues(int lod)  { return mmduIQOffsetValues_[lod]; }
  auto&       getIQOffsetValues(int lod, int numComponents)  { return mmduIQOffsetValues_[lod][numComponents]; }
  auto&       getIQOffsetValues(int lod, int numComponents, int deadzone)  { return mmduIQOffsetValues_[lod][numComponents][deadzone]; }
  auto&       getIQOffsetValues(int lod, int numComponents, int deadzone,int value)  { return mmduIQOffsetValues_[lod][numComponents][deadzone][value]; }

  const auto&       getIQOffsetValues(int lod) const { return mmduIQOffsetValues_[lod]; }
  const auto&       getIQOffsetValues(int lod, int numComponents) const { return mmduIQOffsetValues_[lod][numComponents]; }
  const auto&       getIQOffsetValues(int lod, int numComponents, int deadzone) const { return mmduIQOffsetValues_[lod][numComponents][deadzone]; }
  const auto&       getIQOffsetValues(int lod, int numComponents, int deadzone,int value) const { return mmduIQOffsetValues_[lod][numComponents][deadzone][value]; }
  bool getMmduLiftingOffsetPresentFlag() const { return mmduLiftingOffsetPresentFlag_; }
  void setMmduLiftingOffsetPresentFlag(bool value) { mmduLiftingOffsetPresentFlag_ = value; }
  // get/set functions for mmdu_lifting_offset_delta_values_num -> in lifting structure
  // get/set functions for mmdu_lifting_offset_delta_values_deno -> in lifting structure
  bool getMmduDirectionalLiftingPresentFlag() const { return mmduDirectionalLiftingPresentFlag_; }
  void setMmduDirectionalLiftingPresentFlag(bool value) { mmduDirectionalLiftingPresentFlag_ = value; }
  void setMmduDirectionalLiftingDeltaMeanNum(int32_t value) { mmduDirectionalLiftingDeltaMeanNum_ = value; }
  int32_t& getMmduDirectionalLiftingDeltaMeanNum() { return mmduDirectionalLiftingDeltaMeanNum_; }
  const int32_t& getMmduDirectionalLiftingDeltaMeanNum() const { return mmduDirectionalLiftingDeltaMeanNum_; }
  void setMmduDirectionalLiftingDeltaMeanDeno(int32_t value) { mmduDirectionalLiftingDeltaMeanDeno_ = value; }
  int32_t& getMmduDirectionalLiftingDeltaMeanDeno() { return mmduDirectionalLiftingDeltaMeanDeno_; }
  const int32_t& getMmduDirectionalLiftingDeltaMeanDeno() const { return mmduDirectionalLiftingDeltaMeanDeno_; }
  void setMmduDirectionalLiftingDeltaStdNum(int32_t value) { mmduDirectionalLiftingDeltaStdNum_ = value; }
  int32_t& getMmduDirectionalLiftingDeltaStdNum() { return mmduDirectionalLiftingDeltaStdNum_; }
  const int32_t& getMmduDirectionalLiftingDeltaStdNum() const { return mmduDirectionalLiftingDeltaStdNum_; }
  void setMmduDirectionalLiftingDeltaStdDeno(int32_t value) { mmduDirectionalLiftingDeltaStdDeno_ = value; }
  int32_t& getMmduDirectionalLiftingDeltaStdDeno() { return mmduDirectionalLiftingDeltaStdDeno_; }
  const int32_t& getMmduDirectionalLiftingDeltaStdDeno() const { return mmduDirectionalLiftingDeltaStdDeno_; }
  int getMmduTextureProjectionPresentFlag() {
    return mmduTextureProjectionPresentFlag_;
  }
  void setMmduTextureProjectionPresentFlag(int value) {
    mmduTextureProjectionPresentFlag_ = value;
  }
  TextureProjectionMergeInformation& getTextureProjectionMergeInformation() {
    return orthoAtlasProjectionMerge_;
  }
  const TextureProjectionMergeInformation& getTextureProjectionMergeInformation() const {
    return orthoAtlasProjectionMerge_;
  }

  auto getPatchIndex() const { return _patchIndex; }
  auto getFrameIndex() const { return _frameIndex; }
  auto getTileOrder() const { return _tileOrder; }
  int32_t getRefereceFrameIndex() { return _referenceFrameIndex; }
  void setRefereceFrameIndex(int32_t value) { _referenceFrameIndex = value; }
  auto& getPatchIndex() { return _patchIndex; }
  auto& getFrameIndex() { return _frameIndex; }
  auto& getTileOrder() { return _tileOrder; }
  VdmcLiftingTransformParameters& getMmduLiftingTransformParameters() {
    return _mmduLiftingTransformParameters;
  }
  const VdmcLiftingTransformParameters& getMmduLiftingTransformParameters() const {
    return _mmduLiftingTransformParameters;
  }
  VdmcQuantizationParameters& getMmduQuantizationParameters() {
    return _mmduQuantizationParameters;
  }
  const VdmcQuantizationParameters& getMmduQuantizationParameters() const {
    return _mmduQuantizationParameters;
  }

private:
  int32_t _referenceFrameIndex = -1;
  size_t  _patchIndex = 0;
  size_t  _frameIndex = 0;
  size_t  _tileOrder  = 0;
  int8_t  _mmduLodIdx = 0; // this should always be 0
  VdmcLiftingTransformParameters    _mmduLiftingTransformParameters;
  VdmcQuantizationParameters        _mmduQuantizationParameters;

  // syntax elements
  int64_t  mmduRefIndex_ = 0;  //frame
  bool     mmduSubdivisionIterationCountPresentFlag_ = false;
  uint32_t mmduSubdivisionIterationCount_ = 0;
  bool     mmduInverseQuantizationOffsetEnableFlag_ = 0;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>      mmduIQOffsetValues_;
  bool     mmduLiftingOffsetPresentFlag_ = 0;
  // mmdu_lifting_offset_delta_values_num -> in lifting structure
  // mmdu_lifting_offset_delta_values_deno -> in lifting structure
  bool     mmduDirectionalLiftingPresentFlag_ = 0;
  int32_t  mmduDirectionalLiftingDeltaMeanNum_;
  int32_t  mmduDirectionalLiftingDeltaMeanDeno_;
  int32_t  mmduDirectionalLiftingDeltaStdNum_;
  int32_t  mmduDirectionalLiftingDeltaStdDeno_;
  int                               mmduTextureProjectionPresentFlag_ = 0;
  TextureProjectionMergeInformation orthoAtlasProjectionMerge_;
};

class SkipMeshpatchDataUnit {
public:
  SkipMeshpatchDataUnit() {}
  ~SkipMeshpatchDataUnit() {}

  SkipMeshpatchDataUnit& operator=(const SkipMeshpatchDataUnit&) = default;

  auto getPatchIndex() const { return _patchIndex; }
  auto getFrameIndex() const { return _frameIndex; }
  auto getTileOrder() const { return _tileOrder; }

  auto& getPatchIndex() { return _patchIndex; }
  auto& getFrameIndex() { return _frameIndex; }
  auto& getTileOrder() { return _tileOrder; }

  int32_t getRefereceFrameIndex() { return _referenceFrameIndex; }
  void setRefereceFrameIndex(int32_t value) { _referenceFrameIndex = value; }

private:
  size_t  _patchIndex          = 0;
  size_t  _frameIndex          = 0;
  size_t  _tileOrder           = 0;
  int32_t _referenceFrameIndex = -1;
};
};  // namespace vmesh
