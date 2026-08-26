/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2023, ISO/IEC
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

#if defined(BITSTREAM_TRACE)
#include "util/logger.hpp"
#endif

namespace eb {

class Bitstream;
class MeshCoding;
class MeshCodingHeader;
class MeshPositionEncodingParameters;
class MeshPositionDequantizeParameters;
class MeshAttributesEncodingParameters;
class MeshAttributesDequantizeParameters;
class MeshPositionCodingPayload;
class MeshPositionDeduplicateInformation;
class MeshAttributeCodingPayload;
class MeshAttributeExtraData;
class MeshTexCoordStretchExtraData;
class MeshMaterialIDExtraData;
class MeshAttributeDeduplicateInformation;
class MeshDifferenceInformation;
class MeshNormalOctrahedralExtraData; 

class EbReader {
public:
  EbReader();
  ~EbReader();

  void read(Bitstream& bitstream, MeshCoding& meshCoding);

private:

  // I.8.3.16	Padding to byte alignment syntax => move before in WD
  void padding_to_byte_alignment(Bitstream& bitstream);

  // I.8.3.1 General Mesh coding syntax
  void meshCoding(Bitstream& bitstream, MeshCoding& mc);

  // I.8.3.2 Mesh coding header syntax
  void meshCodingHeader(Bitstream& bitstream, MeshCodingHeader& mch);

  // I.8.3.3 Mesh position encoding parameters syntax
  void meshPositionEncodingParameters(Bitstream& bitstream,
                                      MeshPositionEncodingParameters& mpep);

  // I.8.3.4 Mesh position dequantize parameters syntax
  void
  meshPositionDequantizeParameters(Bitstream&                        bitstream,
                                   MeshPositionDequantizeParameters& mpdp);

  // I.8.3.5 Mesh attributes encoding parameters syntax
  void
  meshAttributesEncodingParameters(Bitstream&                        bitstream,
                                   MeshAttributesEncodingParameters& maep);
                                   
  // I.8.3.6 Mesh attributes dequantize parameters syntax
  void meshAttributesDequantizeParameters(Bitstream&        bitstream,
                                          MeshCodingHeader& mch,
                                          uint32_t          index);

  // I.8.3.7 Mesh position coding payload syntax
  void meshPositionCodingPayload(Bitstream&                 bitstream,
                                 MeshPositionCodingPayload& mpcp,
                                 MeshCodingHeader&          mch);

  // I.8.3.8 Mesh position deduplicate information syntax
  void
  meshPositionDeduplicateInformation(Bitstream& bitstream,
                                     MeshPositionDeduplicateInformation& mpdi,
                                     MeshCodingHeader& mch);

  // I.8.3.9 Mesh difference information syntax(m66188)
  void meshDifferenceInformation(Bitstream& bitstream,
                                 MeshDifferenceInformation& mdi);

  // I.8.3.10 Mesh attribute coding payload syntax
  void meshAttributeCodingPayload(Bitstream& bitstream,
                                  MeshAttributeCodingPayload& macp,
                                  MeshCodingHeader& mch);

  // I.8.3.11 Mesh extra attribute data syntax
  void meshAttributeExtraData(Bitstream&                        bitstream,
                              MeshAttributeExtraData&           mead,
                              MeshCodingHeader&                 mch,
                              MeshAttributesEncodingParameters& maep,
                              uint32_t                          index);

  // I.8.3.12 Mesh texcoord stretch extra data syntax
  void meshTexCoordStretchExtraData(Bitstream&                    bitstream,
                                    MeshTexCoordStretchExtraData& mtcsed);

  // I.8.3.13 Mesh material ID extra data syntax
  void meshMaterialIDExtraData(Bitstream& bitstream,
                               MeshMaterialIDExtraData& mmied);

  // I.8.3.14 Mesh normal octahedral extra data syntax 
  void meshNormalOctrahedralExtraData(Bitstream& bitstream, MeshNormalOctrahedralExtraData& mnoed);

  // I.8.3.15 Mesh attribute deduplicate information syntax
  void
  meshAttributeDeduplicateInformation(Bitstream& bitstream,
                                      MeshAttributeDeduplicateInformation& madi,
                                      MeshCodingHeader& mch,
                                      uint32_t index);



#if defined(BITSTREAM_TRACE)
public:
  void setLogger(Logger& logger) { logger_ = &logger; }

private:
  Logger* logger_ = nullptr;
#endif

private:
    MeshCoding* global_ = nullptr;
public:
    MeshCoding& getGlobal() { return *global_; }
    void        setGlobal(MeshCoding& meshCoding) { global_ = &meshCoding; }

};

};  // namespace eb
