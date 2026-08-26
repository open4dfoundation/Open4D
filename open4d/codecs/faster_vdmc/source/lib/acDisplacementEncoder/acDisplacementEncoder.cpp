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

#include <cmath>

#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "motionContexts.hpp"
#include "entropy.hpp"
#include "vmc.hpp"
#include "util/misc.hpp"
#include "acDisplacementEncoder.hpp"
#include "acDisplacementBitstream.hpp"
#include "acDisplacementContext.hpp"

using namespace acdisplacement;

//============================================================================
//construct refListStruct in DSPS from input parameters, encParams.refFrameDiff
void
AcDisplacementEncoder::constructDspsRefListStruct(DisplSequenceParameterSetRbsp& dsps,
                           const AcDisplacementEncoderParameters&    encParams) {
  //refFrameDiff:1,2,3,4...
  size_t numRefList          = encParams.maxNumRefList;
  size_t numActiveRefEntries = encParams.maxNumRefFrame;
  for (size_t list = 0; list < numRefList; list++) {
    DisplRefListStruct refList;
    refList.setNumRefEntries(numActiveRefEntries);
    refList.allocate();
    for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
      int mfocDiff = -1;
      if (i == 0)
        mfocDiff =
          encParams.refFrameDiff
            [0];  //the absolute difference between the foc values of current & ref
      else
        mfocDiff =
          encParams.refFrameDiff[i]
          - encParams.refFrameDiff
              [i
               - 1];  //the absolute difference between the foc values ref[i-1] & ref[i]
      refList.setAbsDeltaDfocSt(i, std::abs(mfocDiff));
      refList.setStrafEntrySignFlag(
        i, mfocDiff < 0 ? false : true);  //0.minus 1.plus
      refList.setStRefdisplFrameFlag(i, true);
    }
    dsps.addDisplRefListStruct(refList);
  }
}

//============================================================================
void
AcDisplacementEncoder::createDispParameterSet(DisplSequenceParameterSetRbsp& dsps,
                       DisplFrameParameterSetRbsp&    dfps,
                       const AcDisplacementEncoderParameters&    params) {
  int32_t dspsIndex = 0;
  int32_t dfpsIndex = 0;

  // we setup maximum for now
  dsps.getDspsProfileTierLevel().setDptlProfileToolsetIdc(DisplProfileIdc::DISPLACEMENT_PROFILE_AC_DISPLACEMENT_MAIN);
  dsps.getDspsProfileTierLevel().setDptlTierFlag(DisplTierFlag::DISPLACEMENT_TIER_0);
  dsps.getDspsProfileTierLevel().setDptlLevelIdc(DisplLevelIdc::DISPLACEMENT_LEVEL_2_2);

  dsps.setDspsSequenceParameterSetId(dspsIndex);
  dsps.setDspsMaxSubLayersMinus1(0); // fixed as 0 in this version
  dsps.setDspsSingleDimensionFlag(params.applyOneDimensionalDisplacement);
  dsps.setDspsLog2MaxDisplFrameOrderCntLsbMinus4(6);
  dsps.setDspsNumRefDisplFrameListsInDsps(1);
  dsps.setDspsGeometry3dBitdepthMinus1(params.bitDepthPosition - 1);
  dsps.setDspsSubdivisionPresentFlag(
    static_cast<bool>(params.subdivisionIterationCount));
  dsps.setDspsSubdivisionIterationCount(
    uint32_t(params.subdivisionIterationCount));
  dsps.setDspsInverseQuantizationOffsetPresentFlag(
    (params.transformMethod == 0)
      ? false
      : params.InverseQuantizationOffsetFlag);  //m68642

  auto& qp       = dsps.getDspsQuantizationParameters();
  auto  lodCount = dsps.getDspsSubdivisionIterationCount() + 1;
  qp.setNumLod(lodCount);
  auto dimCount = params.applyOneDimensionalDisplacement ? 1 : 3;
  qp.setNumComponents(dimCount);
  qp.setDisplLodQuantizationFlag(params.lodDisplacementQuantizationFlag);
  qp.setDisplBitDepthOffset(params.bitDepthOffset);
  if (params.lodDisplacementQuantizationFlag == 0) {
    for (int d = 0; d < dimCount; d++) {
      qp.setDisplQuantizationParameters(d, params.liftingQP[d]);
      qp.setDisplLog2LodInverseScale(d,
                                     params.log2LevelOfDetailInverseScale[d]);
    }
  } else {
    for (int l = 0; l < lodCount; l++) {
      qp.allocComponents(dimCount, l);
      for (int d = 0; d < dimCount; d++) {
        int8_t value = params.qpPerLevelOfDetails[l][d]
                       - (dsps.getDspsDisplacementReferenceQPMinus49() + 49);
        qp.setDisplLodDeltaQPValue(l, d, std::abs(value));
        qp.setDisplLodDeltaQPSign(l, d, (value < 0) ? 1 : 0);
      }
    }
  }
  constructDspsRefListStruct(dsps, params);
  dfps.setDfpsDisplSequenceParameterSetId(dspsIndex);
  dfps.setDfpsDisplFrameParameterSetId(dfpsIndex);
  dfps.getDisplInformation().getDiDisplIds().resize(
    params.submeshIdList.size(), 0);
  dfps.getDisplInformation().getDiSignalledDisplIdFlag() =
    params.enableSignalledIds;
  if (params.numSubmesh == 1) {
    dfps.getDisplInformation().getDiUseSingleDisplFlag() = true;
  } else {
    dfps.getDisplInformation().getDiUseSingleDisplFlag() = false;
    dfps.getDisplInformation().getDiNumDisplsMinus2() = params.numSubmesh - 2;
  }
  auto numDispls =
    static_cast<size_t>(dfps.getDisplInformation().getDiNumDisplsMinus2() + 2);
  if (dfps.getDisplInformation().getDiSignalledDisplIdFlag()) {
    dfps.getDisplInformation()._displIndexToID.resize(numDispls);
    uint32_t maxVal = 0;
    for (size_t i = 0; i < numDispls; i++) {
      dfps.getDisplInformation().setDiDisplId(i, params.submeshIdList[i]);
      maxVal = std::max(maxVal, params.submeshIdList[i]);
      dfps.getDisplInformation()._displIndexToID[i] =
        dfps.getDisplInformation().getDiDisplId(i);
      if (dfps.getDisplInformation()._displIDToIndex.size()
          <= dfps.getDisplInformation()._displIndexToID[i]) {
        dfps.getDisplInformation()._displIDToIndex.resize(
          dfps.getDisplInformation()._displIndexToID[i] + 1, -1);
      }
      dfps.getDisplInformation()
        ._displIDToIndex[dfps.getDisplInformation()._displIndexToID[i]] =
        static_cast<uint32_t>(i);
    }
    uint32_t v = (maxVal < 2 ? 1 : vmesh::CeilLog2(maxVal + 1));
    uint32_t b = vmesh::CeilLog2(numDispls);
    dfps.getDisplInformation().getDiSignalledDisplIdDeltaLength() = (v - b);
  } else {
    dfps.getDisplInformation().getDiSignalledDisplIdDeltaLength() = 0;
    dfps.getDisplInformation()._displIDToIndex.resize(numDispls);
    dfps.getDisplInformation()._displIndexToID.resize(numDispls);
    for (size_t i = 0; i < numDispls; i++) {
      dfps.getDisplInformation()._displIDToIndex[i] = static_cast<uint32_t>(i);
      dfps.getDisplInformation()._displIndexToID[i] = static_cast<uint32_t>(i);
    }
  }
}

void
AcDisplacementEncoder::constructDisplRefListStruct(DisplSequenceParameterSetRbsp& dsps,
                            DisplFrameParameterSetRbsp&    dfps,
                            DisplacementLayer&             dl,
                            int32_t                        submeshIndex,
                            int32_t                        frameIndex,
                            int32_t referenceFrameIndex) {
  int32_t numRefFrame = 1;
  //referenceFrameIndex = 0,1,2,3...31
  if (referenceFrameIndex < 0) {
    numRefFrame = 0;
    //TODO: [sw] referenceFrameIndex in SKIP (why does it change SKIP to I?)
    //sml.getSubmeshHeader().setSmhType(I_BASEMESH);
    dl.getDisplHeader().setRefDisplFrameListDspsFlag(true);
    dl.getDisplHeader().setRefDisplFrameListIdx(0);
    return;
  } else {
    // sml.getSubmeshHeader().setSmhType(P_BASEMESH);
  }

  //Note: checking if refFrameIndex is present in dsps.refList
  //Note: FrameIndex is different from refIndex smduInterRefIndex is refIndex. refList[refIndex] = refFrameIndex
  int16_t foundRefListIndex  = -1;
  int16_t foundRefFrameIndex = -1;
  int16_t foundRefIndex      = -1;
  for (size_t refListIdx = 0;
       refListIdx < dsps.getDspsNumRefDisplFrameListsInDsps();
       refListIdx++) {
    auto&   refList  = dsps.getDisplRefListStruct()[refListIdx];
    int32_t dfocBase = frameIndex;
    for (size_t refIdx = 0; refIdx < refList.getNumRefEntries(); refIdx++) {
      int16_t deltaDfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                            * refList.getAbsDeltaDfocSt(refIdx);
      auto refFrameIndex = dfocBase - deltaDfocSt;
      dfocBase           = refFrameIndex;
      if (refFrameIndex == referenceFrameIndex) {
        foundRefListIndex  = refListIdx;
        foundRefIndex      = refIdx;
        foundRefFrameIndex = refFrameIndex;
        break;
      }
    }
  }
  if (foundRefListIndex != -1 && foundRefFrameIndex != -1) {
    dl.getDisplHeader().setRefDisplFrameListDspsFlag(true);
    dl.getDisplHeader().setRefDisplFrameListIdx(foundRefListIndex);
    dl.getDisplHeader().setReferenceFrameIndex(foundRefIndex);
#if 0
    printf("frameIndex[%d] submeshIndex[%d] foundRefFrameIdx: mspsRefList[%d][%d]=%d\n", frameIndex, submeshIndex, foundRefListIndex, foundRefIndex, foundRefFrameIndex);
#endif
  } else {
    //new reference structure
    dl.getDisplHeader().setRefDisplFrameListDspsFlag(false);
    DisplRefListStruct refList;
    refList.setNumRefEntries(numRefFrame);
    refList.allocate();
    for (size_t i = 0; i < refList.getNumRefEntries(); i++) {
      int dfocDiff = -1;
      if (i == 0) {
        dfocDiff = frameIndex - referenceFrameIndex;
      } else {
        dfocDiff = frameIndex - referenceFrameIndex;
      }
      refList.setAbsDeltaDfocSt(i, std::abs(dfocDiff));
      refList.setStrafEntrySignFlag(i, dfocDiff < 0 ? false : true);
      refList.setStRefdisplFrameFlag(i, true);
    }
    dl.getDisplHeader().setDisplRefListStruct(refList);
    dl.getDisplHeader().setReferenceFrameIndex(0);
  }

  //size_t numActiveRefEntries=1;
  //refoc = afocbase - diff
  if (numRefFrame > 0) {
    bool bNumRefIdxActiveOverrideFlag = false;
    if ((uint32_t)numRefFrame
        >= (dfps.getDfpsNumRefIdxDefaultActiveMinus1())) {
      bNumRefIdxActiveOverrideFlag =
        ((uint32_t)numRefFrame
         != (dfps.getDfpsNumRefIdxDefaultActiveMinus1() + 1));
    } else {
      bNumRefIdxActiveOverrideFlag =
        numRefFrame
        != dl.getDisplHeader().getDisplRefListStruct().getNumRefEntries();
    }
    dl.getDisplHeader().setNumRefIdxActiveOverrideFlag(
      bNumRefIdxActiveOverrideFlag);
    if (dl.getDisplHeader().getNumRefIdxActiveOverrideFlag())
      dl.getDisplHeader().setNumRefIdxActiveMinus1(numRefFrame - 1);
  }
}

int32_t
AcDisplacementEncoder::compressACDisplacements(AcDisplacementBitstream&      dmSubstream,
                                    std::vector<vmesh::VMCSubmesh>&    gof,
                                    int32_t                     submeshIndex,
                                    int32_t                     frameCount,
                                    const AcDisplacementEncoderParameters& params) {
  std::cout << "\n(compressDisplacementsAC ) \n";
  int32_t dspsIndex = 0;
  int32_t dfpsIndex = 0;

  auto&      dsps = dmSubstream.getDisplSequenceParameterSet(dspsIndex);
  auto&      dfps = dmSubstream.getDisplFrameParameterSet(dfpsIndex);
  int32_t    totalByteCount = 0;
  const auto dispDimensions = params.applyOneDimensionalDisplacement ? 1 : 3;
  const auto numSubBlocks   = params.subBlocksPerLoD;
  const auto lodSubdivStr   = params.blockSubdiv;
  int32_t    lodCount       = int32_t(gof[0].subdivInfoLevelOfDetails.size());

  for (int32_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    auto& frame = gof[frameIndex];
    auto& layer = dmSubstream.addDisplacementLayer();
    layer.getDisplHeader().setDisplFrameParameterSetId(dfpsIndex);
    layer.getDisplHeader().setDisplFrameParameterSetId(0);
    layer.getDisplHeader().setDisplId(params.submeshIdList[submeshIndex]);
    if (frame.submeshType == basemesh::I_BASEMESH){
      layer.getDisplHeader().setDisplType(I_DISPL);
    }
    if (frame.submeshType == basemesh::P_BASEMESH){
      layer.getDisplHeader().setDisplType(P_DISPL);
    }
    layer.getDisplHeader().setDisplFrmOrderCntLsb(frameIndex);

      layer.getDisplHeader().setDhSubdivisionIterationCount(lodCount - 1);
      for (int i = 0; i < lodCount; i++) {
        int32_t vertexCountCurLoD = 0;
        vertexCountCurLoD = (i==0) ? frame.subdivInfoLevelOfDetails[i].pointCount : frame.subdivInfoLevelOfDetails[i].pointCount - frame.subdivInfoLevelOfDetails[i - 1].pointCount;
        layer.getDisplHeader().setDhVertexCountLod(
          i, vertexCountCurLoD);
        layer.getDisplHeader().setDhNumSubblockLodMinus1(i, numSubBlocks[i] - 1);
        layer.getDisplHeader().setDhLodSplitFlag(i, lodSubdivStr[i]);
      }
      std::vector<vmesh::Vec3<MeshType>> disp(frame.disp.size(),
                                              vmesh::Vec3<MeshType>(0.0, 0.0, 0.0));

      int32_t ctxIdx;
      if (frame.submeshType == basemesh::I_BASEMESH) {
        std::cout << "FrameIndex: " << frameIndex
                  << "  submesh: " << submeshIndex << " dispType: "
                  << toString(
                       (DisplacementType)layer.getDisplHeader().getDisplType())
                  << "\n";
        layer.getDisplHeader().setReferenceFrameIndex(-1);
        constructDisplRefListStruct(
          dsps,
          dfps,
          layer,
          submeshIndex,
          frameIndex,
          layer.getDisplHeader().getReferenceFrameIndex());

        ctxIdx = 0;
      } else {
        std::cout << "FrameIndex: " << frameIndex
                  << "  submesh: " << submeshIndex << " dispType: "
                  << toString(
                       (DisplacementType)layer.getDisplHeader().getDisplType())
                  << "\t";
        std::cout << "reference frame(absolute): " << frameIndex - 1 << "\n";
        //refrence frame
        layer.getDisplHeader().setReferenceFrameIndex(frameIndex - 1);
        constructDisplRefListStruct(
          dsps,
          dfps,
          layer,
          submeshIndex,
          frameIndex,
          layer.getDisplHeader().getReferenceFrameIndex());
        int8_t displLayerInter = 1;
        layer.getDisplHeader().setNumLoDInter(lodCount);
        for (int32_t it = 0; it < lodCount; it++) {
          const auto vcount0 =
            it == 0 ? 0 : frame.subdivInfoLevelOfDetails[it - 1].pointCount;
          const auto vcount1  = frame.subdivInfoLevelOfDetails[it].pointCount;
          double     sum_cur  = 0;
          double     sum_diff = 0;
          for (int32_t v = vcount0; v < vcount1; ++v) {
            sum_cur += abs(frame.disp[v][0]);
            sum_diff += abs(frame.disp[v][0] - gof[frameIndex - 1].disp[v][0]);
          }
          for (int32_t v = vcount0; v < vcount1; ++v) {
            if (dispDimensions == 1) {
              frame.disp[v][0] =
                (sum_diff < sum_cur && v < gof[frameIndex - 1].disp.size())
                  ? frame.disp[v][0] - gof[frameIndex - 1].disp[v][0]
                  : frame.disp[v][0];
            } else if (dispDimensions == 3) {
              frame.disp[v] =
                (sum_diff < sum_cur && v < gof[frameIndex - 1].disp.size())
                  ? frame.disp[v] - gof[frameIndex - 1].disp[v]
                  : frame.disp[v];
            }
          }
          layer.getDisplHeader().setDhLoDInter(it,
                                               ((sum_diff < sum_cur) ? 1 : 0));
          displLayerInter &= layer.getDisplHeader().getDhLoDInter(it);
        }
        layer.getDisplHeader().setDhLayerInterDisableFlag(displLayerInter);
        ctxIdx = 1;
      }

      frame.lodCountRef = lodCount;

      quantizeDisplacements(frame, submeshIndex, params);
      if (params.displacementFillZerosFlag) { fillDisZeros(frame, params); }

      if (dispDimensions == 1) {
        for (int32_t i = 0; i < disp.size(); i++) {
          disp[i][0] = frame.disp[i][0];
        }
      } else if (dispDimensions == 3) {
        disp = frame.disp;
      }

      std::vector<std::vector<AcDisplacementContext>> ctxDisp(2);
      AcDisplacementContext                           ctxBypass;
      ctxDisp[0].resize(lodCount);
      ctxDisp[1].resize(lodCount);
      vmesh::EntropyEncoder dispEncoder;
      int32_t maxAcBufLen = (int32_t)(gof[0].disp.size() * gof.size()) * 3;
      dispEncoder.setBuffer(maxAcBufLen, nullptr);
      dispEncoder.start();

      std::vector<uint32_t>          verAccCount(lodCount);
      std::vector<uint32_t>          verCountLoD(lodCount);
      std::vector<std::vector<bool>> nzSubBlock(lodCount);
      std::vector<bool>              emptyLevel(lodCount, false);
      for (int l = 0; l < lodCount; l++) {
        verAccCount[l] = frame.subdivInfoLevelOfDetails[l].pointCount;
        verCountLoD[l] =
          l == 0 ? frame.subdivInfoLevelOfDetails[l].pointCount
                 : frame.subdivInfoLevelOfDetails[l].pointCount
                     - frame.subdivInfoLevelOfDetails[l - 1].pointCount;
      }

      for (int32_t dim = 0; dim < dispDimensions; dim++) {
        for (int32_t level = 0; level < lodCount; ++level) {
          auto logBlockSize = (int32_t)std::ceil(
            log2((double)verCountLoD[level]
                 / (lodSubdivStr[level] ? (double)numSubBlocks[level]
                                        : (double)1)));
          auto blockSize = 1 << logBlockSize;
          nzSubBlock[level].resize(numSubBlocks[level]);
        }
        for (int32_t level = 0; level < lodCount; ++level) {
          auto vStart       = (level == 0) ? 0 : verAccCount[level - 1];
          auto logBlockSize = (int32_t)std::ceil(
            log2((double)verCountLoD[level]
                 / (lodSubdivStr[level] ? (double)numSubBlocks[level]
                                        : (double)1)));
          auto blockSize = 1 << logBlockSize;
          nzSubBlock[level].resize(numSubBlocks[level], false);
          for (int32_t block = 0; block < numSubBlocks[level]; block++) {
            auto    vBlockStart = vStart + block * blockSize;
            int32_t vBlockEnd =
              std::min((int32_t)(vBlockStart + (int32_t)blockSize),
                       (int32_t)disp.size());
            for (int32_t v = vBlockStart; v < vBlockEnd; v++) {
              if (disp[v][dim] != 0) {
                nzSubBlock[level][block] = true;
                emptyLevel[level]        = false;
                break;
              }
            }  //v
          }    //block
        }
        for (int32_t level = 0; level < lodCount; ++level) {
          auto vStart = (level == 0) ? 0 : verAccCount[level - 1];
          auto blockSize =
            1 << (int32_t)std::ceil(
              log2((double)verCountLoD[level]
                   / (lodSubdivStr[level] ? (double)numSubBlocks[level]
                                          : (double)1)));
          for (int32_t block = 0; block < numSubBlocks[level]; block++) {
            dispEncoder.encode(nzSubBlock[level][block],
                               ctxDisp[ctxIdx][level].ctxCodedSubBlock[dim]);
            if (nzSubBlock[level][block]) {
              auto    vBlockStart = vStart + block * blockSize;
              int32_t vBlockEnd = std::min((int32_t)(vBlockStart + blockSize),
                                           (int32_t)verAccCount[level]);
              for (int32_t v = vBlockStart; v < vBlockEnd; v++) {
                auto d = disp[v][dim];
                dispEncoder.encode(d != 0,
                                   ctxDisp[ctxIdx][level].ctxCoeffGtN[0][dim]);
                if (!d) { continue; }
                dispEncoder.encode(d < 0, ctxBypass.ctxStatic);
                d               = std::abs(d);
                int32_t currGtN = 1;
                while (currGtN <= 3) {
                  d = std::abs(d) - 1;
                  dispEncoder.encode(
                    d != 0, ctxDisp[ctxIdx][level].ctxCoeffGtN[currGtN][dim]);
                  if (!d) { break; }
                  currGtN++;
                }
                if (!d) { continue; }
                assert(d > 0);
                dispEncoder.encodeExpGolomb(
                  --d, 0, ctxDisp[0][0].ctxCoeffRemPrefix, 3);
              }  //v
            }    //if(nzSubBlock[level][block])
          }      //block
        }
      }  //dim<3

      // Convert double to int64_t before inverse quant (fixed-point)
      int64_t fixedpointprecInt = DISPL_INVQUANT_FIXEDP;
      frame.dispi.assign(disp.size(), vmesh::Vec3<int64_t>(0, 0, 0));
      for (int32_t i = 0, length = frame.dispi.size(); i < length; ++i) {
        for (int32_t j = 0; j < dispDimensions; ++j) {
          frame.dispi[i][j] = (int64_t)disp[i][j] << fixedpointprecInt;
        }
      }
      // determining the inverse quantization scale
      int dispDimension =
        (params.applyOneDimensionalDisplacement == 1) ? 1 : 3;
      int lodCount = frame.subdivInfoLevelOfDetails.size();
      std::vector<std::vector<int64_t>> iscale;
      std::vector<std::vector<int32_t>> QuantizationParameter;
      std::vector<std::vector<int64_t>> InverseScale;

      //initialization
      iscale.resize(lodCount);
      for (int i = 0; i < lodCount; ++i) iscale[i].resize(dispDimension, 0);
      QuantizationParameter.resize(lodCount);
      for (int l = 0; l < lodCount; ++l)
        QuantizationParameter[l].resize(dispDimension, 0);
      InverseScale.resize(lodCount);
      for (int i = 0; i < lodCount; ++i)
        InverseScale[i].resize(dispDimension, 0);

      //QuantizationParameter
      if (params.lodDisplacementQuantizationFlag == 0) {
        for (int d = 0; d < dispDimension; ++d)
          for (int l = 0; l < lodCount; ++l)
            QuantizationParameter[l][d] = params.liftingQP[d];
      } else {
        for (int l = 0; l < lodCount; ++l)
          for (int d = 0; d < dispDimension; ++d) {
            QuantizationParameter[l][d] = params.qpPerLevelOfDetails[l][d];
          }
      }

      //InverseScale
      int32_t q, r;
      int64_t r6table[6] = {2048, 2299, 2580, 2896, 3251, 3649};
      for (int l = 0; l < lodCount; ++l)
        for (int d = 0; d < dispDimension; ++d) {
          const auto qp = QuantizationParameter[l][d];
          assert(qp >= 4 && qp <= 100);
          q = (qp - 4) / 6;
          r = (qp - 4) - 6 * q;
          assert(q >= 0 && q <= 16);
          assert(r >= 0 && r <= 5);
          assert(params.bitDepthPosition >= 4
                 && params.bitDepthPosition <= 16);
          assert(params.bitDepthOffset <= params.bitDepthPosition);
          InverseScale[l][d] =
            (qp - 4) >= 0
              ? (r6table[r]
                 << (params.bitDepthPosition - params.bitDepthOffset + q - 4))
              : 0;
        }

      // iscale
      if (params.lodDisplacementQuantizationFlag) {
        for (int l = 0; l < lodCount; ++l)
          for (int d = 0; d < dispDimension; ++d)
            iscale[l][d] = InverseScale[l][d];
      } else {
        for (int d = 0; d < dispDimension; ++d)
          iscale[0][d] = InverseScale[0][d];
        for (int l = 1; l < lodCount; ++l)
          for (int d = 0; d < dispDimension; ++d)
            iscale[l][d] =
              (iscale[l - 1][d] << params.log2LevelOfDetailInverseScale[d]);
      }

      std::vector<std::vector<std::vector<std::vector<int8_t>>>> applyIQOffset;
      if (dsps.getDspsInverseQuantizationOffsetPresentFlag()
          && params.InverseQuantizationOffsetFlag) {
        std::vector<std::vector<std::vector<std::vector<int8_t>>>> IQOffset;
        std::vector<double> levelOfDetailInverseScale;
        levelOfDetailInverseScale.resize(dispDimensions, 0);
        for (int d = 0; d < dispDimensions; d++) {
          iscale[0][d] = InverseScale[0][d];
          levelOfDetailInverseScale[d] =
            std::pow(2, params.log2LevelOfDetailInverseScale[d]);
        }
        computeInverseQuantizationOffsetAC(
          frame, iscale, dispDimension, levelOfDetailInverseScale, IQOffset);
        layer.getDisplHeader().setDisplIQOffsetFlag(true);
        layer.getDisplHeader().setIQOffsetValuesSize(lodCount, dispDimensions);
        for (int i = 0; i < lodCount; i++) {
          for (int j = 0; j < dispDimensions; j++) {
            for (int k = 0; k < 3; k++) {
              for (int l = 0; l < 3; l++) {
                layer.getDisplHeader().getIQOffsetValues(i, j, k, l) =
                  int8_t(IQOffset[i][j][k][l]);
              }
            }
          }
        }

        applyIQOffset = layer.getDisplHeader().getIQOffsetValues();

        for (int32_t it = 1; it < lodCount; ++it) {
          for (int32_t k = 0; k < dispDimensions; ++k) {
            for (int32_t l = 0; l < 3; l++) {
              for (int32_t m = 1; m < 3; m++) {
                applyIQOffset[it][k][l][m] =
                  applyIQOffset[it][k][l][m]
                  + applyIQOffset
                    [it - 1][k][l]
                    [m];  // apply Intra prediction for LOD1 and higher levels
              }
            }
          }
        }
      }

      // inverseQuantizeDisplacements
      const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
      for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
        const auto vcount1 = infoLevelOfDetails[it].pointCount;
        for (int32_t v = vcount0; v < vcount1; ++v) {
          auto& d = frame.dispi[v];
          for (int32_t k = 0; k < dispDimension; ++k) {
            int64_t val = d[k] * iscale[it][k];
            if (dsps.getDspsInverseQuantizationOffsetPresentFlag()
                && params.InverseQuantizationOffsetFlag) {
              // Inverse Quantization Offset applied
              int32_t m = (d[k] == 0) ? 0 : (d[k] > 0.0 ? 1 : 2);
              int64_t IQOffsetValue =
                ((applyIQOffset[it][k][m][0] == 0) ? 1 : (-1))
                * ((((int64_t)1 << (23 + DISPL_INVQUANT_FIXEDP))
                    >> applyIQOffset[it][k][m][1])
                   + (((int64_t)1 << (23 + DISPL_INVQUANT_FIXEDP))
                      >> applyIQOffset[it][k][m][2]));

              val += IQOffsetValue;
            }
            d[k] = (val < 0 ? -((-val + (1 << 22)) >> (23))
                            : ((val + (1 << 22)) >> (23)));
          }
        }
        vcount0 = vcount1;
      }

      if (ctxIdx) {
        const auto& frameRef = gof[frameIndex - 1];

        const auto lodCount = int32_t(frame.subdivInfoLevelOfDetails.size());
        for (int32_t it = 0; it < lodCount; ++it) {
          const auto vcount0 =
            it == 0 ? 0 : frame.subdivInfoLevelOfDetails[it - 1].pointCount;
          const auto vcount1 = frame.subdivInfoLevelOfDetails[it].pointCount;
          for (int32_t v = vcount0; v < vcount1; ++v) {
            frame.dispi[v] = (layer.getDisplHeader().getDhLoDInter(it)
                              && it < frameRef.lodCountRef)
                               ? frame.dispi[v] + frameRef.dispRef[v]
                               : frame.dispi[v];
          }
        }
      }
      frame.dispRef = frame.dispi;

      const auto length = dispEncoder.stop();

      assert(length <= std::numeric_limits<uint32_t>::max());
      if (frame.submeshType == basemesh::I_BASEMESH) {
        DisplacementDataIntra& dataunit = layer.getDisplDataunitIntra();
        dataunit.allocateDisplDataBuffer(length);
        std::copy(dispEncoder.buffer(),
                  dispEncoder.buffer() + length,
                  dataunit.getCodedDisplDataUnitBuffer());
        dataunit.setPayloadSize(length);
      } else {
        DisplacementDataInter& dataunit = layer.getDisplDataunitInter();
        dataunit.allocateDisplDataBuffer(length);
        std::copy(dispEncoder.buffer(),
                  dispEncoder.buffer() + length,
                  dataunit.getCodedDisplDataUnitBuffer());
        dataunit.setPayloadSize(length);
      }
      frame.disp.assign(frame.dispi.size(), vmesh::Vec3<MeshType>(0.0, 0.0, 0.0));
      double fixedpointprec = (MeshType)(1 << DISPL_INVQUANT_FIXEDP);
      for (int32_t i = 0, length = frame.dispi.size(); i < length; ++i)
        for (int32_t j = 0; j < dispDimensions; ++j)
          frame.disp[i][j] = (MeshType)(frame.dispi[i][j]) / fixedpointprec;
      frame.dispi.resize(0);
      std::cout << "payloadSize: " << length << "\n";
      totalByteCount += length;

  }  //frame

  return totalByteCount;
}

int32_t
AcDisplacementEncoder::computeInverseQuantizationOffsetAC(
  vmesh::VMCSubmesh                        frame,
  std::vector<std::vector<int64_t>> iscale,
  uint8_t                           dispDimension,
  const std::vector<double>&        liftingLog2LevelOfDetailInverseScale,
  std::vector<std::vector<std::vector<std::vector<int8_t>>>>&
    InverseQuantizationOffsetValues) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);

  // inverseQuantizeDisplacements
  auto dispi = frame.dispi;
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto vcount1 = infoLevelOfDetails[it].pointCount;
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = dispi[v];
      for (int32_t k = 0; k < dispDimension; ++k) {
        int64_t val = d[k] * iscale[it][k];
        d[k]        = (val < 0 ? -((-val + (1 << 22)) >> (23))
                               : ((val + (1 << 22)) >> (23)));
      }
    }
    vcount0 = vcount1;
  }

  std::vector<vmesh::Vec3<MeshType>> disp(frame.disp.size(),
                                          vmesh::Vec3<MeshType>(0.0, 0.0, 0.0));
  double fixedpointprec = (MeshType)(1 << DISPL_INVQUANT_FIXEDP);
  for (int32_t i = 0, length = dispi.size(); i < length; ++i)
    for (int32_t j = 0; j < dispDimension; ++j)
      disp[i][j] = (MeshType)(dispi[i][j]) / fixedpointprec;

  // Inverse quantization offset computation
  std::vector<std::vector<std::vector<int32_t>>> count_zone;
  count_zone.resize(lodCount);
  double ilodScale[3];
  double prec1, prec2;

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
        prec1 = -std::floor(std::log2(
          liftingBias_offset[k][l] > 0.0
            ? liftingBias_offset[k][l]
            : -liftingBias_offset
                [k]
                [l]));  // determine the first log(2) precision for the fractional part
        liftingBias_offset_int[k][l][1] =
          prec1 < 0.0 ? 0.0 : (prec1 > 127.0 ? 127.0 : prec1);
        prec2 = -std::floor(std::log2(
          liftingBias_offset[k][l] > 0.0
            ? (liftingBias_offset[k][l]
               - pow(2, -liftingBias_offset_int[k][l][1]))
            : (
              -liftingBias_offset[k][l]
              - pow(
                2,
                -liftingBias_offset_int
                  [k][l]
                  [1]))));  // determine the second log(2) precision for the fractional part
        liftingBias_offset_int[k][l][2] =
          prec2 < 0.0 ? 0.0 : (prec2 > 127.0 ? 127.0 : prec2);
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

//============================================================================
void
AcDisplacementEncoder::fillDisZeros(vmesh::VMCSubmesh&                 frame,
                         const AcDisplacementEncoderParameters& params) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  auto&       disp               = frame.disp;
  assert(lodCount > 0);
  if (lodCount > 1) {
    const auto vcount0        = infoLevelOfDetails[lodCount - 2].pointCount;
    const auto vcount1        = infoLevelOfDetails[lodCount - 1].pointCount;
    const auto dispDimensions = params.applyOneDimensionalDisplacement ? 1 : 3;
    int32_t    count_not_zero = 0;

    for (int32_t v = vcount0; v < vcount1; v++) {
      auto   d       = disp[v];
      double modulus = 0.0;
      if (dispDimensions == 1) {
        modulus = abs(d[0]);
      } else {
        modulus = d.norm();
      }
      if (modulus > 0.0) { count_not_zero++; }
    }
    double         a         = count_not_zero;
    double         b         = vcount1 - vcount0;
    double         propotion = a / b;
    vmesh::Vec3<MeshType> Zero{0, 0, 0};
    if (propotion <= params.displacementFillZerosThreshold) {
      std::fill(disp.begin() + vcount0, disp.begin() + vcount1, Zero);
    }
  }
}

//============================================================================
bool
AcDisplacementEncoder::quantizeDisplacements(vmesh::VMCSubmesh&                 frame,
                                  int32_t                     submeshIndex,
                                  const AcDisplacementEncoderParameters& params) {
  const auto& infoLevelOfDetails = frame.subdivInfoLevelOfDetails;
  const auto  lodCount           = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  const auto dispDimensions = params.applyOneDimensionalDisplacement ? 1 : 3;
  std::vector<std::vector<double>> scales(
    lodCount, std::vector<double>(dispDimensions, 0));
  double halfVBD = (1 << params.geometryVideoBitDepth) / 2;
  if (params.lodDisplacementQuantizationFlag) {
    for (int32_t it = 0; it < lodCount; ++it) {
      auto& scale = scales[it];
      for (int32_t k = 0; k < dispDimensions; ++k) {
        const auto qp = (int32_t)params.qpPerLevelOfDetails[it][k];
        if (params.displacementQuantizationType
            == vmesh::DisplacementQuantizationType::ADAPTIVE)
          scale[k] = qp;
        else
          scale[k] = qp >= 0
                       ? pow(2.0,
                             16 + params.bitDepthOffset
                               - params.bitDepthPosition + (4 - qp) / 6.0)
                       : 0.0;
      }
    }
  } else {
    std::vector<double> lodScale(dispDimensions);
    auto&               scale = scales[0];
    for (int32_t k = 0; k < dispDimensions; ++k) {
      const auto qp = (int32_t)params.liftingQP[k];
      if (params.displacementQuantizationType
          == vmesh::DisplacementQuantizationType::ADAPTIVE)
        scale[k] = qp;
      else
        scale[k] = qp >= 0 ? pow(2.0,
                                 16 + params.bitDepthOffset
                                   - params.bitDepthPosition + (4 - qp) / 6.0)
                           : 0.0;
      if (params.transformMethod == 1)
        lodScale[k] =
          1.0 / (double)(1 << params.log2LevelOfDetailInverseScale[k]);
      else lodScale[k] = 1.0;

      //printf("%d\tqp %d\tscale %f\tlodScale %f\n", k, qp, scale[k], lodScale[k]);
    }
    for (int32_t it = 1; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimensions; ++k) {
        scales[it][k] = scales[it - 1][k] * lodScale[k];
      }
    }
  }
  auto disp_temp = frame.disp;
  if (params.InverseQuantizationOffsetFlag) {
    auto& qdisplodmean = frame.qdisplodmean;
    qdisplodmean.resize(lodCount);
    std::vector<std::vector<std::vector<int32_t>>> count_zone;
    count_zone.resize(lodCount);
    for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
      const auto& scale   = scales[it];
      const auto  vcount1 = infoLevelOfDetails[it].pointCount;
      qdisplodmean[it].resize(dispDimensions, {0.0, 0.0, 0.0});
      count_zone[it].resize(dispDimensions, {0, 0, 0});

      for (int32_t v = vcount0; v < vcount1; ++v) {
        auto& d      = disp_temp[v];
        auto  d_copy = disp_temp[v];

        for (int32_t k = 0; k < dispDimensions; ++k) {
          d[k]      = d[k] >= 0.0
                        ? std::floor(d[k] * scale[k] + params.liftingBias[k])
                        : -std::floor(-d[k] * scale[k] + params.liftingBias[k]);
          int32_t m = (d[k] == 0 ? 0 : (d[k] > 0.0 ? 1 : 2));
          qdisplodmean[it][k][m] +=
            d_copy[k];  // [0] save the deadzone 0 values
          count_zone[it][k][m]++;
        }
      }
      vcount0 = vcount1;
    }
    for (int32_t it = 0; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimensions; ++k) {
        qdisplodmean[it][k][0] /= count_zone[it][k][0];
        qdisplodmean[it][k][1] /= count_zone[it][k][1];
        qdisplodmean[it][k][2] /= count_zone[it][k][2];
      }
    }
  }
  auto& disp = frame.disp;
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto& scale   = scales[it];
    const auto  vcount1 = infoLevelOfDetails[it].pointCount;
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = disp[v];
#if DEBUG_NONE_TRANSFORM
      if (v == vcount0) {
        std::cout << "pre quantization vcount0" << vcount0 << "disp " << d[0]
                  << " " << d[1] << " " << d[2] << std::endl;
      }
#endif
      for (int32_t k = 0; k < dispDimensions; ++k) {
        d[k] = d[k] >= 0.0
                 ? std::floor(d[k] * scale[k] + params.liftingBias[k])
                 : -std::floor(-d[k] * scale[k] + params.liftingBias[k]);
        // clipping to allowed bitdepth
        if (d[k] > +(halfVBD - 1)) d[k] = +(halfVBD - 1);
        if (d[k] < -(halfVBD - 1)) d[k] = -(halfVBD - 1);
#if DEBUG_NONE_TRANSFORM
        if (v == vcount0) {
          std::cout << "post quantization vcount0" << vcount0 << "quant disp "
                    << d[0] << " " << d[1] << " " << d[2] << std::endl;
        }
#endif
      }
    }
    vcount0 = vcount1;
  }
  return true;
}
