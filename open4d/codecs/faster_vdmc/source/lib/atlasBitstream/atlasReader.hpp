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

#include "bitstream.hpp"
#include "atlasBitstream.hpp"
#include "syntaxElements/aapsVpccExtension.hpp"
#include "syntaxElements/atlasAdaptationParameterSetRbsp.hpp"
#include "syntaxElements/fillerDataRbsp.hpp"
#include "syntaxElements/endOfSubBitstreamRbsp.hpp"
#include "syntaxElements/endOfSequenceRbsp.hpp"
#include "syntaxElements/nalUnit.hpp"
#include "syntaxElements/accessUnitDelimiterRbsp.hpp"
#include "syntaxElements/sampleStreamNalUnit.hpp"

namespace atlas {

class AtlasReader {
public:
  AtlasReader();
  ~AtlasReader();

  // 8.3.2.4 Atlas sub-bitstream syntax
  void decode(AtlasBitstream& atlas, vmesh::Bitstream& bitstream);

#if defined(BITSTREAM_TRACE)
  void setLogger(vmesh::Logger& logger) { logger_ = &logger; }
#endif
private:
  // 8.2 Specification of syntax functions and descriptors
  bool byteAligned(vmesh::Bitstream& bitstream);
  bool lengthAligned(vmesh::Bitstream& bitstream);
  bool moreDataInPayload(vmesh::Bitstream& bitstream);
  bool moreRbspData(vmesh::Bitstream& bitstream);
  bool payloadExtensionPresent(vmesh::Bitstream& bitstream);

  // 8.3.3 Byte alignment syntax
  void byteAlignment(vmesh::Bitstream& bitstream);

  // 8.3.5 NAL unit syntax
  // 8.3.5.1 General NAL unit syntax
  void nalUnit(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  // 8.3.5.2 NAL unit header syntax
  void nalUnitHeader(vmesh::Bitstream& bitstream, vmesh::NalUnit& nalUnit);

  // 8.3.5.2  NAL unit header syntax
  // 8.3.6.1 Atlas sequence parameter set Rbsp
  // 8.3.6.1.1 General Atlas sequence parameter set Rbsp
  void atlasSequenceParameterSetRbsp(AtlasSequenceParameterSetRbsp& asps,
                                     vmesh::Bitstream&              bitstream);

  // 8.3.6.1.3 Atlas sequence parameter set V-DMC extension syntax
  void aspsVdmcExtension(vmesh::Bitstream&              bitstream,
                         AtlasSequenceParameterSetRbsp& asps,
                         AspsVdmcExtension&             ext);

  // 8.3.6.1.4 Quantization parameters syntax
  void vdmcQuantizationParameters(vmesh::Bitstream&           bitstream,
                                  VdmcQuantizationParameters& ltp,
                                  uint32_t subdivIterationCount,
                                  uint8_t  numComp);

  // 8.3.6.1.5 Lifting transform parameters syntax
  void vdmcLiftingTransformParameters(vmesh::Bitstream& bitstream,
                                      VdmcLiftingTransformParameters& ltp,
                                      int      paramLevelIndex,
                                      bool     dirLiftFlag,
                                      uint32_t subdivIterationCount,
                                      size_t   patchMode);

  // 8.3.6.2 Atlas frame parameter set Rbsp syntax
  // 8.3.6.2.1 General atlas frame parameter set Rbsp syntax
  void atlasFrameParameterSetRbsp(AtlasFrameParameterSetRbsp& afps,
                                  AtlasBitstream&   atlas,
                                  vmesh::Bitstream&           bitstream);

  // 8.3.6.2.2 Atlas frame tile information syntax
  void atlasFrameTileInformation(AtlasFrameTileInformation&     afti,
                                 AtlasSequenceParameterSetRbsp& asps,
                                 vmesh::Bitstream&              bitstream);

  // 8.3.6.2.3 Atlas frame parameter set V-DMC extension syntax
  void afpsVdmcExtension(vmesh::Bitstream&              bitstream,
                         AtlasSequenceParameterSetRbsp& asps,
                         AtlasFrameParameterSetRbsp&    afps,
                         AfpsVdmcExtension&             afve);

  // 8.3.6.2.4	Atlas frame tile attribute information syntax
  void
  atlasFrameTileAttributeInformation(vmesh::Bitstream&          bitstream,
                                     uint32_t startId, uint32_t startIdx,
                                     AtlasFrameTileAttributeInformation& aftai,
                                     int32_t                        attrIdx,
                                     AtlasSequenceParameterSetRbsp& asps);

  // 8.3.6.2.5 Atlas frame mesh information syntax
  void atlasFrameMeshInformation(AtlasFrameMeshInformation& afmi,
                                 vmesh::Bitstream&          bitstream);

  // 8.3.6.2 Atlas adaptation parameter set RBSP syntax
  void atlasAdaptationParameterSetRbsp(AtlasAdaptationParameterSetRbsp& aaps,
                                       vmesh::Bitstream& bitstream);

  // 8.3.6.4  Supplemental enhancement information Rbsp
  void seiRbsp(AtlasBitstream&   atlas,
               vmesh::Bitstream& bitstream,
               AtlasNalUnitType  nalUnitType,
               PCCSEI&           sei);

  // 8.3.6.5  Access unit delimiter Rbsp syntax
  void accessUnitDelimiterRbsp(vmesh::AccessUnitDelimiterRbsp& aud,
                               AtlasBitstream&   atlas,
                               vmesh::Bitstream&               bitstream);

  // 8.3.6.6  End of sequence Rbsp syntax
  void endOfSequenceRbsp(vmesh::EndOfSequenceRbsp& eosbsp,
                         AtlasBitstream&   atlas,
                         vmesh::Bitstream&         bitstream);

  // 8.3.6.7  End of bitstream Rbsp syntax
  void endOfAtlasSubBitstreamRbsp(vmesh::EndOfAtlasSubBitstreamRbsp& eoasb,
                                  AtlasBitstream&   atlas,
                                  vmesh::Bitstream& bitstream);

  // 8.3.6.8  Filler data Rbsp syntax
  void fillerDataRbsp(vmesh::FillerDataRbsp& fdrbsp,
                      AtlasBitstream&          atlas,
                      vmesh::Bitstream&      bitstream);

  // 8.3.6.9 Atlas tile group layer Rbsp syntax
  void atlasTileLayerRbsp(AtlasTileLayerRbsp& atgl,
                          AtlasBitstream&     atlas,
                          AtlasNalUnitType    nalUnitType,
                          vmesh::Bitstream&   bitstream);

  // 8.3.6.10 RBSP trailing bit syntax
  void rbspTrailingBits(vmesh::Bitstream& bitstream);

  // 8.3.6.11  Atlas tile group header syntax
  void atlasTileHeader(AtlasTileHeader&  ath,
                       AtlasBitstream&   atlas,
                       AtlasNalUnitType  nalUnitType,
                       vmesh::Bitstream& bitstream);

  // 8.3.6.12  Reference list structure syntax
  void refListStruct(RefListStruct&                 rls,
                     AtlasSequenceParameterSetRbsp& asps,
                     vmesh::Bitstream&              bitstream);

  // 8.3.7	Atlas tile data unit syntax
  // 8.3.7.1	General atlas tile data unit syntax
  void vdmcAtlasTileDataUnit(AtlasTileDataUnit& atdu,
                         AtlasTileHeader&   ath,
                         AtlasBitstream&   atlas,
                         vmesh::Bitstream&  bitstream);

  // 8.3.7.2  Patch information data syntax
  void patchInformationData(PatchInformationData& pid,
                            size_t                patchMode,
                            AtlasTileHeader&      ath,
                            AtlasBitstream&   atlas,
                            vmesh::Bitstream&     bitstream);

  // 8.3.7.3  Patch data unit syntax
  void meshpatchDataUnit(MeshpatchDataUnit& pdu,
                         AtlasTileHeader&   ath,
                         AtlasBitstream&   atlas,
                         size_t             patchMode,
                         vmesh::Bitstream&  bitstream);

  // 8.3.7.4  Skip patch data unit syntax
  void skipMeshpatchDataUnit(vmesh::Bitstream& bitstream);

  // 8.3.7.5  Merge patch data unit syntax
  void mergeMeshpatchDataUnit(MergeMeshpatchDataUnit& impdu,
                              AtlasTileHeader&        ath,
                              AtlasBitstream&   atlas,
                              size_t                  patchMode,
                              vmesh::Bitstream&       bitstream);
  // 8.3.7.6  Inter patch data unit syntax
  void interMeshpatchDataUnit(InterMeshpatchDataUnit& pdu,
                              AtlasTileHeader&        ath,
                              AtlasBitstream&   atlas,
                              size_t                  patchMode,
                              vmesh::Bitstream&       bitstream);
  // 8.3.7.7  Raw patch data unit syntax
  // 8.3.7.8  EOM patch data unit syntax
  // 8.3.7.9  Point local reconstruction data syntax

  // 8.3.7.11 Texture projection information syntax
  void textureProjectionInformation(TextureProjectionInformation&  tpi,
                                    AtlasSequenceParameterSetRbsp& asps,
                                    vmesh::Bitstream&              bitstream);
  // 8.3.7.XX	Texture projection inter information syntax
  void
  textureProjectionInterInformation(TextureProjectionInterInformation& tpii,
                                    AtlasSequenceParameterSetRbsp&     asps,
                                    vmesh::Bitstream& bitstream);
  // 8.3.7.XX	Texture projection merge information syntax
  void
  textureProjectionMergeInformation(TextureProjectionMergeInformation& tpmi,
                                    AtlasSequenceParameterSetRbsp&     asps,
                                    vmesh::Bitstream& bitstream);
  // 8.3.7.12 Sub-patch information syntax
  void subpatchInformation(SubpatchInformation&           si,
                           AtlasSequenceParameterSetRbsp& asps,
                           vmesh::Bitstream&              bitstream);
  //8.3.7.XX    Sub-patch raw information syntax
  void subpatchRawInformation(SubpatchRawInformation&        sri,
                              AtlasSequenceParameterSetRbsp& asps,
                              vmesh::Bitstream&              bitstream);
  // 8.3.7.XX	Sub-patch inter information syntax
  void subpatchInterInformation(SubpatchInterInformation&      sii,
                                AtlasSequenceParameterSetRbsp& asps,
                                vmesh::Bitstream&              bitstream);
  // 8.3.7.XX	Sub-patch merge information syntax
  void subpatchMergeInformation(SubpatchMergeInformation&      smi,
                                AtlasSequenceParameterSetRbsp& asps,
                                vmesh::Bitstream&              bitstream);

  // 8.3.8 Supplemental enhancement information message syntax
  void seiMessage(vmesh::Bitstream& bitstream,
                  AtlasBitstream&   atlas,
                  AtlasNalUnitType  nalUnitType,
                  PCCSEI&           sei);

  // D.2 Sample stream NAL unit syntax and semantics
  // D.2.1 Sample stream NAL header syntax
  void sampleStreamNalHeader(vmesh::Bitstream&           bitstream,
                             vmesh::SampleStreamNalUnit& ssnu);

  // F.2  SEI payload syntax
  // ISO/IEC 23002-5:F.2.1  General SEI message syntax
  void seiPayload(vmesh::Bitstream& bitstream,
                  AtlasBitstream&   atlas,
                  AtlasNalUnitType  nalUnitType,
                  SeiPayloadType    payloadType,
                  size_t            payloadSize,
                  PCCSEI&           sei);

  // ISO/IEC 23002-7   Filler payload SEI message syntax
  void
  fillerPayload(vmesh::Bitstream& bitstream, SEI& sei, size_t payloadSize);

  // ISO/IEC 23002-7   User data registered by Recommendation ITU-T T.35 SEI message syntax
  void userDataRegisteredItuTT35(vmesh::Bitstream& bitstream,
                                 SEI&              sei,
                                 size_t            payloadSize);

  // ISO/IEC 23002-7   User data unregistered SEI message syntax
  void userDataUnregistered(vmesh::Bitstream& bitstream,
                            SEI&              sei,
                            size_t            payloadSize);

  // ISO/IEC 23002-5:F.2.2  Recovery point SEI message syntax
  void recoveryPoint(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.3  No reconstruction SEI message syntax
  void noReconstruction(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-7   Reserved message syntax
  void
  reservedMessage(vmesh::Bitstream& bitstream, SEI& sei, size_t payloadSize);

  // ISO/IEC 23002-5:F.2.4  SEI manifest SEI message syntax
  void seiManifest(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.5  SEI prefix indication SEI message syntax
  void seiPrefixIndication(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.6  Active substreams SEI message syntax
  void activeSubBitstreams(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.7  Component codec mapping SEI message syntax
  void componentCodecMapping(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.8  Volumetric annotation SEI message family syntax
  // ISO/IEC 23002-5:F.2.8.1 Scene object information SEI message syntax
  void sceneObjectInformation(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.8.2 Object label information SEI message syntax
  void objectLabelInformation(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.8.3 Patch information SEI message syntax
  void patchInformation(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.8.4 Volumetric rectangle information SEI message syntax
  void volumetricRectangleInformation(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.8.5 Atlas object information  SEI message syntax
  void atlasObjectInformation(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.9  Buffering period SEI message syntax
  void bufferingPeriod(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:F.2.10  Atlas frame timing SEI message syntax
  void atlasFrameTiming(vmesh::Bitstream& bitstream,
                        SEI&              sei,
                        SEI&              seiBufferingPeriodAbstract,
                        bool              cabDabDelaysPresentFlag);

  // ISO/IEC 23002-5:F.2.11   Viewport SEI messages family syntax
  // ISO/IEC 23002-5:F.2.11.1 Viewport camera parameters SEI messages syntax
  void viewportCameraParameters(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.11.2	Viewport position SEI messages syntax
  void viewportPosition(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.12 Decoded Atlas Information Hash SEI message syntax
  void decodedAtlasInformationHash(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbstract);

  // ISO/IEC 23002-5:F.2.12.1 Decoded high-level hash unit syntax
  void decodedHighLevelHash(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.12.2 Decoded atlas hash unit syntax
  void decodedAtlasHash(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.12.3 Decoded atlas b2p hash unit syntax
  void decodedAtlasB2pHash(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:F.2.12.4 Decoded atlas tiles hash unit syntax
  void decodedAtlasTilesHash(vmesh::Bitstream& bitstream,
                             SEI&              seiAbstract,
                             size_t            id);

  // ISO/IEC 23002-5:F.2.12.5 Decoded atlas tile b2p hash unit syntax
  void decodedAtlasTilesB2pHash(vmesh::Bitstream& bitstream,
                                SEI&              seiAbstract,
                                size_t            id);

  // ISO/IEC 23002-5:F.2.13 Time code SEI message syntax
  void timeCode(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:H.20.2.13 Attribute transformation parameters SEI message syntax
  void attributeTransformationParams(vmesh::Bitstream& bitstream, SEI& sei);

  // ISO/IEC 23002-5:H.20.2.14 Occupancy synthesis SEI message syntax
  void occupancySynthesis(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:H.20.2.15 Geometry smoothing SEI message syntax
  void geometrySmoothing(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:H.20.2.16 Attribute smoothing SEI message syntax
  void attributeSmoothing(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // ISO/IEC 23002-5:H.20.2.17 V-PCC registered SEI message syntax
  void vpccRegisteredSEI(vmesh::Bitstream&  bitstream,
                         SEI&               seiAbstract,
                         VpccSeiPayloadType vpccPayloadType,
                         int32_t            payloadSize);

  // ISO/IEC 23002-5:H.20.2.18 V-PCC registered SEI payload syntax
  void vpccRegisteredSeiPayload(vmesh::Bitstream&  bitstream,
                                VpccSeiPayloadType vpccPayloadType,
                                size_t             payloadSize,
                                SEI&               seiAbstract);

  // F.2.1 V-DMC registered SEI message syntax
  void vdmcRegisteredSEI(vmesh::Bitstream&  bitstream,
                         SEI&               seiAbstract,
                         VdmcSeiPayloadType vdmcPayloadType,
                         int32_t            payloadSize);

  // F.2.2 V-DMC registered SEI payload syntax
  void vdmcRegisteredSeiPayload(vmesh::Bitstream&  bitstream,
                                VdmcSeiPayloadType vdmcPayloadType,
                                size_t             payloadSize,
                                SEI&               seiAbstract);

  // F.2.3 Zippering SEI message syntax
  void zippering(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // F.2.4 Submesh SOI relationship indication SEI message syntax
  void submeshSOIIndicationRelationship(vmesh::Bitstream& bitstream,
                                        SEI&              seiAbstract);

  // F.2.5 Submesh distortion indication SEI message syntax
  void submeshDistortionIndication(vmesh::Bitstream& bitstream,
                                   SEI&              seiAbstract);

  // F.2.6 LoD extraction information SEI message syntax
  void LoDExtractionInformation(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // F.2.7 Tile submesh mapping SEI message syntax
  void tileSubmeshMapping(vmesh::Bitstream& bitstream, SEI& seiAbstract);

  // F.2.8 Attribute extraction information SEI message syntax
  void AttributeExtractionInformation(vmesh::Bitstream& bitstream,
                                      SEI&              seiAbstract);

  // G.2  VUI syntax
  // G.2.1  VUI parameters syntax
  void vuiParameters(vmesh::Bitstream& bitstream, VUIParameters& vp);

  // G.2.2  HRD parameters syntax
  void hrdParameters(vmesh::Bitstream& bitstream, HrdParameters& hp);
  // G.2.3  Sub-layer HRD parameters syntax
  void hrdSubLayerParameters(vmesh::Bitstream&      bitstream,
                             HrdSubLayerParameters& hlsp,
                             size_t                 cabCnt);
  // G.2.4 Maximum coded video resolution syntax
  void maxCodedVideoResolution(vmesh::Bitstream&        bitstream,
                               MaxCodedVideoResolution& mcvr);
  // G.2.5	Coordinate system parameters syntax
  void coordinateSystemParameters(vmesh::Bitstream&           bitstream,
                                  CoordinateSystemParameters& csp);

  // G 2.6 V-DMC VUI extension
  void vdmcVuiParameters(vmesh::Bitstream&  bitstream,
                         VdmcVuiParameters& vp,
                         size_t             numAttributesVideo);

  // H.7.3.4.1	VPS V-PCC extension syntax
  //void vpsVpccExtension(vmesh::Bitstream& bitstream, VpsVpccExtension& ext);

  // H.7.3.6.1.1	ASPS V-PCC extension syntax
  void aspsVpccExtension(vmesh::Bitstream&              bitstream,
                         AtlasSequenceParameterSetRbsp& asps,
                         AspsVpccExtension&             ext);

  // H.7.3.6.1.2 ASPS MIV extension syntax
  void aspsMivExtension(vmesh::Bitstream& bitstream);

  // H.7.3.6.2.1	AFPS V-PCC extension syntax
  void afpsVpccExtension(vmesh::Bitstream& bitstream, AfpsVpccExtension& ext);

  // H.7.3.6.2.1	AAPS V-PCC extension syntax
  void aapsVpccExtension(vmesh::Bitstream& bitstream, AapsVpccExtension& ext);

  // H.7.3.6.2.2	Atlas camera parameters syntax
  void atlasCameraParameters(vmesh::Bitstream&      bitstream,
                             AtlasCameraParameters& acp);

  // ISO/IEC 23090-29:H.14.1.10 Basemesh Attribute transformation parameters SEI message syntax
  void baseMeshAttributeTransformationParams(vmesh::Bitstream& bitstream,
                                             SEI&              seiAbstract);

  int32_t prevPatchSizeU_ = 0;
  int32_t prevPatchSizeV_ = 0;
  int32_t predPatchIndex_ = 0;
  int32_t prevFrameIndex_ = 0;

#if defined(BITSTREAM_TRACE)
  vmesh::Logger* logger_ = nullptr;
#endif
};

};  // namespace atlas
