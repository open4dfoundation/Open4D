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
#if defined(USE_VV_VIDEO_CODEC)
#  include "util/image.hpp"
#  include "vvVideoEncoder.hpp"
#  include "vvVideoEncoderImpl.hpp"

namespace vmesh {

template<typename T>
vvVideoEncoder<T>::vvVideoEncoder() {}
template<typename T>
vvVideoEncoder<T>::~vvVideoEncoder() {}

template<typename T>
void
vvVideoEncoder<T>::encode(FrameSequence<T>&       videoSrc,
                          VideoEncoderParameters& params,
                          std::vector<uint8_t>&   bitstream,
                          FrameSequence<T>&       videoRec) {
  std::stringstream cmd;
  cmd << "vvencFFapp";
  cmd << " -c " << params.encoderConfig_;
  cmd << " --InputFile=src.yuv";
  cmd << " --ReconFile=rec.yuv";
  cmd << " --BitstreamFile=bin.266";
  cmd << " --InputBitDepth=" << params.inputBitDepth_;
  cmd << " --OutputBitDepth=" << params.inputBitDepth_;
  cmd << " --FramesToBeEncoded=" << videoSrc.frameCount();
  cmd << " --fps=30";
  cmd << " --frameskip=0";
  cmd << " --bitrate=0";  // constant qp
  cmd << " --QP=" << params.qp_;
  if (params.internalBitDepth_ != 0) {
    cmd << " --InternalBitDepth=" << params.internalBitDepth_;
  }
  cmd << " --ConformanceWindowMode=1 ";

  bool useConvert = false;
  if (videoSrc.colourSpace() == ColourSpace::YUV444p
      || videoSrc.colourSpace() == ColourSpace::RGB444p
      || videoSrc.colourSpace() == ColourSpace::BGR444p
      || videoSrc.colourSpace() == ColourSpace::GBR444p) {
    cmd << " --InputChromaFormat=400";
    useConvert = true;
  } else if (videoSrc.colourSpace() == ColourSpace::YUV400p) {
    cmd << " --InputChromaFormat=400";
  } else {
    cmd << " --InputChromaFormat=420";
  }
  // Disable multithreading
  cmd << " --threads=1 ";
  cmd << " --MaxParallelFrames=1 ";
  cmd << " --TileParallelCtuEnc=0 ";
  // cmd << " --WppBitEqual=0 ";

  // if ( params.transquantBypassEnable_ != 0 ) { cmd << " --TransquantBypassEnable=1"; }
  // if ( params.cuTransquantBypassFlagForce_ != 0 ) { cmd << " --CUTransquantBypassFlagForce=1"; }
  // if ( params.inputColourSpaceConvert_ ) { cmd << " --InputColourSpaceConvert=RGBtoGBR"; }

  vvVideoEncoderImpl<T> encoder;
  if (useConvert) {
    FrameSequence<T> videoSrc400, videoRec400;
    videoSrc.convert444To400(videoSrc400);
    videoSrc.trace("Src444");
    videoSrc400.trace("Src400");
    cmd << " --Size=" << videoSrc400.width() << "x" << videoSrc400.height();
    std::cout << cmd.str() << std::endl;
    encoder.encode(videoSrc400, cmd.str(), bitstream, videoRec400);
    videoRec400.convert400To444(videoRec);
    videoRec400.trace("Rec400");
    videoRec.trace("Rec444");
  } else {
    cmd << " --Size=" << videoSrc.width() << "x" << videoSrc.height();
    std::cout << cmd.str() << std::endl;
    encoder.encode(videoSrc, cmd.str(), bitstream, videoRec);
  }
}

template class vvVideoEncoder<uint8_t>;
template class vvVideoEncoder<uint16_t>;

}  // namespace vmesh

#endif
