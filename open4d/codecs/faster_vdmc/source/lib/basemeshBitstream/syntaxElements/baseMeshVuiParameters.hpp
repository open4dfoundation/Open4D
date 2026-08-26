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

#include "baseMeshHrdParameters.hpp"
#include "baseMeshHrdSubLayerParameters.hpp"

namespace basemesh {

// H.15.2.1	VUI parameter syntax
class BasemeshVuiParameters {
public:
    auto  getTimingInfoPresentFlag() const { return bmTimingInfoPresentFlag_; }
    auto& getTimingInfoPresentFlag() { return bmTimingInfoPresentFlag_; }
    auto  getNumUnitsInTick() const { return bmNumUnitsInTick_; }
    auto& getNumUnitsInTick() { return bmNumUnitsInTick_; }
    auto  getTimeScale() const { return bmTimeScale_; }
    auto& getTimeScale() { return bmTimeScale_; }
    auto  getMfocProportionalToTimingFlag() const { return mfocProportionalToTimingFlag_; }
    auto& getMfocProportionalToTimingFlag() { return mfocProportionalToTimingFlag_; }
    auto getNumTicksMfocDiffOneMinus1() const { return numTicksMfocDiffOneMinus1_; }
    auto& getNumTicksMfocDiffOneMinus1() { return numTicksMfocDiffOneMinus1_; }
    auto getHrdParametersPresentFlag() const { return hrdParametersPresentFlag_;}
    auto& getHrdParametersPresentFlag() { return hrdParametersPresentFlag_; }
    auto& getHrdParameters() const { return hrdParameters_; }
    auto& getHrdParameters() { return hrdParameters_; }

private:
    bool     bmTimingInfoPresentFlag_ = 0;
    uint32_t bmNumUnitsInTick_ = 1001;
    uint32_t bmTimeScale_ = 60000;
    bool     mfocProportionalToTimingFlag_ = false;
    uint32_t numTicksMfocDiffOneMinus1_ = 0;
    bool     hrdParametersPresentFlag_ = false;

    BaseMeshHrdParameters hrdParameters_;
};
};
