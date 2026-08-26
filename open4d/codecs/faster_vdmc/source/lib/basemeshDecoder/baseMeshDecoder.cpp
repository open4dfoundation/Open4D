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

#include "baseMeshDecoder.hpp"
#include "util/checksum.hpp"
#include "entropy.hpp"
#include "geometryDecoder.hpp"
#include "motionContexts.hpp"
using namespace basemesh;
bool
BaseMeshDecoder::addNeighbor(int32_t               vertex1,
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

bool
BaseMeshDecoder::computeVertexAdjTableMotion(
  const std::vector<vmesh::Vec3<int32_t>>& triangles,
  int32_t                           vertexCount,
  int32_t                           maxNumNeighborsMotion,
  std::vector<int32_t>&             vertexAdjTableMotion,
  std::vector<int32_t>&             numNeighborsMotion) {
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
int
BaseMeshDecoder::decodePredMode(vmesh::EntropyDecoder&     arithmeticDecoder,
                                vmesh::VMCMotionACContext& ctx,
                                int                 maxMode) {
  int mode = 0;

  if (maxMode == 0) return mode;

  int ctxIdx = 0;
  while (arithmeticDecoder.decode(ctx.ctxPred[ctxIdx])) {
    ctxIdx = 1;
    mode++;
    if (mode == maxMode) break;
  }

  return mode;
}

bool
BaseMeshDecoder::decompressMotion(BaseMeshSubmeshLayer& bmsl,
                                  const vmesh::VMCBasemesh&    refFrameInput,
                                  std::vector<int32_t>& numNeighborsMotion,
                                  int                   maxNumNeighbours,
                                  std::vector<int32_t>& vertexAdjTableMotion,
                                  vmesh::VMCBasemesh&          current) {
  printf("\tdecompressMotion \n");
  fflush(stdout);
  auto&              reference = refFrameInput.base.points();
  vmesh::VMCMotionACContext ctx;
  vmesh::EntropyDecoder     arithmeticDecoder;
  auto&              bmth  = bmsl.getSubmeshHeader();
  auto&              bmtdu = bmsl.getSubmeshDataunitInter();
  //parsing the motion vectors -> L.8.3.3
  const auto& bufferWithHeader = bmtdu.getCodedMeshDataUnit();
  // bitsInLastByte should be considered by the entropy decoder if its value is not 8
  // this is only the case when the motion payload is not byte aligned
  const auto  bitsInLastByte = bmtdu.getCodedMeshDataUnitBitsInLastByte();

  uint32_t  byteCount = bufferWithHeader.size();
  vmesh::Bitstream bitstream;
  bitstream.copyFrom((uint8_t*)bufferWithHeader.data(), 0, byteCount);
  bitstream.beginning();
  uint32_t value;
  // motion coding header
  value = bitstream.read(2);
  bmtdu.setMchMaxMvpCandMinus1(value);
  value = bitstream.read(3);
  bmtdu.setMchLog2MotionGroupSize(value);
  //byteAlignment(bitstream);
  uint32_t one  = 1;
  uint32_t zero = 0;
  one           = bitstream.read(1);  // f(1): equal to 1
  while (!bitstream.byteAligned()) {
    zero = bitstream.read(1);  // f(1): equal to 0
  }
  int headerSize = bitstream.getPosition().bytes_;
  //motion coding payload
  value = bitstream.readVu();
  bmtdu.setMcpVertexCount(value);
  int pos = bitstream.getPosition().bytes_;
  // now we need to adjust the buffer for the arithmetic encoder....
  auto motionDataSize = byteCount - pos;
  bmtdu.allocateCodedMeshDataUnit(motionDataSize);
  // copyToBits does not consider bitsInLastByte as motion data is assumed to be byte aligned
  // non meaningful bits may need to be set to 1 otherwise
  bitstream.copyToBits(bmtdu.getCodedMeshDataUnitBuffer(), motionDataSize);
  const auto& buffer = bmtdu.getCodedMeshDataUnit();
  std::cout << "\tMotion header byte count = " << headerSize << '\n';
  std::cout << "\tMotion vertex byte count = " << pos - headerSize << '\n';
  std::cout << "\tMotion byte count = " << motionDataSize << '\n';

  std::cout << "\tMotion byte count = " << byteCount << '\n';
  arithmeticDecoder.setBuffer(buffer.size(),
                              reinterpret_cast<const char*>(buffer.data()));
  arithmeticDecoder.start();
  int                  noSignalledMvCount = 0;
  std::vector<int32_t> mvDupSignalled;
  int                  mcp_mv_signalled_flag_count =
    arithmeticDecoder.decodeExpGolomb(0, ctx.ctxMvDupVert);
  for (int d = 0; d < mcp_mv_signalled_flag_count; d++) {
    mvDupSignalled.push_back(arithmeticDecoder.decode(ctx.ctxMvSignalledFlag));
    if (mvDupSignalled[d] == 0) noSignalledMvCount++;
  }
  int mcp_mv_signalled_flag_trailing0 =
    arithmeticDecoder.decodeExpGolomb(0, ctx.ctxTrailing0);
  noSignalledMvCount += mcp_mv_signalled_flag_trailing0;
  for (int d = 0; d < mcp_mv_signalled_flag_trailing0; d++)
    mvDupSignalled.push_back(0);
  int sigVertexCount  = bmtdu.getMcpVertexCount() - noSignalledMvCount;
  int MotionGroupSize = 1 << bmtdu.getMchLog2MotionGroupSize();
  int groupSize       = MotionGroupSize;
  int groupCount      = (sigVertexCount - 1) / groupSize + 1;
  int groupSizeFinal  = sigVertexCount - groupSize * (groupCount - 1);

  std::vector<int>                           skipGroup(groupCount, 0);
  std::vector<std::vector<int>>              skipGroupPerComponent(groupCount);
  std::vector<std::vector<int>>              predModePerComponent(groupCount);
  std::vector<std::vector<std::vector<int>>> motionResidual(groupCount);
  for (int g = 0; g < groupCount; g++) {
    skipGroup[g] = arithmeticDecoder.decode(ctx.ctxSkip);
    skipGroupPerComponent[g].resize(3, 0);
    predModePerComponent[g].resize(3, 0);
    if (!skipGroup[g]) {
      for (int k = 0; k < 3; k++) {
        if (k != 2
            || (skipGroupPerComponent[g][0] == 0
                || skipGroupPerComponent[g][1] == 0))
          skipGroupPerComponent[g][k] = arithmeticDecoder.decode(ctx.ctxSkip);
        if (skipGroupPerComponent[g][k] == 0)
          predModePerComponent[g][k] = decodePredMode(
            arithmeticDecoder, ctx, bmtdu.getMchMaxMvpCandMinus1());
      }
    }
    if (g == groupCount - 1) groupSize = groupSizeFinal;
    motionResidual[g].resize(groupSize);
    for (int v = 0; v < groupSize; v++) {
      motionResidual[g][v].resize(3, 0);
      if (skipGroup[g] == 0) {
        for (int k = 0; k < 3; k++) {
          if (skipGroupPerComponent[g][k] == 0) {
            int32_t value = 0;
            if (arithmeticDecoder.decode(ctx.ctxCoeffGtN[0][k]) != 0) {
              const auto sign = arithmeticDecoder.decode(ctx.ctxSign[k]);
              ++value;
              if (arithmeticDecoder.decode(ctx.ctxCoeffGtN[1][k]) != 0) {
                value += 1
                         + arithmeticDecoder.decodeExpGolomb(
                           0, ctx.ctxCoeffRemPrefix, 2);
              }
              if (sign != 0) { value = -value; }
            }
            motionResidual[g][v][k] = value;
          }
        }
      }
    }
  }
  arithmeticDecoder.stop();
  printf("\t[flagmv] numCurrentMeshSize: %d\tnumDupSiged: "
         "%d\tnumMvDerivedSize: %d\n",
         bmtdu.getMcpVertexCount(),
         noSignalledMvCount,
         mcp_mv_signalled_flag_count + mcp_mv_signalled_flag_trailing0);

  //decoding the motion vectors -> L.9.2
  std::vector<std::vector<int>> motionVector(bmtdu.getMcpVertexCount());
  assert(bmtdu.getMcpVertexCount() == refFrameInput.base.pointCount());
  std::vector<bool> mvFlag(bmtdu.getMcpVertexCount(), true);
  int               d = 0;
  int               numDuplicates =
    mcp_mv_signalled_flag_count + mcp_mv_signalled_flag_trailing0;
  for (int v = 0; v < refFrameInput.base.pointCount(); v++) {
    if (refFrameInput.baseRepVertexIndices[v] != v && d < numDuplicates) {
      mvFlag[v] = (mvDupSignalled[d] == 1);
      d++;
    }
  }

  int mvCount = 0;
  for (int v = 0; v < refFrameInput.base.pointCount(); v++) {
    motionVector[v].resize(3, 0);
    if (!mvFlag[v]) {
      // duplicate vertex, motion vector is copied from its reference
      int vRef = refFrameInput.baseRepVertexIndices[v];
      for (int k = 0; k < 3; k++) motionVector[v][k] = motionVector[vRef][k];
    } else {
      int gIdx = mvCount / MotionGroupSize;
      int vIdx = mvCount % MotionGroupSize;
      mvCount++;
      if (!skipGroup[gIdx]) {
        for (int k = 0; k < 3; k++) {
          if (!skipGroupPerComponent[gIdx][k]) {
            int motionVectorPrediction = 0;
            if (predModePerComponent[gIdx][k] == 0) {
              // MV_PRED_NONE
              motionVectorPrediction = 0;
            } else {
              int32_t count = numNeighborsMotion[v];
              for (int32_t i = 0; i < count; ++i) {
                const auto vindex1 =
                  vertexAdjTableMotion[v * maxNumNeighbours + i];
                const auto& mv1 = motionVector[vindex1][k];
                motionVectorPrediction += mv1;
              }
              if (count > 1) {
                int offset;
                if (predModePerComponent[gIdx][k] == 1) {
                  // MV_PRED_NEIGHBOUR
                  offset = 0;
                }
                if (predModePerComponent[gIdx][k] == 2) {
                  // MV_DERIVED
                  offset = count >> 1;
                }
                if (motionVectorPrediction > 0)
                  motionVectorPrediction =
                    (motionVectorPrediction + offset) / count;
                else if (motionVectorPrediction < 0)
                  motionVectorPrediction =
                    -1 * (-1 * motionVectorPrediction + offset) / count;
              }
            }
            motionVector[v][k] =
              motionVectorPrediction + motionResidual[gIdx][vIdx][k];
          }
        }
      }
    }
  }

  // now add the motion vectors to the vertices position -> L9.1
  current.base.points().resize(refFrameInput.base.pointCount());
  int maxValue = (1 << refFrameInput.positionBitdepth) - 1;
  for (int vindex = 0; vindex < refFrameInput.base.pointCount(); ++vindex) {
    current.base.setPoint(
      vindex,
      vmesh::Clip3(
        0.0, double(maxValue), reference[vindex][0] + motionVector[vindex][0]),
      vmesh::Clip3(
        0.0, double(maxValue), reference[vindex][1] + motionVector[vindex][1]),
      vmesh::Clip3(0.0,
            double(maxValue),
            reference[vindex][2] + motionVector[vindex][2]));
  }
  return true;
}

//----------------------------------------------------------------------------
const vmesh::VMCBasemesh*
BaseMeshDecoder::findReferenceFrame(
  const std::vector<vmesh::VMCBasemesh>& referenceFrameList,
  int32_t                         referenceFrameAbsoluteIndex,  //0~31
  int32_t                         currentFrameAbsoluteIndex)                            //0~31
{
  const vmesh::VMCBasemesh* refFrame = nullptr;
  for (int i = 0; i < currentFrameAbsoluteIndex; i++) {
    auto& f = referenceFrameList[i];
    if (f.frameIndex == referenceFrameAbsoluteIndex
        && f.frameIndex < currentFrameAbsoluteIndex) {
      refFrame = &f;
    }
  }
  return refFrame;
}

const vmesh::VMCBasemesh*
BaseMeshDecoder::findReferenceFrame(
  const std::vector<vmesh::VMCBasemesh>& referenceFrameList,
  int32_t                         referenceFrameAbsoluteIndex,  //0~31
  const std::vector<int32_t>&     decompressedFrameIndices) {
  const vmesh::VMCBasemesh* refFrame = nullptr;
  for (auto i : decompressedFrameIndices) {
    auto& f = referenceFrameList[i];
    if (f.frameIndex == referenceFrameAbsoluteIndex) { refFrame = &f; }
  }
  return refFrame;
}

bool
BaseMeshDecoder::decompressBaseMesh(
  const BaseMeshBitstream& bmStream,
  //const V3CParameterSet&                vps,
  int32_t               frameIndex,
  BaseMeshSubmeshLayer& bmsl,
  std::vector<std::vector<vmesh::VMCBasemesh>>&
       referenceFrameList,  //TODO: [reference buffer]
  bool keepBasemesh,
  std::vector<std::vector<int32_t>>& decompressedFrameIndices) {
  fflush(stdout);
  int32_t atlasId = bmStream.getAtlasId();
  auto    bmfpsId = bmsl.getSubmeshHeader().getSmhSubmeshFrameParameterSetId();
  auto    bmfps   = bmStream.getBaseMeshFrameParameterSetList()[bmfpsId];
  auto    bmspsId = bmfps.getBfpsMeshSequenceParameterSetId();
  auto&   bmsps   = bmStream.getBaseMeshSequenceParameterSet(bmspsId);

  auto  smuType    = bmsl.getSubmeshHeader().getSmhType();
  auto& bmfpsSi    = bmfps.getSubmeshInformation();
  auto  smuPos     = bmfpsSi._submeshIDToIndex[bmsl.getSubmeshHeader()
                                            .getSmhId()];  //submeshIDx-> 0/1/2
  auto& frame      = decodedFrames_[smuPos][frameIndex];
  frame.frameIndex = frameIndex;
  auto& base       = frame.base;
  //auto& prevBaseRepVertexIndices = prevRefBaseRepVertexIndices[smuPos];
  //frame.baseRepVertexIndices = prevRefBaseRepVertexIndices[smuPos];

  if (smuType == I_BASEMESH) {
    std::cout << "(decompressBaseMesh) FrameIndex: " << frameIndex
              << " submeshIndex: " << smuPos << " submeshType: I_SUBMESH\n";
    frame.submeshType = I_BASEMESH;
    frame.submeshId_  = bmsl.getSubmeshHeader().getSmhId();
    // Decode base mesh
    printf("\tBaseMeshDecoding\n");
    //if (reuseDuplicatedVertFlag_) prevBaseRepVertexIndices.clear(); // always true?
    printf("\tDecodeMeshToBuffer => buffer size = %zu\n",
           bmsl.getSubmeshDataunitIntra().getCodedMeshDataSize());
    const std::vector<uint8_t>& geometryBitstream =
      bmsl.getSubmeshDataunitIntra().getCodedMeshDataUnit();
    const auto geometryBitstreamBitsInLastByte =
        bmsl.getSubmeshDataunitIntra().getCodedMeshDataUnitBitsInLastByte();
    auto decoder = vmesh::GeometryDecoder<MeshType>::create(
      vmesh::GeometryCodecId(bmsps.getBmspsIntraMeshCodecId()));
    vmesh::GeometryDecoderParameters decoderParams;
    // while Annex I syntax ensures geometryBitstreamBitsInLastByte is always 8
    // the generic decoder interface defines the number of meaningful bits in the
    // last bitstream byte
    decoderParams.bitsInLastBitstreamByte_ = geometryBitstreamBitsInLastByte;

    vmesh::tic("baseMeshDecode");
    uint8_t qpPosition;
    uint8_t qpNormal;
    decoder->decode(geometryBitstream, decoderParams, base, qpPosition, qpNormal);
    vmesh::toc("baseMeshDecode");
    printf("\tBaseMeshDecoding: done \n");
    fflush(stdout);

    //conversion from Annex I to Annex H
    size_t bitDepthPosition = bmsps.getBmspsGeometry3dBitDepthMinus1() + 1;
    if (bitDepthPosition != qpPosition) {
      const auto scalePosition =
        ((1 << qpPosition) - 1.0) / ((1 << bitDepthPosition) - 1.0);
      const auto iscalePosition = 1.0 / scalePosition;
      for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
        base.setPoint(v, base.point(v) * iscalePosition);
      }
    }

    // Save intermediate files
    if (keepBasemesh) {
      auto prefix = keepFilesPathPrefix_ + "fr_"
                    + std::to_string(frame.frameIndex) + "_base";
      base.save(prefix + "_dec.ply");
      vmesh::save(prefix + ".drc", geometryBitstream);
    }
    // Extracting qpNormals, bitDepthNormals, qpTexCoord, bitDepthTexCoord.
    auto bitDepthNormal  = 0;
    auto qpTexCoord = 0;
    auto qpFaceId   = 0;
    for (auto i = 0; i < bmsps.getBmspsMeshAttributeCount(); i++) {
      if (bmsps.getBmspsMeshAttributeTypeId(i)
          == vmesh::MeshAttribueType::ATTRIBUTE_TEXCOORD) {
        qpTexCoord = bmsps.getBmspsAttributeBitDepthMinus1(i) + 1;
      } else if (bmsps.getBmspsMeshAttributeTypeId(i)
                 == vmesh::MeshAttribueType::ATTRIBUTE_NORMAL) {
        bitDepthNormal = bmsps.getBmspsAttributeBitDepthMinus1(i) + 1;
        frame.normalBitdepth = bitDepthNormal;
      } else if (bmsps.getBmspsMeshAttributeTypeId(i)
                 == vmesh::MeshAttribueType::ATTRIBUTE_FACEGROUPID) {
        qpFaceId = bmsps.getBmspsAttributeBitDepthMinus1(i) + 1;
      }
    }
    // Normal conversion from Annex I to Annex H
    if (base.normalCount() > 0) {
      if(bitDepthNormal != qpNormal){
        const auto scaleNormal = ((1 << qpNormal) - 1.0)
                                 / ((1 << bitDepthNormal) - 1.0);
        const auto iscaleNormal = 1.0 / scaleNormal;
        for (int32_t n = 0, ncount = base.normalCount(); n < ncount; ++n) {
          base.setNormal(n, base.normal(n) * iscaleNormal);
          }
      }
    }
    // Texture dequantization
    if (base.texCoordCount() > 0) {
      const auto scaleTexCoord  = std::pow(2.0, qpTexCoord) - 1.0;
      const auto iscaleTexCoord = 1.0 / scaleTexCoord;
      for (int32_t tc = 0, tccount = base.texCoordCount(); tc < tccount;
           ++tc) {
        base.setTexCoord(tc, base.texCoord(tc) * iscaleTexCoord);
      }
    }
    // FaceID dequantization
    if (base.faceIdCount() > 0) {
      const auto scaleFaceId  = std::pow(2.0, qpFaceId) - 1.0;
      const auto iscaleFaceId = 1.0 / scaleFaceId;
      for (int32_t tc = 0, tccount = base.faceIdCount(); tc < tccount; ++tc) {
        base.setFaceId(tc, base.faceId(tc) * iscaleFaceId);
      }
    }
    // H.9.4.5.2.2 - calculating the duplicate structure --> using profile flag to indicate if we are going to do this or NOT,
    // which also forces dupNumCount to be zero in the motion codec
    bool deriveDuplicate = true;
    if (bmsps.getBmeshProfileTierLevel()
          .bmptl_toolset_constraints_present_flag_) {
      auto& bmptl     = bmsps.getBmeshProfileTierLevel();
      auto& bmptci    = bmptl.getBmptlProfileToolsetConstraintsInformation();
      deriveDuplicate = !bmptci.getBmptcMotionVectorDerivationDisableFlag();
    }
    if (deriveDuplicate) {
      std::vector<int32_t> duplicateVertexList;
      findRepresentVerticesSimple(
        frame.baseRepVertexIndices, duplicateVertexList, base);
    } else {
      frame.baseRepVertexIndices.resize(frame.base.pointCount());
      for (int i = 0; i < frame.base.pointCount(); i++)
        frame.baseRepVertexIndices[i] = i;  // no duplicate
    }
    // H.9.4.5.2.3 - vertex neighbour table calculation
    auto& triangles = base.triangles();
    computeVertexAdjTableMotion(triangles,
                                base.points().size(),
                                bmsps.getBmspsInterMeshMaxNumNeighboursMinus1()
                                  + 1,
                                frame.vertexAdjTableMotion,
                                frame.numNeighborsMotion);

  } else if (smuType == P_BASEMESH) {
    frame.submeshType = P_BASEMESH;
    frame.submeshId_  = bmsl.getSubmeshHeader().getSmhId();
    auto referenceListIndex =
      bmsl.getSubmeshHeader().getSmhBasemeshReferenceList();
    auto referenceFrameIndex =
      referenceListIndex[bmsl.getSubmeshDataunitInter()
                           .getReferenceFrameIndex()];
    auto* refFrame = findReferenceFrame(referenceFrameList[smuPos],
                                        (int)referenceFrameIndex,
                                        decompressedFrameIndices[smuPos]);
    if (refFrame == nullptr) {
      std::cout << "error: reference frame[" << referenceFrameIndex
                << " ]is not present in the list\n";
      std::cout << "\treference frame indices: ";
      for (auto& f : referenceFrameList[smuPos]) {
        std::cout << f.frameIndex << "\t";
      }
      std::cout << "\n";
    }
    std::cout << "(decompressBaseMesh) FrameIndex: " << frameIndex
              << " submeshIndex: " << smuPos << " submeshType: P_SUBMESH\t";
    std::cout << "referenceFrameIndex: " << referenceFrameIndex << "\n";
    // copy the duplicated indices and the neighbours table
    frame.baseRepVertexIndices = refFrame->baseRepVertexIndices;
    frame.vertexAdjTableMotion = refFrame->vertexAdjTableMotion;
    frame.numNeighborsMotion  = refFrame->numNeighborsMotion;
    base                       = (*refFrame).base;
    decompressMotion(bmsl,
                     (*refFrame),
                     frame.numNeighborsMotion,
                     bmsps.getBmspsInterMeshMaxNumNeighboursMinus1() + 1,
                     frame.vertexAdjTableMotion,
                     frame);
  } else {
    frame.submeshType = SKIP_BASEMESH;
    frame.submeshId_  = bmsl.getSubmeshHeader().getSmhId();
    auto referenceListIndex =
      bmsl.getSubmeshHeader().getSmhBasemeshReferenceList();
    auto  referenceFrameIndex = referenceListIndex[0];
    auto* refFrame            = findReferenceFrame(referenceFrameList[smuPos],
                                        (int)referenceFrameIndex,
                                        decompressedFrameIndices[smuPos]);
    if (refFrame == nullptr) {
      std::cout << "error: reference frame[" << referenceFrameIndex
                << " ]is not present in the list\n";
      std::cout << "\treference frame indices: ";
      for (auto& f : referenceFrameList[smuPos]) {
        std::cout << f.frameIndex << "\t";
      }
      std::cout << "\n";
    }
    std::cout << "(decompressBaseMesh) FrameIndex: " << frameIndex
              << " submeshIndex: " << smuPos << " submeshType: SKIP_SUBMESH\t";
    std::cout << "referenceFrameIndex: " << referenceFrameIndex << "\n";
    // copy the duplicated indices and the neighbours table
    frame.baseRepVertexIndices = refFrame->baseRepVertexIndices;
    frame.vertexAdjTableMotion = refFrame->vertexAdjTableMotion;
    frame.numNeighborsMotion   = refFrame->numNeighborsMotion;
    base                       = refFrame->base;
  }
  decompressedFrameIndices[smuPos].push_back(frameIndex);
  frame.positionBitdepth = bmsps.getBmspsGeometry3dBitDepthMinus1() + 1;
  return true;
}

void
BaseMeshDecoder::decodeBasemeshSubbitstream(BaseMeshBitstream& bmStream,
                                            bool                  keepBasemesh,
                                            bool                  checksum) {
  int32_t bmspsId = 0;
  int32_t bmfpsId = 0;

  mspsSequenceRefDiffList_.resize(1);
  createMspsReferenceLists(bmStream.getBaseMeshSequenceParameterSet(bmspsId),
                           mspsSequenceRefDiffList_[0]);
  auto& bmfti =
    bmStream.getBaseMeshFrameParameterSet(0).getSubmeshInformation();
  for (auto idx : bmfti._submeshIDToIndex) submeshIdtoIndex_.push_back(idx);

  int32_t meshFrameCount  = (int32_t)calcTotalMeshFrameCount(bmStream);
  int32_t maxSubmeshCount = (int32_t)calcMaxSubmeshCount(bmStream);
  decodedFrames_.resize(maxSubmeshCount);
  for (int32_t submeshIdx = 0; submeshIdx < maxSubmeshCount; submeshIdx++) {
    decodedFrames_[submeshIdx].resize(meshFrameCount);
  }
  auto& submeshList = bmStream.getBaseMeshSubmeshLayerList();
  //NOTE: submeshList is in [submesh][frame]
  std::vector<std::vector<int32_t>> decompressedFrameIndices(maxSubmeshCount);
  RefVertexIndices prevRefBaseRepVertexIndices(maxSubmeshCount);
  for (int32_t muIdx = 0; muIdx < (int32_t)submeshList.size(); muIdx++) {
    auto& bmsl    = submeshList[muIdx];
    auto  bmfpsId = bmsl.getSubmeshHeader().getSmhSubmeshFrameParameterSetId();
    auto& bmfps   = bmStream.getBaseMeshFrameParameterSetList()[bmfpsId];
    auto& bmsps   = bmStream.getBaseMeshSequenceParameterSetList()
                    [bmfps.getBfpsMeshSequenceParameterSetId()];
    int32_t frameIndexCalc =
      (int32_t)calculateMFOCval(bmStream, submeshList, muIdx);

    //NOTE : absolute frameIndex is in getSmhBasemeshReferenceList()
    createSmhReferenceList(
      bmsps, bmsl, bmsl.getSubmeshHeader().getSmhBasemeshReferenceList());
    int numSubmeshesInFrame =
      (bmfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1() + 1);
    //TODO: [reference buffer] decodedFrames_ should be replaced with the reference mesh buffers
    decompressBaseMesh(bmStream,
                       //vps,
                       frameIndexCalc,
                       bmsl,
                       decodedFrames_,
                       keepBasemesh,
                       decompressedFrameIndices);
  }
  if (checksum) {
    for (int32_t frameIndex = 0; frameIndex < meshFrameCount; ++frameIndex) {
      for (int32_t submeshIdx = 0; submeshIdx < maxSubmeshCount;
           submeshIdx++) {
        auto& submesh = decodedFrames_[submeshIdx][frameIndex];
        //printf("qpPosition: %d bitDepthPosition:%d iscalePosition: %f\n", qpPosition,bitDepthPosition, iscalePosition);
        //frame.base.save("_dec_base.obj");
        vmesh::Checksum    checksum;
        std::string eString = " Frame[ " + std::to_string(frameIndex) + " ]["
                              + std::to_string(submeshIdx)
                              + "]\tbmstreamRecBase ";
        checksum.print(submesh.base, eString);
      }
    }
  }
}
int32_t
BaseMeshDecoder ::createMspsReferenceLists(
  BaseMeshSequenceParameterSetRbsp&  msps,
  std::vector<std::vector<int32_t>>& mspsRefDiffList) {
  auto numRefList = msps.getBmspsNumRefMeshFrameListsInBmsps();
  mspsRefDiffList.resize(numRefList);
  //refFrameDiff:1,2,3,4...
  for (size_t listIdx = 0; listIdx < numRefList; listIdx++) {
    auto&  refList             = msps.getBmeshRefListStruct(listIdx);
    size_t numActiveRefEntries = refList.getNumRefEntries();
    mspsRefDiffList[listIdx].resize(numActiveRefEntries);
    for (size_t refIdx = 0; refIdx < numActiveRefEntries; refIdx++) {
      //if(refList.getStRefAtalsFrameFlag( refIdx))
      int32_t deltaMfocSt = (2 * refList.getStrafEntrySignFlag(refIdx) - 1)
                            * refList.getAbsDeltaMfocSt(refIdx);
      mspsRefDiffList[listIdx][refIdx] = deltaMfocSt;
    }  //refIdx
  }    //listIdx
  return 0;
}

int32_t
BaseMeshDecoder::createSmhReferenceList(
  BaseMeshSequenceParameterSetRbsp& bmsps,
  BaseMeshSubmeshLayer&             submesh,
  std::vector<int32_t>&             referenceList) {
  //  auto& basemeshStream = getBaseMeshDataStream();
  auto& submeshHeader = submesh.getSubmeshHeader();
  //  auto   bmfpsId       = submeshHeader.getSmhSubmeshFrameParameterSetId();
  //  auto&  bmfps         = basemeshStream.getBaseMeshFrameParameterSetList()[ bmfpsId ];
  //  auto&  bmsps         = basemeshStream.getBaseMeshSequenceParameterSetList()[ bmfps.getBfpsMeshSequenceParameterSetId() ];

  if (submeshHeader.getSmhRefBasemeshFrameListMspsFlag()) {
    size_t refListIdx = 0;
    if (bmsps.getBmspsNumRefMeshFrameListsInBmsps() > 1)
      refListIdx = submeshHeader.getSmhRefMeshFrameListIdx();
    submeshHeader.setSmhRefListStruct(bmsps.getBmeshRefListStruct(refListIdx));
  } else {
  }

  BaseMeshRefListStruct& refListStruct = submeshHeader.getSmhRefListStruct();
  referenceList.clear();
  size_t listSize = refListStruct.getNumRefEntries();
  auto   mfocBase = submeshHeader.getFrameIndex();
  for (size_t idx = 0; idx < listSize; idx++) {
    int deltaMfocSt = 0;
    if (refListStruct.getStRefMeshFrameFlag(idx))
      deltaMfocSt = (2 * refListStruct.getStrafEntrySignFlag(idx) - 1)
                    * refListStruct.getAbsDeltaMfocSt(idx);  // Eq.26
    int refPOC = mfocBase - deltaMfocSt;
    if (refPOC >= 0) referenceList.push_back(refPOC);
    mfocBase = refPOC;
  }

#if 0
  printf("(createSmhReferenceList)referenceList:");
  for ( size_t idx = 0; idx < referenceList.size(); idx++ ) {
    printf("%d\t", (int)referenceList[idx]);
  }
  printf("\n");
#endif

  return referenceList.size() == 0 ? -1 : referenceList[0];
}

size_t
BaseMeshDecoder::calcMaxSubmeshCount(BaseMeshBitstream& bmStream) {
  size_t maxSbmeshCount = 1;
  //auto& bmStream = getBaseMeshDataStream();
  for (auto& mfps : bmStream.getBaseMeshFrameParameterSetList()) {
    maxSbmeshCount = std::max(
      maxSbmeshCount,
      (size_t)(mfps.getSubmeshInformation().getBmsiNumSubmeshesMinus1() + 1));
  }
  return maxSbmeshCount;
}
size_t
BaseMeshDecoder::calcTotalMeshFrameCount(BaseMeshBitstream& bmStream) {
  //auto& bmStream = getBaseMeshDataStream();
  auto&  mslList         = bmStream.getBaseMeshSubmeshLayerList();
  int    prevMspsIndex   = -1;
  size_t totalFrameCount = 0;
  size_t frameCountInMSPS =
    0;  //for the case when this atllist refers multiple MSPS not the overall MSPS
  for (size_t i = 0; i < mslList.size(); i++) {
    auto& mfps =
      bmStream.getBaseMeshFrameParameterSetList()
        [mslList[i].getSubmeshHeader().getSmhSubmeshFrameParameterSetId()];
    int mspsIndex = mfps.getBfpsMeshSequenceParameterSetId();
    if (prevMspsIndex != mspsIndex) {  //new msps
      totalFrameCount += frameCountInMSPS;
      prevMspsIndex    = mspsIndex;
      frameCountInMSPS = 0;
    }
    size_t mfocVal = calculateMFOCval(bmStream, mslList, i);
    mslList[i].getSubmeshHeader().setFrameIndex(
      (uint32_t)(mfocVal + totalFrameCount));
    frameCountInMSPS = std::max(frameCountInMSPS, mfocVal + 1);
  }
  totalFrameCount += frameCountInMSPS;  //is it right?

  return totalFrameCount;
}
size_t
BaseMeshDecoder::calculateMFOCval(BaseMeshBitstream&              bmStream,
                                  std::vector<BaseMeshSubmeshLayer>& smlList,
                                  size_t smlOrder) {
  // 8.2.3.1 Atals frame order count derivation process
  //auto& bmStream = baseMeshBareStream_;
  if (smlOrder == 0) {
    smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntMsb(0);
    smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntVal(
      smlList[smlOrder].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb());
    return smlList[smlOrder].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb();
  }

  size_t prevMeshFrmOrderCntMsb =
    smlList[smlOrder - 1].getSubmeshHeader().getSmhBasemeshFrmOrderCntMsb();
  size_t meshFrmOrderCntMsb = 0;
  auto&  mfps               = bmStream.getBaseMeshFrameParameterSetList()
                 [smlList[smlOrder]
                    .getSubmeshHeader()
                    .getSmhSubmeshFrameParameterSetId()];
  auto& msps = bmStream.getBaseMeshSequenceParameterSetList()
                 [mfps.getBfpsMeshSequenceParameterSetId()];

  size_t maxMeshFrmOrderCntLsb =
    size_t(1) << (msps.getBmspsLog2MaxMeshFrameOrderCntLsbMinus4() + 4);
  size_t afocLsb =
    smlList[smlOrder].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb();
  size_t prevMeshFrmOrderCntLsb =
    smlList[smlOrder - 1].getSubmeshHeader().getSmhBasemeshFrmOrderCntLsb();
  if ((afocLsb < prevMeshFrmOrderCntLsb)
      && ((prevMeshFrmOrderCntLsb - afocLsb) >= (maxMeshFrmOrderCntLsb / 2)))
    meshFrmOrderCntMsb = prevMeshFrmOrderCntMsb + maxMeshFrmOrderCntLsb;
  else if ((afocLsb > prevMeshFrmOrderCntLsb)
           && ((afocLsb - prevMeshFrmOrderCntLsb)
               > (maxMeshFrmOrderCntLsb / 2)))
    meshFrmOrderCntMsb = prevMeshFrmOrderCntMsb - maxMeshFrmOrderCntLsb;
  else meshFrmOrderCntMsb = prevMeshFrmOrderCntMsb;

  smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntMsb(
    meshFrmOrderCntMsb);
  smlList[smlOrder].getSubmeshHeader().setSmhBasemeshFrmOrderCntVal(
    meshFrmOrderCntMsb + afocLsb);
#if 0
  printf("atlPos: %zu\t afocVal: %zu\t afocLsb: %zu\t maxCntLsb:(%zu)\t prevCntLsb:%zu atlasFrmOrderCntMsb: %zu\n", atglOrder, atlasFrmOrderCntMsb + afocLsb, afocLsb, maxAtlasFrmOrderCntLsb,
           prevAtlasFrmOrderCntLsb, atlasFrmOrderCntMsb);
#endif
  return meshFrmOrderCntMsb + afocLsb;
}
