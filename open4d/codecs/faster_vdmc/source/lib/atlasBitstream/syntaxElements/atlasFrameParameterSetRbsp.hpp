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

#include "afpsVdmcExtension.hpp"
#include "afpsVpccExtension.hpp"
#include "atlasFrameTileInformation.hpp"

namespace atlas {

// 8.3.6.2 Atlas frame parameter set Rbsp syntax
// 8.3.6.2.1 General atlas frame parameter set Rbsp syntax
class AtlasFrameParameterSetRbsp {
public:
  AtlasFrameParameterSetRbsp() {}

  ~AtlasFrameParameterSetRbsp() {}
  AtlasFrameParameterSetRbsp&
  operator=(const AtlasFrameParameterSetRbsp&) = default;

  auto getAtlasFrameParameterSetId() const {
    return atlasFrameParameterSetId_;
  }
  auto getAtlasSequenceParameterSetId() const {
    return atlasSequenceParameterSetId_;
  }
  auto getOutputFlagPresentFlag() const { return outputFlagPresentFlag_; }
  auto getNumRefIdxDefaultActiveMinus1() const {
    return numRefIdxDefaultActiveMinus1_;
  }
  auto getAdditionalLtAfocLsbLen() const { return additionalLtAfocLsbLen_; }
  auto getRaw3dOffsetBitCountExplicitModeFlag() const {
    return raw3doffsetBitCountExplicitModeFlag_;
  }
  auto getExtensionFlag() const { return extensionFlag_; }
  auto getExtension6Bits() const { return extension6Bits_; }
  auto getVpccExtensionFlag() const { return vpccExtensionFlag_; }
  auto getVmcExtensionFlag() const { return vdmcExtensionFlag_; }
  auto getMivExtensionFlag() const { return mivExtensionFlag_; }
  auto getLodModeEnableFlag() const { return lodModeEnableFlag_; }

  auto& getAtlasFrameParameterSetId() { return atlasFrameParameterSetId_; }
  auto& getAtlasSequenceParameterSetId() {
    return atlasSequenceParameterSetId_;
  }
  auto& getOutputFlagPresentFlag() { return outputFlagPresentFlag_; }
  auto& getNumRefIdxDefaultActiveMinus1() {
    return numRefIdxDefaultActiveMinus1_;
  }
  auto& getAdditionalLtAfocLsbLen() { return additionalLtAfocLsbLen_; }
  auto& getRaw3dOffsetBitCountExplicitModeFlag() {
    return raw3doffsetBitCountExplicitModeFlag_;
  }
  auto& getExtensionFlag() { return extensionFlag_; }
  auto& getExtension6Bits() { return extension6Bits_; }
  auto& getVpccExtensionFlag() { return vpccExtensionFlag_; }
  auto& getVmcExtensionFlag() { return vdmcExtensionFlag_; }
  auto& getMivExtensionFlag() { return mivExtensionFlag_; }
  auto& getLodModeEnableFlag() { return lodModeEnableFlag_; }

  auto& getAtlasFrameTileInformation() { return atlasFrameTileInformation_; }
  const auto& getAtlasFrameTileInformation() const { return atlasFrameTileInformation_; }
  auto& getAfpsVpccExtension() { return afpsVpccExtension_; }
  auto& getAfveExtension() { return afpsVdmcExtension_; }
  const auto& getAfveExtension() const { return afpsVdmcExtension_; }

  void copyFrom(AtlasFrameParameterSetRbsp& refAfps) {
    atlasSequenceParameterSetId_  = refAfps.getAtlasSequenceParameterSetId();
    numRefIdxDefaultActiveMinus1_ = refAfps.getNumRefIdxDefaultActiveMinus1();
    additionalLtAfocLsbLen_       = refAfps.getAdditionalLtAfocLsbLen();
    lodModeEnableFlag_            = refAfps.getLodModeEnableFlag();
    raw3doffsetBitCountExplicitModeFlag_ =
      refAfps.getRaw3dOffsetBitCountExplicitModeFlag();
    extensionFlag_             = refAfps.getExtensionFlag();
    vpccExtensionFlag_         = refAfps.getVpccExtensionFlag();
    vdmcExtensionFlag_         = refAfps.getVmcExtensionFlag();
    afpsVpccExtension_         = refAfps.getAfpsVpccExtension();
    afpsVdmcExtension_         = refAfps.getAfveExtension();
    extension6Bits_            = refAfps.getExtension6Bits();
    atlasFrameTileInformation_ = refAfps.getAtlasFrameTileInformation();
  }

private:
  uint8_t                   atlasFrameParameterSetId_            = 0;
  uint8_t                   atlasSequenceParameterSetId_         = 0;
  bool                      outputFlagPresentFlag_               = 0;
  uint8_t                   numRefIdxDefaultActiveMinus1_        = 0;
  uint8_t                   additionalLtAfocLsbLen_              = 0;
  bool                      lodModeEnableFlag_                   = false;
  bool                      raw3doffsetBitCountExplicitModeFlag_ = false;
  bool                      extensionFlag_                       = false;
  uint8_t                   extension6Bits_                      = 0;
  bool                      vpccExtensionFlag_                   = false;
  bool                      mivExtensionFlag_                    = false;
  bool                      vdmcExtensionFlag_                   = false;
  AtlasFrameTileInformation atlasFrameTileInformation_;
  AfpsVpccExtension         afpsVpccExtension_;
  AfpsVdmcExtension         afpsVdmcExtension_;
};

};  // namespace vmesh
