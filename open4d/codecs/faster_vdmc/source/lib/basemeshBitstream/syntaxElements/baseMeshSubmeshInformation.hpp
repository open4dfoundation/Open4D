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

namespace basemesh {

// 8.3.6.2.2 BaseMesh submesh information syntax
class BaseMeshSubmeshInformation {
public:
  BaseMeshSubmeshInformation() :
  bmsi_num_submeshes_minus1_(0),
  bmsi_signalled_submesh_id_flag_(0),
  bmsi_signalled_submesh_id_delta_length_(0),
  bmsi_submesh_id_(0){
    bmsi_submesh_id_.resize(1);
  }
  ~BaseMeshSubmeshInformation() {}
  BaseMeshSubmeshInformation& operator=( const BaseMeshSubmeshInformation& ) = default;
  int16_t   getBmsiNumSubmeshesMinus1() { return bmsi_num_submeshes_minus1_; }
  bool      getBmsiSignalledSubmeshIdFlag() { return bmsi_signalled_submesh_id_flag_; }
  uint32_t  getBmsiSignalledSubmeshIdDeltaLength() { return bmsi_signalled_submesh_id_delta_length_; }
//  std::vector<uint32_t>&  getBmsiSubmeshIds() { return bmsi_submesh_id_; }
//  uint32_t                getBmsiSubmeshId(size_t index) { return bmsi_submesh_id_[index]; }
  void   setBmsiNumSubmeshesMinus1(int16_t value) { bmsi_num_submeshes_minus1_ = value;
    if(bmsi_num_submeshes_minus1_>0)bmsi_submesh_id_.resize(bmsi_num_submeshes_minus1_+1, 0);
  }
  void   setBmsiSignalledSubmeshIdFlag(bool value) { bmsi_signalled_submesh_id_flag_ = value; }
  void   setBmsiSignalledSubmeshIdDeltaLength(uint32_t value) { bmsi_signalled_submesh_id_delta_length_ = value; }
//  void   setBmsiSubmeshId(std::vector<uint32_t>  value ) { bmsi_submesh_id_ = value; }
//  void   setBmsiSubmeshId(uint32_t value, size_t index)  { bmsi_submesh_id_[index] = value; }
  
  std::vector<uint32_t> _submeshIDToIndex;
  std::vector<uint32_t> _submeshIndexToID;
  
private:
  int16_t  bmsi_num_submeshes_minus1_;
  bool     bmsi_signalled_submesh_id_flag_;
  uint32_t bmsi_signalled_submesh_id_delta_length_;
  std::vector<uint32_t> bmsi_submesh_id_;
};

};  // namespace vmesh
