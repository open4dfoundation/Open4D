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
#include <iostream>

#include "vmc.hpp"
#include "util/checksum.hpp"

#include "../basemeshDecoder/baseMeshDecoder.hpp"
#include "decodeVideoStream.hpp"
#include "../acDisplacementDecoder/acDisplacementDecoder.hpp"
#include "acDisplacementFrame.hpp"


namespace vmesh {

int32_t
getPatchIdx(std::vector<std::vector<AtlasTile>>& decodedTiles,
            uint32_t                           frameIdx,
            uint32_t                           tileIdx,
            uint32_t                           submeshId,
            uint32_t                           lodIdx) {
  int32_t  patchIdx   = -1;
  uint32_t patchCount = decodedTiles[tileIdx][frameIdx].patches_.size();
  for (uint32_t i = 0; i < patchCount; i++) {
    uint32_t patchSubmeshId =
      decodedTiles[tileIdx][frameIdx].patches_[i].submeshId_;
    uint32_t patchlodIdx = decodedTiles[tileIdx][frameIdx].patches_[i].lodIdx_;
    if (submeshId == patchSubmeshId && lodIdx == patchlodIdx) {
      patchIdx = i;
      break;
    }
  }
  return patchIdx;
}

uint32_t
getPatchIdx(const AtlasTile& decodedTile, uint32_t submeshId, uint32_t lodIdx) {
  uint32_t patchIdx   = 0;
  uint32_t patchCount = decodedTile.patches_.size();
  for (uint32_t i = 0; i < patchCount; i++) {
    uint32_t patchSubmeshId = decodedTile.patches_[i].submeshId_;
    uint32_t patchlodIdx    = 0;
    patchlodIdx             = decodedTile.patches_[i].lodIdx_;
    if (submeshId == patchSubmeshId && lodIdx == patchlodIdx) {
      patchIdx = i;
      break;
    }
  }
  return patchIdx;
}

int32_t
getAttributeTileIndex(const std::vector<std::vector<AtlasTile>>& tiles,
                      uint32_t                                 tileCount,
                      uint32_t                                 frameIdx) {
  auto tileIdx = -1;
  for (int32_t i = 0; i < tileCount; i++) {
    if (tiles[i][frameIdx].tileType_ == I_TILE_ATTR
        || tiles[i][frameIdx].tileType_ == P_TILE_ATTR) {
      tileIdx = i;
      break;
    }
  }
  return tileIdx;
}

bool
VMCDecoder::separateDispTextureVideo(const V3CParameterSet&   vps,
                                     int32_t                  atlasId,
                                     FrameSequence<uint16_t>& decPacked,
                                     FrameSequence<uint16_t>& dispVideo,
                                     FrameSequence<uint16_t>& attrVideo16) {
  auto texWidth =
    vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(atlasId, 0);
  auto texHeight =
    vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(atlasId, 0);

  auto& pi = vps.getVpsPackedVideoExtension().getPackingInformation(atlasId);
  auto  disp_leftx = pi.pin_region_top_left_x(1);
  auto  disp_lefty = pi.pin_region_top_left_y(1);
  auto  disp_w     = pi.pin_region_width_minus1(1) + 1;
  auto  disp_h     = pi.pin_region_height_minus1(1) + 1;
  // std::cout << "disp_leftx = " << disp_leftx << "; disp_lefty = " << disp_lefty << "; disp_w = " << disp_w << "; disp_h = " << disp_h << std::endl;

  int heightDispVideo = disp_h;
  int widthDispVideo  = disp_w;
  dispVideo.resize(widthDispVideo,
                   heightDispVideo,
                   ColourSpace::YUV420p,
                   decPacked.frameCount(),
                   512);

  // get dispVideo and texture video
  for (int i = 0; i < decPacked.frameCount(); i++) {
    auto& texframe  = decPacked.frame(i);
    auto& dispframe = dispVideo.frame(i);
    for (int row = 0; row < heightDispVideo; row++) {
      for (int col = 0; col < widthDispVideo; col++) {
        int t_row = row + (texframe.height() - heightDispVideo);
        dispframe.plane(0).set(row, col, texframe.plane(0).get(t_row, col));
      }
    }
  }

  attrVideo16 = decPacked;
  attrVideo16.standardizeFrameSizes(texWidth, texHeight, true, 512);
  if (0) {
    std::cout << "keepFilesPathPrefix_ = " << _keepFilesPathPrefix
              << std::endl;
    attrVideo16.save(
      attrVideo16.createName(_keepFilesPathPrefix + "temp_decoder_tex", 10));
    dispVideo.save(
      dispVideo.createName(_keepFilesPathPrefix + "temp_decoder_disp", 10));
  }

  decPacked.clear();
  return true;
}

int32_t
VMCDecoder::decompress(V3cBitstream&               syntax,
                       const V3CParameterSet&      vps,
                       int32_t                     codedV3cSequenceCount,
                       Sequence&                   reconstruct,
                       const VMCDecoderParameters& params) {
  printf("Decompress cvs[%d] vpsId %d\n",
         codedV3cSequenceCount,
         vps.getV3CParameterSetId());
  fflush(stdout);
  auto atlasId = syntax.getAtlasId();

  auto&   gi = vps.getGeometryInformation(atlasId);
  auto&   pi = vps.getVpsPackedVideoExtension().getPackingInformation(atlasId);
  uint8_t geometry2dBitDepth = vps.getGeometryVideoPresentFlag(atlasId)
                                 ? gi.getGeometry2dBitdepthMinus1() + 1
                                 : pi.pin_geometry_2d_bit_depth_minus1() + 1;
  setConsitantFourCCCode(syntax);
  //************
  //Decoding Basemesh Sub-bistream
  //************
  auto&           bmStream = syntax.getBaseMeshDataStream();
  basemesh::BaseMeshDecoder basemeshDecoder;
  //TODO: setExternalBitdepth needs to be removed by using proper syntax elements in AnnexH
  auto&  vmcExt = vps.getVpsVdmcExtension();
  size_t bitDepthPosition =
    vmcExt.getVpsExtMeshGeo3dBitdepthMinus1(atlasId) + 1;
  basemeshDecoder.setExternalBitdepth(bitDepthPosition);
  basemeshDecoder.decodeBasemeshSubbitstream(
    bmStream, params.keepBaseMesh, params.checksum);
  auto meshFrameCount  = basemeshDecoder.getMeshFrameCount();
  auto maxSubmeshCount = (int32_t)basemeshDecoder.getMaxSubmeshCount(bmStream);
  std::vector<std::vector<VMCBasemesh>>& decodedMeshes =
    basemeshDecoder.getDecodedMeshFrames();
  submeshIDToIndexBM_ = bmStream.getBaseMeshFrameParameterSet(0)
                          .getSubmeshInformation()
                          ._submeshIDToIndex;
  submeshIndexToIDBM_ = bmStream.getBaseMeshFrameParameterSet(0)
                          .getSubmeshInformation()
                          ._submeshIndexToID;
#if CONFORMANCE_LOG_DEC
  getBasemeshLog(decodedMeshes, submeshIndexToIDBM_);
#endif
  conversionBasemeshes(vps, atlasId, decodedMeshes);
  //************
  //decode AD Stream
  //************
  auto&            adStream = syntax.getAtlasDataStream();
  AtlasDataDecoder adDecoder;
  adDecoder.decodeAtlasDataSubbitstream(adStream, params.checksum);
  auto atlasFrameCount = adDecoder.getAtlasFrameCount();
  auto maxTileCount    = (int32_t)adDecoder.getMaxTileCount();
  std::vector<std::vector<AtlasTile>>& decodedTiles =
    adDecoder.getDecodedAtlasFrames();  //[tiles][frame]
  submeshIDToIndexAD_ = adStream.getAtlasFrameParameterSet(0)
                          .getAfveExtension()
                          .getAtlasFrameMeshInformation()
                          ._submeshIDToIndex;

  auto numTextures = vps.getAttributeVideoPresentFlag(atlasId)
                       ? syntax.getV3CUnitAVD().size()
                       : 1;
  reconstruct.resize(atlasFrameCount, numTextures);
  auto& dmStream = syntax.getDisplacementStream();

  if (maxSubmeshCount > 1 && params.processZipperingSEI) {
    //allocate structures
    applyZippering_.resize(atlasFrameCount, 0);
    zipperingMatchMaxDistancePerFrame_.resize(atlasFrameCount, 0);
    zipperingMatchMaxDistancePerSubmesh_.resize(atlasFrameCount);
    zipperingMatchMaxDistancePerSubmeshPair_.resize(atlasFrameCount);
    zipperingDistanceBorderPoint_.resize(atlasFrameCount);
    zipperingMatchedBorderPoint_.resize(atlasFrameCount);
    methodForUnmatchedLoDs_.resize(atlasFrameCount, 0);
    boundaryIndex_.resize(atlasFrameCount);
    crackCount_.resize(atlasFrameCount);
    zippering_decode(adStream, params);
  }

  //****************
  //displacement data extraction : video
  //****************
  FrameSequence<uint16_t>              dispVideo;
  std::vector<std::vector<acdisplacement::AcDisplacementFrame>> decodedAcDispls;
  if (vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(atlasId)) {
    std::cout << "decompressPackedVideo\n";
    auto numPVD = syntax.getV3CUnitPVD().size();
    if (numPVD > 1) std::cout << "WARNING: #PVD is " << numPVD << "\n";
    VMCVideoDecoder packedVideoDecoder;
    bool            decPackedVideo = packedVideoDecoder.decompressPackedVideo(
      syntax.getV3CUnitPVD(0), 0, params.keepVideoFiles);
    if (!decPackedVideo) return false;
#if CONFORMANCE_LOG_DEC
    TRACE_PICTURE("PackedVideo\n");
    getPictureLog(packedVideoDecoder.getPackedVideo());
#endif

    auto attrIndex = syntax.getV3CUnitPVD(0).getAttributeIndex();
    auto attrType  = vps.getVpsPackedVideoExtension()
                      .getPackingInformation(atlasId)
                      .pin_attribute_type_id(attrIndex);
    FrameSequence<uint16_t> attrVideo16;
    separateDispTextureVideo(vps,
                             atlasId,
                             packedVideoDecoder.getPackedVideo(),
                             dispVideo,
                             attrVideo16);

    auto textureVideoBitDepth = packedVideoDecoder.getPackedVideoBitdepth();
    uint32_t textureSourceBitDepth =
      (uint32_t)vps.getVpsPackedVideoExtension()
        .getPackingInformation(atlasId)
        .pin_attribute_2d_bit_depth_minus1(attrIndex)
      + 1;
    videoConversion(attrVideo16,
                    textureVideoBitDepth,
                    textureSourceBitDepth,
                    attrVideo16.colourSpace(),
                    attrVideo16.width(),
                    attrVideo16.height(),
                    vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(
                      atlasId, attrIndex),
                    vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(
                      atlasId, attrIndex),
                    params);
    reconstruct.attribute(attrIndex) = attrVideo16;
    attrVideo16.clear();
  }

  if (vps.getGeometryVideoPresentFlag(atlasId)) {
    auto codecGroupIdc = VideoCodecGroupIdc::HEVC_Main10;
    if (vps.getProfileTierLevel().getPtlVideoCodecGroupIdc()
          == VideoCodecGroupIdc::Video_MP4RA
        && (vps.getProfileTierLevel().getPtlNonVideoCodecGroupIdc()
              == NonVideoCodecGroupIdc::NonVideo_MP4RA
            || vps.getProfileTierLevel().getPtlNonVideoCodecGroupIdc()
                 == NonVideoCodecGroupIdc::BMS_Main)) {
      //TODO: component mapping SEI
      auto geometryCodecId = getCodedCodecId(
        syntax, vps, vps.getGeometryInformation(atlasId).getGeometryCodecId(), true);
      syntax.getV3CUnitGVD(0).setCoderIdc(geometryCodecId);
    } else {
      syntax.getV3CUnitGVD(0).setCoderIdc(
        getVideoCoderId((VideoCodecGroupIdc)vps.getProfileTierLevel()
                          .getPtlVideoCodecGroupIdc()));
    }
    std::cout << "decompressDisplacements(Video)\n";
    auto numGVD = syntax.getV3CUnitGVD().size();
    if (numGVD > 1) std::cout << "WARNING: #GVD is " << numGVD << "\n";
    if (LoDExtractionEnable) LoDVideobitstreamExtraction(syntax, params);
    VMCVideoDecoder geometryVideoDecoder;
    bool decDispVideo = geometryVideoDecoder.decompressDisplacementsVideo(
      syntax.getV3CUnitGVD(0), params.keepVideoFiles);
    if (!decDispVideo) return false;
    dispVideo = geometryVideoDecoder.getDispVideo();
    geometryVideoDecoder.getDispVideo().clear();
#if CONFORMANCE_LOG_DEC
    TRACE_PICTURE("Geometry\n");
    TRACE_PICTURE("MapIdx = 0, AuxiliaryVideoFlag = 0\n");
    getPictureLog(dispVideo);
#endif
  } else if (vps.getVpsVdmcExtension().getVpsExtACDisplacementPresentFlag(atlasId)) {
    std::cout << "decompressDisplacements(AC)\n";
    ACDisplacementDecoder dispDecoder;
    auto&                 dmStream = syntax.getDisplacementStream();
    bool decDispAC  = dispDecoder.decodeDisplacementSubbitstream(dmStream);
    displIDToIndex_ = dispDecoder.getDisplIDToIndex(dmStream);
    decodedAcDispls = dispDecoder.getdecodedAcDispls();
#if CONFORMANCE_LOG_DEC
    getACDisplacementLog(decodedAcDispls, displIDToIndex_);
#endif 
  }

  if (vps.getAttributeVideoPresentFlag(atlasId)) {
    std::cout << "decompressTextureVideo\n";
    auto numAVD = syntax.getV3CUnitAVD().size();
    if (numAVD > 1){
      std::cout << "WARNING: #AVD is " << numAVD << "\n";
    }

    if (vps.getProfileTierLevel().getPtlVideoCodecGroupIdc() == 15) {
      auto attributeCodecId = getCodedCodecId(
        syntax,
        vps,
        vps.getAttributeInformation(atlasId).getAttributeCodecId(0),
        true);
      syntax.getV3CUnitAVD(0).setCoderIdc(attributeCodecId);
    } else if (vps.getProfileTierLevel().getPtlNonVideoCodecGroupIdc()
               == NonVideoCodecGroupIdc::BMS_Main_DAC_Main) {
      // LK00: I do not understand why we have this if,
      // attributes are always video coded, can we remove it ?
      for (int ai = 0; ai < syntax.getV3CUnitAVD().size(); ai++) {
        syntax.getV3CUnitAVD(ai).setCoderIdc(VideoEncoderId::HM);
      }
    } else {
      for (int ai = 0; ai < syntax.getV3CUnitAVD().size(); ai++) {
        syntax.getV3CUnitAVD(ai).setCoderIdc(
          getVideoCoderId((VideoCodecGroupIdc)vps.getProfileTierLevel()
                            .getPtlVideoCodecGroupIdc()));
      }
    }

    for (int ai = 0; ai < syntax.getV3CUnitAVD().size(); ai++) {
      auto attrIndex = syntax.getV3CUnitAVD(ai).getAttributeIndex();
      auto attrType =
        vps.getAttributeInformation(atlasId).getAttributeTypeId(attrIndex);
      if (attrType == ATTRIBUTE_VIDEO_TEXTURE) {
        VMCVideoDecoder attrVideoDecoder;
        if ((VideoEncoderId)syntax.getV3CUnitAVD(0).getCoderIdc()
            == VideoEncoderId::SHMAPP) {
          auto& dec = attrVideoDecoder.getAttributeVideo();
          dec.resize(vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(
                       atlasId, attrIndex)
                       >> (params.maxLayersMinus1 - params.targetLayer),
                     vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(
                       atlasId, attrIndex)
                       >> (params.maxLayersMinus1 - params.targetLayer),
                     ColourSpace::YUV420p,
                     atlasFrameCount);
        }
        bool decAttVideo = attrVideoDecoder.decompressTextureVideo(
          syntax.getV3CUnitAVD(ai), attrIndex, params.keepVideoFiles, params);
        if (!decAttVideo) { return -1; }

        auto& dec = attrVideoDecoder.getAttributeVideo();
#if CONFORMANCE_LOG_DEC
        TRACE_PICTURE("Attribute\n");
        TRACE_PICTURE("MapIdx = 0, AuxiliaryVideoFlag = 0, AttrIdx = %d, AttrPartIdx = 0, AttrTypeID = %d\n", attrIndex, (int)attrType);
        getPictureLog(dec);
#endif
        auto  attributeeVideoBitDepth =
          attrVideoDecoder.getAttributeVideoBitdepth();
        uint32_t attributeSourceBitDepth =
          (uint32_t)vps.getAttributeInformation(atlasId)
            .getAttribute2dBitdepthMinus1((uint32_t)attrIndex)
          + 1;

        uint32_t sourceWidth =
          vps.getVpsVdmcExtension().getVpsExtConsistentAttributeFrameFlag(
            atlasId)
            ? vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(atlasId,
                                                                     0)
            : vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(
              atlasId, attrIndex);
        uint32_t sourceHeight =
          vps.getVpsVdmcExtension().getVpsExtConsistentAttributeFrameFlag(
            atlasId)
            ? vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(atlasId,
                                                                      0)
            : vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(
              atlasId, attrIndex);

        videoConversion(
          dec,
          attributeeVideoBitDepth,
          attributeSourceBitDepth,
          dec.colourSpace(),
          dec.width(),
          dec.height(),
          //vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(atlasId, attrIndex),
          //vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(atlasId, attrIndex),
          sourceWidth,
          sourceHeight,
          params);
        reconstruct.attribute(attrIndex) = dec;
        dec.clear();
      } else {
        printf("non specified attribute type: %d\n", (int32_t)attrType);
        return -1;
      }
    }
  } else if (!vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(
               atlasId)) {
    reconstruct.attributes().clear();
  }
  fflush(stdout);

  //***************
  //reconstruction
  //***************
  uint8_t displacementCodedType =
    (!vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(atlasId)
     && !vps.getGeometryVideoPresentFlag(atlasId)
     && !vps.getVpsVdmcExtension().getVpsExtACDisplacementPresentFlag(atlasId))
      ? 0
    : (vps.getProfileTierLevel().getPtlNonVideoCodecGroupIdc() == NonVideoCodecGroupIdc::BMS_Main_DAC_Main) ? 1
                                                                         : 2;

  for (int32_t frameIndex = 0; frameIndex < atlasFrameCount; ++frameIndex) {
    //TODO: the number of submeshes can be different per frame
    //TODO: the reconstruction should not work on tiles, the atlas decoder should provide atlas level information
    //TODO: number of submeshes should be base on AFPS information
    std::vector<VMCDecMesh> reconSubmeshesOneFrame(submeshIndexToIDBM_.size());
    for (int32_t tileIndex = 0; tileIndex < maxTileCount; tileIndex++) {
      auto& decodedTile = decodedTiles[tileIndex][frameIndex];
      if (decodedTile.tileType_ == I_TILE || decodedTile.tileType_ == P_TILE
          || decodedTile.tileType_ == SKIP_TILE) {
        // TODO: during reconstruction we should know what is the maximum number of submeshes and which AFPS to use
        auto submeshCount =
          adStream.getAtlasFrameParameterSetList()[decodedTile.tileAfpsId_]
            .getAfveExtension()
            .getAtlasFrameMeshInformation()
            .getNumSubmeshesInAtlasFrameMinus1() + 1;
        for (int32_t submeshIndexInAtlas = 0;
             submeshIndexInAtlas < submeshCount;
             submeshIndexInAtlas++) {
          auto submeshId =
            adStream.getAtlasFrameParameterSetList()[decodedTile.tileAfpsId_]
              .getAfveExtension()
              .getAtlasFrameMeshInformation()
              ._submeshIndexToID[submeshIndexInAtlas];
          auto basePatchIndex =
            getPatchIdx(decodedTiles, frameIndex, tileIndex, submeshId, 0);
          if (basePatchIndex == -1) continue;
          auto  submeshIndexInBaseMesh = submeshIDToIndexBM_[submeshId];
          auto& decodedMesh =
            decodedMeshes[submeshIndexInBaseMesh][frameIndex];
          auto& outputSubmesh = reconSubmeshesOneFrame
            [submeshIndexInBaseMesh];  //vdmcOutputFrame_[submeshPos][frameIndex];
          auto tileIndexAttribute =
            getAttributeTileIndex(decodedTiles, maxTileCount, frameIndex);

          reconstructSubmesh(vps,
                             atlasId,
                             adStream.getAtlasSequenceParameterSetList(),
                             adStream.getAtlasFrameParameterSetList(),
                             decodedTiles,
                             frameIndex,
                             tileIndex,
                             basePatchIndex,
                             submeshIndexInBaseMesh,
                             decodedMesh,
                             dispVideo,
                             decodedAcDispls,
                             displacementCodedType,
                             params,
                             outputSubmesh);
          // workaround when no video attributes (no XX_TILE_ATTR tiles)
          if (tileIndexAttribute == -1) {
            //TODO: attributeIndex needs to be index for texture coordinates
            int32_t attributeIndex = 0;
            auto& asve =
              adStream
                .getAtlasSequenceParameterSetList()[decodedTile.tileAspsId_]
                .getAsveExtension();
            uint32_t bitDepthTexCoord;
            if (asve.getAsveProjectionTexcoordEnableFlag()) {
              bitDepthTexCoord =
                asve.getAsveProjectionTexcoordOutputBitdepthMinus1() + 1;
            }
            else {
              bitDepthTexCoord =
                vps.getVpsVdmcExtension().getVpsExtMeshAttributeBitdepthMinus1(
                  atlasId, 0)
                + 1;  // texture coordinate sent using basemesh
            }
            if (!params.dequantizeUV) {
              const auto scale = (1 << bitDepthTexCoord) - 1.0;
              const auto texCoordCount = outputSubmesh.rec.texCoordCount();
              for (int32_t i = 0; i < texCoordCount; ++i) {
                outputSubmesh.rec.setTexCoord(
                  i, outputSubmesh.rec.texCoord(i) * scale);
              }
            }
          }
        }  //patch
      }
    }  //tile

#if CONFORMANCE_LOG_DEC
    getAtlasLog(frameIndex, decodedTiles, syntax, vps);
    getTileLog(frameIndex, decodedTiles, syntax);
#endif
    //SEI
    // post processing from SEI messages
    if (maxSubmeshCount > 1 && params.processZipperingSEI
        && applyZippering_[frameIndex]==6) {
      std::cout << "zippering\n";
      zippering_reconstruct(frameIndex,
                            reconSubmeshesOneFrame,
                            reconSubmeshesOneFrame.size(),
                            params);
    }

    for (int32_t tileIndex = 0; tileIndex < maxTileCount; tileIndex++) {
      auto& decodedTile = decodedTiles[tileIndex][frameIndex];
      // when no video attributes are present (no _ATTR tiles), then texture
      // coordinate adjusment is not called
      // see above workaround when no video attributes and
      // TODO: the reconstruction should not work on tiles
      if (decodedTile.tileType_ == I_TILE_ATTR
          || decodedTile.tileType_ == P_TILE_ATTR
          || decodedTile.tileType_ == SKIP_TILE_ATTR) {
        auto patchCount = decodedTile.patches_.size();
        for (int32_t patchIndex = 0; patchIndex < patchCount; patchIndex++) {
          const auto& decodedPatch =
            decodedTiles[tileIndex][frameIndex].patches_[patchIndex];
          auto  submeshId     = decodedPatch.submeshId_;
          auto  submeshPos    = submeshIDToIndexBM_[submeshId];
          auto& decodedMesh   = decodedMeshes[submeshPos][frameIndex];
          auto& outputSubmesh = reconSubmeshesOneFrame
            [submeshPos];  //vdmcOutputFrame_[submeshPos][frameIndex];
                           //11.2.9  Texture coordinate adaptation process
          //TODO: attributeIndex needs to be index for texture coordinates
          int32_t attributeIndex = 0;
          auto&   asve =
            adStream
              .getAtlasSequenceParameterSetList()[decodedTile.tileAspsId_]
              .getAsveExtension();
          uint32_t bitDepthTexCoord;
          if (asve.getAsveProjectionTexcoordEnableFlag()) {
            bitDepthTexCoord =
              asve.getAsveProjectionTexcoordOutputBitdepthMinus1() + 1;
          } else {
            bitDepthTexCoord =
              vps.getVpsVdmcExtension().getVpsExtMeshAttributeBitdepthMinus1(
                atlasId, 0)
              + 1;  // texture coordinate sent using basemesh
          }

          if (asve.getAspsAttributeNominalFrameSizeCount() > 0
              && asve
                   .getAsveAttributeSubtextureEnabledFlag()[attributeIndex]) {
            const auto& decodedPatch =
              decodedTiles[tileIndex][frameIndex].patches_[patchIndex];
            auto submeshIndexInBaseMesh = submeshIDToIndexBM_[submeshId];
            std::cout << "DEBUG (LK): submeshId_ " << decodedPatch.submeshId_
                      << "\n";
            std::cout << "DEBUG (LK): tileIndex " << tileIndex << "\n";
            std::cout << "DEBUG (LK): patchIndex " << patchIndex << "\n";
            adjustTextureCoordinates(vps,
                                     atlasId,
                                     attributeIndex,
                                     bitDepthTexCoord,
                                     decodedTiles[tileIndex][frameIndex],
                                     decodedPatch,
                                     submeshIndexInBaseMesh,
                                     frameIndex,
                                     tileIndex,
                                     outputSubmesh.rec);
          }
          if (!params.dequantizeUV) {
            const auto scale         = (1 << bitDepthTexCoord) - 1.0;
            const auto texCoordCount = outputSubmesh.rec.texCoordCount();
            for (int32_t i = 0; i < texCoordCount; ++i) {
              outputSubmesh.rec.setTexCoord(
                i, outputSubmesh.rec.texCoord(i) * scale);
            }
          }
        }  //patch
      }
    }  //tile

#if CONFORMANCE_LOG_DEC
    getMFrameLog(frameIndex, reconSubmeshesOneFrame);
#endif
    //SEI
    // post processing from SEI messages
    if (maxSubmeshCount > 1 && params.processZipperingSEI
        && (applyZippering_[frameIndex]<6 && applyZippering_[frameIndex]>0)) {
      std::cout << "zippering\n";
      zippering_reconstruct(frameIndex,
                            reconSubmeshesOneFrame,
                            reconSubmeshesOneFrame.size(),
                            params);
    }

    //11.3  Append submesh to a mesh process
    appendReconstructedSubmeshes(
      vps, atlasId, frameIndex, reconSubmeshesOneFrame, reconstruct, params);
#if CONFORMANCE_LOG_DEC
    getRecFrameLog(frameIndex, reconstruct.mesh(frameIndex));
#endif
  }  //frame

  //TODO: place checkUVranges() in the correct location : to call asps, tile is needed.
  //Check UV range for debug
  //  const auto& asps = adStream.getAtlasSequenceParameterSet(0);
  //  if (asps.getAsveExtension().getProjectionTextCoordEnableFlag()) {
  //      std::cout << "Check UV range for reconstructcted mesh." << std::endl;
  //      checkUVranges(meshFrameCount, maxSubmeshCount);
  //  }

  return atlasFrameCount;
}

int32_t
VMCDecoder::reconstructSubmesh(
  const V3CParameterSet&                            vps,
  int32_t                                           atlasId,
  const std::vector<AtlasSequenceParameterSetRbsp>& aspsList,
  const std::vector<AtlasFrameParameterSetRbsp>&    afpsList,
  const std::vector<std::vector<AtlasTile>>&        decodedTiles,
  int32_t                                           frameIndex,
  int32_t                                           tileIndex,
  int32_t                                           patchIndex,
  int32_t                                           submeshIndexInBaseMesh,
  VMCBasemesh&
    decodedSubmesh,  //const is dropped due to uvcoordinate generation
  const FrameSequence<uint16_t>&              dispVideo,
  const std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& decodedAcDispls,
  //0 - none 1 - ac displacement 2 - video
  uint8_t                     displacementCodedType,
  const VMCDecoderParameters& params,
  VMCDecMesh&                 recSubmesh) {
  auto&      rec       = recSubmesh.rec;
  // decodedTileAttribute removed from the interface as not used
  // if required, use a list of tileAttributeIndex and get
  // decodedTileAttribute = decodedTiles[tileAttributeIndex][frameIndex];
  const auto& decodedTile = decodedTiles[tileIndex][frameIndex];
  auto       submeshId = decodedTile.patches_[patchIndex].submeshId_;
  const auto bitDepthPosition =
    aspsList[decodedTile.tileAspsId_].getGeometry3dBitdepthMinus1() + 1;
  auto& gi = vps.getGeometryInformation(0);
  auto& pi = vps.getVpsPackedVideoExtension().getPackingInformation(0);
  auto  geometryVideoBitDepth =
    vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(atlasId)
       ? (pi.pin_geometry_2d_bit_depth_minus1() + 1)
       : (gi.getGeometry2dBitdepthMinus1() + 1);
  auto        basePatchIndex   = getPatchIdx(decodedTile, submeshId, 0);
  const auto& baseDecodedPatch = decodedTile.patches_[basePatchIndex];

  //11.2.2  Texture coordinate derivation
  //TODO: what is difference between asps in AtlasPatch and asps pointed by a patch?
  auto& projAsps = aspsList[baseDecodedPatch.projectionAspsId_];
  auto& projAfps = afpsList[baseDecodedPatch.projectionAfpsId_];
  if (projAsps.getAsveExtension().getAsveProjectionTexcoordEnableFlag()) {
    auto& meshinfo =
      projAfps.getAfveExtension().getAtlasFrameMeshInformation();
    auto submeshIdx = meshinfo._submeshIDToIndex[baseDecodedPatch.submeshId_];
    reconstructBaseMeshUvCoordinates(decodedSubmesh.base,
                                     submeshIdx,
                                     frameIndex,
                                     vps,
                                     projAsps,
                                     projAfps,
                                     baseDecodedPatch);
  }

  //11.2.3  Subdivision process
  if (baseDecodedPatch.subdivIteration > 0) {
    if (!LoDExtractionEnable) {
      std::vector<SubdivisionMethod> subdivisionMethod(
        baseDecodedPatch.subdivIteration);
      for (int32_t it = 0; it < baseDecodedPatch.subdivIteration; ++it) {
        subdivisionMethod[it] =
          (SubdivisionMethod)baseDecodedPatch.subdivMethod[it];
      }
      auto asve     = projAsps.getAsveExtension();
      auto bitshift = 16 - bitDepthPosition;
      subdivideBaseMesh(decodedSubmesh.base,
                        frameIndex,
                        submeshIndexInBaseMesh,
                        recSubmesh,
                        &subdivisionMethod,
                        baseDecodedPatch.subdivIteration,
                        asve.getAsveInterpolateSubdividedNormalsFlag(),
                        asve.getAsveSubdivisionMinEdgeLength(),
                        bitshift);
    } else {
      int targetSubdivIteration;
      if (params.targetLoD == -1)
        targetSubdivIteration = baseDecodedPatch.subdivIteration;
      else targetSubdivIteration = params.targetLoD;
      std::vector<SubdivisionMethod> subdivisionMethod(targetSubdivIteration);
      for (int32_t it = 0; it < targetSubdivIteration; ++it) {
        subdivisionMethod[it] =
          (SubdivisionMethod)baseDecodedPatch.subdivMethod[it];
      }
      auto asve     = projAsps.getAsveExtension();
      auto bitshift = 16 - bitDepthPosition;
      subdivideBaseMesh(decodedSubmesh.base,
                        frameIndex,
                        submeshIndexInBaseMesh,
                        recSubmesh,
                        &subdivisionMethod,
                        targetSubdivIteration,
                        asve.getAsveInterpolateSubdividedNormalsFlag(),
                        asve.getAsveSubdivisionMinEdgeLength(),
                        bitshift);
    }
  } else {
    recSubmesh.rec = decodedSubmesh.base;
  }
  if (params.checksum) {
    //printf("qpPosition: %d bitDepthPosition:%d iscalePosition: %f\n", qpPosition,bitDepthPosition, iscalePosition);
    //frame.base.save("_dec_base.obj");
    Checksum    checksum;
    std::string eString =
      "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
      + std::to_string(frameIndex) + " ]["
      + std::to_string(submeshIndexInBaseMesh) + "]\tbmstreamRecSubdiv ";
    checksum.print(recSubmesh.rec, eString);
  }
  //11.2.4  Inverse image packing of transform coefficients
  auto& asps           = aspsList[decodedTile.tileAspsId_];
  auto& submeshRecDisp = recSubmesh.disp;
  if (displacementCodedType == 1) {
    auto displId   = baseDecodedPatch.displId_;
    auto displPos  = displIDToIndex_[displId];
    submeshRecDisp = decodedAcDispls[displPos][frameIndex].disp;
  } else if (displacementCodedType == 2) {
    auto sizeX = baseDecodedPatch.geometryPatchArea.sizeX;
    auto sizeY = baseDecodedPatch.geometryPatchArea.sizeY;
    auto posX  = baseDecodedPatch.geometryPatchArea.LTx
                + decodedTile.tileGeometryArea_.LTx;
    auto posY = baseDecodedPatch.geometryPatchArea.LTy
                + decodedTile.tileGeometryArea_.LTy;

    auto&                subdivInfo = recSubmesh.subdivInfoLevelOfDetails;
    std::vector<int32_t> vertexCountPerLod(subdivInfo.size());
    for (size_t lod = 0; lod < subdivInfo.size(); lod++) {
      vertexCountPerLod[lod] = subdivInfo[lod].pointCount;
      if (lod != 0) {
        vertexCountPerLod[lod] -= subdivInfo[lod - 1].pointCount;
      }
    }
    if (asps.getAsveExtension().getAsveLodPatchesEnableFlag() == 0) {
      //Update vertexCountPerLod and total point count for reverse packing displacement in LoD decode
      TriangleMesh<MeshType> recTemp;
      if (LoDExtractionEnable
          && asps.getAsveExtension().getAsvePackingMethod()) {
        auto curPatch = decodedTile.patches_[patchIndex];
        vertexCountPerLod.resize(curPatch.blockCount.size());
        int totalVertexCount = 0;
        for (size_t lod = 0; lod < curPatch.blockCount.size(); lod++) {
          vertexCountPerLod[lod] =
            (curPatch.blockCount[lod] - 1)
            * (1 << asps.getLog2PatchPackingBlockSize())
            * (1 << asps.getLog2PatchPackingBlockSize());
         vertexCountPerLod[lod] += curPatch.lastPosInBlock[lod];
          if (curPatch.lastPosInBlock[lod] == 0 || curPatch.blockCount[lod] == 0) {
            vertexCountPerLod[lod] =
              curPatch.blockCount[lod]
              * (1 << asps.getLog2PatchPackingBlockSize())
              * (1 << asps.getLog2PatchPackingBlockSize());
          }
          totalVertexCount += vertexCountPerLod[lod];
        }
        recTemp.points().resize(totalVertexCount);
      }
      reconstructDisplacementFromVideoFrame(
        dispVideo.frame(frameIndex),
        vertexCountPerLod,
        submeshRecDisp,
        LoDExtractionEnable
            && asps.getAsveExtension().getAsvePackingMethod()
          ? recTemp
          : rec,
        frameIndex,
        tileIndex,
        patchIndex,
        submeshId,
        sizeX,
        sizeY,
        posX,
        posY,
        1 << asps.getLog2PatchPackingBlockSize(),
        geometryVideoBitDepth,
        asps.getAsveExtension().getAsve1DDisplacementFlag(),
        asps.getAsveExtension().getAsvePackingMethod(),
        dispVideo.colourSpace());
    } else {
      auto lodCount0 = baseDecodedPatch.subdivIteration + 1;
      for (auto lodIndex = 0; lodIndex < lodCount0; lodIndex++) {
        auto lodPatchIndex = getPatchIdx(decodedTile, submeshId, lodIndex);
        const auto& lodDecodedPatch = decodedTile.patches_[lodPatchIndex];
        auto        patchBlockSize  = 1 << asps.getLog2PatchPackingBlockSize();
        auto        lodSizeX = lodDecodedPatch.geometryPatchArea.sizeX;
        auto        lodSizeY = lodDecodedPatch.geometryPatchArea.sizeY;
        auto        lodPosX  = lodDecodedPatch.geometryPatchArea.LTx
                       + decodedTile.tileGeometryArea_.LTx;
        auto lodPosY = lodDecodedPatch.geometryPatchArea.LTy
                       + decodedTile.tileGeometryArea_.LTy;
        auto patchVertexCount = 0;
        if (lodDecodedPatch.lastPosInBlock[0] == 0) {
          patchVertexCount = (patchBlockSize * patchBlockSize)
                             * lodDecodedPatch.blockCount[0];
        } else {
          patchVertexCount = (patchBlockSize * patchBlockSize)
                               * (lodDecodedPatch.blockCount[0] - 1)
                             + lodDecodedPatch.lastPosInBlock[0];
        }
        reconstructDisplacementFromVideoFramemPerLod(
          dispVideo.frame(frameIndex),
          patchVertexCount,
          submeshRecDisp,
          lodSizeX,
          lodSizeY,
          lodPosX,
          lodPosY,
          (1 << asps.getLog2PatchPackingBlockSize()),
          geometryVideoBitDepth,
          asps.getAsveExtension().getAsve1DDisplacementFlag(),
          asps.getAsveExtension().getAsvePackingMethod(),
          dispVideo.colourSpace());
      }
    }
  }
  if (params.checksum && (displacementCodedType != 0)) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\t(recon)submeshDisp["
                          + std::to_string(submeshIndexInBaseMesh)
                          + "] submeshId " + std::to_string(submeshId);
    checksum.print(submeshRecDisp, eString);
    std::cout
      << "**********************************************************\n";
    //dispVideo_.frame(frameIdx).save("test.yuv");
  }
  //11.2.5  Inverse quantization
  if (displacementCodedType != 0 && (!baseDecodedPatch.IQ_skip)) {
    int dispDimension =
      asps.getAsveExtension().getAspsDispComponents();
    const std::vector<std::vector<double>>& iscale =
      baseDecodedPatch.quantizationParameters.iscale;
    bool iqOffsetFlag = baseDecodedPatch.iqOffsetFlag;
    auto iqOffset     = baseDecodedPatch.iqOffsets;
    if (baseDecodedPatch.transformMethod == 1)
      inverseQuantizeDisplacements(submeshRecDisp,
                                   recSubmesh.subdivInfoLevelOfDetails,
                                   iscale,
                                   dispDimension,
                                   iqOffsetFlag,
                                   iqOffset);
    else
      inverseQuantizeDisplacements(submeshRecDisp,
                                   recSubmesh.subdivInfoLevelOfDetails,
                                   iscale,
                                   dispDimension,
                                   false,
                                   iqOffset);

    if (params.checksum) {
      Checksum checksum;
      std::cout
        << "**********************************************************\n";
      std::string eString = "frame " + std::to_string(frameIndex)
                            + "\tdeQsubmeshDisp["
                            + std::to_string(submeshIndexInBaseMesh)
                            + "] submeshId " + std::to_string(submeshId);
      checksum.print(submeshRecDisp, eString);
      std::cout
        << "**********************************************************\n";
    }
  }
  //11.2.6  Inverse transform
  if (displacementCodedType != 0 && baseDecodedPatch.transformMethod) {
    auto lodCount = recSubmesh.subdivInfoLevelOfDetails.size();
    const LiftingTransformParameters& trParams =
      baseDecodedPatch.transformParameters;
    computeInverseLinearLifting(
      recSubmesh.disp,
      recSubmesh.subdivInfoLevelOfDetails,
      recSubmesh.subdivEdges,
      rec.valences(),
      trParams.getValenceUpdateWeightFlag(),
      trParams.liftingOffsetValues_,
      asps.getAsveExtension().getAsveLiftingOffsetPresentFlag(),
      trParams.predWeight_,
      trParams.updateWeight_,
      trParams.getSkipUpdateFlag(),
      trParams.liftingScaleValues_,
      trParams.dirlift_,
	  trParams.dirliftScale1_,
      trParams.dirliftScale2_,
      trParams.dirliftScale3_);
  } else {
    std::cout << "linear lifting inverse transform or none is available\n";
  }

  if (params.checksum) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\tinvTrsubmeshDisp["
                          + std::to_string(submeshIndexInBaseMesh)
                          + "] submeshId " + std::to_string(submeshId);
    checksum.print(submeshRecDisp, eString);
    std::cout
      << "**********************************************************\n";
  }

  //11.2.8  Vertex coordinates reconstruction
  //TODO: is this if() is a right way to avoid it?
  if (displacementCodedType != 0) {
    auto asve = projAsps.getAsveExtension();
    applyDisplacements(submeshRecDisp,
                       bitDepthPosition,
                       recSubmesh.rec,
                       (DisplacementCoordinateSystem)
                         baseDecodedPatch.displacementCoordinateSystem,
                       asve.getAsveInterpolateSubdividedNormalsFlag());
  }
  if (params.checksum) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\treconFinal["
                          + std::to_string(submeshIndexInBaseMesh)
                          + "] submeshId " + std::to_string(submeshId);
    checksum.print(rec, eString);
    std::cout
      << "**********************************************************\n";
  }
  return true;
}

void
VMCDecoder::adjustTextureCoordinates(const V3CParameterSet&  vps,
                                     int32_t                 atlasId,
                                     int32_t                 attributeIndex,
                                     int32_t                 bitDepthTexCoord,
                                     const AtlasTile&          decodedTile,
                                     const AtlasPatch&         decodedPatch,
                                     int32_t                 submeshPos,
                                     int32_t                 frameIndex,
                                     int32_t                 tileIndex,
                                     TriangleMesh<MeshType>& submesh) {
  auto LTx = decodedPatch.attributePatchArea[attributeIndex].LTx
             + decodedTile.tileAttributeAreas_[attributeIndex].LTx;
  auto LTy = decodedPatch.attributePatchArea[attributeIndex].LTy;
  +decodedTile.tileAttributeAreas_[attributeIndex].LTx;

  vmesh::adjustTextureCoordinates(
    submesh,
    decodedPatch.attributePatchArea[attributeIndex].sizeX,
    decodedPatch.attributePatchArea[attributeIndex].sizeY,
    LTx,
    LTy,
    vps.getVpsVdmcExtension().getVpsExtAttributeFrameWidth(atlasId,
                                                           attributeIndex),
    vps.getVpsVdmcExtension().getVpsExtAttributeFrameHeight(atlasId,
                                                            attributeIndex));
}

void
VMCDecoder::appendReconstructedSubmeshes(
  const V3CParameterSet&      vps,
  int32_t                     atlasIndex,
  int32_t                     frameIndex,
  std::vector<VMCDecMesh>&    submeshes,  //frame
  Sequence&                   reconstruct,
  const VMCDecoderParameters& params) {
  TriangleMesh<MeshType>& reconstructed = reconstruct.mesh(frameIndex);

  auto  submeshCount = submeshes.size();
  auto& submesh      = submeshes[0].rec;
  if (!params.reconstructNormals) { submesh.resizeNormals(0); }
  reconstructed = submesh;
  for (int32_t submeshIdx = 1; submeshIdx < submeshCount; submeshIdx++) {
    auto& submesh = submeshes[submeshIdx].rec;
    if (!params.reconstructNormals) { submesh.resizeNormals(0); }
    reconstructed.append(submesh);
    submesh.clear();
    if (0) {
      std::cout << "***************************************************\n";
      std::cout << "***************************************************\n";
      Checksum    checksum;
      std::string eString = "Writing Frame[ " + std::to_string(frameIndex)
                            + " ]\tsubmesh[" + std::to_string(submeshIdx)
                            + "] recon";
      checksum.print(submesh, eString);
      std::cout << "***************************************************\n";
      std::cout << "***************************************************\n";
    }
  }  //submeshIdx
}
int32_t
VMCDecoder::subdivideBaseMesh(TriangleMesh<MeshType>&         base,
                              int32_t                         frameIndex,
                              int32_t                         submeshIndex,
                              VMCDecMesh&                     recFrame,
                              std::vector<SubdivisionMethod>* subdivMethods,
                              const int32_t subdivisionIterationCount,
                              const bool    interpolateSubdividedNormalsFlag,
                              const int32_t edgeLengthThreshold,
                              const int32_t bitshiftEdgeBasedSubdivision) {
  printf("(Subdivide) basemesh frame[ %d ] submesh[ %d ] it: %d\n",
         frameIndex,
         submeshIndex,
         subdivisionIterationCount);
  fflush(stdout);
  auto& infoLevelOfDetails  = recFrame.subdivInfoLevelOfDetails;
  auto& subdivEdges         = recFrame.subdivEdges;
  auto& subdivTexEdges      = recFrame.subdivTexEdges;
  int*  baseEdgesCount      = &recFrame.baseEdgesCount;
  int*  baseTexEdgesCount   = &recFrame.baseTexEdgesCount;
  auto& triangleBaseEdge    = recFrame.triangleBaseEdge;
  auto& texTriangleBaseEdge = recFrame.texTriangleBaseEdge;
  recFrame.rec              = base;
  if (interpolateSubdividedNormalsFlag) { recFrame.rec.computeNormals(); }
  recFrame.rec.subdivideMesh(subdivisionIterationCount,
                             edgeLengthThreshold,
                             bitshiftEdgeBasedSubdivision,
                             subdivMethods,
                             &infoLevelOfDetails,
                             &subdivEdges,
                             &subdivTexEdges,
                             nullptr,
                             baseEdgesCount,
                             baseTexEdgesCount,
                             &triangleBaseEdge,
                             &texTriangleBaseEdge);

  if (interpolateSubdividedNormalsFlag) {
#if DEBUG_INTERPOLATED_NORMALS
    std::cout << "interpolation of normals on subdiv edges" << std::endl;
#endif
    recFrame.rec.resizeNormals(recFrame.rec.pointCount());
    interpolateSubdivision(
      recFrame.rec.normals(), infoLevelOfDetails, subdivEdges, 0.5, 0.5, true);
  }

  return 0;
}
void
VMCDecoder::checkUVranges(int32_t                  fIdx,
                          int32_t                  submeshCount,
                          std::vector<VMCDecMesh>& decMesh) {
  //    for (int32_t fIdx = 0; fIdx < frameCount; ++fIdx) {
  for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    auto& submesh      = decMesh[submeshIdx].rec;
    auto  texCoordBBox = submesh.texCoordBoundingBox();
    if (texCoordBBox.min[0] < 0) {
      std::cout << "UVerror (frame " << fIdx << ", submesh " << submeshIdx
                << ") : texCoordBBox.min[0] < 0." << std::endl;
    }
    if (texCoordBBox.min[1] < 0) {
      std::cout << "UVerror (frame " << fIdx << ", submesh " << submeshIdx
                << ") : texCoordBBox.min[1] < 0." << std::endl;
    }
    if (texCoordBBox.max[0] > 1) {
      std::cout << "UVerror (frame " << fIdx << ", submesh " << submeshIdx
                << ") : texCoordBBox.max[0] > 1." << std::endl;
    }
    if (texCoordBBox.max[1] > 1) {
      std::cout << "UVerror (frame " << fIdx << ", submesh " << submeshIdx
                << ") : texCoordBBox.max[1] > 1." << std::endl;
    }
  }  //submeshIdx
  //    }
}
bool
VMCDecoder::LoDVideobitstreamExtraction(V3cBitstream&               syntax,
                                        const VMCDecoderParameters& params) {
  // read LoD extraction information SEI message
  const auto& adStream          = syntax.getAtlasDataStream();
  auto&       atl               = adStream.getAtlasTileLayerList()[0];
  bool        loadLoDExtraction = atl.getSEI().seiIsPresent(
    atlas::ATLAS_NAL_PREFIX_NSEI, atlas::LOD_EXTRACTION_INFORMATION);
  if (loadLoDExtraction) {
    std::cout << "LoD-based displacement video bitstream extraction process"
              << std::endl;
    auto& sei =
      static_cast<atlas::SEILoDExtractionInformation&>(*atl.getSEI().getSei(
        atlas::ATLAS_NAL_PREFIX_NSEI, atlas::LOD_EXTRACTION_INFORMATION));
    for (int submeshIdx = 0; submeshIdx <= sei.getNumSubmeshCount();
         submeshIdx++) {
      if ((params.targetLoD
           != sei.getSubmeshSubdivisionIterationCount(submeshIdx))
          && (params.targetLoD != -1)) {
        std::vector<uint8_t> targetIdx;
        for (int level = 0; level <= params.targetLoD; level++) {
          if (sei.getExtractableUnitTypeIdx() == 0) {  // MCTS
            if (find(targetIdx.begin(),
                     targetIdx.end(),
                     sei.getMCTSIdx(submeshIdx, level))
                != targetIdx.end())
              continue;
            else targetIdx.push_back(sei.getMCTSIdx(submeshIdx, level));
          } else if (sei.getExtractableUnitTypeIdx() == 1) {  // subpicture
            if (find(targetIdx.begin(),
                     targetIdx.end(),
                     sei.getsubpictureIdx(submeshIdx, level))
                != targetIdx.end())
              continue;
            else targetIdx.push_back(sei.getsubpictureIdx(submeshIdx, level));
          } else {
            std::cout << "not support" << std::endl;
          }
        }

        /*** LoD-based displacement video bitstream extraction process ***/
        std::string find_string = ".vmesh";
        // input bitstream path
        std::string inputBitstreamPath = params.resultPath;
        inputBitstreamPath.replace(inputBitstreamPath.find(find_string),
                                   find_string.length(),
                                   "_dispBitstream.bin");
        // output bitstream path
        std::string outputBitstreamPath = params.resultPath;
        outputBitstreamPath.replace(outputBitstreamPath.find(find_string),
                                    find_string.length(),
                                    "_extractedDispBitstream.bin");

        int extractableUnit = sei.getExtractableUnitTypeIdx();
        // 0 : HEVC MCTS
        // 1:  VVC subpicture

        // save displacement video bitstream
        vmesh::VideoBitstream& videoStream = syntax.getV3CUnitGVD(0);
        vmesh::save(inputBitstreamPath, videoStream.vector());

        if (extractableUnit == 0) {
          if (params.HMMCTSExtractorPath.empty()) {
            std::cout << "Cannot find the MCTS extractor path, build HM used "
                         "by script get_external_tools.sh --HM ";
          } else {
            // bitstream extractor (HM MCTS)
            std::stringstream cmd;
            cmd << params.HMMCTSExtractorPath;
            cmd << " -i " << inputBitstreamPath;
            cmd << " -b " << outputBitstreamPath;
            cmd << " -d " << std::to_string(targetIdx[0]);
            printf("cmd = %s \n", cmd.str().c_str());
            system(cmd.str().c_str());
          }
        }

        // LoD-based displacement video bitstream
        vmesh::VideoBitstream displacementvideoStream(
          vmesh::VIDEO_GEOMETRY_D0);
        videoStream.setVuhMapIndex(0);
        videoStream.setVuhAuxiliaryVideoFlag(0);
        videoStream.setVuhAttributeIndex(0);
        videoStream.setVuhAttributePartitionIndex(0);
        std::vector<uint8_t>& videoBitstream =
          displacementvideoStream.vector();
        std::ifstream fs(outputBitstreamPath, std::ios::in | std::ios::binary);
        std::vector<uint8_t> dispBitstream(
          (std::istreambuf_iterator<char>(fs)),
          std::istreambuf_iterator<char>());
        videoBitstream = dispBitstream;
        videoStream    = displacementvideoStream;
        fs.close();
        vmesh::removeFile(inputBitstreamPath);
        vmesh::removeFile(outputBitstreamPath);
      }
    }
  } else {
    std::cout
      << "Does not contain LoD-based extraction information sei message"
      << std::endl;
  }
  return 0;
}

void
VMCDecoder::setConsitantFourCCCode(const V3cBitstream& syntax) {
  const auto& adStream                = syntax.getAtlasDataStream();
  auto&       atl                     = adStream.getAtlasTileLayerList()[0];
  bool        loadConsitantFourCCCode = atl.getSEI().seiIsPresent(
    atlas::ATLAS_NAL_PREFIX_NSEI, COMPONENT_CODEC_MAPPING);
  if (loadConsitantFourCCCode) {
    auto& sei = static_cast<SEIComponentCodecMapping&>(*atl.getSEI().getSei(
      atlas::ATLAS_NAL_PREFIX_NSEI, COMPONENT_CODEC_MAPPING));
    consitantFourCCCode_.resize(256, std::string(""));
    for (size_t i = 0; i <= sei.getCodecMappingsCountMinus1(); i++) {
      auto codecId                  = sei.getCodecId(i);
      consitantFourCCCode_[codecId] = sei.getCodec4cc(codecId);
      printf("setConsitantFourCCCode: codecId = %3u  fourCCCode = %s \n",
             codecId,
             consitantFourCCCode_[codecId].c_str());
    }
  }
}

int
VMCDecoder::getCodedCodecId(const V3cBitstream&    syntax,
                            const V3CParameterSet& vps,
                            unsigned char          codedCodecId,
                            const bool             isVideo
                            ) {
  auto& plt = vps.getProfileTierLevel();
  if (isVideo) { // video codec group
    printf("getCodedCodecId profileVideoCodecGroupIdc = %d codedCodecId = %u \n",
          plt.getPtlVideoCodecGroupIdc(),
          codedCodecId);
    fflush(stdout);
    
    switch (plt.getPtlVideoCodecGroupIdc()) {
    case VideoCodecGroupIdc::AVC_Progressive_High: 
      break;
    case VideoCodecGroupIdc::HEVC_Main10:
    case VideoCodecGroupIdc::HEVC444:
    case VideoCodecGroupIdc::HEVC_Main:
#if defined(USE_HM_VIDEO_CODEC)
      return HM;
#else
      fprintf(stderr, "HM Codec not supported \n");
      exit(-1);
#endif
      break;
    case VideoCodecGroupIdc::VVC_Main10:
#if defined(USE_VTM_VIDEO_CODEC)
      return VTM;
#else
      fprintf(stderr, "VTM Codec not supported \n");
      exit(-1);
#endif
      break;
    case VideoCodecGroupIdc::Video_MP4RA:
      if (!consitantFourCCCode_[codedCodecId].empty()) {
        std::string codec4cc = consitantFourCCCode_[codedCodecId];
        printf(
          "=> codecId = %u => codec4cc = %s \n", codedCodecId, codec4cc.c_str());
        if (codec4cc.compare("avc3") == 0) {
          fprintf(stderr, "AVC Progressive High Codec not supported \n");
          exit(-1);
        } else if (codec4cc.compare("hev1") == 0) {
#if defined(USE_HM_VIDEO_CODEC)
          return HM;
#else
          fprintf(stderr, "HM Codec not supported \n");
          exit(-1);
#endif
        } else if (codec4cc.compare("hev3") == 0) {
#if defined(USE_SHMAPP_VIDEO_CODEC)
          return SHMAPP;
#else
          fprintf(stderr, "SHM Codec not supported \n");
          exit(-1);
#endif
        } else if (codec4cc.compare("vvi1") == 0) {
#if defined(USE_VTM_VIDEO_CODEC)
          return VTM;
#else
          fprintf(stderr, "VTM Codec not supported \n");
          exit(-1);
#endif
        } else {
          fprintf(stderr,
                  "CODEC_GROUP_MP4RA but codec4cc \"%s\" not supported \n",
                  codec4cc.c_str());
          exit(-1);
        }
      } else {
        fprintf(stderr,
                "CODEC_GROUP_MP4RA but component codec mapping SEI not present "
                "or codec index = %u not set \n",
                codedCodecId);
        exit(-1);
      }
      break;
    default:
      fprintf(stderr,
              "ProfileCodecGroupIdc = %d not supported \n",
              plt.getProfileCodecGroupIdc());
      exit(-1);
      break;
    }
    return UNKNOWN_VIDEO_ENCODER;
  }
  else { // Non video codec group
    printf("getCodedCodecId profileNonVideoCodecGroupIdc = %d codedCodecId = %u \n",
          plt.getPtlNonVideoCodecGroupIdc(),
          codedCodecId);
    fflush(stdout);

    switch (plt.getPtlNonVideoCodecGroupIdc()) {
    case NonVideoCodecGroupIdc::BMS_Main:
    case NonVideoCodecGroupIdc::BMS_Main_DAC_Main:
      return vmesh::GeometryCodecId::MPEG; 
    case NonVideoCodecGroupIdc::NonVideo_MP4RA:
      if (!consitantFourCCCode_[codedCodecId].empty()) {
        std::string codec4cc = consitantFourCCCode_[codedCodecId];
        printf(
          "=> codecId = %u => codec4cc = %s \n", codedCodecId, codec4cc.c_str());
        if (codec4cc.compare("bmsf") == 0) {
          return vmesh::GeometryCodecId::MPEG;
        } else if (codec4cc.compare("dacf") == 0) {
          return 0; 
        }  else {
          fprintf(stderr,
                  "NON_VIDEO_CODEC_GROUP_MP4RA but codec4cc \"%s\" not supported \n",
                  codec4cc.c_str());
          exit(-1);
        }
      } else {
        fprintf(stderr,
                "NON_VIDEO_CODEC_GROUP_MP4RA but component codec mapping SEI not present "
                "or codec index = %u not set \n",
                codedCodecId);
        exit(-1);
      }
      break;
    default:
      fprintf(stderr,
              "ProfileNonVideoCodecGroupIdc = %d not supported \n",
              plt.getPtlNonVideoCodecGroupIdc());
      exit(-1);
      break;
    }
    return vmesh::GeometryCodecId::UNKNOWN_GEOMETRY_CODEC;
  }
}

#if CONFORMANCE_LOG_DEC
void
VMCDecoder::getHighLevelSyntaxLog(
    V3cBitstream& syntax) {
    auto& adStream = syntax.getAtlasDataStream();
    auto& atlList = adStream.getAtlasTileLayerList();
    for (int atlIdx = 0; atlIdx < atlList.size(); atlIdx++) {
        auto& atgl = atlList[atlIdx];
        auto& ath = atgl.getHeader();
        auto afpsId = ath.getAtlasFrameParameterSetId();
        auto& afps = adStream.getAtlasFrameParameterSet(afpsId);
        auto  aspsId = afps.getAtlasSequenceParameterSetId();
        auto& asps = adStream.getAtlasSequenceParameterSet(aspsId);
        size_t atlasFrmOrderCntMsb = 0;
        size_t atlasFrmOrderCntVal = 0;
        int frameIndex = adStream.calculateAFOCval(asps, afps, atlIdx, atlasFrmOrderCntMsb, atlasFrmOrderCntVal);
        // PREFIX SEI
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, COMPONENT_CODEC_MAPPING)) {
            auto* sei =
                static_cast<SEIComponentCodecMapping*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, COMPONENT_CODEC_MAPPING));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********CODEC_COMPONENT_MAPPING_ESEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02dMD5 = %s\n", sei->getPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_EXTRACTION_INFORMATION)) {
            auto* sei =
                static_cast<SEIAttributeExtractionInformation*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_EXTRACTION_INFORMATION));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********ATTRIBUTE_EXTRACTION_INFORMATION_ESEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_TRANSFORMATION_PARAMS)) {
            auto* sei =
                static_cast<SEIAttributeTransformationParams*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_TRANSFORMATION_PARAMS));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********ATTRIBUTE_TRANSFORMATION_PARAMETERS_V3C_SEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02dMD5 = %s\n", sei->getPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, LOD_EXTRACTION_INFORMATION)) {
            auto* sei =
                static_cast<SEILoDExtractionInformation*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, LOD_EXTRACTION_INFORMATION));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********LOD_EXTRACTION_INFORMATION_ESEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, TILE_SUBMESH_MAPPING)) {
            auto* sei =
                static_cast<SEITileSubmeshMapping*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, TILE_SUBMESH_MAPPING));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********TILE_SUBMESH_MAPPING_SEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, ZIPPERING)) {
            auto* sei =
                static_cast<SEIZippering*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, ZIPPERING));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********ZIPPERING_SEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI, SUBMESH_SOI_RELATIONSHIP_INDICATION)) {
            auto* sei =
                static_cast<SEISubmeshSOIIndicationRelationship*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_NSEI, SUBMESH_SOI_RELATIONSHIP_INDICATION));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********SUBMESH_SOI_RELATIONSHIP_INDICATION_SEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI, SUBMESH_DISTORTION_INDICATION)) {
            auto* sei =
                static_cast<SEISubmeshDistortionIndication*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_NSEI, SUBMESH_DISTORTION_INDICATION));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********SUBMESH_DISTORTION_INDICATION_SEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        //SUFFIX SEI
        bool isLastTileOfTheFrames = atlIdx + 1 == atlList.size() ||
            adStream.calculateAFOCval(asps, afps, atlIdx, atlasFrmOrderCntMsb, atlasFrmOrderCntVal) !=
            adStream.calculateAFOCval(asps, afps, atlIdx + 1, atlasFrmOrderCntMsb, atlasFrmOrderCntVal);
        if (isLastTileOfTheFrames && atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI, DECODED_ATLAS_INFORMATION_HASH)) {
            auto* sei = static_cast<SEIDecodedAtlasInformationHash*>(
                atgl.getSEI().getSei(ATLAS_NAL_PREFIX_NSEI, DECODED_ATLAS_INFORMATION_HASH));
            auto& temp = sei->getMD5ByteStrData();
            if (temp.size() > 0) {
                TRACE_HLS("**********DECODED_ATLAS_INFORMATION_HASH_NSEI***********\n");
                Checksum checksum;
                TRACE_HLS("SEI%02dMD5 = %s\n", sei->getPayloadType(), checksum.getChecksum(temp).c_str());
            }
        }
        if (isLastTileOfTheFrames) {
            std::vector<uint8_t> highLevelAtlasData;
            Checksum    checksum;
            adStream.aspsCommonByteString(highLevelAtlasData, asps); 
            adStream.aspsApplicationByteString(highLevelAtlasData, asps);
            adStream.afpsCommonByteString(highLevelAtlasData, asps, afps);
            adStream.afpsApplicationByteString(highLevelAtlasData, asps, afps);
#if DEBUG_CONFORMANCE
            for (auto& c : highLevelAtlasData)
                TRACE_HLS("\n%02x", c);
            TRACE_HLS("\n");
#endif
            TRACE_HLS("AtlasFrameIndex = %d, ", frameIndex);
            TRACE_HLS("HLSMD5 = %s\n", checksum.getChecksum(highLevelAtlasData).c_str());
        }
    }
}
void
VMCDecoder::getPictureLog(FrameSequence<uint16_t> video)
{
    Checksum checksum;
    size_t frameIndex = 0;
    for (auto& image : video) {
        TRACE_PICTURE(" IdxOutOrderCntVal = %d, ", frameIndex++);
        TRACE_PICTURE(" MD5checksumChan0 = %s, ", checksum.getChecksum(image.plane(0)).c_str());
        TRACE_PICTURE(" MD5checksumChan1 = %s, ", checksum.getChecksum(image.plane(1)).c_str());
        TRACE_PICTURE(" MD5checksumChan2 = %s \n", checksum.getChecksum(image.plane(2)).c_str());
    }
    TRACE_PICTURE("Width =  %d, Height = %d \n", video.width(), video.height());
}
void
VMCDecoder::getBasemeshLog(
    std::vector<std::vector<VMCBasemesh>> basemeshes, 
    std::vector<uint32_t>& submeshIdtoIndex) {
    int numSubmeshes = basemeshes.size();
    int numFrames = basemeshes[0].size();
    for (int32_t frameIdx = 0; frameIdx < numFrames; frameIdx++) {
        TriangleMesh<MeshType> basemesh;
        uint32_t vertexCount = 0;
        TRACE_BASEMESH("IdxOutOrderCntVal = %d, ", frameIdx);
        TRACE_BASEMESH("NumSubmeshes = %d,\n", numSubmeshes);
        for (int smIdx = 0; smIdx < numSubmeshes; smIdx++) {
            int submeshId = -1;
            int numVertices = -1;
            int numAttributes = -1;
            auto& sm = basemeshes[smIdx][frameIdx].base;
            Checksum checksum;
            for (int i = 0; i < submeshIdtoIndex.size(); i++) {
                if (smIdx == submeshIdtoIndex[i]) {
                    submeshId = i;
                    break;
                }
            }
            numVertices = sm.pointCount();
            numAttributes = (sm.texCoordCount() > 0);
            TRACE_BASEMESH("SubmeshID = %d, NumVertices = %d, NumAttributes = %d, MD5checkSumSubmesh = %s,\n",
                submeshId, numVertices, numAttributes, checksum.getChecksumConformance(sm).c_str());
        }
    }
}
void
VMCDecoder::getACDisplacementLog(
    std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& decDisplacements,
    std::vector<uint32_t> subDisplIDToIdx) {
    int subDisplCount = decDisplacements.size();
    int numFrames = decDisplacements[0].size();
    for (int32_t frameIdx = 0; frameIdx < numFrames; frameIdx++) {
        TRACE_DISPLACEMENT("IdxOutOrderCntVal = %d, NumSubdisplacements = %d,\n", frameIdx, subDisplCount);
        for (int32_t subDisplIdx = 0; subDisplIdx < subDisplCount; subDisplIdx++) {
            auto& subDispl = decDisplacements[subDisplIdx][frameIdx];
            Checksum checksum;
            int displId = -1; 
            for (int id = 0; id < subDisplIDToIdx.size(); id++) {
                if (subDisplIDToIdx[id] == subDisplIdx) {
                    displId = id;
                    break;
                }
            }
            auto& submeshRecDisp = subDispl.disp;
            int numDisplacements = submeshRecDisp.size();
            TRACE_DISPLACEMENT("SubdisplacementID = %d, NumDisplacements = %d, MD5checkSumSubdisplacement = %s,\n",
                displId, numDisplacements, checksum.getChecksum(submeshRecDisp).c_str());
        }
    }
}
void
VMCDecoder::getAtlasLog(
    int32_t          frameIndex,
    std::vector<std::vector<AtlasTile>>& decodedTiles,
    V3cBitstream& syntax,
    const V3CParameterSet& vps) {
    TRACE_ATLAS("AtlasFrameIndex = %d\n", frameIndex);
    //get the first tile from the frame index to get the AFPS and ASPS
    auto& atlas = syntax.getAtlasDataStream();
    auto& atlList = atlas.getAtlasTileLayerList();
    //find the first tile for the frame index
    int tileIdx = -1;
    for (int i = 0; i < atlList.size() && tileIdx < 0; i++) {
        if (atlList[i].getHeader().getAtlasFrmOrderCntVal() == frameIndex)
            tileIdx = i;
    }
    auto& atlFirst = atlList[tileIdx];
    auto& athFirst = atlFirst.getHeader();
    TRACE_ATLAS("AtlasFrameOrderCntVal = %d, ", athFirst.getAtlasFrmOrderCntVal());
    auto atlasID = syntax.getAtlasId();
    TRACE_ATLAS("AtlasID = % d, ", atlasID)
        auto afpsID = athFirst.getAtlasFrameParameterSetId();
    auto& afps = atlas.getAtlasFrameParameterSet(afpsID);
    auto aspsID = afps.getAtlasSequenceParameterSetId();
    auto& asps = atlas.getAtlasSequenceParameterSet(aspsID);
    auto& asve = asps.getAsveExtension();
    TRACE_ATLAS("AtlasFrameWidth =  %d, AtlasFrameHeight = %d, ASPSFrameSize = %d, ",
        asps.getFrameWidth(), asps.getFrameHeight(), asps.getFrameWidth() * asps.getFrameHeight());
    int attrCount = 0;
    if (vps.getAttributeVideoPresentFlag(0) == 0) {
        // check if the PACKED VIDEO is present
        if (vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(0)) {
            attrCount = vps.getVpsPackedVideoExtension().getPackingInformation(0).pin_attribute_count();
        }
    }
    else {
        auto& ai = vps.getAttributeInformation(atlasID);
        attrCount = ai.getAttributeCount();
    }
    TRACE_ATLAS("AttributeCount  = % d, ", attrCount);
    auto& vpve = vps.getVpsVdmcExtension();
    int attrMaxWidth = 0;
    int attrMaxHeight = 0;
    int numAttrFrameSize = vpve.getVpsExtConsistentAttributeFrameFlag(atlasID)
                             ? vps.getVpsAttributeNominalFrameSizeCount(atlasID)
                             : attrCount;
    for (int i = 0; i < numAttrFrameSize; i++) {
        if (vpve.getVpsExtAttributeFrameHeight(atlasID, i) > attrMaxHeight)
            attrMaxHeight = vpve.getVpsExtAttributeFrameHeight(atlasID, i);
        if (vpve.getVpsExtAttributeFrameWidth(atlasID, i) > attrMaxWidth)
            attrMaxWidth = vpve.getVpsExtAttributeFrameWidth(atlasID, i);
    }
    TRACE_ATLAS("AttributeFrameWidthMax =  %d, AttributeFrameHeightMax = %d, AttributeFrameSizeMax = %d, ",
        attrMaxWidth, attrMaxHeight, attrMaxWidth * attrMaxHeight);
    TRACE_ATLAS("BasemeshAttributeCount  = % d, ", vpve.getVpsExtMeshDataAttributeCount(atlasID));
    auto& afti = afps.getAtlasFrameTileInformation();
    TRACE_ATLAS("NumGeometryTilesAtlasFrame   = % d, ", afti.getTileCount());
    auto& afve = afps.getAfveExtension();
    int numAttrTilesMax = 0;
    for (int i = 0; i < afve.getAtlasFrameTileAttributeInformation().size(); i++) {
        if (afve.getAtlasFrameTileAttributeInformation(i).getTileCount() > numAttrTilesMax)
            numAttrTilesMax = afve.getAtlasFrameTileAttributeInformation(i).getTileCount();
    }
    TRACE_ATLAS("NumAttributeTilesAtlasFrameMax   = % d, ", numAttrTilesMax);
    auto& afmi = afve.getAtlasFrameMeshInformation();
    TRACE_ATLAS("NumSubmeshes = % d, ", afmi.getNumSubmeshesInAtlasFrameMinus1() + 1);
    //now count all the meshpatches in the geometry and attribute tiles
    int numTiles = afti.getTileCount() + numAttrTilesMax;
    int atlasTotalMeshpatches = 0;
    std::vector<uint8_t> atlasData;
    Checksum    checksum;
    for (int tileLayerIndex = 0; tileLayerIndex < atlas.getAtlasTileLayerList().size(); tileLayerIndex++) {
        auto& atl = atlas.getAtlasTileLayerList()[tileLayerIndex];
        auto& ath = atl.getHeader();
        if (ath.getFrameIndex() == frameIndex) {
          int tileIndex = -1;
          if (ath.getType() == I_TILE_ATTR) {
            //search for the index in the afati
            for (int attrIdx = 0; attrIdx < asve.getAspsAttributeNominalFrameSizeCount(); attrIdx++) {
              auto& afati = afve.getAtlasFrameTileAttributeInformation(attrIdx);
              for (int i = 0; i < afati.getNumTilesInAtlasFrameMinus1() + 1; i++) {
                if (afati.getTileId(i) == ath.getAtlasTileHeaderId()) {
                  tileIndex = afati.getTileIndexWithOffset(ath.getAtlasTileHeaderId());
                  break;
                }
              }
            }
          }
          else {
            //search for index in afti
            auto& afti = afps.getAtlasFrameTileInformation();
            for (int i = 0; i < afti.getNumTilesInAtlasFrameMinus1() + 1; i++) {
              if (afti.getTileId(i) == ath.getAtlasTileHeaderId()) {
                tileIndex = i;
                break;
              }
            }
          }
        int numMeshpatchesInTile = decodedTiles[tileIndex][frameIndex].patches_.size();
        atlasTotalMeshpatches += numMeshpatchesInTile;
        for (int patchIndex = 0; patchIndex < numMeshpatchesInTile; patchIndex++) {
            auto& meshpatchHLS = atl.getDataUnit().getPatchInformationData(patchIndex);
            auto& meshPatchDec = decodedTiles[tileIndex][frameIndex].patches_[patchIndex];
            if ((ath.getType() == SKIP_TILE) || (ath.getType() == SKIP_TILE_ATTR)) {
              // do nothing
            }
            else if ((ath.getType() == I_TILE) || (ath.getType() == P_TILE)) {
                atlas.atlasMeshpatchCommonByteString(atlasData, asps, asve, afve, meshPatchDec, true);
            }
            else {
                auto& afati = afve.getAtlasFrameTileAttributeInformation();
                int attrIdx = 0;
                for (int i = 0; i < afati.size(); i++) {
                    for (int j = 0; j < afati[i].getTileCount(); j++) {
                        if (ath.getAtlasTileHeaderId() == afati[i].getTileId(j))
                        {
                            attrIdx = i;
                            break;
                        }
                    }
                }
                atlas.atlasMeshpatchCommonByteString(atlasData, asps, asve, afve, meshPatchDec, false, attrIdx);
            }
        }
    }
    }
    TRACE_ATLAS("AtlasTotalMeshpatches = % d, ", atlasTotalMeshpatches);
#if DEBUG_CONFORMANCE
    for (auto& c : atlasData)
        TRACE_ATLAS("\n%02x", c);
    TRACE_ATLAS("\n");
#endif
    TRACE_ATLAS("ATLASMD5 = %s\n", checksum.getChecksum(atlasData).c_str());
}
void
VMCDecoder::getTileLog(
    int32_t                     frameIndex,
    std::vector<std::vector<AtlasTile>>& decodedTiles,
    V3cBitstream& syntax) {
    auto& atlas = syntax.getAtlasDataStream();
    TRACE_TILE("AtlasFrameIndex = %d\n", frameIndex);
    for (int tileIdx = 0; tileIdx < decodedTiles.size(); tileIdx++) {
      if (decodedTiles[tileIdx][frameIndex].patches_.size() == 0) {
        // the NAL unit from this tile has been skipped, no need to check conformance
        continue;
      }
        auto tileID = decodedTiles[tileIdx][frameIndex].tileId_;
        TRACE_TILE("TileID = % d, AtlasFrameOrderCntVal = %d, TileType = %d, ", tileID, frameIndex, (int)decodedTiles[tileIdx][frameIndex].tileType_);
        auto& afps = atlas.getAtlasFrameParameterSet(decodedTiles[tileIdx][frameIndex].tileAfpsId_);
        auto& afve = afps.getAfveExtension();
        auto aspsID = afps.getAtlasSequenceParameterSetId();
        auto& asps = atlas.getAtlasSequenceParameterSet(aspsID);
        auto& asve = asps.getAsveExtension();
        int tileOffsetX, tileOffsetY, tileWidth, tileHeight = 0;
        if ((decodedTiles[tileIdx][frameIndex].tileType_ == I_TILE_ATTR) || (decodedTiles[tileIdx][frameIndex].tileType_ == P_TILE_ATTR)) {
            int attrIdx = 0;
            int attrTileIdx = 0;
            //search for the indices given the tile ID
            auto& afatiList = afve.getAtlasFrameTileAttributeInformation();
            for (int j = 0; j < afatiList.size(); j++) {
                for (int l = 0; l < afatiList[j].getNumTilesInAtlasFrameMinus1() + 1; l++) {
                    if (afatiList[j].getTileId(l) == tileID) {
                        attrIdx = j;
                        attrTileIdx = l;
                        break;
                    }
                }
            }
            std::vector<imageArea> decodedTextureTileAreas = decodedTiles[tileIdx][frameIndex].tileAttributeAreas_;
            TRACE_TILE("TileOffsetX = %d, TileOffsetY = %d, TileWidth = %d, TileHeight = %d, ",
                decodedTextureTileAreas[attrTileIdx].LTx,
                decodedTextureTileAreas[attrTileIdx].LTy,
                decodedTextureTileAreas[attrTileIdx].sizeX,
                decodedTextureTileAreas[attrTileIdx].sizeY)
        }
        else {
            int geoTileIdx = 0;
            //search for the indices given the tile ID
            auto& afti = afps.getAtlasFrameTileInformation();
            for (int l = 0; l < afti.getNumTilesInAtlasFrameMinus1() + 1; l++) {
                if (afti.getTileId(l) == tileID) {
                    geoTileIdx = l;
                    break;
                }
            }
            imageArea decodedGeometryTileAreas = decodedTiles[tileIdx][frameIndex].tileGeometryArea_;
            TRACE_TILE("TileOffsetX = %d, TileOffsetY = %d, TileWidth = %d, TileHeight = %d, ",
                decodedGeometryTileAreas.LTx,
                decodedGeometryTileAreas.LTy,
                decodedGeometryTileAreas.sizeX,
                decodedGeometryTileAreas.sizeY)
        }
        //now count all the meshpatches in the geometry and attribute tiles
        Checksum    checksum;
        std::vector<uint8_t> atlasData;
        AtlasTileHeader ath;
        ath.setAtlasTileHeaderId(decodedTiles[tileIdx][frameIndex].tileId_);
        ath.setType(decodedTiles[tileIdx][frameIndex].tileType_);
        int numMeshpatchesInTile = decodedTiles[tileIdx][frameIndex].patches_.size();
        for (int patchIndex = 0; patchIndex < numMeshpatchesInTile; patchIndex++) {
            auto& meshPatchDec = decodedTiles[tileIdx][frameIndex].patches_[patchIndex];
            atlas.tileMeshpatchCommonByteString(atlasData, asps, asve, afve, meshPatchDec, ath);
        }
        TRACE_TILE("TileTotalMeshpatches = % d, ", numMeshpatchesInTile);
#if DEBUG_CONFORMANCE
        for (auto& c : atlasData)
            TRACE_TILE("\n%02x", c);
        TRACE_TILE("\n");
#endif
        TRACE_TILE("TILEMD5 = %s\n", checksum.getChecksum(atlasData).c_str());
    }
}
void
VMCDecoder::getMFrameLog(
    int32_t frameIdx,
    std::vector<VMCDecMesh> meshes) {
    TriangleMesh<MeshType> mesh;
    uint32_t vertexCount = 0;
    int submeshCount = meshes.size();
    if (submeshCount == 1) {
        mesh = meshes[0].rec;
    }
    else {
        for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
            mesh.append(meshes[submeshIdx].rec);
        }
    }
    vertexCount = mesh.pointCount();
    TRACE_MFRAME("AtlasFrameIndex = %d, ", frameIdx);
    TRACE_MFRAME("MeshFrameOrderCntVal = %d, NumSubmeshes = %d, NumVertices = %d,",
        frameIdx, submeshCount, vertexCount);
    Checksum    checksum;
    TRACE_MFRAME(" MD5checksum = %s\n", checksum.getChecksumConformance(mesh).c_str());
}
void
VMCDecoder::getRecFrameLog(int32_t        frameIndex,
    TriangleMesh<MeshType>& reconstructed) {
    TRACE_RECFRAME("AtlasFrameIndex = %d, MeshFrameOrderCntVal = %d, NumVertices = %d,",
        frameIndex, frameIndex, reconstructed.pointCount());
    Checksum    checksum;
    TRACE_RECFRAME(" MD5checksum = %s\n", checksum.getChecksumConformance(reconstructed).c_str());
}
#endif

}  // namespace vmesh
