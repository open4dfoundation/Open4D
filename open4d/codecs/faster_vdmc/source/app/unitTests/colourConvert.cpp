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
#include <utility>

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "version.hpp"

#include "colourConverter.hpp"

#include <gtest/gtest.h>
#include "common.hpp"

void
hdrtoolsConvertion(const std::string        mode,
                   const std::string&       inputPath,
                   const int                width,
                   const int                height,
                   const int                frameCount,
                   const int                inputBitDepth,
                   const int                outputBitDepth,
                   const vmesh::ColourSpace inputColourSpace,
                   const vmesh::ColourSpace outputColourSpace,
                   const std::string&       hdrToolsConfigPath) {
  auto recLibsPath = createVideoName(
    "conv_libs_", width, height, outputBitDepth, outputColourSpace);
  auto recSoftPath = createVideoName(
    "conv_soft_", width, height, outputBitDepth, outputColourSpace);

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

  disableSubProcessLog.disable();

  // Load input mesh
  vmesh::FrameSequence<uint16_t> src(
    width, height, inputColourSpace, frameCount);
  if (inputBitDepth == 8) {
    vmesh::FrameSequence<uint8_t> src8(
      width, height, inputColourSpace, frameCount);
    src8.load(inputPath);
    src = src8;
  } else {
    src.load(inputPath);
  }

  // Convert with lib
  vmesh::FrameSequence<uint16_t> rec;
  auto convert = vmesh::ColourConverter<uint16_t>::create(0);
  convert->initialize(mode);
  convert->convert(src, rec);
  if (outputBitDepth == 8) {
    vmesh::FrameSequence<uint8_t> rec8(rec);
    rec8.save(recLibsPath);
  } else {
    rec.save(recLibsPath);
  }

  // Convert with application
  std::stringstream cmd;
  cmd << g_hdrConvertPath << " "
      << "  -f " << hdrToolsConfigPath << " "
      << "  -p SourceFile=" << inputPath << " "
      << "  -p SourceWidth=" << width << " "
      << "  -p SourceHeight=" << height << " "
      << "  -p NumberOfFrames=" << frameCount << " "
      << "  -p OutputFile=" << recSoftPath;
  if (disableSubProcessLog.disableLog()) { cmd << " 2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Compute hashes
  auto hashLibs = hash(recLibsPath);
  auto hashSoft = hash(recSoftPath);
  std::cout << "hashBinLibs = " << std::hex << hashLibs << "\n";
  std::cout << "hashBinSoft = " << std::hex << hashSoft << "\n";

  disableSubProcessLog.enable();

  // Compare hashes
  ASSERT_EQ(hashLibs, hashSoft);

  // Remove files
  remove(recLibsPath.c_str());
  remove(recSoftPath.c_str());

  // Remove hdrtools tmp files
  remove("log.txt");
  remove("test_1920x1080_24p_420.yuv");
}

TEST(colourConvert, hdrToolsUp) {
  hdrtoolsConvertion("YUV420p_BGR444p_10_8_0_0",
                     "data/tex_512x512_10bits_p420.yuv",
                     512,
                     512,
                     2,
                     10,
                     8,
                     vmesh::ColourSpace::YUV420p,
                     vmesh::ColourSpace::BGR444p,
                     "cfg/hdrconvert/yuv420tobgr444.cfg");
}

TEST(colourConvert, hdrToolsDown) {
  hdrtoolsConvertion("BGR444p_YUV420p_8_10_4_0",
                     "data/tex_512x512_8bits_p444.bgr",
                     512,
                     512,
                     2,
                     8,
                     10,
                     vmesh::ColourSpace::BGR444p,
                     vmesh::ColourSpace::YUV420p,
                     "cfg/hdrconvert/bgr444toyuv420.cfg");
}

// TEST(ColourConvert, UpInternal)
// {
//   test(
//     0, "data/tex_512x512_10bits_p420.yuv", 512, 512, 2,
//     vmesh::ColourSpace::YUV420p, vmesh::ColourSpace::BGR444p,
//     "YUV420ToBGR444_10_8_1", "cfg/hdrconvert/yuv420tobgr444.cfg");
// }
