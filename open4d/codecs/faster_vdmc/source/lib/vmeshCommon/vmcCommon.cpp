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

#include <stdio.h>
#include "vmc.hpp"
#include "util/kdtree.hpp"

#define ZIPPERING_TIMING 1
#if ZIPPERING_TIMING
#  include <chrono>
#endif

namespace {

using vmesh::BaseMeshGOPEntry;

int32_t
referenceFrameIndexFromFrameOrderBasemeshGOPSize4(
  int32_t                              frameOrderIndex,
  int32_t                              frameCount,
  const std::vector<BaseMeshGOPEntry>& GOPList) {
  //assert(frameOrderIndex > 0);
  if (GOPList.empty()) { return frameOrderIndex - 1; }
  const auto numGOP     = static_cast<int32_t>(GOPList.size());
  const auto numLastGOP = (frameCount - 1) % numGOP;
  int32_t    ref_poc    = 0;
  if (frameOrderIndex < frameCount - numLastGOP) {
    auto POC = frameOrderIndex % numGOP;
    if (POC == 0) { POC = numGOP; }
    const auto pGOP =
      std::find_if(GOPList.begin(), GOPList.end(), [&](const auto& GOP) {
        return GOP.POC == POC;
      });
    ref_poc = pGOP->referencePics[0];
  } else {
    switch (numLastGOP) {
    case 1: ref_poc = -1; break;
    case 2:
      if (frameCount - frameOrderIndex == 1) ref_poc = -2;
      else ref_poc = 1;
      break;
    case 3:
      if (frameCount - frameOrderIndex == 1) ref_poc = -1;
      else if (frameCount - frameOrderIndex == 2) ref_poc = -2;
      else ref_poc = 1;
      break;
    default: break;
    }
  }
  return frameOrderIndex + ref_poc;
}

}  // namespace

namespace vmesh {

std::tuple<int32_t, uint8_t>
syntaxForTemporalScalabilityBasemesh(
  int32_t                              decodeOrderIndex,
  int32_t                              frameCount,
  const std::vector<BaseMeshGOPEntry>& GOPList) {
  if (GOPList.empty()) { return {decodeOrderIndex, 1}; }
  if (decodeOrderIndex == 0) { return {0, 1U}; }
  assert(0 < decodeOrderIndex && decodeOrderIndex < frameCount);
  const auto numGOP                   = static_cast<int32_t>(GOPList.size());
  const auto numLastGOP               = (frameCount - 1) % numGOP;
  const auto frameCountWithoutLastGOP = frameCount - numLastGOP;
  const auto groupIndex               = (decodeOrderIndex - 1) / numGOP;
  const auto frameOffset              = numGOP * groupIndex;
  int32_t    pictureIndex             = 0;
  if (decodeOrderIndex < frameCountWithoutLastGOP) {
    pictureIndex = (decodeOrderIndex - 1) % numGOP;
  } else {
    std::vector<int32_t> lastGOPIndices;
    lastGOPIndices.reserve(numLastGOP);
    for (int32_t i = 0; i < numGOP; ++i) {
      const auto& GOP   = GOPList[i];
      const auto  frame = frameOffset + GOP.POC;
      if (frame < frameCount) { lastGOPIndices.push_back(i); }
    }
    pictureIndex = lastGOPIndices[decodeOrderIndex - frameCountWithoutLastGOP];
  }
  const auto&   GOP              = GOPList[pictureIndex];
  const int32_t frmOrderCnt      = frameOffset + GOP.POC;
  const uint8_t temporalyIdPlus1 = GOP.temporalId + 1;
  return {frmOrderCnt, temporalyIdPlus1};
}

int32_t
decodeOrderToFrameOrderBasemesh(int32_t decodeOrderIndex,
                                int32_t frameCount,
                                const std::vector<BaseMeshGOPEntry>& GOPList) {
  int32_t frmOrderCnt      = 0;
  uint8_t temporalyIdPlus1 = 1;
  std::tie(frmOrderCnt, temporalyIdPlus1) =
    syntaxForTemporalScalabilityBasemesh(
      decodeOrderIndex, frameCount, GOPList);
  return frmOrderCnt;
}

int32_t
referenceFrameIndexFromFrameOrderBasemesh(
  int32_t                              frameOrderIndex,
  int32_t                              frameCount,
  const std::vector<BaseMeshGOPEntry>& GOPList) {
  assert(frameOrderIndex > 0);
  if (GOPList.empty()) { return frameOrderIndex - 1; }
  if (GOPList.size() == 4) {
    return referenceFrameIndexFromFrameOrderBasemeshGOPSize4(
      frameOrderIndex, frameCount, GOPList);
  }
  const auto numGOP              = static_cast<int32_t>(GOPList.size());
  const auto numLastGOP          = (frameCount - 1) % numGOP;
  int32_t    referenceFrameIndex = frameCount;
  // avoid Infinite loop
  const auto maxSearchNum = numGOP + 1;
  for (int32_t i = 0; i < maxSearchNum; ++i) {
    auto POC = frameOrderIndex % numGOP;
    if (POC == 0) { POC = numGOP; }
    const auto pGOP =
      std::find_if(GOPList.begin(), GOPList.end(), [&](const auto& GOP) {
        return GOP.POC == POC;
      });
    int32_t ref_poc     = pGOP->referencePics[0];
    referenceFrameIndex = frameOrderIndex + ref_poc;
    if (referenceFrameIndex < frameCount) { return referenceFrameIndex; }
    frameOrderIndex = referenceFrameIndex;
  }
  std::cerr << "Error: referenceFrameIndexFromFrameOrder can't find reference "
               "frame index."
            << std::endl;
  return referenceFrameIndex;
}

void
adjustTextureCoordinates(TriangleMesh<MeshType>& submesh,
                         int32_t                 subTextureWidth,
                         int32_t                 subTextureHeight,
                         int32_t                 subTextureLTx,
                         int32_t                 subTextureLTy,
                         int32_t                 textureVideoWidth,
                         int32_t                 textureVideoHeight) {
  Vec2<double> subTextureLT;
  Vec2<double> resolutionRatio;
  resolutionRatio[0] =
    (double)(subTextureWidth - 1) / (double)(textureVideoWidth - 1);
  resolutionRatio[1] =
    (double)(subTextureHeight - 1) / (double)(textureVideoHeight - 1);
  subTextureLT[0] = (double)subTextureLTx / (double)(textureVideoWidth - 1);
  subTextureLT[1] =
    (double)(textureVideoHeight - subTextureHeight - subTextureLTy)
    / (double)(textureVideoHeight - 1);
  const auto texCoordCount = submesh.texCoordCount();
  for (int32_t i = 0; i < texCoordCount; ++i) {
    Vec2<MeshType> coord = submesh.texCoord(i);
    coord[0]             = coord[0] * resolutionRatio[0];
    coord[1]             = coord[1] * resolutionRatio[1];
    coord[0] += subTextureLT[0];
    coord[1] += subTextureLT[1];
    submesh.setTexCoord(i, coord);
  }
}

// ZIPPERING FUNCTIONS
void
zippering_find_borders(std::vector<TriangleMesh<MeshType>>& submeshes,
                       std::vector<std::vector<int8_t>>&    isBoundaryVertex,
                       std::vector<size_t>&                 numBoundaries,
                       TriangleMesh<MeshType>&              boundaryVertices) {
#if ZIPPERING_TIMING
  const auto start = std::chrono::steady_clock::now();
#endif
  auto const submeshCount = submeshes.size();
  isBoundaryVertex.resize(submeshCount);
  numBoundaries.resize(submeshCount, 0);
  // find border vertices and create a point cloud with the boundary vertices
  for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    auto&      submesh       = submeshes[submeshIdx];
    const auto pointCount    = submesh.pointCount();
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
#if 0
                boundaryVertices.addColour(submeshIdx / 2, (submeshIdx % 2), 1);
#endif
        numBoundaries[submeshIdx]++;
      }
    }
  }

#if ZIPPERING_TIMING
  auto end = std::chrono::steady_clock::now();
  auto delta =
    std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "zippering_find_borders_per_LoD time: " << delta.count()
            << " ms\n";
#endif
}

void
zippering_find_matches_distance_matrix(
  std::vector<TriangleMesh<MeshType>>& submeshes,
  TriangleMesh<MeshType>&              boundaryVertices,
  std::vector<size_t>&                 numBoundaries,
  std::vector<std::vector<int64_t>>&
    zipperingDistanceBorderPoint,  //[submesh][borderpoint] - input
  std::vector<std::vector<Vec2<size_t>>>&
    zipperingMatchedBorderPoint  // [submesh][borderpoint] - output
) {
#if ZIPPERING_TIMING
  const auto start = std::chrono::steady_clock::now();
#endif
  auto const submeshCount = submeshes.size();
  // find upper and lower limits for the vertex positions to identify the submeshes
  std::vector<size_t> upperLimit(submeshCount, 0);
  std::vector<size_t> lowerLimit(submeshCount, 0);
  std::vector<int>    submeshID(boundaryVertices.pointCount(), 0);
  lowerLimit[0] = 0;
  upperLimit[0] = numBoundaries[0];
  for (size_t submeshIdx = 1; submeshIdx < submeshCount; submeshIdx++) {
    lowerLimit[submeshIdx] = upperLimit[submeshIdx - 1];
    upperLimit[submeshIdx] =
      numBoundaries[submeshIdx] + upperLimit[submeshIdx - 1];
    for (int i = lowerLimit[submeshIdx]; i < upperLimit[submeshIdx]; i++)
      submeshID[i] = submeshIdx;
  }
  int               pointIdx = -1;
  std::vector<bool> matchedVertex(boundaryVertices.pointCount(), false);
  for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    for (int i = 0; i < numBoundaries[submeshIdx]; i++) {
      pointIdx++;
      if (!matchedVertex[pointIdx]) {
        // initialize unmatched
        zipperingMatchedBorderPoint[submeshIdx][i] =
          Vec2<size_t>(submeshCount, 0);
        // find all nearest points from other submeshes
        MeshType minDist = zipperingDistanceBorderPoint[submeshIdx][i];
        for (int pointIdxMatched = 0;
             pointIdxMatched < boundaryVertices.pointCount();
             pointIdxMatched++) {
          // skip all the points that are from the same submesh
          if (pointIdxMatched >= lowerLimit[submeshIdx]
              && pointIdxMatched < upperLimit[submeshIdx])
            continue;
          int  submeshIdxMatched = submeshID[pointIdxMatched];
          auto distance          = std::ceil((boundaryVertices.point(pointIdx)
                                     - boundaryVertices.point(pointIdxMatched))
                                      .norm2());
          if (distance < minDist) {
            minDist = distance;
            // matching with the closest vertex from a different sub-mesh --> this may not be the best solution????
            zipperingMatchedBorderPoint[submeshIdx][i] =
              Vec2<size_t>(submeshIdxMatched,
                           pointIdxMatched - lowerLimit[submeshIdxMatched]);
          }
        }
        if (zipperingMatchedBorderPoint[submeshIdx][i].x() != submeshCount) {
          // found a match
          matchedVertex[pointIdx] = 1;
          // the structure contains the match
          auto submeshIdxMatched =
            zipperingMatchedBorderPoint[submeshIdx][i].x();
          auto pointIdxMatched = zipperingMatchedBorderPoint[submeshIdx][i].y()
                                 + lowerLimit[submeshIdxMatched];
          // now check if the matched index is already matched with some previous border vertex
          if (matchedVertex[pointIdxMatched] == 0) {
            zipperingMatchedBorderPoint[submeshIdxMatched]
                                       [pointIdxMatched
                                        - lowerLimit[submeshIdxMatched]] =
                                         Vec2<size_t>(submeshIdx, i);
            matchedVertex[pointIdxMatched] = 1;
          }
        }
      }  // end of boundary search
    }
  }
#if ZIPPERING_TIMING
  auto end = std::chrono::steady_clock::now();
  auto delta =
    std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "zippering_find_matches_distance_matrix time: " << delta.count()
            << " ms\n";
#endif
}
void
zippering_find_matches_distance_per_submesh_pair(
  std::vector<TriangleMesh<MeshType>>& submeshes,
  TriangleMesh<MeshType>&              boundaryVertices,
  std::vector<size_t>&                 numBoundaries,
  std::vector<std::vector<int64_t>>&
    zipperingDistancePerSubmeshPair,  //[submesh][borderpoint] - input
  std::vector<std::vector<Vec2<size_t>>>&
       zipperingMatchedBorderPoint,  // [submesh][borderpoint] - output
  bool zipperingLinearSegmentation) {
#if ZIPPERING_TIMING
  const auto start = std::chrono::steady_clock::now();
#endif
  auto const submeshCount = submeshes.size();
  // find upper and lower limits for the vertex positions to identify the submeshes
  std::vector<size_t> upperLimit(submeshCount, 0);
  std::vector<size_t> lowerLimit(submeshCount, 0);
  std::vector<int>    submeshID(boundaryVertices.pointCount(), 0);
  lowerLimit[0] = 0;
  upperLimit[0] = numBoundaries[0];
  for (size_t submeshIdx = 1; submeshIdx < submeshCount; submeshIdx++) {
    lowerLimit[submeshIdx] = upperLimit[submeshIdx - 1];
    upperLimit[submeshIdx] =
      numBoundaries[submeshIdx] + upperLimit[submeshIdx - 1];
    for (int i = lowerLimit[submeshIdx]; i < upperLimit[submeshIdx]; i++)
      submeshID[i] = submeshIdx;
  }
  int               pointIdx = -1;
  std::vector<bool> matchedVertex(boundaryVertices.pointCount(), false);
  for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    for (int i = 0; i < numBoundaries[submeshIdx]; i++) {
      pointIdx++;
      if (!matchedVertex[pointIdx]) {
        // initialize unmatched
        zipperingMatchedBorderPoint[submeshIdx][i] =
          Vec2<size_t>(submeshCount, 0);
        MeshType minDistTrue = std::ceil(
          (boundaryVertices.point(0)
           - boundaryVertices.point(boundaryVertices.pointCount() - 1))
            .norm2());  //TODO: replace it diagonal BBox
        Vec2<size_t> zipperingMatchedBorderPointTrue =
          Vec2<size_t>(submeshCount, 0);
        for (size_t nIdx = 0; nIdx < submeshCount; nIdx++) {
          if (zipperingLinearSegmentation
              && std::abs(static_cast<int>(submeshIdx)
                          - static_cast<int>(nIdx))
                   != 1) {
            continue;
          } else if (!zipperingLinearSegmentation
                     && static_cast<int>(submeshIdx)
                          == static_cast<int>(nIdx)) {
            continue;
          }
          int minId = 0, maxId = 0;
          if (static_cast<int>(submeshIdx) > static_cast<int>(nIdx)) {
            minId = static_cast<int>(nIdx);
            maxId = static_cast<int>(submeshIdx);
          } else {
            minId = static_cast<int>(submeshIdx);
            maxId = static_cast<int>(nIdx);
          }
          MeshType minDist =
            zipperingDistancePerSubmeshPair[minId][maxId - minId - 1];
          for (int pointIdxMatched = lowerLimit[nIdx];
               pointIdxMatched < upperLimit[nIdx];
               pointIdxMatched++) {
            auto distance =
              std::ceil((boundaryVertices.point(pointIdx)
                         - boundaryVertices.point(pointIdxMatched))
                          .norm2());
            if (distance < minDist) {
              minDist = distance;
              zipperingMatchedBorderPoint[submeshIdx][i] =
                Vec2<size_t>(nIdx, pointIdxMatched - lowerLimit[nIdx]);
            }
          }
          if (minDist < minDistTrue && minDist != 0) {
            minDistTrue = minDist;
            zipperingMatchedBorderPointTrue =
              zipperingMatchedBorderPoint[submeshIdx][i];
          }
        }
        zipperingMatchedBorderPoint[submeshIdx][i] =
          zipperingMatchedBorderPointTrue;
        if (zipperingMatchedBorderPoint[submeshIdx][i].x() != submeshCount) {
          // found a match
          matchedVertex[pointIdx] = 1;
          // the structure contains the match
          auto submeshIdxMatched =
            zipperingMatchedBorderPoint[submeshIdx][i].x();
          auto pointIdxMatched = zipperingMatchedBorderPoint[submeshIdx][i].y()
                                 + lowerLimit[submeshIdxMatched];
          // now check if the matched index is already matched with some previous border vertex
          if (matchedVertex[pointIdxMatched] == 0) {
            zipperingMatchedBorderPoint[submeshIdxMatched]
                                       [pointIdxMatched
                                        - lowerLimit[submeshIdxMatched]] =
                                         Vec2<size_t>(submeshIdx, i);
            matchedVertex[pointIdxMatched] = 1;
          }
        }
      }
    }
  }

#if ZIPPERING_TIMING
  auto end = std::chrono::steady_clock::now();
  auto delta =
    std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "zippering_find_matches_distance_per_submesh_pair time: "
            << delta.count() << " ms\n";
#endif
}
void
zippering_fuse_border(
  std::vector<TriangleMesh<MeshType>>&    submeshes,
  std::vector<std::vector<int8_t>>&       isBoundaryVertex,
  std::vector<std::vector<Vec2<size_t>>>& zipperingMatchedBorderPoint,
  const std::vector<std::vector<int32_t>> frameSubmeshLodMaps,
  const int                               zipperingMethodForUnmatchedLoDs) {
#if ZIPPERING_TIMING
  const auto start = std::chrono::steady_clock::now();
#endif
  auto                              submeshCount = submeshes.size();
  std::vector<std::vector<int32_t>> submeshAdjacency;
  submeshAdjacency.resize(submeshCount);
  // initializing the flag structure
  std::vector<std::vector<bool>> matchedBorderPointFlag;
  matchedBorderPointFlag.resize(submeshCount);
  for (int subIdx = 0; subIdx < submeshCount; subIdx++) {
    auto numBorders = zipperingMatchedBorderPoint[subIdx].size();
    matchedBorderPointFlag[subIdx].resize(numBorders, false);
    submeshAdjacency[subIdx].reserve(submeshCount - 1);
  }
  for (int subIdx = 0; subIdx < submeshCount; subIdx++) {
    auto& curSubmesh = submeshes[subIdx];
    auto  numBorders = zipperingMatchedBorderPoint[subIdx].size();
    if (numBorders == 0) continue;
    for (int borderIdx = 0; borderIdx < numBorders; borderIdx++) {
      if (matchedBorderPointFlag[subIdx][borderIdx]) {
        // border has been updated already
        continue;
      } else {
        // converting from border index to mesh vertex index
        int  curVertexIdx      = 0;
        int  borderVertexCount = 0;
        bool borderFoundFlag   = false;
        while (curVertexIdx < curSubmesh.pointCount()) {
          if (isBoundaryVertex[subIdx][curVertexIdx])
            if (borderVertexCount == borderIdx) {
              borderFoundFlag = true;
              break;
            } else {
              borderVertexCount++;
            }
          curVertexIdx++;
        }
        if (!borderFoundFlag) { continue; }
        auto& borderPoint = curSubmesh.point(curVertexIdx);
        int   matchedSubmeshIdx =
          zipperingMatchedBorderPoint[subIdx][borderIdx][0];
        int matchedBorderIdx =
          zipperingMatchedBorderPoint[subIdx][borderIdx][1];
        if (matchedSubmeshIdx == submeshCount)
          continue;  // unmatched border vertex
        auto& matchedSubmesh = submeshes[matchedSubmeshIdx];
        // converting from border index to vertex index
        int matchedVertexIdx = 0;
        borderVertexCount    = 0;
        while (matchedVertexIdx < matchedSubmesh.pointCount()) {
          if (isBoundaryVertex[matchedSubmeshIdx][matchedVertexIdx])
            if (borderVertexCount == matchedBorderIdx) {
              break;
            } else {
              borderVertexCount++;
            }
          matchedVertexIdx++;
        }
        auto& matchedPoint = matchedSubmesh.point(matchedVertexIdx);
        //Store matched submeshes adjacency information
        auto it = std::find(submeshAdjacency[subIdx].begin(),
                            submeshAdjacency[subIdx].end(),
                            matchedSubmeshIdx);
        if (it == submeshAdjacency[subIdx].end())
          submeshAdjacency[subIdx].push_back(matchedSubmeshIdx);
        auto it_matched =
          std::find(submeshAdjacency[matchedSubmeshIdx].begin(),
                    submeshAdjacency[matchedSubmeshIdx].end(),
                    subIdx);
        if (it_matched == submeshAdjacency[matchedSubmeshIdx].end())
          submeshAdjacency[matchedSubmeshIdx].push_back(subIdx);

        if (matchedBorderPointFlag[matchedSubmeshIdx][matchedBorderIdx]) {
          // this border point has been matched before, so just adjust the new border point
          borderPoint = matchedPoint;
        } else {
          matchedBorderPointFlag[subIdx][borderIdx]                   = true;
          matchedBorderPointFlag[matchedSubmeshIdx][matchedBorderIdx] = true;
          //average the points -> this is only always averaging two points, what if we have more matches???
          auto averagePoint = (borderPoint + matchedPoint) / 2;
          borderPoint       = averagePoint;
          matchedPoint      = averagePoint;
        }
      }
    }
  }
  //The code to handle unmatched LoDs, if any
  if (zipperingMethodForUnmatchedLoDs == 1
      || zipperingMethodForUnmatchedLoDs == 2) {
    std::vector<StaticAdjacencyInformation<int32_t>> vertexToTriangle(
      submeshCount);
    for (int i = 0; i < submeshCount; i++) {
      ComputeVertexToTriangle(submeshes[i].triangles(),
                              submeshes[i].pointCount(),
                              vertexToTriangle[i]);
      vertexToTriangle[i].reserve(vertexToTriangle[i].size()
                                  + zipperingMatchedBorderPoint[i].size());
    }
    for (int subIdx = 0; subIdx < submeshCount; subIdx++) {
      for (int j = 0; j < submeshAdjacency[subIdx].size(); j++) {
        auto lodDiff =
          frameSubmeshLodMaps[subIdx].back()
          - frameSubmeshLodMaps[submeshAdjacency[subIdx][j]].back();
        auto unmatchedLODVertices = std::distance(
          frameSubmeshLodMaps[subIdx].begin(),
          std::find(frameSubmeshLodMaps[subIdx].begin(),
                    frameSubmeshLodMaps[subIdx].end(),
                    frameSubmeshLodMaps[subIdx].back() - lodDiff + 1));
        if (lodDiff > 0
            && zipperingMethodForUnmatchedLoDs
                 == 1) {  //Remove border vertices from unmatched LoDs
          for (int i = frameSubmeshLodMaps[subIdx].size() - 1;
               i >= unmatchedLODVertices;
               i--) {
            if (isBoundaryVertex[subIdx][i]) {
              if (matchedBorderPointFlag[subIdx][std::count(
                    isBoundaryVertex[subIdx].begin(),
                    isBoundaryVertex[subIdx].begin() + i,
                    1)])
                continue;
              std::vector<int32_t> tadj;
              std::vector<int8_t>  ttags(submeshes[subIdx].triangleCount());
              int32_t              boundaryNeighbourVertexIdx = 0,
                      boundaryNeighbourIdxWithinTri           = 0,
                      currentIdxWithinTri                     = 0;
              const auto& neighbours = vertexToTriangle[subIdx].neighbours();
              const auto  start =
                vertexToTriangle[subIdx].neighboursStartIndex(i);
              const auto end = vertexToTriangle[subIdx].neighboursEndIndex(i);
              for (int nIdx = start; nIdx < end; ++nIdx) {
                const auto& triangle =
                  submeshes[subIdx].triangles()[neighbours[nIdx]];
                for (boundaryNeighbourIdxWithinTri = 0;
                     boundaryNeighbourIdxWithinTri < 3;
                     boundaryNeighbourIdxWithinTri++) {
                  if (triangle[boundaryNeighbourIdxWithinTri] != i
                      && isBoundaryVertex
                        [subIdx][triangle[boundaryNeighbourIdxWithinTri]]) {
                    ComputeEdgeAdjacentTriangles(
                      i,
                      triangle[boundaryNeighbourIdxWithinTri],
                      vertexToTriangle[subIdx],
                      ttags,
                      tadj);
                    if (tadj.size() == 1) {
                      boundaryNeighbourVertexIdx =
                        triangle[boundaryNeighbourIdxWithinTri];
                      nIdx = end;
                      if (triangle[(boundaryNeighbourIdxWithinTri + 1) % 3]
                          == i)
                        currentIdxWithinTri =
                          (boundaryNeighbourIdxWithinTri + 1) % 3;
                      else
                        currentIdxWithinTri =
                          (boundaryNeighbourIdxWithinTri + 2) % 3;
                      break;
                    }
                  }
                }
              }
              submeshes[subIdx].setPoint(
                i, submeshes[subIdx].points()[boundaryNeighbourVertexIdx]);
              submeshes[subIdx].setTexCoord(
                submeshes[subIdx]
                  .texCoordTriangles()[tadj[0]][currentIdxWithinTri],
                submeshes[subIdx]
                  .texCoords()[submeshes[subIdx].texCoordTriangles()
                                 [tadj[0]][boundaryNeighbourIdxWithinTri]]);
              submeshes[subIdx].setTriangle(tadj[0], 0, 0, 0);
              submeshes[subIdx].setTexCoordTriangle(tadj[0], 0, 0, 0);
            }
          }
          RemoveDegeneratedTriangles(submeshes[subIdx]);
        } else if (
          lodDiff > 0
          && zipperingMethodForUnmatchedLoDs
               == 2) {  //Insert new vertices for border vertices from unmatched LoDs
          for (int i = unmatchedLODVertices;
               i <= frameSubmeshLodMaps[subIdx].size() - 1;
               i++) {
            if (isBoundaryVertex[subIdx][i]) {
              if (matchedBorderPointFlag[subIdx][std::count(
                    isBoundaryVertex[subIdx].begin(),
                    isBoundaryVertex[subIdx].begin() + i,
                    1)])
                continue;
              std::vector<int32_t> vadj;
              std::vector<int8_t>  vtags(submeshes[subIdx].pointCount());
              std::vector<int8_t>  ttags(submeshes[subIdx].triangleCount());
              ComputeAdjacentVertices(i,
                                      submeshes[subIdx].triangles(),
                                      vertexToTriangle[subIdx],
                                      vtags,
                                      vadj);
              std::vector<int32_t> borderIndices;
              for (int vit = 0; vit < vadj.size(); vit++) {
                if (isBoundaryVertex[subIdx][vadj[vit]]) {
                  if (ComputeEdgeAdjacentTriangleCount(
                        i, vadj[vit], vertexToTriangle[subIdx], ttags)
                      == 1) {
                    borderIndices.push_back(
                      std::count(isBoundaryVertex[subIdx].begin(),
                                 isBoundaryVertex[subIdx].begin() + vadj[vit],
                                 1));
                  }
                }
              }
              int v1matchedSubmeshIdx =
                zipperingMatchedBorderPoint[subIdx][borderIndices[0]][0];
              int v1matchedBorderIdx =
                zipperingMatchedBorderPoint[subIdx][borderIndices[0]][1];
              int v2matchedSubmeshIdx =
                zipperingMatchedBorderPoint[subIdx][borderIndices[1]][0];
              int v2matchedBorderIdx =
                zipperingMatchedBorderPoint[subIdx][borderIndices[1]][1];
              if (v1matchedSubmeshIdx == v2matchedSubmeshIdx
                  && v1matchedSubmeshIdx == submeshAdjacency[subIdx][j]) {
                std::vector<int32_t> tadj;
                // converting from border index to vertex index
                int   v1matchedVertexIdx = 0, v2matchedVertexIdx = 0;
                int   borderVertexCount = 0;
                auto& matchedSubmesh    = submeshes[v1matchedSubmeshIdx];
                while (v1matchedVertexIdx < matchedSubmesh.pointCount()) {
                  if (isBoundaryVertex[v1matchedSubmeshIdx]
                                      [v1matchedVertexIdx])
                    if (borderVertexCount == v1matchedBorderIdx) {
                      break;
                    } else {
                      borderVertexCount++;
                    }
                  v1matchedVertexIdx++;
                }
                borderVertexCount = 0;
                while (v2matchedVertexIdx < matchedSubmesh.pointCount()) {
                  if (isBoundaryVertex[v1matchedSubmeshIdx]
                                      [v2matchedVertexIdx])
                    if (borderVertexCount == v2matchedBorderIdx) {
                      break;
                    } else {
                      borderVertexCount++;
                    }
                  v2matchedVertexIdx++;
                }
                //**********************************************************
                tadj.clear();
                ttags.clear();
                auto& vertexToTriangleMatched =
                  vertexToTriangle[v1matchedSubmeshIdx];
                ComputeEdgeAdjacentTriangles(v1matchedVertexIdx,
                                             v2matchedVertexIdx,
                                             vertexToTriangleMatched,
                                             ttags,
                                             tadj);
                if (tadj.size() == 1) {
                  int32_t uncommonVertex = 0, idx = 0;
                  for (idx = 0; idx < 3; idx++) {
                    if (matchedSubmesh.triangles()[tadj[0]][idx]
                          != v1matchedVertexIdx
                        && matchedSubmesh.triangles()[tadj[0]][idx]
                             != v2matchedVertexIdx) {
                      uncommonVertex =
                        matchedSubmesh.triangles()[tadj[0]][idx];
                      break;
                    }
                  }
                  matchedSubmesh.addPoint(
                    (submeshes[subIdx].points()[i]
                     + (matchedSubmesh.points()
                          [matchedSubmesh.triangles()[tadj[0]][(idx + 1) % 3]]
                        + matchedSubmesh
                            .points()[matchedSubmesh
                                        .triangles()[tadj[0]][(idx + 2) % 3]])
                         / 2)
                    / 2);
                  //matchedSubmesh.addPoint((matchedSubmesh.points()[matchedSubmesh.triangles()[tadj[0]][(idx + 1) % 3]] + matchedSubmesh.points()[matchedSubmesh.triangles()[tadj[0]][(idx + 2) % 3]])/2);
                  submeshes[subIdx].setPoint(
                    i,
                    matchedSubmesh.points()[matchedSubmesh.pointCount() - 1]);
                  matchedSubmesh.addTriangle(
                    uncommonVertex,
                    matchedSubmesh.pointCount() - 1,
                    matchedSubmesh.triangles()[tadj[0]][(idx + 2) % 3]);

                  matchedSubmesh.triangles()[tadj[0]] = Triangle(
                    uncommonVertex,
                    matchedSubmesh.triangles()[tadj[0]][(idx + 1) % 3],
                    matchedSubmesh.pointCount() - 1);
                  vertexToTriangleMatched.incrementNeighbourCount(
                    uncommonVertex);
                  vertexToTriangleMatched.addNeighbour(
                    uncommonVertex, matchedSubmesh.triangleCount() - 1);
                  const auto start =
                    vertexToTriangleMatched.neighboursStartIndex((idx + 2)
                                                                 % 3);
                  const auto end =
                    vertexToTriangleMatched.neighboursEndIndex((idx + 2) % 3);
                  for (int nIdx = start; nIdx < end; ++nIdx) {
                    if (vertexToTriangleMatched.neighbours()[nIdx] == tadj[0])
                      vertexToTriangleMatched.updateNeighbour(
                        nIdx, matchedSubmesh.triangleCount() - 1);
                  }
                  vertexToTriangleMatched.incrementSize();
                  vertexToTriangleMatched.incrementNeighbourCount(
                    matchedSubmesh.pointCount() - 1, 2);
                  vertexToTriangleMatched.addNeighbour(
                    matchedSubmesh.pointCount() - 1, tadj[0]);
                  vertexToTriangleMatched.addNeighbour(
                    matchedSubmesh.pointCount() - 1,
                    matchedSubmesh.triangleCount() - 1);

                  auto v1TexCoord =
                    matchedSubmesh
                      .texCoords()[matchedSubmesh.texCoordTriangles()
                                     [tadj[0]][(idx + 1) % 3]];
                  auto v2TexCoord =
                    matchedSubmesh
                      .texCoords()[matchedSubmesh.texCoordTriangles()
                                     [tadj[0]][(idx + 2) % 3]];
                  auto midTexCoord = (v1TexCoord + v2TexCoord) / 2;
                  matchedSubmesh.addTexCoord(midTexCoord);
                  matchedSubmesh.addTexCoordTriangle(
                    matchedSubmesh.texCoordTriangles()[tadj[0]][idx],
                    matchedSubmesh.texCoordCount() - 1,
                    matchedSubmesh
                      .texCoordTriangles()[tadj[0]][(idx + 2) % 3]);
                  matchedSubmesh.texCoordTriangles()[tadj[0]] = Triangle(
                    matchedSubmesh.texCoordTriangles()[tadj[0]][idx],
                    matchedSubmesh.texCoordTriangles()[tadj[0]][(idx + 1) % 3],
                    matchedSubmesh.texCoordCount() - 1);
                }
              }
            }
          }
        }
      }
    }
  }
#if ZIPPERING_TIMING
  auto end = std::chrono::steady_clock::now();
  auto delta =
    std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "zippering_fuse_border time: " << delta.count() << " ms\n";
#endif
}

void
boundarygrow_adjacent(
  std::vector<TriangleMesh<MeshType>>& submeshes,
  std::vector<std::vector<int8_t>>&    isBoundaryVertex,
  StaticAdjacencyInformation<int32_t>  vertexToTriangleOutput,
  std::vector<bool>&                   status,
  std::vector<int>&                    Vlist,
  int                                  vindex,
  int                                  submeshIdx) {
  bool needtraverse = true;
  while (needtraverse) {
    std::vector<int8_t>  vtags_next(submeshes[submeshIdx].pointCount());
    std::vector<int32_t> vadj_next;
    std::vector<int8_t>  ttags(submeshes[submeshIdx].triangleCount());
    bool                 vindex_flag = false;
    ComputeAdjacentVertices(vindex,
                            submeshes[submeshIdx].triangles(),
                            vertexToTriangleOutput,
                            vtags_next,
                            vadj_next);
    for (int vindexnew = 0; vindexnew < vadj_next.size(); vindexnew++) {
      if (ComputeEdgeAdjacentTriangleCount(
            vindex, vadj_next[vindexnew], vertexToTriangleOutput, ttags)
          == 1) {
        ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
        if (isBoundaryVertex[submeshIdx][vadj_next[vindexnew]]) {
          if (status[vadj_next[vindexnew]] == false) {
            Vlist.push_back(vadj_next[vindexnew]);
            status[vadj_next[vindexnew]] = true;
            vindex                       = vadj_next[vindexnew];
            vindex_flag                  = true;
          }
        }
        if (vindex_flag) break;
      }
    }
    if (!vindex_flag) { needtraverse = false; }
  }
}

void
boundarygrow_computeboundary(
  std::vector<TriangleMesh<MeshType>>&        submeshes,
  std::vector<std::vector<int8_t>>&           isBoundaryVertex,
  std::vector<std::vector<std::vector<int>>>& Vlist,
  std::vector<std::vector<std::vector<int>>>& Tlist,
  std::vector<std::vector<std::vector<int>>>& Vlist_ref) {
  auto                          submeshCount = submeshes.size();
  std::vector<std::vector<int>> Vlist_judge  = Vlist_ref[1];
  for (int i = 0; i < submeshCount; i++) {
    int                                 vindex, vindex_next;
    StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
    std::vector<int8_t>                 ttags(submeshes[i].triangleCount());
    std::vector<bool> status(isBoundaryVertex[i].size(), false);
    ComputeVertexToTriangle(submeshes[i].triangles(),
                            submeshes[i].pointCount(),
                            vertexToTriangleOutput);
    bool             needtraverse = true;
    std::vector<int> Vlist_temp;
    if (Vlist_judge.size() == 0) {
      if (i == 0) { continue; }
      Box3<MeshType> boundingBox0 = submeshes[i].boundingBox();
      auto           mindistance  = 100000;
      for (int vi = 0; vi < isBoundaryVertex[i].size(); vi++) {
        if (isBoundaryVertex[i][vi]) {
          auto distance =
            std::abs(submeshes[i].point(vi)[1] - boundingBox0.max[1]);
          if (distance < mindistance) {
            mindistance = distance;
            vindex      = vi;
          }
        }
      }
      Vlist_temp.push_back(vindex);
      status[vindex] = true;
    } else {
      if (i == submeshCount - 1) { continue; }
      int    firstIndex = Vlist_ref[i + 1][0][0];
      int    nextIndex  = Vlist_ref[i + 1][0][1];
      auto   v0         = submeshes[i + 1].point(firstIndex);
      auto   v1         = submeshes[i + 1].point(nextIndex);
      double mindist1   = 100000;
      for (int k = 0; k < isBoundaryVertex[i].size(); k++) {
        if (isBoundaryVertex[i][k]) {
          double dist1 = (v0 - submeshes[i].point(k)).norm();
          if (dist1 < mindist1) {
            mindist1 = dist1;
            vindex   = k;
          }
        }
      }
      Vlist_temp.push_back(vindex);
      status[vindex] = true;
      std::vector<int8_t>  vtags(submeshes[i].pointCount());
      std::vector<int32_t> vadj;
      ComputeAdjacentVertices(
        vindex, submeshes[i].triangles(), vertexToTriangleOutput, vtags, vadj);
      double mindist2 = 100000;
      for (int k = 0; k < vadj.size(); k++) {
        if (ComputeEdgeAdjacentTriangleCount(
              vindex, vadj[k], vertexToTriangleOutput, ttags)
            == 1) {
          auto dist2 = (v1 - submeshes[i].point(vadj[k])).norm();
          if (dist2 < mindist2) {
            mindist2    = dist2;
            vindex_next = vadj[k];
          }
        }
      }
      Vlist_temp.push_back(vindex_next);
      status[vindex_next] = true;
      vindex              = vindex_next;
    }
    boundarygrow_adjacent(submeshes,
                          isBoundaryVertex,
                          vertexToTriangleOutput,
                          status,
                          Vlist_temp,
                          vindex,
                          i);
    Vlist[i].push_back(Vlist_temp);
    int boundaryCount = 0;
    int judgeCount;
    for (int c = 0; c < submeshes[i].pointCount(); c++) {
      if (isBoundaryVertex[i][c]) { boundaryCount++; }
    }
    if (i == 0 || i == submeshCount - 1) {
      judgeCount = boundaryCount / 2;
    } else {
      judgeCount = boundaryCount / 4;
    }
    if (Vlist_temp.size() < judgeCount) {
      auto   vj = submeshes[i].point(Vlist_temp[Vlist_temp.size() - 1]);
      double mindist_judge = 100000;
      int    newv;
      for (int v = 0; v < isBoundaryVertex[i].size(); v++) {
        if (isBoundaryVertex[i][v]) {
          if (status[v] == false) {
            auto   v0         = submeshes[i].point(v);
            double dist_judge = (vj - v0).norm();
            if (dist_judge < mindist_judge) {
              mindist_judge = dist_judge;
              newv          = v;
            }
          }
        }
      }
      Vlist_temp.clear();
      Vlist_temp.push_back(newv);
      status[newv] = true;
      boundarygrow_adjacent(submeshes,
                            isBoundaryVertex,
                            vertexToTriangleOutput,
                            status,
                            Vlist_temp,
                            newv,
                            i);
      Vlist[i].push_back(Vlist_temp);
    }
    for (int c = 0; c < Vlist[i].size(); c++) {
      std::vector<int32_t> Tlist_temp;
      for (int v = 0; v < Vlist[i][c].size(); v++) {
        std::vector<int32_t> tadj;
        ComputeEdgeAdjacentTriangles(Vlist[i][c][v],
                                     Vlist[i][c][(v + 1) % Vlist[i][c].size()],
                                     vertexToTriangleOutput,
                                     ttags,
                                     tadj);
        ttags.resize(submeshes[i].triangleCount(), 0);
        if (tadj.size() != 0) {
          Tlist_temp.push_back(tadj[0]);
        } else {
          Tlist_temp.push_back(Tlist_temp[Tlist_temp.size() - 1]);
        }
      }
      Tlist[i].push_back(Tlist_temp);
    }
  }
}

int
IndexTransform(std::vector<TriangleMesh<MeshType>>& submeshes,
               std::vector<int>                     Vlist,
               size_t                               submeshIdx) {
  auto                                submeshCount = submeshes.size();
  StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
  std::vector<int8_t> ttags(submeshes[submeshIdx].triangleCount());
  ComputeVertexToTriangle(submeshes[submeshIdx].triangles(),
                          submeshes[submeshIdx].pointCount(),
                          vertexToTriangleOutput);
  auto                 firstIndex = Vlist[0];
  auto                 nextIndex  = Vlist[1];
  std::vector<int8_t>  vtags(submeshes[submeshIdx].pointCount());
  std::vector<int32_t> vadj;
  std::vector<int>     boundaryIndex;
  ComputeAdjacentVertices(firstIndex,
                          submeshes[submeshIdx].triangles(),
                          vertexToTriangleOutput,
                          vtags,
                          vadj);
  for (int vindexnew = 0; vindexnew < vadj.size(); vindexnew++) {
    if (ComputeEdgeAdjacentTriangleCount(
          firstIndex, vadj[vindexnew], vertexToTriangleOutput, ttags)
        == 1) {
      boundaryIndex.push_back(vadj[vindexnew]);
    }
    ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
  }
  std::sort(boundaryIndex.begin(), boundaryIndex.end());
  for (int i = 0; i < boundaryIndex.size(); i++) {
    if (boundaryIndex[i] == nextIndex) { return i; }
  }
}

int
TransformIndex(std::vector<TriangleMesh<MeshType>>& submeshes,
               size_t                               submeshIdx,
               StaticAdjacencyInformation<int32_t>  vertexToTriangleOutput,
               int                                  firstIndex,
               int                                  nextIndex_relative) {
  auto                 submeshCount = submeshes.size();
  std::vector<int8_t>  ttags(submeshes[submeshIdx].triangleCount());
  std::vector<int8_t>  vtags(submeshes[submeshIdx].pointCount());
  std::vector<int32_t> vadj;
  std::vector<int>     boundaryIndex;
  ComputeAdjacentVertices(firstIndex,
                          submeshes[submeshIdx].triangles(),
                          vertexToTriangleOutput,
                          vtags,
                          vadj);
  for (int vindexnew = 0; vindexnew < vadj.size(); vindexnew++) {
    if (ComputeEdgeAdjacentTriangleCount(
          firstIndex, vadj[vindexnew], vertexToTriangleOutput, ttags)
        == 1) {
      boundaryIndex.push_back(vadj[vindexnew]);
    }
    ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
  }
  std::sort(boundaryIndex.begin(), boundaryIndex.end());
  return boundaryIndex[nextIndex_relative];
}

template<typename T>
double
boundaryproject(const Vec3<T>& axis, const Vec3<T>& point) {
  return axis[0] * point[0] + axis[1] * point[1] + axis[2] * point[2];
}

template<typename T>
bool
boundaryIntersectPlane(TriangleMesh<T>& mesh1,
                       TriangleMesh<T>& mesh2,
                       const Triangle&  tri1,
                       const Triangle&  tri2) {
  auto vi = mesh1.point(tri1[0]);
  for (int i = 1; i < 3; i++) {
    if (mesh1.point(tri1[i])[1] > vi[1]) { vi = mesh1.point(tri1[i]); }
  }
  auto vj = mesh2.point(tri2[0]);
  for (int j = 1; j < 3; j++) {
    if (mesh2.point(tri2[j])[1] < vj[1]) { vj = mesh2.point(tri2[j]); }
  }
  if (vi[1] > vj[1]) return true;
  else return false;
}

int
boundaryIntersectType(TriangleMesh<MeshType>& mesh1,
                      TriangleMesh<MeshType>& mesh2,
                      const Triangle&         tri1,
                      const Triangle&         tri2) {
  if (boundaryIntersectPlane(mesh1, mesh2, tri1, tri2)) {
    return 1;
  } else {
    return 0;
  }
}

template<typename T>
double
crossProduct(const Vec2<T>& p1, const Vec2<T>& p2, const Vec2<T>& p3) {
  return (p2[0] - p1[0]) * (p3[1] - p1[1]) - (p2[1] - p1[1]) * (p3[0] - p1[0]);
}

template<typename T>
bool
onSegment_grow(const Vec2<T>& p, const Vec2<T>& q, const Vec2<T>& r) {
  return q[0] <= std::max(p[0], r[0]) && q[0] >= std::min(p[0], r[0])
         && q[1] <= std::max(p[1], r[1]) && q[1] >= std::min(p[1], r[1]);
}

template<typename T>
bool
pointOnTriangleEdge(TriangleMesh<MeshType>& mesh1,
                    const Vec2<T>&          pt,
                    const Triangle&         tri) {
  return (crossProduct(mesh1.texCoord(tri[0]), mesh1.texCoord(tri[1]), pt) == 0
          && onSegment_grow(
            mesh1.texCoord(tri[0]), pt, mesh1.texCoord(tri[1])))
         || (crossProduct(mesh1.texCoord(tri[1]), mesh1.texCoord(tri[2]), pt)
               == 0
             && onSegment_grow(
               mesh1.texCoord(tri[1]), pt, mesh1.texCoord(tri[2])))
         || (crossProduct(mesh1.texCoord(tri[2]), mesh1.texCoord(tri[0]), pt)
               == 0
             && onSegment_grow(
               mesh1.texCoord(tri[2]), pt, mesh1.texCoord(tri[0])));
}

template<typename T>
bool
pointOutsideTriangle(TriangleMesh<MeshType>& mesh1,
                     const Vec2<T>&          pt,
                     const Triangle&         tri) {
  return !pointInTriangle(mesh1, pt, tri)
         && !pointOnTriangleEdge(mesh1, pt, tri);
}

template<typename T>
bool
pointInTriangle(TriangleMesh<MeshType>& mesh1,
                const Vec2<T>&          pt,
                const Triangle&         tri) {
  double d1 = crossProduct(mesh1.texCoord(tri[0]), mesh1.texCoord(tri[1]), pt);
  double d2 = crossProduct(mesh1.texCoord(tri[1]), mesh1.texCoord(tri[2]), pt);
  double d3 = crossProduct(mesh1.texCoord(tri[2]), mesh1.texCoord(tri[0]), pt);

  bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

  return !(hasNeg && hasPos);
}

void
boundaryprocess(
  std::vector<TriangleMesh<MeshType>>&           submeshes,
  std::vector<std::vector<int8_t>>&              isBoundaryVertex,
  std::vector<std::vector<std::vector<int>>>&    growuplist_adjacent,
  std::vector<std::vector<std::vector<int>>>&    triangle_grow,
  std::vector<size_t>&                           crackCount,
  std::vector<std::vector<std::vector<size_t>>>& boundaryIndex,
  std::vector<std::vector<size_t>>&              crackIndex) {
  auto                                       submeshCount = submeshes.size();
  std::vector<std::vector<std::vector<int>>> growuplist, judgelist, Tlist_grow,
    Tlist_judge;
  growuplist.resize(submeshCount);
  judgelist.resize(submeshCount);
  Tlist_grow.resize(submeshCount);
  Tlist_judge.resize(submeshCount);
  growuplist_adjacent.resize(submeshCount);
  triangle_grow.resize(submeshCount);
  crackCount.resize(submeshCount, 0);
  crackIndex.resize(submeshCount);
  boundarygrow_computeboundary(submeshes,
                               isBoundaryVertex,
                               growuplist,
                               Tlist_grow,
                               growuplist);  //�����������ж�����ʼ��һ��
  boundarygrow_computeboundary(
    submeshes, isBoundaryVertex, judgelist, Tlist_judge, growuplist);
  for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    std::vector<size_t> Vlist_temp;
    if (growuplist[submeshIdx].size() != 0) {
      for (int c = 0; c < growuplist[submeshIdx].size(); c++) {
        Vlist_temp.push_back(growuplist[submeshIdx][c][0]);
        Vlist_temp.push_back(
          IndexTransform(submeshes, growuplist[submeshIdx][c], submeshIdx));
        Vlist_temp.push_back(submeshIdx - 1);
        boundaryIndex[submeshIdx].push_back(Vlist_temp);
        crackCount[submeshIdx]++;
        Vlist_temp.clear();
      }
    }
    if (judgelist[submeshIdx].size() != 0) {
      for (int c = 0; c < judgelist[submeshIdx].size(); c++) {
        Vlist_temp.push_back(judgelist[submeshIdx][c][0]);
        Vlist_temp.push_back(
          IndexTransform(submeshes, judgelist[submeshIdx][c], submeshIdx));
        Vlist_temp.push_back(submeshIdx);
        boundaryIndex[submeshIdx].push_back(Vlist_temp);
        crackCount[submeshIdx]++;
        Vlist_temp.clear();
      }
    }
  }
  for (size_t submeshIdx = 0; submeshIdx < submeshCount - 1; submeshIdx++) {
    std::vector<int> Vlist, Tlist;
    for (int c = 0; c < judgelist[submeshIdx].size(); c++) {
      for (int v = 0; v < judgelist[submeshIdx][c].size(); v++) {
        Vlist.push_back(judgelist[submeshIdx][c][v]);
        Tlist.push_back(Tlist_judge[submeshIdx][c][v]);
      }
    }
    growuplist_adjacent[submeshIdx].push_back(Vlist);
    triangle_grow[submeshIdx].push_back(Tlist);
    crackIndex[submeshIdx].push_back(submeshIdx);
    Vlist.clear();
    Tlist.clear();
    for (int c = 0; c < growuplist[submeshIdx + 1].size(); c++) {
      for (int v = 0; v < growuplist[submeshIdx + 1][c].size(); v++) {
        Vlist.push_back(growuplist[submeshIdx + 1][c][v]);
        Tlist.push_back(Tlist_grow[submeshIdx + 1][c][v]);
      }
    }
    growuplist_adjacent[submeshIdx].push_back(Vlist);
    triangle_grow[submeshIdx].push_back(Tlist);
    crackIndex[submeshIdx].push_back(submeshIdx + 1);
  }
}

void
boundaryupdate_computeboundary(
  std::vector<TriangleMesh<MeshType>>& submeshes,
  std::vector<std::vector<int8_t>>     isBoundaryVertex,
  std::vector<int>&                    Vlist,
  std::vector<int>&                    Tlist,
  size_t&                              submeshIdx,
  size_t                               firstIndex,
  size_t                               nextIndex_relative) {
  auto              submeshCount = submeshes.size();
  auto              pointCount   = submeshes[submeshIdx].pointCount();
  std::vector<bool> status(pointCount, false);
  StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
  std::vector<int8_t> ttags(submeshes[submeshIdx].triangleCount());
  ComputeVertexToTriangle(submeshes[submeshIdx].triangles(),
                          submeshes[submeshIdx].pointCount(),
                          vertexToTriangleOutput);
  auto nextIndex     = TransformIndex(submeshes,
                                  submeshIdx,
                                  vertexToTriangleOutput,
                                  firstIndex,
                                  nextIndex_relative);
  status[firstIndex] = true;
  status[nextIndex]  = true;
  Vlist.push_back(firstIndex);
  Vlist.push_back(nextIndex);
  bool needtraverse = true;
  auto vindex       = nextIndex;
  while (needtraverse) {
    bool                 found = false;
    std::vector<int8_t>  vtags_next(submeshes[submeshIdx].pointCount());
    std::vector<int32_t> vadj_next;
    std::vector<int8_t>  ttags(submeshes[submeshIdx].triangleCount());
    ComputeAdjacentVertices(vindex,
                            submeshes[submeshIdx].triangles(),
                            vertexToTriangleOutput,
                            vtags_next,
                            vadj_next);
    for (int vindexnew = 0; vindexnew < vadj_next.size(); vindexnew++) {
      if (ComputeEdgeAdjacentTriangleCount(
            vindex, vadj_next[vindexnew], vertexToTriangleOutput, ttags)
          == 1) {
        ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
        if (isBoundaryVertex[submeshIdx][vadj_next[vindexnew]]) {
          if (status[vadj_next[vindexnew]] == false) {
            Vlist.push_back(vadj_next[vindexnew]);
            status[vadj_next[vindexnew]] = true;
            vindex                       = vadj_next[vindexnew];
            found                        = true;
          }
        }
        if (found) break;
      }
    }
    if (!found) { needtraverse = false; }
  }
  for (int v = 0; v < Vlist.size(); v++) {
    std::vector<int32_t> tadj;
    ComputeEdgeAdjacentTriangles(Vlist[v],
                                 Vlist[(v + 1) % Vlist.size()],
                                 vertexToTriangleOutput,
                                 ttags,
                                 tadj);
    ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
    if (tadj.size() != 0) {
      Tlist.push_back(tadj[0]);
    } else {
      Tlist.push_back(Tlist[Tlist.size() - 1]);
    }
  }
}

void
boundaryupdate(std::vector<TriangleMesh<MeshType>>&        submeshes,
               std::vector<std::vector<int8_t>>            isBoundaryVertex,
               std::vector<std::vector<std::vector<int>>>& growuplist_adjacent,
               std::vector<std::vector<std::vector<int>>>& triangle_grow,
               std::vector<std::vector<std::vector<size_t>>>& boundaryIndex,
               std::vector<size_t>&                           crackCount,
               std::vector<std::vector<size_t>>&              crackIndex) {
  auto submeshCount = submeshes.size();
  growuplist_adjacent.resize(submeshCount);
  triangle_grow.resize(submeshCount);
  crackIndex.resize(submeshCount);
  for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    for (int c = 0; c < crackCount[submeshIdx]; c++) {
      std::vector<int> Vlist, Tlist;
      boundaryupdate_computeboundary(submeshes,
                                     isBoundaryVertex,
                                     Vlist,
                                     Tlist,
                                     submeshIdx,
                                     boundaryIndex[submeshIdx][c][0],
                                     boundaryIndex[submeshIdx][c][1]);
      auto crackIdx = boundaryIndex[submeshIdx][c][2];
      if (growuplist_adjacent[crackIdx].size() == 0) {
        std::vector<int> Vlist_temp, Tlist_temp;
        for (int v = 0; v < Vlist.size(); v++) {
          Vlist_temp.push_back(Vlist[v]);
          Tlist_temp.push_back(Tlist[v]);
        }
        growuplist_adjacent[crackIdx].push_back(Vlist_temp);
        triangle_grow[crackIdx].push_back(Tlist_temp);
        crackIndex[crackIdx].push_back(submeshIdx);
      } else {
        auto crackjudge = crackIndex[crackIdx].size();
        bool adjacent   = false;
        for (int cj = 0; cj < crackjudge; cj++) {
          if (submeshIdx == crackIndex[crackIdx][cj]) {
            adjacent = true;
            for (int v = 0; v < Vlist.size(); v++) {
              growuplist_adjacent[crackIdx][cj].push_back(Vlist[v]);
              triangle_grow[crackIdx][cj].push_back(Tlist[v]);
            }
          }
        }
        if (!adjacent) {
          std::vector<int> Vlist_temp, Tlist_temp;
          for (int v = 0; v < Vlist.size(); v++) {
            Vlist_temp.push_back(Vlist[v]);
            Tlist_temp.push_back(Tlist[v]);
          }
          growuplist_adjacent[crackIdx].push_back(Vlist_temp);
          triangle_grow[crackIdx].push_back(Tlist_temp);
          crackIndex[crackIdx].push_back(submeshIdx);
        }
      }
    }
  }
}

void
addnewTriangle(TriangleMesh<MeshType>& submesh,
               int&                    vindex0,
               int&                    vindex1,
               int&                    newv) {
  StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
  std::vector<int8_t>                 ttags(submesh.triangleCount());
  ComputeVertexToTriangle(
    submesh.triangles(), submesh.pointCount(), vertexToTriangleOutput);
  std::vector<int32_t> tadj;
  ComputeEdgeAdjacentTriangles(
    vindex0, vindex1, vertexToTriangleOutput, ttags, tadj);
  if (tadj.size() > 0) {
    int  pos1, pos2;
    auto tri = submesh.triangle(tadj[0]);
    for (int i = 0; i < 3; i++) {
      if (vindex0 == tri[i]) { pos1 = i; }
      if (vindex1 == tri[i]) { pos2 = i; }
    }
    if (pos1 > pos2) {
      if ((pos1 - pos2) == 1) {
        Triangle tri_new = {vindex0, vindex1, newv};
        submesh.addTriangle(tri_new);
      } else {
        Triangle tri_new = {vindex1, vindex0, newv};
        submesh.addTriangle(tri_new);
      }
    } else {
      if ((pos2 - pos1) == 1) {
        Triangle tri_new = {vindex1, vindex0, newv};
        submesh.addTriangle(tri_new);
      } else {
        Triangle tri_new = {vindex0, vindex1, newv};
        submesh.addTriangle(tri_new);
      }
    }
  }
}

void
boundaryfuse(std::vector<TriangleMesh<MeshType>>&        submeshes,
             std::vector<std::vector<std::vector<int>>>& growuplist_adjacent,
             std::vector<std::vector<std::vector<int>>>& triangle_grow,
             std::vector<std::vector<size_t>>&           crackIndex) {
  auto crackCount = growuplist_adjacent.size();
  for (size_t crackIdx = 0; crackIdx < crackCount; crackIdx++) {
    if (growuplist_adjacent[crackIdx].size() != 0) {
      auto                   submeshIdx0 = crackIndex[crackIdx][0];
      auto                   submeshIdx1 = crackIndex[crackIdx][1];
      TriangleMesh<MeshType> boundaryVertices;
      std::vector<int>       closeVlist;
      for (int i = 0; i < growuplist_adjacent[crackIdx][0].size(); i++) {
        boundaryVertices.addPoint(
          submeshes[submeshIdx0].point(growuplist_adjacent[crackIdx][0][i]));
      }
      KdTree<MeshType> kdtreeTarget(3, boundaryVertices.points(), 10);
      for (int vindex = 0; vindex < growuplist_adjacent[crackIdx][1].size();
           vindex++) {
        auto                  queryCount = 1;
        std::vector<int32_t>  indices(queryCount, 0);
        std::vector<MeshType> sqrDists(queryCount, 0);
        kdtreeTarget.query(submeshes[submeshIdx1]
                             .point(growuplist_adjacent[crackIdx][1][vindex])
                             .data(),
                           queryCount,
                           indices.data(),
                           sqrDists.data());
        closeVlist.push_back(indices[0]);
      }
      for (int vindex = 0; vindex < growuplist_adjacent[crackIdx][1].size();
           vindex++) {
        int  judgesize    = growuplist_adjacent[crackIdx][0].size();
        int  pointCounter = submeshes[submeshIdx1].pointCount();
        auto v1           = submeshes[submeshIdx1].point(
          growuplist_adjacent[crackIdx][1][vindex]);
        auto v2 = submeshes[submeshIdx1].point(
          growuplist_adjacent[crackIdx][1]
                             [(vindex + 1)
                              % growuplist_adjacent[crackIdx][1].size()]);
        auto minv1 = closeVlist[vindex];
        auto minv2 = closeVlist[(vindex + 1) % closeVlist.size()];
        if (minv1 == minv2) {
          bool averageflag = false;
          auto tri1        = submeshes[submeshIdx1].triangle(
            triangle_grow[crackIdx][1][vindex]);
          auto tri2 =
            submeshes[submeshIdx0].triangle(triangle_grow[crackIdx][0][minv1]);
          if (boundaryIntersectType(
                submeshes[submeshIdx1], submeshes[submeshIdx0], tri1, tri2)
              != 0) {
            averageflag = true;
          }
          if (averageflag) {
            auto averagePoint = (submeshes[submeshIdx1].point(
                                   growuplist_adjacent[crackIdx][1][vindex])
                                 + submeshes[submeshIdx0].point(
                                   growuplist_adjacent[crackIdx][0][minv1]))
                                / 2;
            submeshes[submeshIdx1].point(
              growuplist_adjacent[crackIdx][1][vindex]) = averagePoint;
            submeshes[submeshIdx1].point(
              growuplist_adjacent[crackIdx][1]
                                 [(vindex + 1)
                                  % growuplist_adjacent[crackIdx][1].size()]) =
              averagePoint;
            submeshes[submeshIdx0].point(
              growuplist_adjacent[crackIdx][0][minv1]) = averagePoint;
          } else {
            auto newv = submeshes[submeshIdx0].point(
              growuplist_adjacent[crackIdx][0]
                                 [minv1
                                  % growuplist_adjacent[crackIdx][0].size()]);
            if (newv != v1) {
              submeshes[submeshIdx1].addPoint(newv);
              pointCounter++;
              Triangle tri_new = {
                growuplist_adjacent[crackIdx][1][vindex],
                growuplist_adjacent[crackIdx][1]
                                   [(vindex + 1)
                                    % growuplist_adjacent[crackIdx][1].size()],
                pointCounter - 1};
              if (!isDegenerate(tri_new)) {
                int newPoint = pointCounter - 1;
                addnewTriangle(
                  submeshes[submeshIdx1],
                  growuplist_adjacent[crackIdx][1][vindex],
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  newPoint);
              }
            }
          }
        } else {
          int  diff        = abs(minv1 - minv2);
          bool averageflag = false;
          if (diff > judgesize / 2) {
            int maxv = minv1, minv = minv2;
            ;
            if (minv > maxv) {
              int temp;
              temp = minv;
              minv = maxv;
              maxv = temp;
            }
            int  length = minv + judgesize - maxv;
            auto tri1   = submeshes[submeshIdx1].triangle(
              triangle_grow[crackIdx][1][vindex]);
            for (int i = 0; i < length; i++) {
              auto tri2 = submeshes[submeshIdx0].triangle(
                triangle_grow[crackIdx][0]
                             [(maxv + i) % triangle_grow[crackIdx][0].size()]);
              if (boundaryIntersectType(
                    submeshes[submeshIdx1], submeshes[submeshIdx0], tri1, tri2)
                  != 0) {
                averageflag = true;
                break;
              }
            }
            if (averageflag) {
              auto averagePoint1 =
                (submeshes[submeshIdx1].point(
                   growuplist_adjacent[crackIdx][1][vindex])
                 + submeshes[submeshIdx0].point(
                   growuplist_adjacent
                     [crackIdx][0]
                     [(minv1) % growuplist_adjacent[crackIdx][0].size()]))
                / 2;
              submeshes[submeshIdx1].point(
                growuplist_adjacent[crackIdx][1][vindex]) = averagePoint1;
              submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv1) % growuplist_adjacent[crackIdx][0].size()]) =
                averagePoint1;
              auto averagePoint2 =
                (submeshes[submeshIdx1].point(
                   growuplist_adjacent
                     [crackIdx][1]
                     [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()])
                 + submeshes[submeshIdx0].point(
                   growuplist_adjacent
                     [crackIdx][0]
                     [(minv2) % growuplist_adjacent[crackIdx][0].size()]))
                / 2;
              submeshes[submeshIdx1].point(
                growuplist_adjacent
                  [crackIdx][1]
                  [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()]) =
                averagePoint2;
              submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv2) % growuplist_adjacent[crackIdx][0].size()]) =
                averagePoint2;
              auto direction =
                submeshes[submeshIdx1].point(
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()])
                - submeshes[submeshIdx1].point(
                  growuplist_adjacent[crackIdx][1][vindex]);
              for (int i = 1; i < length; i++) {
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(maxv + i) % growuplist_adjacent[crackIdx][0].size()]) =
                  submeshes[submeshIdx1].point(
                    growuplist_adjacent[crackIdx][1][vindex])
                  + (direction * i) / length;
              }
            } else {
              auto newv1 = submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv1) % growuplist_adjacent[crackIdx][0].size()]);
              auto newv2 = submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv2) % growuplist_adjacent[crackIdx][0].size()]);
              if (v1 == newv1) {
                submeshes[submeshIdx1].addPoint(newv2);
                pointCounter++;
                Triangle tri_new = {
                  growuplist_adjacent[crackIdx][1][vindex],
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 1};
                if (!isDegenerate(tri_new)) {
                  int newPoint = pointCounter - 1;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    growuplist_adjacent[crackIdx][1][vindex],
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
              } else {
                submeshes[submeshIdx1].addPoint(newv1);
                submeshes[submeshIdx1].addPoint(newv2);
                pointCounter += 2;
                Triangle tri_new1 = {
                  growuplist_adjacent[crackIdx][1][vindex],
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 2};
                if (!isDegenerate(tri_new1)) {
                  int newPoint = pointCounter - 2;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    growuplist_adjacent[crackIdx][1][vindex],
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
                Triangle tri_new2 = {
                  pointCounter - 2,
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 1};
                if (!isDegenerate(tri_new2)) {
                  int newPoint = pointCounter - 1;
                  int oldPoint = pointCounter - 2;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    oldPoint,
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
              }
              auto direction =
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(minv) % growuplist_adjacent[crackIdx][0].size()])
                - submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(maxv) % growuplist_adjacent[crackIdx][0].size()]);
              for (int i = 1; i < length; i++) {
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(maxv + i) % growuplist_adjacent[crackIdx][0].size()]) =
                  submeshes[submeshIdx0].point(
                    growuplist_adjacent
                      [crackIdx][0]
                      [(maxv) % growuplist_adjacent[crackIdx][0].size()])
                  + (direction * i) / length;
              }
            }
          } else {
            int maxv = minv1, minv = minv2;
            ;
            if (minv > maxv) {
              int temp;
              temp = minv;
              minv = maxv;
              maxv = temp;
            }
            int  length = maxv - minv;
            auto tri1   = submeshes[submeshIdx1].triangle(
              triangle_grow[crackIdx][1][vindex]);
            for (int i = 0; i < length; i++) {
              auto tri2 = submeshes[submeshIdx0].triangle(
                triangle_grow[crackIdx][0]
                             [(minv + i) % triangle_grow[crackIdx][0].size()]);
              if (boundaryIntersectType(
                    submeshes[submeshIdx1], submeshes[submeshIdx0], tri1, tri2)
                  != 0) {
                averageflag = true;
                break;
              }
            }
            if (averageflag) {
              auto averagePoint1 =
                (submeshes[submeshIdx1].point(
                   growuplist_adjacent[crackIdx][1][vindex])
                 + submeshes[submeshIdx0].point(
                   growuplist_adjacent
                     [crackIdx][0]
                     [(minv1) % growuplist_adjacent[crackIdx][0].size()]))
                / 2;
              submeshes[submeshIdx1].point(
                growuplist_adjacent[crackIdx][1][vindex]) = averagePoint1;
              submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv1) % growuplist_adjacent[crackIdx][0].size()]) =
                averagePoint1;
              auto averagePoint2 =
                (submeshes[submeshIdx1].point(
                   growuplist_adjacent
                     [crackIdx][1]
                     [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()])
                 + submeshes[submeshIdx0].point(
                   growuplist_adjacent
                     [crackIdx][0]
                     [(minv2) % growuplist_adjacent[crackIdx][0].size()]))
                / 2;
              submeshes[submeshIdx1].point(
                growuplist_adjacent
                  [crackIdx][1]
                  [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()]) =
                averagePoint2;
              submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv2) % growuplist_adjacent[crackIdx][0].size()]) =
                averagePoint2;
              auto direction =
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(maxv) % growuplist_adjacent[crackIdx][0].size()])
                - submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(minv) % growuplist_adjacent[crackIdx][0].size()]);
              for (int i = 1; i < length; i++) {
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(minv + i) % growuplist_adjacent[crackIdx][0].size()]) =
                  submeshes[submeshIdx0].point(
                    growuplist_adjacent
                      [crackIdx][0]
                      [(minv) % growuplist_adjacent[crackIdx][0].size()])
                  + (direction * i) / length;
              }
            } else {
              auto newv1 = submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv1) % growuplist_adjacent[crackIdx][0].size()]);
              auto newv2 = submeshes[submeshIdx0].point(
                growuplist_adjacent
                  [crackIdx][0]
                  [(minv2) % growuplist_adjacent[crackIdx][0].size()]);
              if (v1 == newv1) {
                submeshes[submeshIdx1].addPoint(newv2);
                pointCounter++;
                Triangle tri_new = {
                  growuplist_adjacent[crackIdx][1][vindex],
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 1};
                if (!isDegenerate(tri_new)) {
                  int newPoint = pointCounter - 1;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    growuplist_adjacent[crackIdx][1][vindex],
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
              } else {
                submeshes[submeshIdx1].addPoint(newv1);
                submeshes[submeshIdx1].addPoint(newv2);
                pointCounter += 2;
                Triangle tri_new1 = {
                  growuplist_adjacent[crackIdx][1][vindex],
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 2};
                if (!isDegenerate(tri_new1)) {
                  int newPoint = pointCounter - 2;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    growuplist_adjacent[crackIdx][1][vindex],
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
                Triangle tri_new2 = {
                  pointCounter - 2,
                  growuplist_adjacent
                    [crackIdx][1]
                    [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                  pointCounter - 1};
                if (!isDegenerate(tri_new2)) {
                  int newPoint = pointCounter - 1;
                  int oldPoint = pointCounter - 2;
                  addnewTriangle(
                    submeshes[submeshIdx1],
                    oldPoint,
                    growuplist_adjacent
                      [crackIdx][1]
                      [(vindex + 1) % growuplist_adjacent[crackIdx][1].size()],
                    newPoint);
                }
              }
              auto direction =
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(maxv) % growuplist_adjacent[crackIdx][0].size()])
                - submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(minv) % growuplist_adjacent[crackIdx][0].size()]);
              for (int i = 1; i < length; i++) {
                submeshes[submeshIdx0].point(
                  growuplist_adjacent
                    [crackIdx][0]
                    [(minv + i) % growuplist_adjacent[crackIdx][0].size()]) =
                  submeshes[submeshIdx0].point(
                    growuplist_adjacent
                      [crackIdx][0]
                      [(minv) % growuplist_adjacent[crackIdx][0].size()])
                  + (direction * i) / length;
              }
            }
          }
        }
      }
    }
  }
}

template<typename T>
bool
isPointOutsideTriangle(const Vec2<T>& A,
                       const Vec2<T>& B,
                       const Vec2<T>& C,
                       const Vec2<T>& P) {
  double cross1 = (B - A) ^ (P - A);
  double cross2 = (C - B) ^ (P - B);
  double cross3 = (A - C) ^ (P - C);
  return (cross1 > 0 && cross2 > 0 && cross3 > 0)
         || (cross1 < 0 && cross2 < 0 && cross3 < 0);
}

template<typename T>
void
predict_uv(Vec3<T>& v0_geo,
           Vec3<T>& v1_geo,
           Vec3<T>& v2_geo,
           Vec2<T>& v0_uv,
           Vec2<T>& v1_uv,
           Vec2<T>& uvProj,
           Vec2<T>& uvProjuvCurr) {
  auto   g1        = v0_geo - v1_geo;
  auto   g2        = v2_geo - v1_geo;
  auto   u1        = v0_uv - v1_uv;
  double g1_dot_g2 = boundaryproject(g1, g2);
  double d2_g1     = boundaryproject(g1, g1);
  //
  if (d2_g1 > 0) {
    uvProj                 = v1_uv + u1 * (g1_dot_g2 / d2_g1);
    auto    gProj          = v1_geo + g1 * (g1_dot_g2 / d2_g1);
    double  d2_gProj_gCurr = boundaryproject(v2_geo - gProj, v2_geo - gProj);
    Vec2<T> uvproj_temp    = {u1[1], -u1[0]};
    uvProjuvCurr           = uvproj_temp * std::sqrt(d2_gProj_gCurr / d2_g1);
  }
}

void
addtexture(std::vector<TriangleMesh<MeshType>>& submeshes,
           std::vector<TriangleMesh<MeshType>>& submeshes_temp) {
  int submeshCount = submeshes.size();
  for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    int originTCount = submeshes_temp[submeshIdx].triangleCount();
    int originVCount = submeshes_temp[submeshIdx].pointCount();
    int pointCounter = submeshes_temp[submeshIdx].texCoordCount();
    StaticAdjacencyInformation<int32_t> vertexToTriangleOutput;
    std::vector<int8_t> ttags(submeshes[submeshIdx].triangleCount());
    ComputeVertexToTriangle(submeshes[submeshIdx].triangles(),
                            submeshes[submeshIdx].pointCount(),
                            vertexToTriangleOutput);
    int                    vindex[2] = {0, 0};
    TriangleMesh<MeshType> boundaryVertices;
    for (int i = 0; i < submeshes[submeshIdx].texCoordCount(); i++) {
      boundaryVertices.addTexCoord(submeshes[submeshIdx].texCoord(i));
    }
    KdTree_tex<MeshType> kdtreeTarget(2, boundaryVertices.texCoords(), 10);
    for (int triIndex = originTCount;
         triIndex < submeshes[submeshIdx].triangleCount();
         triIndex++) {
      Vec3<double>              v0_geo, v1_geo, v2_geo;
      Vec2<double>              v0_uv, v1_uv, v2_uv_judge, v2_new;
      Vec2<double>              uvProj, uvProjuvCurr;
      std::vector<Vec2<double>> v2_uv;
      int  tri_judge, texCoordIndex0 = 0, texCoordIndex1 = 0;
      auto tri1 = submeshes[submeshIdx].triangle(triIndex);
      int  j    = 0;
      for (int i = 0; i < 3; i++) {
        if (tri1[i] < originVCount) {
          vindex[j] = tri1[i];
          j++;
        } else {
          v2_geo = submeshes[submeshIdx].point(tri1[i]);
        }
      }
      std::vector<int32_t> tadj;
      ComputeEdgeAdjacentTriangles(
        vindex[0], vindex[1], vertexToTriangleOutput, ttags, tadj);
      ttags.resize(submeshes[submeshIdx].triangleCount(), 0);
      if (tadj.size() > 1) {
        for (int t = 0; t < tadj.size(); t++) {
          if (tadj[t] < triIndex) {
            tri_judge = tadj[t];
            for (int i = 0; i < 3; i++) {
              if (vindex[0] == submeshes[submeshIdx].triangle(tadj[t])[i]) {
                v0_geo = submeshes[submeshIdx].point(vindex[0]);
                v0_uv  = submeshes[submeshIdx].texCoord(
                  submeshes[submeshIdx].texCoordTriangle(tadj[t])[i]);
                texCoordIndex0 =
                  submeshes[submeshIdx].texCoordTriangle(tadj[t])[i];
              } else if (vindex[1]
                         == submeshes[submeshIdx].triangle(tadj[t])[i]) {
                v1_geo = submeshes[submeshIdx].point(vindex[1]);
                v1_uv  = submeshes[submeshIdx].texCoord(
                  submeshes[submeshIdx].texCoordTriangle(tadj[t])[i]);
                texCoordIndex1 =
                  submeshes[submeshIdx].texCoordTriangle(tadj[t])[i];
              } else {
                v2_uv_judge = submeshes[submeshIdx].texCoord(
                  submeshes[submeshIdx].texCoordTriangle(tadj[t])[i]);
              }
            }
          }
        }
        predict_uv(v0_geo, v1_geo, v2_geo, v0_uv, v1_uv, uvProj, uvProjuvCurr);
        v2_uv.resize(2);
        v2_uv[0]      = uvProj + uvProjuvCurr;
        v2_uv[1]      = uvProj - uvProjuvCurr;
        auto dist0_uv = (v2_uv[0] - v2_uv_judge).norm();
        auto dist1_uv = (v2_uv[1] - v2_uv_judge).norm();
        if (dist0_uv > dist1_uv) v2_new = v2_uv[0];
        else v2_new = v2_uv[1];
        auto                  queryCount = submeshCount * 2;
        std::vector<int32_t>  indices(queryCount, 0);
        std::vector<MeshType> sqrDists(queryCount, 0);
        std::vector<int32_t>  judgeTlist;
        kdtreeTarget.query(
          v2_new.data(), queryCount, indices.data(), sqrDists.data());
        StaticAdjacencyInformation<int32_t> vertexToTriangleOutput_tex;
        std::vector<int8_t>                 ttags_tex(
          submeshes[submeshIdx].texCoordTriangleCount());
        ComputeVertexToTriangle(submeshes[submeshIdx].texCoordTriangles(),
                                submeshes[submeshIdx].texCoordCount(),
                                vertexToTriangleOutput_tex);
        for (int vindex = 0; vindex < indices.size(); vindex++) {
          for (int j = vindex + 1; j < indices.size(); j++) {
            if (ComputeEdgeAdjacentTriangleCount(indices[vindex],
                                                 indices[j],
                                                 vertexToTriangleOutput_tex,
                                                 ttags_tex)
                != 0) {
              std::vector<int32_t> tadj;
              ComputeEdgeAdjacentTriangles(indices[vindex],
                                           indices[j],
                                           vertexToTriangleOutput_tex,
                                           ttags_tex,
                                           tadj);
              for (int i = 0; i < tadj.size(); i++) {
                if (tadj[i] != tri_judge) { judgeTlist.push_back(tadj[i]); }
              }
            }
          }
        }
        bool   Intersect      = true;
        double k              = 1;
        int    iterationCount = 0, tri_temp = 0, tri;
        while (Intersect) {
          for (tri = tri_temp; tri < judgeTlist.size(); tri++) {
            if (!pointOutsideTriangle(
                  submeshes[submeshIdx],
                  v2_new,
                  submeshes[submeshIdx].texCoordTriangle(judgeTlist[tri]))) {
              Intersect = true;
              iterationCount++;
              tri_temp = tri;
              if (dist0_uv > dist1_uv) {
                k      = 3 / 4;
                v2_new = uvProj + k * uvProjuvCurr;
              } else {
                k      = 3 / 4;
                v2_new = uvProj - k * uvProjuvCurr;
              }
              break;
            }
          }
          if (iterationCount == 4) {
            v2_new         = uvProj;
            Intersect      = false;
            iterationCount = 0;
          }
          if (tri == judgeTlist.size()) { Intersect = false; }
        }
      }
      for (int i = 0; i < 2; i++) {
        if (v2_new[i] < 0) { v2_new[i] = 0; }
        if (v2_new[i] > 1) { v2_new[i] = 1; }
      }
      Triangle tri_tex = {texCoordIndex0, texCoordIndex1, pointCounter};
      submeshes[submeshIdx].addTexCoord(v2_new);
      submeshes[submeshIdx].addTexCoordTriangle(tri_tex);
      pointCounter++;
      originVCount++;
    }
  }
}

void
setNewMesh(std::vector<TriangleMesh<MeshType>>& submeshes,
           std::vector<TriangleMesh<MeshType>>& submeshes_temp) {
  auto submeshCount = submeshes.size();
  for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    for (int v = submeshes_temp[submeshIdx].pointCount();
         v < submeshes[submeshIdx].pointCount();
         v++) {
      auto v_new = submeshes[submeshIdx].point(v);
      submeshes_temp[submeshIdx].addPoint(v_new);
    }
    for (int vt = submeshes_temp[submeshIdx].texCoordCount();
         vt < submeshes[submeshIdx].texCoordCount();
         vt++) {
      auto vt_new = submeshes[submeshIdx].texCoord(vt);
      submeshes_temp[submeshIdx].addTexCoord(vt_new);
    }
    for (int t = submeshes_temp[submeshIdx].triangleCount();
         t < submeshes[submeshIdx].triangleCount();
         t++) {
      auto tri_new = submeshes[submeshIdx].triangle(t);
      submeshes_temp[submeshIdx].addTriangle(tri_new);
    }
    for (int tex = submeshes_temp[submeshIdx].texCoordTriangleCount();
         tex < submeshes[submeshIdx].texCoordTriangleCount();
         tex++) {
      auto tex_new = submeshes[submeshIdx].texCoordTriangle(tex);
      submeshes_temp[submeshIdx].addTexCoordTriangle(tex_new);
    }
  }
}
////==========================================================
////ReferenceList related functions :
////constructBmspsRefListStruct : construct SPS Reference List structure from encoder Parameters
////createMspsReferenceLists   : create mspsRefDiffList from SPS refrence list structure
////createSmhReferenceList     : create submeshHeader.getSmhBasemeshReferenceList() from submeshHeader reference list structure(it can be SPS reference list structure)
////==========================================================
////for decoding mesh streams
////std::vector<std::vector<std::vector<int32_t>>> bmspsRefDiffList_;//[mspsIdx][listIdx][refIdx]
//int32_t createMspsReferenceLists( V3cBitstream& syntax, std::vector<std::vector<std::vector<int32_t>>>& mspsRefDiffList ){
//  auto&  basemeshStream = syntax.getBaseMeshDataStream();
//  size_t numMSPS = basemeshStream.getBaseMeshSequenceParameterSetList().size();
//  mspsRefDiffList.resize(numMSPS);
//  for(size_t iMsps=0; iMsps<numMSPS; iMsps++){
//    auto& meshSPS  = basemeshStream.getBaseMeshSequenceParameterSet(iMsps);
//    auto numRefList = meshSPS.getBmspsNumRefMeshFrameListsInBmsps();
//    mspsRefDiffList[iMsps].resize(numRefList);
//    //refFrameDiff:1,2,3,4...
//    for ( size_t listIdx = 0; listIdx < numRefList; listIdx++ ) {
//      auto& refList = meshSPS.getBmeshRefListStruct(listIdx);
//      size_t numActiveRefEntries=refList.getNumRefEntries();
//      mspsRefDiffList[iMsps][listIdx].resize(numActiveRefEntries);
//      for ( size_t refIdx = 0; refIdx < numActiveRefEntries; refIdx++ ) {
//        //if(refList.getStRefAtalsFrameFlag( refIdx))
//        int32_t deltaMfocSt = ( 2 * refList.getStrafEntrySignFlag( refIdx ) - 1 ) * refList.getAbsDeltaMfocSt( refIdx );
//        mspsRefDiffList[iMsps][listIdx][refIdx]=deltaMfocSt;
//      }//refIdx
//    }//listIdx
//  }//#imsps
//  return 0;
//}
//
//
//
}  // namespace vmesh
