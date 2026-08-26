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

#include <cstdint>
#include <string>

#include "vmc.hpp"
#include "util/checksum.hpp"
#include "util/mesh.hpp"
#include "motionContexts.hpp"
#include "entropy.hpp"
#include "baseMeshSubmeshLayer.hpp"
#include "baseMeshSequenceParameterSetRbsp.hpp"
#include "baseMeshBitstream.hpp"

const int MAX_GOP = 64;

//============================================================================

namespace basemesh {

class BaseMeshEncoderParameters {
public:
  BaseMeshEncoderParameters() { }
  ~BaseMeshEncoderParameters() { };

  bool    checksum            = true;
  int32_t maxNumRefBmeshList  = 1;
  int32_t maxNumRefBmeshFrame = 4;

  int32_t               qpPosition              = 10;
  int32_t               qpTexCoord              = 8;
  int32_t               bitDepthPosition        = 12;
  int32_t               bitDepthTexCoord        = 12;
  double                minPosition[3]          = {0, 0, 0};
  double                maxPosition[3]          = {0, 0, 0};
  int32_t               numSubmesh              = 1;
  int32_t               numTextures             = 1;
  std::vector<uint32_t> submeshIdList;

  int    iDeriveTextCoordFromPos =
    2;  //0 (disabled), 1 (using UV coords), 2 (using FaceId, DEFAULT), 3 (using Connected Components)

  bool enableSignalledIds = false;
  bool keepBaseMesh          = false;
  bool keepIntermediateFiles = false;
  // normals
  bool    encodeNormals     = false;
  int32_t qpNormals         = 16;
  int32_t predNormal        = 3;
  bool    normalsOctahedral = true;
  bool    entropyPacket     = false;
  int32_t qpOcta            = 16;
  int32_t predGeneric       = 0;

  int32_t                       basemeshGOPSize = 0;
  std::vector<vmesh::BaseMeshGOPEntry> basemeshGOPList =
    std::vector<vmesh::BaseMeshGOPEntry>(MAX_GOP);

  // Base mesh
  vmesh::GeometryCodecId meshCodecId                     = vmesh::GeometryCodecId::MPEG;
  bool            dracoUsePosition                = false;
  bool            dracoUseUV                      = false;
  bool            dracoMeshLossless               = false;
  int32_t         motionGroupSize                 = 16;
  bool            motionWithoutDuplicatedVertices = false;
  std::string     baseMeshVertexTraversal         = {"degree"};
  std::string     motionVertexTraversal           = {"degree"};
  bool            baseMeshDeduplication           = false;
  bool            reverseUnification              = false;
  int             profileGeometryCodec            = 0;
  bool bFaceIdPresentFlag = false;

  // Motion
  int32_t maxNumNeighborsMotion       = 3;
  int32_t maxNumMotionVectorPredictor = 3;

  bool useRawUV             = false;
  std::vector<int> refFrameDiff        = {1, 2, 3, 4};

  // that should not be here
  int32_t subdivisionEdgeLengthThreshold          = 0;
  int32_t subdivisionIterationCount = 3;
  int32_t bitshiftEdgeBasedSubdivision         = 6;
  bool   increaseTopSubmeshSubdivisionCount                = false;

};

//============================================================================

class BaseMeshEncoder {
public:
  BaseMeshEncoder() {

  }
  BaseMeshEncoder(const BaseMeshEncoder& rhs)            = delete;
  BaseMeshEncoder& operator=(const BaseMeshEncoder& rhs) = delete;
  ~BaseMeshEncoder()
    = default;

  bool compressBaseMesh(const std::vector<vmesh::VMCSubmesh>& gof,
                        int32_t                        submeshIndex,
                        int32_t                        frameIndex,
                        vmesh::VMCSubmesh&                    frame,
                        BaseMeshSubmeshLayer&          mfdu,
                        const BaseMeshEncoderParameters&    params,
                        std::vector<int32_t>&   prevBaseRepVertexIndices,
                        vmesh::TriangleMesh<MeshType>& baseEnc,
                        std::vector<int32_t>&   mappingWithDupVertex
                        );


  int32_t initializeBaseMeshParameterSets(BaseMeshBitstream& baseMesh,
                                          const BaseMeshEncoderParameters& params);

  void constructSmlRefListStruct(BaseMeshBitstream&    basemesh,
                                 BaseMeshSubmeshLayer& sml,
                                 int32_t               submeshIndex,
                                 int32_t               frameIndex,
                                 int32_t               referenceFrameIndex);

  inline void setKeepFilesPathPrefix(const std::string& path) {
    keepFilesPathPrefix_ = path;
  }

  vmesh::Checksum& getEncChecksum() { return encChecksum_; }

  void
  constructBmspsRefListStruct(BaseMeshSequenceParameterSetRbsp& bmsps,
                              const BaseMeshEncoderParameters&  params);
  void
  constructBmspsRefListStruct(BaseMeshSequenceParameterSetRbsp&    bmsps,
                                               const std::vector<vmesh::BaseMeshGOPEntry>& GOPList);

  void createBaseMeshParameterSet(BaseMeshSequenceParameterSetRbsp& bmsps,
                                              BaseMeshFrameParameterSetRbsp&    bmfps,
                                              const BaseMeshEncoderParameters&       params);

  static bool
  computeVertexAdjTableMotion(const std::vector<vmesh::Vec3<int32_t>>& triangles,
                              int32_t                           vertexCount,
                              int32_t               maxNumNeighborsMotion,
                              std::vector<int32_t>& vertexAdjTableMotion,
                              std::vector<int32_t>& numNeighborsMotion);


private:

  bool updateBaseMesh(vmesh::TriangleMesh<MeshType>&                  origBase,
                      vmesh::TriangleMesh<MeshType>&                  base,
                      std::vector<vmesh::ConnectedComponent<double>>& packedCCList,
                      const BaseMeshEncoderParameters&              params);






  //==========================================================
  static BaseMeshType
              chooseSkipOrInter(const std::vector<vmesh::Vec3<MeshType>>& current,
                                const std::vector<vmesh::Vec3<MeshType>>& reference);





  void encodePredMode(vmesh::EntropyEncoder&     arithmeticEncoder,
                      vmesh::VMCMotionACContext& ctx,
                      int                 mode,
                      int                 maxMode);
  int  compressMotion(const vmesh::VMCSubmesh&           refFrameInput,
                      vmesh::VMCSubmesh&                 current,
                      std::vector<int32_t>&       vertexAdjTableMotion,
                      std::vector<int32_t>&       numNeighborsMotion,
                      BaseMeshSubmeshLayer&       mfdu,
                      const BaseMeshEncoderParameters& param,
                      int32_t                     frameIndex,
                      std::vector<int32_t>&       prevBaseRepVertexIndices);

  static bool addNeighbor(int32_t               vertex,
                          int32_t               vertexNeighbor,
                          int32_t               maxNumNeighborsMotion,
                          std::vector<int32_t>& vertexAdjTableMotion,
                          std::vector<int32_t>& numNeighborsMotion);

  bool
  computeDracoMappingForOrthoAtlasUpdatedBaseMesh(
    vmesh::TriangleMesh<MeshType>      origBase,
    vmesh::TriangleMesh<MeshType>      modifiedBase,
    std::vector<int32_t>&       mapping,
    int32_t                     frameIndex,
    const BaseMeshEncoderParameters& params,
    int32_t                     packedCCListsize,
    bool                        enableMappingWithDupVertex) const;

  bool
  computeDracoMapping(vmesh::TriangleMesh<MeshType>      base,
                      std::vector<int32_t>&       mapping,
                      int32_t                     frameIndex,
                      int32_t                     submeshIndex,
                      const BaseMeshEncoderParameters& params) const;

  std::string          keepFilesPathPrefix_    = {};
  bool                 reuseDuplicatedVertFlag_ = true;
  std::vector<int32_t> numNeighborsMotion_;
  std::vector<int32_t> umapping_;
  std::vector<int32_t> submeshIdtoIndex_;
  std::vector<std::vector<std::vector<int32_t>>> bmspsRefDiffList_;
  vmesh::Checksum encChecksum_;

};

//============================================================================

}  // namespace vmesh
