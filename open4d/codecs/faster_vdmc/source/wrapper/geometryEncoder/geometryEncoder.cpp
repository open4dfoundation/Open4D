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

#include "geometryEncoder.hpp"

#include "dracoGeometryEncoder.hpp"
#include "mpegGeometryEncoder.hpp"

namespace vmesh {

template<typename T>
GeometryCodecId
GeometryEncoder<T>::getDefaultCodecId() {
#ifdef USE_DRACO_GEOMETRY_CODEC
  return GeometryCodecId::DRACO;
#endif
  printf("Error: geometry codec not set \n");
  return GeometryCodecId::UNKNOWN_GEOMETRY_CODEC;
}

template<typename T>
bool
GeometryEncoder<T>::checkCodecId(GeometryCodecId codecId) {
  switch (codecId) {
#ifdef USE_DRACO_GEOMETRY_CODEC
  case GeometryCodecId::DRACO: break;
#endif
  case GeometryCodecId::MPEG: break;
  default:
    printf("Error: codec id %d not supported \n", (int)codecId);
    return false;
    break;
  }
  return true;
}

template<typename T>
std::shared_ptr<GeometryEncoder<T>>
GeometryEncoder<T>::create(GeometryCodecId codecId) {
  switch (codecId) {
#ifdef USE_DRACO_GEOMETRY_CODEC
  case GeometryCodecId::DRACO:
    return std::make_shared<DracoGeometryEncoder<T>>();
    break;
#endif
  case GeometryCodecId::MPEG:
    return std::make_shared<MpegGeometryEncoder<T>>();
    break;
  default:
    printf("Error GeometryEncoder: codec id not supported ( %d ) \n",
           (int)codecId);
    exit(-1);
    break;
  }
  return nullptr;
}

template class GeometryEncoder<float>;
template class GeometryEncoder<double>;

}  // namespace vmesh