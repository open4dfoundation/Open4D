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

#include <iostream>
#include "sequenceInfo.hpp"
#include "baseMeshBitstream.hpp"
#include "baseMeshWriter.hpp"
#include "baseMeshEncoder.hpp"


int
main(int argc, char* argv[]) {
  std::cout << "ISO/IEC 23090-29 Annex H test encoder application" << '\n';

  auto inputMeshPath      = "/Users/kondrad/Code/data/vdmc/soldier_voxelized/"
                            "soldier_fr%04d_qp10_qt12.obj";
  auto compressedFilename = "/Users/kondrad/Code/data/vdmc/v12/soldier.bmesh";
  auto logFilename = "/Users/kondrad/Code/data/vdmc/v12/soldier_hls_enc";

  vmesh::Logger                       loggerHls;
  vmesh::Bitstream                    bitstream;
  basemesh::BaseMeshWriter            basemeshWriter;
  basemesh::BaseMeshEncoder           baseMeshEncoder;
  basemesh::BaseMeshBitstream         baseMeshStream;
  basemesh::BaseMeshEncoderParameters baseMeshEncoderParams;
  basemesh::BaseMeshBitstreamGofStat  baseMeshStat;

  loggerHls.initilalize(logFilename, true);
#if defined(BITSTREAM_TRACE)
  basemeshWriter.setLogger(loggerHls);
  bitstream.setLogger(loggerHls);
  bitstream.setTrace(true);
#endif
  // for sold s2 c1 r5
  baseMeshEncoderParams.checksum            = true;
  baseMeshEncoderParams.maxNumRefBmeshList  = 1;
  baseMeshEncoderParams.maxNumRefBmeshFrame = 4;
  baseMeshEncoderParams.qpPosition          = 11;
  baseMeshEncoderParams.qpTexCoord          = 10;
  baseMeshEncoderParams.bitDepthPosition    = 10;
  baseMeshEncoderParams.bitDepthTexCoord    = 12;
  baseMeshEncoderParams.minPosition[0]      = -0.3662;
  baseMeshEncoderParams.minPosition[1]      = 1.1072;
  baseMeshEncoderParams.minPosition[2]      = 0.2249;
  baseMeshEncoderParams.maxPosition[0]      = 508.764;
  baseMeshEncoderParams.maxPosition[1]      = 1023.37;
  baseMeshEncoderParams.maxPosition[2]      = 637.421;
  baseMeshEncoderParams.numSubmesh          = 1;
  baseMeshEncoderParams.numTextures         = 1;
  baseMeshEncoderParams.submeshIdList.resize(1);
  baseMeshEncoderParams.submeshIdList[0]        = 0;
  baseMeshEncoderParams.iDeriveTextCoordFromPos = 0;
  baseMeshEncoderParams.enableSignalledIds      = false;
  baseMeshEncoderParams.keepBaseMesh            = false;
  baseMeshEncoderParams.keepIntermediateFiles   = false;
  baseMeshEncoderParams.encodeNormals           = false;
  baseMeshEncoderParams.qpNormals               = 16;
  baseMeshEncoderParams.predNormal              = 3;
  baseMeshEncoderParams.normalsOctahedral       = true;
  baseMeshEncoderParams.entropyPacket           = false;
  baseMeshEncoderParams.qpOcta                  = 16;
  baseMeshEncoderParams.predGeneric             = 0;
  baseMeshEncoderParams.meshCodecId             = vmesh::GeometryCodecId::MPEG;
  baseMeshEncoderParams.dracoUsePosition        = false;
  baseMeshEncoderParams.dracoUseUV              = false;
  baseMeshEncoderParams.dracoMeshLossless       = false;
  baseMeshEncoderParams.motionGroupSize         = 16;
  baseMeshEncoderParams.motionWithoutDuplicatedVertices = false;
  baseMeshEncoderParams.baseMeshVertexTraversal         = "degree";
  ;
  baseMeshEncoderParams.motionVertexTraversal = "degree";
  ;
  baseMeshEncoderParams.baseMeshDeduplication       = false;
  baseMeshEncoderParams.reverseUnification          = false;
  baseMeshEncoderParams.profileGeometryCodec        = 0;
  baseMeshEncoderParams.bFaceIdPresentFlag          = false;
  baseMeshEncoderParams.maxNumNeighborsMotion       = 3;
  baseMeshEncoderParams.maxNumMotionVectorPredictor = 3;
  baseMeshEncoderParams.useRawUV                    = false;

  vmesh::SequenceInfo sequenceInfo;
  sequenceInfo.generate(3, 536, 31);
  vmesh::Sequence                             source;
  std::vector<std::vector<vmesh::VMCSubmesh>> submeshes;
  submeshes.resize(1);
  // Load source mesh sequence
  for (auto& gofInfo : sequenceInfo) {
    if (!source.load(
          inputMeshPath, gofInfo.startFrameIndex_, gofInfo.frameCount_)) {
      std::cerr << "Error: can't load source sequence\n";
      return 1;
    }

    printf("Frame reference structure from sequence.generate() \n");
    for (int fi = 0; fi < gofInfo.frameCount_; fi++) {
      printf("frame[%d]\tref: %d\tprev: %d\n",
             fi,
             gofInfo.frameInfo(fi).referenceFrameIndex,
             gofInfo.frameInfo(fi).previousFrameIndex);
    }

    // Base mesh sequence
    baseMeshStream.setAtlasId(0);
    baseMeshStream.setV3CParameterSetId(0);
    baseMeshEncoder.initializeBaseMeshParameterSets(baseMeshStream,
                                                    baseMeshEncoderParams);

    submeshes[0].resize(gofInfo.frameCount_);
    printf("Compress: frameCount = %d \n", gofInfo.frameCount_);
    fflush(stdout);

    std::vector<int32_t>                       prevBaseRepVertexIndices;
    std::vector<vmesh::TriangleMesh<MeshType>> baseEncList;
    baseEncList.resize(gofInfo.frameCount_);

    std::vector<std::vector<int32_t>> mappingWithDupVertexList;
    mappingWithDupVertexList.resize(gofInfo.frameCount_);
    auto submeshIndex = 0;
    for (int32_t frameIndex = 0; frameIndex < gofInfo.frameCount_;
         ++frameIndex) {
      submeshes[submeshIndex][frameIndex].frameIndex =
        gofInfo.frameInfo(frameIndex).frameIndex;
      submeshes[submeshIndex][frameIndex].submeshType =
        gofInfo.frameInfo(frameIndex).type;
      submeshes[submeshIndex][frameIndex].base = source.mesh(frameIndex);
      auto& bmsl = baseMeshStream.addBaseMeshSubmeshLayer();

      baseMeshEncoder.compressBaseMesh(submeshes[submeshIndex],
                                       submeshIndex,
                                       frameIndex,
                                       submeshes[submeshIndex][frameIndex],
                                       bmsl,
                                       baseMeshEncoderParams,
                                       prevBaseRepVertexIndices,
                                       baseEncList[frameIndex],
                                       mappingWithDupVertexList[frameIndex]);
    }
  }

  // encode bitstream
  basemeshWriter.encode(baseMeshStream, bitstream, baseMeshStat);

  if (!bitstream.save(compressedFilename)) {
    std::cerr << "Error: can't save compressed bitstream!\n";
    return 1;
  }
  return 0;
}
