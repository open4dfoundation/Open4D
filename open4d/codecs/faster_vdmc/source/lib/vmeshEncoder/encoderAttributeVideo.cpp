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
#include "vmc.hpp"
#include "util/misc.hpp"

#include "transferColor.hpp"
#include "videoEncoder.hpp"
#if defined(USE_X265_VIDEO_CODEC)
#  include "x265VideoEncoder.hpp"
#endif

using namespace vmesh;

bool
VMCEncoder::compressVideoAttribute(std::vector<std::vector<vmesh::VMCSubmesh>>&
                                             encSubmeshes,  //[submesh][frame]
                                   Sequence& reconstruct,
                                   int32_t   submeshCount,
                                   int32_t   frameCount,
                                   int32_t   frameOffset,
                                   int32_t   attrIndex,
                                   V3cBitstream&               syntax,
                                   V3CParameterSet&            vps,
                                   const VMCEncoderParameters& params) {
  auto  sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[attrIndex];
  auto  sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[attrIndex];
  auto  videoAttributeColourSpace_  = videoAttributeColourSpaces_[attrIndex];
  auto  videoAttributeBitdetph_     = videoAttributeBitdetphs_[attrIndex];
  auto& videoSize                   = attributeVideoSize_[attrIndex];
  reconstruct.attribute(attrIndex).resize(videoSize.first,
                                          videoSize.second,
                                          sourceAttributeColourSpace_,
                                          frameCount);
#if 1
  if (params.numTilesAttribute != 1 || params.numSubmesh != 1) {
    std::cout << "***subtexture placement attrIndex " << attrIndex
              << " ---------------------------\n";
    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < params.numTilesGeometry + params.numTilesAttribute;
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto LTx = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[attrIndex]
                     .LTx;
        auto LTy = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[attrIndex]
                     .LTy;
        auto sizeX = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[attrIndex]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[attrIndex]
                       .sizeY;
        std::cout << "Tile[" << tileIdx << "] patch[" << patchIdx << "] ";
        std::cout << "submeshId " << submeshId;
        std::cout << " subTexture[" << submeshIdtoIndex_[submeshId] << "]";
        std::cout << " pos in Tile: [" << LTx << "," << LTy << "]";
        //std::cout << " pos in Frame: [" << LTx << "," << LTy <<"]";
        std::cout << " size in Tile: " << sizeX << "x" << sizeY << "\n";
      }
    }
  }
#endif
  bool                           retTexCoding = true;
  vmesh::FrameSequence<uint16_t> textures16bits;
  textures16bits.resize(attributeVideoSize_[attrIndex].first,
                        attributeVideoSize_[attrIndex].second,
                        videoAttributeColourSpace_,
                        0);
  int numLayerMinus1 = params.maxLayersMinus1;
  std::vector<vmesh::FrameSequence<uint16_t>> textures16bitsLayers(
    numLayerMinus1 + 1);
  // create a sequence of uncompressed texture frames
  std::vector<Plane<uint8_t>> oneOccupancySequence(frameCount);
  printf("attribute video %d\t padding: %d textureTransferEnable: %d\n",
         attrIndex,
         (int)params.attributeParameters[attrIndex].enablePadding,
         (int)params.attributeParameters[attrIndex].textureTransferEnable);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    std::vector<Frame<uint8_t>> sourceTexture(
      1);  //only to be used as an input of tranfer

    if (!sourceTexture[0].load(inputAttributesPaths_[attrIndex],
                               frameIndex + frameOffset + startFrame_)) {
      printf("fail to load texture : %s frame %d\n",
             inputAttributesPaths_[attrIndex].c_str(),
             frameIndex + frameOffset + startFrame_);
      exit(2);
    };
    Frame<uint16_t> oneTrTexture;
    oneTrTexture.resize(attributeVideoSize_[attrIndex].first,
                        attributeVideoSize_[attrIndex].second,
                        sourceAttributeColourSpaces_[attrIndex]);
    bool enablePadding = params.attributeParameters[attrIndex].enablePadding;
    Plane<uint8_t>& oneOccupancy = oneOccupancySequence[frameIndex];
    if (params.numSubmesh != 1) {
      enablePadding = false;
      oneOccupancy.resize(videoSize.first, videoSize.second);
    }

    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < (params.numTilesGeometry + params.numTilesAttribute);
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto submeshIdx = submeshIdtoIndex_[submeshId];
        auto sizeX      = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[attrIndex]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[attrIndex]
                       .sizeY;
        auto LTx = reconAtlasTiles_[tileIdx][frameIndex]
                     .patches_[patchIdx]
                     .attributePatchArea[attrIndex]
                     .LTx
                   + reconAtlasTiles_[tileIdx][frameIndex]
                       .tileAttributeAreas_[attrIndex]
                       .LTx;
        auto LTy = reconAtlasTiles_[tileIdx][frameIndex]
                     .patches_[patchIdx]
                     .attributePatchArea[attrIndex]
                     .LTy;
        +reconAtlasTiles_[tileIdx][frameIndex]
           .tileAttributeAreas_[attrIndex]
           .LTx;

        vmesh::TriangleMesh<MeshType> sourceMesh;
        auto                          sourceMeshPath = inputMeshPath_;
        if (params.numSubmesh != 1 && !params.segmentByBaseMesh) {
          sourceMeshPath = inputSubmeshPaths_[submeshIdx];
        }
        if (!sourceMesh.load(sourceMeshPath,
                             frameIndex + frameOffset + startFrame_)) {
          printf("fail to load mesh : %s frame %d submesh %d\n",
                 sourceMeshPath.c_str(),
                 frameIndex + frameOffset + startFrame_,
                 submeshIdx);
          exit(2);
        }
        Frame<uint8_t> subTexture;
        auto           subTextureWidth  = videoSize.first;
        auto           subTextureHeight = videoSize.second;

        if (params.numSubmesh != 1) {
          //predefined submesh texture image size
          subTextureWidth  = sizeX;
          subTextureHeight = sizeY;
        }
        subTexture.resize(
          subTextureWidth, subTextureHeight, sourceAttributeColourSpace_);
        TriangleMesh<MeshType>& recSubmesh =
          reconSubdivmeshes_[submeshIdx][frameIndex];

        Plane<uint8_t> occupancy;
        tic("ColorTransfer");
        if (params.attributeParameters[attrIndex].textureTransferEnable) {
          TransferColor                transferColor;
          auto&                        refMesh        = sourceMesh;
          std::vector<Frame<uint8_t>>& refTexture     = sourceTexture;
          auto&                        targetMesh     = recSubmesh;
          auto&                        targetTexture  = subTexture;
          bool                         usePastTexture = false;
          Frame<uint8_t>               pastTexture;
          //TODO: [sw] reconstruct.texture memory management
          if (submeshCount == 1 && params.textureTransferCopyBackground
              && frameIndex > 0
              && encSubmeshes[submeshIdx][frameIndex].submeshType
                   == basemesh::P_BASEMESH) {
            //occupancy map will be the same as the reference, so use the background from reference as well
            pastTexture = reconstruct.attributeFrame(
              attrIndex,
              encSubmeshes[submeshIdx][frameIndex].referenceFrameIndex);
            //std::string filename = "_test_"+std::to_string(frameIndex)+".png";
            //pastTexture.save(filename);
            usePastTexture = true;
          }
          if (!transferColor.transfer(refMesh,
                                      refTexture,
                                      targetMesh,
                                      targetTexture,
                                      usePastTexture,
                                      pastTexture,
                                      params,
                                      occupancy,
                                      enablePadding,
                                      submeshIdx)) {
            printf("fail to transferTexture\n");
            return false;
          }
        } else {
          //subTexture = sourceTexture;
          //reconstruct.texture(frameIndex) = sourceTexture;
          if (submeshCount == 1) {
            subTexture = sourceTexture[0];
            occupancy.resize(videoSize.first, videoSize.second);
          } else {
            occupancy.resize(subTextureWidth, subTextureHeight);
            for (int32_t i = 0; i < oneTrTexture.planeCount(); i++) {
              for (int32_t y = LTy; y < (LTy + sizeY); y++) {
                for (int32_t x = LTx; x < (LTx + sizeX); x++) {
                  auto v = sourceTexture[0].plane(i).get(y, x);
                  subTexture.plane(i).set((y - LTy), (x - LTx), v);
                }
              }
            }
          }
        }
        toc("ColorTransfer");

        if (params.checksum) {
          Checksum checksumRec;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIndex)
                                + "\tAttributes[" + std::to_string(attrIndex)
                                + "] submesh[" + std::to_string(submeshIdx)
                                + "] ";
          checksumRec.print(subTexture, eString);
          std::cout
            << "**********************************************************\n";
        }
        if (submeshCount == 1) {
          oneTrTexture = subTexture;
          oneOccupancy = occupancy;
        } else
          placeTextures(
            attrIndex,
            frameIndex,
            tileIdx,
            patchIdx,
            subTexture,  //textureSubmesh
            oneTrTexture,
            occupancy,
            oneOccupancy,
            !enablePadding
              && params.attributeParameters[attrIndex].textureTransferEnable,
            params);
      }  //patchIdx
    }
    if (!enablePadding
        && params.attributeParameters[attrIndex].textureTransferEnable
        && params.textureTransferPaddingMethod != PaddingMethod::NONE) {
      std::cout << "background Filling : " << oneTrTexture.width() << "x"
                << oneTrTexture.height() << "\n";
      //NOTE: textureBackgroundFilling is the same as dilateTexture but the first input is in uint16_t instead of uint_8
      textureBackgroundFilling(oneTrTexture, oneOccupancy, params);
    }
    reconstruct.attributeFrame(attrIndex, frameIndex) = oneTrTexture;
    if (params.checksum) {
      Checksum checksumRec;
      std::cout
        << "**********************************************************\n";
      std::string eString =
        "frame " + std::to_string(frameIndex) + "\toneTrTexture ";
      checksumRec.print(oneTrTexture, eString);
      std::cout
        << "**********************************************************\n";
    }

    convertTextureImage(
      oneTrTexture,
      frameIndex,
      sourceAttributeColourSpace_,
      sourceAttributeBitdetph_,
      videoAttributeColourSpace_,
      videoAttributeBitdetph_,
      params.attributeParameters[attrIndex].textureVideoDownsampleFilter,
      params.attributeParameters[attrIndex].textureVideoFullRange);
    if (params.scalableEnableFlag) {
      textures16bitsLayers[numLayerMinus1].frames().push_back(oneTrTexture);
      auto&                        vpsExt = vps.getVpsVdmcExtension();
      int                          textureWidth, textureHeight;
      std::vector<Frame<uint8_t>>  textureLoDVec(numLayerMinus1);
      std::vector<Frame<uint16_t>> oneTrsTexture(numLayerMinus1);
      for (int i = 0; i < numLayerMinus1; i++) {
        textureWidth  = videoSize.first >> (numLayerMinus1 - i);
        textureHeight = videoSize.second >> (numLayerMinus1 - i);
        textureLoDVec[i].resize(
          textureWidth, textureHeight, videoAttributeColourSpace_);
        oneTrsTexture[i].resize(
          textureWidth, textureHeight, videoAttributeColourSpace_);
      }
      std::cout
        << "smoothed pushpull-based multi-resolution texture map generation"
        << std::endl;
      SmoothedPushPullMipMap(reconstruct.attributeFrame(attrIndex, frameIndex),
                             textureLoDVec,
                             oneOccupancySequence[frameIndex],
                             numLayerMinus1);
      for (int i = 0; i < numLayerMinus1; i++) {
        oneTrsTexture[i] = textureLoDVec[i];
        convertTextureImage(
          oneTrsTexture[i],
          frameIndex,
          sourceAttributeColourSpace_,
          sourceAttributeBitdetph_,
          videoAttributeColourSpace_,
          videoAttributeBitdetph_,
          params.attributeParameters[attrIndex].textureVideoDownsampleFilter,
          params.attributeParameters[attrIndex].textureVideoFullRange);
        textures16bitsLayers[i].frames().push_back(oneTrsTexture[i]);
      }
    } else textures16bits.frames().push_back(oneTrTexture);
  }  //frameIndex
  if (params.scalableEnableFlag) {
    for (int i = 0; i <= numLayerMinus1; i++) {
      auto        width  = textures16bitsLayers[i].frame(0).width();
      auto        height = textures16bitsLayers[i].frame(0).height();
      std::string result = removeExtension(params.resultPath);
      std::string multilayerTexures = result + "_" + std::to_string(width)
                                      + "x" + std::to_string(height) + ".yuv";
      textures16bitsLayers[i].save(multilayerTexures);
    }
  }
  if (params.attributeParameters[attrIndex].useOccMapRDO
      && !params.attributeParameters[attrIndex].textureVideoUseX265
      && !params.scalableEnableFlag) {
    if (!create3DMotionEstimationFiles(
          oneOccupancySequence,
          params.attributeParameters[attrIndex].occMapFilename)) {
      std::cerr << "Error: fail to create3DMotionEstimationFiles!\n";
      exit(2);
    }
  }
  std::cout << '\n';
  // compress texture
  auto& attributeVideoSubstream = syntax.addVideoSubstream(V3C_AVD);
  // vps_atlas_count vps_atlas_id - unrelated to vps_v3c_parameter_set_id
  // potential out of range access here
  attributeVideoSubstream.setAtlasId(
    vps.getAtlasId( 0 /* vps.getV3CParameterSetId() */)); // parameter sets overriden, always at 0
  attributeVideoSubstream.setV3CParameterSetId(vps.getV3CParameterSetId());
  attributeVideoSubstream.setVuhAttributeIndex(attrIndex);
  //attributeVideoSubstream.setVideoType(params.videoAttributeTypes[attrIndex]);
  if (params.encodeTextureVideo)
#if CONFORMANCE_LOG_ENC
  TRACE_PICTURE("Attribute\n");
  int attrTypeId = vps.getAttributeInformation(vps.getAtlasId(0 /* vps.getV3CParameterSetId() */)).getAttributeTypeId(attrIndex);
  TRACE_PICTURE("MapIdx = 0, AuxiliaryVideoFlag = 0, AttrIdx = %d, AttrPartIdx = 0, AttrTypeID = %d\n", attrIndex, attrTypeId);
#endif
    retTexCoding &= compressAttributeVideo(
      attributeVideoSubstream,
      (!params.scalableEnableFlag) ? textures16bits
                                   : textures16bitsLayers[numLayerMinus1],
      reconstruct,
      attrIndex,
      frameOffset,
      params);
  return retTexCoding;
}
bool
VMCEncoder::compressAttributeVideo(
  VideoBitstream&             videoStream,
  FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
  Sequence&                   reconstruct,
  int                         attrIdx,
  int32_t                     frameOffset,
  const VMCEncoderParameters& params) {
  printf("Compress texture video \n");
  fflush(stdout);
  const auto frameCount = videoSrcSequence.frameCount();

  auto videoAttributeColourSpace_  = videoAttributeColourSpaces_[0];
  auto videoAttributeBitdetph_     = videoAttributeBitdetphs_[0];
  auto sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[0];
  auto sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[0];

  videoSrcSequence.setSequenceInfo(attributeVideoSize_[attrIdx].first,
                                   attributeVideoSize_[attrIdx].second,
                                   videoAttributeColourSpace_);
  // Save intermediate files
  if (params.keepVideoFiles) {
    videoSrcSequence.save(videoSrcSequence.createName(
      _keepFilesPathPrefix + "tex_enc", videoAttributeBitdetph_));
  }
  if (params.checksum) {
    Checksum checksumRec;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(0) + "\tTextureConverted ";
    checksumRec.print(videoSrcSequence[0], eString);
    std::cout
      << "**********************************************************\n";
  }
  //Encode
  VideoEncoderParameters videoEncoderParams;
  videoEncoderParams.encoderConfig_ =
    params.attributeParameters[attrIdx].textureVideoEncoderConfig;
  videoEncoderParams.inputBitDepth_    = videoAttributeBitdetph_;
  videoEncoderParams.internalBitDepth_ = videoAttributeBitdetph_;
  //    videoEncoderParams.outputBitDepth_   = videoEncoderParams.inputBitDepth_;
  videoEncoderParams.qp_ = params.attributeParameters[attrIdx].textureVideoQP;
  videoEncoderParams.allIntra_ =
    params.attributeParameters[attrIdx].textureVideoAllIntra;
  videoEncoderParams.enableWavefront_ =
    params.attributeParameters[attrIdx].textureVideoWavefront;
  videoEncoderParams.frameThreadCount_ =
    params.attributeParameters[attrIdx].textureVideoFrameThreads;
  videoEncoderParams.poolThreadCount_ =
    params.attributeParameters[attrIdx].textureVideoPoolThreads;
  videoEncoderParams.sliceCount_ =
    params.attributeParameters[attrIdx].textureVideoSliceCount;
  videoEncoderParams.encoderPreset_ =
    params.attributeParameters[attrIdx].textureVideoX265Preset;
  videoEncoderParams.TileUniformSpacing_ = true;
  videoEncoderParams.NumTileColumnsMinus1_ = std::max(
    0, params.attributeParameters[attrIdx].textureVideoTileColumns - 1);
  videoEncoderParams.NumTileRowsMinus1_ = std::max(
    0, params.attributeParameters[attrIdx].textureVideoTileRows - 1);
  if (params.attributeParameters[attrIdx].useOccMapRDO
      && !params.attributeParameters[attrIdx].textureVideoUseX265) {
    videoEncoderParams.usePccRDO_ = true;
    videoEncoderParams.occupancyMapFile_ =
      params.attributeParameters[attrIdx].occMapFilename;
  }
  if (params.textureParameterizationType == 1 && params.packingType == 3) {
    videoEncoderParams.textureParameterizationType_ =
      params.textureParameterizationType;
    videoEncoderParams.packingType_       = params.packingType;
    videoEncoderParams.maxCUWidth_        = params.maxCUWidth;
    videoEncoderParams.maxCUHeight_       = params.maxCUHeight;
    videoEncoderParams.maxPartitionDepth_ = params.maxPartitionDepth;
  }
  if (params.scalableEnableFlag) {
    auto numLayerMinus1           = params.maxLayersMinus1;
    videoEncoderParams.maxLayers_ = numLayerMinus1 + 1;
    videoEncoderParams.srcYuvFileNameLayer_.resize(
      videoEncoderParams.maxLayers_);
    int         width                    = videoSrcSequence.width();
    int         height                   = videoSrcSequence.height();
    std::string result                   = removeExtension(params.resultPath);
    videoEncoderParams.multiLayerCoding_ = true;
    videoEncoderParams.binFileName_      = result + ".bin";
    for (int i = 0; i <= numLayerMinus1; i++) {
      videoEncoderParams.srcYuvFileNameLayer_[i] =
        result + "_" + std::to_string(width >> (numLayerMinus1 - i)) + "x"
        + std::to_string(height >> (numLayerMinus1 - i)) + ".yuv";
    }
    videoEncoderParams.recYuvFileNameLayer_ =
      result + "_rec_" + std::to_string(width) + "x" + std::to_string(height)
      + ".yuv";
#ifdef USE_SHMAPP_VIDEO_CODEC
    videoEncoderParams.encoderPath_ = SHM_ENC_PATH;
#endif
  }
  printf("textureVideoEncoderId = %d(%s) CodeGroup Idc: %s \n",
         (int)params.attributeParameters[attrIdx].textureVideoEncoderId,
         toString(params.attributeParameters[attrIdx].textureVideoEncoderId)
           .c_str(),
         toString(params.profileCodecGroupId).c_str());
  fflush(stdout);
  FrameSequence<uint16_t> rec;
  std::vector<uint8_t>&   videoBitstream = videoStream.vector();
  std::shared_ptr<VideoEncoder<uint16_t>> encoder;
#if defined(USE_X265_VIDEO_CODEC)
  if (params.attributeParameters[attrIdx].textureVideoUseX265) {
    encoder = std::make_shared<x265VideoEncoder<uint16_t>>();
  } else
#endif
  {
    encoder = VideoEncoder<uint16_t>::create(
      params.attributeParameters[attrIdx].textureVideoEncoderId);
  }
  encoder->encode(videoSrcSequence, videoEncoderParams, videoBitstream, rec);
#if CONFORMANCE_LOG_ENC
  size_t frameIndex = 0;
  Checksum checksum;
  for (auto& image : rec) {
      TRACE_PICTURE(" IdxOutOrderCntVal = %d, ", frameIndex++);
      TRACE_PICTURE(" MD5checksumChan0 = %s, ", checksum.getChecksum(image.plane(0)).c_str());
      TRACE_PICTURE(" MD5checksumChan1 = %s, ", checksum.getChecksum(image.plane(1)).c_str());
      TRACE_PICTURE(" MD5checksumChan2 = %s \n", checksum.getChecksum(image.plane(2)).c_str());
  }
  TRACE_PICTURE("Width =  %d, Height = %d \n", rec.width(), rec.height());
#endif
  if (params.scalableEnableFlag) {
    auto numLayerMinus1 = params.maxLayersMinus1;
    rec.resize(videoSrcSequence.width(),
               videoSrcSequence.height(),
               ColourSpace::YUV420p,
               frameCount);
    if (!params.keepVideoFiles) {
      remove(videoEncoderParams.binFileName_.c_str());
      for (int i = 0; i <= numLayerMinus1; i++) {
        remove(videoEncoderParams.srcYuvFileNameLayer_[i].c_str());
      }
      remove(videoEncoderParams.recYuvFileNameLayer_.c_str());
    }
  }
  videoSrcSequence.clear();
  printf("encode texture video done \n");
  fflush(stdout);
  if (params.attributeExtractionEnable)
    setAttributeMCTSVideoEncoderParams(params, videoEncoderParams);

  // Save intermediate files
  if (params.keepVideoFiles) {
    rec.save(rec.createName(_keepFilesPathPrefix + "tex_rec",
                            videoAttributeBitdetph_));
    save(_keepFilesPathPrefix + ".h265", videoBitstream);
  }
  //    reconstruct.textures() = rec;
  // Convert Rec YUV420 to BGR444
  for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    convertTextureImage(
      rec.frame(frameIdx),
      frameIdx,
      videoAttributeColourSpace_,
      videoAttributeBitdetph_,
      sourceAttributeColourSpace_,
      sourceAttributeBitdetph_,
      params.attributeParameters[attrIdx].textureVideoUpsampleFilter,
      params.attributeParameters[attrIdx].textureVideoFullRange);

    //NOTE: texture needs to be Frame<uint8_t> for checksum, the pixel values of rec.frame are already in 8bits.
    reconstruct.attributeFrame(attrIdx, frameIdx) = rec.frame(frameIdx);
    reconstruct.attributeFrame(attrIdx, frameIdx)
      .setColorSpace(rec.frame(frameIdx).colourSpace());
    encChecksum_.add(reconstruct.attributeFrame(attrIdx, frameIdx), attrIdx);
    if (!reconstructedAttributesPaths_[attrIdx].empty()) {
      char fnameEncTexture[1024];
      snprintf(fnameEncTexture,
               1024,
               reconstructedAttributesPaths_[attrIdx].c_str(),
               frameIdx + frameOffset + startFrame_);
      reconstruct.attributeFrame(attrIdx, frameIdx).save(fnameEncTexture);
    }
  }  //frame

  rec.clear();
  return true;
}
