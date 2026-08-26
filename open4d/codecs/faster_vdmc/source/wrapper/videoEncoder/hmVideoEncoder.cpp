/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
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
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
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
#if defined(USE_HM_VIDEO_CODEC)
#  include "util/image.hpp"
#  include "hmVideoEncoder.hpp"
#  include "hmVideoEncoderImpl.hpp"

namespace vmesh {

template<typename T>
hmVideoEncoder<T>::hmVideoEncoder() = default;
template<typename T>
hmVideoEncoder<T>::~hmVideoEncoder() = default;

template<typename T>
void
hmVideoEncoder<T>::encode(FrameSequence<T>&       videoSrc,
                          VideoEncoderParameters& params,
                          std::vector<uint8_t>&   bitstream,
                          FrameSequence<T>&       videoRec) {
  const size_t      width      = videoSrc.width();
  const size_t      height     = videoSrc.height();
  const size_t      frameCount = videoSrc.frameCount();
  std::stringstream cmd;
  std::stringstream cmdTex;
  std::stringstream cmdGeo;

  if (params.jointTextDisp_) {
    cmdTex << "HMEncoder";
    cmdTex << " -c " << params.encoderConfigTex_;
    cmdGeo << "HMEncoder";
    cmdGeo << " -c " << params.encoderConfigGeo_;
  } else {
    cmd << "HMEncoder";
    cmd << " -c " << params.encoderConfig_;
  }

  if (params.profile_ != "main10") cmd << " --Profile=" << params.profile_;
  cmd << " --FrameRate=30";
  cmd << " --FrameSkip=0";
  cmd << " --SourceWidth=" << width;
  cmd << " --SourceHeight=" << height;
  // cmd << " --ConformanceWindowMode=1 ";
  cmd << " --FramesToBeEncoded=" << frameCount;
  if (params.allIntra_) {
    cmd << " --IntraPeriod=1";
    cmd << " --DecodingRefreshType=0";
  }
  // cmd << " --InputFile=" << params.srcYuvFileName_;
  // cmd << " --BitstreamFile=" << params.binFileName_;
  // cmd << " --ReconFile=" << params.recYuvFileName_;
  if (videoSrc.colourSpace() == ColourSpace::YUV444p
      || videoSrc.colourSpace() == ColourSpace::RGB444p
      || videoSrc.colourSpace() == ColourSpace::BGR444p
      || videoSrc.colourSpace() == ColourSpace::GBR444p) {
    cmd << " --InputChromaFormat=444";
  } else if (videoSrc.colourSpace() == ColourSpace::YUV400p) {
    cmd << " --InputChromaFormat=400";
  } else if (videoSrc.colourSpace() == ColourSpace::YUV422p) {
    cmd << " --InputChromaFormat=422";
  } else {
    cmd << " --InputChromaFormat=420";
  }

  if (videoSrc.colourSpace() == ColourSpace::RGB444p
      || videoSrc.colourSpace() == ColourSpace::BGR444p
      || videoSrc.colourSpace() == ColourSpace::GBR444p) {
    cmd << " --VuiParametersPresent";
    cmd << " --VideoSignalTypePresent";
    cmd << " --ColourDescriptionPresent";
    cmd << " --MatrixCoefficients=0";
    cmd << " --InputColourSpaceConvert=RGBtoGBR";
    cmd
      << " --SNRInternalColourSpace";  // do not bother to shuffle when lossless is targeted
  }

  if (params.qp_ < 0) {
    cmd << " --TransquantBypassEnable=0 --CUTransquantBypassFlagForce=0";
    cmd << " --SliceCbQpOffsetIntraOrPeriodic=0 "
           "--SliceCrQpOffsetIntraOrPeriodic=0";
    params.qp_ = -params.qp_;
  }
  cmd << " --QP=" << params.qp_;
  if (params.jointTextDisp_) {
    cmd << " --SliceMode=" << params.sliceMode_;
    cmd << " --SliceArgument=" << params.sliceArgument_;
    cmd << " --LFCrossSliceBoundaryFlag=" << 0;
    cmd << " --TransquantBypassEnable=" << 1;
  }
  if (params.EnableTMCTSFlag) {
    cmd << " --SliceMode=" << params.sliceMode_;
    cmd << " --SliceArgument=" << params.sliceArgument_;
    cmd << " --TileUniformSpacing=" << 0;
    cmd << " --NumTileColumnsMinus1=" << params.NumTileColumnsMinus1_;
    cmd << " --NumTileRowsMinus1=" << params.NumTileRowsMinus1_;
    if (params.NumTileColumnsMinus1_)
      cmd << " --TileColumnWidthArray=" << params.TileColumnWidthArray_;
    if (params.NumTileRowsMinus1_)
      cmd << " --TileRowHeightArray=" << params.TileRowHeightArray_;
    cmd << " --LFCrossSliceBoundaryFlag=" << params.LFCrossSliceBoundaryFlag_;
    cmd << " --SEITempMotionConstrainedTileSets="
        << params.SEITempMotionConstrainedTileSets_;
    cmd << " --SEITMCTSTileConstraint=" << params.SEITMCTSTileConstraint_;
    cmd << " --SEITMCTSExtractionInfo=1";
  }
  if (!params.EnableTMCTSFlag
      && (params.NumTileColumnsMinus1_ > 0
          || params.NumTileRowsMinus1_ > 0)) {
    cmd << " --TileUniformSpacing=1";
    cmd << " --NumTileColumnsMinus1=" << params.NumTileColumnsMinus1_;
    cmd << " --NumTileRowsMinus1=" << params.NumTileRowsMinus1_;
  }
  cmd << " --InputBitDepth=" << params.inputBitDepth_;
  cmd << " --OutputBitDepth=" << params.inputBitDepth_;
  cmd << " --OutputBitDepthC=" << params.inputBitDepth_;
  if (params.internalBitDepth_ != 0) {
    cmd << " --InternalBitDepth=" << params.internalBitDepth_;
    cmd << " --InternalBitDepthC=" << params.internalBitDepth_;
  }
#  if defined(PCC_ME_EXT) && PCC_ME_EXT
  if (params.usePccMotionEstimation_) {
    cmd << " --UsePccMotionEstimation=1";
    cmd << " --BlockToPatchFile=" << params.blockToPatchFile_;
    cmd << " --OccupancyMapFile=" << params.occupancyMapFile_;
    cmd << " --PatchInfoFile=" << params.patchInfoFile_;
  }
#  endif
#  if defined(PCC_RDO_EXT) && PCC_RDO_EXT
  if (params.usePccRDO_ && !params.inputColourSpaceConvert_) {
    cmd << " --UsePccRDO=1";
    cmd << " --OccupancyMapFile=" << params.occupancyMapFile_;
  }
  if (params.textureParameterizationType_ == 1 && params.packingType_ == 3) {
    cmd << " --MaxCUWidth=" << params.maxCUWidth_;
    cmd << " --MaxCUHeight=" << params.maxCUHeight_;
    cmd << " --MaxPartitionDepth=" << params.maxPartitionDepth_;
  }
#  endif
  std::cout << cmd.str() << std::endl;
  hmVideoEncoderImpl<T> encoder;
#  if defined(USE_JOINT_CODING)
  if (params.jointTextDisp_) {
    std::cout << "USE_JOINT_CODING" << std::endl;
    encoder.encode(videoSrc,
                   cmdTex.str() + cmd.str(),
                   cmdTex.str() + cmd.str(),
                   cmdGeo.str() + cmd.str(),
                   bitstream,
                   videoRec);
  } else encoder.encode(videoSrc, cmd.str(), bitstream, videoRec);
}
#  else
  std::cout << "no USE_JOINT_CODING" << std::endl;
  encoder.encode(videoSrc, cmd.str(), bitstream, videoRec);
}
#  endif

template class hmVideoEncoder<uint8_t>;
template class hmVideoEncoder<uint16_t>;

}  // namespace vmesh

#endif
