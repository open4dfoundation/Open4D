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

// Base mesh tile data unit syntax
class BaseMeshSubmeshDataUnitIntra{
public:
    BaseMeshSubmeshDataUnitIntra(){
      _frameIndex=0;
  }
  ~BaseMeshSubmeshDataUnitIntra() {mesh_data_unit_.clear();}
    BaseMeshSubmeshDataUnitIntra& operator=( const BaseMeshSubmeshDataUnitIntra& ) = default;
  
  int32_t     getFrameIndex()             { return _frameIndex; }
  void        setFrameIndex(uint32_t value){ _frameIndex = value; }

  bool readToMeshDataUnit(uint8_t* encodedDataBuffer, uint32_t byteCount){
    mesh_data_unit_.resize( byteCount, 0 );
    std::copy(encodedDataBuffer, encodedDataBuffer + byteCount, reinterpret_cast<char *>(getCodedMeshDataUnitBuffer()));
    return false;
  }
  void                         setPayloadSize(size_t size) { _mesh_data_unit_payload_size_in_byte=size;}
  void                         setCodedMeshDataUnitBitsInLastByte(size_t size) { _mesh_data_unit_bits_in_last_byte = size; }
  void                         setCodedMeshHeaderDataSize(size_t size) { _mesh_data_unit_header_size_in_payload_in_byte = size;}
  void                         allocateMeshDataBuffer(size_t size){ mesh_data_unit_.resize( size, 0 ); }
  
  size_t                       getPayloadSize()             { return _mesh_data_unit_payload_size_in_byte;}
  size_t                       getCodedMeshDataUnitBitsInLastByte()   { return _mesh_data_unit_bits_in_last_byte; }
  std::vector<uint8_t>&        getCodedMeshDataUnit()       { return mesh_data_unit_; }
  size_t                       getCodedMeshDataSize()       { return mesh_data_unit_.size(); }
  uint8_t*                     getCodedMeshDataUnitBuffer() { return mesh_data_unit_.data(); }
  int64_t                      getCodedMeshHeaderDataSize() { return _mesh_data_unit_header_size_in_payload_in_byte; }
  
  const size_t                 getPayloadSize()             const { return _mesh_data_unit_payload_size_in_byte;}
  const size_t                 getCodedMeshDataUnitBitsInLastByte()   const { return _mesh_data_unit_bits_in_last_byte; }
  const std::vector<uint8_t>&  getCodedMeshDataUnit()       const { return mesh_data_unit_; }
  const size_t                 getCodedMeshDataSize()       const { return mesh_data_unit_.size(); }
  const uint8_t*               getCodedMeshDataUnitBuffer() const { return mesh_data_unit_.data(); }
private:
  int32_t     _frameIndex;
  size_t      _mesh_data_unit_payload_size_in_byte;
  size_t      _mesh_data_unit_bits_in_last_byte;
  size_t      _mesh_data_unit_header_size_in_payload_in_byte;
  std::vector<uint8_t> mesh_data_unit_;

};

class BaseMeshSubmeshDataUnitInter{
public:
    BaseMeshSubmeshDataUnitInter(){
    _frameIndex=0;
    _referenceFrameIndex=0;
  }
  ~BaseMeshSubmeshDataUnitInter() {mesh_data_unit_.clear();}
    BaseMeshSubmeshDataUnitInter& operator=( const BaseMeshSubmeshDataUnitInter& ) = default;
  
  int32_t     getReferenceFrameIndex()    { return _referenceFrameIndex; }
  int32_t     getFrameIndex()             { return _frameIndex; }
  void        setFrameIndex(uint32_t value){ _frameIndex = value; }
  void        setReferenceFrameIndex(uint32_t value){ _referenceFrameIndex = value; }

  bool readToMeshDataUnit(uint8_t* encodedDataBuffer, uint32_t byteCount){
    mesh_data_unit_.resize( byteCount, 0 );
    std::copy(encodedDataBuffer, encodedDataBuffer + byteCount, reinterpret_cast<char *>(getCodedMeshDataUnitBuffer()));
    //setSize(byteCount);
    return false;
  }
  size_t                       getPayloadSize()             { return _mesh_data_unit_payload_size_in_byte;}
  size_t                       getCodedMeshDataUnitBitsInLastByte()   { return _mesh_data_unit_bits_in_last_byte; }
  std::vector<uint8_t>&        getCodedMeshDataUnit()       { return mesh_data_unit_; }
  size_t                       getCodedMeshDataSize()       { return mesh_data_unit_.size(); }
  void                         setCodedMotionHeaderDataSize(size_t size) { _motion_data_unit_header_size_in_payload_in_byte = size; }
  uint8_t*                     getCodedMeshDataUnitBuffer() { return mesh_data_unit_.data(); }
  const int32_t                getReferenceFrameIndex() const { return _referenceFrameIndex; }
  const size_t                 getPayloadSize()             const { return _mesh_data_unit_payload_size_in_byte;}
  const size_t                 getCodedMeshDataUnitBitsInLastByte()   const { return _mesh_data_unit_bits_in_last_byte; }
  int64_t                      getCodedMotionHeaderDataSize() { return _motion_data_unit_header_size_in_payload_in_byte; }
  const std::vector<uint8_t>&  getCodedMeshDataUnit()       const { return mesh_data_unit_; }
  const size_t                 getCodedMeshDataSize()       const { return mesh_data_unit_.size(); }
  const uint8_t*               getCodedMeshDataUnitBuffer() const { return mesh_data_unit_.data(); }
  void                  allocateCodedMeshDataUnit(size_t value)  { mesh_data_unit_.resize(value); }
  void                  setPayloadSize(size_t size) { _mesh_data_unit_payload_size_in_byte=size;}
  void                  setCodedMeshDataUnitBitsInLastByte(size_t size) { _mesh_data_unit_bits_in_last_byte = size; }
  uint32_t    getMotionInfoSize() const { return motionInfoSizeInByte_;}
  uint32_t    getMotionInfoSize()       { return motionInfoSizeInByte_;}
  void        setMotionInfoSize(uint32_t value)  { motionInfoSizeInByte_=value;}
  
  void        setMchMaxMvpCandMinus1(int32_t value) { mch_max_num_mvp_cand_minus1_ = value; }
  int8_t      getMchMaxMvpCandMinus1() const { return mch_max_num_mvp_cand_minus1_; }
  int8_t      getMchMaxMvpCandMinus1() { return mch_max_num_mvp_cand_minus1_; }
  void        setMchLog2MotionGroupSize(int32_t value) { mch_log2_motion_group_size_ = value; }
  int32_t     getMchLog2MotionGroupSize() const { return mch_log2_motion_group_size_; }
  int32_t     getMchLog2MotionGroupSize() { return mch_log2_motion_group_size_; }
  void        setMcpVertexCount(int32_t value){ mcp_vertex_count_ = value; }
  int32_t     getMcpVertexCount() const { return mcp_vertex_count_;}
  int32_t     getMcpVertexCount()       { return mcp_vertex_count_;}

private:
  int32_t     _frameIndex;
  size_t      _mesh_data_unit_payload_size_in_byte = 0;
  size_t      _mesh_data_unit_bits_in_last_byte = 0;
  size_t      _motion_data_unit_header_size_in_payload_in_byte = 0;
  int32_t     _referenceFrameIndex; //position in the reference frame list that contains the absolute frame Index
  uint32_t    motionInfoSizeInByte_=0;
  std::vector<uint8_t> mesh_data_unit_;
  uint8_t    mch_max_num_mvp_cand_minus1_ = 0;
  uint32_t   mch_log2_motion_group_size_ = 0;
  int32_t    mcp_vertex_count_=0;

};

};  // namespace vmesh
