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
#if defined(USE_SHMAPP_VIDEO_CODEC)
#  include "shmAppVideoDecoder.hpp"
#  include <sstream>
namespace vmesh {

template<typename T>
shmAppVideoDecoder<T>::shmAppVideoDecoder() {}
template<typename T>
shmAppVideoDecoder<T>::~shmAppVideoDecoder() {}

template<typename T>
void
shmAppVideoDecoder<T>::decode(const std::vector<uint8_t>& bitstream,
                              FrameSequence<T>&           video,
                              std::vector<size_t>&        videoBitDepth,
                              size_t                      targetLoD,
                              const std::string&          decoderPath,
                              const std::string&          parameters) {
  std::ofstream fout;
  std::string   decYuv =
    parameters + "_dec_" + std::to_string(targetLoD) + ".yuv";
  std::string binFileName = parameters + ".bin";
  fout.open(binFileName, std::ios::out | std::ios::binary);
  fout.write((const char*)&bitstream[0], bitstream.size());
  fout.close();
  std::stringstream cmd;
  std::string       decoder = decoderPath;
  cmd.str("");
  cmd << " " << decoder << " "
      << " -b " << binFileName << " "
      << " -olsidx " << std::to_string(targetLoD) << " "
      << " -o" << std::to_string(targetLoD) << " " << decYuv;

  std::cout << cmd.str() << std::endl;
  system(cmd.str().c_str());
  video.load(decYuv);
  remove(binFileName.c_str());
}

template class shmAppVideoDecoder<uint8_t>;
template class shmAppVideoDecoder<uint16_t>;

}  // namespace vmesh
#endif
