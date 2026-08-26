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

#include <cstdint>
#include <fstream>
#include <string>

#include "util/misc.hpp"
#include "util/mesh.hpp"
#include "util/vector.hpp"
#include "vmc.hpp"

#include "sequenceInfo.hpp"

//============================================================================

namespace {

using vmesh::TriangleMesh;
using vmesh::Vec3;

bool
referencable(const TriangleMesh<double>& mesh0,
             const TriangleMesh<double>& mesh1) {
  bool same = false;
  if (mesh1.pointCount() == mesh0.pointCount()
      && mesh1.triangleCount() == mesh0.triangleCount()
      && mesh1.texCoordCount() == mesh0.texCoordCount()
      && mesh1.texCoordTriangleCount() == mesh0.texCoordTriangleCount()) {
    same                     = true;
    const auto texCoordCount = mesh0.texCoordCount();
    for (int32_t v = 0; same && v < texCoordCount; ++v) {
      const auto& texCoord0 = mesh0.texCoord(v);
      const auto& texCoord1 = mesh1.texCoord(v);
      for (int32_t k = 0; k < 2; ++k) {
        if (texCoord0[k] != texCoord1[k]) {
          same = false;
          break;
        }
      }
    }
    const auto triangleCount = mesh0.triangleCount();
    for (int32_t t = 0; same && t < triangleCount; ++t) {
      const auto& tri0         = mesh0.triangle(t);
      const auto& tri1         = mesh1.triangle(t);
      const auto& triTexCoord0 = mesh0.texCoordTriangle(t);
      const auto& triTexCoord1 = mesh1.texCoordTriangle(t);
      for (int32_t k = 0; k < 3; ++k) {
        if (tri0[k] != tri1[k] || triTexCoord0[k] != triTexCoord1[k]) {
          same = false;
          break;
        }
      }
    }
  }
  return same;
}

std::vector<Vec3<int32_t>>
generatePredStructure(const int          frameCount,
                      const int          startFrame,
                      const std::string& inputPath) {
  TriangleMesh<double> mesh0;
  TriangleMesh<double> mesh1;
  auto                 frameIndex0 = startFrame;
  if (!mesh0.load(inputPath, frameIndex0)) {
    std::stringstream ss;
    ss << "Error: can't load frame" << frameIndex0 << '\n';
    throw std::runtime_error(ss.str());
  }
  std::vector<Vec3<int32_t>> predStructure(frameCount);
  predStructure[0]        = Vec3<int32_t>(frameIndex0, frameIndex0, 0);
  int32_t interFrameCount = 0;
  for (int32_t f = 1; f < frameCount; ++f) {
    const auto frameIndex1 = startFrame + f;
    if (!mesh1.load(inputPath, frameIndex1)) {
      std::stringstream ss;
      ss << "Error: can't load frame" << frameIndex1 << '\n';
      throw std::runtime_error(ss.str());
    }
    if (!referencable(mesh0, mesh1)) {
      frameIndex0 = frameIndex1;
      mesh0       = mesh1;
      printf("Analyse => fileName = %3d not same \t", frameIndex1);
    } else {
      printf("Analyse => fileName = %3d     same \t", frameIndex1);
      //std::cout << interFrameCount++ << ' ' << frameIndex0 << " -> "
      //          << frameIndex1 << '\n';
    }
    predStructure[f] = Vec3<int32_t>(frameIndex1, frameIndex0, 0);
    //printf("Analyse => index = %3d ref = %3d \n", frameIndex1, frameIndex0);
    printf(
      "fileName(curr) = %3d fileName(Ref) = %3d \n", frameIndex1, frameIndex0);
  }
  return predStructure;
}

std::vector<Vec3<int32_t>>
generatePredStructureWithGOPList(
  const int                                   frameCount,
  const int                                   startFrame,
  const std::vector<vmesh::BaseMeshGOPEntry>& GOPList,
  const std::string&                          inputPath) {
  std::vector<Vec3<int32_t>> predStructure(frameCount);
  predStructure[0]              = Vec3<int32_t>(startFrame, startFrame, 0);
  int32_t       interFrameCount = 0;
  const int32_t numGOP          = GOPList.size();
  const int32_t numLastGOP      = (frameCount - 1) % numGOP;
  for (int32_t decodeOrderIndex = 1; decodeOrderIndex < frameCount;
       ++decodeOrderIndex) {
    const auto f = vmesh::decodeOrderToFrameOrderBasemesh(
      decodeOrderIndex, frameCount, GOPList);
    const auto           frameIndex1 = startFrame + f;
    TriangleMesh<double> mesh1;
    if (!mesh1.load(inputPath, frameIndex1)) {
      std::stringstream ss;
      ss << "Error: can't load frame" << frameIndex1 << '\n';
      throw std::runtime_error(ss.str());
    }
    const auto referenceFrameIndex =
      vmesh::referenceFrameIndexFromFrameOrderBasemesh(f, frameCount, GOPList);
    const auto           frameIndex0 = startFrame + referenceFrameIndex;
    TriangleMesh<double> mesh0;
    if (!mesh0.load(inputPath, frameIndex0)) {
      std::stringstream ss;
      ss << "Error: can't load frame" << frameIndex0 << '\n';
      throw std::runtime_error(ss.str());
    }

    if (!referencable(mesh0, mesh1)) {
      predStructure[f] = Vec3<int32_t>(frameIndex1, frameIndex1, 0);
      printf("Analyse => index = %3d ref = %3d \n", frameIndex1, frameIndex1);
    } else {
      std::cout << interFrameCount++ << ' ' << frameIndex0 << " -> "
                << frameIndex1 << '\n';
      predStructure[f] = Vec3<int32_t>(frameIndex1, frameIndex0, 0);
      printf("Analyse => index = %3d ref = %3d \n", frameIndex1, frameIndex0);
    }
  }
  return predStructure;
}

int
updateGofInfoWithGOPList(vmesh::VMCGroupOfFramesInfo&                gofInfo,
                         const std::vector<vmesh::BaseMeshGOPEntry>& GOPList,
                         const std::string& inputPath) {
  const auto                 frameCount = gofInfo.frameCount_;
  const auto                 startFrame = gofInfo.startFrameIndex_;
  std::vector<Vec3<int32_t>> predStructure(frameCount);
  predStructure[0]              = Vec3<int32_t>(startFrame, startFrame, 0);
  int32_t       interFrameCount = 0;
  const int32_t numGOP          = GOPList.size();
  const int32_t numLastGOP      = (frameCount - 1) % numGOP;
  for (int32_t decodeOrderIndex = 1; decodeOrderIndex < frameCount;
       ++decodeOrderIndex) {
    const auto f = vmesh::decodeOrderToFrameOrderBasemesh(
      decodeOrderIndex, frameCount, GOPList);
    const auto           frameIndex1 = startFrame + f;
    TriangleMesh<double> mesh1;
    if (!mesh1.load(inputPath, frameIndex1)) {
      std::cerr << "Error: can't load frame" << frameIndex1 << '\n';
      return 1;
    }
    const auto prevIdx =
      vmesh::referenceFrameIndexFromFrameOrderBasemesh(f, frameCount, GOPList);
    const auto           frameIndex0 = predStructure[prevIdx][1];
    TriangleMesh<double> mesh0;
    if (!mesh0.load(inputPath, frameIndex0)) {
      std::cerr << "Error: can't load frame" << frameIndex0 << '\n';
      return 1;
    }

    const bool same = referencable(mesh0, mesh1);
    if (!same) {
      printf("Analyse => fileName = %3d not same \t", frameIndex1);
    } else {
      printf("Analyse => fileName = %3d     same \t", frameIndex1);
    }
    const auto frameIndex          = frameIndex1;
    const auto referenceFrameIndex = same ? frameIndex0 : frameIndex1;
    const auto previousFrameIndex  = same ? startFrame + prevIdx : frameIndex1;
    predStructure[f] = Vec3<int32_t>(frameIndex, referenceFrameIndex, 0);
    printf("fileName(curr) = %3d fileName(Ref) = %3d fileName(prev) = %3d\n",
           frameIndex,
           referenceFrameIndex,
           previousFrameIndex);

    auto& frameInfo = gofInfo[f];
    if (referenceFrameIndex == frameIndex) {
      frameInfo.frameIndex          = frameIndex - startFrame;
      frameInfo.referenceFrameIndex = -1;
      frameInfo.previousFrameIndex  = -1;
      frameInfo.type                = basemesh::I_BASEMESH;
    } else {
      frameInfo.frameIndex          = frameIndex - startFrame;
      frameInfo.referenceFrameIndex = referenceFrameIndex - startFrame;
      frameInfo.previousFrameIndex  = previousFrameIndex - startFrame;
      frameInfo.type                = basemesh::P_BASEMESH;
    }
  }
  return 0;
}

}  // namespace

namespace vmesh {
int
SequenceInfo::generate(const int frameCount,
                       const int startFrame,
                       const int gofSize) {
  int32_t numGof = ceil((double)frameCount / (double)gofSize);
  gofInfo_.resize(numGof);
  int32_t gofStartIndex = 0;
  for (int i = 0; i < numGof; i++) {
    VMCGroupOfFramesInfo& gofInfo = gofInfo_[i];
    gofInfo.frameCount_ =
      i < (numGof - 1) ? gofSize : (frameCount - (numGof - 1) * gofSize);
    gofInfo.startFrameIndex_ = startFrame + gofStartIndex;
    gofInfo.index_           = i;
    gofStartIndex += gofInfo.frameCount_;
    gofInfo.framesInfo_.reserve(gofInfo.frameCount_);

    for (int32_t fi = 0; fi < gofInfo.frameCount_; fi++) {
      VMCFrameInfo frameInfo;
      frameInfo.frameIndex          = fi;
      frameInfo.referenceFrameIndex = -1;
      frameInfo.type                = basemesh::I_BASEMESH;
      gofInfo.framesInfo_.push_back(frameInfo);
    }
  }
  return 0;
}
int
SequenceInfo::generate(const int                            frameCount,
                       const int                            startFrame,
                       const int                            maxGOFSize,
                       const bool                           analyzeGof,
                       const std::vector<BaseMeshGOPEntry>& GOPList,
                       const std::string&                   inputPath) {
  printf("SequenceInfo generate analyzeGof = %d \n",
         static_cast<int>(analyzeGof));
  startFrame_           = startFrame;
  frameCount_           = frameCount;
  groupOfFramesMaxSize_ = maxGOFSize;
  if (!analyzeGof) {
    int32_t currentGofIndex = -1;
    for (int32_t f = 0; f < frameCount_; ++f) {
      const auto frameIndex = startFrame + f;
      const auto gofIndex   = f / groupOfFramesMaxSize_;
      if (gofIndex != currentGofIndex) {
        currentGofIndex = gofIndex;
        gofInfo_.resize(gofInfo_.size() + 1);
        auto& gofInfo            = gofInfo_.back();
        gofInfo.frameCount_      = 1;
        gofInfo.startFrameIndex_ = frameIndex;
        gofInfo.index_           = gofIndex;
        VMCFrameInfo frameInfo;
        frameInfo.frameIndex          = frameIndex - gofInfo.startFrameIndex_;
        frameInfo.referenceFrameIndex = -1;
        frameInfo.type                = basemesh::I_BASEMESH;
        gofInfo.framesInfo_.reserve(groupOfFramesMaxSize_);
        gofInfo.framesInfo_.push_back(frameInfo);
      } else {
        auto& gofInfo = gofInfo_.back();
        ++gofInfo.frameCount_;
        VMCFrameInfo frameInfo;
        frameInfo.frameIndex          = frameIndex - gofInfo.startFrameIndex_;
        frameInfo.referenceFrameIndex = -1;
        frameInfo.type                = basemesh::I_BASEMESH;
        gofInfo.framesInfo_.push_back(frameInfo);
      }
    }
  } else {
    std::vector<Vec3<int32_t>> predStructure;
    try {
      predStructure =
        GOPList.empty()
          ? generatePredStructure(frameCount, startFrame, inputPath)
          : generatePredStructureWithGOPList(
            frameCount, startFrame, GOPList, inputPath);
    } catch (const std::exception& e) {
      std::cerr << e.what();
      return 1;
    }

    int32_t framesInGOF        = 0;
    int32_t startFrameIndexGOF = startFrame;
    int32_t counterGOF         = 0;
    for (int32_t f = 0; f < frameCount;) {
      const auto refFrameIndex      = predStructure[f][1];
      int32_t    coherentFrameCount = 0;
      while (coherentFrameCount <= maxGOFSize
             && f + coherentFrameCount < frameCount
             && predStructure[f + coherentFrameCount][1] == refFrameIndex) {
        ++coherentFrameCount;
      }

      if (framesInGOF + coherentFrameCount <= maxGOFSize) {
        framesInGOF += coherentFrameCount;
      } else {
        const auto start = startFrameIndexGOF;
        const auto end   = startFrameIndexGOF + framesInGOF;
        assert(start >= startFrame);
        assert(end <= startFrame + frameCount_);
        std::cout << "GOf[" << counterGOF << "] " << start << " -> " << end
                  << ' ' << framesInGOF << '\n';
        for (int32_t t = start; t < end; ++t) {
          predStructure[t - startFrame][2] = counterGOF;
        }
        startFrameIndexGOF = startFrame + f;
        framesInGOF        = coherentFrameCount;
        ++counterGOF;
      }
      f += coherentFrameCount;
    }
    if (framesInGOF != 0) {
      const auto start = startFrameIndexGOF;
      const auto end   = startFrameIndexGOF + framesInGOF;
      assert(start >= startFrame);
      assert(end <= startFrame + frameCount);
      std::cout << "GOf[" << counterGOF << "] " << start << " -> " << end
                << ' ' << framesInGOF << '\n';
      for (int32_t t = start; t < end; ++t) {
        predStructure[t - startFrame][2] = counterGOF;
      }
      ++counterGOF;
    }

    int32_t currentGofIndex = -1;
    for (int32_t t = 0; t < frameCount_; ++t) {
      const auto& pred                = predStructure[t];
      const auto  frameIndex          = pred[0];
      const auto  referenceFrameIndex = pred[1];
      const auto  gofIndex            = pred[2];
      assert(referenceFrameIndex <= frameIndex);
      if (gofIndex != currentGofIndex) {
        currentGofIndex = gofIndex;
        gofInfo_.resize(gofInfo_.size() + 1);
        auto& gofInfo            = gofInfo_.back();
        gofInfo.frameCount_      = 1;
        gofInfo.startFrameIndex_ = frameIndex;
        gofInfo.index_           = gofIndex;
        VMCFrameInfo frameInfo;
        frameInfo.frameIndex          = frameIndex - gofInfo.startFrameIndex_;
        frameInfo.referenceFrameIndex = -1;
        frameInfo.type                = basemesh::I_BASEMESH;
        gofInfo.framesInfo_.reserve(groupOfFramesMaxSize_);
        gofInfo.framesInfo_.push_back(frameInfo);
      } else {
        auto& gofInfo = gofInfo_.back();
        ++gofInfo.frameCount_;
        VMCFrameInfo frameInfo;

        if (referenceFrameIndex == frameIndex) {
          frameInfo.frameIndex = frameIndex - gofInfo.startFrameIndex_;
          frameInfo.referenceFrameIndex = -1;
          frameInfo.previousFrameIndex  = -1;
          frameInfo.type                = basemesh::I_BASEMESH;
        } else {
          frameInfo.frameIndex = frameIndex - gofInfo.startFrameIndex_;
          frameInfo.referenceFrameIndex =
            referenceFrameIndex - gofInfo.startFrameIndex_;
          frameInfo.previousFrameIndex = frameInfo.referenceFrameIndex;
          frameInfo.type               = basemesh::P_BASEMESH;
        }
        gofInfo.framesInfo_.push_back(frameInfo);
      }
    }
  }
  return 0;
}

int
SequenceInfo::applyGOP(const std::vector<BaseMeshGOPEntry>& GOPList,
                       const std::string&                   inputPath) {
  for (auto& gofInfo : gofInfo_) {
    if (updateGofInfoWithGOPList(gofInfo, GOPList, inputPath) != 0) {
      return 1;
    }
  }
  return 0;
}

int
SequenceInfo::save(const std::string& outputPath) {
  std::ofstream fout(outputPath);
  if (!fout.is_open()) {
    std::cerr << "Error: can't create output file " << outputPath << '\n';
    return 1;
  }
  for (auto& gofInfo : gofInfo_) {
    for (auto& frameInfo : gofInfo.framesInfo_) {
      auto frameIndex = frameInfo.frameIndex + gofInfo.startFrameIndex_;
      auto refIndex =
        frameInfo.referenceFrameIndex == -1
          ? frameInfo.frameIndex + gofInfo.startFrameIndex_
          : frameInfo.referenceFrameIndex + gofInfo.startFrameIndex_;
      fout << frameIndex << ' ' << refIndex << ' ' << gofInfo.index_ << '\n';
    }
  }
  return 0;
}

int
SequenceInfo::load(int          frameCount,
                   int          startFrame,
                   int          maxGOFSize,
                   std::string& groupOfFramesStructurePath) {
  startFrame_           = startFrame;
  frameCount_           = frameCount;
  groupOfFramesMaxSize_ = maxGOFSize;
  gofInfo_.reserve(10);
  if (groupOfFramesStructurePath.empty()) {
    for (int32_t f = 0, gofIndex = 0; f < frameCount_; ++gofIndex) {
      const auto startFrameGOF = f + startFrame_;
      const auto frameCountGOF =
        std::min(groupOfFramesMaxSize_, frameCount_ - f);
      gofInfo_.resize(gofInfo_.size() + 1);
      auto& gofInfo            = gofInfo_.back();
      gofInfo.frameCount_      = frameCountGOF;
      gofInfo.startFrameIndex_ = startFrameGOF;
      gofInfo.index_           = gofIndex;
      gofInfo.resize(frameCountGOF);
      for (int32_t frameIndexInGOF = 0; frameIndexInGOF < frameCountGOF;
           ++frameIndexInGOF) {
        VMCFrameInfo& frameInfo       = gofInfo.frameInfo(frameIndexInGOF);
        frameInfo.frameIndex          = frameIndexInGOF;
        frameInfo.referenceFrameIndex = -1;
        frameInfo.type                = basemesh::I_BASEMESH;
      }
      f += frameCountGOF;
    }
    return 0;
  }

  std::ifstream fin(groupOfFramesStructurePath);
  if (fin.is_open()) {
    std::string              line;
    std::vector<std::string> tokens;
    int32_t                  currentGofIndex = -1;
    int32_t                  frameCounter    = 0;

    while (getline(fin, line) && frameCounter++ < frameCount_) {
      size_t prev = 0;
      size_t pos  = 0;
      tokens.resize(0);
      while ((pos = line.find_first_of(" ,;:/", prev)) != std::string::npos) {
        if (pos > prev) { tokens.push_back(line.substr(prev, pos - prev)); }
        prev = pos + 1;
      }

      if (prev < line.length()) {
        tokens.push_back(line.substr(prev, std::string::npos));
      }

      const auto frameIndex          = atoi(tokens[0].c_str());
      const auto referenceFrameIndex = atoi(tokens[1].c_str());
      const auto gofIndex            = atoi(tokens[2].c_str());
      assert(referenceFrameIndex <= frameIndex);

      if (gofIndex != currentGofIndex) {
        currentGofIndex = gofIndex;
        gofInfo_.resize(gofInfo_.size() + 1);
        auto& gofInfo            = gofInfo_.back();
        gofInfo.frameCount_      = 1;
        gofInfo.startFrameIndex_ = frameIndex;
        gofInfo.index_           = gofIndex;
        VMCFrameInfo frameInfo;
        frameInfo.frameIndex          = frameIndex - gofInfo.startFrameIndex_;
        frameInfo.referenceFrameIndex = -1;
        frameInfo.type                = basemesh::I_BASEMESH;
        gofInfo.framesInfo_.reserve(groupOfFramesMaxSize_);
        gofInfo.framesInfo_.push_back(frameInfo);
      } else {
        auto& gofInfo = gofInfo_.back();
        ++gofInfo.frameCount_;
        VMCFrameInfo frameInfo;

        if (referenceFrameIndex == frameIndex) {
          frameInfo.frameIndex = frameIndex - gofInfo.startFrameIndex_;
          frameInfo.referenceFrameIndex = -1;
          frameInfo.type                = basemesh::I_BASEMESH;
        } else {
          frameInfo.frameIndex = frameIndex - gofInfo.startFrameIndex_;
          frameInfo.referenceFrameIndex =
            referenceFrameIndex - gofInfo.startFrameIndex_;
          frameInfo.previousFrameIndex = frameInfo.referenceFrameIndex;
          frameInfo.type               = basemesh::P_BASEMESH;
        }
        gofInfo.framesInfo_.push_back(frameInfo);
      }
    }
    fin.close();
    return 0;
  }
  return -1;
}

void
SequenceInfo::trace() {
  int gofIndex = 0;
  printf("SequenceInfo: frameCount = %d startFrame = %d \n",
         frameCount_,
         startFrame_);
  for (auto& seqInfo : gofInfo_) { seqInfo.trace(); }
}

}  // namespace vmesh
