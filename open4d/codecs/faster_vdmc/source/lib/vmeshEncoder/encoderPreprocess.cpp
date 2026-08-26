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

#include "encoder.hpp"
#include <cmath>
#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "entropy.hpp"
#include "vmc.hpp"
#include "metrics.hpp"
#include "geometryDecimate.hpp"
#include "textureParametrization.hpp"
#include "geometryParametrization.hpp"
#include "util/misc.hpp"
#include "baseMeshTracking.hpp"

namespace vmesh {

//----------------------------------------------------------------------------

static bool
saveCache(const VMCEncoderParameters&   params,
          const TriangleMesh<MeshType>& mesh,
          const std::string             name,
          const int32_t                 gofIndex,
          const int32_t                 frameIndex) {
  if (params.cachingDirectory.empty()) return false;
  return mesh.save(params.cachingDirectory + separator() + name
                   + expandNum("_gof%02d", gofIndex)
                   + expandNum("_fr%04d.vmb", frameIndex));
}

//----------------------------------------------------------------------------

static bool
loadCache(const VMCEncoderParameters& params,
          TriangleMesh<MeshType>&     mesh,
          const std::string           name,
          const int32_t               gofIndex,
          const int32_t               frameIndex) {
  mesh.clear();
  if (params.cachingDirectory.empty()) return false;
  return mesh.load(params.cachingDirectory + separator() + name
                   + expandNum("_gof%02d", gofIndex)
                   + expandNum("_fr%04d.vmb", frameIndex));
}

//============================================================================

void
VMCEncoder::unifyVertices(const VMCGroupOfFramesInfo& gofInfo,
                          std::vector<VMCSubmesh>&    gof,
                          const VMCEncoderParameters& params) {
  const auto                        startFrame = gofInfo.startFrameIndex_;
  const auto                        frameCount = gofInfo.frameCount_;
  const auto                        lastFrame  = startFrame + frameCount - 1;
  std::vector<std::vector<int32_t>> umappings(frameCount);
  for (int f = startFrame; f <= lastFrame; ++f) {
    const auto findex = f - startFrame;
    auto&      frame  = gof[findex];
    if (params.subdivIsBase
        || (!params.deformForRecBase && !params.subdivIsBase)) {
      frame.reference.clear();
    }
    frame.mapped.clear();
    frame.decimateTexture.clear();
    if (params.unifyVertices && params.subdivisionIterationCount == 0) {
      auto& base   = frame.base;
      auto& subdiv = frame.subdiv;
      assert(base.pointCount() == subdiv.pointCount());
      assert(base.triangleCount() == subdiv.triangleCount());
      assert(base.texCoordCount() == subdiv.texCoordCount());
      assert(base.texCoordTriangleCount() == subdiv.texCoordTriangleCount());
      std::vector<Vec3<MeshType>> upoints;
      std::vector<Triangle>       utriangles;
      const auto                  pointCount0 = base.pointCount();
      const auto&                 frameInfo   = gofInfo.frameInfo(findex);
      auto&                       umapping    = umappings[findex];
      if (frameInfo.type == basemesh::I_BASEMESH) {
        printf("Frame %2d: INTRA \n", findex);
        UnifyVertices(
          base.points(), base.triangles(), upoints, utriangles, umapping);
        std::swap(base.points(), upoints);
        std::swap(base.triangles(), utriangles);
        RemoveDegeneratedTriangles(base);
        const auto pointCount1 = base.pointCount();
        assert(pointCount1 <= pointCount0);
        upoints.resize(pointCount1);
        for (int32_t vindex = 0; vindex < pointCount0; ++vindex) {
          upoints[umapping[vindex]] = subdiv.point(vindex);
        }
        std::swap(subdiv.points(), upoints);
        subdiv.triangles()         = base.triangles();
        subdiv.texCoordTriangles() = base.texCoordTriangles();
      } else {
        printf("Frame %2d: INTER: Index = %2d ref = %2d Previous = %2d \n",
               findex,
               frameInfo.frameIndex,
               frameInfo.referenceFrameIndex,
               frameInfo.previousFrameIndex);
        umapping             = umappings[frameInfo.previousFrameIndex];
        const auto& refFrame = gof[frameInfo.previousFrameIndex];
        upoints              = base.points();
        base                 = refFrame.base;
        for (int32_t vindex = 0; vindex < pointCount0; ++vindex) {
          base.setPoint(umapping[vindex], upoints[vindex]);
        }
        upoints = subdiv.points();
        subdiv  = refFrame.subdiv;
        for (int32_t vindex = 0; vindex < pointCount0; ++vindex) {
          subdiv.setPoint(umapping[vindex], upoints[vindex]);
        }
      }
      // Save intermediate files
      if (params.keepIntermediateFiles) {
        const auto prefix =
          _keepFilesPathPrefix + "fr_" + std::to_string(findex);
        frame.base.save(prefix + "_base_uni.ply");
        frame.subdiv.save(prefix + "_subdiv_uni.ply");
      }
    }
  }
}

//----------------------------------------------------------------------------

void
VMCEncoder::decimateInput(const TriangleMesh<MeshType>& input,
                          int32_t                       submeshIndex,
                          int32_t                       frameIndex,
                          VMCSubmesh&                   frame,
                          TriangleMesh<MeshType>&       decimate,
                          const VMCEncoderParameters&   params) {
  std::cout << "**********Simplify  submesh [ " << submeshIndex
            << " ] Frame [ " << frame.frameIndex << " ]\n";
  const auto index  = frame.frameIndex;
  auto       prefix = _keepFilesPathPrefix + "fr_" + std::to_string(index);
  // Load cache files
  bool skipSimplify = false;
  if (params.cachingPoint >= CachingPoint::SIMPLIFY) {
    if (loadCache(params, frame.mapped, "mapped", _gofIndex, index)
        && loadCache(params, frame.reference, "reference", _gofIndex, index)
        && loadCache(params, decimate, "decimate", _gofIndex, index)) {
      skipSimplify = true;
    }
  }
  printf("skipSimplify = %d \n", skipSimplify);
  fflush(stdout);
  if (!skipSimplify) {
    // Save intermediate files
    if (params.keepPreMesh) { input.save(prefix + "_simp_input.ply"); }
    // Create mapped, reference and decimate
    GeometryDecimate geometryDecimate;
    if (geometryDecimate.generate(
          input, frame.reference, decimate, frame.mapped, params)) {
      std::cerr << "Error: can't simplify mesh !\n";
      exit(-1);
    }

    // Save intermediate files
    if (params.keepPreMesh) {
      frame.mapped.save(prefix + "_mapped.ply");
      frame.reference.save(prefix + "_reference.ply");
      decimate.save(prefix + "_decimate.ply");
    }

    // Save cache files
    if (params.cachingPoint >= CachingPoint::SIMPLIFY) {
      saveCache(params, frame.mapped, "mapped", _gofIndex, index);
      saveCache(params, frame.reference, "reference", _gofIndex, index);
      saveCache(params, decimate, "decimate", _gofIndex, index);
    }
  }
}
//============================================================================

void
VMCEncoder::removeDegeneratedTrianglesCrossProduct(
  TriangleMesh<MeshType>& mesh,
  std::vector<int32_t>*   originalTriangleIndices,
  bool&                    trackFlag,
  const int32_t&          frameIndex) {
  TriangleMesh<MeshType> degenerateMesh;

  auto& degenerateTriangles         = degenerateMesh.triangles();
  auto& degenerateTexCoordTriangles = degenerateMesh.texCoordTriangles();
  auto& degenerateNormalTriangles   = degenerateMesh.normalTriangles();
  degenerateMesh.points().resize(mesh.pointCount());

  auto triangleCount = mesh.triangleCount();
  if (triangleCount <= 0) { return; }

  const auto hasTexCoords = mesh.texCoordTriangleCount() == triangleCount;
  const auto hasNormals   = mesh.normalTriangleCount() == triangleCount;

  std::vector<int32_t> newOriginalTriangleIndices;
  if (trackFlag) {
    originalTriangleIndices->reserve(triangleCount);
    newOriginalTriangleIndices.reserve(triangleCount);
  }
  std::vector<Triangle> triangles;
  triangles.reserve(triangleCount);

  int32_t removedDegeneratedTrianglesArea0 = 0;

  for (int32_t tindex = 0; tindex < triangleCount; tindex++) {
    const auto& tri   = mesh.triangle(tindex);
    const auto& coord = mesh.points();

    const auto i = tri[0];
    const auto j = tri[1];
    const auto k = tri[2];
    auto       area =
      computeTriangleArea(coord[tri[0]], coord[tri[1]], coord[tri[2]]);

    if (area > 0) {
      if (trackFlag) { originalTriangleIndices->push_back(tindex); }
      triangles.push_back(tri);
    } else {
      std::cout << "Triangle with area 0 is removed." << std::endl;
      removedDegeneratedTrianglesArea0++;
      degenerateTriangles.push_back(tri);
    }
  }

  std::cout << "frame : " << frameIndex
            << " , removedDegeneratedTrianglesArea0count = "
            << removedDegeneratedTrianglesArea0 << std::endl;

  std::swap(mesh.triangles(), triangles);

  if (!removedDegeneratedTrianglesArea0) trackFlag = false;

  //Change connectivity
  if (1) {
    triangles.clear();

    triangleCount                = mesh.triangleCount();
    auto degenerateTriangleCount = degenerateMesh.triangleCount();

    std::vector<int32_t> vertexID0s(degenerateTriangleCount),
      vertexID1s(degenerateTriangleCount), vertexID2s(degenerateTriangleCount);

    for (uint32_t degetindex = 0; degetindex < degenerateTriangleCount;
         degetindex++) {
      auto& degetri = degenerateTriangles[degetindex];

      const auto& coord = mesh.points();

      //Check middle point
      auto                doubleMax = std::numeric_limits<double>::max();
      auto                doubleMin = std::numeric_limits<double>::min();
      std::vector<double> minV      = {doubleMax, doubleMax, doubleMax};
      std::vector<double> maxV      = {doubleMin, doubleMin, doubleMin};

      std::vector<int> minID = {0, 0, 0}, maxID = {0, 0, 0};

      for (int vertexID = 0; vertexID < 3; vertexID++) {
        auto& p = coord[degetri[vertexID]];

        //xyz
        for (int i = 0; i < 3; i++) {
          if (p[i] < minV[i]) {
            minV[i]  = p[i];
            minID[i] = vertexID;
          }
          if (p[i] > maxV[i]) {
            maxV[i]  = p[i];
            maxID[i] = vertexID;
          }
        }
      }

      int middleID = 0;
      for (int vertexID = 0; vertexID < 3; vertexID++) {
        //xyz
        bool flag = false;
        for (int i = 0; i < 3; i++) {
          if (minID[i] == vertexID || maxID[i] == vertexID) { flag = 1; }
        }
        if (!flag) {
          middleID = vertexID;
          break;
        }
      }

      auto& vertexID0 = vertexID0s[degetindex];
      auto& vertexID1 = vertexID1s[degetindex];
      auto& vertexID2 = vertexID2s[degetindex];

      if (middleID == 0) {
        vertexID0 = degetri[0];
        vertexID1 = degetri[1];
        vertexID2 = degetri[2];
      } else if (middleID == 1) {
        vertexID0 = degetri[1];
        vertexID1 = degetri[2];
        vertexID2 = degetri[0];
      } else if (middleID == 2) {
        vertexID0 = degetri[2];
        vertexID1 = degetri[0];
        vertexID2 = degetri[1];
      }
    }

    for (int32_t tindex = 0; tindex < triangleCount; tindex++) {
      const auto& tri   = mesh.triangle(tindex);
      const auto& coord = mesh.points();

      bool      addedFlag = false;
      Vec3<int> newFace1, newFace2;

      for (uint32_t degetindex = 0;
           !addedFlag && (degetindex < degenerateTriangleCount);
           degetindex++) {
        auto& vertexID0 = vertexID0s[degetindex];
        auto& vertexID1 = vertexID1s[degetindex];
        auto& vertexID2 = vertexID2s[degetindex];

        std::vector<Vec3<int>> vIDs{{tri[0], tri[1], tri[2]},
                                    {tri[1], tri[2], tri[0]},
                                    {tri[2], tri[0], tri[1]}};
        for (auto& vID : vIDs) {
          auto& vID0 = vID[0];
          auto& vID1 = vID[1];
          auto& vID2 = vID[2];

          if ((vID1 == vertexID1 && vID2 == vertexID2)
              || (vID1 == vertexID2 && vID2 == vertexID1)) {
            newFace1 = {vID1, vertexID0, vID0};
            newFace2 = {vID0, vertexID0, vID2};

            triangles.push_back(newFace1);
            triangles.push_back(newFace2);
            if (trackFlag) {
              newOriginalTriangleIndices.push_back(-1);
              newOriginalTriangleIndices.push_back(-1);
            }
            addedFlag = true;
            break;
          }
        }
      }

      //if (!addedFlag) triangles.push_back(tri);
      if (!addedFlag) {
        triangles.push_back(tri);
        if (trackFlag) 
        newOriginalTriangleIndices.push_back(
            (*originalTriangleIndices)[tindex]);
      }
    }
    if (trackFlag) 
      *originalTriangleIndices = std::move(newOriginalTriangleIndices);
    std::swap(mesh.triangles(), triangles);
  }
}
//============================================================================

void
VMCEncoder::textureParametrization(
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
  int32_t                                  frameIndexInAllGof) {
  auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
  std::cout << "Texture parametrization submesh[ " << submeshIndex
            << " ] Frame[ " << frameIndex << " ]\n";

  // Load cache files
  bool skipUVAtlas = false;
  if (params.cachingPoint >= CachingPoint::TEXGEN) {
    if (loadCache(
          params, decimateTexture, "decimateTexture", _gofIndex, frameIndex)) {
      skipUVAtlas = true;
    }
  }

  printf("skipUVAtlas = %d \n", skipUVAtlas);
  fflush(stdout);
  if (!skipUVAtlas) {
    //Remove degenerate triangles
    removeDegeneratedTrianglesCrossProduct(
      decimate, originalTriangleIndices, flag, frameIndex);

    TextureParametrization textureParametrization;
    if (params.textureParameterizationType == 1) {
      //for useRawUV == 1
      trasferInformation trasferInfo;
      if (params.useRawUV) {
        trasferInfo.inputMeshPath    = inputMeshPath_;
        trasferInfo.inputTexturePath = inputAttributesPaths_[0];
        trasferInfo.sourceAttributeColourSpace =
          sourceAttributeColourSpaces_[0];
        trasferInfo.texParamWidth  = texParamWidth;
        trasferInfo.texParamHeight = texParamHeight;
      }
      //ORTHO/
      textureParametrization.generate_orthoAtlas(decimate,
                                                 decimateTexture,
                                                 currentPackedCCList,
                                                 previousPackedCCList,
                                                 params,
                                                 prefix,
                                                 frameIndex,
                                                 catOrderGof,
                                                 maxOccupiedU,
                                                 maxOccupiedV,
                                                 frameIndexInAllGof,
                                                 trasferInfo);

    } else if (params.textureParameterizationType == 0) {
      textureParametrization.generate(
        decimate, decimateTexture, texParamWidth, texParamHeight, params);
    } else { // -1 : NONE
      decimateTexture = decimate;
      decimateTexture.resizeTexCoords(0);
      decimateTexture.resizeTexCoordTriangles(0);
    }

    std::string name = "submesh" + std::to_string(submeshIndex);
    // Save intermediate files
    if (params.keepPreMesh) {
      decimateTexture.save(prefix + "_decimateTexture.ply");
    }

    // Save cache files
    if (params.cachingPoint >= CachingPoint::TEXGEN) {
      saveCache(
        params, decimateTexture, "decimateTexture", _gofIndex, frameIndex);
    }
  }
}

//----------------------------------------------------------------------------

void
VMCEncoder::geometryParametrization(std::vector<VMCSubmesh>&      gof,
                                    VMCFrameInfo&                 frameInfo,
                                    VMCSubmesh&                   frame,
                                    int32_t                       submeshIndex,
                                    int32_t                       frameIndex,
                                    int32_t                       frameCount,
                                    const TriangleMesh<MeshType>& input,
                                    const VMCEncoderParameters&   params,
                                    int32_t& lastIntraFrameIndex,
                                    int32_t& gofStartFrameIndex) {
  std::cout << "Geometry parametrization submesh[ " << submeshIndex
            << " ] Frame[ " << frameIndex << " ]\n";
  auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
  fixInstability(frame, params);
  // Load cache files
  bool skipSubDiv = false;
  if (params.cachingPoint >= CachingPoint::SUBDIV) {
    if (loadCache(params, frame.base, "base", _gofIndex, frameIndex)
        && loadCache(params, frame.subdiv, "subdiv", _gofIndex, frameIndex)) {
      skipSubDiv = true;
    }
  }
  printf("skipSubDiv = %d \n", skipSubDiv);
  fflush(stdout);
  if (!skipSubDiv) {
    // Intra geometry parametrization
    std::cout << "Intra geometry parametrization\n";
    GeometryParametrization fitsubdivIntra;
    TriangleMesh<MeshType>  mtargetIntra, subdiv0Intra;
    TriangleMesh<MeshType>  baseIntra, subdivIntra, nsubdivIntra,
      subdivIntraBeforeOpti;
    TriangleMesh<MeshType> baseInter, subdivInter, nsubdivInter,
      subdivInterBeforeOpti;
    auto paramsOpti = params;
#if SUBDIVCOUNTCONSISTENCY
    auto encSubdivisionInterationCount = params.subdivisionIterationCount;
    TriangleMesh<MeshType> subdivIntraExtract, subdivInterExtract;
#endif
    if (params.encoderOptiFlag) {
      paramsOpti.intraGeoParams
        .geometryParametrizationSubdivisionIterationCount =
        params.subdivisionIterationCount + 1;
      paramsOpti.interGeoParams
        .geometryParametrizationSubdivisionIterationCount =
        paramsOpti.subdivisionIterationCount + 1;
      paramsOpti.subdivisionIterationCountEncoderOpti =
        paramsOpti.subdivisionIterationCount + 1;
      fitsubdivIntra.generate(frame.reference,            // target
                              frame.decimateTexture,      // source
                              frame.mapped,               // mapped
                              mtargetIntra,               // mtarget
                              subdiv0Intra,               // subdiv0
                              paramsOpti.intraGeoParams,  // params
                              baseIntra,                  // base
                              subdivIntra,                // deformed
                              nsubdivIntra);              // ndeformed

      subdivIntraBeforeOpti = subdivIntra;
      auto subdivCountDiff  = paramsOpti.subdivisionIterationCountEncoderOpti
                             - paramsOpti.subdivisionIterationCount;
      deformSubdiv(baseIntra,
                   subdivIntra,
                   submeshIndex,
                   frameIndex,
                   subdivCountDiff,
                   paramsOpti);
    } else {
      fitsubdivIntra.generate(frame.reference,        // target
                              frame.decimateTexture,  // source
                              frame.mapped,           // mapped
                              mtargetIntra,           // mtarget
                              subdiv0Intra,           // subdiv0
                              params.intraGeoParams,  // params
                              baseIntra,              // base
                              subdivIntra,            // deformed
                              nsubdivIntra);          // ndeformed
    }
#if SUBDIVCOUNTCONSISTENCY
    if (
        params.subdivInter
        && params.intraGeoParams.geometryParametrizationSubdivisionIterationCount
        != encSubdivisionInterationCount) {
        std::vector<vmesh::SubdivisionMethod> subdivisionMethod(
            params.subdivisionIterationCount);
        for (int it = 0; it < params.subdivisionIterationCount; it++) {
            subdivisionMethod[it] = params.intraGeoParams.subdivisionMethod[it];
        }
        auto subdiv = baseIntra;
        subdivIntraExtract = subdivIntra;
        subdiv.subdivideMesh(
            encSubdivisionInterationCount, 0, 0, &(subdivisionMethod));
        std::swap(subdiv, subdivIntraExtract);
        for (int32_t v = 0, vcount = subdivIntraExtract.pointCount(); v < vcount;
            ++v) {
            subdivIntraExtract.setPoint(v, subdiv.point(v));
        }
    }
#endif
    // Save intermediate files
    if (params.keepPreMesh) {
      baseIntra.save(prefix + "_intra_base.ply");
      subdivIntra.save(prefix + "_intra_subdiv.ply");
      nsubdivIntra.save(prefix + "_intra_nsubdiv.ply");
    }

    bool chooseIntra = true;

    if (params.subdivInter && frameIndex > 0
        && ((!params.subdivInterWithMapping)
            || frameInfo.referenceFrameIndex != -1)) {
      // Inter geometry parametrization
      std::cout << "Inter geometry parametrization: withMapping = "
                << params.subdivInterWithMapping << "\n";
      GeometryParametrization fitsubdivInter;
      if (params.subdivInterWithMapping) {
        // With mapping
        printf("ref frame= %d is updated to lastIntraFrameIndex= %d\n",
               frameInfo.referenceFrameIndex,
               lastIntraFrameIndex);
        fflush(stdout);
        frameInfo.referenceFrameIndex =
          std::max(frameInfo.referenceFrameIndex, lastIntraFrameIndex);
        auto&                  ref = gof[frameInfo.referenceFrameIndex];
        TriangleMesh<MeshType> mappedInter;
        TriangleMesh<MeshType> subdiv0Inter;
        //ref.reference.texturCoordinateCount = input.texturCoordinateCount
        if (params.encoderOptiFlag) {
          fitsubdivInter.generate(ref.reference,              // target
                                  ref.decimateTexture,        // source
                                  ref.mapped,                 // mapped
                                  input,                      // mtarget
                                  subdiv0Inter,               // subdiv0
                                  paramsOpti.interGeoParams,  // params
                                  baseInter,                  // base
                                  subdivInter,                // deformed
                                  nsubdivInter);              // ndeformed
        } else {
          fitsubdivInter.generate(ref.reference,          // target
                                  ref.decimateTexture,    // source
                                  ref.mapped,             // mapped
                                  input,                  // mtarget
                                  subdiv0Inter,           // subdiv0
                                  params.interGeoParams,  // params
                                  baseInter,              // base
                                  subdivInter,            // deformed
                                  nsubdivInter);          // ndeformed
        }
      } else {
        // Without mapping
        // subdiv0 : ld frame - 1 subdiv
        // souce   : ld frame - 1 base
        // target  : ld frame - 1 reference
        auto& ref = gof[referenceFrameIndexFromFrameOrderBasemesh(
          frameIndex, frameCount, params.basemeshGOPList)];
        TriangleMesh<MeshType> mappedInter;
        TriangleMesh<MeshType> mtargetInter;

        if (!params.TrackedMode) {
          if (params.encoderOptiFlag) {
            fitsubdivInter.generate(frame.reference,            // target
                                    ref.base,                   // source
                                    mappedInter,                // mapped
                                    mtargetInter,               // mtarget
                                    ref.subdivBeforeOpti,       // subdiv0
                                    paramsOpti.interGeoParams,  // params
                                    baseInter,                  // base
                                    subdivInter,                // deformed
                                    nsubdivInter);              // ndeformed
          } else {
            fitsubdivInter.generate(frame.reference,        // target
                                    ref.base,               // source
                                    mappedInter,            // mapped
                                    mtargetInter,           // mtarget
                                    ref.subdiv,             // subdiv0
                                    params.interGeoParams,  // params
                                    baseInter,              // base
                                    subdivInter,            // deformed
                                    nsubdivInter);          // ndeformed
          }
        } else {
          std::cout << "params.TrackedMode: " << params.TrackedMode
                    << std::endl;

          Frame<uint8_t> refTexture;
          auto           refTexturePath = inputAttributesPaths_[0];
          if (!refTexture.load(refTexturePath,
                               frameIndex + gofStartFrameIndex - 1)) {
            printf("fail to load texture : %s frame %d\n",
                   inputAttributesPaths_[0].c_str(),
                   frameIndex + gofStartFrameIndex - 1);
            exit(2);
          };

          Frame<uint8_t> curTexture;
          auto           curTexturePath = inputAttributesPaths_[0];
          if (!curTexture.load(curTexturePath,
                               frameIndex + gofStartFrameIndex)) {
            printf("fail to load texture : %s frame %d\n",
                   inputAttributesPaths_[0].c_str(),
                   frameIndex + gofStartFrameIndex);
            exit(2);
          };
          const auto uvScale = 1.0 / ((1 << params.bitDepthTexCoord) - 1);

          vmesh::TriangleMesh<MeshType> refMesh, curMesh;
          refMesh.load(inputMeshPath_, frameIndex + gofStartFrameIndex - 1);
          curMesh.load(inputMeshPath_, frameIndex + gofStartFrameIndex);
          bool isTracked = true;
          if (refMesh.triangleCount() != curMesh.triangleCount()) {
            isTracked = false;
          } else {
            for (int i = 0; i < refMesh.triangleCount(); ++i) {
              auto RefTri = refMesh.triangle(i);
              auto CurTri = curMesh.triangle(i);
              isTracked = ((RefTri[0] == CurTri[0]) && (RefTri[1] == CurTri[1])
                           && (RefTri[2] == CurTri[2]));
              if (isTracked == false) break;
            }
          }

          std::cout << "Generate inter base mesh: " << isTracked << std::endl;

          if (isTracked) {
            FastInterBaseGeneration_1(curMesh,
                                      refMesh,
                                      frame.base,
                                      ref.base,
                                      curTexture,
                                      refTexture,
                                      uvScale);
            //                    FastInterBaseGeneration_hard_deform(frame.reference, ref.reference, frame.base, ref.base, curTexture, refTexture, uvScale);
          } else {
            FastInterBaseGeneration_hard_deform(frame.reference,
                                                ref.reference,
                                                frame.base,
                                                ref.base,
                                                curTexture,
                                                refTexture,
                                                uvScale);
          }

          TriangleMesh<MeshType> subdiv0Inter;
          fitsubdivIntra.generate(frame.reference,        // target
                                  frame.base,             // source
                                  mappedInter,            // mapped
                                  mtargetInter,           // mtarget
                                  subdiv0Inter,           // subdiv0
                                  params.interGeoParams,  // params
                                  baseInter,              // base
                                  subdivInter,            // deformed
                                  nsubdivInter);          // ndeformed
        }
      }
#if SUBDIVCOUNTCONSISTENCY
      if (
          params.interGeoParams.geometryParametrizationSubdivisionIterationCount
          != encSubdivisionInterationCount) {
          std::vector<vmesh::SubdivisionMethod> subdivisionMethod(
              params.subdivisionIterationCount);
          for (int it = 0; it < params.subdivisionIterationCount; it++) {
              subdivisionMethod[it] = params.intraGeoParams.subdivisionMethod[it];
          }
          auto subdiv = baseInter;
          subdivInterExtract = subdivInter;
          subdiv.subdivideMesh(
              encSubdivisionInterationCount, 0, 0, &(subdivisionMethod));
          auto rsubdiv = subdiv;
          std::swap(rsubdiv, subdivInterExtract);
          for (int32_t v = 0, vcount = subdivInterExtract.pointCount();
              v < vcount; ++v) {
              subdivInterExtract.setPoint(v, rsubdiv.point(v));
          }
      }
#endif
      // Save intermediate files
      if (params.keepPreMesh) {
        baseInter.save(prefix + "_inter_base.ply");
        subdivInter.save(prefix + "_inter_subdiv.ply");
        nsubdivInter.save(prefix + "_inter_nsubdiv.ply");
      }

      if (params.encoderOptiFlag) {
        subdivInterBeforeOpti = subdivInter;
        auto subdivCountDiff  = paramsOpti.subdivisionIterationCountEncoderOpti
                               - paramsOpti.subdivisionIterationCount;
        deformSubdiv(baseInter,
                     subdivInter,
                     submeshIndex,
                     frameIndex,
                     subdivCountDiff,
                     paramsOpti);
      }

      //TODO: minPosition, maxPosition are from the frame since they are grabbed from params
      // ComputeMetric/Decision
      VMCMetrics           metricsIntra;
      VMCMetrics           metricsInter;
      VMCMetricsParameters metricParams;
      metricParams.computePcc = true;
      metricParams.qp         = params.bitDepthPosition;
      metricParams.qt         = params.bitDepthTexCoord;
      for (int c = 0; c < 3; c++) {
        metricParams.minPosition[c] = params.minPosition[c];
        metricParams.maxPosition[c] = params.maxPosition[c];
      }
      metricParams.normalCalcModificationEnable =
        params.normalCalcModificationEnable;
#if SUBDIVCOUNTCONSISTENCY
      if (
          params.interGeoParams.geometryParametrizationSubdivisionIterationCount
          != encSubdivisionInterationCount) {
          metricsIntra.compute(input, subdivIntraExtract, metricParams);
          metricsInter.compute(input, subdivInterExtract, metricParams);
      }
      else {
          metricsIntra.compute(input, subdivIntra, metricParams);
          metricsInter.compute(input, subdivInter, metricParams);
      }
#else
      metricsIntra.compute(input, subdivIntra, metricParams);
      metricsInter.compute(input, subdivInter, metricParams);
#endif
      auto metIntra = metricsIntra.getPccResults();
      auto metInter = metricsInter.getPccResults();
      int  pos      = 1;
      chooseIntra =
        metIntra[pos] - metInter[pos] > params.maxAllowedD2PSNRLoss;
      printf("Frame %3d: choose %s: met %8.4f - %8.4f = %8.4f > %8.4f \n",
             frameIndex,
             chooseIntra ? "Intra" : "Inter",
             metIntra[pos],
             metInter[pos],
             metIntra[pos] - metInter[pos],
             params.maxAllowedD2PSNRLoss);
    }

    if (chooseIntra) {
      frame.base                    = baseIntra;
      frame.subdiv                  = subdivIntra;
      frame.subdivBeforeOpti        = subdivIntraBeforeOpti;
      frameInfo.type                = basemesh::I_BASEMESH;
      frameInfo.referenceFrameIndex = -1;
      lastIntraFrameIndex           = frameIndex;
      printf("PREPROCESSING FRAME %3d: INTRA \t", frameIndex);
      printf("subdivInter: %d ", params.subdivInter);
      printf("subdivInterWithMapping: %d ", params.subdivInterWithMapping);
      printf("frameInfo.referenceFrameIndex: %d\n",
             frameInfo.referenceFrameIndex);

    } else {
      frame.base             = baseInter;
      frame.subdiv           = subdivInter;
      frame.subdivBeforeOpti = subdivInterBeforeOpti;
      frameInfo.type         = basemesh::P_BASEMESH;
      frameInfo.referenceFrameIndex =
        referenceFrameIndexFromFrameOrderBasemesh(
          frameInfo.frameIndex, frameCount, params.basemeshGOPList);
      printf("PREPROCESSING FRAME %3d: PREDICTION \t", frameIndex);
      printf("Update referenceFrameIndex = %d \n",
             frameInfo.referenceFrameIndex);
      // from m62198
      auto& ref          = gof[frameInfo.referenceFrameIndex];
      frame.packedCCList = ref.packedCCList;
    }

    // Save cache files
    if (params.cachingPoint >= CachingPoint::SUBDIV) {
      saveCache(params, frame.base, "base", _gofIndex, frameIndex);
      saveCache(params, frame.subdiv, "subdiv", _gofIndex, frameIndex);
    }
  }
}

bool
VMCEncoder::deformSubdiv(TriangleMesh<MeshType>&     base,
                         TriangleMesh<MeshType>&     subdiv,
                         int32_t                     submeshIndex,
                         int32_t                     frameIndex,
                         int32_t&                    subdivCountDiff,
                         const VMCEncoderParameters& params) {
  std::vector<SubdivisionLevelInfo> subdivInfoLevelOfDetailsSubdiv;
  std::vector<int64_t>              subdivEdgesSubdiv;
  auto                              rec = base;
  rec.subdivideMesh(params.subdivisionIterationCountEncoderOpti,
                    0,
                    0,
                    nullptr,
                    &subdivInfoLevelOfDetailsSubdiv,
                    &subdivEdgesSubdiv);

  std::vector<SubdivisionLevelInfo> subdivInfoLevelOfDetailsSubdiv_sub;
  std::vector<int64_t>              subdivEdgesSubdiv_sub;
  auto                              sub = base;
  sub.subdivideMesh(params.subdivisionIterationCount,
                    0,
                    0,
                    nullptr,
                    &subdivInfoLevelOfDetailsSubdiv_sub,
                    &subdivEdgesSubdiv_sub);

  auto                predWeight = pow(2, -params.liftingLog2PredictionWeight);
  auto                skipUpdate = params.liftingSkipUpdate;
  int32_t             lodCountSubdiv = subdivInfoLevelOfDetailsSubdiv.size();
  int32_t             starLod        = lodCountSubdiv - 1;
  int32_t             endLod         = starLod - subdivCountDiff;
  auto&               points         = subdiv.points();
  std::vector<double> updateWeight;
  updateWeight.resize(lodCountSubdiv - 1, 0);
  if (skipUpdate) {
    // updateWeight[l] = 0
  } else {
    for (int l = 0; l < lodCountSubdiv - 1; l++) {
      if ((params.liftingAdaptiveUpdateWeightFlag == 1) || (l == 0)) {
        updateWeight[l] =
          (double)params.liftingUpdateWeightNumerator[l]
          / (double)(params.liftingUpdateWeightDenominatorMinus1[l] + 1);
      } else {
        updateWeight[l] = updateWeight[0];
      }
    }
  }
  for (int32_t it = starLod - 1; it >= endLod; --it) {
    const auto vcount0 = subdivInfoLevelOfDetailsSubdiv[it].pointCount;
    const auto vcount1 = subdivInfoLevelOfDetailsSubdiv[it + 1].pointCount;
    assert(vcount0 < vcount1 && vcount1 <= int32_t(points.size()));
    // predict
    for (int32_t v = vcount0; v < vcount1; ++v) {
      const auto edge = subdivEdgesSubdiv[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      points[v] -= predWeight * (points[v1] + points[v2]);
    }
    // update
    for (int32_t v = vcount0; !skipUpdate && v < vcount1; ++v) {
      const auto edge = subdivEdgesSubdiv[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      const auto d = updateWeight[it] * points[v];
      points[v1] += d;
      points[v2] += d;
    }
  }

  for (int i = 0; i < sub.pointCount(); ++i) {
    sub.point(i) = subdiv.point(i);
  }
  subdiv = sub;

  return false;
}

//----------------------------------------------------------------------------

void
VMCEncoder::updateTextureSizeRemoveNonOccupied(
  const VMCGroupOfFramesInfo&     gofInfo,
  std::vector<vmesh::VMCSubmesh>& gof,
  V3cBitstream&                   syntax,
  const VMCEncoderParameters&     params,
  const int&                      globalMaxOccupiedU,
  const int&                      globalMaxOccupiedV,
  const int32_t&                  submeshIndex) {
  const int32_t frameCount       = gofInfo.frameCount_;
  auto&         textureVideoSize = attributeVideoSize_[0];
  //set width and height based on all frames
  auto geometryVideoBlockSize = (1 << params.log2GeometryVideoBlockSize);

  auto globalMaxOccupiedWidth  = globalMaxOccupiedU * geometryVideoBlockSize;
  auto globalMaxOccupiedHeight = globalMaxOccupiedV * geometryVideoBlockSize;

  //for update uv
  int occupancySizeV, occupancySizeU;

  //flexible packing (firstly set to four times the textureParametrization size, it will be adjusted after packing all frames)
  occupancySizeV = std::ceil((double)params.texParamHeight * 4
                             / (double)geometryVideoBlockSize);
  occupancySizeU = std::ceil((double)params.texParamWidth * 4
                             / (double)geometryVideoBlockSize);

  //Remove non-occupied area
  double texturePackingWidthNewTemp  = globalMaxOccupiedWidth + params.gutter;
  double texturePackingHeightNewTemp = globalMaxOccupiedHeight + params.gutter;

  int minCUwidth  = params.maxCUWidth >> (params.maxPartitionDepth - 1);
  int minCUheight = params.maxCUHeight >> (params.maxPartitionDepth - 1);

  texturePackingWidthNewTemp =
    ceil(texturePackingWidthNewTemp / minCUwidth) * minCUwidth;
  texturePackingHeightNewTemp =
    ceil(texturePackingHeightNewTemp / minCUheight) * minCUheight;

  //geometryVideoBlockSize limitation
  texturePackingWidthNewTemp =
    ceil(texturePackingWidthNewTemp / geometryVideoBlockSize)
    * geometryVideoBlockSize;
  texturePackingHeightNewTemp =
    ceil(texturePackingHeightNewTemp / geometryVideoBlockSize)
    * geometryVideoBlockSize;

  int texturePackingWidthNew  = texturePackingWidthNewTemp;
  int texturePackingHeightNew = texturePackingHeightNewTemp;

  //update encode texture size
  if (params.updateTextureSize > 0) {
    if (submeshIndex == 0) {
      textureVideoSize.first  = texturePackingWidthNew;
      textureVideoSize.second = texturePackingHeightNew;
    } else {
      textureVideoSize.first =
        std::max((int)textureVideoSize.first, texturePackingWidthNew);
      textureVideoSize.second =
        std::max((int)textureVideoSize.second, texturePackingHeightNew);
    }
    std::cout << "[GOF:" << gofInfo.index_ << ", submeshIndex:" << submeshIndex
              << "]"
              << "Texture size is updated to (" << textureVideoSize.first
              << ", " << textureVideoSize.second << ")." << std::endl;
  }
  texturePackingSize.first  = texturePackingWidthNew;
  texturePackingSize.second = texturePackingHeightNew;
  std::cout << "[GOF:" << gofInfo.index_ << ", submeshIndex:" << submeshIndex
            << "]"
            << "Texture Packing  is updated to (" << texturePackingSize.first
            << ", " << texturePackingSize.second << ")" << std::endl;
  auto& adStream = syntax.getAtlasDataStream();
  //update projection parameters
  for (auto& afps : adStream.getAtlasFrameParameterSetList()) {
    auto& afve = afps.getAfveExtension();

    afve.getProjectionTextcoordWidth(submeshIndex)  = texturePackingWidthNew;
    afve.getProjectionTextcoordHeight(submeshIndex) = texturePackingHeightNew;
  }

  //update uv
  double scaleNewToTempWidth =
    occupancySizeU * (double)geometryVideoBlockSize / texturePackingWidthNew;
  double scaleNewToTempHeight =
    occupancySizeV * (double)geometryVideoBlockSize / texturePackingHeightNew;

  std::cout << "scaleNewToTempWidth : " << scaleNewToTempWidth << std::endl;
  std::cout << "scaleNewToTempHeight : " << scaleNewToTempWidth << std::endl;

  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame = gof[frameIndex];

    //update uv
    for (auto& uv : frame.base.texCoords()) {
      uv[0] *= scaleNewToTempWidth;
      uv[1] *= scaleNewToTempHeight;
    }
  }
}
void
VMCEncoder::fixInstability(VMCSubmesh&                 frame,
                           const VMCEncoderParameters& params) {
  double q =
    std::pow(10, params.log10GeometryParametrizationQuantizationValue);
  for (int i = 0; i < frame.decimateTexture.pointCount(); i++) {
    auto& point0 = frame.decimateTexture.point(i);
    point0[0]    = floor(point0[0] * q) / q;
    point0[1]    = floor(point0[1] * q) / q;
    point0[2]    = floor(point0[2] * q) / q;
  }
}
bool
VMCEncoder::preprocessMeshes(const VMCGroupOfFramesInfo&     gofInfoSrc,
                             const Sequence&                 source,
                             std::vector<vmesh::VMCSubmesh>& gof,
                             V3cBitstream&                   syntax,
                             V3CParameterSet&                vps,
                             int32_t                         submeshIndex,
                             int32_t                         submeshCount,
                             const VMCEncoderParameters&     params) {
  auto          gofInfo             = gofInfoSrc;
  const int32_t frameCount          = gofInfo.frameCount_;
  int32_t       lastIntraFrameIndex = 0;
  //  initialize(submeshCount, frameCount, submeshIndex, params);
  if (reconBasemeshes_.size() == 0)
    reconBasemeshes_.resize(submeshCount);  //[submesh][#frame]
  if (reconSubdivmeshes_.size() == 0)
    reconSubdivmeshes_.resize(submeshCount);  //[submesh][#frame]
  reconBasemeshes_[submeshIndex].resize(frameCount);
  reconSubdivmeshes_[submeshIndex].resize(frameCount);

  gof.resize(source.frameCount());
  printf("Compress: frameCount = %d \n", frameCount);
  fflush(stdout);

  //save packing order of the first frame for orthoAtlas
  int              orientationCount = params.use45DegreeProjection ? 18 : 6;
  std::vector<int> catOrderGof(orientationCount, -1);

  //variables for orthoAtlas based on all frames
  int maxOccupiedU       = 0;
  int maxOccupiedV       = 0;
  int globalMaxOccupiedU = 0;
  int globalMaxOccupiedV = 0;

  for (int32_t processIndex = 0; processIndex < frameCount; ++processIndex) {
    const auto frameIndex = decodeOrderToFrameOrderBasemesh(
      processIndex, frameCount, params.basemeshGOPList);
    // for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame      = gof[frameIndex];
    frame.frameIndex = frameIndex;
    //frame.submeshIndex = submeshIndex;
    VMCSubmesh previousFrame;
    previousFrame.frameIndex = -1;
    if (params.bTemporalStabilization && (frameIndex > 0)) {
      previousFrame            = gof[frameIndex - 1];
      previousFrame.frameIndex = frameIndex - 1;
    }
    if (params.baseIsSrc && params.subdivIsBase) {
      if (params.baseIsSrc) { frame.base = source.mesh(frameIndex); }
      if (params.subdivIsBase) {
        auto& frameInfo = gofInfo.frameInfo(frameIndex);
        frame.subdiv    = frame.base;
        if (params.subdivInter && frameInfo.referenceFrameIndex != -1) {
          frameInfo.referenceFrameIndex = frameInfo.frameIndex - 1;
        }
      }
    } else {
      TriangleMesh<MeshType> decimate;
      decimateInput(source.mesh(frameIndex),
                    submeshIndex,
                    frameIndex,
                    frame,
                    decimate,
                    params);

      size_t texParamWidth  = params.texParamWidth;
      size_t texParamHeight = params.texParamHeight;
      if ( (submeshCount != 1) && params.encodeTextureVideo) {
        //  tilePatchOfSubmesh is an attibute tile when attributes are coded
        int32_t tileIndex =
          atlasEncoder_
            .tilePatchOfSubmesh(submeshIndex, params.atlasEncoderParameters_)
            .first;
        int32_t patchIndex =
          atlasEncoder_
            .tilePatchOfSubmesh(submeshIndex, params.atlasEncoderParameters_)
            .second;
        if (tileIndex == -1 || patchIndex == -1) {
          std::cout << "submeshIndex " << submeshIndex
                    << " (submeshId: " << params.submeshIdList[submeshIndex]
                    << ") is not found in tiles\n";
        } else {
          //patch to tile
          double subTextureSizeX = tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[0]
                                     .sizeX;
          double subTextureSizeY = tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[0]
                                     .sizeY;
          //TODO: [sw] disable subTexture (if sectioned texture images are not desired, different function is required here)
          int32_t submeshesInTile =
            tileAreasInVideo_[tileIndex][frameIndex].patches_.size();
          // the result of the tilePatchOfSubmesh will return the attribute tile, where lodPatches are NOT used
          //if (params.lodPatchesEnable){
          //  submeshesInTile = submeshesInTile/(params.subdivisionIterationCount + 1);
          //}
          if (submeshesInTile != params.submeshIdsInTile[tileIndex].size()) {
            std::cout << "Not correct number of submeshes in tile["
                      << tileIndex << "]\n";
            exit(-12);
          }
          double targetVideoWidthRatio =
            subTextureSizeX / params.attributeParameters[0].textureWidth;
          double targetVideoHeightRatio =
            subTextureSizeY / params.attributeParameters[0].textureHeight;
          texParamWidth *=
            std::min(targetVideoWidthRatio, targetVideoHeightRatio);
          texParamHeight *=
            std::min(targetVideoWidthRatio, targetVideoHeightRatio);
        }
      }
      if (texParamWidth == 0 || texParamHeight == 0) {
        std::cout <<"texParamWidth/Height error"<<std::endl;
      }
      if (params.checksum) {
        auto&       frame = gof[frameIndex];
        Checksum    checksum;
        std::string eString = "decimate:  Frame[ " + std::to_string(frameIndex)
                              + " ]submesh[" + std::to_string(submeshIndex)
                              + "]\tdecimate ";
        checksum.print(decimate, eString);
      }

      int frameIndexInAllGof = frameIndex + gofInfo.startFrameIndex_;
      TriangleMesh<MeshType>& decimateTexture = frame.decimateTexture;
      std::vector<int32_t>    originalTriangleIndices;
      bool                    trackFlag = false;
      if (params.subdivInterWithMapping) {
         trackFlag = true;
         auto triangleCount = decimate.triangleCount();
         originalTriangleIndices.reserve(triangleCount);
      }
      textureParametrization(submeshIndex,
                             frameIndex,
                             decimate,
                             decimateTexture,
                             texParamWidth,
                             texParamHeight,
                             params,
                             frame.packedCCList,
                             previousFrame.packedCCList,
                             catOrderGof,
                             maxOccupiedU,
                             maxOccupiedV,
                             trackFlag ? &originalTriangleIndices : nullptr,
                             trackFlag,
                             frameIndexInAllGof);
      if (trackFlag) {
        TriangleMesh<MeshType>&    mapped = frame.mapped;
        std::map<int32_t, int32_t> reversedMap;
        for (size_t i = 0; i < originalTriangleIndices.size(); i++) {
          int32_t originalIndex = originalTriangleIndices[i];
          if (originalIndex != -1) {
            reversedMap[originalIndex] = static_cast<int32_t>(i);
          }
        }

        for (int i = 0; i < mapped.trackCount(); i++) {
          auto& track = mapped.track(i);
          if (track != -1) track = reversedMap[track];
        }
      }
      if (params.checksum) {
        auto&       frame = gof[frameIndex];
        Checksum    checksum;
        std::string eString = "decimateTexture:  Frame[ "
                              + std::to_string(frameIndex) + " ]submesh["
                              + std::to_string(submeshIndex)
                              + "]\tdecimateTexture ";
        checksum.print(decimateTexture, eString);
      }

      decimate.clear();
      auto paramsGeometryParametrization                           = params;
      paramsGeometryParametrization.subdivisionEdgeLengthThreshold = 0;
      paramsGeometryParametrization.intraGeoParams
        .subdivisionEdgeLengthThreshold = 0;
      paramsGeometryParametrization.interGeoParams
        .subdivisionEdgeLengthThreshold = 0;
      geometryParametrization(gof,
                              gofInfo.frameInfo(frameIndex),
                              frame,
                              submeshIndex,
                              frameIndex,
                              frameCount,
                              source.mesh(frameIndex),
                              paramsGeometryParametrization,
                              lastIntraFrameIndex,
                              gofInfo.startFrameIndex_);
      if (params.checksum) {
        auto&       frame = gof[frameIndex];
        Checksum    checksum;
        std::string eString =
          "frame.base:  Frame[ " + std::to_string(frameIndex) + " ]submesh["
          + std::to_string(submeshIndex) + "]\tframe.base ";
        checksum.print(frame.base, eString);
      }

      if (params.subdivIsBase) { frame.subdiv = frame.base; }
    }

    if (globalMaxOccupiedU < maxOccupiedU) globalMaxOccupiedU = maxOccupiedU;
    if (globalMaxOccupiedV < maxOccupiedV) globalMaxOccupiedV = maxOccupiedV;
  }

  if (params.textureParameterizationType == 1 && params.packingType == 3) {
    updateTextureSizeRemoveNonOccupied(gofInfo,
                                       gof,
                                       syntax,
                                       params,
                                       globalMaxOccupiedU,
                                       globalMaxOccupiedV,
                                       submeshIndex);
  }

  //Copy preprocessing info to compression info
  printf("Copy Preprocessing info to compression info\n");
  printf("\tFrame reference structure after preporcessing : Submesh[ %d ]\n",
         submeshIndex);
  gofInfo.trace();

  fflush(stdout);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto&       frame     = gof[frameIndex];
    const auto& frameInfo = gofInfo.frameInfo(frameIndex);
    frame.submeshType     = frameInfo.type;
    frame.referenceFrameIndex =
      frameInfo
        .referenceFrameIndex;  //NOTE: the values are absolute frameIndex within the sequence (0~30 = current frameIndex(0~31)-1)
    frame.frameIndex = frameIndex;
    //frame.submeshIndex = submeshIndex;
    frame.submeshId_ = params.submeshIdList[submeshIndex];

    printf(
      "Frame %2d: type = %s base = %6d points subdiv = %6d points Ref = %d \n",
      frameIndex,
      toString(frame.submeshType).c_str(),
      frame.base.pointCount(),
      frame.subdiv.pointCount(),
      frame.referenceFrameIndex);
    // Save intermediate files
    if (params.keepBaseMesh) {
      auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
      frame.base.save(prefix + "_base_org.obj");
      frame.subdiv.save(prefix + "_subdiv_org.obj");
    }
  }

  // Pre-process Normals
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame      = gof[frameIndex];
    frame.frameIndex = frameIndex;
    if (!(params.baseIsSrc && params.subdivIsBase && params.encodeNormals)) {
      // If lossy compression or not encoding normals, set them to 0
      frame.base.resizeNormals(0);
      frame.base.resizeNormalTriangles(0);
      frame.subdiv.resizeNormals(0);
      frame.subdiv.resizeNormalTriangles(0);
    } else if (!frame.base.normals().empty()) {
      // If lossless compression and encoding normals
      auto normalInt = params.normalInt;
      if (normalInt) {
        // Making sure the normals are pre-quantized and unsigned.
        bool isNormPreQuantized = true;
        bool isNormUnsigned     = true;

        // Check only the first normal to see if pre-quantized. See if normal is normalized or not.
        auto& first_normal = frame.base.normal(0);
        auto  norm         = round(first_normal.norm() * 1000)
                    / 1000;  // Round the normal to three decimal places
        if (norm == 1) {
          isNormPreQuantized = false;
          printf("Frame %2d: WARNING! The normals are not pre-quantized, "
                 "Assuming normalInt parameter value is 0 instead of 1 \n",
                 frameIndex);
        }

        // Compare the minimum normals value to see if it is unsigned or not.
        auto&  normals = frame.base.normals();
        double minimum = 10000;
        for (int32_t n = 0, ncount = frame.base.normalCount(); n < ncount;
             ++n) {
          for (int c = 0; c < 3; c++) {
            minimum = std::min(normals[n][c], minimum);
          }
        }
        if (minimum < 0) {
          isNormUnsigned = false;
          printf("Frame %2d: WARNING! The normals are not Unsigned, Assuming "
                 "normalInt parameter value is 0 instead of 1 \n",
                 frameIndex);
        }

        if (!isNormPreQuantized && !isNormUnsigned) { normalInt = 0; }
      }

      // Quantize the Normals
      if (normalInt == 0) {
        printf("Frame %2d: Quantizing The Normals to Unsigned Integer \n",
               frameIndex);
        auto& normals = frame.base.normals();

        const auto scaleNormal = std::pow(2.0, params.qpNormals) - 1.0;
        double     delta[3]    = {0, 0, 0};
        for (int c = 0; c < 3; c++) {
          delta[c] = params.maxNormals[c] - params.minNormals[c];
        }
        for (int32_t n = 0, ncount = frame.base.normalCount(); n < ncount;
             ++n) {
          Vec3<int32_t> nrm(0);
          for (int c = 0; c < 3; c++) {
            nrm[c] = static_cast<uint32_t>(std::floor(
              ((frame.base.normal(n)[c] - params.minNormals[c]) / delta[c])
                * scaleNormal
              + 0.5f));
          }
          frame.base.setNormal(n, nrm);
          frame.subdiv.setNormal(n, nrm);
        }
      } else {
        const auto scaleNormal = ((1 << params.qpNormals) - 1.0)
                                 / ((1 << params.bitDepthNormals) - 1.0);
        for (int32_t n = 0, ncount = frame.base.normalCount(); n < ncount;
             ++n) {
          frame.base.setNormal(n,
                               Clamp(Round(frame.base.normal(n) * scaleNormal),
                                     0.0,
                                     ((1 << params.qpNormals) - 1.0)));
          frame.subdiv.setNormal(n, frame.base.normal(n));
        }
      }
    }
  }

  // Unify vertices
  printf("UnifyVertices \n");
  fflush(stdout);
  unifyVertices(gofInfo, gof, params);

  return true;
}

//----------------------------------------------------------------------------

bool
VMCEncoder::preprocessWholemesh(const Sequence&                 source,
                                const VMCGroupOfFramesInfo&     gofInfoSrc,
                                std::vector<vmesh::VMCSubmesh>& gof,
                                int32_t                         submeshIndex,
                                const VMCEncoderParameters&     params) {
  auto          gofInfo             = gofInfoSrc;
  const int32_t frameCount          = gofInfo.frameCount_;
  int32_t       lastIntraFrameIndex = 0;

  gof.resize(source.frameCount());
  printf("Compress: frameCount = %d \n", frameCount);
  fflush(stdout);

  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame      = gof[frameIndex];
    frame.frameIndex = frameIndex;
    //frame.submeshIndex               = submeshIndex;
    TriangleMesh<MeshType>& decimate = frame.decimateTexture;
    decimateInput(source.mesh(frameIndex),
                  submeshIndex,
                  frameIndex,
                  frame,
                  decimate,
                  params);
    //Remove degenerate triangles
    bool flag = false;
    removeDegeneratedTrianglesCrossProduct(
      decimate, nullptr, flag, frameIndex);

    auto paramsGeometryParametrization                           = params;
    paramsGeometryParametrization.subdivisionEdgeLengthThreshold = 0;
    paramsGeometryParametrization.intraGeoParams
      .subdivisionEdgeLengthThreshold = 0;
    paramsGeometryParametrization.interGeoParams
      .subdivisionEdgeLengthThreshold = 0;
    geometryParametrization(gof,
                            gofInfo.frameInfo(frameIndex),
                            frame,
                            submeshIndex,
                            frameIndex,
                            frameCount,
                            source.mesh(frameIndex),
                            paramsGeometryParametrization,
                            lastIntraFrameIndex,
                            gofInfo.startFrameIndex_);
  }

  //Copy preprocessing info to compression info
  printf("Copy Preprocessing info to compression info\n");
  printf("\tFrame reference structure after preporcessing : Submesh[ %d ]\n",
         submeshIndex);
  gofInfo.trace();

  fflush(stdout);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto&       frame     = gof[frameIndex];
    const auto& frameInfo = gofInfo.frameInfo(frameIndex);
    frame.submeshType     = frameInfo.type;
    frame.referenceFrameIndex =
      frameInfo
        .referenceFrameIndex;  //NOTE: the values are absolute frameIndex within the sequence (0~30 = current frameIndex(0~31)-1)
    frame.frameIndex = frameIndex;
    //frame.submeshIndex = submeshIndex;
    frame.submeshId_ = params.submeshIdList[submeshIndex];

    printf(
      "Frame %2d: type = %s base = %6d points subdiv = %6d points Ref = %d \n",
      frameIndex,
      toString(frame.submeshType).c_str(),
      frame.base.pointCount(),
      frame.subdiv.pointCount(),
      frame.referenceFrameIndex);
    // Save intermediate files
    if (params.keepBaseMesh) {
      auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
      frame.base.save(prefix + "_base_org.obj");
      frame.subdiv.save(prefix + "_subdiv_org.obj");
    }
  }

  // Unify vertices
  printf("UnifyVertices \n");
  fflush(stdout);
  unifyVertices(gofInfo, gof, params);

  return true;
}

//----------------------------------------------------------------------------

bool
VMCEncoder::preprocessSubmesh(const VMCGroupOfFramesInfo&     gofInfo,
                              std::vector<vmesh::VMCSubmesh>& gof,
                              int32_t                         submeshIndex,
                              const VMCEncoderParameters&     params) {
  const int32_t frameCount   = gofInfo.frameCount_;
  const int32_t submeshCount = params.numSubmesh;

  gof.resize(gofInfo.frameCount_);
  printf("Compress: frameCount = %d \n", frameCount);
  fflush(stdout);

  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    size_t texParamWidth  = params.texParamWidth;
    size_t texParamHeight = params.texParamHeight;
    {
      int32_t tileIndex =
        atlasEncoder_
          .tilePatchOfSubmesh(submeshIndex, params.atlasEncoderParameters_)
          .first;
      int32_t patchIndex =
        atlasEncoder_
          .tilePatchOfSubmesh(submeshIndex, params.atlasEncoderParameters_)
          .second;
      if (tileIndex == -1 || patchIndex == -1) {
        std::cout << "submeshIndex " << submeshIndex
                  << " (submeshId: " << params.submeshIdList[submeshIndex]
                  << ") is not found in tiles\n";
      } else {
        // when no attribute video is encoded (encodeTextureVideo=0),
        // textureParametrization is affected as subAttributeVideoAreas is not accessible
        if (params.videoAttributeCount > 0) {
          //patch to tile
          double subTextureSizeX = tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[0]
                                     .sizeX;
          double subTextureSizeY = tileAreasInVideo_[tileIndex][frameIndex]
                                     .patches_[patchIndex]
                                     .attributePatchArea[0]
                                     .sizeY;
          //TODO: [sw] disable subTexture (if sectioned texture images are not desired, different function is required here)
          int32_t submeshesInTile =
            tileAreasInVideo_[tileIndex][frameIndex].patches_.size();
          // the result of the tilePatchOfSubmesh will return the attribute tile, where lodPatches are NOT used
          //if (params.lodPatchesEnable){
          //  submeshesInTile = submeshesInTile/(params.subdivisionIterationCount + 1);
          //}
          if (submeshesInTile != params.submeshIdsInTile[tileIndex].size()) {
            std::cout << "Not correct number of submeshes in tile["
                      << tileIndex << "]\n";
            exit(-12);
          }
          double targetVideoWidthRatio =
            subTextureSizeX / params.attributeParameters[0].textureWidth;
          double targetVideoHeightRatio =
            subTextureSizeY / params.attributeParameters[0].textureHeight;
          texParamWidth *=
            std::min(targetVideoWidthRatio, targetVideoHeightRatio);
          texParamHeight *=
            std::min(targetVideoWidthRatio, targetVideoHeightRatio);
        }
      }
    }
    if (texParamWidth == 0 || texParamHeight == 0) {
      printf("texParamWidth/Height error\n");
    }

    auto&       base      = baseMeshes_[frameIndex][submeshIndex];
    const auto& frameInfo = gofInfo.frameInfo(frameIndex);
    if (frameInfo.type == basemesh::I_BASEMESH) {
      TriangleMesh<MeshType> decimateTexture;
      TextureParametrization textureParametrization;
      textureParametrization.generate(
        base, decimateTexture, texParamWidth, texParamHeight, params);
      base.texCoords()         = decimateTexture.texCoords();
      base.texCoordTriangles() = decimateTexture.texCoordTriangles();
    } else {
      const auto  referenceFrameIndex = frameInfo.referenceFrameIndex;
      const auto& ref  = baseMeshes_[referenceFrameIndex][submeshIndex];
      base.texCoords() = ref.texCoords();
      base.texCoordTriangles() = ref.texCoordTriangles();
    }
  }

  //Copy preprocessing info to compression info
  printf("Copy Preprocessing info to compression info\n");
  printf("\tFrame reference structure after preporcessing : Submesh[ %d ]\n",
         submeshIndex);
  gofInfo.trace();

  fflush(stdout);
  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto&       frame     = gof[frameIndex];
    const auto& frameInfo = gofInfo.frameInfo(frameIndex);
    frame.submeshType     = frameInfo.type;
    frame.referenceFrameIndex =
      frameInfo
        .referenceFrameIndex;  //NOTE: the values are absolute frameIndex within the sequence (0~30 = current frameIndex(0~31)-1)
    frame.frameIndex = frameIndex;
    //frame.submeshIndex = submeshIndex;
    frame.submeshId_ = params.submeshIdList[submeshIndex];

    printf(
      "Frame %2d: type = %s base = %6d points subdiv = %6d points Ref = %d \n",
      frameIndex,
      toString(frame.submeshType).c_str(),
      frame.base.pointCount(),
      frame.subdiv.pointCount(),
      frame.referenceFrameIndex);
    // Save intermediate files
    if (params.keepBaseMesh) {
      auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
      frame.base.save(prefix + "_base_org.obj");
      frame.subdiv.save(prefix + "_subdiv_org.obj");
    }
  }

  // Unify vertices
  printf("UnifyVertices \n");
  fflush(stdout);
  unifyVertices(gofInfo, gof, params);

  for (int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    auto& frame = gof[frameIndex];
    frame.base  = baseMeshes_[frameIndex][submeshIndex];
    if (params.keepBaseMesh) {
      auto prefix = _keepFilesPathPrefix + "fr_" + std::to_string(frameIndex);
      frame.base.save(prefix + "_base_k.obj");
    }
  }
  return true;
}

}  // namespace vmesh
