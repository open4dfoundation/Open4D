/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
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
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
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

// argument parsing
#include <cxxopts.hpp>

#define TINYPLY_IMPLEMENTATION
#include "tinyply.h"

// internal headers
#include "ebChrono.h"
#include "ebIO.h"
#include "ebModel.h"
#include "ebDecoder.h"

int
main(int argc, char* argv[]) {
  const char* name  = "ebDecode";
  const char* brief = "ISO/IEC 23090-29 Annex I test decoder application";

  std::string reindexOutputStr;

  std::string inputModelFilename;
  std::string outputModelFilename;

  // prepare a decoder wrapper to set config
  eb::EBDecoder eb;

  // command line parameters
  try {
    cxxopts::Options options(name, brief);
    options.add_options()("h,help", "Print usage")(
      "i,inputModel",
      "path to output encoded file (.eb)",
      cxxopts::value<std::string>())("o,outputModel",
                                     "path to output decoded model (obj or vmb file)",
                                     cxxopts::value<std::string>())(
      "reindexOutput",
      "Reindex output mesh so that vertex order fits a given traversal, in "
      "[default, eb, degree], where default uses a value in [eb, degree]"
      "from the bitstream",
      cxxopts::value<std::string>()->default_value("default"));

    auto result = options.parse(argc, argv);

    // Analyse the options
    if (result.count("help") || result.arguments().size() == 0) {
      std::cout << options.help() << std::endl;
      return -1;
    }

    if (result.count("inputModel"))
      inputModelFilename = result["inputModel"].as<std::string>();
    else {
      std::cerr << "Error: missing inputModel parameter" << std::endl;
      std::cout << options.help() << std::endl;
      return -1;
    }

    //
    if (result.count("outputModel"))
      outputModelFilename = result["outputModel"].as<std::string>();
    else {
      std::cerr << "Warning: missing outputModel parameter, compressed model "
                   "will not be saved"
                << std::endl;
      // std::cout << options.help() << std::endl;
      // return -1;
    }

    reindexOutputStr = result["reindexOutput"].as<std::string>();
    if (reindexOutputStr == "default")
        eb.cfg.reindexOutput = eb::EBConfig::Reindex::DEFAULT;
    else if (reindexOutputStr == "eb")
      eb.cfg.reindexOutput = eb::EBConfig::Reindex::EB;
    else if (reindexOutputStr == "degree")
      eb.cfg.reindexOutput = eb::EBConfig::Reindex::DEGREE;
    else {
      std::cerr << "Error: invalid --traversal \"" << reindexOutputStr << "\""
                << std::endl;
      return -1;
    }

  } catch (const cxxopts::OptionException& e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    return -1;
  }

  // load the bitstream
  if (!eb.load(inputModelFilename)) return -1;

  // the output
  eb::Model outputModel;

  // Perform the processings
  auto t1 = eb::now();

  std::cout << "Starting decompression of the model" << std::endl;

  eb.decode(outputModel);

  std::cout << "Time on processing: " << eb::elapsed(t1) / 1000.0 << " sec."
            << std::endl;

  // save the result, if success memory is handled by model store
  return eb::IO::saveModel(outputModelFilename, outputModel, true) ? 0 : -1;
}