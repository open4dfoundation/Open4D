/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "decodeVideoStream.hpp"

bool
VMCVideoDecoder::decompressDisplacementsVideo(
  const VideoBitstream& geometryVideoStream,
  bool                  keepVideoFiles) {
  printf("Decompress displacements video \n");
  fflush(stdout);
  auto  vpsId     = geometryVideoStream.getV3CParameterSetId();
  auto  atlasId   = geometryVideoStream.getAtlasId();
  auto& videoData = geometryVideoStream.vector();
  // Decode video
  auto decoder = VideoDecoder<uint16_t>::create(
    (VideoEncoderId)geometryVideoStream.getCoderIdc());
  geoemtryVideoBitDepth_.resize(2, 0);
  decoder->decode(videoData, dispVideo, geoemtryVideoBitDepth_);

  if (keepVideoFiles) {
    auto path = dispVideo.createName(_keepFilesPathPrefix + "disp_dec",
                                     geoemtryVideoBitDepth_[0]);
    save(removeExtension(path) + ".bin", videoData);
    dispVideo.save(path);
  }
  return true;
}

//----------------------------------------------------------------------------

bool
VMCVideoDecoder::decompressTextureVideo(const VideoBitstream& attVideoStream,
                                        int32_t               attrIdx,
                                        bool                  keepVideoFiles,
                                        const VMCDecoderParameters& params) {
  auto   vpsId          = attVideoStream.getV3CParameterSetId();
  auto   atlasId        = attVideoStream.getAtlasId();
  auto&  videoData      = attVideoStream.vector();
  size_t attributeIndex = attVideoStream.getAttributeIndex();
  attributeVideoBitDepth_.resize(2, 0);
  // Decode video
  printf("Decode video \n");
  fflush(stdout);
  FrameSequence<uint16_t>& dec     = attributeVideo16_;
  auto                     decoder = VideoDecoder<uint16_t>::create(
    (VideoEncoderId)attVideoStream.getCoderIdc());
#ifdef USE_SHMAPP_VIDEO_CODEC
  if ((VideoEncoderId)attVideoStream.getCoderIdc() == VideoEncoderId::SHMAPP) {
    std::string recPath = removeExtension(params.resultPath);
    std::string decoderPath;
    decoderPath = SHM_DEC_PATH;
    decoder->decode(videoData,
                    dec,
                    attributeVideoBitDepth_,
                    params.targetLayer,
                    decoderPath,
                    recPath);
    if (!keepVideoFiles) {
      auto reconfile =
        recPath + "_dec_" + std::to_string(params.targetLayer) + ".yuv";
      remove(reconfile.c_str());
    }
  } else
#endif
    decoder->decode(videoData, dec, attributeVideoBitDepth_);

  // Save intermediate files
  if (keepVideoFiles) {
    auto path = dec.createName(_keepFilesPathPrefix + "tex_dec",
                               (int)attributeVideoBitDepth_[0]);
    dec.save(path);
    save(removeExtension(path) + ".bin", videoData);
  }

  return true;
}
//----------------------------------------------------------------------------

bool
VMCVideoDecoder::decompressPackedVideo(const VideoBitstream& packedVideoStream,
                                       int32_t               attrIdx,
                                       bool                  keepVideoFiles) {
  auto   vpsId          = packedVideoStream.getV3CParameterSetId();
  auto   atlasId        = packedVideoStream.getAtlasId();
  auto&  videoData      = packedVideoStream.vector();
  size_t attributeIndex = packedVideoStream.getAttributeIndex();

  packedVideoBitDepth_.resize(2, 0);

  // Decode video
  printf("Decode video \n");
  fflush(stdout);

  auto decoder = VideoDecoder<uint16_t>::create(
    (VideoEncoderId)packedVideoStream.getCoderIdc());
  decoder->decode(videoData, dispPacked_, packedVideoBitDepth_);

  if (keepVideoFiles) {
    auto path = dispPacked_.createName(_keepFilesPathPrefix + "packed_dec",
                                     packedVideoBitDepth_[0]);
    save(removeExtension(path) + ".bin", videoData);
    dispPacked_.save(path);
  }
  return true;
}
//----------------------------------------------------------------------------
