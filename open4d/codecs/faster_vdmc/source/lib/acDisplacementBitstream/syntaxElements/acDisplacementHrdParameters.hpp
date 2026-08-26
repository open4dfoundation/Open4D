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

#include "acDisplacementHrdSubLayerParameters.hpp"

namespace acdisplacement {

    // J.13.2.2	Basemesh HRD parameters syntax 
    class DisplHrdParameters {
    public:
        DisplHrdParameters() {
            subLayerParameters_[0].resize(maxNumSubLayersMinus1_);
            subLayerParameters_[1].resize(maxNumSubLayersMinus1_);
        }
        ~DisplHrdParameters() {
            subLayerParameters_[0].clear();
            subLayerParameters_[1].clear();
        }
        DisplHrdParameters& operator=(const DisplHrdParameters&) = default;

        auto getMaxNumSubLayersMinus1() { return maxNumSubLayersMinus1_; }

        auto getDisplNalParametersPresentFlag() const { return displNalParametersPresentFlag_; }
        auto& getDisplNalParametersPresentFlag() { return displNalParametersPresentFlag_; }
        auto getDisplClParametersPresentFlag() const { return displClParametersPresentFlag_; }
        auto& getDisplClParametersPresentFlag() { return displClParametersPresentFlag_; }
        auto getDisplBitRateScale() const { return displBitRateScale_; }
        auto& getDisplBitRateScale() { return displBitRateScale_; }
        auto getCdisplbSizeScale() const { return cdisplbSizeScale_; }
        auto& getCdisplbSizeScale() { return cdisplbSizeScale_; }
        auto getCdisplbSizeDuScale() const { return cdisplbSizeDuScale_; }
        auto& getCdisplbSizeDuScale() { return cdisplbSizeDuScale_; }
        auto getInitialCdisplbRemovalDelayLengthMinus1() const { return initialCdisplbRemovalDelayLengthMinus1_; }
        auto& getInitialCdisplbRemovalDelayLengthMinus1() { return initialCdisplbRemovalDelayLengthMinus1_; }
        auto getAuCdisplbRemovalDelayLengthMinus1() const { return auCdisplbRemovalDelayLengthMinus1_; }
        auto& getAuCdisplbRemovalDelayLengthMinus1() { return auCdisplbRemovalDelayLengthMinus1_; }
        auto getDdisplbOutputDelayLengthMinus1() const { return ddisplbOutputDelayLengthMinus1_; }
        auto& getDdisplbOutputDelayLengthMinus1() { return ddisplbOutputDelayLengthMinus1_; }
        auto  getDisplFixedRateGeneralFlag_(size_t i) const { return displFixedRateGeneralFlag_[i]; }
        auto& getDisplFixedRateGeneralFlag(size_t i) { return displFixedRateGeneralFlag_[i]; }
        auto  getDisplFixedRateWithinCbmsFlag(size_t i) const { return displFixedRateWithinCbmsFlag_[i]; }
        auto& getDisplFixedRateWithinCbmsFlag(size_t i) { return displFixedRateWithinCbmsFlag_[i]; }
        auto  getDisplElementalDurationInTcMinus1(size_t i) const { return displElementalDurationInTcMinus1_[i]; }
        auto& getDisplElementalDurationInTcMinus1(size_t i) { return displElementalDurationInTcMinus1_[i]; }
        auto  getDisplHrdReservedFlag(size_t i) const { return displHrdReservedFlag_[i]; }
        auto& getDisplHrdReservedFlag(size_t i) { return displHrdReservedFlag_[i]; }
        auto  getCdislpdCntMinus1(size_t i) const { return cdispldCntMinus1_[i]; }
        auto& getCdispldCntMinus1(size_t i) { return cdispldCntMinus1_[i]; }
        auto& getDisplHdrSubLayerParameters(size_t i, size_t j) const { return subLayerParameters_[i][j]; }
        auto& getDisplHdrSubLayerParameters(size_t i, size_t j) { return subLayerParameters_[i][j]; }

    private:
        //TODO: check if the default values are correct
        static const int32_t               maxNumSubLayersMinus1_ = 0;
        bool                               displNalParametersPresentFlag_ = 0;
        bool                               displClParametersPresentFlag_ = 0;
        uint8_t                            displBitRateScale_ = 0;
        uint8_t                            cdisplbSizeScale_ = 0;
        uint8_t                            cdisplbSizeDuScale_ = 0;
        uint8_t                            initialCdisplbRemovalDelayLengthMinus1_ = 0;
        uint8_t                            auCdisplbRemovalDelayLengthMinus1_ = 0;
        uint8_t                            ddisplbOutputDelayLengthMinus1_ = 0;
        std::vector<uint8_t>               displFixedRateGeneralFlag_;
        std::vector<uint8_t>               displFixedRateWithinCbmsFlag_;
        std::vector<uint32_t>              displElementalDurationInTcMinus1_;
        std::vector<uint8_t>               displHrdReservedFlag_;
        std::vector<uint32_t>              cdispldCntMinus1_;
        std::vector<DisplHrdSubLayerParameters> subLayerParameters_[2];
    };

};  // namespace acdisplacement
