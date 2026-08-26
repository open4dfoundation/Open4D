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

#include "v3cCommon.hpp"
#include "../atlasBitstream/atlasWriter.hpp"
#include "../basemeshBitstream/baseMeshWriter.hpp"
#include "../acDisplacementBitstream/acDisplacementWriter.hpp"
#include <map>

namespace vmesh {

class V3CWriter {
public:
  V3CWriter();
  ~V3CWriter();

  void   report(SampleStreamV3CUnit& ssvu,
                Bitstream&           bitstream,
                size_t               v3cHeaderBytes);
  size_t write(SampleStreamV3CUnit& ssvu,
               Bitstream&           bitstream,
               uint32_t             forcedSsvhUnitSizePrecisionBytes = 0);
  int    encode(V3cBitstream&        syntax,
                V3CParameterSet&     vps,
                SampleStreamV3CUnit& ssvu);

#if defined(BITSTREAM_TRACE)
  void setLogger(Logger& logger) {
    logger_ = &logger;
    atlasSubStream_.setLogger(logger);
    baseMeshSubStream_.setLogger(logger);
    acDisplacementSubStream_.setLogger(logger);
  }
#endif
private:
  atlas::AtlasWriter                   atlasSubStream_;
  basemesh::BaseMeshWriter             baseMeshSubStream_;
  acdisplacement::AcDisplacementWriter acDisplacementSubStream_;
  // V3C Unit
  void addV3CUnit(V3cBitstream&        syntax,
                  V3CParameterSet&     vps,
                  SampleStreamV3CUnit& ssvu,
                  size_t               unitIdx,
                  V3CUnitType          v3cUnitType);

  // 8.2 Specification of syntax functions and descriptors
  bool byteAligned(Bitstream& bitstream);
  bool lengthAligned(Bitstream& bitstream);
  bool moreDataInPayload(Bitstream& bitstream);
  bool payloadExtensionPresent(Bitstream& bitstream);

  // 8.3.2 V3C unit syntax
  // 8.3.2.1 General V3C unit syntax
  void v3cUnit(V3cBitstream&    syntax,
               V3CParameterSet& vps,
               Bitstream&       bitstream,
               size_t           unitIdx,
               V3CUnitType      V3CUnitType);

  // 8.3.2.2 V3C unit header syntax
  void v3cUnitHeader(V3cBitstream&    syntax,
                     V3CParameterSet& vps,
                     Bitstream&       bitstream,
                     size_t           unitIndex,
                     V3CUnitType      V3CUnitType);

  // 8.3.2.3 V3C unit payload syntax
  void v3cUnitPayload(V3cBitstream&    syntax,
                      V3CParameterSet& vps,
                      Bitstream&       bitstream,
                      size_t           unitIdx,
                      V3CUnitType      V3CUnitType);

  void videoSubStream(V3cBitstream& syntax,
                      Bitstream&    bitstream,
                      size_t        index,
                      V3CUnitType   V3CUnitType);

  // 8.3.3 Byte alignment syntax
  void byteAlignment(Bitstream& bitstream);

  // 8.3.4 V3C parameter set syntax
  // 8.3.4.1 General V3C parameter set syntax
  void v3cParameterSet(V3CParameterSet& vps,
                       V3cBitstream&    syntax,
                       Bitstream&       bitstream);

  // 8.3.4.2 Profile, tier, and level syntax
  void profileTierLevel(ProfileTierLevel& ptl, Bitstream& bitstream);

  // 8.3.4.3 Occupancy parameter information syntax
  void occupancyInformation(OccupancyInformation& oi,
                            uint32_t              atlasId,
                            Bitstream&            bitstream);

  // 8.3.4.4 Geometry information syntax
  void geometryInformation(GeometryInformation& gi,
                           V3CParameterSet&     vps,
                           uint32_t             atlasId,
                           Bitstream&           bitstream);

  // 8.3.4.5 Attribute information syntax
  void attributeInformation(AttributeInformation& ai,
                            V3CParameterSet&      vps,
                            uint32_t              atlasId,
                            Bitstream&            bitstream);

  // 8.3.4.6 Profile toolset constraints information syntax
  void profileToolsetConstraintsInformation(
    ProfileToolsetConstraintsInformation& ptci,
    Bitstream&                            bitstream);

  // 8.3.4.7 Packing information syntax
  // 8.3.4.8 VPS extension syntax
  void vpsExtension(V3CParameterSet& vps, uint8_t index, Bitstream& bitstream);
  // 8.3.4.9 Packed video extension syntax
  // 8.3.4.10 Length alignment syntax
  void lengthAlignment(Bitstream& bitstream);
  // 8.3.4.11 V3C parameter set V-DMC extension syntax
  void vpsPackedVideoExtension(V3CParameterSet&         vps,
                               VpsPackedVideoExtension& packedVideoExt,
                               Bitstream&               bitstream);
  void vpsVdmcExtension(V3CParameterSet&  vps,
                        VpsVdmcExtension& ext,
                        Bitstream&        bitstream);
  // 8.3.4.7  Packing information syntax
  void packingInformation(PackingInformation& pi,
                          V3CParameterSet&    vps,
                          uint32_t            atlasId,
                          Bitstream&          bitstream);
  // 8.3.4.12 Displacement information syntax
  void displacementInformation(DisplacementInformation& di,
                               uint32_t                 atlasId,
                               Bitstream&               bitstream);

  // C.2 Sample stream V3C unit syntax and semantics
  // C.2.1 Sample stream V3C header syntax
  void sampleStreamV3CHeader(Bitstream& bitstream, SampleStreamV3CUnit& ssvu);

  // C.2.2 Sample stream NAL unit syntax
  void sampleStreamV3CUnit(Bitstream&           bitstream,
                           SampleStreamV3CUnit& ssvu,
                           V3CUnit&             v3cUnit);

#if defined(BITSTREAM_TRACE)
  Logger* logger_ = nullptr;
#endif
};

};  // namespace vmesh
