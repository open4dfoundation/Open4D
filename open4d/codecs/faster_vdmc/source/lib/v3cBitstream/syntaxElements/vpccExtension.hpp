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

//#include "v3cCommon.hpp"
#include "packingInformation.hpp"

namespace vmesh {

// H.7.3.4.1	VPS V-PCC extension syntax
class VpsVpccExtension {
public:
  VpsVpccExtension() {}
  ~VpsVpccExtension() {}
  VpsVpccExtension& operator=(const VpsVpccExtension&) = default;

private:
};

class VpsPackedVideoExtension {
public:
  VpsPackedVideoExtension() { setAtlasCount(0); }
  VpsPackedVideoExtension(int atlasCount) {
    setAtlasCount(atlasCount);
  }
  ~VpsPackedVideoExtension() {
    packedVideoPresentFlags_.clear();
    packingInformation_.clear();
  }
  VpsPackedVideoExtension& operator=(const VpsPackedVideoExtension&) = default;

  void setAtlasCount(int atlasCount) {
    atlas_count_ = atlasCount;
    packedVideoPresentFlags_.resize(atlasCount);
    packingInformation_.resize(atlasCount);
  }
  int getAtlasCount() { return atlas_count_;}
  bool getPackedVideoPresentFlag(size_t index) const {
    return packedVideoPresentFlags_[index];
  }
  void setPackedVideoPresentFlag( size_t index, bool value ) { packedVideoPresentFlags_[index] = value; }

  PackingInformation& getPackingInformation(size_t index) {
    return packingInformation_[index];
  }
  const PackingInformation& getPackingInformation(size_t index) const {
    return packingInformation_[index];
  }
  void setPackingInformation( size_t index, PackingInformation value ) { packingInformation_[index] = value; }

private:
  uint8_t                            atlas_count_;
  std::vector<bool>                  packedVideoPresentFlags_;
  std::vector<PackingInformation>    packingInformation_;
};
};  // namespace vmesh
