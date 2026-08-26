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

#pragma once

#include "atlasTileStruct.hpp"
#include "atlasBitstream.hpp"

using namespace atlas;
class AtlasDataDecoder {
public:
  AtlasDataDecoder() {}
  ~AtlasDataDecoder() {}
  void setAtlasDataSubBitstream();

  void decodeAtlasDataSubbitstream(AtlasBitstream& adStream, bool checksum);
  std::vector<std::vector<AtlasTile>>& getDecodedAtlasFrames() {
    return decodedTiles_;
  }
  uint32_t               getMaxTileCount() { return maxTileCount; }
  int32_t                getAtlasFrameCount() { return atlasFrameCount; }
  std::vector<uint32_t>& getADTileIdtoIndex() { return tileIdtoIndex_; }
  std::vector<uint32_t>& getADSubmeshIdtoIndex() { return submeshIdtoIndex_; }

private:
  bool
  decodeTextureProjectionInformation(const TextureProjectionInformation&  tpi,
                                     const AtlasSequenceParameterSetRbsp& asps,
                                     int32_t   submeshIndex,
                                     int32_t   frameIndex,
                                     AtlasPatch& patch);
  bool decodeTextureProjectionInterInformation(
    const TextureProjectionInterInformation& tpii,
    const AtlasSequenceParameterSetRbsp&     asps,
    int32_t                                  submeshIndex,
    int32_t                                  frameIndex,
    AtlasPatch&                                patch,
    AtlasPatch&                                refPatch);
  bool decodeTextureProjectionMergeInformation(
    const TextureProjectionMergeInformation& tpmi,
    int32_t                                  submeshIndex,
    int32_t                                  frameIndex,
    AtlasPatch&                                patch,
    AtlasPatch&                                refPatch);
  //  int32_t reconstructBaseMeshUvCoordinates(TriangleMesh<MeshType>& base,
  //                                           int32_t     submeshIndex,
  //                                           int32_t     frameIndex,
  //                                           const V3CParameterSet& vps,
  //                                           AtlasSequenceParameterSetRbsp& asps,
  //                                           AtlasFrameParameterSetRbsp& afps,
  //                                           VMCPatch&   decodedAtlasPatch);
  void decompressTileInformation(std::vector<imageArea>&    tileArea,
                                 int32_t                    attrIndex,
                                 AtlasFrameParameterSetRbsp& afps,
                                 const AtlasSequenceParameterSetRbsp& asps);
  void
  decompressConsistentAttributeTile(int32_t                     attrIndex,
                                    int32_t                     refIndex,
                                    AtlasFrameParameterSetRbsp& afps,
                                    const AtlasSequenceParameterSetRbsp& asps);

  bool decompressAtlasPatch(uint32_t                 atlPos,
                            const AtlasBitstream& adStream,
                            //const V3CParameterSet&                 vps,
                            int32_t               frameIndex,
                            int32_t               tileIndex,
                            AtlasTileLayerRbsp&   atl,
                            std::vector<AtlasTile>& referenceFrameList);
  void decodeLiftingTransformParameters(
    vmesh::LiftingTransformParameters&           transformParameters,
    int                                   subdivIterationCount,
    bool                                  enableLiftingOffsetFlag,
    const VdmcLiftingTransformParameters& ltp,
    const VdmcLiftingTransformParameters& higherltp);
  void decodeQuantizationParameters(vmesh::QuantizationParameters& qParameters,
                                    int                     qpIdx,
                                    const VdmcQuantizationParameters& vdmcQp,
                                    int32_t                           lodCount,
                                    bool                              qpEnable,
                                    const AtlasSequenceParameterSetRbsp& asps,
                                    const AtlasFrameParameterSetRbsp&    afps);
  int32_t
          createAspsReferenceLists(const AtlasSequenceParameterSetRbsp& asps,
                                   std::vector<std::vector<int32_t>>& aspsRefDiffList);
  int32_t createAthReferenceList(const AtlasSequenceParameterSetRbsp& asps,
                                 AtlasTileLayerRbsp&                  tile,
                                 std::vector<int32_t>& referenceList);
  size_t  calcTotalAtlasFrameCount(AtlasBitstream& adStream);
  size_t  calculateAFOCval(AtlasBitstream&               adStream,
                           std::vector<AtlasTileLayerRbsp>& atglList,
                           size_t                           atglOrder);

  std::vector<std::vector<std::vector<int32_t>>>
    aspsSequenceReferenceFrameIndexDiffList_;  //[aspsIndex][listIdx][refIdx] 1,2,3,4
  std::vector<vmesh::QuantizationParameters> aspsLodQuantizationParametersList_;
  std::vector<vmesh::QuantizationParameters> afpsLodQuantizationParametersList_;
  std::vector<uint32_t>               tileIdtoIndex_;     //orange -> 0
  std::vector<uint32_t>               submeshIdtoIndex_;  //orange -> 0

  //output of atlasdata sub-bitstream
  std::vector<std::vector<AtlasTile>> decodedTiles_;  //[tiles][frame]
  int32_t                           atlasFrameCount;
  uint32_t                          maxTileCount;
};
