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

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "version.hpp"

#include "colourConverter.hpp"

//============================================================================

struct Parameters {
  bool               verbose{};
  std::string        inputPath;
  std::string        outputPath;
  std::string        configPath;
  int                width{};
  int                height{};
  int                frameCount{};
  int                inputBitDepth{};
  int                outputBitDepth{};
  int                mode{};
  int                range  = 0;
  int                filter = 0;
  vmesh::ColourSpace inputColorSpace;
  vmesh::ColourSpace outputColorSpace;
};

//============================================================================

static bool
parseParameters(int argc, char* argv[], Parameters& params) try {
  namespace po = df::program_options_lite;

  bool print_help = false;

  /* clang-format off */
  po::Options opts;
  opts.addOptions()
  ("help", print_help, false, "This help text")
  ("config,c", po::parseConfigFile, "Configuration file name")
  ("verbose,v", params.verbose, true, "Verbose output")

  (po::Section("Input"))
  ("inputPath",        params.inputPath,     {}, "Input video path")
  ("width",            params.width,         0,  "Input video width")
  ("height",           params.height,        0,  "Input video height")
  ("frameCount",       params.frameCount,    0,  "Input video frame count")
  ("inputBitDepth",    params.inputBitDepth,    10, "Input bit depth")
  ("inputColorSpace",  params.inputColorSpace, vmesh::ColourSpace::UNKNOW, 
    "Input video color space: YUV420p, YUV444p, RGB444p, BGR444p ")
  
  (po::Section("Output"))
  ("outputPath",       params.outputPath,    {}, "Reconstructed video path")
  ("outputColorSpace", params.outputColorSpace, vmesh::ColourSpace::UNKNOW, 
    "Input video color space: YUV420p, YUV444p, RGB444p, BGR444p ")
  ("outputBitDepth",   params.outputBitDepth,   10, "Output bit depth ")  
    
  (po::Section("Convertion"))
  ("mode",             params.mode,       0, "method: 0:internal, 1:HdrTools")  
  ("HdrToolsCfgPath",  params.configPath, {}, "Configuration file")  
  ("filter",           params.filter,      0, "Downsample/upsample filter")
  ("range",            params.range,       0, "Range: 0: limited, 1: full")
  ;

  /* clang-format on */

  po::setDefaults(opts);
  po::ErrorReporter             err;
  const std::list<const char*>& argv_unhandled =
    po::scanArgv(opts, argc, (const char**)argv, err);

  for (const auto* const arg : argv_unhandled) {
    err.warn() << "Unhandled argument ignored: " << arg << '\n';
  }
  po::dumpCfgBySection(std::cout, opts);

  if (argc == 1 || print_help) {
    std::cout << "usage: " << argv[0] << " [arguments...] \n\n";
    po::doHelp(std::cout, opts, 78);
    return false;
  }

  if (params.inputPath.empty()) {
    err.error() << "Input video pathnot specified\n";
  }
  if (params.outputPath.empty()) {
    err.error() << "Output video path not specified\n";
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

int
main(int argc, char* argv[]) {
  std::cout << "MPEG VMESH version " << ::vmesh::version << '\n';

  Parameters params;
  if (!parseParameters(argc, argv, params)) { return 1; }

  if (params.verbose) { vmesh::vout.rdbuf(std::cout.rdbuf()); }

  // Load input mesh
  vmesh::FrameSequence<uint16_t> src(
    params.width, params.height, params.inputColorSpace, params.frameCount);
  vmesh::FrameSequence<uint16_t> dst;
  src.load(params.inputPath);
  if (src.frameCount() == 0) {
    printf("Src frame count = %d not correct \n", src.frameCount());
    exit(-1);
  }

  printf("Src frame count = %d \n", src.frameCount());

  // convert
  auto convert = vmesh::ColourConverter<uint16_t>::create(params.mode);
  if (params.mode == 0) {
    std::stringstream format;
    format << params.inputColorSpace << "_" << params.outputColorSpace << "_"
           << params.inputBitDepth << "_" << params.outputBitDepth << "_"
           << params.filter << "_" << params.range;
    convert->initialize(format.str());
  } else {
    convert->initialize(params.configPath);
  }
  convert->convert(src, dst);

  // Save reconstructed mesh
  dst.save(params.outputPath);

  return 0;
}
