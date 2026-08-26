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

//============================================================================

#include <program-options-lite/program_options_lite.h>
#include "encoder.hpp"
#include "metrics.hpp"

struct Parameters {
  std::string                 inputMeshPath = {};
  std::string                 temp          = {};
  std::vector<std::string>    inputAttributesPaths;
  std::string                 groupOfFramesStructurePath = {};
  bool                        useSubmeshFolder           = true;
  std::string                 compressedStreamPath       = {};
  std::string                 reconstructedMeshPath      = {};
  std::vector<std::string>    reconstructedAttributesPaths;
  std::string                 reconstructedMaterialLibPath = {};
  int32_t                     startFrame                   = 0;
  int32_t                     frameCount                   = 1;
  double                      framerate                    = 30.;
  bool                        verbose                      = false;
  bool                        checksum                     = true;
  int32_t                     gofParallelism               = 1;
  int32_t                     gofIndexOffset               = 0;
  int32_t                     gofWorkerTextureThreads      = 2;
  vmesh::VMCEncoderParameters encParams;
  vmesh::VMCMetricsParameters metParams;
};

//============================================================================

static bool
checkParameters(df::program_options_lite::ErrorReporter& err,
                Parameters&                              params) {
  bool returnValue = true;

  if (params.encParams.geometryVideoEncoderConfig.empty()
      && params.encParams.encodeDisplacementType == 2) {
    err.error() << "geometry video config not specified\n";
    returnValue = false;
  }

  for (int i = 0; i < params.encParams.videoAttributeCount; i++) {
    if (params.encParams.attributeParameters[i]
          .textureVideoEncoderConfig.empty()) {
      err.error() << i << "-th attribute video encoder config not specified\n";
      returnValue = false;
    }
  }

  if ((params.inputAttributesPaths.size()
       < params.encParams.videoAttributeCount)
      && params.encParams.numTextures == 1) {
    err.error() << " input attribute paths are not specified\n";
    returnValue = false;
  }

  if (params.compressedStreamPath.empty()) {
    err.error() << "compressed input/output not specified\n";
    returnValue = false;
  }

  if (params.inputMeshPath.empty()) {
    err.error() << "input mesh not specified\n";
    returnValue = false;
  }

  if (params.encParams.lodPatchesEnable
      && params.encParams.encodeDisplacementType == 1) {
    err.error() << "Lod patch packing is not specified for AC displacement\n";
    returnValue = false;
  }

  if (params.encParams.lodPatchesEnable
      && params.encParams.increaseTopSubmeshSubdivisionCount) {
    err.error() << "Lod patch packing is not implemented with "
                   "increaseTopSubmeshSubdivisionCount equal to True\n";
    returnValue = false;
  }

  if (params.encParams.lodPatchesEnable && params.encParams.jointTextDisp) {
    err.error() << "Lod patch packing is not implemented with jointTextDisp "
                   "equal to True\n";
    returnValue = false;
  }

  if (params.encParams.lodPatchesEnable && params.encParams.numSubmesh > 1) {
    err.error()
      << "Lod patch packing is implemented only for one submesh use case\n";
    returnValue = false;
  }

  if (params.encParams.lodPatchesEnable
      && params.encParams.numTilesGeometry > 1) {
    err.error()
      << "Lod patch packing is implemented only for one tile use case\n";
    returnValue = false;
  }

  if ((params.encParams.displacementVideoChromaFormat == 3
       && params.encParams.applyOneDimensionalDisplacement)) {
    err.error() << "applyOneDimensionalDisplacement must be 0 when "
                   "displacementVideoChromaFormat=3(YUV444p)\n";
    returnValue = false;
  }

  if (params.encParams.packingScaling >= 1.0) {
    err.error() << "Packing scaling adjustment should be less than 1.0\n";
    returnValue = false;
  }

  if (params.encParams.maxNumMotionVectorPredictor > 3) {
    err.error() << "maxNumMotionVectorPredictor should be less than 4\n";
    returnValue = false;
  }

  if (params.gofParallelism < 1 || params.gofIndexOffset < 0
      || params.gofWorkerTextureThreads < 1) {
    err.error() << "GOF parallelism and worker counts must be positive\n";
    returnValue = false;
  }

  for (const auto& attribute : params.encParams.attributeParameters) {
    if (attribute.textureVideoTileColumns < 1
        || attribute.textureVideoTileRows < 1
        || attribute.textureVideoSliceCount < 1) {
      err.error() << "texture tile and slice counts must be positive\n";
      returnValue = false;
    }
#if !defined(USE_X265_VIDEO_CODEC)
    if (attribute.textureVideoUseX265) {
      err.error() << "textureVideoUseX265 requires USE_X265_VIDEO_CODEC\n";
      returnValue = false;
    }
#endif
  }

  if (params.encParams.motionGroupSize != 1
      && params.encParams.motionGroupSize != 2
      && params.encParams.motionGroupSize != 4
      && params.encParams.motionGroupSize != 8
      && params.encParams.motionGroupSize != 16
      && params.encParams.motionGroupSize != 32
      && params.encParams.motionGroupSize != 64
      && params.encParams.motionGroupSize != 128) {
    err.error() << "motionGroupSize=" << params.encParams.motionGroupSize
                << " can't be specified\n";
    returnValue = false;
  }

  if (params.encParams.encodeTextureVideo
      && params.inputAttributesPaths[0].empty()
      && params.encParams.numTextures == 1
      && params.temp.empty()) {
    err.error() << "input texture not specified\n";
    returnValue = false;
  }

  if (params.encParams.jointTextDisp) {
    #ifdef COMPRESS_VIDEO_PAC_ENABLE
       if (params.encParams.encodeDisplacementType != 2) {
      err.error() << "jointTextDisp=1 is not supported when "
                         "encodeDisplacementType != 2\n";
      returnValue = false;
      
    }
    #else 
      err.error()
      << "jointTextDisp=1 is implemented only when "
            "COMPRESS_VIDEO_PAC_ENABLE is set at compile time\n";
    returnValue = false;
    #endif 
  }

  if (!params.encParams.encodeTextureVideo && params.encParams.useRawUV) {
    err.error()
      << "encodeTextureVideo=0 is not supported for useRawUV=1 because "
         "texture is used for pick up triangles for raw subpatch\n";
    returnValue = false;
  }

  return returnValue;
}

//============================================================================

static bool
parseParameters(int argc, char* argv[], Parameters& params) try {
  namespace po                        = df::program_options_lite;
  bool                 print_help     = false;
  auto&                encParams      = params.encParams;
  auto&                metParams      = params.metParams;
  auto&                intraGeoParams = params.encParams.intraGeoParams;
  auto&                interGeoParams = params.encParams.interGeoParams;
  std::vector<int32_t> liftingQP2;
  int                  subdiv_method  = 1;
  int                  subdiv_method0 = 1;
  int                  subdiv_method1 = 1;
  int                  subdiv_method2 = 1;
  bool                 lodASM         = false;

  // avoid potential issue with relocation on resize
  params.encParams.attributeParameters.reserve(256);

  /* clang-format off */
  po::Options opts;
  opts.addOptions()
  (po::Section("Common"))
    ("help",      print_help,          false, "This help text")
    ("config,c",  po::parseConfigFile, "Configuration file name")
    ("verbose,v", params.verbose,   false, "Verbose output")

  (po::Section("Input"))
    ("srcMesh",
      params.inputMeshPath,
      params.inputMeshPath,
      "Input mesh")
    ("srcTex",
     params.temp,
     {""},
      "Input texture (deprecated consider using srcAttrs instead)")
  ("srcAttrs",
   params.inputAttributesPaths,
   {""},
    "Input attributes path: a%d.png,b%d.png...")
  ("videoAttributeTypes",
    encParams.videoAttributeTypes,
   {0},
    "Input video attribute type: 0(text_coord), 0(text_coord)...")
  ("videoAttributeCount",
    encParams.videoAttributeCount,
    encParams.videoAttributeCount,
    "number of attributes to be coded by video codec")
  ("videoAttributeTypes",
    encParams.videoAttributeTypes,
   {0},
    "Input video attribute type: 0(text_coord), 0(text_coord)...")
    ("textureMapCount",
        encParams.numTextures,
        encParams.numTextures,
          "number of texture maps per mesh :default 1")
    ("positionBitDepth",
      encParams.bitDepthPosition,
      encParams.bitDepthPosition,
      "Input positions bit depth")
    ("texCoordBitDepth",
      encParams.bitDepthTexCoord,
      encParams.bitDepthTexCoord,
      "Input texture coordinates bit depth")
  ("referenceAttributeIdx",
    encParams.referenceAttributeIdx,
    0,
    "Index of the reference attribute for consistent tiling inference"
  )
  ("submeshCount",
   encParams.numSubmesh,
   encParams.numSubmesh,
    "number of submeshes :default 1")
  ("tileCountGeometry",
   encParams.numTilesGeometry,
   encParams.numTilesGeometry,
   "number of tiles with geometry information in the atlas data sub-bitstream")
  ("submeshSegmentationType",
   encParams.submeshSegmentationType,
   encParams.submeshSegmentationType,
    "Submesh Segmentation Method: default 0")

  ("enableSignalledIds",
    encParams.enableSignalledIds,
    encParams.enableSignalledIds,
    "enableSignalledIds")

    ("segmentThreshold",
      encParams.segmentThreshold,
      encParams.segmentThreshold,
      "Threshold used for triangle segmentation by area (when submeshCount:2)")
    ("allowSubmeshOverlap",
      encParams.allowSubmeshOverlap,
      encParams.allowSubmeshOverlap,
      "number of submeshes :default 1")
    ("segmentByBaseMesh",
      encParams.segmentByBaseMesh,
      encParams.segmentByBaseMesh,
      "segmentation by basemesh")
    ("zipperingMethod",
        encParams.zipperingMethod_,
        encParams.zipperingMethod_,
        "zippering method: -1: deactivated, 0/1: per sequence (fixed or max value), 2: per frame (max value), 3: per sub-mesh (max value), 4: per border value, 5: using indexes 7:boundary connect ")
    ("zipperingThrehsold",
        encParams.zipperingThreshold_,
        encParams.zipperingThreshold_,
        "zippering threhsold: default =0")
    ("zipperingMethodForUnmatchedLoDs",
        encParams.zipperingMethodForUnmatchedLoDs_,
        encParams.zipperingMethodForUnmatchedLoDs_,
        "zippering method for unmatched LoDs: 0: no correction, 1: remove vertices from unmatched LoDs, 2: add vertices in neighbouring submesh for unmatched LoDs")
    ("increaseTopSubmeshSubdivisionCount",
        encParams.increaseTopSubmeshSubdivisionCount,
        encParams.increaseTopSubmeshSubdivisionCount,
        "increase top submesh subdivision iteration count by 1 (default: off)")
    ("zipperingMatchedBorderPointDeltaVarianceThreshold",
      encParams.zipperingMatchedBorderPointDeltaVarianceThreshold,
      encParams.zipperingMatchedBorderPointDeltaVarianceThreshold,
      "Write deltas of zippering matched border points if their varience is under this value")
    ("linearSegmentation",
      encParams.linearSegmentation,
      encParams.linearSegmentation,
      "linear or mixed segmentation into submeshes : default 1 (linear)")
    ("TileSubmeshMappingSEI",
      encParams.tileSubmeshMappingSEIFlag_,
      encParams.tileSubmeshMappingSEIFlag_,
      "send the tile submesh mapping SEI (default = 0)")
    ("startFrameIndex",
      params.startFrame,
      params.startFrame,
      "First frame number")
    ("frameCount",
      params.frameCount,
      params.frameCount,
      "Number of frames")
    ("framerate",
      params.framerate,
      params.framerate,
      "Frame rate")
  (po::Section("Output"))
    ("compressed",
      params.compressedStreamPath,
      params.compressedStreamPath,
      "Compressed bitstream")
    ("recMesh",
      params.reconstructedMeshPath,
      params.reconstructedMeshPath,
      "Reconstructed mesh")
    ("recAttrs",
      params.reconstructedAttributesPaths,
     {""},
      "Reconstructed attribute paths")
    ("dequantizeUV",
      encParams.dequantizeUV,
      encParams.dequantizeUV,
      "Dequantize texture coordinates of the reconstructed meshes")
    ("recMat",
      params.reconstructedMaterialLibPath,
      params.reconstructedMaterialLibPath,
      "Reconstructed materials")
    ("reconstructNormals",
      encParams.reconstructNormals,
      encParams.reconstructNormals,
      "0:no Normals 1:local coordinate")
  (po::Section("ProfileCodecGroupId"))
    ( "profileCodecGroupIdc",
      encParams.profileCodecGroupId,
      encParams.profileCodecGroupId,
      "Profile Codec Group Idc" )
  (po::Section("General"))
    ("keep",
      encParams.keepIntermediateFiles,
      encParams.keepIntermediateFiles,
      "Keep intermediate files")
  ("keepBaseMesh",
   encParams.keepBaseMesh,
   encParams.keepBaseMesh,
    "Keep intermediate BaseMesh files")
  ("keepVideo",
   encParams.keepVideoFiles,
   encParams.keepVideoFiles,
    "Keep intermediate Video files")
  ("keepPreMesh",
   encParams.keepPreMesh,
   encParams.keepPreMesh,
    "Keep intermediate mesh files generated during the preprocessing")
  ("maxLayersMinus1",
   encParams.maxLayersMinus1,
   encParams.maxLayersMinus1,
    "Maximum layers for spatial scalability (default: 2)")
  ("deformForRecBase",
   encParams.deformForRecBase,
   encParams.deformForRecBase,
    "deform for rec base")
  ("useSubmeshFolder",
   params.useSubmeshFolder,
   params.useSubmeshFolder,
   "use folders to store submeshes")

    ("checksum",
      params.checksum,
      params.checksum,
      "Compute checksum")
	("lossless",
      encParams.setLosslessTools,
      encParams.setLosslessTools,
      "0:Lossy, 1:Lossless")

  (po::Section("Group of frames analysis"))
    ("gofMaxSize",
      encParams.groupOfFramesMaxSize,
      encParams.groupOfFramesMaxSize,
      "Maximum group of frames size")
    ("analyzeGof",
      encParams.analyzeGof,
      encParams.analyzeGof,
      "Analyze group of frames")
    ("gofParallelism",
      params.gofParallelism,
      params.gofParallelism,
      "Independent GOF encoder processes (1=serial)")
    ("gofIndexOffset",
      params.gofIndexOffset,
      params.gofIndexOffset,
      "Internal VPS/GOF index offset for parallel workers")
    ("gofWorkerTextureThreads",
      params.gofWorkerTextureThreads,
      params.gofWorkerTextureThreads,
      "Texture-transfer workers per parallel GOF process")

  (po::Section("Geometry decimate"))
    ("target",
      encParams.targetTriangleRatio,
      encParams.targetTriangleRatio,
      "Target triangle count ratio")
    ("minCCTriangleCount",
      encParams.minCCTriangleCount,
      encParams.minCCTriangleCount,
      "minimum triangle count per connected component")
    ("minPosition",
      encParams.minPosition,
      { 0.0, 0.0, 0.0 },
      "Min position")
    ("maxPosition",
      encParams.maxPosition,
      { 0.0, 0.0, 0.0 },
      "Max position")
  (po::Section("Texture parametrization"))
    ("textureParameterizationType",
      encParams.textureParameterizationType,
      encParams.textureParameterizationType,
      "Texture Parameterization Type of UVATLAS (0) or ORTHO (1) or NONE (-1)")
    ("orthoAtlasUseVertexCriteria",
      encParams.useVertexCriteria,
      encParams.useVertexCriteria,
      "Use Vertex Criteria (DEFAULT:1)")
    ("orthoAtlasUseSeedHistogram",
      encParams.bUseSeedHistogram,
      encParams.bUseSeedHistogram,
      "Determine CC seed using histogram (DEFAULT:1)")
    ("orthoAtlasStrongGradientThreshold",
      encParams.strongGradientThreshold,
      encParams.strongGradientThreshold,
      "Strong Gradient Threshold (DEFAULT:180)")
    ("orthoAtlasMaxCCAreaRatio",
      encParams.maxCCAreaRatio,
      encParams.maxCCAreaRatio,
      "Minimum Connected Components Area Ratio (DEFAULT:1)")
    ("orthoAtlasMaxNumFaces",
      encParams.maxNumFaces,
      encParams.maxNumFaces,
      "Max Num Faces (DEFAULT:inf)")
    ("orthoAtlasEnableFaceClusterMerge",
      encParams.bFaceClusterMerge,
      encParams.bFaceClusterMerge,
      "Face Cluster Merge (DEFAULT:0)")
    ("orthoAtlasLambdaRDMerge",
      encParams.lambdaRDMerge,
      encParams.lambdaRDMerge,
      "Lambda RD for merging (DEFAULT:1.0)")
    ("orthoAtlasCheck2DConnectivity",
      encParams.check2DConnectivity,
      encParams.check2DConnectivity,
      "Enforce 2D connectivity (DEFAULT:1)")
    ("orthoAtlasAdjustNormalDirection",
      encParams.adjustNormalDirection,
      encParams.adjustNormalDirection,
      "Adjust normal for projected patches (DEFAULT:0)")
    ("orthoAtlasApplyReprojection",
      encParams.applyReprojection,
      encParams.applyReprojection,
      "Reproject patches after checking overlapping (DEFAULT:0)")
    ("orthoAtlasMergeSmallCC",
      encParams.mergeSmallCC,
      encParams.mergeSmallCC,
      "Merge Small Connected Components (DEFAULT:0)")
    ("orthoAtlasSmallCCFaceNum",
      encParams.smallCCFaceNum,
      encParams.smallCCFaceNum,
      "Small CC face numbers (DEFAULT:1)")
    ("orthoAtlasEnablePatchScaling",
      encParams.bPatchScaling,
      encParams.bPatchScaling,
      "Enable patch scaling (DEFAULT:0)")
    ("orthoAtlasEnablePatchTemporalStabilization",
      encParams.bTemporalStabilization,
      encParams.bTemporalStabilization,
      "Enable patch temporal stabilization (DEFAULT:1)")
    ("orthoAtlasDeriveTextCoordFromPos",
      encParams.iDeriveTextCoordFromPos ,
      encParams.iDeriveTextCoordFromPos ,
      "Derivation of text. coords. from position: 0 (disabled), 1 (using UV coords), 2 (using FaceId, DEFAULT), 3 (using Connected Components)")
    ("orthoAtlasUse45DegreeProjection",
      encParams.use45DegreeProjection,
      encParams.use45DegreeProjection,
      "Use 45 degree projection (DEFAULT:1)")
    ("orthoAtlasPackingScaling",
      encParams.packingScaling,
      encParams.packingScaling,
      "Packing scaling adjustment (DEFAULT:0.9)")
    ("orthoAtlasPackSmallPatchesOnTop",
      encParams.packSmallPatchesOnTop,
      encParams.packSmallPatchesOnTop,
      "Pack small patches on top (DEFAULT:1)")
    ("orthoAtlasPackingType",
      encParams.packingType,
      encParams.packingType,
      "Default Packing (0, default), Tetris Packing (1), Projection Packing (2), Projection Packing with Temporal Stabilization (3)")
    ("orthoAtlasUpdateTextureSize",
      encParams.updateTextureSize,
      encParams.updateTextureSize,
      "Update texture size from textureVideoWidth/textureVideoHeight for orthoAtlasPackingType:3")
      ("orthoAtlasBiasEnable",
          encParams.biasEnableFlag,
          encParams.biasEnableFlag,
          "Enable the transmission of sub-patch bias synatx elemnts DEFAULT:0")
#if ORTHOATLAS_ADJ
    ("orthoAtlasParameterAdjustment",
      encParams.doParameterAdjustment,
      encParams.doParameterAdjustment,
      "Update subpatch position and size:1")
    ("orthoAtlasLog2AnalysisBlockSize",
      encParams.log2AnalysisBlockSize,
      encParams.log2AnalysisBlockSize,
      "Log 2 of the size of the block used for parameter adjustment:0")
#endif
    ("orthoFaceIdPresentFlag",
      encParams.bFaceIdPresentFlag,
      encParams.bFaceIdPresentFlag,
      "Use face ID (DEFAULT:false)")
    ("useRawUV",
      encParams.useRawUV,
      encParams.useRawUV,
      "Use RAW subpatch")
    ("targetCountThreshold",
      encParams.targetCountThreshold,
      encParams.targetCountThreshold,
      "targetCountThreshold for RAW subpatch")
    ("rawTextcoordBitdepth",
      encParams.rawTextcoordBitdepth,
      encParams.rawTextcoordBitdepth,
     "Bitdepth for texture coordinates in raw subpatch")
    ("textureParametrizationQuality",
      encParams.uvOptions,
      encParams.uvOptions,
      "Quality level of DEFAULT, FAST or QUALITY")
    ("textureParametrizationMaxCharts",
      encParams.maxCharts,
      encParams.maxCharts,
      "Maximum number of charts to generate")
    ("textureParametrizationMaxStretch",
      encParams.maxStretch,
      encParams.maxStretch,
      "Maximum amount of stretch 0 to 1")
    ("textureParametrizationGutter",
      encParams.gutter,
      encParams.gutter,
      "Gutter width betwen charts in texels")
    ("textureParametrizationWidth",
      encParams.texParamWidth,
      encParams.texParamWidth,
      "texture width")
    ("textureParametrizationHeight",
      encParams.texParamHeight,
      encParams.texParamHeight,
      "texture height")
    ("textureBGR444",
      encParams.attributeParameters[0].textureBGR444,
      encParams.attributeParameters[0].textureBGR444,
      "Texture video encoded in BGR444")
  ("texturePlacementPerPatch",
    encParams.attributeParameters[0].texturePlacementPerPatch,
    encParams.attributeParameters[0].texturePlacementPerPatch,
    "Texture of a submesh corresponding to a patch is placed within the area indicated by the patch")

  (po::Section("Geometry parametrization"))
      ("baseIsSrc",
      encParams.baseIsSrc,
      encParams.baseIsSrc,
      "Base models are src models")
    ("subdivIsBase",
      encParams.subdivIsBase,
      encParams.subdivIsBase,
      "Subdiv models are src models")
    ("subdivInter",
      encParams.subdivInter,
      encParams.subdivInter,
      "Subdiv inter")
    ("subdivInterWithMapping",
      encParams.subdivInterWithMapping,
      encParams.subdivInterWithMapping,
      "Subdiv inter with mapping")
    ("encoderOptiFlag",
        encParams.encoderOptiFlag,
        encParams.encoderOptiFlag,
        "0:Turn off the geometric optimization algorithm of the deformed mesh at the encoding end; 1: Turn on the geometric optimization algorithm of the deformed mesh at the encoding end")
    ("maxAllowedD2PSNRLoss",
      encParams.maxAllowedD2PSNRLoss,
      encParams.maxAllowedD2PSNRLoss,
      "Maximum allowed D2 PSNR Loss")
    ("normalCalcModificationEnable",
      encParams.normalCalcModificationEnable,
      encParams.normalCalcModificationEnable,
      "0: Calculate normal of cloudB from cloudA, 1: Use normal of cloudB(default)")
    ("log10GeometryParametrizationQuantizationValue",
      encParams.log10GeometryParametrizationQuantizationValue,
      encParams.log10GeometryParametrizationQuantizationValue,
      "Quantization value of the input of Geometry Parametrization expressed in log 10")
    ("trackedMode",
      encParams.TrackedMode,
      encParams.TrackedMode,
      "Tracked mode")

  (po::Section("Intra geometry parametrization"))
    ("ai_sdeform",
      intraGeoParams.applySmoothingDeform,
      intraGeoParams.applySmoothingDeform,
      "Apply deformation refinement stage")
    ("ai_subdivIt",
      intraGeoParams.geometryParametrizationSubdivisionIterationCount,
      intraGeoParams.geometryParametrizationSubdivisionIterationCount,
      "Subdivision iteration count")
    ("ai_forceNormalDisp",
      intraGeoParams.initialDeformForceNormalDisplacement,
      intraGeoParams.initialDeformForceNormalDisplacement,
      "Force displacements to aligned with the surface normals")
    ("ai_unifyVertices",
      intraGeoParams.applyVertexUnification,
      intraGeoParams.applyVertexUnification,
      "Unify duplicated vertices")
    ("ai_deformNNCount",
      intraGeoParams.initialDeformNNCount,
      intraGeoParams.initialDeformNNCount,
      "Number of nearest neighbours used during the initial deformation stage")
    ("ai_deformNormalThres",
      intraGeoParams.initialDeformNormalDeviationThreshold,
      intraGeoParams.initialDeformNormalDeviationThreshold,
      "Maximum allowed normal deviation during the initial deformation stage")
    ("ai_sampIt",
      intraGeoParams.geometrySamplingSubdivisionIterationCount,
      intraGeoParams.geometrySamplingSubdivisionIterationCount,
      "Number of subdivision iterations used for geometry sampling")
    ("ai_fitIt",
      intraGeoParams.geometryFittingIterationCount,
      intraGeoParams.geometryFittingIterationCount,
      "Number of iterations used during the deformation refinement stage")
    ("ai_smoothCoeff",
      intraGeoParams.geometrySmoothingCoeffcient,
      intraGeoParams.geometrySmoothingCoeffcient,
      "Initial smoothing coefficient used to smooth the deformed mesh "
      "during deformation refinement")
    ("ai_smoothDecay",
      intraGeoParams.geometrySmoothingCoeffcientDecayRatio,
      intraGeoParams.geometrySmoothingCoeffcientDecayRatio,
      "Decay factor applied to intial smoothing coefficient after every "
      "iteration of deformation refinement")
    ("ai_smoothMissedCoeff",
      intraGeoParams.geometryMissedVerticesSmoothingCoeffcient,
      intraGeoParams.geometryMissedVerticesSmoothingCoeffcient,
      "Smoothing coefficient applied to the missed vertices")
    ("ai_smoothMissedIt",
      intraGeoParams.geometryMissedVerticesSmoothingIterationCount,
      intraGeoParams.geometryMissedVerticesSmoothingIterationCount,
      "Number of iterations when smoothing the positions of the missed vertices")
    ("ai_smoothMethod",
      intraGeoParams.smoothDeformSmoothingMethod,
      intraGeoParams.smoothDeformSmoothingMethod,
      "Smoothing method to be applied when smoothing the deformed mesh during"
      "the deformation refinement stage")
    ("ai_deformUpdateNormals",
      intraGeoParams.smoothDeformUpdateNormals,
      intraGeoParams.smoothDeformUpdateNormals,
      "Recompute normals after each iteration of deformation refinement")
    ("ai_deformFlipThres",
      intraGeoParams.smoothDeformTriangleNormalFlipThreshold,
      intraGeoParams.smoothDeformTriangleNormalFlipThreshold,
      "Threshold to detect triangle normals flip")
    ("ai_useInitialGeom",
      intraGeoParams.smoothingDeformUseInitialGeometry,
      intraGeoParams.smoothingDeformUseInitialGeometry,
      "Use the initial geometry during the the deformation refinement stage")
    ("ai_fitSubdiv",
      intraGeoParams.fitSubdivisionSurface,
      intraGeoParams.fitSubdivisionSurface,
      "Update the positions of the decimated mesh to minimize displacements "
      "between the subdivided mesh and the deformed mesh")
    ("ai_smoothMotion",
      intraGeoParams.smoothingDeformSmoothMotion,
      intraGeoParams.smoothingDeformSmoothMotion,
      "Apply smoothing to motion instead of vertex positions")

  (po::Section("Inter geometry parametrization"))
    ("ld_sdeform",
      interGeoParams.applySmoothingDeform,
      interGeoParams.applySmoothingDeform,
      "Apply deformation refinement stage")
    ("ld_subdivIt",
      interGeoParams.geometryParametrizationSubdivisionIterationCount,
      interGeoParams.geometryParametrizationSubdivisionIterationCount,
      "Subdivision iteration count")
    ("ld_forceNormalDisp",
      interGeoParams.initialDeformForceNormalDisplacement,
      interGeoParams.initialDeformForceNormalDisplacement,
      "Force displacements to aligned with the surface normals")
    ("ld_unifyVertices",
      interGeoParams.applyVertexUnification,
      interGeoParams.applyVertexUnification,
      "Unify duplicated vertices")
    ("ld_deformNNCount",
      interGeoParams.initialDeformNNCount,
      interGeoParams.initialDeformNNCount,
      "Number of nearest neighbours used during the initial deformation stage")
    ("ld_deformNormalThres",
      interGeoParams.initialDeformNormalDeviationThreshold,
      interGeoParams.initialDeformNormalDeviationThreshold,
      "Maximum allowed normal deviation during the initial deformation stage")
    ("ld_sampIt",
      interGeoParams.geometrySamplingSubdivisionIterationCount,
      interGeoParams.geometrySamplingSubdivisionIterationCount,
      "Number of subdivision iterations used for geometry sampling")
    ("ld_fitIt",
      interGeoParams.geometryFittingIterationCount,
      interGeoParams.geometryFittingIterationCount,
      "Number of iterations used during the deformation refinement stage")
    ("ld_smoothCoeff",
      interGeoParams.geometrySmoothingCoeffcient,
      interGeoParams.geometrySmoothingCoeffcient,
      "Initial smoothing coefficient used to smooth the deformed mesh "
      "during deformation refinement")
    ("ld_smoothDecay",
      interGeoParams.geometrySmoothingCoeffcientDecayRatio,
      interGeoParams.geometrySmoothingCoeffcientDecayRatio,
      "Decay factor applied to intial smoothing coefficient after every "
      "iteration of deformation refinement")
    ("ld_smoothMissedCoeff",
      interGeoParams.geometryMissedVerticesSmoothingCoeffcient,
      interGeoParams.geometryMissedVerticesSmoothingCoeffcient,
      "Smoothing coefficient applied to the missed vertices")
    ("ld_smoothMissedIt",
      interGeoParams.geometryMissedVerticesSmoothingIterationCount,
      interGeoParams.geometryMissedVerticesSmoothingIterationCount,
      "Number of iterations when smoothing the positions of the missed vertices")
    ("ld_smoothMethod",
      interGeoParams.smoothDeformSmoothingMethod,
      interGeoParams.smoothDeformSmoothingMethod,
      "Smoothing method to be applied when smoothing the deformed mesh during"
      "the deformation refinement stage")
    ("ld_deformUpdateNormals",
      interGeoParams.smoothDeformUpdateNormals,
      interGeoParams.smoothDeformUpdateNormals,
      "Recompute normals after each iteration of deformation refinement")
    ("ld_deformFlipThres",
      interGeoParams.smoothDeformTriangleNormalFlipThreshold,
      interGeoParams.smoothDeformTriangleNormalFlipThreshold,
      "Threshold to detect triangle normals flip")
    ("ld_useInitialGeom",
      interGeoParams.smoothingDeformUseInitialGeometry,
      interGeoParams.smoothingDeformUseInitialGeometry,
      "Use the initial geometry during the the deformation refinement stage")
    ("ld_fitSubdiv",
      interGeoParams.fitSubdivisionSurface,
      interGeoParams.fitSubdivisionSurface,
      "Update the positions of the decimated mesh to minimize displacements "
      "between the subdivided mesh and the deformed mesh")
    ("ld_smoothMotion",
      interGeoParams.smoothingDeformSmoothMotion,
      interGeoParams.smoothingDeformSmoothMotion,
      "Apply smoothing to motion instead of vertex positions")

    (po::Section("Subdivision"))
    ("subdivisionIterationCount",
      encParams.subdivisionIterationCount,
      encParams.subdivisionIterationCount,
      "Subdivision iteration count")
    ("interpolateSubdividedNormalsFlag", //m68374
      encParams.interpolateSubdividedNormalsFlag,
      encParams.interpolateSubdividedNormalsFlag,
      "Interpolate subdivided normals on edges if true")
    ("subdivisionEdgeLength",
      encParams.floatSubdivisionEdgeLengthThreshold,
      encParams.floatSubdivisionEdgeLengthThreshold,
      "Subdivision edge length threshold")

    (po::Section("Quantization"))
    ("log2LevelOfDetailInverseScale",
      encParams.log2LevelOfDetailInverseScale,
       {1,1,1},
      "Quantization LoD inverse scale for displacements")
    ("liftingQP",
      encParams.liftingQP,
       {16, 28, 28},
      "Quantization parameter for displacements")
    ("adaptiveScale",
      encParams.adaptiveScale,
      { 110, 110, 110 },
      "Scale factor for displacements")
    ("liftingBias",
      encParams.liftingBias,
      {1./3., 1./3., 1./3},
      "Quantization bias for displacements")
    ("lodDisplacementQuantizationFlag",
      encParams.lodDisplacementQuantizationFlag,
      encParams.lodDisplacementQuantizationFlag,
      "Use quantization parameter per LoD for displacements")
    ("liftingQP2",
      liftingQP2,
      {16, 28, 28, 22, 34, 34, 28, 40, 40},
      "Quantization parameter for displacements")
    ("lodAdaptiveSubdivisionFlag,l",
        lodASM,
        false,
        "toggle lodAdaptiveSubdivision (Default: false)")
    ("sm",
        subdiv_method,
        0,
        "Subdivision method for all iterations (0: MIDPOINT (default), 1: LOOP , 2: NORMAL, 3: PYTHAG)")
    ("sml0",
        subdiv_method0,
        0,
        "Subdivision method for the 1st iteration (0: MIDPOINT (default), 1: LOOP , 2: NORMAL, 3: PYTHAG)")
    ("sml1",
        subdiv_method1,
        0,
        "Subdivision method for the 2nd iteration (0: MIDPOINT (default), 1: LOOP , 2: NORMAL, 3: PYTHAG)")
    ("sml2",
        subdiv_method2,
        0,
        "Subdivision method for the 3rd iteration (0: MIDPOINT (default), 1: LOOP , 2: NORMAL, 3: PYTHAG)")
    ("bitDepthOffset",
      encParams.bitDepthOffset,
        0,
      "Quantization parameter for displacements")
    (po::Section("Lifting"))
     ("transformMethod",
      encParams.transformMethod,
      encParams.transformMethod,
      "Selecting the transform method")
    ("liftingAdaptivePredictionWeightFlag",
      encParams.liftingAdaptivePredictionWeightFlag,
      encParams.liftingAdaptivePredictionWeightFlag,
      "Enable Adaptive Lifting Prediction Weight")
    ("liftingAdaptiveUpdateWeightFlag",
      encParams.liftingAdaptiveUpdateWeightFlag,
      encParams.liftingAdaptiveUpdateWeightFlag,
      "Enable signalling of lifting update parameters per LoD")
    ("liftingValenceUpdateWeightFlag",
      encParams.liftingValenceUpdateWeightFlag,
      encParams.liftingValenceUpdateWeightFlag,
      "Enable Valence Lifting Update Weight")
    ("liftingAdaptiveUpdateWeightNumerator",
      encParams.liftingUpdateWeightNumerator,
      {63,21,7,1},
      "The adaptive lifting update weight numerators")
    ("liftingAdaptiveUpdateWeightDenominatorMinus1",
      encParams.liftingUpdateWeightDenominatorMinus1,
      { 79,19,4,0 },
      "The adaptive lifting update weight Denominators")
    ("liftingAdaptivePredictionWeightNumerator",
      encParams.liftingPredictionWeightNumerator,
      {1,1,1,1},
      "The adaptive lifting prediction weight numerators")
    ("liftingAdaptivePredictionWeightDenominatorMinus1",
      encParams.liftingPredictionWeightDenominatorMinus1,
      {3,1,1,1},
      "The adaptive lifting prediction weight Denominators")
    ("InverseQuantizationOffsetFlag",
      encParams.InverseQuantizationOffsetFlag,
      encParams.InverseQuantizationOffsetFlag,
      "Inverse quantization offset enabling flag")
	("dirliftScale1",
      encParams.dirliftScale1,
      {950},
      "Directional lifting scale 1")
    ("dirliftDeltaScale2",
      encParams.dirliftDeltaScale2,
      {25},
      "Directional lifting scale2-scale1")
    ("dirliftDeltaScale3",
      encParams.dirliftDeltaScale3,
      {20},
      "Directional lifting scale3-scale2")
    ("dirliftScaleDeno",
      encParams.dirliftScaleDenoMinus1,
      {999},
      "Directional lifting scale Deno")
      ("dirlift",
      encParams.dirlift,
      false,
      "Enable directional lifting")

  (po::Section("Base mesh"))
    ("baseMeshPositionBitDepth",
      encParams.qpPosition,
      encParams.qpPosition,
      "Quantization bits for base mesh positions")
    ("baseMeshTexCoordBitDepth",
      encParams.qpTexCoord,
      encParams.qpTexCoord,
      "Quantization bits for base mesh texture coordinates")
    ("invertOrientation",
      encParams.invertOrientation,
      encParams.invertOrientation,
      "Invert triangles orientation")
    ("unifyVertices",
      encParams.unifyVertices,
      encParams.unifyVertices,
      "Unify duplicated vertices")
    ("reverseUnification",
      encParams.reverseUnification,
      encParams.reverseUnification,
      "Reverse the unified vertices")
    ("meshCodecId",
      encParams.meshCodecId,
      encParams.meshCodecId,
      "Mesh codec id")
    ("baseMeshVertexTraversal",
      encParams.baseMeshVertexTraversal,
      encParams.baseMeshVertexTraversal,
      "EB vertex traversal method:\n"
        "  eb:     use edge breaker traversal for vertex traversal during predictions\n"
        "  degree: use vertex degree traversal during predictions")
    ("baseMeshDeduplication",
        encParams.baseMeshDeduplication,
        encParams.baseMeshDeduplication,
        "base mesh deduplicate positions and attributes")
    ("entropyPacket",
        encParams.entropyPacket,
        encParams.entropyPacket,
       "entropy packet")
    ("dracoUsePosition",
      encParams.dracoUsePosition,
      encParams.dracoUsePosition,
      "Draco use position")
    ("dracoUseUV",
      encParams.dracoUseUV,
      encParams.dracoUseUV,
      "Draco use UV")
    ("dracoMeshLossless",
      encParams.dracoMeshLossless,
      encParams.dracoMeshLossless,
      "draco mesh lossless")
    ("profileGeometryCodec",
      encParams.profileGeometryCodec,
      encParams.profileGeometryCodec,
      "activate base mesh codec profiling (DEFAULT:0),\n"
      "  set to n > 0 to activate and\n"
      "  perform n runs of the codec for precise timings")
  (po::Section("Motion"))
    ("motionGroupSize",
      encParams.motionGroupSize,
      encParams.motionGroupSize,
      "Motion field coding vertices group size,\n"
        "  1 | 2 | 4 | 8 | 16 | 32 | 64 | 128")
    ("motionWithoutDuplicatedVertices",
      encParams.motionWithoutDuplicatedVertices,
      encParams.motionWithoutDuplicatedVertices,
      "Motion field coding by integrating duplicated vertices in reference frames")
    ("motionVertexTraversal",
        encParams.motionVertexTraversal,
        encParams.motionVertexTraversal,
        "Vertex traversal method for motion predictions:\n"
        "  default: use base mesh default order for vertex traversal during predictions\n"
        "  degree:  use vertex degree traversal during predictions")

  (po::Section("Geometry video"))
    ("geometryVideoCodecId",
      encParams.geometryVideoEncoderId,
      encParams.geometryVideoEncoderId,
      "Geometry video codec id")
    ("geometryVideoEncoderConfig",
      encParams.geometryVideoEncoderConfig,
      encParams.geometryVideoEncoderConfig,
      "Geometry video config file")
    ("lodPatchesEnable",
  encParams.lodPatchesEnable,
  encParams.lodPatchesEnable,
  "Displacements packing in video mode:\n"
  "  0: all LoD displacement in one patch (asve_lod_patches_enable_flag == 0) \n"
  "  1: LoD displacement per patch (asve_lod_patches_enable_flag == 1)")

  ("geometryVideoBitDepth",
    encParams.geometryVideoBitDepth,
    encParams.geometryVideoBitDepth,
    "Geometry video bitdepth")
  ("displacementVideoChromaFormat",
    encParams.displacementVideoChromaFormat,
    encParams.displacementVideoChromaFormat,
    "Chroma Format of displacement : 0.400YUVp 1.420YUVp 2.422YUVp 3.444YUVp")
  ("log2GeometryVideoBlockSize",
    encParams.log2GeometryVideoBlockSize,
    encParams.log2GeometryVideoBlockSize,
    "log2 of the blocksize(width=height) for the geometry video")

  (po::Section("Displacements"))
    ("encodeDisplacements",
      encParams.encodeDisplacementType,
      encParams.encodeDisplacementType,
      "Displacements coding mode:\n"
      "  0: no displacements coding\n"
      "  1: arithmetic coding\n"
      "  2: video coding")
    ("applyOneDimensionalDisplacement",
      encParams.applyOneDimensionalDisplacement,
      encParams.applyOneDimensionalDisplacement,
      "Apply one dimensional displacement")
    ("applyLiftingOffset",
      encParams.applyLiftingOffset,
      encParams.applyLiftingOffset,
      "Apply lifting offset")
    ("displacementFillZerosThreshold",
        encParams.displacementFillZerosThreshold,
        encParams.displacementFillZerosThreshold,
        "Displacement fill zeros threshold")
    ("displacementFillZerosFlag",
        encParams.displacementFillZerosFlag,
        encParams.displacementFillZerosFlag,
        "Displacement fill zeros flag")
    ("displacementReversePacking",
      encParams.displacementReversePacking,
      encParams.displacementReversePacking,
      "Displacement reverse packing")
    ("displacementQuantizationType",
      encParams.displacementQuantizationType,
      encParams.displacementQuantizationType,
      "Displacement packing type: 0-DEFAULT, 1-ADAPTIVE")
    ("subBlocksPerLoD",
      encParams.subBlocksPerLoD,
        { 1, 1, 1, 1 },
      "number of subblocks per level of detail for arithmetic coding")
    ("LoDSubdiv",
      encParams.blockSubdiv,
      { false, false, true, true },
      "Specifies LoD subdivision to blocks")
  (po::Section("Transfer texture"))
    ("textureTransferEnable",
      encParams.attributeParameters[0].textureTransferEnable,
      encParams.attributeParameters[0].textureTransferEnable,
      "Texture transfer enable")
    ("textureTransferSamplingSubdivisionIterationCount",
      encParams.textureTransferSamplingSubdivisionIterationCount,
      encParams.textureTransferSamplingSubdivisionIterationCount,
      "Texture transfer sampling subdivision iteration count")
    ("textureTransferThreadCount",
      encParams.textureTransferThreadCount,
      encParams.textureTransferThreadCount,
      "Preferred-UV texture transfer worker count: 0=auto, 1=serial")
    ("textureTransferPaddingBoundaryIterationCount",
      encParams.textureTransferPaddingBoundaryIterationCount,
      encParams.textureTransferPaddingBoundaryIterationCount,
      "Texture transfer padding boundary iteration count")
    ("textureTransferPaddingDilateIterationCount",
      encParams.textureTransferPaddingDilateIterationCount,
      encParams.textureTransferPaddingDilateIterationCount,
      "Texture transfer padding dilate iteration count")
    ("textureTransferPaddingMethod",
      encParams.textureTransferPaddingMethod,
      encParams.textureTransferPaddingMethod,
      "Texture transfer padding method: ")
    ("textureTransferPaddingSparseLinearThreshold",
      encParams.textureTransferPaddingSparseLinearThreshold,
      encParams.textureTransferPaddingSparseLinearThreshold,
      "Texture transfer padding sparse linear threshold")
    ("textureTransferBasedPointcloud",
      encParams.textureTransferBasedPointcloud,
      encParams.textureTransferBasedPointcloud,
      "Texture transfer padding sparse linear threshold")
    ("textureTransferPreferUV",
      encParams.textureTransferPreferUV,
      encParams.textureTransferPreferUV,
      "Texture transfer prefer UV")
    ("textureTransferWithMap",
      encParams.textureTransferWithMap,
      encParams.textureTransferWithMap,
      "Texture transfer with map for reconstructe sampling" )
    ("textureTransferWithMapSource",
      encParams.textureTransferWithMapSource,
      encParams.textureTransferWithMapSource,
      "Texture transfer with map for source sampling" )
    ("textureTransferMapSamplingParam",
      encParams.textureTransferMapSamplingParam,
      encParams.textureTransferMapSamplingParam,
      "Texture transfer map sampling param")
    ("textureTransferMethod",
      encParams.textureTransferMethod,
      encParams.textureTransferMethod,
      "Texture transfer method: 0: pcc 1: simple 2: simple new, 3: optimized")
    ("textureTransferGridSize",
      encParams.textureTransferGridSize,
      encParams.textureTransferGridSize,
      "textureTransferGridSize")
    ("textureTransferMapProjDim",
      encParams.textureTransferMapProjDim,
      encParams.textureTransferMapProjDim,
      "textureTransferMapProjDim")
    ("textureTransferSigma",
      encParams.textureTransferSigma,
      encParams.textureTransferSigma,
      "textureTransferSigma")
    ("textureTransferMapNumPoints",
      encParams.textureTransferMapNumPoints,
      encParams.textureTransferMapNumPoints,
      "textureTransferMapNumPoints")
    ("textureTransferCopyBackground",
      encParams.textureTransferCopyBackground,
      encParams.textureTransferCopyBackground,
      "textureTransferMapNumPoints")
  (po::Section("Motion coding"))
    ("maxNumNeighborsMotion",
      encParams.maxNumNeighborsMotion,
      encParams.maxNumNeighborsMotion,
      "Max number of vertex neighbors in motion coding")
    ("maxNumMotionVectorPredictor",
      encParams.maxNumMotionVectorPredictor,
      encParams.maxNumMotionVectorPredictor,
      "Max number of motion vector predictor in motion coding")
  (po::Section("Texture video"))
    ("encodeTextureVideo",
      encParams.encodeTextureVideo,
      encParams.encodeTextureVideo,
      "Encode texture video")
    ("textureVideoCodecId",
      encParams.attributeParameters[0].textureVideoEncoderId,
      encParams.attributeParameters[0].textureVideoEncoderId,
      "Texture video codec id")
    ("textureVideoUseX265",
      encParams.attributeParameters[0].textureVideoUseX265,
      encParams.attributeParameters[0].textureVideoUseX265,
      "Use the x265 Main10 encoder for HEVC texture video")
    ("textureVideoAllIntra",
      encParams.attributeParameters[0].textureVideoAllIntra,
      encParams.attributeParameters[0].textureVideoAllIntra,
      "Encode every texture picture as an intra picture")
    ("textureVideoWavefront",
      encParams.attributeParameters[0].textureVideoWavefront,
      encParams.attributeParameters[0].textureVideoWavefront,
      "Enable x265 wavefront row parallelism")
    ("textureVideoFrameThreads",
      encParams.attributeParameters[0].textureVideoFrameThreads,
      encParams.attributeParameters[0].textureVideoFrameThreads,
      "x265 frame threads (0=automatic)")
    ("textureVideoPoolThreads",
      encParams.attributeParameters[0].textureVideoPoolThreads,
      encParams.attributeParameters[0].textureVideoPoolThreads,
      "x265 worker-pool threads (0=automatic)")
    ("textureVideoTileColumns",
      encParams.attributeParameters[0].textureVideoTileColumns,
      encParams.attributeParameters[0].textureVideoTileColumns,
      "Uniform HEVC tile columns for HM (x265 does not implement tiles)")
    ("textureVideoTileRows",
      encParams.attributeParameters[0].textureVideoTileRows,
      encParams.attributeParameters[0].textureVideoTileRows,
      "Uniform HEVC tile rows for HM (x265 does not implement tiles)")
    ("textureVideoSliceCount",
      encParams.attributeParameters[0].textureVideoSliceCount,
      encParams.attributeParameters[0].textureVideoSliceCount,
      "Independent x265 slices per picture")
    ("textureVideoX265Preset",
      encParams.attributeParameters[0].textureVideoX265Preset,
      encParams.attributeParameters[0].textureVideoX265Preset,
      "x265 speed/efficiency preset")
    ("textureVideoEncoderConfig",
      encParams.attributeParameters[0].textureVideoEncoderConfig,
      encParams.attributeParameters[0].textureVideoEncoderConfig,
      "Texture video encoder configuration file")
    ("textureVideoEncoderConvertConfig",
      encParams.attributeParameters[0].textureVideoHDRToolEncConfig,
      encParams.attributeParameters[0].textureVideoHDRToolEncConfig,
      "HDRTools encode configuration file")
    ("textureVideoDecoderConvertConfig",
      encParams.attributeParameters[0].textureVideoHDRToolDecConfig,
      encParams.attributeParameters[0].textureVideoHDRToolDecConfig,
      "HDRTools decode configuration file")
    ("textureVideoDownsampleFilter",
      encParams.attributeParameters[0].textureVideoDownsampleFilter,
      encParams.attributeParameters[0].textureVideoDownsampleFilter,
      "Chroma downsample filter in [0;22]")
    ("textureVideoUpsampleFilter",
      encParams.attributeParameters[0].textureVideoUpsampleFilter,
      encParams.attributeParameters[0].textureVideoUpsampleFilter,
      "Chroma upsample filter in [0;7]")
    ("textureVideoFullRange",
      encParams.attributeParameters[0].textureVideoFullRange,
      encParams.attributeParameters[0].textureVideoFullRange,
      "Texture video range: 0: limited, 1: full")
    ("textureVideoQP",
      encParams.attributeParameters[0].textureVideoQP,
      encParams.attributeParameters[0].textureVideoQP,
      "Quantization parameter for texture video")
    ("textureVideoWidth",
      encParams.attributeParameters[0].textureWidth,
      encParams.attributeParameters[0].textureWidth,
      "Output texture width")
    ("textureVideoHeight",
      encParams.attributeParameters[0].textureHeight,
      encParams.attributeParameters[0].textureHeight,
      "Output texture height")
  ("textureInputBitDepth",
    encParams.attributeParameters[0].textureInputBitDepth,
    encParams.attributeParameters[0].textureInputBitDepth,
    "Input Texture map bitdepth")
  ("textureVideoBitDepth",
    encParams.attributeParameters[0].textureVideoBitDepth,
    encParams.attributeParameters[0].textureVideoBitDepth,
    "Texturemap Video bitdepth")
  ("useOccMapRDO",
    encParams.attributeParameters[0].useOccMapRDO,
    encParams.attributeParameters[0].useOccMapRDO,
    "Use Occupancy maps in the RDO process in HM")
  ("occMapFilename",
    encParams.attributeParameters[0].occMapFilename,
    encParams.attributeParameters[0].occMapFilename,
    "Occupancy maps Filename")
#if USE_JOINT_CODING
  ("jointTextDisp",
    encParams.jointTextDisp,
    encParams.jointTextDisp,
    "joint coding of texture and displacement")
#endif
  ("scalableEnableFlag",
    encParams.scalableEnableFlag,
    encParams.scalableEnableFlag,
    "Texture map scalable flag")
  ("attributeExtractionEnable",
    encParams.attributeExtractionEnable,
    encParams.attributeExtractionEnable,
    "enable attribute extraction, 0: disable (default), 1: enable")
  ("attributeTransformParamsV3CSEIEnable",
    encParams.attributeTransformParamsV3CSEIEnable,
    encParams.attributeTransformParamsV3CSEIEnable,
    "transformation params SEI for v3c")
  ("attributeTransformParamsBaseMeshSEIEnable",
    encParams.attributeTransformParamsBaseMeshSEIEnable,
    encParams.attributeTransformParamsBaseMeshSEIEnable,
    "transformation params SEI for basemesh")
  (po::Section("Bitstreams"))
    ("forceSsvhUnitSizePrecision",
      encParams.forceSsvhUnitSizePrecision,
      encParams.forceSsvhUnitSizePrecision,
      "force SampleStreamV3CUnit size precision bytes")
    ("meshCodecSpecificParametersInBMSPS",
      encParams.meshCodecSpecificParametersInBMSPS,
      encParams.meshCodecSpecificParametersInBMSPS,
      "enable storage of mesh codec specific parameters in BMSPS, 0: disable (default), 1: enable")
    ("motionCodecSpecificParametersInBMSPS",
      encParams.motionCodecSpecificParametersInBMSPS,
      encParams.motionCodecSpecificParametersInBMSPS,
      "enable storage of motion codec specific parameters in BMSPS, 0: disable (default), 1: enable")


  (po::Section("Metrics"))
    ("pcc",
      metParams.computePcc,
      metParams.computePcc,
      "Compute pcc metrics")
    ("ibsm",
      metParams.computeIbsm,
      metParams.computeIbsm,
      "Compute ibsm metrics")
    ("pcqm",
      metParams.computePcqm,
      metParams.computePcqm,
      "Compute pcqm metrics")
    ("gridSize",
      metParams.gridSize,
      metParams.gridSize,
      "Grid size")
    ("resolution",
      metParams.resolution,
      metParams.resolution,
      "Resolution")
    ("pcqmRadiusCurvature",
      metParams.pcqmRadiusCurvature,
      metParams.pcqmRadiusCurvature,
      "PCQM radius curvature")
    ("pcqmThresholdKnnSearch",
      metParams.pcqmThresholdKnnSearch,
      metParams.pcqmThresholdKnnSearch,
      "PCQM threshold Knn search")
    ("pcqmRadiusFactor",
      metParams.pcqmRadiusFactor,
      metParams.pcqmRadiusFactor,
      "PCQM radius factor")

  (po::Section("Caching"))
    ("cachingDirectory",
      encParams.cachingDirectory,
      encParams.cachingDirectory,
      "Caching directory")
    ("cachingPoint",
      encParams.cachingPoint,
      encParams.cachingPoint,
      "Caching points: \n"
      "  - 0/none    : off \n"
      "  - 1/simplify: symplify\n"
      "  - 2/textgen : textgen\n"
      "  - 3/subdiv  : subdiv \n"
      "  - 255/create: create caching files")

    (po::Section("Coding Structure"))
    ("GOPSize",
      encParams.basemeshGOPSize,
      encParams.basemeshGOPSize,
      "GOP Size")

    (po::Section("Normals"))
    ("encodeNormals",
      encParams.encodeNormals,
      encParams.encodeNormals,
      "Whether to encode the normals: Default: 0")
    ("normalInt",
      encParams.normalInt,
      encParams.normalInt,
      "Are the normals integer (1) or float (0): Default: 1")
    ("normalBitDepth",
      encParams.bitDepthNormals,
      encParams.bitDepthNormals,
      "Input Normals bit depth, Default: 16. If the input Normals bit depth is larger than normalBitDepth value, then input Normals would be clamp to normalBitDepth")
    ("baseMeshNormalBitDepth",
      encParams.qpNormals,
      encParams.qpNormals,
      "Quantization bits for base mesh Normals, Default: 16")
    ("minNormals",
      encParams.minNormals,
      { -1.0, -1.0, -1.0 },
      "Min Normals, Default: { -1.0, -1.0, -1.0 }")
    ("maxNormals",
      encParams.maxNormals,
      { 1.0, 1.0, 1.0 },
      "Max Normals, Default: { 1.0, 1.0, 1.0 }")
    ("predNormal",
      encParams.predNormal,
      encParams.predNormal,
      "Prediction scheme for Normals in EB. 1: Delta, 2: MPARA, 3: Cross, Default: 3")
    ("normalsOctahedral",
      encParams.normalsOctahedral,
      encParams.normalsOctahedral,
      "Whether to encode normals using 2D Octahedral representation: Default: 1")
    ("qpOctahedral",
      encParams.qpOcta,
      encParams.qpOcta,
      "Quantization bits for 2D Normals representation, Default: 16")
  ;
  for (int i = 1; i < vmesh::MAX_GOP + 1; i++) {
    std::ostringstream cOSS;
    cOSS << "Frame" << i;
    opts.addOptions()(cOSS.str(), encParams.basemeshGOPList[i - 1], vmesh::BaseMeshGOPEntry());
  }
  /* clang-format on */

  po::setDefaults(opts);
  po::ErrorReporter             err;
  const std::list<const char*>& argv_unhandled =
    po::scanArgv(opts, argc, (const char**)argv, err);

  for (const auto* const arg : argv_unhandled) {
    err.warn() << "Unhandled argument ignored: " << arg << '\n';
  }

  if (argc == 1 || print_help) {
    std::cout << "usage: " << argv[0] << " [arguments...] \n\n";
    po::doHelp(std::cout, opts, 78);
    return false;
  }

  if (!checkParameters(err, params)) { return false; }

  encParams.resultPath = params.compressedStreamPath;
  if (encParams.scalableEnableFlag) {
    encParams.attributeParameters[0].textureVideoEncoderId =
      vmesh::VideoEncoderId::SHMAPP;  // default: SHVC
    encParams.profileCodecGroupId = vmesh::VideoCodecGroupIdc::Video_MP4RA;
    encParams.maxLayersMinus1     = 2;  // default: 3-layer
  }
  if (params.encParams.profileCodecGroupId
      != (int)vmesh::VideoCodecGroupIdc::Video_MP4RA) {
    params.encParams.geometryCodecId = params.encParams.profileCodecGroupId;
    params.encParams.attributeParameters[0].textureCodecId =
      params.encParams.profileCodecGroupId;
  } else {
    params.encParams.geometryCodecId =
      (vmesh::VideoCodecGroupIdc)params.encParams.geometryVideoEncoderId;
    params.encParams.attributeParameters[0].textureCodecId =
      (vmesh::VideoCodecGroupIdc)params.encParams.attributeParameters[0]
        .textureVideoEncoderId;
  }

  if (params.encParams.encodeTextureVideo) {
    if (!params.temp.empty()) {
      if (params.inputAttributesPaths.empty()) {
        params.inputAttributesPaths.push_back(params.temp);
      } else if (params.inputAttributesPaths[0].empty()) {
        params.inputAttributesPaths[0] = params.temp;
      }
    }

    if (params.encParams.videoAttributeCount == 0) {
      params.encParams.videoAttributeCount = 1;
    }
  }

  if (encParams.encodeTextureVideo && params.encParams.numTextures > 1) {
    params.encParams.videoAttributeCount =
      encParams.setLosslessTools ? params.encParams.numTextures : 1;
    std::cout
      << " when multiple texture files are used for the lossy compression, "
         "the video attributes is limitted only to texture.\n";
  }

  if (params.encParams.videoAttributeCount > 1) {
      params.encParams.attributeParameters.resize(
          params.encParams.videoAttributeCount,
          params.encParams.attributeParameters[0]);
  }
  for (int i = 1; i < params.encParams.videoAttributeCount; i++) {
    params.encParams.attributeParameters[i] =
      params.encParams.attributeParameters[0];
  }
  if (params.encParams.videoAttributeCount == 0) {
      auto& attributeParameters = params.encParams.attributeParameters[0];
      attributeParameters.textureVideoEncoderConfig.assign("(NONE)");
      attributeParameters.textureVideoHDRToolEncConfig.assign("(NONE)");
      attributeParameters.textureVideoHDRToolDecConfig.assign("(NONE)");
  }

  if (params.encParams.jointTextDisp) {
    if (!params.encParams.encodeTextureVideo) {
      std::cerr << "Error: jointTextDisp requires that encodeTextureVideo "
                   " is set to true\n";
      exit(0);
    }
    params.encParams.videoAttributeCount = 1;
    std::cout << "jointTextDisp is true, the video attributes is limitted "
                 "only to texture.\n";
  }
#if MULTI_ATTRIBUTE_TEST
  if (params.encParams.videoAttributeCount > 1) {
    params.inputAttributesPaths.resize(3);
    params.reconstructedAttributesPaths.resize(3);
    //  params.encParams.videoAttributeTypes.resize(3);
    params.inputAttributesPaths[1] =
      "/thomas_voxelized/thomas_fr0619.png";  //4096x4096
    params.reconstructedAttributesPaths[1]                = "rec_attr1_%d.png";
    params.encParams.attributeParameters[1].textureWidth  = 4096;
    params.encParams.attributeParameters[1].textureHeight = 4096;
    params.encParams.attributeParameters[1].textureCodecId =
      params.encParams.profileCodecGroupId;
    encParams.attributeParameters[1].textureTransferEnable = false;
    encParams.attributeParameters[1].enablePadding         = false;
    encParams.attributeParameters[1].useOccMapRDO          = false;
    params.inputAttributesPaths[2]         = "/rwtt422_voxelized/model.png";
    params.reconstructedAttributesPaths[2] = "rec_attr2_%d.png";
    params.encParams.attributeParameters[2].textureWidth  = 1024;
    params.encParams.attributeParameters[2].textureHeight = 1024;
    params.encParams.attributeParameters[2].textureCodecId =
      params.encParams.profileCodecGroupId;
    encParams.attributeParameters[2].textureTransferEnable = false;
    encParams.attributeParameters[2].enablePadding         = false;
    encParams.attributeParameters[2].useOccMapRDO          = false;
  }
#endif

#if DISPL_AC_TEMP_SETTING
  if (encParams.encodeDisplacementType == 1) {
    encParams.displacementQuantizationType =
      vmesh::DisplacementQuantizationType::DEFAULT;
    encParams.IQSkipFlag = true;
  } else {
    encParams.IQSkipFlag = false;
  }
#endif

  if (encParams.displacementQuantizationType
      == vmesh::DisplacementQuantizationType::ADAPTIVE) {
    // copy liftingQP into geoVideoQP
    encParams.geoVideoQP = encParams.liftingQP;
    // normalization of adaptiveScale for various bitDepth position and geometryVideoBitDepth
    double bdScale =
      (1 << (4 + encParams.bitDepthPosition - encParams.geometryVideoBitDepth))
      / 16.0;
    encParams.liftingQP[0] = uint32_t(encParams.adaptiveScale[0] / bdScale);
    encParams.liftingQP[1] = uint32_t(encParams.adaptiveScale[1] / bdScale);
    encParams.liftingQP[2] = uint32_t(encParams.adaptiveScale[2] / bdScale);
  }

  if (encParams.transformMethod == 0) {
    encParams.lodDisplacementQuantizationFlag = false;
    encParams.applyLiftingOffset              = false;
    encParams.liftingAdaptiveUpdateWeightFlag = false;
    for (unsigned int& log2LevelOfDetailInverseScale :
         encParams.log2LevelOfDetailInverseScale) {
      log2LevelOfDetailInverseScale = 0;
    }
  }

  if (encParams.setLosslessTools) {
    encParams.encodeDisplacementType      = 0;
    encParams.textureParameterizationType = 0;
    for (int i = 0; i < params.encParams.videoAttributeCount; i++) {
      encParams.attributeParameters[i].textureVideoBitDepth =
        encParams.attributeParameters[i].textureInputBitDepth;
    }
    if (encParams.numSubmesh != 1 || encParams.numTilesGeometry != 1) {
      std::cout << "submesh count and tile count are set as 1\n";
      encParams.numSubmesh       = 1;
      encParams.numTilesGeometry = 1;
    }
    encParams.dracoMeshLossless = true;
    if (params.encParams.profileCodecGroupId
        != (int)vmesh::VideoCodecGroupIdc::Video_MP4RA) {
      params.encParams.geometryCodecId = vmesh::VideoCodecGroupIdc::HEVC444;
      for (auto& attributeParameter : params.encParams.attributeParameters) {
        attributeParameter.textureCodecId = vmesh::VideoCodecGroupIdc::HEVC444;
      }
    }
  }

  // disabling textureParameterization is effective only when setLosslessTools
  // is not set, when disabled it cannot be combined with encodeTextureVideo 
  // set to true
  if (params.encParams.textureParameterizationType == -1) {
      if (params.encParams.encodeTextureVideo) {
          std::cerr << "Error: textureParameterizationType NONE requires "
              "that encodeTextureVideo is set to false\n";
          exit(0);
      }
  }
  if (encParams.encodeDisplacementType == 2 && encParams.jointTextDisp) {
    encParams.textureSliceMode     = 1;
    encParams.textureSliceArgument = int32_t(
      std::ceil((double)encParams.attributeParameters[0].textureHeight / 64)
      * std::ceil((double)encParams.attributeParameters[0].textureWidth / 64));
  }

  if (encParams.numSubmesh != 1 && encParams.numSubmesh != 2
      && encParams.numSubmesh != 3 && encParams.numSubmesh != 4) {
    std::cout << "video subTexture sizes need to be adjusted\n";
  }
  if (!encParams.encodeTextureVideo) {
      encParams.numTilesAttribute = 0;
  }
  else {
    if (encParams.submeshSegmentationType == 0 && encParams.numSubmesh != 1) {
      encParams.attributeParameters[0].texturePlacementPerPatch = true;
      std::cout << "submeshSegmentationType=0 && submeshCount>1 requires "
                   "per-patch placement for texture maps\n";
    }
  }
  //====================================
  //enable signalling submeshId and tileId
  encParams.submeshIdList.resize(encParams.numSubmesh, 0);
  for (size_t i = 0; i < encParams.submeshIdList.size(); i++) {
    encParams.submeshIdList[i] = (uint32_t)i;
  }

  auto tileCount = encParams.numTilesGeometry + encParams.numTilesAttribute;
  encParams.tileIdList.resize(
    encParams.numTilesGeometry + encParams.numTilesAttribute, 0);
  for (size_t i = 0; i < tileCount; i++) {
    encParams.tileIdList[i] = (uint32_t)i;
  }

  encParams.acDisplacementEncoderParameters_.enableSignalledIds = encParams.enableSignalledIds;
  encParams.acDisplacementEncoderParameters_.numSubmesh = encParams.numSubmesh;
  if (encParams.enableSignalledIds) {
    // examples are defined only for numSubmesh = 3 and numTilesGeometry=2
    if (encParams.numSubmesh == 3) {
      encParams.submeshIdList[0] = SUBMESHID2ND;
      encParams.submeshIdList[1] = SUBMESHID1ST;
      encParams.submeshIdList[2] = SUBMESHID3RD;
      encParams.acDisplacementEncoderParameters_.submeshIdList = encParams.submeshIdList;
    }
    if (tileCount == 3) {
      encParams.tileIdList[0] = TILEID1ST;
      encParams.tileIdList[1] = TILEID2ND;
      encParams.tileIdList[3] = TILEID3ND;
    }
  }

  encParams.submeshIdsInTile.resize(tileCount);
  if (encParams.numTilesGeometry == 0) {
    std::cout << "numTilesGeometry is set as 1\n";
    encParams.numTilesGeometry = 1;
  }
  if (encParams.numTilesAttribute > 1) {
      std::cerr << "Error: numTilesAttribute > 1 is not implemented\n";
      exit(0);
  }
  if (encParams.numTilesGeometry == 1) {
    for (size_t i = 0; i < encParams.submeshIdList.size(); i++) {
        for (size_t j = 0; j < tileCount; j++) {
            encParams.submeshIdsInTile[j].push_back(encParams.submeshIdList[i]);
        }
    }
  } else if (encParams.numTilesGeometry == 2) {
    if (encParams.numSubmesh == 3) {
      encParams.submeshIdsInTile[0] = {encParams.submeshIdList[0],
                                       encParams.submeshIdList[2]};
      encParams.submeshIdsInTile[1] = {encParams.submeshIdList[1]};
      if (encParams.numTilesAttribute == 1) {
        encParams.submeshIdsInTile[2] = {encParams.submeshIdList[0],
                                         encParams.submeshIdList[1],
                                         encParams.submeshIdList[2]};
      }
    } else {
      std::cerr << "Error: numTilesGeometry == 2 & numSubmesh != 3 is not "
                   "implemented\n";
      exit(0);
    }
  } else if (encParams.numTilesGeometry > 2) {
    std::cerr << "Error: numTilesGeometry > 2 is not implemented\n";
    exit(0);
  }

  encParams.segmentByBaseMesh =
    encParams.segmentByBaseMesh && encParams.numSubmesh > 1;
  if (encParams.segmentByBaseMesh) { encParams.deformForRecBase = false; }

  if (!encParams.liftingAdaptiveUpdateWeightFlag
      && encParams.liftingValenceUpdateWeightFlag) {
    std::vector<uint32_t> liftingUpdateWeightNumerator         = {6, 6, 6};
    std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1 = {4, 4, 4};
    for (int i = 0; i < encParams.subdivisionIterationCount; i++) {
      encParams.liftingUpdateWeightNumerator[i] =
        liftingUpdateWeightNumerator[i];
      encParams.liftingUpdateWeightDenominatorMinus1[i] =
        liftingUpdateWeightDenominatorMinus1[i];
    }
  }

  if (!encParams.liftingValenceUpdateWeightFlag) {
    if (!encParams.liftingAdaptiveUpdateWeightFlag) {
      std::vector<uint32_t> liftingUpdateWeightNumerator         = {1, 1, 1};
      std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1 = {4, 4, 4};
      for (int i = 0; i < encParams.subdivisionIterationCount; i++) {
        encParams.liftingUpdateWeightNumerator[i] =
          liftingUpdateWeightNumerator[i];
        encParams.liftingUpdateWeightDenominatorMinus1[i] =
          liftingUpdateWeightDenominatorMinus1[i];
      }
    } else {
      std::vector<uint32_t> liftingUpdateWeightNumerator         = {21, 7, 7};
      std::vector<uint32_t> liftingUpdateWeightDenominatorMinus1 = {
        159, 39, 29};
      for (int i = 0; i < encParams.subdivisionIterationCount; i++) {
        encParams.liftingUpdateWeightNumerator[i] =
          liftingUpdateWeightNumerator[i];
        encParams.liftingUpdateWeightDenominatorMinus1[i] =
          liftingUpdateWeightDenominatorMinus1[i];
      }
    }
  }

  if (encParams.encodeDisplacementType == 0) {
    encParams.applyLiftingOffset = false;
  }

  if (encParams.lodDisplacementQuantizationFlag
      && (encParams.encodeDisplacementType != 0)) {
    auto lodCount        = encParams.subdivisionIterationCount + 1;
    auto liftingQP2Count = static_cast<int32_t>(liftingQP2.size());
    if (liftingQP2.empty()) {
      err.error() << "liftingQP2 not specified\n";
    } else if (liftingQP2Count != lodCount * 3) {
      err.error() << "the length of liftingQP2 not " << lodCount * 3 << "\n";
    } else {
      encParams.qpPerLevelOfDetails.clear();
      for (int32_t i = 0; i < liftingQP2Count; i += 3) {
        encParams.qpPerLevelOfDetails.push_back(
          {liftingQP2[i + 0], liftingQP2[i + 1], liftingQP2[i + 2]});
      }
    }
  }
  intraGeoParams.lodAdaptiveSubdivisionFlag = lodASM;
  if (lodASM) {
    intraGeoParams.subdivisionMethod[0] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method0);
    intraGeoParams.subdivisionMethod[1] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method1);
    intraGeoParams.subdivisionMethod[2] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method2);
  } else {
    intraGeoParams.subdivisionMethod[0] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method);
    intraGeoParams.subdivisionMethod[1] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method);
    intraGeoParams.subdivisionMethod[2] =
      static_cast<vmesh::SubdivisionMethod>(subdiv_method);
  }
  if ((encParams.textureParameterizationType < 1)
      || (encParams.baseIsSrc && encParams.subdivIsBase)) {
    //Either no texture parameterization was selected or UV Atlas was chosen, so disable the texture derivation
    encParams.iDeriveTextCoordFromPos = 0;
  }

  if (encParams.packingType != 3) {
    encParams.updateTextureSize = 0;
    if (encParams.textureParameterizationType == 1) {
      std::cout
        << "updateTextureSize is set as 0 since packingType is not 3\n";
    }
  }

  if (encParams.texturePackingWidth == -1) {
    encParams.texturePackingWidth = encParams.texParamWidth;
  }
  if (encParams.texturePackingHeight == -1) {
    encParams.texturePackingHeight = encParams.texParamHeight;
  }

  encParams.basemeshGOPList.resize(encParams.basemeshGOPSize);

  if (encParams.encodeTextureVideo && !encParams.attributeParameters.empty()) {
      if (encParams.attributeParameters[0].occMapFilename == "_occmap.yuv") {
          encParams.attributeParameters[0].occMapFilename =
              params.compressedStreamPath.substr(
                  0, params.compressedStreamPath.rfind('.') - 1)
              + "_occmap.yuv";
      }
  }

  if (encParams.keepIntermediateFiles) {
    encParams.keepBaseMesh   = true;
    encParams.keepVideoFiles = true;
    encParams.keepPreMesh    = true;
  }
  
  if (encParams.zipperingMethod_ >= 0 && encParams.numSubmesh == 1) {
      std::cout << " only one submesh, zippering is disabled\n";
      encParams.zipperingMethod_ = -1;
  }

  if (err.is_errored) { return false; }

  // Dump the complete derived configuration
  po::dumpCfgBySection(std::cout, opts);

  //
  encParams.bitshiftEdgeBasedSubdivision = 16 - encParams.bitDepthPosition;
  for (int i = 0; i < encParams.subdivisionIterationCount; i++) {
    if (encParams.intraGeoParams.subdivisionMethod[i]
        != vmesh::SubdivisionMethod::MID_POINT) {
      encParams.floatSubdivisionEdgeLengthThreshold = 0.0;
    }
  }
  encParams.subdivisionEdgeLengthThreshold =
    int32_t(encParams.floatSubdivisionEdgeLengthThreshold
            * std::pow(2, encParams.bitshiftEdgeBasedSubdivision));
  if (encParams.subdivisionEdgeLengthThreshold > 0) {
    intraGeoParams.subdivisionIterationCount =
      encParams.subdivisionIterationCount;
    interGeoParams.subdivisionIterationCount =
      encParams.subdivisionIterationCount;

    intraGeoParams.subdivisionEdgeLengthThreshold =
      encParams.subdivisionEdgeLengthThreshold;
    intraGeoParams.bitshiftEdgeBasedSubdivision =
      encParams.bitshiftEdgeBasedSubdivision;
    interGeoParams.subdivisionEdgeLengthThreshold =
      encParams.subdivisionEdgeLengthThreshold;
    interGeoParams.bitshiftEdgeBasedSubdivision =
      encParams.bitshiftEdgeBasedSubdivision;
  }

  // copy parameters to atlas parameters

  encParams.atlasEncoderParameters_.encodeTextureVideo = encParams.encodeTextureVideo;
  encParams.atlasEncoderParameters_.displacementCoordinateSystem = encParams.displacementCoordinateSystem;
  encParams.atlasEncoderParameters_.bFaceIdPresentFlag                 = encParams.bFaceIdPresentFlag;
  encParams.atlasEncoderParameters_.increaseTopSubmeshSubdivisionCount = encParams.increaseTopSubmeshSubdivisionCount;
  encParams.atlasEncoderParameters_.submeshIdList = encParams.submeshIdList;
  encParams.atlasEncoderParameters_.bitDepthPosition                 = encParams.bitDepthPosition;
  encParams.atlasEncoderParameters_.bitDepthTexCoord                 = encParams.bitDepthTexCoord;
  encParams.atlasEncoderParameters_.log2GeometryVideoBlockSize       = encParams.log2GeometryVideoBlockSize;
  encParams.atlasEncoderParameters_.subdivisionIterationCount        = encParams.subdivisionIterationCount;
  encParams.atlasEncoderParameters_.interpolateSubdividedNormalsFlag = encParams.interpolateSubdividedNormalsFlag;
  encParams.atlasEncoderParameters_.intraGeoParams = encParams.intraGeoParams;
  //encParams.atlasEncoderParameters_.interGeoParams = encParams.interGeoParams;
  encParams.atlasEncoderParameters_.use45DegreeProjection          = encParams.use45DegreeProjection;
  encParams.atlasEncoderParameters_.subdivisionEdgeLengthThreshold = encParams.subdivisionEdgeLengthThreshold;
  encParams.atlasEncoderParameters_.transformMethod = encParams.transformMethod;  // 0: None Transform, 1: Linear Lifting m68642
  encParams.atlasEncoderParameters_.applyOneDimensionalDisplacement                 = encParams.applyOneDimensionalDisplacement;
  encParams.atlasEncoderParameters_.lodDisplacementQuantizationFlag                 = encParams.lodDisplacementQuantizationFlag;
  encParams.atlasEncoderParameters_.bitDepthOffset                                  = encParams.bitDepthOffset;
  encParams.atlasEncoderParameters_.log2LevelOfDetailInverseScale     = encParams.log2LevelOfDetailInverseScale;
  encParams.atlasEncoderParameters_.liftingQP                         = encParams.liftingQP;
  encParams.atlasEncoderParameters_.qpPerLevelOfDetails = encParams.qpPerLevelOfDetails;
  encParams.atlasEncoderParameters_.displacementQuantizationType = encParams.displacementQuantizationType;
  encParams.atlasEncoderParameters_.IQSkipFlag                      = encParams.IQSkipFlag;
  encParams.atlasEncoderParameters_.InverseQuantizationOffsetFlag   = encParams.InverseQuantizationOffsetFlag;
  encParams.atlasEncoderParameters_.liftingSkipUpdate               = encParams.liftingSkipUpdate;
  encParams.atlasEncoderParameters_.liftingAdaptiveUpdateWeightFlag = encParams.liftingAdaptiveUpdateWeightFlag;
  encParams.atlasEncoderParameters_.liftingValenceUpdateWeightFlag  = encParams.liftingValenceUpdateWeightFlag;

  encParams.atlasEncoderParameters_.liftingUpdateWeightNumerator         = encParams.liftingUpdateWeightNumerator;
  encParams.atlasEncoderParameters_.liftingUpdateWeightDenominatorMinus1 = encParams.liftingUpdateWeightDenominatorMinus1;

  encParams.atlasEncoderParameters_.liftingPredictionWeightNumerator = encParams.liftingPredictionWeightNumerator;
  encParams.atlasEncoderParameters_.liftingPredictionWeightDenominatorMinus1 = encParams.liftingPredictionWeightDenominatorMinus1;

  encParams.atlasEncoderParameters_.liftingPredictionWeightNumeratorDefault = encParams.liftingPredictionWeightNumeratorDefault;
  encParams.atlasEncoderParameters_.liftingPredictionWeightDenominatorMinus1Default = encParams.liftingPredictionWeightDenominatorMinus1Default;

  encParams.atlasEncoderParameters_.liftingAdaptivePredictionWeightFlag = encParams.liftingAdaptivePredictionWeightFlag;

  encParams.atlasEncoderParameters_.encodeDisplacementType = encParams.encodeDisplacementType;

  encParams.atlasEncoderParameters_.applyLiftingOffset = encParams.applyLiftingOffset;

  encParams.atlasEncoderParameters_.dirlift                = encParams.dirlift;
  encParams.atlasEncoderParameters_.dirliftScale1          = encParams.dirliftScale1;
  encParams.atlasEncoderParameters_.dirliftDeltaScale2     = encParams.dirliftDeltaScale2;
  encParams.atlasEncoderParameters_.dirliftDeltaScale3     = encParams.dirliftDeltaScale3;
  encParams.atlasEncoderParameters_.dirliftScaleDenoMinus1 = encParams.dirliftScaleDenoMinus1;

  encParams.atlasEncoderParameters_.displacementReversePacking = encParams.displacementReversePacking;

  encParams.atlasEncoderParameters_.iDeriveTextCoordFromPos =encParams.iDeriveTextCoordFromPos;

  encParams.atlasEncoderParameters_.packingScaling       = encParams.packingScaling;
  encParams.atlasEncoderParameters_.biasEnableFlag       = encParams.biasEnableFlag;
  encParams.atlasEncoderParameters_.useRawUV             = encParams.useRawUV;
  encParams.atlasEncoderParameters_.rawTextcoordBitdepth = encParams.rawTextcoordBitdepth;
  encParams.atlasEncoderParameters_.numSubmesh           = encParams.numSubmesh;
  encParams.atlasEncoderParameters_.enableSignalledIds   = encParams.enableSignalledIds;

  encParams.atlasEncoderParameters_.texturePackingWidth  = encParams.texturePackingWidth;
  encParams.atlasEncoderParameters_.texturePackingHeight = encParams.texturePackingHeight;
  encParams.atlasEncoderParameters_.gutter               = encParams.gutter;

  encParams.atlasEncoderParameters_.numTilesGeometry  = encParams.numTilesGeometry;
  encParams.atlasEncoderParameters_.numTilesAttribute = encParams.numTilesAttribute;
  encParams.atlasEncoderParameters_.tileIdList = encParams.tileIdList;
  encParams.atlasEncoderParameters_.submeshIdsInTile = encParams.submeshIdsInTile;

  encParams.atlasEncoderParameters_.lodPatchesEnable = encParams.lodPatchesEnable;

  encParams.atlasEncoderParameters_.maxNumRefBmeshList  = encParams.maxNumRefBmeshList;
  encParams.atlasEncoderParameters_.maxNumRefBmeshFrame = encParams.maxNumRefBmeshFrame;

  encParams.atlasEncoderParameters_.refFrameDiff = encParams.refFrameDiff;

  encParams.atlasEncoderParameters_.analyzeGof = encParams.analyzeGof;
  encParams.atlasEncoderParameters_.basemeshGOPList = encParams.basemeshGOPList;
  encParams.atlasEncoderParameters_.packingType = encParams.packingType;
  encParams.atlasEncoderParameters_.texturePlacementPerPatch  = encParams.attributeParameters[0].texturePlacementPerPatch;

  // copy parameters to ac displacement parameters
  encParams.acDisplacementEncoderParameters_.enableSignalledIds = encParams.enableSignalledIds;
  encParams.acDisplacementEncoderParameters_.numSubmesh         = encParams.numSubmesh;
  encParams.acDisplacementEncoderParameters_.submeshIdList = encParams.submeshIdList;
  encParams.acDisplacementEncoderParameters_.bitDepthPosition          = encParams.bitDepthPosition;
  encParams.acDisplacementEncoderParameters_.subdivisionIterationCount = encParams.subdivisionIterationCount;
  encParams.acDisplacementEncoderParameters_.transformMethod                   = encParams.transformMethod;
  encParams.acDisplacementEncoderParameters_.lodDisplacementQuantizationFlag   = encParams.lodDisplacementQuantizationFlag;
  encParams.acDisplacementEncoderParameters_.bitDepthOffset                    = encParams.bitDepthOffset;
  encParams.acDisplacementEncoderParameters_.log2LevelOfDetailInverseScale     = encParams.log2LevelOfDetailInverseScale;
  encParams.acDisplacementEncoderParameters_.liftingQP                         = encParams.liftingQP;
  encParams.acDisplacementEncoderParameters_.qpPerLevelOfDetails =encParams.qpPerLevelOfDetails;
  encParams.acDisplacementEncoderParameters_.InverseQuantizationOffsetFlag   = encParams.InverseQuantizationOffsetFlag;
  encParams.acDisplacementEncoderParameters_.applyOneDimensionalDisplacement = encParams.applyOneDimensionalDisplacement;
  encParams.acDisplacementEncoderParameters_.checksum                        = encParams.checksum;
  encParams.acDisplacementEncoderParameters_.subBlocksPerLoD = encParams.subBlocksPerLoD;
  encParams.acDisplacementEncoderParameters_.blockSubdiv = encParams.blockSubdiv;
  encParams.acDisplacementEncoderParameters_.displacementFillZerosFlag = encParams.displacementFillZerosFlag;
  for(int i = 0; i < 3; i++) {
    encParams.acDisplacementEncoderParameters_.liftingBias[i] =
      encParams.liftingBias[i];
  }
  encParams.acDisplacementEncoderParameters_.displacementQuantizationType =
    encParams.displacementQuantizationType;
  encParams.acDisplacementEncoderParameters_.geometryVideoBitDepth          = encParams.geometryVideoBitDepth;
  encParams.acDisplacementEncoderParameters_.displacementFillZerosThreshold = encParams.displacementFillZerosThreshold;

  // copy parameters to basemesh parameters
  encParams.baseMeshEncoderParameters_.checksum = encParams.checksum;
  encParams.baseMeshEncoderParameters_.maxNumRefBmeshList = encParams.maxNumRefBmeshList;
  encParams.baseMeshEncoderParameters_.maxNumRefBmeshFrame = encParams.maxNumRefBmeshFrame;
  encParams.baseMeshEncoderParameters_.qpPosition = encParams.qpPosition;
  encParams.baseMeshEncoderParameters_.qpTexCoord = encParams.qpTexCoord;
  encParams.baseMeshEncoderParameters_.bitDepthPosition = encParams.bitDepthPosition;
  encParams.baseMeshEncoderParameters_.bitDepthTexCoord = encParams.bitDepthTexCoord;
  for(int i = 0; i < 3; i++) {
    encParams.baseMeshEncoderParameters_.minPosition[i] = encParams.minPosition[i];
    encParams.baseMeshEncoderParameters_.maxPosition[i] = encParams.maxPosition[i];
  }
  encParams.baseMeshEncoderParameters_.numSubmesh = encParams.numSubmesh;
  encParams.baseMeshEncoderParameters_.numTextures = encParams.numTextures;
  encParams.baseMeshEncoderParameters_.submeshIdList = encParams.submeshIdList;

  encParams.baseMeshEncoderParameters_.iDeriveTextCoordFromPos = encParams.iDeriveTextCoordFromPos;
  encParams.baseMeshEncoderParameters_.enableSignalledIds = encParams.enableSignalledIds;
  encParams.baseMeshEncoderParameters_.keepBaseMesh = encParams.keepBaseMesh;
  encParams.baseMeshEncoderParameters_.keepIntermediateFiles = encParams.keepIntermediateFiles;
  encParams.baseMeshEncoderParameters_.encodeNormals = encParams.encodeNormals;
  encParams.baseMeshEncoderParameters_.qpNormals = encParams.qpNormals;
  encParams.baseMeshEncoderParameters_.predNormal = encParams.predNormal;
  encParams.baseMeshEncoderParameters_.normalsOctahedral = encParams.normalsOctahedral;
  encParams.baseMeshEncoderParameters_.entropyPacket = encParams.entropyPacket;
  encParams.baseMeshEncoderParameters_.qpOcta = encParams.qpOcta;
  encParams.baseMeshEncoderParameters_.predGeneric = encParams.predGeneric;

  encParams.baseMeshEncoderParameters_.basemeshGOPList = encParams.basemeshGOPList;


  encParams.baseMeshEncoderParameters_.meshCodecId = encParams.meshCodecId;
  encParams.baseMeshEncoderParameters_.dracoUsePosition = encParams.dracoUsePosition;
  encParams.baseMeshEncoderParameters_.dracoUseUV = encParams.dracoUseUV;
  encParams.baseMeshEncoderParameters_.dracoMeshLossless = encParams.dracoMeshLossless;
  encParams.baseMeshEncoderParameters_.motionGroupSize = encParams.motionGroupSize;
  encParams.baseMeshEncoderParameters_.motionWithoutDuplicatedVertices = encParams.motionWithoutDuplicatedVertices;
  encParams.baseMeshEncoderParameters_.baseMeshVertexTraversal = encParams.baseMeshVertexTraversal;
  encParams.baseMeshEncoderParameters_.motionVertexTraversal = encParams.motionVertexTraversal;
  encParams.baseMeshEncoderParameters_.baseMeshDeduplication = encParams.baseMeshDeduplication;
  encParams.baseMeshEncoderParameters_.reverseUnification = encParams.reverseUnification;
  encParams.baseMeshEncoderParameters_.profileGeometryCodec = encParams.profileGeometryCodec;
  encParams.baseMeshEncoderParameters_.profileGeometryCodec = encParams.profileGeometryCodec;
  encParams.baseMeshEncoderParameters_.bFaceIdPresentFlag = encParams.bFaceIdPresentFlag;
  encParams.baseMeshEncoderParameters_.maxNumMotionVectorPredictor = encParams.maxNumMotionVectorPredictor;
  encParams.baseMeshEncoderParameters_.subdivisionEdgeLengthThreshold = encParams.subdivisionEdgeLengthThreshold;
  encParams.baseMeshEncoderParameters_.useRawUV = encParams.useRawUV;
  encParams.baseMeshEncoderParameters_.subdivisionIterationCount = encParams.subdivisionIterationCount;
  encParams.baseMeshEncoderParameters_.bitshiftEdgeBasedSubdivision = encParams.bitshiftEdgeBasedSubdivision;
  encParams.baseMeshEncoderParameters_.increaseTopSubmeshSubdivisionCount = encParams.increaseTopSubmeshSubdivisionCount;

  // Copy duplicate parameters
  for (int k = 0; k < 3; k++) {
    metParams.minPosition[k] = encParams.minPosition[k];
    metParams.maxPosition[k] = encParams.maxPosition[k];
  }
  metParams.qp           = encParams.bitDepthPosition;
  metParams.qt           = encParams.bitDepthTexCoord;
  metParams.dequantizeUV = encParams.dequantizeUV;
  metParams.verbose      = params.verbose;
  if (params.encParams.increaseTopSubmeshSubdivisionCount) {
    intraGeoParams.geometrySamplingSubdivisionIterationCount =
      params.encParams.subdivisionIterationCount + 1;
    intraGeoParams.geometryParametrizationSubdivisionIterationCount =
      params.encParams.subdivisionIterationCount + 1;
    interGeoParams.geometrySamplingSubdivisionIterationCount =
      params.encParams.subdivisionIterationCount + 1;
    interGeoParams.geometryParametrizationSubdivisionIterationCount =
      params.encParams.subdivisionIterationCount + 1;
    params.encParams.textureTransferSamplingSubdivisionIterationCount =
      params.encParams.subdivisionIterationCount;
  }
  return true;
} catch (df::program_options_lite::ParseFailure& e) {
  std::cerr << "Error parsing option \"" << e.arg << "\" with argument \""
            << e.val << "\".\n";
  return false;
}

//============================================================================
