/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2025, ISO/IEC
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

#include <list>
#include "v3cCommon.hpp"
#include "writerCommon.hpp"
#include "bitstream.hpp"
#include "bitstreamStat.hpp"
#include "v3cBitstream.hpp"
#include "v3cWriter.hpp"

using namespace vmesh;

V3CWriter::V3CWriter()  = default;
V3CWriter::~V3CWriter() = default;
void
V3CWriter::report(SampleStreamV3CUnit& ssvu,
                  Bitstream&           bitstream,
                  size_t               v3cHeaderBytes) {
  uint64_t v3cUnitSize[NUM_V3C_UNIT_TYPE] = {
    0,
  };
  for (auto& vu : ssvu.getV3CUnit()) {
    V3CUnitType vuType = vu.getType();
    size_t      vuSize = vu.getSize();
    switch (vuType) {
    case V3C_VPS: v3cUnitSize[V3C_VPS] += vuSize; break;
    case V3C_AD: v3cUnitSize[V3C_AD] += vuSize; break;
    case V3C_OVD: v3cUnitSize[V3C_OVD] += vuSize; break;
    case V3C_GVD: v3cUnitSize[V3C_GVD] += vuSize; break;
    case V3C_BMD: v3cUnitSize[V3C_BMD] += vuSize; break;
    case V3C_PVD: v3cUnitSize[V3C_PVD] += vuSize; break;
    case V3C_ADD: v3cUnitSize[V3C_ADD] += vuSize; break;
    default: v3cUnitSize[V3C_RSVD_09] += vuSize; break;
    }
  }
  printf("\n------- All frames -----------\n");
  printf("Bitstream stat: \n");
  printf("  V3CHeader:                %9zu B %9zu b\n",
         v3cHeaderBytes,
         v3cHeaderBytes * 8);
  printf("  V3CUnitSize[ V3C_VPS ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_VPS],
         v3cUnitSize[V3C_VPS] * 8);
  printf("  V3CUnitSize[ V3C_AD  ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_AD],
         v3cUnitSize[V3C_AD] * 8);
  printf("  V3CUnitSize[ V3C_OVD ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_OVD],
         v3cUnitSize[V3C_OVD] * 8);
  printf("  V3CUnitSize[ V3C_GVD ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_GVD],
         v3cUnitSize[V3C_GVD] * 8);
  printf("  V3CUnitSize[ V3C_BMD ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_BMD],
         v3cUnitSize[V3C_BMD] * 8);
  printf("  V3CUnitSize[ V3C_PVD ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_PVD],
         v3cUnitSize[V3C_PVD] * 8);
  printf("  V3CUnitSize[ V3C_ADD ]:   %9llu B %9llu b\n",
         v3cUnitSize[V3C_ADD],
         v3cUnitSize[V3C_ADD] * 8);
  if (v3cUnitSize[V3C_RSVD_09] != 0)
    printf("  V3CUnitSize[ else    ]:   %9llu B %9llu b\n",
           v3cUnitSize[V3C_RSVD_09],
           v3cUnitSize[V3C_RSVD_09] * 8);
  printf("  Total:                    %9llu B %9llu b \n",
         bitstream.size(),
         bitstream.size() * 8);
}
size_t
V3CWriter::write(SampleStreamV3CUnit& ssvu,
                 Bitstream&           bitstream,
                 uint32_t             forcedSsvhUnitSizePrecisionBytes) {
  TRACE_BITSTREAM_IN("%s", "SampleStreamV3CUnits");
  size_t headerSize = 0;
  // Calculating the precision of the unit size
  uint32_t maxUnitSize = 0;
  for (auto& v3cUnit : ssvu.getV3CUnit()) {
    if (maxUnitSize < v3cUnit.getSize()) {
      maxUnitSize = static_cast<uint32_t>(v3cUnit.getSize());
    }
  }
  uint32_t precision = static_cast<uint32_t>(std::min(
    std::max(
      static_cast<int>(ceil(static_cast<double>(ceilLog2(maxUnitSize)) / 8.0)),
      1),
    8));
  precision          = (std::max)(precision, forcedSsvhUnitSizePrecisionBytes);
  ssvu.getSsvhUnitSizePrecisionBytesMinus1() = precision - 1;

  sampleStreamV3CHeader(bitstream, ssvu);
  headerSize += 1;
  size_t unitCount = 0;
  for (auto& v3cUnit : ssvu.getV3CUnit()) {
    sampleStreamV3CUnit(bitstream, ssvu, v3cUnit);
    unitCount++;
    headerSize += ssvu.getSsvhUnitSizePrecisionBytesMinus1() + 1;
  }
  TRACE_BITSTREAM_OUT("%s", "SampleStreamV3CUnits");
  return headerSize;
}

void
V3CWriter::addV3CUnit(V3cBitstream&        syntax,
                      V3CParameterSet&     vps,
                      SampleStreamV3CUnit& ssvu,
                      size_t               unitIdx,
                      V3CUnitType          v3cUnitType) {
  auto& bistreamStat = syntax.getBitstreamStat();
  auto& v3cu         = ssvu.addV3CUnit();
  auto& bitstream    = v3cu.getBitstream();
  v3cu.getType()     = v3cUnitType;
  v3cUnit(syntax, vps, bitstream, unitIdx, v3cUnitType);
  v3cu.getSize() = bitstream.size();
  //bistreamStat.overwriteV3CUnitSize(v3cUnitType, bitstream.size());
  bistreamStat.setV3CUnitSize(v3cUnitType, bitstream.size());
}

int
V3CWriter::encode(V3cBitstream&        syntax,
                  V3CParameterSet&     vps,
                  SampleStreamV3CUnit& ssvu) {
  auto& bistreamStat = syntax.getBitstreamStat();
  auto& adStream     = syntax.getAtlasDataStream();
  auto& asps         = adStream.getAtlasSequenceParameterSet(0);
  auto  atlasCount   = vps.getAtlasCountMinus1() + 1;
  auto  atlasId      = syntax.getAtlasId();
  bistreamStat.newGOF();

  // Add V3C parameter set
  addV3CUnit(syntax, vps, ssvu, 0, V3C_VPS);

  // Add AtlasData
  addV3CUnit(syntax, vps, ssvu, 0, V3C_AD);

  // Add Base mesh Data
  addV3CUnit(syntax, vps, ssvu, 0, V3C_BMD);

  //Add GVD
  if (vps.getGeometryVideoPresentFlag(atlasId)) {
    for (size_t unitIdx = 0; unitIdx < syntax.getV3CUnitGVD().size();
          unitIdx++) {
      addV3CUnit(syntax, vps, ssvu, unitIdx, V3C_GVD);
    }
  }

  //Add ADD
  if (vps.getVpsVdmcExtension().getVpsExtACDisplacementPresentFlag(atlasId)) {
      addV3CUnit(syntax, vps, ssvu, 0, V3C_ADD);
  }

  //Add AVD
  if (vps.getAttributeVideoPresentFlag(atlasId)) {
    for (size_t unitIdx = 0; unitIdx < syntax.getV3CUnitAVD().size();
         unitIdx++) {
      addV3CUnit(syntax, vps, ssvu, unitIdx, V3C_AVD);
    }
  }

  //Add PVD
  if (vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(atlasId)) {
    for (size_t unitIdx = 0; unitIdx < syntax.getV3CUnitPVD().size();
         unitIdx++) {
      addV3CUnit(syntax, vps, ssvu, unitIdx, V3C_PVD);
    }
  }
  return 0;
}

void
V3CWriter::videoSubStream(V3cBitstream& syntax,
                          Bitstream&    bitstream,
                          size_t        index,
                          V3CUnitType   v3cUnitType) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto& bistreamStat = syntax.getBitstreamStat();

  VideoBitstream& videoStream =
    v3cUnitType == V3C_AVD
      ? syntax.getV3CUnitAVD(index)
      : (v3cUnitType == V3C_GVD
           ? syntax.getV3CUnitGVD(index)
           : (v3cUnitType == V3C_PVD ? syntax.getV3CUnitPVD(index)
                                     : syntax.getV3CUnitOVD(index)));
  videoStream.byteStreamToSampleStream();
  if (1)
    printf("[videoSubStream] %s\tv3cUnitHeader total: %llu\n",
           toString(v3cUnitType).c_str(),
           bitstream.size());
  bitstream.writeVideo(videoStream.buffer(), videoStream.size());
  videoStream.trace();
  bistreamStat.setVideo(videoStream);
  if (1)
    printf("[videoSubStream] %s\t size: %llu\n",
           toString(v3cUnitType).c_str(),
           bitstream.size());
  TRACE_BITSTREAM_OUT("%s", __func__);
}
// 8.2 Specification of syntax functions and descriptors
bool
V3CWriter::byteAligned(Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
V3CWriter::lengthAligned(Bitstream& bitstream) {
  return bitstream.byteAligned();
}
bool
V3CWriter::moreDataInPayload(Bitstream& bitstream) {
  return !bitstream.byteAligned();
}

bool

V3CWriter::payloadExtensionPresent(Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  TRACE_BITSTREAM_OUT("%s", __func__);
  return false;
}

// 8.3.2 V3C unit syntax
// 8.3.2.1 General V3C unit syntax
void
V3CWriter::v3cUnit(V3cBitstream&    syntax,
                   V3CParameterSet& vps,
                   Bitstream&       bitstream,
                   size_t           unitIdx,
                   V3CUnitType      v3cUnitType) {
#if defined(BITSTREAM_TRACE)
  bitstream.setTrace(true);
  bitstream.setLogger(*logger_);
#endif
  TRACE_BITSTREAM_IN("%s(%s)", __func__, toString(v3cUnitType).c_str());
  v3cUnitHeader(syntax, vps, bitstream, unitIdx, v3cUnitType);
  v3cUnitPayload(syntax, vps, bitstream, unitIdx, v3cUnitType);
  TRACE_BITSTREAM_OUT("%s(%s)", __func__, toString(v3cUnitType).c_str());
}

// 8.3.2.2 V3C unit header syntax
void
V3CWriter::v3cUnitHeader(V3cBitstream&    syntax,
                         V3CParameterSet& vps,
                         Bitstream&       bitstream,
                         size_t           unitIndex,
                         V3CUnitType      v3cUnitType) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(v3cUnitType, 5);  // u(5)
  if (v3cUnitType == V3C_AVD || v3cUnitType == V3C_GVD
      || v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD
      || v3cUnitType == V3C_CAD || v3cUnitType == V3C_PVD
      || v3cUnitType == V3C_BMD || v3cUnitType == V3C_ADD) {
    WRITE_CODE(syntax.getActiveVpsId(), 4);  // u(4)
  }
  if (v3cUnitType == V3C_AVD || v3cUnitType == V3C_GVD
      || v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD
      || v3cUnitType == V3C_PVD || v3cUnitType == V3C_BMD
      || v3cUnitType == V3C_ADD) {
    WRITE_CODE(syntax.getAtlasId(), 6);  // u(6)
  }
  if (v3cUnitType == V3C_AVD) {
    auto& vpcc = syntax.getV3CUnitAVD(unitIndex);
    WRITE_CODE(vpcc.getAttributeIndex(), 7);           // u(7)
    WRITE_CODE(vpcc.getAttributePartitionIndex(), 5);  // u(5)
    WRITE_CODE(vpcc.getMapIndex(), 4);                 // u(4)
    WRITE_CODE(vpcc.getAuxiliaryVideoFlag(), 1);       // u(1)
  } else if (v3cUnitType == V3C_GVD) {
    auto& vpcc = syntax.getV3CUnitGVD(unitIndex);
    WRITE_CODE(vpcc.getMapIndex(), 4);            // u(4)
    WRITE_CODE(vpcc.getAuxiliaryVideoFlag(), 1);  // u(1)
    WRITE_CODE(zero, 12);                         // u(12)
  } else if (v3cUnitType == V3C_ADD) {
    WRITE_CODE(zero, 17);  // u(17)
  } else if (v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD  
          || v3cUnitType == V3C_PVD || v3cUnitType == V3C_BMD) {
    WRITE_CODE(zero, 17);  // u(17)
  } else if (v3cUnitType == V3C_CAD) {
    WRITE_CODE(zero, 23);  // u(23)
  } else {
    WRITE_CODE(zero, 27);  // u(27)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.2.3 V3C unit payload syntax
void
V3CWriter::v3cUnitPayload(V3cBitstream&    syntax,
                          V3CParameterSet& vps,
                          Bitstream&       bitstream,
                          size_t           unitIndex,
                          V3CUnitType      v3cUnitType) {
  TRACE_BITSTREAM_IN("%s(%s)", __func__, toString(v3cUnitType).c_str());
  if (v3cUnitType == V3C_VPS) {
    v3cParameterSet(vps, syntax, bitstream);
  } else if (v3cUnitType == V3C_AD || v3cUnitType == V3C_CAD) {
    atlasSubStream_.encode(syntax.getAtlasDataStream(), bitstream);
  } else if (v3cUnitType == V3C_BMD) {
    baseMeshSubStream_.encode(syntax.getBaseMeshDataStream(), bitstream, syntax.getBitstreamStat().getBaseMeshStat());
  } else if (v3cUnitType == V3C_OVD || v3cUnitType == V3C_GVD
             || v3cUnitType == V3C_AVD || v3cUnitType == V3C_PVD) {
    videoSubStream(syntax, bitstream, unitIndex, v3cUnitType);
  } else if (v3cUnitType == V3C_ADD) {
    acDisplacementSubStream_.encode(syntax.getDisplacementStream(), bitstream, syntax.getBitstreamStat().getAcDisplacementStat());
  }
  TRACE_BITSTREAM_OUT("%s(%s)", __func__, toString(v3cUnitType).c_str());
}

// 8.3.3 Byte alignment syntax
void
V3CWriter::byteAlignment(Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  WRITE_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4 V3C parameter set syntax
// 8.3.4.1 General V3C parameter set syntax
void
V3CWriter::v3cParameterSet(V3CParameterSet& vps,
                           V3cBitstream&    syntax,
                           Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  profileTierLevel(vps.getProfileTierLevel(), bitstream);
  WRITE_CODE(vps.getV3CParameterSetId(), 4);  // u(4)
  WRITE_CODE(zero, 8);                        // u(8)
  WRITE_CODE(vps.getAtlasCountMinus1(), 6);   // u(6)
  for (uint32_t j = 0; j < vps.getAtlasCountMinus1() + 1; j++) {
    WRITE_CODE(vps.getAtlasId(j), 6);         // u(6)
    WRITE_UVLC(vps.getFrameWidth(j));         // ue(v)
    WRITE_UVLC(vps.getFrameHeight(j));        // ue(v)
    WRITE_CODE(vps.getMapCountMinus1(j), 4);  // u(4)
    if (vps.getMapCountMinus1(j) > 0) {
      WRITE_CODE(vps.getMultipleMapStreamsPresentFlag(j), 1);  // u(1)
    }
    for (size_t i = 1; i <= vps.getMapCountMinus1(j); i++) {
      if (vps.getMultipleMapStreamsPresentFlag(j)) {
        WRITE_CODE(vps.getMapAbsoluteCodingEnableFlag(j, i), 1);  // u(1)
      }
      if (static_cast<int>(vps.getMapAbsoluteCodingEnableFlag(j, i)) == 0) {
        WRITE_UVLC(vps.getMapPredictorIndexDiff(j, i));
      }
    }
    WRITE_CODE(vps.getAuxiliaryVideoPresentFlag(j), 1);  // u(1)
    WRITE_CODE(vps.getOccupancyVideoPresentFlag(j), 1);  // u(1)
    WRITE_CODE(vps.getGeometryVideoPresentFlag(j), 1);   // u(1)
    WRITE_CODE(vps.getAttributeVideoPresentFlag(j), 1);  // u(1)
    if (vps.getOccupancyVideoPresentFlag(j)) {
      occupancyInformation(
        vps.getOccupancyInformation(j), vps.getAtlasId(j), bitstream);
    }
    if (vps.getGeometryVideoPresentFlag(j)) {
      geometryInformation(
        vps.getGeometryInformation(j), vps, vps.getAtlasId(j), bitstream);
    }
    if (vps.getAttributeVideoPresentFlag(j)) {
      attributeInformation(
        vps.getAttributeInformation(j), vps, vps.getAtlasId(j), bitstream);
    }
  }
  WRITE_CODE(vps.getExtensionPresentFlag(), 1);  // u(1)
  if (vps.getExtensionPresentFlag()) {
    WRITE_CODE(vps.getExtensionCount(), 8);  // u(8)
  }
  if (vps.getExtensionCount()) {
    // calculating the total size of the extensions
    int vpsExtensionsLength = 3 * vps.getExtensionCount();
    for (int i = 0; i < vps.getExtensionCount(); i++) {
      Bitstream tempBitstream;
      vpsExtension(vps, i, tempBitstream);
      vpsExtensionsLength += tempBitstream.size();
      vps.setExtensionLength(i, tempBitstream.size());
    }
    vps.setExtensionLengthMinus1(vpsExtensionsLength - 1);
    WRITE_UVLC(vps.getExtensionLengthMinus1());  // ue(v)
    for (int i = 0; i < vps.getExtensionCount(); i++) {
      WRITE_CODE(vps.getExtensionType(i), 8);     // u(8)
      WRITE_CODE(vps.getExtensionLength(i), 16);  // u(16)
      Bitstream tempBitstream;
#if defined(BITSTREAM_TRACE)
      tempBitstream.setTrace(true);
      tempBitstream.setLogger(bitstream.getLogger());
#endif
      vpsExtension(vps, i, tempBitstream);
      bitstream.copyFromBits(tempBitstream, 0, tempBitstream.size());
    }
  }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.2 Profile, tier, and level syntax
void
V3CWriter::profileTierLevel(ProfileTierLevel& ptl, Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t ptl_reserved_0xfff_12bits = 0x0FFF;
  uint32_t ptl_reserved_zero_7bits = 0;
  WRITE_CODE(ptl.getTierFlag(), 1);                  // u(1)
  WRITE_CODE(ptl.getProfileCodecGroupIdc(), 7);      // u(7)
  WRITE_CODE(ptl.getProfileToolsetIdc(), 8);         // u(8)
  WRITE_CODE(ptl.getProfileReconstructionIdc(), 8);  // u(8)
  WRITE_CODE(ptl.getAttributeTierFlag(), 1);         // u(1)
  WRITE_CODE(ptl.getAttributeLevelIdc(), 8);         // u(8)
  WRITE_CODE(ptl_reserved_zero_7bits, 7);            // u(7)
  WRITE_CODE(ptl.getMaxDecodesIdc(), 4);             // u(4)
  WRITE_CODE(ptl_reserved_0xfff_12bits, 12);         // u(12)
  WRITE_CODE(ptl.getLevelIdc(), 8);                  // u(8)
  WRITE_CODE(ptl.getNumSubProfiles(), 6);            // u(6)
  WRITE_CODE(ptl.getExtendedSubProfileFlag(), 1);    // u(1)
  for (size_t i = 0; i < ptl.getNumSubProfiles(); i++) {
    size_t v = ptl.getExtendedSubProfileFlag() == 0 ? 32 : 64;
    WRITE_CODE(ptl.getSubProfileIdc(i), v);  // u(v)
  }
  WRITE_CODE(ptl.getToolConstraintsPresentFlag(), 1);  // u(1)
  if (ptl.getToolConstraintsPresentFlag()) {
    profileToolsetConstraintsInformation(
      ptl.getProfileToolsetConstraintsInformation(), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.3 Occupancy parameter set syntax
void
V3CWriter::occupancyInformation(OccupancyInformation& oi,
                                uint32_t              atlasId,
                                Bitstream&            bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(oi.getOccupancyCodecId(), 8);                    // u(8)
  WRITE_CODE(oi.getLossyOccupancyCompressionThreshold(), 8);  // u(8)
  WRITE_CODE(oi.getOccupancy2DBitdepthMinus1(), 5);           // u(5)
  WRITE_CODE(oi.getOccupancyMSBAlignFlag(), 1);               // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.4 Geometry parameter set syntax
void
V3CWriter::geometryInformation(GeometryInformation& gi,
                               V3CParameterSet&     vps,
                               uint32_t             atlasId,
                               Bitstream&           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(gi.getGeometryCodecId(), 8);                      // u(8)
  WRITE_CODE(gi.getGeometry2dBitdepthMinus1(), 5);             // u(5)
  WRITE_CODE(gi.getGeometryMSBAlignFlag(), 1);                 // u(1)
  WRITE_CODE(gi.getGeometry3dCoordinatesBitdepthMinus1(), 5);  // u(5)
  if (vps.getAuxiliaryVideoPresentFlag(atlasId)) {
    WRITE_CODE(gi.getAuxiliaryGeometryCodecId(), 8);  // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.5 Attribute information
void
V3CWriter::attributeInformation(AttributeInformation& ai,
                                V3CParameterSet&      vps,
                                uint32_t              atlasId,
                                Bitstream&            bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(ai.getAttributeCount(), 7);  // u(7)
  for (uint32_t i = 0; i < ai.getAttributeCount(); i++) {
    WRITE_CODE(ai.getAttributeTypeId(i), 4);   // u(4)
    WRITE_CODE(ai.getAttributeCodecId(i), 8);  // u(8)
    if (vps.getAuxiliaryVideoPresentFlag(atlasId)) {
      WRITE_CODE(ai.getAuxiliaryAttributeCodecId(i), 8);  // u(8)
    }
    if (vps.getMapCountMinus1(atlasId) > 0) {
      WRITE_CODE(ai.getAttributeMapAbsoluteCodingPersistenceFlag(i),
                 1);  // u(1)
    }
    WRITE_CODE(ai.getAttributeDimensionMinus1(i), 6);  // u(6)
    if (ai.getAttributeDimensionMinus1(i) > 0) {
      WRITE_CODE(ai.getAttributeDimensionPartitionsMinus1(i), 6);  // u(6)
      int32_t remainingDimensions = ai.getAttributeDimensionMinus1(i);
      int32_t k = ai.getAttributeDimensionPartitionsMinus1(i);
      for (int32_t j = 0; j < k; j++) {
        if (k - j != remainingDimensions) {
          WRITE_UVLC(static_cast<uint32_t>(
            ai.getAttributePartitionChannelsMinus1(i, j)));  // ue(v)
        }
        remainingDimensions -=
          ai.getAttributePartitionChannelsMinus1(i, j) + 1;
      }
    }
    WRITE_CODE(ai.getAttribute2dBitdepthMinus1(i), 5);  // u(5)
    WRITE_CODE(ai.getAttributeMSBAlignFlag(i), 1);      // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.6	Profile toolset constraints information syntax
void
V3CWriter::profileToolsetConstraintsInformation(
  ProfileToolsetConstraintsInformation& ptci,
  Bitstream&                            bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(ptci.getOneFrameOnlyFlag(), 1);                         // u(1)
  WRITE_CODE(ptci.getEOMContraintFlag(), 1);                         // u(1)
  WRITE_CODE(ptci.getMaxMapCountMinus1(), 4);                        // u(4)
  WRITE_CODE(ptci.getMaxAtlasCountMinus1(), 4);                      // u(4)
  WRITE_CODE(ptci.getMultipleMapStreamsConstraintFlag(), 1);         // u(1)
  WRITE_CODE(ptci.getPLRConstraintFlag(), 1);                        // u(1)
  WRITE_CODE(ptci.getAttributeMaxDimensionMinus1(), 6);              // u(6)
  WRITE_CODE(ptci.getAttributeMaxDimensionPartitionsMinus1(), 6);    // u(6)
  WRITE_CODE(ptci.getNoEightOrientationsConstraintFlag(), 1);        // u(1)
  WRITE_CODE(ptci.getNo45DegreeProjectionPatchConstraintFlag(), 1);  // u(1)
  WRITE_CODE(ptci.getRestrictedGeometryFlag(), 1);                   // u(1)
  WRITE_CODE(0, 5);                                                  // u(5)
  WRITE_CODE(ptci.getNumReservedConstraintBytes(), 8);               // u(8)
  if(ptci.getNumReservedConstraintBytes() > 0) {
    WRITE_CODE(ptci.getNoSubdivisionFlag(), 1);                         // u(1)
    WRITE_CODE(ptci.getNoDisplacementFlag(), 1);                        // u(1)
    WRITE_CODE(ptci.getDisplacementAscendingFlag(), 1);                 // u(1)
    WRITE_CODE(ptci.getDisplacementDimensionConstraintFlag(), 1);       // u(1)
    WRITE_CODE(ptci.getDisplacementVideoFlag(), 1);                     // u(1)
    WRITE_CODE(ptci.getNoPackedVideoFlag(), 1);                         // u(1)
    WRITE_CODE(0, 2);                                                    // u(2)
  }
  for (size_t i = 1; i < ptci.getNumReservedConstraintBytes(); i++) {
    WRITE_CODE(ptci.getReservedConstraintByte(i), 8);  // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.7 Packing information syntax
void
V3CWriter::packingInformation(PackingInformation& pi,
                              V3CParameterSet&    vps,
                              uint32_t            atlasId,
                              Bitstream&          bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(pi.pin_codec_id(), 8);
  WRITE_CODE(pi.pin_occupancy_present_flag(), 1);
  WRITE_CODE(pi.pin_geometry_present_flag(), 1);
  WRITE_CODE(pi.pin_attribute_present_flag(), 1);
  if (pi.pin_occupancy_present_flag()) {
    WRITE_CODE(pi.pin_occupancy_2d_bit_depth_minus1(), 5);
    WRITE_CODE(pi.pin_occupancy_MSB_align_flag(), 1);
    WRITE_CODE(pi.pin_lossy_occupancy_compression_threshold(), 8);
  }
  if (pi.pin_geometry_present_flag()) {
    WRITE_CODE(pi.pin_geometry_2d_bit_depth_minus1(), 5);
    WRITE_CODE(pi.pin_geometry_MSB_align_flag(), 1);
    WRITE_CODE(pi.pin_geometry_3d_coordinates_bit_depth_minus1(), 5);
  }
  if (pi.pin_attribute_present_flag()) {
    WRITE_CODE(pi.pin_attribute_count(), 7);
    for (size_t i = 0; i < pi.pin_attribute_count(); i++) {
      WRITE_CODE(pi.pin_attribute_type_id(i), 4);
      WRITE_CODE(pi.pin_attribute_2d_bit_depth_minus1(i), 5);
      WRITE_CODE(pi.pin_attribute_MSB_align_flag(i), 1);
      WRITE_CODE(pi.pin_attribute_map_absolute_coding_persistence_flag(i), 1);
      WRITE_CODE(pi.pin_attribute_dimension_minus1(i), 6);
      auto    d = pi.pin_attribute_dimension_minus1(i);
      uint8_t m = 0;
      if (d != 0) {
        WRITE_CODE(pi.pin_attribute_dimension_partitions_minus1(i), 6);
        m = pi.pin_attribute_dimension_partitions_minus1(i);
      }
      for (uint8_t k = 0; k < m; k++) {
        uint8_t n = 0;
        if (k + d != m) {
          WRITE_UVLC(pi.pin_attribute_partition_channels_minus1(i, k));
          n = pi.pin_attribute_partition_channels_minus1(i, k);
        }
        d -= (uint8_t)(n + 1);
      }
    }
  }

  WRITE_UVLC(pi.pin_regions_count_minus1());
  for (size_t i = 0; i <= pi.pin_regions_count_minus1(); ++i) {
    WRITE_CODE(pi.pin_region_tile_id(i), 8);
    WRITE_CODE(pi.pin_region_type_id_minus2(i), 2);
    WRITE_CODE(pi.pin_region_top_left_x(i), 16);
    WRITE_CODE(pi.pin_region_top_left_y(i), 16);
    WRITE_CODE(pi.pin_region_width_minus1(i), 16);
    WRITE_CODE(pi.pin_region_height_minus1(i), 16);
    WRITE_CODE(pi.pin_region_unpack_top_left_x(i), 16);
    WRITE_CODE(pi.pin_region_unpack_top_left_y(i), 16);
    WRITE_CODE(pi.pin_region_rotation_flag(i), 1);

    if ((pi.pinRegionTypeId(i) == V3CUnitType::V3C_AVD)
        || pi.pinRegionTypeId(i) == V3CUnitType::V3C_GVD) {
      WRITE_CODE(pi.pin_region_map_index(i), 4);
      WRITE_CODE(pi.pin_region_auxiliary_data_flag(i), 1);
    }
    if (pi.pinRegionTypeId(i) == V3CUnitType::V3C_AVD) {
      WRITE_CODE(pi.pin_region_attr_index(i), 7);
      const auto k = pi.pin_region_attr_index(i);
      if (pi.pin_attribute_dimension_minus1(k) > 0) {
        WRITE_CODE(pi.pin_region_attr_partition_index(i), 5);
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.8 VPS extension syntax
void
V3CWriter::vpsExtension(V3CParameterSet& vps,
                        uint8_t          index,
                        Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  auto type = V3CExtensionType(vps.getExtensionType(index));
  if (type == VPS_EXT_PACKED) {
    vpsPackedVideoExtension(vps, vps.getVpsPackedVideoExtension(), bitstream);
  } else if (type == VPS_EXT_MIV) {
    // Miv - Specified in ISO/IEC 23090-12
  } else if (type == VPS_EXT_MIV2) {
    // Miv2 - Specified in ISO/IEC 23090-12
  } else if (type == VPS_EXT_VDMC) {
    vpsVdmcExtension(vps, vps.getVpsVdmcExtension(), bitstream);
  } else {
    for (size_t i = 0; i < vps.getExtensionLength(index); i++)
      WRITE_CODE(vps.getExtensionDataByte(index, i), 8);  // u(8)
  }
  lengthAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.9 Packed video extension syntax

// 8.3.4.10 Length alignment syntax
void
V3CWriter::lengthAlignment(Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  while (!lengthAligned(bitstream)) {
    WRITE_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}
void
V3CWriter::vpsPackedVideoExtension(V3CParameterSet&  vps,
                                   VpsPackedVideoExtension& packedVideoExt,
                                   Bitstream&        bitstream){
  TRACE_BITSTREAM_IN("%s", __func__);
  for (size_t i = 0; i < packedVideoExt.getAtlasCount(); i++) {
    auto atlasID = vps.getAtlasId(i);
    TRACE_BITSTREAM("atlasID : %d \n", atlasID);
    WRITE_CODE(packedVideoExt.getPackedVideoPresentFlag(i), 1);
    if(packedVideoExt.getPackedVideoPresentFlag(i))
    packingInformation(packedVideoExt.getPackingInformation(i), vps, vps.getAtlasId(i), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.11 V3C parameter set V-DMC extension syntax
void
V3CWriter::vpsVdmcExtension(V3CParameterSet&  vps,
                            VpsVdmcExtension& vmcExt,
                            Bitstream&        bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  for (size_t i = 0; i < vmcExt.getAtlasCount(); i++) {
    auto atlasID = vps.getAtlasId(i);
    TRACE_BITSTREAM("atlasID : %d \n", atlasID);
    WRITE_CODE(vmcExt.getVpsExtMeshDataSubstreamCodecId(atlasID), 8);  // u(8)
    WRITE_CODE(vmcExt.getVpsExtMeshGeo3dBitdepthMinus1(atlasID), 5);   // u(5)
    WRITE_CODE(vmcExt.getVpsExtMeshGeo3dMSBAlignFlag(atlasID), 1);     // u(1)
    WRITE_CODE(vmcExt.getVpsExtMeshDataAttributeCount(atlasID), 8);    // u(8)
    for (size_t i = 0; i < vmcExt.getVpsExtMeshDataAttributeCount(atlasID);
         i++) {
      TRACE_BITSTREAM("basemesh attribute idx : %d \n", i);
      WRITE_CODE(vmcExt.getVpsExtMeshAttributeIndex(atlasID, i), 7);  // u(7)
      WRITE_CODE(vmcExt.getVpsExtMeshAttributeBitdepthMinus1(atlasID, i),
                 5);  // u(5)
      WRITE_CODE(vmcExt.getVpsExtMeshAttributeMSBAlignFlag(atlasID, i),
                 1);                                                 // u(1)
      WRITE_CODE(vmcExt.getVpsExtMeshAttributeType(atlasID, i), 4);  // u(4)
    }
    WRITE_CODE(vmcExt.getVpsExtACDisplacementPresentFlag(atlasID), 1);     // u(1)
    if (vmcExt.getVpsExtACDisplacementPresentFlag(atlasID)) {
      displacementInformation(
        vmcExt.getDisplacementInformation(atlasID), atlasID, bitstream);
    }

    WRITE_CODE(vmcExt.getVpsExtConsistentAttributeFrameFlag(atlasID), 1);     // u(1)
    uint32_t vpsAttributeNominalFrameSizeCount = vps.getVpsAttributeNominalFrameSizeCount(atlasID);
    for (uint32_t i = 0; i < vpsAttributeNominalFrameSizeCount; i++) {
      TRACE_BITSTREAM("video attribute idx : %d \n", i);
      WRITE_UVLC(vmcExt.getVpsExtAttributeFrameWidth(atlasID, i));   // ue(v)
      WRITE_UVLC(vmcExt.getVpsExtAttributeFrameHeight(atlasID, i));  // ue(v)
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.12 Displacement information syntax
void
V3CWriter::displacementInformation(DisplacementInformation& di,
                                  uint32_t             atlasId,
                                  Bitstream&           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(di.getDisplacementCodecId(), 8);                      // u(8)
  WRITE_CODE(di.get1dDisplacementFlag(), 1);                       // u(1)
  WRITE_CODE(di.getDisplacementReservedBits(), 15);                // u(15)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// C.2 Sample stream V3C unit syntax and semantics
// C.2.1 Sample stream V3C header syntax
void
V3CWriter::sampleStreamV3CHeader(Bitstream&           bitstream,
                                 SampleStreamV3CUnit& ssvu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  WRITE_CODE(ssvu.getSsvhUnitSizePrecisionBytesMinus1(), 3);  // u(3)
  WRITE_CODE(zero, 5);                                        // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// C.2.2 Sample stream V3C unit syntax
void
V3CWriter::sampleStreamV3CUnit(Bitstream&           bitstream,
                               SampleStreamV3CUnit& ssvu,
                               V3CUnit&             v3cUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  WRITE_CODE(v3cUnit.getSize(),
             8 * (ssvu.getSsvhUnitSizePrecisionBytesMinus1() + 1));  // u(v)
  v3cUnit.getBitstream().vector().resize(v3cUnit.getSize());
  WRITE_VECTOR(v3cUnit.getBitstream().vector());
  printf("sampleStreamV3CUnit => %s size: %zu\n",
         toString(v3cUnit.getType()).c_str(),
         v3cUnit.getSize());
  TRACE_BITSTREAM_OUT(
    "%s type = %s ", __func__, toString(v3cUnit.getType()).c_str());
}
