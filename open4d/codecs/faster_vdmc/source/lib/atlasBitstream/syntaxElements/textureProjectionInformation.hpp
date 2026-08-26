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

namespace atlas {

class SubpatchInformation {
    public:
        SubpatchInformation() {
        }
        ~SubpatchInformation() {
        }

        SubpatchInformation& operator=(const SubpatchInformation&) = default;

        auto  getProjectionId() const { return projectionId_; }
        auto  getOrientationId() const { return orientationId_; }
        auto  get2dPosX() const { return pos2dX_; }
        auto  get2dPosY() const { return pos2dY_; }
        auto  getPosBiasX() const { return posBiasX_; }
        auto  getPosBiasY() const { return posBiasY_; }
        auto  get2dSizeXMinus1() const { return size2dXMinus1_; }
        auto  get2dSizeYMinus1() const { return size2dYMinus1_; }
        auto  getScalePresentFlag() const { return scalePresentFlag_; }
        auto  getSubpatchScale() const { return subpatchScale_; }

        auto&  getProjectionId() { return projectionId_; }
        auto&  getOrientationId() { return orientationId_; }
        auto&  get2dPosX() { return pos2dX_; }
        auto&  get2dPosY() { return pos2dY_; }
        auto&  getPosBiasX() { return posBiasX_; }
        auto&  getPosBiasY() { return posBiasY_; }
        auto&  get2dSizeXMinus1() { return size2dXMinus1_; }
        auto&  get2dSizeYMinus1() { return size2dYMinus1_; }
        auto&  getScalePresentFlag() { return scalePresentFlag_; }
        auto&  getSubpatchScale() { return subpatchScale_; }

    private:
        uint8_t  projectionId_;
        uint8_t  orientationId_;
        size_t   pos2dX_;
        size_t   pos2dY_;
        size_t   posBiasX_;
        size_t   posBiasY_;
        int64_t  size2dXMinus1_;
        int64_t  size2dYMinus1_;
        uint8_t  scalePresentFlag_;
        size_t   subpatchScale_;
    };

class SubpatchRawInformation {
public:
    SubpatchRawInformation() {
    }
    ~SubpatchRawInformation() {
    }

    SubpatchRawInformation& operator=(const SubpatchRawInformation&) = default;

    auto getNumRawUVMinus1() const { return numRawUVMinus1_; }
    auto getUcoord(int idx) const { return uCoords_[idx]; }
    auto getVcoord(int idx) const { return vCoords_[idx]; }

    auto& getNumRawUVMinus1() { return numRawUVMinus1_; }
    auto& getUcoord(int idx) {
        if (idx >= uCoords_.size()) uCoords_.resize(idx + 1);
        return uCoords_[idx];
    }
    auto& getVcoord(int idx) {
        if (idx >= vCoords_.size()) vCoords_.resize(idx + 1);
        return vCoords_[idx];
    }

private:
    size_t              numRawUVMinus1_;
    std::vector<int>    uCoords_;
    std::vector<int>    vCoords_;
};

class SubpatchInterInformation {
public:
    SubpatchInterInformation() {
    }
    ~SubpatchInterInformation() {
    }

    SubpatchInterInformation& operator=(const SubpatchInterInformation&) = default;

    auto  getSubpatchIdxDiff() const { return subpatchIdxDiff_; }
    auto  get2dPosXDelta() const { return pos2dXDelta_; }
    auto  get2dPosYDelta() const { return pos2dYDelta_; }
    auto  getPosBiasXDelta() const { return posBiasXDelta_; }
    auto  getPosBiasYDelta() const { return posBiasYDelta_; }
    auto  get2dSizeXDelta() const { return size2dXDelta_; }
    auto  get2dSizeYDelta() const { return size2dYDelta_; }

    auto& getSubpatchIdxDiff() { return subpatchIdxDiff_; }
    auto& get2dPosXDelta() { return pos2dXDelta_; }
    auto& get2dPosYDelta() { return pos2dYDelta_; }
    auto& getPosBiasXDelta() { return posBiasXDelta_; }
    auto& getPosBiasYDelta() { return posBiasYDelta_; }
    auto& get2dSizeXDelta() { return size2dXDelta_; }
    auto& get2dSizeYDelta() { return size2dYDelta_; }

private:
    int      subpatchIdxDiff_;
    int64_t   pos2dXDelta_;
    int64_t   pos2dYDelta_;
    int64_t posBiasXDelta_;
    int64_t posBiasYDelta_;
    int64_t  size2dXDelta_;
    int64_t  size2dYDelta_;
};

class SubpatchMergeInformation {
public:
    SubpatchMergeInformation() {
    }
    ~SubpatchMergeInformation() {
    }

    SubpatchMergeInformation& operator=(const SubpatchMergeInformation&) = default;

    auto  getSubpatchIdx() const { return subpatchIdx_; }
    auto  get2dPosXDelta() const { return pos2dXDelta_; }
    auto  get2dPosYDelta() const { return pos2dYDelta_; }
    auto  getPosBiasXDelta() const { return posBiasXDelta_; }
    auto  getPosBiasYDelta() const { return posBiasYDelta_; }
    auto  get2dSizeXDelta() const { return size2dXDelta_; }
    auto  get2dSizeYDelta() const { return size2dYDelta_; }

    auto& getSubpatchIdx() { return subpatchIdx_; }
    auto& get2dPosXDelta() { return pos2dXDelta_; }
    auto& get2dPosYDelta() { return pos2dYDelta_; }
    auto& getPosBiasXDelta() { return posBiasXDelta_; }
    auto& getPosBiasYDelta() { return posBiasYDelta_; }
    auto& get2dSizeXDelta() { return size2dXDelta_; }
    auto& get2dSizeYDelta() { return size2dYDelta_; }

private:
    size_t   subpatchIdx_;
    int64_t  pos2dXDelta_;
    int64_t  pos2dYDelta_;
    int64_t posBiasXDelta_;
    int64_t posBiasYDelta_;
    int64_t  size2dXDelta_;
    int64_t  size2dYDelta_;
};

class TextureProjectionInformation {
public:
  TextureProjectionInformation() {
    clearSubPatches();
  }
  ~TextureProjectionInformation() {
    clearSubPatches();
  }

  TextureProjectionInformation& operator=(const TextureProjectionInformation&) = default;

  auto  getFaceIdPresentFlag() const { return faceIdPresentFlag_; }
  auto  getFrameUpscaleMinus1() const { return frameUpscale_; }
  auto  getFrameDownscale() const { return frameDownscale_; }
  auto  getSubpatchCountMinus1() const { return subpatchCountMinus1_; }
  auto  getSubpatch(int idx) const { return subpatches[idx]; }

  auto& getFaceIdPresentFlag() { return faceIdPresentFlag_; }
  auto& getFrameUpscaleMinus1() { return frameUpscale_; }
  auto& getFrameDownscale() { return frameDownscale_; }
  auto& getSubpatchCountMinus1() { return subpatchCountMinus1_; }
  auto& getSubpatch(int idx) { 
      if (idx >= subpatches.size()) subpatches.resize(idx + 1);
      return subpatches[idx]; 
  }
  auto  getFaceId2SubpatchIdx(int idx) const { return faceIdToSubpatchIdx_[idx]; }
  auto& getFaceId2SubpatchIdx(int idx) {
      if (idx >= subpatches.size()) faceIdToSubpatchIdx_.resize(idx + 1);
      return faceIdToSubpatchIdx_[idx]; }

  auto  getSubpatchRawEnableFlag() const { return subpatchRawEnableFlag_; }
  auto& getSubpatchRawEnableFlag() { return subpatchRawEnableFlag_; }

  auto  getSubpatchRawPresentFlag(int idx) const { return subpatchRawPresentFlag[idx]; }
  auto& getSubpatchRawPresentFlag(int idx) {
      if (idx >= subpatchRawPresentFlag.size()) subpatchRawPresentFlag.resize(idx + 1);
      return subpatchRawPresentFlag[idx];
  }

  auto  getSubpatchRaw(int idx) const { return subpatchesRaw[idx]; }
  auto& getSubpatchRaw(int idx) {
      if (idx >= subpatchesRaw.size()) subpatchesRaw.resize(idx + 1);
      return subpatchesRaw[idx];
  }

  void allocateSubPatches(size_t subPatchCount) {
      subpatchCountMinus1_ = subPatchCount - 1;
      subpatches.resize(subPatchCount);
      faceIdToSubpatchIdx_.resize(subPatchCount);
      subpatchRawPresentFlag.resize(subPatchCount);
      subpatchesRaw.resize(subPatchCount);
  }
  void clearSubPatches() {
      subpatches.clear();
      faceIdToSubpatchIdx_.clear();
      subpatchRawPresentFlag.clear();
      subpatchesRaw.clear();
  }


private:
    bool faceIdPresentFlag_ = false;
    int64_t                   frameUpscale_ = 1;
    int                       frameDownscale_ = 0;
    size_t                subpatchCountMinus1_ = 0;
    std::vector<SubpatchInformation> subpatches;
    std::vector<int> faceIdToSubpatchIdx_;
    bool subpatchRawEnableFlag_ = true;
    std::vector<int> subpatchRawPresentFlag;
    std::vector<SubpatchRawInformation> subpatchesRaw;
};

class TextureProjectionInterInformation {
public:
    TextureProjectionInterInformation() {
        clearSubPatches();
    }
    ~TextureProjectionInterInformation() {
        clearSubPatches();
    }

    TextureProjectionInterInformation& operator=(const TextureProjectionInterInformation&) = default;

    auto  getFaceIdPresentFlag() const { return faceIdPresentFlag_; }
    auto  getFrameUpscaleMinus1() const { return frameUpscale_; }
    auto  getFrameDownscale() const { return frameDownscale_; }
        auto  getSubpatchCountMinus1() const { return subpatchCountMinus1_; }
        auto  getSubpatchInterEnableFlag() const { return subpatchInterEnableFlag_; }
    auto  getUpdateFlag(int idx) const { return updateFlag[idx]; }
    auto  getSubpatch(int idx) const { return subpatches[idx]; }
    auto  getSubpatchInter(int idx) const { return subpatchesInter[idx]; }
    auto  getFaceId2SubpatchIdx(int idx) const { return faceIdToSubpatchIdx_[idx]; }

    auto& getFaceIdPresentFlag() { return faceIdPresentFlag_; }
    auto& getFrameUpscaleMinus1() { return frameUpscale_; }
    auto& getFrameDownscale() { return frameDownscale_; }
    auto& getSubpatchCountMinus1() { return subpatchCountMinus1_; }
    auto& getSubpatchInterEnableFlag() { return subpatchInterEnableFlag_; }
    auto& getUpdateFlag(int idx) {
        if (idx >= updateFlag.size()) updateFlag.resize(idx + 1);
        return updateFlag[idx];
    }
    auto& getSubpatch(int idx) {
        if (idx >= subpatches.size()) subpatches.resize(idx + 1);
        return subpatches[idx];
    }
    auto& getSubpatchInter(int idx) {
        if (idx >= subpatchesInter.size()) subpatchesInter.resize(idx + 1);
        return subpatchesInter[idx];
    }
    auto& getFaceId2SubpatchIdx(int idx) {
        if (idx >= subpatches.size()) faceIdToSubpatchIdx_.resize(idx + 1);
        return faceIdToSubpatchIdx_[idx];
    }
    void allocateSubPatches(size_t subPatchCount) {
        subpatchCountMinus1_ = subPatchCount - 1;
        updateFlag.resize(subPatchCount, false);
        subpatches.resize(subPatchCount);
        subpatchesInter.resize(subPatchCount);
        faceIdToSubpatchIdx_.resize(subPatchCount);
        subpatchRawPresentFlag.resize(subPatchCount);
        subpatchesRaw.resize(subPatchCount);
    }
    void clearSubPatches() {
        updateFlag.clear();
        subpatches.clear();
        subpatchesInter.clear();
        faceIdToSubpatchIdx_.clear();
        subpatchRawPresentFlag.clear();
        subpatchesRaw.clear();
    }

    auto  getSubpatchRawEnableFlag() const { return subpatchRawEnableFlag_; }
    auto& getSubpatchRawEnableFlag() { return subpatchRawEnableFlag_; }

    auto  getSubpatchRawPresentFlag(int idx) const { return subpatchRawPresentFlag[idx]; }
    auto& getSubpatchRawPresentFlag(int idx) {
        if (idx >= subpatchRawPresentFlag.size()) subpatchRawPresentFlag.resize(idx + 1);
        return subpatchRawPresentFlag[idx];
    }

    auto  getSubpatchRaw(int idx) const { return subpatchesRaw[idx]; }
    auto& getSubpatchRaw(int idx) {
        if (idx >= subpatchesRaw.size()) subpatchesRaw.resize(idx + 1);
        return subpatchesRaw[idx];
    }

private:
    bool faceIdPresentFlag_ = false;
    int64_t                   frameUpscale_ = 0;
    int                       frameDownscale_ = 0;
    bool subpatchInterEnableFlag_ = true;
    size_t                subpatchCountMinus1_ = 0;
    std::vector<int> updateFlag;
    std::vector<SubpatchInformation> subpatches;
    std::vector<SubpatchInterInformation> subpatchesInter;
    std::vector<int> faceIdToSubpatchIdx_;
    bool subpatchRawEnableFlag_ = true;
    std::vector<int> subpatchRawPresentFlag;
    std::vector<SubpatchRawInformation> subpatchesRaw;
};

class TextureProjectionMergeInformation {
public:
    TextureProjectionMergeInformation() {
        clearSubPatches();
    }
    ~TextureProjectionMergeInformation() {
        clearSubPatches();
    }

    TextureProjectionMergeInformation& operator=(const TextureProjectionMergeInformation&) = default;

    auto  getSubpatchMergePresentFlag() const { return subpatchMergePresentFlag_; }
    auto  getSubpatchCountMinus1() const { return subpatchCountMinus1_; }
    auto  getSubpatchMerge(int idx) const { return subpatchesMerge[idx]; }

    auto&  getSubpatchMergePresentFlag() { return subpatchMergePresentFlag_; }
    auto&  getSubpatchCountMinus1() { return subpatchCountMinus1_; }
    auto& getSubpatchMerge(int idx) {
        if (idx >= subpatchesMerge.size()) subpatchesMerge.resize(idx + 1);
        return subpatchesMerge[idx];
    }
    void allocateSubPatches(size_t subPatchCount) {
        subpatchCountMinus1_ = subPatchCount - 1;
        subpatchesMerge.resize(subPatchCount);
    }
    void clearSubPatches() {
        subpatchesMerge.clear();
    }

private:
    bool subpatchMergePresentFlag_ = false;
    size_t                subpatchCountMinus1_ = 0;
    std::vector<SubpatchMergeInformation> subpatchesMerge;
};

};  // namespace vmesh
