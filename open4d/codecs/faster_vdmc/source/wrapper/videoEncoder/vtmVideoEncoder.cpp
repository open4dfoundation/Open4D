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
#if defined(USE_VTM_VIDEO_CODEC)
#  include "program_options_lite.h"
#  include "util/image.hpp"
#  include "vtmVideoEncoder.hpp"
#  include "vtmVideoEncoderImpl.hpp"
#  include "EncoderLib/EncLibCommon.h"
#  include <string>

namespace vmesh {

template<typename T>
vtmVideoEncoder<T>::vtmVideoEncoder() {}
template<typename T>
vtmVideoEncoder<T>::~vtmVideoEncoder() {}

template<typename T>
void
vtmVideoEncoder<T>::encode(FrameSequence<T>&       videoSrc,
                           VideoEncoderParameters& params,
                           std::vector<uint8_t>&   bitstream,
                           FrameSequence<T>&       videoRec) {
  const size_t width      = videoSrc.width();
  const size_t height     = videoSrc.height();
  const size_t frameCount = videoSrc.frameCount();

  std::stringstream cmd;
  cmd << "VTMEncoder";
  cmd << " -c " << params.encoderConfig_;
  if (params.multiLayerCoding_) cmd << " -c " << params.layerConfig_;
  // cmd << " --InputFile=" << params.srcYuvFileName_;
  cmd << " --InputBitDepth=" << params.inputBitDepth_;
  cmd << " --OutputBitDepth=" << params.inputBitDepth_;
  cmd << " --OutputBitDepthC=" << params.inputBitDepth_;
  cmd << " --FrameRate=30";
  cmd << " --FrameSkip=0";
  cmd << " --SourceWidth=" << width;
  cmd << " --SourceHeight=" << height;
  cmd << " --ConformanceWindowMode=1 ";
  cmd << " --FramesToBeEncoded=" << frameCount;
  // cmd << " --BitstreamFile=" << params.binFileName_;
  // cmd << " --ReconFile=" << params.recYuvFileName_;
  cmd << " --QP=" << params.qp_;
  if (params.internalBitDepth_ != 0) {
    cmd << " --InternalBitDepth=" << params.internalBitDepth_;
  }
  if (params.usePccMotionEstimation_) {
    cmd << " --UsePccMotionEstimation=1"
        << " --BlockToPatchFile=" << params.blockToPatchFile_
        << " --OccupancyMapFile=" << params.occupancyMapFile_
        << " --PatchInfoFile=" << params.patchInfoFile_;
  }
  if (videoSrc.colourSpace() == ColourSpace::YUV444p
      || videoSrc.colourSpace() == ColourSpace::RGB444p
      || videoSrc.colourSpace() == ColourSpace::BGR444p
      || videoSrc.colourSpace() == ColourSpace::GBR444p) {
    cmd << " --InputChromaFormat=444";
  } else if (videoSrc.colourSpace() == ColourSpace::YUV400p) {
    cmd << " --InputChromaFormat=400";
  } else {
    cmd << " --InputChromaFormat=420";
  }

  if (params.multiLayerCoding_) {
    const int numLayer = params.maxLayers_;
    params.srcYuvs_.push_back(params.srcYuvFileName_);
    params.recYuvs_.push_back(params.recYuvFileName_);
    for (int i = 1; i < numLayer; i++) {
      string temp;
      temp = params.srcYuvs_[0];
      temp.replace(temp.find(std::to_string(width)),
                   (std::to_string(width)).length(),
                   (std::to_string(width >> i)));
      temp.replace(temp.find(std::to_string(height)),
                   (std::to_string(height)).length(),
                   (std::to_string(height >> i)));
      params.srcYuvs_.push_back(temp);

      temp = params.recYuvs_[0];
      temp.replace(temp.find(std::to_string(width)),
                   (std::to_string(width)).length(),
                   (std::to_string(width >> i)));
      temp.replace(temp.find(std::to_string(height)),
                   (std::to_string(height)).length(),
                   (std::to_string(height >> i)));
      params.recYuvs_.push_back(temp);
    }
    for (int i = 0; i < numLayer; i++) {
      cmd << " -l" << std::to_string(i) << " -i "
          << params.srcYuvs_[numLayer - i - 1] << " -o "
          << params.recYuvs_[numLayer - i - 1] << " -wdt "
          << std::to_string((width >> (numLayer - i - 1))) << " -hgt "
          << std::to_string((height >> (numLayer - i - 1))) << " -q "
          << std::to_string(params.qp_);
    }
  }

  std::cout << cmd.str() << std::endl;

  std::string arguments = cmd.str();

  fprintf(stdout, "\n");
  fprintf(stdout, "VVCSoftware: VTM Encoder Version %s ", VTM_VERSION);
  fprintf(stdout, NVM_ONOS);
  fprintf(stdout, NVM_COMPILEDBY);
  fprintf(stdout, NVM_BITS);
  fprintf(stdout, "\n");

  std::ostringstream oss(ostringstream::binary | ostringstream::out);
  std::ostream&      bitstreamFile = oss;
  EncLibCommon       encLibCommon;

  initROM();
  videoRec.clear();
  std::vector<vtmVideoEncoderImpl<T>*> pcEncApp(1);
  bool                                 resized  = false;
  int                                  layerIdx = 0;
  int                                  maxLayer = 0;

  do {
    pcEncApp[layerIdx] =
      new vtmVideoEncoderImpl<T>(bitstreamFile, &encLibCommon);

    // create application encoder class per layer
    pcEncApp[layerIdx]->create();

    // parse configuration per layer
    bool               layerCheck = true;
    std::istringstream iss(arguments);
    std::string        token;
    std::vector<char*> args;
    std::vector<char*> layerArgs;
    try {
      while (iss >> token) {
        char* arg = new char[token.size() + 1];
        copy(token.begin(), token.end(), arg);

        // -l parsing
        if (arg[0] == '-' && arg[1] == 'l'
            && (arg[2] == std::to_string(layerIdx).c_str()[0]))
          layerCheck = true;

        else if (arg[0] == '-' && arg[1] == 'l'
                 && (arg[2] != std::to_string(layerIdx).c_str()[0]))
          layerCheck = false;

        if (layerCheck == true) {
          if (arg[0] == '-' && arg[1] == 'l') continue;
          arg[token.size()] = '\0';
          layerArgs.push_back(arg);
        }
      }
      // parse configuration
      if (!pcEncApp[layerIdx]->parseCfg(layerArgs.size(), &layerArgs[0])) {
        pcEncApp[layerIdx]->destroy();
        return;
      }

    } catch (df::program_options_lite::ParseFailure& e) {
      std::cerr << "Error parsing option \"" << e.arg << "\" with argument \""
                << e.val << "\"." << std::endl;
      return;
    }
    pcEncApp[layerIdx]->createLib(layerIdx);
    for (size_t i = 0; i < args.size(); i++) { delete[] args[i]; }
    for (size_t i = 0; i < layerArgs.size(); i++) { delete[] layerArgs[i]; }

    if (!resized) {
      pcEncApp.resize(pcEncApp[layerIdx]->getMaxLayers());
      resized = true;
    }
    maxLayer = pcEncApp[layerIdx]->getMaxLayers();
    layerIdx++;
  } while (layerIdx < pcEncApp.size());

  if (layerIdx > 1) {
    int                                      nbLayersUsingAlf = 0;
    int                                      totalUsedAPSIDs  = 0;
    std::array<uint8_t, ALF_CTB_MAX_NUM_APS> usedAlfAps;
    usedAlfAps.fill(0);
    bool overlapAPS = false;
    for (uint32_t i = 0; i < layerIdx; i++) {
      if (pcEncApp[i]->getALFEnabled()) {
        nbLayersUsingAlf++;
        totalUsedAPSIDs += pcEncApp[i]->getMaxNumALFAPS();
        for (int apsid = 0; apsid < pcEncApp[i]->getMaxNumALFAPS(); apsid++) {
          usedAlfAps[apsid + pcEncApp[i]->getALFAPSIDShift()]++;
          if (usedAlfAps[apsid + pcEncApp[i]->getALFAPSIDShift()] > 1) {
            overlapAPS = true;
          }
        }
      }
    }
    if (totalUsedAPSIDs > ALF_CTB_MAX_NUM_APS || overlapAPS) {
      msg(VTM_WARNING,
          "Number of configured ALF APS Ids exceeds maximum for multilayer, "
          "or overlap APS Ids - reconfiguring with automatic settings\n");
      int apsShift = 0;
      for (uint32_t i = 0; i < layerIdx; i++) {
        if (pcEncApp[i]->getALFEnabled()) {
          int nbAPS = pcEncApp[i]->getMaxNumALFAPS();
          if (totalUsedAPSIDs > ALF_CTB_MAX_NUM_APS) {
            nbAPS = std::min(
              nbAPS, std::max(1, ALF_CTB_MAX_NUM_APS / nbLayersUsingAlf));
            nbAPS = std::min(nbAPS, ALF_CTB_MAX_NUM_APS - apsShift);
          }
          msg(VTM_WARNING,
              "\tlayer %d : %d: %d -> %d \n",
              i,
              nbAPS,
              apsShift,
              apsShift + nbAPS - 1);
          pcEncApp[i]->forceMaxNumALFAPS(nbAPS);
          pcEncApp[i]->forceALFAPSIDShift(apsShift);
          apsShift += nbAPS;
        }
      }
    }

    VPS* vps = pcEncApp[0]->getVPS();
    //check chroma format and bit-depth for dependent layers
    for (uint32_t i = 0; i < layerIdx; i++) {
      const ChromaFormat curLayerChromaFormatIdc =
        pcEncApp[i]->getChromaFormatIDC();
      int curLayerBitDepth = pcEncApp[i]->getBitDepth();
      for (uint32_t j = 0; j < layerIdx; j++) {
        if (vps->getDirectRefLayerFlag(i, j)) {
          const ChromaFormat refLayerChromaFormatIdcInVPS =
            pcEncApp[j]->getChromaFormatIDC();
          CHECK(curLayerChromaFormatIdc != refLayerChromaFormatIdcInVPS,
                "The chroma formats of the current layer and the reference "
                "layer are different");
          int refLayerBitDepthInVPS = pcEncApp[j]->getBitDepth();
          CHECK(curLayerBitDepth != refLayerBitDepthInVPS,
                "The bit-depth of the current layer and the reference layer "
                "are different");
        }
      }
    }
  }

  // call encoding function per layer
  bool eos = false;

  while (!eos) {
    // read GOP
    bool             keepLoop = true;
    FrameSequence<T> videoSrcLoD;
    while (keepLoop) {
      for (int layerIdx = 0; layerIdx < maxLayer; layerIdx++) {
#  ifndef _DEBUG
        try {
#  endif
          if (params.multiLayerCoding_) {
            videoSrcLoD.resize((width >> (maxLayer - layerIdx - 1)),
                               (height >> (maxLayer - layerIdx - 1)),
                               ColourSpace::YUV420p,
                               frameCount);
            // load downsampling image
            videoSrcLoD.load(params.srcYuvs_[maxLayer - layerIdx - 1]);
            keepLoop = pcEncApp[layerIdx]->encodePrep(
              eos, videoSrcLoD, arguments, videoRec);
          } else {
            keepLoop = pcEncApp[layerIdx]->encodePrep(
              eos, videoSrc, arguments, videoRec);
          }

#  ifndef _DEBUG
        } catch (Exception& e) {
          std::cerr << e.what() << std::endl;
          return;
        } catch (const std::bad_alloc& e) {
          std::cout << "Memory allocation failed: " << e.what() << std::endl;
          return;
        }
#  endif
      }
    }

    // encode GOP
    keepLoop = true;
    while (keepLoop) {
      for (int layerIdx = 0; layerIdx < maxLayer; layerIdx++) {
#  ifndef _DEBUG
        try {
#  endif

          if (params.multiLayerCoding_) {
            videoSrcLoD.resize((width >> (maxLayer - layerIdx - 1)),
                               (height >> (maxLayer - layerIdx - 1)),
                               ColourSpace::YUV420p,
                               frameCount);
            // load downsampling image
            videoSrcLoD.load(params.srcYuvs_[maxLayer - layerIdx - 1]);
            keepLoop = pcEncApp[layerIdx]->encode(
              videoSrcLoD, arguments, bitstream, videoRec);
          } else {
            keepLoop = pcEncApp[layerIdx]->encode(
              videoSrc, arguments, bitstream, videoRec);
          }

#  ifndef _DEBUG
        } catch (Exception& e) {
          std::cerr << e.what() << std::endl;
          return;
        } catch (const std::bad_alloc& e) {
          std::cout << "Memory allocation failed: " << e.what() << std::endl;
          return;
        }
#  endif
      }
    }
  }
  // bitstream copy
  auto buffer = oss.str();
  bitstream.resize(buffer.size());
  std::copy(buffer.data(), buffer.data() + buffer.size(), bitstream.data());

  for (auto& encApp : pcEncApp) {
    encApp->destroyLib();
    // destroy application encoder class per layer
    encApp->destroy();
    delete encApp;
  }
  // destroy ROM
  destroyROM();
  pcEncApp.clear();
}

template class vtmVideoEncoder<uint8_t>;
template class vtmVideoEncoder<uint16_t>;

}  // namespace vmesh

#endif
