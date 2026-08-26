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

#include <string>

using namespace vmesh;

#if defined(BITSTREAM_TRACE)
static std::string
format(const std::string& var) {
  std::string substr = var;
  auto        npos   = substr.find("get");
  if (npos != std::string::npos) substr = substr.substr(npos + 3);
  npos = substr.find("(");
  if (npos != std::string::npos) substr = substr.substr(0, npos);
  return substr;
}

#  define WRITE_CODE_40bits(VAR, LEN) \
    { \
      bitstream.write40bits(VAR, LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define WRITE_CODE(VAR, LEN) \
    { \
      bitstream.write(VAR, LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define WRITE_CODES(VAR, LEN) \
    { \
      bitstream.writeS(VAR, LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "s(" + std::to_string(LEN) + ")"); \
    }

#  define WRITE_UVLC(VAR) \
    { \
      bitstream.writeUvlc(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }

#  define WRITE_VU(VAR) \
    { \
      bitstream.writeVu(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "vu(v)"); \
    }

#  define WRITE_SVLC(VAR) \
    { \
      bitstream.writeSvlc(VAR); \
      bitstream.traceBit(format(#VAR), VAR, "se(v)"); \
    }

#  define WRITE_FLOAT(VAR) \
    { \
      bitstream.writeFloat(VAR); \
      bitstream.traceBitFloat(format(#VAR), VAR, "f(32)"); \
    }
#  define WRITE_DOUBLE(VAR) \
    { \
      bitstream.writeDouble(VAR); \
      bitstream.traceBitDouble(format(#VAR), VAR, "f(64)"); \
    }
#  define WRITE_STRING(VAR) \
    { \
      bitstream.writeString(VAR); \
      bitstream.traceBitStr(format(#VAR), VAR, "f(32)"); \
    }

#  define WRITE_VECTOR(VAR) \
    { \
      bitstream.write(VAR); \
      bitstream.trace( \
        "Data stream: %s size = %zu \n", format(#VAR).c_str(), VAR.size()); \
    }

#  define WRITE_VIDEO(VAR) \
    { \
      bitstream.write(VAR); \
      bitstream.trace("Video stream: type = %s size = %zu \n", \
                      toString(VAR.type()).c_str(), \
                      VAR.size()); \
    }
#else
#  define WRITE_CODE(VAR, LEN) bitstream.write(VAR, LEN);
#  define WRITE_CODES(VAR, LEN) bitstream.writeS(VAR, LEN);
#  define WRITE_UVLC(VAR) bitstream.writeUvlc(VAR);
#  define WRITE_SVLC(VAR) bitstream.writeSvlc(VAR);
#  define WRITE_FLOAT(VAR) bitstream.writeFloat(VAR);
#  define WRITE_DOUBLE(VAR) bitstream.writeDouble(VAR);
#  define WRITE_STRING(VAR) bitstream.writeString(VAR);
#  define WRITE_VECTOR(VAR) bitstream.write(VAR);
#  define WRITE_VIDEO(VAR) bitstream.write(VAR);
#  define WRITE_VU(VAR) bitstream.writeVu(VAR);
#  define WRITE_CODE_40bits(VAR, LEN) bitstream.write40bits(VAR, LEN);
#endif
