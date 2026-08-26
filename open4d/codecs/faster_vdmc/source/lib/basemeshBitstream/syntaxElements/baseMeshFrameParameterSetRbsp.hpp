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

#include "baseMeshSubmeshInformation.hpp"

namespace basemesh {

// 8.3.6.2 BaseMesh frame parameter set Rbsp syntax
// 8.3.6.2.1 General baseMesh frame parameter set Rbsp syntax
class BaseMeshFrameParameterSetRbsp {
public:
  BaseMeshFrameParameterSetRbsp() :
  bfps_mesh_sequence_parameter_set_id_(0),
  bfps_mesh_frame_parameter_set_id_(0),
  bfps_output_flag_present_flag_(1),
  bfps_num_ref_idx_default_active_minus1_(0),
  bfps_additional_lt_mfoc_lsb_len_(0),
  bfps_extension_present_flag_(0) {}
  ~BaseMeshFrameParameterSetRbsp() {}
  
  BaseMeshFrameParameterSetRbsp& operator=( const BaseMeshFrameParameterSetRbsp& ) = default;

  uint8_t   getBfpsMeshSequenceParameterSetId()   { return bfps_mesh_sequence_parameter_set_id_; }
  uint8_t   getBfpsMeshFrameParameterSetId()      { return bfps_mesh_frame_parameter_set_id_; }
  uint8_t   getBfpsOutputFlagPresentFlag()        { return bfps_output_flag_present_flag_; }
  uint32_t  getBfpsNumRefIdxDefaultActiveMinus1() { return bfps_num_ref_idx_default_active_minus1_; }
  uint32_t  getBfpsAdditionalLtMfocLsbLen()       { return bfps_additional_lt_mfoc_lsb_len_; }
  uint8_t   getBfpsExtensionPresentFlag()         { return bfps_extension_present_flag_; }
  uint8_t   getBfpsExtension8Bits()               { return bfps_extension_8bits_ ;}
  bool      getBfpsExtensionDataFlag()            { return bfps_extension_data_flag_ ;}

  BaseMeshSubmeshInformation&   getSubmeshInformation() { return submeshInformation_; }
  const uint8_t   getBfpsMeshSequenceParameterSetId()   const{ return bfps_mesh_sequence_parameter_set_id_; }
  const uint8_t   getBfpsMeshFrameParameterSetId()      const{ return bfps_mesh_frame_parameter_set_id_; }
  const uint8_t   getBfpsOutputFlagPresentFlag()        const{ return bfps_output_flag_present_flag_; }
  const uint32_t  getBfpsNumRefIdxDefaultActiveMinus1() const{ return bfps_num_ref_idx_default_active_minus1_; }
  const uint32_t  getBfpsAdditionalLtMfocLsbLen()       const{ return bfps_additional_lt_mfoc_lsb_len_; }
  const uint8_t   getBfpsExtensionPresentFlag()         const{ return bfps_extension_present_flag_; }
  const uint8_t   getBfpsExtension8Bits()               const{ return bfps_extension_8bits_ ;}
  const bool      getBfpsExtensionDataFlag()            const{ return bfps_extension_data_flag_ ;}
  
  
  void      setBfpsMeshSequenceParameterSetId(uint8_t value) { bfps_mesh_sequence_parameter_set_id_ = value; }
  void      setBfpsMeshFrameParameterSetId(uint8_t value) { bfps_mesh_frame_parameter_set_id_ = value; }
  void      setBfpsOutputFlagPresentFlag(uint8_t value) { bfps_output_flag_present_flag_ = value; }
  void      setBfpsNumRefIdxDefaultActiveMinus1(uint32_t value) { bfps_num_ref_idx_default_active_minus1_ = value; }
  void      setBfpsAdditionalLtMfocLsbLen(uint32_t value) { bfps_additional_lt_mfoc_lsb_len_ = value; }
  void      setBfpsExtensionPresentFlag(uint8_t value) { bfps_extension_present_flag_ = value; }
  void      setSubmeshInformation(BaseMeshSubmeshInformation value) { submeshInformation_ = value; }
  void      setBfpsExtension8Bits(uint8_t value) { bfps_extension_8bits_ = value;}
  void      setBfpsExtensionDataFlag(uint8_t value){ bfps_extension_data_flag_ = value;}
  
private:
  uint8_t  bfps_mesh_sequence_parameter_set_id_;
  uint8_t  bfps_mesh_frame_parameter_set_id_;
  uint8_t  bfps_output_flag_present_flag_;
  uint32_t bfps_num_ref_idx_default_active_minus1_;
  uint32_t bfps_additional_lt_mfoc_lsb_len_;
  uint8_t  bfps_extension_present_flag_;
  uint8_t  bfps_extension_8bits_;
  bool     bfps_extension_data_flag_;
  BaseMeshSubmeshInformation submeshInformation_;

};

};
