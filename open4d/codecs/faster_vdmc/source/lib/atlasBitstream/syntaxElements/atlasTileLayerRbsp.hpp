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

#include "atlasTileHeader.hpp"
#include "atlasTileDataUnit.hpp"
#include "sei.hpp"

namespace atlas {

// 8.3.6.10  Atlas tile layer RBSP syntax
class AtlasTileLayerRbsp {
public:
  AtlasTileLayerRbsp() {}
  ~AtlasTileLayerRbsp() {}

  AtlasTileLayerRbsp& operator=(const AtlasTileLayerRbsp&) = default;

  AtlasTileHeader&         getHeader() { return header_; }
  AtlasTileDataUnit&       getDataUnit() { return dataUnit_; }
  const AtlasTileHeader&   getHeader() const { return header_; }
  const AtlasTileDataUnit& getDataUnit() const { return dataUnit_; }

  auto getTileOrder() const { return _tileOrder; }
  auto getEncFrameIndex() const { return _encFrameIndex; }
  auto getEncTileIndex() const { return _encTileIndex; }
  auto getSEI() const { return sei_; }

  auto& getTileOrder() { return _tileOrder; }
  auto& getEncFrameIndex() { return _encFrameIndex; }
  auto& getEncTileIndex() { return _encTileIndex; }

  auto& getSEI() { return sei_; }

private:
  AtlasTileHeader   header_;
  AtlasTileDataUnit dataUnit_;
  size_t            _tileOrder           = 0;
  size_t            _encFrameIndex       = -1;
  size_t            _encTileIndex        = -1;
  PCCSEI     sei_;
};

};  // namespace vmesh
