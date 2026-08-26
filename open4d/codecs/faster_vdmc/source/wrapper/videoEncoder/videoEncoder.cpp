/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
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

#include "videoEncoder.hpp"

#include "hmVideoEncoder.hpp"
#include "vtmVideoEncoder.hpp"
#include "vvVideoEncoder.hpp"
#if defined(USE_SHMAPP_VIDEO_CODEC)
#  include "shmAppVideoEncoder.hpp"
#endif

namespace vmesh {

template<typename T>
VideoEncoderId
VideoEncoder<T>::getDefaultCodecId() {
#if defined(USE_HM_VIDEO_CODEC)
  return VideoEncoderId::HM;
#endif
#if defined(USE_VTM_VIDEO_CODEC)
  return VideoEncoderId::VTM;
#endif
#if defined(USE_SHMAPP_VIDEO_CODEC)
  return VideoEncoderId::SHMAPP;
#endif
  return VideoEncoderId::UNKNOWN_VIDEO_ENCODER;
}

template<typename T>
bool
VideoEncoder<T>::checkCodecId(VideoEncoderId codecId) {
  switch (codecId) {
#if defined(USE_HM_VIDEO_CODEC)
  case VideoEncoderId::HM: break;
#endif
#if defined(USE_VTM_VIDEO_CODEC)
  case VideoEncoderId::VTM: break;
#endif
#if defined(USE_SHMAPP_VIDEO_CODEC)
    return VideoEncoderId::SHMAPP;
#endif
  default:
    printf("Error: codec id %d not supported \n", (int)codecId);
    return false;
    break;
  }
  return true;
}

template<typename T>
std::shared_ptr<VideoEncoder<T>>
VideoEncoder<T>::create(VideoEncoderId codecId) {
  switch (codecId) {
#if defined(USE_HM_VIDEO_CODEC)
  case VideoEncoderId::HM: return std::make_shared<hmVideoEncoder<T>>(); break;
#endif
#if defined(USE_VTM_VIDEO_CODEC)
  case VideoEncoderId::VTM:
    return std::make_shared<vtmVideoEncoder<T>>();
    break;
#endif
#if defined(USE_VV_VIDEO_CODEC)
  case VideoEncoderId::VV: return std::make_shared<vvVideoEncoder<T>>(); break;
#endif
#if defined(USE_SHMAPP_VIDEO_CODEC)
  case VideoEncoderId::SHMAPP:
    return std::make_shared<shmAppVideoEncoder<T>>();
    break;
#endif
  default:
    printf("Error VideoEncoder: codec id not supported ( %d ) \n",
           (int)codecId);
    exit(-1);
    break;
  }
  return nullptr;
}

template class VideoEncoder<uint8_t>;
template class VideoEncoder<uint16_t>;

}  // namespace vmesh
