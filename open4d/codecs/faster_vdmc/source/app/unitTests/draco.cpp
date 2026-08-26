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

#include "geometryEncoder.hpp"
#include "geometryDecoder.hpp"

#include <gtest/gtest.h>
#include "common.hpp"

TEST(draco, encode) {
  disableSubProcessLog.disable();
  // Set parameters
  vmesh::GeometryCodecId           codecId = vmesh::GeometryCodecId::DRACO;
  vmesh::GeometryEncoderParameters encoderParams;
  vmesh::GeometryDecoderParameters decoderParams;
  std::string                      inputMesh = "data/gof_0_fr_0_qbase.obj";
  std::string                      recOrg    = "rec_org.obj";
  std::string                      binOrg    = "bin_org.drc";
  std::string                      recNew    = "rec_new.obj";
  std::string                      binNew    = "bin_new.drc";
  encoderParams.qp_                          = 11;
  encoderParams.qt_                          = 10;
  encoderParams.qn_                          = -1;
  encoderParams.qg_                          = -1;
  encoderParams.cl_                          = 10;
  encoderParams.dracoUsePosition_            = false;
  encoderParams.dracoUseUV_                  = false;
  decoderParams.dracoUsePosition_            = false;
  decoderParams.dracoUseUV_                  = false;

  if (!checkSoftwarePath()) {
    FAIL() << "All software paths not exist: ";
    return;
  }
  if (!exists(inputMesh)) {
    printf("Input path not exists (%s) \n", inputMesh.c_str());
    FAIL() << "Input path not exists: " << inputMesh;
    return;
  }

  // Encode with GeometryEncoder
  vmesh::TriangleMesh<double> src;
  vmesh::TriangleMesh<double> rec;
  vmesh::TriangleMesh<double> dec;
  vmesh::Bitstream            bitstream;
  src.load(inputMesh);
  auto encoder = vmesh::GeometryEncoder<double>::create(codecId);
  encoder->encode(src, encoderParams, bitstream.vector(), rec);
  bitstream.save(binNew);

  // Decode with GeometryDecoder
  auto decoder = vmesh::GeometryDecoder<double>::create(codecId);
  decoder->decode(bitstream.vector(), decoderParams, dec);

  // Encode with original draco application
  std::stringstream cmd;
  cmd << g_dracoEncoderPath << "  "
      << " -i " << inputMesh << " -o " << binOrg << " -qp "
      << encoderParams.qp_ << " -qt " << encoderParams.qt_ << " -qn "
      << encoderParams.qn_ << " -qg " << encoderParams.qg_ << " -cl "
      << encoderParams.cl_;
  if (disableSubProcessLog.disableLog()) { cmd << " > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Decode with original draco application
  cmd.str("");
  cmd << g_dracoDecoderPath << "  "
      << " -i " << binOrg << " -o " << recOrg;
  if (disableSubProcessLog.disableLog()) { cmd << " 2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  // Compare bitstreams
  auto hashBinLibs = hash(binOrg);
  auto hashBinSoft = hash(binNew);
  std::cout << "hashBinLibs = " << std::hex << hashBinLibs << "\n";
  std::cout << "hashBinSoft = " << std::hex << hashBinSoft << "\n";
  disableSubProcessLog.enable();

  ASSERT_EQ(hashBinLibs, hashBinSoft);

  // Remove tmp files
  remove(binOrg.c_str());
  remove(binNew.c_str());
  remove(recOrg.c_str());
  remove(recNew.c_str());
}

TEST(draco, decode) {
  disableSubProcessLog.disable();
  // Set parameters
  vmesh::GeometryDecoderParameters decoderParams;
  vmesh::GeometryCodecId           codecId    = vmesh::GeometryCodecId::DRACO;
  std::string                      binPath    = "data/gof_0_fr_0_cbase.drc";
  std::string                      decAppPath = "dec_app.obj";
  std::string                      decLibPath = "dec_lib.obj";
  decoderParams.dracoUsePosition_             = false;
  decoderParams.dracoUseUV_                   = false;

  if (!checkSoftwarePath()) {
    FAIL() << "All software paths not exist: ";
    return;
  }
  if (!exists(binPath)) {
    printf("Input path not exists (%s) \n", binPath.c_str());
    FAIL() << "Input path not exists: " << binPath;
    return;
  }

  // Decode with original draco application
  std::stringstream cmd;
  cmd << g_dracoDecoderPath << "  "
      << " -i " << binPath << " -o " << decAppPath;
  if (disableSubProcessLog.disableLog()) { cmd << " 2>&1 > /dev/null"; }
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());
  vmesh::TriangleMesh<double> decApp;
  decApp.load(decAppPath);
  decApp.save(decAppPath);

  // Decode with GeometryEncoder
  vmesh::Bitstream            bitstream;
  vmesh::TriangleMesh<double> decLib;
  bitstream.load(binPath);
  auto decoder = vmesh::GeometryDecoder<double>::create(codecId);
  decoder->decode(bitstream.vector(), decoderParams, decLib);
  decLib.save(decLibPath);

  // Compare bitstreams
  auto hashDecApp = hash(decAppPath);
  auto hashDecLib = hash(decLibPath);
  std::cout << "hashDecApp = " << std::hex << hashDecApp << "\n";
  std::cout << "hashDecLib = " << std::hex << hashDecLib << "\n";
  disableSubProcessLog.enable();
  ASSERT_EQ(hashDecApp, hashDecLib);

  // Remove tmp files
  remove(decAppPath.c_str());
  remove(decLibPath.c_str());
}
