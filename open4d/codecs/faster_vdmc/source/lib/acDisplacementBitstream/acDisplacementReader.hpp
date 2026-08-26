/* The copyright in this software is being made available under the BSD
* License, included below. This software may be subject to other third party
* and contributor rights, including patent rights, and no such rights are
* granted under this license.
*
* Copyright (c) 2025, ISO/IEC
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
#pragma once

#include "acDisplacementBitstream.hpp"
#include "acDisplacementStat.hpp"
#include "sampleStreamNalUnit.hpp"
#include "nalUnit.hpp"

namespace acdisplacement {

class AcDisplacementReader {
public:
  AcDisplacementReader();
  ~AcDisplacementReader();

#if defined(BITSTREAM_TRACE)
  void setLogger(vmesh::Logger& logger) { logger_ = &logger; }
#endif

  void decode(AcDisplacementBitstream&        acd,
              vmesh::Bitstream&               bitstream,
              AcDisplacementBitstreamGofStat& bistreamStat);

private:
  // D.2 Sample stream NAL unit syntax and semantics
  // D.2.1 Sample stream NAL header syntax
  void sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                             vmesh::SampleStreamNalUnit& ssnu);

  // 8.2 Specification of syntax functions and descriptors
  bool byteAligned(vmesh::Bitstream& bitstream);
  bool lengthAligned(vmesh::Bitstream& bitstream);
  bool moreDataInPayload(vmesh::Bitstream& bitstream);
  bool moreRbspData(vmesh::Bitstream& bitstream);
  bool payloadExtensionPresent(vmesh::Bitstream& bitstream);
  // 8.3.6.10 RBSP trailing bit syntax
  void rbspTrailingBits(vmesh::Bitstream& bitstream);

  void
  displacementLayerRbsp(DisplacementLayer&                       dl,
                        DisplNalUnitType                         nalUnitType,
                        std::vector<DisplFrameParameterSetRbsp>& dfpsList,
                        std::vector<DisplSequenceParameterSetRbsp>& dspsList,
                        vmesh::Bitstream&                           bitstream);

  uint64_t
  displacementHeader(DisplacementHeader&                         dh,
                     DisplNalUnitType                            nalUnitType,
                     std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
                     std::vector<DisplSequenceParameterSetRbsp>& mspsList,
                     vmesh::Bitstream&                           bitstream);
  uint64_t
           displacementDataUnit(DisplacementLayer&                          dl,
                                std::vector<DisplFrameParameterSetRbsp>&    mfpsList,
                                std::vector<DisplSequenceParameterSetRbsp>& mspsList,
                                vmesh::Bitstream&                           bitstream);
  uint64_t displacementDataUnitInter(DisplacementDataInter& displdata,
                                     uint64_t               duSize,
                                     vmesh::Bitstream&      bitstream);
  uint64_t displacementDataUnitIntra(DisplacementDataIntra& displdata,
                                     uint64_t               duSize,
                                     vmesh::Bitstream&      bitstream);
  void     displacementNoDataUnit(vmesh::Bitstream& bitstream);
  void     displInformation(DisplInformation& di, vmesh::Bitstream& bitstream);
  uint32_t displFrameParameterSetRbsp(
    DisplFrameParameterSetRbsp&                 dfps,
    std::vector<DisplSequenceParameterSetRbsp>& mspsList,
    vmesh::Bitstream&                           bitstream);
  void refListStruct(DisplRefListStruct&            rls,
                     DisplSequenceParameterSetRbsp& dsps,
                     vmesh::Bitstream&              bitstream);

  void dispProfileToolsetConstraintsInformation(
    DisplProfileToolsetConstraintsInformation& dptci,
    vmesh::Bitstream&                          bitstream);

  uint32_t displProfileTierLevel(DisplProfileTierLevel& dpftl,
                                 vmesh::Bitstream&      bitstream);
  uint32_t displSequenceParameterSetRbsp(DisplSequenceParameterSetRbsp& dsps,
                                         vmesh::Bitstream& bitstream);

  void displQuantizationParameters(vmesh::Bitstream&            bitstream,
                                   DisplQuantizationParameters& ltp,
                                   uint32_t subdivIterationCount,
                                   uint8_t  numComp);

  void displacementHrdParameters(vmesh::Bitstream& bitstream,
      bool commonInPresentFlag, int maxSubLayersMinus1, DisplHrdParameters& hp);

  void displacementVuiParameters(vmesh::Bitstream& bitstream,
      DisplSequenceParameterSetRbsp displsps,
      DisplVuiParameters& vp);

  void displacementHrdSubLayerParameters(vmesh::Bitstream& bitstream,
      DisplHrdParameters& hp,
      DisplHrdSubLayerParameters& hlsp,
      size_t                 cabCnt);

  // 8.3.5 NAL unit syntax
  // 8.3.5.1 General NAL unit syntax
  void nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  // 8.3.5.2 NAL unit header syntax
  void nalUnitHeader(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  // 8.3.3 Byte alignment syntax
  void byteAlignment(vmesh::Bitstream& bitstream);

  int32_t prevPatchSizeU_ = 0;
  int32_t prevPatchSizeV_ = 0;
  int32_t predPatchIndex_ = 0;
  int32_t prevFrameIndex_ = 0;

#if defined(BITSTREAM_TRACE)
  vmesh::Logger* logger_ = nullptr;
#endif
};

};  // namespace acdisplacement
