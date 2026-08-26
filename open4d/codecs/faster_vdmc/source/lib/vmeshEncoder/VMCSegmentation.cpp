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

#include <stdio.h>
#include "encoder.hpp"
#include <unordered_map>
#include <array>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <list>
#include <numeric>
#include <random>
#include <vector>

using namespace vmesh;

std::vector<std::string>
VMCEncoder::makeSubmeshPaths(const std::string& srcFilenamePath,
                             const std::string& submeshPrefix,
                             uint32_t           submeshCount) {
  std::vector<std::string> submeshPaths(submeshCount);

  auto last_seperator = srcFilenamePath.find_last_of('/');
  for (uint32_t submeshIdx = 0; submeshIdx < submeshCount;
       submeshIdx++) {  //for output
    submeshPaths[submeshIdx] =
      submeshPrefix
      + srcFilenamePath.substr(last_seperator + 1,
                               srcFilenamePath.rfind(".") - last_seperator - 1)
      + "_submesh" + std::to_string(submeshIdx) + ".obj";
  }
  return submeshPaths;
}

int32_t
vmesh::VMCEncoder::submeshSegment(const std::string&          sourceMeshPath,
                                  std::vector<std::string>&   submeshPaths,
                                  uint32_t                    numSubmeshes0,
                                  uint32_t                    firstFrameIndex0,
                                  uint32_t                    frameCount0,
                                  const VMCEncoderParameters& params,
                                  std::string&                submeshPrefix,
                                  bool                        loadOnly) {
  uint32_t    firstFrameIndex = firstFrameIndex0;
  uint32_t    submeshCount    = numSubmeshes0;
  std::string srcFilenamePath = sourceMeshPath;

  auto last_seperator = srcFilenamePath.find_last_of('/');
  submeshPaths.resize(submeshCount);
  for (uint32_t submeshIdx = 0; submeshIdx < submeshCount;
       submeshIdx++) {  //for output
    submeshPaths[submeshIdx] =
      submeshPrefix
      + srcFilenamePath.substr(last_seperator + 1,
                               srcFilenamePath.rfind(".") - last_seperator - 1)
      + "_submesh" + std::to_string(submeshIdx) + ".obj";
  }

  if (loadOnly) {
    //TODO: check if files exist
    return -1;
  }

  std::vector<std::vector<TriangleMesh<MeshType>>> submeshOutputList(
    frameCount0);
  int32_t frameIdx = 1;
  for (frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
    std::string srcFilename =
      expandNum(srcFilenamePath, firstFrameIndex + frameIdx);
    vmesh::TriangleMesh<MeshType> srcMesh0;
    if (!srcMesh0.load(srcFilename)) { break; }
    //srcMesh0.setFrameIndex(0);
    Box3<MeshType>              boundingBox0 = srcMesh0.boundingBox();
    std::vector<Box3<MeshType>> boundingBoxes0(submeshCount);

    //vertically uniform submeshCount division
    int32_t topVertDist = (boundingBox0.max[1] - boundingBox0.min[1]) / 4;

    //top
    boundingBoxes0[0].max = boundingBox0.max;
    boundingBoxes0[0].min = Vec3<MeshType>(boundingBox0.min[0],
                                           boundingBox0.max[1] - topVertDist,
                                           boundingBox0.min[2]);  //3510

    for (int submeshIdx = 1; submeshIdx < submeshCount; submeshIdx++) {
      boundingBoxes0[submeshIdx].max =
        Vec3<MeshType>(boundingBoxes0[submeshIdx - 1].max[0],
                       boundingBoxes0[submeshIdx - 1].min[1],
                       boundingBoxes0[submeshIdx - 1].max[2]);
      boundingBoxes0[submeshIdx].min =
        Vec3<MeshType>(boundingBoxes0[submeshIdx - 1].min[0],
                       boundingBoxes0[submeshIdx].max[1] - topVertDist,
                       boundingBoxes0[submeshIdx - 1].min[2]);  //3510
    }
    boundingBoxes0[submeshCount - 1].min = boundingBox0.min;

    printf(
      "submeshSegment) frameIndex %d\t#submesh %d\n", frameIdx, submeshCount);
    for (int i = 0; i < submeshCount; i++) {
      printf("submeshSegment) submesh[%d] (%f, %f, %f) ~(%f,%f,%f)\n",
             i,
             boundingBoxes0[i].min[0],
             boundingBoxes0[i].min[1],
             boundingBoxes0[i].min[2],
             boundingBoxes0[i].max[0],
             boundingBoxes0[i].max[1],
             boundingBoxes0[i].max[2]);
    }

    vmesh::TriangleMesh<MeshType> srcMesh;
    srcMesh.load(srcFilename);
    //save material in the lossless mode
    if (srcMesh.materialNames().size() > 1 && params.dracoMeshLossless) {
      std::vector<vmesh::Material<double>> materials;
      materials.resize(srcMesh.materialNames().size());
      std::string materialLibPath = submeshPrefix + srcMesh.materialLibrary();
      for (int matIdx = 0; matIdx < srcMesh.materialNames().size(); matIdx++) {
        materials[matIdx].name = srcMesh.materialNames()[matIdx];
        if (srcMesh.textureMapUrls()[matIdx] == "") {
          materials[matIdx].texture = "";
        } else {
          std::string textureName = srcMesh.textureMapUrls()[matIdx];
          //std::string texturePath = removeExtension(submeshPrefix) + "_" + _textureMapUrls[matIdx];
          materials[matIdx].texture = textureName;
          //// save the corresponding texture
          //_textures[texIdx++].frame(f).saveImage(texturePath);
        }
        if (matIdx == 0) {
          std::cout << "Saving material file:" << materialLibPath << std::endl;
          if (!materials[matIdx].save(materialLibPath)) return false;
        } else {
          std::cout << "Appending material " << materials[matIdx].name
                    << " to file:" << materialLibPath << std::endl;
          if (!materials[matIdx].append(materialLibPath)) return false;
        }
      }
    }
    //srcMesh.setFrameIndex(frameIdx);
    Box3<MeshType> boundingBoxCurrent = srcMesh.boundingBox();

    bool                                       bEmptySubmesh     = false;
    uint32_t                                   submeshPointCount = 0;
    std::vector<vmesh::TriangleMesh<MeshType>> submeshes(submeshCount);
    std::vector<bool>                          inSubMesh;
    inSubMesh.resize(srcMesh.triangleCount(), false);
    for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      std::vector<int32_t> triIndexList;
      for (int ti = 0; ti < srcMesh.triangleCount(); ti++) {
        if (!inSubMesh[ti]) {
          int32_t viSet[3];
          viSet[0] = srcMesh.triangle(ti)[0];
          viSet[1] = srcMesh.triangle(ti)[1];
          viSet[2] = srcMesh.triangle(ti)[2];
          auto y0  = srcMesh.point(viSet[0])[1];
          auto y1  = srcMesh.point(viSet[1])[1];
          auto y2  = srcMesh.point(viSet[2])[1];
          if ((y0 >= boundingBoxes0[submeshIdx].min[1]
               && y0 <= boundingBoxes0[submeshIdx].max[1])
              || (y1 >= boundingBoxes0[submeshIdx].min[1]
                  && y1 <= boundingBoxes0[submeshIdx].max[1])
              || (y2 >= boundingBoxes0[submeshIdx].min[1]
                  && y2 <= boundingBoxes0[submeshIdx].max[1])) {
            //add triangle to the submesh
            triIndexList.push_back(ti);
            if (!params.allowSubmeshOverlap) {
              inSubMesh[ti] = true;
            }  //in thee boundingbox
          }
        }
      }  //ti
      expandMeshDupPoints(submeshes[submeshIdx], srcMesh, triIndexList);

      submeshPointCount += submeshes[submeshIdx].pointCount();
      if (submeshes[submeshIdx].pointCount() == 0) {
        bEmptySubmesh = true;
        printf("Submesh[%zu] is empty\n", submeshIdx);
        break;
      }
    }

    //      if(bEmptySubmesh || submeshPointCount<srcMesh.pointCount()){
    //        if(submeshPointCount<srcMesh.pointCount())
    //          printf("Frame[%d] Points(%d) are left in the source mesh\n", frameIdx, srcMesh.pointCount()-submeshPointCount);
    //        firstInGroup = frameIdx;
    //        break;
    //      }

    //save submesh
    for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      auto& mesh = submeshes[submeshIdx];
      //mesh.setFrameIndex(frameIdx);
      submeshOutputList[frameIdx].push_back(mesh);
      std::string submeshPath =
        expandNum(submeshPaths[submeshIdx], firstFrameIndex + frameIdx);
      //srcFilename.substr(last_seperator+1, srcFilename.rfind(".")-last_seperator-1) +"_submesh"+std::to_string(submeshIdx)+".obj";
      printf("Saving %s (frame[%d] #submesh %d)\n",
             submeshPath.data(),
             frameIdx,
             submeshIdx);
      submeshOutputList[frameIdx][submeshIdx].save(submeshPath);
    }
  }  //frameIdx

  int32_t frameCount = frameIdx;
  printf("submesh information----\n");
  for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    printf("Frame[%d] #submesh %d\n", frameIdx, submeshCount);
    for (int submeshIdx = 0; submeshIdx < submeshOutputList[frameIdx].size();
         submeshIdx++) {
      printf("submesh[%d]\tpoint %d\tuvCordinate %d\ttriangles %d\n",
             submeshIdx,
             submeshOutputList[frameIdx][submeshIdx].pointCount(),
             submeshOutputList[frameIdx][submeshIdx].texCoordCount(),
             submeshOutputList[frameIdx][submeshIdx].triangleCount());
    }
  }
  printf("-----------\n");

  return frameCount;
}
#define DEBUG_SEGMENTATION 0
int32_t
vmesh::VMCEncoder::submeshSegmentByTriangleDensity(
  const std::string&          sourceMeshPath,
  std::vector<std::string>&   submeshPaths,
  uint32_t                    numSubmeshes0,
  uint32_t                    firstFrameIndex0,
  uint32_t                    frameCount0,
  const VMCEncoderParameters& params,
  std::string&                submeshPrefix,
  bool                        loadOnly) {
  if (numSubmeshes0 != 2) {
    std::cout << "For now submesh by triangle density can only process two "
                 "sub-meshes, please choose a different segmentation method, "
                 "or adjust the sub-mesh count"
              << std::endl;
    exit(-1);
  }

  uint32_t firstFrameIndex = firstFrameIndex0;
  uint32_t submeshCount    = numSubmeshes0;

  std::string srcFilenamePath = sourceMeshPath;

  auto last_seperator = srcFilenamePath.find_last_of('/');
  submeshPaths.resize(submeshCount);
  for (uint32_t submeshIdx = 0; submeshIdx < submeshCount;
       submeshIdx++) {  //for output
    submeshPaths[submeshIdx] =
      submeshPrefix
      + srcFilenamePath.substr(last_seperator + 1,
                               srcFilenamePath.rfind(".") - last_seperator - 1)
      + "_submesh" + std::to_string(submeshIdx) + ".obj";
  }

  if (loadOnly) {
    //TODO: check if files exist
    return -1;
  }

  std::vector<std::vector<TriangleMesh<MeshType>>> submeshOutputList(
    frameCount0);
  int32_t frameIdx = 1;
  for (frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
    std::string srcFilename =
      expandNum(srcFilenamePath, firstFrameIndex + frameIdx);
    vmesh::TriangleMesh<MeshType> srcMeshOrig;
    vmesh::TriangleMesh<MeshType> srcMesh;
    if (!srcMeshOrig.load(srcFilename)) { break; }
    std::vector<int> mapping;
    UnifyVertices(srcMeshOrig.points(),
                  srcMeshOrig.triangles(),
                  srcMesh.points(),
                  srcMesh.triangles(),
                  mapping);
    //checking for triangle density, and putting similar densities in a sub-mesh, but only keeping the
    auto  numTriangles = srcMesh.triangleCount();
    auto  numVertices  = srcMesh.pointCount();
    auto& triangles    = srcMesh.triangles();
    auto& vertices     = srcMesh.points();
#if DEBUG_SEGMENTATION
    TriangleMesh<MeshType> debugMesh;
    debugMesh = srcMesh;
    debugMesh.texCoords().clear();
    debugMesh.texCoordTriangles().clear();
    debugMesh.resizeFaceIds(numTriangles);
#endif
    std::vector<double> triangleAreas;
    srcMesh.computeTriangleAreas(triangleAreas);
#if DEBUG_SEGMENTATION
    std::string triangleName = submeshPrefix + "/triangle_areas.txt";
    auto        fp           = fopen(triangleName.data(), "wt");
    if (fp) {
      for (auto area : triangleAreas) { fprintf(fp, "%f\n", area); }
    }
    fclose(fp);
#endif
    std::vector<int> subMeshID;
    subMeshID.resize(numTriangles, 0);
    // now creating the category
    double threshold =
      params
        .segmentThreshold;  // this could be automatically obtained from the data, maybe using Otsu's method?

    for (int i = 0; i < numTriangles; i++) {
      if (triangleAreas[i] > threshold) subMeshID[i] = 1;
#if DEBUG_SEGMENTATION
      debugMesh.setFaceId(i, subMeshID[i]);
#endif
    }
#if DEBUG_SEGMENTATION
    std::string debugName =
      submeshPrefix
      + srcFilename.substr(last_seperator + 1,
                           srcFilename.rfind(".") - last_seperator - 1)
      + "_threshold" + std::to_string(threshold) + "_SEGMENTATION_#1.obj";
    debugMesh.saveToOBJUsingFidAsColor(debugName);
#endif

    // now change the classification if all the neighboring triangles have a different classification
    bool                                       useVertexCriteria = false;
    vmesh::StaticAdjacencyInformation<int32_t> triangleToTriangle;
    triangleToTriangle.resize(numTriangles);
    vmesh::StaticAdjacencyInformation<int32_t> vertexToTriangle;
    ComputeVertexToTriangle(triangles, numVertices, vertexToTriangle);
    if (useVertexCriteria) {
      std::vector<int8_t> ttags;
      ttags.resize(numTriangles);
      for (int32_t triIdx = 0; triIdx < numTriangles; ++triIdx) {
        const auto ncount = ComputeAdjacentTrianglesCount(
          triangles[triIdx], vertexToTriangle, ttags);
        triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
      }
      triangleToTriangle.updateShift();
      std::vector<int32_t> tadj;
      for (int32_t triIdx = 0; triIdx < numTriangles; ++triIdx) {
        ComputeAdjacentTriangles(
          triangles[triIdx], vertexToTriangle, ttags, tadj);
        for (const auto triIdxNeighbor : tadj) {
          triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
        }
      }
    } else {
      std::vector<int8_t> ttags;
      ttags.resize(numTriangles);
      for (int32_t triIdx = 0; triIdx < numTriangles; ++triIdx) {
        auto ncount = ComputeEdgeAdjacentTriangleCount(
          triangles[triIdx][0], triangles[triIdx][1], vertexToTriangle, ttags);
        ncount += ComputeEdgeAdjacentTriangleCount(
          triangles[triIdx][1], triangles[triIdx][2], vertexToTriangle, ttags);
        ncount += ComputeEdgeAdjacentTriangleCount(
          triangles[triIdx][2], triangles[triIdx][0], vertexToTriangle, ttags);
        triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
      }
      triangleToTriangle.updateShift();
      std::vector<int32_t> tadj;
      for (int32_t triIdx = 0; triIdx < numTriangles; ++triIdx) {
        ComputeEdgeAdjacentTriangles(triangles[triIdx][0],
                                     triangles[triIdx][1],
                                     vertexToTriangle,
                                     ttags,
                                     tadj);
        for (const auto triIdxNeighbor : tadj) {
          triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
        }
        ComputeEdgeAdjacentTriangles(triangles[triIdx][1],
                                     triangles[triIdx][2],
                                     vertexToTriangle,
                                     ttags,
                                     tadj);
        for (const auto triIdxNeighbor : tadj) {
          triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
        }
        ComputeEdgeAdjacentTriangles(triangles[triIdx][2],
                                     triangles[triIdx][0],
                                     vertexToTriangle,
                                     ttags,
                                     tadj);
        for (const auto triIdxNeighbor : tadj) {
          triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
        }
      }
    }

    const auto* neighbours = triangleToTriangle.neighbours();
    for (int i = 0; i < numTriangles; i++) {
      auto       selfID     = subMeshID[i];
      int        neighborID = -1;
      bool       change     = true;
      const auto start      = triangleToTriangle.neighboursStartIndex(i);
      const auto end        = triangleToTriangle.neighboursEndIndex(i);
      for (int32_t n = start; n < end && change; ++n) {
        const auto nIndex = neighbours[n];
        if (nIndex == i) continue;
        neighborID = subMeshID[nIndex];
        if (neighborID == selfID) { change = false; }
      }
      if (change) {
        subMeshID[i] = neighborID;
#if DEBUG_SEGMENTATION
        debugMesh.setFaceId(i, subMeshID[i]);
#endif
      }
    }
#if DEBUG_SEGMENTATION
    debugName =
      submeshPrefix
      + srcFilename.substr(last_seperator + 1,
                           srcFilename.rfind(".") - last_seperator - 1)
      + "_threshold" + std::to_string(threshold) + "_SEGMENTATION_#2.obj";
    debugMesh.saveToOBJUsingFidAsColor(debugName);
#endif
    // now create connected components
    std::vector<int32_t> lifo;
    lifo.reserve(numTriangles);
    int32_t          ccCount = 0;
    std::vector<int> partition;
    std::vector<int> numTrianglesInCC;
    partition.resize(numTriangles, -1);
    for (int32_t triangleIndex = 0; triangleIndex < numTriangles;
         ++triangleIndex) {
      if (partition[triangleIndex] == -1) {
        const auto ccIndex = ccCount++;
        numTrianglesInCC.push_back(1);
        auto refSubMeshID        = subMeshID[triangleIndex];
        partition[triangleIndex] = ccIndex;
#if DEBUG_SEGMENTATION
        debugMesh.setFaceId(triangleIndex, ccIndex);
#endif
        lifo.push_back(triangleIndex);
        while (!lifo.empty()) {
          const auto tIndex = lifo.back();
          lifo.pop_back();
          const auto start = triangleToTriangle.neighboursStartIndex(tIndex);
          const auto end   = triangleToTriangle.neighboursEndIndex(tIndex);
          for (int32_t n = start; n < end; ++n) {
            const auto nIndex = neighbours[n];
            if ((partition[nIndex] == -1)
                && (refSubMeshID == subMeshID[nIndex])) {
              partition[nIndex] = ccIndex;
#if DEBUG_SEGMENTATION
              debugMesh.setFaceId(nIndex, ccIndex);
#endif
              lifo.push_back(nIndex);
              numTrianglesInCC.back()++;
            }
          }
        }
      }
    }
#if DEBUG_SEGMENTATION
    debugName =
      submeshPrefix
      + srcFilename.substr(last_seperator + 1,
                           srcFilename.rfind(".") - last_seperator - 1)
      + "_threshold" + std::to_string(threshold) + "_SEGMENTATION_#3.obj";
    debugMesh.saveToOBJUsingFidAsColor(debugName);
#endif
    // and merge the connected components that share and edge with the first and the second components with the most number of triangles
    std::vector<std::size_t> indexes(numTrianglesInCC.size());
    std::iota(indexes.begin(), indexes.end(), 0);
    std::sort(indexes.begin(), indexes.end(), [&](int lhs, int rhs) {
      return numTrianglesInCC[lhs] > numTrianglesInCC[rhs];
    });
    auto ccIdxFirst  = indexes[0];
    auto ccIdxSecond = indexes[1];
    for (int ccIdx = 0; ccIdx < ccCount; ccIdx++) {
      if ((ccIdx == ccIdxFirst) || (ccIdx == ccIdxSecond)) continue;
      // check if shares and edge with the first CC
      int numSharedTrianglesFirst  = 0;
      int numSharedTrianglesSecond = 0;
      for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
        if (partition[tIndex] == ccIdx) {
          // triangle belongs to the current CC, let's check if its neighbors belong to either the first or the second CC
          const auto start = triangleToTriangle.neighboursStartIndex(tIndex);
          const auto end   = triangleToTriangle.neighboursEndIndex(tIndex);
          bool       shareEdgeFirst  = false;
          bool       shareEdgeSecond = false;
          for (int32_t n = start; n < end; ++n) {
            const auto nIndex = neighbours[n];
            if (partition[nIndex] == ccIdxFirst) shareEdgeFirst = true;
            if (partition[nIndex] == ccIdxSecond) shareEdgeSecond = true;
          }
          if (shareEdgeFirst) numSharedTrianglesFirst++;
          if (shareEdgeSecond) numSharedTrianglesSecond++;
        }
      }
      if (numSharedTrianglesFirst > numSharedTrianglesSecond) {
        // first connected component will absorb this element
        for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
          if (partition[tIndex] == ccIdx) partition[tIndex] = ccIdxFirst;
        }
      } else if (numSharedTrianglesSecond > numSharedTrianglesFirst) {
        // second connected component will absorb this element
        for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
          if (partition[tIndex] == ccIdx) partition[tIndex] = ccIdxSecond;
        }
      } else {
        // the connected component either touches both, or do not touch them at all, let's have it absovered by the first one
        for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
          if (partition[tIndex] == ccIdx) partition[tIndex] = ccIdxFirst;
        }
      }
    }
    for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
      if (partition[tIndex] == ccIdxFirst) {
        subMeshID[tIndex] = 0;
#if DEBUG_SEGMENTATION
        debugMesh.setFaceId(tIndex, 0);
#endif
      } else {
        subMeshID[tIndex] = 1;
#if DEBUG_SEGMENTATION
        debugMesh.setFaceId(tIndex, 1);
#endif
      }
    }
#if DEBUG_SEGMENTATION
    debugName =
      submeshPrefix
      + srcFilename.substr(last_seperator + 1,
                           srcFilename.rfind(".") - last_seperator - 1)
      + "_threshold" + std::to_string(threshold) + "_SEGMENTATION_#4.obj";
    debugMesh.saveToOBJUsingFidAsColor(debugName);
#endif
    std::vector<std::vector<int32_t>> additionalTriangles;
    additionalTriangles.resize(submeshCount);
    if (params.allowSubmeshOverlap) {
      //expand the border of the sub-meshes to overlap with each other
      for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        for (int tIndex = 0; tIndex < numTriangles; tIndex++) {
          if (subMeshID[tIndex] == submeshIdx) {
            // triangle belongs to the current CC, let's check if its neighbors belong to either the first or the second CC
            const auto start = triangleToTriangle.neighboursStartIndex(tIndex);
            const auto end   = triangleToTriangle.neighboursEndIndex(tIndex);
            bool       shareEdgeFirst  = false;
            bool       shareEdgeSecond = false;
            for (int32_t n = start; n < end; ++n) {
              const auto nIndex = neighbours[n];
              if (subMeshID[nIndex] != submeshIdx)
                additionalTriangles[submeshIdx].push_back(nIndex);
            }
          }
        }
      }
    }

    // load sub-meshes
    bool                                       bEmptySubmesh     = false;
    uint32_t                                   submeshPointCount = 0;
    std::vector<vmesh::TriangleMesh<MeshType>> submeshes(submeshCount);
    for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      std::vector<int32_t> triIndexList;
      for (int ti = 0; ti < srcMesh.triangleCount(); ti++) {
        if (subMeshID[ti] == submeshIdx) {
          //add triangle to the submesh
          triIndexList.push_back(ti);
        }
      }
      if (params.allowSubmeshOverlap) {
        for (auto& brdIdx : additionalTriangles[submeshIdx])
          triIndexList.push_back(brdIdx);
      }
      expandMeshDupPoints(submeshes[submeshIdx], srcMeshOrig, triIndexList);
      submeshPointCount += submeshes[submeshIdx].pointCount();
      if (submeshes[submeshIdx].pointCount() == 0) {
        bEmptySubmesh = true;
        printf("Submesh[%zu] is empty\n", submeshIdx);
        break;
      }
    }

    //save submesh
    bool saveSubmesh = true;
    if (saveSubmesh) {
      for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        auto& mesh = submeshes[submeshIdx];
        //testing if the mesh has at least more than 1% of the vertices of the original mesh, otherwise we quit
        if (100.0 * (double)mesh.triangleCount() / (double)srcMesh.triangleCount() < 1.0) {
            std::cout << "The submesh contains less than 1% of the total triangle count (" << mesh.triangleCount() << "/" << srcMesh.triangleCount() << ")" << std::endl;
            return 0;
        }
        //mesh.setFrameIndex(frameIdx);
        submeshOutputList[frameIdx].push_back(mesh);
        std::string submeshPath =
          expandNum(submeshPaths[submeshIdx], firstFrameIndex + frameIdx);
        //srcFilename.substr(last_separator + 1, srcFilename.rfind(".") - last_separator - 1) + "_submesh" + std::to_string(submeshIdx) + ".obj";
        printf("Saving %s (frame[%d] #submesh %d)\n",
               submeshPath.data(),
               frameIdx,
               submeshIdx);
        submeshOutputList[frameIdx][submeshIdx].save(submeshPath);
      }
    }

  }  //frameIdx

  int32_t frameCount = frameIdx;
  printf("submesh information----\n");
  for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    printf("Frame[%d] #submesh %d\n", frameIdx, submeshCount);
    for (int submeshIdx = 0; submeshIdx < submeshOutputList[frameIdx].size();
         submeshIdx++) {
      printf("submesh[%d]\tpoint %d\tuvCordinate %d\ttriangles %d\n",
             submeshIdx,
             submeshOutputList[frameIdx][submeshIdx].pointCount(),
             submeshOutputList[frameIdx][submeshIdx].texCoordCount(),
             submeshOutputList[frameIdx][submeshIdx].triangleCount());
    }
  }
  printf("-----------\n");
  return frameCount;
}

namespace {

std::vector<std::vector<int32_t>>
createSegmentListsWithBoundingBoxes(
  const TriangleMesh<MeshType>&      srcMesh,
  const std::vector<Box3<MeshType>>& boundingBoxes) {
  const auto                        submeshCount = boundingBoxes.size();
  std::vector<std::vector<int32_t>> triIndexLists(submeshCount);
  std::vector<int32_t> unsegmentedTriIndexList(srcMesh.triangleCount());
  std::iota(unsegmentedTriIndexList.begin(), unsegmentedTriIndexList.end(), 0);
  for (uint32_t submeshIdx = 0; submeshIdx < submeshCount; ++submeshIdx) {
    auto&                triIndexList = triIndexLists[submeshIdx];
    std::vector<int32_t> left;
    for (auto ti : unsegmentedTriIndexList) {
      int32_t viSet[3];
      viSet[0] = srcMesh.triangle(ti)[0];
      viSet[1] = srcMesh.triangle(ti)[1];
      viSet[2] = srcMesh.triangle(ti)[2];
      auto p0  = srcMesh.point(viSet[0]);
      auto p1  = srcMesh.point(viSet[1]);
      auto p2  = srcMesh.point(viSet[2]);
      if ((boundingBoxes[submeshIdx].contains(p0))
          || (boundingBoxes[submeshIdx].contains(p1))
          || (boundingBoxes[submeshIdx].contains(p2))) {
        //add triangle to the submesh
        triIndexList.push_back(ti);
      } else {
        left.push_back(ti);
      }  //in thee boundingbox
    }    //ti
    unsegmentedTriIndexList = left;
  }
  if (!unsegmentedTriIndexList.empty()) {
    std::cerr << "Warning: boundingBoxes doesn't cover srcMesh completely.\n";
    for (uint32_t submeshIdx = 0; submeshIdx < submeshCount; ++submeshIdx) {
      std::cerr << "  boundingBoxes[" << submeshIdx
                << "]: " << boundingBoxes[submeshIdx] << "\n";
    }
    std::cerr << "  srcMesh.boundingBox(): " << srcMesh.boundingBox()
              << std::endl;
  }
  return triIndexLists;
}

std::vector<std::vector<int32_t>>
createSegmentListsVertically(const TriangleMesh<MeshType>& srcMesh,
                             uint32_t                      submeshCount) {
  const Box3<MeshType>        srcBoundingBox = srcMesh.boundingBox();
  std::vector<Box3<MeshType>> boundingBoxes(submeshCount);

  //vertically uniform submeshCount division
  MeshType topVertDist =
    (srcBoundingBox.max[1] - srcBoundingBox.min[1]) / submeshCount;

  //top
  boundingBoxes[0].max = srcBoundingBox.max;
  boundingBoxes[0].min = Vec3<MeshType>(srcBoundingBox.min[0],
                                        srcBoundingBox.max[1] - topVertDist,
                                        srcBoundingBox.min[2]);

  for (uint32_t submeshIdx = 1; submeshIdx < submeshCount; ++submeshIdx) {
    boundingBoxes[submeshIdx].max =
      Vec3<MeshType>(boundingBoxes[submeshIdx - 1].max[0],
                     boundingBoxes[submeshIdx - 1].min[1],
                     boundingBoxes[submeshIdx - 1].max[2]);
    boundingBoxes[submeshIdx].min =
      Vec3<MeshType>(boundingBoxes[submeshIdx - 1].min[0],
                     boundingBoxes[submeshIdx].max[1] - topVertDist,
                     boundingBoxes[submeshIdx - 1].min[2]);
  }
  boundingBoxes[submeshCount - 1].min = srcBoundingBox.min;

  return createSegmentListsWithBoundingBoxes(srcMesh, boundingBoxes);
}

std::vector<vmesh::TriangleMesh<MeshType>>
segment(const TriangleMesh<MeshType>&            srcMesh,
        const std::vector<std::vector<int32_t>>& triIndexLists) {
  uint32_t submeshCount = triIndexLists.size();
  std::vector<vmesh::TriangleMesh<MeshType>> submeshes(submeshCount);
  for (uint32_t submeshIdx = 0; submeshIdx < submeshCount; ++submeshIdx) {
    const auto& triIndexList = triIndexLists[submeshIdx];
    expandMeshDupPoints(submeshes[submeshIdx], srcMesh, triIndexList);
  }
  return submeshes;
}

bool
saveSubmeshes(
  const std::vector<std::vector<TriangleMesh<MeshType>>>& submeshes,
  const std::vector<std::string>&                         submeshPaths,
  uint32_t                                                firstFrameIndex) {
  uint32_t frameCount = submeshes.size();
  for (uint32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    uint32_t submeshCount = submeshes[frameIdx].size();
    for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      std::string submeshPath =
        expandNum(submeshPaths[submeshIdx], firstFrameIndex + frameIdx);
      //srcFilename.substr(last_seperator+1, srcFilename.rfind(".")-last_seperator-1) +"_submesh"+std::to_string(submeshIdx)+".obj";
      printf("Saving %s (frame[%d] #submesh %d)\n",
             submeshPath.data(),
             frameIdx,
             submeshIdx);
      auto saved = submeshes[frameIdx][submeshIdx].save(submeshPath);
      if (!saved) {
        printf("failed to save\n");
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool
VMCEncoder::submeshSegmentByBaseMesh(
  const VMCGroupOfFramesInfo&     gofInfo,
  const std::vector<std::string>& submeshPaths) {
  const auto frameCount   = gofInfo.frameCount_;
  const auto submeshCount = submeshPaths.size();
  std::map<int32_t, std::vector<std::vector<int32_t>>> triIndexLists;
  for (int32_t fi = 0; fi < frameCount; ++fi) {
    auto meshType = gofInfo.frameInfo(fi).type;
    if (meshType == basemesh::I_BASEMESH) {
      triIndexLists[fi] =
        createSegmentListsVertically(wholeBaseMeshes_[fi], submeshCount);
      if (triIndexLists[fi].size() != submeshCount) {
        std::cerr << "Warning: submeshSegment submeshCount is expected "
                  << submeshCount << " , but actually "
                  << triIndexLists[fi].size() << std::endl;
      } else {
        for (uint32_t submeshIdx = 0; submeshIdx < submeshCount;
             ++submeshIdx) {
          if (triIndexLists[fi][submeshIdx].empty()) {
            std::cerr << "Warning: submeshSegment at submesh " << submeshIdx
                      << " in frame " << fi << " is empty" << std::endl;
          }
        }
      }
    }
  }
  for (int32_t fi = 0; fi < frameCount; ++fi) {
    auto meshType = gofInfo.frameInfo(fi).type;
    auto rfi      = gofInfo.frameInfo(fi).referenceFrameIndex;
    if (meshType != basemesh::I_BASEMESH) {
      auto it = triIndexLists.find(rfi);
      if (it == triIndexLists.end()) {
        // perhaps referenceFrameIndex is front frame.
        std::cerr << "Error: NO triIndexLists[" << rfi << "] in frame " << fi
                  << std::endl;
        return false;
      }
      triIndexLists[fi] = it->second;
    }
  }
  if (baseMeshes_.size() < frameCount) { baseMeshes_.resize(frameCount); }
  for (int32_t fi = 0; fi < frameCount; ++fi) {
    baseMeshes_[fi] = segment(wholeBaseMeshes_[fi], triIndexLists[fi]);
  }
  auto firstFrameIndex = gofInfo.startFrameIndex_;
  return saveSubmeshes(baseMeshes_, submeshPaths, firstFrameIndex);
}

void
vmesh::VMCEncoder::updateGofInfo(vmesh::Sequence&      source,
                                 VMCGroupOfFramesInfo& gofInfo) {
  //seqInfo is used
  //      frameInfo.referenceFrameIndex =
  //        referenceFrameIndex - gofInfo.startFrameIndex_;

  auto frameCount  = gofInfo.frameCount_;
  auto frameIndex0 = 0;
  //clean gof
  for (int32_t fi = 0; fi < gofInfo.frameCount_; fi++) {
    auto& frameInfo               = gofInfo.framesInfo_[fi];
    frameInfo.frameIndex          = fi;
    frameInfo.referenceFrameIndex = -1;
    frameInfo.type                = basemesh::I_BASEMESH;
  }
  //NOTE: reference can be between the last intra frame and the current frame
  int lastIntraFi = 0;
  for (int32_t currFi = 1; currFi < frameCount; ++currFi) {
    auto& currMesh                                = source.mesh(currFi);
    gofInfo.frameInfo(currFi).referenceFrameIndex = -1;
    for (int32_t prevFi = lastIntraFi; prevFi < currFi; prevFi++) {
      auto& refMesh = source.mesh(prevFi);
      bool  same    = false;

      if (currMesh.pointCount() == refMesh.pointCount()
          && currMesh.triangleCount() == refMesh.triangleCount()
          && currMesh.texCoordCount() == refMesh.texCoordCount()
          && currMesh.texCoordTriangleCount()
               == refMesh.texCoordTriangleCount()) {
        same                     = true;
        const auto texCoordCount = refMesh.texCoordCount();
        for (int32_t v = 0; same && v < texCoordCount; ++v) {
          const auto& texCoord0 = refMesh.texCoord(v);
          const auto& texCoord1 = currMesh.texCoord(v);
          for (int32_t k = 0; k < 2; ++k) {
            if (texCoord0[k] != texCoord1[k]) {
              //printf("textcoord:%d\t%f vs %f\n", v, texCoord0[k], texCoord1[k]);
              same = false;
              break;
            }
          }
        }
        const auto triangleCount = refMesh.triangleCount();
        for (int32_t t = 0; same && t < triangleCount; ++t) {
          const auto& tri0         = refMesh.triangle(t);
          const auto& tri1         = currMesh.triangle(t);
          const auto& triTexCoord0 = refMesh.texCoordTriangle(t);
          const auto& triTexCoord1 = currMesh.texCoordTriangle(t);
          for (int32_t k = 0; k < 3; ++k) {
            if (tri0[k] != tri1[k] || triTexCoord0[k] != triTexCoord1[k]) {
              same = false;
              //printf("triangleCount:%d\t%d,%d vs %d,%d\n", t, tri0[k],triTexCoord0[k], tri1[k], triTexCoord1[k]);
              break;
            }
          }
        }
      }
      if (same) {
        gofInfo.frameInfo(currFi).referenceFrameIndex = prevFi;
        gofInfo.frameInfo(currFi).previousFrameIndex  = currFi - 1;
        gofInfo.frameInfo(currFi).type                = basemesh::P_BASEMESH;
        break;
      }
    }  //prevF
    //NOTE : this is required since sequenceInfo.generate() searches referference frame differently for the multiple submesh case
    if (gofInfo.frameInfo(currFi).type != basemesh::P_BASEMESH) lastIntraFi = currFi;
  }  //currFi
}
