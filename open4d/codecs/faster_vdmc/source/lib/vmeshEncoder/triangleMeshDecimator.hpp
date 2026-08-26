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

#include <memory>

#include "error.hpp"

#define SIMPLIFY_MESH_TRACK_POINTS 1

namespace vmesh {

//============================================================================

enum class VertexPlacement {
  END_POINTS              = 0,
  END_POINTS_OR_MID_POINT = 1,
  OPTIMAL                 = 2
};

//============================================================================

struct TriangleMeshDecimatorParameters {
  double          triangleFlipPenalty    = 1e+7;
  double          triangleFlipThreshold  = 0.5;
  double          boundaryWeight         = 1e+7;
  double          angleQualityThreshold  = 1.0;
  double          maxError               = 1.0;
  bool            areaWeightedQuadratics = true;
  int             triangleCount          = 1;
  int             vertexCount            = 1;
  VertexPlacement vertexPlacement        = VertexPlacement::OPTIMAL;
#if SIMPLIFY_MESH_TRACK_POINTS
  double trackedTriangleFlipThreshold              = 0.0;
  double trackedPointNormalFlipThreshold           = 0.0;
  bool   preserveTrackedTriangleNormalsOrientation = true;
#endif  //SIMPLIFY_MESH_TRACK_POINTS
};

//============================================================================

class TriangleMeshDecimatorImpl;
class TriangleMeshDecimator {
public:
  TriangleMeshDecimator();
  TriangleMeshDecimator(const TriangleMeshDecimator&)            = delete;
  TriangleMeshDecimator& operator=(const TriangleMeshDecimator&) = delete;
  ~TriangleMeshDecimator();

  Error decimate(const double*                          points,
                 int                                    pointCount,
                 const int*                             triangles,
                 int                                    triangleCount,
                 bool                                   trackFlag,
                 const TriangleMeshDecimatorParameters& params);

  int   decimatedTriangleCount() const;
  int   decimatedPointCount() const;
  Error decimatedMesh(double* dpoints,
                      int     dpointCount,
                      int32_t* dtracktris,
                      int32_t* dmodifytris,
                      int*    dtriangles,
                      bool     trackFlag,
                      int     dtriangleCount) const;

#if SIMPLIFY_MESH_TRACK_POINTS
  int   trackedPointCount() const;
  Error trackedPoints(double* tpoints,
                      int*    tindexes,
                      int*    ttracks,
                      bool    trackFlag,
                      int     pointCount) const;
#endif  // SIMPLIFY_MESH_TRACK_POINTS

private:
  std::unique_ptr<TriangleMeshDecimatorImpl> _pimpl;
};

//============================================================================

}  // namespace vmesh
