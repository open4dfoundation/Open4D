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

#ifndef _EB_GEOMETRY_H_
#define _EB_GEOMETRY_H_

//
#include <vector>
#include <algorithm>
#include "glm/glm.hpp"

namespace eb {

class Geometry {
 public:
  Geometry(){};

// minimum precision parameter do not change
#define ZERO_TOLERANCE 1e-06F

  // computes the position of a point from triangle (v0,v1,v2) and barycentric coordinates (u,v)
  static inline void triangleInterpolation( const glm::vec3& v0,
                                            const glm::vec3& v1,
                                            const glm::vec3& v2,
                                            float            u,
                                            float            v,
                                            glm::vec3&       p ) {
    p = v0 * ( 1.0f - u - v ) + v1 * u + v2 * v;
  }

  // computes the position of a point from 2D triangle (v0,v1,v2) and barycentric coordinates (u,v)
  static inline void triangleInterpolation( const glm::vec2& v0,
                                            const glm::vec2& v1,
                                            const glm::vec2& v2,
                                            float            u,
                                            float            v,
                                            glm::vec2&       p ) {
    p = v0 * ( 1.0f - u - v ) + v1 * u + v2 * v;
  }

  // computes the normal to the triangle
  static inline void triangleNormal( const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3, glm::vec3& n ) {
    n = glm::normalize( glm::cross( v2 - v1, v3 - v1 ) );
  }

  // Three dimentional triangle area
  static inline double triangleArea( const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3 ) {
    return 0.5 * glm::length( glm::cross( v2 - v1, v3 - v1 ) );
  }

  // Three dimentional triangle area
  static inline double triangleAreaXZ( const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3 ) {
    return triangleArea( v1 * glm::vec3( 1, 0, 1 ),  //
                         v2 * glm::vec3( 1, 0, 1 ),  //
                         v3 * glm::vec3( 1, 0, 1 ) );
  }

  static inline double triangleAreaXY( const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3 ) {
    return triangleArea( v1 * glm::vec3( 1, 1, 0 ),  //
                         v2 * glm::vec3( 1, 1, 0 ),  //
                         v3 * glm::vec3( 1, 1, 0 ) );
  }
  static inline double triangleAreaYZ( const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3 ) {
    return triangleArea( v1 * glm::vec3( 0, 1, 1 ),  //
                         v2 * glm::vec3( 0, 1, 1 ),  //
                         v3 * glm::vec3( 0, 1, 1 ) );
  }

  // Two dimentional triangle area
  static inline double triangleArea( const glm::vec2& v1, const glm::vec2& v2, const glm::vec2& v3 ) {
    return 0.5 * glm::length( glm::cross( glm::vec3( v2 - v1, 0.0 ), glm::vec3( v3 - v1, 0.0 ) ) );
  }


  // computes the bounding box of the triangle
  static inline void triangleBBox( const glm::vec3& v1,
                                   const glm::vec3& v2,
                                   const glm::vec3& v3,
                                   glm::vec3&       minPos,
                                   glm::vec3&       maxPos ) {
    minPos = { FLT_MAX, FLT_MAX, FLT_MAX };
    maxPos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    minPos = glm::min( v1, minPos );
    minPos = glm::min( v2, minPos );
    minPos = glm::min( v3, minPos );
    maxPos = glm::max( v1, maxPos );
    maxPos = glm::max( v2, maxPos );
    maxPos = glm::max( v3, maxPos );
  }

  // if reset is false the minPos and maxPos will not be reset before processing
  // this permits to invoke the function on several point set and obtain the global box
  static inline void computeBBox( const std::vector<float>& vertices,
                                  glm::vec3&                minPos,
                                  glm::vec3&                maxPos,
                                  bool                      reset = true ) {
    if ( reset ) {
      minPos = { FLT_MAX, FLT_MAX, FLT_MAX };
      maxPos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    }
    for ( size_t i = 0; i < vertices.size() / 3; i++ ) {
      for ( glm::vec3::length_type j = 0; j < 3; j++ ) {
        minPos[j] = std::fmin( vertices[i * 3 + j], minPos[j] );
        maxPos[j] = std::fmax( vertices[i * 3 + j], maxPos[j] );
      }
    }
  }

  static inline void computeBBox( const std::vector<float>& vertices,
                                  glm::vec2&                minPos,
                                  glm::vec2&                maxPos,
                                  bool                      reset = true ) {
    if ( reset ) {
      minPos = { FLT_MAX, FLT_MAX };
      maxPos = { -FLT_MAX, -FLT_MAX };
    }
    for ( size_t i = 0; i < vertices.size() / 2; i++ ) {
      for ( glm::vec3::length_type j = 0; j < 2; j++ ) {
        minPos[j] = std::fmin( vertices[i * 2 + j], minPos[j] );
        maxPos[j] = std::fmax( vertices[i * 2 + j], maxPos[j] );
      }
    }
  }

  // if reset is false the minPos and maxPos will not be reset before processing
  // this permits to invoke the function on several point set and obtain the global box
  static inline void
  computeBBox( const std::vector<glm::vec3>& vertices, glm::vec3& minPos, glm::vec3& maxPos, bool reset = true ) {
    if ( reset ) {
      minPos = { FLT_MAX, FLT_MAX, FLT_MAX };
      maxPos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    }
    for ( size_t i = 0; i < vertices.size(); i++ ) {
      minPos = glm::min( vertices[i], minPos );
      maxPos = glm::max( vertices[i], maxPos );
    }
  }

  static inline void
  computeBBox( const std::vector<glm::vec2>& vertices, glm::vec2& minPos, glm::vec2& maxPos, bool reset = true ) {
    if ( reset ) {
      minPos = { FLT_MAX, FLT_MAX };
      maxPos = { -FLT_MAX, -FLT_MAX };
    }
    for ( size_t i = 0; i < vertices.size(); i++ ) {
      minPos = glm::min( vertices[i], minPos );
      maxPos = glm::max( vertices[i], maxPos );
    }
  }

  // compute on count values from start index
  static inline void computeBBox( const std::vector<glm::vec3>& vertices,
                                  glm::vec3&                    minPos,
                                  glm::vec3&                    maxPos,
                                  size_t                        count,
                                  size_t                        startIdx = 0 ) {
    count  = std::min( count, vertices.size() );
    minPos = { FLT_MAX, FLT_MAX, FLT_MAX };
    maxPos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for ( size_t i = startIdx; i < count; i++ ) {
      minPos = glm::min( vertices[i], minPos );
      maxPos = glm::max( vertices[i], maxPos );
    }
  }

  static inline void computeBBox( const std::vector<glm::vec2>& vertices,
                                  glm::vec2&                    minPos,
                                  glm::vec2&                    maxPos,
                                  size_t                        count,
                                  size_t                        startIdx = 0 ) {
    count  = std::min( count, vertices.size() );
    minPos = { FLT_MAX, FLT_MAX };
    maxPos = { -FLT_MAX, -FLT_MAX };
    for ( size_t i = startIdx; i < count; i++ ) {
      minPos = glm::min( vertices[i], minPos );
      maxPos = glm::max( vertices[i], maxPos );
    }
  }

  static inline void computeBBox( const glm::vec3& minPosA,
                                  const glm::vec3& maxPosA,
                                  const glm::vec3& minPosB,
                                  const glm::vec3& maxPosB,
                                  glm::vec3&       minPos,
                                  glm::vec3&       maxPos ) {
    minPos = glm::min( minPosA, minPosB );
    maxPos = glm::max( maxPosA, maxPosB );
  }

  static inline void computeBBox( const glm::vec2& minPosA,
                                  const glm::vec2& maxPosA,
                                  const glm::vec2& minPosB,
                                  const glm::vec2& maxPosB,
                                  glm::vec2&       minPos,
                                  glm::vec2&       maxPos ) {
    minPos = glm::min( minPosA, minPosB );
    maxPos = glm::max( maxPosA, maxPosB );
  }

  // transform the box into a cube bbox fitting original
  static inline void toCubicalBBox( glm::vec3& minPos, glm::vec3& maxPos ) {
    const glm::vec3 halfSize    = ( maxPos - minPos ) / 2.f;
    const glm::vec3 center      = maxPos - halfSize;
    const float     maxHalfSize = std::max( std::max( halfSize.x, halfSize.y ), halfSize.z );
    minPos                      = center - glm::vec3( maxHalfSize, maxHalfSize, maxHalfSize );
    maxPos                      = center + glm::vec3( maxHalfSize, maxHalfSize, maxHalfSize );
  }

  // Compute barycentric coordinates (u, v, w)~res(x,y,z) for
  // point p with respect to triangle (a, b, c)
  // return true if point is inside the triangle
  static bool getBarycentric( glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3& res );

  // return true if there is an intersection
  // and res will contain t (the parameter on the ray to intersection)
  // and u,v the barycentric of the intersection on the triangle surface
  static bool evalRayTriangle( const glm::vec3& rayOrigin,
                               const glm::vec3& rayDirection,
                               const glm::vec3& v0,
                               const glm::vec3& v1,
                               const glm::vec3& v2,
                               glm::vec3&       res,
                               float            epsilon = ZERO_TOLERANCE );

  // https://github.com/autonomousvision/occupancy_flow/blob/master/im2mesh/utils/libvoxelize/tribox2.h
  /********************************************************/
  /* AABB-triangle overlap test code                      */
  /* by Tomas Akenine-M�ller                              */
  /* Function: int triBoxOverlap(float boxcenter[3],      */
  /*          float boxhalfsize[3],float triverts[3][3]); */
  /* History:                                             */
  /*   2001-03-05: released the code in its first version */
  /*   2001-06-18: changed the order of the tests, faster */
  /*                                                      */
  /* Acknowledgement: Many thanks to Pierre Terdiman for  */
  /* suggestions and discussions on how to optimize code. */
  /* Thanks to David Hunt for finding a ">="-bug!         */
  /********************************************************/

  /*
  Copyright 2020 Tomas Akenine-Möller

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
  documentation files (the "Software"), to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
  to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial
  portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
  WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
  OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
  OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  */

  static inline int planeBoxOverlap( glm::vec3& normal, float d, const glm::vec3& maxbox ) {
    glm::vec3 vmin, vmax;
    for ( int q = 0; q < 3; q++ ) {
      if ( normal[q] > 0.0f ) {
        vmin[q] = -maxbox[q];
        vmax[q] = maxbox[q];
      } else {
        vmin[q] = maxbox[q];
        vmax[q] = -maxbox[q];
      }
    }
    if ( glm::dot( normal, vmin ) + d > 0.0f ) { return 0; }
    if ( glm::dot( normal, vmax ) + d >= 0.0f ) { return 1; }
    return 0;
  }

  static inline bool triangleBoxIntersection( const glm::vec3& pmin,
                                              const glm::vec3& pmax,
                                              const glm::vec3& a,
                                              const glm::vec3& b,
                                              const glm::vec3& c ) {
    const glm::vec3 halfsize = ( pmax - pmin ) / 2.f;
    const glm::vec3 center   = pmax - halfsize;

    // use separating axis theorem to test overlap between triangle and box need to test for overlap in these
    // directions: 1) the {x,y,z}-directions (actually, since we use the AABB of the triangle we do not even need to
    // test these) 2) normal of the triangle 3) crossproduct(edge from tri, {x,y,z}-directin)  this gives 3x3=9 more
    // tests This is the fastest branch on Sun  move everything so that the boxcenter is in (0,0,0) compute vertices and
    // triangle edges
    glm::vec3 v0 = a - center, v1 = b - center, v2 = c - center;
    glm::vec3 e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;

    // Bullet 3:
    //  test the 9 tests first (this was faster)
#define AXISTEST( i, j, va, vb, e )                                                \
  {                                                                                \
    auto  pa = e[i] * va[j] - e[j] * va[i], pb = e[i] * vb[j] - e[j] * vb[i];      \
    float rad = std::fabs( e[i] ) * halfsize[j] + std::fabs( e[j] ) * halfsize[i]; \
    if ( std::min( pa, pb ) > rad || std::max( pa, pb ) < -rad ) { return 0; }     \
  }
    AXISTEST( 2, 1, v0, v2, e0 );
    AXISTEST( 2, 1, v0, v2, e1 );
    AXISTEST( 2, 1, v0, v1, e2 );

    AXISTEST( 0, 2, v0, v2, e0 );
    AXISTEST( 0, 2, v0, v2, e1 );
    AXISTEST( 0, 2, v0, v1, e2 );

    AXISTEST( 1, 0, v1, v2, e0 );
    AXISTEST( 1, 0, v0, v1, e1 );
    AXISTEST( 1, 0, v1, v2, e2 );

    // Bullet 1:
    //  first test overlap in the {x,y,z}-directions find min, max of the triangle each direction, and test for overlap
    //  in that direction -- this is equivalent to testing a minimal AABB around  the triangle against the AABB

    // test in XYZ-direction
    glm::vec3 vmin = glm::min( v0, glm::min( v1, v2 ) );
    glm::vec3 vmax = glm::max( v0, glm::max( v1, v2 ) );
    if ( vmin[0] > halfsize[0] || vmax[0] < -halfsize[0] ) { return 0; }
    if ( vmin[1] > halfsize[1] || vmax[1] < -halfsize[1] ) { return 0; }
    if ( vmin[2] > halfsize[2] || vmax[2] < -halfsize[2] ) { return 0; }

    // Bullet 2:
    //  test if the box intersects the plane of the triangle  compute plane equation of triangle: normal*x+d=0
    glm::vec3 normal = glm::cross( e0, e1 );
    float     d      = -glm::dot( normal, v0 );
    if ( !planeBoxOverlap( normal, d, halfsize ) ) { return 0; }
    return 1;  // box and triangle overlaps
  }
};

}  // namespace mm

#endif