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
#pragma once

#include "util/image.hpp"

namespace vmesh {

struct VideoEncoderParameters {
  std::string encoderPath_      = {};
  std::string srcYuvFileName_   = {};
  std::string binFileName_      = {};
  std::string recYuvFileName_   = {};
  std::string encoderConfig_    = {};
  std::string profile_          = "main10";
  int32_t     qp_               = 30;
  int32_t     inputBitDepth_    = 8;
  int32_t     internalBitDepth_ = 8;
  bool        allIntra_         = false;
  bool        enableWavefront_  = true;
  int32_t     frameThreadCount_ = 0;
  int32_t     poolThreadCount_  = 0;
  int32_t     sliceCount_       = 1;
  std::string encoderPreset_    = "fast";
  //int32_t     outputBitDepth_              = 8;
  bool                     usePccMotionEstimation_      = false;
  std::string              blockToPatchFile_            = {};
  std::string              occupancyMapFile_            = {};
  std::string              patchInfoFile_               = {};
  bool                     transquantBypassEnable_      = false;
  bool                     cuTransquantBypassFlagForce_ = false;
  bool                     inputColourSpaceConvert_     = false;
  bool                     usePccRDO_                   = false;
  int32_t                  maxLayers_                   = 3;
  std::vector<std::string> srcYuvs_          = {};  // source YUV file name
  std::vector<std::string> recYuvs_          = {};  // recYUV file name
  std::string              layerConfig_      = {};
  bool                     multiLayerCoding_ = false;
  int                      textureParameterizationType_ = 0;
  int                      packingType_                 = 0;
  int32_t                  maxCUWidth_                  = 64;
  int32_t                  maxCUHeight_                 = 64;
  int32_t                  maxPartitionDepth_           = 4;
  int32_t                  sliceMode_                   = 0;
  int32_t                  sliceArgument_               = 1500;
  std::string              encoderConfigTex_            = {};
  std::string              encoderConfigGeo_            = {};
  bool                     jointTextDisp_               = false;
  // enabling TMCTS
  bool        EnableTMCTSFlag                   = false;
  bool        SEITempMotionConstrainedTileSets_ = false;
  bool        SEITMCTSTileConstraint_           = false;
  bool        SEIMctsExtractInfoSet_            = false;
  bool        LFCrossSliceBoundaryFlag_         = true;
  bool        TileUniformSpacing_               = true;
  int32_t     NumTileRowsMinus1_                = 0;
  int32_t     NumTileColumnsMinus1_             = 0;
  std::string TileColumnWidthArray_             = {};
  std::string TileRowHeightArray_               = {};

  // LoD-based displacement video bistream extraction
  std::vector<int> MCTSidx;    // MCTS index
  std::vector<int> subpicIdx;  // subpicture index

  std::vector<std::string> srcYuvFileNameLayer_;
  std::string              recYuvFileNameLayer_;
  int32_t                  numLayerMinus1_ = 2;  // default: 2
};

template<class T>
class VideoEncoder {
public:
  VideoEncoder()  = default;
  ~VideoEncoder() = default;

  static std::shared_ptr<VideoEncoder<T>> create(VideoEncoderId codecId);
  static VideoEncoderId                   getDefaultCodecId();
  static bool                             checkCodecId(VideoEncoderId codecId);

  virtual void encode(FrameSequence<T>&       videoSrc,
                      VideoEncoderParameters& params,
                      std::vector<uint8_t>&   bitstream,
                      FrameSequence<T>&       videoRec) = 0;
};

}  // namespace vmesh
