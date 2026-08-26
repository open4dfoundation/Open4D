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

#include "videoDecoder.hpp"

#include "hmVideoDecoder.hpp"
#include "vtmVideoDecoder.hpp"
#include "vvVideoDecoder.hpp"
#if defined(USE_SHMAPP_VIDEO_CODEC)
#  include "shmAppVideoDecoder.hpp"
#endif

namespace vmesh {

template<typename T>
std::shared_ptr<VideoDecoder<T>>
VideoDecoder<T>::create(VideoEncoderId codecId) {
  printf("VideoDecoder: create codecId = %d \n", codecId);
  fflush(stdout);
  switch (codecId) {
#if defined(USE_HM_VIDEO_CODEC)
  case VideoEncoderId::HM: return std::make_shared<hmVideoDecoder<T>>(); break;
#endif
#if defined(USE_VTM_VIDEO_CODEC)
  case VideoEncoderId::VTM:
    return std::make_shared<vtmVideoDecoder<T>>();
    break;
#endif
#if 0  //defined(USE_VV_VIDEO_CODEC)  // TODO: broken
  case VideoEncoderId::VV: return std::make_shared<vvVideoDecoder<T>>(); break;
#endif
#if defined(USE_SHMAPP_VIDEO_CODEC)
  case VideoEncoderId::SHMAPP:
    return std::make_shared<shmAppVideoDecoder<T>>();
    break;
#endif
  default:
    printf("Error: codec id not supported \n");
    exit(-1);
    break;
  }
  return nullptr;
}

template class VideoDecoder<uint8_t>;
template class VideoDecoder<uint16_t>;

}  // namespace vmesh
