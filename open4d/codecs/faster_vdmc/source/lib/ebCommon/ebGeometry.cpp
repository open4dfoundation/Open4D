/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
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

//
#include <vector>
#include "glm/glm.hpp"
//
#include "ebGeometry.h"

using namespace eb;

// Compute barycentric coordinates (u, v, w)~res(x,y,z) for
// point p with respect to triangle (a, b, c)
// return true if point is inside the triangle
bool Geometry::getBarycentric( glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3& res ) {
  glm::vec2 v0 = b - a, v1 = c - a, v2 = p - a;
  float     den = v0.x * v1.y - v1.x * v0.y;
  float     u   = ( v2.x * v1.y - v1.x * v2.y ) / den;
  float     v   = ( v0.x * v2.y - v2.x * v0.y ) / den;
  float     w   = 1.0f - u - v;
  res.x         = u;
  res.y         = v;
  res.z         = w;
  if ( 0 <= u && u <= 1 && 0 <= v && v <= 1 && u + v <= 1 ) return true;
  else return false;
}
// return true if there is an intersection
// and res will contain t (the parameter on the ray to intersection)
// and u,v the barycentric of the intersection on the triangle surface
bool Geometry::evalRayTriangle( const glm::vec3& rayOrigin,
                                const glm::vec3& rayDirection,
                                const glm::vec3& v0,
                                const glm::vec3& v1,
                                const glm::vec3& v2,
                                glm::vec3&       res,
                                float            epsilon ) {
  glm::vec3 tvec, pvec, qvec;
  float     det, inv_det, t, u, v;

  /* find vectors for two edges sharing vert0 */
  glm::vec3 edge1( v1 - v0 );
  glm::vec3 edge2( v2 - v0 );

  /* begin calculating determinant - also used to calculate U parameter */
  pvec = glm::cross( rayDirection, edge2 );

  /* if determinant is near zero, ray lies in plane of triangle */
  det = glm::dot( edge1, pvec );

  /* the non-culling branch */
  if ( det > -epsilon && det < epsilon ) return false;

  inv_det = 1.0f / det;

  /* calculate distance from vert0 to ray origin */
  tvec = rayOrigin - v0;

  /* calculate U parameter and test bounds */
  u = glm::dot( tvec, pvec ) * inv_det;

  if ( u < 0.0f || u > 1.0f ) return false;

  /* prepare to test V parameter */
  qvec = glm::cross( tvec, edge1 );

  /* calculate V parameter and test bounds */
  v = glm::dot( rayDirection, qvec ) * inv_det;

  if ( v < 0.0f || u + v > 1.0f ) return false;

  /* calculate t, ray intersects triangle */
  t = glm::dot( edge2, qvec ) * inv_det;

  // push the result
  res.x = t;
  res.y = u;
  res.z = v;

  return true;
}
