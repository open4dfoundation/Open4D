/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
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

#include "baseMeshRefListStruct.hpp"

namespace basemesh {

// Base mesh tile header syntax
class BaseMeshSubmeshHeader {
public:
  BaseMeshSubmeshHeader() :
  smh_no_output_of_prior_submesh_frames_flag_(0),
  smh_submesh_frame_parameter_set_id_(0),
  smh_id_(0),
  smh_type_(I_BASEMESH),
  smh_mesh_output_flag_(0),
  smh_basemesh_frm_order_cnt_lsb_(0),
  smh_ref_basemesh_frame_list_msps_flag_(1),
  smh_ref_mesh_frame_list_idx_(0),
  msh_num_ref_idx_active_override_flag_(0),
  msh_num_ref_idx_active_minus1_(0)
  {
    _frameIndex=0;
    _smhBasemeshFrmOrderCntMsb=0;
    _smhBasemeshFrmOrderCntVal=0;
    msh_additional_mfoc_lsb_present_flag_.clear();
    msh_additional_mfoc_lsb_val_.clear();
  }
  BaseMeshSubmeshHeader(int32_t frameIndex, int32_t refFrameIndex, BaseMeshType type) {
    _frameIndex=frameIndex;
    smh_type_=type;
  }
  BaseMeshSubmeshHeader(BaseMeshType type) { _frameIndex=0; smh_type_=type;}
  ~BaseMeshSubmeshHeader() {}
  BaseMeshSubmeshHeader& operator=( const BaseMeshSubmeshHeader& ) = default;

  int32_t     getFrameIndex()             { return _frameIndex; }
  void        setFrameIndex(uint32_t value){ _frameIndex = value; }
  int32_t     getSubmeshIndex()             { return _submeshIdx; }
  void        setSubmeshIndex(uint32_t value){ _submeshIdx = value; }

  bool                   getSmhNoOutputOfPriorSubmeshFramesFlag()         { return smh_no_output_of_prior_submesh_frames_flag_; }
  size_t                 getSmhSubmeshFrameParameterSetId()               { return smh_submesh_frame_parameter_set_id_; }
  size_t                 getSmhId()                                       { return smh_id_; }
  BaseMeshType           getSmhType()                                     { return smh_type_; }
  bool                   getSmhMeshOutputFlag()                           { return smh_mesh_output_flag_; }
  size_t                 getSmhBasemeshFrmOrderCntMsb()                   { return _smhBasemeshFrmOrderCntMsb; }
  size_t                 getSmhBasemeshFrmOrderCntVal()                   { return _smhBasemeshFrmOrderCntVal; }
  std::vector<int32_t>&   getSmhBasemeshReferenceList()                   { return _smhBasemeshReferenceList;}
  size_t                 getSmhBasemeshFrmOrderCntLsb()                   { return smh_basemesh_frm_order_cnt_lsb_; }
  bool                   getSmhRefBasemeshFrameListMspsFlag()             { return smh_ref_basemesh_frame_list_msps_flag_; }
  size_t                 getSmhRefMeshFrameListIdx()                      { return smh_ref_mesh_frame_list_idx_; }
  std::vector<bool>&     getSmhAdditionalMfocLsbPresentFlag()             { return msh_additional_mfoc_lsb_present_flag_; }
  std::vector<size_t>&   getSmhAdditionalMfocLsbVal()                     { return msh_additional_mfoc_lsb_val_; }
  bool                   getSmhAdditionalMfocLsbPresentFlag(size_t index) { return msh_additional_mfoc_lsb_present_flag_[index]; }
  size_t                 getSmhAdditionalMfocLsbVal(size_t index)         { return msh_additional_mfoc_lsb_val_[index]; }
  bool                   getSmhNumRefIdxActiveOverrideFlag()              { return msh_num_ref_idx_active_override_flag_; }
  size_t                 getSmhNumRefIdxActiveMinus1()                    { return msh_num_ref_idx_active_minus1_; }

  const bool                   getSmhNoOutputOfPriorSubmeshFramesFlag()         const { return smh_no_output_of_prior_submesh_frames_flag_; }
  const size_t                 getSmhSubmeshFrameParameterSetId()               const { return smh_submesh_frame_parameter_set_id_; }
  const size_t                 getSmhId()                                       const { return smh_id_; }
  const BaseMeshType           getSmhType()                                     const { return smh_type_; }
  const bool                   getSmhMeshOutputFlag()                           const { return smh_mesh_output_flag_; }
  const size_t                 getSmhBasemeshFrmOrderCntMsb()                   const {return _smhBasemeshFrmOrderCntMsb; }
  const size_t                 getSmhBasemeshFrmOrderCntVal()                   const {return _smhBasemeshFrmOrderCntVal; }
  const std::vector<int32_t>&  getSmhBasemeshReferenceList()                    const {return _smhBasemeshReferenceList;}
  const size_t                 getSmhBasemeshFrmOrderCntLsb()                   const { return smh_basemesh_frm_order_cnt_lsb_; }
  const bool                   getSmhRefBasemeshFrameListMspsFlag()             const { return smh_ref_basemesh_frame_list_msps_flag_; }
  const size_t                 getSmhRefMeshFrameListIdx()                      const { return smh_ref_mesh_frame_list_idx_; }
  const std::vector<bool>&     getMshAdditionalMfocLsbPresentFlag()             const { return msh_additional_mfoc_lsb_present_flag_; }
  const std::vector<size_t>&   getMshAdditionalMfocLsbVal()                     const { return msh_additional_mfoc_lsb_val_; }
  const bool                   getMshAdditionalMfocLsbPresentFlag(size_t index) const { return msh_additional_mfoc_lsb_present_flag_[index]; }
  const size_t                 getMshAdditionalMfocLsbVal(size_t index)         const { return msh_additional_mfoc_lsb_val_[index]; }
  const bool                   getMshNumRefIdxActiveOverrideFlag()              const { return msh_num_ref_idx_active_override_flag_; }
  const size_t                 getMshNumRefIdxActiveMinus1()                    const { return msh_num_ref_idx_active_minus1_; }
  
  void             setSmhNoOutputOfPriorSubmeshFramesFlag(bool value) { smh_no_output_of_prior_submesh_frames_flag_ = value; }
  void             setSmhSubmeshFrameParameterSetId(size_t value) { smh_submesh_frame_parameter_set_id_ = value; }
  void             setSmhId(size_t value)                         { smh_id_ = value; }
  void             setSmhType(BaseMeshType value)               { smh_type_ = value; }
  void             setSmhMeshOutputFlag(bool value)               { smh_mesh_output_flag_ = value; }
  void             setSmhBasemeshFrmOrderCntMsb(size_t value){ _smhBasemeshFrmOrderCntMsb=value; }
  void             setSmhBasemeshFrmOrderCntVal(size_t value){ _smhBasemeshFrmOrderCntVal=value; }
  void             setSmhBasemeshFrmOrderCntLsb(size_t value)     { smh_basemesh_frm_order_cnt_lsb_ = value; }
  void             setSmhRefBasemeshFrameListMspsFlag(bool value) { smh_ref_basemesh_frame_list_msps_flag_ = value; }
  void             setSmhRefMeshFrameListIdx(size_t value)        { smh_ref_mesh_frame_list_idx_ = value; }
  void             setSmhAdditionalMfocLsbPresentFlag(std::vector<bool>& value) { msh_additional_mfoc_lsb_present_flag_ = value; }
  void             setSmhAdditionalMfocLsbVal(std::vector<size_t>& value)       { msh_additional_mfoc_lsb_val_ = value; }
  void             setSmhAdditionalMfocLsbPresentFlag(bool value, size_t index) { msh_additional_mfoc_lsb_present_flag_[index] = value; }
  void             setSmhAdditionalMfocLsbVal(size_t value, size_t index)       { msh_additional_mfoc_lsb_val_[index] = value; }
  void             setSmhNumRefIdxActiveOverrideFlag(bool value)                { msh_num_ref_idx_active_override_flag_ = value; }
  void             setSmhNumRefIdxActiveMinus1(size_t value)                    { msh_num_ref_idx_active_minus1_ = value; }
  
  const BaseMeshRefListStruct& getSmhRefListStruct() const { return smh_ref_list_struct_; }
  BaseMeshRefListStruct&                getSmhRefListStruct(){ return smh_ref_list_struct_; }
  void   setSmhRefListStruct(BaseMeshRefListStruct& value)   { smh_ref_list_struct_=value; }
  
  bool             getDecoded(){return decoded_;}
  void             setDecoded(bool value){decoded_=value;}
private:
  int32_t             _frameIndex=0;
  int32_t             _submeshIdx=0;
  bool                smh_no_output_of_prior_submesh_frames_flag_;
  size_t              smh_submesh_frame_parameter_set_id_;
  size_t              smh_id_;
  BaseMeshType        smh_type_;
  bool                smh_mesh_output_flag_;
  size_t              smh_basemesh_frm_order_cnt_lsb_;
  bool                smh_ref_basemesh_frame_list_msps_flag_;
  BaseMeshRefListStruct smh_ref_list_struct_;
  size_t              smh_ref_mesh_frame_list_idx_;
  std::vector<bool>   msh_additional_mfoc_lsb_present_flag_;
  std::vector<size_t> msh_additional_mfoc_lsb_val_;
  bool                msh_num_ref_idx_active_override_flag_;
  size_t              msh_num_ref_idx_active_minus1_;
  size_t              _smhBasemeshFrmOrderCntMsb;
  size_t              _smhBasemeshFrmOrderCntVal;
  std::vector<int32_t>_smhBasemeshReferenceList;
  
  //decoder
  bool             decoded_;
};

};  // namespace vmesh
