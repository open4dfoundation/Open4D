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
#include <chrono>
#include <iostream>

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "version.hpp"
#include "../../lib/bitstreamsCommon/bitstream.hpp"

#include "videoEncoder.hpp"
#include "videoDecoder.hpp"

#include <gtest/gtest.h>
#include "common.hpp"

void
encodeDecodeHM(const std::string&       prefix,
               const std::string&       inputPath,
               const int                width,
               const int                height,
               const int                bitDepth,
               const vmesh::ColourSpace colorSpace,
               const int                frameCount,
               const std::string&       configPath) {
  auto binLibsPath = prefix + "_libs.h265";
  auto binSoftPath = prefix + "_soft.h265";
  auto recLibsPath =
    createVideoName(prefix + "_rec_libs", width, height, bitDepth, colorSpace);
  auto recSoftPath =
    createVideoName(prefix + "_rec_soft", width, height, bitDepth, colorSpace);
  auto decLibsPath =
    createVideoName(prefix + "_dec_libs", width, height, bitDepth, colorSpace);
  auto decSoftPath =
    createVideoName(prefix + "_dec_soft", width, height, bitDepth, colorSpace);

  // Check encoder and decoder path exist
  if (!checkSoftwarePath()) {
    FAIL() << "All software paths not exist: ";
    return;
  }
  if (!exists(inputPath)) {
    printf("Input path not exists (%s) \n", inputPath.c_str());
    FAIL() << "Input path not exists: " << inputPath;
    return;
  }
  if (!exists(configPath)) {
    printf("Config path not exists (%s) \n", configPath.c_str());
    FAIL() << "Config path not exists: " << configPath;
    return;
  }

  // Set parameters
  vmesh::VideoEncoderParameters params;
  vmesh::VideoCodecId           codecId = vmesh::VideoCodecId::HM;
  params.encoderConfig_                 = configPath;
  params.qp_                            = 38;
  params.inputBitDepth_                 = bitDepth;
  params.internalBitDepth_              = bitDepth;
  params.outputBitDepth_                = bitDepth;

  // Load input mesh
  vmesh::FrameSequence<uint16_t> src(width, height, colorSpace, frameCount);
  src.load(inputPath);
  if (src.frameCount() == 0) {
    printf("Src frame count = %d \n", src.frameCount());
    exit(-1);
  }

  disableSubProcessLog.disable();

  // Encode lib
  vmesh::FrameSequence<uint16_t> rec;
  vmesh::Bitstream               bitstream;
  auto encoder = vmesh::VideoEncoder<uint16_t>::create(codecId);
  encoder->encode(src, params, bitstream.vector(), rec);

  // Decode lib
  vmesh::FrameSequence<uint16_t> dec;
  auto decoder = vmesh::VideoDecoder<uint16_t>::create(codecId);
  decoder->decode(bitstream.vector(), dec, 10);

  // Save bitstream, reconstructed and decoded
  rec.save(recLibsPath);
  dec.save(decLibsPath);
  bitstream.save(binLibsPath);

  rec[0].log("rec");
  dec[0].log("dec");

  disableSubProcessLog.enable();

  printf("Start Soft part \n");
  disableSubProcessLog.disable();

  // Encode with application
  std::stringstream cmd;
  cmd << g_hmEncoderPath << " "
      << "  -c " << configPath << " "
      << "  --InputFile=" << inputPath << " "
      << "  --InputBitDepth=" << bitDepth << " "
      << "  --OutputBitDepth=" << bitDepth << " "
      << "  --OutputBitDepthC=" << bitDepth << " "
      << "  --InternalBitDepth=" << bitDepth << " "
      << "  --InternalBitDepthC=" << bitDepth << " "
      << "  --InputChromaFormat="
      << (colorSpace == vmesh::ColourSpace::YUV420p ? "420" : "444")
      << "  --FrameRate=30 "
      << "  --FrameSkip=0 "
      << "  --FramesToBeEncoded=" << frameCount << " "
      << "  --SourceWidth=" << width << " "
      << "  --SourceHeight=" << height << " "
      << "  --BitstreamFile=" << binSoftPath << " "
      << "  --ReconFile=" << recSoftPath << " "
      << "  --QP=38 ";
  if (disableSubProcessLog.disableLog()) { cmd << " 2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Decode with application
  cmd.str("");
  cmd << g_hmDecoderPath << " "
      << "  --BitstreamFile=" << binSoftPath << " "
      << "  --ReconFile=" << decSoftPath;
  if (disableSubProcessLog.disableLog()) { cmd << " 2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Compute hashes
  auto hashBinLibs = hash(binLibsPath);
  auto hashBinSoft = hash(binSoftPath);
  auto hashRecLibs = hash(recLibsPath);
  auto hashRecSoft = hash(recSoftPath);
  auto hashDecLibs = hash(decLibsPath);
  auto hashDecSoft = hash(decSoftPath);
  std::cout << "hashBinLibs = " << std::hex << hashBinLibs << "\n";
  std::cout << "hashBinSoft = " << std::hex << hashBinSoft << "\n";
  std::cout << "hashRecLibs = " << std::hex << hashRecLibs << "\n";
  std::cout << "hashRecSoft = " << std::hex << hashRecSoft << "\n";
  std::cout << "hashDecLibs = " << std::hex << hashDecLibs << "\n";
  std::cout << "hashDecSoft = " << std::hex << hashDecSoft << "\n";

  disableSubProcessLog.enable();

  // Compare hashes
  ASSERT_EQ(hashBinLibs, hashBinSoft)
    << "Libs and soft bitstreams are differentes";
  ASSERT_EQ(hashRecLibs, hashRecSoft)
    << "Libs and soft rec videos are differentes";
  ASSERT_EQ(hashDecLibs, hashDecSoft)
    << "Libs and soft dec videos are differentes";
  ASSERT_EQ(hashRecLibs, hashDecLibs)
    << "Libs rec and dec videos are differentes";
  ASSERT_EQ(hashRecSoft, hashDecSoft)
    << "Soft rec and dec videos are differentes";

  // Remove files
  remove(binLibsPath.c_str());
  remove(binSoftPath.c_str());
  remove(recLibsPath.c_str());
  remove(recSoftPath.c_str());
  remove(decLibsPath.c_str());
  remove(decSoftPath.c_str());
}

TEST(hm, disp) {
  encodeDecodeHM("disp",
                 "data/disp_256x160_10bits_p444.brg",
                 256,
                 160,
                 10,
                 vmesh::ColourSpace::BGR444p,
                 2,
                 "cfg/hm/displacements-ai.cfg");
}

TEST(hm, disp2) {
  encodeDecodeHM("disp",
                 "data/GOF_0_disp_enc_256x160_10bits_p444.yuv",
                 256,
                 160,
                 10,
                 vmesh::ColourSpace::BGR444p,
                 1,
                 "cfg/hm/displacements-ai.cfg");
}

TEST(hm, texture) {
  encodeDecodeHM("disp",
                 "data/tex_512x512_10bits_p420.yuv",
                 512,
                 512,
                 10,
                 vmesh::ColourSpace::YUV420p,
                 2,
                 "cfg/hm/texture-ai.cfg");
}
