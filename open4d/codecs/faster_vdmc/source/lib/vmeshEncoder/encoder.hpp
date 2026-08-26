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

#include "util/mesh.hpp"
#include "vmc.hpp"
#include "v3cBitstream.hpp"
#include "util/checksum.hpp"
#include "motionContexts.hpp"
#include "entropy.hpp"
#include <UVAtlas.h>
#include "colourConverter.hpp"
#include "videoEncoder.hpp"
#include "../basemeshEncoder/baseMeshEncoder.hpp"
#include "../acDisplacementEncoder/acDisplacementEncoder.hpp"
#include "../atlasEncoder/atlasEncoder.hpp"
#include "../atlasDecoder/atlasDecoder.hpp"
#include "../atlasCommon/atlasTileStruct.hpp"

#define ORTHOATLAS_ADJ 1
#define ORTHOATLAS_ADJ_DEBUG 0
#define COMPRESS_VIDEO_PAC_ENABLE 1

//============================================================================
namespace DirectX {
static std::istream&
operator>>(std::istream& in, UVATLAS& val) {
  std::string str;
  in >> str;
  if (str == "DEFAULT") {
    val = UVATLAS_DEFAULT;
  } else if (str == "FAST") {
    val = UVATLAS_GEODESIC_FAST;
  } else if (str == "QUALITY") {
    val = UVATLAS_GEODESIC_QUALITY;
  } else {
    in.setstate(std::ios::failbit);
  }
  return in;
}

//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, UVATLAS val) {
  switch (val) {
  case UVATLAS_DEFAULT: out << "DEFAULT"; break;
  case UVATLAS_GEODESIC_FAST: out << "FAST"; break;
  case UVATLAS_GEODESIC_QUALITY: out << "QUALITY"; break;
  default: out << int(val) << " (unknown)";
  }
  return out;
}
}  // namespace DirectX

//============================================================================

namespace vmesh {

//============================================================================

static std::istream&
operator>>(std::istream& in, CachingPoint& val) {
  std::string str;
  in >> str;
  if (str == "simplify" || str == "1") {
    val = CachingPoint::SIMPLIFY;
  } else if (str == "texgen" || str == "2") {
    val = CachingPoint::TEXGEN;
  } else if (str == "subdiv" || str == "3") {
    val = CachingPoint::SUBDIV;
  } else {
    val = CachingPoint::NONE;
  }
  return in;
}

//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, CachingPoint val) {
  switch (val) {
  case CachingPoint::NONE: out << "none"; break;
  case CachingPoint::SIMPLIFY: out << "simplify"; break;
  case CachingPoint::TEXGEN: out << "texgen"; break;
  case CachingPoint::SUBDIV: out << "subdiv"; break;
  default: out << int(val) << " (unknown)";
  }
  return out;
}

//============================================================================

static std::istream&
operator>>(std::istream& in, SmoothingMethod& val) {
  unsigned int tmp = 0;
  in >> tmp;
  val = SmoothingMethod(tmp);
  return in;
}

//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, SmoothingMethod val) {
  switch (val) {
  case SmoothingMethod::NONE: out << "0"; break;
  case SmoothingMethod::VERTEX_CONSTRAINT: out << "1"; break;
  }
  return out;
}

//============================================================================

static std::istream&
operator>>(std::istream& in, PaddingMethod& val) {
  std::string str;
  in >> str;
  if (str == "push_pull" || str == "1") {
    val = PaddingMethod::PUSH_PULL;
  } else if (str == "sparse_linear" || str == "2") {
    val = PaddingMethod::SPARSE_LINEAR;
  } else if (str == "smoothed_push_pull" || str == "3") {
    val = PaddingMethod::SMOOTHED_PUSH_PULL;
  } else if (str == "harmonic" || str == "4") {
    val = PaddingMethod::HARMONIC_FILL;
  } else {
    val = PaddingMethod::NONE;
  }
  return in;
}

//----------------------------------------------------------------------------

static std::ostream&
operator<<(std::ostream& out, PaddingMethod val) {
  switch (val) {
  case PaddingMethod::NONE: out << "none"; break;
  case PaddingMethod::PUSH_PULL: out << "push_pull"; break;
  case PaddingMethod::SPARSE_LINEAR: out << "sparse_linear"; break;
  case PaddingMethod::SMOOTHED_PUSH_PULL: out << "smoothed_push_pull"; break;
  case PaddingMethod::HARMONIC_FILL: out << "harmonic"; break;
  }
  return out;
}

//============================================================================

static std::istream&
operator>>(std::istream& in, DisplacementQuantizationType& type) {
  return in >> reinterpret_cast<int&>(type);
}

//----------------------------------------------------------------------------
static std::ostream&
operator<<(std::ostream& out, DisplacementQuantizationType val) {
  switch (val) {
  case DisplacementQuantizationType::DEFAULT: out << "0"; break;
  case DisplacementQuantizationType::ADAPTIVE: out << "1"; break;
  }
  return out;
}

static const int MAX_GOP = 64;
struct VMCEncoderAttributeParameters {
  bool              enablePadding                = true;
  bool              textureTransferEnable        = true;
  int32_t           textureWidth                 = 2048;
  int32_t           textureHeight                = 2048;
  int32_t           textureInputBitDepth         = 8;
  int32_t           textureVideoBitDepth         = 10;
  int32_t           textureVideoQP               = 8;
  std::string       textureVideoEncoderConfig    = {};
  std::string       textureVideoHDRToolEncConfig = {};
  std::string       textureVideoHDRToolDecConfig = {};
  int32_t           textureVideoDownsampleFilter = 4;
  int32_t           textureVideoUpsampleFilter   = 0;
  bool              textureVideoFullRange        = false;
  VideoEncoderId    textureVideoEncoderId        = VideoEncoderId::HM;
  bool              textureVideoUseX265          = false;
  bool              textureVideoAllIntra         = false;
  bool              textureVideoWavefront        = true;
  int32_t           textureVideoFrameThreads     = 0;
  int32_t           textureVideoPoolThreads      = 0;
  int32_t           textureVideoTileColumns      = 1;
  int32_t           textureVideoTileRows         = 1;
  int32_t           textureVideoSliceCount       = 1;
  std::string       textureVideoX265Preset       = "fast";
  VideoCodecGroupIdc textureCodecId               = (VideoCodecGroupIdc)7;
  bool              textureBGR444                = false;
  bool              texturePlacementPerPatch     = true;
  bool              useOccMapRDO                 = true;
  std::string       occMapFilename               = "_occmap.yuv";
};
class VMCEncoderParameters {
public:
  VMCEncoderParameters() { attributeParameters.resize(1); }
  ~VMCEncoderParameters() { attributeParameters.clear(); };

  AtlasEncoderParameters              atlasEncoderParameters_;
  basemesh::BaseMeshEncoderParameters baseMeshEncoderParameters_;
  acdisplacement::AcDisplacementEncoderParameters
    acDisplacementEncoderParameters_;

  //mode
  bool setLosslessTools = false;
  //maxNumRefFrame
  bool    checksum            = true;
  int32_t maxNumRefBmeshList  = 1;
  int32_t maxNumRefBmeshFrame = 4;
  // Mesh
  int32_t               qpPosition              = 10;
  int32_t               qpTexCoord              = 8;
  int32_t               bitDepthPosition        = 12;
  int32_t               bitDepthTexCoord        = 12;
  double                minPosition[3]          = {0, 0, 0};
  double                maxPosition[3]          = {0, 0, 0};
  int32_t               submeshSegmentationType = 0;
  int32_t               numSubmesh              = 1;
  int32_t               numTextures             = 1;
  std::vector<uint32_t> submeshIdList;

  bool   allowSubmeshOverlap                  = true;
  double segmentThreshold                     = 100.0;
  bool   linearSegmentation                   = true;
  bool   meshCodecSpecificParametersInBMSPS   = false;
  bool   motionCodecSpecificParametersInBMSPS = false;
  bool   deformForRecBase                     = true;
  bool   segmentByBaseMesh                    = false;

  // Gof analysis
  int32_t groupOfFramesMaxSize = 32;
  bool    analyzeGof           = false;
  // scalable coding
  bool              scalableEnableFlag  = false;
  std::string       resultPath          = {};
  int32_t           maxLayersMinus1     = -1;
  std::string       vtmLayerConfig      = {};
  VideoCodecGroupIdc profileCodecGroupId = VideoCodecGroupIdc::HEVC_Main10;
  // Coding structure
  int32_t                       basemeshGOPSize = 0;
  std::vector<BaseMeshGOPEntry> basemeshGOPList =
    std::vector<BaseMeshGOPEntry>(MAX_GOP);
  // Geometry video
  uint32_t          log2GeometryVideoBlockSize    = 6;  // 64 x 64
  int32_t           geometryVideoWidthInBlocks    = 4;
  int32_t           geometryVideoBitDepth         = 10;
  std::string       geometryVideoEncoderConfig    = {};
  VideoEncoderId    geometryVideoEncoderId        = VideoEncoderId::HM;
  VideoCodecGroupIdc geometryCodecId               = (VideoCodecGroupIdc)7;
  bool              displacementReversePacking    = true;
  uint32_t          displacementVideoChromaFormat = 1;

  // Texture video
  std::vector<int32_t>                       videoAttributeTypes = {0};
  int32_t                                    videoAttributeCount = 0;
  std::vector<VMCEncoderAttributeParameters> attributeParameters;
  int32_t                                    referenceAttributeIdx = 0;

  // Packed video
  bool    jointTextDisp        = false;
  int32_t textureSliceMode     = 0;
  int32_t textureSliceArgument = 1500;

  // Base mesh
  GeometryCodecId meshCodecId                     = GeometryCodecId::MPEG;
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
  int32_t         TrackedMode                     = 0;
  double          log10GeometryParametrizationQuantizationValue = 5;

  // Displacements
  DisplacementCoordinateSystem displacementCoordinateSystem =
    DisplacementCoordinateSystem::LOCAL;

  DisplacementQuantizationType displacementQuantizationType =
    DisplacementQuantizationType::DEFAULT;

  int32_t               encodeDisplacementType          = 2;
  bool                  IQSkipFlag                      = false;
  bool                  encodeTextureVideo              = true;
  bool                  applyOneDimensionalDisplacement = true;
  bool                  applyLiftingOffset              = true;
  bool                  lodDisplacementQuantizationFlag = false;
  std::vector<uint32_t> subBlocksPerLoD                 = {1, 1, 1, 1};
  std::vector<bool>     blockSubdiv = {false, false, true, true};
  double                displacementFillZerosThreshold = 0.05;
  bool                  displacementFillZerosFlag      = false;
  bool                  lodPatchesEnable               = false;

  // Motion
  int32_t maxNumNeighborsMotion       = 3;
  int32_t maxNumMotionVectorPredictor = 3;

  // Subdivision
  int32_t subdivisionIterationCount            = 3;
  bool    interpolateSubdividedNormalsFlag     = false;
  int32_t subdivisionIterationCountEncoderOpti = 4;
  double  floatSubdivisionEdgeLengthThreshold  = 0.0;
  int32_t subdivisionEdgeLengthThreshold       = 0;
  int32_t bitshiftEdgeBasedSubdivision         = 6;

  // Quantization
  std::vector<uint32_t> log2LevelOfDetailInverseScale     = {1, 1, 1};
  std::vector<uint32_t> adaptiveScale                     = {110, 110, 110};
  std::vector<uint32_t> geoVideoQP                        = {16, 28, 28};
  std::vector<uint32_t> liftingQP                         = {16, 28, 28};
  std::vector<std::array<int32_t, 3>> qpPerLevelOfDetails = {{16, 28, 28},
                                                             {22, 34, 34},
                                                             {28, 40, 40}};
  int32_t                             bitDepthOffset      = 0;
  double liftingBias[3] = {1. / 3., 1. / 3., 1. / 3.};

  // Lifting
  int32_t transformMethod = 1;  // 0: None Transform, 1: Linear Lifting m68642
  int32_t liftingLog2PredictionWeight                    = 1;  //0.5;
  bool    liftingSkipUpdate                              = false;
  bool    liftingAdaptivePredictionWeightFlag            = false;
  bool    liftingAdaptiveUpdateWeightFlag                = true;
  bool    liftingValenceUpdateWeightFlag                 = true;
  std::vector<uint32_t> liftingPredictionWeightNumerator = {1, 1, 1, 1};
  std::vector<uint32_t> liftingPredictionWeightDenominatorMinus1 = {3,
                                                                    1,
                                                                    1,
                                                                    1};
  std::vector<uint32_t> liftingPredictionWeightNumeratorDefault = {1, 1, 1, 1};
  std::vector<uint32_t> liftingPredictionWeightDenominatorMinus1Default = {1,
                                                                           1,
                                                                           1,
                                                                           1};
  std::vector<uint32_t> liftingUpdateWeightNumerator         = {63, 21, 7, 1};
  std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1 = {79, 19, 4, 0};
  bool                  InverseQuantizationOffsetFlag        = true;
  //directional lifting
  bool     dirlift                = false;
  uint32_t dirliftScale1          = 950;
  uint32_t dirliftDeltaScale2     = 25;
  uint32_t dirliftDeltaScale3     = 20;
  uint32_t dirliftScaleDenoMinus1 = 999;

  // Texture transfer
  int32_t       textureTransferSamplingSubdivisionIterationCount = 3;
  int32_t       textureTransferThreadCount                        = 0;
  int32_t       textureTransferPaddingBoundaryIterationCount     = 2;
  int32_t       textureTransferPaddingDilateIterationCount       = 2;
  PaddingMethod textureTransferPaddingMethod =
    PaddingMethod::SMOOTHED_PUSH_PULL;
  double  textureTransferPaddingSparseLinearThreshold = 0.05;  //0.005
  double  textureTransferBasedPointcloud              = true;
  double  textureTransferPreferUV                     = false;
  bool    textureTransferWithMap                      = false;
  bool    textureTransferWithMapSource                = false;
  int32_t textureTransferMapSamplingParam             = 1;
  int32_t textureTransferGridSize                     = 0;
  int32_t textureTransferMapProjDim                   = 0;
  int32_t textureTransferMapNumPoints                 = 8;
  int32_t textureTransferMethod                       = 0;
  float   textureTransferSigma                        = 0.2f;
  bool    textureTransferCopyBackground               = true;

  // Input
  bool unifyVertices     = false;
  bool invertOrientation = false;

  // Output
  bool dequantizeUV          = true;
  bool keepIntermediateFiles = false;
  bool keepPreMesh           = false;
  bool keepBaseMesh          = false;
  bool keepVideoFiles        = false;
  bool reconstructNormals    = true;

  // GeometryDecimate
  int32_t texCoordQuantizationBits        = 0;
  int32_t minCCTriangleCount              = 0;
  double  targetTriangleRatio             = 0.125;
  double  triangleFlipThreshold           = 0.3;
  double  trackedTriangleFlipThreshold    = 0.1;
  double  trackedPointNormalFlipThreshold = 0.5;

  // TextureParametrization
  int textureParameterizationType = 0;  // 0: UVAtlas, 1: ortho, -1 : NONE
  //ortho parameters
  bool   useVertexCriteria       = false;
  bool   bUseSeedHistogram       = true;
  double strongGradientThreshold = 180;
  double maxCCAreaRatio          = 1;
  int    maxNumFaces             = std::numeric_limits<int>::max();
  bool   bFaceClusterMerge       = true;
  double lambdaRDMerge           = 1.0;
  bool   check2DConnectivity     = true;
  bool   adjustNormalDirection   = false;
  bool   applyReprojection       = false;
  bool   mergeSmallCC            = false;
  int    smallCCFaceNum          = 1;
  bool   bPatchScaling           = false;
  bool   bTemporalStabilization  = false;
  int    iDeriveTextCoordFromPos =
    2;  //0 (disabled), 1 (using UV coords), 2 (using FaceId, DEFAULT), 3 (using Connected Components)
  bool   use45DegreeProjection = false;
  double packingScaling        = 0.95;
  bool   packSmallPatchesOnTop = false;
  int    packingType           = 0;
  int    updateTextureSize     = 2;
  bool   biasEnableFlag        = false;
#if ORTHOATLAS_ADJ
  bool doParameterAdjustment = true;
  int  log2AnalysisBlockSize = 0;
#endif
  bool bFaceIdPresentFlag = false;

  int32_t maxCUWidth        = 64;
  int32_t maxCUHeight       = 64;
  int32_t maxPartitionDepth = 4;

  bool useRawUV             = false;
  int  targetCountThreshold = 5;
  int  rawTextcoordBitdepth = 6;

  //UV atlas parameters
  size_t           maxCharts      = size_t();
  float            maxStretch     = 0.16667F;
  float            gutter         = 2.F;
  int32_t          texParamWidth  = 512;
  int32_t          texParamHeight = 512;
  DirectX::UVATLAS uvOptions      = DirectX::UVATLAS_DEFAULT;

  int32_t texturePackingWidth  = -1;
  int32_t texturePackingHeight = -1;

  // GeometryParametrization
  bool                              baseIsSrc              = false;
  bool                              subdivIsBase           = false;
  bool                              subdivInter            = false;
  bool                              subdivInterWithMapping = false;
  bool                              encoderOptiFlag        = false;
  float                             maxAllowedD2PSNRLoss   = 1.F;
  GeometryParametrizationParameters intraGeoParams;
  GeometryParametrizationParameters interGeoParams;

  // Caching
  CachingPoint cachingPoint     = CachingPoint::NONE;
  std::string  cachingDirectory = {};

  // Bitsteam
  uint32_t forceSsvhUnitSizePrecision = 0;

  // New
  int32_t          maxNumRefAtlasList  = 1;
  int32_t          maxNumRefAtlasFrame = 4;
  std::vector<int> refFrameDiff        = {1, 2, 3, 4};
  int32_t          patchSegmentMethod =
    2;  //0. facegroup 1. connected component 2.single patch 3.one triangle one patch"
  std::string compressedFilename   = {};
  int32_t     texturePaddingMethod = (int32_t)PaddingMethod::SPARSE_LINEAR;

  // zippering variables
  int zipperingMethod_ =
    -1;  // -1: deactivated, 0/1: per sequence (fixed or max value), 2: per frame (max value), 3: per sub-mesh (max value), 4: per border value, 5: using indexes 7: boundary connect
  size_t zipperingThreshold_                               = 5;
  int    zipperingMethodForUnmatchedLoDs_                  = 0;
  bool   increaseTopSubmeshSubdivisionCount                = false;
  double zipperingMatchedBorderPointDeltaVarianceThreshold = 0.0;
  bool   LoDExtractionEnable                               = false;
  bool   attributeExtractionEnable                         = false;
  // submesh tile mapping SEI
  bool tileSubmeshMappingSEIFlag_ = false;
  // transformation params SEI
  bool attributeTransformParamsV3CSEIEnable      = false;
  bool attributeTransformParamsBaseMeshSEIEnable = false;
  // Metrics
  bool normalCalcModificationEnable = true;

  //Atlas Data Sub-bitstream
  int32_t                            numTilesGeometry  = 1;
  int32_t                            numTilesAttribute = 1;
  std::vector<uint32_t>              tileIdList;
  std::vector<std::vector<uint32_t>> submeshIdsInTile;  //={{0,2}, {1}};

  bool enableSignalledIds = false;

  // Normals
  bool    encodeNormals     = false;
  bool    normalInt         = true;
  int32_t qpNormals         = 16;
  int32_t bitDepthNormals   = 16;
  double  minNormals[3]     = {-1.0, -1.0, -1.0};
  double  maxNormals[3]     = {1.0, 1.0, 1.0};
  int32_t predNormal        = 3;
  bool    normalsOctahedral = true;
  bool    entropyPacket     = false;
  int32_t qpOcta            = 16;
  int32_t predGeneric       = 0;
};

//============================================================================

class VMCEncoder {
public:
  VMCEncoder(uint32_t videoAttCount) {
    //    subTextureVideoAreas.resize(1);
    //    subTextureVideoAreas[0].LTx=0;
    //    subTextureVideoAreas[0].LTy=0;
    //    subTextureVideoSize.resize(1);
    //    subTextureVideoTL.resize(1);
    //    subTextureVideoTL[0].first  = 0;
    //    subTextureVideoTL[0].first  = 0;
    //    sourceAttributeColourSpace_ = ColourSpace::BGR444p;
    //    videoAttributeColourSpace_  = ColourSpace::YUV420p;
    //    sourceAttributeBitdetph_    = 8;
    //    videoAttributeBitdetph_     = 10;
    encChecksum_.checksumAtts().resize(videoAttCount);
    attributeVideoSize_.resize(videoAttCount);
    sourceAttributeColourSpaces_.resize(videoAttCount);
    ;
    videoAttributeColourSpaces_.resize(videoAttCount);
    ;
    sourceAttributeBitdetphs_.resize(videoAttCount);
    ;
    videoAttributeBitdetphs_.resize(videoAttCount);
    ;
  }
  VMCEncoder(const VMCEncoder& rhs)            = delete;
  VMCEncoder& operator=(const VMCEncoder& rhs) = delete;
  ~VMCEncoder()                                = default;
  static std::vector<std::string>
          makeSubmeshPaths(const std::string& srcFilenamePath,
                           const std::string& submeshPrefix,
                           uint32_t           submeshCount);
  int32_t submeshSegment(const std::string&          sourceMeshPath,
                         std::vector<std::string>&   submeshPaths,
                         uint32_t                    numSubmeshes0,
                         uint32_t                    firstFrameIndex0,
                         uint32_t                    frameCount0,
                         const VMCEncoderParameters& params,
                         std::string&                submeshFolderPath,
                         bool                        loadOnly);
  int32_t
       submeshSegmentByTriangleDensity(const std::string&          sourceMeshPath,
                                       std::vector<std::string>&   submeshPaths,
                                       uint32_t                    numSubmeshes0,
                                       uint32_t                    firstFrameIndex0,
                                       uint32_t                    frameCount0,
                                       const VMCEncoderParameters& params,
                                       std::string& submeshFolderPath,
                                       bool         loadOnly);
  bool submeshSegmentByBaseMesh(const VMCGroupOfFramesInfo&     gofInfo,
                                const std::vector<std::string>& submeshPaths);
  void setSubtextureVideoSize(int32_t                 textureWidth,
                              int32_t                 textureHeight,
                              std::vector<imageArea>& subTextureVideoAreas,
                              int32_t                 submeshCount);
  //  void    setGeometryVideoCodecConfig(const VMCEncoderParameters& encParams);
  void    setAttributeVideoCodecConfig(const VMCEncoderParameters& encParams);
  void    setFilePaths(std::vector<std::string>&       inputSubmeshPaths,
                       std::string                     inputMeshPath,
                       const std::vector<std::string>& inputAttributesPaths,
                       std::string                     reconstructMeshPath,
                       const std::vector<std::string>& reconstructAttributesPaths,
                       std::string reconstructedMaterialLibPath,
                       int32_t     startFrame);
  int32_t initializeParameterSets(V3cBitstream&               syntax,
                                  V3CParameterSet&            vps,
                                  int32_t                     atlasCount,
                                  int32_t                     atlasId,
                                  int32_t                     gofIndex,
                                  const VMCEncoderParameters& params);
  void setProfileAndLevels(V3cBitstream& syntax, 
    V3CParameterSet& vps,
    const VMCEncoderParameters& params);

  void setIdIndexMapping(const VMCEncoderParameters& encParams);
  bool initializeImageSizesAndTiles(const VMCEncoderParameters& params,
                                    int32_t                     frameCount);
  bool adjustTextureImageSize(const VMCEncoderParameters& encParams,
                              int32_t                     frameCount);

  bool
  compressMesh(const VMCGroupOfFramesInfo& gofInfo,
               //const Sequence&                 source,
               std::vector<vmesh::VMCSubmesh>&    encSubmeshes,
               V3cBitstream&                      syntax,
               V3CParameterSet&                   vps,
               int32_t                            submeshIndex,
               int32_t                            submeshCount,
               const VMCEncoderParameters&        params,
               std::vector<std::vector<int32_t>>& submeshLodMap,
               std::vector<std::vector<int32_t>>& submeshChildToParentMap);

  void updateMesh(
    std::vector<std::vector<VMCSubmesh>>& encFrames,  //[submesh][frame]
    V3cBitstream&                         syntax,
    const VMCEncoderParameters&           params);

  void compressAtlas(std::vector<std::vector<VMCSubmesh>>& encFrames,
                     V3cBitstream&                         syntax,
                     V3CParameterSet&                      vps,
                     const VMCEncoderParameters&           encParams);

  bool segmentByBaseMesh(VMCGroupOfFramesInfo&           gofInfo,
                         const Sequence&                 source,
                         const std::vector<std::string>& submeshPaths,
                         const VMCEncoderParameters&     params);
  bool preprocessMeshes(const VMCGroupOfFramesInfo&     gofInfoSrc,
                        const Sequence&                 source,
                        std::vector<vmesh::VMCSubmesh>& gof,
                        V3cBitstream&                   syntax,
                        V3CParameterSet&                vps,
                        int32_t                         submeshIndex,
                        int32_t                         submeshCount,
                        const VMCEncoderParameters&     params);
  bool preprocessSubmesh(const VMCGroupOfFramesInfo&     gofInfo,
                         std::vector<vmesh::VMCSubmesh>& gof,
                         int32_t                         submeshIndex,
                         const VMCEncoderParameters&     params);
  bool createDisplacements(vmesh::VMCSubmesh&            submesh,
                           int32_t                       submeshIndex,
                           int32_t                       frameIndex,
                           const TriangleMesh<MeshType>& rec,
                           const VMCEncoderParameters&   params);
#ifdef COMPRESS_VIDEO_PAC_ENABLE
  bool
  compressVideoPac(std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
                   Sequence&                                    reconstruct,
                   int32_t                                      submeshCount,
                   int32_t                                      frameCount,
                   int32_t                                      frameOffset,
                   V3cBitstream&                                syntax,
                   V3CParameterSet&                             vps,
                   const VMCEncoderParameters&                  params);
#endif
  bool
  compressVideoGeo(std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
                   int32_t                                      frameCount,
                   V3cBitstream&                                syntax,
                   V3CParameterSet&                             vps,
                   const VMCEncoderParameters&                  params);

  bool compressAcGeo(std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
                     int32_t                                      frameCount,
                     V3cBitstream&                                syntax,
                     V3CParameterSet&                             vps,
                     const VMCEncoderParameters&                  params);

  bool
  reconstructPositions(std::vector<vmesh::VMCSubmesh>& encSubmeshes,  //[frame]
                       int32_t                         frameIndex,
                       int32_t                         tileIndex,
                       int32_t                         submeshIdx,
                       int32_t                         patchIndex,
                       V3cBitstream&                   syntax,
                       V3CParameterSet&                vps,
                       const VMCEncoderParameters&     params);
  bool compressVideoAttribute(std::vector<std::vector<vmesh::VMCSubmesh>>&
                                            encSubmeshes,  //[submesh][frame]
                              Sequence&     reconstruct,
                              int32_t       submeshCount,
                              int32_t       frameCount,
                              int32_t       frameOffset,
                              int32_t       attrIdx,
                              V3cBitstream& syntax,
                              V3CParameterSet&            vps,
                              const VMCEncoderParameters& params);
  bool compressVideoTexture(std::vector<std::vector<vmesh::VMCSubmesh>>&
                                             encSubmeshes,  //[submesh][frame]
                            Sequence&        reconstruct,
                            int32_t          submeshCount,
                            int32_t          frameCount,
                            int32_t          frameOffset,
                            int32_t          attrIdx,
                            V3cBitstream&    syntax,
                            V3CParameterSet& vps,
                            const VMCEncoderParameters& params);
  //  //==========================================================
  //  //v4.0_integration : 24588b402a506716c33488c08942fdb0c3f1c85f
  //  bool compressAcDisplacement(std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
  //                     int32_t                                      frameCount,
  //                     V3cBitstream&                                syntax,
  //                     V3CParameterSet&                             vps,
  //                     const VMCEncoderParameters&                  params);
  //==========================================================
  inline void setKeepFilesPathPrefix(const std::string& path) {
    _keepFilesPathPrefix = path;
  }
  void updateGofInfo(vmesh::Sequence& source, VMCGroupOfFramesInfo& gofInfo);

  void optimizeZipperingThreshold(
    const VMCEncoderParameters&                          params,
    const std::vector<std::vector<std::vector<int32_t>>> submeshLodMaps,
    const std::vector<std::vector<std::vector<int32_t>>>
      submeshChildToParentMaps);
  void zippering(
    int32_t                                              frameCount,
    int32_t                                              submeshCount,
    const VMCEncoderParameters&                          params,
    const std::vector<std::vector<std::vector<int32_t>>> submeshLodMaps,
    const std::string&                                   inputpath,
    int32_t                                              startFrame);
  void     addZipperingSEI(int32_t                     frameCount,
                           V3cBitstream&               syntax,
                           const VMCEncoderParameters& params);
  void     updateReconMesh(int32_t frameCount, int32_t submeshCount);
  void     addTileSubmeshMappingSEI(int32_t                     frameCount,
                                    V3cBitstream&               syntax,
                                    const VMCEncoderParameters& params);
  void     addAttributeTransformationParamsV3cSEI(V3cBitstream& syntax);
  void     addAttributeTransformationParamsBaseMeshSEI(V3cBitstream& syntax);
  uint32_t writeReconstructedMeshes(int32_t        frameCount,
                                    int32_t        submeshCount,
                                    const uint32_t bitDepthTexCoord,
                                    int32_t        frameOffset,
                                    const VMCEncoderParameters& params);
  void     adjustTextureCoordinates(int32_t                     frameCount,
                                    int32_t                     submeshCount,
                                    const VMCEncoderParameters& params);
  void     updateTextureSizeScaleToMaxArea(const VMCEncoderParameters& params);
  void     addLoDExtractioninformationSEI(int32_t                     frameCount,
                                          V3cBitstream&               syntax,
                                          const VMCEncoderParameters& params);
  bool     setMCTSVideoEncoderParams(const VMCEncoderParameters& params,
                                     VideoEncoderParameters& videoEncoderParams);
  void     createCodecComponentMappingSei(V3cBitstream&               syntax,
                                          V3CParameterSet&            vps,
                                          const VMCEncoderParameters& params);
  void
       addAttributeExtractionInformationSEI(int32_t                     frameCount,
                                            V3cBitstream&               syntax,
                                            const VMCEncoderParameters& params);
  bool setAttributeMCTSVideoEncoderParams(
    const VMCEncoderParameters& params,
    VideoEncoderParameters&     videoEncoderParams);
  vmesh::Checksum& getEncChecksum() { return encChecksum_; }

#if CONFORMANCE_LOG_ENC
  void setLogger(Logger& logger) {
    logger_ = &logger;
    TRACE_DESCRIPTION("**********BITSTREAM DESCRIPTION**********\n (add "
                      "description of conformance bitstream here)");
    TRACE_HLS("**********HIGH LEVEL SYNTAX**********\n");
    TRACE_ATLAS("**********ATLAS**********\n");
    TRACE_TILE("**********TILE**********\n");
    TRACE_MFRAME(
      "**********MESH FRAME (before post-reconstruction)**********\n");
    TRACE_RECFRAME("**********RECONSTRUCTED MESH FRAME**********\n");
    TRACE_PICTURE("**********VIDEO BITSTREAMS**********\n");
    TRACE_BASEMESH("**********BASEMESH BITSTREAM**********\n");
    TRACE_DISPLACEMENT("**********DISPLACEMENT BISTREAM**********\n");
  }
  // see A.2.3.2 in ISO/IEC 23090-36
  void getHighLevelSyntaxLog(V3cBitstream& syntax);
  // see A.2.3.3 in ISO/IEC 23090-36
  void getPictureLog();
  // see A.2.3.4 in ISO/IEC 23090-36
  void getBasemeshLog(int32_t frameCount);
  // see A.2.3.5 in ISO/IEC 23090-36
  void getACDisplacementLog(
    std::vector<std::vector<vmesh::VMCSubmesh>>& encSubmeshes,
    int32_t                                      frameCount,
    V3cBitstream&                                syntax,
    const VMCEncoderParameters&                  params);
  // see A.2.3.7 in ISO/IEC 23090-36
  void getAtlasLog(int32_t                     frameCount,
                   V3cBitstream&               syntax,
                   V3CParameterSet&            vps,
                   const VMCEncoderParameters& params);
  // see A.2.3.8 in ISO/IEC 23090-36
  void getTileLog(int32_t                     frameCount,
                  V3cBitstream&               syntax,
                  V3CParameterSet&            vps,
                  const VMCEncoderParameters& params);
  // see A.2.3.9 in ISO/IEC 23090-36
  void getMFrameLog(int32_t        frameCount,
                    int32_t        submeshCount,
                    const uint32_t bitDepthTexCoord);
  // see A.2.3.10 in ISO/IEC 23090-36
  void getRecFrameLog(int32_t                 frameIndex,
                      TriangleMesh<MeshType>& reconstructed);
  // auxiliary function to convert types
  VMCPatch convertVCMAtlasPatch(AspsVdmcExtension& asve,
                                AfpsVdmcExtension& afve,
                                AtlasTileHeader&   ath,
                                AtlasPatch&        var);
  Logger* logger_ = nullptr;
#endif

private:
  bool preprocessWholemesh(const Sequence&                 source,
                           const VMCGroupOfFramesInfo&     gofInfoSrc,
                           std::vector<vmesh::VMCSubmesh>& gof,
                           int32_t                         submeshIndex,
                           const VMCEncoderParameters&     params);

  void decimateInput(const TriangleMesh<MeshType>& input,
                     int32_t                       submeshIndex,
                     int32_t                       frameIndex,
                     VMCSubmesh&                   frame,
                     TriangleMesh<MeshType>&       decimate,
                     const VMCEncoderParameters&   params);

  void removeDegeneratedTrianglesCrossProduct(
    TriangleMesh<MeshType>& mesh,
    std::vector<int32_t>*   originalTriangleIndices,
    bool& flag,
    const int32_t& frameIndex);

  void textureParametrization(
    int32_t                                  submeshIndex,
    int32_t                                  frameIndex,
    TriangleMesh<MeshType>&                  decimate,
    TriangleMesh<MeshType>&                  decimateTexture,
    size_t                                   texParamWidth,
    size_t                                   texParamHeight,
    const VMCEncoderParameters&              params,
    std::vector<ConnectedComponent<double>>& currentPackedCCList,
    std::vector<ConnectedComponent<double>>& previousPackedCCList,
    std::vector<int>&                        catOrderGof,
    int&                                     maxOccupiedU,
    int&                                     maxOccupiedV,
    std::vector<int32_t>*                    originalTriangleIndices,
    bool&                                     flag,
    int32_t                                  frameIndexInAllGof);

  void geometryParametrization(std::vector<VMCSubmesh>&      gof,
                               VMCFrameInfo&                 frameInfo,
                               VMCSubmesh&                   frame,
                               int32_t                       submeshIndex,
                               int32_t                       frameIndex,
                               int32_t                       frameCount,
                               const TriangleMesh<MeshType>& input,
                               const VMCEncoderParameters&   params,
                               int32_t& lastIntraFrameIndex,
                               int32_t& gofStartFrameIndex);

  bool deformSubdiv(TriangleMesh<MeshType>&     base,
                    TriangleMesh<MeshType>&     subdiv,
                    int32_t                     submeshIndex,
                    int32_t                     frameIndex,
                    int32_t&                    subdivCountDiff,
                    const VMCEncoderParameters& params);

  void fixInstability(VMCSubmesh& frame, const VMCEncoderParameters& params);

  void updateTextureSizeRemoveNonOccupied(const VMCGroupOfFramesInfo& gofInfo,
                                          std::vector<vmesh::VMCSubmesh>& gof,
                                          V3cBitstream&               syntax,
                                          const VMCEncoderParameters& params,
                                          const int&     globalMaxOccupiedU,
                                          const int&     globalMaxOccupiedV,
                                          const int32_t& submeshIndex);

  void unifyVertices(const VMCGroupOfFramesInfo& gofInfo,
                     std::vector<VMCSubmesh>&    gof,
                     const VMCEncoderParameters& params);

  //==========================================================

  void updateTextureCoordinates(VMCSubmesh&                    frame,
                                TriangleMesh<MeshType>&        baseEnc,
                                const std::vector<int32_t>&    mapping,
                                const std::vector<VMCSubmesh>& gof,
                                const VMCEncoderParameters&    params);
  void updateRawUVs(VMCSubmesh&                    frame,
                    TriangleMesh<MeshType>&        rec,
                    TriangleMesh<MeshType>&        enc,
                    const std::vector<int32_t>&    mapping,
                    std::vector<int>&              trianglePartition,
                    const std::vector<VMCSubmesh>& gof,
                    const VMCEncoderParameters&    params);

  //==========================================================

  static bool computeDisplacements(VMCSubmesh&                   frame,
                                   int32_t                       submeshIndex,
                                   int32_t                       frameIndex,
                                   const TriangleMesh<MeshType>& rec,
                                   const VMCEncoderParameters&   params);
  static bool quantizeDisplacements(VMCSubmesh&                 frame,
                                    int32_t                     submeshIndex,
                                    const VMCEncoderParameters& params);
  static void fillDisZeros(VMCSubmesh&                 frame,
                           const VMCEncoderParameters& params);

  bool computeDisplacementVideoFramePerLod(std::vector<Vec3<MeshType>>& disp,
                                           std::vector<lodInfo>& lodVertexInfo,
                                           uint32_t              lodIndex,
                                           uint32_t              sizeX,
                                           uint32_t              sizeY,
                                           uint32_t              posX,
                                           uint32_t              posY,
                                           Frame<uint16_t>& dispVideoFrame,
                                           const VMCEncoderParameters& params);

  bool createDisplacementVideoFrame(std::vector<Vec3<MeshType>>& disp,
                                    std::vector<int32_t>& vertexCountPerLod,
                                    int32_t               frameIndex,
                                    int32_t               tileIndex,
                                    int32_t               patchIndex,
                                    int32_t               submeshId,
                                    uint32_t              sizeX,
                                    uint32_t              sizeY,
                                    uint32_t              posX,
                                    uint32_t              posY,
                                    Frame<uint16_t>&      dispVideoFrame,
                                    const VMCEncoderParameters& params);

  bool compressDisplacementsVideo(FrameSequence<uint16_t>&    dispVideo,
                                  FrameSequence<uint16_t>&    dispVideoRec,
                                  VideoBitstream&             videoStream,
                                  int32_t                     frameCount,
                                  const VMCEncoderParameters& params);
  bool compressTextureVideo(
    VideoBitstream&             videoStream,
    FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
    Sequence&                   reconstruct,
    int                         attrIdx,
    int32_t                     frameOffset,
    const VMCEncoderParameters& params);
  bool compressAttributeVideo(
    VideoBitstream&             videoStream,
    FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
    Sequence&                   reconstruct,
    int                         attrIdx,
    int32_t                     frameOffset,
    const VMCEncoderParameters& params);
  bool compressPackedVideo(
    VideoBitstream&             videoStream,
    FrameSequence<uint16_t>&    videoSrcSequence,  //= reconTextures16bits;
    FrameSequence<uint16_t>&    recPacked,
    Sequence&                   reconstruct,
    int32_t                     frameOffset,
    const VMCEncoderParameters& params);

  template<typename T>  //Frame<uint16_t>& image -> Frame<T>&image
  void convertTextureImage(Frame<T>&          image,
                           int32_t            frameIndex,
                           vmesh::ColourSpace inputColorFormat,
                           int32_t            inputBitdepth,
                           vmesh::ColourSpace outputColorFormat,
                           int32_t            outputBitdepth,
                           int32_t            textureVideoUpsampleFilter,
                           bool               textureVideoFullRange) {
    // convert BGR444 8bits to BGR444 10bits
    if (inputColorFormat != outputColorFormat) {  // convert BGR444 to YUV420
#if USE_HDRTOOLS
      auto convert = ColourConverter<uint16_t>::create(1);
      convert->initialize(params.textureVideoHDRToolEncConfig);
#else
      auto convert = ColourConverter<uint16_t>::create(0);
      auto mode =
        toString(inputColorFormat) + "_" + toString(outputColorFormat) + "_"
        + std::to_string(inputBitdepth) + "_" + std::to_string(outputBitdepth)
        + "_"
        //"YUV420p_BGR444p_10_8_"
        + std::to_string(textureVideoUpsampleFilter) + "_"
        + std::to_string(textureVideoFullRange);
      if (frameIndex == 0) std::cout << "convert mode: " << mode << "\n";
      convert->initialize(mode);
#endif
      convert->convert(image);
    }
  }

  void textureBackgroundFilling(Frame<uint16_t>& reconstructedTexture,
                                Plane<uint8_t>&  occupancy,
                                const VMCEncoderParameters& params);
  bool
  create3DMotionEstimationFiles(std::vector<Plane<uint8_t>>& occupancySequence,
                                const std::string&           path);

  void calculateMaxPixelCountForDisplacementFrame(
    std::vector<std::vector<VMCSubmesh>>& encFrames,
    const VMCEncoderParameters&           encParams,
    std::vector<int32_t>&                 maxPixelCount);

  void calculateMaxBlocksPerSubmeshPerTile(
    std::vector<std::vector<VMCSubmesh>>& encFrames,
    const VMCEncoderParameters&           encParams,
    std::vector<int32_t>&                 maxPixelCount,
    std::vector<std::vector<int32_t>>&    maxBlockWidth,
    std::vector<std::vector<int32_t>>&    maxBlockHeight);

  void setPatchAreaForAcDisplacement();
  void setPatchAreaForVideoDisplacement(
    std::vector<std::vector<VMCSubmesh>>& encFrames,
    const VMCEncoderParameters&           encParams);
  void
  setPatchAreaForDisplacements(std::vector<std::vector<VMCSubmesh>>& encFrames,
                               const VMCEncoderParameters& encParams);
  void placeTextures(int32_t                     attrIndex,
                     int32_t                     frameIndex,
                     int32_t                     tileIndex,
                     int32_t                     patchIndex,
                     Frame<uint8_t>&             textureSubmesh,
                     Frame<uint16_t>&            oneTrTexture,
                     Plane<uint8_t>&             occupancy,
                     Plane<uint8_t>&             oneOccupancy,
                     bool                        placeOccupancy,
                     const VMCEncoderParameters& encParams);

  void checkAttributeTilesConsistency(AfpsVdmcExtension&             afve,
                                      AtlasSequenceParameterSetRbsp& asps,
                                      int32_t refAttrIdx);

  static std::vector<std::pair<int32_t, int32_t>>
  findSameVertices(const vmesh::TriangleMesh<MeshType>& srcMesh,
                   const vmesh::TriangleMesh<MeshType>& targetMesh,
                   MeshType                             sqrDistEps = 1.0E-4);

  std::string          _keepFilesPathPrefix    = {};
  int32_t              _gofIndex               = 0;
  bool                 reuseDuplicatedVertFlag = true;
  std::vector<int32_t> numNeighborsMotion;
  std::vector<int32_t> umapping;

  std::vector<std::string> inputSubmeshPaths_;
  std::string              inputMeshPath_;
  std::vector<std::string> inputAttributesPaths_;

  std::vector<int32_t> submeshIdtoIndex_;  //orange -> 0
  std::vector<int32_t> tileIdtoIndex_;     //orange -> 0

  std::string              reconstructedMeshPath_;
  std::vector<std::string> reconstructedAttributesPaths_;
  std::string              reconstructedMaterialLibPath_;
  int32_t                  startFrame_;

  std::pair<int32_t, int32_t> geometryVideoSize =
    std::pair<int32_t, int32_t>(0, 0);  //width,height
  std::vector<std::pair<uint32_t, uint32_t>> attributeVideoSize_;
  //  std::pair<int32_t, int32_t> textureVideoSize = std::pair<int32_t, int32_t>(0, 0);  //width,height
  std::pair<int32_t, int32_t> texturePackingSize =
    std::pair<int32_t, int32_t>(0, 0);  //width,height

  std::vector<std::vector<AtlasTile>> tileAreasInVideo_;
  std::vector<std::vector<AtlasTile>> reconAtlasTiles_;  //[tile][frame]
  std::vector<std::vector<AtlasTile>> debugReconAtlasTilesUsingAtlasDecoder_;

  std::vector<std::vector<TriangleMesh<MeshType>>>
    baseMeshes_;  //[#frame][submesh]
  std::vector<std::vector<TriangleMesh<MeshType>>>
    reconBasemeshes_;  //[submesh][#frame]
  std::vector<std::vector<TriangleMesh<MeshType>>>
    reconSubdivmeshes_;  //[submesh][#frame]
  std::vector<std::vector<TriangleMesh<MeshType>>>
    reconMeshes_;  //[submesh][#frame]
  std::vector<std::vector<TriangleMesh<MeshType>>>
    zipperingMeshes_;  //[submesh][#frame]
  std::vector<std::vector<std::vector<int32_t>>> mspsRefDiffList_;

  std::vector<vmesh::ColourSpace> sourceAttributeColourSpaces_;
  std::vector<vmesh::ColourSpace> videoAttributeColourSpaces_;
  std::vector<uint8_t>            sourceAttributeBitdetphs_;
  std::vector<uint8_t>            videoAttributeBitdetphs_;

  //TransformParameters                            seqTransformParameter;
  vmesh::Checksum encChecksum_;

  //zippering
  size_t                           zipperingMatchMaxDistance_ = 0;
  std::vector<size_t>              zipperingMatchMaxDistancePerFrame_;
  std::vector<std::vector<size_t>> zipperingMatchMaxDistancePerSubmesh_;
  std::vector<std::vector<std::vector<int64_t>>> zipperingDistanceBorderPoint_;
  std::vector<std::vector<std::vector<Vec2<size_t>>>>
    zipperingMatchedBorderPoint_;
  std::vector<std::vector<std::vector<Vec2<int64_t>>>>
                            zipperingMatchedBorderPointDelta_;
  std::vector<Vec2<double>> zipperingMatchedBorderPointDeltaVariance_;
  std::vector<std::vector<std::vector<int64_t>>>
    zipperingMatchMaxDistancePerSubmeshPair_;
  std::vector<std::vector<std::vector<std::vector<size_t>>>> boundaryIndex_;
  std::vector<std::vector<size_t>>                           crackCount_;
  std::vector<bool>                                          zipperingSwitch_;

  MeshSequence<MeshType> wholeBaseMeshes_;
  MeshSequence<MeshType> wholeReconSubdivMeshes_;
  MeshSequence<MeshType> wholeSubdivMeshes_;
  // LoD-based extraction information SEI message
  std::vector<int> MCTSidx;    // MCTS index
  std::vector<int> subpicIdx;  // subpicture index
  // Attribute extraction information SEI message
  std::vector<int> attrMCTSidx;  // MCTS index of attribute video stream
  std::vector<int>
    attrSubpicIdx;  // subpicture index of attribute video stream

  AtlasEncoder                          atlasEncoder_;
  AtlasDataDecoder                      atlasDecoder_;
  basemesh::BaseMeshEncoder             baseMeshEncoder_;
  acdisplacement::AcDisplacementEncoder acDisplacementEncoder_;
};

}  // namespace vmesh
