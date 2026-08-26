/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "metrics.hpp"
#include "util/image.hpp"
#include "util/checksum.hpp"

#include "mmSample.h"
#include "mmModel.h"
#include "mmImage.h"
#include "mmCompare.h"
#include "mmDequantize.h"
#include "mmIO.h"

using namespace vmesh;

//============================================================================

template<typename T>
void
convert(const TriangleMesh<T>& src, mm::Model& dst) {
  dst.reset();
  const auto& points = src.points();
  dst.vertices.resize(points.size() * 3);
  for (size_t i = 0; i < points.size(); i++) {
    dst.vertices[3 * i + 0] = points[i][0];
    dst.vertices[3 * i + 1] = points[i][1];
    dst.vertices[3 * i + 2] = points[i][2];
  }
  if (!src.texCoords().empty()) {
    const auto& texCoords = src.texCoords();
    dst.uvcoords.resize(texCoords.size() * 2);
    for (size_t i = 0; i < texCoords.size(); i++) {
      dst.uvcoords[2 * i + 0] = texCoords[i][0];
      dst.uvcoords[2 * i + 1] = texCoords[i][1];
    }
  }
  const auto& triangles = src.triangles();
  dst.triangles.resize(triangles.size() * 3);
  for (size_t i = 0; i < triangles.size(); i++) {
    dst.triangles[3 * i + 0] = triangles[i][0];
    dst.triangles[3 * i + 1] = triangles[i][1];
    dst.triangles[3 * i + 2] = triangles[i][2];
  }
  if (!src.texCoordTriangles().empty()) {
    const auto& texCoordsTri = src.texCoordTriangles();
    dst.trianglesuv.resize(texCoordsTri.size() * 3);
    for (size_t i = 0; i < texCoordsTri.size(); i++) {
      dst.trianglesuv[3 * i + 0] = texCoordsTri[i][0];
      dst.trianglesuv[3 * i + 1] = texCoordsTri[i][1];
      dst.trianglesuv[3 * i + 2] = texCoordsTri[i][2];
    }
  }
  if (!src.normals().empty()) {
    const auto& normals = src.normals();
    dst.normals.resize(normals.size() * 3);
    for (size_t i = 0; i < normals.size(); i++) {
      dst.normals[3 * i + 0] = normals[i][0];
      dst.normals[3 * i + 1] = normals[i][1];
      dst.normals[3 * i + 2] = normals[i][2];
    }
  }
  if (!src.colours().empty()) {
    const auto& colours = src.colours();
    dst.colors.resize(colours.size() * 3);
    for (size_t i = 0; i < colours.size(); i++) {
      dst.colors[3 * i + 0] = colours[i][0];
      dst.colors[3 * i + 1] = colours[i][1];
      dst.colors[3 * i + 2] = colours[i][2];
    }
  }
}

template<typename T>
static void
convertToMesh(TriangleMesh<T>& mesh, const mm::Model& model) {
  auto& points = mesh.points();
  points.resize(model.vertices.size() / 3);
  for (size_t i = 0; i < points.size(); i++) {
    points[i][0] = model.vertices[3 * i + 0];
    points[i][1] = model.vertices[3 * i + 1];
    points[i][2] = model.vertices[3 * i + 2];
  }
  if (model.hasColors()) {
    auto& colours = mesh.colours();
    colours.resize(points.size());
    for (size_t i = 0; i < colours.size(); i++) {
      colours[i][0] = model.colors[3 * i + 0];
      colours[i][1] = model.colors[3 * i + 1];
      colours[i][2] = model.colors[3 * i + 2];
    }
  }
}

//============================================================================

void
convert(const Frame<uint8_t>& src, mm::Image& dst) {
  const int32_t width  = src.width();
  const int32_t height = src.height();
  dst.reset(width, height);
  for (int32_t v = 0; v < height; v++) {
    for (int32_t u = 0; u < width; u++) {
      dst.data[(v * width + u) * 3 + 2] = src.plane(0).get(v, u);
      dst.data[(v * width + u) * 3 + 1] = src.plane(1).get(v, u);
      dst.data[(v * width + u) * 3 + 0] = src.plane(2).get(v, u);
    }
  }
}

//============================================================================

void
convert(const mm::Image& src, Frame<uint8_t>& dst) {
  const int32_t width  = src.width;
  const int32_t height = src.height;
  dst.resize(width, height, ColourSpace::BGR444p);
  for (int32_t v = 0; v < height; v++) {
    for (int32_t u = 0; u < width; u++) {
      dst.plane(0).set(v, u, src.data[(v * width + u) * 3 + 2]);
      dst.plane(1).set(v, u, src.data[(v * width + u) * 3 + 1]);
      dst.plane(2).set(v, u, src.data[(v * width + u) * 3 + 0]);
    }
  }
}

//============================================================================

void
log(const std::string& str, const mm::Image& image) {
  printf("%s: %dx%d nbc = %d \n",
         str.c_str(),
         image.width,
         image.height,
         image.nbc);
  fflush(stdout);
  for (int c = 2; c >= 0; c--) {
    for (int v = 0; v < std::min(image.height, 4096); v++) {
      printf("%4d: ", v);
      for (int u = 0; u < std::min(image.width, 4096); u++) {
        printf("%2x ", image.data[(v * image.width + u) * image.nbc + c]);
      }
      printf("\n");
    }
    printf("\n");
  }
}

//============================================================================

void
VMCMetrics::compute(const Sequence&             sequence0,
                    const Sequence&             sequence1,
                    const VMCMetricsParameters& params) {
  if (!compare) { compare = std::make_shared<mm::Compare>(); }
  for (int32_t i = 0; i < sequence0.frameCount(); i++) {
    std::vector<Frame<uint8_t>> textureSeqA;
    std::vector<Frame<uint8_t>> textureSeqB;
    for (int texIdx = 0; texIdx < sequence0.attributes().size(); texIdx++)
      textureSeqA.push_back(sequence0.attributeFrame(texIdx, i));
    for (int texIdx = 0; texIdx < sequence1.attributes().size(); texIdx++)
      textureSeqB.push_back(sequence1.attributeFrame(texIdx, i));
    compute(
      sequence0.mesh(i), sequence1.mesh(i), textureSeqA, textureSeqB, params);
  }
}

//============================================================================

void
VMCMetrics::compute(const std::string              srcMesh,
                    const std::string              recMesh,
                    const std::vector<std::string> srcMap,
                    const std::vector<std::string> recMap,
                    const VMCMetricsParameters&    params) {
  if (!compare) { compare = std::make_shared<mm::Compare>(); }
  mm::Model srcModel;
  bool      loadSrcModel = mm::IO::_loadModel(srcMesh, srcModel);
  std::vector<mm::ImagePtr> srcImages;
  std::vector<mm::Image>    srcImagesArray;
  std::vector<std::string>  textureMapSrcUrls;
  if (srcMap.size() != 0) textureMapSrcUrls = srcMap;
  else textureMapSrcUrls = srcModel.textureMapUrls;
  bool loadSrcTextures = mm::IO::loadImages(textureMapSrcUrls, srcImages);
  if (loadSrcTextures) {
    srcImagesArray.resize(srcImages.size());
    for (int i = 0; i < srcImages.size(); i++)
      srcImagesArray[i] = *srcImages[i];
  }

  mm::Model recModel;
  bool      loadRecModel = mm::IO::_loadModel(recMesh, recModel);
  std::vector<mm::ImagePtr> recImages;
  std::vector<mm::Image>    recImagesArray;
  std::vector<std::string>  textureMapRecUrls;
  if (recMap.size() != 0) textureMapRecUrls = recMap;
  else textureMapRecUrls = recModel.textureMapUrls;
  bool loadRecTextures = mm::IO::loadImages(textureMapRecUrls, recImages);
  if (loadRecTextures) {
    recImagesArray.resize(recImages.size());
    for (int i = 0; i < recImages.size(); i++)
      recImagesArray[i] = *recImages[i];
  }

  if (loadSrcModel && loadSrcTextures && loadRecModel && loadRecTextures) {
    compute(
      srcModel, recModel, srcImagesArray, recImagesArray, "", "", params);
  } else {
    printf("Error: can't load models and maps. \n");
    exit(-1);
  }
}

//============================================================================

template<typename T>
void
VMCMetrics::compute(const TriangleMesh<T>&            srcMesh,
                    const TriangleMesh<T>&            recMesh,
                    const std::vector<Frame<uint8_t>> srcMaps,
                    const std::vector<Frame<uint8_t>> recMaps,
                    const VMCMetricsParameters&       params) {
  if (!compare) { compare = std::make_shared<mm::Compare>(); }
  mm::Model              srcModel;
  mm::Model              recModel;
  std::vector<mm::Image> srcImages;
  std::vector<mm::Image> recImages;
  convert(srcMesh, srcModel);
  convert(recMesh, recModel);
  for (int mapIdx = 0; mapIdx < srcMaps.size(); mapIdx++)
    convert(srcMaps[mapIdx], srcImages[mapIdx]);
  for (int mapIdx = 0; mapIdx < recMaps.size(); mapIdx++)
    convert(recMaps[mapIdx], recImages[mapIdx]);
  compute(srcModel, recModel, srcImages, recImages, "", "", params);
  print(compare->size() - 1);
}

//============================================================================

template<typename T>
void
VMCMetrics::computePerFace(const TriangleMesh<T>&            srcMesh,
                           const TriangleMesh<T>&            recMesh,
                           const std::vector<Frame<uint8_t>> srcMaps,
                           const std::vector<Frame<uint8_t>> recMaps,
                           const VMCMetricsParameters&       params,
                           std::vector<std::vector<double>>& metricsResults,
                           std::vector<Vec2<int>>& sampledPointCountList,
                           std::string             keepFilesPathPrefix,
                           bool                    keepIntermediateFiles) {
  mm::Model              srcModel;
  mm::Model              recModel;
  std::vector<mm::Image> srcImages;
  std::vector<mm::Image> recImages;

  srcImages.resize(srcMaps.size());
  recImages.resize(recMaps.size());

  convert(srcMesh, srcModel);
  for (int mapIdx = 0; mapIdx < srcMaps.size(); mapIdx++)
    convert(srcMaps[mapIdx], srcImages[mapIdx]);
  for (int mapIdx = 0; mapIdx < recMaps.size(); mapIdx++)
    convert(recMaps[mapIdx], recImages[mapIdx]);

  //metric per face
  Vec2<bool>                    sampleAB(true, true);  //{rec, org}
  Vec2<mm::Model>               sampledAB;
  Vec2<bool>                    dequantUvAB(false, true);   //{rec, org}
  Vec2<bool>                    reindexAB(false, true);     //{rec, org})
  Vec2<bool>                    removeDupAB(false, false);  //{rec, org}
  bool                          calcEachFace = false;
  std::vector<std::vector<int>> faceIndexPerPointAB;
  faceIndexPerPointAB.resize(2);
  TriangleMesh<MeshType> sampledAmeshAll;  //for debug
  //calc model to model and then pick up results per face
  compare = std::make_shared<mm::Compare>();
  convert(recMesh, recModel);
  std::vector<int> numPointPerFace(recModel.triangles.size() / 3, 0);
  Vec2<int>        sampledPointCount;
  //compare recModel and srcMdel
  compute(recModel,
          srcModel,
          recImages,
          srcImages,
          "",
          "",
          params,
          sampledAB,
          sampledPointCount,
          sampleAB,
          dequantUvAB,
          reindexAB,
          removeDupAB,
          faceIndexPerPointAB,
          true);
  std::vector<std::vector<double>> resultPerFace(recMesh.triangles().size());
  for (int i = 0; i < recMesh.triangles().size(); i++) {
    auto& res = resultPerFace[i];
    res.resize(10);
    for (auto& v : res) { v = 0.0; }
  }
  //calculate results per face
  auto      metricsResultsPerPoint = getPccResultsPerPointMse(0);
  auto      pointCount             = getPccResultsPerPointMse(0).size();
  glm::vec3 minPos(
    params.minPosition[0], params.minPosition[1], params.minPosition[2]);
  glm::vec3 maxPos(
    params.maxPosition[0], params.maxPosition[1], params.maxPosition[2]);
  glm::vec3 dim        = maxPos - minPos;
  float     resolution = resolution =
    std::max(dim.x, std::max(dim.y, dim.z));        // auto
  auto faceIndexPerPoint = faceIndexPerPointAB[0];  //rec
  for (long i = 0; i < pointCount; ++i) {
    auto& result     = metricsResultsPerPoint[i];
    int   idxRecFace = faceIndexPerPoint[i];
    numPointPerFace[idxRecFace]++;
    for (int j = 0; j < result.size(); j++) {
      resultPerFace[idxRecFace][j] += result[j];
    }
  }
  for (int i = 0; i < recMesh.triangles().size(); i++) {
    auto& res = resultPerFace[i];
    for (auto& v : res) { v /= numPointPerFace[i]; };
    //calc PSNR
    std::vector<double> resPSNR(res.size());
    resPSNR[0]        = getPSNR(res[0], resolution, 3);  //c2c_psnr
    resPSNR[1]        = getPSNR(res[1], resolution, 3);  //c2p_psnr
    resPSNR[2]        = getPSNR(res[2], 1.0);            //color_psnr[0]
    resPSNR[3]        = getPSNR(res[3], 1.0);            //color_psnr[1]
    resPSNR[4]        = getPSNR(res[4], 1.0);            //color_psnr[2]
    resPSNR[5]        = getPSNR(res[5], resolution, 3);  //c2c_hausdorff_psnr
    resPSNR[6]        = getPSNR(res[6], resolution, 3);  //c2p_hausdorff_psnr
    resPSNR[7]        = getPSNR(res[7], 255.0);  //color_rgb_hausdorff_psnr[0]
    resPSNR[8]        = getPSNR(res[8], 255.0);  //color_rgb_hausdorff_psnr[1]
    resPSNR[9]        = getPSNR(res[9], 255.0);  //color_rgb_hausdorff_psnr[2]
    metricsResults[i] = resPSNR;
    sampledPointCountList[i] = {numPointPerFace[i], sampledPointCount[1]};
  }
  print(compare->size() - 1);
}
//============================================================================

template<typename T>
void
VMCMetrics::compute(const TriangleMesh<T>&      srcMesh,
                    const TriangleMesh<T>&      recMesh,
                    const VMCMetricsParameters& params) {
  if (!compare) { compare = std::make_shared<mm::Compare>(); }
  mm::Model              srcModel;
  mm::Model              recModel;
  std::vector<mm::Image> imageList;
  mm::Image              image(10, 10, 0);
  imageList.push_back(image);
  convert(srcMesh, srcModel);
  convert(recMesh, recModel);
  compute(srcModel, recModel, imageList, imageList, "", "", params);
}

//============================================================================

void
VMCMetrics::compute(const mm::Model&             srcModel,
                    const mm::Model&             recModel,
                    const std::vector<mm::Image> srcMap,
                    const std::vector<mm::Image> recMap,
                    const std::string&           srcName,
                    const std::string&           recName,
                    const VMCMetricsParameters&  params) {
  mm::Model dequantize[2];
  mm::Model reindex[2];
  mm::Model sampled[2];

  std::vector<mm::ImagePtr> srcMapPtr;
  srcMapPtr.resize(srcMap.size());
  for (int i = 0; i < srcMap.size(); i++) {
    srcMapPtr[i]  = mm::ImagePtr(new mm::Image());
    *srcMapPtr[i] = srcMap[i];
  }
  std::vector<mm::ImagePtr> recMapPtr;
  recMapPtr.resize(recMap.size());
  for (int i = 0; i < recMap.size(); i++) {
    recMapPtr[i]  = mm::ImagePtr(new mm::Image());
    *recMapPtr[i] = recMap[i];
  }

  const bool verbose = params.verbose;
  // Sample
  glm::vec3 minPos(
    params.minPosition[0], params.minPosition[1], params.minPosition[2]);
  glm::vec3 maxPos(
    params.maxPosition[0], params.maxPosition[1], params.maxPosition[2]);
  for (size_t i = 0; i < 2; i++) {
    const auto& model = i == 0 ? srcModel : recModel;
    if (verbose) {
      std::cout << (i == 0 ? "Src" : "Rec")
                << " model: " << (i == 0 ? srcName : recName) << std::endl;
      std::cout << "  Vertices: " << model.vertices.size() / 3 << std::endl;
      std::cout << "  UVs: " << model.uvcoords.size() / 2 << std::endl;
      std::cout << "  Colors: " << model.colors.size() / 3 << std::endl;
      std::cout << "  Normals: " << model.normals.size() / 3 << std::endl;
      std::cout << "  Triangles: " << model.triangles.size() / 3 << std::endl;
      std::cout << "  Trianglesuv: " << model.trianglesuv.size() / 3
                << std::endl;
    }
    // Dequantize
    glm::vec2 zero2(0, 0);
    glm::vec2 one2(1, 1);
    glm::vec3 zero3(0, 0, 0);

    auto qt = i == 0 || !params.dequantizeUV ? params.qt : 0;
    if (verbose) {
      std::cout << "De-quantizing" << std::endl;
      std::cout << "  qp = " << params.qp << std::endl;
      std::cout << "  qt = " << qt << std::endl;
      std::cout << "  qn = " << 0 << std::endl;
      std::cout << "  qc = " << 0 << std::endl;
    }
    mm::Dequantize::dequantize(model,          // input
                               dequantize[i],  // output
                               params.qp,      // qp
                               qt,             // qt,
                               0,              // qn,
                               0,              // qc,
                               minPos,         // minPos
                               maxPos,         // maxPos
                               zero2,          // minUv
                               one2,           // maxUv
                               zero3,          // minNrm
                               zero3,          // maxNrm
                               zero3,          // minCol
                               zero3,          // maxCol
                               true,           // useFixedPoint,
                               false,          // colorSpaceConversion
                               verbose);       // verbose

    if (params.computePcc || params.computePcqm) {
      // Reindex
      const std::string sort = "oriented";
      if (verbose) {
        std::cout << "Reindex" << std::endl;
        std::cout << "  sort = " << sort << std::endl;
      }
      if (sort == "none") {
        mm::reindex(dequantize[i], reindex[i]);
      } else if (sort == "vertex" || sort == "oriented"
                 || sort == "unoriented") {
        mm::reorder(dequantize[i], sort, reindex[i]);
      } else {
        std::cout << "Error: invalid sorting method " << sort << std::endl;
      }

      const bool   useNormal     = true;
      const bool   bilinear      = true;
      const bool   hideProgress  = true;
      const size_t nbSamplesMin  = 0;
      const size_t nbSamplesMax  = 0;
      const size_t maxIterations = 10;
      const bool   useFixedPoint = true;
      if (verbose) {
        std::cout << "Sampling in GRID mode" << std::endl;
        std::cout << "  Grid Size = " << params.gridSize << std::endl;
        std::cout << "  Use Normal = " << useNormal << std::endl;
        std::cout << "  Bilinear = " << bilinear << std::endl;
        std::cout << "  hideProgress = " << hideProgress << std::endl;
        std::cout << "  nbSamplesMin = " << nbSamplesMin << std::endl;
        std::cout << "  nbSamplesMax = " << nbSamplesMax << std::endl;
        std::cout << "  maxIterations = " << maxIterations << std::endl;
        std::cout << "  using contrained mode with gridSize " << std::endl;
      }
      mm::Sample::meshToPcGrid(reindex[i],                      // input
                               sampled[i],                      // output
                               i == 0 ? srcMapPtr : recMapPtr,  // map
                               params.gridSize,                 // grid res
                               bilinear,                        // bilinear
                               !hideProgress,                   // lowprogress
                               useNormal,                       // use normal
                               useFixedPoint,  // useFixedPoint
                               minPos,         // minPos
                               maxPos,         // maxPos
                               verbose);       // verbose
    }
  }

  // PCC
  mm::Model               outPcc[2];
  pcc_quality::commandPar pccParams;
  glm::vec3               dim = maxPos - minPos;
  pccParams.singlePass        = false;
  pccParams.hausdorff         = false;
  pccParams.bColor            = true;
  pccParams.bLidar            = false;  // always false, no option
  pccParams.resolution      = std::max(dim.x, std::max(dim.y, dim.z));  // auto
  pccParams.neighborsProc   = 1;
  pccParams.dropDuplicates  = 2;
  pccParams.bAverageNormals = true;
  pccParams.normalCalcModificationEnable = params.normalCalcModificationEnable;

  if (params.computePcc) {
    if (verbose) {
      std::cout << "Compare models using MPEG PCC distortion metric"
                << std::endl;
      std::cout << "  singlePass     = " << pccParams.singlePass << std::endl;
      std::cout << "  hausdorff      = " << pccParams.hausdorff << std::endl;
      std::cout << "  color          = " << pccParams.bColor << std::endl;
      std::cout << "  resolution     = " << pccParams.resolution << std::endl;
      std::cout << "  neighborsProc  = " << pccParams.neighborsProc
                << std::endl;
      std::cout << "  dropDuplicates = " << pccParams.dropDuplicates
                << std::endl;
      std::cout << "  averageNormals = " << pccParams.bAverageNormals
                << std::endl;
    }
    compare->pcc(sampled[0],  // modelA
                 sampled[1],  // modelB
                 srcMapPtr,   // mapA
                 recMapPtr,   // mapB
                 pccParams,   // params
                 outPcc[0],   // outputA
                 outPcc[1],   // outputB
                 verbose);    // verbose
  }

  // PCQM
  if (params.computePcqm) {
    mm::ModelPtr pcqmModelA(new mm::Model());
    *pcqmModelA = sampled[0];
    mm::ModelPtr pcqmModelB(new mm::Model());
    *pcqmModelB = sampled[1];
    mm::ModelPtr outAPcqm(new mm::Model());
    mm::ModelPtr outBPcqm(new mm::Model());

    compare->pcqm(pcqmModelA,                     // modelA
                  pcqmModelB,                     // modelB
                  srcMapPtr,                      // mapA
                  recMapPtr,                      // mapB
                  params.pcqmRadiusCurvature,     // radiusCurvature
                  params.pcqmThresholdKnnSearch,  // thresholdKnnSearch
                  params.pcqmRadiusFactor,        // radiusFactor
                  outAPcqm,                       // outputA
                  outBPcqm,                       // outputB
                  verbose);                       // verbose
  }

  // IBSM
  if (params.computeIbsm) {
    mm::ModelPtr ibsmModelA(new mm::Model());
    *ibsmModelA = dequantize[0];
    mm::ModelPtr ibsmModelB;
    *ibsmModelB = dequantize[1];
    mm::ModelPtr       outAIbsm(new mm::Model());
    mm::ModelPtr       outBIbsm(new mm::Model());
    const unsigned int ibsmResolution   = 2048;
    const unsigned int ibsmCameraCount  = 16;
    const glm::vec3    ibsmCamRotParams = {0.0F, 0.0F, 0.0F};
    const std::string  ibsmRenderer     = "sw_raster";
    const std::string  ibsmOutputPrefix;
    const bool         ibsmDisableReordering = false;
    const bool         ibsmDisableCulling    = false;
    if (verbose) {
      std::cout << "Compare models using IBSM distortion metric" << std::endl;
      std::cout << "  ibsmRenderer =     " << ibsmRenderer << std::endl;
      std::cout << "  ibsmCameraCount    = " << ibsmCameraCount << std::endl;
      std::cout << "  ibsmCameraRotation = " << ibsmCamRotParams[0] << " "
                << ibsmCamRotParams[1] << " " << ibsmCamRotParams[2] << " "
                << std::endl;
      std::cout << "  ibsmResolution     = " << ibsmResolution << std::endl;
      std::cout << "  ibsmDisableCulling = " << ibsmDisableCulling
                << std::endl;
      std::cout << "  ibsmOutputPrefix   = "
                << "" << std::endl;
    }
    compare->ibsm(ibsmModelA,             // modelA
                  ibsmModelB,             // modelB
                  srcMapPtr,              // mapA
                  recMapPtr,              // mapB
                  ibsmDisableReordering,  // disableReordering
                  ibsmResolution,         // resolution
                  ibsmCameraCount,        // cameraCount
                  ibsmCamRotParams,       // camRotParams
                  ibsmRenderer,           // renderer
                  ibsmOutputPrefix,       // outputPrefix
                  ibsmDisableCulling,     // disableCulling
                  outAIbsm,               // outputA
                  outBIbsm,               // outputB
                  verbose);               // verbose
  }
}

void
VMCMetrics::compute(const mm::Model&               srcModel,
                    const mm::Model&               recModel,
                    const std::vector<mm::Image>   srcMap,
                    const std::vector<mm::Image>   recMap,
                    const std::string&             srcName,
                    const std::string&             recName,
                    const VMCMetricsParameters&    params,
                    Vec2<mm::Model>&               sampledAB,
                    Vec2<int>&                     sampledPointCount,
                    Vec2<bool>&                    sampleAB,
                    Vec2<bool>&                    dequantUvAB,
                    Vec2<bool>&                    reindexAB,
                    Vec2<bool>&                    removeDupAB,
                    std::vector<std::vector<int>>& faceIndexPerPointAB,
                    const bool                     calcMetPerPoint) {
  mm::Model dequantize[2];
  mm::Model reindex[2];
  mm::Model sampled[2];

  std::vector<mm::ImagePtr> srcMapPtr;
  srcMapPtr.resize(srcMap.size());
  for (int i = 0; i < srcMap.size(); i++) {
    srcMapPtr[i]  = mm::ImagePtr(new mm::Image());
    *srcMapPtr[i] = srcMap[i];
  }
  std::vector<mm::ImagePtr> recMapPtr;
  recMapPtr.resize(recMap.size());
  for (int i = 0; i < recMap.size(); i++) {
    recMapPtr[i]  = mm::ImagePtr(new mm::Image());
    *recMapPtr[i] = recMap[i];
  }

  const bool verbose = params.verbose;
  // Sample
  glm::vec3 minPos(
    params.minPosition[0], params.minPosition[1], params.minPosition[2]);
  glm::vec3 maxPos(
    params.maxPosition[0], params.maxPosition[1], params.maxPosition[2]);
  for (size_t i = 0; i < 2; i++) {
    if (!sampleAB[i]) {
      sampled[i] = sampledAB[i];
    } else {
      const auto& model = i == 0 ? srcModel : recModel;
      if (verbose) {
        std::cout << (i == 0 ? "Src" : "Rec")
                  << " model: " << (i == 0 ? srcName : recName) << std::endl;
        std::cout << "  Vertices: " << model.vertices.size() / 3 << std::endl;
        std::cout << "  UVs: " << model.uvcoords.size() / 2 << std::endl;
        std::cout << "  Colors: " << model.colors.size() / 3 << std::endl;
        std::cout << "  Normals: " << model.normals.size() / 3 << std::endl;
        std::cout << "  Triangles: " << model.triangles.size() / 3
                  << std::endl;
        std::cout << "  Trianglesuv: " << model.trianglesuv.size() / 3
                  << std::endl;
      }
      // Dequantize
      glm::vec2 zero2(0, 0);
      glm::vec2 one2(1, 1);
      glm::vec3 zero3(0, 0, 0);

      auto qt = dequantUvAB[i] == 1 || !params.dequantizeUV ? params.qt : 0;
      if (verbose) {
        std::cout << "De-quantizing" << std::endl;
        std::cout << "  qp = " << params.qp << std::endl;
        std::cout << "  qt = " << qt << std::endl;
        std::cout << "  qn = " << 0 << std::endl;
        std::cout << "  qc = " << 0 << std::endl;
      }
      mm::Dequantize::dequantize(model,          // input
                                 dequantize[i],  // output
                                 params.qp,      // qp
                                 qt,             // qt,
                                 0,              // qn,
                                 0,              // qc,
                                 minPos,         // minPos
                                 maxPos,         // maxPos
                                 zero2,          // minUv
                                 one2,           // maxUv
                                 zero3,          // minNrm
                                 zero3,          // maxNrm
                                 zero3,          // minCol
                                 zero3,          // maxCol
                                 true,           // useFixedPoint,
                                 false,          // colorSpaceConversion
                                 verbose);       // verbose

      if (params.computePcc || params.computePcqm) {
        // Reindex
        if (reindexAB[i]) {
          const std::string sort = "oriented";
          if (verbose) {
            std::cout << "Reindex" << std::endl;
            std::cout << "  sort = " << sort << std::endl;
          }
          if (sort == "none") {
            mm::reindex(dequantize[i], reindex[i]);
          } else if (sort == "vertex" || sort == "oriented"
                     || sort == "unoriented") {
            mm::reorder(dequantize[i], sort, reindex[i]);
          } else {
            std::cout << "Error: invalid sorting method " << sort << std::endl;
          }
        } else {
          reindex[i] = dequantize[i];
          reindex[i].triangleMatIdx.resize(reindex[i].triangles.size() / 3);
        }

        const bool   useNormal     = true;
        const bool   bilinear      = true;
        const bool   hideProgress  = true;
        const size_t nbSamplesMin  = 0;
        const size_t nbSamplesMax  = 0;
        const size_t maxIterations = 10;
        const bool   useFixedPoint = true;
        if (verbose) {
          std::cout << "Sampling in GRID mode" << std::endl;
          std::cout << "  Grid Size = " << params.gridSize << std::endl;
          std::cout << "  Use Normal = " << useNormal << std::endl;
          std::cout << "  Bilinear = " << bilinear << std::endl;
          std::cout << "  hideProgress = " << hideProgress << std::endl;
          std::cout << "  nbSamplesMin = " << nbSamplesMin << std::endl;
          std::cout << "  nbSamplesMax = " << nbSamplesMax << std::endl;
          std::cout << "  maxIterations = " << maxIterations << std::endl;
          std::cout << "  using contrained mode with gridSize " << std::endl;
        }
        if (calcMetPerPoint) {
          mm::Sample::meshToPcGrid(reindex[i],                      // input
                                   sampled[i],                      // output
                                   i == 0 ? srcMapPtr : recMapPtr,  // map
                                   params.gridSize,                 // grid res
                                   bilinear,                        // bilinear
                                   !hideProgress,  // lowprogress
                                   useNormal,      // use normal
                                   useFixedPoint,  // useFixedPoint
                                   minPos,         // minPos
                                   maxPos,         // maxPos
                                   verbose,        // verbose
                                   &faceIndexPerPointAB[i]);
        } else {
          mm::Sample::meshToPcGrid(reindex[i],                      // input
                                   sampled[i],                      // output
                                   i == 0 ? srcMapPtr : recMapPtr,  // map
                                   params.gridSize,                 // grid res
                                   bilinear,                        // bilinear
                                   !hideProgress,  // lowprogress
                                   useNormal,      // use normal
                                   useFixedPoint,  // useFixedPoint
                                   minPos,         // minPos
                                   maxPos,         // maxPos
                                   verbose);       // verbose
        }
      }
      if (sampleAB[i]) { sampledAB[i] = sampled[i]; }
    }
    sampledPointCount[i] = sampled[i].vertices.size() / 3;
  }

  // PCC
  mm::Model               outPcc[2];
  pcc_quality::commandPar pccParams;
  glm::vec3               dim = maxPos - minPos;
  //pccParams.singlePass = false;
  pccParams.singlePass      = true;  //only A(recFace)->B(org)
  pccParams.hausdorff       = false;
  pccParams.bColor          = true;
  pccParams.bLidar          = false;  // always false, no option
  pccParams.resolution      = std::max(dim.x, std::max(dim.y, dim.z));  // auto
  pccParams.neighborsProc   = 1;
  pccParams.dropDuplicates  = 2;
  pccParams.bAverageNormals = true;
  pccParams.normalCalcModificationEnable = params.normalCalcModificationEnable;

  if (params.computePcc) {
    if (verbose) {
      std::cout << "Compare models using MPEG PCC distortion metric"
                << std::endl;
      std::cout << "  singlePass     = " << pccParams.singlePass << std::endl;
      std::cout << "  hausdorff      = " << pccParams.hausdorff << std::endl;
      std::cout << "  color          = " << pccParams.bColor << std::endl;
      std::cout << "  resolution     = " << pccParams.resolution << std::endl;
      std::cout << "  neighborsProc  = " << pccParams.neighborsProc
                << std::endl;
      std::cout << "  dropDuplicates = " << pccParams.dropDuplicates
                << std::endl;
      std::cout << "  averageNormals = " << pccParams.bAverageNormals
                << std::endl;
    }
    compare->pcc(sampled[0],        // modelA
                 sampled[1],        // modelB
                 srcMapPtr,         // mapA
                 recMapPtr,         // mapB
                 pccParams,         // params
                 outPcc[0],         // outputA
                 outPcc[1],         // outputB
                 verbose,           // verbose
                 removeDupAB[0],    // removeDupA
                 removeDupAB[1],    // removeDupB
                 calcMetPerPoint);  //calcMetPerPoint
  }

  // PCQM
  if (params.computePcqm) {
    mm::ModelPtr pcqmModelA(new mm::Model());
    *pcqmModelA = sampled[0];
    mm::ModelPtr pcqmModelB(new mm::Model());
    *pcqmModelB = sampled[1];
    mm::ModelPtr outAPcqm(new mm::Model());
    mm::ModelPtr outBPcqm(new mm::Model());

    compare->pcqm(pcqmModelA,                     // modelA
                  pcqmModelB,                     // modelB
                  srcMapPtr,                      // mapA
                  recMapPtr,                      // mapB
                  params.pcqmRadiusCurvature,     // radiusCurvature
                  params.pcqmThresholdKnnSearch,  // thresholdKnnSearch
                  params.pcqmRadiusFactor,        // radiusFactor
                  outAPcqm,                       // outputA
                  outBPcqm,                       // outputB
                  verbose);                       // verbose
  }

  // IBSM
  if (params.computeIbsm) {
    mm::ModelPtr ibsmModelA(new mm::Model());
    *ibsmModelA = dequantize[0];
    mm::ModelPtr ibsmModelB;
    *ibsmModelB = dequantize[1];
    mm::ModelPtr       outAIbsm(new mm::Model());
    mm::ModelPtr       outBIbsm(new mm::Model());
    const unsigned int ibsmResolution   = 2048;
    const unsigned int ibsmCameraCount  = 16;
    const glm::vec3    ibsmCamRotParams = {0.0F, 0.0F, 0.0F};
    const std::string  ibsmRenderer     = "sw_raster";
    const std::string  ibsmOutputPrefix;
    const bool         ibsmDisableReordering = false;
    const bool         ibsmDisableCulling    = false;
    if (verbose) {
      std::cout << "Compare models using IBSM distortion metric" << std::endl;
      std::cout << "  ibsmRenderer =     " << ibsmRenderer << std::endl;
      std::cout << "  ibsmCameraCount    = " << ibsmCameraCount << std::endl;
      std::cout << "  ibsmCameraRotation = " << ibsmCamRotParams[0] << " "
                << ibsmCamRotParams[1] << " " << ibsmCamRotParams[2] << " "
                << std::endl;
      std::cout << "  ibsmResolution     = " << ibsmResolution << std::endl;
      std::cout << "  ibsmDisableCulling = " << ibsmDisableCulling
                << std::endl;
      std::cout << "  ibsmOutputPrefix   = "
                << "" << std::endl;
    }
    compare->ibsm(ibsmModelA,             // modelA
                  ibsmModelB,             // modelB
                  srcMapPtr,              // mapA
                  recMapPtr,              // mapB
                  ibsmDisableReordering,  // disableReordering
                  ibsmResolution,         // resolution
                  ibsmCameraCount,        // cameraCount
                  ibsmCamRotParams,       // camRotParams
                  ibsmRenderer,           // renderer
                  ibsmOutputPrefix,       // outputPrefix
                  ibsmDisableCulling,     // disableCulling
                  outAIbsm,               // outputA
                  outBIbsm,               // outputB
                  verbose);               // verbose
  }
}

//============================================================================

// void
// print(double value) {
//   if (value > 999.98) std::cout << "999.99 ";
//   else std::cout << value << ' ';
// }
void
VMCMetrics::print(int32_t frameIndex) {
  auto print = [](const std::vector<double> pcc,
                  const std::vector<double> ibsm,
                  const std::string         str) {
    std::cout << std::left << std::setw(25) << str << ": ";
    std::cout << std::right << std::setw(12) << pcc[0] << ' ';
    std::cout << std::right << std::setw(12) << pcc[1] << ' ';
    std::cout << std::right << std::setw(12) << pcc[2] << ' ';
    std::cout << std::right << std::setw(12) << pcc[3] << ' ';
    std::cout << std::right << std::setw(12) << pcc[4] << ' ';
    std::cout << std::right << std::setw(12) << ibsm[6] << ' ';
    std::cout << std::right << std::setw(12) << ibsm[2] << ' ';
    std::cout << std::left << '\n';
  };
  if (frameIndex >= 0) {
    print(compare->getPccResults(frameIndex),
          compare->getIbsmResults(frameIndex),
          "Metric_frame_" + std::to_string(frameIndex));
  } else {
    print(compare->getFinalPccResults(),
          compare->getFinalIbsmResults(),
          "Metric_results");
  }
}

void
VMCMetrics::display(bool verbose) {
  if (verbose) {
    printf("Metrics results :\n");
    printf("PCC:\n");
    compare->pccFinalize();
    printf("PCQM:\n");
    compare->pcqmFinalize();
    printf("IBSM:\n");
    compare->ibsmFinalize();
  } else {
    for (size_t i = 0; i < compare->size(); i++) { print(i); }
    print();
  }
}

//============================================================================

std::vector<double>
VMCMetrics::getPccResults() {
  return compare->getFinalPccResults();
}

std::vector<std::vector<double>>
VMCMetrics::getPccResultsPerPoint(const int abIndex) {
  return compare->getPccResultsPerPoint(abIndex);
}

std::vector<std::vector<double>>
VMCMetrics::getPccResultsPerPointMse(const int abIndex) {
  return compare->getPccResultsPerPointMse(abIndex);
}

//============================================================================

std::vector<double>
VMCMetrics::getPcqmResults() {
  return compare->getFinalPcqmResults();
}

//============================================================================

std::vector<double>
VMCMetrics::getIbsmResults() {
  return compare->getFinalIbsmResults();
}

//============================================================================

template void convert<float>(const TriangleMesh<float>&, mm::Model&);
template void convert<double>(const TriangleMesh<double>&, mm::Model&);

template void VMCMetrics::compute<float>(const TriangleMesh<float>&,
                                         const TriangleMesh<float>&,
                                         const std::vector<Frame<uint8_t>>,
                                         const std::vector<Frame<uint8_t>>,
                                         const VMCMetricsParameters&);

template void VMCMetrics::compute<double>(const TriangleMesh<double>&,
                                          const TriangleMesh<double>&,
                                          const std::vector<Frame<uint8_t>>,
                                          const std::vector<Frame<uint8_t>>,
                                          const VMCMetricsParameters&);

template void VMCMetrics::compute<float>(const TriangleMesh<float>&,
                                         const TriangleMesh<float>&,
                                         const VMCMetricsParameters&);

template void VMCMetrics::compute<double>(const TriangleMesh<double>&,
                                          const TriangleMesh<double>&,
                                          const VMCMetricsParameters&);

template void
VMCMetrics::computePerFace<float>(const TriangleMesh<float>&,
                                  const TriangleMesh<float>&,
                                  const std::vector<Frame<uint8_t>>,
                                  const std::vector<Frame<uint8_t>>,
                                  const VMCMetricsParameters&,
                                  std::vector<std::vector<double>>&,
                                  std::vector<Vec2<int>>&,
                                  std::string keepFilesPathPrefix,
                                  bool        keepIntermediateFiles);

template void
VMCMetrics::computePerFace<double>(const TriangleMesh<double>&,
                                   const TriangleMesh<double>&,
                                   const std::vector<Frame<uint8_t>>,
                                   const std::vector<Frame<uint8_t>>,
                                   const VMCMetricsParameters&,
                                   std::vector<std::vector<double>>&,
                                   std::vector<Vec2<int>>&,
                                   std::string keepFilesPathPrefix,
                                   bool        keepIntermediateFiles);

//============================================================================
