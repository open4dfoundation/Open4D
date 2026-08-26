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
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include <iostream>
#include <stdio.h>
#include "vmc.hpp"
#include "transferColor.hpp"
#include "colourConverter.hpp"

namespace vmesh {

//============================================================================
void
VMCEncoder::createCodecComponentMappingSei(
  V3cBitstream&               syntax,
  V3CParameterSet&            vps,
  const VMCEncoderParameters& params) {
  size_t atlasIndex = syntax.getAtlasId();
  auto&  ai         = vps.getAttributeInformation(atlasIndex);
  auto&  gi         = vps.getGeometryInformation(atlasIndex);
  bool   useHevc    = params.geometryVideoEncoderId == VideoEncoderId::HM
                 || params.attributeParameters[0].textureVideoEncoderId
                      == VideoEncoderId::HM;
  bool useShvc = params.geometryVideoEncoderId == VideoEncoderId::SHMAPP
                 || params.attributeParameters[0].textureVideoEncoderId
                      == VideoEncoderId::SHMAPP;
  bool useVvc = params.geometryVideoEncoderId == VideoEncoderId::VTM
                || params.attributeParameters[0].textureVideoEncoderId
                     == VideoEncoderId::VTM;
  bool useMpegBasemeshCodec = params.meshCodecId == GeometryCodecId::MPEG;
  bool useACDisplacement    = params.encodeDisplacementType == 1;

  printf(
    "CODEC ID = %d %d %d \n", gi.getGeometryCodecId(), ai.getAttributeCodecId(0), 
      vps.getVpsVdmcExtension().getVpsExtMeshDataSubstreamCodecId(atlasIndex));
  printf("ProfileCodecGroupIdc = CODEC_GROUP_MP4RA: HEVC = %d SHVC = %d VVC = "
         "%d MpegBasemeshCodec = %d ACDisp = %d \n",
         useHevc,
         useShvc,
         useVvc,
         useMpegBasemeshCodec,
         useACDisplacement);

  AtlasTileLayerRbsp& atl =
    syntax.getAtlasDataStream().getAtlasTileLayerList()[0];
  auto& sei = static_cast<SEIComponentCodecMapping&>(
    atl.getSEI().addSeiPrefix(COMPONENT_CODEC_MAPPING, true));
  sei.getComponentCodecCancelFlag(false);
  sei.getCodecMappingsCountMinus1(useHevc + useShvc + useVvc + useMpegBasemeshCodec + useACDisplacement + - 1);
  printf("sei.getCodecMappingsCountMinus1() = %u \n",
         sei.getCodecMappingsCountMinus1());
  sei.allocate();
  uint8_t index = 0;
  if (useHevc) {
    sei.getCodecId(index++, VideoEncoderId::HM);
    sei.getCodec4cc(VideoEncoderId::HM, "hev1");
  }
  if (useShvc) {
    sei.getCodecId(index++, VideoEncoderId::SHMAPP);
    sei.getCodec4cc(VideoEncoderId::SHMAPP, "hev3");
  }
  if (useVvc) {
    sei.getCodecId(index++, VideoEncoderId::VTM);
    sei.getCodec4cc(VideoEncoderId::VTM, "vvi1");
  }
  if (useMpegBasemeshCodec) {
    sei.getCodecId(index++, GeometryCodecId::MPEG);
    sei.getCodec4cc(GeometryCodecId::MPEG, "bmsf");
  }
  if (useACDisplacement) {
    sei.getCodecId(index, index);
    sei.getCodec4cc(index, "dacf");
  }
}

//============================================================================
void
VMCEncoder::addAttributeExtractionInformationSEI(
  int32_t                     frameCount,
  V3cBitstream&               syntax,
  const VMCEncoderParameters& params) {
  std::cout << "Attribute extraction information SEI message" << std::endl;
  AtlasTileLayerRbsp& atl =
    syntax.getAtlasDataStream().getAtlasTileLayerList()[0];  // frameIdx==0
  auto& sei = static_cast<SEIAttributeExtractionInformation&>(
    atl.getSEI().addSeiPrefix(ATTRIBUTE_EXTRACTION_INFORMATION, false));

  sei.getCancelFlag() = false;
  if (!sei.getCancelFlag()) {
    auto& adStream      = syntax.getAtlasDataStream();
    auto  afpsId        = atl.getHeader().getAtlasFrameParameterSetId();
    auto& afps          = adStream.getAtlasFrameParameterSet(afpsId);
    auto& afpsVdmcExt   = afps.getAfveExtension();
    auto& meshInfo      = afpsVdmcExt.getAtlasFrameMeshInformation();
    auto  paramsTexture = params.attributeParameters[0];

    // 0: MCTS , 1 : subpicture
    if (paramsTexture.textureVideoEncoderId == 0)
      sei.getExtractableUnitTypeIdx() = 0;  // HEVC MCTS
    else if (paramsTexture.textureVideoEncoderId == 1)
      sei.getExtractableUnitTypeIdx() = 1;  // VVC subpicture
    else std::cout << "not support" << std::endl;

    int numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
    int attributeCount =
      params.numTextures > 1 ? params.numTextures : params.videoAttributeCount;
    sei.getNumSubmeshCount() = (numSubmeshes - 1);
    sei.getAttributeCount()  = attributeCount;
    sei.allocate();
    for (int32_t submeshIdx = 0; submeshIdx < numSubmeshes; submeshIdx++) {
      sei.getSubmeshId(submeshIdx) =
        meshInfo.getSubmeshId(submeshIdx);  // submesh index
      for (int32_t attributeIdx = 0; attributeIdx < sei.getAttributeCount();
           attributeIdx++) {
        sei.getExtractionInfoPresentFlag(attributeIdx) = 1;
        if (sei.getExtractionInfoPresentFlag(attributeIdx)) {
          if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
            sei.getMCTSIdx(submeshIdx, attributeIdx) =
              attrMCTSidx[attributeIdx];
          } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
            sei.getsubpictureIdx(submeshIdx, attributeIdx) =
              attrSubpicIdx[attributeIdx];
          }
        }
      }
    }
  }
}

//============================================================================
void
VMCEncoder::addLoDExtractioninformationSEI(
  int32_t                     frameCount,
  V3cBitstream&               syntax,
  const VMCEncoderParameters& params) {
  std::cout << "LoD extraction information SEI message" << std::endl;
  AtlasTileLayerRbsp& atl =
    syntax.getAtlasDataStream().getAtlasTileLayerList()[0];  // frameIdx==0
  auto& sei = static_cast<SEILoDExtractionInformation&>(
    atl.getSEI().addSeiPrefix(LOD_EXTRACTION_INFORMATION, false));

  auto& adStream    = syntax.getAtlasDataStream();
  auto  afpsId      = atl.getHeader().getAtlasFrameParameterSetId();
  auto& afps        = adStream.getAtlasFrameParameterSet(afpsId);
  auto& afpsVdmcExt = afps.getAfveExtension();
  auto& meshInfo    = afpsVdmcExt.getAtlasFrameMeshInformation();

  // 0: MCTS , 1 : subpicture
  if (params.geometryVideoEncoderId == 0)
    sei.getExtractableUnitTypeIdx() = 0;  // HEVC MCTS
  else if (params.geometryVideoEncoderId == 1)
    sei.getExtractableUnitTypeIdx() = 1;  // VVC subpicture
  else std::cout << "not support" << std::endl;

  int numSubmeshes         = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
  sei.getNumSubmeshCount() = (numSubmeshes - 1);
  sei.allocate();
  for (int32_t submeshIdx = 0; submeshIdx < numSubmeshes; submeshIdx++) {
    sei.getSubmeshId(submeshIdx) =
      meshInfo.getSubmeshId(submeshIdx);  // submesh index
    sei.getSubmeshSubdivisionIterationCount(submeshIdx) =
      params.subdivisionIterationCount;
    sei.allocateSubdivisionIteractionCountPerSubmesh(
      submeshIdx, sei.getSubmeshSubdivisionIterationCount(submeshIdx));

    for (int32_t i = 0;
         i <= sei.getSubmeshSubdivisionIterationCount(submeshIdx);
         i++) {
      if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
        sei.getMCTSIdx(submeshIdx, i) = MCTSidx[i];
      } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
        sei.getsubpictureIdx(submeshIdx, i) = subpicIdx[i];
      }
    }
  }
}

//============================================================================
void
VMCEncoder::addZipperingSEI(int32_t                     frameCount,
                            V3cBitstream&               syntax,
                            const VMCEncoderParameters& params) {
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    auto tileCount = params.numTilesGeometry + params.numTilesAttribute;
    AtlasTileLayerRbsp& atl =
      syntax.getAtlasDataStream()
        .getAtlasTileLayerList()[frameIdx
                                 * tileCount];  //first tile of each frame
    // add SEI message
    bool addSEI = ((frameIdx == 0) || (params.zipperingMethod_ > 1));
    if (addSEI) {
      auto& sei =
        static_cast<SEIZippering&>(atl.getSEI().addSeiPrefix(ZIPPERING, true));
      sei.getPersistenceFlag()  = true;
      sei.getResetFlag()        = true;
      sei.getInstancesUpdated() = 1;
      sei.allocate();
      for (size_t i = 0; i < sei.getInstancesUpdated(); i++) {
        size_t k                     = i;
        sei.getInstanceIndex(i)      = k;
        sei.getInstanceCancelFlag(k) = false;
        int method;
        if (params.zipperingMethod_ == 6) method = 2;
        else if (params.zipperingMethod_ == 7) method = 3;
        else method = 1;
        if (method == 3 && zipperingSwitch_[frameIdx]) { method = 1; }
        sei.getMethodType(k) = method;
        int zipperingMethod = (params.zipperingMethod_ == 7 && zipperingSwitch_[frameIdx]) ? 3 : params.zipperingMethod_;
        switch (zipperingMethod) {
        case 0:  // send single user-defined value per sequence
        case 1:  // send encoder optimized value per sequence
          sei.getZipperingMaxMatchDistance(k) = zipperingMatchMaxDistance_;
          break;
        case 2:  // send value per frame
        case 3:  // send value per frame, per submesh
        case 4:
          sei.getZipperingMaxMatchDistance(k) =
            zipperingMatchMaxDistancePerFrame_[frameIdx];
          sei.getZipperingSendDistancePerSubmesh(k) =
            (5 > zipperingMethod && zipperingMethod > 2);
          if (sei.getZipperingSendDistancePerSubmesh(k)) {
            //allocate sub-meshes
            auto& adStream = syntax.getAtlasDataStream();
            auto  afpsId   = atl.getHeader().getAtlasFrameParameterSetId();
            auto& afps     = adStream.getAtlasFrameParameterSet(afpsId);
            auto& afve     = afps.getAfveExtension();
            auto& meshInfo = afve.getAtlasFrameMeshInformation();
            int   numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
            sei.getZipperingNumberOfSubmeshesMinus1(k) = numSubmeshes - 1;
            sei.allocateDistancePerSubmesh(k, numSubmeshes);
            for (int p = 0; p < numSubmeshes; p++) {
              sei.getZipperingMaxMatchDistancePerSubmesh(k, p) =
                zipperingMatchMaxDistancePerSubmesh_[frameIdx][p];
              sei.getZipperingSendDistancePerBorderPoint(k, p) =
                (6 > zipperingMethod && zipperingMethod > 3);
              if (sei.getZipperingSendDistancePerBorderPoint(k, p)) {
                int numBorderPoints =
                  zipperingDistanceBorderPoint_[frameIdx][p].size();
                sei.getZipperingNumberOfBorderPoints(k, p) = numBorderPoints;
                sei.allocateDistancePerBorderPoint(k, p, numBorderPoints);
                for (int b = 0; b < numBorderPoints; b++) {
                  sei.getZipperingDistancePerBorderPoint(k, p, b) =
                    zipperingDistanceBorderPoint_[frameIdx][p][b];
                }
              }
            }
          }
          break;
        case 5: {
          sei.getZipperingMaxMatchDistance(k) =
            zipperingMatchMaxDistancePerFrame_[frameIdx];
          sei.getZipperingLinearSegmentation(k) = params.linearSegmentation;
          sei.getZipperingSendDistancePerSubmesh(k)     = 0;
          sei.getZipperingSendDistancePerSubmeshPair(k) = 1;
          auto& adStream   = syntax.getAtlasDataStream();
          auto  afpsId     = atl.getHeader().getAtlasFrameParameterSetId();
          auto& afps       = adStream.getAtlasFrameParameterSet(afpsId);
          auto& afve       = afps.getAfveExtension();
          auto& meshInfo   = afve.getAtlasFrameMeshInformation();
          int numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
          sei.getZipperingNumberOfSubmeshesMinus1(k) = numSubmeshes - 1;
          sei.allocateDistancePerSubmeshPair(k, numSubmeshes);
          for (int p = 0; p < numSubmeshes; p++) {
            for (int t = p + 1; t < numSubmeshes; t++) {
              sei.getZipperingMaxMatchDistancePerSubmeshPair(k, p, t - p - 1) =
                zipperingMatchMaxDistancePerSubmeshPair_[frameIdx][p]
                                                        [t - p - 1];
            }
          }
        } break;
        case 6: {
          auto& adStream   = syntax.getAtlasDataStream();
          auto  afpsId     = atl.getHeader().getAtlasFrameParameterSetId();
          auto& afps       = adStream.getAtlasFrameParameterSet(afpsId);
          auto& afve       = afps.getAfveExtension();
          auto& meshInfo   = afve.getAtlasFrameMeshInformation();
          int numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
          sei.getMethodForUnmatchedLoDs(k) =
            params.zipperingMethodForUnmatchedLoDs_;
          sei.getZipperingNumberOfSubmeshesMinus1(k) = numSubmeshes - 1;
          sei.getZipperingDeltaFlag(k) =
            zipperingMatchedBorderPointDeltaVariance_[frameIdx][1]
            < params.zipperingMatchedBorderPointDeltaVarianceThreshold;
          sei.allocateMatchPerSubmesh(k, numSubmeshes);
          for (int p = 0; p < numSubmeshes; p++) {
            int numBorderPoints =
              zipperingDistanceBorderPoint_[frameIdx][p].size();
            sei.getZipperingNumberOfBorderPoints(k, p) = numBorderPoints;
            sei.allocateMatchPerBorderPoint(k, p, numBorderPoints);
            for (int b = 0; b < numBorderPoints; b++) {
              sei.getZipperingBorderPointMatchSubmeshIndex(k, p, b) =
                zipperingMatchedBorderPoint_[frameIdx][p][b][0];
              sei.getZipperingBorderPointMatchBorderIndex(k, p, b) =
                zipperingMatchedBorderPoint_[frameIdx][p][b][1];
              sei.getZipperingBorderPointMatchSubmeshIndexDelta(k, p, b) =
                zipperingMatchedBorderPointDelta_[frameIdx][p][b][0];
              sei.getZipperingBorderPointMatchBorderIndexDelta(k, p, b) =
                zipperingMatchedBorderPointDelta_[frameIdx][p][b][1];
            }
          }
        } break;
        case 7: {
          auto& adStream   = syntax.getAtlasDataStream();
          auto  afpsId     = atl.getHeader().getAtlasFrameParameterSetId();
          auto& afps       = adStream.getAtlasFrameParameterSet(afpsId);
          auto& afve       = afps.getAfveExtension();
          auto& meshInfo   = afve.getAtlasFrameMeshInformation();
          int numSubmeshes = meshInfo.getNumSubmeshesInAtlasFrameMinus1() + 1;
          sei.getZipperingNumberOfSubmeshesMinus1(k) = numSubmeshes - 1;
          sei.allocateboundaryPerSubmesh(k, numSubmeshes);
          for (int p = 0; p < numSubmeshes; p++) {
              sei.getcrackCountPerSubmesh(k, p) = crackCount_[frameIdx][p];
              auto numCrackCount = crackCount_[frameIdx][p];
              sei.allocateboundarycrack(k, p, numCrackCount);
              for (int c = 0; c < numCrackCount; c++) {
                  for (int b = 0; b < 3; b++) {
                      sei.getboundaryIndex(k, p, c, b) =
                          boundaryIndex_[frameIdx][p][c][b];
                  }
              }
          }
        } break;
        }
      }
    }
  }
}

//============================================================================
void
VMCEncoder::addTileSubmeshMappingSEI(int32_t                     frameCount,
                                     V3cBitstream&               syntax,
                                     const VMCEncoderParameters& params) {
  std::vector<std::vector<uint8_t>> submeshIDPerTile;
  auto                              geoTiles  = params.numTilesGeometry;
  auto                              attrTiles = params.numTilesAttribute;
  auto                              numTiles  = geoTiles + attrTiles;
  submeshIDPerTile.resize(geoTiles + attrTiles);

  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    bool sendSEI =
      (frameIdx
       == 0);  // only sending the SEI at the first frame (only INTRA patches are present)
    std::vector<uint32_t> tileID;
    std::vector<bool>     tileTypeFlag;
    for (int32_t tileIdx = 0; tileIdx < numTiles; tileIdx++) {
      AtlasTileLayerRbsp& atl =
        syntax.getAtlasDataStream()
          .getAtlasTileLayerList()[frameIdx * numTiles + tileIdx];
      auto& atd  = atl.getDataUnit();
      auto& ath  = atl.getHeader();
      auto& afps = syntax.getAtlasDataStream().getAtlasFrameParameterSet(
        ath.getAtlasFrameParameterSetId());
      if ((ath.getType() == SKIP_TILE) || (ath.getType() == P_TILE)
          || (ath.getType() == I_TILE)) {
        auto& afti = afps.getAtlasFrameTileInformation();
        tileID.push_back(afti.getTileId(tileIdx));
        tileTypeFlag.push_back(false);
      } else {
        auto& aftai =
          afps.getAfveExtension().getAtlasFrameTileAttributeInformation();
        tileID.push_back(aftai[0].getTileId(tileIdx));
        tileTypeFlag.push_back(true);
      }
      std::vector<uint8_t> submeshIDs;
      int32_t              predictorIdx = 0;
      for (int32_t patchIdx = 0; patchIdx < atd.getPatchCount(); patchIdx++) {
        auto  patchMode = atd.getPatchMode(patchIdx);
        auto& pid       = atd.getPatchInformationData(patchIdx);
        if (ath.getType() == SKIP_TILE) {
          // skip mode: currently not supported but added it for convenience. Could
          // easily be removed
        } else if (ath.getType() == P_TILE) {
          if (patchMode == P_SKIP) {
            //TODO
            auto referenceFrameIndex = ath.getReferenceList()[0];
            auto referencePatchIndex = predictorIdx;
            predictorIdx             = referencePatchIndex + 1;
          } else if (patchMode == P_INTRA) {
            auto& mdu = pid.getMeshpatchDataUnit();
            submeshIDs.push_back(mdu.getMduSubmeshId());
          } else if (patchMode == P_INTER) {
            auto& imdu = pid.getInterMeshpatchDataUnit();
            //TODO
            auto referenceFrameIndex =
              ath.getReferenceList()[imdu.getImduRefIndex()];
            auto referencePatchIndex = imdu.getImduPatchIndex() + predictorIdx;
            predictorIdx             = referencePatchIndex + 1;
          } else if (patchMode == P_MERGE) {
            auto& mdu = pid.getMergeMeshpatchDataUnit();
            //TODO
            auto referenceFrameIndex =
              ath.getReferenceList()[mdu.getMmduRefIndex()];
            auto referencePatchIndex = predictorIdx;
            predictorIdx             = referencePatchIndex + 1;
          }
        } else if ((ath.getType()
                    == I_TILE) /* || (ath.getType() == I_TILE_ATTR)*/) {
          if (patchMode == I_INTRA) {
            auto& mdu = pid.getMeshpatchDataUnit();
            submeshIDs.push_back(mdu.getMduSubmeshId());
          }
        }
      }
      submeshIDPerTile[tileIdx] = submeshIDs;
    }
    if (sendSEI) {
      AtlasTileLayerRbsp& atl =
        syntax.getAtlasDataStream()
          .getAtlasTileLayerList()[frameIdx * numTiles];
      auto& sei = static_cast<SEITileSubmeshMapping&>(
        atl.getSEI().addSeiPrefix(TILE_SUBMESH_MAPPING, true));
      uint8_t currCount[2] = {0, 0};
      sei.setPersistenceMappingFlag(true);
      sei.setNumberTilesMinus1(tileID.size() - 1);
      auto maxTileId = *std::max_element(tileID.begin(), tileID.end());
      if (maxTileId == 0) sei.setTileIdLengthMinus1(0);  // will use 1 bit,
      else sei.setTileIdLengthMinus1(CeilLog2(maxTileId + 1) - 1);
      sei.setCodecTileSignalFlag(true);
      if (sei.getCodecTileSignalFlag()) {
        sei.setGeoCodecTileAlignmentFlag(false);
        sei.setAttrCodecTileAlignmentFlag(false);
        currCount[0] = 0;
        currCount[1] = 0;
      }
      for (int i = 0; i < sei.getNumberTilesMinus1() + 1; i++) {
        sei.setTileId(i, tileID[i]);
        sei.setTileTypeFlag(i, tileTypeFlag[i]);
        int numSubmeshes = 0;
        sei.setNumSubmeshesMinus1(i, submeshIDPerTile[i].size() - 1);
        numSubmeshes      = submeshIDPerTile[i].size();
        auto maxSubmeshId = *std::max_element(submeshIDPerTile[i].begin(),
                                              submeshIDPerTile[i].end());
        if (maxSubmeshId == 0) sei.setSubmeshIdLengthMinus1(i, 0);
        else sei.setSubmeshIdLengthMinus1(i, CeilLog2(maxSubmeshId + 1) - 1);
        for (int j = 0; j < numSubmeshes; j++) {
          sei.setSubmeshId(i, j, submeshIDPerTile[i][j]);
        }
        if (sei.getCodecTileSignalFlag()) {
          if ((sei.getTileTypeFlag(i) == 0
               && !sei.getGeoCodecTileAlignmentFlag())
              || sei.getTileTypeFlag(i) == 1
                   && !sei.getAttrCodecTileAlignmentFlag()) {
            sei.setNumCodecTilesInTileMinus1(i, 2 - 1);
            for (int j = 0; j < sei.getNumCodecTilesInTileMinus1(i) + 1; j++) {
              sei.setCodecTileIdx(i, j, 2 * i + j);
            }
          }
          if (sei.getGeoCodecTileAlignmentFlag()
              || sei.getAttrCodecTileAlignmentFlag()) {
            if (sei.getTileTypeFlag(i) == 1 && currCount[1] != 0) {
              sei.setCodecTileIdxResetFlag(i, 1);
              if (sei.getCodecTileIdxResetFlag(i)) { currCount[1] = 0; }
            }
            sei.setCodecTileIdx(i, 0, currCount[sei.getTileTypeFlag(i)]);
            currCount[sei.getTileTypeFlag(i)] += 1;
          }
        }
      }
    }
  }
}
//==================================================
void
VMCEncoder::addAttributeTransformationParamsV3cSEI(V3cBitstream& syntax) {
  AtlasTileLayerRbsp& atl =
    syntax.getAtlasDataStream().getAtlasTileLayerList()[0];
  auto& sei = static_cast<SEIAttributeTransformationParams&>(
    atl.getSEI().addSeiPrefix(ATTRIBUTE_TRANSFORMATION_PARAMS, false));

  // set parameters (Sample code. Need to modify for each own case.)
  const int numUpdates = 1;
  const int index      = 0;
  const int dimension  = 2;
  sei.allocate();
  sei.getCancelFlag()          = 0;
  sei.getNumAttributeUpdates() = numUpdates;
  for (int j = 0; j < numUpdates; j++) {
    sei.getAttributeIdx(j)        = index;
    sei.getDimensionMinus1(index) = dimension - 1;
    sei.allocate(index);
    for (int i = 0; i < dimension; i++) {
      sei.getScaleParamsEnabledFlag(index, i)  = 1;
      sei.getAttributeScale(index, i)          = 1 << 16;  // scale = 1(Q16)
      sei.getOffsetParamsEnabledFlag(index, i) = 1;
      sei.getAttributeOffset(index, i)         = 0 << 16;  // offset = 0(Q16)
    }
  }
}

//==================================================
void
VMCEncoder::addAttributeTransformationParamsBaseMeshSEI(V3cBitstream& syntax) {
  basemesh::BaseMeshSubmeshLayer& bmsl =
    syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[0];
  auto& sei = static_cast<basemesh::BMSEIAttributeTransformationParams&>(
    bmsl.getSEI().addSeiPrefix(basemesh::BASEMESH_ATTRIBUTE_TRANSFORMATION_PARAMS,
                               false));

  // set parameters(Sample code. Need to modify for each own case.)
  const int numUpdates = 1;
  const int index      = 0;
  const int dimension  = 2;
  sei.allocate();
  sei.getCancelFlag()          = 0;
  sei.getNumAttributeUpdates() = numUpdates;
  for (int j = 0; j < numUpdates; j++) {
    sei.getAttributeIdx(j)        = index;
    sei.getDimensionMinus1(index) = dimension - 1;
    sei.allocate(index);
    for (int i = 0; i < dimension; i++) {
      sei.getScaleParamsEnabledFlag(index, i)  = 1;
      sei.getAttributeScale(index, i)          = 1 << 16;  // scale = 1(Q16)
      sei.getOffsetParamsEnabledFlag(index, i) = 1;
      sei.getAttributeOffset(index, i)         = 0 << 16;  // offset = 0(Q16)
    }
  }
}

}  // namespace vmesh
