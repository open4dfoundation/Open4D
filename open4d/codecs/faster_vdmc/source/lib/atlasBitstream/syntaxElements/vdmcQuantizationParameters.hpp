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

namespace atlas {

class VdmcQuantizationParameters{
public:
 VdmcQuantizationParameters() {
 }
 VdmcQuantizationParameters(int paramIndex){
   _vdmcParamLevelIndex_=paramIndex;
 }
 ~VdmcQuantizationParameters() {}
 VdmcQuantizationParameters& operator=(const VdmcQuantizationParameters&) = default;

 void setVdmcLodQuantizationFlag(bool value) { vdmcLodQuantizationFlag_ = value; }
 bool getVdmcLodQuantizationFlag() { return vdmcLodQuantizationFlag_; }
 bool getVdmcLodQuantizationFlag()   const { return vdmcLodQuantizationFlag_; }

 void    setVdmcBitDepthOffset(int32_t value) { vdmcBitDepthOffset_ = value; }
 int32_t getVdmcBitDepthOffset() { return vdmcBitDepthOffset_; }
 int32_t getVdmcBitDepthOffset() const { return vdmcBitDepthOffset_; }

 void                         setVdmcQuantizationParameters(uint32_t index, uint32_t value) { vdmcQuantizationParameters_[index] = value; }
 std::vector<uint32_t>&       getVdmcQuantizationParameters() { return vdmcQuantizationParameters_; }
 const std::vector<uint32_t>& getVdmcQuantizationParameters() const { return vdmcQuantizationParameters_; }

 void                         setVdmcLog2LodInverseScale(uint32_t index, uint32_t value) { vdmcLog2LodInverseScale_[index] = value; }
 std::vector<uint32_t>&       getVdmcLog2LodInverseScale() { return vdmcLog2LodInverseScale_; }
 const std::vector<uint32_t>& getVdmcLog2LodInverseScale() const { return vdmcLog2LodInverseScale_; }

 void                               setVdmcLodDeltaQPValue(int level, int axis, uint8_t value) { vdmcLodDeltaQPValue_[level][axis] = value; }
 uint8_t                            getVdmcLodDeltaQPValue(int level, int axis) const { return vdmcLodDeltaQPValue_[level][axis]; }
 uint8_t                            getVdmcLodDeltaQPValue(int level, int axis) { return vdmcLodDeltaQPValue_[level][axis]; }
 std::vector<uint8_t>&              getVdmcLodDeltaQPValue(int level) { return vdmcLodDeltaQPValue_[level]; }
 std::vector<uint8_t>               getVdmcLodDeltaQPValue(int level) const { return vdmcLodDeltaQPValue_[level]; }
 std::vector<std::vector<uint8_t>>& getVdmcLodDeltaQPValue() { return vdmcLodDeltaQPValue_; }
 std::vector<std::vector<uint8_t>>  getVdmcLodDeltaQPValue() const { return vdmcLodDeltaQPValue_; }

 void                               setVdmcLodDeltaQPSign(int level, int axis, uint8_t value) { vdmcLodDeltaQPSign_[level][axis] = value; }
 uint8_t                            getVdmcLodDeltaQPSign(int level, int axis) const { return vdmcLodDeltaQPSign_[level][axis]; }
 uint8_t                            getVdmcLodDeltaQPSign(int level, int axis) { return vdmcLodDeltaQPSign_[level][axis]; }
 std::vector<uint8_t>&              getVdmcLodDeltaQPSign(int level) { return vdmcLodDeltaQPSign_[level]; }
 std::vector<uint8_t>               getVdmcLodDeltaQPSign(int level) const { return vdmcLodDeltaQPSign_[level]; }
 std::vector<std::vector<uint8_t>>& getVdmcLodDeltaQPSign() { return vdmcLodDeltaQPSign_; }
 std::vector<std::vector<uint8_t>>  getVdmcLodDeltaQPSign() const { return vdmcLodDeltaQPSign_; }

 void setVdmcDirectQuantizationEnabledFlag(bool value) { vdmcDirectQuantizationEnabledFlag_ = value; }
 bool getVdmcDirectQuantizationEnabledFlag() const { return vdmcDirectQuantizationEnabledFlag_; }

 void   setNumComponents(uint32_t value) {
   numComponents_ = value;
   vdmcQuantizationParameters_.resize(numComponents_);
   vdmcLog2LodInverseScale_.resize(numComponents_);
 }
 void   setNumLod(uint32_t value) {
   lodCount_ = value;
   vdmcLodDeltaQPValue_.resize(lodCount_);
   vdmcLodDeltaQPSign_.resize(lodCount_);
 }
 void   allocComponents( size_t numComponents, size_t lodIdx) {
   vdmcLodDeltaQPValue_[lodIdx].resize(numComponents);
   vdmcLodDeltaQPSign_[lodIdx].resize(numComponents);
 }

private:
 int                      _vdmcParamLevelIndex_=0; //0.ASPS 1.AFPS 2.PDU
 uint8_t                  numComponents_ = 0;
 uint8_t                  lodCount_ = 0;
 bool                     vdmcLodQuantizationFlag_ = false;
 int32_t                  vdmcBitDepthOffset_ = 0;
 std::vector<uint32_t>    vdmcQuantizationParameters_;//[xyz]
 std::vector<uint32_t>    vdmcLog2LodInverseScale_;//[xyz]
 std::vector<std::vector<uint8_t>>     vdmcLodDeltaQPValue_;//[lod][axis]
 std::vector<std::vector<uint8_t>>     vdmcLodDeltaQPSign_;//[lod][axis]
 bool                     vdmcDirectQuantizationEnabledFlag_ = false;

};



};  // namespace vmesh
