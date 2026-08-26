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

namespace atlas {

class AtlasCameraParameters {
public:
  AtlasCameraParameters() {}
  ~AtlasCameraParameters() {}
  AtlasCameraParameters& operator=(const AtlasCameraParameters&) = default;

  auto getCameraModel() const { return cameraModel_; }
  auto getScaleEnabledFlag() const { return scaleEnabledFlag_; }
  auto getOffsetEnabledFlag() const { return offsetEnabledFlag_; }
  auto getRotationEnabledFlag() const { return rotationEnabledFlag_; }
  auto getScaleOnAxis(size_t index) const { return scaleOnAxis_[index]; }
  auto getOffsetOnAxis(size_t index) const { return offsetOnAxis_[index]; }
  auto getRotation(size_t index) const { return rotation_[index]; }

  auto& getCameraModel() { return cameraModel_; }
  auto& getScaleEnabledFlag() { return scaleEnabledFlag_; }
  auto& getOffsetEnabledFlag() { return offsetEnabledFlag_; }
  auto& getRotationEnabledFlag() { return rotationEnabledFlag_; }
  auto& getScaleOnAxis(size_t index) { return scaleOnAxis_[index]; }
  auto& getOffsetOnAxis(size_t index) { return offsetOnAxis_[index]; }
  auto& getRotation(size_t index) { return rotation_[index]; }

private:
  uint8_t  cameraModel_         = 0;
  bool     scaleEnabledFlag_    = false;
  bool     offsetEnabledFlag_   = false;
  bool     rotationEnabledFlag_ = false;
  uint32_t scaleOnAxis_[3]      = {0, 0, 0};
  int32_t  offsetOnAxis_[3]     = {0, 0, 0};
  int32_t  rotation_[3]         = {0, 0, 0};
};

};  // namespace vmesh
