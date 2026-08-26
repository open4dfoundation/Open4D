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

#pragma once

#include <stdio.h>
#include "vmc.hpp"
#include "entropy.hpp"
#include "geometryDecoder.hpp"
#include "motionContexts.hpp"
#include "bitstream.hpp"
#include "baseMeshBitstream.hpp"

namespace basemesh {

struct externalVariables {
  size_t              externalBitdepth_ = 1;
};  //TODO: this needs to be removed to make basemesh decoding self complete. it requires normative changes.

class BaseMeshDecoder {
public:
  BaseMeshDecoder() {}
  ~BaseMeshDecoder() {}
  void setBasemeshSubBitstream();

  void decodeBasemeshSubbitstream(BaseMeshBitstream& bmStream,
                                  bool               keepBasemesh,
                                  bool               checksum);
  std::vector<std::vector<vmesh::VMCBasemesh>>& getDecodedMeshFrames() {
    return decodedFrames_;
  }
  size_t getMeshFrameCount() { return decodedFrames_[0].size(); }
  size_t getMaxSubmeshCount(basemesh::BaseMeshBitstream& bmStream) {
    return calcMaxSubmeshCount(bmStream);
  }
  void setExternalBitdepth(size_t bitDepthPosition) {
    extVariables_.externalBitdepth_ = bitDepthPosition;
  }

  void setKeepFilesPathPrefix(std::string path) {
    keepFilesPathPrefix_ = path;
  }

private:
  using VertexIndices    = std::vector<int32_t>;
  using RefVertexIndices = std::vector<VertexIndices>;

  size_t getExternalBitDepth() { return extVariables_.externalBitdepth_; }

  bool addNeighbor(int32_t               vertex1,
                   int32_t               vertex2,
                   int32_t               maxNumNeighborsMotion,
                   std::vector<int32_t>& vertexAdjTableMotion,
                   std::vector<int32_t>& numNeighborsMotion);

  bool computeVertexAdjTableMotion(
    const std::vector<vmesh::Vec3<int32_t>>& triangles,
    int32_t                                  vertexCount,
    int32_t                                  maxNumNeighborsMotion,
    std::vector<int32_t>&                    vertexAdjTableMotion,
    std::vector<int32_t>&                    numNeighborsMotion);
  int decodePredMode(vmesh::EntropyDecoder&     arithmeticDecoder,
                     vmesh::VMCMotionACContext& ctx,
                     int                        maxMode);

  bool decompressMotion(basemesh::BaseMeshSubmeshLayer& bmsl,
                        const vmesh::VMCBasemesh&       refFrameInput,
                        std::vector<int32_t>&           numNeighborsMotion,
                        int                             maxNumNeighbours,
                        std::vector<int32_t>&           vertexAdjTableMotion,
                        vmesh::VMCBasemesh&             current);

  const vmesh::VMCBasemesh*
  findReferenceFrame(const std::vector<vmesh::VMCBasemesh>& referenceFrameList,
                     int32_t referenceFrameAbsoluteIndex,  //0~31
                     int32_t currentFrameAbsoluteIndex);

  const vmesh::VMCBasemesh*
  findReferenceFrame(const std::vector<vmesh::VMCBasemesh>& referenceFrameList,
                     int32_t referenceFrameAbsoluteIndex,  //0~31
                     const std::vector<int32_t>& decompressedFrameIndices);

  bool decompressBaseMesh(
    const basemesh::BaseMeshBitstream& bmStream,
    //const V3CParameterSet&                vps,
    int32_t                         frameIndex,
    basemesh::BaseMeshSubmeshLayer& bmsl,
    std::vector<std::vector<vmesh::VMCBasemesh>>&
         referenceFrameList,  //TODO: [reference buffer]
    bool keepBasemesh,
    std::vector<std::vector<int32_t>>& decompressedFrameIndices);
  int32_t
  createMspsReferenceLists(basemesh::BaseMeshSequenceParameterSetRbsp& msps,
                           std::vector<std::vector<int32_t>>& mspsRefDiffList);
  int32_t
  createSmhReferenceList(basemesh::BaseMeshSequenceParameterSetRbsp& bmsps,
                         basemesh::BaseMeshSubmeshLayer&             submesh,
                         std::vector<int32_t>& referenceList);

  size_t calcMaxSubmeshCount(basemesh::BaseMeshBitstream& bmStream);
  size_t calcTotalMeshFrameCount(basemesh::BaseMeshBitstream& bmStream);
  size_t calculateMFOCval(basemesh::BaseMeshBitstream& bmStream,
                          std::vector<basemesh::BaseMeshSubmeshLayer>& smlList,
                          size_t smlOrder);
  //--------
  std::vector<std::vector<std::vector<int32_t>>> mspsSequenceRefDiffList_;
  std::vector<uint32_t> submeshIdtoIndex_;  //orange -> 0

  //bool                                 reuseDuplicatedVertFlag_ = true;
  std::vector<std::vector<vmesh::VMCBasemesh>>
    decodedFrames_;  //[submesh][frame]

  std::string       keepFilesPathPrefix_ = "";
  externalVariables extVariables_;
};

};  // namespace basemesh