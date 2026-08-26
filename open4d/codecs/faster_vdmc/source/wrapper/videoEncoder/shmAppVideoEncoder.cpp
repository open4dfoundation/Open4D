/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
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
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
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
#ifdef USE_SHMAPP_VIDEO_CODEC
#  include "util/image.hpp"
#  include "shmAppVideoEncoder.hpp"
#  include "util/misc.hpp"
#  include <sstream>

namespace vmesh {

template<typename T>
shmAppVideoEncoder<T>::shmAppVideoEncoder() = default;
template<typename T>
shmAppVideoEncoder<T>::~shmAppVideoEncoder() = default;

template<typename T>
void
shmAppVideoEncoder<T>::encode(FrameSequence<T>&       videoSrc,
                              VideoEncoderParameters& params,
                              std::vector<uint8_t>&   bitstream,
                              FrameSequence<T>&       videoRec) {
  const auto frameCount     = videoSrc.frameCount();
  const int  numLayerMinus1 = params.numLayerMinus1_;
  bool       isAI           = endsWith(params.encoderConfig_,
                       "ai-" + std::to_string(numLayerMinus1 + 1) + "L.cfg");
  //SHVC Encode
  std::cout << "SHM Encoding Start" << std::endl;
  std::stringstream cmd;
  cmd.str("");
  cmd << params.encoderPath_ << " "
      << " -c " << params.encoderConfig_
      << " --FramesToBeEncoded=" << frameCount << " -b "
      << params.binFileName_;
  for (int i = 0; i <= numLayerMinus1; i++) {
    cmd << " -i" << std::to_string(i) << " " << params.srcYuvFileNameLayer_[i]
        << " -wdt" << std::to_string(i) << " "
        << std::to_string(videoSrc.width() >> (numLayerMinus1 - i)) << " -hgt"
        << std::to_string(i) << " "
        << std::to_string(videoSrc.height() >> (numLayerMinus1 - i));
    if ((i == numLayerMinus1) && !isAI)
      cmd << " -q" << std::to_string(i) << " "
          << std::to_string(params.qp_ - 2);
    else
      cmd << " -q" << std::to_string(i) << " " << std::to_string(params.qp_);
  }
  cmd << " -o" << std::to_string(numLayerMinus1) << " "
      << params.recYuvFileNameLayer_;
  std::cout << cmd.str() << std::endl;
  system(cmd.str().c_str());
  std::ifstream        file(params.binFileName_.c_str(),
                     std::ios::in | std::ios::binary);
  std::vector<uint8_t> textureBitstream((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
  bitstream = textureBitstream;
}

template class shmAppVideoEncoder<uint8_t>;
template class shmAppVideoEncoder<uint16_t>;

}  // namespace vmesh

#endif
