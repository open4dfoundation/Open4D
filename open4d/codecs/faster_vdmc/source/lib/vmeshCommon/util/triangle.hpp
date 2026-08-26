/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
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

#include <cstdint>

#include "util/vector.hpp"

namespace vmesh {

//============================================================================

using Triangle     = Vec3<int32_t>;
using HashTriangle = HashVector3<int32_t>;

//============================================================================

template<typename T>
Vec3<T>
ClosestPointInTriangle(const Vec3<T>  p,
                       const Vec3<T>& a,
                       const Vec3<T>& b,
                       const Vec3<T>& c,
                       Vec3<T>*       barycentricCoords = nullptr) {
  const auto ab     = b - a;
  const auto ac     = c - a;
  const auto bc     = c - b;
  const auto ap     = p - a;
  const auto bp     = p - b;
  const auto cp     = p - c;
  const auto snom   = ap * ab;
  const auto sdenom = -(bp * ab);
  const auto tnom   = ap * ac;
  const auto tdenom = -(cp * ac);
  const auto eps    = T(1.0e-10);
  const auto zero   = T(0);
  const auto one    = T(1);

  if (snom <= eps && tnom <= eps) {
    if (barycentricCoords) {
      barycentricCoords->x() = one;
      barycentricCoords->y() = zero;
      barycentricCoords->z() = zero;
    }
    return a;
  }

  const auto unom   = bp * bc;
  const auto udenom = -(cp * bc);

  if (sdenom <= eps && unom <= eps) {
    if (barycentricCoords) {
      barycentricCoords->x() = zero;
      barycentricCoords->y() = one;
      barycentricCoords->z() = zero;
    }
    return b;
  }

  if (tdenom <= eps && udenom <= eps) {
    if (barycentricCoords) {
      barycentricCoords->x() = zero;
      barycentricCoords->y() = zero;
      barycentricCoords->z() = one;
    }
    return c;
  }

  const auto n  = ab ^ ac;
  const auto vc = n * (ap ^ bp);

  if (vc <= zero && snom >= eps && sdenom >= eps) {
    const auto s = snom / (snom + sdenom);
    if (barycentricCoords) {
      barycentricCoords->x() = one - s;
      barycentricCoords->y() = s;
      barycentricCoords->z() = zero;
    }

    return a + s * ab;
  }

  const auto va = n * (bp ^ cp);
  if (va <= zero && unom >= eps && udenom >= eps) {
    const auto s = unom / (unom + udenom);
    if (barycentricCoords) {
      barycentricCoords->x() = zero;
      barycentricCoords->y() = one - s;
      barycentricCoords->z() = s;
    }
    return b + s * bc;
  }

  const auto vb = n * (cp ^ ap);
  if (vb <= zero && tnom >= eps && tdenom >= eps) {
    const auto s = tnom / (tnom + tdenom);
    if (barycentricCoords) {
      barycentricCoords->x() = one - s;
      barycentricCoords->y() = zero;
      barycentricCoords->z() = s;
    }
    return a + s * ac;
  }

  const auto sum = va + vb + vc;
  const auto u   = va / sum;
  const auto v   = vb / sum;
  const auto w   = vc / sum;
  if (barycentricCoords) {
    barycentricCoords->x() = u;
    barycentricCoords->y() = v;
    barycentricCoords->z() = w;
  }
  return u * a + v * b + w * c;
}

//----------------------------------------------------------------------------

template<class T>
Vec3<T>
computeTriangleNormal(const Vec3<T>& p0,
                      const Vec3<T>& p1,
                      const Vec3<T>& p2,
                      const bool     normalize) {
  auto normal = (p1 - p0) ^ (p2 - p0);
  if (normalize) { normal.normalize(); }
  return normal;
}

//----------------------------------------------------------------------------

template<class T>
T
computeTriangleArea(const Vec3<T>& p0, const Vec3<T>& p1, const Vec3<T>& p2) {
  return T(0.5) * computeTriangleNormal(p0, p1, p2, false).norm();
}

//----------------------------------------------------------------------------

inline bool
isDegenerate(const Triangle& tri) {
  return tri[0] == tri[1] || tri[0] == tri[2] || tri[1] == tri[2];
}

//============================================================================

}  // namespace vmesh
