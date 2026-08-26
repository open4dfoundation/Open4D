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

#include "atlasBitstream.hpp"
#include "atlasTileStruct.hpp"
#include "vmc.hpp"
#include "util/checksum.hpp"
#include "../vmeshEncoder/geometryParametrization.hpp"

const int ATLAS_MAX_GOP = 64;

//============================================================================

namespace atlas {

class AtlasEncoderParameters {
public:
  AtlasEncoderParameters() {}
  ~AtlasEncoderParameters(){};

  bool                         encodeTextureVideo = true;
  vmesh::DisplacementCoordinateSystem displacementCoordinateSystem =
    vmesh::DisplacementCoordinateSystem::LOCAL;
  bool                              bFaceIdPresentFlag                 = false;
  bool                              increaseTopSubmeshSubdivisionCount = false;
  std::vector<uint32_t>             submeshIdList;
  int32_t                           bitDepthPosition                 = 12;
  int32_t                           bitDepthTexCoord                 = 12;
  uint32_t                          log2GeometryVideoBlockSize       = 6;
  int32_t                           subdivisionIterationCount        = 3;
  bool                              interpolateSubdividedNormalsFlag = false;
  vmesh::GeometryParametrizationParameters intraGeoParams;
  bool                              use45DegreeProjection          = false;
  int32_t                           subdivisionEdgeLengthThreshold = 0;
  int32_t transformMethod = 1;  // 0: None Transform, 1: Linear Lifting m68642
  bool    applyOneDimensionalDisplacement                 = true;
  bool    lodDisplacementQuantizationFlag                 = false;
  int32_t bitDepthOffset                                  = 0;
  std::vector<uint32_t> log2LevelOfDetailInverseScale     = {1, 1, 1};
  std::vector<uint32_t> liftingQP                         = {16, 28, 28};
  std::vector<std::array<int32_t, 3>> qpPerLevelOfDetails = {{16, 28, 28},
                                                             {22, 34, 34},
                                                             {28, 40, 40}};
  vmesh::DisplacementQuantizationType        displacementQuantizationType =
    vmesh::DisplacementQuantizationType::DEFAULT;
  bool IQSkipFlag                      = false;
  bool InverseQuantizationOffsetFlag   = true;
  bool liftingSkipUpdate               = false;
  bool liftingAdaptiveUpdateWeightFlag = true;
  bool liftingValenceUpdateWeightFlag  = true;

  std::vector<uint32_t> liftingUpdateWeightNumerator         = {63, 21, 7, 1};
  std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1 = {79, 19, 4, 0};

  std::vector<uint32_t> liftingPredictionWeightNumerator = {1, 1, 1, 1};
  std::vector<uint32_t> liftingPredictionWeightDenominatorMinus1 = {3,
                                                                    1,
                                                                    1,
                                                                    1};

  std::vector<uint32_t> liftingPredictionWeightNumeratorDefault = {1, 1, 1, 1};
  std::vector<uint32_t> liftingPredictionWeightDenominatorMinus1Default = {1,
                                                                           1,
                                                                           1,
                                                                           1};

  bool liftingAdaptivePredictionWeightFlag = false;

  int32_t encodeDisplacementType = 2;

  bool applyLiftingOffset = true;

  bool     dirlift                = false;
  uint32_t dirliftScale1          = 950;
  uint32_t dirliftDeltaScale2     = 25;
  uint32_t dirliftDeltaScale3     = 20;
  uint32_t dirliftScaleDenoMinus1 = 999;

  bool displacementReversePacking = true;

  int iDeriveTextCoordFromPos =
    2;  //0 (disabled), 1 (using UV coords), 2 (using FaceId, DEFAULT), 3 (using Connected Components)

  double  packingScaling       = 0.95;
  bool    biasEnableFlag       = false;
  bool    useRawUV             = false;
  int     rawTextcoordBitdepth = 6;
  int32_t numSubmesh           = 1;
  bool    enableSignalledIds   = false;

  int32_t texturePackingWidth  = -1;
  int32_t texturePackingHeight = -1;
  float   gutter               = 2.F;

  int32_t                            numTilesGeometry  = 1;
  int32_t                            numTilesAttribute = 1;
  std::vector<uint32_t>              tileIdList;
  std::vector<std::vector<uint32_t>> submeshIdsInTile;

  bool lodPatchesEnable = false;

  int32_t maxNumRefBmeshList  = 1;
  int32_t maxNumRefBmeshFrame = 4;

  std::vector<int> refFrameDiff = {1, 2, 3, 4};

  bool analyzeGof = false;

  std::vector<vmesh::BaseMeshGOPEntry> basemeshGOPList =
    std::vector<vmesh::BaseMeshGOPEntry>(ATLAS_MAX_GOP);

  int     packingType           = 0;
  int32_t referenceAttributeIdx = 0;

  bool texturePlacementPerPatch = true;
};

//============================================================================

class AtlasEncoder {
public:
  AtlasEncoder() {}
  AtlasEncoder(const AtlasEncoder& rhs)            = delete;
  AtlasEncoder& operator=(const AtlasEncoder& rhs) = delete;
  ~AtlasEncoder()                                  = default;

  int32_t initializeAtlasParameterSets(AtlasBitstream& adStream,
                                       const AtlasEncoderParameters& params);

  void compressAtlas(
    AtlasBitstream&                             adStream,
    std::vector<std::vector<AtlasTile>>&        tileAreasInVideo_,
    std::vector<std::vector<AtlasTile>>&        reconAtlasTiles_,
    std::vector<std::vector<vmesh::VMCSubmesh>>&       encFrames,
    const AtlasEncoderParameters&               params,
    std::pair<int32_t, int32_t>&                geometryVideoSize,
    std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_);


  void setsubmeshIdtoIndex(std::vector<int32_t>& submeshIdtoIndex) {
    submeshIdtoIndex_ = submeshIdtoIndex;
  }

  std::pair<int32_t, int32_t>
  tilePatchOfSubmesh(int32_t                       submeshIndex,
                     const AtlasEncoderParameters& encParams);

private:

  void initializeAtlasTileLayer(AtlasBitstream& adStream,
                                int32_t         frameCount,
                                const AtlasEncoderParameters& params);

  int32_t createAtlasPatchInformation(
    bool                                 geometryPatchFlag,
    vmesh::VMCSubmesh&                          submesh,
    std::vector<AtlasTile>&              referenceFrames,
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    int32_t                              submeshId,
    int32_t                              referenceFrameIndex,
    int32_t                              referencePatchIndex,
    int32_t                              predictorPatchIndex,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    AtlasTileLayerRbsp&                  atl,
    PatchInformationData&                pid,
    std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
    const AtlasEncoderParameters&        params);

  int32_t createAtlasMeshPatchInformation(
    vmesh::VMCSubmesh&                          submesh,
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    int32_t                              submeshId,
    int32_t                              referencePatchIndex,
    int32_t                              predictorPatchIndex,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    AtlasTileLayerRbsp&                  atl,
    PatchInformationData&                pid,
    std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
    const AtlasEncoderParameters&        params);

  int32_t createAtlasMergePatchInformation(
    vmesh::VMCSubmesh&                          submesh,
    std::vector<AtlasTile>&              referenceFrames,
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    int32_t                              submeshId,
    int32_t                              referenceFrameIndex,
    int32_t                              referencePatchIndex,
    int32_t                              predictorPatchIndex,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    AtlasTileLayerRbsp&                  atl,
    PatchInformationData&                pid,
    std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
    const AtlasEncoderParameters&        params);

  int32_t createAtlasInterPatchInformation(
    vmesh::VMCSubmesh&                          submesh,
    std::vector<AtlasTile>&              referenceFrames,
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    int32_t                              submeshId,
    int32_t                              referenceFrameIndex,
    int32_t                              referencePatchIndex,
    int32_t                              predictorPatchIndex,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    AtlasTileLayerRbsp&                  atl,
    PatchInformationData&                pid,
    std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
    const AtlasEncoderParameters&        params);

  int32_t reconstructAtlasPatchIntra(
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    PatchInformationData&                pi,
    AtlasTileLayerRbsp&                  atl,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
    int32_t                              predictorIdx);

  int32_t reconstructAtlasPatchInter(
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    PatchInformationData&                pi,
    AtlasTileLayerRbsp&                  atl,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
    int32_t                              predictorIdx);

  int32_t reconstructAtlasPatchMerge(
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    PatchInformationData&                pi,
    AtlasTileLayerRbsp&                  atl,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
    int32_t                              predictorIdx);

  int32_t reconstructAtlasPatchSkip(
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    PatchInformationData&                pi,
    AtlasTileLayerRbsp&                  atl,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
    int32_t                              predictorIdx);

  int32_t reconstructAtlasPatchAttIntra(
    int32_t                              frameIndex,
    int32_t                              tileIndex,
    int32_t                              patchIndex,
    PatchInformationData&                pi,
    AtlasTileLayerRbsp&                  atl,
    AtlasSequenceParameterSetRbsp&       asps,
    AtlasFrameParameterSetRbsp&          afps,
    std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
    int32_t                              predictorIdx);

  int32_t
  reconstructAtlasPatch(int32_t                              frameIndex,
                        int32_t                              tileIndex,
                        int32_t                              patchIndex,
                        PatchInformationData&                pi,
                        AtlasTileLayerRbsp&                  atl,
                        AtlasSequenceParameterSetRbsp&       asps,
                        AtlasFrameParameterSetRbsp&          afps,
                        std::vector<std::vector<AtlasTile>>& reconAtlasTiles_,
                        int32_t                              predictorIdx);
  public:
  static void reconstructTileInformation(std::vector<imageArea>&     tileArea,
                                  int32_t                     attrIndex,
                                  AtlasFrameParameterSetRbsp& afps,
                                  const AtlasSequenceParameterSetRbsp& asps);
  private:
  void setAtlasTileInformation(
    AtlasFrameParameterSetRbsp&          afps,
    AtlasSequenceParameterSetRbsp&       asps,
    std::vector<std::vector<AtlasTile>>& tileAreasInVideo,
    std::pair<int32_t, int32_t>&         geometryVideoSize,
    const AtlasEncoderParameters&        params);

  void setAtlasTileAttributeInformation(
    int32_t                                     attrIndex,
    AtlasFrameParameterSetRbsp&                 afps,
    AtlasSequenceParameterSetRbsp&              asps,
    std::vector<std::vector<AtlasTile>>&        tileAreasInVideo,
    std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_,
    const AtlasEncoderParameters&               params);

  void constructAtlasHeaderRefListStruct(AtlasBitstream&     adStream,
                                         AtlasTileLayerRbsp& atl,
                                         int32_t             frameIndex);

  bool createAtlasMeshSubPatch(vmesh::VMCSubmesh&                   encFrame,
                               int32_t                       submeshPos,
                               int32_t                       frameIndex,
                               TextureProjectionInformation& tpi,
                               int32_t                       qpTexCoord,
                               double                        packingScaling,
                               bool use45DegreeProjection,
                               bool bFaceIdPresentFlag);

  bool createAtlasMeshSubPatchInter(vmesh::VMCSubmesh& encFrame,
                                    int32_t     submeshPos,
                                    int32_t     frameIndex,
                                    TextureProjectionInterInformation& tpii,
                                    double      packingScaling,
                                    AtlasPatch& refFrame,
                                    bool        enableSubpatchInter,
                                    bool        use45DegreeProjection,
                                    bool        bFaceIdPresentFlag);

  bool createAtlasMeshSubPatchMerge(vmesh::VMCSubmesh& encFrame,
                                    int32_t     submeshPos,
                                    int32_t     frameIndex,
                                    TextureProjectionMergeInformation& tpmi,
                                    double      packingScaling,
                                    AtlasPatch& refFrame);

  bool reconstructTextureProjectionInformation(
    const TextureProjectionInformation& tpi,
    double                              aspsScale,
    int32_t                             qpTexCoord,
    bool                                extendedProjectionEnabledFlag,
    AtlasPatch&                         reconAtlasPatch);

  bool reconstructTextureProjectionInterInformation(
    const TextureProjectionInterInformation& tpii,
    double                                   aspsScale,
    int32_t                                  qpTexCoord,
    bool                                     extendedProjectionEnabledFlag,
    AtlasPatch&                              reconAtlasPatch,
    AtlasPatch&                              reconAtlasPatchRef);

  bool reconstructTextureProjectionMergeInformation(
    const TextureProjectionMergeInformation& tpmi,
    double                                   aspsScale,
    AtlasPatch&                              reconAtlasPatch,
    AtlasPatch&                              reconAtlasPatchRef);

  void checkAttributeTilesConsistency(
    AfpsVdmcExtension&                          afve,
    AtlasSequenceParameterSetRbsp&              asps,
    int32_t                                     refAttrIdx,
    std::vector<std::vector<AtlasTile>>&        tileAreasInVideo_,
    std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize_);

  std::vector<int32_t> submeshIdtoIndex_;
};

//============================================================================

}  // namespace vmesh
