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

namespace basemesh {


class BaseMeshRefListStruct {
public:
  BaseMeshRefListStruct() : numRefEntries_( 0 ) {
    absDeltaMfocSt_.clear();
    mfocLsbLt_.clear();
    stRefMeshFrameFlag_.clear();
    strpfEntrySignFlag_.clear();
  }
  ~BaseMeshRefListStruct() {
    absDeltaMfocSt_.clear();
    mfocLsbLt_.clear();
    stRefMeshFrameFlag_.clear();
    strpfEntrySignFlag_.clear();
  }
  BaseMeshRefListStruct& operator=( const BaseMeshRefListStruct& ) = default;

  void allocate() {
    absDeltaMfocSt_.resize( numRefEntries_, 0 );
    mfocLsbLt_.resize( numRefEntries_, 0 );
    stRefMeshFrameFlag_.resize( numRefEntries_, false );
    strpfEntrySignFlag_.resize( numRefEntries_, false );
  }

  void addAbsDeltaMfocSt( uint8_t value ) { absDeltaMfocSt_.push_back( value ); }
  void addStrafEntrySignFlag( bool value ) { strpfEntrySignFlag_.push_back( value ); }
  void addMfocLsbLt( uint8_t value ) { mfocLsbLt_.push_back( value ); }
  void addStRefMeshFrameFlag( bool value ) { stRefMeshFrameFlag_.push_back( value ); }

  uint8_t getNumRefEntries() { return numRefEntries_; }
  uint8_t getAbsDeltaMfocSt( uint16_t index ) { return absDeltaMfocSt_[index]; }
  bool    getStrafEntrySignFlag( uint16_t index ) { return strpfEntrySignFlag_[index]; }
  uint8_t getMfocLsbLt( uint16_t index ) { return mfocLsbLt_[index]; }
  bool    getStRefMeshFrameFlag( uint16_t index ) { return stRefMeshFrameFlag_[index]; }

  void setNumRefEntries( uint8_t value ) {
    if(value!=numRefEntries_){
      numRefEntries_ = value;
      allocate();
    }
  }
  void setAbsDeltaMfocSt( uint16_t index, uint8_t value ) { absDeltaMfocSt_[index] = value; }
  void setStrafEntrySignFlag( uint16_t index, bool value ) { strpfEntrySignFlag_[index] = value; }
  void setMfocLsbLt( uint16_t index, uint8_t value ) { mfocLsbLt_[index] = value; }
  void setStRefMeshFrameFlag( uint16_t index, bool value ) { stRefMeshFrameFlag_[index] = value; }

private:
  uint8_t              numRefEntries_;
  std::vector<uint8_t> absDeltaMfocSt_;
  std::vector<uint8_t> mfocLsbLt_;
  std::vector<bool>    stRefMeshFrameFlag_;
  std::vector<bool>    strpfEntrySignFlag_;
};

};  // namespace vmesh
