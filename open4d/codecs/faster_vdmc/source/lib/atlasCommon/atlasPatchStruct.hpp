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

#include "vmc.hpp"
#include "atlasCommon.hpp"
#include "vdmcLiftingTransformParameters.hpp"


namespace atlas {

//struct VMCPatch {
//  std::vector<imageArea>       attributePatchArea;
//  vmesh::imageArea             geometryPatchArea = {0, 0, 0, 0};
//  std::vector<int32_t>         subdivMethod;
//  bool                         lodAdaptiveSubdivisionFlag = 0;
//  int32_t                      subdivIteration;
//  int32_t                      subdivMinEdgeLength = 0;
//  std::vector<uint32_t>        blockCount;
//  std::vector<uint32_t>        lastPosInBlock;
//  DisplacementCoordinateSystem displacementCoordinateSystem =
//    DisplacementCoordinateSystem::LOCAL;
//  uint8_t                transformMethod;
//  QuantizationParameters quantizationParameters;
//  bool                   iqOffsetFlag;
//  std::vector<std::vector<std::vector<std::vector<int8_t>>>> iqOffsets;
//  LiftingTransformParameters transformParameters;
//  int32_t                    submeshId_;
//  int32_t                    displId_;
//  int32_t                    lodIdx_ = 0;
//  int32_t                    projectionTextcoordSubpatchCountMinus1_;
//  int64_t                    projectionTextcoordFrameUpscale_;
//  int                        projectionTextcoordFrameDownscale_;
//  double                     projectionTextcoordFrameScale_;
//  int32_t                    projectionAspsId_ = 0;
//  int32_t                    projectionAfpsId_ = 0;
//  bool                       IQ_skip           = 0;
//  directionalLiftParameterss  dirLiftParams;
//
//  struct Subpatch {
//    int                projectionTextcoordFaceId_        = -1;
//    size_t             projectionTextcoordProjectionId_  = 0;
//    size_t             projectionTextcoordOrientationId_ = 0;
//    size_t             projectionTextcoord2dPosX_        = 0;
//    size_t             projectionTextcoord2dPosY_        = 0;
//    size_t             projectionTextcoord2dBiasX_;
//    size_t             projectionTextcoord2dBiasY_;
//    uint64_t           projectionTextcoord2dSizeXMinus1_    = 0;
//    uint64_t           projectionTextcoord2dSizeYMinus1_    = 0;
//    uint8_t            projectionTextcoordScalePresentFlag_ = 0;
//    size_t             projectionTextcoordSubpatchScale_    = 0;
//    size_t             numRawUVMinus1_                      = 0;
//    std::vector<float> uCoords_;
//    std::vector<float> vCoords_;
//  };
//  std::vector<Subpatch> projectionTextcoordSubpatches_;
//  int32_t               projectionTextcoordMode =
//    0;  // 0: don't generate, 1: generate, 2: copy from reference frame
//  int32_t projectionTexcoordRefFrame;
//  int32_t projectionTexcoordRefPatch;
//};

struct imageArea {
  uint32_t sizeX = 1;
  uint32_t sizeY = 1;
  uint32_t LTx   = 0;
  uint32_t LTy   = 0;

  void printArea() const {
    std::cout << "\t(" << LTx << ", ";
    std::cout << LTx << ", ";
    std::cout << sizeX << ", ";
    std::cout << sizeY << ")\n";
  }
};

struct VMCPatch {
  std::vector<imageArea>       attributePatchArea;
  imageArea             geometryPatchArea = {0, 0, 0, 0};
  std::vector<int32_t>         subdivMethod;
  bool                         lodAdaptiveSubdivisionFlag = 0;
  int32_t                      subdivIteration;
  int32_t                      subdivMinEdgeLength = 0;
  std::vector<uint32_t>        blockCount;
  std::vector<uint32_t>        lastPosInBlock;
  vmesh::DisplacementCoordinateSystem displacementCoordinateSystem =
    vmesh::DisplacementCoordinateSystem::LOCAL;
  uint8_t                transformMethod;
  vmesh::QuantizationParameters quantizationParameters;
  bool                   iqOffsetFlag;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>> iqOffsets;
  vmesh::LiftingTransformParameters transformParameters;
  int32_t                    submeshId_;
  int32_t                    displId_;
  int32_t                    lodIdx_ = 0;
  int32_t                    projectionTextcoordSubpatchCountMinus1_;
  int64_t                    projectionTextcoordFrameUpscale_;
  int                        projectionTextcoordFrameDownscale_;
  double                     projectionTextcoordFrameScale_;
  int32_t                    projectionAspsId_ = 0;
  int32_t                    projectionAfpsId_ = 0;
  bool                       IQ_skip           = 0;
  vmesh::directionalLiftParameters  dirLiftParams;

  struct Subpatch {
    int                projectionTextcoordFaceId_        = -1;
    size_t             projectionTextcoordProjectionId_  = 0;
    size_t             projectionTextcoordOrientationId_ = 0;
    size_t             projectionTextcoord2dPosX_        = 0;
    size_t             projectionTextcoord2dPosY_        = 0;
    size_t             projectionTextcoord2dBiasX_;
    size_t             projectionTextcoord2dBiasY_;
    uint64_t           projectionTextcoord2dSizeXMinus1_    = 0;
    uint64_t           projectionTextcoord2dSizeYMinus1_    = 0;
    uint8_t            projectionTextcoordScalePresentFlag_ = 0;
    size_t             projectionTextcoordSubpatchScale_    = 0;
    size_t             numRawUVMinus1_                      = 0;
    std::vector<float> uCoords_;
    std::vector<float> vCoords_;
  };
  std::vector<Subpatch> projectionTextcoordSubpatches_;
  int32_t               projectionTextcoordMode =
    0;  // 0: don't generate, 1: generate, 2: copy from reference frame
  int32_t projectionTexcoordRefFrame;
  int32_t projectionTexcoordRefPatch;
  };

struct AtlasPatch {
  // use to find patch ?
  TileType tileType_;
  uint32_t tileId_;

  std::vector<imageArea> attributePatchArea;
  imageArea              geometryPatchArea = {0, 0, 0, 0};

  // rest in order that is in specification
  int32_t submeshId_ = 0;
  int32_t displId_   = 0;
  int32_t lodIdx_    = 0;

  int32_t              subdivIteration;
  bool                 lodAdaptiveSubdivisionFlag = 0;
  std::vector<int32_t> subdivMethod;
  int32_t              subdivMinEdgeLength = 0;

  vmesh::DisplacementCoordinateSystem displacementCoordinateSystem =
    vmesh::DisplacementCoordinateSystem::LOCAL;
  uint8_t                transformMethod;
  vmesh::QuantizationParameters quantizationParameters;
  bool                   iqOffsetFlag = false;
  std::vector<std::vector<std::vector<std::vector<int8_t>>>> iqOffsets;
  vmesh::LiftingTransformParameters     transformParameters;  // used in decoder
  VdmcLiftingTransformParameters transformParameter;   // used in encoder
  bool                           IQ_skip = true;
  vmesh::directionalLiftParameters      dirLiftParams;     // used in decoder
  std::vector<int32_t> directionalLiftParameterss;  // used in encoder

  std::vector<uint32_t> blockCount;
  std::vector<uint32_t> lastPosInBlock;
  uint32_t              vertexCount;  // used in encoder only

  double                                  frameScale;
  std::vector<vmesh::ConnectedComponent<double>> packedCCList;
  std::vector<double>                     updateWeight_;
  std::vector<double>                     predWeight_;

  int32_t projectionTextcoordSubpatchCountMinus1_;
  int64_t projectionTextcoordFrameUpscale_;
  int     projectionTextcoordFrameDownscale_;
  double  projectionTextcoordFrameScale_;
  int32_t projectionAspsId_ = 0;
  int32_t projectionAfpsId_ = 0;

  struct Subpatch {
    int                projectionTextcoordFaceId_        = -1;
    size_t             projectionTextcoordProjectionId_  = 0;
    size_t             projectionTextcoordOrientationId_ = 0;
    size_t             projectionTextcoord2dPosX_        = 0;
    size_t             projectionTextcoord2dPosY_        = 0;
    size_t             projectionTextcoord2dBiasX_;
    size_t             projectionTextcoord2dBiasY_;
    uint64_t           projectionTextcoord2dSizeXMinus1_    = 0;
    uint64_t           projectionTextcoord2dSizeYMinus1_    = 0;
    uint8_t            projectionTextcoordScalePresentFlag_ = 0;
    size_t             projectionTextcoordSubpatchScale_    = 0;
    size_t             numRawUVMinus1_                      = 0;
    std::vector<float> uCoords_;
    std::vector<float> vCoords_;
  };

  std::vector<Subpatch> projectionTextcoordSubpatches_;
  // 0: don't generate, 1: generate, 2: copy from reference frame
  int32_t projectionTextcoordMode = 0;
  int32_t projectionTexcoordRefFrame;
  int32_t projectionTexcoordRefPatch;
};

}  // namespace atlas