/* The copyright in this software is being made available under the BSD
* Licence, included below.  This software may be subject to other third
* party and contributor rights, including patent rights, and no such
* rights are granted under this licence.
*
* Copyright (c) 2025, ISO/IEC
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
#pragma once

#include <cstdint>
#include <string>

#include "util/mesh.hpp"
#include "vmc.hpp"
#include "util/checksum.hpp"
#include "motionContexts.hpp"
#include "entropy.hpp"
#include "acDisplacementBitstream.hpp"
#include "acDisplacementQuantizationParameters.hpp"

//============================================================================

namespace acdisplacement {

class AcDisplacementEncoderParameters {
public:
  AcDisplacementEncoderParameters() {}
  ~AcDisplacementEncoderParameters(){};

  bool                  enableSignalledIds = false;
  int32_t               numSubmesh         = 1;
  std::vector<uint32_t> submeshIdList;

  size_t           maxNumRefList  = 1;
  size_t           maxNumRefFrame = 4;
  std::vector<int> refFrameDiff   = {1, 2, 3, 4};

  int32_t bitDepthPosition          = 12;
  int32_t subdivisionIterationCount = 3;

  int32_t               transformMethod                   = 1;
  bool                  lodDisplacementQuantizationFlag   = false;
  int32_t               bitDepthOffset                    = 0;
  std::vector<uint32_t> log2LevelOfDetailInverseScale     = {1, 1, 1};
  std::vector<uint32_t> liftingQP                         = {16, 28, 28};
  std::vector<std::array<int32_t, 3>> qpPerLevelOfDetails = {{16, 28, 28},
                                                             {22, 34, 34},
                                                             {28, 40, 40}};
  bool                                InverseQuantizationOffsetFlag   = true;
  bool                                applyOneDimensionalDisplacement = true;
  bool                                checksum                        = true;

  std::vector<uint32_t> subBlocksPerLoD = {1, 1, 1, 1};

  std::vector<bool> blockSubdiv = {false, false, true, true};

  bool displacementFillZerosFlag = false;

  double liftingBias[3] = {1. / 3., 1. / 3., 1. / 3.};

  vmesh::DisplacementQuantizationType displacementQuantizationType =
    vmesh::DisplacementQuantizationType::DEFAULT;

  int32_t geometryVideoBitDepth          = 10;
  double  displacementFillZerosThreshold = 0.05;
};

//============================================================================

class AcDisplacementEncoder {
public:
  AcDisplacementEncoder() {}
  AcDisplacementEncoder(const AcDisplacementEncoder& rhs)            = delete;
  AcDisplacementEncoder& operator=(const AcDisplacementEncoder& rhs) = delete;
  ~AcDisplacementEncoder()                                           = default;


  int32_t
  compressACDisplacements(AcDisplacementBitstream&               dmSubstream,
                          std::vector<vmesh::VMCSubmesh>&               gof,
                          int32_t                                submeshIndex,
                          int32_t                                frameCount,
                          const AcDisplacementEncoderParameters& params);


  void createDispParameterSet(DisplSequenceParameterSetRbsp&         dsps,
                              DisplFrameParameterSetRbsp&            dfps,
                              const AcDisplacementEncoderParameters& params);

private:
  void
  constructDspsRefListStruct(DisplSequenceParameterSetRbsp&         dsps,
                             const AcDisplacementEncoderParameters& encParams);

  void constructDisplRefListStruct(DisplSequenceParameterSetRbsp& dsps,
                                   DisplFrameParameterSetRbsp&    dfps,
                                   DisplacementLayer&             dl,
                                   int32_t                        submeshIndex,
                                   int32_t                        frameIndex,
                                   int32_t referenceFrameIndex);



  int32_t computeInverseQuantizationOffsetAC(
    vmesh::VMCSubmesh                        frame,
    std::vector<std::vector<int64_t>> iscale,
    uint8_t                           dispDimension,
    const std::vector<double>&        liftingLog2LevelOfDetailInverseScale,
    std::vector<std::vector<std::vector<std::vector<int8_t>>>>&
      InverseQuantizationOffsetValues);

  static bool
  quantizeDisplacements(vmesh::VMCSubmesh&                            frame,
                        int32_t                                submeshIndex,
                        const AcDisplacementEncoderParameters& params);

  static void fillDisZeros(vmesh::VMCSubmesh&                            frame,
                           const AcDisplacementEncoderParameters& params);
};

}  // namespace vmesh
