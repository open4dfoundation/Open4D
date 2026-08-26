/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2025, ISO/IEC
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

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <chrono>
#include <thread>

#include "util/image.hpp"
#include "util/mesh.hpp"

#include <unordered_map>
#include "vmcChrono.hpp"
#include "baseMeshCommon.hpp"

#define DEBUG_INTERPOLATED_NORMALS 1
#define TENTATIVE_BUGFIX_MULTI_TILE_SUBMESH 1
namespace vmesh {

//============================================================================

enum class DisplacementCoordinateSystem {
  CANNONICAL = 0,
  LOCAL      = 1,
};

//============================================================================
enum class DisplacementQuantizationType {
  DEFAULT  = 0,
  ADAPTIVE = 1
};

enum class PaddingMethod {
  NONE               = 0,
  PUSH_PULL          = 1,
  SPARSE_LINEAR      = 2,
  SMOOTHED_PUSH_PULL = 3,
  HARMONIC_FILL      = 4
};

//============================================================================

enum class CachingPoint {
  NONE     = 0,
  SIMPLIFY = 1,
  TEXGEN   = 2,
  SUBDIV   = 3
};

//============================================================================

enum class SmoothingMethod {
  NONE              = 0,
  VERTEX_CONSTRAINT = 1
};

//============================================================================
enum VideoAttribueType {
  ATTRIBUTE_VIDEO_TEXTURE = 0,
};
//============================================================================

struct VMCFrameInfo {
  int32_t                frameIndex          = -1;
  int32_t                referenceFrameIndex = -1;
  int32_t                previousFrameIndex  = -1;
  basemesh::BaseMeshType type                = basemesh::I_BASEMESH;
};

//============================================================================

struct VMCGroupOfFramesInfo {
  void resize(int32_t frameCount) { framesInfo_.resize(frameCount); }

  const VMCFrameInfo& frameInfo(int32_t frameIndex) const {
    assert(frameIndex >= 0 && frameIndex < frameCount_);
    return framesInfo_[frameIndex];
  }

  VMCFrameInfo& frameInfo(int32_t frameIndex) {
    assert(frameIndex >= 0 && frameIndex < frameCount_);
    return framesInfo_[frameIndex];
  }

  VMCFrameInfo& operator[](int index) { return framesInfo_[index]; }

  void trace() const {
    printf("  - Gof %2d: frameCount = %d startFrame = %d \n",
           index_,
           frameCount_,
           startFrameIndex_);
    for (const auto& frameInfo : framesInfo_) {
      printf("    - frameIndex = %3d refIndex = %3d type = %s \n",
             frameInfo.frameIndex,
             frameInfo.referenceFrameIndex,
             toString(frameInfo.type).c_str());
    }
  }
  int32_t                   startFrameIndex_ = -1;
  int32_t                   frameCount_      = -1;
  int32_t                   index_           = 0;
  std::vector<VMCFrameInfo> framesInfo_;
};

//============================================================================

struct BaseMeshGOPEntry {
  uint8_t              sliceType        = 'P';
  int32_t              POC              = 1;
  int32_t              temporalId       = 0;
  int32_t              numRefPicsActive = 0;
  int32_t              numRefPics       = 0;
  std::vector<int32_t> referencePics;
};

//----------------------------------------------------------------------------

static std::istream&
operator>>(std::istream& in, BaseMeshGOPEntry& entry) {
  in >> entry.sliceType;
  in >> entry.POC;
  in >> entry.temporalId;
  in >> entry.numRefPicsActive;
  in >> entry.numRefPics;
  entry.referencePics.resize(entry.numRefPics);
  for (int32_t i = 0; i < entry.numRefPics; ++i) {
    in >> entry.referencePics[i];
  }
  return in;
}

//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, BaseMeshGOPEntry& entry) {
  const int W = 4;
  out << std::right << entry.sliceType;
  out << std::setw(W) << entry.POC;
  out << std::setw(W) << entry.temporalId;
  out << std::setw(W) << entry.numRefPicsActive;
  out << std::setw(W) << entry.numRefPics;
  for (int i = 0; i < entry.numRefPics; ++i) {
    out << std::setw(W) << entry.referencePics[i];
  }
  return out;
}

//----------------------------------------------------------------------------

std::tuple<int32_t, uint8_t> syntaxForTemporalScalabilityBasemesh(
  int32_t                              decodeOrderIndex,
  int32_t                              frameCount,
  const std::vector<BaseMeshGOPEntry>& GOPList);

int32_t
decodeOrderToFrameOrderBasemesh(int32_t decodeOrderIndex,
                                int32_t frameCount,
                                const std::vector<BaseMeshGOPEntry>& GOPList);

int32_t referenceFrameIndexFromFrameOrderBasemesh(
  int32_t                              frameOrderIndex,
  int32_t                              frameCount,
  const std::vector<BaseMeshGOPEntry>& GOPList);

//============================================================================

struct VMCSubmesh {
  int32_t frameIndex;
  int32_t
    referenceFrameIndex;  //NOTE: the values are absolute frameIndex within the sequence(0...31)
  basemesh::BaseMeshType            submeshType;
  uint32_t                          submeshId_;
  TriangleMesh<MeshType>            base;
  TriangleMesh<MeshType>            reference;
  TriangleMesh<MeshType>            mapped;
  TriangleMesh<MeshType>            decimateTexture;
  TriangleMesh<MeshType>            subdiv;
  TriangleMesh<MeshType>            subdivBeforeOpti;
  std::vector<int32_t>              mapping;
  std::vector<Vec3<MeshType>>       disp;
  std::vector<Vec3<int64_t>>        dispi;
  std::vector<Vec3<int64_t>>        dispRef;
  int                               lodCountRef;
  std::vector<int64_t>              subdivEdges;
  std::vector<SubdivisionLevelInfo> subdivInfoLevelOfDetails;
  std::vector<int32_t>              baseRepVertexIndices;
  TriangleMesh<int32_t>             baseClean;
  std::vector<Vec2<int32_t>>        baseIntegrateIndices;
  int                               baseEdgesCount;
  std::vector<Vec3<int32_t>>        triangleBaseEdge;
  std::vector<int64_t>              subdivTexEdges;
  int                               baseTexEdgesCount;
  std::vector<Vec3<int32_t>>        texTriangleBaseEdge;
  TriangleMesh<double>              rec;
  std::vector<double>               liftingdisplodmean;
  //directional lifting
  std::vector<double>  liftingdispstat;
  std::vector<int32_t> vertexAdjTableMotion;
  std::vector<int32_t> numNeighborsMotion;

  std::vector<int32_t>                                       lastPosInBlock;
  std::vector<std::vector<std::vector<double>>>              qdisplodmean;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>> IQOffset;
  uint32_t                                                   atlPos;

  // texture mapping variables
  std::vector<ConnectedComponent<double>> packedCCList;
  TriangleMesh<MeshType>                  orthoAtlasUVderivationInfo;
};


struct VMCBasemesh {
  int32_t                submeshId_;
  int32_t                frameIndex;
  basemesh::BaseMeshType submeshType;
  std::vector<int32_t>   baseRepVertexIndices;
  std::vector<int32_t>   vertexAdjTableMotion;
  std::vector<int32_t>   numNeighborsMotion;
  uint8_t                positionBitdepth;
  uint8_t                normalBitdepth;
  TriangleMesh<MeshType> base;
};
struct VMCDecMesh {
  TriangleMesh<MeshType>            rec;
  std::vector<Vec3<MeshType>>       disp;
  std::vector<int64_t>              subdivEdges;
  int                               baseEdgesCount;
  std::vector<Vec3<int32_t>>        triangleBaseEdge;
  std::vector<int64_t>              subdivTexEdges;
  int                               baseTexEdgesCount;
  std::vector<Vec3<int32_t>>        texTriangleBaseEdge;
  std::vector<SubdivisionLevelInfo> subdivInfoLevelOfDetails;
};
//============================================================================

struct VMCGroupOfFrames {
  void resize(int32_t frameCount) {
    frames.resize(frameCount);
    for (int32_t i = 0; i < frameCount; i++) { frames[i].frameIndex = i; }
  }

  const VMCSubmesh& frame(int32_t frameIndex) const {
    assert(frameIndex >= 0 && frameIndex < frameCount());
    return frames[frameIndex];
  }

  VMCSubmesh& frame(int32_t frameIndex) {
    assert(frameIndex >= 0 && frameIndex < frameCount());
    return frames[frameIndex];
  }

  int32_t frameCount() const { return int32_t(frames.size()); }

  VMCSubmesh& operator[](int index) { return frames[index]; }

  typename std::vector<VMCSubmesh>::iterator begin() { return frames.begin(); }
  typename std::vector<VMCSubmesh>::iterator end() { return frames.end(); }

  std::vector<VMCSubmesh> frames;
};

//============================================================================
class LiftingTransformParameters {
public:
  LiftingTransformParameters() {}
  ~LiftingTransformParameters() {}

  void setSkipUpdateFlag(bool value) { skipUpdateFlag_ = value; }
  bool getSkipUpdateFlag() { return skipUpdateFlag_; }
  bool getSkipUpdateFlag() const { return skipUpdateFlag_; }

  void setValenceUpdateWeightFlag(bool value) {
    valenceUpdateWeightFlag_ = value;
  }
  bool getValenceUpdateWeightFlag() { return valenceUpdateWeightFlag_; }
  bool getValenceUpdateWeightFlag() const { return valenceUpdateWeightFlag_; }
  //const std::vector<bool>& getValenceUpdateWeightFlag() const{ return valenceUpdateWeightFlag_; }
  //std::vector<bool>& getValenceUpdateWeightFlag() { return valenceUpdateWeightFlag_; }

  void setLiftingUpdateWeight(uint32_t index, double value) {
    updateWeight_[index] = value;
  }
  std::vector<double>& getLiftingUpdateWeight() { return updateWeight_; }

  void setLiftingPredictionWeight(uint32_t index, double value) {
    predWeight_[index] = value;
  }
  std::vector<double>& getLiftingPredictionWeight() { return predWeight_; }

  void setLiftingOffsetValues(int32_t lod, double value) {
    liftingOffsetValues_[lod] = value;
  }
  std::vector<double>& getLiftingOffsetValues() {
    return liftingOffsetValues_;
  }

  void setDirectionalLiftingFlag(bool value) { dirlift_ = value; }
  bool getDirectionalLiftingFlag() { return dirlift_; }
  bool getDirectionalLiftingFlag() const { return dirlift_; }

  void    setDirectionalLiftingScale1(double value) { dirliftScale1_ = value; }
  double& getDirectionalLiftingScale1() { return dirliftScale1_; }
  const double& getDirectionalLiftingScale1() const { return dirliftScale1_; }

  void    setDirectionalLiftingScale2(double value) { dirliftScale2_ = value; }
  double& getDirectionalLiftingScale2() { return dirliftScale2_; }
  const double& getDirectionalLiftingScale2() const { return dirliftScale2_; }

  void    setDirectionalLiftingScale3(double value) { dirliftScale3_ = value; }
  double& getDirectionalLiftingScale3() { return dirliftScale3_; }
  const double& getDirectionalLiftingScale3() const { return dirliftScale3_; }

  //private:
  bool                 skipUpdateFlag_          = 0;
  bool                 valenceUpdateWeightFlag_ = 0;
  std::vector<double>  updateWeight_;
  std::vector<double>  predWeight_;
  std::vector<double>  liftingOffsetValues_;
  std::vector<int32_t> liftingOffsetValuesNumerator_;
  std::vector<int32_t> liftingOffsetValuesDenominator_;
  //directional lifting
  std::vector<double> liftingScaleValues_;
  bool                dirlift_ = 0;
  double              dirliftScale1_;
  double              dirliftScale2_;
  double              dirliftScale3_;
};
struct QuantizationParameters {
  std::vector<std::vector<double>> iscale;
  std::vector<std::vector<double>> lodQp;
  std::vector<uint32_t>            log2InverseScale;
  uint8_t                          lodQuantFlag;
  uint8_t                          directQuantFlag;
  uint16_t                         BitDepthOffset;
};

struct directionalLiftParameters {
  int32_t MeanNumerator_;
  int32_t StdNumerator_;
  int32_t MeanDenominator_;
  int32_t StdDenominator_;
};

//============================================================================
struct lodInfo {
  uint32_t totalSize;
  uint32_t lodIndex;
  uint32_t vertexCount;
  uint32_t startVertexIndex;
  uint32_t endVertexIndex;
  uint32_t blockCount;
  uint32_t startBlockIndex;
  uint32_t endBlockIndex;
};

class Sequence {
public:
  Sequence() {}
  Sequence(const Sequence&) = default;

  Sequence& operator=(const Sequence&) = default;

  ~Sequence() = default;

  MeshSequence<MeshType>&       meshes() { return _meshes; }
  const MeshSequence<MeshType>& meshes() const { return _meshes; }
  const auto& mesh(int32_t frameIndex) const { return _meshes[frameIndex]; }
  auto&       mesh(int32_t frameIndex) { return _meshes[frameIndex]; }

  std::vector<FrameSequence<uint8_t>>& attributes() { return _attributes; }
  FrameSequence<uint8_t>& attribute(int attIdx) { return _attributes[attIdx]; }
  auto&                   attributeFrame(int32_t attIdx, int32_t frameIndex) {
    return _attributes[attIdx][frameIndex];
  }

  const std::vector<FrameSequence<uint8_t>>& attributes() const {
    return _attributes;
  }
  const FrameSequence<uint8_t>& attribute(int attIdx) const {
    return _attributes[attIdx];
  }
  const auto& attributeFrame(int32_t attIdx, int32_t frameIndex) const {
    return _attributes[attIdx][frameIndex];
  }

  void resize(int32_t frameCount, int attrCount) {
    _meshes.resize(frameCount);
    _attributes.resize(attrCount);
    for (auto& att : _attributes)
      att.resize(0, 0, ColourSpace::BGR444p, frameCount);
  }
  const int32_t frameCount() const { return _meshes.frameCount(); }

  bool load(const std::string& meshPath,
            const int32_t      frameStart,
            const int32_t      frameCount) {
    if (!_meshes.load(meshPath, frameStart, frameCount)) return false;
    return true;
  }

  //  bool saveMeshes(const std::string& meshPath,
  //                  const std::string& materialLibPath,
  //                  const int32_t      frameStart) {
  //    std::vector<vmesh::Material<double>> materials;
  //    bool saveTexture = true;
  //    if (meshPath.empty()) { return true; }
  //    if ((!materialLibPath.empty()) && extension(meshPath) == "obj") {
  //      for (int f = 0; f < _meshes.frameCount(); ++f) {
  //        const auto n      = frameStart + f;
  //        auto       strTex = vmesh::expandNum(texturePath, n);
  //        auto       strMat = vmesh::expandNum(materialLibPath, n);
  //        _meshes[f].setMaterialLibrary(vmesh::basename(strMat));
  //        if (_meshes[f].materialNames().size() > 0) {
  //            saveTexture = false;
  //            materials.resize(_meshes[f].materialNames().size());
  //            int texIdx = 0;
  //            for (int matIdx = 0; matIdx < _meshes[f].materialNames().size(); matIdx++) {
  //                materials[matIdx].name = _meshes[f].materialNames()[matIdx];
  //                if (_meshes[f].textureMapUrls()[matIdx] == "")
  //                {
  //                    materials[matIdx].texture = "";
  //                }
  //                else {
  //                    std::string textureName = removeExtension(vmesh::basename(strTex)) + "_" + _meshes[f].textureMapUrls()[matIdx];
  //                    std::string texturePath = removeExtension(strTex) + "_" + _meshes[f].textureMapUrls()[matIdx];
  //                    materials[matIdx].texture = textureName;
  //                    // save the corresponding texture
  //                  _attributes[texIdx++].frame(f).saveImage(texturePath);
  //                }
  //                if (matIdx == 0) {
  //                    std::cout << "Saving material file:" << strMat << std::endl;
  //                    if (!materials[matIdx].save(strMat)) return false;
  //                }
  //                else
  //                {
  //                    std::cout << "Appending material "<< materials[matIdx].name <<" to file:" << strMat << std::endl;
  //                    if (!materials[matIdx].append(strMat)) return false;
  //                }
  //            }
  //        }
  //        else {
  //            materials.resize(1);
  //            materials[0].texture = vmesh::basename(strTex);
  //            if (!materials[0].save(strMat)) return false;
  //        }
  //      }
  //    } else {
  //      for (int f = 0; f < _meshes.frameCount(); ++f) {
  //        const auto n      = frameStart + f;
  //        auto       strTex = vmesh::expandNum(texturePath, n);
  //        _meshes[f].setMaterialLibrary(vmesh::basename(strTex));
  //      }
  //    }
  //    bool retVal = _meshes.save(meshPath, frameStart);
  //    if (saveTexture) {
  //    for (auto& texture : _attributes)
  //        retVal &= texture.save(texturePath, frameStart);
  //    }
  //
  //    return retVal;
  //  }
  bool saveAttributes(const std::string& texturePath,
                      const int32_t      frameStart) {
    bool retVal    = true;
    auto pos       = texturePath.find_last_of(".");
    auto extension = texturePath.substr(pos, texturePath.length() - pos - 1);
    for (size_t attIdx = 0; attIdx < _attributes.size(); attIdx++) {
      std::string attrPath = texturePath.substr(0, pos - 1) + "_attr"
                             + std::to_string(attIdx) + extension;
      retVal &= _attributes[attIdx].save(attrPath, frameStart);
    }
    return retVal;
  }

  bool save(const std::string& meshPath,
            const std::string& texturePath,
            const std::string& materialLibPath,
            const int32_t      frameStart,
            int32_t            threadCount = 1) {
    if (meshPath.empty()) { return true; }
    const auto frameCount = _meshes.frameCount();
    if (frameCount == 0) { return true; }
    const bool writeMaterials =
      !materialLibPath.empty() && extension(meshPath) == "obj";
    bool saveTexture = true;
    if (writeMaterials) {
      for (int32_t f = 0; f < frameCount; ++f) {
        if (!_meshes[f].materialNames().empty()) {
          saveTexture = false;
          break;
        }
      }
    }

    const auto saveFrame = [&](int32_t f) {
      const auto n       = frameStart + f;
      const auto strTex  = vmesh::expandNum(texturePath, n);
      auto&      mesh    = _meshes[f];
      if (writeMaterials) {
        const auto strMat = vmesh::expandNum(materialLibPath, n);
        mesh.setMaterialLibrary(vmesh::basename(strMat));
        std::vector<vmesh::Material<double>> materials;
        if (!mesh.materialNames().empty()) {
          materials.resize(mesh.materialNames().size());
          size_t texIdx = 0;
          for (size_t matIdx = 0; matIdx < mesh.materialNames().size();
               ++matIdx) {
            materials[matIdx].name = mesh.materialNames()[matIdx];
            if (mesh.textureMapUrls()[matIdx].empty()) {
              materials[matIdx].texture = "";
            } else {
              const auto textureName =
                removeExtension(vmesh::basename(strTex)) + "_"
                + mesh.textureMapUrls()[matIdx];
              const auto frameTexturePath =
                removeExtension(strTex) + "_" + mesh.textureMapUrls()[matIdx];
              materials[matIdx].texture = textureName;
              if (!_attributes.empty()) {
                _attributes[texIdx++].frame(f).saveImage(frameTexturePath);
              }
            }
            if (matIdx == 0) {
              std::cout << "Saving material file:" << strMat << std::endl;
              if (!materials[matIdx].save(strMat)) { return false; }
            } else {
              std::cout << "Appending material " << materials[matIdx].name
                        << " to file:" << strMat << std::endl;
              if (!materials[matIdx].append(strMat)) { return false; }
            }
          }
        } else {
          materials.resize(1);
          materials[0].texture = vmesh::basename(strTex);
          if (!materials[0].save(strMat)) { return false; }
        }
      } else {
        mesh.setMaterialLibrary(vmesh::basename(strTex));
      }

      if (!mesh.save(vmesh::expandNum(meshPath, n))) { return false; }
      if (!saveTexture) { return true; }
      if (_attributes.size() == 1) {
        if (!_attributes[0].frame(f).saveImage(strTex)) { return false; }
      } else if (!_attributes.empty()) {
        auto pos       = texturePath.find_last_of(".");
        auto extension = texturePath.substr(pos, texturePath.length() - pos);
        for (size_t attIdx = 0; attIdx < _attributes.size(); attIdx++) {
          std::string attrPath = texturePath.substr(0, pos) + "_attr"
                                 + std::to_string(attIdx) + extension;
          if (!_attributes[attIdx].frame(f).saveImage(
                vmesh::expandNum(attrPath, n))) {
            return false;
          }
        }
      }
      return true;
    };

    if (threadCount == 0) {
      constexpr int32_t kAutomaticWorkerLimit = 8;
      threadCount = std::min(
        kAutomaticWorkerLimit,
        std::max(1, int32_t(std::thread::hardware_concurrency())));
    }
    threadCount = std::max(1, std::min(threadCount, frameCount));
    if (threadCount == 1) {
      for (int32_t f = 0; f < frameCount; ++f) {
        if (!saveFrame(f)) { return false; }
      }
      return true;
    }

    // Frames write to distinct paths, so the serial file contents are
    // preserved while independent frame exports run concurrently.
    std::atomic<int32_t> nextFrame(0);
    std::atomic<bool>    success(true);
    const auto worker = [&]() {
      for (;;) {
        const auto f = nextFrame.fetch_add(1, std::memory_order_relaxed);
        if (f >= frameCount) { break; }
        if (!saveFrame(f)) { success.store(false, std::memory_order_relaxed); }
      }
    };
    std::vector<std::thread> workers;
    workers.reserve(threadCount - 1);
    for (int32_t i = 1; i < threadCount; ++i) {
      workers.emplace_back(worker);
    }
    worker();
    for (auto& thread : workers) { thread.join(); }
    return success.load(std::memory_order_relaxed);
  }

  bool checkUVranges() {
    for (int f = 0; f < _meshes.frameCount(); ++f) {
      auto texCoordBBox = _meshes[f].texCoordBoundingBox();
      if (texCoordBBox.min[0] < 0) {
        std::cout << "UVerror : texCoordBBox.min[0] < 0." << std::endl;
      }
      if (texCoordBBox.min[1] < 0) {
        std::cout << "UVerror : texCoordBBox.min[1] < 0." << std::endl;
      }
      if (texCoordBBox.max[0] > 1) {
        std::cout << "UVerror : texCoordBBox.max[0] > 1." << std::endl;
      }
      if (texCoordBBox.max[1] > 1) {
        std::cout << "UVerror : texCoordBBox.max[1] > 1." << std::endl;
      }
    }
    return true;
  }

private:
  MeshSequence<MeshType>              _meshes;
  std::vector<FrameSequence<uint8_t>> _attributes;
};
//static std::pair<int32_t, int32_t>
//displacementMapping(int32_t v,
//                    int32_t geometryPatchWidthInBlocks,
//                    int32_t geometryVideoBlockSize) {
//  auto       pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
//  const auto blockIndex = v / pixelsPerBlock;  //TODO: [sw] optimize power of 2
//  const auto indexWithinBlock =
//    v - blockIndex * pixelsPerBlock;  //NOTE: to avoid modulo
//  const auto x0 = (blockIndex % geometryPatchWidthInBlocks)
//                  * geometryVideoBlockSize;  //TODO: [sw] optimize power of 2
//  const auto y0 = (blockIndex / geometryPatchWidthInBlocks)
//                  * geometryVideoBlockSize;  //TODO: [sw] optimize power of 2
//  int32_t x = 0;
//  int32_t y = 0;
//  vmesh::computeMorton2D(indexWithinBlock, x, y);
//  assert(x < geometryVideoBlockSize);
//  assert(y < geometryVideoBlockSize);
//  const auto x1 = x0 + x;
//  const auto y1 = y0 + y;
//
//  return std::pair<int32_t, int32_t>(x1, y1);
//}
static int32_t
getLodVertexInfo(std::vector<int32_t>& vertexCountPerLod,
                 std::vector<lodInfo>& lodVertexInfo,
                 uint32_t              numVertex,
                 int32_t               geometryVideoBlockSize,
                 uint32_t              geometryPatchWidthInBlocks,
                 uint32_t              lodPatchesEnable) {
  //allocate packing structures
  auto    pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  int32_t blockCount     = 0;
  lodVertexInfo.resize(vertexCountPerLod.size());
  auto remainingBlocksInRow = geometryPatchWidthInBlocks;
  for (size_t lod = 0; lod < vertexCountPerLod.size(); lod++) {
    int32_t blockShift             = 0;
    lodVertexInfo[lod].lodIndex    = lod;
    lodVertexInfo[lod].totalSize   = numVertex;
    lodVertexInfo[lod].vertexCount = vertexCountPerLod[lod];
    lodVertexInfo[lod].blockCount =
      (vertexCountPerLod[lod] + pixelsPerBlock - 1) / pixelsPerBlock;
    lodVertexInfo[lod].startVertexIndex = 0;
    lodVertexInfo[lod].startBlockIndex  = 0;
    if (lod != 0) {
      if ((lodPatchesEnable == 1)
          && (lodVertexInfo[lod].blockCount > remainingBlocksInRow)) {
        if (blockCount % geometryPatchWidthInBlocks != 0) {
          blockShift = geometryPatchWidthInBlocks
                       - (blockCount % geometryPatchWidthInBlocks);
        }
      }
      lodVertexInfo[lod].startVertexIndex =
        lodVertexInfo[lod - 1].startVertexIndex + vertexCountPerLod[lod - 1];
      lodVertexInfo[lod].startBlockIndex +=
        lodVertexInfo[lod - 1].startBlockIndex
        + lodVertexInfo[lod - 1].blockCount + blockShift;
    }
    lodVertexInfo[lod].endVertexIndex =
      lod == (vertexCountPerLod.size() - 1)
        ? (int32_t)(numVertex - 1)
        : (lodVertexInfo[lod + 1].startVertexIndex - 1);
    blockCount += lodVertexInfo[lod].blockCount + blockShift;
    remainingBlocksInRow =
      (geometryPatchWidthInBlocks - (blockCount % geometryPatchWidthInBlocks));
  }

  return blockCount;
}

static void
getDisplacementToPixelPerLod(
  std::vector<std::pair<int32_t, int32_t>>& dispToPixPos,
  int32_t                                   lodIndex,
  uint32_t                                  posX,
  uint32_t                                  posY,
  uint32_t                                  sizeX,
  uint32_t                                  sizeY,
  std::vector<lodInfo>&                     lodVertexInfo,
  const int32_t                             geometryVideoBlockSize,
  bool                                      displacementReversePacking) {
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  const auto sizeXInBlocks  = (sizeX - 1) / geometryVideoBlockSize + 1;
  const auto sizeYInBlocks  = (sizeY - 1) / geometryVideoBlockSize + 1;

  dispToPixPos.resize(lodVertexInfo[lodIndex].vertexCount);
  const uint32_t totalBlocksInPatch = sizeXInBlocks * sizeYInBlocks;

  for (int32_t v = 0; v < lodVertexInfo[lodIndex].vertexCount; ++v) {
    if (v > lodVertexInfo[lodIndex].vertexCount) {
      printf("lod[%d] vertexIndex(%d(=%d in Lod)) is out of range[%d,%d]\n",
             lodIndex,
             v,
             lodVertexInfo[lodIndex].startVertexIndex,
             lodVertexInfo[lodIndex].endVertexIndex);
      exit(-5);
    }

    uint32_t blockIndexInPatch = v / pixelsPerBlock;
    auto     vIndexWithinBlock = v % pixelsPerBlock;

    if (displacementReversePacking) {
      blockIndexInPatch = totalBlocksInPatch - 1 - blockIndexInPatch;
      vIndexWithinBlock = pixelsPerBlock - 1 - vIndexWithinBlock;
    }

    if (blockIndexInPatch < 0) {
      printf("lod[%d] v[%d] blockIndex:%d\n", lodIndex, v, blockIndexInPatch);
      exit(-5);
    }
    const auto x0 =
      (blockIndexInPatch % sizeXInBlocks) * geometryVideoBlockSize;
    const auto y0 =
      (blockIndexInPatch / sizeXInBlocks) * geometryVideoBlockSize;
    int32_t x = 0;
    int32_t y = 0;
    computeMorton2D(vIndexWithinBlock, x, y);
    assert(x < geometryVideoBlockSize);
    assert(y < geometryVideoBlockSize);
    const auto x1 = posX + x0 + x;
    const auto y1 = posY + y0 + y;

    dispToPixPos[v] = std::pair<int32_t, int32_t>(x1, y1);
  }
}

static void
getDisplacementToPixel(std::vector<std::pair<int32_t, int32_t>>& dispToPixPos,
                       int32_t                                   frameIndex,
                       int32_t                                   tileIndex,
                       int32_t                                   patchIndex,
                       int32_t                                   submeshId,
                       uint32_t                                  sizeX,
                       uint32_t                                  sizeY,
                       uint32_t                                  posX,
                       uint32_t                                  posY,
                       uint32_t                                  numVertex,
                       std::vector<lodInfo>&                     lodVertexInfo,
                       const int32_t geometryVideoBlockSize,
                       bool          displacementReversePacking,
                       int32_t       videoWidth,
                       int32_t       videoHeight) {
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  printf("(getDisplacementToPixel) frame [%d] tile [%d] patch [%d] "
         "position: (%03d, %03d)\t size %dx%d submeshId: %d\tvideoSize: "
         "%dx%d\tdisp.size() %zu\n",
         frameIndex,
         tileIndex,
         patchIndex,
         posX,
         posY,
         sizeX,
         sizeY,
         submeshId,
         videoWidth,
         videoHeight,
         numVertex);

  fflush(stdout);
  uint32_t geometryPatchWidthInBlocks =
    (uint32_t)std::ceil((double)sizeX / (double)geometryVideoBlockSize);

  dispToPixPos.resize(numVertex);
  const int32_t totalBlocksInPatch = ((int32_t)sizeX * sizeY) / pixelsPerBlock;
  //  printf("\t(createDisplacementVideoFrame) blockCount:%d totalBlocksInPatch:%d origHeight:%d displacementVideoHeightInBlocks:%d geometryPatchWidthInBlocks:%d\n",
  //         blockCount, totalBlocksInPatch, origHeight, displacementVideoHeightInBlocks, geometryPatchWidthInBlocks);
  //  fflush(stdout);
  auto numLods = lodVertexInfo.size();
  for (int32_t lodIdx = 0; lodIdx < numLods; lodIdx++) {
    for (int32_t v = 0; v < lodVertexInfo[lodIdx].vertexCount; ++v) {
      auto vIndexInDisp = v + lodVertexInfo[lodIdx].startVertexIndex;
      if (vIndexInDisp > lodVertexInfo[lodIdx].endVertexIndex) {
        printf("lod[%d] vertexIndex(%d(=%d in Lod)) is out of range[%d,%d]\n",
               lodIdx,
               vIndexInDisp,
               v,
               lodVertexInfo[lodIdx].startVertexIndex,
               lodVertexInfo[lodIdx].endVertexIndex);
        exit(-5);
      }
      int32_t blockIndex =
        v / pixelsPerBlock + lodVertexInfo[lodIdx].startBlockIndex;
      auto indexWithinBlock = v % pixelsPerBlock;

      if (displacementReversePacking) {
        blockIndex       = totalBlocksInPatch - 1 - blockIndex;
        indexWithinBlock = pixelsPerBlock - 1 - indexWithinBlock;
      }
      if (blockIndex < 0) {
        printf("lod[%d] v[%d] vInDisp[%d] blockIndex:%d\n",
               lodIdx,
               v,
               vIndexInDisp,
               blockIndex);
        exit(-5);
      }
      const auto x0 =
        (blockIndex % geometryPatchWidthInBlocks) * geometryVideoBlockSize;
      const auto y0 =
        (blockIndex / geometryPatchWidthInBlocks) * geometryVideoBlockSize;
      int32_t x = 0;
      int32_t y = 0;
      computeMorton2D(indexWithinBlock, x, y);
      assert(x < geometryVideoBlockSize);
      assert(y < geometryVideoBlockSize);
      const auto x1 = posX + x0 + x;
      const auto y1 = posY + y0 + y;

      dispToPixPos[vIndexInDisp] = std::pair<int32_t, int32_t>(x1, y1);
    }
  }
}

static int32_t
reconstructDisplacementFromVideoFramemPerLod(
  const Frame<uint16_t>&       dispVideoFrame,
  uint32_t                     subPatchVertexCount,
  std::vector<Vec3<MeshType>>& disp,
  uint32_t                     sizeX,
  uint32_t                     sizeY,
  uint32_t                     posX,
  uint32_t                     posY,
  const int32_t                geometryVideoBlockSize,
  const int32_t                geometryVideoBitDepth,
  const bool                   oneDimensionalDisplacement,
  const int32_t                displacementReversePacking,
  const ColourSpace            displacementVideoChromaFormat) {
  uint32_t startDispIndex = disp.size();
  disp.resize(startDispIndex + subPatchVertexCount, Vec3<double>(0));

  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  const auto shift          = uint16_t((1 << geometryVideoBitDepth) >> 1);
  const int32_t  dispDim    = oneDimensionalDisplacement ? 1 : 3;
  const auto     sizeXInBlocks      = (sizeX - 1) / geometryVideoBlockSize + 1;
  const auto     sizeYInBlocks      = (sizeY - 1) / geometryVideoBlockSize + 1;
  const uint32_t totalBlocksInPatch = sizeXInBlocks * sizeYInBlocks;

  for (int32_t v = 0; v < subPatchVertexCount; ++v) {
    int32_t blockIndexInPatch = v / pixelsPerBlock;
    auto    vIndexWithinBlock = v % pixelsPerBlock;

    if (displacementReversePacking) {
      blockIndexInPatch = totalBlocksInPatch - 1 - blockIndexInPatch;
      vIndexWithinBlock = pixelsPerBlock - 1 - vIndexWithinBlock;
    }

    if (blockIndexInPatch < 0) {
      printf("v[%d] blockIndex:%d\n", v, blockIndexInPatch);
      exit(-5);
    }

    const auto x0 =
      (blockIndexInPatch % sizeXInBlocks) * geometryVideoBlockSize;
    const auto y0 =
      (blockIndexInPatch / sizeXInBlocks) * geometryVideoBlockSize;
    int32_t x = 0;
    int32_t y = 0;
    computeMorton2D(vIndexWithinBlock, x, y);
    assert(x < geometryVideoBlockSize);
    assert(y < geometryVideoBlockSize);
    int32_t x1 = posX + x0 + x;
    int32_t y1 = posY + y0 + y;

    auto& d = disp[startDispIndex + v];
    for (int32_t p = 0; p < dispDim; ++p) {
      if (displacementVideoChromaFormat == ColourSpace::YUV420p
          || displacementVideoChromaFormat == ColourSpace::YUV422p) {
        const auto& plane = dispVideoFrame.plane(0);
        d[p]              = double(plane.get(y1, x1)) - shift;
      } else {
        const auto& plane = dispVideoFrame.plane(p);
        d[p]              = double(plane.get(y1, x1)) - shift;
      }
    }
  }

  return 0;
}

static int32_t
reconstructDisplacementFromVideoFrame(
  const Frame<uint16_t>&        dispVideoFrame,
  std::vector<int32_t>&         vertexCountPerLod,
  std::vector<Vec3<MeshType>>&  disp,
  const TriangleMesh<MeshType>& rec,
  int32_t                       frameIndex,
  int32_t                       tileIndex,
  int32_t                       patchIndex,
  int32_t                       submeshId,
  uint32_t                      sizeX,
  uint32_t                      sizeY,
  uint32_t                      posX,
  uint32_t                      posY,
  const int32_t                 geometryVideoBlockSize,
  const int32_t                 geometryVideoBitDepth,
  const bool                    oneDimensionalDisplacement,
  const int32_t                 displacementReversePacking,
  const ColourSpace             displacementVideoChromaFormat) {
  printf("Reconstruct displacements from video frame \n");
  fflush(stdout);

  const uint32_t geometryPatchWidthInBlocks =
    (uint32_t)std::ceil((double)sizeX / (double)geometryVideoBlockSize);
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  const auto shift          = uint16_t((1 << geometryVideoBitDepth) >> 1);
  const auto pointCount     = rec.pointCount();
  int32_t    lodExtraPixels = 0;
  int        numLods        = (int)vertexCountPerLod.size();

  int32_t              blockCount = 0;
  std::vector<lodInfo> lodVertexInfo;
  blockCount = getLodVertexInfo(vertexCountPerLod,
                                lodVertexInfo,
                                pointCount,
                                geometryVideoBlockSize,
                                geometryPatchWidthInBlocks,
                                0);
  const int32_t origHeightInBlocks =
    (blockCount + geometryPatchWidthInBlocks - 1) / geometryPatchWidthInBlocks;
  const int32_t origHeight = origHeightInBlocks * geometryVideoBlockSize;
  const int32_t totalBlocksInPatch = ((int32_t)sizeX * sizeY) / pixelsPerBlock;
  //  printf("\t(createDisplacementVideoFrame) blockCount:%d totalBlocksInPatch:%d origHeight:%d displacementVideoHeightInBlocks:%d geometryPatchWidthInBlocks:%d\n",
  //         blockCount, totalBlocksInPatch, origHeight, displacementVideoHeightInBlocks, geometryPatchWidthInBlocks);
  //  fflush(stdout);

  printf("(Reconstruct displacements) frame[%d] tile[%d] patch[%d] "
         "sumbeshId[%d] blockCount: %d "
         "submeshOrigHeight: %d patchSize: %dx%d\tposition: (%d, "
         "%d)\timageSize: %dx%d\n",
         frameIndex,
         tileIndex,
         patchIndex,
         submeshId,
         blockCount,
         origHeight,
         sizeX,
         sizeY,
         posX,
         posY,
         dispVideoFrame.width(),
         dispVideoFrame.height());
  const int32_t dispDim = oneDimensionalDisplacement ? 1 : 3;
  disp.assign(pointCount, Vec3<double>(0));

  std::vector<std::pair<int32_t, int32_t>> dispToPixPos;
  getDisplacementToPixel(dispToPixPos,
                         frameIndex,
                         tileIndex,
                         patchIndex,
                         submeshId,
                         sizeX,
                         origHeight,
                         posX,
                         posY,
                         pointCount,
                         lodVertexInfo,
                         geometryVideoBlockSize,
                         displacementReversePacking,
                         dispVideoFrame.width(),
                         dispVideoFrame.height());

  for (int32_t v = 0; v < pointCount; v++) {
    auto  x1 = dispToPixPos[v].first;
    auto  y1 = dispToPixPos[v].second;
    auto& d  = disp[v];
    for (int32_t p = 0; p < dispDim; ++p) {
      if (displacementVideoChromaFormat == ColourSpace::YUV420p
          || displacementVideoChromaFormat == ColourSpace::YUV422p) {
        const auto& plane = dispVideoFrame.plane(0);
        auto        posY  = p * origHeight + y1;
        d[p]              = double(plane.get(posY, x1)) - shift;
      } else {
        const auto& plane = dispVideoFrame.plane(p);
        d[p]              = double(plane.get(y1, x1)) - shift;
      }
    }
  }
  return 0;
}

//----------------------------------------------------------------------------

static int32_t
subdivideBaseMesh(
  VMCSubmesh&                     frame,
  int32_t                         frameIndex,
  int32_t                         submeshIndex,
  TriangleMesh<MeshType>&         rec,
  std::vector<SubdivisionMethod>* subdivisionMethod,
  const int32_t                   subdivisionIterationCount,
  const int32_t                   edgeLengthThreshold          = 0,
  const int32_t                   bitshiftEdgeBasedSubdivision = 0,
  bool                            interpolateNormals           = false,
  std::vector<int32_t>*           submeshFrameLodMap           = nullptr,
  std::vector<int32_t>*           submeshFrameChildToParentMap = nullptr) {
  printf("(Subdivide) basemesh frame[ %d ] submesh[ %d ] it: %d\n",
         frameIndex,
         submeshIndex,
         subdivisionIterationCount);
  fflush(stdout);
  auto& infoLevelOfDetails  = frame.subdivInfoLevelOfDetails;
  auto& subdivEdges         = frame.subdivEdges;
  auto& subdivTexEdges      = frame.subdivTexEdges;
  int*  baseEdgesCount      = &frame.baseEdgesCount;
  int*  baseTexEdgesCount   = &frame.baseTexEdgesCount;
  auto& triangleBaseEdge    = frame.triangleBaseEdge;
  auto& texTriangleBaseEdge = frame.texTriangleBaseEdge;
  rec                       = frame.base;
  for (int32_t it = 0; it < subdivisionIterationCount; ++it) {
    if ((*subdivisionMethod)[it] >= SubdivisionMethod::METHOD_NUM) return -1;
  }
  if (interpolateNormals) { rec.computeNormals(); }
  rec.subdivideMesh(subdivisionIterationCount,
                    edgeLengthThreshold,
                    bitshiftEdgeBasedSubdivision,
                    subdivisionMethod,
                    &infoLevelOfDetails,
                    &subdivEdges,
                    &subdivTexEdges,
                    nullptr,
                    baseEdgesCount,
                    baseTexEdgesCount,
                    &triangleBaseEdge,
                    &texTriangleBaseEdge,
                    submeshFrameLodMap,
                    submeshFrameChildToParentMap);

  if (interpolateNormals) {
    std::cout << "interpolate subdivided normals on edges" << std::endl;
    rec.resizeNormals(rec.pointCount());
    interpolateSubdivision(
      rec.normals(), infoLevelOfDetails, subdivEdges, 0.5, 0.5, true);
  }
  return 0;
}

//----------------------------------------------------------------------------

static int32_t
applyDisplacements(
  const std::vector<Vec3<MeshType>>&  disp,
  const int32_t                       bitDepthPosition,
  TriangleMesh<MeshType>&             rec,
  const DisplacementCoordinateSystem& displacementCoordinateSystem,
  bool                                interpolateNormals) {
  printf("(apply displacements) : displacementCoordinateSystem %d\n",
         (int)displacementCoordinateSystem);
  fflush(stdout);

  if (!interpolateNormals) {
    printf("(apply displacements) : creating the normals\n");
    fflush(stdout);
    rec.resizeNormals(rec.pointCount());
    rec.computeNormals();
  } else {
    printf("(apply displacements) : use subdivision interpolated normals\n");
    fflush(stdout);
  }

#if DEBUG_INTERPOLATED_NORMALS
  int  vindex = rec.pointCount() / 2;
  auto n      = rec.normal(vindex);
  std::cout << "VMC interpolated Normals before applying disp at " << vindex
            << " n[0]=" << n[0] << " n[1]=" << n[1] << " n[2]=" << n[2]
            << std::endl;
#endif
  //const auto& disp = frame.disp;
  for (int32_t v = 0, vcount = rec.pointCount(); v < vcount; ++v) {
    const auto& d = disp[v];
    if (displacementCoordinateSystem == DisplacementCoordinateSystem::LOCAL) {
      const auto     n = rec.normal(v);
      Vec3<MeshType> t{};
      Vec3<MeshType> b{};
      computeLocalCoordinatesSystem(n, t, b);
      rec.point(v) += d[0] * n + d[1] * t + d[2] * b;
    } else {
      rec.point(v) += d;
    }

    //clip
    for (int32_t i = 0; i < 3; ++i) {
      if (rec.point(v)[i] < 0.0) {
        //std::cout << "Value is less than minimum." << std::endl;
        rec.point(v)[i] = 0;
      } else if (rec.point(v)[i] >= (1 << bitDepthPosition)) {
        //std::cout << "Value is bigger than maximum." << std::endl;
        rec.point(v)[i] = (1 << bitDepthPosition) - 1;
      }
    }
  }
  return 0;
}
//Inverse quantization offset computation

static int32_t
computeInverseQuantizationOffset(
  VMCSubmesh                       frame,
  std::vector<std::vector<double>> iscale,
  uint8_t                          dispDimension,
  const std::vector<double>&       liftingLog2LevelOfDetailInverseScale,
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>&
    InverseQuantizationOffsetValues) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  auto disp = frame.disp;
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto vcount1 = infoLevelOfDetails[it].pointCount;
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = disp[v];
      for (int32_t k = 0; k < dispDimension; ++k) { d[k] *= iscale[it][k]; }
    }
    vcount0 = vcount1;
  }

  std::vector<std::vector<std::vector<int32_t>>> count_zone;
  count_zone.resize(lodCount);
  double ilodScale[3];

  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto vcount1 = infoLevelOfDetails[it].pointCount;
    std::vector<std::vector<double>>              liftingBias_offset;
    std::vector<std::vector<std::vector<int8_t>>> liftingBias_offset_int;

    liftingBias_offset.resize(dispDimension, {0.0, 0.0, 0.0});
    liftingBias_offset_int.resize(dispDimension,
                                  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}});
    count_zone[it].resize(dispDimension, {0, 0, 0});

    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto d = disp[v];
      for (int32_t k = 0; k < dispDimension; ++k) {
        int32_t m = (d[k] == 0) ? 0 : (d[k] > 0.0 ? 1 : 2);
        liftingBias_offset[k][m] += d[k];
        count_zone[it][k][m]++;
      }
    }
    vcount0 = vcount1;
    for (int32_t k = 0; k < dispDimension; ++k) {
      ilodScale[k] = liftingLog2LevelOfDetailInverseScale[k];
      for (int l = 0; l < 3; ++l) {
        liftingBias_offset[k][l] /= count_zone[it][k][l];
        liftingBias_offset[k][l] =
          frame.qdisplodmean[it][k][l] - liftingBias_offset[k][l];
        liftingBias_offset[k][l] *= pow(1. / ilodScale[k], it);
        //Encoding optimization : Per LOD adaptive downscale the offset functionality to avoid over-correction by the offset in high LODs
        liftingBias_offset_int[k][l][0] =
          liftingBias_offset[k][l] > 0.0 ? 0 : 1;  // determine sign
        liftingBias_offset_int[k][l][1] = -std::floor(std::log2(
          liftingBias_offset[k][l] > 0.0
            ? liftingBias_offset[k][l]
            : -liftingBias_offset
                [k]
                [l]));  // determine the first log(2) precision for the fractional part
        liftingBias_offset_int[k][l][2] = -std::floor(std::log2(
          liftingBias_offset[k][l] > 0.0
            ? (liftingBias_offset[k][l]
               - pow(2, -liftingBias_offset_int[k][l][1]))
            : (-liftingBias_offset[k][l]
               - pow(
                 2,
                 -liftingBias_offset_int
                   [k][l]
                   [1]))));  // determine the second log(2) precision for the fractional part
      }
    }
    InverseQuantizationOffsetValues.push_back(liftingBias_offset_int);
  }
  for (int32_t it = lodCount - 1; it >= 1; --it) {
    for (int32_t k = 0; k < dispDimension; ++k) {
      for (int32_t l = 0; l < 3; l++) {
        for (int32_t m = 1; m < 3; m++) {
          InverseQuantizationOffsetValues[it][k][l][m] =
            InverseQuantizationOffsetValues[it][k][l][m]
            - InverseQuantizationOffsetValues
              [it - 1][k][l]
              [m];  // apply Intra prediction for LOD1 and higher levels
        }
      }
    }
  }

  return 0;
}
//----------------------------------------------------------------------------
static int32_t
inverseQuantizeDisplacements(
  std::vector<Vec3<MeshType>>&             disp,
  const std::vector<SubdivisionLevelInfo>& infoLevelOfDetails,
  std::vector<std::vector<double>>         iscale,
  uint8_t                                  dispDimension,
  const bool                               InverseQuantizationOffsetFlag,
  const std::vector<std::vector<std::vector<std::vector<int8_t>>>>&
    InverseQuantizationOffsetValues) {
  //const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto lodCount = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  //auto& disp = frame.disp;
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto vcount1 = infoLevelOfDetails[it].pointCount;
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = disp[v];
#if DEBUG_NONE_TRANSFORM
      if (v == vcount0) {
        std::cout << "quantized disp at inverse quant vcount0" << vcount0
                  << "disp " << d[0] << " " << d[1] << " " << d[2]
                  << std::endl;
      }
#endif
      for (int32_t k = 0; k < dispDimension; ++k) { d[k] *= iscale[it][k]; }
#if DEBUG_NONE_TRANSFORM
      if (v == vcount0) {
        std::cout << "dequantized disp at inverse quant vcount0" << vcount0
                  << "disp " << d[0] << " " << d[1] << " " << d[2]
                  << std::endl;
      }
#endif
    }
    vcount0 = vcount1;
  }

  if (InverseQuantizationOffsetFlag) {
    auto IQ_offset = InverseQuantizationOffsetValues;
    for (int32_t it = 1; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimension; ++k) {
        for (int32_t l = 0; l < 3; l++) {
          for (int32_t m = 1; m < 3; m++) {
            IQ_offset[it][k][l][m] =
              IQ_offset[it][k][l][m]
              + IQ_offset
                [it - 1][k][l]
                [m];  // apply Intra prediction for LOD1 and higher levels
          }
        }
      }
    }
    std::vector<std::vector<std::vector<int32_t>>> count_zone_inv;
    count_zone_inv.resize(lodCount);
    for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
      const auto vcount1 = infoLevelOfDetails[it].pointCount;
      for (int32_t v = vcount0; v < vcount1; ++v) {
        auto& d = disp[v];
        for (int32_t k = 0; k < dispDimension; ++k) {
          // Inverse Quantization Offset applied
          int32_t m = (d[k] == 0) ? 0 : (d[k] > 0.0 ? 1 : 2);
          d[k]      = d[k]
                 + ((IQ_offset[it][k][m][0] == 0) ? 1 : (-1))
                     * (pow(2, -IQ_offset[it][k][m][1])
                        + pow(2, -IQ_offset[it][k][m][2]));
        }
      }
      vcount0 = vcount1;
    }
  }
  return 0;
}
static int32_t
inverseQuantizeDisplacements(
  VMCSubmesh&                      frame,
  std::vector<std::vector<double>> iscale,
  uint8_t                          dispDimension,
  const bool                       InverseQuantizationOffsetFlag,
  const std::vector<std::vector<std::vector<std::vector<int8_t>>>>&
    InverseQuantizationOffsetValues) {
  inverseQuantizeDisplacements(frame.disp,
                               frame.subdivInfoLevelOfDetails,
                               iscale,
                               dispDimension,
                               InverseQuantizationOffsetFlag,
                               InverseQuantizationOffsetValues);
  return 0;
}
//============================================================================
static int32_t
reuseRepresentVertices(
  std::vector<int32_t>&   baseDuplicatedPair,  //baseRepVertexIndices,
  TriangleMesh<MeshType>& base,
  std::vector<int32_t>&   prevBaseRepVertexIndices,
  std::vector<int32_t>&   duplicateVertexList) {
  auto&      vertexList  = base.points();
  const auto pointCount0 = int32_t(base.pointCount());

  baseDuplicatedPair.resize(base.pointCount());
  // copy the duplicated indices
  baseDuplicatedPair = prevBaseRepVertexIndices;
  duplicateVertexList.clear();
  for (int32_t vindex0 = 0; vindex0 < pointCount0; ++vindex0) {
    if (baseDuplicatedPair[vindex0] != vindex0) {
      duplicateVertexList.push_back(vindex0);
    }
  }
  return 0;
}

//============================================================================
static int32_t
findRepresentVerticesSimple(
  std::vector<int32_t>&   baseDuplicatedPair,   //baseRepVertexIndices,
  std::vector<int32_t>&   duplicateVertexList,  //duplicatedVertexIndices,
  TriangleMesh<MeshType>& base) {
  auto&      vertexList  = base.points();
  const auto pointCount0 = int32_t(base.pointCount());

  baseDuplicatedPair.resize(base.pointCount());
  std::unordered_map<Vec3<MeshType>, int32_t, HashVector3<MeshType>>
          uniquePoints;
  int32_t uniquePointCount = 0;
  duplicateVertexList.clear();
  for (int32_t vindex0 = 0; vindex0 < pointCount0; ++vindex0) {
    int32_t vertexIndex = 0;
    auto    point       = vertexList[vindex0];
    if (uniquePoints.find(point) == uniquePoints.end()) {  //new
      uniquePoints[point] = vindex0;  //newMesh.pointCount()-1;
      vertexIndex         = vindex0;
      uniquePointCount++;
    } else {
      vertexIndex = uniquePoints[point];
      duplicateVertexList.push_back(vindex0);
    }
    baseDuplicatedPair[vindex0] = vertexIndex;
  }
  return uniquePointCount;
}
void adjustTextureCoordinates(TriangleMesh<MeshType>& submesh,
                              int32_t                 subTextureWidth,
                              int32_t                 subTextureHeight,
                              int32_t                 subTextureLTx,
                              int32_t                 subTextureLTy,
                              int32_t                 textureVideoWidth,
                              int32_t                 textureVideoHeight);

//============================================================================
// ZIPPERING FUNCTIONS

void zippering_find_borders(std::vector<TriangleMesh<MeshType>>& submeshes,
                            std::vector<std::vector<int8_t>>& isBoundaryVertex,
                            std::vector<size_t>&              numBoundaries,
                            TriangleMesh<MeshType>& boundaryVertices);
void zippering_find_matches_distance_matrix(
  std::vector<TriangleMesh<MeshType>>& submeshes,
  TriangleMesh<MeshType>&              boundaryVertices,
  std::vector<size_t>&                 numBoundaries,
  std::vector<std::vector<int64_t>>&
    zipperingDistanceBorderPoint,  //[submesh][borderpoint] - input
  std::vector<std::vector<Vec2<size_t>>>&
    zipperingMatchedBorderPoint  // [submesh][borderpoint] - output
);
void zippering_find_matches_distance_per_submesh_pair(
  std::vector<TriangleMesh<MeshType>>&    submeshes,
  TriangleMesh<MeshType>&                 boundaryVertices,
  std::vector<size_t>&                    numBoundaries,
  std::vector<std::vector<int64_t>>&      zipperingDistancePerSubmeshPair,
  std::vector<std::vector<Vec2<size_t>>>& zipperingMatchedBorderPoint,
  bool                                    zipperingLinearSegmentation);

void zippering_fuse_border(
  std::vector<TriangleMesh<MeshType>>&    submeshes,
  std::vector<std::vector<int8_t>>&       isBoundaryVertex,
  std::vector<std::vector<Vec2<size_t>>>& zipperingMatchedBorderPoint,
  const std::vector<std::vector<int32_t>> frameSubmeshLodMaps,
  const int                               zipperingMethodForUnmatchedLoDs = 0);
void boundaryprocess(
  std::vector<TriangleMesh<MeshType>>&           submeshes,
  std::vector<std::vector<int8_t>>&              isBoundaryVertex,
  std::vector<std::vector<std::vector<int>>>&    growuplist_adjacent,
  std::vector<std::vector<std::vector<int>>>&    triangle_grow,
  std::vector<size_t>&                           crackCount,
  std::vector<std::vector<std::vector<size_t>>>& boundaryIndex,
  std::vector<std::vector<size_t>>&              crackIndex);
void
boundaryupdate(std::vector<TriangleMesh<MeshType>>&        submeshes,
               std::vector<std::vector<int8_t>>            isBoundaryVertex,
               std::vector<std::vector<std::vector<int>>>& growuplist_adjacent,
               std::vector<std::vector<std::vector<int>>>& triangle_grow,
               std::vector<std::vector<std::vector<size_t>>>& boundaryIndex,
               std::vector<size_t>&                           crackCount,
               std::vector<std::vector<size_t>>&              crackIndex);
void
     boundaryfuse(std::vector<TriangleMesh<MeshType>>&        submeshes,
                  std::vector<std::vector<std::vector<int>>>& growuplist_adjacent,
                  std::vector<std::vector<std::vector<int>>>& triangle_grow,
                  std::vector<std::vector<size_t>>&           crackIndex);
void addtexture(std::vector<TriangleMesh<MeshType>>& submeshes,
                std::vector<TriangleMesh<MeshType>>& submeshes_temp);
void setNewMesh(std::vector<TriangleMesh<MeshType>>& submeshes,
                std::vector<TriangleMesh<MeshType>>& submeshes_temp);
//============================================================================
static void
expandMeshDupPoints(vmesh::TriangleMesh<MeshType>&       newMesh,
                    const vmesh::TriangleMesh<MeshType>& srcMesh,
                    const std::vector<int32_t>&          triIndexList,
                    std::vector<int32_t>* vertexMappingPtr = nullptr) {
  std::vector<int32_t> vertexMappingGeo;
  if (vertexMappingPtr != nullptr) { vertexMappingGeo = *vertexMappingPtr; }
  //geometry
  if (srcMesh.points().size() > 0 && srcMesh.triangles().size() > 0) {
    std::unordered_map<int32_t, int32_t> uniquePointsNewMeshGeo;
    for (int32_t ti = 0; ti < triIndexList.size(); ti++) {
      int32_t   triIndex = triIndexList[ti];
      auto      triangle = srcMesh.triangle(triIndex);
      Vec3<int> newTriangle(-1, -1, -1);
      for (int k = 0; k < 3; k++) {
        int32_t newVi = -1;
        int32_t srcVi = triangle[k];
        if (uniquePointsNewMeshGeo.find(srcVi)
            == uniquePointsNewMeshGeo.end()) {
          vertexMappingGeo.push_back(srcVi);
          newVi                         = (int32_t)vertexMappingGeo.size() - 1;
          uniquePointsNewMeshGeo[srcVi] = newVi;
        } else {
          newVi = uniquePointsNewMeshGeo[srcVi];
        }
        newTriangle[k] = newVi;
      }  //k

      newMesh.addTriangle(newTriangle);
    }
    for (int vi = 0; vi < vertexMappingGeo.size(); vi++) {  //new
      auto srcVerIndex = vertexMappingGeo[vi];
      newMesh.addPoint(srcMesh.point(srcVerIndex));
    }
  }
  //texture
  if (srcMesh.texCoords().size() > 0
      && srcMesh.texCoordTriangles().size() > 0) {
    std::vector<int32_t>                 vertexMappingUv;
    std::unordered_map<int32_t, int32_t> uniquePointsNewMeshUv;
    for (int32_t ti = 0; ti < triIndexList.size(); ti++) {
      int32_t   triIndex = triIndexList[ti];
      auto      triangle = srcMesh.texCoordTriangle(triIndex);
      Vec3<int> newTriangle(-1, -1, -1);
      for (int k = 0; k < 3; k++) {
        int32_t newVi = -1;
        int32_t srcVi = triangle[k];
        if (uniquePointsNewMeshUv.find(srcVi) == uniquePointsNewMeshUv.end()) {
          vertexMappingUv.push_back(srcVi);
          newVi                        = (int32_t)vertexMappingUv.size() - 1;
          uniquePointsNewMeshUv[srcVi] = newVi;
        } else {
          newVi = uniquePointsNewMeshUv[srcVi];
        }
        newTriangle[k] = newVi;
      }  //k

      newMesh.addTexCoordTriangle(newTriangle);
    }
    for (int vi = 0; vi < vertexMappingUv.size(); vi++) {  //new
      auto srcVerIndex = vertexMappingUv[vi];
      newMesh.addTexCoord(srcMesh.texCoord(srcVerIndex));
    }
  }  //texcoords

  if (srcMesh.normals().size() > 0 && srcMesh.normalTriangles().size() > 0) {
    std::vector<int32_t>                 vertexMappingNormal;
    std::unordered_map<int32_t, int32_t> uniquePointsNewMeshNormal;
    for (int32_t ti = 0; ti < triIndexList.size(); ti++) {
      int32_t   triIndex = triIndexList[ti];
      auto      triangle = srcMesh.normalTriangle(triIndex);
      Vec3<int> newTriangle(-1, -1, -1);
      for (int k = 0; k < 3; k++) {
        int32_t newVi = -1;
        int32_t srcVi = triangle[k];
        if (uniquePointsNewMeshNormal.find(srcVi)
            == uniquePointsNewMeshNormal.end()) {
          vertexMappingNormal.push_back(srcVi);
          newVi = (int32_t)vertexMappingNormal.size() - 1;
          uniquePointsNewMeshNormal[srcVi] = newVi;
        } else {
          newVi = uniquePointsNewMeshNormal[srcVi];
        }
        newTriangle[k] = newVi;
      }  //k

      newMesh.addNormalTriangle(newTriangle);
    }
    for (int vi = 0; vi < vertexMappingNormal.size(); vi++) {  //new
      auto srcVerIndex = vertexMappingNormal[vi];
      newMesh.addNormal(srcMesh.normal(srcVerIndex));
    }
  }

  //material
  if (srcMesh.materialNames().size() > 1) {
    newMesh.materialNames()   = srcMesh.materialNames();
    newMesh.materialLibrary() = srcMesh.materialLibrary();
    newMesh.textureMapUrls()  = srcMesh.textureMapUrls();
    for (int32_t ti = 0; ti < triIndexList.size(); ti++) {
      int32_t triIndex  = triIndexList[ti];
      auto    newMatIdx = srcMesh.materialIdx(triIndex);
      newMesh.addMaterialIdx(newMatIdx);
    }
  }
}

//============================================================================

typedef std::tuple<std::string, double, double> TicToc;
extern std::vector<TicToc>                      g_ticTocList;

//============================================================================

static void
tic(const std::string& name) {
  auto it = std::find_if(
    g_ticTocList.begin(), g_ticTocList.end(), [&name](const TicToc& element) {
      return std::get<0>(element) == name;
    });
  auto start = vmesh::now();
  if (it != g_ticTocList.end()) {
    std::get<1>(*it) = start;
  } else {
    g_ticTocList.push_back(std::make_tuple(name, start, 0));
  }
}

//============================================================================

static void
toc(const std::string& name) {
  auto it = std::find_if(
    g_ticTocList.begin(), g_ticTocList.end(), [&name](const TicToc& element) {
      return std::get<0>(element) == name;
    });
  if (it != g_ticTocList.end()) {
    auto end      = vmesh::now();
    auto duration = end - std::get<1>(*it);
    std::get<2>(*it) += duration;
    std::cout << "Duration " << std::left << std::setw(25) << name
              << ": time = " << std::right << std::setw(15)
              << duration / 1000.0 << " s Total = " << std::setw(15)
              << std::get<2>(*it) / 1000.0 << "\n";
  } else {
    std::cout << "Duration " << name << ": can't find clock\n";
  }
}
//============================================================================

static void
resetTime(const std::string& name) {
  auto it = std::find_if(
    g_ticTocList.begin(), g_ticTocList.end(), [&name](const TicToc& element) {
      return std::get<0>(element) == name;
    });
  if (it != g_ticTocList.end()) {
    std::get<2>(*it) = std::numeric_limits<double>::max();
  }
}

//============================================================================

static double
getTime(const std::string& name) {
  double duration = -1.0;
  auto   it       = std::find_if(
    g_ticTocList.begin(), g_ticTocList.end(), [&name](const TicToc& element) {
      return std::get<0>(element) == name;
    });
  if (it != g_ticTocList.end())
    duration = (std::get<2>(*it) == std::numeric_limits<double>::max()
                  ? -1.0
                  : std::get<2>(*it) / 1000.0);
  return duration;
}

//============================================================================

static void
traceTime() {
  std::cout << "Duration: \n";
  for (auto& el : g_ticTocList) {
    std::cout << "  " << std::left << std::setw(25) << std::get<0>(el)
              << ": time = " << std::right << std::setw(15)
              << (std::get<2>(el) == std::numeric_limits<double>::max()
                    ? -1.0
                    : std::get<2>(el) / 1000.0)
              << " s \n";
  }
}

//============================================================================
static unsigned
CeilLog2(unsigned uiVal) {
  unsigned uiTmp = uiVal - 1;
  unsigned uiRet = 0;

  while (uiTmp != 0) {
    uiTmp >>= 1;
    uiRet++;
  }
  return uiRet;
}

template<typename T>
inline T
Clip3(const T minVal, const T maxVal, const T a) {
  return std::min<T>(std::max<T>(minVal, a), maxVal);
}  ///< general min/max clip

}  // namespace vmesh
