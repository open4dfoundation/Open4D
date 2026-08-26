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
#include <stddef.h>

#define ENFORCE_BYPASS 1
#define ENFORCE_NORESET 1
#define RESET_CONTEXTS_PER_ATTRIBUTE 0

#define MAX_CTX_COEFF_REM_PREFIX_GEO 5
#define MAX_CTX_COEFF_REM_SUFFIX_GEO 5
#define MAX_CTX_COEFF_REM_PREFIX_UV 4
#define MAX_CTX_COEFF_REM_SUFFIX_UV 4
#define MAX_CTX_COEFF_REM_PREFIX_NOR_F 10
#define MAX_CTX_COEFF_REM_PREFIX_NOR_C 1
#define MAX_CTX_COEFF_REM_SUFFIX_NOR 1
#define MAX_CTX_COEFF_REM_PREFIX_NOR_SEC 1
#define MAX_CTX_COEFF_REM_SUFFIX_NOR_SEC 1
#define MAX_CTX_COEFF_REM_PREFIX_GEN 12
#define MAX_CTX_COEFF_REM_SUFFIX_GEN 12
#define MAX_CTX_COEFF_REM_PREFIX_MTL 8
#define MAX_CTX_COEFF_REM_SUFFIX_MTL 8

namespace eb {

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
  using Base::pos;

  using typename Base::AdaptiveBitModel;

  //--------------------------------------------------------------------------
  // :: encoding / common binarisation methods

  void encodeExpGolomb(unsigned int symbol, int k, AdaptiveBitModel& bModel1);

  template<size_t NumPrefixCtx, size_t NumSuffixCtx>
  void encodeExpGolomb(
    unsigned int symbol,
    int k,
    AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel (&ctxSuffix)[NumSuffixCtx]);

  template<size_t NumPrefixCtx, size_t NumSuffixCtx>
  void encodeExpGolomb(
      unsigned int symbol,
      int k,
      AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
      AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
      int maxPrefixIdx,
      int maxSuffixIdx);

  template<size_t NumTruncCtx, size_t NumPrefixCtx, size_t NumSuffixCtx>
  void encodeTUExpGolombS(
      int symbol,
      int maxOffset,
      int k,
      StaticBitModel& ctxSign,
      AdaptiveBitModel(&ctxTrunc)[NumTruncCtx],
      AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
      AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
      int ctxPrefixBins,
      int ctxSuffixBins,
      int maxTruncIdx,
      int maxPrefixIdx,
      int maxSuffixIdx);
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

  template<size_t NumPrefixCtx, size_t NumSuffixCtx>
  unsigned int decodeExpGolomb(
    int k,
    AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel (&ctxSuffix)[NumSuffixCtx]);

  template<size_t NumPrefixCtx, size_t NumSuffixCtx>
  unsigned int decodeExpGolomb(
      int k,
      AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
      AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
      int maxPrefixIdx,
      int maxSuffixIdx);

  template<size_t NumTruncCtx, size_t NumPrefixCtx, size_t NumSuffixCtx>
  int decodeTUExpGolombS(
      int maxOffset,
      int k,
      StaticBitModel& ctxSign,
      AdaptiveBitModel(&ctxTrunc)[NumTruncCtx],
      AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
      AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
      int ctxPrefixBins,
      int ctxSuffixBins,
      int maxTruncIdx,
      int maxPrefixIdx,
      int maxSuffixIdx);

  template<size_t NumPrefixCtx>
  unsigned int decodeExpGolombS(
      int k,
      AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx]);
};

//============================================================================
// :: Various binarisation forms

template<class Base>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(
  unsigned int symbol, int k, AdaptiveBitModel& ctxPrefix)
{
  while (1) {
    if (symbol >= (1u << k)) {
      encode(1, ctxPrefix);
      symbol -= (1u << k);
      k++;
    } else {
      encode(0, ctxPrefix);
      while (k--)
        encode((symbol >> k) & 1);
      break;
    }
  }
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx, size_t NumSuffixCtx>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(
  unsigned int symbol,
  int k,
  AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
  AdaptiveBitModel (&ctxSuffix)[NumSuffixCtx])
{
  constexpr int maxPrefixIdx = NumPrefixCtx - 1;
  constexpr int maxSuffixIdx = NumSuffixCtx - 1;
  const int k0 = k;

  while (symbol >= (1u << k)) {
    encode(1, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
    symbol -= 1u << k;
    k++;
  }
  encode(0, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);

  while (k--)
    encode((symbol >> k) & 1, ctxSuffix[std::min(maxSuffixIdx, k)]);
}


//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx, size_t NumSuffixCtx>
inline void
EntropyEncoderWrapper<Base>::encodeExpGolomb(
    unsigned int symbol,
    int k,
    AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
    int maxPrefixIdx,
    int maxSuffixIdx)
{
    const int k0 = k;

    while (symbol >= (1u << k)) {
        encode(1, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
        symbol -= 1u << k;
        k++;
    }
    encode(0, ctxPrefix[std::min(maxPrefixIdx, k - k0)]);

    while (k--)
        encode((symbol >> k) & 1, ctxSuffix[std::min(maxSuffixIdx, k)]);
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumTruncCtx, size_t NumPrefixCtx, size_t NumSuffixCtx>
inline void
EntropyEncoderWrapper<Base>::encodeTUExpGolombS(
    int symbol,
    int maxOffset,
    int k,
    StaticBitModel& ctxSign,
    AdaptiveBitModel(&ctxTrunc)[NumTruncCtx],
    AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
    int ctxPrefixBins,
    int ctxSuffixBins,
    int maxTruncIdx,
    int maxPrefixIdx,
    int maxSuffixIdx)
{
    unsigned int absVal = std::abs(symbol);

    for (auto BinIdxTu = 0; absVal >= BinIdxTu && BinIdxTu < maxOffset; ++BinIdxTu)
        encode(absVal > BinIdxTu, ctxTrunc[std::min(maxTruncIdx, BinIdxTu)]);

    // requires maxOffset > 0
    if (absVal > 0)
    {
        if (absVal >= maxOffset)
        {
            unsigned int value = absVal - maxOffset;

            int BinIdxPfx;
            for (BinIdxPfx = 0; value >= (1u << (BinIdxPfx + k)); ++BinIdxPfx)
            {
                if (ctxPrefixBins > 0) {
                  encode(1, ctxPrefix[std::min(maxPrefixIdx, BinIdxPfx)]);
                  ctxPrefixBins--;
                } else {
                  encode(1);
                }
                value -= 1u << (BinIdxPfx + k);
            }
            if (ctxPrefixBins > 0) {
              encode(0, ctxPrefix[std::min(maxPrefixIdx, BinIdxPfx)]);
              ctxPrefixBins--;
            } else {
              encode(0);
            }

            for (auto BinIdxSfx = 0; BinIdxSfx < BinIdxPfx + k; ++BinIdxSfx) // aligned with WD
              if (ctxSuffixBins > 0) {
                encode(value & (1u << BinIdxSfx), ctxSuffix[std::min(maxSuffixIdx, BinIdxSfx)]); 							// m66151
                // encode(value & (1u << (BinIdxPfx + k -1 - BinIdxSfx)), ctxSuffix[std::min(maxSuffixIdx, BinIdxSfx)]); 	// WD align
                ctxSuffixBins--;
              } else {
                encode(value & (1u << BinIdxSfx));
              }
        }
        encode(symbol < 0, ctxSign);
    }
}

//----------------------------------------------------------------------------

template<class Base>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(
  int k, AdaptiveBitModel& ctxPrefix)
{
  unsigned int l;
  int symbol = 0;
  int binary_symbol = 0;
  do {
    l = decode(ctxPrefix);
    if (l == 1) {
      symbol += (1 << k);
      k++;
    }
  } while (l != 0);
  while (k--)  //next binary part
    if (decode() == 1) {
      binary_symbol |= (1 << k);
    }
  return static_cast<unsigned int>(symbol + binary_symbol);
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx, size_t NumSuffixCtx>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(
  int k,
  AdaptiveBitModel (&ctxPrefix)[NumPrefixCtx],
  AdaptiveBitModel (&ctxSuffix)[NumSuffixCtx])
{
  constexpr int maxPrefixIdx = NumPrefixCtx - 1;
  constexpr int maxSuffixIdx = NumSuffixCtx - 1;
  const int k0 = k;
  unsigned int l;
  int symbol = 0;
  int binary_symbol = 0;

  do {
    l = decode(ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
    if (l == 1) {
      symbol += (1 << k);
      k++;
    }
  } while (l != 0);

  while (k--)
    binary_symbol |= decode(ctxSuffix[std::min(maxSuffixIdx, k)]) << k;

  return static_cast<unsigned int>(symbol + binary_symbol);
}

//----------------------------------------------------------------------------

template<class Base>
template<size_t NumPrefixCtx, size_t NumSuffixCtx>
inline unsigned int
EntropyDecoderWrapper<Base>::decodeExpGolomb(
    int k,
    AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
    int maxPrefixIdx,
    int maxSuffixIdx)
{
    const int k0 = k;
    unsigned int l;
    int symbol = 0;
    int binary_symbol = 0;

    do {
        l = decode(ctxPrefix[std::min(maxPrefixIdx, k - k0)]);
        if (l == 1) {
            symbol += (1 << k);
            k++;
        }
    } while (l != 0);

    while (k--)
        binary_symbol |= decode(ctxSuffix[std::min(maxSuffixIdx, k)]) << k;

    return static_cast<unsigned int>(symbol + binary_symbol);
}


//----------------------------------------------------------------------------

template<class Base>
template<size_t NumTruncCtx, size_t NumPrefixCtx, size_t NumSuffixCtx>
inline int
EntropyDecoderWrapper<Base>::decodeTUExpGolombS(
    int maxOffset,
    int k,
    StaticBitModel& ctxSign,
    AdaptiveBitModel(&ctxTrunc)[NumTruncCtx],
    AdaptiveBitModel(&ctxPrefix)[NumPrefixCtx],
    AdaptiveBitModel(&ctxSuffix)[NumSuffixCtx],
    int ctxPrefixBins,
    int ctxSuffixBins,
    int maxTruncIdx,
    int maxPrefixIdx,
    int maxSuffixIdx)
{
    int offset = 0;
    int prefix = 0;
    int suffix = 0;
    int val = 0;
    for (auto BinIdxTu = 0; offset < maxOffset && decode(ctxTrunc[std::min(maxTruncIdx, BinIdxTu)]) != 0; ++BinIdxTu)
        offset++;
    // requires maxOffset > 0
    if (offset > 0)
    {
        if (offset == maxOffset)
        {
            for (auto BinIdxPfx = 0; (ctxPrefixBins > 0 ? decode(ctxPrefix[std::min(maxPrefixIdx, BinIdxPfx)]) : decode()) != 0; ++BinIdxPfx, ctxPrefixBins--)
                prefix++;
            
            for (auto BinIdxSfx =  0; BinIdxSfx < k + prefix; ++BinIdxSfx) // bin order aligned with WD
              if (ctxSuffixBins > 0) {
                suffix |= decode(ctxSuffix[std::min(maxSuffixIdx, BinIdxSfx)]) << BinIdxSfx; 	// m66151
                //suffix = (suffix<<1) | decode(ctxSuffix[std::min(maxSuffixIdx, BinIdxSfx)]);	// WD align
                ctxSuffixBins--;
              } else {
                suffix |= decode() << BinIdxSfx;
              }
        }
        const bool sign = decode(ctxSign);
        const int absVal = offset + (((1 << prefix) - 1) << k) + suffix;
        val = sign ? -absVal : absVal;
    }
    return val;
}


//============================================================================

}  
