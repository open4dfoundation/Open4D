/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
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
#include "vmc.hpp"

//============================================================================

namespace vmesh {

//============================================================================
struct VMCGroupOfFramesInfo;

class SequenceInfo {
public:
  SequenceInfo()  = default;
  ~SequenceInfo() = default;

  VMCGroupOfFramesInfo&       gof(int index) { return gofInfo_[index]; }
  const VMCGroupOfFramesInfo& gof(int index) const { return gofInfo_[index]; }
  VMCGroupOfFramesInfo&       operator[](int index) { return gofInfo_[index]; }

  std::vector<VMCGroupOfFramesInfo>::iterator begin() {
    return gofInfo_.begin();
  }
  std::vector<VMCGroupOfFramesInfo>::iterator end() { return gofInfo_.end(); }

  int generate(int                                  frameCount,
               int                                  startFrame,
               int                                  maxGOFSize,
               bool                                 analyzeGof,
               const std::vector<BaseMeshGOPEntry>& GOPList,
               const std::string&                   inputPath);
  int generate(const int frameCount, const int startFrame, const int gofSize);
  int applyGOP(const std::vector<BaseMeshGOPEntry>& GOPList,
               const std::string&                   inputPath);

  int save(const std::string& outputPath);

  int load(int          frameCount,
           int          startFrame,
           int          maxGOFSize,
           std::string& groupOfFramesStructurePath);

  int gofCount() const { return (int)gofInfo_.size(); }

  void trace();

private:
  int                               frameCount_           = 0;
  int                               startFrame_           = 0;
  int                               groupOfFramesMaxSize_ = 0;
  std::vector<VMCGroupOfFramesInfo> gofInfo_;
};

}  // namespace vmesh
