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

#include "decoder.hpp"

#include <cstdio>
#include <sstream>

#include "../atlasDecoder/atlasDecoder.hpp"
#include "vmc.hpp"

using namespace vmesh;

//============================================================================
//v4.0_integration : 0dc060ea8289149262d6ce1ac680577ca50c02e4
//============================================================================


int32_t
VMCDecoder::reconstructBaseMeshUvCoordinates(
  TriangleMesh<MeshType>&              base,
  int32_t                              submeshIndex,
  int32_t                              frameIndex,
  const V3CParameterSet&               vps,
  const AtlasSequenceParameterSetRbsp& asps,
  const AtlasFrameParameterSetRbsp&    afps,
  const AtlasPatch&                      decodedAtlasPatch) {
  //auto&                          base = frame.base;
  if (decodedAtlasPatch.projectionTextcoordMode == 0) {
    return 0;  // don't generate texture coordinates
  }
  // create the connected components
  int numCC = decodedAtlasPatch.projectionTextcoordSubpatchCountMinus1_ + 1;
  std::vector<vmesh::ConnectedComponent<double>> connectedComponents;
  std::vector<vmesh::Box2<double>>               bbBoxes;
  std::vector<int>                               trianglePartition;
  connectedComponents.resize(numCC);
  bbBoxes.resize(numCC);
  trianglePartition.resize(base.triangleCount(), -1);
  int orthMapping = -1;
  if (asps.getAsveExtension().getAsveProjectionTexcoordMappingAttributeIndexPresentFlag()) {
    int attrIdx =
      asps.getAsveExtension().getAsveProjectionTexcoordMappingAttributeIndex();
    int atlId = 0;
    if (vps.getVpsVdmcExtension().getVpsExtMeshAttributeType(atlId, attrIdx)
        == (uint8_t)basemesh::BASEMESH_ATTRIBUTE_FACEGROUPID) {
      orthMapping = 1;  // using faceID
    } else if (vps.getVpsVdmcExtension().getVpsExtMeshAttributeType(atlId,
                                                                    attrIdx)
               == (uint8_t)basemesh::BASEMESH_ATTRIBUTE_TEXCOORD)
      orthMapping = 0;  // using UV coordinates
  } else orthMapping = 2;
  switch (orthMapping) {
  default:
  case 0:  // uv coordinate is being used for transmitting the triangle to patch mapping
  {
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri    = base.triangle(triIdx);
      auto texTri = base.texCoordTriangle(triIdx);
      int  faceId = base.texCoord(texTri[0])[0];
      int  idxCC  = -1;
      for (int i = 0;
           i < decodedAtlasPatch.projectionTextcoordSubpatches_.size();
           i++) {
        auto& subpatch = decodedAtlasPatch.projectionTextcoordSubpatches_[i];
        if (faceId == subpatch.projectionTextcoordFaceId_) {
          idxCC = i;
          i     = decodedAtlasPatch.projectionTextcoordSubpatches_.size();
        }
      }
      //now add the points
      trianglePartition[triIdx] = idxCC;
      for (int i = 0; i < 3; i++) {
        connectedComponents[idxCC].addPoint(base.point(tri[i]));
      }
      connectedComponents[idxCC].addTriangle(
        connectedComponents[idxCC].points().size() - 3,
        connectedComponents[idxCC].points().size() - 2,
        connectedComponents[idxCC].points().size() - 1);
    }
    break;
  }
  case 1:  // face ID is being used for transmitting the triangle to patch mapping
  {
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri    = base.triangle(triIdx);
      auto faceId = base.faceId(triIdx);
      int  idxCC  = -1;
      for (int i = 0;
           i < decodedAtlasPatch.projectionTextcoordSubpatches_.size();
           i++) {
        auto& subpatch = decodedAtlasPatch.projectionTextcoordSubpatches_[i];
        if (faceId == subpatch.projectionTextcoordFaceId_) {
          idxCC = i;
          i     = decodedAtlasPatch.projectionTextcoordSubpatches_.size();
        }
      }  //now add the points
      trianglePartition[triIdx] = idxCC;
      for (int i = 0; i < 3; i++) {
        connectedComponents[idxCC].addPoint(base.point(tri[i]));
      }
      connectedComponents[idxCC].addTriangle(
        connectedComponents[idxCC].points().size() - 3,
        connectedComponents[idxCC].points().size() - 2,
        connectedComponents[idxCC].points().size() - 1);
    }
    break;
  }
  case 2:  // connected components is being used for implicit triangle to patch mapping
  {
    std::vector<int> partition;
    bool                 flag = false;
    std::vector<int32_t> oldToNewTriangleIndex;
    std::vector<int32_t> posMapping; 
    ExtractConnectedComponents(base.triangles(),
                               base.pointCount(),
                               base,
                               partition,
                               posMapping,
                               oldToNewTriangleIndex,
                               flag);
    std::vector<vmesh::ConnectedComponent<double>> unsortedConnectedComponents;
    unsortedConnectedComponents.resize(connectedComponents.size());
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri   = base.triangle(triIdx);
      int  idxCC = partition[triIdx];
      //now add the points
      trianglePartition[triIdx] = idxCC;
      for (int i = 0; i < 3; i++) {
        unsortedConnectedComponents[idxCC].addPoint(base.point(tri[i]));
      }
      unsortedConnectedComponents[idxCC].addTriangle(
        unsortedConnectedComponents[idxCC].points().size() - 3,
        unsortedConnectedComponents[idxCC].points().size() - 2,
        unsortedConnectedComponents[idxCC].points().size() - 1);
    }
    // index sorting
    std::vector<int> sortedIndex(numCC);
    for (int val = 0; val < numCC; val++) sortedIndex[val] = val;
    std::sort(sortedIndex.begin(), sortedIndex.end(), [&](int a, int b) {
      return (unsortedConnectedComponents[a].area()
              > unsortedConnectedComponents[b].area());
    });
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      int idxCC = partition[triIdx];
      //now add the points
      trianglePartition[triIdx] =
        std::find(sortedIndex.begin(), sortedIndex.end(), idxCC)
        - sortedIndex.begin();
    }
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto tri   = base.triangle(triIdx);
      int  idxCC = trianglePartition[triIdx];
      //now add the points
      for (int i = 0; i < 3; i++) {
        connectedComponents[idxCC].addPoint(base.point(tri[i]));
      }
      connectedComponents[idxCC].addTriangle(
        connectedComponents[idxCC].points().size() - 3,
        connectedComponents[idxCC].points().size() - 2,
        connectedComponents[idxCC].points().size() - 1);
    }
    break;
  }
  }
  // remove the UVs
  base.texCoords().clear();
  base.texCoordTriangles().clear();
  // create the homography transform
  double packScale =
    asps.getAsveExtension().getAspsProjectionTexcoordUpscaleFactor() / asps.getAsveExtension().getAspsProjectionTexcoordDownscaleFactor();
  int64_t frameUpscale  = decodedAtlasPatch.projectionTextcoordFrameUpscale_;
  int    frameDownscale = decodedAtlasPatch.projectionTextcoordFrameDownscale_;
  double frameScale =
    (double)frameUpscale / (double)std::pow(2, frameDownscale);
  for (int idxCC = 0; idxCC < numCC; idxCC++) {
    auto& subpatch = decodedAtlasPatch.projectionTextcoordSubpatches_[idxCC];
    connectedComponents[idxCC].setProjection(
      subpatch.projectionTextcoordProjectionId_);
    connectedComponents[idxCC].setOrientation(
      subpatch.projectionTextcoordOrientationId_);
    connectedComponents[idxCC].setU0(subpatch.projectionTextcoord2dPosX_);
    connectedComponents[idxCC].setV0(subpatch.projectionTextcoord2dPosY_);
    double patchScale = frameScale;
    if (subpatch.projectionTextcoordScalePresentFlag_) {
      for (int i = 0; i <= subpatch.projectionTextcoordSubpatchScale_; i++)
        patchScale *= packScale;
    }
    connectedComponents[idxCC].setScale(patchScale);
    int rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
    if (connectedComponents[idxCC].getProjection() == rawUvId) {
      connectedComponents[idxCC].setNumRawUVMinus1(subpatch.numRawUVMinus1_);
      for (uint32_t i = 0;
           i < connectedComponents[idxCC].getNumRawUVMinus1() + 1;
           i++) {
        connectedComponents[idxCC].setUcoord(i, subpatch.uCoords_[i]);
        connectedComponents[idxCC].setVcoord(i, subpatch.vCoords_[i]);
      }
    }
    if (asps.getAsveExtension().getAsveProjectionTexcoordBboxBiasEnableFlag()) {
      connectedComponents[idxCC].setU1(subpatch.projectionTextcoord2dBiasX_);
      connectedComponents[idxCC].setV1(subpatch.projectionTextcoord2dBiasY_);
      connectedComponents[idxCC].setSizeU(
        subpatch.projectionTextcoord2dSizeXMinus1_ + 1);
      connectedComponents[idxCC].setSizeV(
        subpatch.projectionTextcoord2dSizeYMinus1_ + 1);
    } else {
      // extract the values from the projected decoded bounding box
      auto gutter =
        afps.getAfveExtension().getProjectionTextcoordGutter(submeshIndex);
      auto occupancyBlockSize = (1 << asps.getLog2PatchPackingBlockSize());
      auto projection         = connectedComponents[idxCC].getProjection();
      auto bbCC = connectedComponents[idxCC].boundingBoxProjected(projection);
      auto bbScale =
        Box2<double>(bbCC.min * connectedComponents[idxCC].getScale(),
                     bbCC.max * connectedComponents[idxCC].getScale());
      auto bbScalePlusGutter =
        Box2<double>(bbScale.min, bbScale.max + Vec2<double>(2 * gutter));
      auto bbScalePlusGutterQuant = Box2<double>(
        Vec2<double>(
          std::floor(bbScalePlusGutter.min[0] / (double)occupancyBlockSize),
          std::floor(bbScalePlusGutter.min[1] / (double)occupancyBlockSize)),
        Vec2<double>(
          std::floor(bbScalePlusGutter.max[0] / (double)occupancyBlockSize),
          std::floor(bbScalePlusGutter.max[1] / (double)occupancyBlockSize)));
      int U1;
      if ((projection == 0) || (projection == 4) || (projection == 5)
          || (projection == 6) || (projection == 9) || (projection == 11)
          || (projection == 12) || (projection == 14) || (projection == 15)) {
        U1 = std::ceil(bbCC.max[0]);
      } else U1 = std::floor(bbCC.min[0]);
      int V1 = std::floor(bbCC.min[1]);
      int widthOccCC =
        std::ceil((bbScalePlusGutter.max[0] - bbScalePlusGutter.min[0])
                  / (double)occupancyBlockSize);
      int heightOccCC =
        std::ceil((bbScalePlusGutter.max[1] - bbScalePlusGutter.min[1])
                  / (double)occupancyBlockSize);
      connectedComponents[idxCC].setU1(U1);
      connectedComponents[idxCC].setV1(V1);
      connectedComponents[idxCC].setSizeU(widthOccCC);
      connectedComponents[idxCC].setSizeV(heightOccCC);
    }
#ifdef DEBUG_ORTHO
    if (params.keepIntermediateFiles) {
      auto prefix =
        keepFilesPathPrefix_ + "_transmitted_CC#" + std::to_string(idxCC);
      connectedComponents[idxCC].save(prefix + "_DEC.obj");
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
    int  rawUvId = asps.getExtendedProjectionEnabledFlag() ? 18 : 6;
    if (connectedComponents[idxCC].getProjection() == rawUvId) {
      for (int i = 0; i < 3; i++) {
        if (codedUVindex[idxCC]
            > connectedComponents[idxCC].getNumRawUVMinus1()) {
          std::cout << "error : codedUVindex > "
                       "frame.packedCCList[idxCC].getNumRawUVMinus1()"
                    << std::endl;
        }
        Vec2<double> UV;
        UV[0]    = connectedComponents[idxCC].getUcoord(codedUVindex[idxCC]);
        UV[1]    = connectedComponents[idxCC].getVcoord(codedUVindex[idxCC]);
        uvIdx[i] = base.texCoords().size();
        base.addTexCoord(UV);
        codedUVindex[idxCC]++;
      }
    } else {
      for (int i = 0; i < 3; i++) {
        auto newUV = connectedComponents[idxCC].convert(
          base.point(tri[i]),
          afps.getAfveExtension().getProjectionTextcoordWidth(submeshIndex),
          afps.getAfveExtension().getProjectionTextcoordHeight(submeshIndex),
          afps.getAfveExtension().getProjectionTextcoordGutter(submeshIndex),
          1 << asps
                 .getLog2PatchPackingBlockSize());  //we are using the same block size as the geometry, should we change and use a specific one for the projection???
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
  return 0;
}
