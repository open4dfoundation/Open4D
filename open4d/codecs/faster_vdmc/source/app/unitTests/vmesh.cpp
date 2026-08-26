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

#include <gtest/gtest.h>
#include "common.hpp"

TEST(vmesh, all) {
  disableSubProcessLog.disable();
  // Set parameters
  const std::string meshPath    = "data/levi_fr0000_qp12_qt13.obj";
  const std::string texPath     = "data/levi_fr0000.png";
  const std::string cfgPath     = "data/encoder.cfg";
  const std::string binPath     = "bin.bin";
  const std::string recObjPath  = "rec.ply";
  const std::string recPngPath  = "rec.png";
  const std::string decObjPath  = "dec.ply";
  const std::string decPngPath  = "rec.png";
  const std::string checsumPath = "bin.checksum";

  if (!checkSoftwarePath()) {
    FAIL() << "All software paths not exist: ";
    return;
  }
  if (!exists(meshPath)) {
    printf("Input path not exists (%s) \n", meshPath.c_str());
    FAIL() << "Input path not exists: " << meshPath;
    return;
  }
  if (!exists(texPath)) {
    printf("Input path not exists (%s) \n", texPath.c_str());
    FAIL() << "Input path not exists: " << texPath;
    return;
  }
  if (!exists(cfgPath)) {
    printf("Input path not exists (%s) \n", cfgPath.c_str());
    FAIL() << "Input path not exists: " << cfgPath;
    return;
  }

  // Encode
  std::stringstream cmd;
  cmd << g_vmeshEncodePath << "  "
      << "  -c " << cfgPath            // Configuration file name
      << "  --srcMesh=" << meshPath    // Input mesh
      << "  --srcTex=" << texPath      // Input texture
      << "  --compressed=" << binPath  // Compressed bitstream
      << "  --recMesh=" << recObjPath  // Reconstructed mesh
      << "  --recTex=" << recPngPath;  // Reconstructed texture

  if (disableSubProcessLog.disableLog()) { cmd << "  2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Decode
  cmd.str("");
  cmd << g_vmeshDecodePath << "  "
      << "  --compressed=" << binPath  // Compressed bitstream
      << "  --decMesh=" << decObjPath  // Reconstructed mesh
      << "  --decTex=" << decPngPath;  // Reconstructed texture

  if (disableSubProcessLog.disableLog()) { cmd << "  2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Compare
  auto hashRecObj = hash(recObjPath);
  auto hashDecObj = hash(decObjPath);
  auto hashRecPng = hash(recPngPath);
  auto hashDecPng = hash(decPngPath);
  std::cout << "hashRecObj = " << std::hex << hashRecObj << "\n";
  std::cout << "hashDecObj = " << std::hex << hashDecObj << "\n";
  std::cout << "hashRecPng = " << std::hex << hashRecPng << "\n";
  std::cout << "hashDecPng = " << std::hex << hashDecPng << "\n";
  disableSubProcessLog.enable();

  ASSERT_EQ(hashRecObj, hashDecObj);
  ASSERT_EQ(hashRecPng, hashDecPng);

  // Remove tmp files
  remove(binPath.c_str());
  remove(recObjPath.c_str());
  remove(decObjPath.c_str());
  remove(recPngPath.c_str());
  remove(decPngPath.c_str());
  remove(checsumPath.c_str());
  remove("mat.mtl");
}
