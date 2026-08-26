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

namespace acdisplacement {

// ******************************************************************* //
// Profile Tier Level
// ******************************************************************* //
enum DisplProfileIdc {
  DISPLACEMENT_PROFILE_AC_DISPLACEMENT_MAIN = 0,
  DISPLACEMENT_PROFILE_RESERVED = 1
};

// *_level_idc shall be set equal to a value of 30 times the level number
enum DisplLevelIdc {
  DISPLACEMENT_LEVEL_1_0 = 30,
  DISPLACEMENT_LEVEL_2_0 = 60,
  DISPLACEMENT_LEVEL_2_1 = 63,
  DISPLACEMENT_LEVEL_2_2 = 66

};

enum DisplTierFlag {
  DISPLACEMENT_TIER_0 = 0,
  DISPLACEMENT_TIER_1 = 1
};

// ******************************************************************* //
// Common constants
// ******************************************************************* //

enum DisplNalUnitType {
  DISPLACEMENT_NAL_TRAIL_N,
  DISPLACEMENT_NAL_TRAIL_R,
  DISPLACEMENT_NAL_TSA_N,
  DISPLACEMENT_NAL_TSA_R,
  DISPLACEMENT_NAL_STSA_N,
  DISPLACEMENT_NAL_STSA_R,
  DISPLACEMENT_NAL_RADL_N,
  DISPLACEMENT_NAL_RADL_R,
  DISPLACEMENT_NAL_RASL_N,
  DISPLACEMENT_NAL_RASL_R,
  DISPLACEMENT_NAL_SKIP_N,
  DISPLACEMENT_NAL_SKIP_R,
  DISPLACEMENT_NAL_RSV_BMCL_N12,
  DISPLACEMENT_NAL_RSV_BMCL_N14,
  DISPLACEMENT_NAL_RSV_BMCL_R13,
  DISPLACEMENT_NAL_RSV_BMCL_R15,
  DISPLACEMENT_NAL_BLA_W_LP,
  DISPLACEMENT_NAL_BLA_W_RADL,
  DISPLACEMENT_NAL_BLA_N_LP,
  DISPLACEMENT_NAL_IDR_W_RADL,
  DISPLACEMENT_NAL_IDR_N_LP,
  DISPLACEMENT_NAL_CRA,
  DISPLACEMENT_NAL_RSV_IRAP_BMCL_22,
  DISPLACEMENT_NAL_RSV_IRAP_BMCL_23,
  DISPLACEMENT_NAL_RSV_BMCL_24,
  DISPLACEMENT_NAL_RSV_BMCL_25,
  DISPLACEMENT_NAL_RSV_BMCL_26,
  DISPLACEMENT_NAL_RSV_BMCL_27,
  DISPLACEMENT_NAL_RSV_BMCL_28,
  DISPLACEMENT_NAL_RSV_BMCL_29,
  DISPLACEMENT_NAL_BMSPS,  //30
  DISPLACEMENT_NAL_BMFPS,  //31
  DISPLACEMENT_NAL_AUD,
  DISPLACEMENT_NAL_EOS,
  DISPLACEMENT_NAL_EOB,
  DISPLACEMENT_NAL_FD,
  DISPLACEMENT_NAL_PREFIX_NSEI,
  DISPLACEMENT_NAL_SUFFIX_NSEI,
  DISPLACEMENT_NAL_PREFIX_ESEI,
  DISPLACEMENT_NAL_SUFFIX_ESEI,
  DISPLACEMENT_NAL_RSV_NBMCL_40,
  DISPLACEMENT_NAL_RSV_NBMCL_44,
  DISPLACEMENT_NAL_UNSPEC_45,
  DISPLACEMENT_NAL_UNSPEC_63,
};

enum DisplacementType {
  P_DISPL = 0,    // 0: Inter
  I_DISPL,        // 1: Intra
  RESERVED_DISPL  // 3: 3 to N (N not defined?)
};

// ******************************************************************* //
// Static functions
// ******************************************************************* //

static inline std::string
toString(DisplNalUnitType type) {
  switch (type) {
  case DISPLACEMENT_NAL_BMSPS:
    return std::string("DISPLACEMENT_NAL_BSPS");
    break;
  case DISPLACEMENT_NAL_BMFPS:
    return std::string("DISPLACEMENT_NAL_BFPS");
    break;
  case DISPLACEMENT_NAL_AUD: return std::string("DISPLACEMENT_NAL_AUD"); break;
  case DISPLACEMENT_NAL_TRAIL_N:
    return std::string("DISPLACEMENT_NAL_TRAIL_N");
    break;
  case DISPLACEMENT_NAL_TRAIL_R:
    return std::string("DISPLACEMENT_NAL_TRAIL_R");
    break;
  case DISPLACEMENT_NAL_TSA_N:
    return std::string("DISPLACEMENT_NAL_TSA_N");
    break;
  case DISPLACEMENT_NAL_TSA_R:
    return std::string("DISPLACEMENT_NAL_TSA_R");
    break;
  case DISPLACEMENT_NAL_STSA_N:
    return std::string("DISPLACEMENT_NAL_STSA_N");
    break;
  case DISPLACEMENT_NAL_STSA_R:
    return std::string("DISPLACEMENT_NAL_STSA_R");
    break;
  case DISPLACEMENT_NAL_RADL_N:
    return std::string("DISPLACEMENT_NAL_RADL_N");
    break;
  case DISPLACEMENT_NAL_RADL_R:
    return std::string("DISPLACEMENT_NAL_RADL_R");
    break;
  case DISPLACEMENT_NAL_RASL_N:
    return std::string("DISPLACEMENT_NAL_RASL_N");
    break;
  case DISPLACEMENT_NAL_RASL_R:
    return std::string("DISPLACEMENT_NAL_RASL_R");
    break;
  case DISPLACEMENT_NAL_SKIP_N:
    return std::string("DISPLACEMENT_NAL_SKIP_N");
    break;
  case DISPLACEMENT_NAL_SKIP_R:
    return std::string("DISPLACEMENT_NAL_SKIP_R");
    break;
  case DISPLACEMENT_NAL_IDR_N_LP:
    return std::string("DISPLACEMENT_NAL_IDR_N_LP");
    break;
  case DISPLACEMENT_NAL_PREFIX_ESEI:
    return std::string("DISPLACEMENT_NAL_PREFIX_ESEI");
    break;
  case DISPLACEMENT_NAL_PREFIX_NSEI:
    return std::string("DISPLACEMENT_NAL_PREFIX_NSEI");
    break;
  case DISPLACEMENT_NAL_SUFFIX_ESEI:
    return std::string("DISPLACEMENT_NAL_SUFFIX_ESEI");
    break;
  case DISPLACEMENT_NAL_SUFFIX_NSEI:
    return std::string("DISPLACEMENT_NAL_SUFFIX_NSEI");
    break;
  default: return std::string("others"); break;
  }
}

static inline std::string
toString(DisplacementType type) {
  switch (type) {
  case P_DISPL: return std::string("P_DISPL"); break;
  case I_DISPL: return std::string("I_DISPL"); break;
  default: return std::string("reserved?"); break;
  }
}

}  // namespace vmesh
