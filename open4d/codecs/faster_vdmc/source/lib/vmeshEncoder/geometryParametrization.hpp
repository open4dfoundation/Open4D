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

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "util/vector.hpp"

#include "util/mesh.hpp"
#include "util/kdtree.hpp"
#include "vmc.hpp"

namespace vmesh {

//============================================================================

struct GeometryParametrizationParameters {
  int     geometrySamplingSubdivisionIterationCount     = 3;
  bool    applyVertexUnification                        = true;
  int32_t geometryFittingIterationCount                 = 16;
  double  geometrySmoothingCoeffcient                   = 0.25;
  double  geometrySmoothingCoeffcientDecayRatio         = 0.75;
  double  geometryMissedVerticesSmoothingCoeffcient     = 0.1;
  int32_t geometryMissedVerticesSmoothingIterationCount = 10;
  bool    lodAdaptiveSubdivisionFlag                    = false;
  std::vector<SubdivisionMethod> subdivisionMethod      = {
    SubdivisionMethod::MID_POINT,
    SubdivisionMethod::MID_POINT,
    SubdivisionMethod::MID_POINT};
  int32_t         geometryParametrizationSubdivisionIterationCount = 3;
  bool            fitSubdivisionSurface                            = true;
  double          initialDeformNormalDeviationThreshold            = 0.1;
  int32_t         initialDeformNNCount                             = 1;
  bool            initialDeformForceNormalDisplacement             = false;
  bool            applySmoothingDeform                             = true;
  SmoothingMethod smoothDeformSmoothingMethod =
    SmoothingMethod::VERTEX_CONSTRAINT;
  bool    smoothDeformUpdateNormals               = true;
  double  smoothDeformTriangleNormalFlipThreshold = -0.5;
  bool    smoothingDeformUseInitialGeometry       = true;
  bool    smoothingDeformSmoothMotion             = true;
  int32_t subdivisionIterationCount               = 3;
  int32_t subdivisionEdgeLengthThreshold          = 0;
  int32_t bitshiftEdgeBasedSubdivision            = 0;
};

//============================================================================

class GeometryParametrization {
public:
  GeometryParametrization()  = default;
  ~GeometryParametrization() = default;

  template<typename T>
  bool generate(TriangleMesh<T>&                         target,
                TriangleMesh<T>&                         source,
                TriangleMesh<T>&                         mapped,
                const TriangleMesh<T>&                   mtarget,
                const TriangleMesh<T>&                   subdiv0,
                const GeometryParametrizationParameters& params,
                TriangleMesh<T>&                         base,
                TriangleMesh<T>&                         deformed,
                TriangleMesh<T>&                         ndeformed,
                TriangleMesh<T>*                         refMesh = nullptr,
                bool                                     flag    = false);

private:
  template<typename T>
  static bool CheckTriangleNormalInversion(
    int32_t                                    vindex,
    const TriangleMesh<T>&                     output,
    const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
    const std::vector<Vec3<T>>&                initialTriangleNormals,
    const GeometryParametrizationParameters&   params);

  template<typename T>
  static void FitMesh(const TriangleMesh<T>& target,
                      const KdTree<T>&       kdtree,
                      TriangleMesh<T>&       output);

  template<typename T>
  static void UpdateMissedVertices(
    const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
    const std::vector<int8_t>&                 isBoundaryVertex,
    const std::vector<int32_t>&                missedVertices,
    TriangleMesh<T>&                           output,
    std::vector<int32_t>&                      vadj,
    std::vector<int8_t>&                       vtags,
    std::vector<int8_t>&                       ttags,
    const GeometryParametrizationParameters&   params);

  template<typename T>
  static void InitialDeform(const TriangleMesh<T>&                   target,
                            const TriangleMesh<T>&                   mapped,
                            const TriangleMesh<T>&                   motion,
                            const TriangleMesh<T>&                   mappedorg,
                            const TriangleMesh<T>&                   decimateorg,
                            TriangleMesh<T>&                         output,
                            const bool trackFlag,
                            const GeometryParametrizationParameters& params);

  template<typename T>
  static void InitialDeform(const TriangleMesh<T>&                   target,
                            TriangleMesh<T>&                         output,
                            const GeometryParametrizationParameters& params);

  template<typename T>
  static void Deform(const TriangleMesh<T>&      target,
                     const std::vector<Vec3<T>>& initialPositions,
                     const std::vector<Vec3<T>>& initialTriangleNormals,
                     TriangleMesh<T>&            output,
                     const GeometryParametrizationParameters& params);

  template<typename T>
  static bool Subdivide(TriangleMesh<T>&                         mesh,
                        const GeometryParametrizationParameters& params,
                        TriangleMesh<T>*                         refMesh);

  template<typename T>
  static bool FitMesh(const TriangleMesh<T>&                   target,
                      TriangleMesh<T>&                         mapped,
                      const TriangleMesh<T>&                   motion,
                      const TriangleMesh<T>&                   decimate,
                      TriangleMesh<T>&                         deformed,
                      bool trackFlag,
                      const GeometryParametrizationParameters& params);

  template<typename T>
  static bool ComputeMotion(const TriangleMesh<T>& reference,
                            const TriangleMesh<T>& target,
                            TriangleMesh<T>&       motion);
};

}  // namespace vmesh