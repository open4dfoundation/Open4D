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

#if defined(WIN32) || defined(_WIN32)
#  define NOMINMAX
#  include <Windows.h>
#endif

namespace atlas {

enum PatchOrientation {
  PATCH_ORIENTATION_DEFAULT = 0,  // 0: default
  PATCH_ORIENTATION_SWAP    = 1,  // 1: swap
  PATCH_ORIENTATION_ROT90   = 2,  // 2: rotation 90
  PATCH_ORIENTATION_ROT180  = 3,  // 3: rotation 180
  PATCH_ORIENTATION_ROT270  = 4,  // 4: rotation 270
  PATCH_ORIENTATION_MIRROR  = 5,  // 5: mirror
  PATCH_ORIENTATION_MROT90  = 6,  // 6: mirror + rotation 90
  PATCH_ORIENTATION_MROT180 = 7,  // 7: mirror + rotation 180
  PATCH_ORIENTATION_MROT270 =
    8  // 8: similar to SWAP, not used switched SWAP with ROT90 positions
};

enum TileType {
  P_TILE = 0,      // 0: Inter atlas tile
  I_TILE,          // 1: Intra atlas tile
  SKIP_TILE,       // 2: SKIP atlas tile
  P_TILE_ATTR,     // 3: Inter atlas tile
  I_TILE_ATTR,     // 4: Intra atlas tile
  SKIP_TILE_ATTR,  // 5: SKIP atlas tile
  RESERVED_7       // 7: 7 to N (N not defined?)
};

enum PatchModeITile {
  I_INTRA = 0,  //  0: Non-predicted patch mode
  I_RESERVED_1,   //  1: I_RESERVED Reserved modes
  I_RESERVED_2,   //  2: I_RESERVED Reserved modes
  I_RESERVED_3,   //  3: I_RESERVED Reserved modes
  I_RESERVED_4,   //  4: I_RESERVED Reserved modes
  I_RESERVED_5,   //  5: I_RESERVED Reserved modes
  I_RESERVED_6,   //  6: I_RESERVED Reserved modes
  I_RESERVED_7,   //  7: I_RESERVED Reserved modes
  I_RESERVED_8,   //  8: I_RESERVED Reserved modes
  I_RESERVED_9,   //  9: I_RESERVED Reserved modes
  I_RESERVED_10,  // 10: I_RESERVED Reserved modes
  I_RESERVED_11,  // 11: I_RESERVED Reserved modes
  I_RESERVED_12,  // 12: I_RESERVED Reserved modes
  I_RESERVED_13,  // 13: I_RESERVED Reserved modes
  I_END           // 14: Patch termination mode
};

enum PatchModeIAttrTile {
  I_INTRA_ATTR = 0,    //  0: Non-predicted patch mode
  I_RESERVED_ATTR_1,   //  1: I_RESERVED Reserved modes
  I_RESERVED_ATTR_2,   //  2: I_RESERVED Reserved modes
  I_RESERVED_ATTR_3,   //  3: I_RESERVED Reserved modes
  I_RESERVED_ATTR_4,   //  4: I_RESERVED Reserved modes
  I_RESERVED_ATTR_5,   //  5: I_RESERVED Reserved modes
  I_RESERVED_ATTR_6,   //  6: I_RESERVED Reserved modes
  I_RESERVED_ATTR_7,   //  7: I_RESERVED Reserved modes
  I_RESERVED_ATTR_8,   //  8: I_RESERVED Reserved modes
  I_RESERVED_ATTR_9,   //  9: I_RESERVED Reserved modes
  I_RESERVED_ATTR_10,  // 10: I_RESERVED Reserved modes
  I_RESERVED_ATTR_11,  // 11: I_RESERVED Reserved modes
  I_RESERVED_ATTR_12,  // 12: I_RESERVED Reserved modes
  I_RESERVED_ATTR_13,  // 13: I_RESERVED Reserved modes
  I_END_ATTR           // 14: Patch termination mode
};

enum PatchModePTile {
  P_SKIP = 0,  //  0: Patch Skip mode
  P_MERGE,     //  1: Patch Merge mode
  P_INTER,     //  2: Inter predicted Patch mode
  P_INTRA,     //  3: Non-predicted Patch mode
  P_RESERVED_4,   //  4: Reserved modes
  P_RESERVED_5,   //  5: Reserved modes
  P_RESERVED_6,   //  6: Reserved modes
  P_RESERVED_7,   //  7: Reserved modes
  P_RESERVED_8,   //  8: Reserved modes
  P_RESERVED_9,   //  9: Reserved modes
  P_RESERVED_10,  // 10: Reserved modes
  P_RESERVED_11,  // 11: Reserved modes
  P_RESERVED_12,  // 12: Reserved modes
  P_RESERVED_13,  // 13: Reserved modes
  P_END,          // 14: Patch termination mode
};

enum PatchModePAttrTile {
  P_SKIP_ATTR = 0,     //  0: Patch Skip mode
  P_INTRA_ATTR,        //  1: Non-predicted Patch mode
  P_RESERVED_ATTR_2,   //  2: Reserved mode
  P_RESERVED_ATTR_3,   //  3: Reserved mode
  P_RESERVED_ATTR_4,   //  4: Reserved mode
  P_RESERVED_ATTR_5,   //  5: Reserved mode
  P_RESERVED_ATTR_6,   //  6: Reserved mode
  P_RESERVED_ATTR_7,   //  7: Reserved mode
  P_RESERVED_ATTR_8,   //  8: Reserved mode
  P_RESERVED_ATTR_9,   //  9: Reserved mode
  P_RESERVED_ATTR_10,  // 10: Reserved mode
  P_RESERVED_ATTR_11,  // 11: Reserved mode
  P_RESERVED_ATTR_12,  // 12: Reserved mode
  P_RESERVED_ATTR_13,  // 13: Reserved mode
  P_END_ATTR,          // 14: Patch termination mode
};

enum HashPatchType {
  PROJECTED = 0,  // 0: protected
  RAW,            // 1: raw
  EOM             // 2: eom
};

enum SeiPayloadType {
  BUFFERING_PERIOD   = 0,  //  0: buffering period
  ATLAS_FRAME_TIMING = 1,  //  1: atlas frame timing
  FILLER_PAYLOAD = 2,  //  2: filler payload -> Specified in ISO/IEC 23002-7
  USER_DATAREGISTERED_ITUTT35 =
    3,  //  3: user data RegisteredItuTT35 -> Specified in ISO/IEC 23002-7
  USER_DATA_UNREGISTERED =
    4,  //  4: user data unregistered -> Specified in ISO/IEC 23002-7
  RECOVERY_POINT           = 5,   //  5: recovery point
  NO_RECONSTRUCTION        = 6,   //  6: no reconstruction
  TIME_CODE                = 7,   //  7: time code
  SEI_MANIFEST             = 8,   //  8: sei manifest
  SEI_PREFIX_INDICATION    = 9,   //  9: sei prefix indication
  ACTIVE_SUB_BITSTREAMS    = 10,  // 10: active subBitstreams
  COMPONENT_CODEC_MAPPING  = 11,  // 11: component Codec mapping
  SCENE_OBJECT_INFORMATION = 12,  // 12: scene object information m52705
  OBJECT_LABEL_INFORMATION = 13,  // 13: Object label information
  PATCH_INFORMATION        = 14,  // 14: Patch information SEI message syntax
  VOLUMETRIC_RECTANGLE_INFORMATION =
    15,                                 // 15: Volumetric rectangle information
  ATLAS_OBJECT_INFORMATION       = 16,  // 16: atlas information
  VIEWPORT_CAMERA_PARAMETERS     = 17,  // 17: viewport camera parameters
  VIEWPORT_POSITION              = 18,  // 18: viewport position
  DECODED_ATLAS_INFORMATION_HASH = 19,  // 19: decoded atlas information hash
  ATTRIBUTE_TRANSFORMATION_PARAMS =
    64,  // 64: attribute transformation params -> Specified in Annex H
  OCCUPANCY_SYNTHESIS = 65,  // 65: occupancy synthesis -> Specified in Annex H
  GEOMETRY_SMOOTHING  = 66,  // 66: geometry smoothing -> Specified in Annex H
  ATTRIBUTE_SMOOTHING = 67,  // 67: attribute smoothing -> Specified in Annex H
  VPCC_REGISTERED     = 68,  // 68: v-pcc registered SEI
  VIEWING_SPACE = 128,  // 128: viewing space -> Specified in ISO/IEC 23090-12
  VIEWING_SPACE_HANDLING =
    129,  // 129: viewing space handling -> Specified in ISO/IEC 23090-12
  GEOMETRY_UPSCALING_PARAMETERS =
    130,  // 130: geometry upscaling parameters -> Specified in ISO/IEC 23090-12
  ATLAS_VIEW_ENABLED =
    131,  // 131: atlas view enabled -> Specified in ISO/IEC 23090-12
  OMAF_V1_COMPATIBLE =
    132,  // 132: omaf_v1_compatible -> Specified in ISO/IEC 23090-12
  GEOMETRY_ASSISTANCE =
    133,  // 133: geometry assistance -> Specified in ISO/IEC 23090-12
  EXTENDED_GEOMETRY_ASSISTANCE =
    134,  // 134: extended geometry assistance -> Specified in ISO/IEC 23090-12
  MIV_REGISTERED =
    135,  // 134: miv registered SEI -> Specified in ISO/IEC 23090-12
  VDMC_REGISTERED =
    192,  // 192: v-dmc registered SEI -> Specified in ISO/IEC 23090-29
  RESERVED_MESSAGE =
    69  //  69: reserved message -> Specified in ISO/IEC 23002-7
};

enum VdmcSeiPayloadType {
  ZIPPERING = 0,  //  0: zippering
  SUBMESH_SOI_RELATIONSHIP_INDICATION =
    1,  //  1: submesh SOI relationship indication
  SUBMESH_DISTORTION_INDICATION = 2,  //  2: submesh distortion indication
  LOD_EXTRACTION_INFORMATION    = 3,  //  3: LoD extraction information
  TILE_SUBMESH_MAPPING          = 4,  //  4: submesh tile mapping
  ATTRIBUTE_EXTRACTION_INFORMATION =
    5,  //  5: Attribute extraction information
  RESERVED_MESSAGE_VDMC = 6
};

enum VpccSeiPayloadType {
  RESERVED_MESSAGE_VPCC = 0
};

enum AtlasNalUnitType {
  ATLAS_NAL_TRAIL_N =
    0,  //  0: Coded tile of a non-TSA, non STSA trailing atlas frame ACL
  ATLAS_NAL_TRAIL_R,  //  1: Coded tile of a non-TSA, non STSA trailing atlas frame ACL
  ATLAS_NAL_TSA_N,   //  2: Coded tile of a TSA atlas frame ACL
  ATLAS_NAL_TSA_R,   //  3: Coded tile of a TSA atlas frame ACL
  ATLAS_NAL_STSA_N,  //  4: Coded tile of a STSA atlas frame ACL
  ATLAS_NAL_STSA_R,  //  5: Coded tile of a STSA atlas frame ACL
  ATLAS_NAL_RADL_N,  //  6: Coded tile of a RADL atlas frame ACL
  ATLAS_NAL_RADL_R,  //  7: Coded tile of a RADL atlas frame ACL
  ATLAS_NAL_RASL_N,  //  8: Coded tile of a RASL atlas frame ACL
  ATLAS_NAL_RASL_R,  //  9: Coded tile of a RASL atlas frame ACL
  ATLAS_NAL_SKIP_N,  // 10: Coded tile of a skipped atlas frame ACL
  ATLAS_NAL_SKIP_R,  // 11: Coded tile of a skipped atlas frame ACL
  ATLAS_NAL_RSV_ACL_N12,  // 12: Reserved non-IRAP sub-layer non-reference ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_N14,  // 14: Reserved non-IRAP sub-layer non-reference ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_R13,  // 13: Reserved non-IRAP sub-layer reference ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_R15,  // 15: Reserved non-IRAP sub-layer reference ACL NAL unit types ACL
  ATLAS_NAL_BLA_W_LP,         // 16: Coded tile of a BLA atlas frame ACL
  ATLAS_NAL_BLA_W_RADL,       // 17: Coded tile of a BLA atlas frame ACL
  ATLAS_NAL_BLA_N_LP,         // 18: Coded tile of a BLA atlas frame ACL
  ATLAS_NAL_GBLA_W_LP,        // 19: Coded tile of a GBLA atlas frame ACL
  ATLAS_NAL_GBLA_W_RADL,      // 20: Coded tile of a GBLA atlas frame ACL
  ATLAS_NAL_GBLA_N_LP,        // 21: Coded tile of a GBLA atlas frame ACL
  ATLAS_NAL_IDR_W_RADL,       // 22: Coded tile of an IDR atlas frame ACL
  ATLAS_NAL_IDR_N_LP,         // 23: Coded tile of an IDR atlas frame ACL
  ATLAS_NAL_GIDR_W_RADL,      // 24: Coded tile of a GIDR atlas frame ACL
  ATLAS_NAL_GIDR_N_LP,        // 25: Coded tile of a GIDR atlas frame ACL
  ATLAS_NAL_CRA,              // 26: Coded tile of a CRA atlas frame ACL
  ATLAS_NAL_GCRA,             // 27: Coded tile of a GCRA atlas frame ACL
  ATLAS_NAL_RSV_IRAP_ACL_28,  // 28: Reserved IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_IRAP_ACL_29,  // 29: Reserved IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_30,       // 30: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_31,       // 31: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_32,       // 32: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_33,       // 33: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_34,       // 34: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_RSV_ACL_35,       // 35: Reserved non-IRAP ACL NAL unit types ACL
  ATLAS_NAL_ASPS,             // 36: Atlas sequence parameter set non-ACL
  ATLAS_NAL_AFPS,             // 37: Atlas frame parameter set non-ACL
  ATLAS_NAL_AUD,              // 38: Access unit delimiter non-ACL
  ATLAS_NAL_V3C_AUD,          // 39: V3C access unit delimiter non-ACL
  ATLAS_NAL_EOS,              // 40: End of sequence non-ACL
  ATLAS_NAL_EOB,              // 41: End of bitstream non-ACL
  ATLAS_NAL_FD,               // 42: Filler non-ACL
  ATLAS_NAL_PREFIX_NSEI,  // 43: Non-essential supplemental enhancement information non-ACL
  ATLAS_NAL_SUFFIX_NSEI,  // 44: Non-essential supplemental enhancement information non-ACL
  ATLAS_NAL_PREFIX_ESEI,  // 45: Essential supplemental enhancement information non-ACL
  ATLAS_NAL_SUFFIX_ESEI,  // 46: Essential supplemental enhancement information non-ACL
  ATLAS_NAL_AAPS,         // 47: Atlas adaptation parameter set non-ACL
  ATLAS_NAL_RSV_NACL_48,  // 48:  Reserved non-ACL NAL unit types non-ACL
  ATLAS_NAL_RSV_NACL_49,  // 49: Reserved non-ACL NAL unit types non-ACL
  ATLAS_NAL_RSV_NACL_50,  // 50: Reserved non-ACL NAL unit types non-ACL
  ATLAS_NAL_RSV_NACL_51,  // 51: Reserved non-ACL NAL unit types non-ACL
  ATLAS_NAL_RSV_NACL_52,  // 52: Reserved non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_53,    // 53: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_54,    // 54: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_55,    // 55: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_56,    // 56: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_57,    // 57: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_58,    // 58: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_59,    // 59: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_60,    // 60: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_61,    // 61: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_62,    // 62: Unspecified non-ACL NAL unit types non-ACL
  ATLAS_NAL_UNSPEC_63     // 63: Unspecified non-ACL NAL unit types non-ACL
};

// ******************************************************************* //
// Static functions
// ******************************************************************* //

static inline std::string
toString(AtlasNalUnitType type) {
  switch (type) {
  case ATLAS_NAL_ASPS: return std::string("ATLAS_NAL_ASPS"); break;
  case ATLAS_NAL_AFPS: return std::string("ATLAS_NAL_AFPS"); break;
  case ATLAS_NAL_AUD: return std::string("ATLAS_NAL_AUD"); break;
  case ATLAS_NAL_TRAIL_N: return std::string("ATLAS_NAL_TRAIL_N"); break;
  case ATLAS_NAL_TRAIL_R: return std::string("ATLAS_NAL_TRAIL_R"); break;
  case ATLAS_NAL_TSA_N: return std::string("ATLAS_NAL_TSA_N"); break;
  case ATLAS_NAL_TSA_R: return std::string("ATLAS_NAL_TSA_R"); break;
  case ATLAS_NAL_STSA_N: return std::string("ATLAS_NAL_STSA_N"); break;
  case ATLAS_NAL_STSA_R: return std::string("ATLAS_NAL_STSA_R"); break;
  case ATLAS_NAL_RADL_N: return std::string("ATLAS_NAL_RADL_N"); break;
  case ATLAS_NAL_RADL_R: return std::string("ATLAS_NAL_RADL_R"); break;
  case ATLAS_NAL_RASL_N: return std::string("ATLAS_NAL_RASL_N"); break;
  case ATLAS_NAL_RASL_R: return std::string("ATLAS_NAL_RASL_R"); break;
  case ATLAS_NAL_SKIP_N: return std::string("ATLAS_NAL_SKIP_N"); break;
  case ATLAS_NAL_SKIP_R: return std::string("ATLAS_NAL_SKIP_R"); break;
  case ATLAS_NAL_IDR_N_LP: return std::string("ATLAS_NAL_IDR_N_LP"); break;
  case ATLAS_NAL_PREFIX_ESEI:
    return std::string("ATLAS_NAL_PREFIX_ESEI");
    break;
  case ATLAS_NAL_PREFIX_NSEI:
    return std::string("ATLAS_NAL_PREFIX_NSEI");
    break;
  case ATLAS_NAL_SUFFIX_ESEI:
    return std::string("ATLAS_NAL_SUFFIX_ESEI");
    break;
  case ATLAS_NAL_SUFFIX_NSEI:
    return std::string("ATLAS_NAL_SUFFIX_NSEI");
    break;
  default: return std::string("others"); break;
  }
}

static inline std::string
toString(TileType type) {
  switch (type) {
  case P_TILE: return std::string("P_TILE"); break;
  case I_TILE: return std::string("I_TILE"); break;
  case SKIP_TILE: return std::string("SKIP_TILE"); break;
  case P_TILE_ATTR: return std::string("P_TILE_ATTR"); break;
  case I_TILE_ATTR: return std::string("I_TILE_ATTR"); break;
  case SKIP_TILE_ATTR: return std::string("SKIP_TILE_ATTR"); break;
  default: return std::string("reserved?"); break;
  }
}

static inline std::string
toString(PatchModeITile type) {
  switch (type) {
  case I_INTRA: return std::string("I_INTRA"); break;
  case I_END: return std::string("I_END"); break;
  default: return std::string("reserved"); break;
  }
}

static inline std::string
toString(PatchModePTile type) {
  switch (type) {
  case P_SKIP: return std::string("P_SKIP"); break;
  case P_MERGE: return std::string("P_MERGE"); break;
  case P_INTER: return std::string("P_INTER"); break;
  case P_INTRA: return std::string("P_INTRA"); break;
  case P_END: return std::string("P_END"); break;
  default: return std::string("reserved"); break;
  }
}

static inline std::string
toString(PatchModeIAttrTile type) {
  switch (type){
  case I_INTRA_ATTR: return std::string("I_INTRA_ATTR"); break;
  case I_END_ATTR: return std::string("I_END_ATTR"); break;
  default: return std::string("reserved"); break;
  }
}

static inline std::string
toString(PatchModePAttrTile type) {
  switch (type){
  case P_SKIP_ATTR: return std::string("P_SKIP_ATTR"); break;
  case P_END_ATTR: return std::string("P_END_ATTR"); break;
  default: return std::string("reserved"); break;
  }
}

static inline std::string
toString(SeiPayloadType payloadType) {
  switch (payloadType) {
  case BUFFERING_PERIOD: return std::string("BUFFERING_PERIOD"); break;
  case ATLAS_FRAME_TIMING: return std::string("ATLAS_FRAME_TIMING"); break;
  case FILLER_PAYLOAD: return std::string("FILLER_PAYLOAD"); break;
  case USER_DATAREGISTERED_ITUTT35:
    return std::string("USER_DATAREGISTERED_ITUTT35");
    break;
  case USER_DATA_UNREGISTERED:
    return std::string("USER_DATA_UNREGISTERED");
    break;
  case RECOVERY_POINT: return std::string("RECOVERY_POINT"); break;
  case NO_RECONSTRUCTION: return std::string("NO_RECONSTRUCTION"); break;
  case TIME_CODE: return std::string("TIME_CODE"); break;
  case SEI_MANIFEST: return std::string("SEI_MANIFEST"); break;
  case SEI_PREFIX_INDICATION:
    return std::string("SEI_PREFIX_INDICATION");
    break;
  case ACTIVE_SUB_BITSTREAMS:
    return std::string("ACTIVE_SUB_BITSTREAMS");
    break;
  case COMPONENT_CODEC_MAPPING:
    return std::string("COMPONENT_CODEC_MAPPING");
    break;
  case SCENE_OBJECT_INFORMATION:
    return std::string("SCENE_OBJECT_INFORMATION");
    break;
  case OBJECT_LABEL_INFORMATION:
    return std::string("OBJECT_LABEL_INFORMATION");
    break;
  case PATCH_INFORMATION: return std::string("PATCH_INFORMATION"); break;
  case VOLUMETRIC_RECTANGLE_INFORMATION:
    return std::string("VOLUMETRIC_RECTANGLE_INFORMATION");
    break;
  case ATLAS_OBJECT_INFORMATION:
    return std::string("ATLAS_OBJECT_INFORMATION");
    break;
  case VIEWPORT_CAMERA_PARAMETERS:
    return std::string("VIEWPORT_CAMERA_PARAMETERS");
    break;
  case VIEWPORT_POSITION: return std::string("VIEWPORT_POSITION"); break;
  case DECODED_ATLAS_INFORMATION_HASH:
    return std::string("DECODED_ATLAS_INFORMATION_HASH");
    break;
  case ATTRIBUTE_TRANSFORMATION_PARAMS:
    return std::string("ATTRIBUTE_TRANSFORMATION_PARAMS");
    break;
  case OCCUPANCY_SYNTHESIS: return std::string("OCCUPANCY_SYNTHESIS"); break;
  case GEOMETRY_SMOOTHING: return std::string("GEOMETRY_SMOOTHING"); break;
  case ATTRIBUTE_SMOOTHING: return std::string("ATTRIBUTE_SMOOTHING"); break;
  case VPCC_REGISTERED: return std::string("VPCC_REGISTERED"); break;
  case VDMC_REGISTERED: return std::string("VDMC_REGISTERED"); break;
  case RESERVED_MESSAGE: return std::string("RESERVED_MESSAGE"); break;
  default: return std::string("others"); break;
  }
}

}  // namespace vmesh
