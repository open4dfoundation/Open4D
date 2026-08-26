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

#include "baseMeshCommon.hpp"
#include "baseMeshBitstream.hpp"
#include "accessUnitDelimiterRbsp.hpp"
#include "endOfSequenceRbsp.hpp"
#include "endOfSubBitstreamRbsp.hpp"
#include "fillerDataRbsp.hpp"
#include "nalUnit.hpp"
#include "sampleStreamNalUnit.hpp"
#include "baseMeshStat.hpp"
#include "bitstream.hpp"

namespace basemesh {

class BaseMeshReader {
public:
 BaseMeshReader();
 ~BaseMeshReader();

#if defined(BITSTREAM_TRACE)
 void setLogger(vmesh::Logger& logger) {
   logger_ = &logger;
 }
#endif
 void decode(BaseMeshBitstream& baseMesh, vmesh::Bitstream& bitstream,
             BaseMeshBitstreamGofStat&        bistreamStat);
private:

 // 8.2 Specification of syntax functions and descriptors
 bool byteAligned(vmesh::Bitstream& bitstream);
 bool lengthAligned(vmesh::Bitstream& bitstream);
 bool moreDataInPayload(vmesh::Bitstream& bitstream);
 bool moreRbspData(vmesh::Bitstream& bitstream);
 //bool nextBits(Bitstream& bitstream, int n);
 bool payloadExtensionPresent(vmesh::Bitstream& bitstream);

 //Base mesh reference structure
 void baseMeshRefListStruct(BaseMeshRefListStruct&                rls,
                            BaseMeshSequenceParameterSetRbsp&  msps,
                            vmesh::Bitstream&                         bitstream );

 void baseMeshProfileToolsetConstraintsInformation(
   BaseMeshProfileToolsetConstraintsInformation& mpftl, vmesh::Bitstream& bitstream);
 //Base mesh meshProfileTierLevel
 void baseMeshProfileTierLevel(BaseMeshProfileTierLevel& mpftl, vmesh::Bitstream& bitstream, uint8_t maxNumBmeshSubLayersMinus1);
 void baseMeshSpsExtension(BaseMeshSequenceParameterSetRbsp& msps, uint32_t extIdx, vmesh::Bitstream& bitstream);
 // Base mesh sequence parameter set Rbsp
 void
 baseMeshSequenceParameterSetRbsp(BaseMeshSequenceParameterSetRbsp& bmsps,
                                  vmesh::Bitstream& bitstream);

 // Base mesh frame parameter set Rbsp
 void baseMeshFrameParameterSetRbsp(BaseMeshFrameParameterSetRbsp& bmfps,
                                    std::vector<BaseMeshSequenceParameterSetRbsp>& bmspsList,
                                    vmesh::Bitstream&                     bitstream);

 // Base mesh frame tile information syntax
 void baseMeshSubmeshInformation(BaseMeshSubmeshInformation&     bmfti,
                                 vmesh::Bitstream& bitstream);


 size_t baseMeshSubmeshLayerRbsp(BaseMeshSubmeshLayer&  bmsl,
                                   BaseMeshNalUnitType    nalUnitType,
                                   uint64_t               nalUnitSize,
                                   std::vector<BaseMeshFrameParameterSetRbsp>& mfpsList,
                                   std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
                                   vmesh::Bitstream&             bitstream);
 uint64_t baseMeshSubmeshHeader(BaseMeshSubmeshHeader&                         bmsh,
                                BaseMeshNalUnitType                            nalUnitType,
                                std::vector<BaseMeshFrameParameterSetRbsp>&    mfpsList,
                                std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
                                vmesh::Bitstream&                                     bitstream);
 size_t  baseMeshSubmeshDataUnit(BaseMeshSubmeshLayer&    bmsl,
                                  uint64_t                 mfuRbspSize,
                                  uint64_t                 mfuHeaderSize,
                                  std::vector<BaseMeshFrameParameterSetRbsp>& mfpsList,
                                  std::vector<BaseMeshSequenceParameterSetRbsp>& mspsList,
                                  vmesh::Bitstream&               bitstream);
 uint64_t baseMeshSubmeshDataUnitIntra( BaseMeshSubmeshDataUnitIntra& meshdata,
                                       uint64_t                 smduSize,
                                       vmesh::Bitstream&                bitstream);
 uint64_t baseMeshSubmeshDataUnitInter ( BaseMeshSubmeshDataUnitInter& meshdata,
                                       BaseMeshSubmeshHeader& smh,
                                       uint64_t                  smduSize,
                                       vmesh::Bitstream&                bitstream);
 uint64_t baseMeshSubmeshDataUnitSkip ( BaseMeshSubmeshDataUnitInter& meshdata,
                                      uint64_t                  smduSize,
                                      vmesh::Bitstream&                bitstream);
 // 8.3.3 Byte alignment syntax
 void byteAlignment(vmesh::Bitstream& bitstream);

 // 8.3.5 NAL unit syntax
 // 8.3.5.1 General NAL unit syntax
 void nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

 // 8.3.5.2 NAL unit header syntax
 void nalUnitHeader(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);


 void seiRbsp(vmesh::Bitstream&          bitstream,
              BaseMeshNalUnitType nalUnitType,
              basemesh::BaseMeshSei&             sei);

 // 8.3.6.5  Access unit delimiter Rbsp syntax
 void accessUnitDelimiterRbsp(vmesh::AccessUnitDelimiterRbsp& aud,
                              vmesh::Bitstream&               bitstream);

 // 8.3.6.6  End of sequence Rbsp syntax
 void endOfSequenceRbsp(vmesh::EndOfSequenceRbsp& eosbsp,
                        vmesh::Bitstream&         bitstream);

 // 8.3.6.7  End of bitstream Rbsp syntax
 void endOfAtlasSubBitstreamRbsp(vmesh::EndOfAtlasSubBitstreamRbsp& eoasb,
                                 vmesh::Bitstream&                  bitstream);

 // 8.3.6.8  Filler data Rbsp syntax
 void fillerDataRbsp(vmesh::FillerDataRbsp& fdrbsp,
                     vmesh::Bitstream&      bitstream);



 // 8.3.6.10 RBSP trailing bit syntax
 void rbspTrailingBits(vmesh::Bitstream& bitstream);

 void seiMessage(vmesh::Bitstream&          bitstream,
                 BaseMeshNalUnitType nalUnitType,
                 basemesh::BaseMeshSei&             sei);


 // D.2 Sample stream NAL unit syntax and semantics
 // D.2.1 Sample stream NAL header syntax
 void sampleStreamNalHeader(vmesh::Bitstream& bitstream, vmesh::SampleStreamNalUnit& ssnu);

 // F.2  SEI payload syntax
 void seiPayload(vmesh::Bitstream&                  bitstream,
                 BaseMeshNalUnitType         nalUnitType,
                 BaseMeshSeiPayloadType      payloadType,
                 size_t                      payloadSize,
                 basemesh::BaseMeshSei&                     seiList);

 // H.15.2.1  VUI parameters syntax
 void basemeshVuiParameters(vmesh::Bitstream& bitstream, BaseMeshSequenceParameterSetRbsp bmsps, BasemeshVuiParameters& vp);
 // H.15.2.2	Basemesh HRD parameters syntax x
 void basemeshHrdParameters(vmesh::Bitstream& bitstream, bool commonInPresentFlag, int maxSubLayersMinus1,
                            BaseMeshHrdParameters& hp);
 // H.15.2.3	Basemesh sub-layer HRD parameters syntax
 void basemeshHrdSubLayerParameters(vmesh::Bitstream& bitstream,
                                    BaseMeshHrdParameters& hp,
                                    BaseMeshHrdSubLayerParameters& hlsp, size_t cabCnt);

 // ISO/IEC 23090-29:H.14.1.10 Basemesh Attribute transformation parameters SEI message syntax
 void baseMeshAttributeTransformationParams(vmesh::Bitstream& bitstream, basemesh::SEI& seiAbstract);

 int32_t prevPatchSizeU_ = 0;
 int32_t prevPatchSizeV_ = 0;
 int32_t predPatchIndex_ = 0;
 int32_t prevFrameIndex_ = 0;

#if defined(BITSTREAM_TRACE)
 vmesh::Logger* logger_ = nullptr;
#endif

};

};  // namespace vmesh
