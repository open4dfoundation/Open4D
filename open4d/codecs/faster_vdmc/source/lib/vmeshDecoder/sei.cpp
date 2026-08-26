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
#include <string>
#include "vmc.hpp"
#include "v3cBitstream.hpp"
#include "decoder.hpp"
using namespace vmesh;

//this function was copied from AtlasDataDecoder
size_t
VMCDecoder::calculateAFOCvalForZippering(
  AtlasBitstream&               amStream,
  std::vector<AtlasTileLayerRbsp>& atglList,
  size_t                           atglOrder) {
  // 8.2.3.1 Atals frame order count derivation process
  if (atglOrder == 0) {
    atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb(0);
    atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal(
      atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb());
    return atglList[atglOrder].getHeader().getAtlasFrmOrderCntLsb();
  }

  size_t prevAtlasFrmOrderCntMsb =
    atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntMsb();
  size_t atlasFrmOrderCntMsb = 0;
  auto&  atgh                = atglList[atglOrder].getHeader();
  auto&  afps =
    amStream.getAtlasFrameParameterSet(atgh.getAtlasFrameParameterSetId());
  auto& asps = amStream.getAtlasSequenceParameterSet(
    afps.getAtlasSequenceParameterSetId());

  size_t maxAtlasFrmOrderCntLsb =
    size_t(1) << (asps.getLog2MaxAtlasFrameOrderCntLsbMinus4() + 4);
  size_t afocLsb = atgh.getAtlasFrmOrderCntLsb();
  size_t prevAtlasFrmOrderCntLsb =
    atglList[atglOrder - 1].getHeader().getAtlasFrmOrderCntLsb();
  if ((afocLsb < prevAtlasFrmOrderCntLsb)
      && ((prevAtlasFrmOrderCntLsb - afocLsb) >= (maxAtlasFrmOrderCntLsb / 2)))
    atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb + maxAtlasFrmOrderCntLsb;
  else if ((afocLsb > prevAtlasFrmOrderCntLsb)
           && ((afocLsb - prevAtlasFrmOrderCntLsb)
               > (maxAtlasFrmOrderCntLsb / 2)))
    atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb - maxAtlasFrmOrderCntLsb;
  else atlasFrmOrderCntMsb = prevAtlasFrmOrderCntMsb;

  atglList[atglOrder].getHeader().setAtlasFrmOrderCntMsb(atlasFrmOrderCntMsb);
  atglList[atglOrder].getHeader().setAtlasFrmOrderCntVal(atlasFrmOrderCntMsb
                                                         + afocLsb);
#if 0
    printf("atlPos: %zu\t afocVal: %zu\t afocLsb: %zu\t maxCntLsb:(%zu)\t prevCntLsb:%zu atlasFrmOrderCntMsb: %zu\n", atglOrder, atlasFrmOrderCntMsb + afocLsb, afocLsb, maxAtlasFrmOrderCntLsb,
        prevAtlasFrmOrderCntLsb, atlasFrmOrderCntMsb);
#endif
  return atlasFrmOrderCntMsb + afocLsb;
}

void
VMCDecoder::zippering_decode(AtlasBitstream&          adStream,
                             const VMCDecoderParameters& params) {
  if (!params.processZipperingSEI) return;

  bool persist           = false;
  int  persistFrameIndex = -1;
  for (int32_t tileIdx = 0; tileIdx < adStream.getAtlasTileLayerList().size();
       ++tileIdx) {
    // read parameters from SEI message
    auto& atlList = adStream.getAtlasTileLayerList();
    auto& atl     = atlList[tileIdx];
    bool  loadZippering =
      atl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, ZIPPERING);
    int32_t frameIndexCalc =
      (int32_t)calculateAFOCvalForZippering(adStream, atlList, tileIdx);
    if (loadZippering) {
      auto& sei = static_cast<SEIZippering&>(
        *atl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, ZIPPERING));
      persist = sei.getPersistenceFlag();
      if (persist) persistFrameIndex = frameIndexCalc;
      for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
        size_t k = sei.getInstanceIndex(i);
        if (!sei.getInstanceCancelFlag(k)) {
          if (sei.getMethodType(k) == 1) {
            //distance-based zippering
            applyZippering_[frameIndexCalc] = 1;  // single value per-frame
            zipperingMatchMaxDistancePerFrame_[frameIndexCalc] =
              sei.getZipperingMaxMatchDistance(k);
            zipperingLinearSegmentation_ =
              sei.getZipperingLinearSegmentation(k);
            if (sei.getZipperingSendDistancePerSubmesh(k)) {
              applyZippering_[frameIndexCalc] =
                2;  // single value per sub-mesh
              auto numSubmeshes =
                sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
              zipperingMatchMaxDistancePerSubmesh_[frameIndexCalc].resize(
                numSubmeshes, 0);
              zipperingDistanceBorderPoint_[frameIndexCalc].resize(
                numSubmeshes);
              zipperingMatchedBorderPoint_[frameIndexCalc].resize(
                numSubmeshes);
              for (int s = 0; s < numSubmeshes; s++) {
                zipperingMatchMaxDistancePerSubmesh_[frameIndexCalc][s] =
                  sei.getZipperingMaxMatchDistancePerSubmesh(k, s);
                if (sei.getZipperingSendDistancePerBorderPoint(k, s)) {
                  applyZippering_[frameIndexCalc] =
                    3;  // single value per border-point
                  auto numBorders = sei.getZipperingNumberOfBorderPoints(k, s);
                  zipperingDistanceBorderPoint_[frameIndexCalc][s].resize(
                    numBorders, 0);
                  zipperingMatchedBorderPoint_[frameIndexCalc][s].resize(
                    numBorders);
                  for (int b = 0; b < numBorders; b++) {
                    zipperingDistanceBorderPoint_[frameIndexCalc][s][b] =
                      sei.getZipperingDistancePerBorderPoint(k, s, b);
                  }
                }
              }
            } else {
              if (sei.getZipperingSendDistancePerSubmeshPair(k)) {
                applyZippering_[frameIndexCalc] = 4;  //value per submesh-pair
                auto numSubmeshes =
                  sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
                zipperingLinearSegmentation_ =
                  sei.getZipperingLinearSegmentation(k);
                zipperingMatchedBorderPoint_[frameIndexCalc].resize(
                  numSubmeshes);
                zipperingMatchMaxDistancePerSubmeshPair_[frameIndexCalc]
                  .resize(numSubmeshes);
                for (int p = 0; p < numSubmeshes; p++) {
                  zipperingMatchMaxDistancePerSubmeshPair_[frameIndexCalc][p]
                    .resize(numSubmeshes - 1 - p, 0);
                  for (int t = p + 1; t < numSubmeshes; t++) {
                    zipperingMatchMaxDistancePerSubmeshPair_
                      [frameIndexCalc][p][t - p - 1] =
                        sei.getZipperingMaxMatchDistancePerSubmeshPair(
                          k, p, t - p - 1);
                  }
                }
              }
            }
          } else if (sei.getMethodType(k) == 2) {
            //distance-based zippering
            applyZippering_[frameIndexCalc] = 5;
            methodForUnmatchedLoDs_[frameIndexCalc] =
              sei.getMethodForUnmatchedLoDs(k);
            auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
            zipperingMatchedBorderPoint_[frameIndexCalc].resize(numSubmeshes);
            for (int s = 0; s < numSubmeshes; s++) {
              auto numBorders = sei.getZipperingNumberOfBorderPoints(k, s);
              zipperingMatchedBorderPoint_[frameIndexCalc][s].resize(
                numBorders);
              for (int b = 0; b < numBorders; b++) {
                auto matchedSubmeshIdx =
                  sei.getZipperingBorderPointMatchSubmeshIndex(k, s, b);
                auto matchedBorderIdx =
                  sei.getZipperingBorderPointMatchBorderIndex(k, s, b);
                zipperingMatchedBorderPoint_[frameIndexCalc][s][b] =
                  Vec2<size_t>(matchedSubmeshIdx, matchedBorderIdx);
              }
            }
          } else {
            applyZippering_[frameIndexCalc] = 6;
            auto numSubmeshes = sei.getZipperingNumberOfSubmeshesMinus1(k) + 1;
            boundaryIndex_[frameIndexCalc].resize(numSubmeshes);
            crackCount_[frameIndexCalc].resize(numSubmeshes);
            for (int p = 0; p < numSubmeshes; p++) {
              crackCount_[frameIndexCalc][p] =
                sei.getcrackCountPerSubmesh(k, p);
              auto numCrackCount = sei.getcrackCountPerSubmesh(k, p);
              boundaryIndex_[frameIndexCalc][p].resize(numCrackCount);
              for (int c = 0; c < numCrackCount; c++) {
                boundaryIndex_[frameIndexCalc][p][c].resize(3);
                for (int b = 0; b < 3; b++) {
                  boundaryIndex_[frameIndexCalc][p][c][b] =
                    sei.getboundaryIndex(k, p, c, b);
                }
              }
            }
          }
        } else {
          //set values to default
        }
      }
    } else {
      if (persist) {
        // copy structure from previous frame
        zipperingMatchMaxDistancePerFrame_[frameIndexCalc] =
          zipperingMatchMaxDistancePerFrame_[persistFrameIndex];
        zipperingMatchMaxDistancePerSubmesh_[frameIndexCalc] =
          zipperingMatchMaxDistancePerSubmesh_[persistFrameIndex];
        zipperingMatchMaxDistancePerSubmeshPair_[frameIndexCalc] =
          zipperingMatchMaxDistancePerSubmeshPair_[persistFrameIndex];
        zipperingDistanceBorderPoint_[frameIndexCalc] =
          zipperingDistanceBorderPoint_[persistFrameIndex];
        zipperingMatchedBorderPoint_[frameIndexCalc] =
          zipperingMatchedBorderPoint_[persistFrameIndex];
        applyZippering_[frameIndexCalc] = applyZippering_[persistFrameIndex];
      }
    }
  }
}

void
VMCDecoder::zippering_reconstruct(int32_t                  frameIdx,
                                  std::vector<VMCDecMesh>& recMeshes,  //frame,
                                  int32_t                  submeshCount,
                                  const VMCDecoderParameters& params) {
  auto& applyZippering = applyZippering_;

  //copy the submeshes locally
  std::vector<TriangleMesh<MeshType>> submeshes;
  submeshes.resize(submeshCount);
  std::vector<std::vector<int32_t>> frameSubmeshLodMaps;
  frameSubmeshLodMaps.resize(submeshCount);
  for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    submeshes[submeshIdx] = recMeshes[submeshIdx].rec;
    if (applyZippering[frameIdx] == 5 && methodForUnmatchedLoDs_[frameIdx]) {
      auto submeshInfoLevelOfDetails =
        recMeshes[submeshIdx].subdivInfoLevelOfDetails;
      int initialIdx = 0;
      for (int it = 0; it < submeshInfoLevelOfDetails.size(); it++) {
        for (int v = initialIdx; v < submeshInfoLevelOfDetails[it].pointCount;
             v++) {
          frameSubmeshLodMaps[submeshIdx].push_back(it);
        }
        initialIdx = submeshInfoLevelOfDetails[it].pointCount;
      }
    }
  }
  // find the borders
  std::vector<std::vector<int8_t>> isBoundaryVertex;
  std::vector<size_t>              numBoundaries;
  TriangleMesh<MeshType>           boundaryVertices;
  vmesh::zippering_find_borders(
    submeshes, isBoundaryVertex, numBoundaries, boundaryVertices);
  methodForUnmatchedLoDs_[frameIdx] =
    applyZippering[frameIdx] == 5 ? methodForUnmatchedLoDs_[frameIdx] : 0;
  switch (applyZippering[frameIdx]) {
  case 1:  // max value per frame ->
    // copy the value to the input structure
    zipperingDistanceBorderPoint_[frameIdx].resize(submeshCount);
    zipperingMatchedBorderPoint_[frameIdx].resize(submeshCount);
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      zipperingDistanceBorderPoint_[frameIdx][submeshIdx].resize(
        numBoundaries[submeshIdx]);
      zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
        numBoundaries[submeshIdx]);
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
                                 methodForUnmatchedLoDs_[frameIdx]);
    break;
  case 2:  // max value per sub-mesh ->
    // copy the value to the input structure
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      zipperingDistanceBorderPoint_[frameIdx][submeshIdx].resize(
        numBoundaries[submeshIdx]);
      zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
        numBoundaries[submeshIdx]);
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
                                 methodForUnmatchedLoDs_[frameIdx]);
    break;
  case 3:  // max value per border point
    // check if values are sent, if not they will assume to be zero
      for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
          if (zipperingDistanceBorderPoint_[frameIdx][submeshIdx].size() != numBoundaries[submeshIdx]) {
              int startBorderIdx = zipperingDistanceBorderPoint_[frameIdx][submeshIdx].size();
              zipperingDistanceBorderPoint_[frameIdx][submeshIdx].resize(
                  numBoundaries[submeshIdx]);
              for (int32_t borderIdx = startBorderIdx; borderIdx < numBoundaries[submeshIdx];
                  borderIdx++) {
                  zipperingDistanceBorderPoint_[frameIdx][submeshIdx][borderIdx] = 0;
              }
          }
          if (zipperingMatchedBorderPoint_[frameIdx][submeshIdx].size() != numBoundaries[submeshIdx]) {
              zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
                  numBoundaries[submeshIdx]);
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
                                 methodForUnmatchedLoDs_[frameIdx]);
    break;
  case 4:
    // search
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      zipperingMatchedBorderPoint_[frameIdx][submeshIdx].resize(
        numBoundaries[submeshIdx]);
    }
    vmesh::zippering_find_matches_distance_per_submesh_pair(
      submeshes,
      boundaryVertices,
      numBoundaries,
      zipperingMatchMaxDistancePerSubmeshPair_[frameIdx],
      zipperingMatchedBorderPoint_[frameIdx],
      zipperingLinearSegmentation_);
    vmesh::zippering_fuse_border(submeshes,
                                 isBoundaryVertex,
                                 zipperingMatchedBorderPoint_[frameIdx],
                                 frameSubmeshLodMaps,
                                 methodForUnmatchedLoDs_[frameIdx]);
    break;
  case 5:  // explicit matched border points
    // fuse the matched border vertices together
    vmesh::zippering_fuse_border(submeshes,
                                 isBoundaryVertex,
                                 zipperingMatchedBorderPoint_[frameIdx],
                                 frameSubmeshLodMaps,
                                 methodForUnmatchedLoDs_[frameIdx]);
    break;
  case 6: {  //boundary connect zippering
    std::vector<std::vector<std::vector<int>>> growuplist;
    std::vector<std::vector<std::vector<int>>> triangle_grow;
    std::vector<std::vector<size_t>>           crackIndex;
    std::vector<TriangleMesh<MeshType>>        submeshes_temp;
    submeshes_temp = submeshes;
    vmesh::boundaryupdate(submeshes,
                          isBoundaryVertex,
                          growuplist,
                          triangle_grow,
                          boundaryIndex_[frameIdx],
                          crackCount_[frameIdx],
                          crackIndex);
    vmesh::boundaryfuse(submeshes, growuplist, triangle_grow, crackIndex);
    vmesh::addtexture(submeshes, submeshes_temp);
    for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
      submeshes[submeshIdx].resizeNormals(submeshes[submeshIdx].pointCount());
      submeshes[submeshIdx].computeNormals();
    }
    break;
  }
  }
  //copy the results back
  for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    recMeshes[submeshIdx].rec = submeshes[submeshIdx];
  }
}
