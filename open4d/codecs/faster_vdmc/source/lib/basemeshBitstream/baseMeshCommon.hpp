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

namespace basemesh {

// ******************************************************************* //
// Profile Tier Level
// ******************************************************************* //
enum BaseMeshProfile {
  BASEMESH_PROFILE_BASEMESH_MAIN = 0,
  BASEMESH_PROFILE_RESERVED = 1
};

// bmptl_level_idc shall be set equal to a value of 30 times the level number
enum BaseMeshLevel {
  BASEMESH_LEVEL_1_0 = 30,
  BASEMESH_LEVEL_2_0 = 60,
  BASEMESH_LEVEL_3_0 = 90
};

enum BaseMeshTier {
  BASEMESH_TIER_0 = 0,
  BASEMESH_TIER_1 = 1
};

// ******************************************************************* //
// Common constants
// ******************************************************************* //
enum BaseMeshSeiPayloadType {
  BASEMESH_BUFFERING_PERIOD            = 0,  //  0: basemesh buffering period
  BASEMESH_FRAME_TIMING                = 1,  //  1: basemesh frame timing
  BASEMESH_FILLER_PAYLOAD              = 2,  //  2: filler payload
  BASEMESH_USER_DATAREGISTERED_ITUTT35 = 3,  //  3: user data RegisteredItuTT35
  BASEMESH_USER_DATA_UNREGISTERED      = 4,  //  4: user data unregistered
  BASEMESH_RECOVERY_POINT              = 5,  //  5: recovery point
  BASEMESH_NO_RECONSTRUCTION           = 6,  //  6: no reconstruction
  BASEMESH_TIME_CODE                   = 7,  //  7: time code
  BASEMESH_SEI_MANIFEST                = 8,  //  8: sei manifest
  BASEMESH_SEI_PREFIX_INDICATION       = 9,  //  9: sei prefix indication
  BASEMESH_COMPONENT_CODEC_MAPPING     = 10,  //  10: component codec mapping
  BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS =
    11,  //  11: basemesh attribute transformation params
  BASEMESH_RESERVED_MESSAGE = 12,
};

//************//
enum BaseMeshNalUnitType {
  BASEMESH_NAL_TRAIL_N,
  BASEMESH_NAL_TRAIL_R,
  BASEMESH_NAL_TSA_N,
  BASEMESH_NAL_TSA_R,
  BASEMESH_NAL_STSA_N,
  BASEMESH_NAL_STSA_R,
  BASEMESH_NAL_RADL_N,
  BASEMESH_NAL_RADL_R,
  BASEMESH_NAL_RASL_N,
  BASEMESH_NAL_RASL_R,
  BASEMESH_NAL_SKIP_N,
  BASEMESH_NAL_SKIP_R,
  BASEMESH_NAL_RSV_BMCL_N12,
  BASEMESH_NAL_RSV_BMCL_N14,
  BASEMESH_NAL_RSV_BMCL_R13,
  BASEMESH_NAL_RSV_BMCL_R15,
  BASEMESH_NAL_BLA_W_LP,
  BASEMESH_NAL_BLA_W_RADL,
  BASEMESH_NAL_BLA_N_LP,
  BASEMESH_NAL_IDR_W_RADL,
  BASEMESH_NAL_IDR_N_LP,
  BASEMESH_NAL_CRA,
  BASEMESH_NAL_RSV_IRAP_BMCL_22,
  BASEMESH_NAL_RSV_IRAP_BMCL_23,
  BASEMESH_NAL_RSV_BMCL_24,
  BASEMESH_NAL_RSV_BMCL_25,
  BASEMESH_NAL_RSV_BMCL_26,
  BASEMESH_NAL_RSV_BMCL_27,
  BASEMESH_NAL_RSV_BMCL_28,
  BASEMESH_NAL_RSV_BMCL_29,
  BASEMESH_NAL_BMSPS,  //30
  BASEMESH_NAL_BMFPS,  //31
  BASEMESH_NAL_AUD,
  BASEMESH_NAL_EOS,
  BASEMESH_NAL_EOB,
  BASEMESH_NAL_FD,
  BASEMESH_NAL_PREFIX_NSEI,
  BASEMESH_NAL_SUFFIX_NSEI,
  BASEMESH_NAL_PREFIX_ESEI,
  BASEMESH_NAL_SUFFIX_ESEI,
  BASEMESH_NAL_RSV_NBMCL_40,
  BASEMESH_NAL_RSV_NBMCL_44,
  BASEMESH_NAL_UNSPEC_45,
  BASEMESH_NAL_UNSPEC_63,
};

enum BaseMeshType {
  P_BASEMESH = 0,    // 0: Inter base mesh
  I_BASEMESH,        // 1: Intra base mesh
  SKIP_BASEMESH,     // 2: SKIP base mesh
  RESERVED_BASEMESH  // 4: 4 to N (N not defined?)
};

//************//
enum VMCMeshType {
  BASE = 0,
  MOTION
};

enum BasemeshAttributeType {  // table 1
  BASEMESH_ATTRIBUTE_COLOR        = 0,
  BASEMESH_ATTRIBUTE_MATERIALID   = 1,
  BASEMESH_ATTRIBUTE_TRANSPARENCY = 2,
  BASEMESH_ATTRIBUTE_REFLECTANCE  = 3,
  BASEMESH_ATTRIBUTE_NORMAL       = 4,
  BASEMESH_ATTRIBUTE_TEXCOORD     = 5,
  BASEMESH_ATTRIBUTE_FACEGROUPID  = 6,
  BASEMESH_ATTRIBUTE_RESERVED7    = 7,
  BASEMESH_ATTRIBUTE_RESERVED8    = 8,
  BASEMESH_ATTRIBUTE_RESERVED9    = 9,
  BASEMESH_ATTRIBUTE_RESERVED10   = 10,
  BASEMESH_ATTRIBUTE_RESERVED11   = 11,
  BASEMESH_ATTRIBUTE_RESERVED12   = 12,
  BASEMESH_ATTRIBUTE_RESERVED13   = 13,
  BASEMESH_ATTRIBUTE_RESERVED14   = 14,
  BASEMESH_ATTRIBUTE_UNSPECIFIED  = 15
};

// ******************************************************************* //
// Static functions
// ******************************************************************* //

static inline std::string
toString(BaseMeshNalUnitType type) {
  switch (type) {
  case BASEMESH_NAL_BMSPS: return std::string("BASEMESH_NAL_BSPS"); break;
  case BASEMESH_NAL_BMFPS: return std::string("BASEMESH_NAL_BFPS"); break;
  case BASEMESH_NAL_AUD: return std::string("BASEMESH_NAL_AUD"); break;
  case BASEMESH_NAL_TRAIL_N: return std::string("BASEMESH_NAL_TRAIL_N"); break;
  case BASEMESH_NAL_TRAIL_R: return std::string("BASEMESH_NAL_TRAIL_R"); break;
  case BASEMESH_NAL_TSA_N: return std::string("BASEMESH_NAL_TSA_N"); break;
  case BASEMESH_NAL_TSA_R: return std::string("BASEMESH_NAL_TSA_R"); break;
  case BASEMESH_NAL_STSA_N: return std::string("BASEMESH_NAL_STSA_N"); break;
  case BASEMESH_NAL_STSA_R: return std::string("BASEMESH_NAL_STSA_R"); break;
  case BASEMESH_NAL_RADL_N: return std::string("BASEMESH_NAL_RADL_N"); break;
  case BASEMESH_NAL_RADL_R: return std::string("BASEMESH_NAL_RADL_R"); break;
  case BASEMESH_NAL_RASL_N: return std::string("BASEMESH_NAL_RASL_N"); break;
  case BASEMESH_NAL_RASL_R: return std::string("BASEMESH_NAL_RASL_R"); break;
  case BASEMESH_NAL_SKIP_N: return std::string("BASEMESH_NAL_SKIP_N"); break;
  case BASEMESH_NAL_SKIP_R: return std::string("BASEMESH_NAL_SKIP_R"); break;
  case BASEMESH_NAL_IDR_N_LP:
    return std::string("BASEMESH_NAL_IDR_N_LP");
    break;
  case BASEMESH_NAL_PREFIX_ESEI:
    return std::string("BASEMESH_NAL_PREFIX_ESEI");
    break;
  case BASEMESH_NAL_PREFIX_NSEI:
    return std::string("BASEMESH_NAL_PREFIX_NSEI");
    break;
  case BASEMESH_NAL_SUFFIX_ESEI:
    return std::string("BASEMESH_NAL_SUFFIX_ESEI");
    break;
  case BASEMESH_NAL_SUFFIX_NSEI:
    return std::string("BASEMESH_NAL_SUFFIX_NSEI");
    break;
  default: return std::string("others"); break;
  }
}

static inline std::string
toString(BaseMeshType type) {
  switch (type) {
  case P_BASEMESH: return std::string("P_BASEMESH"); break;
  case I_BASEMESH: return std::string("I_BASEMESH"); break;
  case SKIP_BASEMESH: return std::string("SKIP_BASEMESH"); break;
  default: return std::string("reserved?"); break;
  }
}

static inline std::string
toString(VMCMeshType type) {
  if (type == VMCMeshType::BASE) return std::string("base mesh ");
  return std::string("not supported");
}

static inline std::string
toString(BaseMeshSeiPayloadType payloadType) {
  switch (payloadType) {
  case BASEMESH_BUFFERING_PERIOD:
    return std::string("BASEMESH_BUFFERING_PERIOD");
    break;
  case BASEMESH_FRAME_TIMING:
    return std::string("BASEMESH_FRAME_TIMING");
    break;
  case BASEMESH_FILLER_PAYLOAD:
    return std::string("BASEMESH_FILLER_PAYLOAD");
    break;
  case BASEMESH_USER_DATAREGISTERED_ITUTT35:
    return std::string("BASEMESH_USER_DATAREGISTERED_ITUTT35");
    break;
  case BASEMESH_USER_DATA_UNREGISTERED:
    return std::string("BASEMESH_USER_DATA_UNREGISTERED");
    break;
  case BASEMESH_RECOVERY_POINT:
    return std::string("BASEMESH_RECOVERY_POINT");
    break;
  case BASEMESH_NO_RECONSTRUCTION:
    return std::string("BASEMESH_NO_RECONSTRUCTION");
    break;
  case BASEMESH_TIME_CODE: return std::string("BASEMESH_TIME_CODE"); break;
  case BASEMESH_SEI_MANIFEST:
    return std::string("BASEMESH_SEI_MANIFEST");
    break;
  case BASEMESH_SEI_PREFIX_INDICATION:
    return std::string("BASEMESH_SEI_PREFIX_INDICATION");
    break;
  case BASEMESH_COMPONENT_CODEC_MAPPING:
    return std::string("BASEMESH_COMPONENT_CODEC_MAPPING");
    break;
  case BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS:
    return std::string("BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS");
    break;
  case BASEMESH_RESERVED_MESSAGE: return std::string("RESERVED_MESSAGE"); break;
  default: return std::string("others"); break;
  }
}
}  // namespace vmesh
