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


#include <cstdio>
#include <sstream>

#include "entropy.hpp"
#include "vmc.hpp"
#include "acDisplacementDecoder.hpp"
#include "acDisplacementContext.hpp"

using namespace acdisplacement;
bool
ACDisplacementDecoder::decodeDisplacementSubbitstream(
  AcDisplacementBitstream& dmStream) {
  bool    decDispAC        = true;
  auto&   displacementList = dmStream.getDisplacementLayerList();
  int32_t displFrameCount =
    static_cast<int32_t>(calcTotalDisplacemetFrameCount(dmStream));
  int32_t maxDisplCount = static_cast<int32_t>(calcMaxDisplCount(dmStream));
  decodedAcDispls_.resize(maxDisplCount);
  for (int32_t displIdx = 0; displIdx < maxDisplCount; displIdx++) {
    decodedAcDispls_[displIdx].resize(displFrameCount);
  }
  for (int32_t duIdx = 0; duIdx < (int32_t)displacementList.size(); duIdx++) {
    auto& dsl    = displacementList[duIdx];
    auto  dfpsId = dsl.getDisplHeader().getDisplFrameParameterSetId();
    auto& dfps   = dmStream.getDisplFrameParameterSetList()[dfpsId];
    auto& dsps   = dmStream.getDisplSequenceParameterSetList()
                   [dfps.getDfpsDisplSequenceParameterSetId()];
    auto&   dinfo          = dfps.getDisplInformation();
    int32_t frameIndexCalc = (int32_t)dmStream.calculateDFOCval(duIdx);
    dsl.getDisplHeader().setFrameIndex(frameIndexCalc);
    //TODO: displId mapping
    auto displIndex = dinfo._displIDToIndex[dsl.getDisplHeader().getDisplId()];
    dsl.getDisplHeader().setDisplTileIndex(displIndex);
    //NOTE : absolute frameIndex is in getDhdisplReferenceList()
    createDhReferenceList(
      dsps, dsl, dsl.getDisplHeader().getDhdisplReferenceList());
    int numSubmeshesInFrame =
      dfps.getDisplInformation().getDiUseSingleDisplFlag()
        ? 1
        : (dfps.getDisplInformation().getDiNumDisplsMinus2() + 2);
    //TODO: [reference buffer] decodedFrames_ should be replaced with the reference displacement buffers
    decDispAC |= decompressDisplacementsAC(dmStream,  //vps,
                                           frameIndexCalc,
                                           dsl,
                                           dinfo,
                                           decodedAcDispls_);
  }
  return decDispAC;
}

bool
ACDisplacementDecoder::decompressDisplacementsAC(
  AcDisplacementBitstream&                dmStream,
  int32_t                               frameIndex,
  const DisplacementLayer&              dsl,
  const DisplInformation&               dinfo,
  std::vector<std::vector<AcDisplacementFrame>>& referenceFrameList) {
  printf("Decompress displacement AC: Frame = %d \n", frameIndex);
  fflush(stdout);
  int32_t atlasId = dmStream.getAtlasId();
  auto    dfpsId  = dsl.getDisplHeader().getDisplFrameParameterSetId();

  auto  dfps   = dmStream.getDisplFrameParameterSet(dfpsId);
  auto  dspsId = dfps.getDfpsDisplSequenceParameterSetId();
  auto& dsps   = dmStream.getDisplSequenceParameterSetList()[dspsId];

  auto duType = dsl.getDisplHeader().getDisplType();
  //TODO: displId mapping
  auto displId    = dsl.getDisplHeader().getDisplId();
  auto displIndex = dinfo._displIDToIndex[displId];
  std::cout << "WARNING:submesh index, displacement block index and patch "
               "index are assumed to be the same!!!!!!\n";
  std::cout << "WARNING:multiple submeshes may not be handled correctly!!!\n";
  auto& frame      = referenceFrameList[displIndex][frameIndex];
  frame.frameIndex = frameIndex;
  bool ret         = false;
  const auto& vertexCountLoD =
    dsl.getDisplHeader().getDhVertexCountLod();
  std::vector<uint32_t> infoLevelOfDetails(vertexCountLoD.size());
  infoLevelOfDetails[0] = vertexCountLoD[0];
  for (int32_t it = 1; it < dsl.getDisplHeader().getDhVertexCountLod().size(); ++it) {
    infoLevelOfDetails[it] = infoLevelOfDetails[it - 1] + vertexCountLoD[it];
  }

  if (duType == I_DISPL) {
    std::cout << "(decompressDisplacementsAC) FrameIndex: " << frameIndex
              << " dispType: I_DISPL\n";
    ret = decodeAC(frameIndex,
                   displIndex,
                   dsl.getDisplDataunitIntra().getCodedDisplDataUnit(),
                   (DisplacementType)duType,
                   dsl.getDisplHeader().getDhNumSubblockLodMinus1(),
                   dsps.getDspsSingleDimensionFlag() ? 1 : 3,
                   infoLevelOfDetails,
                   frame.dispi);
  } else {
    auto referenceFrameIndex =
      dsl.getDisplHeader().getDhdisplReferenceList()[0];
    auto* refFrame = findReferenceFrameAC(
      referenceFrameList[displIndex], (int)referenceFrameIndex, frameIndex);
    if (refFrame == nullptr) {
      std::cout << "error: reference frame[" << referenceFrameIndex
                << " ]is not present in the list\n";
      std::cout << "\treference frame indices: ";
      for (auto& f : referenceFrameList[displIndex]) {
        std::cout << f.frameIndex << "\t";
      }
      std::cout << "\n";
      return 0;
    }
    std::cout << "(decompressDisplacementsAC) FrameIndex: " << frameIndex
              << " dispType: P_DISPL\t";
    std::cout << "referenceFrameIndex: " << referenceFrameIndex << "\n";
    ret = decodeAC(frameIndex,
                   displIndex,
                   dsl.getDisplDataunitInter().getCodedDisplDataUnit(),
                   (DisplacementType)duType,
                   dsl.getDisplHeader().getDhNumSubblockLodMinus1(),
                   dsps.getDspsSingleDimensionFlag() ? 1 : 3,
                   infoLevelOfDetails,
                   frame.dispi);
  }

  const auto bitDepthPosition = dsps.getDspsGeometry3dBitdepthMinus1() + 1;
  int        dispDimension = (dsps.getDspsSingleDimensionFlag() == 1) ? 1 : 3;
  int        lodCount = dsl.getDisplHeader().getDhVertexCountLod().size();
  int        lodCountDsps = dsps.getDspsSubdivisionIterationCount() + 1;
  int        lodCountDfps = dfps.getDfpsSubdivisionIterationCount() + 1;
  std::vector<std::vector<int64_t>> iscale;
  std::vector<std::vector<int32_t>> QuantizationParameterDsps;
  std::vector<std::vector<int32_t>> QuantizationParameterDfps;
  std::vector<std::vector<int32_t>> QuantizationParameter;
  std::vector<std::vector<int64_t>> InverseScale;

  //initialization
  iscale.resize(lodCount);
  for (int i = 0; i < lodCount; i++) iscale[i].resize(dispDimension, 0);
  QuantizationParameterDsps.resize(lodCountDsps);
  for (int l = 0; l < lodCountDsps; l++)
    QuantizationParameterDsps[l].resize(dispDimension, 0);
  QuantizationParameterDfps.resize(lodCountDfps);
  for (int l = 0; l < lodCountDfps; l++)
    QuantizationParameterDfps[l].resize(dispDimension, 0);
  QuantizationParameter.resize(lodCount);
  for (int l = 0; l < lodCount; l++)
    QuantizationParameter[l].resize(dispDimension, 0);
  InverseScale.resize(lodCount);
  for (int i = 0; i < lodCount; i++) InverseScale[i].resize(dispDimension, 0);

  //QuantizationParameter
  auto& displQuantParam = dsl.getDisplHeader().getDhQuantizationParameters();
  const auto bitDepthOffset = displQuantParam.getDisplBitDepthOffset();
  if (displQuantParam.getDisplLodQuantizationFlag() == 0) {
    for (int d = 0; d < dispDimension; d++)
      for (int l = 0; l < lodCount; l++)
        QuantizationParameter[l][d] =
          displQuantParam.getDisplQuantizationParameters()[d];
  } else {
    int qpIndex = 0;
    if (dsl.getDisplHeader().getDhQuantizationOverrideFlag()) {
      qpIndex = 2;
    } else if (dfps.getDfpsQuantizationParametersEnableFlag()) {
      qpIndex = 1;
    }

    for (int l = 0; l < lodCountDsps; l++) {
      for (int d = 0; d < dispDimension; d++) {
        QuantizationParameterDsps[l][d] =
          dsps.getDspsDisplacementReferenceQPMinus49() + 49
          + (1
             - 2
                 * dsps.getDspsQuantizationParameters()
                     .getDisplLodDeltaQPSign()[l][d])
              * dsps.getDspsQuantizationParameters()
                  .getDisplLodDeltaQPValue()[l][d];
      }
    }

    if (qpIndex >= 1) {
      for (int l = 0; l < lodCountDfps; l++) {
        for (int d = 0; d < dispDimension; d++) {
          int8_t delta = 0;
          if (dfps.getDfpsQuantizationParametersEnableFlag())
            delta = (1
                     - 2
                         * dfps.getDfpsQuantizationParameters()
                             .getDisplLodDeltaQPSign()[l][d])
                    * dfps.getDfpsQuantizationParameters()
                        .getDisplLodDeltaQPValue()[l][d];
          if (lodCountDfps <= lodCountDsps)
            QuantizationParameterDfps[l][d] =
              QuantizationParameterDsps[l][d] + delta;
          else
            QuantizationParameterDfps[l][d] =
              QuantizationParameterDsps[0][d] + delta;
        }
      }
    } else {
      for (int l = 0; l < lodCount; l++) {
        for (int d = 0; d < dispDimension; d++) {
          QuantizationParameter[l][d] = QuantizationParameterDsps[l][d];
        }
      }
    }

    if (qpIndex >= 2) {
      for (int l = 0; l < lodCount; l++) {
        for (int d = 0; d < dispDimension; d++) {
          int8_t delta = 0;
          if (dsl.getDisplHeader().getDhQuantizationOverrideFlag())
            delta = (1 - 2 * displQuantParam.getDisplLodDeltaQPSign()[l][d])
                    * displQuantParam.getDisplLodDeltaQPValue()[l][d];
          if (lodCount <= lodCountDfps)
            QuantizationParameter[l][d] =
              QuantizationParameterDfps[l][d] + delta;
          else
            QuantizationParameter[l][d] =
              QuantizationParameterDfps[0][d] + delta;
        }
      }
    } else if (qpIndex == 1) {
      for (int l = 0; l < lodCount; l++) {
        for (int d = 0; d < dispDimension; d++) {
          QuantizationParameter[l][d] = QuantizationParameterDfps[l][d];
        }
      }
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
      assert(bitDepthPosition >= 4 && bitDepthPosition <= 16);
      assert(bitDepthOffset <= bitDepthPosition);
      InverseScale[l][d] =
        (qp - 4) >= 0
          ? (r6table[r] << (bitDepthPosition - bitDepthOffset + q - 4))
          : 0;
    }

  // iscale
  if (displQuantParam.getDisplLodQuantizationFlag()) {
    for (int l = 0; l < lodCount; ++l)
      for (int d = 0; d < dispDimension; ++d)
        iscale[l][d] = InverseScale[l][d];
  } else {
    for (int d = 0; d < dispDimension; ++d) iscale[0][d] = InverseScale[0][d];
    for (int l = 1; l < lodCount; l++)
      for (int d = 0; d < dispDimension; ++d)
        iscale[l][d] = (iscale[l - 1][d]
                        << displQuantParam.getDisplLog2LodInverseScale()[d]);
  }

  std::vector<std::vector<std::vector<std::vector<int8_t>>>> applyIQOffset;
  if (dsps.getDspsInverseQuantizationOffsetPresentFlag()
      && dsl.getDisplHeader().getDisplIQOffsetFlag()) {
    applyIQOffset = dsl.getDisplHeader().getIQOffsetValues();
    for (int32_t it = 1; it < lodCount; ++it) {
      for (int32_t k = 0; k < dispDimension; ++k) {
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
  for (int32_t it = 0, vcount0 = 0; it < lodCount; ++it) {
    const auto vcount1 = infoLevelOfDetails[it];
    for (int32_t v = vcount0; v < vcount1; ++v) {
      auto& d = frame.dispi[v];
      for (int32_t k = 0; k < dispDimension; ++k) {
        int64_t val = d[k] * iscale[it][k];
        if (dsps.getDspsInverseQuantizationOffsetPresentFlag()
            && dsl.getDisplHeader().getDisplIQOffsetFlag()) {
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

  if (duType == I_DISPL) {
    frame.dispRef = frame.dispi;
  } else {
    auto referenceFrameIndex =
      dsl.getDisplHeader().getDhdisplReferenceList()[0];

    auto* refFrame = findReferenceFrameAC(
      referenceFrameList[displIndex], (int)referenceFrameIndex, frameIndex);
    for (int32_t it = 0; it < lodCount; it++) {
      const auto vcount0 =
        it == 0 ? 0 : infoLevelOfDetails[it - 1];
      const auto vcount1  = infoLevelOfDetails[it];
      uint8_t    LoDinter = dsl.getDisplHeader().getDhLayerInterDisableFlag()
                              ? 1
                              : dsl.getDisplHeader().getDhLoDInter(it);
      for (int32_t v = vcount0; v < vcount1; ++v) {
        frame.dispi[v] = (LoDinter && v < (*refFrame).disp.size())
                           ? frame.dispi[v] + (*refFrame).dispRef[v]
                           : frame.dispi[v];
      }
    }
    frame.dispRef = frame.dispi;
  }

  frame.disp.assign(frame.dispi.size(), vmesh::Vec3<MeshType>(0.0, 0.0, 0.0));
  double fixedpointprec = (MeshType)(1 << DISPL_INVQUANT_FIXEDP);
  for (int32_t i = 0, length = frame.dispi.size(); i < length; ++i)
    for (int32_t j = 0; j < dispDimension; ++j)
      frame.disp[i][j] = (MeshType)(frame.dispi[i][j]) / fixedpointprec;
  frame.dispi.resize(0);
  frame.lodCountRef = lodCount;

  return ret;
}

bool

ACDisplacementDecoder::decodeAC(
  int32_t                      frameIndex,
  int32_t                      partIndex,
  const std::vector<uint8_t>&  dataunit,
  DisplacementType             dispType,
  const std::vector<uint32_t>& numSubblockLodMinus1,
  uint32_t                     dispDimensions,
  const std::vector<uint32_t>& pointCountSumAtLevel,
  std::vector<vmesh::Vec3<int64_t>>&  dispi) {
  vmesh::EntropyDecoder dispDecoder;
  dispDecoder.setBuffer(dataunit.size(),
                        reinterpret_cast<const char*>(dataunit.data()));
  dispDecoder.start();
  std::cout << "payloadSize: " << dataunit.size() << "\n";

  //int32_t lodCount = int32_t(gof.frames[0].subdivInfoLevelOfDetails.size());
  std::vector<std::vector<AcDisplacementContext>> ctxDisp(2);
  auto lodCount = pointCountSumAtLevel.size();
  ctxDisp[0].resize(lodCount);
  ctxDisp[1].resize(lodCount);
  AcDisplacementContext ctxBypass;

  //  const auto& frameInfo = gofInfo.frameInfo(frameIndex);
  int32_t ctxIdx;
  if (dispType == I_DISPL) {
    ctxIdx = 0;
  } else {
    ctxIdx = 1;
  }

  dispi.assign(pointCountSumAtLevel[lodCount - 1], vmesh::Vec3<int64_t>(0, 0, 0));
  int64_t fixedpointprec = DISPL_INVQUANT_FIXEDP;

  std::vector<uint32_t>          verAccCount(lodCount);
  std::vector<uint32_t>          verCountLoD(lodCount);
  std::vector<std::vector<bool>> nzSubBlock(lodCount);
  std::vector<bool>              emptyLevel(lodCount, false);
  for (int l = 0; l < lodCount; l++) {
    verAccCount[l] = pointCountSumAtLevel[l];
    verCountLoD[l] = l == 0
                       ? pointCountSumAtLevel[l]
                       : pointCountSumAtLevel[l] - pointCountSumAtLevel[l - 1];
  }

  for (int32_t dim = 0; dim < dispDimensions; dim++) {
    for (int32_t level = 0; level < lodCount; ++level) {
      nzSubBlock[level].resize(numSubblockLodMinus1[level] + 1);
    }

    for (int32_t level = 0; level < lodCount; ++level) {
      auto vStart       = (level == 0) ? 0 : verAccCount[level - 1];
      auto logBlockSize = (int32_t)std::ceil(
        log2((double)verCountLoD[level]
             / static_cast<double>(numSubblockLodMinus1[level] + 1)));
      auto blockSize = 1 << logBlockSize;
      for (int32_t block = 0; block < (numSubblockLodMinus1[level] + 1); block++) {
        nzSubBlock[level][block] =
          dispDecoder.decode(ctxDisp[ctxIdx][level].ctxCodedSubBlock[dim]);
        if (nzSubBlock[level][block]) {
          auto    vBlockStart = vStart + block * blockSize;
          int32_t vBlockEnd   = std::min((int32_t)(vBlockStart + blockSize),
                                       (int32_t)verAccCount[level]);
          for (int32_t v = vBlockStart; v < vBlockEnd; v++) {
            int32_t value    = 0;
            int32_t coeffGt0 = 0;
            coeffGt0 =
              dispDecoder.decode(ctxDisp[ctxIdx][level].ctxCoeffGtN[0][dim]);

            if (coeffGt0) {
              const auto sign     = dispDecoder.decode(ctxBypass.ctxStatic);
              int32_t    coeffGtN = coeffGt0;
              int32_t    currGtN  = 1;
              while (currGtN <= 3) {
                ++value;
                coeffGtN = dispDecoder.decode(
                  ctxDisp[ctxIdx][level].ctxCoeffGtN[currGtN][dim]);
                if (!coeffGtN) { break; }
                currGtN++;
              }
              if (coeffGtN) {
                value += 1
                         + dispDecoder.decodeExpGolomb(
                           0, ctxDisp[0][0].ctxCoeffRemPrefix, 3);
              }
              if (sign) { value = -value; }
            }
            dispi[v][dim] = (int64_t)value << fixedpointprec;
          }  //v
        }    //if(nzSubBlock[level][block])
      }      //block
    }
  }  //dim
  return true;
}

const AcDisplacementFrame*
ACDisplacementDecoder::findReferenceFrameAC(
  const std::vector<AcDisplacementFrame>& referenceFrameList,
  int32_t                        referenceFrameAbsoluteIndex,  //0~31
  int32_t                        currentFrameAbsoluteIndex)                           //0~31
{
  const AcDisplacementFrame* refFrame = nullptr;
  for (int i = 0; i < currentFrameAbsoluteIndex; i++) {
    auto& f = referenceFrameList[i];
    if (f.frameIndex == referenceFrameAbsoluteIndex
        && f.frameIndex < currentFrameAbsoluteIndex) {
      refFrame = &f;
    }
  }
  return refFrame;
}
int32_t
ACDisplacementDecoder::createDspsReferenceLists(
  DisplSequenceParameterSetRbsp&     dsps,
  std::vector<std::vector<int32_t>>& dspsRefDiffList) {
  auto numRefList = dsps.getDspsNumRefDisplFrameListsInDsps();
  dspsRefDiffList.resize(numRefList);
  //refFrameDiff:1,2,3,4...
  for (size_t listIdx = 0; listIdx < numRefList; listIdx++) {
    auto&  refList             = dsps.getDisplRefListStruct()[listIdx];
    size_t numActiveRefEntries = refList.getNumRefEntries();
    dspsRefDiffList[listIdx].resize(numActiveRefEntries);
    for (size_t refIdx = 0; refIdx < numActiveRefEntries; refIdx++) {
      int32_t deltaDfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                            * refList.getAbsDeltaDfocSt(refIdx);
      dspsRefDiffList[listIdx][refIdx] = deltaDfocSt;
    }  //refIdx
  }    //listIdx
  return 0;
}
int32_t
ACDisplacementDecoder::createDhReferenceList(
  DisplSequenceParameterSetRbsp& dsps,
  DisplacementLayer&             dispLayer,
  std::vector<int32_t>&          referenceList) {
  auto& dh = dispLayer.getDisplHeader();
  if (dh.getRefDisplFrameListDspsFlag()) {
    size_t refListIdx = 0;
    if (dsps.getDspsNumRefDisplFrameListsInDsps() > 1)
      refListIdx = dh.getRefDisplFrameListIdx();
    dh.setDisplRefListStruct(dsps.getDisplRefListStruct()[refListIdx]);
  } else {
  }

  DisplRefListStruct& refListStruct = dh.getDisplRefListStruct();
  referenceList.clear();
  size_t listSize = refListStruct.getNumRefEntries();
  auto   dfocBase = dh.getFrameIndex();
  for (size_t idx = 0; idx < listSize; idx++) {
    int deltaDfocSt = 0;
    if (refListStruct.getStRefdisplFrameFlag(idx))
      deltaDfocSt = (2 * refListStruct.getStrafEntrySignFlag(idx) - 1)
                    * refListStruct.getAbsDeltaDfocSt(idx);  // Eq.26
    int refPOC = dfocBase - deltaDfocSt;
    if (refPOC >= 0) referenceList.push_back(refPOC);
    dfocBase = refPOC;
  }

  return referenceList.size() == 0 ? -1 : referenceList[0];
}
size_t
ACDisplacementDecoder::calcTotalDisplacemetFrameCount(
  AcDisplacementBitstream& dmStream) {
  auto&  displList       = dmStream.getDisplacementLayerList();
  int    prevAspsIndex   = -1;
  size_t totalFrameCount = 0;
  size_t frameCountInASPS =
    0;  //for the case when this atllist refers multiple MSPS not the overall MSPS
  for (size_t i = 0; i < displList.size(); i++) {
    auto& dfps = dmStream.getDisplFrameParameterSet(
      displList[i].getDisplHeader().getDisplFrameParameterSetId());
    int aspsIndex = dfps.getDfpsDisplSequenceParameterSetId();
    if (prevAspsIndex != aspsIndex) {  //new msps
      totalFrameCount += frameCountInASPS;
      prevAspsIndex    = aspsIndex;
      frameCountInASPS = 0;
    }
    //size_t afocVal = calculateDFOCval( dmStream, displList, i );
    size_t afocVal = dmStream.calculateDFOCval(i);
    displList[i].getDisplHeader().setFrameIndex(
      (uint32_t)(afocVal + totalFrameCount));
    frameCountInASPS = std::max(frameCountInASPS, afocVal + 1);
  }
  totalFrameCount += frameCountInASPS;  //is it right?

  return totalFrameCount;
}
//size_t ACDisplacementDecoder::calculateDFOCval( size_t dlOrder ) {
//  // 8.2.3.1 Atals frame order count derivation process
//
//  if ( dlOrder == 0 ) {
//    displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntMsb(0);
//    displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntVal( displacementLayer_[dlOrder].getDisplHeader().getDisplFrmOrderCntLsb() );
//    return displacementLayer_[dlOrder].getDisplHeader().getDisplFrmOrderCntLsb();
//  }
//
//  size_t prevDispFrmOrderCntMsb = displacementLayer_[dlOrder - 1].getDisplHeader().getDhdisplFrmOrderCntMsb();
//  size_t dispFrmOrderCntMsb     = 0;
//  auto&  dfps                   = getDisplFrameParameterSet( displacementLayer_[dlOrder].getDisplHeader().getDisplFrameParameterSetId());
//  auto&  dsps                   = getDisplSequenceParameterSet( dfps.getDfpsDisplSequenceParameterSetId() );
//
//  size_t maxDispFrmOrderCntLsb  = size_t( 1 ) << ( dsps.getDspsLog2MaxDisplFrameOrderCntLsbMinus4() + 4 );
//  size_t dfocLsb                = displacementLayer_[dlOrder].getDisplHeader().getDisplFrmOrderCntLsb();
//  size_t prevDispFrmOrderCntLsb = displacementLayer_[dlOrder - 1].getDisplHeader().getDisplFrmOrderCntLsb();
//  if ( ( dfocLsb < prevDispFrmOrderCntLsb ) &&
//       ( ( prevDispFrmOrderCntMsb - dfocLsb ) >= ( maxDispFrmOrderCntLsb / 2 ) ) )
//    dispFrmOrderCntMsb = prevDispFrmOrderCntMsb + maxDispFrmOrderCntLsb;
//  else if ( ( dfocLsb > prevDispFrmOrderCntLsb ) &&
//            ( ( dfocLsb - prevDispFrmOrderCntLsb ) > ( maxDispFrmOrderCntLsb / 2 ) ) )
//    dispFrmOrderCntMsb = prevDispFrmOrderCntMsb - maxDispFrmOrderCntLsb;
//  else
//    dispFrmOrderCntMsb = prevDispFrmOrderCntMsb;
//
//  displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntMsb( dispFrmOrderCntMsb );
//  displacementLayer_[dlOrder].getDisplHeader().setDhdisplFrmOrderCntVal( dispFrmOrderCntMsb + dfocLsb );
//
//  return dispFrmOrderCntMsb + dfocLsb;
//}
size_t
ACDisplacementDecoder::calcMaxDisplCount(AcDisplacementBitstream& dmStream) {
  size_t maxDisplCount = 1;
  //auto& dmStream = getDisplacementStream();
  for (auto& dfps : dmStream.getDisplFrameParameterSetList()) {
    if (!dfps.getDisplInformation().getDiUseSingleDisplFlag())
      maxDisplCount = std::max(
        maxDisplCount,
        (size_t)(dfps.getDisplInformation().getDiNumDisplsMinus2() + 2));
  }
  return maxDisplCount;
}
