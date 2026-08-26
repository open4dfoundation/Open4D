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

#include "metrics.hpp"
#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "version.hpp"

//============================================================================

struct Parameters {
  std::string                 srcMeshPath    = {};
  std::string                 srcTexturePath = {};
  std::string                 decMeshPath    = {};
  std::string                 decTexturePath = {};
  int32_t                     startFrame     = 1;
  int32_t                     frameCount     = 1;
  vmesh::VMCMetricsParameters metParams;
};

//============================================================================

static bool
parseParameters(int argc, char* argv[], Parameters& params) try {
  namespace po     = df::program_options_lite;
  bool  print_help = false;
  auto& metParams  = params.metParams;

  /* clang-format off */
  po::Options opts;
  opts.addOptions()
  (po::Section("Common"))
    ("help",      print_help,          false, "This help text")
    ("config,c",  po::parseConfigFile, "Configuration file name")
    ("verbose,v", metParams.verbose,   false, "Verbose output")

  (po::Section("Source"))
    ("srcMesh",
      params.srcMeshPath,
      params.srcMeshPath,
      "Source mesh")
    ("srcTex",
      params.srcTexturePath,
      params.srcTexturePath,
      "Source texture")
  
  (po::Section("Decoded"))
    ("decMesh", 
      params.decMeshPath,
      params.decMeshPath,
      "Reconstructed/decoded mesh")
    ("decTex",
      params.decTexturePath,
      params.decTexturePath,
      "Reconstructed/decoded texture")
        
  (po::Section("Sequence"))
    ("startFrameIndex",
      params.startFrame,
      params.startFrame,
      "First frame number")
    ("frameCount",
      params.frameCount,
      params.frameCount,
      "Number of frames")
    ("minPosition",
      metParams.minPosition,
      {0.0, 0.0, 0.0},
      "Min position")
    ("maxPosition",
      metParams.maxPosition,
      {0.0, 0.0, 0.0},
      "Max position")
     ("positionBitDepth",
      metParams.qp,
      metParams.qp,
      "Position bit depth")
    ("texCoordBitDepth",
      metParams.qt,
      metParams.qt,
      "Texture coordinate bit depth")
    ("dequantizeUV",
      metParams.dequantizeUV, 
      metParams.dequantizeUV,
      "Texture coordinates of the decoded meshes are quantized")
    
  (po::Section("PCC metric"))
    ("pcc",
      metParams.computePcc,
      metParams.computePcc,
      "Compute pcc metrics")
    ("gridSize",
      metParams.gridSize,
      metParams.gridSize,
      "Grid size")
    ("resolution",
      metParams.resolution,
      metParams.resolution,
      "Resolution")
      
  (po::Section("IBSM metric"))
    ("ibsm",
      metParams.computeIbsm,
      metParams.computeIbsm,
      "Compute ibsm metrics")

  (po::Section("PCQM metric"))
    ("pcqm",
      metParams.computePcqm,
      metParams.computePcqm,
      "Compute PCQM metrics")
    ("pcqmRadiusCurvature",
      metParams.pcqmRadiusCurvature,
      metParams.pcqmRadiusCurvature,
      "PCQM radius curvature")
    ("pcqmThresholdKnnSearch",
      metParams.pcqmThresholdKnnSearch,
      metParams.pcqmThresholdKnnSearch,
      "PCQM threshold Knn search")
    ("pcqmRadiusFactor",
      metParams.pcqmRadiusFactor,
      metParams.pcqmRadiusFactor,
      "PCQM radius factor")
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

  if (params.srcMeshPath.empty()) {
    err.error() << "Src mesh not specified\n";
  }
  if (params.srcTexturePath.empty()) {
    err.error() << "Src texture not specified\n";
  }
  if (params.decMeshPath.empty()) {
    err.error() << "Rec/dec mesh not specified\n";
  }
  if (params.decTexturePath.empty()) {
    err.error() << "Rec/dec texture not specified\n";
  }
  if (params.metParams.computePcc && params.metParams.resolution == 0) {
    err.error() << "PCC resolution must be set\n";
  }
  if (params.metParams.minPosition[0] == 0.0
      && params.metParams.minPosition[1] == 0.0
      && params.metParams.minPosition[2] == 0.0) {
    err.error() << "Min position must be set\n";
  }
  if (params.metParams.maxPosition[0] == 0.0
      && params.metParams.maxPosition[1] == 0.0
      && params.metParams.maxPosition[2] == 0.0) {
    err.error() << "Max position must be set\n";
  }
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

int32_t
metrics(const Parameters& params) {
  vmesh::VMCMetrics metrics;
  for (int i = 0; i < params.frameCount; ++i) {
    const auto f = params.startFrame + i;
#if 0
    vmesh::TriangleMesh<MeshType> srcMesh;
    vmesh::TriangleMesh<MeshType> recMesh;
    vmesh::Frame<uint8_t>         srcText;
    vmesh::Frame<uint8_t>         recText;
    if (!srcMesh.load(params.srcMeshPath, f)
        || !srcText.load(params.srcTexturePath, f)
        || !recMesh.load(params.decMeshPath, f)
        || !recText.load(params.decTexturePath, f)) {
      printf("Error loading frame %d \n", f);
      return -1;
    }
    printf("Compute metric frame %d / %d  \n", i, params.frameCount);
    metrics.compute(srcMesh, recMesh, srcText, recText, params.metParams);
#else
    std::vector<std::string> strTexturePathcArray;
    strTexturePathcArray.push_back(vmesh::expandNum(params.srcTexturePath, f));
    std::vector<std::string> decTexturePathcArray;
    decTexturePathcArray.push_back(vmesh::expandNum(params.decTexturePath, f));
    metrics.compute(vmesh::expandNum(params.srcMeshPath, f),
                    vmesh::expandNum(params.decMeshPath, f),
                    strTexturePathcArray,
                    decTexturePathcArray,
                    params.metParams);
#endif
  }
  metrics.display(params.metParams.verbose);
  std::cout << "\nAll frames have been processed. \n";
  return 0;
}

//============================================================================

int
main(int argc, char* argv[]) {
  std::cout << "MPEG VMESH version " << ::vmesh::version << '\n';

  // this is mandatory to print floats with full precision
  std::cout.precision(std::numeric_limits<float>::max_digits10);

  Parameters params;
  if (!parseParameters(argc, argv, params)) { return 1; }

  if (params.metParams.verbose) { vmesh::vout.rdbuf(std::cout.rdbuf()); }

  if (metrics(params) != 0) {
    std::cerr << "Error: can't compute metrics!\n";
    return 1;
  }

  return 0;
}
