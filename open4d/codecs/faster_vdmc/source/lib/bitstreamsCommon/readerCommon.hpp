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

#  define READ_CODE_40bits(VAR, LEN) \
    { \
      VAR = bitstream.read40bits(LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define READ_CODE(VAR, LEN) \
    { \
      VAR = bitstream.read(LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }
#  define READ_CODE_DESC(VAR, LEN, DESC) \
    { \
      VAR = bitstream.read(LEN); \
      bitstream.traceBit( \
        format(DESC), VAR, "u(" + std::to_string(LEN) + ")"); \
    }
#  define READ_UVLC_DESC(VAR, DESC) \
    { \
      VAR = bitstream.readUvlc(); \
      bitstream.traceBit(format(DESC), VAR, "ue(v)"); \
    }
#  define READ_VU(VAR) \
    { \
      VAR = bitstream.readVu(); \
      bitstream.traceBit(format(#VAR), VAR, "vu(v)"); \
    }
#  define READ_SVLC_DESC(VAR, DESC) \
    { \
      VAR = bitstream.readSvlc(); \
      bitstream.traceBit(format(DESC), VAR, "se(v)"); \
    }
#  define READ_CODE_CAST(VAR, LEN, CAST) \
    { \
      VAR = static_cast<CAST>(bitstream.read(LEN)); \
      bitstream.traceBit( \
        format(#VAR), VAR, "u(" + std::to_string(LEN) + ")"); \
    }

#  define READ_CODES(VAR, LEN) \
    { \
      VAR = bitstream.readS(LEN); \
      bitstream.traceBit( \
        format(#VAR), VAR, "s(" + std::to_string(LEN) + ")"); \
    }

#  define READ_UVLC(VAR) \
    { \
      VAR = bitstream.readUvlc(); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }
#  define READ_UVLC_CAST(VAR, CAST) \
    { \
      VAR = static_cast<CAST>(bitstream.readUvlc()); \
      bitstream.traceBit(format(#VAR), VAR, "ue(v)"); \
    }

#  define READ_SVLC(VAR) \
    { \
      VAR = bitstream.readSvlc(); \
      bitstream.traceBit(format(#VAR), VAR, "se(v)"); \
    }

#  define READ_FLOAT(VAR) \
    { \
      VAR = bitstream.readFloat(); \
      bitstream.traceBitFloat(format(#VAR), VAR, "f(32)"); \
    }

#  define READ_DOUBLE(VAR) \
    { \
      VAR = bitstream.readDouble(); \
      bitstream.traceBitDouble(format(#VAR), VAR, "f(64)"); \
    }

#  define READ_STRING(VAR) \
    { \
      VAR = bitstream.readString(); \
      bitstream.traceBitStr(format(#VAR), VAR, "str()"); \
    }

#  define READ_VECTOR(VAR, SIZE) \
    { \
      bitstream.read(VAR, SIZE); \
      bitstream.trace( \
        "Data stream: %s size = %zu \n", format(#VAR).c_str(), VAR.size()); \
    }
#  define READ_VIDEO(VAR, SIZE) \
    { \
      bitstream.read(VAR, SIZE); \
      bitstream.trace("Video stream: type = %s size = %zu \n", \
                      toString(VAR.type()).c_str(), \
                      SIZE); \
    }
#else
#  define READ_CODE_DESC(VAR, LEN, DESC) VAR = bitstream.read(LEN);
#  define READ_UVLC_DESC(VAR, DESC) VAR = bitstream.readUvlc();
#  define READ_SVLC_DESC(VAR, DESC) VAR = bitstream.readSvlc();
#  define READ_CODE(VAR, LEN) VAR = bitstream.read(LEN);
#  define READ_CODE_CAST(VAR, LEN, CAST) \
    VAR = static_cast<CAST>(bitstream.read(LEN));
#  define READ_CODES(VAR, LEN) VAR = bitstream.readS(LEN);
#  define READ_UVLC(VAR) VAR = bitstream.readUvlc();
#  define READ_UVLC_CAST(VAR, CAST) \
    VAR = static_cast<CAST>(bitstream.readUvlc());
#  define READ_SVLC(VAR) VAR = bitstream.readSvlc();
#  define READ_FLOAT(VAR) VAR = bitstream.readFloat();
#  define READ_DOUBLE(VAR) VAR = bitstream.readDouble();
#  define READ_STRING(VAR) VAR = bitstream.readString();
#  define READ_VECTOR(VAR, SIZE) bitstream.read(VAR, SIZE);
#  define READ_VIDEO(VAR, SIZE) bitstream.read(VAR, SIZE);
#  define READ_VU(VAR) VAR = bitstream.readVu();
#  define READ_CODE_40bits(VAR, LEN) VAR = bitstream.read40bits(LEN);
#endif
