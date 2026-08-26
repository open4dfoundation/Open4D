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

#include <cmath>
#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>

#include "baseMeshEncoder.hpp"
#include "motionContexts.hpp"
#include "entropy.hpp"
#include "vmc.hpp"
#include "util/misc.hpp"
#include "geometryEncoder.hpp"
#include "util/mesh.hpp"
#include "bitstream.hpp"

namespace basemesh {

//============================================================================
//construct refListStruct in BMSPS from input parameters, params.refFrameDiff
void
BaseMeshEncoder::constructBmspsRefListStruct(
  BaseMeshSequenceParameterSetRbsp& bmsps,
  const BaseMeshEncoderParameters&  params) {
  //refFrameDiff:1,2,3,4...
  size_t numRefList          = params.maxNumRefBmeshList;
  size_t numActiveRefEntries = params.maxNumRefBmeshFrame;
  for (size_t list = 0; list < numRefList; list++) {
    BaseMeshRefListStruct refList;
    refList.setNumRefEntries(numActiveRefEntries);
    refList.allocate();
    for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
      int mfocDiff = -1;
      if (i == 0) {
        //the absolute difference between the foc values of current & ref
        mfocDiff = params.refFrameDiff[0];
      } else {
        //the absolute difference between the foc values ref[i-1] & ref[i]
        mfocDiff = params.refFrameDiff[i] - params.refFrameDiff[i - 1];
      }
      refList.setAbsDeltaMfocSt(i, std::abs(mfocDiff));
      refList.setStrafEntrySignFlag(i, mfocDiff < 0 ? false : true);
      refList.setStRefMeshFrameFlag(i, true);
    }
    bmsps.addBmeshRefListStruct(refList);
  }
}

//============================================================================
void
BaseMeshEncoder::constructBmspsRefListStruct(
  BaseMeshSequenceParameterSetRbsp&           bmsps,
  const std::vector<vmesh::BaseMeshGOPEntry>& GOPList) {
  size_t numRefList = GOPList.size();
  for (size_t list = 0; list < numRefList; ++list) {
    const auto&           referencePics = GOPList[list].referencePics;
    BaseMeshRefListStruct refList;
    refList.setNumRefEntries(referencePics.size());
    refList.allocate();
    for (size_t i = 0; i < refList.getNumRefEntries(); ++i) {
      int mfocDiff =
        (i == 0) ? referencePics[0] : referencePics[i] - referencePics[i - 1];
      refList.setAbsDeltaMfocSt(i, std::abs(mfocDiff));
      refList.setStrafEntrySignFlag(i, mfocDiff < 0 ? false : true);
      refList.setStRefMeshFrameFlag(i, true);
    }
    bmsps.addBmeshRefListStruct(refList);
  }
}

//============================================================================
void
BaseMeshEncoder::createBaseMeshParameterSet(
  BaseMeshSequenceParameterSetRbsp& bmsps,
  BaseMeshFrameParameterSetRbsp&    bmfps,
  const BaseMeshEncoderParameters&  params) {

  // Initialize with the maximum values possible for tier 0 for now
  bmsps.getBmeshProfileTierLevel().setBmptlProfileIdc(BaseMeshProfile::BASEMESH_PROFILE_BASEMESH_MAIN);
  bmsps.getBmeshProfileTierLevel().setBmptlTierFlag(BaseMeshTier::BASEMESH_TIER_0);
  bmsps.getBmeshProfileTierLevel().setBmptlLevelIdc(BaseMeshLevel::BASEMESH_LEVEL_2_0);

  bmsps.setBmspsGeometry3dBitDepthMinus1(params.qpPosition - 1);
  if (!params.basemeshGOPList.empty()) {
    bmsps.setBmspsMaxSubLayersMinus1(log2(params.basemeshGOPSize));
    for (size_t i = 0; i < bmsps.getBmspsMaxSubLayersMinus1() + 1; i++) {
      bmsps.getBmeshProfileTierLevel().setBmptlSubLayerProfilePresentFlag(1,
                                                                          i);
      bmsps.getBmeshProfileTierLevel().setBmptlSubLayerLevelPresentFlag(1, i);
      if (bmsps.getBmeshProfileTierLevel().getBmptlSubLayerProfilePresentFlag(
            i)) {
        bmsps.getBmeshProfileTierLevel().setBmptlSubLayerTierFlag(BaseMeshTier::BASEMESH_TIER_0, i);
        bmsps.getBmeshProfileTierLevel().setBmptlSubLayerProfileIdc(
          BaseMeshProfile::BASEMESH_PROFILE_BASEMESH_MAIN, i);
      }
      if (bmsps.getBmeshProfileTierLevel().getBmptlSubLayerLevelPresentFlag(
            i)) {
        bmsps.getBmeshProfileTierLevel().setBmptlSubLayerLevelIdc(
          BaseMeshLevel::BASEMESH_LEVEL_2_0, i);
      }
    }
  }
  bmsps.setBmspsIntraMeshCodecId(params.meshCodecId);

  uint8_t attributeCount =
    params.encodeNormals + (params.iDeriveTextCoordFromPos < 3);
  if ((params.iDeriveTextCoordFromPos == 0) && (params.numTextures > 1)
      && (params.dracoMeshLossless))
    attributeCount++;  // will send the face ID in the basemesh as well to indicate the texture map index
  bmsps.setBmspsMeshAttributeCount(attributeCount);

  auto attrIdx = 0;
  if (params.iDeriveTextCoordFromPos < 3) {
    if (params.iDeriveTextCoordFromPos == 0) {
      if (params.numTextures > 1 && params.dracoMeshLossless) {
        // basemesh will contain two attributes: ATTR_TEXTCOORD and ATTRIBUTE_FACEGROUPID
        bmsps.setBmspsMeshAttributeTypeId(
          vmesh::MeshAttribueType::ATTRIBUTE_TEXCOORD, attrIdx);
        bmsps.setBmspsAttributeBitDepthMinus1(params.qpTexCoord - 1,
                                              attrIdx++);
        bmsps.setBmspsMeshAttributeTypeId(
          vmesh::MeshAttribueType::ATTRIBUTE_FACEGROUPID, attrIdx);
        bmsps.setBmspsAttributeBitDepthMinus1(
          0, attrIdx);  // to avoid quantization, since values are integers
      } else {
        bmsps.setBmspsMeshAttributeTypeId(
          vmesh::MeshAttribueType::ATTRIBUTE_TEXCOORD, attrIdx);
        bmsps.setBmspsAttributeBitDepthMinus1(params.qpTexCoord - 1, attrIdx);
      }
    } else if (params.iDeriveTextCoordFromPos == 1) {
      bmsps.setBmspsMeshAttributeTypeId(
        vmesh::MeshAttribueType::ATTRIBUTE_TEXCOORD, attrIdx);
      bmsps.setBmspsAttributeBitDepthMinus1(
        0, attrIdx);  // this should be adjusted???
    } else if (params.iDeriveTextCoordFromPos == 2) {
      bmsps.setBmspsMeshAttributeTypeId(
        vmesh::MeshAttribueType::ATTRIBUTE_FACEGROUPID, attrIdx);
      bmsps.setBmspsAttributeBitDepthMinus1(0, attrIdx);
    }
    attrIdx++;
  }
  if (params.encodeNormals) {
    bmsps.setBmspsMeshAttributeTypeId(
      vmesh::MeshAttribueType::ATTRIBUTE_NORMAL, attrIdx);
    bmsps.setBmspsAttributeBitDepthMinus1(params.qpNormals - 1, attrIdx);
    attrIdx++;
  }

  if (params.basemeshGOPList.empty()) {
    constructBmspsRefListStruct(bmsps, params);
  } else {
    constructBmspsRefListStruct(bmsps, params.basemeshGOPList);
  }
  //bmsps.setBmspsInterMeshLog2MotionGroupSize(uint8_t(std::ceil(std::log2(params.motionGroupSize))));
  assert(params.maxNumNeighborsMotion >= 1
         && params.maxNumNeighborsMotion
              <= 8);  // maxNumNeighborsMotion should be in the range of 1 to 8
  bmsps.setBmspsInterMeshMaxNumNeighboursMinus1(params.maxNumNeighborsMotion
                                                - 1);
  //bmsps.setMaxNumMotionVectorPredictor(params.maxNumMotionVectorPredictor);
  if (params.motionWithoutDuplicatedVertices) {
    auto& bmptl = bmsps.getBmeshProfileTierLevel();
    bmptl.setBmptlToolsetConstraintsPresentFlag(true);
    auto& bmptci = bmptl.getBmptlProfileToolsetConstraintsInformation();
    bmptci.setBmptcMotionVectorDerivationDisableFlag(true);
  }
  bmfps.setBfpsMeshSequenceParameterSetId(
    (bmsps.getBmspsSequenceParameterSetId()));
  bmfps.getSubmeshInformation().setBmsiNumSubmeshesMinus1(params.numSubmesh
                                                          - 1);
  bmfps.getSubmeshInformation().setBmsiSignalledSubmeshIdFlag(
    params.enableSignalledIds);
  if (bmfps.getSubmeshInformation().getBmsiSignalledSubmeshIdFlag()) {
    bmfps.getSubmeshInformation()._submeshIndexToID.resize(params.numSubmesh);
    uint32_t maxVal = 0;
    for (int i = 0; i < params.submeshIdList.size(); i++) {
      maxVal = std::max(maxVal, params.submeshIdList[i]);
      bmfps.getSubmeshInformation()._submeshIndexToID[i] =
        params.submeshIdList[i];
    }
    bmfps.getSubmeshInformation()._submeshIDToIndex.resize(maxVal + 1);
    for (int i = 0; i < params.submeshIdList.size(); i++) {
      bmfps.getSubmeshInformation()
        ._submeshIDToIndex[params.submeshIdList[i]] = i;
    }

    //uint32_t v = (uint32_t)ceil(log2(maxVal+1));
    uint32_t v = (maxVal < 2 ? 1 : vmesh::CeilLog2(maxVal + 1));
    uint32_t b = (uint32_t)ceil(
      log2(bmfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1() + 1));
    bmfps.getSubmeshInformation().setBmsiSignalledSubmeshIdDeltaLength(v - b);
  } else {
    bmfps.getSubmeshInformation()._submeshIndexToID.resize(params.numSubmesh);
    bmfps.getSubmeshInformation()._submeshIDToIndex.resize(params.numSubmesh);
    for (int i = 0; i < params.numSubmesh; i++) {
      bmfps.getSubmeshInformation()._submeshIndexToID[i] =
        bmfps.getSubmeshInformation()._submeshIDToIndex[i] = i;
    }
    bmfps.getSubmeshInformation().setBmsiSignalledSubmeshIdDeltaLength(0);
  }
}

//============================================================================
int32_t
BaseMeshEncoder::initializeBaseMeshParameterSets(
  BaseMeshBitstream&               baseMesh,
  const BaseMeshEncoderParameters& params) {
  const uint8_t bmspsId = 0;
  const uint8_t bmfpsId = 0;

  // Mesh sub bitstream
  //  auto& baseMesh = syntax.getBaseMeshDataStream();
  //  baseMesh.setAtlasId(syntax.getAtlasId());
  //  baseMesh.setV3CParameterSetId(syntax.getActiveVpsId());

  // Base mesh sequence parameter set
  auto& bmsps = baseMesh.addBaseMeshSequenceParameterSet(bmspsId);
  auto& bmfps = baseMesh.addBaseMeshFrameParameterSet(bmfpsId);
  createBaseMeshParameterSet(bmsps, bmfps, params);

  return 0;
}

//============================================================================
bool
BaseMeshEncoder::addNeighbor(int32_t               vertex1,
                             int32_t               vertex2,
                             int32_t               maxNumNeighborsMotion,
                             std::vector<int32_t>& vertexAdjTableMotion,
                             std::vector<int32_t>& numNeighborsMotion) {
  bool duplicate      = false;
  auto vertex         = vertex1;
  auto vertexNeighbor = vertex2;
  auto predCount      = numNeighborsMotion[vertex];
  predCount           = std::min(predCount, maxNumNeighborsMotion);
  if (vertex > vertexNeighbor) {  // Check if vertexNeighbor is available
    for (int32_t n = 0; n < predCount; ++n) {
      if (vertexAdjTableMotion[vertex * maxNumNeighborsMotion + n]
          == vertexNeighbor) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      if (predCount == maxNumNeighborsMotion) {
        vertexAdjTableMotion[vertex * maxNumNeighborsMotion
                             + maxNumNeighborsMotion - 1] = vertexNeighbor;
      } else {
        vertexAdjTableMotion[vertex * maxNumNeighborsMotion + predCount] =
          vertexNeighbor;
        numNeighborsMotion[vertex] = predCount + 1;
      }
    }
  }
  duplicate      = false;
  vertex         = vertex2;
  vertexNeighbor = vertex1;
  predCount      = numNeighborsMotion[vertex];
  if (vertex > vertexNeighbor) {  // Check if vertexNeighbor is available
    for (int32_t n = 0; n < predCount; ++n) {
      if (vertexAdjTableMotion[vertex * maxNumNeighborsMotion + n]
          == vertexNeighbor) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      if (predCount == maxNumNeighborsMotion) {
        vertexAdjTableMotion[vertex * maxNumNeighborsMotion
                             + maxNumNeighborsMotion - 1] = vertexNeighbor;
      } else {
        vertexAdjTableMotion[vertex * maxNumNeighborsMotion + predCount] =
          vertexNeighbor;
        numNeighborsMotion[vertex] = predCount + 1;
      }
    }
  }
  return true;
}

//============================================================================
bool
BaseMeshEncoder::computeVertexAdjTableMotion(
  const std::vector<vmesh::Vec3<int32_t>>& triangles,
  int32_t                                  vertexCount,
  int32_t                                  maxNumNeighborsMotion,
  std::vector<int32_t>&                    vertexAdjTableMotion,
  std::vector<int32_t>&                    numNeighborsMotion) {
  const auto triangleCount = int32_t(triangles.size());
  // const auto vertexCount = int32_t(reference.size());
  if (vertexAdjTableMotion.size() < vertexCount * maxNumNeighborsMotion) {
    vertexAdjTableMotion.resize(vertexCount * maxNumNeighborsMotion);
  }
  if (numNeighborsMotion.size() < vertexCount) {
    numNeighborsMotion.resize(vertexCount);
  }
  for (int32_t v = 0; v < vertexCount; ++v) { numNeighborsMotion[v] = 0; }

  for (int32_t triangleIndex = 0; triangleIndex < triangleCount;
       ++triangleIndex) {
    const auto& tri = triangles[triangleIndex];
    assert(tri.i() >= 0 && tri.i() < vertexCount);
    assert(tri.j() >= 0 && tri.j() < vertexCount);
    assert(tri.k() >= 0 && tri.k() < vertexCount);
    // Add Neighbors
    addNeighbor(tri.i(),
                tri.j(),
                maxNumNeighborsMotion,
                vertexAdjTableMotion,
                numNeighborsMotion);
    addNeighbor(tri.i(),
                tri.k(),
                maxNumNeighborsMotion,
                vertexAdjTableMotion,
                numNeighborsMotion);
    addNeighbor(tri.j(),
                tri.k(),
                maxNumNeighborsMotion,
                vertexAdjTableMotion,
                numNeighborsMotion);
  }
  return true;
}

//============================================================================
BaseMeshType
BaseMeshEncoder::chooseSkipOrInter(
  const std::vector<vmesh::Vec3<MeshType>>& current,
  const std::vector<vmesh::Vec3<MeshType>>& reference) {
  const auto                pointCount = int32_t(current.size());
  vmesh::VMCMotionACContext ctx;
  float                     cost_inter = 0.0;
  float                     cost_skip  = 0.0;
  float                     lamda      = 6.25 * pointCount;
  for (int vindex = 0; vindex < pointCount; ++vindex) {
    const auto motion = current[vindex] - reference[vindex];
    for (int32_t k = 0; k < 3; ++k) {
      cost_skip += motion[k] > 0 ? float(motion[k]) : float(-motion[k]);
    }
    const auto res0  = motion;
    const auto bits0 = ctx.estimateBits(res0, 0);
    cost_inter += float(bits0);
  }
  cost_skip *= lamda;
  if (cost_skip > cost_inter) {
    return P_BASEMESH;
  } else {
    return SKIP_BASEMESH;
  }
}

//============================================================================
void
BaseMeshEncoder::encodePredMode(vmesh::EntropyEncoder&     arithmeticEncoder,
                                vmesh::VMCMotionACContext& ctx,
                                int                        mode,
                                int                        maxMode) {
  if (maxMode == 0) return;

  int ctxIdx = 0;
  for (int i = 0; i < mode; i++) {
    arithmeticEncoder.encode(1, ctx.ctxPred[ctxIdx]);
    ctxIdx = 1;
  }

  if (mode != maxMode) arithmeticEncoder.encode(0, ctx.ctxPred[ctxIdx]);
}

//============================================================================
int
BaseMeshEncoder::compressMotion(
  const vmesh::VMCSubmesh&         refFrameInput,
  vmesh::VMCSubmesh&               currentMesh,
  std::vector<int32_t>&            vertexAdjTableMotion,
  std::vector<int32_t>&            numNeighborsMotion,
  BaseMeshSubmeshLayer&            bmsl,
  const BaseMeshEncoderParameters& param,
  int32_t                          frameIndex,
  std::vector<int32_t>&            prevBaseRepVertexIndices) {
  printf("\tcompressMotion \n");
  std::vector<vmesh::Vec3<MeshType>>& current     = currentMesh.base.points();
  const auto                          pointCount  = int32_t(current.size());
  const auto                          maxAcBufLen = pointCount * 3 * 4 + 1024;
  vmesh::VMCMotionACContext           ctx;
  vmesh::EntropyEncoder               arithmeticEncoder;
  arithmeticEncoder.setBuffer(maxAcBufLen, nullptr);
  arithmeticEncoder.start();
  auto&                             reference = refFrameInput.base.points();
  std::vector<vmesh::Vec3<int32_t>> motion(pointCount);
  vmesh::Vec3<double> maxValues((1 << param.qpPosition) - 1);
  vmesh::Vec3<double> minValues(0.0);
  for (int vindex = 0; vindex < pointCount; ++vindex) {
    for (int k = 0; k < 3; k++) {
      current[vindex][k] = vmesh::Clip3(minValues[k], maxValues[k], current[vindex][k]);
    }
    motion[vindex] = vmesh::Vec3<int32_t>(current[vindex] - reference[vindex]);
  }
  const auto        motionCount    = static_cast<int32_t>(motion.size());
  const auto&       refFrame       = refFrameInput;
  int32_t           numFirstVertex = 0;
  int32_t           numDupVertex   = 0;
  int32_t           numDupDerived  = 0;
  int32_t           numDupSiged    = 0;
  std::vector<bool> mvSignalled(pointCount, 1);
  std::vector<bool> mvRealSignalled(pointCount, 1);
  //if (!reuseDuplicatedVertFlag_ || prevBaseRepVertexIndices.empty()) {
  //  findRepresentVerticesSimple(
  //    refFrame.baseRepVertexIndices, duplicateVertexList, refFrame.base);
  //  prevBaseRepVertexIndices.resize(pointCount);
  //  prevBaseRepVertexIndices = refFrame.baseRepVertexIndices;
  //} else if (reuseDuplicatedVertFlag_ && (!prevBaseRepVertexIndices.empty())) {
  //  assert(pointCount == prevBaseRepVertexIndices.size());
  //  reuseRepresentVertices(refFrame.baseRepVertexIndices,
  //                         refFrame.base,
  //                         prevBaseRepVertexIndices,
  //                         duplicateVertexList);
  //}
  bmsl.getSubmeshDataunitInter().setMchLog2MotionGroupSize(
    uint8_t(std::ceil(std::log2(param.motionGroupSize))));
  bmsl.getSubmeshDataunitInter().setMchMaxMvpCandMinus1(
    param.maxNumMotionVectorPredictor - 1);
  bmsl.getSubmeshDataunitInter().setMcpVertexCount(pointCount);
  for (int vindex = 0; vindex < pointCount; ++vindex) {
    auto refVertexIndex = refFrame.baseRepVertexIndices[vindex];
    if (vindex != refVertexIndex) {
      numDupVertex++;
      if (motion[vindex] == motion[refVertexIndex]) {
        mvSignalled[vindex]               = 0;
        mvRealSignalled[numDupVertex - 1] = 0;
        numDupDerived++;
      } else {
        numDupSiged++;
      }
    } else {
      numFirstVertex++;
    }
  }

  if (param.motionWithoutDuplicatedVertices) {
    numDupDerived = 0;
    for (int vindex = 0; vindex < pointCount; ++vindex) {
      mvSignalled[vindex]     = 1;
      mvRealSignalled[vindex] = 1;
    }
  }
  if (numDupDerived == 0) { prevBaseRepVertexIndices.clear(); }

  //  if(numDupDerived<motionCount*0.3)
  //    copyMvPresent=false;

  int numSignalledDupVertex = 0;
  int numTrailing0          = 0;
  for (int findex = numDupVertex - 1; findex > -1; --findex) {
    if (mvRealSignalled[findex] == 1) {
      numSignalledDupVertex = findex + 1;
      break;
    }
  }
  numTrailing0 = numDupVertex - numSignalledDupVertex;
  arithmeticEncoder.encodeExpGolomb(
    numSignalledDupVertex, 0, ctx.ctxMvDupVert);
  for (int findex = 0; findex < numSignalledDupVertex; ++findex) {
    arithmeticEncoder.encode(mvRealSignalled[findex] == 0 ? 0 : 1,
                             ctx.ctxMvSignalledFlag);
  }
  arithmeticEncoder.encodeExpGolomb(numTrailing0, 0, ctx.ctxTrailing0);
  printf("\t[simpleMD_f%d] numCurrentMeshSize: "
         "%zu\tnumRepVertex:%d\tnumDupVertex:%d\tnumMvSigSize: "
         "%d\tnumMvDerivedSize: %d\t",
         frameIndex,
         current.size(),
         numFirstVertex,
         numDupVertex,
         numDupSiged,
         numDupDerived);
  fflush(stdout);

  int32_t PRED_MOTIONGROUP_SKIP = param.maxNumMotionVectorPredictor;
  int32_t MGcnt =
    (motionCount - numDupDerived - 1) / param.motionGroupSize + 1;
  for (int32_t vStart0 = 0, vEnd = 0, g = 0; g < MGcnt; g++) {
    vStart0 = vEnd;

    std::vector<std::vector<vmesh::Vec3<int32_t>>> resVN(
      param.maxNumMotionVectorPredictor);
    std::vector<vmesh::Vec3<double>> costs(
      param.maxNumMotionVectorPredictor + 1, vmesh::Vec3<double>(0, 0, 0));
    for (int i = 0; i < param.maxNumMotionVectorPredictor; i++) {
      resVN[i].resize(0);
      for (int k = 0; k < 3; k++) {
        costs[i][k] =
          ctx.estimatePredMode(i, param.maxNumMotionVectorPredictor - 1);
      }
    }

    int32_t groupSize = std::min(motionCount - vStart0, param.motionGroupSize);
    int32_t signalledMotionCount = 0;
    int32_t motionInGroupCount   = 0;
    while (signalledMotionCount < groupSize
           && (vStart0 + motionInGroupCount < motionCount)) {
      int vindex = vStart0 + motionInGroupCount;
      motionInGroupCount++;
      if (mvSignalled[vindex] == 0) { continue; }
      vmesh::Vec3<int32_t> pred(0);
      int32_t              predCount = numNeighborsMotion[vindex];
      vmesh::Vec3<int32_t> prednobias(0);
      for (int32_t i = 0; i < predCount; ++i) {
        const auto vindex1 =
          vertexAdjTableMotion[vindex * param.maxNumNeighborsMotion + i];
        const auto& mv1 = motion[vindex1];
        for (int32_t k = 0; k < 3; ++k) { pred[k] += mv1[k]; }
        prednobias = pred;
      }
      if (predCount > 1) {
        const auto bias = predCount >> 1;
        for (int32_t k = 0; k < 3; ++k) {
          pred[k]       = pred[k] >= 0 ? (pred[k] + bias) / predCount
                                       : -(-pred[k] + bias) / predCount;
          prednobias[k] = prednobias[k] / predCount;
        }
      }

      for (int i = 0; i < param.maxNumMotionVectorPredictor; i++) {
        auto res = motion[vindex];
        res -= (i == 0) ? 0 : (i == 1) ? prednobias : pred;
        resVN[i].push_back(res);
        costs[i] += ctx.estimateRes3(res);
      }

      for (int k = 0; k < 3; k++) {
        costs[PRED_MOTIONGROUP_SKIP][k] += abs(motion[vindex][k]);
      }
      signalledMotionCount++;
    }

    vEnd = vStart0 + motionInGroupCount;
    std::vector<double> bitsT(param.maxNumMotionVectorPredictor + 1, 0);
    double              lambda = 6.25 * 560;
    costs[PRED_MOTIONGROUP_SKIP] *= lambda;
    for (int i = 0; i < param.maxNumMotionVectorPredictor + 1; i++) {
      for (int k = 0; k < 3; k++) { bitsT[i] += (double)costs[i][k]; }
    }
    //prediction type(skip) for motionGroup decision : all component skip, k-th component skip
    bool                 bSkipGroupFlag = false;
    std::vector<bool>    bSkipGroupCompFlags(3, false);
    std::vector<int32_t> predIndex(3, 0);
    int32_t              bestPred = (int32_t)std::distance(
      bitsT.begin(), std::min_element(bitsT.begin(), bitsT.end()));

    if (bestPred == PRED_MOTIONGROUP_SKIP) {
      bSkipGroupFlag = true;
    } else {
      for (int k = 0; k < 3; k++) {
        std::vector<double> componentCost(param.maxNumMotionVectorPredictor
                                          + 1);
        for (int i = 0; i < param.maxNumMotionVectorPredictor + 1; i++) {
          componentCost[i] = costs[i][k];
        }
        predIndex[k] = (int32_t)std::distance(
          componentCost.begin(),
          std::min_element(componentCost.begin(), componentCost.end()));
        if (predIndex[k] == PRED_MOTIONGROUP_SKIP) {
          bSkipGroupCompFlags[k] = true;
        }
      }
    }
    arithmeticEncoder.encode(bSkipGroupFlag, ctx.ctxSkip);
    if (bSkipGroupFlag) {
      //reconstruct current and motion
      for (int v = vStart0; v < vEnd; ++v) {
        if (mvSignalled[v] == 0) { continue; }
        current[v] = reference[v];
        motion[v]  = 0;
        for (int n = v + 1; n < pointCount; n++) {
          if (v == refFrame.baseRepVertexIndices[n] && mvSignalled[n] == 0) {
            current[n] = reference[n];
            motion[n]  = 0;
          }
        }
      }
    } else {
      for (int k = 0; k < 3; k++) {
        if (k < 2
            || (k == 2
                && (bSkipGroupCompFlags[0] == 0
                    || bSkipGroupCompFlags[1] == 0))) {
          arithmeticEncoder.encode(bSkipGroupCompFlags[k], ctx.ctxSkip);
        }

        if (bSkipGroupCompFlags[k]) {
          //recon
          for (int v = vStart0; v < vEnd; ++v) {
            if (mvSignalled[v] == 0) { continue; }
            current[v][k] = reference[v][k];
            motion[v][k]  = 0;
            for (int n = v + 1; n < pointCount; n++) {
              if (v == refFrame.baseRepVertexIndices[n]
                  && mvSignalled[n] == 0) {
                current[n][k] = reference[n][k];
                motion[n][k]  = 0;
              }
            }
          }
        } else {
          encodePredMode(arithmeticEncoder,
                         ctx,
                         predIndex[k],
                         param.maxNumMotionVectorPredictor - 1);
        }
      }

      //motion coding
      int resPos = 0;
      for (int v = vStart0; v < vEnd; ++v) {
        if (mvSignalled[v] == 0) { continue; }
        for (int k = 0; k < 3; k++) {
          if (!bSkipGroupCompFlags[k]) {
            auto value = resVN[predIndex[k]][resPos][k];
            arithmeticEncoder.encode(static_cast<int>(value != 0),
                                     ctx.ctxCoeffGtN[0][k]);
            if (value == 0) { continue; }
            arithmeticEncoder.encode(static_cast<int>(value < 0),
                                     ctx.ctxSign[k]);
            value = std::abs(value) - 1;
            arithmeticEncoder.encode(static_cast<int>(value != 0),
                                     ctx.ctxCoeffGtN[1][k]);
            if (value == 0) { continue; }
            assert(value > 0);
            arithmeticEncoder.encodeExpGolomb(
              --value, 0, ctx.ctxCoeffRemPrefix, 2);
          }
        }
        resPos++;
      }
    }  //else of(group skip)
  }
  const auto length = arithmeticEncoder.stop();
  std::cout << "\tMotion byte count = " << length << '\n';
  assert(length <= std::numeric_limits<uint32_t>::max());
  auto& bmtdu = bmsl.getSubmeshDataunitInter();
  //bmtdu.getCodedMeshDataUnit().resize(length);
  //std::copy(arithmeticEncoder.buffer(),
  //          arithmeticEncoder.buffer() + length,
  //          bmtdu.getCodedMeshDataUnit().begin());
  //return true;
  // header comes here
  vmesh::Bitstream bitstream;
  // motion coding header
  bitstream.write(bmtdu.getMchMaxMvpCandMinus1(), 2);
  bitstream.write(bmtdu.getMchLog2MotionGroupSize(), 3);
  //byteAlignment(bitstream);
  uint32_t one  = 1;
  uint32_t zero = 0;
  bitstream.write(one, 1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    bitstream.write(zero, 1);  // f(1): equal to 0
  }
  int headerSize = bitstream.getPosition().bytes_;
  //motion coding payload
  bitstream.writeVu(bmtdu.getMcpVertexCount());
  auto pos = bitstream.getPosition().bytes_;
  std::cout << "\tMotion header byte count = " << headerSize << '\n';
  std::cout << "\tMotion vertex byte count = " << pos - headerSize << '\n';
  std::cout << "\tMotion byte count = " << length << '\n';
  bitstream.copyFromBits((uint8_t*)arithmeticEncoder.buffer(), 0, length);
  pos = bitstream.getPosition().bytes_;
  bmtdu.getCodedMeshDataUnit().resize(pos);
  std::copy(bitstream.buffer(),
            bitstream.buffer() + pos,
            bmtdu.getCodedMeshDataUnit().begin());
  return headerSize;
}

//============================================================================
bool
BaseMeshEncoder::compressBaseMesh(
  const std::vector<vmesh::VMCSubmesh>& gof,
  int32_t                               submeshIndex,
  int32_t                               frameIndex,
  vmesh::VMCSubmesh&                    frame,
  BaseMeshSubmeshLayer&                 bmsl,
  const BaseMeshEncoderParameters&      params,
  std::vector<int32_t>&                 prevBaseRepVertexIndices,
  vmesh::TriangleMesh<MeshType>&        baseEnc,
  std::vector<int32_t>&                 mappingWithDupVertex) {
  // if (!encodeFrameHeader(frameInfo, bitstream)) { return false; }
  const double scalePosition =
    ((1 << params.qpPosition) - 1.0) / ((1 << params.bitDepthPosition) - 1.0);
  const double scaleTexCoord  = std::pow(2.0, params.qpTexCoord) - 1.0;
  const double iscalePosition = 1.0 / scalePosition;
  const double iscaleTexCoord = 1.0 / scaleTexCoord;
  auto&        base           = frame.base;
  auto&        subdiv         = frame.subdiv;
  auto&        bmsh           = bmsl.getSubmeshHeader();

  bmsl.setFrameIndex(frameIndex);
  bmsl.setSubmeshIndex(submeshIndex);
  bmsh.setSmhSubmeshFrameParameterSetId(0);  
  bmsh.setSmhBasemeshFrmOrderCntLsb(frameIndex);
  bmsh.setSmhId(params.submeshIdList[submeshIndex]);

  // Save intermediate files
  std::string prefix = "";
  if (params.keepBaseMesh) {
    prefix = keepFilesPathPrefix_ + "fr_" + std::to_string(frame.frameIndex);
    base.save(prefix + "_base_compress.ply");
    subdiv.save(prefix + "_subdiv_compress.ply");
  }
  if (frame.submeshType == I_BASEMESH) {
    std::cout << "\n(compressBaseMesh) FrameIndex: " << frame.frameIndex
              << " submeshIndex: " << submeshIndex
              << " submeshType: I_SUBMESH\n";
    bmsh.setSmhType(frame.submeshType);
    auto&      bmsdu        = bmsl.getSubmeshDataunitIntra();
    const auto texCoordBBox = base.texCoordBoundingBox();
    const auto delta        = texCoordBBox.max - texCoordBBox.min;
    const auto d            = std::max(delta[0], delta[1]);
    if (d > 2.0) {  // make sure texCoords are between 0.0 and 1.0
      const double scale =
        1.0 / (std::pow(2.0, params.bitDepthTexCoord) - 1.0);
      for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount;
           ++tc) {
        base.setTexCoord(tc, base.texCoord(tc) * scale);
      }
    }
    if (reuseDuplicatedVertFlag_) prevBaseRepVertexIndices.clear();

    // TODO: should this be done outside of the baseMeshEncoder
    //alter the base mesh in case we are deriving text coordinates at decoder
    if (params.iDeriveTextCoordFromPos > 0) {
      auto oBase = base;
      updateBaseMesh(oBase, base, frame.packedCCList, params);
      computeDracoMappingForOrthoAtlasUpdatedBaseMesh(
        oBase,
        base,
        frame.mapping,
        frame.frameIndex,
        params,
        frame.packedCCList.size(),
        false);

      if (params.useRawUV && params.iDeriveTextCoordFromPos == 3) {
        computeDracoMappingForOrthoAtlasUpdatedBaseMesh(
          oBase,
          base,
          mappingWithDupVertex,
          frame.frameIndex,
          params,
          frame.packedCCList.size(),
          true);
      }
      //base.save("ortho.base.revision.obj" );
    } else if (!params.dracoMeshLossless) {
      computeDracoMapping(base,
                          frame.mapping,
                          frame.frameIndex,
                          submeshIndex,
                          params);
    }
    if (params.keepIntermediateFiles) {
      base.save(prefix + "_post_mapping_base.ply");
    }
    // quantize base mesh
    for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
      base.setPoint(v,
                    Clamp(Round(base.point(v) * scalePosition),
                          0.0,
                          ((1 << params.qpPosition) - 1.0)));
    }
    if (params.iDeriveTextCoordFromPos == 0) {
      // texture coordinates were sent inside the base mesh, performing de-quantization before coding
      for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount;
           ++tc) {
        base.setTexCoord(tc, Round(base.texCoord(tc) * scaleTexCoord));
      }
    }

    // Save intermediate files
    if (params.keepIntermediateFiles) {
      base.save(prefix + "_post_mapping_base_quant.ply");
    }

    // Encode
    vmesh::GeometryEncoderParameters encoderParams;

    encoderParams.qn_               = params.qpNormals;
    encoderParams.encodeNormals     = params.encodeNormals;
    encoderParams.predNormal        = params.predNormal;
    encoderParams.normalsOctahedral = params.normalsOctahedral;
    encoderParams.entropyPacket     = params.entropyPacket;
    encoderParams.qpOcta_           = params.qpOcta;

    encoderParams.qp_ = params.qpPosition;
    encoderParams.qt_ = params.qpTexCoord;
    encoderParams.qg_ = -1;
    if (params.numTextures > 1 && params.dracoMeshLossless) {
      encoderParams.qg_ = std::ceil(std::log2(params.numTextures));
      base.faceIds()    = base.materialIdxs();
    }
    if (params.iDeriveTextCoordFromPos
        == 1) {  //AJT::Added orthoAtlas related parts
      auto maxVal =
        std::max_element(base.texCoords().begin(), base.texCoords().end());
      encoderParams.qt_ = std::ceil(std::log2((*maxVal)[0] + 1));
      encoderParams.qg_ = -1;
    } else if (params.iDeriveTextCoordFromPos == 2) {
      encoderParams.qt_ = -1;
      auto maxVal =
        std::max_element(base.faceIds().begin(), base.faceIds().end());
      encoderParams.qg_ = std::ceil(std::log2((*maxVal) + 1));
    } else if (params.iDeriveTextCoordFromPos == 3) {
      encoderParams.qt_ = -1;
      encoderParams.qg_ = -1;
    }
    encoderParams.predGeneric            = params.predGeneric;
    encoderParams.dracoUsePosition_      = params.dracoUsePosition;
    encoderParams.dracoUseUV_            = params.dracoUseUV;
    encoderParams.dracoMeshLossless_     = params.dracoMeshLossless;
    encoderParams.traversal_             = params.baseMeshVertexTraversal;
    encoderParams.reindexOutput_         = params.motionVertexTraversal;
    encoderParams.baseMeshDeduplication_ = params.baseMeshDeduplication;
    encoderParams.reverseUnification_    = params.reverseUnification;
    encoderParams.logFileName_           = (params.keepBaseMesh) ? prefix : "";
    encoderParams.profile                = params.profileGeometryCodec;

    vmesh::TriangleMesh<MeshType> rec;
    std::vector<uint8_t>& geometryBitstream = bmsdu.getCodedMeshDataUnit();
    auto                  encoder =
      vmesh::GeometryEncoder<MeshType>::create(params.meshCodecId);
    printf("BaseMeshEnco: use_position = %d use_uv = %d mesh_lossless = %d \n",
           encoderParams.dracoUsePosition_,
           encoderParams.dracoUseUV_,
           encoderParams.dracoMeshLossless_);
    vmesh::tic("baseMeshEncode");
    encoder->encode(base, encoderParams, geometryBitstream, rec);
    vmesh::toc("baseMeshEncode");

    if (params.numTextures > 1 && params.dracoMeshLossless) {
      // now return the matrial idx from the fid attribute
      rec.materialIdxs() = rec.faceIds();
      rec.faceIds().clear();
    }

    bmsdu.setCodedMeshHeaderDataSize(encoder->getMeshCodingHeaderLength());

    // Save intermediate files
    if (params.keepBaseMesh) {
      base.save(prefix + "_base_enc.obj");
      rec.save(prefix + "_base_rec.obj");
      vmesh::save(prefix + "_base.drc", geometryBitstream);
    }

    baseEnc = base;
    // Round positions
    base = rec;
    if (params.iDeriveTextCoordFromPos > 0) {
      frame.orthoAtlasUVderivationInfo = rec;
    }

    // reconstruct UV coordinates
    if (params.iDeriveTextCoordFromPos == 0) {
      // texture coordinates were sent inside the base mesh, so now we need to reverse the quantization
      for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount;
           ++tc) {
        base.setTexCoord(tc, base.texCoord(tc) * iscaleTexCoord);
      }
    }

    // calculating the neighboring vertices structure
    auto& triangles = base.triangles();
    computeVertexAdjTableMotion(triangles,
                                base.pointCount(),
                                params.maxNumNeighborsMotion,
                                frame.vertexAdjTableMotion,
                                frame.numNeighborsMotion);
    // calculating the duplicate structure
    // there should be a flag to indicate if we are going to do this or NOT in BMSPS,
    // which also forces dupNumCount to be zero in the motion codec
    if (params.motionWithoutDuplicatedVertices == 0) {
      std::vector<int32_t> duplicateVertexList;
      findRepresentVerticesSimple(
        frame.baseRepVertexIndices, duplicateVertexList, base);
    } else {
      frame.baseRepVertexIndices.resize(base.pointCount());
      for (int i = 0; i < base.pointCount(); i++)
        frame.baseRepVertexIndices[i] = i;  // no duplicates
    }
    //prevBaseRepVertexIndices.resize(base.pointCount());
    prevBaseRepVertexIndices = frame.baseRepVertexIndices;

  } else {
    bmsh.setSmhType(P_BASEMESH);
    auto& bmsdu = bmsl.getSubmeshDataunitInter();

    //TODO: update gof to reference list
    //frame.referenceFrameIndex : 0~31
    const auto& refFrame = gof[frame.referenceFrameIndex];
    auto&       mapping  = frame.mapping;
    std::vector<vmesh::Vec3<MeshType>> qpositions;
    mapping                          = refFrame.mapping;
    const auto pointCountRecBaseMesh = refFrame.base.pointCount();
    qpositions.resize(pointCountRecBaseMesh);
    for (int32_t v = 0; v < pointCountRecBaseMesh; ++v) {
      assert(mapping[v] >= 0 && mapping[v] < base.pointCount());
      qpositions[v] = (base.point(mapping[v]) * scalePosition).round();
    }
    base    = refFrame.base;
    baseEnc = base;
    for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
      base.point(v) = qpositions[v];
    }
    //if (refFrame.submeshType == I_BASEMESH) {
    //  auto& triangles = refFrame.base.triangles();
    //  computeVertexAdjTableMotion(triangles,
    //                              pointCountRecBaseMesh,
    //                              params.maxNumNeighborsMotion,
    //                              frame.vertexAdjTableMotion,
    //                              frame.numNeighborsMotion_);
    //} else {
    frame.vertexAdjTableMotion = refFrame.vertexAdjTableMotion;
    frame.numNeighborsMotion   = refFrame.numNeighborsMotion;
    //}
    // copy the duplicated indices
    frame.baseRepVertexIndices = refFrame.baseRepVertexIndices;
    // decide to use P_BASEMESH or SKIP_BASEMESH
    auto frameInfoInterOrSkipType =
      chooseSkipOrInter(base.points(), refFrame.base.points());
    if (frameInfoInterOrSkipType == P_BASEMESH) {
      std::cout << "\n(compressBaseMesh) FrameIndex: " << frame.frameIndex
                << " submeshIndex: " << submeshIndex
                << " submeshType: P_SUBMESH\t";
      std::cout << "referenceFrameIndex : " << frame.referenceFrameIndex
                << " \n";
      fflush(stdout);
      int headerSize = compressMotion(refFrame,
                                      frame,
                                      frame.vertexAdjTableMotion,
                                      frame.numNeighborsMotion,
                                      bmsl,
                                      params,
                                      frame.frameIndex,
                                      prevBaseRepVertexIndices);
      bmsdu.setCodedMotionHeaderDataSize(headerSize);
      auto& refList = bmsh.getSmhRefListStruct();
      refList.setNumRefEntries(1);
      refList.allocate();
      if (frame.frameIndex > frame.referenceFrameIndex) {
        refList.setAbsDeltaMfocSt(
          0, frame.frameIndex - frame.referenceFrameIndex);
        refList.setStrafEntrySignFlag(0, false);
        refList.setStRefMeshFrameFlag(0, true);
      } else {
        refList.setAbsDeltaMfocSt(
          0, frame.referenceFrameIndex - frame.frameIndex);
        refList.setStrafEntrySignFlag(0, true);
        refList.setStRefMeshFrameFlag(0, true);
      }
    } else if (frameInfoInterOrSkipType == SKIP_BASEMESH) {
      bmsh.setSmhType(SKIP_BASEMESH);
      std::cout << "(compressBaseMesh) FrameIndex: " << frame.frameIndex
                << " submeshIndex: " << submeshIndex
                << " submeshType: SKIP_SUBMESH\t";
      std::cout << "frameInfo.referenceFrameIndex : "
                << frame.referenceFrameIndex << " \n";
      //      printf("Skip: index = %d ref = %d \n",
      //             frameInfo.frameIndex,
      //             frameInfo.referenceFrameIndex);
      fflush(stdout);

      // copy and quantize base mesh
      base = refFrame.base;
    }

    // reconstruct UV coordinates
    if (params.iDeriveTextCoordFromPos > 0) {
      if (params.iDeriveTextCoordFromPos == 1) {
        // copy the UV coordinates from the reference frame
        base.texCoords().clear();
        base.texCoords() = refFrame.orthoAtlasUVderivationInfo.texCoords();
        base.texCoordTriangles().clear();
        base.texCoordTriangles() =
          refFrame.orthoAtlasUVderivationInfo.texCoordTriangles();
      } else if (params.iDeriveTextCoordFromPos == 2) {
        // copy the fid from the reference frame
        base.texCoords().clear();
        base.texCoords() = refFrame.orthoAtlasUVderivationInfo.texCoords();
        base.texCoordTriangles().clear();
        base.texCoordTriangles() =
          refFrame.orthoAtlasUVderivationInfo.texCoordTriangles();
        base.faceIds().clear();
        base.faceIds() = refFrame.orthoAtlasUVderivationInfo.faceIds();
      }
      frame.orthoAtlasUVderivationInfo = refFrame.orthoAtlasUVderivationInfo;
      frame.packedCCList               = refFrame.packedCCList;
      auto& refList                    = bmsh.getSmhRefListStruct();
      refList.setNumRefEntries(1);
      refList.allocate();
      if (frame.frameIndex > frame.referenceFrameIndex) {
        refList.setAbsDeltaMfocSt(
          0, frame.frameIndex - frame.referenceFrameIndex);
        refList.setStrafEntrySignFlag(0, false);
        refList.setStRefMeshFrameFlag(0, true);
      } else {
        refList.setAbsDeltaMfocSt(
          0, frame.referenceFrameIndex - frame.frameIndex);
        refList.setStrafEntrySignFlag(0, true);
        refList.setStRefMeshFrameFlag(0, true);
      }
    }
  }
  //NOTE: this is moved behind the scaling process
  //  if (params.iDeriveTextCoordFromPos > 0) {
  //    updateTextureCoordinates(frame, params);
  //  }
  //note : base is overwritten by reconstructed base
  return true;
}

bool
BaseMeshEncoder::updateBaseMesh(
  vmesh::TriangleMesh<MeshType>&                  oBase,
  vmesh::TriangleMesh<MeshType>&                  base,
  std::vector<vmesh::ConnectedComponent<double>>& packedCCList,
  const BaseMeshEncoderParameters&                params) {
  switch (params.iDeriveTextCoordFromPos) {
  default:
  case 1:  // using UV coordinates
  {
    int idxCC  = 0;
    int idxTri = 0;
    for (auto& cc : packedCCList) {
      //mapping subpatch index to face ID
      double faceId = -1;
      if (params.bFaceIdPresentFlag)
        faceId =
          idxCC * 10;  // this is just for testing the mapping functionality
      else faceId = idxCC;
      cc.setFaceId(faceId);
      for (int idxTriCC = 0; idxTriCC < cc.triangleCount(); idxTriCC++) {
        auto triTex = base.texCoordTriangle(idxTri + idxTriCC);
        for (int i = 0; i < 3; i++) {
          int32_t tc = triTex[i];
          base.setTexCoord(tc, vmesh::Vec2<double>(faceId, 0));
        }
      }
      idxTri += cc.triangleCount();
      idxCC++;
    }
    oBase = base;
    break;
  }
  case 2:  // using FaceID
  {
    int idxCC  = 0;
    int idxTri = 0;
    base.resizeFaceIds(base.triangleCount());
    base.texCoords().clear();
    base.texCoordTriangles().clear();
    for (auto& cc : packedCCList) {
      //mapping subpatch index to face ID
      double faceId = -1;
      if (params.bFaceIdPresentFlag)
        faceId =
          idxCC * 10;  // this is just for testing the mapping functionality
      else faceId = idxCC;
      cc.setFaceId(faceId);
      for (int idxTriCC = 0; idxTriCC < cc.triangleCount(); idxTriCC++) {
        base.setFaceId(idxTri + idxTriCC, faceId);
      }
      idxTri += cc.triangleCount();
      idxCC++;
    }
    oBase = base;
    break;
  }
  case 3:  // using connected components -> connectivity from texture coordinates
  {
    auto coordList = base.points();
    base.points().clear();
    base.points().resize(base.texCoords().size(), vmesh::Vec3<double>(0));
    for (int triIdx = 0; triIdx < base.triangleCount(); triIdx++) {
      auto triangle    = base.triangle(triIdx);
      auto texTriangle = base.texCoordTriangle(triIdx);
      for (int i = 0; i < 3; i++) {
        base.points()[texTriangle[i]] = coordList[triangle[i]];
      }
    }
    base.triangles().clear();
    base.triangles() = base.texCoordTriangles();
    base.texCoords().clear();
    base.texCoordTriangles().clear();
    oBase.texCoords().clear();
    oBase.texCoordTriangles().clear();
    break;
  }
  }

  return true;
}

//============================================================================
//constructSmlRefListStruct
//construct the reference list structure in submesh header
void
BaseMeshEncoder::constructSmlRefListStruct(BaseMeshBitstream&    basemesh,
                                           BaseMeshSubmeshLayer& sml,
                                           int32_t               submeshIndex,
                                           int32_t               frameIndex,
                                           int32_t referenceFrameIndex) {
  int32_t numRefFrame = 1;
  //referenceFrameIndex = 0,1,2,3...31
  if (referenceFrameIndex < 0) {
    numRefFrame = 0;
    //TODO: [sw] referenceFrameIndex in SKIP (why does it change SKIP to I?)
    //sml.getSubmeshHeader().setSmhType(I_BASEMESH);
    sml.getSubmeshHeader().setSmhRefBasemeshFrameListMspsFlag(true);
    sml.getSubmeshHeader().setSmhRefMeshFrameListIdx(0);
    return;
  } else {
    // sml.getSubmeshHeader().setSmhType(P_BASEMESH);
  }
  size_t bmfpsId = sml.getSubmeshHeader().getSmhSubmeshFrameParameterSetId();
  auto&  bmfps =
    basemesh.getBaseMeshFrameParameterSet(bmfpsId);
  size_t bmspsId = bmfps.getBfpsMeshSequenceParameterSetId();
  auto&  bmsps =
    basemesh.getBaseMeshSequenceParameterSet(bmspsId);

#if 0  //constructSmlRefListStruct
  printf("(constructSmlRefListStruct) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf("(constructSmlRefListStruct) frame[%d] submeshp[%d] frame.referenceframeIndex:%d\n", frameIndex, submeshIndex, referenceFrameIndex);
  for(size_t refListIdx=0; refListIdx<msps.getBmspsNumRefMeshFrameListsInBmsps();refListIdx++){
    auto& refList = msps.getBmeshRefListStruct( refListIdx );
    int32_t mfocBase = frameIndex;
    for(size_t refIdx=0; refIdx<refList.getNumRefEntries(); refIdx++){
      int16_t deltaMfocSt = ( 2 * refList.getStrafEntrySignFlag( refIdx ) - 1 ) * refList.getAbsDeltaMfocSt( refIdx );
      auto refFrameIndex = mfocBase - deltaMfocSt;
      printf("BMSPSRefList[%zu][%zu] = %d-%d\t= %d\n", refListIdx, refIdx, mfocBase, (int16_t)deltaMfocSt, (int16_t)refFrameIndex);
      mfocBase = refFrameIndex;
    }
  }
  printf("(constructSmlRefListStruct) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
#endif

  //Note: checking if refFrameIndex is present in msps.refList
  //Note: that refFrameIndex is different from refIndex smduInterRefIndex is refIndex. refList[refIndex] = refFrameIndex
  int16_t foundRefListIndex  = -1;
  int16_t foundRefFrameIndex = -1;
  int16_t foundRefIndex      = -1;
  for (size_t refListIdx = 0;
       refListIdx < bmsps.getBmspsNumRefMeshFrameListsInBmsps();
       refListIdx++) {
    auto&   refList  = bmsps.getBmeshRefListStruct(refListIdx);
    int32_t mfocBase = frameIndex;
    for (size_t refIdx = 0; refIdx < refList.getNumRefEntries(); refIdx++) {
      int16_t deltaMfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                            * refList.getAbsDeltaMfocSt(refIdx);
      auto refFrameIndex = mfocBase - deltaMfocSt;
      mfocBase           = refFrameIndex;
      if (refFrameIndex == referenceFrameIndex) {
        foundRefListIndex  = refListIdx;
        foundRefIndex      = refIdx;
        foundRefFrameIndex = refFrameIndex;
        break;
      }
    }
  }
  if (foundRefListIndex != -1 && foundRefFrameIndex != -1) {
    sml.getSubmeshHeader().setSmhRefBasemeshFrameListMspsFlag(true);
    sml.getSubmeshHeader().setSmhRefMeshFrameListIdx(foundRefListIndex);
    sml.getSubmeshDataunitInter().setReferenceFrameIndex(foundRefIndex);
#if 0
    printf("frameIndex[%d] submeshIndex[%d] foundRefFrameIdx: mspsRefList[%d][%d]=%d\n", frameIndex, submeshIndex, foundRefListIndex, foundRefIndex, foundRefFrameIndex);
#endif
  } else {
    //new reference structure
    sml.getSubmeshHeader().setSmhRefBasemeshFrameListMspsFlag(false);
    BaseMeshRefListStruct refList;
    refList.setNumRefEntries(numRefFrame);
    refList.allocate();
    for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
      int mfocDiff = -1;
      if (i == 0) {
        mfocDiff = frameIndex - referenceFrameIndex;
      } else {
        mfocDiff = frameIndex - referenceFrameIndex;
      }
      refList.setAbsDeltaMfocSt(i, std::abs(mfocDiff));
      refList.setStrafEntrySignFlag(i, mfocDiff < 0 ? false : true);
      refList.setStRefMeshFrameFlag(i, true);
    }
    sml.getSubmeshHeader().setSmhRefListStruct(refList);
    sml.getSubmeshDataunitInter().setReferenceFrameIndex(0);
  }

  //size_t numActiveRefEntries=1;
  //refoc = afocbase - diff
  if (numRefFrame > 0) {
    bool bNumRefIdxActiveOverrideFlag = false;
    if ((uint32_t)numRefFrame
        >= (bmfps.getBfpsNumRefIdxDefaultActiveMinus1())) {
      bNumRefIdxActiveOverrideFlag =
        ((uint32_t)numRefFrame
         != (bmfps.getBfpsNumRefIdxDefaultActiveMinus1() + 1));
    } else {
      bNumRefIdxActiveOverrideFlag =
        numRefFrame
        != sml.getSubmeshHeader().getSmhRefListStruct().getNumRefEntries();
    }
    sml.getSubmeshHeader().setSmhNumRefIdxActiveOverrideFlag(
      bNumRefIdxActiveOverrideFlag);
    if (sml.getSubmeshHeader().getSmhNumRefIdxActiveOverrideFlag())
      sml.getSubmeshHeader().setSmhNumRefIdxActiveMinus1(numRefFrame - 1);
  }
}

bool
BaseMeshEncoder::computeDracoMappingForOrthoAtlasUpdatedBaseMesh(
  vmesh::TriangleMesh<MeshType>    origBase,
  vmesh::TriangleMesh<MeshType>    modifiedBase,
  std::vector<int32_t>&            mapping,
  int32_t                          frameIndex,
  const BaseMeshEncoderParameters& params,
  int32_t                          packedCCListsize,
  bool                             enableMappingWithDupVertex) const {
  // Save intermediate files
  if (params.keepIntermediateFiles) {
    origBase.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                  + "_mapping_base_original.ply");
    modifiedBase.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                      + "_mapping_base_modified.ply");
  }

  // simulation for edge-based subdivision
  auto                          simEncBase = modifiedBase;
  vmesh::TriangleMesh<MeshType> simRecBase;
  if (params.subdivisionEdgeLengthThreshold > 0) {
    const double simScalePosition = ((1 << params.qpPosition) - 1.0)
                                    / ((1 << params.bitDepthPosition) - 1.0);
    const double simScaleTexCoord = std::pow(2.0, params.qpTexCoord) - 1.0;
    // quantize base mesh
    for (int32_t v = 0, vcount = simEncBase.pointCount(); v < vcount; ++v) {
      simEncBase.setPoint(v,
                          Clamp(Round(simEncBase.point(v) * simScalePosition),
                                0.0,
                                ((1 << params.qpPosition) - 1.0)));
    }
    if (params.iDeriveTextCoordFromPos == 0) {
      // texture coordinates were sent inside the base mesh, performing de-quantization before coding
      for (int32_t tc = 0, tccount = simEncBase.texCoordCount(); tc < tccount;
           ++tc) {
        simEncBase.setTexCoord(
          tc, Round(simEncBase.texCoord(tc) * simScaleTexCoord));
      }
    }
    // Encode
    vmesh::GeometryEncoderParameters simEncoderParams;

    simEncoderParams.qn_               = params.qpNormals;
    simEncoderParams.encodeNormals     = params.encodeNormals;
    simEncoderParams.predNormal        = params.predNormal;
    simEncoderParams.normalsOctahedral = params.normalsOctahedral;
    simEncoderParams.entropyPacket     = params.entropyPacket;
    simEncoderParams.qpOcta_           = params.qpOcta;

    simEncoderParams.qp_ = params.qpPosition;
    simEncoderParams.qt_ = params.qpTexCoord;
    simEncoderParams.qg_ = -1;
    if (params.numTextures > 1 && params.dracoMeshLossless) {
      simEncoderParams.qg_ = std::ceil(std::log2(params.numTextures));
      simEncBase.faceIds() = simEncBase.materialIdxs();
    }
    if (params.iDeriveTextCoordFromPos
        == 1) {  //AJT::Added orthoAtlas related parts
      simEncoderParams.qt_ = std::ceil(std::log2(packedCCListsize));
      simEncoderParams.qg_ = -1;
    } else if (params.iDeriveTextCoordFromPos == 2) {
      simEncoderParams.qt_ = -1;
      simEncoderParams.qg_ = std::ceil(std::log2(packedCCListsize));
    } else if (params.iDeriveTextCoordFromPos == 3) {
      simEncoderParams.qt_ = -1;
      simEncoderParams.qg_ = -1;
    }
    simEncoderParams.dracoUsePosition_      = params.dracoUsePosition;
    simEncoderParams.dracoUseUV_            = params.dracoUseUV;
    simEncoderParams.dracoMeshLossless_     = params.dracoMeshLossless;
    simEncoderParams.traversal_             = params.baseMeshVertexTraversal;
    simEncoderParams.reindexOutput_         = params.motionVertexTraversal;
    simEncoderParams.baseMeshDeduplication_ = params.baseMeshDeduplication;
    simEncoderParams.reverseUnification_    = params.reverseUnification;
    simEncoderParams.profile                = params.profileGeometryCodec;

    std::vector<uint8_t> tmpBitstream;
    auto                 encoder =
      vmesh::GeometryEncoder<MeshType>::create(params.meshCodecId);
    encoder->encode(simEncBase, simEncoderParams, tmpBitstream, simRecBase);

    const double simIscalePosition = 1.0 / simScalePosition;
    for (int32_t v = 0, vcount = simRecBase.pointCount(); v < vcount; ++v) {
      simRecBase.setPoint(v, simRecBase.point(v) * simIscalePosition);
    }
  }

  auto compressBase = modifiedBase;
  // Scale
  const auto scalePosition = 1 << (18 - params.bitDepthPosition);
  const auto scaleTexCoord = 1 << 18;
  for (int32_t v = 0, vcount = origBase.pointCount(); v < vcount; ++v) {
    origBase.setPoint(v, vmesh::Vec3<MeshType>(1 << 18) + Round(origBase.point(v) * (MeshType)scalePosition));
  }
  if (origBase.texCoordCount() > 0) {
    for (int32_t tc = 0, tccount = origBase.texCoordCount(); tc < tccount;
         ++tc) {
      origBase.setTexCoord(tc, Round(origBase.texCoord(tc) * scaleTexCoord));
    }
  }
  // shift added to cope with potential negative base.point values
  for (int32_t v = 0, vcount = modifiedBase.pointCount(); v < vcount; ++v) {
    modifiedBase.setPoint(
      v, vmesh::Vec3<MeshType>(1 << 18) + Round(modifiedBase.point(v) * (MeshType)scalePosition));
  }
  if (modifiedBase.texCoordCount() > 0
      && (params.iDeriveTextCoordFromPos == 0)) {
    for (int32_t tc = 0, tccount = modifiedBase.texCoordCount(); tc < tccount;
         ++tc) {
      modifiedBase.setTexCoord(
        tc, Round(modifiedBase.texCoord(tc) * scaleTexCoord));
    }
  }
  // Save intermediate files
  if (params.keepIntermediateFiles) {
    origBase.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                  + "_mapping_scale_original.ply");
    modifiedBase.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                      + "_mapping_scale_modified.ply");
  }

  // Encode
  vmesh::GeometryEncoderParameters encoderParams;
  // remove this one ??
  encoderParams.cl_ = 10;  // this is the default for draco
  //  were not specified before, using scale, extend to 20bits as base values may extend beyond the original scale
  encoderParams.qp_ = 20;
  // in practice 19 bits are used for texcoords as 1.0 is scaled as 0x4000
  // this only has incidence when qp-bits packing is used in the encoder
  encoderParams.qt_ = 19;
  if (params.iDeriveTextCoordFromPos
      == 1) {  //AJT::Added orthoAtlas related parts
    encoderParams.qt_ = 19;
    encoderParams.qg_ = -1;
  } else if (params.iDeriveTextCoordFromPos == 2) {
    encoderParams.qt_ = -1;
    encoderParams.qg_ = 19;
  } else if (params.iDeriveTextCoordFromPos == 3) {
    encoderParams.qt_ = -1;
    encoderParams.qg_ = -1;
  }
  // end remove those
  encoderParams.predGeneric            = params.predGeneric;
  encoderParams.dracoUsePosition_      = params.dracoUsePosition;
  encoderParams.dracoUseUV_            = params.dracoUseUV;
  encoderParams.dracoMeshLossless_     = params.dracoMeshLossless;
  encoderParams.traversal_             = params.baseMeshVertexTraversal;
  encoderParams.reindexOutput_         = params.motionVertexTraversal;
  encoderParams.baseMeshDeduplication_ = params.baseMeshDeduplication;
  encoderParams.reverseUnification_    = params.reverseUnification;
  encoderParams.traversal_             = params.baseMeshVertexTraversal;
  encoderParams.reindexOutput_         = params.motionVertexTraversal;
  encoderParams.verbose                = false;

  vmesh::TriangleMesh<MeshType> rec;
  std::vector<uint8_t>          geometryBitstream;
  auto encoder = vmesh::GeometryEncoder<MeshType>::create(params.meshCodecId);
  printf("DracoMapping: use_position = %d use_uv = %d mesh_lossless = %d \n",
         encoderParams.dracoUsePosition_,
         encoderParams.dracoUseUV_,
         encoderParams.dracoMeshLossless_);
  encoder->encode(modifiedBase, encoderParams, geometryBitstream, rec);

  // Save intermediate files
  if (params.keepIntermediateFiles) {
    auto prefix =
      keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex) + "_mapping";
    if (params.iDeriveTextCoordFromPos == 2) {
      modifiedBase.saveToOBJUsingFidAsColor(prefix + "_mapping_enc");
      rec.saveToOBJUsingFidAsColor(prefix + "_mapping_rec");
    } else {
      modifiedBase.save(prefix + "_enc.ply");
      rec.save(prefix + "_rec.ply");
    }
    vmesh::save(prefix + ".drc", geometryBitstream);
  }

  // get mapping between basemesh and reconstructed basemesh for edge-based subdivision
  std::vector<int32_t> baseMapping;
  if (params.subdivisionEdgeLengthThreshold > 0) {
    auto                 refRecBase = rec;
    auto                 refBase    = modifiedBase;
    std::vector<int32_t> texCoordToPoint0;
    if (refBase.texCoordCount() > 0)
      refBase.computeTexCoordToPointMapping(texCoordToPoint0);
    struct ArrayHasher {
      std::size_t operator()(const std::array<double, 5>& a) const {
        std::size_t h = 0;
        for (auto e : a) {
          h ^= std::hash<double>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
      }
    };
    std::map<std::array<double, 5>, int32_t> baseMap0;
    const auto baseTexCoordCount0 = refBase.texCoordCount();
    const auto baseCoordCount0    = refBase.pointCount();
    int32_t    vCount =
      baseTexCoordCount0 > 0 ? baseTexCoordCount0 : baseCoordCount0;
    for (int32_t v = 0; v < vCount; ++v) {
      const auto&                 point0        = baseTexCoordCount0 > 0
                                                    ? refBase.point(std::max(0, texCoordToPoint0[v]))
                                                    : refBase.point(v);
      const auto&                 baseTexCoord0 = baseTexCoordCount0 > 0
                                                    ? refBase.texCoord(v)
                                                    : vmesh::Vec2<MeshType>(0.0);
      const std::array<double, 5> vertex0{
        point0[0], point0[1], point0[2], baseTexCoord0[0], baseTexCoord0[1]};
      baseMap0[vertex0] = baseTexCoordCount0 > 0 ? texCoordToPoint0[v] : v;
    }

    const auto basePointCount1 = refRecBase.pointCount();
    baseMapping.resize(basePointCount1, -1);
    std::vector<bool> tags(basePointCount1, false);
    for (int32_t t = 0, tcount = refRecBase.triangleCount(); t < tcount; ++t) {
      const auto& tri = refRecBase.triangle(t);
      const auto& triUV =
        refRecBase.texCoordCount() > 0 ? refRecBase.texCoordTriangle(t) : tri;
      for (int32_t k = 0; k < 3; ++k) {
        const auto indexPos = tri[k];
        if (tags[indexPos]) { continue; }
        tags[indexPos]                            = true;
        const auto                  indexTexCoord = triUV[k];
        const auto&                 point1        = refRecBase.point(indexPos);
        const auto&                 texCoord1 = refRecBase.texCoordCount() > 0
                                                  ? refRecBase.texCoord(indexTexCoord)
                                                  : vmesh::Vec2<MeshType>(0.0);
        const std::array<double, 5> vertex1   = {
          point1[0], point1[1], point1[2], texCoord1[0], texCoord1[1]};
        const auto it = baseMap0.find(vertex1);
        if (it != baseMap0.end()) {
          baseMapping[indexPos] = baseMap0[vertex1];
          auto& pointSimEncBase = simEncBase.point(baseMapping[indexPos]);
          pointSimEncBase       = simRecBase.point(indexPos);
        } else {
          assert(0);
        }
      }
    }
  }

  std::vector<vmesh::TriangleMesh<MeshType>> refMeshes(
    params.subdivisionIterationCount + 1),
    refRecMeshes(params.subdivisionIterationCount + 1);
  if (params.subdivisionEdgeLengthThreshold > 0) {
    for (int32_t it = 0; it < params.subdivisionIterationCount; ++it) {
      refMeshes[it] = simEncBase;
      refMeshes[it].subdivideMesh(it);
      refRecMeshes[it] = simRecBase;
      refRecMeshes[it].subdivideMesh(it);
    }
    refMeshes[params.subdivisionIterationCount] = simEncBase;
    refMeshes[params.subdivisionIterationCount].subdivideMesh(
      params.subdivisionIterationCount,
      params.subdivisionEdgeLengthThreshold,
      params.bitshiftEdgeBasedSubdivision);
    refRecMeshes[params.subdivisionIterationCount] = simRecBase;
    refRecMeshes[params.subdivisionIterationCount].subdivideMesh(
      params.subdivisionIterationCount,
      params.subdivisionEdgeLengthThreshold,
      params.bitshiftEdgeBasedSubdivision);
  }

  // Geometry parametrisation base (before compression)
  auto fsubdiv0 = (origBase.pointCount() != modifiedBase.pointCount())
                    ? origBase
                    : modifiedBase;
  if (enableMappingWithDupVertex) fsubdiv0 = modifiedBase;
  fsubdiv0.subdivideMesh(params.subdivisionIterationCount,
                         params.subdivisionEdgeLengthThreshold,
                         params.bitshiftEdgeBasedSubdivision,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         &refMeshes);
  std::map<std::array<double, 5>, int32_t> map0;
  if (!enableMappingWithDupVertex) {
    if (params.keepIntermediateFiles) {
      fsubdiv0.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                    + "_mapping_fsubdiv0.ply");
    }
    std::vector<int32_t> texCoordToPoint0;
    if (fsubdiv0.texCoordCount() > 0)
      fsubdiv0.computeTexCoordToPointMapping(texCoordToPoint0);
    struct ArrayHasher {
      std::size_t operator()(const std::array<double, 5>& a) const {
        std::size_t h = 0;
        for (auto e : a) {
          h ^= std::hash<double>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
      }
    };
    //std::map<std::array<double, 5>, int32_t> map0;
    const auto texCoordCount0 = fsubdiv0.texCoordCount();
    const auto coordCount0    = fsubdiv0.pointCount();
    int32_t    vCount = texCoordCount0 > 0 ? texCoordCount0 : coordCount0;
    for (int32_t v = 0; v < vCount; ++v) {
      const auto& point0 = texCoordCount0 > 0
                             ? fsubdiv0.point(std::max(0, texCoordToPoint0[v]))
                             : fsubdiv0.point(v);
      const auto& texCoord0 =
        texCoordCount0 > 0 ? fsubdiv0.texCoord(v) : vmesh::Vec2<MeshType>(0.0);
      const std::array<double, 5> vertex0{
        point0[0], point0[1], point0[2], texCoord0[0], texCoord0[1]};
      map0[vertex0] = texCoordCount0 > 0 ? texCoordToPoint0[v] : v;
    }
  }

  // Geometry parametrisation rec (after compression)
  auto fsubdiv1 = rec;
  fsubdiv1.subdivideMesh(params.subdivisionIterationCount,
                         params.subdivisionEdgeLengthThreshold,
                         params.bitshiftEdgeBasedSubdivision,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         &refRecMeshes);
  if (!enableMappingWithDupVertex) {
    if (params.keepIntermediateFiles) {
      fsubdiv1.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                    + "_mapping_fsubdiv1.ply");
    }
    const auto pointCount1 = fsubdiv1.pointCount();
    mapping.resize(pointCount1, -1);
    std::vector<bool> tags(pointCount1, false);
    for (int32_t t = 0, tcount = fsubdiv1.triangleCount(); t < tcount; ++t) {
      const auto& tri = fsubdiv1.triangle(t);
      const auto& triUV =
        fsubdiv1.texCoordCount() > 0 ? fsubdiv1.texCoordTriangle(t) : tri;
      for (int32_t k = 0; k < 3; ++k) {
        const auto indexPos = tri[k];
        if (tags[indexPos]) { continue; }
        tags[indexPos]                            = true;
        const auto                  indexTexCoord = triUV[k];
        const auto&                 point1        = fsubdiv1.point(indexPos);
        const auto&                 texCoord1 = fsubdiv1.texCoordCount() > 0
                                                  ? fsubdiv1.texCoord(indexTexCoord)
                                                  : vmesh::Vec2<MeshType>(0.0);
        const std::array<double, 5> vertex1   = {
          point1[0], point1[1], point1[2], texCoord1[0], texCoord1[1]};
        const auto it = map0.find(vertex1);
        if (it != map0.end()) {
          mapping[indexPos] = map0[vertex1];
        } else {
          assert(0);
        }
      }
    }
  } else {
    mapping.clear();
    mapping.resize(fsubdiv1.pointCount(), -1);
    for (int32_t t1 = 0, tcount1 = fsubdiv1.triangleCount(); t1 < tcount1;
         ++t1) {
      const auto& tri1      = fsubdiv1.triangle(t1);
      const auto& t1_point0 = fsubdiv1.point(tri1[0]);
      const auto& t1_point1 = fsubdiv1.point(tri1[1]);
      const auto& t1_point2 = fsubdiv1.point(tri1[2]);
      for (int32_t t0 = 0, tcount0 = fsubdiv0.triangleCount(); t0 < tcount0;
           ++t0) {
        const auto& tri0      = fsubdiv0.triangle(t0);
        const auto& t0_point0 = fsubdiv0.point(tri0[0]);
        const auto& t0_point1 = fsubdiv0.point(tri0[1]);
        const auto& t0_point2 = fsubdiv0.point(tri0[2]);
        if (t1_point0 == t0_point0 && t1_point1 == t0_point1
            && t1_point2 == t0_point2) {
          mapping[tri1[0]] = tri0[0];
          mapping[tri1[1]] = tri0[1];
          mapping[tri1[2]] = tri0[2];
        } else if (t1_point0 == t0_point1 && t1_point1 == t0_point2
                   && t1_point2 == t0_point0) {
          mapping[tri1[0]] = tri0[1];
          mapping[tri1[1]] = tri0[2];
          mapping[tri1[2]] = tri0[0];
        } else if (t1_point0 == t0_point2 && t1_point1 == t0_point0
                   && t1_point2 == t0_point1) {
          mapping[tri1[0]] = tri0[2];
          mapping[tri1[1]] = tri0[0];
          mapping[tri1[2]] = tri0[1];
        }
      }
    }
  }

  return true;  //AJT::changed from false to true
}
//----------------------------------------------------------------------------

bool
BaseMeshEncoder::computeDracoMapping(
  vmesh::TriangleMesh<MeshType>    base,
  std::vector<int32_t>&            mapping,
  const int32_t                    frameIndex,
  int32_t                          submeshIndex,
  const BaseMeshEncoderParameters& params) const {
  // Save intermediate files
  if (params.keepIntermediateFiles) {
    base.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
              + "_mapping_src.ply");
  }

  // simulation for edge-based subdivision
  auto                          simEncBase = base;
  vmesh::TriangleMesh<MeshType> simRecBase;
  if (params.subdivisionEdgeLengthThreshold > 0) {
    const double simScalePosition = ((1 << params.qpPosition) - 1.0)
                                    / ((1 << params.bitDepthPosition) - 1.0);
    // quantize base mesh
    for (int32_t v = 0, vcount = simEncBase.pointCount(); v < vcount; ++v) {
      simEncBase.setPoint(v,
                          Clamp(Round(simEncBase.point(v) * simScalePosition),
                                0.0,
                                ((1 << params.qpPosition) - 1.0)));
    }
    if (params.iDeriveTextCoordFromPos == 0) {
      // texture coordinates were sent inside the base mesh, performing de-quantization before coding
      //for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount; ++tc) {
      //  base.setTexCoord(tc, Round(base.texCoord(tc) * scaleTexCoord));
      //}
    }
    // Encode
    vmesh::GeometryEncoderParameters simEncoderParams;

    simEncoderParams.qn_               = params.qpNormals;
    simEncoderParams.encodeNormals     = params.encodeNormals;
    simEncoderParams.predNormal        = params.predNormal;
    simEncoderParams.normalsOctahedral = params.normalsOctahedral;
    simEncoderParams.entropyPacket     = params.entropyPacket;
    simEncoderParams.qpOcta_           = params.qpOcta;

    simEncoderParams.qp_ = params.qpPosition;
    simEncoderParams.qt_ = params.qpTexCoord;
    simEncoderParams.qg_ = -1;
    simEncoderParams.dracoUsePosition_      = params.dracoUsePosition;
    simEncoderParams.dracoUseUV_            = params.dracoUseUV;
    simEncoderParams.dracoMeshLossless_     = params.dracoMeshLossless;
    simEncoderParams.traversal_             = params.baseMeshVertexTraversal;
    simEncoderParams.reindexOutput_         = params.motionVertexTraversal;
    simEncoderParams.baseMeshDeduplication_ = params.baseMeshDeduplication;
    simEncoderParams.reverseUnification_    = params.reverseUnification;
    simEncoderParams.profile                = params.profileGeometryCodec;

    std::vector<uint8_t> tmpBitstream;
    auto                 encoder =
      vmesh::GeometryEncoder<MeshType>::create(params.meshCodecId);
    encoder->encode(simEncBase, simEncoderParams, tmpBitstream, simRecBase);

    const double simIscalePosition = 1.0 / simScalePosition;
    for (int32_t v = 0, vcount = simRecBase.pointCount(); v < vcount; ++v) {
      simRecBase.setPoint(v, simRecBase.point(v) * simIscalePosition);
    }
  }

  // Scale
  const auto scalePosition = 1 << (18 - params.bitDepthPosition);
  const auto scaleTexCoord = 1 << 18;
  for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
    base.setPoint(v, vmesh::Vec3<MeshType>(1 << 18) + Round(base.point(v) * (MeshType)scalePosition));
  }
  for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount; ++tc) {
    base.setTexCoord(tc, Round(base.texCoord(tc) * scaleTexCoord));
  }
  // Save intermediate files
  if (params.keepIntermediateFiles) {
    base.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
              + "_mapping_scale.obj");
  }

  // Encode
  vmesh::GeometryEncoderParameters encoderParams;
  // remove this one ??
  encoderParams.cl_ = 10;  // this is the default for draco
  //  were not specified before, using scale, extend to 20bits as base values may extend beyond the original scale
  encoderParams.qp_ = 20;
  // in practice 19 bits are used for texcoords as 1.0 is scaled as 0x4000
  // this only has incidence when qp-bits packing is used in the encoder
  encoderParams.qt_ = 19;
  encoderParams.qg_ = 19;

  encoderParams.qn_               = 18;
  encoderParams.qpOcta_           = 17;
  encoderParams.normalsOctahedral = params.normalsOctahedral;
  encoderParams.entropyPacket     = params.entropyPacket;
  encoderParams.encodeNormals     = params.encodeNormals;
  encoderParams.predNormal        = params.predNormal;
  encoderParams.predGeneric       = params.predGeneric;
  // end remove those
  encoderParams.dracoUsePosition_      = params.dracoUsePosition;
  encoderParams.dracoUseUV_            = params.dracoUseUV;
  encoderParams.dracoMeshLossless_     = params.dracoMeshLossless;
  encoderParams.traversal_             = params.baseMeshVertexTraversal;
  encoderParams.reindexOutput_         = params.motionVertexTraversal;
  encoderParams.baseMeshDeduplication_ = params.baseMeshDeduplication;
  encoderParams.reverseUnification_    = params.reverseUnification;
  encoderParams.verbose                = false;

  vmesh::TriangleMesh<MeshType> rec;
  std::vector<uint8_t>          geometryBitstream;
  auto encoder = vmesh::GeometryEncoder<MeshType>::create(params.meshCodecId);
  printf("DracoMapping: use_position = %d use_uv = %d mesh_lossless = %d \n",
         encoderParams.dracoUsePosition_,
         encoderParams.dracoUseUV_,
         encoderParams.dracoMeshLossless_);
  encoder->encode(base, encoderParams, geometryBitstream, rec);

  // Save intermediate files
  if (params.keepIntermediateFiles) {
    auto prefix =
      keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex) + "_mapping";
    base.save(prefix + "_enc.ply");
    rec.save(prefix + "_rec.ply");
    vmesh::save(prefix + ".drc", geometryBitstream);
  }

  // get mapping between basemesh and reconstructed basemesh for edge-based subdivision
  std::vector<int32_t> baseMapping;
  if (params.subdivisionEdgeLengthThreshold > 0) {
    auto                 refRecBase = rec;
    auto                 refBase    = base;
    std::vector<int32_t> texCoordToPoint0;
    if (refBase.texCoordCount() > 0)
      refBase.computeTexCoordToPointMapping(texCoordToPoint0);
    struct ArrayHasher {
      std::size_t operator()(const std::array<double, 5>& a) const {
        std::size_t h = 0;
        for (auto e : a) {
          h ^= std::hash<double>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
      }
    };
    std::map<std::array<double, 5>, int32_t> baseMap0;
    const auto baseTexCoordCount0 = refBase.texCoordCount();
    const auto baseCoordCount0    = refBase.pointCount();
    int32_t    vCount =
      baseTexCoordCount0 > 0 ? baseTexCoordCount0 : baseCoordCount0;
    for (int32_t v = 0; v < vCount; ++v) {
      const auto&                 point0        = baseTexCoordCount0 > 0
                                                    ? refBase.point(std::max(0, texCoordToPoint0[v]))
                                                    : refBase.point(v);
      const auto&                 baseTexCoord0 = baseTexCoordCount0 > 0
                                                    ? refBase.texCoord(v)
                                                    : vmesh::Vec2<MeshType>(0.0);
      const std::array<double, 5> vertex0{
        point0[0], point0[1], point0[2], baseTexCoord0[0], baseTexCoord0[1]};
      baseMap0[vertex0] = baseTexCoordCount0 > 0 ? texCoordToPoint0[v] : v;
    }

    const auto basePointCount1 = refRecBase.pointCount();
    baseMapping.resize(basePointCount1, -1);
    std::vector<bool> tags(basePointCount1, false);
    for (int32_t t = 0, tcount = refRecBase.triangleCount(); t < tcount; ++t) {
      const auto& tri = refRecBase.triangle(t);
      const auto& triUV =
        refRecBase.texCoordCount() > 0 ? refRecBase.texCoordTriangle(t) : tri;
      for (int32_t k = 0; k < 3; ++k) {
        const auto indexPos = tri[k];
        if (tags[indexPos]) { continue; }
        tags[indexPos]                            = true;
        const auto                  indexTexCoord = triUV[k];
        const auto&                 point1        = refRecBase.point(indexPos);
        const auto&                 texCoord1 = refRecBase.texCoordCount() > 0
                                                  ? refRecBase.texCoord(indexTexCoord)
                                                  : vmesh::Vec2<MeshType>(0.0);
        const std::array<double, 5> vertex1   = {
          point1[0], point1[1], point1[2], texCoord1[0], texCoord1[1]};
        const auto it = baseMap0.find(vertex1);
        if (it != baseMap0.end()) {
          baseMapping[indexPos] = baseMap0[vertex1];
          auto& pointSimEncBase = simEncBase.point(baseMapping[indexPos]);
          pointSimEncBase       = simRecBase.point(indexPos);
        } else {
          assert(0);
        }
      }
    }
  }

  std::vector<vmesh::TriangleMesh<MeshType>> refMeshes(
    params.subdivisionIterationCount),
    refRecMeshes(params.subdivisionIterationCount);
  if (params.subdivisionEdgeLengthThreshold > 0) {
    for (int32_t it = 0; it < params.subdivisionIterationCount; ++it) {
      refMeshes[it] = simEncBase;
      refMeshes[it].subdivideMesh(it);
      refRecMeshes[it] = simRecBase;
      refRecMeshes[it].subdivideMesh(it);
    }
  }

  // Geometry parametrisation base
  auto fsubdiv0 = base;
  if (!params.increaseTopSubmeshSubdivisionCount)
    fsubdiv0.subdivideMesh(params.subdivisionIterationCount,
                           params.subdivisionEdgeLengthThreshold,
                           params.bitshiftEdgeBasedSubdivision,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           &refMeshes);
  else
    fsubdiv0.subdivideMesh(submeshIndex == 0
                             ? params.subdivisionIterationCount
                                 ? params.subdivisionIterationCount + 1
                                 : params.subdivisionIterationCount
                             : params.subdivisionIterationCount,
                           params.subdivisionEdgeLengthThreshold,
                           params.bitshiftEdgeBasedSubdivision,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           &refMeshes);
  if (params.keepIntermediateFiles) {
    fsubdiv0.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                  + "_mapping_fsubdiv0.ply");
  }
  std::vector<int32_t> texCoordToPoint0;
  if (fsubdiv0.texCoordCount() > 0)
    fsubdiv0.computeTexCoordToPointMapping(texCoordToPoint0);
  struct ArrayHasher {
    std::size_t operator()(const std::array<double, 5>& a) const {
      std::size_t h = 0;
      for (auto e : a) {
        h ^= std::hash<double>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
      }
      return h;
    }
  };
  std::map<std::array<double, 5>, int32_t> map0;
  const auto texCoordCount0 = fsubdiv0.texCoordCount();
  const auto coordCount0    = fsubdiv0.pointCount();
  int32_t    vCount = texCoordCount0 > 0 ? texCoordCount0 : coordCount0;
  for (int32_t v = 0; v < vCount; ++v) {
    const auto& point0 = texCoordCount0 > 0
                           ? fsubdiv0.point(std::max(0, texCoordToPoint0[v]))
                           : fsubdiv0.point(v);
    const auto& texCoord0 =
      texCoordCount0 > 0 ? fsubdiv0.texCoord(v) : vmesh::Vec2<MeshType>(0.0);
    const std::array<double, 5> vertex0{
      point0[0], point0[1], point0[2], texCoord0[0], texCoord0[1]};
    map0[vertex0] = texCoordCount0 > 0 ? texCoordToPoint0[v] : v;
  }

  // Geometry parametrisation rec (after compression)
  auto fsubdiv1 = rec;
  if (!params.increaseTopSubmeshSubdivisionCount)
    fsubdiv1.subdivideMesh(params.subdivisionIterationCount,
                           params.subdivisionEdgeLengthThreshold,
                           params.bitshiftEdgeBasedSubdivision,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           &refRecMeshes);
  else
    fsubdiv1.subdivideMesh(submeshIndex == 0
                             ? params.subdivisionIterationCount
                                 ? params.subdivisionIterationCount + 1
                                 : params.subdivisionIterationCount
                             : params.subdivisionIterationCount,
                           params.subdivisionEdgeLengthThreshold,
                           params.bitshiftEdgeBasedSubdivision,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           &refRecMeshes);
  if (params.keepIntermediateFiles) {
    fsubdiv1.save(keepFilesPathPrefix_ + "fr_" + std::to_string(frameIndex)
                  + "_mapping_fsubdiv1.ply");
  }
  const auto pointCount1 = fsubdiv1.pointCount();
  mapping.resize(pointCount1, -1);
  std::vector<bool> tags(pointCount1, false);
  for (int32_t t = 0, tcount = fsubdiv1.triangleCount(); t < tcount; ++t) {
    const auto& tri = fsubdiv1.triangle(t);
    const auto& triUV =
      fsubdiv1.texCoordCount() > 0 ? fsubdiv1.texCoordTriangle(t) : tri;
    for (int32_t k = 0; k < 3; ++k) {
      const auto indexPos = tri[k];
      if (tags[indexPos]) { continue; }
      tags[indexPos]                            = true;
      const auto                  indexTexCoord = triUV[k];
      const auto&                 point1        = fsubdiv1.point(indexPos);
      const auto&                 texCoord1     = fsubdiv1.texCoordCount() > 0
                                                    ? fsubdiv1.texCoord(indexTexCoord)
                                                    : vmesh::Vec2<MeshType>(0.0);
      const std::array<double, 5> vertex1       = {
        point1[0], point1[1], point1[2], texCoord1[0], texCoord1[1]};
      const auto it = map0.find(vertex1);
      if (it != map0.end()) {
        mapping[indexPos] = map0[vertex1];
      } else {
        assert(0);
      }
    }
  }

  return true;
}

}  // namespace basemesh
