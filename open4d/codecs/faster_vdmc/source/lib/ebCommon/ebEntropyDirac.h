/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <schroedinger/schroarith.h>

#include <algorithm>
#include <assert.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace eb {
namespace dirac {

  //==========================================================================

  struct SchroContext {
    uint16_t probability = 0x8000;  // p=0.5

    template<class... Args>
    void reset(Args...)
    {
      probability = 0x8000;
    }
    int32_t estimateBits(const bool bit) const
    {  // returns 1024 * number of bits
      // 1024 * -log2(x/64)
      const int32_t LUT[17] = {6144, 4096, 3072, 2473, 2048, 1718,
                               1449, 1221, 1024, 850,  694,  554,
                               425,  307,  197,  95,   0};
      return LUT
        [((bit ? (1 << 16) - int32_t(probability) : probability) + 2048)
         >> 12];
    }
  };

  //--------------------------------------------------------------------------
  // The approximate (7 bit) probability of a symbol being 1 or 0 according
  // to a context model.

  inline int approxSymbolProbability(int bit, SchroContext& model)
  {
    int p = std::max(1, model.probability >> 9);
    return bit ? 128 - p : p;
  }

  //==========================================================================

  struct SchroContextFixed {
    //uint16_t probability = 0x8000; // p=0.5
  };

  //==========================================================================

  class ArithmeticEncoder {
  public:
    using AdaptiveBitModel = SchroContext;

    ArithmeticEncoder() = default;
    ArithmeticEncoder(size_t bufferSize, std::nullptr_t)
    {
      setBuffer(bufferSize, nullptr);
    }

    //------------------------------------------------------------------------

    void setBuffer(size_t size, uint8_t* buffer);

    //------------------------------------------------------------------------

    void start() { schro_arith_encode_init(&impl, &writeByteCallback, this); }

    //------------------------------------------------------------------------

    size_t stop()
    {
      schro_arith_flush(&impl);
      return _bufWr - _buf;
    }

    //------------------------------------------------------------------------

    size_t pos()
    {
        return _bufWr - _buf;
    }

    //------------------------------------------------------------------------

    const char* buffer() { return reinterpret_cast<char*>(_buf); }

    //------------------------------------------------------------------------

    void encode(int bit, SchroContextFixed&) { encode(bit); }

    //------------------------------------------------------------------------

    void encode(int bit)
    {
      uint16_t probability = 0x8000;  // p=0.5
      schro_arith_encode_bit(&impl, &probability, bit);
    }

    //------------------------------------------------------------------------

    void encode(int bit, SchroContext& model)
    {
      schro_arith_encode_bit(&impl, &model.probability, bit);
    }

    //------------------------------------------------------------------------
  private:
    static void writeByteCallback(uint8_t byte, void* thisptr);

    //------------------------------------------------------------------------

  private:
    ::SchroArith impl;
    uint8_t* _buf;
    uint8_t* _bufWr;
    size_t _bufSize;
    std::unique_ptr<uint8_t[]> allocatedBuffer;
  };

  //==========================================================================

  class ArithmeticDecoder {
  public:
    using AdaptiveBitModel = SchroContext;

    void setBuffer(size_t size, const char* buffer)
    {
      _buffer = reinterpret_cast<const uint8_t*>(buffer);
      _bufferLen = size;
    }

    //------------------------------------------------------------------------

    void start() { schro_arith_decode_init(&impl, &readByteCallback, this); }

    //------------------------------------------------------------------------

    void stop() { schro_arith_decode_flush(&impl); }

    //------------------------------------------------------------------------

    int decode(SchroContextFixed&) { return decode(); }

    //------------------------------------------------------------------------

    int decode()
    {
      uint16_t probability = 0x8000;  // p=0.5
      return schro_arith_decode_bit(&impl, &probability);
    }

    //------------------------------------------------------------------------

    int decode(SchroContext& model)
    {
      return schro_arith_decode_bit(&impl, &model.probability);
    }

    //------------------------------------------------------------------------
  private:
    static uint8_t readByteCallback(void* thisptr);

  private:
    ::SchroArith impl;

    // the user supplied buffer.
    const uint8_t* _buffer;

    // the length of the user supplied buffer
    size_t _bufferLen;
  };

  //==========================================================================

}  // namespace dirac

using StaticBitModel = dirac::SchroContextFixed;
using AdaptiveBitModel = dirac::SchroContext;

//============================================================================

}  