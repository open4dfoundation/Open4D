/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
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

#ifndef _EB_COLOR_SPACES_H_
#define _EB_COLOR_SPACES_H_

// mathematics
#include <glm/vec3.hpp>

namespace eb {

// converts yuv or rgb colors from [0,255] to [0,1]
inline void color256ToUnit( const glm::vec3& in256, glm::vec3& outUnit ) {
  const float divider = 1.0F / 255.0F;
  // shall be vectorized by compiler
  for ( glm::vec3::length_type i = 0; i < 3; ++i ) outUnit[i] = in256[i] * divider;
}

// converts yuv or rgb colors from [0,1] to [0,255]
inline void colorUnitTo256( const glm::vec3& in256, glm::vec3& outUnit ) {
  // shall be vectorized by compiler
  for ( glm::vec3::length_type i = 0; i < 3; ++i ) outUnit[i] = in256[i] * 255.0F;
}

// RGB and YUV components in [0,255]
inline void rgbToYuvBt709_256( const glm::vec3& rgb, glm::vec3& yuv ) {
  yuv[0] = 0.2126F * rgb[0] + 0.7152F * rgb[1] + 0.0722F * rgb[2];
  yuv[1] = -0.1146F * rgb[0] - 0.3854F * rgb[1] + 0.5000F * rgb[2] + 128.0F;
  yuv[2] = 0.5000F * rgb[0] - 0.4542F * rgb[1] - 0.0458F * rgb[2] + 128.0F;
}

// RGB and YUV components in [0,255]
inline glm::vec3 rgbToYuvBt709_256( const glm::vec3& rgb ) {
  return glm::vec3( 0.2126F * rgb[0] + 0.7152F * rgb[1] + 0.0722F * rgb[2],
                    -0.1146F * rgb[0] - 0.3854F * rgb[1] + 0.5000F * rgb[2] + 128.0F,
                    0.5000F * rgb[0] - 0.4542F * rgb[1] - 0.0458F * rgb[2] + 128.0F );
}

// RGB and YUV components in [0,255]
inline void yuvBt709ToRgb_256( const glm::vec3& yuv, glm::vec3& rgb ) {
  const float uc = yuv[1] - 128.0F;
  const float vc = yuv[2] - 128.0F;

  rgb[0] = yuv[0] + 1.57480F * vc;
  rgb[1] = yuv[0] - 0.18733F * uc - 0.46813F * vc;
  rgb[2] = yuv[0] + 1.85563F * uc;
}

// RGB and YUV components in [0,1]
inline void rgbToYuvBt709_unit( const glm::vec3& rgb, glm::vec3& yuv ) {
  yuv[0] = 0.2126F * rgb[0] + 0.7152F * rgb[1] + 0.0722F * rgb[2];
  yuv[1] = -0.1146F * rgb[0] - 0.3854F * rgb[1] + 0.5000F * rgb[2] + 0.5F;
  yuv[2] = 0.5000F * rgb[0] - 0.4542F * rgb[1] - 0.0458F * rgb[2] + 0.5F;
}

// RGB and YUV components in [0,1]
inline void yuvBt709ToRgb_unit( const glm::vec3& yuv, glm::vec3& rgb ) {
  const float uc = yuv[1] - 0.5F;
  const float vc = yuv[2] - 0.5F;

  rgb[0] = yuv[0] + 1.57480F * vc;
  rgb[1] = yuv[0] - 0.18733F * uc - 0.46813F * vc;
  rgb[2] = yuv[0] + 1.85563F * uc;
}

}  // namespace mm

#endif
