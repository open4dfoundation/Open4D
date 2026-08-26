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

#include <cstdint>
#include <cstddef>
#include <vector>
#include "baseMeshVuiParameters.hpp"
#include "baseMeshRefListStruct.hpp"

namespace basemesh {

//H.8.1.3.1.3  Profile toolset constraints information syntax
class BaseMeshProfileToolsetConstraintsInformation {
public:
  BaseMeshProfileToolsetConstraintsInformation():
  bmptc_one_mesh_frame_only_flag_(0),
  bmptc_intra_frames_only_flag_(0),
  bmptc_motion_vector_derivation_disable_flag_(0),
  bmptc_no_cra_bla_flag_(0),
  bmptc_no_rasl_flag_(0),
  bmptc_no_radl_flag_(0),
  bmptc_one_submesh_per_frame_flag_(0),
  bmptc_no_temporal_scalability_flag_(0),
  bmptc_num_reserved_constraint_bytes_(0) {
    bmptc_reserved_constraint_byte_.clear();
  }
  ~BaseMeshProfileToolsetConstraintsInformation(){}
  BaseMeshProfileToolsetConstraintsInformation& operator=( const BaseMeshProfileToolsetConstraintsInformation& ) = default;
  
  bool                  getBmptcOneBeshFrameOnlyFlag()      { return bmptc_one_mesh_frame_only_flag_;}
  bool                  getBmptcIntraFrameOnlyFlag()        { return bmptc_intra_frames_only_flag_; }
  bool                  getBmptcMotionVectorDerivationDisableFlag()         { return bmptc_motion_vector_derivation_disable_flag_; }
  const bool            getBmptcMotionVectorDerivationDisableFlag() const { return bmptc_motion_vector_derivation_disable_flag_; }
  bool                  getBmptcNoCraBlaFlag() { return bmptc_no_cra_bla_flag_; }
  bool                  getBmptcNoRaslFlag() { return bmptc_no_rasl_flag_; }
  bool                  getBmptcNoRadlFlag() { return bmptc_no_radl_flag_; }
  bool                  getBmptcOneSubmeshPerFrameFlag() { return bmptc_one_submesh_per_frame_flag_; }
  bool                  getBmptcNoTemporalScalabilityFlag() { return bmptc_no_temporal_scalability_flag_; }
  uint8_t               getBmptcNumReservedConstraintBytes(){ return bmptc_num_reserved_constraint_bytes_;}
  std::vector<uint8_t>& getBmptcReservedConstraintByte()    { return bmptc_reserved_constraint_byte_;}

  void                  setBmptcOneMeshFrameOnlyFlag(bool value)      { bmptc_one_mesh_frame_only_flag_=value; }
  void                  setBmptcIntraFrameOnlyFlag(bool value)      { bmptc_intra_frames_only_flag_=value; }
  void                  setBmptcMotionVectorDerivationDisableFlag(bool value)         {  bmptc_motion_vector_derivation_disable_flag_=value; }
  void                  setBmptcNoCraBlaFlag(bool value) { bmptc_no_cra_bla_flag_ = value; }
  void                  setBmptcNoRaslFlag(bool value) { bmptc_no_rasl_flag_ = value; }
  void                  setBmptcNoRadlFlag(bool value) { bmptc_no_radl_flag_ = value; }
  void                  setBmptcOneSubmeshPerFrameFlag(bool value) { bmptc_one_submesh_per_frame_flag_ = value; }
  void                  setBmptcNoTemporalScalabilityFlag(bool value) { bmptc_no_temporal_scalability_flag_ = value; }
  void                  setBmptcNumReservedConstraintBytes(uint8_t value){ bmptc_num_reserved_constraint_bytes_=value; bmptc_reserved_constraint_byte_.resize(value);}
  void                  setBmptcReservedConstraintByte(uint8_t index, uint8_t value){ bmptc_reserved_constraint_byte_[index]=value;}
  
private:
  bool                 bmptc_one_mesh_frame_only_flag_;
  bool                 bmptc_intra_frames_only_flag_;
  bool                 bmptc_motion_vector_derivation_disable_flag_;
  bool                 bmptc_no_cra_bla_flag_;
  bool                 bmptc_no_rasl_flag_;
  bool                 bmptc_no_radl_flag_;
  bool                 bmptc_one_submesh_per_frame_flag_;
  bool                 bmptc_no_temporal_scalability_flag_;
  uint8_t              bmptc_num_reserved_constraint_bytes_;
  std::vector<uint8_t> bmptc_reserved_constraint_byte_;
};

//H.8.1.3.1.2  Basemesh Profile, tier, and level syntax
class BaseMeshProfileTierLevel {
public:
  BaseMeshProfileTierLevel():
  bmptl_tier_flag_(0),
  bmptl_profile_idc_(0),
  bmptl_reserved_zero_32bits_(0),
  bmptl_level_idc_(0),
  bmptl_num_sub_profiles_(0),
  bmptl_extended_sub_profile_flag_(0),
  bmptl_toolset_constraints_present_flag_(0) {

    bmptl_sub_layer_profile_present_flag_.resize(0);
    bmptl_sub_layer_level_present_flag_.resize(0);
    bmptl_sub_layer_tier_flag_.resize(0);
    bmptl_sub_layer_profile_idc_.resize(0);
    bmptl_sub_layer_level_idc_.resize(0);
    bmptl_sub_profile_idc_.resize(0);
  }
  ~BaseMeshProfileTierLevel(){}
  BaseMeshProfileTierLevel& operator=( const BaseMeshProfileTierLevel& ) = default;
  
  bool     getBmptlTierFlag() { return bmptl_tier_flag_; }
  uint8_t  getBmptlProfileIdc() { return bmptl_profile_idc_; }
  uint32_t getBmptlReservedZero32bits() { return bmptl_reserved_zero_32bits_; }
  uint8_t  getBmptlLevelIdc() { return bmptl_level_idc_; }
  uint8_t  getBmptlNumSubProfiles() { return bmptl_num_sub_profiles_; }
  bool     getBmptlExtendedSubProfileFlag() { return bmptl_extended_sub_profile_flag_; }
  bool     getBmptlToolsetConstraintsPresentFlag() { return bmptl_toolset_constraints_present_flag_; }
  std::vector<bool>&  getBmptlSubLayerProfilePresentFlag() { return bmptl_sub_layer_profile_present_flag_; }
  bool                getBmptlSubLayerProfilePresentFlag(size_t i) { 
    if (i >= bmptl_sub_layer_profile_present_flag_.size() ) bmptl_sub_layer_profile_present_flag_.resize(bmptl_sub_layer_profile_present_flag_.size() + 1);
    return bmptl_sub_layer_profile_present_flag_[i]; 
    }
  std::vector<bool>&  getBmptlSubLayerLevelPresentFlag() { return bmptl_sub_layer_level_present_flag_; }
  bool                getBmptlSubLayerLevelPresentFlag(size_t i) { 
    if (i >= bmptl_sub_layer_level_present_flag_.size() ) bmptl_sub_layer_level_present_flag_.resize(bmptl_sub_layer_level_present_flag_.size() + 1);
    return bmptl_sub_layer_level_present_flag_[i]; 
    }
  std::vector<bool>&  getBmptlSubLayerTierFlag() { return bmptl_sub_layer_tier_flag_; }
  bool                getBmptlSubLayerTierFlag(size_t i) { 
    if (i >= bmptl_sub_layer_tier_flag_.size() ) bmptl_sub_layer_tier_flag_.resize(bmptl_sub_layer_tier_flag_.size() + 1);
    return bmptl_sub_layer_tier_flag_[i]; 
    }
  std::vector<uint8_t>&  getBmptlSubLayerProfileIdc() { return bmptl_sub_layer_profile_idc_; }
  uint8_t                getBmptlSubLayerProfileIdc(size_t i) { 
    if (i >= bmptl_sub_layer_profile_idc_.size() ) bmptl_sub_layer_profile_idc_.resize(bmptl_sub_layer_profile_idc_.size() + 1);
    return bmptl_sub_layer_profile_idc_[i]; 
    }
  std::vector<uint8_t>&  getBmptlSubLayerLevelIdc() { return bmptl_sub_layer_level_idc_; }
  uint8_t                getBmptlSubLayerLevelIdc(size_t i) { 
    if (i >= bmptl_sub_layer_level_idc_.size() ) bmptl_sub_layer_level_idc_.resize(bmptl_sub_layer_level_idc_.size() + 1);
    return bmptl_sub_layer_level_idc_[i]; 
    }
  std::vector<uint32_t>&  getBmptlSubProfileIdc() { return bmptl_sub_profile_idc_; }
  uint32_t                getBmptlSubProfileIdc(size_t i) { return bmptl_sub_profile_idc_[i]; }
  BaseMeshProfileToolsetConstraintsInformation& getBmptlProfileToolsetConstraintsInformation() { return bmptl_profile_toolset_constraints_information; }
  const BaseMeshProfileToolsetConstraintsInformation& getBmptlProfileToolsetConstraintsInformation() const { return bmptl_profile_toolset_constraints_information; }

  void   setBmptlTierFlag(bool value) { bmptl_tier_flag_ = value; }
  void   setBmptlProfileIdc(uint8_t value) { bmptl_profile_idc_ = value; }
  void   setBmptlReservedZero32bits(uint32_t value) { bmptl_reserved_zero_32bits_ = value; }
  void   setBmptlLevelIdc(uint8_t value) { bmptl_level_idc_ = value; }
  void   setBmptlSubLayerProfilePresentFlag(std::vector<bool>& value) { bmptl_sub_layer_profile_present_flag_ = value; }
  void   setBmptlSubLayerProfilePresentFlag(bool value, size_t i) { 
    if (i >= bmptl_sub_layer_profile_present_flag_.size() ) bmptl_sub_layer_profile_present_flag_.resize(i + 1);
    bmptl_sub_layer_profile_present_flag_[i] = value; 
    }
  void   setBmptlSubLayerLevelPresentFlag(std::vector<bool>& value) { bmptl_sub_layer_level_present_flag_ = value; }
  void   setBmptlSubLayerLevelPresentFlag(bool value, size_t i) { 
    if (i >= bmptl_sub_layer_level_present_flag_.size() ) bmptl_sub_layer_level_present_flag_.resize(i + 1);
    bmptl_sub_layer_level_present_flag_[i] = value; 
    }
  void   setBmptlSubLayerTierFlag(std::vector<bool>& value) { bmptl_sub_layer_tier_flag_ = value; }
  void   setBmptlSubLayerTierFlag(bool value, size_t i) { 
    if (i >= bmptl_sub_layer_tier_flag_.size() ) bmptl_sub_layer_tier_flag_.resize(i + 1);
    bmptl_sub_layer_tier_flag_[i] = value; 
    }
  void   setBmptlSubLayerProfileIdc(std::vector<uint8_t>& value) { bmptl_sub_layer_profile_idc_ = value; }
  void   setBmptlSubLayerProfileIdc(uint8_t value, size_t i) { 
    if (i >= bmptl_sub_layer_profile_idc_.size() ) bmptl_sub_layer_profile_idc_.resize(i + 1);
    bmptl_sub_layer_profile_idc_[i] = value; 
    }
  void   setBmptlSubLayerLevelIdc(std::vector<uint8_t>& value) { bmptl_sub_layer_level_idc_ = value; }
  void   setBmptlSubLayerLevelIdc(uint8_t value, size_t i) { 
    if (i >= bmptl_sub_layer_level_idc_.size() ) bmptl_sub_layer_level_idc_.resize(i + 1);
    bmptl_sub_layer_level_idc_[i] = value; 
    }
  void   setBmptlNumSubProfiles(uint8_t value) { bmptl_num_sub_profiles_ = value; }
  void   setBmptlExtendedSubProfileFlag(bool value) { bmptl_extended_sub_profile_flag_ = value; }
  void   setBmptlToolsetConstraintsPresentFlag(bool value) { bmptl_toolset_constraints_present_flag_ = value; }
  void   setBmptlSubProfileIdc(std::vector<uint32_t>& value) { bmptl_sub_profile_idc_ = value; }
  void   setBmptlSubProfileIdc(uint32_t value, size_t i) { bmptl_sub_profile_idc_[i] = value; }
  
  bool     bmptl_tier_flag_;
  uint8_t  bmptl_profile_idc_;
  uint32_t bmptl_reserved_zero_32bits_;
  uint8_t  bmptl_level_idc_;
  uint8_t  bmptl_num_sub_profiles_;
  bool     bmptl_extended_sub_profile_flag_;
  bool     bmptl_toolset_constraints_present_flag_;
  std::vector<bool> bmptl_sub_layer_profile_present_flag_;
  std::vector<bool> bmptl_sub_layer_level_present_flag_;
  std::vector<bool> bmptl_sub_layer_tier_flag_;
  std::vector<uint8_t> bmptl_sub_layer_profile_idc_;
  std::vector<uint8_t> bmptl_sub_layer_level_idc_;
  std::vector<uint32_t> bmptl_sub_profile_idc_;
  BaseMeshProfileToolsetConstraintsInformation
    bmptl_profile_toolset_constraints_information;
};

//H.8.1.3.1.1  General basemesh sequence parameter set RBSP syntax
class BaseMeshSequenceParameterSetRbsp {
public:
  BaseMeshSequenceParameterSetRbsp() :
  bmsps_sequence_parameter_set_id_( 0 ),
  bmsps_max_sub_layers_minus1_( 0 ),
  bmsps_temporal_id_nesting_flag_( 1 ),
  bmsps_intra_mesh_codec_id_( 0 ),
  bmsps_inter_mesh_codec_id_( 0 ),
  bmsps_geometry_3d_bit_depth_minus1_( 9 ),
  bmsps_mesh_attribute_count_( 1 ),
  bmsps_log2_max_mesh_frame_order_cnt_lsb_minus4_( 6 ),
  bmsps_long_term_ref_mesh_frames_flag_( 0 ),
  bmsps_num_ref_mesh_frame_lists_in_bmsps_( 1 ),
  bmsps_codec_specific_parameters_present_flag_( 0 ),
  bmsps_mesh_codec_prefix_length_minus1_( 0 ),
  bmsps_motion_codec_specific_parameters_present_flag_(0),
  bmsps_motion_codec_prefix_length_minus1_(0),
  bmsps_extension_present_flag_( 0 ),
  bmsps_extension_count_( 0 ),
  bmsps_extensions_length_minus1_( 0 ){
    bmsps_mesh_attribute_type_id_.resize(bmsps_mesh_attribute_count_, 0);
    bmsps_mesh_attribute_index_.resize(bmsps_mesh_attribute_count_,0);
    bmsps_mesh_attribute_dimension_minus1_.resize (bmsps_mesh_attribute_count_,0);
    bmsps_attribute_bit_depth_minus1_.resize(bmsps_mesh_attribute_count_, 9);
    bmsps_attribute_msb_align_flag_.resize(bmsps_mesh_attribute_count_, 0);
    bmsps_max_dec_mesh_frame_buffering_minus1_.resize(bmsps_max_sub_layers_minus1_+1, 0);
    bmsps_max_num_reorder_frames_.resize(bmsps_max_sub_layers_minus1_+1, 0);
    bmsps_max_latency_increase_plus1_.resize(bmsps_max_sub_layers_minus1_+1, 1);
    bmesh_ref_list_struct_.clear(); //(bmsps_num_ref_mesh_frame_lists_in_bmsps_);
  }
  ~BaseMeshSequenceParameterSetRbsp() {
    bmsps_mesh_attribute_type_id_.clear();
    bmsps_mesh_attribute_index_.clear();
    bmsps_attribute_bit_depth_minus1_.clear();
    bmsps_attribute_msb_align_flag_.clear();
    bmesh_ref_list_struct_.clear();
  }
  
  BaseMeshSequenceParameterSetRbsp& operator=( const BaseMeshSequenceParameterSetRbsp& ) = default;
  uint8_t   getBmspsSequenceParameterSetId()      { return bmsps_sequence_parameter_set_id_; }
  uint8_t   getBmspsMaxSubLayersMinus1()          { return bmsps_max_sub_layers_minus1_; }
  bool      getBmspsTemporalIdNestingFlag()       { return bmsps_temporal_id_nesting_flag_; }
  uint8_t   getBmspsIntraMeshCodecId()            { return bmsps_intra_mesh_codec_id_; }
  uint8_t   getBmspsInterMeshCodecId()            { return bmsps_inter_mesh_codec_id_; }
  uint8_t   getBmspsGeometry3dBitDepthMinus1()    { return bmsps_geometry_3d_bit_depth_minus1_; }
  uint8_t   getBmspsGeometryMsbAlignFlag() { return bmsps_geometry_msb_align_flag_; }
  //uint32_t  getBmspsIntraMeshPostReindexMethod()  { return bmsps_intra_mesh_post_reindex_method_; }
  uint8_t   getBmspsMeshAttributeCount()          { return bmsps_mesh_attribute_count_; }
  std::vector<uint8_t>&  getBmspsMeshAttributeTypeId()                  { return bmsps_mesh_attribute_type_id_; }
  std::vector<uint8_t>&  getBmspsAttributeBitDepthMinus1()              { return bmsps_attribute_bit_depth_minus1_; }
  std::vector<bool>&     getBmspsAttributeMsbAlignFlag()                { return bmsps_attribute_msb_align_flag_; }
  std::vector<uint8_t>&  getBmspsAttributeDimensionMinus1()             { return bmsps_mesh_attribute_dimension_minus1_;}
  uint8_t                getBmspsMeshAttributeTypeId(uint8_t index)     { return bmsps_mesh_attribute_type_id_[index]; }
  uint8_t                getBmspsAttributeBitDepthMinus1(uint8_t index) { return bmsps_attribute_bit_depth_minus1_[index]; }
  bool                   getBmspsAttributeMsbAlignFlag(uint8_t index)   { return bmsps_attribute_msb_align_flag_[index]; }
  uint8_t                getBmspsAttributeDimensionMinus1(uint8_t index) { return bmsps_mesh_attribute_dimension_minus1_[index];}
  uint32_t               getBmspsLog2MaxMeshFrameOrderCntLsbMinus4()    { return bmsps_log2_max_mesh_frame_order_cnt_lsb_minus4_; }
  bool                   getBmspsSubLayerOrderingInfoPresentFlag()      { return bmsps_sub_layer_ordering_info_present_flag_; }
  uint32_t               getBmspsMaxDecMeshFrameBufferingMinus1(uint8_t index)       { return bmsps_max_dec_mesh_frame_buffering_minus1_[index]; }
  uint32_t               getBmspsMaxNumReorderFrames(uint8_t index)                  { return bmsps_max_num_reorder_frames_[index]; }
  uint32_t               getBmspsMaxLatencyIncreasePlus1(uint8_t index)              { return bmsps_max_latency_increase_plus1_[index]; }
  uint8_t                getBmspsLongTermRefMeshFramesFlag()            { return bmsps_long_term_ref_mesh_frames_flag_; }
  uint32_t               getBmspsNumRefMeshFrameListsInBmsps()          { return bmsps_num_ref_mesh_frame_lists_in_bmsps_; }
  bool                   getBmspsExtensionPresentFlag()                 { return bmsps_extension_present_flag_; }
  bool                   getBmspsCodecSpecificParametersPresentFlag()   { return bmsps_codec_specific_parameters_present_flag_; }
  uint32_t               getBmspsMeshCodecPrefixLengthMinus1()          { return bmsps_mesh_codec_prefix_length_minus1_; }
  const std::vector<uint8_t>&  getBmspsMeshCodecPrefixData()       const{ return bmsps_mesh_codec_prefix_data_; }
  bool                   getBmspsMotionCodecSpecificParametersPresentFlag() { return bmsps_motion_codec_specific_parameters_present_flag_; }
  uint32_t               getBmspsMotionCodecPrefixLengthMinus1() { return bmsps_motion_codec_prefix_length_minus1_; }
  const std::vector<uint8_t>& getBmspsMotionCodecPrefixData()       const { return bmsps_motion_codec_prefix_data_; }
  uint8_t                getBmspsExtensionCount()                       { return bmsps_extension_count_; }
  uint8_t                getBmspsExtensionType(int index )              { return bmsps_extension_type_[index]; }
  uint8_t                getBmspsExtensionLength(int index )            { return bmsps_extension_length_[index]; }
  uint32_t               getBmspsExtensionsLengthMinus1()               { return bmsps_extensions_length_minus1_; }
  BaseMeshProfileTierLevel&  getBmeshProfileTierLevel()                     { return bmesh_profile_tier_level_; }

  std::vector<BaseMeshRefListStruct>&   getBmeshRefListStruct()             { return bmesh_ref_list_struct_;}
  BaseMeshRefListStruct&                getBmeshRefListStruct(uint8_t index){ return bmesh_ref_list_struct_[index];}
  //uint8_t                           getBmspsInterMeshLog2MotionGroupSize() { return bmsps_inter_mesh_log2_motion_group_size_; }
  uint8_t                           getBmspsInterMeshMaxNumNeighboursMinus1()          { return bmsps_inter_mesh_max_num_neighbours_minus1_; }
  //uint8_t                           getBmspsMaxNumMotionVectorPredictor()    { return maxNumMotionVectorPredictor_; }
  const uint8_t   getBmspsSequenceParameterSetId()      const { return bmsps_sequence_parameter_set_id_; }
  const uint8_t   getBmspsMaxSubLayersMinus1()          const { return bmsps_max_sub_layers_minus1_; }
  const bool      getBmspsTemporalIdNestingFlag()       const { return bmsps_temporal_id_nesting_flag_; }
  const uint8_t   getBmspsIntraMeshCodecId()            const { return bmsps_intra_mesh_codec_id_; }
  const uint8_t   getBmspsInterMeshCodecId()            const { return bmsps_inter_mesh_codec_id_; }
  const uint8_t   getBmspsGeometry3dBitDepthMinus1()    const { return bmsps_geometry_3d_bit_depth_minus1_; }
  const uint8_t   getBmspsGeometryMsbAlignFlag()    const { return bmsps_geometry_msb_align_flag_; }
  //const uint32_t  getBmspsIntraMeshPostReindexMethod()  const { return bmsps_intra_mesh_post_reindex_method_; }
  const uint8_t   getBmspsMeshAttributeCount()          const { return bmsps_mesh_attribute_count_; }
  const std::vector<uint8_t>&  getBmspsMeshAttributeTypeId()                  const{ return bmsps_mesh_attribute_type_id_; }
  const std::vector<uint8_t>&  getBmspsMeshAttributeIndex()                   const{  return bmsps_mesh_attribute_index_;}
  const std::vector<uint8_t>&  getBmspsAttributeBitDepthMinus1()              const{ return bmsps_attribute_bit_depth_minus1_; }
  const std::vector<uint8_t>&  getBmspsAttributeDimensionMinus1()             const{ return bmsps_mesh_attribute_dimension_minus1_;}
  const std::vector<bool>&     getBmspsAttributeMsbAlignFlag()                const{ return bmsps_attribute_msb_align_flag_; }
  const uint8_t                getBmspsMeshAttributeTypeId(uint8_t index)     const{ return bmsps_mesh_attribute_type_id_[index]; }
  const uint8_t                getBmspsMeshAttributeIndex(uint8_t index)      const{ return bmsps_mesh_attribute_index_[index];}
  const uint8_t                getBmspsMeshAttributeDimensionMinus1(uint8_t index) const {return bmsps_mesh_attribute_dimension_minus1_[index];}
  const uint8_t                getBmspsAttributeBitDepthMinus1(uint8_t index) const{ return bmsps_attribute_bit_depth_minus1_[index]; }
  const bool                   getBmspsAttributeMsbAlignFlag(uint8_t index)   const{ return bmsps_attribute_msb_align_flag_[index]; }
  const uint32_t               getBmspsLog2MaxMeshFrameOrderCntLsbMinus4()    const{ return bmsps_log2_max_mesh_frame_order_cnt_lsb_minus4_; }
  const bool                   getBmspsSubLayerOrderingInfoPresentFlag()      const{ return bmsps_sub_layer_ordering_info_present_flag_; }
  const uint32_t               getBmspsMaxDecMeshFrameBufferingMinus1(uint8_t index)       const{ return bmsps_max_dec_mesh_frame_buffering_minus1_[index]; }
  const uint32_t               getBmspsMaxNumReorderFrames(uint8_t index)                  const{ return bmsps_max_num_reorder_frames_[index]; }
  const uint32_t               getBmspsMaxLatencyIncreasePlus1(uint8_t index)              const{ return bmsps_max_latency_increase_plus1_[index]; }
  const uint8_t                getBmspsLongTermRefMeshFramesFlag()            const{ return bmsps_long_term_ref_mesh_frames_flag_; }
  const uint32_t               getBmspsNumRefMeshFrameListsInBmsps()          const{ return bmsps_num_ref_mesh_frame_lists_in_bmsps_; }
  const bool                   getBmspsVuiParametersPresentFlag()                 const { return bmsps_vui_parameters_present_flag_; }
  BasemeshVuiParameters& getVuiParameters() { return bmesh_vui_; }
  const BasemeshVuiParameters& getVuiParameters()                     const { return bmesh_vui_; }
  const bool                   getBmspsExtensionPresentFlag()                 const { return bmsps_extension_present_flag_; }
  const uint8_t                getBmspsExtensionCount()                       const{ return bmsps_extension_count_; }
  const uint8_t                getBmspsExtensionType(int index )              const{ return bmsps_extension_type_[index]; }
  const uint8_t                getBmspsExtensionLength(int index )            const{ return bmsps_extension_length_[index]; }
  const uint32_t               getBmspsExtensionsLengthMinus1()               const{ return bmsps_extensions_length_minus1_; }
  const BaseMeshProfileTierLevel&  getBmeshProfileTierLevel()                     const{ return bmesh_profile_tier_level_; }
  const std::vector<BaseMeshRefListStruct>&   getBmeshRefListStruct()             const{ return bmesh_ref_list_struct_;}
  const BaseMeshRefListStruct&                getBmeshRefListStruct(uint8_t index)const{ return bmesh_ref_list_struct_[index];}
  //const uint8_t                           getBmspsInterMeshLog2MotionGroupSize() const { return bmsps_inter_mesh_log2_motion_group_size_; }
  const uint8_t                           getBmspsInterMeshMaxNumNeighboursMinus1()          const{ return bmsps_inter_mesh_max_num_neighbours_minus1_; }
  //const uint8_t                           getMaxNumMotionVectorPredictor()    const{ return maxNumMotionVectorPredictor_; }

  void   setBmspsSequenceParameterSetId(uint8_t value) { bmsps_sequence_parameter_set_id_ = value; }
  void   setBmspsMaxSubLayersMinus1(uint8_t value) { 
    bmsps_max_sub_layers_minus1_ = value; 
    bmsps_max_dec_mesh_frame_buffering_minus1_.resize(bmsps_max_sub_layers_minus1_+1, 0);
    bmsps_max_num_reorder_frames_.resize(bmsps_max_sub_layers_minus1_+1, 0);
    bmsps_max_latency_increase_plus1_.resize(bmsps_max_sub_layers_minus1_+1, 1);
  }
  void   setBmspsTemporalIdNestingFlag(bool value) { bmsps_temporal_id_nesting_flag_ = value; }
  void   setBmspsIntraMeshCodecId(uint8_t value) { bmsps_intra_mesh_codec_id_ = value; }
  void   setBmspsInterMeshCodecId(uint8_t value) { bmsps_inter_mesh_codec_id_ = value; }
  void   setBmspsGeometry3dBitDepthMinus1(uint8_t value) { bmsps_geometry_3d_bit_depth_minus1_ = value; }
  void   setBmspsGeometryMsbAlignFlag(uint8_t value) { bmsps_geometry_msb_align_flag_ = value; }
  //void   setBmspsIntraMeshPostReindexMethod(uint32_t value) { bmsps_intra_mesh_post_reindex_method_ = value; }
  void   setBmspsMeshAttributeCount(uint8_t value) {
    bmsps_mesh_attribute_count_ = value;
    if(bmsps_mesh_attribute_count_>1){
      bmsps_mesh_attribute_type_id_.resize    (bmsps_mesh_attribute_count_, 0);
      bmsps_mesh_attribute_index_.resize      (bmsps_mesh_attribute_count_, 0);
      bmsps_mesh_attribute_dimension_minus1_.resize (bmsps_mesh_attribute_count_,0);
      bmsps_attribute_bit_depth_minus1_.resize(bmsps_mesh_attribute_count_, 9);
      bmsps_attribute_msb_align_flag_.resize  (bmsps_mesh_attribute_count_, 0);
    }
  }
  
  void   setBmspsMeshAttributeTypeId(std::vector<uint8_t>& value) { bmsps_mesh_attribute_type_id_ = value; }
  void   setBmspsAttributeBitDepthMinus1(std::vector<uint8_t>& value) { bmsps_attribute_bit_depth_minus1_ = value; }
  void   setBmspsAttributeMsbAlignFlag(std::vector<bool>& value) { bmsps_attribute_msb_align_flag_ = value; }
  void   setBmspsMeshAttributeDimensionMinus1(std::vector<uint8_t>& value ) { bmsps_mesh_attribute_dimension_minus1_ = value;}
  
  void   setBmspsMeshAttributeTypeId(uint8_t value, uint8_t index) { bmsps_mesh_attribute_type_id_[index] = value; }
  void   setBmspsMeshAttributeIndex(uint8_t value, uint8_t index) { bmsps_mesh_attribute_index_[index] = value;}
  void   setBmspsMeshAttributeDimensionMinus1(uint8_t value, uint8_t index) { bmsps_mesh_attribute_dimension_minus1_[index] = value;}
  void   setBmspsAttributeBitDepthMinus1(uint8_t value, uint8_t index) { bmsps_attribute_bit_depth_minus1_[index] = value; }
  void   setBmspsAttributeMsbAlignFlag(uint8_t value, uint8_t index) { bmsps_attribute_msb_align_flag_[index] = value; }
  
  void   setBmspsLog2MaxMeshFrameOrderCntLsbMinus4(uint32_t value) { bmsps_log2_max_mesh_frame_order_cnt_lsb_minus4_ = value; }
  void   setBmspsSubLayerOrderingInfoPresentFlag(bool value) { bmsps_sub_layer_ordering_info_present_flag_ = value; }
  void   setBmspsMaxDecMeshFrameBufferingMinus1(uint32_t value, uint8_t index) { bmsps_max_dec_mesh_frame_buffering_minus1_[index] = value; }
  void   setBmspsMaxNumReorderFrames(uint32_t value, uint8_t index) { bmsps_max_num_reorder_frames_[index] = value; }
  void   setBmspsMaxLatencyIncreasePlus1(uint32_t value, uint8_t index) { bmsps_max_latency_increase_plus1_[index] = value; }
  void   setBmspsLongTermRefMeshFramesFlag(uint8_t value) { bmsps_long_term_ref_mesh_frames_flag_ = value; }
  void   setBmspsCodecSpecificParametersPresentFlag(bool value) { bmsps_codec_specific_parameters_present_flag_ = value; }
  void   setBmspsMeshCodecPrefixLengthMinus1(uint32_t value) { bmsps_mesh_codec_prefix_length_minus1_ = value; }
  void   setBmspsMeshCodecPrefixData(std::vector<uint8_t>& value) { bmsps_mesh_codec_prefix_data_ = value; }
  void   setBmspsMotionCodecSpecificParametersPresentFlag(bool value) { bmsps_motion_codec_specific_parameters_present_flag_ = value; }
  void   setBmspsMotionCodecPrefixLengthMinus1(uint32_t value) { bmsps_motion_codec_prefix_length_minus1_ = value; }
  void   setBmspsMotionCodecPrefixData(std::vector<uint8_t>& value) { bmsps_motion_codec_prefix_data_ = value; }
  void   setBmspsNumRefMeshFrameListsInBmsps(uint32_t value) { bmsps_num_ref_mesh_frame_lists_in_bmsps_ = value; bmesh_ref_list_struct_.resize(bmsps_num_ref_mesh_frame_lists_in_bmsps_); }
  void   setBmspsVuiParametersPresentFlag(bool value) { bmsps_vui_parameters_present_flag_ = value; }
  void   setVuiParameters(BasemeshVuiParameters& value) { bmesh_vui_= value; }
  void   setBmspsExtensionPresentFlag(bool value) { bmsps_extension_present_flag_ = value; }
  void   setBmspsExtensionCount(uint8_t value) {
    bmsps_extension_count_ = value;
    bmsps_extension_type_.resize(bmsps_extension_count_, 0);
    bmsps_extension_length_.resize(bmsps_extension_count_, (uint16_t)0);
    bmsps_extension_dataByte_.resize(bmsps_extension_count_);
  }
  void   setBmspsExtensionType(int index, uint8_t value) { bmsps_extension_type_[index] = value; }
  void   setBmspsExtensionLength(int index, uint8_t value) { bmsps_extension_length_[index] = value; }
  void   setBmspsExtensionsLengthMinus1(uint32_t value) { bmsps_extensions_length_minus1_ = value; }
  void   setBmeshProfileTierLevel(BaseMeshProfileTierLevel&value){ bmesh_profile_tier_level_=value; }
  void   setBmeshRefListStruct(std::vector<BaseMeshRefListStruct>& value){ bmesh_ref_list_struct_=value;}
  void   addBmeshRefListStruct(BaseMeshRefListStruct value ) { bmesh_ref_list_struct_.push_back( value ); }
  //void   setBmspsInterMeshLog2MotionGroupSize(uint8_t value) { bmsps_inter_mesh_log2_motion_group_size_ = value; }
  void   setBmspsInterMeshMaxNumNeighboursMinus1(uint32_t value) { bmsps_inter_mesh_max_num_neighbours_minus1_ = value; }
  //void   setMaxNumMotionVectorPredictor(uint32_t value) { maxNumMotionVectorPredictor_ = value; }

private:
  uint8_t                           bmsps_sequence_parameter_set_id_=0;
  uint8_t                           bmsps_max_sub_layers_minus1_=0;
  bool                              bmsps_temporal_id_nesting_flag_=1;
  BaseMeshProfileTierLevel          bmesh_profile_tier_level_;
  uint8_t                           bmsps_intra_mesh_codec_id_=0;
  uint8_t                           bmsps_inter_mesh_codec_id_=0;
  uint8_t                           bmsps_geometry_3d_bit_depth_minus1_=0;
  uint8_t                           bmsps_geometry_msb_align_flag_ = 0;
  //uint32_t                          bmsps_intra_mesh_post_reindex_method_=0; -> moved to annex I
  uint8_t                           bmsps_mesh_attribute_count_=0;
  std::vector<uint8_t>              bmsps_mesh_attribute_index_;
  std::vector<uint8_t>              bmsps_mesh_attribute_type_id_;
  std::vector<uint8_t>              bmsps_mesh_attribute_dimension_minus1_;
  std::vector<uint8_t>              bmsps_attribute_bit_depth_minus1_;
  std::vector<bool>                 bmsps_attribute_msb_align_flag_;
  uint32_t                          bmsps_log2_max_mesh_frame_order_cnt_lsb_minus4_=6;
  bool                              bmsps_sub_layer_ordering_info_present_flag_ =0;
  std::vector<uint32_t>             bmsps_max_dec_mesh_frame_buffering_minus1_;
  std::vector<uint32_t>             bmsps_max_num_reorder_frames_;
  std::vector<uint32_t>             bmsps_max_latency_increase_plus1_;
  uint8_t                           bmsps_long_term_ref_mesh_frames_flag_=0;
  uint32_t                          bmsps_num_ref_mesh_frame_lists_in_bmsps_=1;
  std::vector<BaseMeshRefListStruct>    bmesh_ref_list_struct_;
  uint8_t                           bmsps_inter_mesh_max_num_neighbours_minus1_=0;
  bool                              bmsps_codec_specific_parameters_present_flag_=0;
  uint32_t                          bmsps_mesh_codec_prefix_length_minus1_=0;
  std::vector<uint8_t>              bmsps_mesh_codec_prefix_data_;
  bool                              bmsps_motion_codec_specific_parameters_present_flag_ = 0;
  uint32_t                          bmsps_motion_codec_prefix_length_minus1_ = 0;
  std::vector<uint8_t>              bmsps_motion_codec_prefix_data_;
  bool                              bmsps_vui_parameters_present_flag_=0; 
  BasemeshVuiParameters             bmesh_vui_;
  bool                              bmsps_extension_present_flag_ =0;
  uint8_t                           bmsps_extension_count_;
  uint32_t                          bmsps_extensions_length_minus1_;
  std::vector<uint8_t>              bmsps_extension_type_;
  std::vector<uint16_t>             bmsps_extension_length_;
  std::vector<std::vector<uint8_t>> bmsps_extension_dataByte_;
  
  //uint8_t                           bmsps_inter_mesh_log2_motion_group_size_; -> moved to annex L
  //uint8_t                           maxNumMotionVectorPredictor_; -> moved to annex L

  //bmesh_profile_tier_level( )
//  rbsp_trailing_bits( )

};

};  // namespace vmesh
