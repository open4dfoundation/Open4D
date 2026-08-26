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
#pragma once

#include "util/mesh.hpp"

namespace vmesh {

struct GeometryEncoderParameters {
  std::string encoderPath_   = {};
  std::string srcFileName_   = {};
  std::string binFileName_   = {};
  std::string recFileName_   = {};
  std::string logFileName_   = {};
  std::string encoderConfig_ = {};
  int32_t     qp_ =
    11;  // quantization bits for the position attribute, default=11.
  int32_t qt_ =
    10;  // quantization bits for the texture coordinate attribute, default=10.
  int32_t qn_ =
    -1;  // quantization bits for the normal vector attribute, default=8.
  int32_t qg_ = -1;  // quantization bits for any generic attribute, default=8.
  int32_t cl_ = 10;  // compression level [0-10], most=10, least=0, default=7.
  bool    dracoUsePosition_  = false;  // draco use position mode (m60340)
  bool    dracoUseUV_        = false;  // draco use uv mode (m60293)
  bool    dracoMeshLossless_ = false;  // whether mesh is lossless (m60289)
  std::string traversal_     = {"degree"};  // vertex traversal method
  std::string reindexOutput_ = {
    "degree"};  // perform a reindex on decoded model
  bool baseMeshDeduplication_ =
    false;  // add position and attributes deduplication information
  bool reverseUnification_ = false;  //reverse unified vertices
  bool verbose             = true;   // encoder verbosity switch
  int  profile =
    0;  // if set to n > 0, encode/decode will be run n times to get precise time profiling

  bool    encodeNormals     = false;
  int32_t predNormal        = 3;
  bool    normalsOctahedral = true;
  bool    entropyPacket     = false;
  int32_t qpOcta_           = -1;
  int32_t predGeneric       = 0;
};

template<class T>
class GeometryEncoder {
public:
  GeometryEncoder()  = default;
  ~GeometryEncoder() = default;

  static std::shared_ptr<GeometryEncoder<T>> create(GeometryCodecId codecId);
  static GeometryCodecId                     getDefaultCodecId();
  static bool checkCodecId(GeometryCodecId codecId);

  virtual void encode(TriangleMesh<T>&           src,
                      GeometryEncoderParameters& params,
                      std::vector<uint8_t>&      bitstream,
                      TriangleMesh<T>&           rec) = 0;
  int          getMeshCodingHeaderLength() { return headerLengthInBytes_; }
  void         setMeshCodingHeaderLength(int64_t headerLengthInBytes) {
    headerLengthInBytes_ = headerLengthInBytes;
    return;
  }

private:
  int headerLengthInBytes_ = 0;
};

}  // namespace vmesh
