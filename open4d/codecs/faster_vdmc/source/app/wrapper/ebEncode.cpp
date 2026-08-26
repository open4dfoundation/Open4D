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
#include "ebEncoder.h"
#include "ebReversiEncoder.h"

int
main(int argc, char* argv[]) {
  const char* name  = "ebEncode";
  const char* brief = "ISO/IEC 23090-29 Annex I test encoder application";

  eb::EBEncoder* eb = 0;

  std::string traversalStr;
  std::string posPredStr;
  std::string uvPredStr;
  std::string normPredStr;
  std::string genPredStr;
  std::string inputModelFilename;
  std::string outputModelFilename;

  // command line parameters
  try {
    cxxopts::Options options(name, brief);
    options.add_options()("h,help", "Print usage")(
      "i,inputModel",
      "path to input model (obj or vmb file)",
      cxxopts::value<std::string>())("o,outputModel",
                                     "path to output encoded file (.eb)",
                                     cxxopts::value<std::string>())(
      "qp",
      "Positions quantization bits in [-1,20] range. Special qp=0 means auto "
      "integer "
      "input, no quantization; qp=-1 means skip. Other values will be "
      "clamped.",
      cxxopts::value<int>()->default_value("11"))(
      "qt",
      "UVCoords quantization bits in [-1,20] range. Special qt=0 means auto "
      "integer "
      "input, no quantization; qt=-1 means skip. Other will be clamped.",
      cxxopts::value<int>()->default_value("10"))(
      "qn",
      "Normals quantization bits in [-1,20] range. Special qn=0 means auto "
      "integer input, "
      "no quantization; qn=-1 means skip. Other will be clamped.",
      cxxopts::value<int>()->default_value("16"))(
      "qg",
      "Generic quantization bits in [-1,20] range. Special qg=0 means auto "
      "integer input, "
      "no quantization; qn=-1 means skip. Other will be clamped.",
      cxxopts::value<int>()->default_value("16"))(
      "qc",
      "Colors  quantization bits in [-1,20] range. Special qc=0 means auto "
      "integer input, "
      "no quantization; qc=-1 means skip. Other will be clamped.",
      cxxopts::value<int>()->default_value("8"))(
      "qm",
      "MaterialId quantization bits in [-1,20] range. Special qm=0 means auto "
      "integer "
      "input, no quantization; qm=-1 means skip. Other will be clamped.",
      cxxopts::value<int>()->default_value("-1"))(
      "yuv",
      "Colors space conversion to yuv prior to quantization if any.",
      cxxopts::value<bool>()->default_value("false"))(
      "format",
      "output write format in [binary].",
      cxxopts::value<std::string>()->default_value("binary"))(
      "traversal",
      "Vertex traversal method in [eb, degree]",
      cxxopts::value<std::string>()->default_value("degree"))(
      "posPred",
      "Positions predictor in [none, mpara]",
      cxxopts::value<std::string>()->default_value("mpara"))(
      "uvPred",
      "UV coordinates predictor in [none, stretch]",
      cxxopts::value<std::string>()->default_value("stretch"))(
      "useMainIndexIfEqual",
      "When set, if aux attribute has separate index table but that is "
      "equal to main one, it will use main one which will prevent"
      "testing and coding seams. ",
      cxxopts::value<bool>()->default_value("false"))(
      "intAttr",
      "Consider input attributes (pos and uv) as integers stored on the "
      "number of bits resp. defined by --qp and --qt",
      cxxopts::value<bool>()->default_value("false"))(
      "unify",
      "Unify vertices positions and attributes (default=1, set to 0 to "
      "deactivate)"
      "Shall not be used if intAttr is set (i.e. if model is prequantized),"
      "this would introduce many topology artifacts",
      cxxopts::value<bool>()->default_value("true"))(
      "reverseUnification",
      "Adds information to enable reverseUnification",
      cxxopts::value<bool>()->default_value("false"))(
      "reindex",
      "reindex traversal method in [eb, degree] when reverse unification",
      cxxopts::value<std::string>()->default_value("degree"))(
      "deduplicate",
      "Deduplicate vertices positions added to handle non-manifold meshes on "
      "decode (adds related information in the bitstream). The purpose is to"
      "encode the mesh in a lossless manner and try to recreate original "
      "topology",
      cxxopts::value<bool>()->default_value("false"))(
      "optionFlags",
      "Flags for options during development",
      cxxopts::value<uint32_t>()->default_value("0"))(
      "genPred",
      "Generic predictor in [delta, mpara], Default: delta",
      cxxopts::value<std::string>()->default_value("delta"))(
      "useEntropyPacket",
      "Use entropy packet. (default=0, set to 1 to enable entropy packet)",
      cxxopts::value<bool>()->default_value("false"))(
      "normPred",
      "Normal predictor in [delta, mpara, cross], Default: cross",
      cxxopts::value<std::string>()->default_value("cross"))(
      "useOctahedral",
      "To use Octahedral encoding. (default=1, set to 0 to deactivate)",
      cxxopts::value<bool>()->default_value("true"))(
      "qpOcta",
      "Normals quantization bits for 2D octahedral representation."
      "default = 16",
      cxxopts::value<int>()->default_value("16"))(
      "normalEncodeSecondResidual",  // Have not been implemented yet (i.e., always true)
      "A flag to encode second residual (lossless) for Octahedral mode. "
      "(default=1, set to 0 to deactivate)"
      "Needs to always be set to 1 in V-DMC. Does not support "
      "reverseUnification",
      cxxopts::value<bool>()->default_value("true"))(
      "wrapAround",
      "To use Wrap around for Octahedral normal encoding. (default=1, set to "
      "0 to deactivate)",
      cxxopts::value<bool>()->default_value("true"));

    auto result = options.parse(argc, argv);

    // Analyse the options
    if (result.count("help") || result.arguments().size() == 0) {
      std::cout << options.help() << std::endl;
      return -1;
    }

    eb                  = new eb::EBReversiEncoder();
    eb->cfg.intAttr     = result["intAttr"].as<bool>();
    eb->cfg.deduplicate = result["deduplicate"].as<bool>();
    eb->cfg.unify       = result["unify"].as<bool>();
    if (eb->cfg.unify && eb->cfg.intAttr) {
      std::cout
        << "Warning: unify option shall not be used together with intAttr."
        << std::endl;
    }
    eb->cfg.useMainIndexIfEqual = result["useMainIndexIfEqual"].as<bool>();

    eb->cfg.reverseUnification = result["reverseUnification"].as<bool>();

    eb->cfg.optionFlags = result["optionFlags"].as<uint32_t>();

    eb->qp = result["qp"].as<int>();
    if (eb->qp != 0 && eb->qp != -1) glm::clamp(eb->qp, 1, 20);

    eb->qt = result["qt"].as<int>();
    if (eb->qt != 0 && eb->qt != -1) glm::clamp(eb->qt, 1, 20);

    eb->qn = result["qn"].as<int>();
    if (eb->qn != 0 && eb->qn != -1) glm::clamp(eb->qn, 1, 20);

    eb->qc = result["qc"].as<int>();
    if (eb->qc != 0 && eb->qc != -1) glm::clamp(eb->qc, 1, 20);

    eb->qm = result["qm"].as<int>();
    if (eb->qm != 0 && eb->qm != -1) glm::clamp(eb->qm, 1, 20);

    eb->qg = result["qg"].as<int>();
    if (eb->qg != 0 && eb->qg != -1) glm::clamp(eb->qg, 1, 20);

    std::string format = result["format"].as<std::string>();
    if (format != "binary") {
      std::cerr << "Error: invalid --format \"" << format << "\"" << std::endl;
      return -1;
    }

    traversalStr = result["traversal"].as<std::string>();
    if (traversalStr == "eb") eb->cfg.traversal = eb::EBConfig::Traversal::EB;
    else if (traversalStr == "degree")
      eb->cfg.traversal = eb::EBConfig::Traversal::DEGREE;
    else {
      std::cerr << "Error: invalid --traversal \"" << traversalStr << "\""
                << std::endl;
      return -1;
    }

    traversalStr = result["reindex"].as<std::string>();
    if (traversalStr == "eb")
      eb->cfg.reindexOutput = eb::EBConfig::Reindex::EB;
    else if (traversalStr == "degree")
      eb->cfg.reindexOutput = eb::EBConfig::Reindex::DEGREE;
    else {
      std::cerr << "Error: invalid --reindex \"" << traversalStr << "\""
                << std::endl;
      return -1;
    }

    posPredStr = result["posPred"].as<std::string>();
    if (posPredStr == "mpara") eb->cfg.posPred = eb::EBConfig::PosPred::MPARA;
    else {
      std::cerr << "Error: invalid --posPred \"" << posPredStr << "\""
                << std::endl;
      return -1;
    }

    uvPredStr = result["uvPred"].as<std::string>();
    if (uvPredStr == "stretch") eb->cfg.uvPred = eb::EBConfig::UvPred::STRETCH;
    else {
      std::cerr << "Error: invalid --uvPred \"" << uvPredStr << "\""
                << std::endl;
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
    if (result.count("outputModel")) {
      outputModelFilename = result["outputModel"].as<std::string>();
    } else {
      std::cerr << "Warning: missing outputModel parameter, compressed model "
                   "will not be saved"
                << std::endl;
    }

    // Normals
    normPredStr = result["normPred"].as<std::string>();
    if (normPredStr == "delta")
      eb->cfg.normPred = eb::EBConfig::NormPred::DELTA;
    else if (normPredStr == "mpara")
      eb->cfg.normPred = eb::EBConfig::NormPred::MPARA;
    else if (normPredStr == "cross")
      eb->cfg.normPred = eb::EBConfig::NormPred::CROSS;
    else {
      std::cerr << "Error: invalid --normPred \"" << normPredStr << "\""
                << std::endl;
      return -1;
    }

    eb->cfg.useEntropyPacket = result["useEntropyPacket"].as<bool>();
    eb->cfg.useOctahedral    = result["useOctahedral"].as<bool>();
    eb->cfg.normalEncodeSecondResidual =
      result["normalEncodeSecondResidual"].as<bool>();
    eb->cfg.wrapAround = result["wrapAround"].as<bool>();

    eb->cfg.qpOcta = result["qpOcta"].as<int>();

    // Generoc
    genPredStr = result["genPred"].as<std::string>();
    if (genPredStr == "delta") eb->cfg.genPred = eb::EBConfig::GenPred::DELTA;
    else if (genPredStr == "mpara")
      eb->cfg.genPred = eb::EBConfig::GenPred::MPARA;
    else {
      std::cerr << "Error: invalid --genPred \"" << genPredStr << "\""
                << std::endl;
      return -1;
    }

  } catch (const cxxopts::OptionException& e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    return -1;
  }

  // this is mandatory to print floats with full precision
  std::cout.precision(std::numeric_limits<float>::max_digits10);

  // the input
  eb::Model inputModel;

  if (!eb::IO::loadModel(inputModelFilename, inputModel)) { return -1; }

  if (inputModel.getPositionCount() == 0) {
    std::cout << "Error: invalid input model (missing vertices) from "
              << inputModelFilename << std::endl;
    return -1;
  }
  if (inputModel.getTriangleCount() == 0) {
    std::cout << "Error: invalid input model (not a mesh) from "
              << inputModelFilename << std::endl;
    return -1;
  }

  // Perform the processings
  clock_t t1 = clock();

  std::cout << "Starting EB compression of the model" << std::endl;
  std::cout << "  qp = " << eb->qp << std::endl;
  std::cout << "  qt = " << eb->qt << std::endl;
  std::cout << "  qm = " << eb->qm << std::endl;
  std::cout << "  qn = " << eb->qn << std::endl;
  std::cout << "  qg = " << eb->qg << std::endl;
  std::cout << "  qc = " << eb->qc << " not used yet" << std::endl;
  std::cout << "  yuv = " << eb->yuv << " not used yet" << std::endl;
  std::cout << "  traversal = " << traversalStr << std::endl;
  std::cout << "  intAttr = " << eb->cfg.intAttr << std::endl;
  std::cout << "  unify = " << eb->cfg.unify << std::endl;
  std::cout << "  useMainIndexIfEqual = " << eb->cfg.useMainIndexIfEqual
            << std::endl;
  std::cout << "  deduplicate = " << eb->cfg.deduplicate << std::endl;
  std::cout << "  normPred = " << normPredStr << std::endl;
  std::cout << "  useOctahedral = " << eb->cfg.useOctahedral << std::endl;
  std::cout << "  wrapAround = " << eb->cfg.wrapAround << std::endl;

  eb->encode(inputModel);

  clock_t t2 = clock();
  std::cout << "Time on processing: " << ((float)(t2 - t1)) / CLOCKS_PER_SEC
            << " sec." << std::endl;

  // save the result
  eb->save(outputModelFilename);

  delete eb;

  return 0;
}