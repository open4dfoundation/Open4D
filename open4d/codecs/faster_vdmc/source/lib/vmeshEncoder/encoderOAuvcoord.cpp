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

#include "encoder.hpp"

#include <cmath>

#include <array>
#include <cstdio>
#include "entropy.hpp"
#include "vmc.hpp"
#include "util/misc.hpp"

//AJT::Added these includes
#include "geometryEncoder.hpp"
#include "util/kdtree.hpp"

using namespace vmesh;





void
VMCEncoder::updateRawUVs(VMCSubmesh&                    frame,
                         TriangleMesh<MeshType>&        rec,
                         TriangleMesh<MeshType>&        enc,
                         const std::vector<int32_t>&    mapping,
                         std::vector<int>&              trianglePartition,
                         const std::vector<VMCSubmesh>& gof,
                         const VMCEncoderParameters&    params) {
  if (frame.submeshType == basemesh::I_BASEMESH) {
    //calculate face mapping CCs <-> decoded
    std::vector<int> faceIdMappingNewToOld(rec.triangles().size(),
                                           -1);  //decoded -> face mapping CCs
    std::vector<int> faceIdMappingOldToNew(rec.triangles().size(),
                                           -1);  //face mapping CCs -> decoded
    //create old triangle List
    TriangleMesh<MeshType> meshProjectedCCs;
    meshProjectedCCs                         = enc;
    auto&                         triListOld = meshProjectedCCs.triangles();
    std::vector<std::vector<int>> uvIdMappingNewToOld(rec.triangles().size());
    for (int triIdx = 0; triIdx < rec.triangles().size(); triIdx++) {
      uvIdMappingNewToOld[triIdx].resize(3);
    }
    for (int triIdx = 0; triIdx < rec.triangles().size(); triIdx++) {
      auto      tri                = rec.triangle(triIdx);
      Vec3<int> triOldWithNewOrder = {
        mapping[tri[0]], mapping[tri[1]], mapping[tri[2]]};
      for (int triIdxOld = 0; triIdxOld < meshProjectedCCs.triangles().size();
           triIdxOld++) {
        auto triOld = meshProjectedCCs.triangle(triIdxOld);
        bool found  = true;
        if (triOldWithNewOrder[0] == triOld[0]
            && triOldWithNewOrder[1] == triOld[1]
            && triOldWithNewOrder[2] == triOld[2]) {
          uvIdMappingNewToOld[triIdx][0] = 0;
          uvIdMappingNewToOld[triIdx][1] = 1;
          uvIdMappingNewToOld[triIdx][2] = 2;
        } else if (triOldWithNewOrder[0] == triOld[1]
                   && triOldWithNewOrder[1] == triOld[2]
                   && triOldWithNewOrder[2] == triOld[0]) {
          uvIdMappingNewToOld[triIdx][0] = 1;
          uvIdMappingNewToOld[triIdx][1] = 2;
          uvIdMappingNewToOld[triIdx][2] = 0;
        } else if (triOldWithNewOrder[0] == triOld[2]
                   && triOldWithNewOrder[1] == triOld[0]
                   && triOldWithNewOrder[2] == triOld[1]) {
          uvIdMappingNewToOld[triIdx][0] = 2;
          uvIdMappingNewToOld[triIdx][1] = 0;
          uvIdMappingNewToOld[triIdx][2] = 1;
        } else {
          found = false;
        }
        if (found) {
          faceIdMappingNewToOld[triIdx]    = triIdxOld;
          faceIdMappingOldToNew[triIdxOld] = triIdx;
        }
      }
    }
    //update ucoord and vcoord with vertex order of decoded mesh
    std::vector<std::vector<float>> uCoordsList, vCoordsList;
    uCoordsList.resize(frame.packedCCList.size());
    vCoordsList.resize(frame.packedCCList.size());
    int                           numCC = frame.packedCCList.size();
    std::vector<int>              codedUVindex(numCC, 0);
    std::vector<std::vector<int>> faceIdOldListInRawPatches(numCC);
    //create old face id list in raw patches
    for (int triIdx = 0; triIdx < rec.triangleCount(); triIdx++) {
      auto tri     = rec.triangle(triIdx);
      int  idxCC   = trianglePartition[triIdx];
      int  rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (frame.packedCCList[idxCC].getProjection() == rawUvId) {
        std::cout << "RAW patch triIdx (compressBaseMesh) = " << triIdx
                  << ", idxCC = " << idxCC << std::endl;
        auto faceIdOld = faceIdMappingNewToOld[triIdx];
        faceIdOldListInRawPatches[idxCC].push_back(faceIdOld);
      }
    }
    //sort faceIdOldListInRawPatches to get order in ucoord and vcoord list
    for (int idxCC = 0; idxCC < numCC; idxCC++) {
      std::sort(faceIdOldListInRawPatches[idxCC].begin(),
                faceIdOldListInRawPatches[idxCC].end());
    }
    for (int triIdx = 0; triIdx < rec.triangleCount(); triIdx++) {
      auto tri = rec.triangle(triIdx);
      int  idxCC;

      idxCC = trianglePartition[triIdx];

      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (frame.packedCCList[idxCC].getProjection() == rawUvId) {
        for (int i = 0; i < 3; i++) {
          if (codedUVindex[idxCC]
              > frame.packedCCList[idxCC].getNumRawUVMinus1()) {
            std::cout << "error : codedUVindex > "
                         "frame.packedCCList[idxCC].getNumRawUVMinus1()"
                      << std::endl;
          }
          auto uvIdxOld0 = uvIdMappingNewToOld[triIdx][0];
          auto uvIdxOld1 = uvIdMappingNewToOld[triIdx][1];
          auto uvIdxOld2 = uvIdMappingNewToOld[triIdx][2];
          auto faceIdOld = faceIdMappingNewToOld[triIdx];
          auto pos       = std::find(faceIdOldListInRawPatches[idxCC].begin(),
                               faceIdOldListInRawPatches[idxCC].end(),
                               faceIdOld);
          if (pos != faceIdOldListInRawPatches[idxCC].end()) {  //included
            int posInList = pos - faceIdOldListInRawPatches[idxCC].begin();
            uCoordsList[idxCC].push_back(frame.packedCCList[idxCC].getUcoord(
              posInList * 3 + uvIdMappingNewToOld[triIdx][i]));
            vCoordsList[idxCC].push_back(frame.packedCCList[idxCC].getVcoord(
              posInList * 3 + uvIdMappingNewToOld[triIdx][i]));
            codedUVindex[idxCC]++;
          } else {
            std::cout << "error" << std::endl;
          }
        }
      }
    }
    //update
    for (int idxCC = 0; idxCC < frame.packedCCList.size(); idxCC++) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (frame.packedCCList[idxCC].getProjection() == rawUvId) {
        codedUVindex[idxCC] = 0;
        frame.packedCCList[idxCC].getUcoords().clear();
        frame.packedCCList[idxCC].getVcoords().clear();
        for (int idxUV = 0; idxUV < uCoordsList[idxCC].size(); idxUV++) {
          if (codedUVindex[idxCC]
              > frame.packedCCList[idxCC].getNumRawUVMinus1()) {
            std::cout << "error : codedUVindex > "
                         "frame.packedCCList[idxCC].getNumRawUVMinus1()"
                      << std::endl;
          }
          frame.packedCCList[idxCC].setUcoord(idxUV,
                                              uCoordsList[idxCC][idxUV]);
          frame.packedCCList[idxCC].setVcoord(idxUV,
                                              vCoordsList[idxCC][idxUV]);
          codedUVindex[idxCC]++;
        }
      }
    }
  } else {
    auto refFrame = gof[frame.referenceFrameIndex];
    //copy UV of reference frame for RAW subpatch
    for (int idxCC = 0; idxCC < refFrame.packedCCList.size(); idxCC++) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (refFrame.packedCCList[idxCC].getProjection() == rawUvId) {
        frame.packedCCList[idxCC].setNumRawUVMinus1(
          refFrame.packedCCList[idxCC].getNumRawUVMinus1());
        frame.packedCCList[idxCC].getUcoords().clear();
        frame.packedCCList[idxCC].getVcoords().clear();
        for (int idxUV = 0;
             idxUV < refFrame.packedCCList[idxCC].getUcoords().size();
             idxUV++) {
          frame.packedCCList[idxCC].setUcoord(
            idxUV, refFrame.packedCCList[idxCC].getUcoord(idxUV));
          frame.packedCCList[idxCC].setVcoord(
            idxUV, refFrame.packedCCList[idxCC].getVcoord(idxUV));
        }
      }
    }
  }
}

void
VMCEncoder::updateTextureCoordinates(VMCSubmesh&                    frame,
                                     TriangleMesh<MeshType>&        baseEnc,
                                     const std::vector<int32_t>&    mapping,
                                     const std::vector<VMCSubmesh>& gof,
                                     const VMCEncoderParameters&    params) {
  auto& base = frame.base;
  // check the number of connected components
  int numCC = frame.packedCCList.size();
  // create the connected components
  std::vector<vmesh::ConnectedComponent<MeshType>> decodedCC;
  std::vector<vmesh::Box2<MeshType>>               bbBoxes;
  std::vector<int>                                 trianglePartition;
  decodedCC.resize(numCC);
  bbBoxes.resize(numCC);
  trianglePartition.resize(base.triangleCount(), -1);
  switch (params.iDeriveTextCoordFromPos) {
  default:
  case 1:  // using UV coordinates
  {
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri    = base.triangle(triIdx);
      auto texTri = base.texCoordTriangle(triIdx);
      int  faceId = base.texCoord(texTri[0])[0];
      int  idxCC  = -1;
      for (int i = 0; i < numCC; i++) {
        if (faceId == frame.packedCCList[i].getFaceId()) {
          idxCC = i;
          i     = numCC;
        }
      }
      decodedCC[idxCC].setFaceId(faceId);
      //now add the points
      trianglePartition[triIdx] = idxCC;
      for (int i = 0; i < 3; i++) {
        decodedCC[idxCC].addPoint(base.point(tri[i]));
      }
      decodedCC[idxCC].addTriangle(decodedCC[idxCC].points().size() - 3,
                                   decodedCC[idxCC].points().size() - 2,
                                   decodedCC[idxCC].points().size() - 1);
    }
    break;
  }
  case 2:  // using FaceID
  {
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri    = base.triangle(triIdx);
      auto faceId = base.faceId(triIdx);
      int  idxCC  = -1;
      for (int i = 0; i < numCC; i++) {
        if (faceId == frame.packedCCList[i].getFaceId()) {
          idxCC = i;
          i     = numCC;
        }
      }
      decodedCC[idxCC].setFaceId(faceId);
      //now add the points
      trianglePartition[triIdx] = idxCC;
      for (int i = 0; i < 3; i++) {
        decodedCC[idxCC].addPoint(base.point(tri[i]));
      }
      decodedCC[idxCC].addTriangle(decodedCC[idxCC].points().size() - 3,
                                   decodedCC[idxCC].points().size() - 2,
                                   decodedCC[idxCC].points().size() - 1);
    }
    break;
  }
  case 3:  // using connected components -> connectivity from texture coordinates
  {
    std::vector<int> partition;
    bool                 flag = false;
    std::vector<int32_t> oldToNewTriangleIndex;
    std::vector<int32_t> posMapping; 
    numCC = ExtractConnectedComponents(base.triangles(),
                                       base.pointCount(),
                                       base,
                                       partition,
                                       posMapping,
                                       oldToNewTriangleIndex,
                                       flag);
    decodedCC.resize(numCC);
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri   = base.triangle(triIdx);
      int  idxCC = partition[triIdx];
      //now add the points
      for (int i = 0; i < 3; i++) {
        decodedCC[idxCC].addPoint(base.point(tri[i]));
      }
      decodedCC[idxCC].addTriangle(decodedCC[idxCC].points().size() - 3,
                                   decodedCC[idxCC].points().size() - 2,
                                   decodedCC[idxCC].points().size() - 1);
    }
    // index sorting
    std::vector<int> sortedIndex(numCC);
    for (int val = 0; val < numCC; val++) sortedIndex[val] = val;
    std::sort(sortedIndex.begin(), sortedIndex.end(), [&](int a, int b) {
      return (decodedCC[a].area() > decodedCC[b].area());
    });
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      int idxCC = partition[triIdx];
      //now add the points
      trianglePartition[triIdx] =
        std::find(sortedIndex.begin(), sortedIndex.end(), idxCC)
        - sortedIndex.begin();
    }
    // vector sorting
    std::sort(
      decodedCC.begin(),
      decodedCC.end(),
      [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
        return (a.area() > b.area());
      });
    //now search for the packedList that is corresponding to the decodedCC
    std::vector<vmesh::ConnectedComponent<MeshType>> reorderedPackedCCList;
    for (int idxCC = 0; idxCC < numCC; idxCC++) {
      double bestIntersectVolume = 0.0;
      double bestAreaDiff        = std::numeric_limits<double>::max();
      int    foundIdx            = -1;
      int    foundIdxByVolume    = -1;
      int    foundIdxByArea      = -1;
      auto   decBB               = decodedCC[idxCC].boundingBox();
      auto   decArea             = decodedCC[idxCC].area();
      for (int idxPackedCCList = 0;
           idxPackedCCList < frame.packedCCList.size();
           idxPackedCCList++) {
        //they have to have the same number of triangles
        if (frame.packedCCList[idxPackedCCList].triangleCount()
            != decodedCC[idxCC].triangleCount())
          continue;
        //bounding boxes should intersect
        auto packedListBB = frame.packedCCList[idxPackedCCList].boundingBox();
        if (decBB.intersects(packedListBB)) {
          //the match will be the one with the higher intersection volume
          auto intersectVolume = (decBB & packedListBB).volume();
          if (intersectVolume > bestIntersectVolume) {
            bestIntersectVolume = intersectVolume;
            foundIdxByVolume    = idxPackedCCList;
          }
        }
        auto areaDiff =
          std::abs(frame.packedCCList[idxPackedCCList].area() - decArea);
        if (areaDiff < bestAreaDiff) {
          bestAreaDiff   = areaDiff;
          foundIdxByArea = idxPackedCCList;
        }
      }
      if (foundIdxByVolume != foundIdxByArea) {
        if (foundIdxByVolume == -1) {
          //case of degenerate bounding box (plane), so we need to use the area
          foundIdx = foundIdxByArea;
        } else {
          //choose intersection of volume over area
          foundIdx = foundIdxByVolume;
        }
      } else {
        foundIdx = foundIdxByVolume;
      }
      if (foundIdx == -1) {
        std::cout << "Error: idxCC is not matched." << std::endl;
        exit(1);
      }
      reorderedPackedCCList.push_back(frame.packedCCList[foundIdx]);
      frame.packedCCList.erase(frame.packedCCList.begin() + foundIdx);
    }
    //now save the reordered list in the frame structure
    frame.packedCCList = reorderedPackedCCList;
    break;
  }
  }
  if (params.useRawUV) {
    updateRawUVs(
      frame, base, baseEnc, mapping, trianglePartition, gof, params);
  }
  // remove the UVs
  base.texCoords().clear();
  base.texCoordTriangles().clear();
  int texParamWidth  = texturePackingSize.first;
  int texParamHeight = texturePackingSize.second;
#if ORTHOATLAS_ADJ
  bool doUVadjustment =
    params.doParameterAdjustment;  // this wil be an input argument
  // create occupancy information
  int occupancyBlockSize = (1 << params.log2GeometryVideoBlockSize);
  int analysisBlockSize =
    (1
     << params
          .log2AnalysisBlockSize);  // could be any value between 1 and occupancySize
  std::vector<bool> occupancyMap;
  int               omWidth      = (texParamWidth / analysisBlockSize);
  int               omHeight     = (texParamHeight / analysisBlockSize);
  double            patchScaling = 1.0 / (double)analysisBlockSize;
  if (doUVadjustment) { occupancyMap.resize(omWidth * omHeight, false); }
#endif
  // create the homography transform
  for (int idxCC = 0; idxCC < numCC; idxCC++) {
    bool doUVadjustmentPerCC = doUVadjustment;
    // load the parameters from the bitstream (orientation, projection direction, etc.)
    decodedCC[idxCC].setProjection(frame.packedCCList[idxCC].getProjection());
    decodedCC[idxCC].setOrientation(
      frame.packedCCList[idxCC].getOrientation());
    decodedCC[idxCC].setU0(frame.packedCCList[idxCC].getU0());
    decodedCC[idxCC].setV0(frame.packedCCList[idxCC].getV0());
    decodedCC[idxCC].setScale(frame.packedCCList[idxCC].getScale());
    int rawUvId = params.use45DegreeProjection ? 18 : 6;
    if (frame.packedCCList[idxCC].getProjection() == rawUvId) {
      doUVadjustmentPerCC = false;
      continue;
    }
    if (params.biasEnableFlag) {
      // using the values from the texture parameterization stage
      decodedCC[idxCC].setU1(frame.packedCCList[idxCC].getU1());
      decodedCC[idxCC].setV1(frame.packedCCList[idxCC].getV1());
      decodedCC[idxCC].setSizeU(frame.packedCCList[idxCC].getSizeU());
      decodedCC[idxCC].setSizeV(frame.packedCCList[idxCC].getSizeV());
      bbBoxes[idxCC] = decodedCC[idxCC].boundingBoxProjected(
        frame.packedCCList[idxCC].getProjection());
    } else {
      // extract the values from the projected decoded bounding box
      auto projection = decodedCC[idxCC].getProjection();
      bbBoxes[idxCC]  = decodedCC[idxCC].boundingBoxProjected(projection);
      auto bbScale =
        Box2<double>(bbBoxes[idxCC].min * decodedCC[idxCC].getScale(),
                     bbBoxes[idxCC].max * decodedCC[idxCC].getScale());
      auto bbScalePlusGutter = Box2<double>(
        bbScale.min, bbScale.max + Vec2<double>(2 * params.gutter));
      int U1;
      if ((projection == 0) || (projection == 4) || (projection == 5)
          || (projection == 6) || (projection == 9) || (projection == 11)
          || (projection == 12) || (projection == 14) || (projection == 15)) {
        U1 = std::ceil(bbBoxes[idxCC].max[0]);
      } else U1 = std::floor(bbBoxes[idxCC].min[0]);
      int V1 = std::floor(bbBoxes[idxCC].min[1]);
      int widthOccCC =
        std::ceil((bbScalePlusGutter.max[0] - bbScalePlusGutter.min[0])
                  / (double)occupancyBlockSize);
      int heightOccCC =
        std::ceil((bbScalePlusGutter.max[1] - bbScalePlusGutter.min[1])
                  / (double)occupancyBlockSize);
      decodedCC[idxCC].setU1(U1);
      decodedCC[idxCC].setV1(V1);
      decodedCC[idxCC].setSizeU(widthOccCC);
      decodedCC[idxCC].setSizeV(heightOccCC);
    }
#if DEBUG_ORTHO
    if (params.keepIntermediateFiles) {
      auto prefixDEBUG = prefix + "_transmitted_CC#" + std::to_string(idxCC);
      decodedCC[idxCC].save(prefixDEBUG + "_ENC.obj");
    }
#endif
#if ORTHOATLAS_ADJ
    if (doUVadjustment && doUVadjustmentPerCC) {
      // adjust the size of the patch according to the dimensions of the decoded bounding box
      auto oldU0    = decodedCC[idxCC].getU0();
      auto oldV0    = decodedCC[idxCC].getV0();
      auto oldSizeU = decodedCC[idxCC].getSizeU();
      auto oldSizeV = decodedCC[idxCC].getSizeV();
#  if ORTHOATLAS_ADJ_DEBUG
      std::cout << "Connected Component(" << idxCC << ") bounding box = ["
                << bbBoxes[idxCC].min[0] << "," << bbBoxes[idxCC].min[1]
                << "],[" << bbBoxes[idxCC].max[0] << ", "
                << bbBoxes[idxCC].max[1] << "]" << std::endl;
      std::cout << "Projection:(" << decodedCC[idxCC].getProjection()
                << "), Orientation:(" << decodedCC[idxCC].getOrientation()
                << ")" << std::endl;
#  endif
      int  newU0, newV0, newSizeU, newSizeV;
      int  projection = decodedCC[idxCC].getProjection();
      bool winding =
        ((projection == 0) || (projection == 4) || (projection == 5)
         || (projection == 6) || (projection == 9) || (projection == 11)
         || (projection == 12) || (projection == 14) || (projection == 15));
      float  gutter = params.gutter;
      double scale  = decodedCC[idxCC].getScale();
      newSizeU      = oldSizeU;
      while ((bbBoxes[idxCC].max[0] - bbBoxes[idxCC].min[0])
             > ((double)newSizeU * (double)occupancyBlockSize - params.gutter)
                 / scale)
        newSizeU++;
      newSizeV = oldSizeV;
      while ((bbBoxes[idxCC].max[1] - bbBoxes[idxCC].min[1])
             > ((double)newSizeV * (double)occupancyBlockSize - params.gutter)
                 / scale)
        newSizeV++;
      // updating the syntax element
      decodedCC[idxCC].setSizeU(newSizeU);
      decodedCC[idxCC].setSizeV(newSizeV);
      frame.packedCCList[idxCC].setSizeU(newSizeU);
      frame.packedCCList[idxCC].setSizeV(newSizeV);
      //now move the U0,V0 to be in the same original position
      switch (decodedCC[idxCC].getOrientation()) {
      case 0:
        newU0 = oldU0;
        newV0 = oldV0;
        break;
      case 1:  //90
        newU0 = oldU0 - (newSizeV - oldSizeV);
        newV0 = oldV0;
        break;
      case 2:  //180
        newU0 = oldU0 - (newSizeU - oldSizeU);
        newV0 = oldV0 - (newSizeV - oldSizeV);
        break;
      case 3:  //270
        newU0 = oldU0;
        newV0 = oldV0 - (newSizeU - oldSizeU);
        break;
      }
      decodedCC[idxCC].setU0(newU0);
      decodedCC[idxCC].setV0(newV0);
      frame.packedCCList[idxCC].setU0(newU0);
      frame.packedCCList[idxCC].setV0(newV0);
#  if ORTHOATLAS_ADJ_DEBUG
      if ((oldSizeU != newSizeU) || (oldSizeV != newSizeV))
        std::cout << "Updating size [" << oldSizeU << "," << oldSizeV
                  << "] -> [" << newSizeU << ", " << newSizeV << "]"
                  << std::endl;
      if ((oldU0 != newU0) || (oldV0 != newV0))
        std::cout << "Updating position [" << oldU0 << "," << oldV0 << "] -> ["
                  << newU0 << ", " << newV0 << "]" << std::endl;
#  endif
      // adjust the U0,V0 if necessary
      int               omCCWidth, omCCHeight;
      std::vector<bool> occupancyMapCC;
      omCCWidth  = newSizeU * occupancyBlockSize * patchScaling;
      omCCHeight = newSizeV * occupancyBlockSize * patchScaling;
      if ((decodedCC[idxCC].getOrientation() == 1)
          || (decodedCC[idxCC].getOrientation() == 3)) {
        auto tmp   = omCCWidth;
        omCCWidth  = omCCHeight;
        omCCHeight = tmp;
      }
      occupancyMapCC.clear();
      occupancyMapCC.resize(omCCWidth * omCCHeight, false);
#  if ORTHOATLAS_ADJ_DEBUG
      vmesh::Frame<uint8_t> occupancyVideoCC;
      occupancyVideoCC.resize(
        omCCWidth, omCCHeight, vmesh::ColourSpace::BGR444p);
#  endif
      // patch translation in texture map
      auto patchTranslation =
        Vec2<MeshType>(decodedCC[idxCC].getU0() * occupancyBlockSize,
                       decodedCC[idxCC].getV0() * occupancyBlockSize);
      //calculate all triangle bounding boxes
      std::vector<Box2<double>> bboxTriangles;
      int numTriangles = decodedCC[idxCC].triangles().size();
      bboxTriangles.resize(numTriangles);
      for (int triIdx = 0; triIdx < numTriangles; triIdx++) {
        auto& tri = decodedCC[idxCC].triangles()[triIdx];
        int   uvIdx[3];
        bboxTriangles[triIdx].min = std::numeric_limits<double>::max();
        bboxTriangles[triIdx].max = std::numeric_limits<double>::min();
        // project each vertex of the triangle to find the UV coordinate
        for (int i = 0; i < 3; i++) {
          auto uv = decodedCC[idxCC].convert(decodedCC[idxCC].point(tri[i]),
                                             1,  // not normalizing
                                             1,  // not normalizing
                                             params.gutter,
                                             occupancyBlockSize);
          // remove the 2D translation in texture map, and rescale according to the block sizes
          uv -= patchTranslation;
          uv *= patchScaling;
          // store the bounding box coordinates
          for (int32_t k = 0; k < 2; ++k) {
            bboxTriangles[triIdx].min[k] =
              std::min(bboxTriangles[triIdx].min[k], uv[k]);
            bboxTriangles[triIdx].max[k] =
              std::max(bboxTriangles[triIdx].max[k], uv[k]);
          }
        }
      }
      // now rasterize the connected component occupancy map given the triangle's bounding boxes
      for (int h = 0; h < omCCHeight; h++) {
        for (int w = 0; w < omCCWidth; w++) {
          bool occupied = false;
          for (int triIdx = 0; triIdx < bboxTriangles.size() && !occupied;
               triIdx++) {
            if (bboxTriangles[triIdx].contains(Vec2<double>(w, h)))
              occupied = true;
          }
          occupancyMapCC[w + h * omCCWidth] = occupied;
        }
      }
#  if ORTHOATLAS_ADJ_DEBUG
      if (params.keepIntermediateFiles) {
        for (int h = 0; h < omCCHeight; h++)
          for (int w = 0; w < omCCWidth; w++)
            occupancyVideoCC.plane(0).set(
              h, w, 255 * occupancyMapCC[w + h * omCCWidth]);
        auto prefix = keepFilesPathPrefix_ + "fr"
                      + std::to_string(frame.frameIndex) + "_occ_patch_"
                      + std::to_string(idxCC) + "_" + std::to_string(omCCWidth)
                      + "x" + std::to_string(omCCHeight) + "_bgrp";
        occupancyVideoCC.saveImage(prefix + ".png");
      }
#  endif
      // now check if the initial U0,V0 position is correct, if not search for the best position in a (delta) radius
      bool foundPosition = false;
      int  adjU0;
      int  adjV0;
      //spiral search
      int x   = 0;
      int y   = 0;
      int end = 9;  //(-1,+1)
      int minWrongPosition =
        omCCWidth * omCCHeight;  // all the values are wrong
      for (int i = 0; i < end && !foundPosition; ++i) {
#  if ORTHOATLAS_ADJ_DEBUG
        vmesh::Frame<uint8_t> occupancyVideo;
        if (params.keepIntermediateFiles) {
          occupancyVideo.resize(
            omWidth, omHeight, vmesh::ColourSpace::BGR444p);
          for (int h = 0; h < omHeight; h++)
            for (int w = 0; w < omWidth; w++)
              occupancyVideo.plane(0).set(
                h, w, 255 * occupancyMap[w + h * omWidth]);
        }
#  endif
        int wrongPosition = 0;
        int vMap          = (newV0 + y) * occupancyBlockSize * patchScaling;
        int uMap          = (newU0 + x) * occupancyBlockSize * patchScaling;
        if (vMap < 0 || vMap >= omHeight)
          wrongPosition =
            omCCWidth
            * omCCHeight;  // this would generate U0,V0 outside of the image, which is not allowed
        else if (uMap < 0 || uMap >= omWidth)
          wrongPosition =
            omCCWidth
            * omCCHeight;  // this would generate U0,V0 outside of the image, which is not allowed
        for (int v = 0; v < omCCHeight; v++) {
          for (int u = 0; u < omCCWidth; u++) {
            if (occupancyMapCC[u + v * omCCWidth]) {
              vMap = v + (newV0 + y) * occupancyBlockSize * patchScaling;
              uMap = u + (newU0 + x) * occupancyBlockSize * patchScaling;
              if (vMap < 0 || vMap >= omHeight) wrongPosition++;
              else if (uMap < 0 || uMap >= omWidth) wrongPosition++;
              else {
                if (occupancyMap[uMap + vMap * omWidth]) {
                  wrongPosition++;
#  if ORTHOATLAS_ADJ_DEBUG
                  if (params.keepIntermediateFiles)
                    occupancyVideo.plane(1).set(vMap, uMap, 255);
#  endif
                }
#  if ORTHOATLAS_ADJ_DEBUG
                else {
                  if (params.keepIntermediateFiles)
                    occupancyVideo.plane(2).set(vMap, uMap, 255);
                }
#  endif
              }
            }
          }
        }
#  if ORTHOATLAS_ADJ_DEBUG
        if (params.keepIntermediateFiles) {
          auto prefix = keepFilesPathPrefix_ + "fr"
                        + std::to_string(frame.frameIndex) + "_occ_patch_"
                        + std::to_string(idxCC) + "_" + std::to_string(x) + "-"
                        + std::to_string(y) + "_" + std::to_string(omWidth)
                        + "x" + std::to_string(omHeight) + "_bgrp";
          occupancyVideo.saveImage(prefix + ".png");
        }
#  endif
        if (wrongPosition == 0) {
          foundPosition = true;
          adjU0         = newU0 + x;
          adjV0         = newV0 + y;
#  if ORTHOATLAS_ADJ_DEBUG
          std::cout << "Delta(" << idxCC << ") = [" << x << ", " << y
                    << "] achieved 0 errors" << std::endl;
          std::cout << "Changing [" << newU0 << "," << newV0 << "] -> ["
                    << adjU0 << ", " << adjV0 << "]" << std::endl;
#  endif
        } else if (wrongPosition < minWrongPosition) {
          adjU0            = newU0 + x;
          adjV0            = newV0 + y;
          minWrongPosition = wrongPosition;
#  if ORTHOATLAS_ADJ_DEBUG
          std::cout << "Delta(" << idxCC << ")=[" << x << "," << y
                    << "] chosen with " << wrongPosition << " errors"
                    << std::endl;
          std::cout << "Possible change [" << newU0 << "," << newV0 << "] -> ["
                    << adjU0 << ", " << adjV0 << "]" << std::endl;
#  endif
        } else {
#  if ORTHOATLAS_ADJ_DEBUG
          std::cout << "Delta(" << idxCC << ")=[" << x << "," << y
                    << "] NOT chosen with " << wrongPosition << " errors"
                    << std::endl;
#  endif
        }
        //spiral search
        if (abs(x) <= abs(y) && (x != y || x >= 0)) {
          x += ((y >= 0) ? 1 : -1);
        } else {
          y += ((x >= 0) ? -1 : 1);
        }
      }
      //update the connected component
      decodedCC[idxCC].setU0(adjU0);
      decodedCC[idxCC].setV0(adjV0);
      frame.packedCCList[idxCC].setU0(adjU0);
      frame.packedCCList[idxCC].setV0(adjV0);

      // now copy the CC occupancy to the occupancy map and update the U0/V0 values
#  if ORTHOATLAS_ADJ_DEBUG
      std::cout << "Pos2D(" << idxCC << ")=[" << decodedCC[idxCC].getU0()
                << "," << decodedCC[idxCC].getV0() << "]" << std::endl;
#  endif
      for (int v = 0; v < omCCHeight; v++) {
        for (int u = 0; u < omCCWidth; u++) {
          int vMap = v + adjV0 * occupancyBlockSize * patchScaling;
          int uMap = u + adjU0 * occupancyBlockSize * patchScaling;
          if (vMap < 0) vMap = 0;
          else if (vMap >= omHeight) vMap = omHeight - 1;
          if (uMap < 0) uMap = 0;
          if (uMap >= omWidth) uMap = omWidth - 1;
          occupancyMap[uMap + vMap * omWidth] =
            (occupancyMapCC[u + v * omCCWidth]
             || occupancyMap[uMap + vMap * omWidth]);
        }
      }
      if ((oldSizeU != newSizeU) || (oldSizeV != newSizeV))
        std::cout << "Updating Connected Component(" << idxCC << ") size ["
                  << oldSizeU << "," << oldSizeV << "] -> [" << newSizeU
                  << ", " << newSizeV << "]" << std::endl;
      if ((oldU0 != adjU0) || (oldV0 != adjV0))
        std::cout << "Updating Connected Component(" << idxCC << ") position ["
                  << oldU0 << "," << oldV0 << "] -> [" << adjU0 << ", "
                  << adjV0 << "]" << std::endl;
      if ((newSizeU < 0) || (newSizeV < 0)) exit(-1);
      if ((adjU0 < 0) || (newSizeV < 0)) exit(-1);
    }
#endif
  }
  // create the (u,v) coordinate from (x,y,z) and the corresponding homography transform
  base.texCoordTriangles().resize(base.triangleCount());
  std::vector<int> codedUVindex(numCC, 0);
  for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
    auto tri   = base.triangle(triIdx);
    int  idxCC = trianglePartition[triIdx];
    int  uvIdx[3];
    int  rawUvId = params.use45DegreeProjection ? 18 : 6;
    if (decodedCC[idxCC].getProjection() == rawUvId) {
      for (int i = 0; i < 3; i++) {
        if (codedUVindex[idxCC]
            > frame.packedCCList[idxCC].getNumRawUVMinus1()) {
          std::cout << "error : codedUVindex > "
                       "frame.packedCCList[idxCC].getNumRawUVMinus1()"
                    << std::endl;
        }
        float UV[2];
        UV[0] = frame.packedCCList[idxCC].getUcoord(codedUVindex[idxCC]);
        UV[1] = frame.packedCCList[idxCC].getVcoord(codedUVindex[idxCC]);
        int     qpTexCoord    = params.rawTextcoordBitdepth;
        int32_t scaleTexCoord = (1u << qpTexCoord) - 1;
        //quantize UV
        Vec2<int> quantUV;
        quantUV[0] = std::round(UV[0] * scaleTexCoord);
        quantUV[1] = std::round(UV[1] * scaleTexCoord);
        frame.packedCCList[idxCC].setUcoordQuant(codedUVindex[idxCC],
                                                 quantUV[0]);
        frame.packedCCList[idxCC].setVcoordQuant(codedUVindex[idxCC],
                                                 quantUV[1]);
        //dequantize
        UV[0] = (float)quantUV[0] / (float)scaleTexCoord;
        UV[1] = (float)quantUV[1] / (float)scaleTexCoord;
        frame.packedCCList[idxCC].setUcoord(codedUVindex[idxCC], UV[0]);
        frame.packedCCList[idxCC].setVcoord(codedUVindex[idxCC], UV[1]);
        uvIdx[i] = base.texCoords().size();
        Vec2<float> temp;
        temp[0] = UV[0];
        temp[1] = UV[1];
        base.addTexCoord(temp);
        codedUVindex[idxCC]++;
      }
    } else {
      for (int i = 0; i < 3; i++) {
        auto newUV =
          decodedCC[idxCC].convert(base.point(tri[i]),
                                   texParamWidth,
                                   texParamHeight,
                                   params.gutter,
                                   (1 << params.log2GeometryVideoBlockSize));
        //check if the newUV already exist
        auto pos =
          std::find(base.texCoords().begin(), base.texCoords().end(), newUV);
        if (pos == base.texCoords().end()) {
          uvIdx[i] = base.texCoords().size();
          base.addTexCoord(newUV);
        } else {
          uvIdx[i] = pos - base.texCoords().begin();
        }
      }
    }
    //for degenerate triangles in 2D, since it can happen, but the saveOBJ function complains, we will create a new UV coordinate
    if (uvIdx[0] == uvIdx[1] && uvIdx[0] == uvIdx[2]) {
      uvIdx[1] = base.texCoords().size();
      base.addTexCoord(base.texCoords()[uvIdx[0]]);
      uvIdx[2] = base.texCoords().size();
      base.addTexCoord(base.texCoords()[uvIdx[0]]);
    } else if (uvIdx[0] == uvIdx[1]) {
      uvIdx[1] = base.texCoords().size();
      base.addTexCoord(base.texCoords()[uvIdx[0]]);
    } else if (uvIdx[0] == uvIdx[2]) {
      uvIdx[2] = base.texCoords().size();
      base.addTexCoord(base.texCoords()[uvIdx[0]]);
    } else if (uvIdx[1] == uvIdx[2]) {
      uvIdx[2] = base.texCoords().size();
      base.addTexCoord(base.texCoords()[uvIdx[1]]);
    }
    base.setTexCoordTriangle(triIdx,
                             vmesh::Triangle(uvIdx[0], uvIdx[1], uvIdx[2]));
  }
  // Save intermediate files
  if (params.keepIntermediateFiles) {
    std::string prefix =
      _keepFilesPathPrefix + "fr_" + std::to_string(frame.frameIndex);
    base.save(prefix + "_base_recon_uv.ply");
  }
}

