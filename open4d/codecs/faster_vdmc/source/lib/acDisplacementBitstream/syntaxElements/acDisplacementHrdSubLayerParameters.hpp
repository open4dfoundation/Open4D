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

namespace acdisplacement {

    // J.13.2.3	Displacement sub-layer HRD parameters syntax
    class DisplHrdSubLayerParameters {
    public:
        DisplHrdSubLayerParameters() {}
        ~DisplHrdSubLayerParameters() {
            displBitRateValueMinus1_.clear();
            cdisplSizeValueMinus1_.clear();
            displCbrFlag_.clear();
        }
        DisplHrdSubLayerParameters& operator=(const DisplHrdSubLayerParameters&) = default;
        void                   allocate(size_t size) {
            displBitRateValueMinus1_.resize(size, 0);
            cdisplSizeValueMinus1_.resize(size, 0);
            displCbrFlag_.resize(size, false);
        }
        auto size() { return displBitRateValueMinus1_.size(); }

        auto getDisplBitRateValueMinus1(size_t i) const { return displBitRateValueMinus1_[i]; }
        auto& getDisplBitRateValueMinus1(size_t i) { return displBitRateValueMinus1_[i]; }
        auto getCdisplSizeValueMinus1(size_t i) const { return cdisplSizeValueMinus1_[i]; }
        auto& getCdisplSizeValueMinus1(size_t i) { return cdisplSizeValueMinus1_[i]; }
        auto getDisplCbrFlag(size_t i) const { return displCbrFlag_[i]; }
        auto& getDisplCbrFlag(size_t i) { return displCbrFlag_[i]; }


    private:
        std::vector<uint32_t> displBitRateValueMinus1_;
        std::vector<uint32_t> cdisplSizeValueMinus1_;
        std::vector<uint8_t>  displCbrFlag_;
    };
};  // namespace acdisplacement
