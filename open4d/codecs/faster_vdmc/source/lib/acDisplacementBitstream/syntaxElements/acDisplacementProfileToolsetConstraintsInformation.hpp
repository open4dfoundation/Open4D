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

namespace acdisplacement {

//H.8.1.3.1.3  Profile toolset constraints information syntax
class DisplProfileToolsetConstraintsInformation{
public:
 DisplProfileToolsetConstraintsInformation(){}
 ~DisplProfileToolsetConstraintsInformation(){}
 DisplProfileToolsetConstraintsInformation& operator=( const DisplProfileToolsetConstraintsInformation& ) = default;

 void                       setDptcOneDisplacementFrameOnlyFlag(bool  value) { dptcOneDisplacementFrameOnlyFlag_ = value; }
 void                       setDptcIntraFramesOnlyFlag(bool  value) { dptcIntraFramesOnlyFlag_ = value; }
 void                       setDptcReservedZero6bits(uint8_t  value) { dptcReservedZero6bits_ = value; }
 void                       setDptcNumReservedConstraintBytes(uint32_t  value) { dptcNumReservedConstraintBytes_ = value; }
 void                       setDptcReservedConstraintByte(std::vector<uint32_t>& value) { dptcReservedConstraintByte_ = value; }
 void                       setDptcReservedConstraintByte(size_t idx, uint32_t value) { dptcReservedConstraintByte_[idx] = value; }

 bool                       getDptcOneDisplacementFrameOnlyFlag() { return dptcOneDisplacementFrameOnlyFlag_; }
 bool                       getDptcIntraFramesOnlyFlag() { return dptcIntraFramesOnlyFlag_; }
 uint8_t                    getDptcReservedZero6bits() { return dptcReservedZero6bits_; }
 uint32_t                   getDptcNumReservedConstraintBytes() { return dptcNumReservedConstraintBytes_; }
 std::vector<uint32_t>&     getDptcReservedConstraintByte() { return dptcReservedConstraintByte_; }


private:
 bool                     dptcOneDisplacementFrameOnlyFlag_ = 0;
 bool                     dptcIntraFramesOnlyFlag_ = 0;
 uint8_t                  dptcReservedZero6bits_ = 0;
 uint32_t                 dptcNumReservedConstraintBytes_ = 0;
 std::vector<uint32_t>    dptcReservedConstraintByte_;


};

};  // namespace vdispl
