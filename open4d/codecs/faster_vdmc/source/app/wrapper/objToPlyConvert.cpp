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
#include <program-options-lite/program_options_lite.h>

#include "mesh.hpp"
#include "image.hpp"
#include "util/misc.hpp"
#include "checksum.hpp"
#include "util/verbose.hpp"
#include "version.hpp"

//============================================================================

struct Parameters {
  bool        verbose        = true;
  std::string srcMeshPath    = {};
  std::string srcTexturePath = {};
  std::string dstMeshPath    = {};
  std::string dstTexturePath = {};
  uint32_t    qt             = 0;
  bool        binary         = true;
};

//============================================================================

static bool
parseParameters(int argc, char* argv[], Parameters& params) try {
  namespace po    = df::program_options_lite;
  bool print_help = false;

  /* clang-format off */
  po::Options opts;
  opts.addOptions()
  ("help", print_help, false, "This help text")
  ("config,c", po::parseConfigFile, "Configuration file name")
  ("verbose,v", params.verbose, true, "Verbose output")

  (po::Section("Input/Output"))
    ("srcMesh",
      params.srcMeshPath,
      params.srcMeshPath,
      "Source mesh")
    ("srcTex",
      params.srcTexturePath,
      params.srcTexturePath,
      "Source texture")
    ("dstMesh",
      params.dstMeshPath,
      params.dstMeshPath,
      "Output mesh")
    ("dstTex",
      params.dstTexturePath,
      params.dstTexturePath,
      "Output texture")
    ("qt",
      params.qt,
      params.qt,
      "qt")
    ("binary",
      params.binary,
      params.binary,
      "Binary PLY format")
    
  ;
  /* clang-format on */

  po::setDefaults(opts);
  po::ErrorReporter             err;
  const std::list<const char*>& argv_unhandled =
    po::scanArgv(opts, argc, (const char**)argv, err);

  for (const auto* const arg : argv_unhandled) {
    err.warn() << "Unhandled argument ignored: " << arg << '\n';
  }

  if (argc == 1 || print_help) {
    std::cout << "usage: " << argv[0] << " [arguments...] \n\n";
    po::doHelp(std::cout, opts, 78);
    return false;
  }

  if (params.srcMeshPath.empty()) err.error() << "Src mesh not specified\n";
  if (params.srcTexturePath.empty())
    err.error() << "Src texture not specified\n";
  if (params.dstMeshPath.empty()) err.error() << "Output mesh not specified\n";
  if (params.dstTexturePath.empty())
    err.error() << "Output texture not specified\n";

  if (err.is_errored) { return false; }

  // Dump the complete derived configuration
  po::dumpCfgBySection(std::cout, opts);
  return true;
} catch (df::program_options_lite::ParseFailure& e) {
  std::cerr << "Error parsing option \"" << e.arg << "\" with argument \""
            << e.val << "\".\n";
  return false;
}

//============================================================================

int
main(int argc, char* argv[]) {
  std::cout << "MPEG VMESH version " << ::vmesh::version << '\n';

  // this is mandatory to print floats with full precision
  std::cout.precision(std::numeric_limits<float>::max_digits10);

  Parameters params;
  if (!parseParameters(argc, argv, params)) { return 1; }

  if (params.verbose) { vmesh::vout.rdbuf(std::cout.rdbuf()); }

  // Load source
  vmesh::TriangleMesh<double> srcMesh, recMesh;
  vmesh::Frame<uint8_t>       srcTex;

  // Load OBJ
  if (!srcMesh.load(params.srcMeshPath)) {
    printf("Error loading src mesh: %s \n", params.srcMeshPath.c_str());
    return -1;
  }
  // Load PNG
  if (!srcTex.load(params.srcTexturePath)) {
    printf("Error loading src texture: %s \n", params.srcTexturePath.c_str());
    return -1;
  }

  // Save PNG
  srcMesh.materialLibrary() = params.dstTexturePath;
  printf("Save PNG: %s \n", srcMesh.materialLibrary().c_str());
  if (!srcTex.save(srcMesh.materialLibrary())) { return -1; }

  // Save PLY
  printf("Save PLY: %s \n", params.dstMeshPath.c_str());
  srcMesh.save(params.dstMeshPath, params.binary);

  // Load PLY
  printf("Load PLY: %s \n", params.dstMeshPath.c_str());
  recMesh.load(params.dstMeshPath);

  // Crosscheck: reload ply and compute checksum
  vmesh::Checksum checksum;
  printf("Compute checksum:\n");
  checksum.print(srcMesh, "srcMesh");
  checksum.print(recMesh, "recMesh");

  // Resave obj for comparison
  std::string str = vmesh::removeExtension(params.dstMeshPath) + "_resave.obj";
  printf("Save OBJ: %s \n", str.c_str());
  recMesh.save(str, 1);

  return 0;
}
