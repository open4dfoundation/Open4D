/* The copyright in this software is being made available under the BSD
* Licence, included below.  This software may be subject to other third
* party and contributor rights, including patent rights, and no such
* rights are granted under this licence.
*
* Copyright (c) 2025, ISO/IEC
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

#include "encoder.hpp"

#include <cmath>
#include <array>
#include <iostream>
#include "vmc.hpp"
#include "metrics.hpp"
#include "transferColor.hpp"
#include "util/misc.hpp"
#include "util/mesh.hpp"

namespace vmesh {

//============================================================================
void
VMCEncoder::optimizeZipperingThreshold(
  const VMCEncoderParameters&                          params,
  const std::vector<std::vector<std::vector<int32_t>>> submeshLodMaps,
  const std::vector<std::vector<std::vector<int32_t>>>
    submeshChildToParentMaps) {
  zipperingMatchMaxDistance_ = params.zipperingThreshold_;
  if (params.zipperingMethod_ > 0) {
    auto frameCount   = reconSubdivmeshes_[0].size();
    auto submeshCount = reconSubdivmeshes_.size();
    // estimate the max distance parameter for zippering according to the distortion
    // this could be done per frame as well, if we need to make a finer adjustment
    zipperingMatchMaxDistancePerFrame_.resize(frameCount, 0);
    zipperingMatchMaxDistancePerSubmesh_.resize(frameCount);
    zipperingMatchMaxDistancePerSubmeshPair_.resize(frameCount);
    zipperingDistanceBorderPoint_.resize(frameCount);
    zipperingMatchedBorderPoint_.resize(frameCount);
    zipperingMatchedBorderPointDelta_.resize(frameCount);
    zipperingMatchedBorderPointDeltaVariance_.resize(frameCount);
    zipperingMatchMaxDistance_ = 0;
    for (size_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
        if ((params.zipperingMethod_ >= 0 && params.zipperingMethod_ != 7) || (params.zipperingMethod_ == 7 && zipperingSwitch_[frameIdx])) {
            zipperingMatchMaxDistancePerSubmesh_[frameIdx].resize(submeshCount);
            zipperingMatchMaxDistancePerSubmeshPair_[frameIdx].resize(submeshCount
                - 1);
            for (size_t submeshIdx = 0; submeshIdx < submeshCount - 1;
                submeshIdx++) {
                zipperingMatchMaxDistancePerSubmeshPair_[frameIdx][submeshIdx].resize(
                    submeshCount - 1 - submeshIdx);
            }
            zipperingDistanceBorderPoint_[frameIdx].resize(submeshCount);
            zipperingMatchedBorderPoint_[frameIdx].resize(submeshCount);
            TriangleMesh<MeshType>           boundaryVertices;
            std::vector<size_t>              numBoundaries(submeshCount, 0);
            std::vector<size_t>              accNumBoundaries(submeshCount + 1, 0);
            std::vector<std::vector<int8_t>> isBoundaryVertex(submeshCount);
            // find border vertices and create a point cloud with the boundary vertices
            for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
                auto& submesh = reconSubdivmeshes_[submeshIdx][frameIdx];
                const auto pointCount = submesh.pointCount();
                const auto triangleCount = submesh.triangleCount();
                StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
                ComputeVertexToTriangle(
                    submesh.triangles(), submesh.pointCount(), vertexToTriangleOutput);
                std::vector<int8_t> vtags(pointCount);
                std::vector<int8_t> ttags(triangleCount);
                ComputeBoundaryVertices(submesh.triangles(),
                    vertexToTriangleOutput,
                    vtags,
                    ttags,
                    isBoundaryVertex[submeshIdx]);
                // add boundary vertices to point cloud
                for (int i = 0; i < pointCount; i++) {
                    if (isBoundaryVertex[submeshIdx][i]) {
                        boundaryVertices.addPoint(submesh.point(i));
                        numBoundaries[submeshIdx]++;
                    }
                }
                //allocate zippering structures
                zipperingDistanceBorderPoint_[frameIdx][submeshIdx].resize(
                    numBoundaries[submeshIdx]);
                zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
                    numBoundaries[submeshIdx]);
            }
            //accumulate the number of boundaries
            accNumBoundaries[0] = 0;
            for (size_t submeshIdx = 1; submeshIdx < submeshCount + 1;
                submeshIdx++) {
                accNumBoundaries[submeshIdx] =
                    numBoundaries[submeshIdx - 1] + accNumBoundaries[submeshIdx - 1];
            }
            // now search for the best boundary point match
            KdTree<MeshType> kdtreeTarget(
                3, boundaryVertices.points(), 10);  // dim, cloud, max leaf
            int               pointIdx = -1;
            std::vector<bool> matchedVertex(boundaryVertices.pointCount(), false);
            for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
                for (int i = 0; i < numBoundaries[submeshIdx]; i++) {
                    pointIdx++;
                    if (!matchedVertex[pointIdx]) {
                        // intitialize without a match
                        zipperingMatchedBorderPoint_[frameIdx][submeshIdx][i] =
                            Vec2<size_t>(submeshCount, 0);
                        zipperingDistanceBorderPoint_[frameIdx][submeshIdx][i] = 0;
                        // find all nearest points
                        auto queryCount =
                            submeshCount
                            * 2;  // boundaryVertices.pointCount(); // the closest N*2 values cannot be in the same submesh, otherwise it won't get a match
                          //while (queryCount < boundaryVertices.pointCount()) {
                        std::vector<int32_t>  indices(queryCount, 0);
                        std::vector<MeshType> sqrDists(queryCount, 0);
                        int curVertexIdx, curVertexLod, matchedVertexIdx, matchedVertexLod;
                        if (params.zipperingMethodForUnmatchedLoDs_) {
                            auto curSubmesh = reconSubdivmeshes_[submeshIdx][frameIdx];
                            curVertexIdx = 0;
                            int borderVertexCount = 0,
                                relativeIdx = pointIdx - accNumBoundaries[submeshIdx];
                            while (curVertexIdx < curSubmesh.pointCount()) {
                                if (isBoundaryVertex[submeshIdx][curVertexIdx])
                                    if (borderVertexCount == relativeIdx) {
                                        break;
                                    }
                                    else {
                                        borderVertexCount++;
                                    }
                                curVertexIdx++;
                            }
                            curVertexLod =
                                submeshLodMaps[submeshIdx][frameIdx][curVertexIdx];

                            if (curVertexLod == params.subdivisionIterationCount + 1) {
                                continue;
                            }
                        }
                        kdtreeTarget.query(boundaryVertices.point(pointIdx).data(),
                            queryCount,
                            indices.data(),
                            sqrDists.data());
                        bool sufficiently = false;
                        for (size_t queryIdx = 0; queryIdx < queryCount && !sufficiently;
                            ++queryIdx) {
                            auto index = indices[queryIdx];
                            auto sqrDist = std::ceil(sqrDists[queryIdx]);
                            auto submeshIdxMatched =
                                static_cast<int32_t>(std::distance(
                                    accNumBoundaries.begin(),
                                    std::upper_bound(
                                        accNumBoundaries.begin(), accNumBoundaries.end(), index)))
                                - 1;
                            auto pointIdxMatched =
                                index - accNumBoundaries[submeshIdxMatched];
                            if (params.zipperingMethodForUnmatchedLoDs_) {
                                auto matchedSubmesh =
                                    reconSubdivmeshes_[submeshIdxMatched][frameIdx];
                                matchedVertexIdx = 0;
                                int matchedVertexCount = 0;
                                while (matchedVertexIdx < matchedSubmesh.pointCount()) {
                                    if (isBoundaryVertex[submeshIdxMatched][matchedVertexIdx])
                                        if (matchedVertexCount == pointIdxMatched) {
                                            break;
                                        }
                                        else {
                                            matchedVertexCount++;
                                        }
                                    matchedVertexIdx++;
                                }
                                matchedVertexLod = submeshLodMaps[submeshIdxMatched][frameIdx]
                                    [matchedVertexIdx];
                            }
                            if (submeshIdxMatched != submeshIdx) {
                                if (params.zipperingMethodForUnmatchedLoDs_
                                    && curVertexLod == matchedVertexLod) {
                                    auto curvindex0 =
                                        submeshChildToParentMaps[submeshIdx][frameIdx]
                                        [2 * curVertexIdx];
                                    auto curvindex1 =
                                        submeshChildToParentMaps[submeshIdx][frameIdx]
                                        [2 * curVertexIdx + 1];
                                    auto matchedvindex0 =
                                        submeshChildToParentMaps[submeshIdxMatched][frameIdx]
                                        [2 * matchedVertexIdx];
                                    auto matchedvindex1 =
                                        submeshChildToParentMaps[submeshIdxMatched][frameIdx]
                                        [2 * matchedVertexIdx + 1];
                                    if (curVertexLod != 0) {
                                        auto curBorderindex0 = std::count(
                                            isBoundaryVertex[submeshIdx].begin(),
                                            isBoundaryVertex[submeshIdx].begin() + curvindex0,
                                            1);
                                        auto curBorderindex1 = std::count(
                                            isBoundaryVertex[submeshIdx].begin(),
                                            isBoundaryVertex[submeshIdx].begin() + curvindex1,
                                            1);

                                        auto matchedBorderindex0 =
                                            std::count(isBoundaryVertex[submeshIdxMatched].begin(),
                                                isBoundaryVertex[submeshIdxMatched].begin()
                                                + matchedvindex0,
                                                1);
                                        auto matchedBorderindex1 =
                                            std::count(isBoundaryVertex[submeshIdxMatched].begin(),
                                                isBoundaryVertex[submeshIdxMatched].begin()
                                                + matchedvindex1,
                                                1);
                                        if (std::max(matchedBorderindex0, matchedBorderindex1)
                                            != std::max(
                                                zipperingMatchedBorderPoint_[frameIdx][submeshIdx]
                                                [curBorderindex0][1],
                                                zipperingMatchedBorderPoint_[frameIdx][submeshIdx]
                                                [curBorderindex1][1])
                                            || std::min(matchedBorderindex0, matchedBorderindex1)
                                            != std::min(
                                                zipperingMatchedBorderPoint_
                                                [frameIdx][submeshIdx][curBorderindex0][1],
                                                zipperingMatchedBorderPoint_
                                                [frameIdx][submeshIdx][curBorderindex1][1])) {
                                            continue;
                                        }
                                    }
                                }
                                zipperingMatchedBorderPoint_[frameIdx][submeshIdx][i] =
                                    Vec2<size_t>(submeshIdxMatched, pointIdxMatched);
                                zipperingDistanceBorderPoint_[frameIdx][submeshIdx][i] =
                                    sqrDist;
                                if (sqrDist > zipperingMatchMaxDistancePerSubmesh_[frameIdx]
                                    [submeshIdx])
                                    zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx] =
                                    sqrDist;
                                int minId = 0, maxId = 0;
                                if (static_cast<int>(submeshIdx)
                        > static_cast<int>(submeshIdxMatched)) {
                                    minId = static_cast<int>(submeshIdxMatched);
                                    maxId = static_cast<int>(submeshIdx);
                                }
                                else {
                                    minId = static_cast<int>(submeshIdx);
                                    maxId = static_cast<int>(submeshIdxMatched);
                                }
                                if (sqrDist > zipperingMatchMaxDistancePerSubmeshPair_
                                    [frameIdx][minId][maxId - minId - 1])
                                    zipperingMatchMaxDistancePerSubmeshPair_[frameIdx][minId]
                                    [maxId - minId - 1] =
                                    sqrDist;
                                matchedVertex[pointIdx] = 1;
                                // now check if the matched index is already matched with some previous border vertex
                                if (matchedVertex[index] == 0) {
                                    zipperingMatchedBorderPoint_[frameIdx][submeshIdxMatched]
                                        [pointIdxMatched] =
                                        Vec2<size_t>(submeshIdx, i);
                                    zipperingDistanceBorderPoint_[frameIdx][submeshIdxMatched]
                                        [pointIdxMatched] = sqrDist;
                                    if (sqrDist > zipperingMatchMaxDistancePerSubmesh_
                                        [frameIdx][submeshIdxMatched])
                                        zipperingMatchMaxDistancePerSubmesh_[frameIdx]
                                        [submeshIdxMatched] =
                                        sqrDist;
                                    matchedVertex[index] = 1;
                                }
                                sufficiently = true;
                            }
                        }
                        //if (sufficiently) { break; }
                        //++queryCount;
                        //} // end of query
                    }  // end of boundary search
                    if (zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx]
                        < zipperingDistanceBorderPoint_[frameIdx][submeshIdx][i])
                        zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx] =
                        zipperingDistanceBorderPoint_[frameIdx][submeshIdx][i];
                }
                if (zipperingMatchMaxDistancePerFrame_[frameIdx]
                    < zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx])
                    zipperingMatchMaxDistancePerFrame_[frameIdx] =
                    zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx];
            }
            if (zipperingMatchMaxDistancePerFrame_[frameIdx]
                    > zipperingMatchMaxDistance_) {
                zipperingMatchMaxDistance_ =
                    zipperingMatchMaxDistancePerFrame_[frameIdx];
            }
            // delta
            std::vector<int64_t> submeshIndicesDelta;
            std::vector<int64_t> borderPointIndicesDelta;
            zipperingMatchedBorderPointDelta_[frameIdx].resize(submeshCount);
            std::vector<std::vector<uint8_t>> zipperingBorderPointMatchIndexFlag(
                submeshCount);
            for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
                zipperingBorderPointMatchIndexFlag[submeshIdx].assign(
                    numBoundaries[submeshIdx], 0);
                zipperingMatchedBorderPointDelta_[frameIdx][submeshIdx].assign(
                    numBoundaries[submeshIdx], Vec2<int64_t>(0, 0));
            }
            for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
                int64_t prevSubmeshIdx = 0;
                int64_t prevBorderPointIdx = 0;
                for (int i = 0; i < numBoundaries[submeshIdx]; i++) {
                    if (zipperingBorderPointMatchIndexFlag[submeshIdx][i] == 0) {
                        auto submeshIdxMatched =
                            zipperingMatchedBorderPoint_[frameIdx][submeshIdx][i][0];
                        auto pointIdxMatched =
                            zipperingMatchedBorderPoint_[frameIdx][submeshIdx][i][1];
                        auto submeshIdxDelta =
                            static_cast<int64_t>(submeshIdxMatched) - prevSubmeshIdx;
                        submeshIndicesDelta.push_back(submeshIdxDelta);
                        zipperingMatchedBorderPointDelta_[frameIdx][submeshIdx][i][0] =
                            submeshIdxDelta;
                        prevSubmeshIdx = submeshIdxMatched;
                        if (submeshIdxMatched != submeshCount) {
                            auto borderPointIdxDelta =
                                static_cast<int64_t>(pointIdxMatched) - prevBorderPointIdx;
                            borderPointIndicesDelta.push_back(borderPointIdxDelta);
                            zipperingMatchedBorderPointDelta_[frameIdx][submeshIdx][i][1] =
                                borderPointIdxDelta;
                            prevBorderPointIdx = pointIdxMatched;
                            if (submeshIdxMatched > submeshIdx) {
                                zipperingBorderPointMatchIndexFlag[submeshIdxMatched]
                                    [pointIdxMatched] = 1;
                            }
                        }
                    }
                }
            }
            auto submeshIndicesDeltaVariance =
                calcVariance(submeshIndicesDelta.begin(), submeshIndicesDelta.end());
            auto borderPointIndicesDeltaVariance = calcVariance(
                borderPointIndicesDelta.begin(), borderPointIndicesDelta.end());
            zipperingMatchedBorderPointDeltaVariance_[frameIdx] = {
              submeshIndicesDeltaVariance, borderPointIndicesDeltaVariance };
        }
    }
    std::cout << "Updating the sequence zipperingMaxMatchDistance_ to "
              << zipperingMatchMaxDistance_ << std::endl;
  }
}

//============================================================================
void
VMCEncoder::zippering(
  int32_t                                              frameCount,
  int32_t                                              submeshCount,
  const VMCEncoderParameters&                          params,
  const std::vector<std::vector<std::vector<int32_t>>> submeshLodMaps,
  const std::string&                                   inputpath,
  int32_t                                              startFrame) {
  if (params.zipperingMethod_ == 0) {
    zipperingDistanceBorderPoint_.resize(frameCount);
    zipperingMatchedBorderPoint_.resize(frameCount);
  }
  if (params.zipperingMethod_ == 7 && zipperingSwitch_.size() == 0) {
      zipperingSwitch_.resize(frameCount, false);
      zipperingMeshes_.resize(submeshCount);
      boundaryIndex_.resize(frameCount);
      crackCount_.resize(frameCount);
  }
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    std::vector<TriangleMesh<MeshType>> submeshes;
    submeshes.resize(submeshCount);
    std::vector<std::vector<int32_t>> frameSubmeshLodMaps;
    frameSubmeshLodMaps.resize(submeshCount);
    if (params.zipperingMethod_ == 7 && boundaryIndex_[frameIdx].size() == 0) boundaryIndex_[frameIdx].resize(submeshCount);
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      submeshes[submeshIdx] = reconSubdivmeshes_[submeshIdx][frameIdx];
      if (params.zipperingMethodForUnmatchedLoDs_)
        frameSubmeshLodMaps[submeshIdx] = submeshLodMaps[submeshIdx][frameIdx];
    }
    // find the borders
    std::vector<std::vector<int8_t>> isBoundaryVertex;
    std::vector<size_t>              numBoundaries;
    TriangleMesh<MeshType>           boundaryVertices;
    vmesh::zippering_find_borders(
      submeshes, isBoundaryVertex, numBoundaries, boundaryVertices);
    int zipperingMethod = (params.zipperingMethod_ == 7 && zipperingSwitch_[frameIdx]) ? 3 : params.zipperingMethod_;
    switch (zipperingMethod) {
    case 0:  // fixed value ->
    case 1:  // variable threshold for the entire sequence ->
      // copy the value to the input structure
      if (params.zipperingMethod_ == 0) {
        zipperingDistanceBorderPoint_[frameIdx].resize(submeshCount);
        zipperingMatchedBorderPoint_[frameIdx].resize(submeshCount);
        for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
          zipperingDistanceBorderPoint_[frameIdx][submeshIdx].resize(
            numBoundaries[submeshIdx]);
          zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
            numBoundaries[submeshIdx]);
        }
      }
      for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        for (int32_t borderIdx = 0; borderIdx < numBoundaries[submeshIdx];
             borderIdx++) {
          zipperingDistanceBorderPoint_[frameIdx][submeshIdx][borderIdx] =
            zipperingMatchMaxDistance_;
          zipperingMatchedBorderPoint_[frameIdx][submeshIdx][borderIdx] =
            Vec2<size_t>(submeshCount, 0);
        }
      }
      // now search for the matches
      vmesh::zippering_find_matches_distance_matrix(
        submeshes,
        boundaryVertices,
        numBoundaries,
        zipperingDistanceBorderPoint_[frameIdx],
        zipperingMatchedBorderPoint_[frameIdx]);
      // fuse the matched border vertices together
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 2:  // max value per frame ->
      // copy the value to the input structure
      for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        for (int32_t borderIdx = 0; borderIdx < numBoundaries[submeshIdx];
             borderIdx++) {
          zipperingDistanceBorderPoint_[frameIdx][submeshIdx][borderIdx] =
            zipperingMatchMaxDistancePerFrame_[frameIdx];
        }
      }
      // now search for the matches
      vmesh::zippering_find_matches_distance_matrix(
        submeshes,
        boundaryVertices,
        numBoundaries,
        zipperingDistanceBorderPoint_[frameIdx],
        zipperingMatchedBorderPoint_[frameIdx]);
      // fuse the matched border vertices together
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 3:  // max value per sub-mesh ->
      // copy the value to the input structure
      for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        for (int32_t borderIdx = 0; borderIdx < numBoundaries[submeshIdx];
             borderIdx++) {
          zipperingDistanceBorderPoint_[frameIdx][submeshIdx][borderIdx] =
            zipperingMatchMaxDistancePerSubmesh_[frameIdx][submeshIdx];
        }
      }
      // now search for the matches
      vmesh::zippering_find_matches_distance_matrix(
        submeshes,
        boundaryVertices,
        numBoundaries,
        zipperingDistanceBorderPoint_[frameIdx],
        zipperingMatchedBorderPoint_[frameIdx]);
      // fuse the matched border vertices together
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 4:  // max value per border point
      // now search for the matches
      vmesh::zippering_find_matches_distance_matrix(
        submeshes,
        boundaryVertices,
        numBoundaries,
        zipperingDistanceBorderPoint_[frameIdx],
        zipperingMatchedBorderPoint_[frameIdx]);
      // fuse the matched border vertices together
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 5:
      vmesh::zippering_find_matches_distance_per_submesh_pair(
        submeshes,
        boundaryVertices,
        numBoundaries,
        zipperingMatchMaxDistancePerSubmeshPair_[frameIdx],
        zipperingMatchedBorderPoint_[frameIdx],
        params.linearSegmentation);
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 6:  // explicit matched border points
      // fuse the matched border vertices together
      vmesh::zippering_fuse_border(submeshes,
                                   isBoundaryVertex,
                                   zipperingMatchedBorderPoint_[frameIdx],
                                   frameSubmeshLodMaps,
                                   params.zipperingMethodForUnmatchedLoDs_);
      break;
    case 7: {  //boundary connect zippering
      if (crackCount_[crackCount_.size() - 1].size() == 0) {
          std::vector<std::vector<std::vector<int>>> growuplist;
          std::vector<std::vector<std::vector<int>>> triangle_grow;
          std::vector<std::vector<size_t>>           crackIndex;
          std::vector<TriangleMesh<MeshType>>        submeshes_temp;
          TriangleMesh<MeshType>                     input, mesh_grow, mesh_origin;
          submeshes_temp = submeshes;
          std::string srcFilename = expandNum(inputpath, startFrame + frameIdx);
          input.load(srcFilename);
          vmesh::boundaryprocess(submeshes,
              isBoundaryVertex,
              growuplist,
              triangle_grow,
              crackCount_[frameIdx],
              boundaryIndex_[frameIdx],
              crackIndex);
          vmesh::boundaryfuse(submeshes, growuplist, triangle_grow, crackIndex);
          vmesh::addtexture(submeshes, submeshes_temp);
          for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
              submeshes[submeshIdx].resizeNormals(
                  submeshes[submeshIdx].pointCount());
              submeshes[submeshIdx].computeNormals();
              mesh_grow.append(submeshes[submeshIdx]);
              mesh_origin.append(reconSubdivmeshes_[submeshIdx][frameIdx]);
          }
          VMCMetrics           metricsOrigin;
          VMCMetrics           metricsGrow;
          VMCMetricsParameters metricParams;
          metricParams.computePcc = true;
          metricParams.qp = params.bitDepthPosition;
          metricParams.qt = params.bitDepthTexCoord;
          for (int c = 0; c < 3; c++) {
              metricParams.minPosition[c] = params.minPosition[c];
              metricParams.maxPosition[c] = params.maxPosition[c];
          }
          metricParams.normalCalcModificationEnable =
              params.normalCalcModificationEnable;
          metricsOrigin.compute(input, mesh_origin, metricParams);
          metricsGrow.compute(input, mesh_grow, metricParams);
          auto metOrigin = metricsOrigin.getPccResults();
          auto metGrow = metricsGrow.getPccResults();
          if (metOrigin[1] - metGrow[1] > 0.01) {
              submeshes = submeshes_temp;
              zipperingSwitch_[frameIdx] = true;
          }
          else {
              for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
                  zipperingMeshes_[submeshIdx].resize(frameCount);
                  zipperingMeshes_[submeshIdx][frameIdx] = submeshes[submeshIdx];
              }
              vmesh::setNewMesh(submeshes, submeshes_temp);
              submeshes = submeshes_temp;
          }
      }
      break;
    }
    }
    //copy the results back
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      reconSubdivmeshes_[submeshIdx][frameIdx] = submeshes[submeshIdx];
    }
  }
}
}  // namespace vmesh
