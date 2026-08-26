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

namespace atlas {

// 8.3.6.2.5 Atlas mesh information syntax
class AtlasFrameMeshInformation {
public:
  AtlasFrameMeshInformation() : numSubmeshesInAtlasFrameMinus1_(0),
      signalledSubmeshIdFlag_(0),
      signalledSubmeshIdDeltaLength_(0),
      submeshId_(0) {
      submeshId_.resize(1);
  };
  ~AtlasFrameMeshInformation() {
    submeshId_.clear();
  };

  AtlasFrameMeshInformation&
       operator=(const AtlasFrameMeshInformation&) = default;
  bool operator==(const AtlasFrameMeshInformation& other) const {
    if (numSubmeshesInAtlasFrameMinus1_ != other.numSubmeshesInAtlasFrameMinus1_) {
      return false;
    }
    return true;
  }
  auto getNumSubmeshesInAtlasFrameMinus1() const {
    return numSubmeshesInAtlasFrameMinus1_;
  }
  auto getSignalledSubmeshIdFlag() const { return signalledSubmeshIdFlag_; }
  auto getSignalledSubmeshIdDeltaLength() const {
    return signalledSubmeshIdDeltaLength_;
  }
  auto getSubmeshId(size_t index) const { return submeshId_[index]; }
  auto& getNumSubmeshesInAtlasFrameMinus1() { return numSubmeshesInAtlasFrameMinus1_; }
  auto& getSignalledSubmeshIdFlag() { return signalledSubmeshIdFlag_; }
  auto& getSignalledSubmeshIdDeltaLength() {
    return signalledSubmeshIdDeltaLength_;
  }
  auto& getSubmeshId(size_t index) {
    if (index >= submeshId_.size()) submeshId_.resize(submeshId_.size() + 1);
    return submeshId_[index];
  }
  auto& getSubmeshIds() {
    return submeshId_;
  }
  void setSubmeshId(size_t index, uint32_t value) {
    if(index >= submeshId_.size()) submeshId_.resize(index+1);
    submeshId_[index] = value;
  }

  std::vector<uint32_t> _submeshIDToIndex;
  std::vector<uint32_t> _submeshIndexToID;
private:
  int32_t               numSubmeshesInAtlasFrameMinus1_       = 0;
  bool                  signalledSubmeshIdFlag_         = false;
  uint32_t              signalledSubmeshIdDeltaLength_ = 0;
  std::vector<uint32_t> submeshId_;
};

};  // namespace vmesh
