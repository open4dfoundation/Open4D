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
#include "v3cCommon.hpp"
#include "readerCommon.hpp"
#include "videoBitstream.hpp"
#include "bitstream.hpp"
#include "bitstreamStat.hpp"
#include "v3cBitstream.hpp"
#include "v3cReader.hpp"

using namespace vmesh;

V3CReader::V3CReader() {}
V3CReader::~V3CReader() = default;

void
V3CReader::report(SampleStreamV3CUnit& ssvu,
                  Bitstream&           bitstream,
                  size_t v3cHeaderBytes){
  uint64_t v3cUnitSize[NUM_V3C_UNIT_TYPE]={0,};
  for(auto& vu : ssvu.getV3CUnit()){
    V3CUnitType vuType = vu.getType();
    size_t      vuSize = vu.getSize();
    switch(vuType){
      case V3C_VPS:
        v3cUnitSize[V3C_VPS] += vuSize;
        break;
      case V3C_AD:
        v3cUnitSize[V3C_AD] += vuSize;
        break;
      case V3C_OVD:
        v3cUnitSize[V3C_OVD] += vuSize;
        break;
      case V3C_GVD:
        v3cUnitSize[V3C_GVD] += vuSize;
        break;
      case V3C_BMD:
        v3cUnitSize[V3C_BMD] += vuSize;
        break;
      case V3C_PVD:
        v3cUnitSize[V3C_PVD] += vuSize;
        break;
      case V3C_ADD:
        v3cUnitSize[V3C_ADD] += vuSize;
        break;
      default:
        v3cUnitSize[V3C_RSVD_09] += vuSize;
        break;
    }
  }
  printf("\n------- All frames -----------\n");
  printf("Bitstream stat: \n");
  printf("  V3CHeader:                %9zu B %9zu b\n", v3cHeaderBytes, v3cHeaderBytes * 8);
  printf("  V3CUnitSize[ V3C_VPS ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_VPS ], v3cUnitSize[ V3C_VPS ] * 8);
  printf("  V3CUnitSize[ V3C_AD  ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_AD  ], v3cUnitSize[ V3C_AD  ] * 8);
  printf("  V3CUnitSize[ V3C_OVD ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_OVD ], v3cUnitSize[ V3C_OVD ] * 8);
  printf("  V3CUnitSize[ V3C_GVD ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_GVD ], v3cUnitSize[ V3C_GVD ] * 8);
  printf("  V3CUnitSize[ V3C_BMD ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_BMD ], v3cUnitSize[ V3C_BMD ] * 8);
  printf("  V3CUnitSize[ V3C_PVD ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_PVD ], v3cUnitSize[ V3C_PVD ] * 8);
  printf("  V3CUnitSize[ V3C_ADD ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_ADD ], v3cUnitSize[ V3C_ADD ] * 8);
  if(v3cUnitSize[ V3C_RSVD_09 ]!=0)
  printf("  V3CUnitSize[ else    ]:   %9llu B %9llu b\n", v3cUnitSize[ V3C_RSVD_09 ], v3cUnitSize[ V3C_RSVD_09 ] * 8);
  printf("  Total:                    %9llu B %9llu b \n", bitstream.size(), bitstream.size()*8);
}
size_t
V3CReader::read(Bitstream& bitstream, SampleStreamV3CUnit& ssvu) {
  size_t headerSize = 0;
  TRACE_BITSTREAM_IN("%s", "SampleStreamV3CUnits");
  sampleStreamV3CHeader(bitstream, ssvu);
  headerSize++;
  size_t unitCount = 0;
  while (bitstream.moreData()) {
    auto& v3cUnit = ssvu.addV3CUnit();
    sampleStreamV3CUnit(bitstream, ssvu, v3cUnit);
    unitCount++;
    headerSize += ssvu.getSsvhUnitSizePrecisionBytesMinus1() + 1;
  }
  TRACE_BITSTREAM_OUT("%s", "SampleStreamV3CUnits");
  return headerSize;
}

int32_t
V3CReader::decode(SampleStreamV3CUnit& ssvu, V3cBitstream& syntax, std::vector<V3CParameterSet>& vpsList) {
  int   numVPS       = 0;  // counter for the atlas information
  auto& bistreamStat = syntax.getBitstreamStat();
  bistreamStat.newGOF();
  while (ssvu.getV3CUnitCount() > 0) {
    auto& v3cV3cu     = ssvu.front();
    auto  v3cUnitType = NUM_V3C_UNIT_TYPE;
    if (v3cV3cu.getType() == V3C_VPS && (++numVPS > 1)) break;
    if(v3cV3cu.getType() == V3C_VPS) vpsList.resize(vpsList.size()+1);
    v3cUnit(syntax, vpsList.back(), v3cV3cu, v3cUnitType);
    ssvu.popFront();
  }
  return 1;
}

void
V3CReader::videoSubStream(V3cBitstream& syntax,
                          Bitstream&    bitstream,
                          V3CUnitType&  V3CUnitType,
                          size_t        index,
                          size_t        v3cPayloadSize){
  TRACE_BITSTREAM_IN("%s", __func__);
  //index-th stream if syntax has multiple videostreams with the same V3CUnittype (=multiple GVD)
  VideoBitstream& videoStream = V3CUnitType==V3C_AVD? syntax.getV3CUnitAVD(index) : (V3CUnitType==V3C_GVD? syntax.getV3CUnitGVD(index) :  (V3CUnitType==V3C_PVD? syntax.getV3CUnitPVD(index) : syntax.getV3CUnitOVD(index)));
  if(1) printf("[videoSubStream] %s\tv3cUnitHeader total: %llu\n",  toString(V3CUnitType).c_str(), bitstream.size());
  videoStream.resize(v3cPayloadSize);
  bitstream.readVideo(videoStream.buffer(), v3cPayloadSize);
  //bitstream.readVideo( videoStream, v3cPayloadSize );
  auto& bistreamStat = syntax.getBitstreamStat();
  VideoEncoderId codecId = VideoEncoderId::UNKNOWN_VIDEO_ENCODER;
  if(V3CUnitType==V3C_AVD){
    codecId = (VideoEncoderId)syntax.getV3CUnitAVD(index).getCoderIdc();
  }
  else if(V3CUnitType==V3C_GVD){
    codecId = (VideoEncoderId)syntax.getV3CUnitGVD(index).getCoderIdc();
  }
  else if(V3CUnitType==V3C_OVD){
     codecId = (VideoEncoderId)syntax.getV3CUnitGVD(index).getCoderIdc();
  }
  else if(V3CUnitType==V3C_PVD){
    codecId = (VideoEncoderId)syntax.getV3CUnitPVD(index).getCoderIdc();
  }
  bool isAVC= (codecId == VideoEncoderId::JM);
  bool isVVC= (codecId == VideoEncoderId::VV)|| (codecId == VideoEncoderId::VTM);
  bistreamStat.setVideo(videoStream);
  videoStream.sampleStreamToByteStream(isAVC, isVVC);
  if(1) printf("[videoSubStream] %s size(without v3cUnitHeader): %zu\n",  toString(V3CUnitType).c_str(), v3cPayloadSize);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.2 Specification of syntax functions and descriptors
bool
V3CReader::byteAligned(Bitstream& bitstream) {
    return bitstream.byteAligned();
}
bool
V3CReader::lengthAligned(Bitstream& bitstream) {
    return bitstream.byteAligned();
}
bool
V3CReader::moreDataInPayload(Bitstream& bitstream) {
    return !bitstream.byteAligned();
}
bool
V3CReader::moreRbspData(Bitstream& bitstream) {
    uint32_t value = 0;
    // Return false if there is no more data.
    if (!bitstream.moreData()) { return false; }
    // Store bitstream state.
    auto position = bitstream.getPosition();
#if defined(BITSTREAM_TRACE)
    bitstream.setTrace(false);
#endif
    // Skip first bit. It may be part of a RBSP or a rbsp_one_stop_bit.
    READ_CODE(value, 1);  // u(1)
    while (bitstream.moreData()) {
        READ_CODE(value, 1);  // u(1)
        if (value) {
            // We found a one bit beyond the first bit. Restore bitstream state and return true.
            bitstream.setPosition(position);
#if defined(BITSTREAM_TRACE)
            bitstream.setTrace(true);
#endif
            return true;
        }
    }
    // We did not found a one bit beyond the first bit. Restore bitstream state and return false.
    bitstream.setPosition(position);
#if defined(BITSTREAM_TRACE)
    bitstream.setTrace(true);
#endif
    return false;
}
bool
V3CReader::payloadExtensionPresent(Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    TRACE_BITSTREAM_OUT("%s", __func__);
    return false;
}

// 8.3.2 V3C unit syntax
// 8.3.2.1 General V3C unit syntax
void
V3CReader::v3cUnit(V3cBitstream& syntax,
                   V3CParameterSet&      vps,
                   V3CUnit&      v3cNalu,
                   V3CUnitType&  v3cUnitType) {
  Bitstream& bitstream = v3cNalu.getBitstream();
#if defined(BITSTREAM_TRACE)
  bitstream.setTrace(true);
  bitstream.setLogger(*logger_);
#endif
  TRACE_BITSTREAM_IN(
    "%s(%s)", __func__, toString(v3cNalu.getType()).c_str());
  auto position = static_cast<int32_t>(bitstream.size());
  size_t streamPos = v3cUnitHeader(syntax, vps, bitstream, v3cUnitType);
  assert(v3cUnitType == v3cNalu.getType());
  v3cUnitPayload(syntax, vps, bitstream, v3cUnitType, v3cNalu.getSize(), streamPos);
  syntax.getBitstreamStat().setV3CUnitSize(
    v3cUnitType, static_cast<int32_t>(bitstream.size()) - position);
  TRACE_BITSTREAM_OUT(
    "%s(%s)", __func__, toString(v3cNalu.getType()).c_str());
}

// 8.3.2.2 V3C unit header syntax
size_t
V3CReader::v3cUnitHeader(V3cBitstream& syntax,
                         V3CParameterSet&      vps,
                         Bitstream&    bitstream,
                         V3CUnitType&  v3cUnitType) {
  TRACE_BITSTREAM_IN("%s", __func__);
  size_t streamPos=0;
  uint32_t zero = 0;
  READ_CODE_CAST(v3cUnitType, 5, V3CUnitType);  // u(5)
  if (v3cUnitType == V3C_AVD || v3cUnitType == V3C_GVD
      || v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD
      || v3cUnitType == V3C_CAD || v3cUnitType == V3C_PVD
      || v3cUnitType == V3C_BMD || v3cUnitType == V3C_ADD) {
      //auto& v3cUnitHeader = syntax.getV3CUnitHeader(v3cUnitType);
      READ_CODE(syntax.getActiveVpsId(), 4);  // u(4)
  }
  if (v3cUnitType == V3C_AVD || v3cUnitType == V3C_GVD
      || v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD
      || v3cUnitType == V3C_PVD || v3cUnitType == V3C_BMD
      || v3cUnitType == V3C_ADD) {
      //auto& v3cUnitHeader = syntax.getV3CUnitHeader(v3cUnitType);
    READ_CODE(syntax.getAtlasId(), 6);            // u(6)
  }
  if (v3cUnitType == V3C_AVD) {
    auto& videoStream = syntax.addVideoSubstream(v3cUnitType);
    videoStream.setAtlasId                   ( syntax.getAtlasId());
    videoStream.setV3CParameterSetId         ( syntax.getActiveVpsId()  );
    videoStream.setVuhAttributeIndex         ( bitstream.read(7) ); //cUnitHeader.getAttributeIndex() );            // u(7)
    TRACE_BITSTREAM("AttributeIndex .................................... = %d      u(7)\n", videoStream.getAttributeIndex());
    videoStream.setVuhAttributePartitionIndex( bitstream.read(5) ); //cUnitHeader.getAttributeDimensionIndex() );   // u(5)
    TRACE_BITSTREAM("AttributePartitionIndex ........................... = %d      u(5)\n", videoStream.getAttributePartitionIndex());
    videoStream.setVuhMapIndex               ( bitstream.read(4) ); //cUnitHeader.getMapIndex() );                  // u(4)
    TRACE_BITSTREAM("MapIndex .......................................... = %d      u(4)\n", videoStream.getMapIndex());
    videoStream.setVuhAuxiliaryVideoFlag     ( bitstream.read(1) ); //cUnitHeader.getAuxiliaryVideoFlag() );  // u(1)
    TRACE_BITSTREAM("AuxiliaryVideoFlag ................................ = %d      u(1)\n", videoStream.getAuxiliaryVideoFlag());
    streamPos = syntax.getV3CUnitAVD().size()-1;

  } else if (v3cUnitType == V3C_GVD) {
    auto& videoStream = syntax.addVideoSubstream(v3cUnitType);
    videoStream.setAtlasId          ( syntax.getAtlasId());
    videoStream.setV3CParameterSetId( syntax.getActiveVpsId()  );
    videoStream.setVuhMapIndex          ( bitstream.read(4) );                  // u(4)
    TRACE_BITSTREAM("MapIndex .......................................... = %d      u(4)\n", videoStream.getMapIndex());
    videoStream.setVuhAuxiliaryVideoFlag( bitstream.read(1) );  // u(1)
    TRACE_BITSTREAM("AuxiliaryVideoFlag ................................ = %d      u(1)\n", videoStream.getAuxiliaryVideoFlag());
    READ_CODE(zero, 12);                         // u(12)
    streamPos = syntax.getV3CUnitGVD().size()-1;
  } else if (v3cUnitType == V3C_ADD) {
    auto& dmStream = syntax.getDisplacementStream();
    READ_CODE(zero, 17);  // u(17)
  } else if (v3cUnitType == V3C_OVD || v3cUnitType == V3C_AD
      || v3cUnitType == V3C_PVD || v3cUnitType == V3C_BMD) {
      auto activeVpsId =  syntax.getActiveVpsId();
    auto atlasId = syntax.getAtlasId();
    if(v3cUnitType == V3C_OVD ){
      auto& videoStream = syntax.addVideoSubstream(v3cUnitType);
      videoStream.setAtlasId(atlasId);
      videoStream.setV3CParameterSetId( activeVpsId );
      streamPos = syntax.getV3CUnitOVD().size()-1;
    }else if ( v3cUnitType == V3C_AD ) {
      auto& adStream = syntax.getAtlasDataStream();
      adStream.setAtlasId(atlasId);
      adStream.setV3CParameterSetId( activeVpsId );
      streamPos = 0;
    }else if ( v3cUnitType == V3C_PVD ) {
      auto& videoStream = syntax.addVideoSubstream(v3cUnitType);
      videoStream.setAtlasId(atlasId);
      videoStream.setV3CParameterSetId( activeVpsId );
      streamPos = syntax.getV3CUnitPVD().size()-1;
    }else if ( v3cUnitType == V3C_BMD ) {
      auto& mdStream = syntax.getBaseMeshDataStream();
      mdStream.setAtlasId(atlasId);
      mdStream.setV3CParameterSetId( activeVpsId );
      streamPos = 0;
    }

    READ_CODE(zero, 17);  // u(17)
  }
  else if (v3cUnitType == V3C_CAD) {
      READ_CODE(zero, 23);  // u(23)
  } else {
    READ_CODE(zero, 27);  // u(27)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
  return streamPos;
}

// 8.3.2.3 V3C unit payload syntax
void
V3CReader::v3cUnitPayload(V3cBitstream& syntax,
                          V3CParameterSet&      vps,
                          Bitstream&    bitstream,
                          V3CUnitType&  v3cUnitType,
                          size_t        v3cUnitSize,
                          size_t        streamPos) {
  TRACE_BITSTREAM_IN("%s(%s)", __func__, toString(v3cUnitType).c_str());
  if (v3cUnitType == V3C_VPS) {
    v3cParameterSet(vps, syntax, bitstream);
    // TODO: is this proper place to do the software limitation check ?
    // TODO: here some checks
    VERIFY_SOFTWARE_LIMITATION(vps.getAtlasCountMinus1() == 0);
    VERIFY_SOFTWARE_LIMITATION(vps.getVpsAttributeNominalFrameSizeCount(0) < 2);
  } else if (v3cUnitType == V3C_AD || v3cUnitType == V3C_CAD) {
    atlasSubStream_.decode(syntax.getAtlasDataStream(), bitstream);
    // TODO: is this proper place to do the conformance check ?
    // TODO: this should be done for all conformance constrains
    // TODO: this should be done for all ASPS, AFPS, VPS
    // TODO: here just an example
    VERIFY_CONFORMANCE(vps.getVpsAttributeNominalFrameSizeCount(syntax.getAtlasId()) ==
                       syntax.getAtlasDataStream().getAtlasSequenceParameterSetList()[0].getAsveExtension().getAspsAttributeNominalFrameSizeCount() );
    VERIFY_CONFORMANCE(vps.getVpsVdmcExtension().getVpsExtConsistentAttributeFrameFlag(0) ==
                       syntax.getAtlasDataStream().getAtlasSequenceParameterSetList()[0].getAsveExtension().getAsveConsistentAttributeFrameFlag() );
  } else if (v3cUnitType == V3C_BMD) {
    baseMeshSubStream_.decode(syntax.getBaseMeshDataStream(), bitstream, syntax.getBitstreamStat().getBaseMeshStat());
  } else if (v3cUnitType == V3C_OVD || v3cUnitType == V3C_GVD
             || v3cUnitType == V3C_AVD || v3cUnitType == V3C_PVD) {
    videoSubStream(syntax, bitstream, v3cUnitType, streamPos, v3cUnitSize -4);
  } else if (v3cUnitType == V3C_ADD) {
    acDisplacementSubStream_.decode(syntax.getDisplacementStream(), bitstream, syntax.getBitstreamStat().getAcDisplacementStat());
  }
  TRACE_BITSTREAM_OUT("%s(%s)", __func__, toString(v3cUnitType).c_str());
}

// 8.3.3 Byte alignment syntax
void
V3CReader::byteAlignment(Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t one  = 1;
  uint32_t zero = 0;
  READ_CODE(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    READ_CODE(zero, 1);  // f(1): equal to 0
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4 V3C parameter set syntax
// 8.3.4.1 General V3C parameter set syntax
void
V3CReader::v3cParameterSet(V3CParameterSet& vps,
                           V3cBitstream&    syntax,
                           Bitstream&       bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  profileTierLevel(vps.getProfileTierLevel(), bitstream);
  READ_CODE(vps.getV3CParameterSetId(), 4);  // u(4)
  READ_CODE(zero, 8);                        // u(8)
  READ_CODE(vps.getAtlasCountMinus1(), 6);   // u(6)
  vps.allocateAtlas(vps.getAtlasCountMinus1() + 1);
  //syntax.allocateAtlas(vps.getAtlasCountMinus1() + 1);
  //syntax.allocateBaseMesh(1);
  for (uint32_t j = 0; j < vps.getAtlasCountMinus1() + 1; j++) {
    READ_CODE(vps.getAtlasId(j), 6);         // u(6)
    READ_UVLC(vps.getFrameWidth(j));         // ue(v)
    READ_UVLC(vps.getFrameHeight(j));        // ue(v)
    READ_CODE(vps.getMapCountMinus1(j), 4);  // u(4)
    vps.allocateMap(j);
    if (vps.getMapCountMinus1(j) > 0) {
      READ_CODE(vps.getMultipleMapStreamsPresentFlag(j), 1);  // u(1)
    }
    vps.getMapAbsoluteCodingEnableFlag(j, 0) = true;
    for (size_t i = 1; i <= vps.getMapCountMinus1(j); i++) {
      if (vps.getMultipleMapStreamsPresentFlag(j)) {
        READ_CODE(vps.getMapAbsoluteCodingEnableFlag(j, i), 1);  // u(1)
      } else {
        vps.getMapAbsoluteCodingEnableFlag(j, i) = true;
      }
      if (static_cast<int>(vps.getMapAbsoluteCodingEnableFlag(j, i)) == 0) {
        if (i > 0) {
          READ_UVLC(vps.getMapPredictorIndexDiff(j,
                                                 i));  // ue(v)
        } else {
          vps.getMapPredictorIndexDiff(j, i) = false;
        }
      }
    }
    READ_CODE(vps.getAuxiliaryVideoPresentFlag(j), 1);  // u(1)
    READ_CODE(vps.getOccupancyVideoPresentFlag(j), 1);  // u(1)
    uint32_t value;
    READ_CODE_DESC(value, 1, "GeometryVideoPresentFlag"); vps.setGeometryVideoPresentFlag(j, value);   // u(1)
    READ_CODE(vps.getAttributeVideoPresentFlag(j), 1);  // u(1)
    if (vps.getOccupancyVideoPresentFlag(j)) {
      occupancyInformation(vps.getOccupancyInformation(j), vps.getAtlasId(j), bitstream);
    }
    if (vps.getGeometryVideoPresentFlag(j)) {
      geometryInformation(vps.getGeometryInformation(j), vps, vps.getAtlasId(j), bitstream);
    }
    if (vps.getAttributeVideoPresentFlag(j)) {
      attributeInformation(vps.getAttributeInformation(j), vps, vps.getAtlasId(j), bitstream);
    }
  }
  uint32_t value;
  READ_CODE_DESC(value, 1, "ExtensionPresentFlag"); vps.setExtensionPresentFlag(value); // u(1)
  if (vps.getExtensionPresentFlag()) {
    READ_CODE_DESC(value, 8, "ExtensionCount"); vps.setExtensionCount(value);  // u(8)
  }
  if (vps.getExtensionCount()) {
    READ_UVLC_DESC(value, "ExtensionLengthMinus1"); vps.setExtensionLengthMinus1(value);  // ue(v)
    for (uint32_t i = 0; i < vps.getExtensionCount(); i++) {
      READ_CODE_DESC(value, 8, "ExtensionType"); vps.setExtensionType(i, value);     // u(8)
      READ_CODE_DESC(value, 16, "ExtensionLength"); vps.setExtensionLength(i, value);  // u(16)
      Bitstream tempBitstream;
#if defined(BITSTREAM_TRACE)
      tempBitstream.setTrace(true);
      tempBitstream.setLogger( bitstream.getLogger() );
#endif
      bitstream.copyToBits(tempBitstream, vps.getExtensionLength(i));
      vpsExtension(vps, i, tempBitstream);
    }
  }
  byteAlignment(bitstream);
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.2 Profile, tier, and level syntax
void
V3CReader::profileTierLevel(ProfileTierLevel& ptl, Bitstream& bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t ptl_reserved_zero_7bits = 0;
  uint32_t ptl_reserved_0xfff_12bits = 0x0FFF;
  READ_CODE(ptl.getTierFlag(), 1);                  // u(1)
  uint32_t value;
  READ_CODE_DESC(value, 7,"ProfileCodecGroupIdc");
  ptl.setPtlCodecGroupIdc(value);// u(7)
  READ_CODE(ptl.getProfileToolsetIdc(), 8);         // u(8)
  READ_CODE(ptl.getProfileReconstructionIdc(), 8);  // u(8)
  READ_CODE(ptl.getAttributeTierFlag(), 1);         // u(1)
  READ_CODE(ptl.getAttributeLevelIdc(), 8);         // u(8)
  READ_CODE(ptl_reserved_zero_7bits, 7);            // u(7)
  READ_CODE(ptl.getMaxDecodesIdc(), 4);             // u(4)
  READ_CODE(ptl_reserved_0xfff_12bits, 12);         // u(12)
  READ_CODE(ptl.getLevelIdc(), 8);                  // u(8)
  READ_CODE(ptl.getNumSubProfiles(), 6);            // u(6)
  READ_CODE(ptl.getExtendedSubProfileFlag(), 1);    // u(1)
  ptl.allocate();
  for (size_t i = 0; i < ptl.getNumSubProfiles(); i++) {
    size_t v = ptl.getExtendedSubProfileFlag() == 0 ? 32 : 64;
    READ_CODE(ptl.getSubProfileIdc(i), v);  // u(v)
  }
  READ_CODE(ptl.getToolConstraintsPresentFlag(), 1);  // u(1)
  if (ptl.getToolConstraintsPresentFlag()) {
    profileToolsetConstraintsInformation(
      ptl.getProfileToolsetConstraintsInformation(), bitstream);
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.3 Occupancy parameter set syntax
void
V3CReader::occupancyInformation(OccupancyInformation& oi,
                                uint32_t             atlasId,
                                Bitstream&            bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(oi.getOccupancyCodecId(), 8);                    // u(8)
  READ_CODE(oi.getLossyOccupancyCompressionThreshold(), 8);  // u(8)
  READ_CODE(oi.getOccupancy2DBitdepthMinus1(), 5);           // u(5)
  READ_CODE(oi.getOccupancyMSBAlignFlag(), 1);               // u(1)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.4 Geometry parameter set syntax
void
V3CReader::geometryInformation(GeometryInformation& gi,
                               V3CParameterSet&     vps,
                               uint32_t             atlasId,
                               Bitstream&           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(gi.getGeometryCodecId(), 8);                      // u(8)
  READ_CODE(gi.getGeometry2dBitdepthMinus1(), 5);             // u(5)
  READ_CODE(gi.getGeometryMSBAlignFlag(), 1);                 // u(1)
  uint32_t value;
  READ_CODE_DESC(value, 5, "gi.getGeometry3dCoordinatesBitdepthMinus1()");  // u(5)
  gi.setGeometry3dCoordinatesBitdepthMinus1(value);
  if (vps.getAuxiliaryVideoPresentFlag(atlasId)) {
    READ_CODE(gi.getAuxiliaryGeometryCodecId(), 8);  // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.5 Attribute information
void
V3CReader::attributeInformation(AttributeInformation& ai,
                                V3CParameterSet&      vps,
                                uint32_t             atlasId,
                                Bitstream&            bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(ai.getAttributeCount(), 7);  // u(7)
  ai.allocate(ai.getAttributeCount());
  for (uint32_t i = 0; i < ai.getAttributeCount(); i++) {
    READ_CODE(ai.getAttributeTypeId(i), 4);   // u(4)
    READ_CODE(ai.getAttributeCodecId(i), 8);  // u(8)
    if (vps.getAuxiliaryVideoPresentFlag(atlasId)) {
      READ_CODE(ai.getAuxiliaryAttributeCodecId(i), 8);  // u(8)
    }
    ai.getAttributeMapAbsoluteCodingPersistenceFlag(i) = true;
    if (vps.getMapCountMinus1(atlasId) > 0) {
      READ_CODE(ai.getAttributeMapAbsoluteCodingPersistenceFlag(i),
                1);  // u(1)
    }
    READ_CODE(ai.getAttributeDimensionMinus1(i), 6);  // u(6)
    if (ai.getAttributeDimensionMinus1(i) > 0) {
      READ_CODE(ai.getAttributeDimensionPartitionsMinus1(i), 6);  // u(6)
      int32_t remainingDimensions = ai.getAttributeDimensionMinus1(i);
      int32_t k = ai.getAttributeDimensionPartitionsMinus1(i);
      for (int32_t j = 0; j < k; j++) {
        if (k - j == remainingDimensions) {
          ai.getAttributePartitionChannelsMinus1(i, j) = 0;
        } else {
          READ_UVLC(ai.getAttributePartitionChannelsMinus1(i, j));  // ue(v)
        }
        remainingDimensions -=
          ai.getAttributePartitionChannelsMinus1(i, j) + 1;
      }
      ai.getAttributePartitionChannelsMinus1(i, k) = remainingDimensions;
    }
    READ_CODE(ai.getAttribute2dBitdepthMinus1(i), 5);  // u(5)
    READ_CODE(ai.getAttributeMSBAlignFlag(i), 1);      // u(1)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.6 Profile toolset constraints information syntax
void
V3CReader::profileToolsetConstraintsInformation(
  ProfileToolsetConstraintsInformation& ptci,
  Bitstream&                            bitstream) {
  uint32_t zero = 0;
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(ptci.getOneFrameOnlyFlag(), 1);                         // u(1)
  READ_CODE(ptci.getEOMContraintFlag(), 1);                         // u(1)
  READ_CODE(ptci.getMaxMapCountMinus1(), 4);                        // u(4)
  READ_CODE(ptci.getMaxAtlasCountMinus1(), 4);                      // u(4)
  READ_CODE(ptci.getMultipleMapStreamsConstraintFlag(), 1);         // u(1)
  READ_CODE(ptci.getPLRConstraintFlag(), 1);                        // u(1)
  READ_CODE(ptci.getAttributeMaxDimensionMinus1(), 6);              // u(6)
  READ_CODE(ptci.getAttributeMaxDimensionPartitionsMinus1(), 6);    // u(6)
  READ_CODE(ptci.getNoEightOrientationsConstraintFlag(), 1);        // u(1)
  READ_CODE(ptci.getNo45DegreeProjectionPatchConstraintFlag(), 1);  // u(1)
  READ_CODE(ptci.getRestrictedGeometryFlag(), 1);                   // u(1)
  READ_CODE(zero, 5);                                               // u(5)
  READ_CODE(ptci.getNumReservedConstraintBytes(), 8);               // u(8)
  if(ptci.getNumReservedConstraintBytes() > 0) {
    READ_CODE(ptci.getNoSubdivisionFlag(), 1);                         // u(1)
    READ_CODE(ptci.getNoDisplacementFlag(), 1);                        // u(1)
    READ_CODE(ptci.getDisplacementAscendingFlag(), 1);                 // u(1)
    READ_CODE(ptci.getDisplacementDimensionConstraintFlag(), 1);       // u(1)
    READ_CODE(ptci.getDisplacementVideoFlag(), 1);                     // u(1)
    READ_CODE(ptci.getNoPackedVideoFlag(), 1);                         // u(1)
    READ_CODE(zero, 2);                                               // u(2)
  }
  ptci.allocate();
  for (size_t i = 1; i < ptci.getNumReservedConstraintBytes(); i++) {
    READ_CODE(ptci.getReservedConstraintByte(i), 8);  // u(8)
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.7 Packing information syntax
void
V3CReader::packingInformation(PackingInformation& pi,
                              V3CParameterSet&    vps,
                              uint32_t            atlasId,
                              Bitstream&          bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  pi.pin_codec_id(bitstream.read(8));
  pi.pin_occupancy_present_flag(bitstream.read(1));
  pi.pin_geometry_present_flag(bitstream.read(1));
  pi.pin_attribute_present_flag(bitstream.read(1));

  if (pi.pin_occupancy_present_flag()) {
    pi.pin_occupancy_2d_bit_depth_minus1(bitstream.read(5));
    pi.pin_occupancy_MSB_align_flag(bitstream.read(1));
    pi.pin_lossy_occupancy_compression_threshold(bitstream.read(8));
  }
  if (pi.pin_geometry_present_flag()) {
    pi.pin_geometry_2d_bit_depth_minus1(bitstream.read(5));
    pi.pin_geometry_MSB_align_flag(bitstream.read(1));
    pi.pin_geometry_3d_coordinates_bit_depth_minus1(bitstream.read(5));
  }

  if (pi.pin_attribute_present_flag()) {
    pi.pin_attribute_count(bitstream.read(7));
    for (size_t i = 0; i < pi.pin_attribute_count(); i++) {
      pi.pin_attribute_type_id(i, (AiAttributeTypeId)(bitstream.read(4)));
      pi.pin_attribute_2d_bit_depth_minus1(i, bitstream.read(5));
      pi.pin_attribute_MSB_align_flag(i, bitstream.read(1));
      pi.pin_attribute_map_absolute_coding_persistence_flag(i, bitstream.read(1));
      pi.pin_attribute_dimension_minus1(i, bitstream.read(6));
      auto d = pi.pin_attribute_dimension_minus1(i);
      uint8_t m = 0;
      if (d == 0) {
        pi.pin_attribute_dimension_partitions_minus1(i, 0);
      } else {
        pi.pin_attribute_dimension_partitions_minus1(i, bitstream.read(6));
        m = pi.pin_attribute_dimension_partitions_minus1(i);
      }
      for (uint8_t k = 0; k < m; k++) {
        uint8_t n = 0;
        if (k + d == m) {
          pi.pin_attribute_partition_channels_minus1(i, k, 0);
        } else {
          pi.pin_attribute_partition_channels_minus1(i, k, bitstream.readUvlc());
          n = pi.pin_attribute_partition_channels_minus1(i, k);
        }
        d -= (uint8_t)(n + 1);
      }
      pi.pin_attribute_partition_channels_minus1(i, m, d);
    }
  }

  pi.pin_regions_count_minus1(bitstream.readUvlc());
  for (size_t i = 0; i <= pi.pin_regions_count_minus1(); ++i) {
    pi.pin_region_tile_id(i, bitstream.read(8));
    pi.pin_region_type_id_minus2(i, bitstream.read(2));
    pi.pin_region_top_left_x(i, bitstream.read(16));
    pi.pin_region_top_left_y(i, bitstream.read(16));
    pi.pin_region_width_minus1(i, bitstream.read(16));
    pi.pin_region_height_minus1(i, bitstream.read(16));
    pi.pin_region_unpack_top_left_x(i, bitstream.read(16));
    pi.pin_region_unpack_top_left_y(i, bitstream.read(16));
    pi.pin_region_rotation_flag(i, bitstream.read(1));
    if (pi.pinRegionTypeId(i) == V3CUnitType::V3C_AVD ||
        pi.pinRegionTypeId(i) == V3CUnitType::V3C_GVD) {
      pi.pin_region_map_index(i, bitstream.read(4));
      pi.pin_region_auxiliary_data_flag(i, bitstream.read(1));
    }
    if (pi.pinRegionTypeId(i) == V3CUnitType::V3C_AVD) {
      pi.pin_region_attr_index(i, bitstream.read(7));
      const auto k = pi.pin_region_attr_index(i);
      if (pi.pin_attribute_dimension_minus1(k) > 0) {
        pi.pin_region_attr_partition_index(i, bitstream.read(5));
      }
    }
  }
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.8 VPS extension syntax
void
V3CReader::vpsExtension(V3CParameterSet& vps,
    uint8_t          index,
    Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    auto type = V3CExtensionType(vps.getExtensionType(index));
    if (type == VPS_EXT_PACKED) {
        // Packed
      vpsPackedVideoExtension(vps, vps.getVpsPackedVideoExtension(), bitstream);
    }
    else if (type == VPS_EXT_MIV) {
        // Miv - Specified in ISO/IEC 23090-12
    }
    else if (type == VPS_EXT_MIV2) {
        // Miv2 - Specified in ISO/IEC 23090-12:2023
    }
    else if (type == VPS_EXT_VDMC) {
        vpsVdmcExtension(vps, vps.getVpsVdmcExtension(), bitstream);
    }
    else {
        vps.getExtensionDataByte(index).resize(vps.getExtensionLength(index));
        for (size_t i = 0; i < vps.getExtensionLength(index); i++) {
            uint32_t data = 0;
            READ_CODE(data, 8);  // u(8)
            vps.setExtensionDataByte(index, i, data);
        }
    }
    lengthAlignment(bitstream);
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.9 Packed video extension syntax

// 8.3.4.10 Length alignment syntax
void
V3CReader::lengthAlignment(Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    uint32_t zero = 0;
    while (!lengthAligned(bitstream)) {
        READ_CODE(zero, 1);  // f(1): equal to 0
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}
void
V3CReader:: vpsPackedVideoExtension(V3CParameterSet&  vps,
                                    VpsPackedVideoExtension& packedVideoExt,
                                    Bitstream&        bitstream){
  TRACE_BITSTREAM_IN("%s", __func__);
  packedVideoExt.setAtlasCount(vps.getAtlasCountMinus1() + 1);
  for (size_t atlasIdx = 0; atlasIdx < packedVideoExt.getAtlasCount(); atlasIdx++) {
    TRACE_BITSTREAM("atlasID : %d \n", vps.getAtlasId(atlasIdx));
    int value;
    READ_CODE_DESC(value, 1, "PackedVideoPresentFlag");
    packedVideoExt.setPackedVideoPresentFlag(atlasIdx, value);
    if(packedVideoExt.getPackedVideoPresentFlag(atlasIdx))
    packingInformation(packedVideoExt.getPackingInformation(atlasIdx), vps, vps.getAtlasId(atlasIdx), bitstream);;
  }
}
// 8.3.4.11 V3C parameter set V-DMC extension syntax
void
V3CReader::vpsVdmcExtension(V3CParameterSet& vps,
    VpsVdmcExtension& vmcExt,
    Bitstream& bitstream) {
    TRACE_BITSTREAM_IN("%s", __func__);
    vmcExt.setAtlasCount(vps.getAtlasCountMinus1() + 1);
    for (size_t atlasIdx = 0; atlasIdx < vmcExt.getAtlasCount(); atlasIdx++) {
        uint16_t atlasID = vps.getAtlasId(atlasIdx);
        TRACE_BITSTREAM("atlasID : %d \n", atlasID);
        uint8_t value;
        READ_CODE_DESC(value, 8, "VpsExtMeshDataSubstreamCodecId");     // u(8)
        vmcExt.setVpsExtMeshDataSubstreamCodecId(value, atlasID);
        READ_CODE_DESC(value, 5, "VpsExtMeshGeo3dBitdepthMinus1");      // u(5)
        vmcExt.setVpsExtMeshGeo3dBitdepthMinus1(value, atlasID);
        READ_CODE_DESC(value, 1, "VpsExtMeshGeo3dMSBAlignFlag");        // u(1)
        vmcExt.setVpsExtMeshGeo3dMSBAlignFlag(value, atlasID);
        READ_CODE_DESC(value, 8, "VpsExtMeshDataAttributeCount");       // u(8)
        vmcExt.setVpsExtMeshDataAttributeCount(value, atlasID);
        size_t numNonVideoAttribute = vmcExt.getVpsExtMeshDataAttributeCount(atlasID);
        for (size_t i = 0; i < vmcExt.getVpsExtMeshDataAttributeCount(atlasID); i++) {
            TRACE_BITSTREAM("basemesh attribute idx : %d \n", i);
            READ_CODE_DESC(value, 7, "VpsExtMeshAttributeIndex");          // u(7)
            vmcExt.setVpsExtMeshAttributeIndex(value, atlasID, i);
            READ_CODE_DESC(value, 5, "VpsExtMeshAttributeBitdepthMinus1"); // u(5)
            vmcExt.setVpsExtMeshAttributeBitdepthMinus1(value, atlasID, i);
            READ_CODE_DESC(value, 1, "VpsExtMeshAttributeMSBAlignFlag");   // u(1)
            vmcExt.setVpsExtMeshAttributeMSBAlignFlag(value, atlasID, i);
            READ_CODE_DESC(value, 4, "VpsExtMeshAttributeType");           // u(4)
            vmcExt.setVpsExtMeshAttributeType(value, atlasID, i);
        }
        READ_CODE_DESC(value, 1, "VpsExtACDisplacementPresentFlag");  // u(1)
        vmcExt.setVpsExtACDisplacementPresentFlag(atlasID, value);
        if (vmcExt.getVpsExtACDisplacementPresentFlag(atlasID)) {
          displacementInformation(vmcExt.getDisplacementInformation(atlasID), atlasID, bitstream);
        }

        READ_CODE_DESC(value, 1, "VpsExtConsistentAttributeFrameFlag");     // u(1)
        vmcExt.setVpsExtConsistentAttributeFrameFlag(value, atlasID);
        uint32_t vpsAttributeNominalFrameSizeCount = vps.getVpsAttributeNominalFrameSizeCount(atlasID);
        vmcExt.allocateAttributeFramesize(vpsAttributeNominalFrameSizeCount, atlasID);
        for (uint32_t i = 0; i < vpsAttributeNominalFrameSizeCount; i++) {
            TRACE_BITSTREAM("video attribute idx : %d \n", i);
            uint32_t size = 0;
            READ_UVLC_DESC(size, "VpsExtAttributeFrameWidth"); // ue(v)
            vmcExt.setVpsExtAttributeFrameWidth(size, atlasID, i);
            READ_UVLC_DESC(size, "VpsExtAttributeFrameHeight"); // ue(v)
            vmcExt.setVpsExtAttributeFrameHeight(size, atlasID, i);
        }
    }
    TRACE_BITSTREAM_OUT("%s", __func__);
}

// 8.3.4.12 Displacement information syntax
void
V3CReader::displacementInformation(DisplacementInformation& di,
                                  uint32_t             atlasId,
                                  Bitstream&           bitstream) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(di.getDisplacementCodecId(), 8);                      // u(8)
  READ_CODE(di.get1dDisplacementFlag(), 1);                       // u(1)
  READ_CODE(di.getDisplacementReservedBits(), 15);                // u(15)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// C.2 Sample stream V3C unit syntax and semantics
// C.2.1 Sample stream V3C header syntax
void
V3CReader::sampleStreamV3CHeader(Bitstream&           bitstream,
                                 SampleStreamV3CUnit& ssvu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(ssvu.getSsvhUnitSizePrecisionBytesMinus1(), 3);  // u(3)
  READ_CODE(zero, 5);                                        // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}

// C.2.2 Sample stream NAL unit syntax
void
V3CReader::sampleStreamV3CUnit(Bitstream&           bitstream,
                               SampleStreamV3CUnit& ssvu,
                               V3CUnit&             v3cUnit) {
  TRACE_BITSTREAM_IN("%s", __func__);
  READ_CODE(v3cUnit.getSize(),
            8 * (ssvu.getSsvhUnitSizePrecisionBytesMinus1() + 1));  // u(v)
  auto pos = bitstream.getPosition();
  READ_VECTOR(v3cUnit.getBitstream().vector(), v3cUnit.getSize());
  uint8_t v3cUnitType8 = v3cUnit.getBitstream().buffer()[0];
  auto    v3cUnitType  = static_cast<V3CUnitType>(v3cUnitType8 >> 3);
  v3cUnit.getType() = v3cUnitType;
  //printf("sampleStreamV3CUnit => %s \n", toString( v3cUnit.getType()).c_str());
  printf("sampleStreamV3CUnit => %s size: %zu\n", toString( v3cUnit.getType()).c_str(), v3cUnit.getSize());
  fflush(stdout);
  TRACE_BITSTREAM_OUT(
    "%s type = %s ", __func__, toString(v3cUnit.getType()).c_str());
}

// D.2 Sample stream NAL unit syntax and semantics
// D.2.1 Sample stream NAL header syntax
void
V3CReader::sampleStreamNalHeader(Bitstream&           bitstream,
                                 SampleStreamNalUnit& ssnu) {
  TRACE_BITSTREAM_IN("%s", __func__);
  uint32_t zero = 0;
  READ_CODE(ssnu.getSizePrecisionBytesMinus1(), 3);  // u(3)
  READ_CODE(zero, 5);                                // u(5)
  TRACE_BITSTREAM_OUT("%s", __func__);
}


