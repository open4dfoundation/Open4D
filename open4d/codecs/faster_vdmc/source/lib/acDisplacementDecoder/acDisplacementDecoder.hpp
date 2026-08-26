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

#include "vmc.hpp"
#include "acDisplacementDecoder.hpp"
#include "acDisplacementBitstream.hpp"
#include "acDisplacementFrame.hpp"
#include "acDisplacementContext.hpp"

using namespace acdisplacement;

class ACDisplacementDecoder {
public:
  ACDisplacementDecoder() {}
  ~ACDisplacementDecoder() {}
  void setACDisplacementSubBitstream();
  bool
  decodeDisplacementSubbitstream(AcDisplacementBitstream& displacementStream);
  std::vector<uint32_t>&
  getDisplIDToIndex(AcDisplacementBitstream& displacementStream) {
    return displacementStream.getDisplFrameParameterSetList()[0]
      .getDisplInformation()
      ._displIDToIndex;
  }
  std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& getdecodedAcDispls() {
    return decodedAcDispls_;  //[acdispl][frame]
  }

private:
  bool decompressDisplacementsAC(
    AcDisplacementBitstream& displacementStream,
    int32_t                               frameIndex,
    const DisplacementLayer&              dsl,
    const DisplInformation&               dinfo,
    std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& referenceFrameList);
  bool decodeAC(int32_t                      frameIndex,
                int32_t                      partIndex,
                const std::vector<uint8_t>&  dataunit,
                DisplacementType             dispType,
                const std::vector<uint32_t>& numSubblockLodMinus1,
                uint32_t                     dispDimensions,
                const std::vector<uint32_t>& pointCountPerLevel,
                std::vector<vmesh::Vec3<int64_t>>&  disp);
  const acdisplacement::AcDisplacementFrame*
  findReferenceFrameAC(const std::vector<acdisplacement::AcDisplacementFrame>& referenceFrameList,
                       int32_t referenceFrameAbsoluteIndex,  //0~31
                       int32_t currentFrameAbsoluteIndex);   //0~31

  int32_t
          createDspsReferenceLists(DisplSequenceParameterSetRbsp&     dsps,
                                   std::vector<std::vector<int32_t>>& dspsRefDiffList);
  int32_t createDhReferenceList(DisplSequenceParameterSetRbsp& dsps,
                                DisplacementLayer&             dispLayer,
                                std::vector<int32_t>&          referenceList);

  std::string _keepFilesPathPrefix = {};

  std::vector<std::vector<acdisplacement::AcDisplacementFrame>> decodedAcDispls_;  //[acdispl][frame]

  size_t calcTotalDisplacemetFrameCount(AcDisplacementBitstream& dmStream);
  size_t calculateDFOCval(AcDisplacementBitstream&          dmStream,
                          std::vector<DisplacementLayer>& displList,
                          size_t                          displOrder);
  size_t calcMaxDisplCount(AcDisplacementBitstream& dmStream);
};
