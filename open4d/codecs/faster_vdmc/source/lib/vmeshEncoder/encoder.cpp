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

#include "encoder.hpp"

#include <cmath>
#include <array>
#include <iostream>

#include "vmc.hpp"
#include "geometryParametrization.hpp"
#include "transferColor.hpp"
#include "util/misc.hpp"
#include "colourConverter.hpp"

#include "util/mesh.hpp"

namespace vmesh {

//============================================================================
int32_t
getPatchIdx(const AtlasTile& tile, uint32_t submeshId, uint32_t lodIdx) {
  uint32_t patchIdx   = -1;
  uint32_t patchCount = tile.patches_.size();
  for (uint32_t i = 0; i < patchCount; i++) {
    uint32_t patchSubmeshId = tile.patches_[i].submeshId_;
    uint32_t patchlodIdx    = 0;
    patchlodIdx             = tile.patches_[i].lodIdx_;
    if (submeshId == patchSubmeshId && lodIdx == patchlodIdx) {
      patchIdx = i;
      break;
    }
  }
  return patchIdx;
}

//============================================================================
int32_t
setV3CParameterSet(V3CParameterSet&            vps,
                   int32_t                     atlasCount,
                   uint16_t                    atlasId,
                   uint32_t                    v3cParameterSetId,
                   const VMCEncoderParameters& params) {
  uint16_t frameWidth                    = 0;  //geometryWidth
  uint16_t frameHeight                   = 0;  //geometryHeight
  uint16_t avgFrameRate                  = 30;
  uint32_t mapCountMinus1                = 0;
  bool     multipleMapStreamsPresentFlag = 0;
  bool     auxiliaryVideoPresentFlag     = 0;
  bool     occupancyVideoPresentFlag     = 0;
  bool     geometryVideoPresentFlag      = params.encodeDisplacementType == 2;
  bool     attributeVideoPresentFlag     = params.encodeTextureVideo;
  bool     packedVideoPresentFlag        = params.jointTextDisp;

  int16_t atlasIndex = -1;
  for (atlasIndex = 0; atlasIndex < vps.getAtlasCountMinus1(); atlasIndex++) {
    if (vps.getAtlasId(atlasIndex) == atlasId) break;
  }
  if (atlasIndex < 0) {
    std::cout << "atlasId" << atlasId << " is not found in the list\n";
  }

  vps.init(v3cParameterSetId,
           atlasCount,
           atlasIndex,
           frameWidth,
           frameHeight,
           avgFrameRate,
           mapCountMinus1,
           multipleMapStreamsPresentFlag,
           auxiliaryVideoPresentFlag,
           occupancyVideoPresentFlag,
           geometryVideoPresentFlag,
           attributeVideoPresentFlag,
           packedVideoPresentFlag);

  //======================================================================
  //profileTierLevel Codec GroupIdc : ISO/IEC 23090-5:2023(E) TableA-1
  //TODO: [profilecodec_idc]
  vps.getProfileTierLevel().setPtlVideoCodecGroupIdc(
    params.profileCodecGroupId);

  if (params.encodeDisplacementType == 1)  //displacement codec
  {
    // BMS Main, DAC Main
    vps.getProfileTierLevel().setPtlNonVideoCodecGroupIdc(
      NonVideoCodecGroupIdc::BMS_Main_DAC_Main);
  } else {
    // BMS Main
    vps.getProfileTierLevel().setPtlNonVideoCodecGroupIdc(
      NonVideoCodecGroupIdc::BMS_Main);
  }

  // Geometry information
  auto& gi                         = vps.getGeometryInformation(atlasIndex);
  gi.getGeometry2dBitdepthMinus1() = params.geometryVideoBitDepth - 1;
  gi.setGeometry3dCoordinatesBitdepthMinus1(params.bitDepthPosition - 1);
  gi.getGeometryCodecId() = params.geometryCodecId;

  // Attribute information
  auto& ai = vps.getAttributeInformation(atlasIndex);
  if (params.videoAttributeCount > 0 && params.encodeTextureVideo) {
    //TODO:  it cannot signal multiple files with multiple attributes
    if (params.numTextures > 1) {
      int numAttributesTexture = 1;
      if (params.dracoMeshLossless && (params.numTextures > 1))
        numAttributesTexture =
          params
            .numTextures;  // for now, only for the lossless condition we will encode textures separately
      uint8_t attributeCount =
        params.encodeTextureVideo * numAttributesTexture;
      if (attributeCount > 0 && params.encodeTextureVideo) {
        ai.allocate(attributeCount);
        for (auto j = 0; j < attributeCount; j++) {
          //TODO: is it 0 or j?
          ai.getAttribute2dBitdepthMinus1(j) =
            params.attributeParameters[0].textureInputBitDepth - 1;
          if (vps.getProfileTierLevel().getPtlVideoCodecGroupIdc() == 15)
            ai.getAttributeCodecId(j) =
              params.attributeParameters[0].textureCodecId;
          else ai.getAttributeCodecId(j) = 0;
          ai.getAttributeTypeId(j) = 0;  // ATTR_TEXTURE
        }
      }
    } else {
      ai.allocate(params.videoAttributeCount);
      for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
        ai.getAttribute2dBitdepthMinus1(attIdx) =
          params.attributeParameters[attIdx].textureInputBitDepth - 1;
        ai.getAttributeCodecId(attIdx) =
          params.attributeParameters[attIdx].textureCodecId;
        ai.getAttributeTypeId(attIdx) = params.videoAttributeTypes[attIdx];
      }
    }
  }

  // Packed information
  vps.getVpsPackedVideoExtension().setPackedVideoPresentFlag(0, 0);
  if (params.jointTextDisp) {
    int attCnt = params.encodeTextureVideo ? 1 : 0;
    int geoCnt = (params.encodeDisplacementType == 2) ? 1 : 0;
    vps.getVpsPackedVideoExtension().setPackedVideoPresentFlag(0, 1);
    auto& pi =
      vps.getVpsPackedVideoExtension().getPackingInformation(atlasIndex);
    pi.pin_regions_count_minus1(attCnt + geoCnt - 1);
    pi.pin_geometry_present_flag(params.encodeDisplacementType == 2);
    pi.pin_attribute_present_flag(params.encodeTextureVideo);
    // update geometry information
    pi.pin_geometry_2d_bit_depth_minus1(gi.getGeometry2dBitdepthMinus1());
    pi.pin_geometry_3d_coordinates_bit_depth_minus1(
      gi.getGeometry3dCoordinatesBitdepthMinus1());
    // update attribute information
    pi.pin_attribute_allocate(1);
    pi.pin_attribute_2d_bit_depth_minus1(
      0, params.attributeParameters[0].textureInputBitDepth - 1);
    if (vps.getProfileTierLevel().getPtlVideoCodecGroupIdc() == 15)
      pi.pin_codec_id(params.attributeParameters[0].textureCodecId);
    else pi.pin_codec_id(0);
  }

  vps.setExtensionPresentFlag(true);
  vps.setExtensionCount(1);
  vps.setExtensionType(0, (uint8_t)VPS_EXT_VDMC);
  if (params.jointTextDisp) {
    vps.setExtensionCount(2);
    vps.setExtensionType(0, (uint8_t)VPS_EXT_PACKED);
    vps.setExtensionType(1, (uint8_t)VPS_EXT_VDMC);
  }
  auto& vmcExt = vps.getVpsVdmcExtension();
  vmcExt.setAtlasCount(1);
  vmcExt.setVpsExtMeshDataSubstreamCodecId(0, atlasId); //Indicates annex H, no alternative so far
  vmcExt.setVpsExtMeshGeo3dBitdepthMinus1(params.bitDepthPosition - 1,
                                          atlasIndex);
  //vmcExt.setVpsExtMeshGeo3dMSBAlignFlag(false, atlasId);

  int attributeCount =
    params.encodeNormals + (params.iDeriveTextCoordFromPos < 3);
  if ((params.iDeriveTextCoordFromPos == 0) && (params.numTextures > 1)
      && (params.dracoMeshLossless))
    attributeCount++;  // will send the face ID in the basemesh as well to indicate the texture map index
  vmcExt.setVpsExtMeshDataAttributeCount(attributeCount, atlasIndex);
  auto attrIdx = 0;
  if (params.iDeriveTextCoordFromPos < 3) {  // Texture Coordinates
    if (params.iDeriveTextCoordFromPos == 0) {
      if (params.numTextures > 1 && params.dracoMeshLossless) {
        // basemesh contains ATTR_TEXTCOORD and ATTR_TEXTCOORD_INDEX (NEW PROPOSAL)
        vmcExt.setVpsExtMeshAttributeBitdepthMinus1(
          params.bitDepthTexCoord - 1, atlasIndex, attrIdx);
        vmcExt.setVpsExtMeshAttributeType(
          basemesh::BASEMESH_ATTRIBUTE_TEXCOORD, atlasIndex, attrIdx);
        vmcExt.setVpsExtMeshAttributeIndex(attrIdx, atlasIndex, attrIdx);
        attrIdx++;  // attribute signaling the texture index
        vmcExt.setVpsExtMeshAttributeBitdepthMinus1(
          std::ceil(std::log2(params.numTextures)),
          atlasIndex,
          attrIdx);  // TODO: it works for now, but it should be the maximum value of material index
        vmcExt.setVpsExtMeshAttributeType(
          basemesh::BASEMESH_ATTRIBUTE_MATERIALID, atlasIndex, attrIdx);
        vmcExt.setVpsExtMeshAttributeIndex(attrIdx, atlasIndex, attrIdx);
      } else {
        // basemesh contains a single ATTR_TEXTCOORD
        vmcExt.setVpsExtMeshAttributeBitdepthMinus1(
          params.bitDepthTexCoord - 1, atlasIndex, attrIdx);
        vmcExt.setVpsExtMeshAttributeType(
          basemesh::BASEMESH_ATTRIBUTE_TEXCOORD, atlasIndex, attrIdx);
        vmcExt.setVpsExtMeshAttributeIndex(
          attrIdx,
          atlasIndex,
          attrIdx);  // mapping from the first attribute in basemesh
      }
    } else if (params.iDeriveTextCoordFromPos == 1) {
      vmcExt.setVpsExtMeshAttributeBitdepthMinus1(10, atlasIndex, attrIdx);
      vmcExt.setVpsExtMeshAttributeType(
        basemesh::BASEMESH_ATTRIBUTE_TEXCOORD, atlasIndex, attrIdx);
      vmcExt.setVpsExtMeshAttributeIndex(
        attrIdx,
        atlasIndex,
        attrIdx);  // mapping from the first attribute in basemesh -> will be substituted by orthoAtlas
    } else if (params.iDeriveTextCoordFromPos == 2) {
      vmcExt.setVpsExtMeshAttributeBitdepthMinus1(10, atlasIndex, attrIdx);
      vmcExt.setVpsExtMeshAttributeType(
        basemesh::BASEMESH_ATTRIBUTE_FACEGROUPID, atlasIndex, attrIdx);
      vmcExt.setVpsExtMeshAttributeIndex(
        attrIdx,
        atlasIndex,
        attrIdx);  // mapping from the first attribute in basemesh -> will be substitued by orthoAtlas
    }
    attrIdx++;
  }
  if (params.encodeDisplacementType == 1)  // AC displacement
    vmcExt.setVpsExtACDisplacementPresentFlag(atlasIndex, true);
  else vmcExt.setVpsExtACDisplacementPresentFlag(atlasIndex, false);
  if (vmcExt.getVpsExtACDisplacementPresentFlag(atlasIndex)) {
    auto& di = vmcExt.getDisplacementInformation(atlasIndex);
    di.getDisplacementCodecId()          = 0;
  }

  if (params.encodeNormals) {  // Normals
    vmcExt.setVpsExtMeshAttributeBitdepthMinus1(
      params.bitDepthNormals - 1, atlasIndex, attrIdx);
    vmcExt.setVpsExtMeshAttributeType(
      basemesh::BASEMESH_ATTRIBUTE_NORMAL, atlasIndex, attrIdx);
    vmcExt.setVpsExtMeshAttributeIndex(attrIdx, atlasIndex, attrIdx);
    attrIdx++;
  }

  //NOTE: this can be updated later in updateParameterSets()
  if (vps.getAttributeInformation(atlasIndex).getAttributeCount() == 0) {
    // bitstream conformance requirement
    vmcExt.setVpsExtConsistentAttributeFrameFlag(1, atlasIndex);
  } else {
    vmcExt.setVpsExtConsistentAttributeFrameFlag(1, atlasIndex);
  }

  //NOTE: getVpsAttributeNominalFrameCount() requires PackedVideoExtension first.
  int32_t vpsAttributeNominalFrameCount =
    vps.getVpsAttributeNominalFrameSizeCount(atlasIndex);
  vmcExt.allocateAttributeFramesize(vpsAttributeNominalFrameCount, atlasIndex);
  for (size_t i = 0; i < vpsAttributeNominalFrameCount; i++) {
    vmcExt.setVpsExtAttributeFrameWidth(
      params.attributeParameters[i].textureWidth, atlasIndex, i);
    vmcExt.setVpsExtAttributeFrameHeight(
      params.attributeParameters[i].textureHeight, atlasIndex, i);
  }
  return 0;
}

//============================================================================
int32_t
VMCEncoder::initializeParameterSets(V3cBitstream&               syntax,
                                    V3CParameterSet&            vps,
                                    int32_t                     atlasCount,
                                    int32_t                     atlasId,
                                    int32_t                     gofIndex,
                                    const VMCEncoderParameters& params) {
  syntax.getActiveVpsId() = gofIndex;
  syntax.getAtlasId()     = atlasId;
  setV3CParameterSet(
    vps, atlasCount, syntax.getAtlasId(), syntax.getActiveVpsId(), params);

  // should this be atlas encoder parameters ?
  uint32_t attributeNominalFrameCount =
      vps.getVpsAttributeNominalFrameSizeCount(0);
  bool consistentAttributeFrameFlag =
      vps.getVpsVdmcExtension().getVpsExtConsistentAttributeFrameFlag(0);
  bool videoDisplacementFlag  = (params.encodeDisplacementType == 1 ? 0 : 1);

  syntax.getAtlasDataStream().setAtlasId(syntax.getAtlasId());
  syntax.getAtlasDataStream().setV3CParameterSetId(vps.getV3CParameterSetId());
  atlasEncoder_.initializeAtlasParameterSets(syntax.getAtlasDataStream(),
                                             params.atlasEncoderParameters_);

  syntax.getBaseMeshDataStream().setAtlasId(syntax.getAtlasId());
  syntax.getBaseMeshDataStream().setV3CParameterSetId(vps.getV3CParameterSetId());
  baseMeshEncoder_.initializeBaseMeshParameterSets(syntax.getBaseMeshDataStream(), params.baseMeshEncoderParameters_);

  return 0;
}

//============================================================================
void
VMCEncoder::setProfileAndLevels(V3cBitstream& syntax, 
  V3CParameterSet& vps,
  const VMCEncoderParameters& params)
{
  // toolset
  bool hasInterOrAttrTile = false;
  bool hasInterOrAttrMeshpatch = false;
  auto& atlList = syntax.getAtlasDataStream().getAtlasTileLayerList();
  for (auto& atl : atlList) {
    auto& athType = atl.getHeader().getType();
    if ((athType == SKIP_TILE) || (athType == P_TILE)) {
      hasInterOrAttrTile = true;
      auto& atlData = atl.getDataUnit();
      for (int i = 0; i < atlData.getPatchCount(); i++) {
        auto meshpatchMode = atlData.getPatchInformationData(i).getPatchMode();
        if ((meshpatchMode == P_SKIP) || (meshpatchMode == P_MERGE) || (meshpatchMode == P_INTER) || (meshpatchMode == P_INTRA) || (meshpatchMode == P_END))
          hasInterOrAttrMeshpatch = true;
      }
    }
    if ((athType == I_TILE_ATTR) || (athType == P_TILE_ATTR))
      if(params.numSubmesh > 1) // will send the tile only if we have more than one submesh
        hasInterOrAttrTile = true;
  }
  if(
    params.intraGeoParams.lodAdaptiveSubdivisionFlag == 1 //asve_lod_adaptive_subdivision_flag == 1
    || params.applyOneDimensionalDisplacement == 0 //asve_1d_displacement_flag==0 
    || false // afve_overriden_flag== 1 
    || params.increaseTopSubmeshSubdivisionCount == 1 //mdu_parameters_override_flag == 1
    || false //aftai_single_tile_in_atlas_frame_flag==0
    || (params.numSubmesh > 1) //aftai_nal_tile_present_flag==1
    || hasInterOrAttrTile //Tile(ath_type == SKIP_TILE, P_TILE, I_TILE_ATTR, P_TILE_ATTR)
    || hasInterOrAttrMeshpatch )//(atdu_meshpatch_mode == P_SKIP, P_MERGE, P_INTER, P_INTRA, P_END))
    vps.getProfileTierLevel().getProfileToolsetIdc() = ToolsetIdc::TOOLSET_1;
  else
    vps.getProfileTierLevel().getProfileToolsetIdc() = ToolsetIdc::TOOLSET_0;
  // Reconstruction
  bool hasOrthoAtlas = params.textureParameterizationType == 1;
  bool midpointSubdivisionOnly = true;
  for (int it = 0; it < params.subdivisionIterationCount; it++) {
    if (params.intraGeoParams.lodAdaptiveSubdivisionFlag || it == 0) {
      if (params.intraGeoParams.subdivisionMethod[it] != SubdivisionMethod::MID_POINT)
        midpointSubdivisionOnly = false;
    }
    else {
      if (params.intraGeoParams.subdivisionMethod[0] != SubdivisionMethod::MID_POINT)
        midpointSubdivisionOnly = false;
    }
  }
  if(hasOrthoAtlas && midpointSubdivisionOnly)
    vps.getProfileTierLevel().getProfileReconstructionIdc() = ReconstructionIdc::REC_0;
  if (hasOrthoAtlas && !midpointSubdivisionOnly)
    vps.getProfileTierLevel().getProfileReconstructionIdc() = ReconstructionIdc::REC_1;
  if (!hasOrthoAtlas && midpointSubdivisionOnly)
    vps.getProfileTierLevel().getProfileReconstructionIdc() = ReconstructionIdc::REC_2;
  if (!hasOrthoAtlas && !midpointSubdivisionOnly)
    vps.getProfileTierLevel().getProfileReconstructionIdc() = ReconstructionIdc::REC_3;
  // geometry level IDC
  if (params.encodeDisplacementType == 2) {
    uint32_t geoFrameSize = 0;
    uint8_t atlasId = 0;
    geoFrameSize = vps.getFrameHeight(atlasId) * vps.getFrameWidth(atlasId);
    GeometryLevelIdc geoLevelIdc;
    geoLevelIdc = getGeometryLevelIdcFromGeoAtlasSize(geoFrameSize);
    if ((uint32_t)geoLevelIdc < (uint32_t)getGeometryLevelIdcFromNumSubmeshes(params.numSubmesh))
      geoLevelIdc = getGeometryLevelIdcFromNumSubmeshes(params.numSubmesh);
    if ((uint32_t)geoLevelIdc < (uint32_t)getGeometryLevelIdcFromNumTiles(params.numTilesGeometry))
      geoLevelIdc = getGeometryLevelIdcFromNumTiles(params.numTilesGeometry);
    uint32_t maxNumVertices = 0;
    for (int frIdx = 0; frIdx < reconBasemeshes_[0].size(); frIdx++) {
      int numVertices = 0;
      for (int smIdx = 0; smIdx < reconBasemeshes_.size(); smIdx++) {
        numVertices += reconBasemeshes_[smIdx][frIdx].pointCount();
      }
      if (maxNumVertices < numVertices)
        maxNumVertices = numVertices;
    }
    if ((uint32_t)geoLevelIdc < (uint32_t)getGeometryLevelIdcFromBasemeshVertices(maxNumVertices))
      geoLevelIdc = getGeometryLevelIdcFromBasemeshVertices(maxNumVertices);
    if ((uint32_t)geoLevelIdc < (uint32_t)getGeometryLevelIdcFromNumSubmeshes(params.numSubmesh))
      geoLevelIdc = getGeometryLevelIdcFromNumSubmeshes(params.numSubmesh);
    vps.getProfileTierLevel().getLevelIdc() = geoLevelIdc;
  }
  else {
    // settign highest level for AC displacement and no disparity case
    vps.getProfileTierLevel().getLevelIdc() = GeometryLevelIdc::LEVEL_2_2;
  }
  vps.getProfileTierLevel().getTierFlag() = GeometryTierFlag::TIER_0;

  uint32_t attrFrameSize = 0;
  for (size_t i = 0; i < params.attributeParameters.size(); i++) {
    attrFrameSize += params.attributeParameters[i].textureWidth
      * params.attributeParameters[i].textureHeight;
  }
  vps.getProfileTierLevel().getAttributeLevelIdc() = getAttributLevelIdc(attrFrameSize);
  vps.getProfileTierLevel().getAttributeTierFlag() = AttributeTierFlag::ATTR_TIER_0;

  std::cout << "Profiles and Levels" << std::endl;
  std::cout << "\tToolsetIdc:" << toString((ToolsetIdc)vps.getProfileTierLevel().getProfileToolsetIdc()) << std::endl;
  std::cout << "\tReconstructionIdc:" << toString((ReconstructionIdc)vps.getProfileTierLevel().getProfileReconstructionIdc()) << std::endl;
  std::cout << "\tGeometryLevelIdc:" << toString((GeometryLevelIdc)vps.getProfileTierLevel().getLevelIdc()) << std::endl;
  std::cout << "\tGeometryTierFlag:" << vps.getProfileTierLevel().getTierFlag() << std::endl;
  std::cout << "\tAttributeLevelIdc:" << toString((AttributeLevelIdc)vps.getProfileTierLevel().getAttributeLevelIdc()) << std::endl;
  std::cout << "\tAttributeTierFlag:" << vps.getProfileTierLevel().getAttributeTierFlag() << std::endl;
}

//============================================================================
void
VMCEncoder::setIdIndexMapping(const VMCEncoderParameters& encParams) {
  //std::vector<uint32_t> submeshIdtoIndex; //orange -> 0
  //std::vector<uint32_t> tileIdtoIndex; //orange -> 0
  uint32_t maxSubmeshId = 0;
  for (size_t i = 0; i < encParams.submeshIdList.size(); i++) {
    maxSubmeshId = std::max(maxSubmeshId, encParams.submeshIdList[i]);
  }
  uint32_t maxTileId = 0;
  for (size_t i = 0; i < encParams.tileIdList.size(); i++) {
    maxTileId = std::max(maxTileId, encParams.tileIdList[i]);
  }

  submeshIdtoIndex_.resize(maxSubmeshId + 1, -1);
  tileIdtoIndex_.resize(maxTileId + 1, -1);
  for (size_t i = 0; i < encParams.submeshIdList.size(); i++) {
    submeshIdtoIndex_[encParams.submeshIdList[i]] = (uint32_t)i;
  }
  for (size_t i = 0; i < encParams.tileIdList.size(); i++) {
    tileIdtoIndex_[encParams.tileIdList[i]] = (uint32_t)i;
  }

  atlasEncoder_.setsubmeshIdtoIndex(submeshIdtoIndex_);
}

//============================================================================
void
VMCEncoder::setFilePaths(
  std::vector<std::string>&       inputSubmeshPaths,
  std::string                     inputMeshPath,
  const std::vector<std::string>& inputAttributesPaths,
  std::string                     reconstructMeshPath,
  const std::vector<std::string>& reconstructedAttributesPaths,
  std::string                     reconstructedMaterialLibPath,
  int32_t                         startFrame) {
  inputSubmeshPaths_            = inputSubmeshPaths;
  inputMeshPath_                = inputMeshPath;
  inputAttributesPaths_         = inputAttributesPaths;
  reconstructedMeshPath_        = reconstructMeshPath;
  reconstructedAttributesPaths_ = reconstructedAttributesPaths;
  reconstructedMaterialLibPath_ = reconstructedMaterialLibPath;
  startFrame_                   = startFrame;
}

//============================================================================
void
VMCEncoder::setAttributeVideoCodecConfig(
  const VMCEncoderParameters& encParams) {
  sourceAttributeColourSpaces_.resize(encParams.videoAttributeCount);
  videoAttributeColourSpaces_.resize(encParams.videoAttributeCount);
  sourceAttributeBitdetphs_.resize(encParams.videoAttributeCount);
  videoAttributeBitdetphs_.resize(encParams.videoAttributeCount);

  for (int attIdx = 0; attIdx < encParams.videoAttributeCount; attIdx++) {
    sourceAttributeColourSpaces_[attIdx] = vmesh::ColourSpace::BGR444p;
    videoAttributeColourSpaces_[attIdx]  = vmesh::ColourSpace::YUV420p;
    if (encParams.attributeParameters[attIdx].textureBGR444)
      videoAttributeColourSpaces_[attIdx] = vmesh::ColourSpace::BGR444p;
    sourceAttributeBitdetphs_[attIdx] =
      encParams.attributeParameters[attIdx].textureInputBitDepth;
    videoAttributeBitdetphs_[attIdx] =
      encParams.attributeParameters[attIdx].textureVideoBitDepth;
  }
}

//*************//
//a texture map corresponding to a submesh is placed on a prefixed location
//#submesh=1,3,4 & 31 are defined here
void
VMCEncoder::setSubtextureVideoSize(
  int32_t                 textureWidth,
  int32_t                 textureHeight,
  std::vector<imageArea>& subTextureVideoAreas,
  int32_t                 submeshCount) {
  subTextureVideoAreas.resize(submeshCount);

  for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    subTextureVideoAreas[submeshIdx].LTx   = 0;
    subTextureVideoAreas[submeshIdx].LTy   = 0;
    subTextureVideoAreas[submeshIdx].sizeX = textureWidth;
    subTextureVideoAreas[submeshIdx].sizeY = textureHeight;
  }  //submeshIdx
  if (submeshCount == 1) return;
  //  if (!encParams.encodeTextureVideo) return;

  if (submeshCount == 2) {
    subTextureVideoAreas[0] = atlas::imageArea{
      (uint32_t)textureWidth / 2, (uint32_t)textureHeight, 0, 0};
    subTextureVideoAreas[1] = atlas::imageArea{(uint32_t)textureWidth / 2,
                                               (uint32_t)textureHeight,
                                               (uint32_t)textureWidth / 2,
                                               0};
  } else if (submeshCount == 3) {
    subTextureVideoAreas[0] = atlas::imageArea{
      (uint32_t)textureWidth / 2, (uint32_t)textureHeight / 2, 0, 0};
    subTextureVideoAreas[1] = atlas::imageArea{(uint32_t)textureWidth / 2,
                                               (uint32_t)textureHeight,
                                               (uint32_t)textureWidth / 2,
                                               0};  //LTx, LTy
    subTextureVideoAreas[2] =
      atlas::imageArea{(uint32_t)textureWidth / 2,
                       (uint32_t)textureHeight / 2,
                       0,                             //LTx
                       (uint32_t)textureHeight / 2};  //LTy

  } else if (submeshCount == 4) {
    auto subTextureWidth  = textureWidth / 2;
    auto subTextureHeight = textureHeight / 2;
    for (int si = 0; si < submeshCount; si++) {
      subTextureVideoAreas[si].sizeX = subTextureWidth;
      subTextureVideoAreas[si].sizeY = subTextureHeight;
    }
    for (int si = 0; si < submeshCount; si++) {
      subTextureVideoAreas[si].LTx = (si % 2) * subTextureWidth;   //x
      subTextureVideoAreas[si].LTy = (si / 2) * subTextureHeight;  //y
    }
  } else if (submeshCount == 31) {
    auto subTextureWidth  = textureWidth / 4;
    auto subTextureHeight = textureHeight / 8;
    for (int si = 0; si < submeshCount - 1; si++) {
      subTextureVideoAreas[si].sizeX = subTextureWidth;
      subTextureVideoAreas[si].sizeY = subTextureHeight;
    }
    subTextureVideoAreas[submeshCount - 1].sizeX = subTextureWidth * 2;
    subTextureVideoAreas[submeshCount - 1].sizeY = subTextureHeight;

    for (int si = 0; si < submeshCount; si++) {
      subTextureVideoAreas[si].LTx = (si % 4) * subTextureWidth;   //x
      subTextureVideoAreas[si].LTy = (si / 4) * subTextureHeight;  //y
    }
  }
}

//============================================================================
void
VMCEncoder::textureBackgroundFilling(Frame<uint16_t>&            inTexture,
                                     Plane<uint8_t>&             occupancy,
                                     const VMCEncoderParameters& params) {
  Frame<uint8_t> reconstructedTexture(
    inTexture.width(), inTexture.height(), inTexture.colourSpace());
  reconstructedTexture = inTexture;

  if (params.textureTransferPaddingDilateIterationCount != 0) {
    Frame<uint8_t> tmpTexture;
    Plane<uint8_t> tmpOccupancy;
    for (int32_t it = 0;
         it < params.textureTransferPaddingDilateIterationCount;
         ++it) {
      DilatePadding(reconstructedTexture, occupancy, tmpTexture, tmpOccupancy);
      DilatePadding(tmpTexture, tmpOccupancy, reconstructedTexture, occupancy);
    }
  }
  if (params.textureTransferPaddingMethod == PaddingMethod::PUSH_PULL) {
    PullPushPadding(reconstructedTexture, occupancy);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::SPARSE_LINEAR) {
    PullPushPadding(reconstructedTexture, occupancy);
    SparseLinearPadding(reconstructedTexture,
                        occupancy,
                        params.textureTransferPaddingSparseLinearThreshold);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::SMOOTHED_PUSH_PULL) {
    SmoothedPushPull(reconstructedTexture, occupancy);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::HARMONIC_FILL) {
    HarmonicBackgroundFill(reconstructedTexture, occupancy);
  }

  inTexture = reconstructedTexture;
}

//============================================================================
bool
VMCEncoder::compressAcGeo(
  std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
  int32_t                                      frameCount,
  V3cBitstream&                                syntax,
  V3CParameterSet&                             vps,
  const VMCEncoderParameters&                  params) {
  auto& dmSubstream = syntax.getDisplacementStream();
  dmSubstream.setAtlasId(vps.getAtlasId(vps.getV3CParameterSetId()));
  dmSubstream.setV3CParameterSetId(vps.getV3CParameterSetId());
  auto& dsps  = dmSubstream.addDisplSequenceParameterSet();
  auto& dfps  = dmSubstream.addDisplFrameParameterSet();
  auto& dinfo = dfps.getDisplInformation();
  acDisplacementEncoder_.createDispParameterSet(dsps, dfps, params.acDisplacementEncoderParameters_);

  auto submeshCount = encSubmeshes.size();
  for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
    if (acDisplacementEncoder_.compressACDisplacements(dmSubstream,
                                encSubmeshes[submeshIdx],
                                submeshIdx,
                                frameCount,
                                params.acDisplacementEncoderParameters_)
        == 0) {
      std::cout << "compress Displacement AC failed\n";
      return false;
    }
  }
#if CONFORMANCE_LOG_ENC
  getACDisplacementLog(encSubmeshes,frameCount,syntax,params);
#endif
  std::cout << "\nreconstruction\n";
  //reconstruct displacement
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    for (int32_t tileIdx = 0; tileIdx < params.numTilesGeometry; tileIdx++) {
      int32_t patchCount =
        (int32_t)reconAtlasTiles_[tileIdx][frameIdx].patches_.size();
      for (int32_t patchIdx = 0; patchIdx < patchCount; patchIdx++) {
        //patch - displId
        auto displId = params.submeshIdsInTile[tileIdx][patchIdx];
        //displId - displIndex
        auto  displIndex = dinfo._displIDToIndex[displId];
        auto& rec        = reconSubdivmeshes_[displIndex][frameIdx];
        if (params.checksum) {
          Checksum checksum;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIdx) + "\tREC["
                                + std::to_string(displIndex) + "] ";
          checksum.print(rec, eString);
          std::cout
            << "**********************************************************\n";
        }
        auto& submeshRecDisp = encSubmeshes[displIndex][frameIdx].disp;
        if (params.checksum) {
          Checksum checksum;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIdx)
                                + "\t(recon)submeshDisp["
                                + std::to_string(displIndex) + "] ";
          checksum.print(encSubmeshes[displIndex][frameIdx].disp, eString);
          std::cout
            << "**********************************************************\n";
        }

        reconstructPositions(encSubmeshes[displIndex],
                             frameIdx,
                             tileIdx,
                             displIndex,
                             patchIdx,
                             syntax,
                             vps,
                             params);

      }  //patchIdx
    }
  }
  return true;
}

//============================================================================
bool
VMCEncoder::compressDisplacementsVideo(FrameSequence<uint16_t>& dispVideo,
                                       FrameSequence<uint16_t>& dispVideoRec,
                                       VideoBitstream&          videoStream,
                                       int32_t                  frameCount,
                                       const VMCEncoderParameters& params) {
  //Encode
  if (params.keepVideoFiles) {
    auto prefix = _keepFilesPathPrefix + "disp";
    dispVideo.save(
      dispVideo.createName(prefix + "_enc", params.geometryVideoBitDepth));
  }
  VideoEncoderParameters videoEncoderParams;
  videoEncoderParams.encoderConfig_    = params.geometryVideoEncoderConfig;
  videoEncoderParams.inputBitDepth_    = params.geometryVideoBitDepth;
  videoEncoderParams.internalBitDepth_ = params.geometryVideoBitDepth;
  videoEncoderParams.qp_               = (params.displacementQuantizationType
                            == DisplacementQuantizationType::ADAPTIVE)
                                           ? -(params.geoVideoQP[0] - 8)
                                           : +8;
  videoEncoderParams.usePccRDO_        = false;
  if (params.displacementVideoChromaFormat != 1)
    videoEncoderParams.profile_ = "main-RExt";
  if (params.lodPatchesEnable && params.LoDExtractionEnable)
    setMCTSVideoEncoderParams(params, videoEncoderParams);
  FrameSequence<uint16_t>& rec            = dispVideoRec;
  std::vector<uint8_t>&    videoBitstream = videoStream.vector();
  printf("*********************************************************\n");
  printf("geometryVideoEncoderId = %d (%s) CodeGroup Idc: %s \n",
         (int)params.geometryVideoEncoderId,
         toString(params.geometryVideoEncoderId).c_str(),
         toString(params.profileCodecGroupId).c_str());
  fflush(stdout);
  //TODO: [profilecodec_idc] the config should contain the profile indicated by
  // profileCodecGroupId(or geometryCodecId if profileCodecGroupId=127) such as
  // HEVCMAIN10 etc not only encoder s/w(HM, FFMPEG etc)
  auto encoder = VideoEncoder<uint16_t>::create(params.geometryVideoEncoderId);
  encoder->encode(dispVideo, videoEncoderParams, videoBitstream, rec);
#if CONFORMANCE_LOG_ENC
  size_t   frameIndex = 0;
  Checksum checksum;
  for (auto& image : rec) {
    TRACE_PICTURE(" IdxOutOrderCntVal = %d, ", frameIndex++);
    TRACE_PICTURE(" MD5checksumChan0 = %s, ",
                  checksum.getChecksum(image.plane(0)).c_str());
    TRACE_PICTURE(" MD5checksumChan1 = %s, ",
                  checksum.getChecksum(image.plane(1)).c_str());
    TRACE_PICTURE(" MD5checksumChan2 = %s \n",
                  checksum.getChecksum(image.plane(2)).c_str());
  }
  TRACE_PICTURE("Width =  %d, Height = %d \n", rec.width(), rec.height());
#endif
  // Save intermediate files
  if (params.keepVideoFiles) {
    auto prefix = _keepFilesPathPrefix + "disp";
    rec.save(rec.createName(prefix + "_rec", params.geometryVideoBitDepth));
    save(prefix + ".h265", videoBitstream);
  }
  return true;
}

//============================================================================
bool
VMCEncoder::compressTextureVideo(
  VideoBitstream&             videoStream,
  FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
  Sequence&                   reconstruct,
  int                         attrIdx,
  int32_t                     frameOffset,
  const VMCEncoderParameters& params) {
  printf("Compress texture video \n");
  fflush(stdout);
  const auto frameCount = videoSrcSequence.frameCount();

  auto videoAttributeColourSpace_  = videoAttributeColourSpaces_[0];
  auto videoAttributeBitdetph_     = videoAttributeBitdetphs_[0];
  auto sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[0];
  auto sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[0];
  auto textureVideoSize            = attributeVideoSize_[0];
  auto paramsTexture               = params.attributeParameters[0];
  auto reconstructedTexturePath_   = reconstructedAttributesPaths_[0];
  if (params.encodeTextureVideo) {
    videoSrcSequence.setSequenceInfo(textureVideoSize.first,
                                     textureVideoSize.second,
                                     videoAttributeColourSpace_);
    // Save intermediate files
    if (params.keepVideoFiles) {
      videoSrcSequence.save(videoSrcSequence.createName(
        _keepFilesPathPrefix + "tex_enc", videoAttributeBitdetph_));
    }
    if (params.checksum) {
      Checksum checksumRec;
      std::cout
        << "**********************************************************\n";
      std::string eString =
        "frame " + std::to_string(0) + "\tTextureConverted ";
      checksumRec.print(videoSrcSequence[0], eString);
      std::cout
        << "**********************************************************\n";
    }
    //Encode
    VideoEncoderParameters videoEncoderParams;
    videoEncoderParams.encoderConfig_ =
      paramsTexture.textureVideoEncoderConfig;
    videoEncoderParams.inputBitDepth_    = videoAttributeBitdetph_;
    videoEncoderParams.internalBitDepth_ = videoAttributeBitdetph_;
    //    videoEncoderParams.outputBitDepth_   = videoEncoderParams.inputBitDepth_;
    videoEncoderParams.qp_ = paramsTexture.textureVideoQP;
    if (paramsTexture.useOccMapRDO) {
      videoEncoderParams.usePccRDO_        = true;
      videoEncoderParams.occupancyMapFile_ = paramsTexture.occMapFilename;
    }
    if (params.textureParameterizationType == 1 && params.packingType == 3) {
      videoEncoderParams.textureParameterizationType_ =
        params.textureParameterizationType;
      videoEncoderParams.packingType_       = params.packingType;
      videoEncoderParams.maxCUWidth_        = params.maxCUWidth;
      videoEncoderParams.maxCUHeight_       = params.maxCUHeight;
      videoEncoderParams.maxPartitionDepth_ = params.maxPartitionDepth;
    }

    if (params.scalableEnableFlag) {
      auto numLayerMinus1           = params.maxLayersMinus1;
      videoEncoderParams.maxLayers_ = numLayerMinus1 + 1;
      videoEncoderParams.srcYuvFileNameLayer_.resize(
        videoEncoderParams.maxLayers_);
      int         width  = videoSrcSequence.width();
      int         height = videoSrcSequence.height();
      std::string result = removeExtension(params.resultPath);
      videoEncoderParams.multiLayerCoding_ = true;
      videoEncoderParams.binFileName_      = result + ".bin";
      for (int i = 0; i <= numLayerMinus1; i++) {
        videoEncoderParams.srcYuvFileNameLayer_[i] =
          result + "_" + std::to_string(width >> (numLayerMinus1 - i)) + "x"
          + std::to_string(height >> (numLayerMinus1 - i)) + ".yuv";
      }
      videoEncoderParams.recYuvFileNameLayer_ =
        result + "_rec_" + std::to_string(width) + "x" + std::to_string(height)
        + ".yuv";
#ifdef USE_SHMAPP_VIDEO_CODEC
      videoEncoderParams.encoderPath_ = SHM_ENC_PATH;
#endif
    }
    printf("textureVideoEncoderId = %d(%s) CodeGroup Idc: %s \n",
           (int)paramsTexture.textureVideoEncoderId,
           toString(paramsTexture.textureVideoEncoderId).c_str(),
           toString(params.profileCodecGroupId).c_str());
    fflush(stdout);
    if (params.attributeExtractionEnable)
      setAttributeMCTSVideoEncoderParams(params, videoEncoderParams);
    FrameSequence<uint16_t> rec;
    std::vector<uint8_t>&   videoBitstream = videoStream.vector();
    auto                    encoder =
      VideoEncoder<uint16_t>::create(paramsTexture.textureVideoEncoderId);
    encoder->encode(videoSrcSequence, videoEncoderParams, videoBitstream, rec);
#if CONFORMANCE_LOG_ENC
    size_t   frameIndex = 0;
    Checksum checksum;
    for (auto& image : rec) {
      TRACE_PICTURE(" IdxOutOrderCntVal = %d, ", frameIndex++);
      TRACE_PICTURE(" MD5checksumChan0 = %s, ",
                    checksum.getChecksum(image.plane(0)).c_str());
      TRACE_PICTURE(" MD5checksumChan1 = %s, ",
                    checksum.getChecksum(image.plane(1)).c_str());
      TRACE_PICTURE(" MD5checksumChan2 = %s \n",
                    checksum.getChecksum(image.plane(2)).c_str());
    }
    TRACE_PICTURE("Width =  %d, Height = %d \n", rec.width(), rec.height());
#endif
    if (params.scalableEnableFlag) {
      auto numLayerMinus1 = params.maxLayersMinus1;
      rec.resize(videoSrcSequence.width(),
                 videoSrcSequence.height(),
                 ColourSpace::YUV420p,
                 frameCount);
      if (!params.keepVideoFiles) {
        remove(videoEncoderParams.binFileName_.c_str());
        for (int i = 0; i <= numLayerMinus1; i++) {
          remove(videoEncoderParams.srcYuvFileNameLayer_[i].c_str());
        }
        remove(videoEncoderParams.recYuvFileNameLayer_.c_str());
      }
    }
    videoSrcSequence.clear();
    printf("encode texture video done \n");
    fflush(stdout);

    // Save intermediate files
    if (params.keepVideoFiles) {
      rec.save(rec.createName(_keepFilesPathPrefix + "tex_rec",
                              videoAttributeBitdetph_));
      save(_keepFilesPathPrefix + ".h265", videoBitstream);
    }
    //    reconstruct.textures() = rec;
    if (params.numTextures > 1 && params.setLosslessTools)
      encChecksum_.checksumAtts().resize(params.numTextures);
    // Convert Rec YUV420 to BGR444
    for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
      convertTextureImage(rec.frame(frameIdx),
                          frameIdx,
                          videoAttributeColourSpace_,
                          videoAttributeBitdetph_,
                          sourceAttributeColourSpace_,
                          sourceAttributeBitdetph_,
                          paramsTexture.textureVideoUpsampleFilter,
                          paramsTexture.textureVideoFullRange);

      //NOTE: texture needs to be Frame<uint8_t> for checksum, the pixel values of rec.frame are already in 8bits.
      reconstruct.attributeFrame(attrIdx, frameIdx) = rec.frame(frameIdx);
      reconstruct.attributeFrame(attrIdx, frameIdx)
        .setColorSpace(rec.frame(frameIdx).colourSpace());
      encChecksum_.add(reconstruct.attributeFrame(attrIdx, frameIdx), attrIdx);
      if (!reconstructedTexturePath_.empty()) {
        std::string filename = reconstructedTexturePath_ + "_tex"
                               + std::to_string(attrIdx) + ".png";
        char fnameEncTexture[1024];
        snprintf(fnameEncTexture,
                 1024,
                 filename.c_str(),
                 frameIdx + frameOffset + startFrame_);
        reconstruct.attributeFrame(attrIdx, frameIdx).save(fnameEncTexture);
      }
    }  //frame

    rec.clear();
  }  //else
  return true;
}

//============================================================================
bool
VMCEncoder::compressPackedVideo(
  VideoBitstream&             videoStream,
  FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
  FrameSequence<uint16_t>&    recPacked,
  Sequence&                   reconstruct,
  int32_t                     frameOffset,
  const VMCEncoderParameters& params) {
  printf("Compress packed video \n");
  fflush(stdout);
  auto videoAttributeColourSpace_  = videoAttributeColourSpaces_[0];
  auto videoAttributeBitdetph_     = videoAttributeBitdetphs_[0];
  auto sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[0];
  auto sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[0];
  auto textureVideoSize            = attributeVideoSize_[0];
  auto paramsTexture               = params.attributeParameters[0];
  auto reconstructedTexturePath_   = reconstructedAttributesPaths_[0];

  const auto width      = videoSrcSequence.width();
  const auto height     = videoSrcSequence.height();
  const auto frameCount = videoSrcSequence.frameCount();

  if (params.encodeTextureVideo) {
    videoSrcSequence.setSequenceInfo(
      width, height, videoAttributeColourSpace_);
    // Save intermediate files
    if (params.keepVideoFiles) {
      videoSrcSequence.save(videoSrcSequence.createName(
        _keepFilesPathPrefix + "pac_enc", videoAttributeBitdetph_));
    }
    if (params.checksum) {
      Checksum checksumRec;
      std::cout
        << "**********************************************************\n";
      std::string eString =
        "frame " + std::to_string(0) + "\tTextureConverted ";
      checksumRec.print(videoSrcSequence[0], eString);
      std::cout
        << "**********************************************************\n";
    }
    //Encode
    VideoEncoderParameters videoEncoderParams;
    videoEncoderParams.encoderConfig_ =
      paramsTexture.textureVideoEncoderConfig;
    videoEncoderParams.inputBitDepth_    = videoAttributeBitdetph_;
    videoEncoderParams.internalBitDepth_ = videoAttributeBitdetph_;
    //    videoEncoderParams.outputBitDepth_   = videoEncoderParams.inputBitDepth_;
    videoEncoderParams.qp_ = paramsTexture.textureVideoQP;
    if (params.jointTextDisp) {
      videoEncoderParams.sliceMode_     = params.textureSliceMode;
      videoEncoderParams.sliceArgument_ = params.textureSliceArgument;
      videoEncoderParams.encoderConfigTex_ =
        paramsTexture.textureVideoEncoderConfig;
      videoEncoderParams.encoderConfigGeo_ = params.geometryVideoEncoderConfig;
      videoEncoderParams.jointTextDisp_    = params.jointTextDisp;
    }
    if (paramsTexture.useOccMapRDO) {
      videoEncoderParams.usePccRDO_        = true;
      videoEncoderParams.occupancyMapFile_ = paramsTexture.occMapFilename;
    }
    if (params.textureParameterizationType == 1 && params.packingType == 3) {
      videoEncoderParams.textureParameterizationType_ =
        params.textureParameterizationType;
      videoEncoderParams.packingType_       = params.packingType;
      videoEncoderParams.maxCUWidth_        = params.maxCUWidth;
      videoEncoderParams.maxCUHeight_       = params.maxCUHeight;
      videoEncoderParams.maxPartitionDepth_ = params.maxPartitionDepth;
    }
    printf("textureVideoEncoderId = %d (%s) CodeGroup Idc: %s \n",
           (int)paramsTexture.textureVideoEncoderId,
           toString(paramsTexture.textureVideoEncoderId).c_str(),
           (params.profileCodecGroupId != VideoCodecGroupIdc::Video_MP4RA
              ? toString(params.profileCodecGroupId).c_str()
              : toString(paramsTexture.textureCodecId).c_str()));
    fflush(stdout);

    std::vector<uint8_t>& videoBitstream = videoStream.vector();
    auto                  encoder =
      VideoEncoder<uint16_t>::create(paramsTexture.textureVideoEncoderId);
    encoder->encode(
      videoSrcSequence, videoEncoderParams, videoBitstream, recPacked);
#if CONFORMANCE_LOG_ENC
    size_t   frameIndex = 0;
    Checksum checksum;
    TRACE_PICTURE("PackedVideo\n");
    for (auto& image : recPacked) {
      TRACE_PICTURE(" IdxOutOrderCntVal = %d, ", frameIndex++);
      TRACE_PICTURE(" MD5checksumChan0 = %s, ",
                    checksum.getChecksum(image.plane(0)).c_str());
      TRACE_PICTURE(" MD5checksumChan1 = %s, ",
                    checksum.getChecksum(image.plane(1)).c_str());
      TRACE_PICTURE(" MD5checksumChan2 = %s \n",
                    checksum.getChecksum(image.plane(2)).c_str());
    }
    TRACE_PICTURE(
      "Width =  %d, Height = %d \n", recPacked.width(), recPacked.height());
#endif
    videoSrcSequence.clear();
    printf("encode packed video done \n");
    fflush(stdout);

    // Save intermediate files
    if (params.keepVideoFiles) {
      recPacked.save(recPacked.createName(_keepFilesPathPrefix + "pac_rec",
                                          videoAttributeBitdetph_));
      save(_keepFilesPathPrefix + ".h265", videoBitstream);
    }
    //    reconstruct.textures() = rec;
    // Convert Rec YUV420 to BGR444
    auto rec = recPacked;
    rec.standardizeFrameSizes(
      paramsTexture.textureWidth, paramsTexture.textureHeight, true, 512);
    if (params.keepVideoFiles) {
      rec.save(rec.createName(_keepFilesPathPrefix + "temp_tex_dec", 10));
    }

    for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
      convertTextureImage(rec.frame(frameIdx),
                          frameIdx,
                          videoAttributeColourSpace_,
                          videoAttributeBitdetph_,
                          sourceAttributeColourSpace_,
                          sourceAttributeBitdetph_,
                          paramsTexture.textureVideoUpsampleFilter,
                          paramsTexture.textureVideoFullRange);

      //NOTE: texture needs to be Frame<uint8_t> for checksum, the pixel values of rec.frame are already in 8bits.
      reconstruct.attributeFrame(0, frameIdx) = rec.frame(frameIdx);
      reconstruct.attributeFrame(0, frameIdx)
        .setColorSpace(rec.frame(frameIdx).colourSpace());
      encChecksum_.add(reconstruct.attributeFrame(0, frameIdx), 0);
      if (!reconstructedTexturePath_.empty()) {
        char fnameEncTexture[1024];
        snprintf(fnameEncTexture,
                 1024,
                 reconstructedTexturePath_.c_str(),
                 frameIdx + frameOffset + startFrame_);
        reconstruct.attributeFrame(0, frameIdx).save(fnameEncTexture);
      }
    }  //frame
    rec.clear();
  }  //else
  return true;
}

//============================================================================
bool
VMCEncoder::computeDisplacements(VMCSubmesh&                   frame,
                                 int32_t                       submeshIndex,
                                 int32_t                       frameIndex,
                                 const TriangleMesh<MeshType>& rec,
                                 const VMCEncoderParameters&   params) {
  const auto& subdiv = frame.subdiv;
  auto&       disp   = frame.disp;
  disp.resize(rec.pointCount());
  for (int32_t v = 0, vcount = rec.pointCount(); v < vcount; ++v) {
    if (params.displacementCoordinateSystem
        == DisplacementCoordinateSystem::LOCAL) {
      const auto     n = rec.normal(v);
      Vec3<MeshType> t{};
      Vec3<MeshType> b{};
      computeLocalCoordinatesSystem(n, t, b);
      const auto& pt0   = rec.point(v);
      const auto& pt1   = subdiv.point(v);
      const auto  delta = pt1 - pt0;
      disp[v]           = Vec3<MeshType>(delta * n, delta * t, delta * b);
    } else {
      disp[v] = subdiv.point(v) - rec.point(v);
    }
  }
  return true;
}

//============================================================================
void
VMCEncoder::fillDisZeros(VMCSubmesh&                 frame,
                         const VMCEncoderParameters& params) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  auto&       disp               = frame.disp;
  assert(lodCount > 0);
  if (lodCount > 1) {
    const auto vcount0        = infoLevelOfDetails[lodCount - 2].pointCount;
    const auto vcount1        = infoLevelOfDetails[lodCount - 1].pointCount;
    const auto dispDimensions = params.applyOneDimensionalDisplacement ? 1 : 3;
    int32_t    count_not_zero = 0;

    for (int32_t v = vcount0; v < vcount1; v++) {
      auto   d       = disp[v];
      double modulus = 0.0;
      if (dispDimensions == 1) {
        modulus = abs(d[0]);
      } else {
        modulus = d.norm();
      }
      if (modulus > 0.0) { count_not_zero++; }
    }
    double         a         = count_not_zero;
    double         b         = vcount1 - vcount0;
    double         propotion = a / b;
    Vec3<MeshType> Zero{0, 0, 0};
    if (propotion <= params.displacementFillZerosThreshold) {
      std::fill(disp.begin() + vcount0, disp.begin() + vcount1, Zero);
    }
  }
}

//============================================================================
bool
VMCEncoder::quantizeDisplacements(VMCSubmesh&                 frame,
                                  int32_t                     submeshIndex,
                                  const VMCEncoderParameters& params) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  const auto dispDimensions = params.applyOneDimensionalDisplacement ? 1 : 3;
  std::vector<std::vector<double>> scales(
    lodCount, std::vector<double>(dispDimensions, 0));
  double halfVBD = (1 << params.geometryVideoBitDepth) / 2;
  if (params.lodDisplacementQuantizationFlag) {
    for (int32_t it = 0; it < lodCount; ++it) {
      auto& scale = scales[it];
      for (int32_t k = 0; k < dispDimensions; ++k) {
        const auto qp = (int32_t)params.qpPerLevelOfDetails[it][k];
        if (params.displacementQuantizationType
            == DisplacementQuantizationType::ADAPTIVE)
          scale[k] = qp;
        else
          scale[k] = qp >= 0
                       ? pow(2.0,
                             16 + params.bitDepthOffset
                               - params.bitDepthPosition + (4 - qp) / 6.0)
                       : 0.0;
      }
    }
  } else {
    std::vector<double> lodScale(dispDimensions);
    auto&               scale = scales[0];
    for (int32_t k = 0; k < dispDimensions; ++k) {
      const auto qp = (int32_t)params.liftingQP[k];
      if (params.displacementQuantizationType
          == DisplacementQuantizationType::ADAPTIVE)
        scale[k] = qp;
      else
        scale[k] = qp >= 0 ? pow(2.0,
                                 16 + params.bitDepthOffset
                                   - params.bitDepthPosition + (4 - qp) / 6.0)
                           : 0.0;
      if (params.transformMethod == 1)
        lodScale[k] =
          1.0 / (double)(1 << params.log2LevelOfDetailInverseScale[k]);
      else lodScale[k] = 1.0;

      //printf("%d\tqp %d\tscale %f\tlodScale %f\n", k, qp, scale[k], lodScale[k]);
    }
    for (int32_t it = 1; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimensions; ++k) {
        scales[it][k] = scales[it - 1][k] * lodScale[k];
      }
    }
  }
  auto disp_temp = frame.disp;
  if (params.InverseQuantizationOffsetFlag) {
    auto& qdisplodmean = frame.qdisplodmean;
    qdisplodmean.resize(lodCount);
    std::vector<std::vector<std::vector<int32_t>>> count_zone;
    count_zone.resize(lodCount);
    for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
      const auto& scale   = scales[it];
      const auto  vcount1 = infoLevelOfDetails[it].pointCount;
      qdisplodmean[it].resize(dispDimensions, {0.0, 0.0, 0.0});
      count_zone[it].resize(dispDimensions, {0, 0, 0});

      for (int32_t v = vcount0; v < vcount1; ++v) {
        auto& d      = disp_temp[v];
        auto  d_copy = disp_temp[v];

        for (int32_t k = 0; k < dispDimensions; ++k) {
          d[k]      = d[k] >= 0.0
                        ? std::floor(d[k] * scale[k] + params.liftingBias[k])
                        : -std::floor(-d[k] * scale[k] + params.liftingBias[k]);
          int32_t m = (d[k] == 0 ? 0 : (d[k] > 0.0 ? 1 : 2));
          qdisplodmean[it][k][m] +=
            d_copy[k];  // [0] save the deadzone 0 values
          count_zone[it][k][m]++;
        }
      }
      vcount0 = vcount1;
    }
    for (int32_t it = 0; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimensions; ++k) {
        qdisplodmean[it][k][0] /= count_zone[it][k][0];
        qdisplodmean[it][k][1] /= count_zone[it][k][1];
        qdisplodmean[it][k][2] /= count_zone[it][k][2];
      }
    }
  }
  auto& disp = frame.disp;
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto& scale   = scales[it];
    const auto  vcount1 = infoLevelOfDetails[it].pointCount;
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = disp[v];
#if DEBUG_NONE_TRANSFORM
      if (v == vcount0) {
        std::cout << "pre quantization vcount0" << vcount0 << "disp " << d[0]
                  << " " << d[1] << " " << d[2] << std::endl;
      }
#endif
      for (int32_t k = 0; k < dispDimensions; ++k) {
        d[k] = d[k] >= 0.0
                 ? std::floor(d[k] * scale[k] + params.liftingBias[k])
                 : -std::floor(-d[k] * scale[k] + params.liftingBias[k]);
        // clipping to allowed bitdepth
        if (d[k] > +(halfVBD - 1)) d[k] = +(halfVBD - 1);
        if (d[k] < -(halfVBD - 1)) d[k] = -(halfVBD - 1);
#if DEBUG_NONE_TRANSFORM
        if (v == vcount0) {
          std::cout << "post quantization vcount0" << vcount0 << "quant disp "
                    << d[0] << " " << d[1] << " " << d[2] << std::endl;
        }
#endif
      }
    }
    vcount0 = vcount1;
  }
  return true;
}

//============================================================================
bool
VMCEncoder::computeDisplacementVideoFramePerLod(
  std::vector<Vec3<MeshType>>& disp,
  std::vector<lodInfo>&        lodVertexInfo,
  uint32_t                     lodIndex,
  uint32_t                     sizeX,
  uint32_t                     sizeY,
  uint32_t                     posX,
  uint32_t                     posY,
  Frame<uint16_t>&             dispVideoFrame,
  const VMCEncoderParameters&  params) {
  int32_t geometryVideoBlockSize = (1 << params.log2GeometryVideoBlockSize);
  const int32_t dispDim = params.applyOneDimensionalDisplacement ? 1 : 3;
  auto          displacementReversePacking = params.displacementReversePacking;
  const auto    shift = uint16_t((1 << params.geometryVideoBitDepth) >> 1);

  uint32_t dispCount = lodVertexInfo[lodIndex].vertexCount;
  uint32_t dispStart = lodVertexInfo[lodIndex].startVertexIndex;

  std::vector<std::pair<int32_t, int32_t>> dispToPixPos;

  getDisplacementToPixelPerLod(dispToPixPos,
                               lodIndex,
                               posX,
                               posY,
                               sizeX,
                               sizeY,
                               lodVertexInfo,
                               geometryVideoBlockSize,
                               displacementReversePacking);

  for (int32_t v = 0; v < dispCount; v++) {
    auto  x1 = dispToPixPos[v].first;
    auto  y1 = dispToPixPos[v].second;
    auto& d  = disp[v + dispStart];
    for (int32_t p = 0; p < dispDim; ++p) {
      const auto dshift = int32_t(shift + d[p]);
      assert(dshift >= 0 && dshift < (1 << params.geometryVideoBitDepth));
      if ((params.displacementVideoChromaFormat == 0
           && !params.applyOneDimensionalDisplacement)
          || params.displacementVideoChromaFormat == 1
          || params.displacementVideoChromaFormat == 2) {
        if (y1 >= dispVideoFrame.height() || x1 >= dispVideoFrame.width()) {
          printf("(%d,%d) is out of videoSize(%dx%d)\n",
                 x1,
                 y1,
                 dispVideoFrame.width(),
                 dispVideoFrame.height());
          fflush(stdout);
          exit(-5);
        }

        dispVideoFrame.plane(0).set(y1, x1, uint16_t(dshift));
      } else {
        dispVideoFrame.plane(p).set(y1, x1, uint16_t(dshift));
      }
    }
  }

  return true;
}

//============================================================================
bool
VMCEncoder::createDisplacementVideoFrame(
  std::vector<Vec3<MeshType>>& disp,
  std::vector<int32_t>&        vertexCountPerLod,
  int32_t                      frameIndex,
  int32_t                      tileIndex,
  int32_t                      patchIndex,
  int32_t                      submeshId,
  uint32_t                     sizeX,
  uint32_t                     sizeY,
  uint32_t                     posX,
  uint32_t                     posY,
  Frame<uint16_t>&             dispVideoFrame,
  const VMCEncoderParameters&  params) {
  int32_t    geometryVideoBlockSize = (1 << params.log2GeometryVideoBlockSize);
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  const auto shift = uint16_t((1 << params.geometryVideoBitDepth) >> 1);
  const uint32_t pointCount = disp.size();
  const auto     planeCount = dispVideoFrame.planeCount();
  printf("(createDisplacementVideoFrame 1) frame [%d] tile [%d] patch [%d] "
         "position: (%03d, %03d)\t size %dx%d submeshId: %d\tvideoSize: "
         "%dx%d\tdisp.size() %zu\n",
         frameIndex,
         tileIndex,
         patchIndex,
         posX,
         posY,
         sizeX,
         sizeY,
         submeshId,
         dispVideoFrame.width(),
         dispVideoFrame.height(),
         disp.size());
  fflush(stdout);

  uint32_t geometryPatchWidthInBlocks =
    (uint32_t)std::ceil((double)sizeX / (double)geometryVideoBlockSize);

  std::cout << " pointCount " << std::endl;
  //allocate packing structures
  int32_t              blockCount = 0;
  std::vector<lodInfo> lodVertexInfo;
  blockCount = getLodVertexInfo(vertexCountPerLod,
                                lodVertexInfo,
                                pointCount,
                                geometryVideoBlockSize,
                                geometryPatchWidthInBlocks,
                                params.lodPatchesEnable);

  const int32_t origHeightInBlocks =
    floor((blockCount + geometryPatchWidthInBlocks - 1)
          / geometryPatchWidthInBlocks);
  const int32_t origHeight = origHeightInBlocks * geometryVideoBlockSize;
  const int32_t totalBlocksInPatch =
    ((int32_t)sizeX * (int32_t)sizeY) / pixelsPerBlock;
  //  printf("\t(createDisplacementVideoFrame) blockCount:%d totalBlocksInPatch:%d origHeight:%d displacementVideoHeightInBlocks:%d geometryPatchWidthInBlocks:%d\n",
  //         blockCount, totalBlocksInPatch, origHeight, displacementVideoHeightInBlocks, geometryPatchWidthInBlocks);
  //  fflush(stdout);

  std::cout << " blockCount " << blockCount << " geometryPatchWidthInBlocks "
            << geometryPatchWidthInBlocks << " origHeightInBlocks "
            << origHeightInBlocks << std::endl;

  auto oneDimensionalDisplacement = params.applyOneDimensionalDisplacement;
  auto displacementReversePacking = params.displacementReversePacking;

  const int32_t dispDim = oneDimensionalDisplacement ? 1 : 3;

  std::vector<std::pair<int32_t, int32_t>> dispToPixPos;
  getDisplacementToPixel(dispToPixPos,
                         frameIndex,
                         tileIndex,
                         patchIndex,
                         submeshId,
                         sizeX,
                         origHeight,
                         posX,
                         posY,
                         pointCount,
                         lodVertexInfo,
                         geometryVideoBlockSize,
                         displacementReversePacking,
                         dispVideoFrame.width(),
                         dispVideoFrame.height());

  for (int32_t v = 0; v < pointCount; v++) {
    auto  x1 = dispToPixPos[v].first;
    auto  y1 = dispToPixPos[v].second;
    auto& d  = disp[v];
    for (int32_t p = 0; p < dispDim; ++p) {
      const auto dshift = int32_t(shift + d[p]);
      assert(dshift >= 0 && dshift < (1 << params.geometryVideoBitDepth));
      if ((params.displacementVideoChromaFormat == 0
           && !params.applyOneDimensionalDisplacement)
          || params.displacementVideoChromaFormat == 1
          || params.displacementVideoChromaFormat == 2) {
        auto posY = p * origHeight + y1;
        if (posY >= dispVideoFrame.height() || x1 >= dispVideoFrame.width()) {
          printf("(%d,%d) is out of videoSize(%dx%d)\n",
                 x1,
                 posY,
                 dispVideoFrame.width(),
                 dispVideoFrame.height());
          fflush(stdout);
          exit(-5);
        }

        dispVideoFrame.plane(0).set(posY, x1, uint16_t(dshift));
      } else {
        dispVideoFrame.plane(p).set(y1, x1, uint16_t(dshift));
      }
    }
  }

  return true;
}

#ifdef COMPRESS_VIDEO_PAC_ENABLE
//============================================================================
bool
VMCEncoder::compressVideoPac(
  std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
  Sequence&                                    reconstruct,
  int32_t                                      submeshCount,
  int32_t                                      frameCount,
  int32_t                                      frameOffset,
  V3cBitstream&                                syntax,
  V3CParameterSet&                             vps,
  const VMCEncoderParameters&                  params) {
  auto colourSpaceDispVideo = ColourSpace::YUV420p;
  //ColourSpace colourSpaceDispVideo = (ColourSpace) params.displacementVideoChromaFormat;
  if (params.displacementVideoChromaFormat == 0) {
    colourSpaceDispVideo = ColourSpace::YUV400p;
  } else if (params.displacementVideoChromaFormat == 1) {
    colourSpaceDispVideo = ColourSpace::YUV420p;
  } else if (params.displacementVideoChromaFormat == 2) {
    colourSpaceDispVideo = ColourSpace::YUV422p;
  } else if (params.displacementVideoChromaFormat == 3) {
    colourSpaceDispVideo = ColourSpace::YUV444p;
  } else {
    colourSpaceDispVideo = ColourSpace::UNKNOW;
  }
  FrameSequence<uint16_t> dispVideo;
  dispVideo.resize(geometryVideoSize.first,
                   geometryVideoSize.second,
                   colourSpaceDispVideo,
                   frameCount,
                   uint16_t((1 << params.geometryVideoBitDepth) >> 1));
  //dispVideo.resize(geometryVideoSize.first, geometryVideoSize.second, colourSpaceDispVideo, frameCount);
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    auto&      dispVideoFrame = dispVideo.frame(frameIdx);
    const auto shift      = uint16_t((1 << params.geometryVideoBitDepth) >> 1);
    const auto planeCount = dispVideoFrame.planeCount();
    for (int32_t p = 0; p < planeCount; ++p) {
      dispVideoFrame.plane(p).fill(shift);
    }

    for (int32_t tileIdx = 0; tileIdx < params.numTilesGeometry; tileIdx++) {
      int32_t patchCount =
        (int32_t)reconAtlasTiles_[tileIdx][frameIdx].patches_.size();
      for (int32_t patchIdx = 0; patchIdx < patchCount; patchIdx++) {
        auto sizeX = reconAtlasTiles_[tileIdx][frameIdx]
                       .patches_[patchIdx]
                       .geometryPatchArea.sizeX;  //[submeshIndex]
        auto sizeY = reconAtlasTiles_[tileIdx][frameIdx]
                       .patches_[patchIdx]
                       .geometryPatchArea.sizeY;  //[submeshIndex]
        auto posX =
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTx
          + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx;
        auto posY =
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTy
          + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy;

        auto submeshId =
          reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_;
        auto submeshIndex = submeshIdtoIndex_[submeshId];

        auto& subdivInfo =
          encSubmeshes[submeshIndex][frameIdx].subdivInfoLevelOfDetails;
        std::vector<int32_t> vertexCountPerLod(subdivInfo.size());
        for (size_t lod = 0; lod < subdivInfo.size(); lod++) {
          vertexCountPerLod[lod] = subdivInfo[lod].pointCount;
          if (lod != 0)
            vertexCountPerLod[lod] -= subdivInfo[lod - 1].pointCount;
        }
        createDisplacementVideoFrame(encSubmeshes[submeshIndex][frameIdx].disp,
                                     vertexCountPerLod,
                                     frameIdx,
                                     tileIdx,
                                     patchIdx,
                                     submeshId,
                                     sizeX,
                                     sizeY,
                                     posX,
                                     posY,
                                     dispVideoFrame,
                                     params);
      }
    }  //tile
  }    //frame

  FrameSequence<uint16_t> dispVideoRec;
  dispVideoRec = dispVideo;
  // already write info into dispVideoRec
  //reconstruct displacement
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    for (int32_t tileIdx = 0; tileIdx < params.numTilesGeometry; tileIdx++) {
      auto numPatchesInTile = params.submeshIdsInTile[tileIdx].size();
      for (int32_t patchIdx = 0; patchIdx < numPatchesInTile; patchIdx++) {
        auto  submeshIdInTile = params.submeshIdsInTile[tileIdx][patchIdx];
        auto  submeshIdx      = submeshIdtoIndex_[submeshIdInTile];
        auto& rec             = reconSubdivmeshes_[submeshIdx][frameIdx];
        if (params.checksum) {
#  if ENCDEC_CHECKSUM
          rec.normals().clear();
#  endif
          Checksum checksum;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIdx) + "\tREC["
                                + std::to_string(submeshIdx) + "] submeshId "
                                + std::to_string(submeshIdInTile) + ":";
          checksum.print(rec, eString);
          std::cout
            << "**********************************************************\n";
        }

        auto sizeX = reconAtlasTiles_[tileIdx][frameIdx]
                       .patches_[patchIdx]
                       .geometryPatchArea.sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][frameIdx]
                       .patches_[patchIdx]
                       .geometryPatchArea.sizeY;
        auto posX =
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTx
          + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx;
        auto posY =
          reconAtlasTiles_[tileIdx][frameIdx]
            .patches_[patchIdx]
            .geometryPatchArea.LTy
          + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy;

        auto& subdivInfo =
          encSubmeshes[submeshIdx][frameIdx].subdivInfoLevelOfDetails;
        std::vector<int32_t> vertexCountPerLod(subdivInfo.size());
        for (size_t lod = 0; lod < subdivInfo.size(); lod++) {
          vertexCountPerLod[lod] = subdivInfo[lod].pointCount;
          if (lod != 0)
            vertexCountPerLod[lod] -= subdivInfo[lod - 1].pointCount;
        }
        reconstructDisplacementFromVideoFrame(
          dispVideoRec.frame(frameIdx),
          vertexCountPerLod,
          encSubmeshes[submeshIdx][frameIdx].disp,
          rec,
          frameIdx,
          tileIdx,
          patchIdx,
          submeshIdInTile,
          sizeX,
          sizeY,
          posX,
          posY,
          (1 << params.log2GeometryVideoBlockSize),
          params.geometryVideoBitDepth,
          params.applyOneDimensionalDisplacement,
          params.displacementReversePacking,
          (ColourSpace)params.displacementVideoChromaFormat);

        if (params.checksum) {
          Checksum checksum;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIdx)
                                + "\t(recon)submeshDisp["
                                + std::to_string(submeshIdx) + "] submeshId "
                                + std::to_string(submeshIdInTile) + ":";
          checksum.print(encSubmeshes[submeshIdx][frameIdx].disp, eString);
          std::cout
            << "**********************************************************\n";
        }

        auto submeshId =
          reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_;
        auto submeshIndex = submeshIdtoIndex_[submeshId];
        reconstructPositions(encSubmeshes[submeshIndex],
                             frameIdx,
                             tileIdx,
                             submeshIndex,
                             patchIdx,
                             syntax,
                             vps,
                             params);

      }  //patch
    }    //tile
  }
  auto  sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[0];
  auto  sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[0];
  auto  videoAttributeColourSpace_  = videoAttributeColourSpaces_[0];
  auto  videoAttributeBitdetph_     = videoAttributeBitdetphs_[0];
  auto  textureVideoSize            = attributeVideoSize_[0];
  auto& paramsTexture               = params.attributeParameters[0];
  auto& inputTexturePath_           = inputAttributesPaths_[0];
  vmesh::FrameSequence<uint16_t> textures16bits;
#  if 1
  if (params.numTilesAttribute != 1 || params.numSubmesh != 1) {
    std::cout << "***subtexture placement---------------------------\n";
    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < params.numTilesGeometry + params.numTilesAttribute;
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto LTx = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTx;
        auto LTy = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTy;
        auto sizeX = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeY;
        std::cout << "Tile[" << tileIdx << "] patch[" << patchIdx << "] ";
        std::cout << "submeshId " << submeshId;
        std::cout << " subTexture[" << submeshIdtoIndex_[submeshId] << "]";
        std::cout << " pos in Tile: [" << LTx << "," << LTy << "]";
        //std::cout << " pos in Frame: [" << LTx << "," << LTy <<"]";
        std::cout << " size in Tile: " << sizeX << "x" << sizeY << "\n";
      }
    }
  }
#  endif
  reconstruct.resize(frameCount, 1);
  encChecksum_.checksumAtts().resize(1);
  reconstruct.attribute(0).resize(
    textureVideoSize.first,  //TODO: does "0" need to be changed?
    textureVideoSize.second,
    ColourSpace::BGR444p,
    frameCount);
  std::vector<Plane<uint8_t>> oneOccupancySequence(frameCount);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    std::vector<Frame<uint8_t>> sourceTexture(1);
    auto                        sourceTexturePath = inputTexturePath_;
    std::vector<Vec2<int32_t>>  position;
    std::vector<Vec2<int32_t>>  dimension;
    bool                        updateSubmeshUV = false;
    if (params.numTextures > 1) {
      vmesh::TriangleMesh<MeshType> sourceMesh;
      auto                          sourceMeshPath = inputMeshPath_;
      // the names of textures is located in the material file indicated by the mesh, so we need to read the mesh again
      if (!sourceMesh.load(sourceMeshPath,
                           frameIndex + frameOffset + startFrame_)) {
        printf("fail to load mesh : %s frame %d\n",
               sourceMeshPath.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      }
      auto listOfIndices = sourceMesh.materialIdxs();
      std::sort(listOfIndices.begin(), listOfIndices.end());
      listOfIndices.erase(unique(listOfIndices.begin(), listOfIndices.end()),
                          listOfIndices.end());
      int textureCount = listOfIndices.size();
      if (textureCount != params.numTextures) {
        printf("source mesh does not contain the correct number of texture "
               "URLs (%d != %d): %s frame %d\n",
               textureCount,
               params.numTextures,
               sourceMeshPath.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      }

      if (!params.dracoMeshLossless) {
        sourceTexture.resize(sourceMesh.textureMapUrls().size());
        for (int i = 0; i < sourceMesh.textureMapUrls().size(); i++) {
          auto& url = sourceMesh.textureMapUrls()[i];
          if (!url.empty()) {
            sourceTexturePath =
              dirname(sourceMeshPath) + sourceMesh.textureMapUrls()[i];
            if (!sourceTexture[i].load(
                  sourceTexturePath, frameIndex + frameOffset + startFrame_)) {
              printf("fail to load texture : %s frame %d\n",
                     sourceTexturePath.c_str(),
                     frameIndex + frameOffset + startFrame_);
              exit(2);
            };
          }
        }
      } else {
        sourceTexture.resize(1);
        // for lossless compression, we just read and encode the image corresponding to the attribute index
        sourceTexturePath = dirname(sourceMeshPath)
                            + sourceMesh.textureMapUrls()[listOfIndices[0]];
        if (!sourceTexture[0].load(sourceTexturePath,
                                   frameIndex + frameOffset + startFrame_)) {
          printf("fail to load texture : %s frame %d\n",
                 sourceTexturePath.c_str(),
                 frameIndex + frameOffset + startFrame_);
          exit(2);
        };
      }
    } else {
      sourceTexture.resize(1);
      if (!sourceTexture[0].load(sourceTexturePath,
                                 frameIndex + frameOffset + startFrame_)) {
        printf("fail to load texture : %s frame %d\n",
               inputTexturePath_.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      };
    }
    Frame<uint16_t> oneTrTexture;
    oneTrTexture.resize(textureVideoSize.first,
                        textureVideoSize.second,
                        sourceAttributeColourSpace_);
    bool            enablePadding = paramsTexture.enablePadding;
    Plane<uint8_t>& oneOccupancy  = oneOccupancySequence[frameIndex];
    if (params.numSubmesh != 1) {
      enablePadding = false;
      oneOccupancy.resize(textureVideoSize.first, textureVideoSize.second);
    }

    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < (params.numTilesGeometry + params.numTilesAttribute);
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto submeshIdx = submeshIdtoIndex_[submeshId];
        auto sizeX      = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeY;
        auto LTx =
          reconAtlasTiles_[tileIdx][frameIndex]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .LTx
          + reconAtlasTiles_[tileIdx][frameIndex].tileAttributeAreas_[0].LTx;
        auto LTy = reconAtlasTiles_[tileIdx][frameIndex]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTy;
        +reconAtlasTiles_[tileIdx][frameIndex].tileAttributeAreas_[0].LTx;

        vmesh::TriangleMesh<MeshType> sourceMesh;
        auto                          sourceMeshPath = inputMeshPath_;
        if (params.numSubmesh != 1 && !params.segmentByBaseMesh
            && !(params.numTextures > 1)) {
          sourceMeshPath = inputSubmeshPaths_[submeshIdx];
        }
        if (!sourceMesh.load(sourceMeshPath,
                             frameIndex + frameOffset + startFrame_)) {
          printf("fail to load mesh : %s frame %d submesh %d\n",
                 sourceMeshPath.c_str(),
                 frameIndex + frameOffset + startFrame_,
                 submeshIdx);
          exit(2);
        }
        Frame<uint8_t> subTexture;
        auto           subTextureWidth  = textureVideoSize.first;
        auto           subTextureHeight = textureVideoSize.second;

        if (params.numSubmesh != 1) {
          //predefined submesh texture image size
          subTextureWidth  = sizeX;
          subTextureHeight = sizeY;
        }
        subTexture.resize(
          subTextureWidth, subTextureHeight, sourceAttributeColourSpace_);
        TriangleMesh<MeshType>& recSubmesh =
          reconSubdivmeshes_[submeshIdx][frameIndex];

        Plane<uint8_t> occupancy;
        tic("ColorTransfer");
        if (paramsTexture.textureTransferEnable) {
          TransferColor                transferColor;
          auto&                        refMesh        = sourceMesh;
          std::vector<Frame<uint8_t>>& refTexture     = sourceTexture;
          auto&                        targetMesh     = recSubmesh;
          auto&                        targetTexture  = subTexture;
          bool                         usePastTexture = false;
          Frame<uint8_t>               pastTexture;
          //TODO: [sw] reconstruct.texture memory management
          if (submeshCount == 1 && params.textureTransferCopyBackground
              && frameIndex > 0
              && encSubmeshes[submeshIdx][frameIndex].submeshType
                   == basemesh::P_BASEMESH) {
            //occupancy map will be the same as the reference, so use the background from reference as well
            pastTexture = reconstruct.attributeFrame(
              0, encSubmeshes[submeshIdx][frameIndex].referenceFrameIndex);
            //std::string filename = "_test_"+std::to_string(frameIndex)+".png";
            //pastTexture.save(filename);
            usePastTexture = true;
          }
          if (!transferColor.transfer(refMesh,
                                      refTexture,
                                      targetMesh,
                                      targetTexture,
                                      usePastTexture,
                                      pastTexture,
                                      params,
                                      occupancy,
                                      enablePadding,
                                      submeshIdx)) {
            printf("fail to transferTexture\n");
            return false;
          }
        } else {
          //subTexture = sourceTexture;
          //reconstruct.texture(frameIndex) = sourceTexture;
          if (submeshCount == 1) {
            subTexture = sourceTexture[0];
            occupancy.resize(textureVideoSize.first, textureVideoSize.second);
          } else {
            occupancy.resize(subTextureWidth, subTextureHeight);
            for (int32_t i = 0; i < oneTrTexture.planeCount(); i++) {
              for (int32_t y = LTy; y < (LTy + sizeY); y++) {
                for (int32_t x = LTx; x < (LTx + sizeX); x++) {
                  auto v = sourceTexture[0].plane(i).get(y, x);
                  subTexture.plane(i).set((y - LTy), (x - LTx), v);
                }
              }
            }
          }
        }
        toc("ColorTransfer");

        if (params.checksum) {
          Checksum checksumRec;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIndex)
                                + "\tTextureTred[" + std::to_string(submeshIdx)
                                + "] ";
          checksumRec.print(subTexture, eString);
          std::cout
            << "**********************************************************\n";
        }
        if (submeshCount == 1) {
          oneTrTexture = subTexture;
          oneOccupancy = occupancy;
        } else
          placeTextures(0,
                        frameIndex,
                        tileIdx,
                        patchIdx,
                        subTexture,  //textureSubmesh
                        oneTrTexture,
                        occupancy,
                        oneOccupancy,
                        !enablePadding && paramsTexture.textureTransferEnable,
                        params);
      }  //patchIdx
    }
    if (!enablePadding && paramsTexture.textureTransferEnable
        && params.textureTransferPaddingMethod != PaddingMethod::NONE) {
      std::cout << "background Filling : " << oneTrTexture.width() << "x"
                << oneTrTexture.height() << "\n";
      //NOTE: textureBackgroundFilling is the same as dilateTexture but the first input is in uint16_t instead of uint_8
      textureBackgroundFilling(oneTrTexture, oneOccupancy, params);
    }
    reconstruct.attributeFrame(0, frameIndex) = oneTrTexture;
    if (params.checksum) {
      Checksum checksumRec;
      std::cout
        << "**********************************************************\n";
      std::string eString =
        "frame " + std::to_string(frameIndex) + "\toneTrTexture ";
      checksumRec.print(oneTrTexture, eString);
      std::cout
        << "**********************************************************\n";
    }

    convertTextureImage(oneTrTexture,
                        frameIndex,
                        sourceAttributeColourSpace_,
                        sourceAttributeBitdetph_,
                        videoAttributeColourSpace_,
                        videoAttributeBitdetph_,
                        paramsTexture.textureVideoDownsampleFilter,
                        paramsTexture.textureVideoFullRange);
    textures16bits.frames().push_back(oneTrTexture);
  }  //frameIndex

  std::cout << '\n';

  textures16bits.setSequenceInfo(paramsTexture.textureWidth,
                                 paramsTexture.textureHeight,
                                 videoAttributeColourSpace_);
  if (0) {
    textures16bits.save(
      textures16bits.createName(_keepFilesPathPrefix + "temp_tex_enc", 10));
  }
  if (params.jointTextDisp) {
    auto& pi = vps.getVpsPackedVideoExtension().getPackingInformation(0);
    pi.pin_region_top_left_x(0, 0);
    pi.pin_region_top_left_y(0, 0);
    pi.pin_region_width_minus1(0, paramsTexture.textureWidth - 1);
    pi.pin_region_height_minus1(0, paramsTexture.textureHeight - 1);
    pi.pin_region_type_id_minus2(0, 2);

    pi.pin_region_top_left_x(1, 0);
    pi.pin_region_top_left_y(1, paramsTexture.textureHeight + 16);
    pi.pin_region_width_minus1(1, dispVideo.width() - 1);
    pi.pin_region_height_minus1(1, dispVideo.height() - 1);
    pi.pin_region_type_id_minus2(0, 1);
  }
  // write dispVideo information
  int widthDispVideo  = dispVideo.width();
  int heightDispVideo = dispVideo.height();
  // std::cout << "widthDispVideo = " << widthDispVideo << ", heightDispVideo = " << heightDispVideo << std::endl;
  textures16bits.standardizeFrameSizes(textures16bits.width(),
                                       textures16bits.height()
                                         + heightDispVideo + 16,
                                       true,
                                       512);
  for (int i = 0; i < textures16bits.frameCount(); i++) {
    auto& texframe  = textures16bits.frame(i);
    auto& dispframe = dispVideo.frame(i);
    for (int row = textures16bits.height() - heightDispVideo - 16;
         row < textures16bits.height();
         row++) {
      for (int col = 0; col < textures16bits.width(); col++) {
        if (row >= textures16bits.height() - heightDispVideo
            && col < widthDispVideo) {
          texframe.plane(0).set(
            row,
            col,
            dispframe.plane(0).get(
              row - (textures16bits.height() - heightDispVideo), col));
          texframe.plane(1).set(row / 2, col / 2, 512);
          texframe.plane(2).set(row / 2, col / 2, 512);
        } else {
          texframe.plane(0).set(row, col, 512);
          texframe.plane(1).set(row / 2, col / 2, 512);
          texframe.plane(2).set(row / 2, col / 2, 512);
        }
      }
    }
  }
  if (0) {
    textures16bits.save(
      textures16bits.createName(_keepFilesPathPrefix + "temp_pac_enc", 10));
    dispVideo.save(
      dispVideo.createName(_keepFilesPathPrefix + "temp_disp_enc", 10));
  }

  for (int i = 0; i < oneOccupancySequence.size(); i++) {
    auto& occFrame = oneOccupancySequence[i];
    occFrame.addPadding(
      textures16bits.width(), textures16bits.height(), true, 1);
  }

  if (paramsTexture.useOccMapRDO) {
    if (!create3DMotionEstimationFiles(oneOccupancySequence,
                                       paramsTexture.occMapFilename)) {
      std::cerr << "Error: fail to create3DMotionEstimationFiles!\n";
      exit(2);
    }
  }
  std::cout << '\n';
  // Encode packed video

  FrameSequence<uint16_t> recPacked;
  auto& packedVideoSubstream = syntax.addVideoSubstream(V3C_PVD);
  packedVideoSubstream.setAtlasId(vps.getAtlasId(vps.getV3CParameterSetId()));
  packedVideoSubstream.setV3CParameterSetId(vps.getV3CParameterSetId());
  bool retTexCoding = compressPackedVideo(packedVideoSubstream,
                                          textures16bits,
                                          recPacked,
                                          reconstruct,
                                          frameOffset,
                                          params);

  return retTexCoding;
}
#endif

//============================================================================
bool
VMCEncoder::compressVideoGeo(std::vector<std::vector<vmesh::VMCSubmesh>>&
                                              encSubmeshes,  //[submesh][frame]
                             int32_t          frameCount,
                             V3cBitstream&    syntax,
                             V3CParameterSet& vps,
                             const VMCEncoderParameters& params) {
  auto colourSpaceDispVideo = ColourSpace::YUV420p;
  //ColourSpace colourSpaceDispVideo = (ColourSpace) params.displacementVideoChromaFormat;
  if (params.displacementVideoChromaFormat == 0) {
    colourSpaceDispVideo = ColourSpace::YUV400p;
  } else if (params.displacementVideoChromaFormat == 1) {
    colourSpaceDispVideo = ColourSpace::YUV420p;
  } else if (params.displacementVideoChromaFormat == 2) {
    colourSpaceDispVideo = ColourSpace::YUV422p;
  } else if (params.displacementVideoChromaFormat == 3) {
    colourSpaceDispVideo = ColourSpace::YUV444p;
  } else {
    colourSpaceDispVideo = ColourSpace::UNKNOW;
  }
  FrameSequence<uint16_t> dispVideo;
  dispVideo.resize(geometryVideoSize.first,
                   geometryVideoSize.second,
                   colourSpaceDispVideo,
                   frameCount,
                   uint16_t((1 << params.geometryVideoBitDepth) >> 1));
  //dispVideo.resize(geometryVideoSize.first, geometryVideoSize.second, colourSpaceDispVideo, frameCount);
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    auto&      dispVideoFrame = dispVideo.frame(frameIdx);
    const auto shift      = uint16_t((1 << params.geometryVideoBitDepth) >> 1);
    const auto planeCount = dispVideoFrame.planeCount();
    for (int32_t p = 0; p < planeCount; ++p) {
      dispVideoFrame.plane(p).fill(shift);
    }

      for (int32_t tileIdx = 0; tileIdx < params.numTilesGeometry; tileIdx++) {
        int32_t patchCount =
          (int32_t)reconAtlasTiles_[tileIdx][frameIdx].patches_.size();
        for (int32_t patchIdx = 0; patchIdx < patchCount; patchIdx++) {
          auto submeshId =
            reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_;
          auto submeshIndex = submeshIdtoIndex_[submeshId];

          auto& subdivInfo =
            encSubmeshes[submeshIndex][frameIdx].subdivInfoLevelOfDetails;
          std::vector<int32_t> vertexCountPerLod(subdivInfo.size());
          for (size_t lod = 0; lod < subdivInfo.size(); lod++) {
            vertexCountPerLod[lod] = subdivInfo[lod].pointCount;
            if (lod != 0)
              vertexCountPerLod[lod] -= subdivInfo[lod - 1].pointCount;
          }

          auto lodIndex =
            reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].lodIdx_;

          if (params.lodPatchesEnable) {
            auto sizeX =
              reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.sizeX;
            auto sizeY =
              reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.sizeY;

            int32_t              blockCount = 0;
            std::vector<lodInfo> lodVertexInfo;
            int32_t              geometryVideoBlockSize =
              (1 << params.log2GeometryVideoBlockSize);
            uint32_t geometryPatchWidthInBlocks = (uint32_t)std::ceil(
              (double)sizeX / (double)geometryVideoBlockSize);
            const uint32_t pointCount =
              encSubmeshes[submeshIndex][frameIdx].disp.size();
            getLodVertexInfo(vertexCountPerLod,
                             lodVertexInfo,
                             pointCount,
                             geometryVideoBlockSize,
                             geometryPatchWidthInBlocks,
                             params.lodPatchesEnable);

            auto geoPatchSizeX = reconAtlasTiles_[tileIdx][frameIdx]
                                   .patches_[patchIdx]
                                   .geometryPatchArea.sizeX;
            auto geoPatchSizeY = reconAtlasTiles_[tileIdx][frameIdx]
                                   .patches_[patchIdx]
                                   .geometryPatchArea.sizeY;
            auto geoPatchLTx =
              reconAtlasTiles_[tileIdx][frameIdx]
                .patches_[patchIdx]
                .geometryPatchArea.LTx
              + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx;
            auto geoPatchLTy =
              reconAtlasTiles_[tileIdx][frameIdx]
                .patches_[patchIdx]
                .geometryPatchArea.LTy
              + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy;

            computeDisplacementVideoFramePerLod(
              encSubmeshes[submeshIndex][frameIdx].disp,
              lodVertexInfo,
              lodIndex,
              geoPatchSizeX,
              geoPatchSizeY,
              geoPatchLTx,
              geoPatchLTy,
              dispVideoFrame,
              params);
          } else {
            auto sizeX = reconAtlasTiles_[tileIdx][frameIdx]
                           .patches_[patchIdx]
                           .geometryPatchArea.sizeX;
            auto sizeY = reconAtlasTiles_[tileIdx][frameIdx]
                           .patches_[patchIdx]
                           .geometryPatchArea.sizeY;
            auto posX =
              reconAtlasTiles_[tileIdx][frameIdx]
                .patches_[patchIdx]
                .geometryPatchArea.LTx
              + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx;
            auto posY =
              reconAtlasTiles_[tileIdx][frameIdx]
                .patches_[patchIdx]
                .geometryPatchArea.LTy
              + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy;

            createDisplacementVideoFrame(
              encSubmeshes[submeshIndex][frameIdx].disp,
              vertexCountPerLod,
              frameIdx,
              tileIdx,
              patchIdx,
              submeshId,
              sizeX,
              sizeY,
              posX,
              posY,
              dispVideoFrame,
              params);
          }
        }  //patch
      }    //tile
  }      //frame

  FrameSequence<uint16_t> dispVideoRec;
  dispVideoRec.resize(geometryVideoSize.first,
                      geometryVideoSize.second,
                      colourSpaceDispVideo,
                      frameCount);
  // Encode displacements video
  auto& geometryVideoSubstream = syntax.addVideoSubstream(V3C_GVD);
  geometryVideoSubstream.setAtlasId(
    vps.getAtlasId(vps.getV3CParameterSetId()));
  geometryVideoSubstream.setV3CParameterSetId(vps.getV3CParameterSetId());
#if CONFORMANCE_LOG_ENC
  TRACE_PICTURE("Geometry\n");
  TRACE_PICTURE("MapIdx = 0, AuxiliaryVideoFlag = 0\n");
#endif
  if (!compressDisplacementsVideo(
        dispVideo, dispVideoRec, geometryVideoSubstream, frameCount, params)) {
    std::cout << "compress Displacement (Video) failed\n";
    return false;
  }

  //reconstruct displacement
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    // clear displacement
      if (params.lodPatchesEnable == 1) {
        for (int32_t submeshIdx = 0; submeshIdx < params.submeshIdList.size();
             submeshIdx++) {
          encSubmeshes[submeshIdx][frameIdx].disp.clear();
        }
      }

      for (int32_t tileIdx = 0; tileIdx < params.numTilesGeometry; tileIdx++) {
        auto numPatchesInTile = params.submeshIdsInTile[tileIdx].size();
        if (params.lodPatchesEnable == 1) {
          numPatchesInTile =
            numPatchesInTile * (params.subdivisionIterationCount + 1);
        }
        for (int32_t patchIdx = 0; patchIdx < numPatchesInTile; patchIdx++) {
          auto submeshId =
            reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].submeshId_;
          auto  submeshIdx = submeshIdtoIndex_[submeshId];
          auto& rec        = reconSubdivmeshes_[submeshIdx][frameIdx];
          if (params.checksum) {
#if ENCDEC_CHECKSUM
            rec.normals().clear();
#endif
            Checksum checksum;
            std::cout << "**********************************************************\n";
            std::string eString = "frame " + std::to_string(frameIdx)
                                  + "\tREC[" + std::to_string(submeshIdx)
                                  + "] submeshId " + std::to_string(submeshId)
                                  + ":";
            checksum.print(rec, eString);
            std::cout << "**********************************************************\n";
          }

          auto sizeX = reconAtlasTiles_[tileIdx][frameIdx]
                         .patches_[patchIdx]
                         .geometryPatchArea.sizeX;
          auto sizeY = reconAtlasTiles_[tileIdx][frameIdx]
                         .patches_[patchIdx]
                         .geometryPatchArea.sizeY;
          auto posX =
            reconAtlasTiles_[tileIdx][frameIdx]
              .patches_[patchIdx]
              .geometryPatchArea.LTx
            + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTx;
          auto posY =
            reconAtlasTiles_[tileIdx][frameIdx]
              .patches_[patchIdx]
              .geometryPatchArea.LTy
            + reconAtlasTiles_[tileIdx][frameIdx].tileGeometryArea_.LTy;
          auto& curPatch =
            reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx];
          std::vector<int32_t> vertexCountPerLod(
            curPatch.blockCount.size());
          for (size_t lod = 0; lod < curPatch.blockCount.size(); lod++) {
            vertexCountPerLod[lod] =
              (curPatch.blockCount[lod] - 1)
              * (1 << params.log2GeometryVideoBlockSize)
              * (1 << params.log2GeometryVideoBlockSize);
            vertexCountPerLod[lod] += curPatch.lastPosInBlock[lod];
            if (curPatch.lastPosInBlock[lod] == 0 || curPatch.blockCount[lod] == 0) {
              vertexCountPerLod[lod] =
                curPatch.blockCount[lod]
                * (1 << params.log2GeometryVideoBlockSize)
                * (1 << params.log2GeometryVideoBlockSize);
            }
          }

          if (params.lodPatchesEnable == 1) {
            auto lodIdx =
              reconAtlasTiles_[tileIdx][frameIdx].patches_[patchIdx].lodIdx_;
            auto patchVertexCount = vertexCountPerLod[lodIdx];
            reconstructDisplacementFromVideoFramemPerLod(
              dispVideoRec.frame(frameIdx),
              patchVertexCount,
              encSubmeshes[submeshIdx][frameIdx].disp,
              sizeX,
              sizeY,
              posX,
              posY,
              (1 << params.log2GeometryVideoBlockSize),
              params.geometryVideoBitDepth,
              params.applyOneDimensionalDisplacement,
              params.displacementReversePacking,
              (ColourSpace)params.displacementVideoChromaFormat);
          } else {
            reconstructDisplacementFromVideoFrame(
              dispVideoRec.frame(frameIdx),
              vertexCountPerLod,
              encSubmeshes[submeshIdx][frameIdx].disp,
              rec,
              frameIdx,
              tileIdx,
              patchIdx,
              submeshId,
              sizeX,
              sizeY,
              posX,
              posY,
              (1 << params.log2GeometryVideoBlockSize),
              params.geometryVideoBitDepth,
              params.applyOneDimensionalDisplacement,
              params.displacementReversePacking,
              (ColourSpace)params.displacementVideoChromaFormat);
          }

          if (params.checksum) {
            Checksum checksum;
            std::cout << "**********************************************************\n";
            std::string eString = "frame " + std::to_string(frameIdx)
                                  + "\t(recon)submeshDisp["
                                  + std::to_string(submeshIdx) + "] submeshId "
                                  + std::to_string(submeshId) + ":";
            checksum.print(encSubmeshes[submeshIdx][frameIdx].disp, eString);
            std::cout << "**********************************************************\n";
          }
        }  //patch

        uint32_t submeshCountInTile = params.submeshIdsInTile[tileIdx].size();
        for (uint32_t submeshIndexInTile = 0;
             submeshIndexInTile < submeshCountInTile;
             submeshIndexInTile++) {
          auto submeshId =
            params.submeshIdsInTile[tileIdx][submeshIndexInTile];
          auto submeshIdx = submeshIdtoIndex_[submeshId];
          auto basePatchIndex =
            getPatchIdx(reconAtlasTiles_[tileIdx][frameIdx], submeshId, 0);
          if (basePatchIndex == -1) { continue; }
          reconstructPositions(encSubmeshes[submeshIdx],
                               frameIdx,
                               tileIdx,
                               submeshIdx,
                               basePatchIndex,
                               syntax,
                               vps,
                               params);
        }
      }  //tile
  }    //frame
  return true;
}

//============================================================================
bool
VMCEncoder::reconstructPositions(
  std::vector<vmesh::VMCSubmesh>& encSubmeshes,  //[frame]
  int32_t                         frameIndex,
  int32_t                         tileIndex,
  int32_t                         submeshIdx,
  int32_t                         patchIndex,
  V3cBitstream&                   syntax,
  V3CParameterSet&                vps,
  const VMCEncoderParameters&     params) {
  auto&   rec             = reconSubdivmeshes_[submeshIdx][frameIndex];
  // requires submeshIdx => submeshIndexInTile
  //int32_t submeshIdInTile = params.submeshIdsInTile[tileIndex][submeshIdx]; // requires submeshIdx => submeshIndexInTile
  int32_t submeshIdInTile = -1;
  for (auto index = 0; index < submeshIdtoIndex_.size(); index++) {
    if (submeshIdtoIndex_[index] == submeshIdx)
      submeshIdInTile = index;
  }
  auto&   submeshRecDisp  = encSubmeshes[frameIndex].disp;
  // determining the inverse quantization scale
  int dispDimension = (params.applyOneDimensionalDisplacement == 1) ? 1 : 3;
  int lodCount      = encSubmeshes[frameIndex].subdivInfoLevelOfDetails.size();
  if (!params.IQSkipFlag) {
    std::vector<std::vector<double>> iscale;
    std::vector<std::vector<double>> QuantizationParameter[3];
    std::vector<std::vector<double>> InverseScale;
    //initialization
    iscale.resize(lodCount);
    for (int i = 0; i < lodCount; i++) iscale[i].resize(dispDimension, 0);
    for (int i = 0; i < 3; i++) {
      QuantizationParameter[i].resize(lodCount);
      for (int l = 0; l < lodCount; l++)
        QuantizationParameter[i][l].resize(dispDimension, 0);
    }
    InverseScale.resize(lodCount);
    for (int i = 0; i < lodCount; i++)
      InverseScale[i].resize(dispDimension, 0);
    //QuantizationParameter
    int qpIdx = 0;  // THE ENCODER IS ONLY USING ASPS FOR NOW
    for (int idx = 0; idx < 3; idx++) {
      if (params.lodDisplacementQuantizationFlag == 0) {
        for (int d = 0; d < dispDimension; d++)
          for (int l = 0; l < lodCount; l++)
            QuantizationParameter[idx][l][d] = params.liftingQP[d];
      } else {
        for (int l = 0; l < lodCount; l++)
          for (int d = 0; d < dispDimension; d++) {
            QuantizationParameter[idx][l][d] =
              params.qpPerLevelOfDetails[l][d];
          }
      }
    }
    //InverseScale
    for (int l = 0; l < lodCount; l++)
      for (int d = 0; d < dispDimension; d++) {
        if (params.displacementQuantizationType
            == DisplacementQuantizationType::ADAPTIVE) {
          InverseScale[l][d] = 1.0 / QuantizationParameter[qpIdx][l][d];
        } else {
          InverseScale[l][d] =
            pow(0.5,
                16 + params.bitDepthOffset - params.bitDepthPosition
                  + (4 - QuantizationParameter[qpIdx][l][d]) / 6.0);
        }
      }
    // iscale
    if (params.lodDisplacementQuantizationFlag) {
      for (int l = 0; l < lodCount; l++)
        for (int d = 0; d < dispDimension; d++)
          iscale[l][d] = InverseScale[l][d];
    } else {
      std::vector<double> levelOfDetailInverseScale;
      levelOfDetailInverseScale.resize(dispDimension, 0);
      for (int d = 0; d < dispDimension; d++) {
        iscale[0][d] = InverseScale[0][d];
        levelOfDetailInverseScale[d] =
          std::pow(2, params.log2LevelOfDetailInverseScale[d]);
      }
      for (int l = 1; l < lodCount; l++)
        for (int d = 0; d < dispDimension; d++)
          iscale[l][d] = iscale[l - 1][d] * levelOfDetailInverseScale[d];
    }
      std::vector<double> levelOfDetailInverseScale;
      levelOfDetailInverseScale.resize(dispDimension, 0);
      for (int d = 0; d < dispDimension; d++) {
        iscale[0][d] = InverseScale[0][d];
        levelOfDetailInverseScale[d] =
          std::pow(2, params.log2LevelOfDetailInverseScale[d]);
      }
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].IQ_skip =
      false;
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.BitDepthOffset = params.bitDepthOffset;
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.directQuantFlag =
      (params.displacementQuantizationType
        == DisplacementQuantizationType::ADAPTIVE);
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.iscale = iscale;
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.lodQp = QuantizationParameter[qpIdx];
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.lodQuantFlag =
      params.lodDisplacementQuantizationFlag;
    reconAtlasTiles_[tileIndex][frameIndex]
      .patches_[patchIndex]
      .quantizationParameters.log2InverseScale =
      params.log2LevelOfDetailInverseScale;
    if (params.InverseQuantizationOffsetFlag) {
      auto&               IQOffset = encSubmeshes[frameIndex].IQOffset;
      computeInverseQuantizationOffset(encSubmeshes[frameIndex],
                                       iscale,
                                       dispDimension,
                                       levelOfDetailInverseScale,
                                       IQOffset);
      auto&       adStream = syntax.getAtlasDataStream();
      const auto& asps     = adStream.getAtlasSequenceParameterSet(0);
      auto&       atl =
        adStream.getAtlasTileLayerList()[encSubmeshes[frameIndex].atlPos];
      auto& ath  = atl.getHeader();
      auto& atdu = atl.getDataUnit();
      auto& pi   = atdu.getPatchInformationData(patchIndex);
      if ((ath.getType() == I_TILE && pi.getPatchMode() == I_INTRA)
          || (ath.getType() == P_TILE && pi.getPatchMode() == P_INTRA)) {
        auto& pdu = pi.getMeshpatchDataUnit();
        auto& qp  = pdu.getMduQuantizationParameters();
        // qp = asps.getAsveExtension().getAsveQuantizationParameters();
        pdu.setMduInverseQuantizationOffsetEnableFlag(true);
        // pdu.setMduParametersOverrideFlag(true);
        // pdu.setMduQuantizationPresentFlag(true);
        pdu.setIQOffsetValuesSize(lodCount, dispDimension);
        pdu.getIQOffsetValues() = IQOffset;
        /*
        Not sure why we are zeroing the values here, commenting for now
        if (params.lodDisplacementQuantizationFlag) {
          int8_t value = 0;
          qp.setNumLod(lodCount);
          for (int i = 0; i < lodCount; i++) {
            qp.allocComponents(dispDimension, i);
            for (int j = 0; j < dispDimension; j++) {
              qp.setVdmcLodDeltaQPValue(i, j, std::abs(value));
              qp.setVdmcLodDeltaQPSign(i, j, (value < 0) ? 1 : 0);
            }
          }
        }*/
      } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_INTER)) {
        auto& imdu = pi.getInterMeshpatchDataUnit();
        auto& qp   = imdu.getImduQuantizationParameters();
        qp         = asps.getAsveExtension().getAsveQuantizationParameters();
        imdu.setImduInverseQuantizationOffsetEnableFlag(true);
        imdu.setIQOffsetValuesSize(lodCount, dispDimension);
        imdu.getIQOffsetValues() = IQOffset;
      } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_MERGE)) {
        auto& mmdu = pi.getMergeMeshpatchDataUnit();
        auto& qp   = mmdu.getMmduQuantizationParameters();
        qp = asps.getAsveExtension().getAsveQuantizationParameters();
        mmdu.setMmduInverseQuantizationOffsetEnableFlag(true);
        mmdu.setIQOffsetValuesSize(lodCount, dispDimension);
        mmdu.getIQOffsetValues() = IQOffset;
      } else if ((ath.getType() == P_TILE && pi.getPatchMode() == P_SKIP)) {
      }
      reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].iqOffsets =
        IQOffset;
      reconAtlasTiles_[tileIndex][frameIndex]
        .patches_[patchIndex]
        .iqOffsetFlag = true;
    }

    if (params.transformMethod == 1)
      inverseQuantizeDisplacements(encSubmeshes[frameIndex],
                                   iscale,
                                   dispDimension,
                                   params.InverseQuantizationOffsetFlag,
                                   encSubmeshes[frameIndex].IQOffset);
    else {
      std::vector<std::vector<std::vector<std::vector<int8_t>>>> IQOffset;
      inverseQuantizeDisplacements(
        encSubmeshes[frameIndex], iscale, dispDimension, false, IQOffset);
    }

  } else
    reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].IQ_skip =
      true;
  if (params.checksum) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\tdeQsubmeshDisp[" + std::to_string(submeshIdx)
                          + "] submeshId " + std::to_string(submeshIdInTile)
                          + ":";
    checksum.print(submeshRecDisp, eString);
    std::cout
      << "**********************************************************\n";
  }
  //mesh.h
  std::vector<double> predWeight;
  predWeight.resize(lodCount - 1, 0);
  for (int l = 0; l < lodCount - 1; l++) {
    if (params.liftingAdaptivePredictionWeightFlag) {
      predWeight[l] =
        (double)(params.liftingPredictionWeightNumerator[l])
        / (double)(params.liftingPredictionWeightDenominatorMinus1[l] + 1);
    } else {
      predWeight[l] =
        (double)(params.liftingPredictionWeightNumeratorDefault[l])
        / (double)(params.liftingPredictionWeightDenominatorMinus1Default[l]
                   + 1);
    }
  }
  reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].predWeight_ =
    predWeight;
  std::vector<double> updateWeight;
  updateWeight.resize(lodCount - 1, 0);
  if (params.liftingSkipUpdate) {
    // updateWeight[l] = 0
  } else {
    for (int l = 0; l < lodCount - 1; l++) {
      if ((params.liftingAdaptiveUpdateWeightFlag == 1) || (l == 0)) {
        updateWeight[l] =
          (double)params.liftingUpdateWeightNumerator[l]
          / (double)(params.liftingUpdateWeightDenominatorMinus1[l] + 1);
      } else {
        updateWeight[l] = updateWeight[0];
      }
    }
  }
  reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].updateWeight_ =
    updateWeight;

  std::vector<double> liftingOffsets;
  std::vector<double> liftingScales;
  double              dirliftScale1 = 0;
  double              dirliftScale2 = 0;
  double              dirliftScale3 = 0;
  if (params.transformMethod == 1) {
    liftingOffsets.resize(lodCount - 1, 0);
    liftingScales.resize(2, 0);
    for (int l = 0; l < lodCount - 1; l++) {
      liftingOffsets[l] =
        (double)((int32_t(encSubmeshes[frameIndex].liftingdisplodmean[l]
                          * 100.0))
                 / 100.0);
    }
    if (params.dirlift) {
      for (int l = 0; l < 2; l++) {
        liftingScales[l] =
          (double)((int32_t(encSubmeshes[frameIndex].liftingdispstat[l]
                            * 100.0))
                   / 100.0);
      }
      dirliftScale1 = (double)params.dirliftScale1
                      / (double)(params.dirliftScaleDenoMinus1 + 1);
      dirliftScale2 = dirliftScale1
                      + (double)params.dirliftDeltaScale2
                          / (double)(params.dirliftScaleDenoMinus1 + 1);
      dirliftScale3 = dirliftScale2
                      + (double)params.dirliftDeltaScale3
                          / (double)(params.dirliftScaleDenoMinus1 + 1);
    }
    computeInverseLinearLifting(
      encSubmeshes[frameIndex].disp,
      encSubmeshes[frameIndex].subdivInfoLevelOfDetails,
      encSubmeshes[frameIndex].subdivEdges,
      rec.valences(),
      params.liftingValenceUpdateWeightFlag,
      liftingOffsets,
      params.applyLiftingOffset,
      predWeight,
      updateWeight,
      params.liftingSkipUpdate,
      liftingScales,
      params.dirlift,
      dirliftScale1,
      dirliftScale2,
      dirliftScale3);
  } else {
    std::cout << "None transform - no inverse linear lifting" << std::endl;
  }

  if (params.checksum) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\tinvTrsubmeshDisp[" + std::to_string(submeshIdx)
                          + "] submeshId " + std::to_string(submeshIdInTile)
                          + ":";
    checksum.print(submeshRecDisp, eString);
    std::cout
      << "**********************************************************\n";
  }
  applyDisplacements(encSubmeshes[frameIndex].disp,
                     params.bitDepthPosition,
                     rec,
                     params.displacementCoordinateSystem,
                     params.interpolateSubdividedNormalsFlag);
  if (params.checksum) {
    Checksum checksum;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\treconFinal[" + std::to_string(submeshIdx)
                          + "] submeshId " + std::to_string(submeshIdInTile)
                          + ":";
    checksum.print(rec, eString);
    std::cout
      << "**********************************************************\n";
  }

  return true;
}

//============================================================================
bool
VMCEncoder::compressVideoTexture(std::vector<std::vector<vmesh::VMCSubmesh>>&
                                           encSubmeshes,  //[submesh][frame]
                                 Sequence& reconstruct,
                                 int32_t   submeshCount,
                                 int32_t   frameCount,
                                 int32_t   frameOffset,
                                 int32_t   attrIdx,
                                 V3cBitstream&               syntax,
                                 V3CParameterSet&            vps,
                                 const VMCEncoderParameters& params) {
  //TODO: is it 0 or attrIdx?
  auto  sourceAttributeColourSpace_ = sourceAttributeColourSpaces_[0];
  auto  sourceAttributeBitdetph_    = sourceAttributeBitdetphs_[0];
  auto  videoAttributeColourSpace_  = videoAttributeColourSpaces_[0];
  auto  videoAttributeBitdetph_     = videoAttributeBitdetphs_[0];
  auto  textureVideoSize            = attributeVideoSize_[0];
  auto& paramsTexture               = params.attributeParameters[0];
  auto& inputTexturePath_           = inputAttributesPaths_[0];

  int numOutputTextures = params.dracoMeshLossless ? params.numTextures : 1;
#if 1
  if (params.numTilesAttribute != 1 || params.numSubmesh != 1) {
    std::cout << "***subtexture placement---------------------------\n";
    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < params.numTilesGeometry + params.numTilesAttribute;
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto LTx = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTx;
        auto LTy = reconAtlasTiles_[tileIdx][0]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTy;
        auto sizeX = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][0]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeY;
        std::cout << "Tile[" << tileIdx << "] patch[" << patchIdx << "] ";
        std::cout << "submeshId " << submeshId;
        std::cout << " subTexture[" << submeshIdtoIndex_[submeshId] << "]";
        std::cout << " pos in Tile: [" << LTx << "," << LTy << "]";
        //std::cout << " pos in Frame: [" << LTx << "," << LTy <<"]";
        std::cout << " size in Tile: " << sizeX << "x" << sizeY << "\n";
      }
    }
  }
#endif
  // allocating output structures
  reconstruct.attribute(attrIdx).resize(textureVideoSize.first,
                                        textureVideoSize.second,
                                        ColourSpace::BGR444p,
                                        frameCount);
  bool                           retTexCoding = true;
  vmesh::FrameSequence<uint16_t> textures16bits;
  int                            numLayerMinus1 = params.maxLayersMinus1;
  std::vector<vmesh::FrameSequence<uint16_t>> textures16bitsLayers(
    numLayerMinus1 + 1);
  // create a sequence of uncompressed texture frames
  std::vector<Plane<uint8_t>> oneOccupancySequence(frameCount);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    std::vector<Frame<uint8_t>> sourceTexture;
    auto                        sourceTexturePath = inputTexturePath_;
    std::vector<Vec2<int32_t>>  position;
    std::vector<Vec2<int32_t>>  dimension;
    bool                        updateSubmeshUV = false;
    if (params.numTextures > 1) {
      vmesh::TriangleMesh<MeshType> sourceMesh;
      auto                          sourceMeshPath = inputMeshPath_;
      // the names of textures is located in the material file indicated by the mesh, so we need to read the mesh again
      if (!sourceMesh.load(sourceMeshPath,
                           frameIndex + frameOffset + startFrame_)) {
        printf("fail to load mesh : %s frame %d\n",
               sourceMeshPath.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      }
      auto listOfIndices = sourceMesh.materialIdxs();
      std::sort(listOfIndices.begin(), listOfIndices.end());
      listOfIndices.erase(unique(listOfIndices.begin(), listOfIndices.end()),
                          listOfIndices.end());
      int textureCount = listOfIndices.size();
      if (textureCount != params.numTextures) {
        printf("source mesh does not contain the correct number of texture "
               "URLs (%d != %d): %s frame %d\n",
               textureCount,
               params.numTextures,
               sourceMeshPath.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      }

      if (!params.dracoMeshLossless) {
        sourceTexture.resize(sourceMesh.textureMapUrls().size());
        for (int i = 0; i < sourceMesh.textureMapUrls().size(); i++) {
          auto& url = sourceMesh.textureMapUrls()[i];
          if (!url.empty()) {
            sourceTexturePath =
              dirname(sourceMeshPath) + sourceMesh.textureMapUrls()[i];
            if (!sourceTexture[i].load(
                  sourceTexturePath, frameIndex + frameOffset + startFrame_)) {
              printf("fail to load texture : %s frame %d\n",
                     sourceTexturePath.c_str(),
                     frameIndex + frameOffset + startFrame_);
              exit(2);
            };
          }
        }
      } else {
        sourceTexture.resize(1);
        // for lossless compression, we just read and encode the image corresponding to the attribute index
        sourceTexturePath =
          dirname(sourceMeshPath)
          + sourceMesh.textureMapUrls()[listOfIndices[attrIdx]];
        if (!sourceTexture[0].load(sourceTexturePath,
                                   frameIndex + frameOffset + startFrame_)) {
          printf("fail to load texture : %s frame %d\n",
                 sourceTexturePath.c_str(),
                 frameIndex + frameOffset + startFrame_);
          exit(2);
        };
      }
    } else {
      sourceTexture.resize(1);
      if (!sourceTexture[0].load(sourceTexturePath,
                                 frameIndex + frameOffset + startFrame_)) {
        printf("fail to load texture : %s frame %d\n",
               inputTexturePath_.c_str(),
               frameIndex + frameOffset + startFrame_);
        exit(2);
      };
    }
    Frame<uint16_t> oneTrTexture;
    oneTrTexture.resize(textureVideoSize.first,
                        textureVideoSize.second,
                        sourceAttributeColourSpace_);
    bool enablePadding           = true;
    enablePadding                = (params.numSubmesh == 1);
    Plane<uint8_t>& oneOccupancy = oneOccupancySequence[frameIndex];
    if (!enablePadding)
      oneOccupancy.resize(textureVideoSize.first, textureVideoSize.second);

    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < params.numTilesGeometry + params.numTilesAttribute;
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto submeshId =
          reconAtlasTiles_[tileIdx][0].patches_[patchIdx].submeshId_;
        auto submeshIdx = submeshIdtoIndex_[submeshId];
        //TODO: is it 0 or attIdx?
        auto sizeX = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeX;
        auto sizeY = reconAtlasTiles_[tileIdx][frameIndex]
                       .patches_[patchIdx]
                       .attributePatchArea[0]
                       .sizeY;
        auto LTx =
          reconAtlasTiles_[tileIdx][frameIndex]
            .patches_[patchIdx]
            .attributePatchArea[0]
            .LTx
          + reconAtlasTiles_[tileIdx][frameIndex].tileAttributeAreas_[0].LTx;
        auto LTy = reconAtlasTiles_[tileIdx][frameIndex]
                     .patches_[patchIdx]
                     .attributePatchArea[0]
                     .LTy;
        +reconAtlasTiles_[tileIdx][frameIndex].tileAttributeAreas_[0].LTx;

        vmesh::TriangleMesh<MeshType> sourceMesh;
        auto                          sourceMeshPath = inputMeshPath_;
        if (params.numSubmesh != 1 && !params.segmentByBaseMesh
            && !(params.numTextures > 1)) {
          sourceMeshPath = inputSubmeshPaths_[submeshIdx];
        }
        if (!sourceMesh.load(sourceMeshPath,
                             frameIndex + frameOffset + startFrame_)) {
          printf("fail to load mesh : %s frame %d submesh %d\n",
                 sourceMeshPath.c_str(),
                 frameIndex + frameOffset + startFrame_,
                 submeshIdx);
          exit(2);
        }
        Frame<uint8_t> subTexture;
        auto           subTextureWidth  = textureVideoSize.first;
        auto           subTextureHeight = textureVideoSize.second;

        if (params.numSubmesh != 1) {
          //predefined submesh texture image size
          subTextureWidth  = sizeX;
          subTextureHeight = sizeY;
        }
        subTexture.resize(
          subTextureWidth, subTextureHeight, sourceAttributeColourSpace_);
        TriangleMesh<MeshType>& recSubmesh =
          reconSubdivmeshes_[submeshIdx][frameIndex];
        Plane<uint8_t> occupancy;
        tic("ColorTransfer");
        if (paramsTexture.textureTransferEnable) {
          TransferColor  transferColor;
          auto&          refMesh        = sourceMesh;
          auto&          refTexture     = sourceTexture;
          auto&          targetMesh     = recSubmesh;
          auto&          targetTexture  = subTexture;
          bool           usePastTexture = false;
          Frame<uint8_t> pastTexture;
          //TODO: [sw] reconstruct.texture memory management
          if (submeshCount == 1 && params.textureTransferCopyBackground
              && frameIndex > 0
              && encSubmeshes[submeshIdx][frameIndex].submeshType
                   == basemesh::P_BASEMESH) {
            //occupancy map will be the same as the reference, so use the background from reference as well
            //TODO: 0 or attrIdx?
            pastTexture = reconstruct.attributeFrame(
              0, encSubmeshes[submeshIdx][frameIndex].referenceFrameIndex);
            //std::string filename = "_test_"+std::to_string(frameIndex)+".png";
            //pastTexture.save(filename);
            usePastTexture = true;
          }
          if (!transferColor.transfer(refMesh,
                                      refTexture,
                                      targetMesh,
                                      targetTexture,
                                      usePastTexture,
                                      pastTexture,
                                      params,
                                      occupancy,
                                      enablePadding,
                                      submeshIdx)) {
            printf("fail to transferTexture\n");
            return false;
          }
        } else {
          //subTexture = sourceTexture;
          //reconstruct.texture(frameIndex) = sourceTexture;
          //the source mesh has only one texture if we are just copying and not doing any texture transfer
          if (submeshCount == 1) {
            subTexture = sourceTexture[0];
          } else {
            for (int32_t i = 0; i < oneTrTexture.planeCount(); i++) {
              for (int32_t y = LTy; y < (LTy + sizeY); y++) {
                for (int32_t x = LTx; x < (LTx + sizeX); x++) {
                  auto v = sourceTexture[0].plane(i).get(y, x);
                  subTexture.plane(i).set((y - LTy), (x - LTx), v);
                }
              }
            }
          }
        }
        toc("ColorTransfer");

        if (params.checksum) {
          Checksum checksumRec;
          std::cout
            << "**********************************************************\n";
          std::string eString = "frame " + std::to_string(frameIndex)
                                + "\tTextureTred[" + std::to_string(submeshIdx)
                                + "] ";
          checksumRec.print(subTexture, eString);
          std::cout
            << "**********************************************************\n";
        }
        if (submeshCount == 1) {
          oneTrTexture = subTexture;
          oneOccupancy = occupancy;
        } else
          placeTextures(0,  //TODO: is it 0 or attrIdx?
                        frameIndex,
                        tileIdx,
                        patchIdx,
                        subTexture,  //textureSubmesh
                        oneTrTexture,
                        occupancy,
                        oneOccupancy,
                        !enablePadding && paramsTexture.textureTransferEnable,
                        params);
      }  //patchIdx
    }
    if (!enablePadding && paramsTexture.textureTransferEnable
        && params.textureTransferPaddingMethod != PaddingMethod::NONE) {
      std::cout << "background Filling : " << oneTrTexture.width() << "x"
                << oneTrTexture.height() << "\n";
      //NOTE: textureBackgroundFilling is the same as dilateTexture but the first input is in uint16_t instead of uint_8
      textureBackgroundFilling(oneTrTexture, oneOccupancy, params);
    }
    reconstruct.attributeFrame(attrIdx, frameIndex) = oneTrTexture;
    if (params.checksum) {
      Checksum checksumRec;
      std::cout
        << "**********************************************************\n";
      std::string eString =
        "frame " + std::to_string(frameIndex) + "\toneTrTexture ";
      checksumRec.print(oneTrTexture, eString);
      std::cout
        << "**********************************************************\n";
    }

    convertTextureImage(oneTrTexture,
                        frameIndex,
                        sourceAttributeColourSpace_,
                        sourceAttributeBitdetph_,
                        videoAttributeColourSpace_,
                        videoAttributeBitdetph_,
                        paramsTexture.textureVideoDownsampleFilter,
                        paramsTexture.textureVideoFullRange);
    if (params.scalableEnableFlag) {
      textures16bitsLayers[numLayerMinus1].frames().push_back(oneTrTexture);
      auto&                        vpsExt = vps.getVpsVdmcExtension();
      int                          textureWidth, textureHeight;
      std::vector<Frame<uint8_t>>  textureLoDVec(numLayerMinus1);
      std::vector<Frame<uint16_t>> oneTrsTexture(numLayerMinus1);
      for (int i = 0; i < numLayerMinus1; i++) {
        textureWidth  = textureVideoSize.first >> (numLayerMinus1 - i);
        textureHeight = textureVideoSize.second >> (numLayerMinus1 - i);
        textureLoDVec[i].resize(
          textureWidth, textureHeight, videoAttributeColourSpace_);
        oneTrsTexture[i].resize(
          textureWidth, textureHeight, videoAttributeColourSpace_);
      }
      std::cout
        << "smoothed pushpull-based multi-resolution texture map generation"
        << std::endl;
      SmoothedPushPullMipMap(reconstruct.attributeFrame(attrIdx, frameIndex),
                             textureLoDVec,
                             oneOccupancySequence[frameIndex],
                             numLayerMinus1);
      for (int i = 0; i < numLayerMinus1; i++) {
        oneTrsTexture[i] = textureLoDVec[i];
        convertTextureImage(oneTrsTexture[i],
                            frameIndex,
                            sourceAttributeColourSpace_,
                            sourceAttributeBitdetph_,
                            videoAttributeColourSpace_,
                            videoAttributeBitdetph_,
                            paramsTexture.textureVideoDownsampleFilter,
                            paramsTexture.textureVideoFullRange);
        textures16bitsLayers[i].frames().push_back(oneTrsTexture[i]);
      }
    } else textures16bits.frames().push_back(oneTrTexture);
  }  //frameIndex
  if (params.scalableEnableFlag) {
    for (int i = 0; i <= numLayerMinus1; i++) {
      auto        width  = textures16bitsLayers[i].frame(0).width();
      auto        height = textures16bitsLayers[i].frame(0).height();
      std::string result = removeExtension(params.resultPath);
      std::string multilayerTexures = result + "_" + std::to_string(width)
                                      + "x" + std::to_string(height) + ".yuv";
      textures16bitsLayers[i].save(multilayerTexures);
    }
  }
  if (paramsTexture.useOccMapRDO && !params.scalableEnableFlag) {
    if (!create3DMotionEstimationFiles(oneOccupancySequence,
                                       paramsTexture.occMapFilename)) {
      std::cerr << "Error: fail to create3DMotionEstimationFiles!\n";
      exit(2);
    }
  }
  std::cout << '\n';
  // compress texture
  auto& attributeVideoSubstream = syntax.addVideoSubstream(V3C_AVD);
  attributeVideoSubstream.setAtlasId(
    vps.getAtlasId(vps.getV3CParameterSetId()));
  attributeVideoSubstream.setV3CParameterSetId(vps.getV3CParameterSetId());
  attributeVideoSubstream.setVuhAttributeIndex(attrIdx);
#if CONFORMANCE_LOG_ENC
  TRACE_PICTURE("Attribute\n");
  int attrTypeId =
    vps.getAttributeInformation(vps.getAtlasId(vps.getV3CParameterSetId()))
      .getAttributeTypeId(attrIdx);
  TRACE_PICTURE("MapIdx = 0, AuxiliaryVideoFlag = 0, AttrIdx = %d, "
                "AttrPartIdx = 0, AttrTypeID = %d\n",
                attrIdx,
                attrTypeId);
#endif
  retTexCoding &= compressTextureVideo(
    attributeVideoSubstream,
    (!params.scalableEnableFlag) ? textures16bits
                                 : textures16bitsLayers[numLayerMinus1],
    reconstruct,
    attrIdx,
    frameOffset,
    params);
  return retTexCoding;
}

//============================================================================
bool
VMCEncoder::create3DMotionEstimationFiles(
  std::vector<Plane<uint8_t>>& occupancySequence,
  const std::string&           path) {
  FILE*   occupancyFile = fopen(path.c_str(), "wb");
  if (occupancyFile == nullptr) {
    std::cerr << "Error: fail to open occupancyFile: " << path << std::endl;
    return false;
  }
  int32_t frameSize =
    occupancySequence[0].width() * occupancySequence[0].height();
  unsigned char* occFileData = new unsigned char[frameSize];

  for (size_t frIdx = 0; frIdx < occupancySequence.size(); ++frIdx) {
    auto&    frame   = occupancySequence[frIdx];
    uint32_t zeroVal = 0, oneVal = 1;
    uint32_t val255 = 255;
    for (int y = 0; y < frame.height(); y++) {
      for (int x = 0; x < frame.width(); x++) {
        occFileData[y * frame.width() + x] = (frame.get(y, x) > 0) ? 1 : 0;
      }
    }
    fwrite(occFileData, sizeof(unsigned char), frameSize, occupancyFile);
  }  //fIdx
  fclose(occupancyFile);
  delete[] occFileData;
  return true;
}

//============================================================================
bool
VMCEncoder::initializeImageSizesAndTiles(const VMCEncoderParameters& params,
                                 int32_t                     frameCount) {
  uint32_t submeshCount = params.numSubmesh;
  uint32_t tileCount    = params.numTilesGeometry + params.numTilesAttribute;

  std::vector<imageArea> subGeometryPatchAreas;
  std::vector<imageArea> subTextureVideoAreas;
  std::vector<imageArea> geometryTileAreas;
  std::vector<imageArea> textureTileAreas;
  uint32_t               geoVideoWidth = params.geometryVideoWidthInBlocks
                           * (1 << params.log2GeometryVideoBlockSize);

  texturePackingSize.first  = params.texturePackingWidth;
  texturePackingSize.second = params.texturePackingHeight;
  attributeVideoSize_.resize(params.videoAttributeCount);
  for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
    attributeVideoSize_[attIdx].first =
      params.attributeParameters[attIdx].textureWidth;
    attributeVideoSize_[attIdx].second =
      params.attributeParameters[attIdx].textureHeight;
  }
  uint32_t texVideoWidth =
    params.videoAttributeCount > 0 ? attributeVideoSize_[0].first : 0;
  uint32_t texVideoHeight =
    params.videoAttributeCount > 0 ? attributeVideoSize_[0].second : 0;

  tileAreasInVideo_.resize(tileCount);
  for (uint32_t i = 0; i < tileCount; i++) {
    tileAreasInVideo_[i].resize(frameCount);
    for (uint32_t f = 0; f < frameCount; f++) {
      if (i < params.numTilesGeometry) {
        tileAreasInVideo_[i][f].tileType_ = I_TILE;
      } else {
        tileAreasInVideo_[i][f].tileType_ = I_TILE_ATTR;
      }
      tileAreasInVideo_[i][f].tileId_ = params.tileIdList[i];
      tileAreasInVideo_[i][f].tileAttributeAreas_.resize(
        params.videoAttributeCount);
      tileAreasInVideo_[i][f].tileGeometryArea_ =
        imageArea{geoVideoWidth, 0, 0, 0};
      for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
        tileAreasInVideo_[i][f].tileAttributeAreas_[attIdx] =
          imageArea{(uint32_t)attributeVideoSize_[0].first,
                    (uint32_t)attributeVideoSize_[0].second,
                    0,
                    0};
      }
      auto patchCount = params.submeshIdsInTile[i].size();
      if (params.lodPatchesEnable == 1
          && tileAreasInVideo_[i][f].tileType_ == I_TILE) {
        patchCount = patchCount * (params.subdivisionIterationCount + 1);
      }
      tileAreasInVideo_[i][f].patches_.resize(patchCount);
      for (auto& patch : tileAreasInVideo_[i][f].patches_) {
        patch.geometryPatchArea = imageArea{geoVideoWidth, 0, 0, 0};
        patch.attributePatchArea.resize(params.videoAttributeCount);
        for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
          patch.attributePatchArea[attIdx] =
            imageArea{(uint32_t)attributeVideoSize_[0].first,
                      (uint32_t)attributeVideoSize_[0].second,
                      0,
                      0};
        }
      }
    }
  }

  if (!params.encodeTextureVideo) {
    for (uint32_t fi = 0; fi < frameCount; fi++) {
      for (int ti = 0; ti < tileCount; ti++) {
        auto patchCount = params.submeshIdsInTile[ti].size();
        if (params.lodPatchesEnable == 1) {
          patchCount = patchCount * (params.subdivisionIterationCount + 1);
        }
        for (int pi = 0; pi < patchCount; pi++) {
          for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
            auto& areaTexture =
              tileAreasInVideo_[ti][fi].patches_[pi].attributePatchArea[0];
            areaTexture.sizeX = texVideoWidth;
            areaTexture.sizeY = texVideoHeight;
            areaTexture.LTx   = 0;
            areaTexture.LTy   = 0;
          }  //attIdx
        }    //pi
      }      //ti
    }        //fi
  } else {
    if (params.numTilesGeometry == 1) {
      for (uint32_t fi = 0; fi < frameCount; fi++) {
        for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
          std::vector<imageArea> subTextureVideoAreas(submeshCount);
          setSubtextureVideoSize(
            params.attributeParameters[attIdx].textureWidth,
            params.attributeParameters[attIdx].textureHeight,
            subTextureVideoAreas,
            submeshCount);
          for (int i = 0; i < tileAreasInVideo_[1][fi].patches_.size(); i++)
            tileAreasInVideo_[1][fi]
              .patches_[i]
              .attributePatchArea[attIdx] = subTextureVideoAreas[i];
        }
      }
    } else if (params.numTilesGeometry == 2 && submeshCount == 3) {
      //textureTile
      for (uint32_t fi = 0; fi < frameCount; fi++) {
        //attributeTiles: std::vector<imageArea> attributesTileArea_;
        for (int attIdx = 0; attIdx < params.videoAttributeCount; attIdx++) {
          auto videoWidth  = attributeVideoSize_[attIdx].first;
          auto videoHeight = attributeVideoSize_[attIdx].second;

          tileAreasInVideo_[2][fi].tileAttributeAreas_[attIdx] =
            imageArea{videoWidth, videoHeight, 0, 0};

          tileAreasInVideo_[2][fi].patches_[0].attributePatchArea[attIdx] =
            imageArea{videoWidth / 2, videoHeight / 2, 0, 0};

          tileAreasInVideo_[2][fi].patches_[2].attributePatchArea[attIdx] =
            imageArea{videoWidth / 2, videoHeight / 2, 0, videoHeight / 2};

          tileAreasInVideo_[2][fi].patches_[1].attributePatchArea[attIdx] =
            imageArea{videoWidth / 2, videoHeight, videoWidth / 2, 0};
        }
      }
    } else {
      printf("the case tileCount %d\tsubmeshCount %d\t is not defined\n",
             tileCount,
             submeshCount);
    }
  }

  for (uint32_t fi = 0; fi < frameCount; fi++) {
    for (int ti = 0; ti < tileCount; ti++) {
      auto patchCount = params.submeshIdsInTile[ti].size();
      for (int pi = 0; pi < patchCount; pi++) {
        auto& areaDisp =
          tileAreasInVideo_[ti][fi].patches_[pi].geometryPatchArea;
        printf("initial Area Tile[%d] Patch[%d]\t", ti, pi);
        printf("Displace(%d,%d) %dx%d\t",
               areaDisp.LTx,
               areaDisp.LTy,
               areaDisp.sizeX,
               areaDisp.sizeY);
        for (int attrIdx = 0; attrIdx < params.videoAttributeCount;
             attrIdx++) {
          auto& areaTexture = tileAreasInVideo_[ti][fi]
                                .patches_[pi]
                                .attributePatchArea[attrIdx];
          printf("Attribute[%d] (%d,%d) %dx%d\n",
                 attrIdx,
                 areaTexture.LTx,
                 areaTexture.LTy,
                 areaTexture.sizeX,
                 areaTexture.sizeY);
        }
        if (params.videoAttributeCount == 0) printf("\n");
      }
    }
  }

  return 0;
}

//============================================================================
bool
VMCEncoder::adjustTextureImageSize(const VMCEncoderParameters& params,
                                   int32_t                     frameCount) {
  //TODO: it may not work with multuple ATTRIBUTE_TEXTUREs
  uint32_t submeshCount = params.numSubmesh;
  uint32_t tileCount    = params.numTilesAttribute + params.numTilesGeometry;

  std::vector<imageArea> subTextureVideoAreas;
  std::vector<imageArea> textureTileAreas;
  auto                   textureVideoSize = attributeVideoSize_[0];
  uint32_t               texVideoWidth    = textureVideoSize.first;
  uint32_t               texVideoHeight   = textureVideoSize.second;

  tileAreasInVideo_.resize(tileCount);
  for (uint32_t fi = 0; fi < frameCount; fi++) {
    for (uint32_t ti = 0; ti < tileCount; ti++) {
      tileAreasInVideo_[ti][fi].tileAttributeAreas_[0] =
        imageArea{texVideoWidth, texVideoHeight, 0, 0};
      auto patchCount = params.submeshIdsInTile[ti].size();
      tileAreasInVideo_[ti][fi].patches_.resize(patchCount);
      for (auto& patch : tileAreasInVideo_[ti][fi].patches_) {
        patch.attributePatchArea[0] =
          imageArea{texVideoWidth, texVideoHeight, 0, 0};
      }
    }
  }

  if (!params.encodeTextureVideo) {
    for (uint32_t fi = 0; fi < frameCount; fi++) {
      for (int ti = 0; ti < tileCount; ti++) {
        auto patchCount = params.submeshIdsInTile[ti].size();
        for (int pi = 0; pi < patchCount; pi++) {
          auto& areaTexture =
            tileAreasInVideo_[ti][fi].patches_[pi].attributePatchArea[0];
          areaTexture.sizeX = texVideoWidth;
          areaTexture.sizeY = texVideoHeight;
          areaTexture.LTx   = 0;
          areaTexture.LTy   = 0;
        }
      }
    }
  } else {
    if (tileCount == 1) {
      std::vector<imageArea> subTextureVideoAreas(submeshCount);
      setSubtextureVideoSize(textureVideoSize.first,
                             textureVideoSize.second,
                             subTextureVideoAreas,
                             submeshCount);
      for (uint32_t fi = 0; fi < frameCount; fi++) {
        for (int ti = 0; ti < tileCount; ti++) {
          auto patchCount = params.submeshIdsInTile[ti].size();
          if (params.lodPatchesEnable == 1) {
            patchCount = patchCount * (params.subdivisionIterationCount + 1);
          }
          for (int i = 0; i < submeshCount; i++) {
            tileAreasInVideo_[ti][fi].patches_[i].attributePatchArea[0] =
              subTextureVideoAreas[i];
          }
        }
      }

    } else if (tileCount == 2 && submeshCount == 3) {
      //textureTile
      for (uint32_t fi = 0; fi < frameCount; fi++) {
        tileAreasInVideo_[0][fi].tileAttributeAreas_[0] =
          imageArea{texVideoWidth / 2, texVideoHeight, 0, 0};
        tileAreasInVideo_[1][fi].tileAttributeAreas_[0] =
          imageArea{texVideoWidth / 2, texVideoHeight, texVideoWidth / 2, 0};

        tileAreasInVideo_[0][fi].patches_[0].attributePatchArea[0] =
          imageArea{tileAreasInVideo_[0][fi].tileAttributeAreas_[0].sizeX,
                    tileAreasInVideo_[0][fi].tileAttributeAreas_[0].sizeY / 2,
                    0,
                    0};
        tileAreasInVideo_[0][fi].patches_[1].attributePatchArea[0] =
          imageArea{tileAreasInVideo_[0][fi].tileAttributeAreas_[0].sizeX,
                    tileAreasInVideo_[0][fi].tileAttributeAreas_[0].sizeY / 2,
                    0,
                    tileAreasInVideo_[0][fi].tileAttributeAreas_[0].sizeY / 2};
        tileAreasInVideo_[1][fi].patches_[0].attributePatchArea[0] =
          imageArea{tileAreasInVideo_[1][fi].tileAttributeAreas_[0].sizeX,
                    tileAreasInVideo_[1][fi].tileAttributeAreas_[0].sizeY,
                    0,
                    0};
      }
    } else {
      printf("the case tileCount %d\tsubmeshCount %d\t is not defined\n",
             tileCount,
             submeshCount);
    }
  }

  for (uint32_t fi = 0; fi < frameCount; fi++) {
    for (int ti = 0; ti < tileCount; ti++) {
      auto patchCount = params.submeshIdsInTile[ti].size();
      for (int pi = 0; pi < patchCount; pi++) {
        auto& areaTexture =
          tileAreasInVideo_[ti][fi].patches_[pi].attributePatchArea[0];
        printf("Adjusted Texture Area Tile[%d] Patch[%d]\t", ti, pi);
        printf("Texture (%d,%d) %dx%d\n",
               areaTexture.LTx,
               areaTexture.LTy,
               areaTexture.sizeX,
               areaTexture.sizeY);
      }
    }
  }

  return 0;
}

//----------------------------------------------------------------------------
std::vector<std::pair<int32_t, int32_t>>
VMCEncoder::findSameVertices(const vmesh::TriangleMesh<MeshType>& srcMesh,
                             const vmesh::TriangleMesh<MeshType>& targetMesh,
                             MeshType                             sqrDistEps) {
  vmesh::KdTree<MeshType>                  targetTree(3, targetMesh.points());
  auto                                     pointCount = srcMesh.pointCount();
  std::vector<std::pair<int32_t, int32_t>> indexPairs;
  for (int32_t vindex = 0; vindex < pointCount; ++vindex) {
    int32_t     index   = 0;
    MeshType    sqrDist = std::numeric_limits<MeshType>::max();
    const auto& point   = srcMesh.point(vindex);
    targetTree.query(point.data(), 1, &index, &sqrDist);
    if (sqrDist < sqrDistEps) { indexPairs.push_back({vindex, index}); }
  }
  return indexPairs;
}

//============================================================================
bool
VMCEncoder::compressMesh(
  const VMCGroupOfFramesInfo& gofInfoSrc,
  //const Sequence&                 source,
  std::vector<vmesh::VMCSubmesh>&    gof,
  V3cBitstream&                      syntax,
  V3CParameterSet&                   vps,
  int32_t                            submeshIndex,
  int32_t                            submeshCount,
  const VMCEncoderParameters&        params,
  std::vector<std::vector<int32_t>>& submeshLodMap,
  std::vector<std::vector<int32_t>>& submeshChildToParentMap) {
  auto          gofInfo             = gofInfoSrc;
  const int32_t frameCount          = gofInfo.frameCount_;
  int32_t       lastIntraFrameIndex = 0;
  //  initialize(submeshCount, frameCount, submeshIndex, params);
  if (reconBasemeshes_.size() == 0)
    reconBasemeshes_.resize(submeshCount);  //[submesh][#frame]
  if (reconSubdivmeshes_.size() == 0)
    reconSubdivmeshes_.resize(submeshCount);  //[submesh][#frame]
  reconBasemeshes_[submeshIndex].resize(frameCount);
  reconSubdivmeshes_[submeshIndex].resize(frameCount);
// move outside to encoderApp
  if (params.zipperingMethodForUnmatchedLoDs_) {
    submeshLodMap.resize(frameCount);
    submeshChildToParentMap.resize(frameCount);
  }
  gof.resize(gofInfoSrc.frameCount_);
  printf("Compress: frameCount = %d \n", frameCount);
  fflush(stdout);

  //save packing order of the first frame for orthoAtlas
  int              orientationCount = params.use45DegreeProjection ? 18 : 6;
  std::vector<int> catOrderGof(orientationCount, -1);

  //variables for orthoAtlas based on all frames
  int maxOccupiedU       = 0;
  int maxOccupiedV       = 0;
  int globalMaxOccupiedU = 0;
  int globalMaxOccupiedV = 0;
  printf("************************************************\n");
  printf("**************** Compress total frameCount %d\n", frameCount);
  printf("************************************************\n");
  auto&                baseMesh = syntax.getBaseMeshDataStream();
  std::vector<int32_t> prevBaseRepVertexIndices;
  std::vector<TriangleMesh<MeshType>> baseEncList;
  std::vector<std::vector<int32_t>>   mappingWithDupVertexList;
  baseEncList.resize(frameCount);
  mappingWithDupVertexList.resize(frameCount);

  for (int32_t processIndex = 0; processIndex < frameCount; ++processIndex) {
    int32_t frameIndex       = 0;
    uint8_t temporalyIdPlus1 = 1;
    std::tie(frameIndex, temporalyIdPlus1) =
      syntaxForTemporalScalabilityBasemesh(
        processIndex, frameCount, params.basemeshGOPList);
    auto& frame        = gof[frameIndex];
    auto& rec          = reconSubdivmeshes_[submeshIndex][frameIndex];
    auto& bmsl         = baseMesh.addBaseMeshSubmeshLayer();
    bmsl.setBaseMeshTemporalyIdPlus1(temporalyIdPlus1);
    //TODO: [reference buffer] gof should be replaced with the reference mesh buffers(list)
    if (params.checksum) {
      auto&       frame = gof[frameIndex];
      Checksum    checksum;
      std::string eString =
        "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
        + std::to_string(frameIndex) + " ]submesh["
        + std::to_string(submeshIndex) + "]\tOriBase ";
      checksum.print(frame.base, eString);
    }

    baseMeshEncoder_.compressBaseMesh(gof,
                                      submeshIndex,
                                      frameIndex,
                                      frame,
                                      bmsl,
                                      params.baseMeshEncoderParameters_,
                                      prevBaseRepVertexIndices,
                                      baseEncList[frameIndex],
                                      mappingWithDupVertexList[frameIndex]);

#if CONFORMANCE_LOG_ENC
    //storing the decoded basemesh for conformance
    reconBasemeshes_[submeshIndex][frameIndex] = frame.base;
#endif
    if (params.checksum) {
      auto&       frame = gof[frameIndex];
      Checksum    checksum;
      std::string eString =
        "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
        + std::to_string(frameIndex) + " ]submesh["
        + std::to_string(submeshIndex) + "]\tRecBase(not scaled) ";
      checksum.print(frame.base, eString);
    }
  }  //for(processIdx)

  //*****************//
  //scaling of the reconstructed base meshes are performed after compressing all the basemeshes due to theinter prediction
  for (int32_t processIndex = 0; processIndex < frameCount; ++processIndex) {
    const auto frameIndex = decodeOrderToFrameOrderBasemesh(
      processIndex, frameCount, params.basemeshGOPList);
    if (gof[frameIndex].base.pointCount() == 0) {
      std::cout << "gof[frameIndex].base.pointCount() "
                << gof[frameIndex].base.pointCount() << "\n";
    } else {
      auto&        frame         = gof[frameIndex];
      const double scalePosition = ((1 << params.qpPosition) - 1.0)
                                   / ((1 << params.bitDepthPosition) - 1.0);
      const double scaleTexCoord  = std::pow(2.0, params.qpTexCoord) - 1.0;
      const double iscalePosition = 1.0 / scalePosition;
      auto&        base           = frame.base;
      for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
        base.setPoint(v, base.point(v) * iscalePosition);
      }
      if (params.checksum) {
        auto&       frame = gof[frameIndex];
        Checksum    checksum;
        std::string eString =
          "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
          + std::to_string(frameIndex) + " ]submesh["
          + std::to_string(submeshIndex) + "]\tRecBase ";
        checksum.print(frame.base, eString);
      }
      if (params.iDeriveTextCoordFromPos > 0) {
        if (params.useRawUV && params.iDeriveTextCoordFromPos == 3) {
          updateTextureCoordinates(frame,
                                   baseEncList[frameIndex],
                                   mappingWithDupVertexList[frameIndex],
                                   gof,
                                   params);
        } else {
          updateTextureCoordinates(
            frame, baseEncList[frameIndex], frame.mapping, gof, params);
        }
      }

      //rec base deform
      //auto tempMesh = frame.base;
      if (!params.subdivIsBase && params.deformForRecBase) {
        GeometryParametrization fitsubdivIntra;
        TriangleMesh<MeshType>  mtargetIntra, subdiv0Intra, mapped;
        TriangleMesh<MeshType>  baseIntra, subdivIntra, nsubdivIntra;
        TriangleMesh<MeshType>  meshRef = frame.base;
        fitsubdivIntra.generate(frame.reference,        // target
                                frame.base,             // source
                                mapped,                 // mapped
                                mtargetIntra,           // mtarget
                                subdiv0Intra,           // subdiv0
                                params.intraGeoParams,  // params
                                baseIntra,              // base
                                subdivIntra,            // deformed
                                nsubdivIntra,           // ndeformed
                                &meshRef,
                                true);
        //frame.base = baseIntra;
        frame.subdiv = subdivIntra;
      }
    }
  }

  for (int32_t processIndex = 0; processIndex < frameCount; ++processIndex) {
    const auto frameIndex = decodeOrderToFrameOrderBasemesh(
      processIndex, frameCount, params.basemeshGOPList);
    if (gof[frameIndex].base.pointCount() == 0) {
      std::cout << "gof[frameIndex].base.pointCount() "
                << gof[frameIndex].base.pointCount() << "\n";
    } else {
      auto& frame = gof[frameIndex];
      auto& rec   = reconSubdivmeshes_[submeshIndex][frameIndex];
      //subdivision
      std::vector<vmesh::SubdivisionMethod> subdivisionMethod(
        params.subdivisionIterationCount);
      for (int it = 0; it < params.subdivisionIterationCount; it++) {
        subdivisionMethod[it] = params.intraGeoParams.subdivisionMethod[it];
      }
      bool interpolateNormals = ((params.interpolateSubdividedNormalsFlag > 0)&&(!params.dracoMeshLossless));
      if (!params.increaseTopSubmeshSubdivisionCount) {
        subdivideBaseMesh(frame,
                          frameIndex,
                          submeshIndex,
                          rec,
                          &(subdivisionMethod),
                          params.subdivisionIterationCount,
                          params.subdivisionEdgeLengthThreshold,
                          params.bitshiftEdgeBasedSubdivision,
                          interpolateNormals);
      } else {
        // need to increase the size of the last LoD, copying the last method
        if (submeshIndex == 0) {
          if (params.subdivisionIterationCount) {
            subdivisionMethod.resize(subdivisionMethod.size() + 1);
            subdivisionMethod[subdivisionMethod.size() - 1] =
              subdivisionMethod[subdivisionMethod.size() - 2];
          }
        }
        subdivideBaseMesh(
          frame,
          submeshIndex,
          frameIndex,
          rec,
          &(subdivisionMethod),
          submeshIndex == 0 ? params.subdivisionIterationCount
                                ? params.subdivisionIterationCount + 1
                                : params.subdivisionIterationCount
                            : params.subdivisionIterationCount,
          params.subdivisionEdgeLengthThreshold,
          params.bitshiftEdgeBasedSubdivision,
          interpolateNormals,
          params.zipperingMethodForUnmatchedLoDs_ ? &submeshLodMap[frameIndex]
                                                  : nullptr,
          params.zipperingMethodForUnmatchedLoDs_
            ? &submeshChildToParentMap[frameIndex]
            : nullptr);
      }

      if (params.checksum) {
        auto&       frame = gof[frameIndex];
        Checksum    checksum;
        std::string eString =
          "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
          + std::to_string(frameIndex) + " ] submesh["
          + std::to_string(submeshIndex) + "]\tRecSubdiv ";
        checksum.print(reconSubdivmeshes_[submeshIndex][frameIndex], eString);
      }

      if (params.segmentByBaseMesh) {
        const auto& wholeRec    = wholeReconSubdivMeshes_[frameIndex];
        const auto& wholeSubdiv = wholeSubdivMeshes_[frameIndex];
        frame.subdiv            = rec;
        auto indexPairs         = findSameVertices(rec, wholeRec, 100);
        for (const auto& indexPair : indexPairs) {
          auto v      = indexPair.first;
          auto vindex = indexPair.second;
          assert(vindex >= 0 && vindex < frame.subdiv.pointCount());
          frame.subdiv.setPoint(v, wholeSubdiv.point(vindex));
        }
        if (params.keepBaseMesh) {
          auto prefix =
            _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
          frame.subdiv.save(prefix + "_submesh_subdiv.ply");
          rec.save(prefix + "_submesh_rec.ply");
          wholeRec.save(prefix + "_whole_rec.ply");
          wholeSubdiv.save(prefix + "_whole_subdiv.ply");
        }
      } else {
        if (!params.dracoMeshLossless && !params.deformForRecBase) {
          auto rsubdiv = rec;
          std::swap(rsubdiv, frame.subdiv);
          const auto& mapping = frame.mapping;
          for (int32_t v = 0, vcount = frame.subdiv.pointCount(); v < vcount;
               ++v) {
            const auto vindex = mapping[v];
            assert(vindex >= 0 && vindex < vcount);
            frame.subdiv.setPoint(v, rsubdiv.point(vindex));
          }
        }
      }
      if (params.zipperingMethodForUnmatchedLoDs_) {
          submeshLodMap[frameIndex].resize(frame.subdiv.pointCount());
          std::fill(submeshLodMap[frameIndex].begin(),
              submeshLodMap[frameIndex].end(),
              0);
          submeshChildToParentMap[frameIndex].resize(
              2 * frame.subdiv.pointCount());
          std::fill(submeshChildToParentMap[frameIndex].begin(),
              submeshChildToParentMap[frameIndex].end(),
              0);
      }
      if (params.encodeDisplacementType != 0) {
        if (!interpolateNormals) {
          rec.resizeNormals(rec.pointCount());
          rec.computeNormals();
        }
#if DEBUG_INTERPOLATED_NORMALS
        int  vindex = rec.pointCount() / 2;
        auto n      = rec.normal(vindex);
        std::cout << "Encoder interpolated Normals before applying disp at "
                  << vindex << " n[0]=" << n[0] << " n[1]=" << n[1]
                  << " n[2]=" << n[2] << std::endl;
#endif
        createDisplacements(frame, submeshIndex, frameIndex, rec, params);
      }
      //  frame.base = (!params.baseIsSrc && params.deformForRecBase) ? tempMesh : frame.base;
    }
  }
  return true;
}

//============================================================================
bool
VMCEncoder::segmentByBaseMesh(VMCGroupOfFramesInfo&           gofInfo,
                              const Sequence&                 source,
                              const std::vector<std::string>& submeshPaths,
                              const VMCEncoderParameters&     params) {
  const int32_t frameCount = gofInfo.frameCount_;
  if (wholeBaseMeshes_.frameCount() < frameCount) {
    wholeBaseMeshes_.resize(frameCount);
  }
  if (wholeReconSubdivMeshes_.frameCount() < frameCount) {
    wholeReconSubdivMeshes_.resize(frameCount);
  }
  if (wholeSubdivMeshes_.frameCount() < frameCount) {
    wholeSubdivMeshes_.resize(frameCount);
  }
  std::vector<vmesh::VMCSubmesh> gof;
  preprocessWholemesh(source, gofInfo, gof, 0, params);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame                    = gof[frameIndex];
    wholeBaseMeshes_[frameIndex]   = frame.base;
    wholeSubdivMeshes_[frameIndex] = frame.subdiv;
    auto& rec                      = wholeReconSubdivMeshes_[frameIndex];
    //subdivision
    std::vector<vmesh::SubdivisionMethod> subdivisionMethod(
      params.subdivisionIterationCount);
    for (int it = 0; it < params.subdivisionIterationCount; it++) {
      subdivisionMethod[it] = params.intraGeoParams.subdivisionMethod[it];
    }
    bool interpolateNormals = (params.interpolateSubdividedNormalsFlag > 0);
    subdivideBaseMesh(frame,
                      -1,
                      frameIndex,
                      rec,
                      &(subdivisionMethod),
                      params.subdivisionIterationCount,
                      params.subdivisionEdgeLengthThreshold,
                      params.bitshiftEdgeBasedSubdivision,
                      interpolateNormals);
    // update BaseMeshType
    gofInfo[frameIndex].type = gof[frameIndex].submeshType;
    gofInfo[frameIndex].referenceFrameIndex =
      gof[frameIndex].referenceFrameIndex;
  }
  // segment wholeBaseMeses
  if (!submeshSegmentByBaseMesh(gofInfo, submeshPaths)) {
    std::cerr << "Error: can't segment meshes!\n";
    return false;
  }
  // submeshes are saved to files here
  return true;
}

//============================================================================
bool
VMCEncoder::createDisplacements(vmesh::VMCSubmesh&            submesh,
                                int32_t                       submeshIndex,
                                int32_t                       frameIndex,
                                const TriangleMesh<MeshType>& rec,
                                const VMCEncoderParameters&   params) {
  std::cout << "create Displacement submesh[ " << submeshIndex << " ] Frame[ "
            << frameIndex << " ]\n";
  auto& submeshDisp = submesh.disp;
  computeDisplacements(submesh, submeshIndex, frameIndex, rec, params);

  if (params.transformMethod == 1) {
    std::cout << "Linear Lifting: Forward lifting" << std::endl;
    computeForwardLinearLifting(
      submesh.disp,
      submesh.subdivInfoLevelOfDetails,
      submesh.subdivEdges,
      rec.valences(),
      params.liftingValenceUpdateWeightFlag,
      &submesh.liftingdisplodmean,
      params.applyLiftingOffset,
      params.liftingSkipUpdate,
      params.liftingAdaptivePredictionWeightFlag,
      params.liftingPredictionWeightNumerator,
      params.liftingPredictionWeightDenominatorMinus1,
      params.liftingPredictionWeightNumeratorDefault,
      params.liftingPredictionWeightDenominatorMinus1Default,
      params.liftingAdaptiveUpdateWeightFlag,
      params.liftingUpdateWeightNumerator,
      params.liftingUpdateWeightDenominatorMinus1,
      &submesh.liftingdispstat,
      params.dirlift,
      params.dirliftScale1,
      params.dirliftDeltaScale2,
      params.dirliftDeltaScale3,
      params.dirliftScaleDenoMinus1);
  } else {
    std::cout << "None Transform - skipping forward transform " << std::endl;
  }

  if (!params.IQSkipFlag) {
    quantizeDisplacements(submesh, submeshIndex, params);
    if (params.displacementFillZerosFlag) { fillDisZeros(submesh, params); }
  }

  if (params.checksum) {
    Checksum checksumRec;
    std::cout
      << "**********************************************************\n";
    std::string eString = "frame " + std::to_string(frameIndex)
                          + "\t(source)submeshDisp["
                          + std::to_string(submeshIndex) + "] ";
    checksumRec.print(submesh.disp, eString);
    std::cout
      << "**********************************************************\n";
  }
  return 0;
}

//============================================================================
void
VMCEncoder::placeTextures(int32_t                     attrIndex,
                          int32_t                     frameIndex,
                          int32_t                     tileIndex,
                          int32_t                     patchIndex,
                          Frame<uint8_t>&             textureSubmesh,
                          Frame<uint16_t>&            oneTrTexture,
                          Plane<uint8_t>&             occupancy,
                          Plane<uint8_t>&             oneOccupancy,
                          bool                        placeOccupancy,
                          const VMCEncoderParameters& encParams) {
  //place sub-texture on the frame image
  //|0|2|
  //|1| |
  //  auto LTx = reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].subTextureVideoArea.LTx;
  //  auto LTy = reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex].subTextureVideoArea.LTy;
  auto LTx = reconAtlasTiles_[tileIndex][frameIndex]
               .patches_[patchIndex]
               .attributePatchArea[attrIndex]
               .LTx
             + reconAtlasTiles_[tileIndex][frameIndex]
                 .tileAttributeAreas_[attrIndex]
                 .LTx;
  auto LTy = reconAtlasTiles_[tileIndex][frameIndex]
               .patches_[patchIndex]
               .attributePatchArea[attrIndex]
               .LTy;
  +reconAtlasTiles_[tileIndex][frameIndex].tileAttributeAreas_[attrIndex].LTy;
  printf("attributes[%d] placeTexture : patch[%d]  pos(%d,%d) size(%dx%d)\n",
         attrIndex,
         patchIndex,
         LTx,
         LTy,
         textureSubmesh.width(),
         textureSubmesh.height());
  for (int32_t i = 0; i < oneTrTexture.planeCount(); i++) {
    auto& plane = oneTrTexture.plane(i);
    for (int32_t y = 0; y < textureSubmesh.height(); y++) {
      for (int32_t x = 0; x < textureSubmesh.width(); x++) {
        auto v = textureSubmesh.plane(i).get(y, x);
        plane.set(y + LTy, x + LTx, v);
      }
    }
  }
  if (placeOccupancy) {
    for (int32_t y = 0; y < occupancy.height(); y++) {
      for (int32_t x = 0; x < occupancy.width(); x++) {
        auto v = occupancy.get(y, x);
        oneOccupancy.set(y + LTy, x + LTx, v);
      }
    }
  }
}

//============================================================================
void
VMCEncoder::updateMesh(
  std::vector<std::vector<VMCSubmesh>>& encFrames,  //[submesh][frame]
  V3cBitstream&                         syntax,
  const VMCEncoderParameters&           params) {

  // TODO: LK this probably can be removed as this can be done in the bitstream class itself
  auto& smlList = syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList();
  auto& bmfpsList =
    syntax.getBaseMeshDataStream().getBaseMeshFrameParameterSetList();
  for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
    basemesh::BaseMeshSubmeshLayer& sml =
      syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];
    auto  bmfpsId = sml.getSubmeshHeader().getSmhSubmeshFrameParameterSetId();
    auto& bmfpsSi = bmfpsList[bmfpsId].getSubmeshInformation();
    auto  submeshPos =
      bmfpsSi._submeshIDToIndex[sml.getSubmeshHeader().getSmhId()];
    int32_t frameIndexCalc = (int32_t)syntax.getBaseMeshDataStream().calculateBmFocVal(smlList,
                                                                                       smlIdx);
#if 0
    printf("(constructSmlRefListStruct) : frame%d submesh%d\treference frame index %d\n", frameIndexCalc, submeshIdx, encFrames[submeshIdx][frameIndexCalc].referenceFrameIndex);
#endif
    baseMeshEncoder_.constructSmlRefListStruct(
      syntax.getBaseMeshDataStream(),
      sml,
      (int32_t)submeshPos,
      frameIndexCalc,
      encFrames[submeshPos][frameIndexCalc].referenceFrameIndex);
  }

  if (params.meshCodecSpecificParametersInBMSPS) {
    int64_t   mcHeaderDataSizeRef = 0;
    Bitstream mcHeaderDataBitstreamRef;
    bool      allMcHeadersTheSame = false;
    bool      firstIntraDataUnit  = true;
    for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
      basemesh::BaseMeshSubmeshLayer& sml =
        syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];

      if (sml.getSubmeshHeader().getSmhType() == basemesh::I_BASEMESH) {
        auto&     dui = sml.getSubmeshDataunitIntra();
        Bitstream mcHeaderDataBitstream;
        int64_t   mcHeaderDataSize = dui.getCodedMeshHeaderDataSize();
        if (firstIntraDataUnit) {
          mcHeaderDataSizeRef = mcHeaderDataSize;
          mcHeaderDataBitstreamRef.copyFromBits(
            dui.getCodedMeshDataUnitBuffer(), 0, mcHeaderDataSizeRef);
          allMcHeadersTheSame = true;
          firstIntraDataUnit  = false;
        } else {
          if (mcHeaderDataSize != mcHeaderDataSizeRef) {
            allMcHeadersTheSame = false;
            break;
          }

          if (std::memcmp(mcHeaderDataBitstreamRef.buffer(),
                          dui.getCodedMeshDataUnitBuffer(),
                          mcHeaderDataSizeRef)) {
            allMcHeadersTheSame = false;
            break;
          }
        }
      }
    }

    if (allMcHeadersTheSame) {
      // re-write the data units and update BMSPS
      for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
        basemesh::BaseMeshSubmeshLayer& sml =
          syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];

        if (sml.getSubmeshHeader().getSmhType() == basemesh::I_BASEMESH) {
          basemesh::BaseMeshSubmeshDataUnitIntra& dui  = sml.getSubmeshDataunitIntra();
          std::vector<uint8_t>&         data = dui.getCodedMeshDataUnit();
          data.erase(data.begin(), data.begin() + mcHeaderDataSizeRef);
          dui.setCodedMeshHeaderDataSize(0);
          dui.setPayloadSize(data.size());
        }
      }

      auto& bmsps =
        syntax.getBaseMeshDataStream().getBaseMeshSequenceParameterSet(0);
      bmsps.setBmspsCodecSpecificParametersPresentFlag(1);
      bmsps.setBmspsMeshCodecPrefixLengthMinus1(mcHeaderDataSizeRef - 1);
      bmsps.setBmspsMeshCodecPrefixData(mcHeaderDataBitstreamRef.vector());
    }
  }
  if (params.motionCodecSpecificParametersInBMSPS) {
    int64_t   motionHeaderDataSizeRef = 0;
    Bitstream motionHeaderDataBitstreamRef;
    bool      allMotionHeadersTheSame = false;
    bool      firstInterDataUnit      = true;
    for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
      basemesh::BaseMeshSubmeshLayer& sml =
        syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];

      if (sml.getSubmeshHeader().getSmhType() == basemesh::P_BASEMESH) {
        auto&     dui = sml.getSubmeshDataunitInter();
        Bitstream motionHeaderDataBitstream;
        int64_t   motionHeaderDataSize = dui.getCodedMotionHeaderDataSize();
        if (firstInterDataUnit) {
          motionHeaderDataSizeRef = motionHeaderDataSize;
          motionHeaderDataBitstreamRef.copyFromBits(
            dui.getCodedMeshDataUnitBuffer(), 0, motionHeaderDataSizeRef);
          allMotionHeadersTheSame = true;
          firstInterDataUnit      = false;
        } else {
          if (motionHeaderDataSize != motionHeaderDataSizeRef) {
            allMotionHeadersTheSame = false;
            break;
          }

          if (std::memcmp(motionHeaderDataBitstreamRef.buffer(),
                          dui.getCodedMeshDataUnitBuffer(),
                          motionHeaderDataSizeRef)) {
            allMotionHeadersTheSame = false;
            break;
          }
        }
      }
    }

    if (allMotionHeadersTheSame) {
      // re-write the data units and update BMSPS
      for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
        basemesh::BaseMeshSubmeshLayer& sml =
          syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];

        if (sml.getSubmeshHeader().getSmhType() == basemesh::P_BASEMESH) {
          basemesh::BaseMeshSubmeshDataUnitInter& dui  = sml.getSubmeshDataunitInter();
          std::vector<uint8_t>&         data = dui.getCodedMeshDataUnit();
          data.erase(data.begin(), data.begin() + motionHeaderDataSizeRef);
          dui.setCodedMotionHeaderDataSize(0);
          dui.setPayloadSize(data.size());
        }
      }

      auto& bmsps =
        syntax.getBaseMeshDataStream().getBaseMeshSequenceParameterSet(0);
      bmsps.setBmspsMotionCodecSpecificParametersPresentFlag(1);
      bmsps.setBmspsMotionCodecPrefixLengthMinus1(motionHeaderDataSizeRef - 1);
      bmsps.setBmspsMotionCodecPrefixData(
        motionHeaderDataBitstreamRef.vector());
    }
  }
}

//============================================================================
void
VMCEncoder::compressAtlas(
  std::vector<std::vector<VMCSubmesh>>& encFrames,  //[submesh][frame]
  V3cBitstream&                         syntax,
  V3CParameterSet&                      vps,
  const VMCEncoderParameters&           params) {
  std::cout << "\nCreate and Update parameter sets and atlas data "
               "sub-bitstream and basemesh sub-bitstream\n";
  if (params.checksum) {
    for (int32_t frameIndex = 0; frameIndex < encFrames[0].size();
         ++frameIndex) {
      for (int submeshIndex = 0; submeshIndex < encFrames.size();
           submeshIndex++) {
        auto&       frame = encFrames[submeshIndex][frameIndex];
        Checksum    checksum;
        std::string eString =
          "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
          + std::to_string(frameIndex) + " ][" + std::to_string(submeshIndex)
          + "]\tbmstreamRecBase ";
        checksum.print(frame.base, eString);
      }
    }

    for (int32_t frameIndex = 0; frameIndex < encFrames[0].size();
         ++frameIndex) {
      for (int submeshIndex = 0; submeshIndex < encFrames.size();
           submeshIndex++) {
        Checksum    checksum;
        std::string eString =
          "VpsId: " + std::to_string(vps.getV3CParameterSetId()) + " Frame[ "
          + std::to_string(frameIndex) + " ][" + std::to_string(submeshIndex)
          + "]\tbmstreamRecSubdiv ";
        checksum.print(reconSubdivmeshes_[submeshIndex][frameIndex], eString);
      }
    }
  }

  // Update VPS
  auto&   adStream           = syntax.getAtlasDataStream();
  auto&   atLayer            = adStream.getAtlasTileLayerList();
  int32_t frameCountSubmesh0 = (int32_t)encFrames[0].size();
  if (params.encodeDisplacementType == 2) {
    setPatchAreaForDisplacements(encFrames, params);
  } else {
    geometryVideoSize.first  = 0;
    geometryVideoSize.second = 0;
  }

  if (params.videoAttributeCount > 0) {
    if (!params.encodeTextureVideo) {
      attributeVideoSize_[0].first  = 0;
      attributeVideoSize_[0].second = 0;
    } else if (params.textureParameterizationType == 1
               && params.packingType == 3 && params.updateTextureSize > 0) {
      if (params.updateTextureSize == 2) {
        //scale texture to max area
        updateTextureSizeScaleToMaxArea(params);
      }
      //resize for multiple tiles
      VERIFY_SOFTWARE_LIMITATION(params.numTilesAttribute < 2);
      if (params.numTilesAttribute > 1) {
        double partitionWidthIn  = 256;
        double partitionHeightIn = 256;
        attributeVideoSize_[0].first =
          floor(attributeVideoSize_[0].first / 2 / partitionWidthIn)
          * partitionWidthIn * 2;
        attributeVideoSize_[0].second =
          floor(attributeVideoSize_[0].second / 2 / partitionWidthIn)
          * partitionWidthIn * 2;
      }
      //resize texture with updated size
      adjustTextureImageSize(params, encFrames[0].size());
    }
  }

  vps.setFrameWidth(
    vps.getAtlasId(0),
    params.encodeDisplacementType != 2 ? 0 : geometryVideoSize.first);
  vps.setFrameHeight(
    vps.getAtlasId(0),
    params.encodeDisplacementType != 2 ? 0 : geometryVideoSize.second);
  auto& vmcExt = vps.getVpsVdmcExtension();
  auto& ai     = vps.getAttributeInformation(vps.getAtlasId(0));

  uint32_t vpsAttributeNominalFrameCount =
    vps.getVpsAttributeNominalFrameSizeCount(vps.getAtlasId(0));
  for (uint32_t attIdx = 0; attIdx < vpsAttributeNominalFrameCount; attIdx++) {
    vmcExt.setVpsExtAttributeFrameWidth(
      attributeVideoSize_[attIdx].first, vps.getAtlasId(0), attIdx);
    vmcExt.setVpsExtAttributeFrameHeight(
      attributeVideoSize_[attIdx].second, vps.getAtlasId(0), attIdx);
  }

  atlasEncoder_.compressAtlas(adStream,
                              tileAreasInVideo_,
                              reconAtlasTiles_,
                              encFrames,
                              params.atlasEncoderParameters_,
                              geometryVideoSize,
                              attributeVideoSize_);
  atlasDecoder_.decodeAtlasDataSubbitstream(adStream, false);
  debugReconAtlasTilesUsingAtlasDecoder_ =  atlasDecoder_.getDecodedAtlasFrames();

  //Update MD (initialized in initialize())
  // TODO: LK this probably can be removed as this can be done in the bitstream class itself
  auto& smlList = syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList();
  auto& bmfpsList =
    syntax.getBaseMeshDataStream().getBaseMeshFrameParameterSetList();
  for (int32_t smlIdx = 0; smlIdx < (int32_t)smlList.size(); smlIdx++) {
    basemesh::BaseMeshSubmeshLayer& sml =
      syntax.getBaseMeshDataStream().getBaseMeshSubmeshLayerList()[smlIdx];
    auto  bmfpsId = sml.getSubmeshHeader().getSmhSubmeshFrameParameterSetId();
    auto& bmfpsSi = bmfpsList[bmfpsId].getSubmeshInformation();
    auto  submeshPos =
      bmfpsSi._submeshIDToIndex[sml.getSubmeshHeader().getSmhId()];
    int32_t frameIndexCalc = (int32_t)syntax.getBaseMeshDataStream().calculateBmFocVal(smlList,
                                                                smlIdx);
#if 0
    printf("(constructSmlRefListStruct) : frame%d submesh%d\treference frame index %d\n", frameIndexCalc, submeshIdx, encFrames[submeshIdx][frameIndexCalc].referenceFrameIndex);
#endif
    baseMeshEncoder_.constructSmlRefListStruct(
      syntax.getBaseMeshDataStream(),
      sml,
      (int32_t)submeshPos,
      frameIndexCalc,
      encFrames[submeshPos][frameIndexCalc].referenceFrameIndex);
  }

}

//============================================================================
void
VMCEncoder::updateReconMesh(int32_t frameCount, int32_t submeshCount) {
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
      if (!zipperingSwitch_[frameIdx]) {
          for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
              reconSubdivmeshes_[submeshIdx][frameIdx] =
                  zipperingMeshes_[submeshIdx][frameIdx];
          }
      }
  }
}

//============================================================================
//construct the reference list structure in the atlas header
//============================================================================
uint32_t
VMCEncoder::writeReconstructedMeshes(int32_t        frameCount,
                                     int32_t        submeshCount,
                                     const uint32_t bitDepthTexCoord,
                                     int32_t        frameOffset,
                                     const VMCEncoderParameters& params) {
  uint32_t faceCount = 0;
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    TriangleMesh<MeshType> reconstructed;
    if (!params.dequantizeUV) {
      //cout<<"Normalize uv coordiantes frame stream[ "<<streamIdx<<" ]\n";
      const auto scale = (1 << bitDepthTexCoord) - 1.0;
      for (int32_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        auto&      submesh       = reconSubdivmeshes_[submeshIdx][frameIdx];
        const auto texCoordCount = submesh.texCoordCount();
        for (int32_t i = 0; i < texCoordCount; ++i) {
          submesh.setTexCoord(i, submesh.texCoord(i) * scale);
        }

        // Reconstruct normals
        if (!params.reconstructNormals) {
          for (auto& rec : reconSubdivmeshes_[submeshIdx]) {
            rec.resizeNormals(0);
          }
        }

      }  //submeshIdx
    }
    //if (params.checksum)
    if (0) {
      if (submeshCount > 1) {
        std::cout << "***************************************************\n";
        std::cout << "***************************************************\n";
        for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
          Checksum    checksum;
          std::string eString = "Writing Frame[ " + std::to_string(frameIdx)
                                + " ]\tsubmesh[" + std::to_string(submeshIdx)
                                + "] recon";
          checksum.print(reconSubdivmeshes_[submeshIdx][frameIdx], eString);
        }
        std::cout << "***************************************************\n";
        std::cout << "***************************************************\n";
      }
    }
    if (submeshCount == 1) {
      reconstructed = reconSubdivmeshes_[0][frameIdx];
    } else {
      reconstructed = reconSubdivmeshes_[0][frameIdx];
      for (size_t submeshIdx = 1; submeshIdx < submeshCount; submeshIdx++) {
        reconstructed.append(reconSubdivmeshes_[submeshIdx][frameIdx]);
      }
    }
    faceCount += reconstructed.triangleCount();
    //checksum
    encChecksum_.add(reconstructed);

    if (!reconstructedMeshPath_.empty()) {
      std::cout << "save reconstructed mesh files\n";
      char fnameEncMesh[1024];
      char fnameEncMaterial[1024];
      char fnameEncTexture[1024];
      snprintf(fnameEncMesh,
               1024,
               reconstructedMeshPath_.c_str(),
               frameIdx + startFrame_ + frameOffset);
      Material<double> material;
      if (!reconstructedMaterialLibPath_.empty()) {
        snprintf(fnameEncTexture,
                 1024,
                 reconstructedAttributesPaths_[0].c_str(),
                 frameIdx + startFrame_ + frameOffset);
        snprintf(fnameEncMaterial,
                 1024,
                 reconstructedMaterialLibPath_.c_str(),
                 frameIdx + startFrame_ + frameOffset);
        material.texture = fnameEncTexture;
        material.save(fnameEncMaterial);
      }
      if (!reconstructedMaterialLibPath_.empty())
        reconstructed.setMaterialLibrary(fnameEncMaterial);
      reconstructed.save(fnameEncMesh);
    }
#if CONFORMANCE_LOG_ENC
    getRecFrameLog(frameIdx, reconstructed);
#endif
  }
  return faceCount;
}

//============================================================================
void
VMCEncoder::adjustTextureCoordinates(int32_t                     frameCount,
                                     int32_t                     submeshCount,
                                     const VMCEncoderParameters& params) {
  //getMduAttributes2dPosX & getMduAttributes2dPosY is in [(0,0)~(videoWidth-1, videoHeight-1)]
  //it needs to be adjusted for [(0,0)~(textureParamWidth-1, textureParamHeight-1)]

  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    for (int32_t tileIdx = params.numTilesGeometry;
         tileIdx < (params.numTilesGeometry + params.numTilesAttribute);
         tileIdx++) {
      for (int32_t patchIdx = 0;
           patchIdx < params.submeshIdsInTile[tileIdx].size();
           patchIdx++) {
        auto& patch =
          reconAtlasTiles_[tileIdx][frameIdx].patches_
            [patchIdx];  //tileAreasInVideo_[tileIdx].patches_[patchIdx];
        //patch - submeshId
        auto submeshId = params.submeshIdsInTile[tileIdx][patchIdx];
        //submeshId -> submeshIndex
        auto  submeshIndex = submeshIdtoIndex_[submeshId];
        auto& submesh      = reconSubdivmeshes_[submeshIndex][frameIdx];

        auto LTx =
          patch.attributePatchArea[0].LTx
          + reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_[0].LTx;
        auto LTy = patch.attributePatchArea[0].LTy;
        +reconAtlasTiles_[tileIdx][frameIdx].tileAttributeAreas_[0].LTx;

        vmesh::adjustTextureCoordinates(submesh,
                                        patch.attributePatchArea[0].sizeX,
                                        patch.attributePatchArea[0].sizeY,
                                        LTx,
                                        LTy,
                                        attributeVideoSize_[0].first,
                                        attributeVideoSize_[0].second);
        //if(params.checksum)
        if (0) {
          printf("**adjustTextureCoordinates: frame[%d] submesh[%d] "
                 "submeshId:%d %d,%d\t%dx%d -> %dx%d\n",
                 frameIdx,
                 submeshIndex,
                 submeshId,
                 LTx,
                 LTy,
                 patch.attributePatchArea[0].sizeX,
                 patch.attributePatchArea[0].sizeY,
                 attributeVideoSize_[0].first,
                 attributeVideoSize_[0].second);
        }
      }
    }
  }
}

//============================================================================
void
VMCEncoder::updateTextureSizeScaleToMaxArea(
  const VMCEncoderParameters& params) {
  auto textureWidthOrg        = params.attributeParameters[0].textureWidth;
  auto textureHeightOrg       = params.attributeParameters[0].textureHeight;
  auto geometryVideoBlockSize = (1 << params.log2GeometryVideoBlockSize);

  auto&  textureVideoSize = attributeVideoSize_[0];
  auto   packingAreaMax   = textureWidthOrg * textureHeightOrg;
  auto   packingAreaCur   = textureVideoSize.first * textureVideoSize.second;
  double scale            = sqrt((double)packingAreaMax / packingAreaCur);

  auto textureWidthTemp  = textureVideoSize.first * scale;
  auto textureHeightTemp = textureVideoSize.second * scale;

  int minCUwidth  = params.maxCUWidth >> (params.maxPartitionDepth - 1);
  int minCUheight = params.maxCUHeight >> (params.maxPartitionDepth - 1);

  textureWidthTemp  = floor(textureWidthTemp / minCUwidth) * minCUwidth;
  textureHeightTemp = floor(textureHeightTemp / minCUheight) * minCUheight;

  //geometryVideoBlockSize limitation
  textureWidthTemp =
    floor(textureWidthTemp / geometryVideoBlockSize) * geometryVideoBlockSize;
  textureHeightTemp =
    floor(textureHeightTemp / geometryVideoBlockSize) * geometryVideoBlockSize;

  textureVideoSize.first  = textureWidthTemp;
  textureVideoSize.second = textureHeightTemp;

  std::cout << "(updateTextureSizeScaleToMaxArea) "
            << "Texture size is updated to (" << textureVideoSize.first << ", "
            << textureVideoSize.second << ") with scale " << scale
            << " to be less than " << textureWidthOrg << " x "
            << textureHeightOrg << "." << std::endl;
}

//============================================================================
bool
VMCEncoder::setMCTSVideoEncoderParams(
  const VMCEncoderParameters& params,
  VideoEncoderParameters&     videoEncoderParams) {
  videoEncoderParams.EnableTMCTSFlag = true;
  auto                  lodCount     = params.subdivisionIterationCount + 1;
  std::vector<uint32_t> TileRowHeights;
  std::vector<uint32_t> TileColWidths;
  std::vector<uint32_t> Ssx;
  std::vector<uint32_t> Ssy;
  std::vector<uint32_t> SLTx;
  std::vector<uint32_t> SLTy;
  uint32_t              numTileRows = 0;
  uint32_t              numTileCols = 0;
  // refinement level N       --> video tile 0
  // refinement level 0 ~ N-1 --> video tile 1
  int tileNums                             = 2;
  videoEncoderParams.NumTileColumnsMinus1_ = 0;
  videoEncoderParams.NumTileRowsMinus1_    = tileNums - 1;
  uint32_t prevW                           = 0;
  uint32_t prevLTy                         = 0;
  uint32_t prevSy                          = 0;
  uint32_t counter                         = 0;
  for (auto tileIdx = 0; tileIdx < tileAreasInVideo_.size(); tileIdx++) {
    int patchIdx;
    patchIdx = lodCount - 1;
    for (auto i = 0; i < tileNums; i++) {
      auto geometryArea =
        tileAreasInVideo_[tileIdx][0].patches_[patchIdx].geometryPatchArea;
      uint32_t sx  = geometryArea.sizeX;
      uint32_t sy  = geometryArea.sizeY;
      uint32_t LTx = geometryArea.LTx;
      uint32_t LTy = geometryArea.LTy;
      if (numTileRows == 0) {
        numTileCols++;
        TileColWidths.push_back(sx / 64);
      }
      if (counter > 0)
        if (prevLTy != LTy) {
          numTileRows++;
          TileRowHeights.push_back(prevSy / 64);
        } else prevW += sx;
      counter++;
      prevLTy = LTy;
      prevSy  = sy;
      if (i == tileNums - 1) { TileRowHeights.push_back(sy / 64 + LTy / 64); }
      patchIdx--;
    }
  }
  videoEncoderParams.NumTileRowsMinus1_    = TileRowHeights.size() - 1;
  videoEncoderParams.TileRowHeightArray_   = {};
  videoEncoderParams.TileColumnWidthArray_ = {};

  // packing based tile allocation
  auto temp = 0;
  for (auto i = 0; i < TileRowHeights.size() - 1; i++) {
    if (params.displacementReversePacking) {  // reverse packing case
      if (temp == 0)
        videoEncoderParams.TileRowHeightArray_ =
          videoEncoderParams.TileRowHeightArray_
          + std::to_string(TileRowHeights[i]);
      else
        videoEncoderParams.TileRowHeightArray_ =
          videoEncoderParams.TileRowHeightArray_ + ","
          + std::to_string(TileRowHeights[i]);
    } else {  // forward packing case
      if (temp == 0)
        videoEncoderParams.TileRowHeightArray_ =
          videoEncoderParams.TileRowHeightArray_
          + std::to_string(TileRowHeights[TileRowHeights.size() - 1 - i]);
      else
        videoEncoderParams.TileRowHeightArray_ =
          videoEncoderParams.TileRowHeightArray_ + ","
          + std::to_string(TileRowHeights[TileRowHeights.size() - 1 - i]);
    }
    temp++;
  }

  videoEncoderParams.NumTileColumnsMinus1_             = 0;
  videoEncoderParams.TileUniformSpacing_               = false;
  videoEncoderParams.SEITempMotionConstrainedTileSets_ = true;
  videoEncoderParams.SEITMCTSTileConstraint_           = true;
  videoEncoderParams.sliceMode_                        = 0;
  videoEncoderParams.sliceArgument_                    = 1500;
  videoEncoderParams.LFCrossSliceBoundaryFlag_         = false;
  printf("*********************************************************\n");
  printf("TMCTS Enabled \n");
  printf("Parameters TileUniformSpacing %d, SEITempMotionConstrainedTileSets "
         "%d, SEITMCTSTileConstraint %d, sliceMode %d, sliceArgument %d, "
         "LFCrossSliceBoundaryFlag %d \n",
         videoEncoderParams.TileUniformSpacing_,
         videoEncoderParams.SEITempMotionConstrainedTileSets_,
         videoEncoderParams.SEITMCTSTileConstraint_,
         videoEncoderParams.sliceMode_,
         videoEncoderParams.sliceArgument_,
         videoEncoderParams.LFCrossSliceBoundaryFlag_);
  printf("Num Tiles Rows Minus 1 %d \n",
         videoEncoderParams.NumTileRowsMinus1_);
  printf("Num Tiles Cols Minus 1 %d \n",
         videoEncoderParams.NumTileColumnsMinus1_);
  printf("Tiles Row Heights %s \n",
         videoEncoderParams.TileRowHeightArray_.c_str());
  printf("Tiles Col Widths %s \n",
         videoEncoderParams.TileColumnWidthArray_.c_str());
  MCTSidx.resize(params.subdivisionIterationCount + 1, 0);
  int idx      = 0;
  int patchIdx = 0;
  if (params.displacementReversePacking) patchIdx = lodCount - 1;
  else patchIdx = 0;
  for (int i = 0; i < lodCount; i++) {
    if ((patchIdx == params.subdivisionIterationCount
         && params.displacementReversePacking)
        || (patchIdx == 0 && !params.displacementReversePacking)) {
      MCTSidx[patchIdx] = idx;
      idx++;
    } else MCTSidx[patchIdx] = idx;
    if (params.displacementReversePacking) patchIdx--;
    else patchIdx++;
  }
  return 0;
}

//============================================================================
bool
VMCEncoder::setAttributeMCTSVideoEncoderParams(
  const VMCEncoderParameters& params,
  VideoEncoderParameters&     videoEncoderParams) {
  // temporary code for testing
  auto attributeCount =
    params.numTextures > 1 ? params.numTextures : params.videoAttributeCount;
  attrMCTSidx.resize(attributeCount, 0);
  for (int attributeIdx = 0; attributeIdx < attributeCount; attributeIdx++) {
    attrMCTSidx[attributeIdx] = 1;
  }
  attrMCTSidx[0] = 0;
  return 0;
}
#if CONFORMANCE_LOG_ENC
void
VMCEncoder::getHighLevelSyntaxLog(V3cBitstream& syntax) {
  auto& adStream = syntax.getAtlasDataStream();
  auto& atlList  = adStream.getAtlasTileLayerList();
  for (int atlIdx = 0; atlIdx < atlList.size(); atlIdx++) {
    auto&  atgl                = atlList[atlIdx];
    auto&  ath                 = atgl.getHeader();
    auto   afpsId              = ath.getAtlasFrameParameterSetId();
    auto&  afps                = adStream.getAtlasFrameParameterSet(afpsId);
    auto   aspsId              = afps.getAtlasSequenceParameterSetId();
    auto&  asps                = adStream.getAtlasSequenceParameterSet(aspsId);
    size_t atlasFrmOrderCntMsb = 0;
    size_t atlasFrmOrderCntVal = 0;
    int    frameIndex          = adStream.calculateAFOCval(
      asps, afps, atlIdx, atlasFrmOrderCntMsb, atlasFrmOrderCntVal);
    // PREFIX SEI
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI,
                                   COMPONENT_CODEC_MAPPING)) {
      auto* sei = static_cast<SEIComponentCodecMapping*>(
        atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, COMPONENT_CODEC_MAPPING));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS("**********CODEC_COMPONENT_MAPPING_ESEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI,
                                   ATTRIBUTE_EXTRACTION_INFORMATION)) {
      auto* sei =
        static_cast<SEIAttributeExtractionInformation*>(atgl.getSEI().getSei(
          ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_EXTRACTION_INFORMATION));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS(
          "**********ATTRIBUTE_EXTRACTION_INFORMATION_ESEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02d-%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  sei->getVdmcPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI,
                                   ATTRIBUTE_TRANSFORMATION_PARAMS)) {
      auto* sei =
        static_cast<SEIAttributeTransformationParams*>(atgl.getSEI().getSei(
          ATLAS_NAL_PREFIX_ESEI, ATTRIBUTE_TRANSFORMATION_PARAMS));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS("**********ATTRIBUTE_TRANSFORMATION_PARAMETERS_V3C_SEI******"
                  "*****\n");
        Checksum checksum;
        TRACE_HLS("SEI%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI,
                                   LOD_EXTRACTION_INFORMATION)) {
      auto* sei =
        static_cast<SEILoDExtractionInformation*>(atgl.getSEI().getSei(
          ATLAS_NAL_PREFIX_ESEI, LOD_EXTRACTION_INFORMATION));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS("**********LOD_EXTRACTION_INFORMATION_ESEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02d-%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  sei->getVdmcPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI,
                                   TILE_SUBMESH_MAPPING)) {
      auto* sei = static_cast<SEITileSubmeshMapping*>(
        atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, TILE_SUBMESH_MAPPING));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS("**********TILE_SUBMESH_MAPPING_SEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02d-%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  sei->getVdmcPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_ESEI, ZIPPERING)) {
      auto* sei = static_cast<SEIZippering*>(
        atgl.getSEI().getSei(ATLAS_NAL_PREFIX_ESEI, ZIPPERING));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS("**********ZIPPERING_SEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02d-%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  sei->getVdmcPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI, SUBMESH_SOI_RELATIONSHIP_INDICATION)) {
        auto* sei =
            static_cast<SEISubmeshSOIIndicationRelationship*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_NSEI, SUBMESH_SOI_RELATIONSHIP_INDICATION));
        auto& temp = sei->getMD5ByteStrData();
        if (temp.size() > 0) {
            TRACE_HLS("**********SUBMESH_SOI_RELATIONSHIP_INDICATION_SEI***********\n");
            Checksum checksum;
            TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
        }
    }
    if (atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI, SUBMESH_DISTORTION_INDICATION)) {
        auto* sei =
            static_cast<SEISubmeshDistortionIndication*>(atgl.getSEI().getSei(ATLAS_NAL_PREFIX_NSEI, SUBMESH_DISTORTION_INDICATION));
        auto& temp = sei->getMD5ByteStrData();
        if (temp.size() > 0) {
            TRACE_HLS("**********SUBMESH_DISTORTION_INDICATION_SEI***********\n");
            Checksum checksum;
            TRACE_HLS("SEI%02d-%02dMD5 = %s\n", sei->getPayloadType(), sei->getVdmcPayloadType(), checksum.getChecksum(temp).c_str());
        }
    }
    //SUFFIX SEI
    bool isLastTileOfTheFrames =
      atlIdx + 1 == atlList.size()
      || adStream.calculateAFOCval(
           asps, afps, atlIdx, atlasFrmOrderCntMsb, atlasFrmOrderCntVal)
           != adStream.calculateAFOCval(
             asps, afps, atlIdx + 1, atlasFrmOrderCntMsb, atlasFrmOrderCntVal);
    if (isLastTileOfTheFrames
        && atgl.getSEI().seiIsPresent(ATLAS_NAL_PREFIX_NSEI,
                                      DECODED_ATLAS_INFORMATION_HASH)) {
      auto* sei =
        static_cast<SEIDecodedAtlasInformationHash*>(atgl.getSEI().getSei(
          ATLAS_NAL_PREFIX_NSEI, DECODED_ATLAS_INFORMATION_HASH));
      auto& temp = sei->getMD5ByteStrData();
      if (temp.size() > 0) {
        TRACE_HLS(
          "**********DECODED_ATLAS_INFORMATION_HASH_NSEI***********\n");
        Checksum checksum;
        TRACE_HLS("SEI%02dMD5 = %s\n",
                  sei->getPayloadType(),
                  checksum.getChecksum(temp).c_str());
      }
    }
    if (isLastTileOfTheFrames) {
      std::vector<uint8_t> highLevelAtlasData;
      Checksum             checksum;
      adStream.aspsCommonByteString(highLevelAtlasData, asps);
      adStream.aspsApplicationByteString(highLevelAtlasData, asps);
      adStream.afpsCommonByteString(highLevelAtlasData, asps, afps);
      adStream.afpsApplicationByteString(highLevelAtlasData, asps, afps);
#  if DEBUG_CONFORMANCE
      for (auto& c : highLevelAtlasData) TRACE_HLS("\n%02x", c);
      TRACE_HLS("\n");
#  endif
      TRACE_HLS("AtlasFrameIndex = %d, ", frameIndex);
      TRACE_HLS("HLSMD5 = %s\n",
                checksum.getChecksum(highLevelAtlasData).c_str());
    }
  }
}
void
VMCEncoder::getPictureLog() {}
void
VMCEncoder::getBasemeshLog(int32_t frameCount) {
  int numSubmeshes = reconBasemeshes_.size();
  for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    TRACE_BASEMESH("IdxOutOrderCntVal = %d, ", frameIndex);
    TRACE_BASEMESH("NumSubmeshes = %d,\n", numSubmeshes);
    for (int smIdx = 0; smIdx < numSubmeshes; smIdx++) {
      int      submeshId;
      int      numVertices;
      int      numAttributes;
      auto&    sm = reconBasemeshes_[smIdx][frameIndex];
      Checksum checksum;
      for (int i = 0; i < submeshIdtoIndex_.size(); i++) {
        if (smIdx == submeshIdtoIndex_[i]) {
          submeshId = i;
          break;
        }
      }
      numVertices   = sm.pointCount();
      numAttributes = (sm.texCoordCount() > 0);
      TRACE_BASEMESH("SubmeshID = %d, NumVertices = %d, NumAttributes = %d, "
                     "MD5checkSumSubmesh = %s,\n",
                     submeshId,
                     numVertices,
                     numAttributes,
                     checksum.getChecksumConformance(sm).c_str());
    }
  }
}
void
VMCEncoder::getACDisplacementLog(
  std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
  int32_t                                      frameCount,
  V3cBitstream&                                syntax,
  const VMCEncoderParameters&                  params) {
  auto& dmSubstream = syntax.getDisplacementStream();
  auto& dfps =
    dmSubstream.getDisplFrameParameterSet(0);  //assuming we only have one FPS
  auto& dinfo = dfps.getDisplInformation();
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    TRACE_DISPLACEMENT("IdxOutOrderCntVal = %d, NumSubdisplacements = %d,\n",
                       frameIdx,
                       reconSubdivmeshes_.size());
    for (int32_t subDisplIdx = 0; subDisplIdx < encSubmeshes.size();
         subDisplIdx++) {
      int displId = -1;
      for (int id = 0; id < dinfo._displIDToIndex.size(); id++) {
        if (dinfo._displIDToIndex[id] == subDisplIdx) {
          displId = id;
          break;
        }
      }
      auto&    submeshRecDisp   = encSubmeshes[subDisplIdx][frameIdx].disp;
      int      numDisplacements = submeshRecDisp.size();
      Checksum checksum;
      TRACE_DISPLACEMENT("SubdisplacementID = %d, NumDisplacements = %d, "
                         "MD5checkSumSubdisplacement = %s,\n",
                         displId,
                         numDisplacements,
                         checksum.getChecksum(submeshRecDisp).c_str());
    }
  }
}
void
VMCEncoder::getAtlasLog(int32_t                     frameCount,
                        V3cBitstream&               syntax,
                        V3CParameterSet&            vps,
                        const VMCEncoderParameters& params) {
  int   numTiles = params.numTilesGeometry + params.numTilesAttribute;
  auto& atlas    = syntax.getAtlasDataStream();
  for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    TRACE_ATLAS("AtlasFrameIndex = %d\n", frameIndex);
    //get the first tile from the frame index to get the AFPS and ASPS
    auto& atlFirst = atlas.getAtlasTileLayerList()[numTiles * frameIndex];
    auto& athFirst = atlFirst.getHeader();
    TRACE_ATLAS("AtlasFrameOrderCntVal = %d, ",
                athFirst.getAtlasFrmOrderCntVal());
    auto atlasID = syntax.getAtlasId();
    TRACE_ATLAS("AtlasID = % d, ", atlasID)
    auto  afpsID = athFirst.getAtlasFrameParameterSetId();
    auto& afps   = atlas.getAtlasFrameParameterSet(afpsID);
    auto  aspsID = afps.getAtlasSequenceParameterSetId();
    auto& asps   = atlas.getAtlasSequenceParameterSet(aspsID);
    auto& asve   = asps.getAsveExtension();
    TRACE_ATLAS(
      "AtlasFrameWidth =  %d, AtlasFrameHeight = %d, ASPSFrameSize = %d, ",
      asps.getFrameWidth(),
      asps.getFrameHeight(),
      asps.getFrameWidth() * asps.getFrameHeight());
    int attrCount = 0;
    if (vps.getAttributeVideoPresentFlag(0) == 0) {
      // check if the PACKED VIDEO is present
      if (vps.getVpsPackedVideoExtension().getPackedVideoPresentFlag(0)) {
        attrCount = vps.getVpsPackedVideoExtension()
                      .getPackingInformation(0)
                      .pin_attribute_count();
      }
    } else {
      auto& ai  = vps.getAttributeInformation(atlasID);
      attrCount = ai.getAttributeCount();
    }
    TRACE_ATLAS("AttributeCount  = % d, ", attrCount);
    auto& vpve          = vps.getVpsVdmcExtension();
    int   attrMaxWidth  = 0;
    int   attrMaxHeight = 0;
    // using VpsAttributeNominalFrameSizeCount when VpsExtConsistentAttributeFrameFlag is false
    int numAttrFrameSize = vpve.getVpsExtConsistentAttributeFrameFlag(atlasID)
                             ? vps.getVpsAttributeNominalFrameSizeCount(atlasID)
                             : attrCount;
    for (int i = 0; i < numAttrFrameSize; i++) {
      if (vpve.getVpsExtAttributeFrameHeight(atlasID, i) > attrMaxHeight)
        attrMaxHeight = vpve.getVpsExtAttributeFrameHeight(atlasID, i);
      if (vpve.getVpsExtAttributeFrameWidth(atlasID, i) > attrMaxWidth)
        attrMaxWidth = vpve.getVpsExtAttributeFrameWidth(atlasID, i);
    }
    TRACE_ATLAS("AttributeFrameWidthMax =  %d, AttributeFrameHeightMax = %d, "
                "AttributeFrameSizeMax = %d, ",
                attrMaxWidth,
                attrMaxHeight,
                attrMaxWidth * attrMaxHeight);
    TRACE_ATLAS("BasemeshAttributeCount  = % d, ",
                vpve.getVpsExtMeshDataAttributeCount(atlasID));
    auto& afti = afps.getAtlasFrameTileInformation();
    TRACE_ATLAS("NumGeometryTilesAtlasFrame   = % d, ", afti.getTileCount());
    auto& afve            = afps.getAfveExtension();
    int   numAttrTilesMax = 0;
    for (int i = 0; i < afve.getAtlasFrameTileAttributeInformation().size();
         i++) {
      if (afve.getAtlasFrameTileAttributeInformation(i).getTileCount()
          > numAttrTilesMax)
        numAttrTilesMax =
          afve.getAtlasFrameTileAttributeInformation(i).getTileCount();
    }
    TRACE_ATLAS("NumAttributeTilesAtlasFrameMax   = % d, ", numAttrTilesMax);
    auto& afmi = afve.getAtlasFrameMeshInformation();
    TRACE_ATLAS("NumSubmeshes = % d, ",
                afmi.getNumSubmeshesInAtlasFrameMinus1() + 1);
    //now count all the meshpatches in the geometry and attribute tiles
    int numTiles = params.numTilesGeometry + params.numTilesAttribute;
    int atlasTotalMeshpatches = 0;
    std::vector<uint8_t> atlasData;
    Checksum             checksum;
    for (int tileIndex = 0; tileIndex < numTiles; tileIndex++) {
      auto& atl =
        atlas.getAtlasTileLayerList()[numTiles * frameIndex + tileIndex];
      auto& ath = atl.getHeader();
      bool skipTile = false;
      if (ath.getType() == I_TILE_ATTR) {
        //check if the tile NAL unit should be present or not
        for (int i = 0; i < afve.getAtlasFrameTileAttributeInformation().size();
          i++) {
          auto afati = afve.getAtlasFrameTileAttributeInformation(i);
          if (afati.getNumTilesInAtlasFrameMinus1() == 0 && afati.getTileId(0) == ath.getAtlasTileHeaderId() && !afati.getNalTilePresentFlag())
          {
            skipTile = true;
            break;
          }
        }
      }
      if (!skipTile) {
      int   numMeshpatchesInTile =
        reconAtlasTiles_[tileIndex][frameIndex].patches_.size();
      atlasTotalMeshpatches += numMeshpatchesInTile;
      for (int patchIndex = 0; patchIndex < numMeshpatchesInTile;
           patchIndex++) {
        auto& meshpatchHLS =
          atl.getDataUnit().getPatchInformationData(patchIndex);
        auto& meshPatchEnc =
            reconAtlasTiles_[tileIndex][frameIndex].patches_[patchIndex];
        VMCPatch atlasMeshpatch;
        //converting the encoder variable
        atlasMeshpatch = convertVCMAtlasPatch(asve, afve, ath, meshPatchEnc);
        if ((ath.getType() == SKIP_TILE) || (ath.getType() == SKIP_TILE_ATTR)) {
          // do nothing
          }
          else if ((ath.getType() == I_TILE) || (ath.getType() == P_TILE)) {
          atlas.atlasMeshpatchCommonByteString(
            atlasData, asps, asve, afve, atlasMeshpatch, true);
          }
          else {
          auto& afati   = afve.getAtlasFrameTileAttributeInformation();
          int   attrIdx = 0;
          for (int i = 0; i < afati.size(); i++) {
            for (int j = 0; j < afati[i].getTileCount(); j++) {
              if (ath.getAtlasTileHeaderId() == afati[i].getTileId(j)) {
                attrIdx = i;
                break;
              }
            }
          }
          atlas.atlasMeshpatchCommonByteString(
            atlasData, asps, asve, afve, atlasMeshpatch, false, attrIdx);
        }
      }
    }
    }
    TRACE_ATLAS("AtlasTotalMeshpatches = % d, ", atlasTotalMeshpatches);
#  if DEBUG_CONFORMANCE
    for (auto& c : atlasData) TRACE_ATLAS("\n%02x", c);
    TRACE_ATLAS("\n");
#  endif
    TRACE_ATLAS("ATLASMD5 = %s\n", checksum.getChecksum(atlasData).c_str());
  }
}
void
VMCEncoder::getTileLog(int32_t                     frameCount,
                       V3cBitstream&               syntax,
                       V3CParameterSet&            vps,
                       const VMCEncoderParameters& params) {
  int   numTiles = params.numTilesGeometry + params.numTilesAttribute;
  auto& atlas    = syntax.getAtlasDataStream();
  for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    TRACE_TILE("AtlasFrameIndex = %d\n", frameIndex);
    for (int tileIdx = 0; tileIdx < numTiles; tileIdx++) {
      auto  atlasID = syntax.getAtlasId();
      auto& atl =
        atlas.getAtlasTileLayerList()[numTiles * frameIndex + tileIdx];
      auto& ath    = atl.getHeader();
      auto  tileID = ath.getAtlasTileHeaderId();
      bool skipTile = false;
      if ((ath.getType() == I_TILE_ATTR)) {
        auto  afpsID = ath.getAtlasFrameParameterSetId();
        auto& afps = atlas.getAtlasFrameParameterSet(afpsID);
        auto& afve = afps.getAfveExtension();
        //check if the tile NAL unit should be present or not
        for (int i = 0; i < afve.getAtlasFrameTileAttributeInformation().size();
          i++) {
          auto afati = afve.getAtlasFrameTileAttributeInformation(i);
          if (afati.getNumTilesInAtlasFrameMinus1() == 0 && afati.getTileId(0) == ath.getAtlasTileHeaderId() && !afati.getNalTilePresentFlag())
          {
            skipTile = true;
            break;
          }
        }
      }
      if (!skipTile) {
      TRACE_TILE("TileID = % d, AtlasFrameOrderCntVal = %d, TileType = %d, ",
                 tileID,
                 ath.getAtlasFrmOrderCntVal(),
                 (int)ath.getType());
      auto  afpsID = ath.getAtlasFrameParameterSetId();
      auto& afps   = atlas.getAtlasFrameParameterSet(afpsID);
      auto& afve   = afps.getAfveExtension();
      auto  aspsID = afps.getAtlasSequenceParameterSetId();
      auto& asps   = atlas.getAtlasSequenceParameterSet(aspsID);
      auto& asve   = asps.getAsveExtension();
      auto& ai     = vps.getAttributeInformation(atlasID);
      auto& vpve   = vps.getVpsVdmcExtension();
      int   tileOffsetX, tileOffsetY, tileWidth, tileHeight = 0;
      if ((ath.getType() == SKIP_TILE ) || (ath.getType() == SKIP_TILE_ATTR )) {
        continue;
        }
        else if ((ath.getType() == I_TILE_ATTR) || (ath.getType() == P_TILE_ATTR)) {
        int attrIdx     = 0;
        int attrTileIdx = 0;
        //search for the indices given the tile ID
        auto& afatiList = afve.getAtlasFrameTileAttributeInformation();
        for (int j = 0; j < afatiList.size(); j++) {
          for (int l = 0; l < afatiList[j].getNumTilesInAtlasFrameMinus1() + 1;
               l++) {
            if (afatiList[j].getTileId(l) == tileID) {
              attrIdx     = j;
              attrTileIdx = l;
              break;
            }
          }
        }
        std::vector<imageArea> decodedTextureTileAreas;
        AtlasEncoder::reconstructTileInformation(
          decodedTextureTileAreas, attrIdx, afps, asps);
        TRACE_TILE("TileOffsetX = %d, TileOffsetY = %d, TileWidth = %d, "
                   "TileHeight = %d, ",
                   decodedTextureTileAreas[attrTileIdx].LTx,
                   decodedTextureTileAreas[attrTileIdx].LTy,
                   decodedTextureTileAreas[attrTileIdx].sizeX,
                   decodedTextureTileAreas[attrTileIdx].sizeY)
        }
        else {
        int geoTileIdx = 0;
        //search for the indices given the tile ID
        auto& afti = afps.getAtlasFrameTileInformation();
        for (int l = 0; l < afti.getNumTilesInAtlasFrameMinus1() + 1; l++) {
          if (afti.getTileId(l) == tileID) {
            geoTileIdx = l;
            break;
          }
        }
        std::vector<imageArea> decodedGeometryTileAreas;
        AtlasEncoder::reconstructTileInformation(decodedGeometryTileAreas, -1, afps, asps);
        TRACE_TILE("TileOffsetX = %d, TileOffsetY = %d, TileWidth = %d, "
                   "TileHeight = %d, ",
                   decodedGeometryTileAreas[geoTileIdx].LTx,
                   decodedGeometryTileAreas[geoTileIdx].LTy,
                   decodedGeometryTileAreas[geoTileIdx].sizeX,
                   decodedGeometryTileAreas[geoTileIdx].sizeY)
      }
      //now count all the meshpatches in the geometry and attribute tiles
      Checksum             checksum;
      std::vector<uint8_t> atlasData;
      int                  numMeshpatchesInTile =
        reconAtlasTiles_[tileIdx][frameIndex].patches_.size();
      for (int patchIndex = 0; patchIndex < numMeshpatchesInTile;
           patchIndex++) {
        auto& meshpatchHLS =
          atl.getDataUnit().getPatchInformationData(patchIndex);
        auto& meshPatchEnc =
            reconAtlasTiles_[tileIdx][frameIndex].patches_[patchIndex];
        VMCPatch tileMeshpatch;
        //converting the encoder variable
        tileMeshpatch = convertVCMAtlasPatch(asve, afve, ath, meshPatchEnc);
        atlas.tileMeshpatchCommonByteString(
          atlasData, asps, asve, afve, tileMeshpatch, ath);
      }
      TRACE_TILE("TileTotalMeshpatches = % d, ", numMeshpatchesInTile);
#  if DEBUG_CONFORMANCE
      for (auto& c : atlasData) TRACE_TILE("\n%02x", c);
      TRACE_TILE("\n");
#  endif
      TRACE_TILE("TILEMD5 = %s\n", checksum.getChecksum(atlasData).c_str());
    }
  }
}
}
void
VMCEncoder::getMFrameLog(int32_t        frameCount,
                         int32_t        submeshCount,
                         const uint32_t bitDepthTexCoord) {
  uint32_t vertexCount = 0;
  for (int32_t frameIdx = 0; frameIdx < frameCount; frameIdx++) {
    TriangleMesh<MeshType> reconstructed;
    if (submeshCount == 1) {
      reconstructed = reconSubdivmeshes_[0][frameIdx];
    } else {
      for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        reconstructed.append(reconSubdivmeshes_[submeshIdx][frameIdx]);
      }
    }
    // revert the texture coordinate dequantization
    const auto scale         = (1 << bitDepthTexCoord) - 1.0;
    const auto texCoordCount = reconstructed.texCoordCount();
    for (int32_t i = 0; i < texCoordCount; ++i) {
      reconstructed.setTexCoord(i, reconstructed.texCoord(i) * scale);
    }
    vertexCount = reconstructed.pointCount();
    TRACE_MFRAME("AtlasFrameIndex = %d, ", frameIdx);
    TRACE_MFRAME(
      "MeshFrameOrderCntVal = %d, NumSubmeshes = %d, NumVertices = %d,",
      frameIdx,
      submeshCount,
      vertexCount);
    Checksum checksum;
    TRACE_MFRAME(" MD5checksum = %s\n",
                 checksum.getChecksumConformance(reconstructed).c_str());
  }
}
void
VMCEncoder::getRecFrameLog(int32_t                 frameIndex,
                           TriangleMesh<MeshType>& reconstructed) {
  TRACE_RECFRAME(
    "AtlasFrameIndex = %d, MeshFrameOrderCntVal = %d, NumVertices = %d,",
    frameIndex,
    frameIndex,
    reconstructed.pointCount());
  Checksum checksum;
  TRACE_RECFRAME(" MD5checksum = %s\n",
                 checksum.getChecksumConformance(reconstructed).c_str());
}
// revert -> readd conversion to avoid crash in AtlasBitstream ByteStr conversions
VMCPatch
VMCEncoder::convertVCMAtlasPatch(AspsVdmcExtension& asve,
                                 AfpsVdmcExtension& afve,
                                 AtlasTileHeader&   ath,
                                 AtlasPatch&     var) {
  VMCPatch ret;
  // ID
  ret.submeshId_ = var.submeshId_;
  if ((ath.getType() == I_TILE_ATTR) || (ath.getType() == P_TILE_ATTR)) {
    ret.attributePatchArea = var.attributePatchArea;
  } else if ((ath.getType() == I_TILE) || (ath.getType() == P_TILE)) {
    ret.displId_ = var.submeshId_;  //TODO
    ret.lodIdx_  = var.lodIdx_;
    // location
    ret.geometryPatchArea = var.geometryPatchArea;
    if (ret.lodIdx_ == 0) {
      // subdivision
      ret.subdivIteration     = var.subdivIteration;
      ret.subdivMethod        = var.subdivMethod;
      ret.subdivMinEdgeLength = var.subdivMinEdgeLength;
      //quantization TODO
      ret.IQ_skip = var.IQ_skip;
      if (!var.IQ_skip) {
        ret.quantizationParameters = var.quantizationParameters;
        ret.iqOffsetFlag           = var.iqOffsetFlag;
        if (var.iqOffsetFlag) { ret.iqOffsets = var.iqOffsets; }
      }
      // coordinate system
      ret.displacementCoordinateSystem = var.displacementCoordinateSystem;
      // transform TODO
      ret.transformMethod = var.transformMethod;
      if (var.transformMethod == (uint8_t)TransformMethod::LINEAR_LIFTING) {
        if (asve.getAsveLiftingOffsetPresentFlag()) {
          ret.transformParameters.liftingOffsetValuesNumerator_ =
            var.transformParameter.getLiftingOffsetValuesNumerator();
          ret.transformParameters.liftingOffsetValuesDenominator_ =
            var.transformParameter.getLiftingOffsetValuesDenominatorMinus1();
        }
        if (asve.getAsveDirectionalLiftingPresentFlag()) {
          ret.dirLiftParams.MeanNumerator_ = var.directionalLiftParameterss[0];
          ret.dirLiftParams.MeanDenominator_ =
            var.directionalLiftParameterss[1];
          ret.dirLiftParams.StdNumerator_   = var.directionalLiftParameterss[2];
          ret.dirLiftParams.StdDenominator_ = var.directionalLiftParameterss[3];
        }
        ret.transformParameters.skipUpdateFlag_ =
          var.transformParameter.getSkipUpdateFlag();
        ret.transformParameters.valenceUpdateWeightFlag_ =
          var.transformParameter.getValenceUpdateWeightFlag();
        ret.transformParameters.updateWeight_ = var.updateWeight_;
        ret.transformParameters.predWeight_   = var.predWeight_;
      }
    }
    // vertex count
    if (asve.getAsveLodPatchesEnableFlag()) {
      ret.blockCount.resize(1);
      ret.blockCount[0] = var.blockCount[var.lodIdx_];
      ret.lastPosInBlock.resize(1);
      ret.lastPosInBlock[0] = var.lastPosInBlock[var.lodIdx_];
    } else {
      ret.blockCount = var.blockCount;
      ret.lastPosInBlock   = var.lastPosInBlock;
    }
    if (ret.lodIdx_ == 0) {
      // orthoAtlas
      int numSubpatches                           = var.packedCCList.size();
      ret.projectionTextcoordSubpatchCountMinus1_ = numSubpatches - 1;
      ret.projectionTextcoordFrameScale_          = var.frameScale;
      ret.projectionTextcoordSubpatches_.resize(numSubpatches);
      for (int spIdx = 0; spIdx < numSubpatches; spIdx++) {
        auto& retSubpatch = ret.projectionTextcoordSubpatches_[spIdx];
        auto& varSubpatch = var.packedCCList[spIdx];
        // load subpatch variables
        retSubpatch.projectionTextcoordFaceId_ = varSubpatch.getFaceId();
        retSubpatch.projectionTextcoordProjectionId_ =
          varSubpatch.getProjection();
        retSubpatch.projectionTextcoordOrientationId_ =
          varSubpatch.getOrientation();
        retSubpatch.projectionTextcoord2dPosX_ = varSubpatch.getU0();
        retSubpatch.projectionTextcoord2dPosY_ = varSubpatch.getV0();
        if (asve.getAsveProjectionTexcoordBboxBiasEnableFlag()) {
          retSubpatch.projectionTextcoord2dBiasX_ = varSubpatch.getU1();
          retSubpatch.projectionTextcoord2dBiasY_ = varSubpatch.getV1();
          retSubpatch.projectionTextcoord2dSizeXMinus1_ =
            varSubpatch.getSizeU() - 1;
          retSubpatch.projectionTextcoord2dSizeYMinus1_ =
            varSubpatch.getSizeV() - 1;
        } else {
          retSubpatch.projectionTextcoord2dBiasX_       = 0;
          retSubpatch.projectionTextcoord2dBiasY_       = 0;
          retSubpatch.projectionTextcoord2dSizeXMinus1_ = 0;
          retSubpatch.projectionTextcoord2dSizeYMinus1_ = 0;
        }
        retSubpatch.projectionTextcoordScalePresentFlag_ =
          (varSubpatch.getScale() != varSubpatch.getFrameScale());
        auto   ratio = varSubpatch.getScale() / varSubpatch.getFrameScale();
        double num   = log(ratio);
        double den   = log(
          (double)(asve.getAsveProjectionTexCoordUpscaleFactorMinus1() + 1)
          / (double)(pow(2, asve.getAsveProjectionTexcoordLog2DownscaleFactor())));
        double nDbl                                   = num / den;
        retSubpatch.projectionTextcoordSubpatchScale_ = round(nDbl) - 1;
        retSubpatch.numRawUVMinus1_ = varSubpatch.getNumRawUVMinus1();
        retSubpatch.uCoords_        = varSubpatch.getUcoords();
        retSubpatch.vCoords_        = varSubpatch.getVcoords();
      }
    }
  }
  return ret;
}

#endif
}  // namespace vmesh
