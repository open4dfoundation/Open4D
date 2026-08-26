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

#include <map>
#include "bitstream.hpp"
#include "sampleStreamNalUnit.hpp"
#include "nalUnit.hpp"
#include "baseMeshBitstream.hpp"
#include "baseMeshCommon.hpp"
#include "syntaxElements/baseMeshFrameParameterSetRbsp.hpp"
#include "syntaxElements/baseMeshSequenceParameterSetRbsp.hpp"
#include "syntaxElements/baseMeshSubmeshDataUnit.hpp"
#include "baseMeshStat.hpp"


namespace basemesh {

class BaseMeshWriter {
public:
  BaseMeshWriter();
  ~BaseMeshWriter();

  void encode(BaseMeshBitstream&        baseMesh,
              vmesh::Bitstream&         bitstream,
              BaseMeshBitstreamGofStat& bistreamStat);

#if defined(BITSTREAM_TRACE)
  void setLogger(vmesh::Logger& logger) { logger_ = &logger; }
#endif
private:
  // 8.2 Specification of syntax functions and descriptors
  bool byteAligned(vmesh::Bitstream& bitstream);
  bool moreDataInPayload(vmesh::Bitstream& bitstream);
  bool moreRbspData(vmesh::Bitstream& bitstream);
  bool payloadExtensionPresent(vmesh::Bitstream& bitstream);
  // 8.3.3 Byte alignment syntax
  void byteAlignment(vmesh::Bitstream& bitstream);

  // Base mesh sub-bitstream syntax
  std::pair<uint32_t, uint32_t>
       calcMaxDataUnitSize(std::vector<BaseMeshSubmeshLayer>& smlList);
  void baseMeshSubmeshLayerRbsp(
    BaseMeshSubmeshLayer&                          bmsl,
    BaseMeshNalUnitType                            nalUnitType,
    std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
    std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
    vmesh::Bitstream&                              bitstream,
    int                                            layerCount = 0);
  uint64_t baseMeshSubmeshHeader(
    BaseMeshSubmeshHeader&                         smh,
    BaseMeshNalUnitType                            nalUnitType,
    std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
    std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
    vmesh::Bitstream&                              bitstream);
  uint64_t baseMeshSubmeshDataUnit(
    BaseMeshSubmeshLayer&                          bmsl,
    std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
    std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
    vmesh::Bitstream&                              bitstream,
    int                                            layerCount = 0);
  uint64_t baseMeshSubmeshDataUnitInter(BaseMeshSubmeshDataUnitInter& meshdata,
                                        BaseMeshSubmeshHeader&        smh,
                                        vmesh::Bitstream& bitstream,
                                        int               layerCount = 0);
  uint64_t baseMeshSubmeshDataUnitIntra(BaseMeshSubmeshDataUnitIntra& meshdata,
                                        vmesh::Bitstream& bitstream);
  uint64_t baseMeshSubmeshDataUnitSkip(BaseMeshSubmeshDataUnitInter& meshdata,
                                       vmesh::Bitstream& bitstream);
  uint32_t baseMeshSubmeshInformation(BaseMeshSubmeshInformation& msmi,
                                      vmesh::Bitstream&           bitstream);
  uint32_t baseMeshFrameParameterSetRbsp(
    BaseMeshFrameParameterSetRbsp& mfps,
    std::vector<BaseMeshSequenceParameterSetRbsp>& /*mspsList*/,
    vmesh::Bitstream& bitstream);
  void baseMeshRefListStruct(BaseMeshRefListStruct&            rls,
                             BaseMeshSequenceParameterSetRbsp& msps,
                             vmesh::Bitstream&                 bitstream);
  void baseMeshProfileToolsetConstraintsInformation(
    BaseMeshProfileToolsetConstraintsInformation& mpftc,
    vmesh::Bitstream&                             bitstream);
  uint32_t baseMeshProfileTierLevel(BaseMeshProfileTierLevel& mpftl,
                                    vmesh::Bitstream&         bitstream,
                                    uint8_t maxNumBmeshSubLayersMinus1);
  void     baseMeshSpsExtension(BaseMeshSequenceParameterSetRbsp& msps,
                                uint32_t                          extIdx,
                                vmesh::Bitstream&                 bitstream);
  uint32_t
  baseMeshSequenceParameterSetRbsp(BaseMeshSequenceParameterSetRbsp& msps,
                                   vmesh::Bitstream& bitstream);

  // 8.3.5 NAL unit syntax
  // 8.3.5.1 General NAL unit syntax
  void nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  // 8.3.5.2 NAL unit header syntax
  void nalUnitHeader(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  void seiRbsp(vmesh::Bitstream&   bitstream,
               BaseMeshNalUnitType nalUnitType,
               basemesh::SEI&      sei);

  // 8.3.6.10 RBSP trailing bit syntax
  void rbspTrailingBits(vmesh::Bitstream& bitstream);

  void seiMessage(vmesh::Bitstream&   bitstream,
                  BaseMeshNalUnitType nalUnitType,
                  basemesh::SEI&      sei);

  // D.2 Sample stream NAL unit syntax and semantics
  // D.2.1 Sample stream NAL header syntax
  void sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                             vmesh::SampleStreamNalUnit& ssnu);

  // H.14 SEI payload syntax
  // H.14.1  General SEI message syntax
  void seiPayload(vmesh::Bitstream&   bitstream,
                  basemesh::SEI&      sei,
                  BaseMeshNalUnitType nalUnitType);

  // H.15.2.1  VUI parameters syntax
  void basemeshVuiParameters(vmesh::Bitstream&                bitstream,
                             BaseMeshSequenceParameterSetRbsp bmsps,
                             BasemeshVuiParameters&           vp);
  // H.15.2.2	Basemesh HRD parameters syntax x
  void basemeshHrdParameters(vmesh::Bitstream&      bitstream,
                             bool                   commonInPresentFlag,
                             int                    maxSubLayersMinus1,
                             BaseMeshHrdParameters& hp);
  // H.15.2.3	Basemesh sub-layer HRD parameters syntax
  void basemeshHrdSubLayerParameters(vmesh::Bitstream&              bitstream,
                                     BaseMeshHrdParameters&         hp,
                                     BaseMeshHrdSubLayerParameters& hlsp,
                                     size_t                         cabCnt);
  // ISO/IEC 23090-29:H.14.1.10 Basemesh Attribute transformation parameters SEI message syntax
  void baseMeshAttributeTransformationParams(vmesh::Bitstream& bitstream,
                                             basemesh::SEI&    seiAbstract);

#if defined(BITSTREAM_TRACE)
  vmesh::Logger* logger_ = nullptr;
#endif
};

};  // namespace basemesh
