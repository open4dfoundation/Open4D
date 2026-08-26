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

#include <algorithm>
#include <cstddef>

namespace vmesh {

//============================================================================
// :: Entropy codec interface (Encoder)
//
// The base class must implement the following methods:
//  - void setBuffer(size_t size, const uint8_t *buffer);
//  - void start();
//  - size_t stop();
//  - void encode(int symbol, StaticBitModel&);
//  - void encode(int symbol, AdaptiveBitModel&);

template<class Base>
class EntropyEncoderWrapper : protected Base {
public:
  using Base::Base;
  using Base::buffer;
  using Base::encode;
  using Base::setBuffer;
  using Base::start;
  using Base::stop;

  using typename Base::AdaptiveBitModel;

  //--------------------------------------------------------------------------
  // :: encoding / common binarisation methods

  void encodeExpGolomb(unsigned int symbol, int k, AdaptiveBitModel& bModel1);

  template<size_t NumPrefixCtx>
  void encodeExpGolomb(unsigned int symbol,
                       int          k,
                       AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
                       int b);

  template<size_t NumPrefixCtx>
  void encodeExpGolomb(unsigned int symbol,
                       int          k,
                       StaticBitModel (&ctxPrefix)[NumPrefixCtx]);
};

//============================================================================
// :: Entropy codec interface (Decoder)
//
// The base class must implement the following methods:
//  - void setBuffer(size_t size, const uint8_t *buffer);
//  - void start();
//  - void stop();
//  - int decode(StaticBitModel&);
//  - int decode(AdaptiveBitModel&);

template<class Base>
class EntropyDecoderWrapper : protected Base {
public:
  EntropyDecoderWrapper() : Base() {}

  using Base::decode;
  using Base::setBuffer;
  using Base::start;
  using Base::stop;

  using typename Base::AdaptiveBitModel;

  //--------------------------------------------------------------------------
  // :: encoding / common binarisation methods

  unsigned int decodeExpGolomb(int k, AdaptiveBitModel& bModel1);

  template<size_t NumPrefixCtx>
  unsigned int
  decodeExpGolomb(int k, AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx], int b);
  template<size_t NumPrefixCtx>
  unsigned int decodeExpGolomb(int k,
                               StaticBitModel (&ctxPrefix)[NumPrefixCtx]);
};

//============================================================================
// :: Various binarisation forms

template<class Base>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(unsigned int      symbol,
                                             int               k,
                                             AdaptiveBitModel& ctxPrefix) {
  while (1) {
    if (symbol >= (1U << k)) {
      encode(1, ctxPrefix);
      symbol -= (1U << k);
      k++;
    } else {
      encode(0, ctxPrefix);
      while ((k--) != 0) { encode((symbol >> k) & 1); }
      break;
    }
  }
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(
  unsigned int symbol,
  int          k,
  AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
  int b) {
  constexpr int maxPrefixIdx = NumPrefixCtx - 1;
  const int     k0           = k;

  while (symbol >= (1U << k)) {
    if (b > 0) {
      encode(1, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
      b--;
    } else {
      encode(1);
    }
    symbol -= 1U << k;
    k++;
  }
  if (b > 0) {
    encode(0, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
    b--;
  } else {
    encode(0);
  }

  while (k--) { encode((symbol >> k) & 1); }
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(
  unsigned int symbol,
  int          k,
  StaticBitModel (&ctxPrefix)[NumPrefixCtx]) {
  constexpr int maxPrefixIdx = NumPrefixCtx - 1;
  const int     k0           = k;

  while (symbol >= (1U << k)) {
    encode(1, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
    symbol -= 1U << k;
    k++;
  }
  encode(0, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);

  while (k--) { encode((symbol >> k) & 1); }
}

//----------------------------------------------------------------------------

template<class Base>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(int               k,
                                             AdaptiveBitModel& ctxPrefix) {
  unsigned int l             = 0;
  int          symbol        = 0;
  int          binary_symbol = 0;
  do {
    l = decode(ctxPrefix);
    if (l == 1) {
      symbol += (1 << k);
      k++;
    }
  } while (l != 0);
  while ((k--) != 0) {  //next binary part
    if (decode() == 1) { binary_symbol |= (1 << k); }
  }
  return static_cast<unsigned int>(symbol + binary_symbol);
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(
  int k,
  AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
  int b) {
  constexpr int maxPrefixIdx  = NumPrefixCtx - 1;
  const int     k0            = k;
  unsigned int  l             = 0;
  int           symbol        = 0;
  int           binary_symbol = 0;

  do {
    if (b > 0) {
      l = decode(ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
      b--;
    } else {
      l = decode();
    }
    if (l == 1) {
      symbol += (1 << k);
      k++;
    }
  } while (l != 0);

  while (k--) { binary_symbol |= decode() << k; }

  return static_cast<unsigned int>(symbol + binary_symbol);
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(
  int k,
  StaticBitModel (&ctxPrefix)[NumPrefixCtx]) {
  constexpr int maxPrefixIdx  = NumPrefixCtx - 1;
  const int     k0            = k;
  unsigned int  l             = 0;
  int           symbol        = 0;
  int           binary_symbol = 0;

  do {
    l = decode(ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
    if (l == 1) {
      symbol += (1 << k);
      k++;
    }
  } while (l != 0);

  while (k--) { binary_symbol |= decode() << k; }

  return static_cast<unsigned int>(symbol + binary_symbol);
}

//============================================================================

}  // namespace vmesh
