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
#include "colourConverter.hpp"

#if defined(USE_HDRTOOLS)

#  include "hdrToolsLibColourConverter.hpp"
#  include "hdrToolsLibColourConverterImpl.hpp"

using namespace vmesh;

template<typename T>
HdrToolsColourConverter<T>::HdrToolsColourConverter() = default;
template<typename T>
HdrToolsColourConverter<T>::~HdrToolsColourConverter() = default;

template<typename T>
void
HdrToolsColourConverter<T>::initialize(std::string configFile) {
  converter.reset();
  converter = std::make_shared<HdrToolsColourConverterImpl<T>>();
  converter->initialize(configFile);
}

template<typename T>
void
HdrToolsColourConverter<T>::convert(FrameSequence<T>& src) {
  if (src.frameCount() > 0) {
    for (int i = 0; i < src.frameCount(); i++) { convert(src[i]); }
    src.colourSpace() = src[0].colourSpace();
  }
}

template<typename T>
void
HdrToolsColourConverter<T>::convert(Frame<T>& src) {
  Frame<T> dst;
  convert(src, dst);
  src.swap(dst);
}

template<typename T>
void
HdrToolsColourConverter<T>::convert(FrameSequence<T>& src,
                                    FrameSequence<T>& dst) {
  converter->convert(src, dst);
}

template<typename T>
void
HdrToolsColourConverter<T>::convert(Frame<T>& src, Frame<T>& dst) {
  converter->convert(src, dst);
}

namespace vmesh {
template class HdrToolsColourConverter<uint8_t>;
template class HdrToolsColourConverter<uint16_t>;
}  // namespace vmesh
#endif  //~USE_HDRTOOLS
