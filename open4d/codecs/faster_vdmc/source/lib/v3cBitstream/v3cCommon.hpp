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
#pragma once

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include "util/image.hpp"

#if defined(WIN32) || defined(_WIN32)
#  define NOMINMAX
#  include <Windows.h>
#endif

#define MULTI_ATTRIBUTE_TEST 1
#define ENCDEC_CHECKSUM 0
//increasing order
#define SUBMESHID1ST 66
#define SUBMESHID2ND 82
#define SUBMESHID3RD  84
#define TILEID1ST 17
#define TILEID2ND  29
#define TILEID3ND  31

#define QUANT_INTER_TEMPORARY_FIX 1

#if defined(CONFORMANCE_LOG)
#define CONFORMANCE_LOG_ENC 1
#define CONFORMANCE_LOG_DEC 1
#define CONFORMANCE_LOG_CHECK 1
#define DEBUG_CONFORMANCE 0
#else
#define CONFORMANCE_LOG_ENC 0
#define CONFORMANCE_LOG_DEC 0
#define CONFORMANCE_LOG_CHECK 0
#define DEBUG_CONFORMANCE 0	
#endif

#define COMPRESS_VIDEO_PAC_ENABLE 1

namespace vmesh {

// ******************************************************************* //
// Common constants
// ******************************************************************* //
#define MAX_NUM_ATTR_PARTITIONS 64

enum ToolsetIdc {
  TOOLSET_0 = 128,
  TOOLSET_1 = 129
};

// *_level_idc shall be set equal to a value of 30 times the level number
enum GeometryLevelIdc {
  LEVEL_1_0 = 30,
  LEVEL_2_0 = 60,
  LEVEL_2_1 = 63,
  LEVEL_2_2 = 66,
  LEVEL_UNKNOWN = 256,
};

enum GeometryTierFlag {
  TIER_0 = 0,
  TIER_1 = 1
};

enum AttributeLevelIdc {
  ATTR_LEVEL_1_0 = 30,
  ATTR_LEVEL_2_0 = 60,
  ATTR_LEVEL_3_0 = 90,
  ATTR_LEVEL_UNKNOWN = 256,
};

enum AttributeTierFlag {
  ATTR_TIER_0 = 0,
  ATTR_TIER_1 = 1
};

enum ReconstructionIdc {
  REC_0 = 0,
  REC_1 = 1,
  REC_2 = 2,
  REC_3 = 3,
  REC_UNCONSTRAINED = 255,
};

enum VideoCodecGroupIdc {
  AVC_Progressive_High = 0,
  HEVC_Main10          = 1,
  HEVC444              = 2,
  VVC_Main10           = 3,
  HEVC_Main            = 4,
  //Reserved  5..126
  Video_MP4RA = 15
};

enum NonVideoCodecGroupIdc {
  BMS_Main = 0,
  BMS_Main_DAC_Main = 1,
  //Reserved  2..126
  NonVideo_MP4RA = 7
};

enum VideoType {
  VIDEO_OCCUPANCY = 0,
  VIDEO_GEOMETRY,
  VIDEO_GEOMETRY_D0,
  VIDEO_GEOMETRY_D1,
  VIDEO_GEOMETRY_D2,
  VIDEO_GEOMETRY_D3,
  VIDEO_GEOMETRY_D4,
  VIDEO_GEOMETRY_D5,
  VIDEO_GEOMETRY_D6,
  VIDEO_GEOMETRY_D7,
  VIDEO_GEOMETRY_D8,
  VIDEO_GEOMETRY_D9,
  VIDEO_GEOMETRY_D10,
  VIDEO_GEOMETRY_D11,
  VIDEO_GEOMETRY_D12,
  VIDEO_GEOMETRY_D13,
  VIDEO_GEOMETRY_D14,
  VIDEO_GEOMETRY_D15,
  VIDEO_GEOMETRY_RAW,
  VIDEO_PACKED,
  VIDEO_ATTRIBUTE,
  VIDEO_ATTRIBUTE_T0,
  VIDEO_ATTRIBUTE_T1  = VIDEO_ATTRIBUTE_T0 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T2  = VIDEO_ATTRIBUTE_T1 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T3  = VIDEO_ATTRIBUTE_T2 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T4  = VIDEO_ATTRIBUTE_T3 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T5  = VIDEO_ATTRIBUTE_T4 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T6  = VIDEO_ATTRIBUTE_T5 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T7  = VIDEO_ATTRIBUTE_T6 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T8  = VIDEO_ATTRIBUTE_T7 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T9  = VIDEO_ATTRIBUTE_T8 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T10 = VIDEO_ATTRIBUTE_T9 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T11 = VIDEO_ATTRIBUTE_T10 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T12 = VIDEO_ATTRIBUTE_T11 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T13 = VIDEO_ATTRIBUTE_T12 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T14 = VIDEO_ATTRIBUTE_T13 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_T15 = VIDEO_ATTRIBUTE_T14 + MAX_NUM_ATTR_PARTITIONS,
  VIDEO_ATTRIBUTE_RAW = VIDEO_ATTRIBUTE_T15 + MAX_NUM_ATTR_PARTITIONS,
  NUM_VIDEO_TYPE      = VIDEO_ATTRIBUTE_RAW + MAX_NUM_ATTR_PARTITIONS,
};
enum MetadataType {
  METADATA_GOF = 0,
  METADATA_FRAME,
  METADATA_PATCH
};

enum V3CUnitType {
  V3C_VPS = 0,       //  0: Sequence parameter set
  V3C_AD,            //  1: Patch Data Group
  V3C_OVD,           //  2: Occupancy Video Data
  V3C_GVD,           //  3: Geometry Video Data
  V3C_AVD,           //  4: Attribute Video Data
  V3C_PVD,           //  5: Packed Video Data
  V3C_CAD,           //  6: Common Atlas Data
  V3C_BMD,           //  7: Basemesh Data
  V3C_ADD,           //  8: AC displacement Data
  V3C_RSVD_09,       //  9: Reserved
  V3C_RSVD_10,       // 10: Reserved
  V3C_RSVD_11,       // 11: Reserved
  V3C_RSVD_12,       // 12: Reserved
  V3C_RSVD_13,       // 13: Reserved
  V3C_RSVD_14,       // 14: Reserved
  V3C_RSVD_15,       // 15: Reserved
  V3C_RSVD_16,       // 16: Reserved
  V3C_RSVD_17,       // 17: Reserved
  V3C_RSVD_18,       // 18: Reserved
  V3C_RSVD_19,       // 19: Reserved
  V3C_RSVD_20,       // 20: Reserved
  V3C_RSVD_21,       // 21: Reserved
  V3C_RSVD_22,       // 22: Reserved
  V3C_RSVD_23,       // 23: Reserved
  V3C_RSVD_24,       // 24: Reserved
  V3C_RSVD_25,       // 25: Reserved
  V3C_RSVD_26,       // 26: Reserved
  V3C_RSVD_27,       // 27: Reserved
  V3C_RSVD_28,       // 28: Reserved
  V3C_RSVD_29,       // 29: Reserved
  V3C_RSVD_30,       // 30: Reserved
  V3C_RSVD_31,       // 32: Reserved
  NUM_V3C_UNIT_TYPE  // 33: undefined
};

enum CodecGroup {
  CODEC_GROUP_AVC_PROGRESSIVE_HIGH = 0,
  CODEC_GROUP_HEVC_MAIN10          = 1,
  CODEC_GROUP_HEVC444              = 2,
  CODEC_GROUP_VVC_MAIN10           = 3,
  CODEC_GROUP_MP4RA                = 127  // => CCM SEI + oi/gi/ai codec id
};

enum V3CExtensionType {
  VPS_EXT_UNSPECIFIED = 0,
  VPS_EXT_PACKED,
  VPS_EXT_MIV,
  VPS_EXT_MIV2,
  VPS_EXT_VDMC
};

enum AttributeType {
  TEXCOORD = 0,
  NORMAL,
  MATERIALID,
  TRANSPARENCY,
  RELECTION,
  FACEGROUPID,
  SUBMESHID
};

// ******************************************************************* //
// Static functions
// ******************************************************************* //
static GeometryLevelIdc
getGeometryLevelIdcFromGeoAtlasSize(uint32_t maxGeoAtlasSize) {
  if (maxGeoAtlasSize <= 16384) return GeometryLevelIdc::LEVEL_1_0;
  else if (maxGeoAtlasSize <= 65536) return GeometryLevelIdc::LEVEL_2_0;
  else if (maxGeoAtlasSize <= 524288) return GeometryLevelIdc::LEVEL_2_1;
  else if (maxGeoAtlasSize <= 1048576) return GeometryLevelIdc::LEVEL_2_2;
  else return GeometryLevelIdc::LEVEL_UNKNOWN;
}

static GeometryLevelIdc
getGeometryLevelIdcFromNumTiles(uint32_t maxNumTiles) {
  if (maxNumTiles <= 1) return GeometryLevelIdc::LEVEL_1_0;
  else if (maxNumTiles <= 1) return GeometryLevelIdc::LEVEL_2_0;
  else if (maxNumTiles <= 2) return GeometryLevelIdc::LEVEL_2_1;
  else if (maxNumTiles <= 2) return GeometryLevelIdc::LEVEL_2_2;
  else return GeometryLevelIdc::LEVEL_UNKNOWN;
}

static GeometryLevelIdc
getGeometryLevelIdcFromNumSubmeshes(uint32_t maxNumSubmeshesPerFrame) {
  if (maxNumSubmeshesPerFrame <= 1) return GeometryLevelIdc::LEVEL_1_0;
  else if (maxNumSubmeshesPerFrame <= 1) return GeometryLevelIdc::LEVEL_2_0;
  else if (maxNumSubmeshesPerFrame <= 2) return GeometryLevelIdc::LEVEL_2_1;
  else if (maxNumSubmeshesPerFrame <= 4) return GeometryLevelIdc::LEVEL_2_2;
  else return GeometryLevelIdc::LEVEL_2_2;
}

static GeometryLevelIdc
getGeometryLevelIdcFromBasemeshVertices(uint32_t maxVertexPerBasemeshFrame) {
  if (maxVertexPerBasemeshFrame <= 4000) return GeometryLevelIdc::LEVEL_1_0;
  else if (maxVertexPerBasemeshFrame <= 8000) return GeometryLevelIdc::LEVEL_2_0;
  else if (maxVertexPerBasemeshFrame <= 12000) return GeometryLevelIdc::LEVEL_2_1;
  else if (maxVertexPerBasemeshFrame <= 24000) return GeometryLevelIdc::LEVEL_2_2;
  else return GeometryLevelIdc::LEVEL_UNKNOWN;
}

static AttributeLevelIdc
getAttributLevelIdc(uint32_t attributeAtlasSize) {
  if (attributeAtlasSize <= 2228224) return AttributeLevelIdc::ATTR_LEVEL_1_0;
  else if (attributeAtlasSize <= 8912896) return AttributeLevelIdc::ATTR_LEVEL_2_0;
  else if (attributeAtlasSize <= 35651584) return AttributeLevelIdc::ATTR_LEVEL_3_0;
  else return AttributeLevelIdc::ATTR_LEVEL_UNKNOWN;
}

static inline std::string
toString(VideoCodecGroupIdc val) {
  switch (val) {
  case VideoCodecGroupIdc::AVC_Progressive_High: return "AVC_Progressive_High";
  case VideoCodecGroupIdc::HEVC_Main10: return "HEVC_Main10";
  case VideoCodecGroupIdc::HEVC444: return "HEVC444";
  case VideoCodecGroupIdc::VVC_Main10: return "VVC_Main10";
  case VideoCodecGroupIdc::HEVC_Main: return "HEVC_Main";
  case VideoCodecGroupIdc::Video_MP4RA: return "MP4RA";
  default: return "UNKNOWN";
  }
}

static inline std::string
toString(NonVideoCodecGroupIdc val) {
  switch (val) {
  case NonVideoCodecGroupIdc::BMS_Main_DAC_Main: return "BMS_Main_DAC_Main";
  case NonVideoCodecGroupIdc::BMS_Main: return "BMS_Main";
  case NonVideoCodecGroupIdc::NonVideo_MP4RA: return "MP4RA";
  default: return "UNKNOWN";
  }
}

static inline std::string
toString(ToolsetIdc val) {
  switch (val) {
  case ToolsetIdc::TOOLSET_0: return "Toolset0";
  case ToolsetIdc::TOOLSET_1: return "Toolset1";
  default: return "UNKNOWN";
  }
}

static inline std::string
toString(GeometryLevelIdc val) {
  switch (val) {
  case GeometryLevelIdc::LEVEL_1_0: return "1.0";
  case GeometryLevelIdc::LEVEL_2_0: return "2.0";
  case GeometryLevelIdc::LEVEL_2_1: return "2.1";
  case GeometryLevelIdc::LEVEL_2_2: return "2.2";
  default: return "UNKNOWN";
  }
}

static inline std::string
toString(AttributeLevelIdc val) {
  switch (val) {
  case AttributeLevelIdc::ATTR_LEVEL_1_0: return "1.0";
  case AttributeLevelIdc::ATTR_LEVEL_2_0: return "2.0";
  case AttributeLevelIdc::ATTR_LEVEL_3_0: return "3.0";
  default: return "UNKNOWN";
  }
}
static inline std::string
toString(ReconstructionIdc val) {
  switch (val) {
  case ReconstructionIdc::REC_0: return "REC0";
  case ReconstructionIdc::REC_1: return "REC1";
  case ReconstructionIdc::REC_2: return "REC2";
  case ReconstructionIdc::REC_3: return "REC3";
  default: return "UNKNOWN";
  }
}
//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, VideoCodecGroupIdc val) {
  switch (val) {
  case VideoCodecGroupIdc::AVC_Progressive_High:
    out << "AVC_Progressive_High";
    break;
  case VideoCodecGroupIdc::HEVC_Main10: out << "HEVC_Main10"; break;
  case VideoCodecGroupIdc::HEVC444: out << "HEVC444"; break;
  case VideoCodecGroupIdc::VVC_Main10: out << "VVC_Main10"; break;
  case VideoCodecGroupIdc::HEVC_Main: out << "HEVC_Main"; break;
  case VideoCodecGroupIdc::Video_MP4RA: out << "MP4RA"; break;
  }
  return out;
}

static std::ostream&
operator<<(std::ostream& out, NonVideoCodecGroupIdc val) {
  switch (val) {
  case NonVideoCodecGroupIdc::BMS_Main:
    out << "BMS_Main";
    break;
  case NonVideoCodecGroupIdc::BMS_Main_DAC_Main: out << "BMS_Main_DAC_Main"; break;
  case NonVideoCodecGroupIdc::NonVideo_MP4RA: out << "MP4RA"; break;
  }
  return out;
}

//============================================================================

static std::istream&
operator>>(std::istream& in, VideoCodecGroupIdc& val) {
  std::string str;
  in >> str;
  val = VideoCodecGroupIdc::Video_MP4RA;
  if (str == "AVC_Progressive_High")
    val = VideoCodecGroupIdc::AVC_Progressive_High;
  if (str == "HEVC_Main10") val = VideoCodecGroupIdc::HEVC_Main10;
  if (str == "HEVC444") val = VideoCodecGroupIdc::HEVC444;
  if (str == "VVC_Main10") val = VideoCodecGroupIdc::VVC_Main10;
  if (str == "HEVC_Main") val = VideoCodecGroupIdc::HEVC_Main;
  if (str == "MP4RA") val = VideoCodecGroupIdc::Video_MP4RA;
  return in;
}

//----------------------------------------------------------------------------
static std::istream&
operator>>(std::istream& in, NonVideoCodecGroupIdc& val) {
  std::string str;
  in >> str;
  val = NonVideoCodecGroupIdc::NonVideo_MP4RA;
  if (str == "BMS_Main")
    val = NonVideoCodecGroupIdc::BMS_Main;
  if (str == "BMS_Main_DAC_Main") val = NonVideoCodecGroupIdc::BMS_Main_DAC_Main;
  if (str == "MP4RA") val = NonVideoCodecGroupIdc::NonVideo_MP4RA;
  return in;
}

//============================================================================
static bool
checkEncoderCfg(std::string& token, VideoCodecGroupIdc idc) {
  if (idc == 1 && token == "Main10") return true;
  else if (idc == 2 && token == "HEVC444") return true;
  else if (idc == 3 && token == "VVC_Main10") return true;
  else if (idc == 4 && token == "Main") return true;
  else {
    printf(
      "Profile in cfg: %s\tCodecGroupIdc: %d\n", token.c_str(), (int32_t)idc);
    return false;
  }
}
static VideoEncoderId
getVideoCoderId(VideoCodecGroupIdc idc) {
  if (idc == AVC_Progressive_High) return VideoEncoderId::JM;
  else if (idc == HEVC_Main10) return VideoEncoderId::HM;
  else if (idc == HEVC444) return VideoEncoderId::HM;
  else if (idc == VVC_Main10) return VideoEncoderId::VTM;
  else if (idc == HEVC_Main) return VideoEncoderId::HM;
  else return VideoEncoderId::UNKNOWN_VIDEO_ENCODER;
}

static inline std::string
toString(V3CUnitType type) {
  switch (type) {
  case V3C_VPS: return std::string("V3C_VPS"); break;
  case V3C_AD: return std::string("V3C_AD"); break;
  case V3C_BMD: return std::string("V3C_BMD"); break;
  case V3C_OVD: return std::string("V3C_OVD"); break;
  case V3C_GVD: return std::string("V3C_GVD"); break;
  case V3C_AVD: return std::string("V3C_AVD"); break;
  case V3C_PVD: return std::string("V3C_PVD"); break;
  case V3C_ADD: return std::string("V3C_ADD"); break;
  default: return std::string("reserved?"); break;
  }
}

static inline std::string
toString(VideoType type) {
  size_t typeIndex = (size_t)type;
  if (typeIndex == (size_t)VIDEO_OCCUPANCY)
    return std::string("occupancy map video ");
  else if (typeIndex == (size_t)VIDEO_GEOMETRY)
    return std::string("geometry video ");
  else if (typeIndex >= (size_t)VIDEO_GEOMETRY_D0
           && typeIndex <= (size_t)VIDEO_GEOMETRY_D15)
    return std::string("geometry D")
           + std::to_string(typeIndex - (size_t)VIDEO_GEOMETRY_D0)
           + std::string(" video ");
  else if (typeIndex == (size_t)VIDEO_GEOMETRY_RAW)
    return std::string("raw points geometry video ");
  else if (typeIndex == (size_t)VIDEO_ATTRIBUTE)
    return std::string("attribute video ");
  else if (typeIndex >= (size_t)VIDEO_ATTRIBUTE_T0
           && typeIndex <= (size_t)VIDEO_ATTRIBUTE_T15)
    return std::string("attribute T")
           + std::to_string(typeIndex - (size_t)VIDEO_ATTRIBUTE_T0)
           + std::string(" video ");
  else if (typeIndex == (size_t)VIDEO_ATTRIBUTE_RAW)
    return std::string("raw points attribute video ");
  else if (typeIndex == (size_t)VIDEO_PACKED)
    return std::string("packed video ");    
  else return std::string("not supported");
}

static bool
exist(const std::string& sString) {
  struct stat buffer;
  return (stat(sString.c_str(), &buffer) == 0);
}

static inline void
removeFile(const std::string string) {
  if (exist(string)) {
    if (remove(string.c_str()) != 0) {
      std::cout << "Could not remove the file: " << string << std::endl;
    }
  }
}

#ifndef _WIN32
static char
getSeparator() {
  return '/';
}
#else
static char
getSeparator() {
  return '\\';
}
#endif

static char
getSeparator(const std::string& eFilename) {
  auto pos1 = eFilename.find_last_of('/'), pos2 = eFilename.find_last_of('\\');
  auto pos = (std::max)(pos1 != std::string::npos ? pos1 : 0,
                        pos2 != std::string::npos ? pos2 : 0);
  return (pos != 0 ? eFilename[pos] : getSeparator());
}

static inline std::string
removeFileExtension(const std::string string) {
  size_t pos = string.find_last_of(".");
  return pos != std::string::npos ? string.substr(0, pos) : string;
}

static inline std::string
getDirectoryName(const std::string& string) {
  auto position = string.find_last_of(getSeparator(string));
  if (position != std::string::npos) { return string.substr(0, position); }
  return string;
}

static inline std::string
getBasename(const std::string& string) {
  auto position = string.find_last_of(getSeparator());
  if (position != std::string::npos) {
    return string.substr(position + 1, string.length());
  }
  return string;
}

static inline std::string
addVideoFormat(const std::string filename,
               const size_t      width,
               const size_t      height,
               const bool        isYUV = true,
               const bool        is420 = true,
               const std::string pixel = "8") {
  std::stringstream result;
  result << filename << "_" << width << "x" << height << "_" << pixel << "bit_"
         << ((isYUV & is420) ? "p420" : "p444") << (isYUV ? ".yuv" : ".rgb");
  return result.str();
}

template<typename T>
static inline const T
endianSwap(const T u) {
  union {
    T       u;
    uint8_t u8[sizeof(T)];
  } source, dest;
  source.u = u;
  for (size_t k = 0; k < sizeof(T); k++)
    dest.u8[k] = source.u8[sizeof(T) - k - 1];
  return dest.u;
}

static inline uint32_t
getFixedLengthCodeBitsCount(uint32_t range) {
  int count = 0;
  if (range > 0) {
    range -= 1;
    while (range > 0) {
      count++;
      range >>= 1;
    }
  }
  return count;
}

static inline int
floorLog2(uint32_t x) {
  if (x == 0) {
    // note: ceilLog2() expects -1 as return value
    return -1;
  }
#ifdef __GNUC__
  return 31 - __builtin_clz(x);
#else
#  ifdef _MSC_VER
  unsigned long r = 0;
  _BitScanReverse(&r, x);
  return r;
#  else
  int result = 0;
  if (x & 0xffff0000) {
    x >>= 16;
    result += 16;
  }
  if (x & 0xff00) {
    x >>= 8;
    result += 8;
  }
  if (x & 0xf0) {
    x >>= 4;
    result += 4;
  }
  if (x & 0xc) {
    x >>= 2;
    result += 2;
  }
  if (x & 0x2) {
    x >>= 1;
    result += 1;
  }
  return result;
#  endif
#endif
}

static inline int
ceilLog2(uint32_t x) {
  return (x == 0) ? -1 : floorLog2(x - 1) + 1;
}
}  // namespace vmesh
