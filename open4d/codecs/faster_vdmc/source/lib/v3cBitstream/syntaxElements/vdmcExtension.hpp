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

#include "v3cCommon.hpp"
#include "../../atlasBitstream/syntaxElements/atlasFrameMeshInformation.hpp"
#include "../../atlasBitstream/syntaxElements/atlasFrameTileInformation.hpp"
#include "../../atlasBitstream/syntaxElements/atlasFrameTileAttributeInformation.hpp"
#include "vmc.hpp"
#include "displacementInformation.hpp"
namespace vmesh {

// VPS V-DMC extension syntax
class VpsVdmcExtension {
public:
  VpsVdmcExtension(){
      setAtlasCount(0);
  }
  VpsVdmcExtension(uint32_t atlasCount) {
    setAtlasCount(atlasCount);
  }
 ~VpsVdmcExtension() {
     clearAtlasCount();
 }
  VpsVdmcExtension& operator=( const VpsVdmcExtension& ) = default;

  uint8_t getAtlasCount() { return atlas_count_; }
  void   setAtlasCount(uint32_t value) {
      atlas_count_ = value;
      vps_ext_mesh_data_substream_codec_id_.resize(atlas_count_, 0);
      vps_ext_mesh_geo_3d_bit_depth_minus1_.resize(atlas_count_, 0);
      vps_ext_mesh_geo_3d_msb_align_flag_.resize(atlas_count_, 0);
      vps_ext_mesh_data_attribute_count_.resize(atlas_count_, 0);
      vps_ext_bmesh_attribute_index_.resize(atlas_count_);
      vps_ext_mesh_attribute_bitdepth_minus1_.resize(atlas_count_);
      vps_ext_mesh_attribute_msb_align_flag_.resize(atlas_count_);
      vps_ext_mesh_attribute_type_.resize(atlas_count_);
      vps_ext_consistent_attribute_frame_flag.resize(atlas_count_);
      vps_ext_attribute_frame_width_.resize(atlas_count_);
      vps_ext_attribute_frame_height_.resize(atlas_count_);
      vps_ext_ac_displacement_present_flag_.resize(atlas_count_, 0);
      displacementInformation_.resize(atlas_count_);
  }
  void   clearAtlasCount() {
      for (int atlasIdx = 0; atlasIdx < atlas_count_; atlasIdx++) {
          vps_ext_attribute_frame_height_[atlasIdx].clear();
          vps_ext_attribute_frame_width_[atlasIdx].clear();
          vps_ext_mesh_attribute_type_[atlasIdx].clear();
          vps_ext_mesh_attribute_msb_align_flag_[atlasIdx].clear();
          vps_ext_mesh_attribute_bitdepth_minus1_[atlasIdx].clear();
          vps_ext_bmesh_attribute_index_[atlasIdx].clear();
      }
      vps_ext_consistent_attribute_frame_flag.clear();
      vps_ext_attribute_frame_height_.clear();
      vps_ext_attribute_frame_width_.clear();
      vps_ext_mesh_attribute_type_.clear();
      vps_ext_mesh_attribute_msb_align_flag_.clear();
      vps_ext_mesh_attribute_bitdepth_minus1_.clear();
      vps_ext_bmesh_attribute_index_.clear();
      vps_ext_mesh_data_attribute_count_.clear();
      vps_ext_mesh_geo_3d_msb_align_flag_.clear();
      vps_ext_mesh_geo_3d_bit_depth_minus1_.clear();
      vps_ext_mesh_data_substream_codec_id_.clear();
      vps_ext_ac_displacement_present_flag_.clear();
      atlas_count_ = 0;
  }
  // get functions
  uint8_t getVpsExtMeshDataSubstreamCodecId(size_t atlasIdx) { return vps_ext_mesh_data_substream_codec_id_[atlasIdx]; }
  uint8_t getVpsExtMeshGeo3dBitdepthMinus1(size_t atlasIdx)  { return vps_ext_mesh_geo_3d_bit_depth_minus1_[atlasIdx]; }
  uint8_t getVpsExtMeshGeo3dMSBAlignFlag(size_t atlasIdx)    { return vps_ext_mesh_geo_3d_msb_align_flag_[atlasIdx]; }
  uint8_t getVpsExtMeshDataAttributeCount(size_t atlasIdx)   { return vps_ext_mesh_data_attribute_count_[atlasIdx]; }
  uint8_t getVpsExtMeshAttributeIndex(size_t atlasIdx, size_t attIdx)          { return vps_ext_bmesh_attribute_index_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeBitdepthMinus1(size_t atlasIdx, size_t attIdx) { return vps_ext_mesh_attribute_bitdepth_minus1_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeMSBAlignFlag(size_t atlasIdx, size_t attIdx)   { return vps_ext_mesh_attribute_msb_align_flag_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeType(size_t atlasIdx, size_t attIdx)           { return vps_ext_mesh_attribute_type_[atlasIdx][attIdx]; }
  uint8_t getVpsExtConsistentAttributeFrameFlag(size_t atlasIdx){ return vps_ext_consistent_attribute_frame_flag[atlasIdx];}
  uint32_t getVpsExtAttributeFrameWidth(size_t atlasIdx, size_t attIdx)         { return vps_ext_attribute_frame_width_[atlasIdx][attIdx]; }
  uint32_t getVpsExtAttributeFrameHeight(size_t atlasIdx, size_t attIdx)        { return vps_ext_attribute_frame_height_[atlasIdx][attIdx]; }
  uint8_t getVpsExtACDisplacementPresentFlag(size_t index) { return vps_ext_ac_displacement_present_flag_[index]; }
  DisplacementInformation& getDisplacementInformation(size_t index) { return displacementInformation_[index]; }
  // get functions const
  uint8_t getVpsExtMeshDataSubstreamCodecId(size_t atlasIdx) const { return vps_ext_mesh_data_substream_codec_id_[atlasIdx]; }
  uint8_t getVpsExtMeshGeo3dBitdepthMinus1(size_t atlasIdx) const { return vps_ext_mesh_geo_3d_bit_depth_minus1_[atlasIdx]; }
  uint8_t getVpsExtMeshGeo3dMSBAlignFlag(size_t atlasIdx) const { return vps_ext_mesh_geo_3d_msb_align_flag_[atlasIdx]; }
  uint8_t getVpsExtMeshDataAttributeCount(size_t atlasIdx) const { return vps_ext_mesh_data_attribute_count_[atlasIdx]; }
  uint8_t getVpsExtMeshAttributeIndex(size_t atlasIdx, size_t attIdx) const { return vps_ext_bmesh_attribute_index_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeBitdepthMinus1(size_t atlasIdx, size_t attIdx) const { return vps_ext_mesh_attribute_bitdepth_minus1_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeMSBAlignFlag(size_t atlasIdx, size_t attIdx) const { return vps_ext_mesh_attribute_msb_align_flag_[atlasIdx][attIdx]; }
  uint8_t getVpsExtMeshAttributeType(size_t atlasIdx, size_t attIdx) const { return vps_ext_mesh_attribute_type_[atlasIdx][attIdx]; }
  uint8_t getVpsExtConsistentAttributeFrameFlag(size_t atlasIdx) const { return vps_ext_consistent_attribute_frame_flag[atlasIdx];}
  uint32_t getVpsExtAttributeFrameWidth(size_t atlasIdx, size_t attIdx) const { return vps_ext_attribute_frame_width_[atlasIdx][attIdx]; }
  uint32_t getVpsExtAttributeFrameHeight(size_t atlasIdx, size_t attIdx) const { return vps_ext_attribute_frame_height_[atlasIdx][attIdx]; }
  bool getVpsExtACDisplacementPresentFlag(size_t index) const {  return vps_ext_ac_displacement_present_flag_[index];  }
  const DisplacementInformation& getDisplacementInformation(size_t index) const { return displacementInformation_[index]; }
  //set functions
  void setVpsExtMeshDataSubstreamCodecId(uint8_t value, size_t atlasIdx) { vps_ext_mesh_data_substream_codec_id_[atlasIdx] = value; }
  void setVpsExtMeshGeo3dBitdepthMinus1(uint8_t value, size_t atlasIdx)  { vps_ext_mesh_geo_3d_bit_depth_minus1_[atlasIdx] = value; }
  void setVpsExtMeshGeo3dMSBAlignFlag(uint8_t value, size_t atlasIdx)    { vps_ext_mesh_geo_3d_msb_align_flag_[atlasIdx] = value; }
  void setVpsExtMeshDataAttributeCount(uint8_t value, size_t atlasIdx)   {
      vps_ext_mesh_data_attribute_count_[atlasIdx] = value;
      vps_ext_mesh_attribute_type_[atlasIdx].resize(vps_ext_mesh_data_attribute_count_[atlasIdx]);
      vps_ext_mesh_attribute_bitdepth_minus1_[atlasIdx].resize(vps_ext_mesh_data_attribute_count_[atlasIdx]);
      vps_ext_mesh_attribute_msb_align_flag_[atlasIdx].resize(vps_ext_mesh_data_attribute_count_[atlasIdx]);
      vps_ext_bmesh_attribute_index_[atlasIdx].resize(vps_ext_mesh_data_attribute_count_[atlasIdx]);
  }
  void setVpsExtMeshAttributeIndex(uint8_t value, size_t atlasIdx, size_t attIdx) { vps_ext_bmesh_attribute_index_[atlasIdx][attIdx] = value; }
  void setVpsExtMeshAttributeBitdepthMinus1(uint8_t value, size_t atlasIdx, size_t attIdx) { vps_ext_mesh_attribute_bitdepth_minus1_[atlasIdx][attIdx] = value; }
  void setVpsExtMeshAttributeMSBAlignFlag(uint8_t value, size_t atlasIdx, size_t attIdx) { vps_ext_mesh_attribute_msb_align_flag_[atlasIdx][attIdx] = value; }
  void setVpsExtMeshAttributeType(uint8_t value, size_t atlasIdx, size_t attIdx) { vps_ext_mesh_attribute_type_[atlasIdx][attIdx] = value; }
  void allocateAttributeFramesize(size_t aiAttributeCount, size_t atlasIdx) {
      vps_ext_attribute_frame_width_[atlasIdx].resize(aiAttributeCount);
      vps_ext_attribute_frame_height_[atlasIdx].resize(aiAttributeCount);
  }
  void setVpsExtConsistentAttributeFrameFlag(uint32_t value, size_t atlasIdx){ vps_ext_consistent_attribute_frame_flag[atlasIdx] = value; }
  void setVpsExtAttributeFrameWidth(uint32_t value, size_t atlasIdx, size_t attIdx) { vps_ext_attribute_frame_width_[atlasIdx][attIdx] = value; }
  void setVpsExtAttributeFrameHeight(uint32_t value, size_t atlasIdx, size_t attIdx) { vps_ext_attribute_frame_height_[atlasIdx][attIdx] = value; }
  void setVpsExtACDisplacementPresentFlag( size_t index, bool value ) { vps_ext_ac_displacement_present_flag_[index] = value; }
  void setDisplacementInformation( size_t index, DisplacementInformation value ) { displacementInformation_[index] = value; }

private:
  uint8_t                            atlas_count_;
  std::vector<uint8_t>               vps_ext_mesh_data_substream_codec_id_;//[atlasCount]
  std::vector<uint8_t>               vps_ext_mesh_geo_3d_bit_depth_minus1_;//[atlasCount]
  std::vector<uint8_t>               vps_ext_mesh_geo_3d_msb_align_flag_;  //[atlasCount]
  std::vector<uint8_t>               vps_ext_mesh_data_attribute_count_;   //[atlasCount]
  std::vector<std::vector<uint8_t>>  vps_ext_bmesh_attribute_index_;          //[atlasCount][basemesh attribute count]
  std::vector<std::vector<uint8_t>>  vps_ext_mesh_attribute_bitdepth_minus1_; //[atlasCount][basemesh attribute count]
  std::vector<std::vector<uint8_t>>  vps_ext_mesh_attribute_msb_align_flag_;  //[atlasCount][basemesh attribute count]
  std::vector<std::vector<uint8_t>>  vps_ext_mesh_attribute_type_;            //[atlasCount][basemesh attribute count]
  std::vector<uint8_t>               vps_ext_consistent_attribute_frame_flag; //[atlasCount]
  std::vector<std::vector<uint32_t>>  vps_ext_attribute_frame_width_;          //[atlasCount][attribute count]
  std::vector<std::vector<uint32_t>>  vps_ext_attribute_frame_height_;         //[atlasCount][attribute count]
  std::vector<uint8_t>                vps_ext_ac_displacement_present_flag_;
  std::vector<DisplacementInformation> displacementInformation_;
};

};  // namespace vmesh
