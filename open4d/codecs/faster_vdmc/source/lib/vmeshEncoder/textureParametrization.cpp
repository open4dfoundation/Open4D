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

//============================================================================

#include <chrono>
#include <iostream>
#include <fstream>
#include <list>
#include <vector>
#include <memory>
#include <algorithm>
#include "encoder.hpp"
#include "textureParametrization.hpp"
//texture transfer
#include "transferColor.hpp"
#include "metrics.hpp"
#include "util/mesh.hpp"

//#include "util/checksum.hpp"

// NB: these must come after the standard headers as they define prohibited
//     macros that conflict with the system implementation
#include <DirectXMath.h>
#include <DirectXMesh.h>

#include <UVAtlas.h>

#define DEBUG_ORTHO_CREATE_PATCH 0
#define DEBUG_ORTHO_CREATE_PATCH_VERBOSE 0
#define DEBUG_ORTHO_PATCH_PACKING 0
#define DEBUG_ORTHO_PATCH_PACKING_VERBOSE 0

namespace vmesh {

//============================================================================

HRESULT __cdecl UVAtlasCallback(float fPercentDone) {
  // static auto prev = std::chrono::steady_clock::now();
  // const auto  tick = std::chrono::steady_clock::now();
  // if (tick - prev > std::chrono::seconds(1)) {
  //   std::cout << fPercentDone * 100. << "%   \r" << std::flush;
  //   prev = tick;
  // }
  return S_OK;
}

//============================================================================

template<typename T>
bool
TextureParametrization::generate(const TriangleMesh<T>&      decimate,
                                 TriangleMesh<T>&            decimateTexture,
                                 size_t                      texParamWidth,
                                 size_t                      texParamHeight,
                                 const VMCEncoderParameters& params) {
  TriangleMesh<float> mesh;
  mesh.convert(decimate);

  // Remove unwanted mesh components
  mesh.displacements().clear();
  mesh.colours().clear();
  mesh.texCoords().clear();
  mesh.texCoordTriangles().clear();
  mesh.normals().clear();
  mesh.normalTriangles().clear();

  std::cout << mesh.pointCount() << " vertices, " << mesh.triangleCount()
            << " faces\n";

  if ((mesh.pointCount() == 0) || (mesh.triangleCount() == 0)) {
    std::cerr << "ERROR: Invalid mesh\n";
    return 1;
  }

  // Prepare mesh for processing
  const float           epsilon = 0.F;
  std::vector<uint32_t> adjacency(3 * mesh.triangleCount());
  auto                  hr = DirectX::GenerateAdjacencyAndPointReps(
    reinterpret_cast<uint32_t*>(mesh.triangles().data()),
    mesh.triangleCount(),
    reinterpret_cast<DirectX::XMFLOAT3*>(mesh.points().data()),
    mesh.pointCount(),
    epsilon,
    nullptr,
    adjacency.data());
  if (FAILED(hr)) {
    std::cerr << "ERROR: Failed generating adjacency (" << hr << ")\n";
    return 1;
  }

  // Validation
  std::wstring msgs;
  DirectX::Validate(reinterpret_cast<uint32_t*>(mesh.triangles().data()),
                    mesh.triangleCount(),
                    mesh.pointCount(),
                    adjacency.data(),
                    DirectX::VALIDATE_BACKFACING | DirectX::VALIDATE_BOWTIES,
                    &msgs);
  if (!msgs.empty()) {
    std::cerr << "WARNING: \n";
    std::wcerr << msgs;
  }

  // Clean
  std::vector<uint32_t> dups;
  bool                  breakBowties = true;
  hr = DirectX::Clean(reinterpret_cast<uint32_t*>(mesh.triangles().data()),
                      mesh.triangleCount(),
                      mesh.pointCount(),
                      adjacency.data(),
                      nullptr,
                      dups,
                      breakBowties);
  if (FAILED(hr)) {
    std::cerr << "ERROR: Failed mesh clean " << hr << '\n';
    return 1;
  }

  if (!dups.empty()) {
    std::cout << " [" << dups.size() << " vertex dups]\n";

    mesh.reservePoints(mesh.pointCount() + dups.size());
    for (auto dupIdx : dups) { mesh.addPoint(mesh.point(dupIdx)); }
  }

  // Perform UVAtlas isocharting
  std::cout << "Computing isochart atlas on mesh...\n";

  std::vector<DirectX::UVAtlasVertex> vb;
  std::vector<uint8_t>                ib;
  float                               outStretch = 0.F;
  size_t                              outCharts  = 0;
  std::vector<uint32_t>               facePartitioning;
  std::vector<uint32_t>               vertexRemapArray;
  const auto                          start = std::chrono::steady_clock::now();
  hr =
    UVAtlasCreate(reinterpret_cast<DirectX::XMFLOAT3*>(mesh.points().data()),
                  mesh.pointCount(),
                  reinterpret_cast<uint32_t*>(mesh.triangles().data()),
                  DXGI_FORMAT_R32_UINT,
                  mesh.triangleCount(),
                  params.maxCharts,
                  params.maxStretch,
                  texParamWidth,
                  texParamHeight,
                  params.gutter,
                  adjacency.data(),
                  nullptr,
                  nullptr,
                  UVAtlasCallback,
                  DirectX::UVATLAS_DEFAULT_CALLBACK_FREQUENCY,
                  params.uvOptions,
                  vb,
                  ib,
                  &facePartitioning,
                  &vertexRemapArray,
                  &outStretch,
                  &outCharts);
  if (FAILED(hr)) {
    std::cerr << "ERROR: Failed creating isocharts " << hr << '\n';
    exit(1);
    return 1;
  }

  namespace chrono = std::chrono;
  auto end         = chrono::steady_clock::now();
  auto deltams     = chrono::duration_cast<chrono::milliseconds>(end - start);
  std::cout << "(UVAtlas) Processing time: " << deltams.count() << " ms\n";
  std::cout << "(UVAtlas) Output # of charts: " << outCharts
            << ", resulting stretching " << outStretch << ", " << vb.size()
            << " verts\n";

  assert(ib.size() == 3 * mesh.triangles().size() * sizeof(uint32_t));
  memcpy(mesh.triangles().data(), ib.data(), ib.size());

  assert(vertexRemapArray.size() == vb.size());
  std::vector<Vec3<float>> pos(vertexRemapArray.size());
  hr = DirectX::UVAtlasApplyRemap(
    reinterpret_cast<DirectX::XMFLOAT3*>(mesh.points().data()),
    sizeof(DirectX::XMFLOAT3),
    mesh.pointCount(),
    vertexRemapArray.size(),
    vertexRemapArray.data(),
    reinterpret_cast<DirectX::XMFLOAT3*>(pos.data()));
  if (FAILED(hr)) {
    std::cerr << "ERROR: Failed applying atlas vertex remap (" << hr << ")\n";
    return 1;
  }
  //  std::swap(mesh.points(), pos); // float to double conversion issues
  mesh.points().clear();
  mesh.points().resize(pos.size());
  for (size_t i = 0; i < pos.size(); i++) { mesh.point(i) = pos[i]; }

  msgs.clear();
  DirectX::Validate(reinterpret_cast<uint32_t*>(mesh.triangles().data()),
                    mesh.triangleCount(),
                    mesh.pointCount(),
                    adjacency.data(),
                    DirectX::VALIDATE_DEFAULT,
                    &msgs);
  if (!msgs.empty()) {
    std::cerr << "WARNING: \n";
    std::wcerr << msgs;
  }

  // Copy isochart UVs into mesh
  mesh.reserveTexCoords(vb.size());
  std::transform(vb.begin(),
                 vb.end(),
                 std::back_inserter(mesh.texCoords()),
                 [](DirectX::UVAtlasVertex& vtx) {
                   return Vec2<float>{vtx.uv.x, vtx.uv.y};
                 });

  mesh.texCoordTriangles() = mesh.triangles();

  decimateTexture.convert(mesh);

  return 0;
}

//============================================================================
bool
TextureParametrization::generate_orthoAtlas(
  const TriangleMesh<MeshType>&              decimate,
  TriangleMesh<MeshType>&                    decimateTexture,
  std::vector<ConnectedComponent<MeshType>>& curCC,
  std::vector<ConnectedComponent<MeshType>>& previousCC,
  const VMCEncoderParameters&                params,
  std::string                                keepFilesPathPrefix,
  int32_t                                    frameIndex,
  std::vector<int>&                          catOrderGof,
  int&                                       maxOccupiedU,
  int&                                       maxOccupiedV,
  int32_t                                    frameIndexInAllGof,
  trasferInformation&                        trasferInfo) {
  TriangleMesh<float> input;
  input.convert(decimate);
  TriangleMesh<MeshType> mesh;
  mesh.convert(input);

  // Remove unwanted mesh components
  mesh.displacements().clear();
  mesh.colours().clear();
  mesh.texCoords().clear();
  mesh.texCoordTriangles().clear();
  mesh.normals().clear();
  mesh.normalTriangles().clear();

  std::cout << mesh.pointCount() << " vertices, " << mesh.triangleCount()
            << " faces\n";

  if (!mesh.pointCount() || !mesh.triangleCount()) {
    std::cerr << "ERROR: Invalid mesh\n";
    return false;
  }

  std::vector<std::shared_ptr<TriangleMesh<MeshType>>> projectedCCList;
  std::vector<int32_t>   projectedConnectedComponentsCategories;
  std::vector<Vec2<int>> bias;
  const auto             startCreate = std::chrono::steady_clock::now();
  create_patches(mesh,
                 projectedCCList,
                 projectedConnectedComponentsCategories,
                 bias,
                 params,
                 keepFilesPathPrefix);
  namespace chrono = std::chrono;
  auto endCreate   = chrono::steady_clock::now();
  auto deltaCreatems =
    chrono::duration_cast<chrono::milliseconds>(endCreate - startCreate);
#if DEBUG_ORTHO_CREATE_PATCH
  std::cout << "Patch Creation time: " << deltaCreatems.count() << " ms\n";
#endif
  TriangleMesh<MeshType> meshProjectedCCs;
  if (params.useRawUV) {
    for (int idx = 0; idx < projectedCCList.size(); idx++) {
      auto&                               ccMesh = projectedCCList[idx];
      vmesh::ConnectedComponent<MeshType> cc;
      //copy mesh
      cc.triangles()         = ccMesh->triangles();
      cc.points()            = ccMesh->points();
      cc.texCoords()         = ccMesh->texCoords();
      cc.texCoordTriangles() = ccMesh->texCoordTriangles();
      meshProjectedCCs.append(cc);
    }
    if (params.keepIntermediateFiles) {
      meshProjectedCCs.save(keepFilesPathPrefix + "_meshProjectedCCs.obj");
    }
  }
  chooseFaceFromPsnrInfo chooseFaceFromPsnrInfo;
  bool                   flag                       = true;
  bool                   generateFaceMetricListMode = true;
  bool                   updatePacking              = false;
  while (flag) {
    //choose faces for RAW subpatch and update CCs
    if (params.useRawUV && !generateFaceMetricListMode) {
      //Choose triangle
      std::vector<int>                       targetTriIdxListAllNewCC;
      std::vector<std::vector<int>>          targetTriIdxListPerCC;
      std::vector<Vec2<double>>              texCoordsRawListAll;
      std::vector<std::vector<Vec2<double>>> texCoordsRawListPerCC;
      std::vector<int> newCCidList(meshProjectedCCs.triangles().size(), -1);
      chooseRawTriangle(params,
                        projectedCCList,
                        meshProjectedCCs,
                        chooseFaceFromPsnrInfo,
                        targetTriIdxListAllNewCC,
                        frameIndexInAllGof,
                        keepFilesPathPrefix);
      if (targetTriIdxListAllNewCC.size() > 0) {
        updatePacking = true;
        //Set new UVs with UVAtlas for picked up meshes
        //create CC mesh
        TriangleMesh<MeshType> meshPickUpTriangles;
        //mapping decimatedTexture -> CCs
        std::vector<int> faceIdMappingCCsToUVAtlas(
          meshProjectedCCs.triangles().size(), -1);
        int triCountAdded = 0;
        for (auto& targetTriIdx : targetTriIdxListAllNewCC) {
          auto tri = meshProjectedCCs.triangle(targetTriIdx);
          //create mesh including only target triangles
          auto&          vertexList = meshPickUpTriangles.points();
          auto&          triList    = meshPickUpTriangles.triangles();
          Vec3<MeshType> vertex[3];
          for (int idx = 0; idx < 3; idx++) {
            int32_t posId = tri[idx];
            vertex[idx]   = meshProjectedCCs.point(posId);
          }
          //inserting vertex and triangle
          int vertIdx[3];
          for (int idx = 0; idx < 3; idx++) {
            auto vertPos =
              std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
            if (vertPos == vertexList.end()) {
              vertIdx[idx] = vertexList.size();
              vertexList.push_back(vertex[idx]);
            } else vertIdx[idx] = vertPos - vertexList.begin();
          }
          triList.push_back(
            vmesh::Triangle(vertIdx[0], vertIdx[1], vertIdx[2]));
          faceIdMappingCCsToUVAtlas[targetTriIdx] = triCountAdded;
          triCountAdded++;
        }
        //UVAtlas for picked up meshes
        TriangleMesh<MeshType> meshPickUpTrianglesUVAtlas =
          meshPickUpTriangles;
        TextureParametrization textureParametrization;
        textureParametrization.generate(meshPickUpTriangles,
                                        meshPickUpTrianglesUVAtlas,
                                        params.texParamWidth,
                                        params.texParamHeight,
                                        params);
        if (params.keepPreMesh) {
          meshPickUpTrianglesUVAtlas.save(
            keepFilesPathPrefix + "_meshPickUpTriangles_UVAtlas.obj");
        }
        //pick up UVs from UVAtlas
        for (auto& targetTriIdx : targetTriIdxListAllNewCC) {
          auto faceIdOld = faceIdMappingCCsToUVAtlas[targetTriIdx];
          auto triUV = meshPickUpTrianglesUVAtlas.texCoordTriangle(faceIdOld);
          for (int i = 0; i < 3; i++) {
            Vec2<double> uv = meshPickUpTrianglesUVAtlas.texCoord(triUV[i]);
            texCoordsRawListAll.push_back(uv);
          }
        }
        //Merge CCs
        int                           ccidBase = projectedCCList.size();
        std::vector<std::vector<int>> texCoordListPerCC;
        bool                          mergeConnectedRawPatchIntoOneCC = true;
        if (params.useRawUV && params.iDeriveTextCoordFromPos == 3) {
          //RAW subpatch should not be included into one triangle for iDeriveTextCoordFromPos=3
          mergeConnectedRawPatchIntoOneCC = false;
        }
        if (!mergeConnectedRawPatchIntoOneCC) {  //Not merge (1 triangle / CC)
          for (int idx = 0; idx < targetTriIdxListAllNewCC.size(); idx++) {
            auto& targetTriIdx = targetTriIdxListAllNewCC[idx];
            //add new CC
            int newCCid = targetTriIdxListPerCC.size();
            targetTriIdxListPerCC.resize(newCCid + 1);
            texCoordsRawListPerCC.resize(newCCid + 1);
            targetTriIdxListPerCC[newCCid].push_back(targetTriIdx);
            for (int i = 0; i < 3; i++) {
              Vec2<double> uv = texCoordsRawListAll[idx * 3 + i];
              texCoordsRawListPerCC[newCCid].push_back(uv);
            }
            newCCidList[targetTriIdx] = ccidBase + newCCid;
          }
        } else {  //Merge raw patches per CC
          for (int idx = 0; idx < targetTriIdxListAllNewCC.size(); idx++) {
            bool  found        = false;
            auto& targetTriIdx = targetTriIdxListAllNewCC[idx];
            auto  triTex = meshProjectedCCs.texCoordTriangle(targetTriIdx);
            for (int ccid = 0; ccid < texCoordListPerCC.size() && !found;
                 ccid++) {
              auto& texCoordList = texCoordListPerCC[ccid];
              auto  pos0 =
                std::find(texCoordList.begin(), texCoordList.end(), triTex[0]);
              auto pos1 =
                std::find(texCoordList.begin(), texCoordList.end(), triTex[1]);
              auto pos2 =
                std::find(texCoordList.begin(), texCoordList.end(), triTex[2]);
              if (pos0 != texCoordList.end() || pos1 != texCoordList.end()
                  || pos2 != texCoordList.end()) {  //included
                targetTriIdxListPerCC[ccid].push_back(targetTriIdx);
                for (int i = 0; i < 3; i++) {
                  Vec2<double> uv = texCoordsRawListAll[idx * 3 + i];
                  texCoordsRawListPerCC[ccid].push_back(uv);
                  texCoordListPerCC[ccid].push_back(triTex[i]);
                }
                newCCidList[targetTriIdx] = ccidBase + ccid;
                found                     = true;
              } else {
                found = false;
              }
            }
            if (!found) {  //add new CC
              int newCCid = targetTriIdxListPerCC.size();
              targetTriIdxListPerCC.resize(newCCid + 1);
              texCoordsRawListPerCC.resize(newCCid + 1);
              texCoordListPerCC.resize(newCCid + 1);
              targetTriIdxListPerCC[newCCid].push_back(targetTriIdx);
              for (int i = 0; i < 3; i++) {
                Vec2<double> uv = texCoordsRawListAll[idx * 3 + i];
                texCoordsRawListPerCC[newCCid].push_back(uv);
                texCoordListPerCC[newCCid].push_back(triTex[i]);
              }
              newCCidList[targetTriIdx] = ccidBase + newCCid;
            }
          }
        }
        //add Raw patches
        int rawUvId = params.use45DegreeProjection ? 18 : 6;
        //create new CCs
        auto newCCcount = targetTriIdxListPerCC.size();
        for (int ccid = 0; ccid < newCCcount; ccid++) {
          projectedConnectedComponentsCategories.push_back(rawUvId);
          std::shared_ptr<TriangleMesh<MeshType>> newCC;
          newCC.reset(new TriangleMesh<MeshType>);
          newCC->setMaterialLibrary(mesh.materialLibrary());
          projectedCCList.push_back(newCC);
        }
        std::vector<bool> ccEmptyFlag(projectedCCList.size(), false);
        int               tricountPre = 0;
        int               tricountCur = 0;
        for (int idx = 0; idx < projectedCCList.size(); idx++) {
          tricountPre += tricountCur;
          auto& ccMesh = projectedCCList[idx];
          tricountCur  = ccMesh->triangleCount();
          if (projectedConnectedComponentsCategories[idx] != rawUvId) {
            //add to newCC
            bool targetTriangleIncluded = false;
            for (int triIdx = 0; triIdx < ccMesh->triangles().size();
                 triIdx++) {
              auto triIdxAll = tricountPre + triIdx;
              auto newCCid   = newCCidList[triIdxAll];
              if (newCCid == -1) continue;
              auto& newCC      = projectedCCList[newCCid];
              auto& vertexList = newCC->points();
              auto& triList    = newCC->triangles();
              auto& uvList     = newCC->texCoords();
              auto& uvTriList  = newCC->texCoordTriangles();

              auto& targetTriIdxList =
                targetTriIdxListPerCC[newCCid - ccidBase];
              auto pos = std::find(
                targetTriIdxList.begin(), targetTriIdxList.end(), triIdxAll);
              auto& texCoordsRawList =
                texCoordsRawListPerCC[newCCid - ccidBase];
              if (pos != targetTriIdxList.end()) {  //included
                targetTriangleIncluded   = true;
                int            posInList = pos - targetTriIdxList.begin();
                auto           triTex    = ccMesh->texCoordTriangle(triIdx);
                auto           tri       = ccMesh->triangle(triIdx);
                Vec2<MeshType> uv[3];
                Vec3<MeshType> vertex[3];
                for (int idx = 0; idx < 3; idx++) {
                  int32_t uvId  = triTex[idx];
                  int32_t posId = tri[idx];
                  uv[idx]       = texCoordsRawList[posInList * 3 + idx];
                  vertex[idx]   = ccMesh->point(posId);
                }
                //inserting UV coordinates and triangle
                int uvIdx[3];
                for (int idx = 0; idx < 3; idx++) {
                  auto uvPos =
                    std::find(uvList.begin(), uvList.end(), uv[idx]);
                  //add uv coordinates and uv triangle
                  uvIdx[idx] = uvList.size();
                  uvList.push_back(uv[idx]);
                }
                uvTriList.push_back(
                  vmesh::Triangle(uvIdx[0], uvIdx[1], uvIdx[2]));
                //inserting vertex and triangle
                int vertIdx[3];
                for (int idx = 0; idx < 3; idx++) {
                  auto vertPos = std::find(
                    vertexList.begin(), vertexList.end(), vertex[idx]);
                  if (vertPos == vertexList.end()) {
                    //add uv coordinates and uv triangle
                    vertIdx[idx] = vertexList.size();
                    vertexList.push_back(vertex[idx]);
                  } else vertIdx[idx] = vertPos - vertexList.begin();
                }
                triList.push_back(
                  vmesh::Triangle(vertIdx[0], vertIdx[1], vertIdx[2]));
              }
            }
            //remove target triangle from old CC
            if (targetTriangleIncluded) {
              std::shared_ptr<TriangleMesh<MeshType>> tempCC;
              tempCC.reset(new TriangleMesh<MeshType>);
              auto& vertexListTemp = tempCC->points();
              auto& triListTemp    = tempCC->triangles();
              auto& uvListTemp     = tempCC->texCoords();
              auto& uvTriListTemp  = tempCC->texCoordTriangles();
              for (int triIdx = 0; triIdx < ccMesh->triangles().size();
                   triIdx++) {
                auto triIdxAll = tricountPre + triIdx;
                auto newCCid   = newCCidList[triIdxAll];
                bool found     = true;
                if (newCCid == -1) {
                  found = false;
                } else {
                  auto& targetTriIdxList =
                    targetTriIdxListPerCC[newCCid - ccidBase];
                  auto pos = std::find(targetTriIdxList.begin(),
                                       targetTriIdxList.end(),
                                       triIdxAll);
                  if (pos == targetTriIdxList.end()) found = false;
                }
                if (!found) {  //not included
                  auto           triTex = ccMesh->texCoordTriangle(triIdx);
                  auto           tri    = ccMesh->triangle(triIdx);
                  Vec2<MeshType> uv[3];
                  Vec3<MeshType> vertex[3];
                  for (int idx = 0; idx < 3; idx++) {
                    int32_t uvId  = triTex[idx];
                    int32_t posId = tri[idx];
                    uv[idx]       = ccMesh->texCoord(uvId);
                    vertex[idx]   = ccMesh->point(posId);
                  }
                  //inserting UV coordinates and triangle
                  int uvIdx[3];
                  for (int idx = 0; idx < 3; idx++) {
                    auto uvPos =
                      std::find(uvListTemp.begin(), uvListTemp.end(), uv[idx]);
                    if (uvPos == uvListTemp.end()) {
                      //add uv coordinates and uv triangle
                      uvIdx[idx] = uvListTemp.size();
                      uvListTemp.push_back(uv[idx]);
                    } else uvIdx[idx] = uvPos - uvListTemp.begin();
                  }
                  uvTriListTemp.push_back(
                    vmesh::Triangle(uvIdx[0], uvIdx[1], uvIdx[2]));
                  //inserting vertex and triangle
                  int vertIdx[3];
                  for (int idx = 0; idx < 3; idx++) {
                    auto vertPos = std::find(vertexListTemp.begin(),
                                             vertexListTemp.end(),
                                             vertex[idx]);
                    if (vertPos == vertexListTemp.end()) {
                      //add uv coordinates and uv triangle
                      vertIdx[idx] = vertexListTemp.size();
                      vertexListTemp.push_back(vertex[idx]);
                    } else vertIdx[idx] = vertPos - vertexListTemp.begin();
                  }
                  triListTemp.push_back(
                    vmesh::Triangle(vertIdx[0], vertIdx[1], vertIdx[2]));
                }
              }
              ccMesh->clear();
              ccMesh->setMaterialLibrary(mesh.materialLibrary());
              ccMesh->points()            = tempCC->points();
              ccMesh->triangles()         = tempCC->triangles();
              ccMesh->texCoords()         = tempCC->texCoords();
              ccMesh->texCoordTriangles() = tempCC->texCoordTriangles();
              if (
                ccMesh->triangles().size()
                < 1) {  //All triangles have been moved to RAW subpatch and CC are now empty
                ccEmptyFlag[idx] = true;
              }
            }
          }
        }
        //Remove empty CCs
        std::vector<std::shared_ptr<TriangleMesh<MeshType>>>
                             projectedCCListTemp;
        std::vector<int32_t> projectedConnectedComponentsCategoriesTemp;
        projectedCCListTemp.reserve(projectedCCList.size());
        projectedConnectedComponentsCategoriesTemp.reserve(
          projectedConnectedComponentsCategories.size());
        for (int idx = 0; idx < projectedCCList.size(); idx++) {
          if (!ccEmptyFlag[idx]) {
            projectedCCListTemp.push_back(projectedCCList[idx]);
            projectedConnectedComponentsCategoriesTemp.push_back(
              projectedConnectedComponentsCategories[idx]);
          }
        }
        projectedCCList = projectedCCListTemp;
        projectedConnectedComponentsCategories =
          projectedConnectedComponentsCategoriesTemp;
        //Convert UVs with BB for RAW patch
        for (int idx = 0; idx < projectedCCList.size(); idx++) {
          auto& ccMesh = projectedCCList[idx];
          if (projectedConnectedComponentsCategories[idx] == rawUvId) {
            auto& uvList = ccMesh->texCoords();
            auto  bbBox  = ccMesh->texCoordBoundingBox();
            // scale UV to be similar to the 3D area of original triangle
            for (int texTriIdx = 0;
                 texTriIdx < ccMesh->texCoordTriangles().size();
                 texTriIdx++) {
              for (int i = 0; i < 3; i++) {
                //shift UV
                auto& uv = uvList[texTriIdx * 3 + i];
                uv -= bbBox.min;
              }
            }
            double areaTotal        = 0;
            double UVAtlasAreaTotal = 0;
            double scaleMin         = std::numeric_limits<double>::max();
            double scaleMax         = std::numeric_limits<double>::min();
            for (int texTriIdx = 0;
                 texTriIdx < ccMesh->texCoordTriangles().size();
                 texTriIdx++) {
              auto tri = ccMesh->triangle(texTriIdx);
              //3D area
              auto   p0   = ccMesh->point(tri[0]);
              auto   p1   = ccMesh->point(tri[1]);
              auto   p2   = ccMesh->point(tri[2]);
              double area = computeTriangleArea(p0, p1, p2);
              //new UV area
              std::vector<Vec2<double>> newUV(3);
              for (int i = 0; i < 3; i++) {
                auto& uv = uvList[texTriIdx * 3 + i];
                //scale to position bit depth
                int postionSize = 1 << params.bitDepthPosition;
                uv[0] *= postionSize;
                uv[1] *= postionSize;
                newUV[i][0] = uv[0];
                newUV[i][1] = uv[1];
              }
              Vec3<double> uv3d0  = {newUV[0][0], newUV[0][1], 0};
              Vec3<double> uv3d1  = {newUV[1][0], newUV[1][1], 0};
              Vec3<double> uv3d2  = {newUV[2][0], newUV[2][1], 0};
              double UVAtlasArea  = computeTriangleArea(uv3d0, uv3d1, uv3d2);
              auto   scalePerFace = area / UVAtlasArea;
              if (scaleMin > scalePerFace) scaleMin = scalePerFace;
              if (scaleMax < scalePerFace) scaleMax = scalePerFace;
              areaTotal += area;
              UVAtlasAreaTotal += UVAtlasArea;
            }
            auto scale = sqrt(scaleMax);
            for (int texTriIdx = 0;
                 texTriIdx < ccMesh->texCoordTriangles().size();
                 texTriIdx++) {
              for (int i = 0; i < 3; i++) {
                auto& uv = uvList[texTriIdx * 3 + i];
                uv[0] *= scale;
                uv[1] *= scale;
              }
            }
          }
        }
        bias.resize(projectedCCList.size());
        if (params.keepIntermediateFiles) {
          TriangleMesh<MeshType> meshProjectedCCsUpdated;
          for (int idx = 0; idx < projectedCCList.size(); idx++) {
            auto&                               ccMesh = projectedCCList[idx];
            vmesh::ConnectedComponent<MeshType> cc;
            //copy mesh
            cc.triangles()         = ccMesh->triangles();
            cc.points()            = ccMesh->points();
            cc.texCoords()         = ccMesh->texCoords();
            cc.texCoordTriangles() = ccMesh->texCoordTriangles();
            meshProjectedCCsUpdated.append(cc);
          }
          meshProjectedCCsUpdated.save(keepFilesPathPrefix
                                       + "_meshProjectedCCs_updated.obj");
        }
      }
    }

    ////////////////////////////////////////////////////////////
    if (!params.useRawUV || updatePacking || generateFaceMetricListMode) {
      //packing
      const auto startPack = std::chrono::steady_clock::now();
      std::vector<ConnectedComponent<MeshType>> packedCCList;
      if (previousCC.size() == 0) {
        double adjustScaling = 1.0;
        switch (params.packingType) {
        default:
        case 0:  // default packing
        {
          while (!pack_patches_with_patch_scale_and_rotation(
            mesh,
            projectedCCList,
            projectedConnectedComponentsCategories,
            bias,
            packedCCList,
            params,
            adjustScaling,
            keepFilesPathPrefix)) {
            adjustScaling *= params.packingScaling;
#if DEBUG_ORTHO_PATCH_PACKING
            std::cout
              << "Packing did not fit, adjusting the scale of patches to "
              << adjustScaling << std::endl;
#endif
          }
          break;
        }
        case 1:  // tetris packing
        {
          while (!tetris_packing_patches_with_patch_scale_and_rotation(
            mesh,
            projectedCCList,
            projectedConnectedComponentsCategories,
            bias,
            packedCCList,
            params,
            adjustScaling,
            keepFilesPathPrefix)) {
            adjustScaling *= params.packingScaling;
#if DEBUG_ORTHO_PATCH_PACKING
            std::cout
              << "Packing did not fit, adjusting the scale of patches to "
              << adjustScaling << std::endl;
#endif
          }
          break;
        }
        case 2:  // projection packing
        {
          while (!projection_packing_patches_with_patch_scale_and_rotation(
            mesh,
            projectedCCList,
            projectedConnectedComponentsCategories,
            bias,
            packedCCList,
            params,
            adjustScaling,
            keepFilesPathPrefix)) {
            adjustScaling *= params.packingScaling;
#if DEBUG_ORTHO_PATCH_PACKING
            std::cout
              << "Packing did not fit, adjusting the scale of patches to "
              << adjustScaling << std::endl;
#endif
          }
          break;
        }
        case 3:  // projection packing stable
        {
          while (!projection_packing_patches_and_temporal_stabilization(
            mesh,
            projectedCCList,
            projectedConnectedComponentsCategories,
            bias,
            packedCCList,
            params,
            adjustScaling,
            keepFilesPathPrefix,
            frameIndex,
            catOrderGof,
            maxOccupiedU,
            maxOccupiedV)) {
            adjustScaling *= params.packingScaling;
#if DEBUG_ORTHO_PATCH_PACKING
            std::cout
              << "Packing did not fit, adjusting the scale of patches to "
              << adjustScaling << std::endl;
#endif
          }
          break;
        }
        }
      } else {
        //extract connected components from past frame
        auto& previousFrameProjectedCCList = previousCC;
        //now place the new patches, considering the past ones
        double adjustScaling = 1.0;
        while (
          !pack_patches_with_patch_scale_and_rotation_and_temporal_stabilization(
            mesh,
            projectedCCList,
            projectedConnectedComponentsCategories,
            bias,
            packedCCList,
            previousFrameProjectedCCList,
            params,
            adjustScaling,
            keepFilesPathPrefix)) {
          adjustScaling *= params.packingScaling;
#if DEBUG_ORTHO_PATCH_PACKING
          std::cout
            << "Packing did not fit, adjusting the scale of patches to "
            << adjustScaling << std::endl;
#endif
        }
      }
      namespace chrono = std::chrono;
      auto endPack     = chrono::steady_clock::now();
      auto deltaPackms =
        chrono::duration_cast<chrono::milliseconds>(endPack - startPack);
#if DEBUG_ORTHO_PATCH_PACKING
      std::cout << "Patch Packing time: " << deltaPackms.count() << " ms\n";
#endif
      decimateTexture.convert(mesh);
      curCC = packedCCList;

      std::cout << "(orthoAtlas) Processing time: "
                << deltaCreatems.count() + deltaPackms.count() << " ms\n";
      std::cout << "(orthoAtlas) Output # of charts: " << curCC.size() << ", "
                << mesh.pointCount() << " verts\n";

      if (params.useRawUV && !generateFaceMetricListMode
          && params.keepIntermediateFiles) {
        TriangleMesh<MeshType> rawUvPatchMesh, orthoPatchMesh;
        int                    rawUvId = params.use45DegreeProjection ? 18 : 6;
        for (int idx = 0; idx < packedCCList.size(); idx++) {
          auto&                               ccMesh = packedCCList[idx];
          vmesh::ConnectedComponent<MeshType> cc;
          //copy mesh
          cc.triangles()         = ccMesh.triangles();
          cc.points()            = ccMesh.points();
          cc.texCoords()         = ccMesh.texCoords();
          cc.texCoordTriangles() = ccMesh.texCoordTriangles();
          if (ccMesh.getProjection() == rawUvId) {
            rawUvPatchMesh.append(cc);
          } else {
            orthoPatchMesh.append(cc);
          }
        }
        orthoPatchMesh.save(keepFilesPathPrefix + "_orthoPatchMesh.obj");
        rawUvPatchMesh.save(keepFilesPathPrefix + "_rawUvPatchMesh.obj");
      }

      //create chooseFaceFromPsnrInfo (transfer & create SN lists)
      if (params.useRawUV && generateFaceMetricListMode) {
        createChooseFaceFromPsnrInfo(chooseFaceFromPsnrInfo,
                                     params,
                                     trasferInfo,
                                     decimateTexture,
                                     meshProjectedCCs,
                                     packedCCList,
                                     frameIndexInAllGof,
                                     keepFilesPathPrefix);
      }
    }
    if (params.useRawUV && generateFaceMetricListMode) {
      flag                       = true;
      generateFaceMetricListMode = false;
    } else {
      flag = false;
    }
  }
  return true;
}

bool
TextureParametrization::checkOcclusion(TriangleMesh<MeshType>& refMesh,
                                       int32_t                 category,
                                       TriangleMesh<MeshType>& curMesh) {
  std::vector<vmesh::Box2<MeshType>> triAABBList;
  std::vector<int64_t>               edgeList;
  bool                               canProject = true;
  for (int fIdx = 0; fIdx < refMesh.triangleCount(); fIdx++) {
    auto tri = refMesh.triangle(fIdx);
    auto texTri = refMesh.texCoordTriangle(fIdx);
    Vec2<MeshType>        uvCC[3];
    Vec3<MeshType>        vertexCC[3];
    vmesh::Box2<MeshType> triBB;
    for (int idx = 0; idx < 3; idx++) {
      vertexCC[idx] = refMesh.point(tri[idx]);
      uvCC[idx]     = refMesh.texCoord(texTri[idx]);
      triBB.enclose(uvCC[idx]);
      auto edgeIdx = EdgeIndex(tri[idx], tri[(idx + 1) % 3]);
      //if (std::find(edgeList.begin(), edgeList.end(), edgeIdx) == edgeList.end())
      edgeList.push_back(edgeIdx);
    }
    triAABBList.push_back(triBB);
  }
  auto&                 vertexList = refMesh.points();
  auto&                 triList    = refMesh.triangles();
  auto&                 uvList     = refMesh.texCoords();
  auto&                 uvTriList  = refMesh.texCoordTriangles();
  auto                  curTri     = curMesh.triangle(0);
  Vec2<MeshType>        uv[3];
  Vec3<MeshType>        vertex[3];
  vmesh::Box2<MeshType> triBB;
  bool                  vertexInList[3];
  int                   vertexCCIndex[3];
  bool                  edgeInList[3];
  for (int idx = 0; idx < 3; idx++) {
    vertex[idx] = curMesh.point(curTri[idx]);
    uv[idx]     = projectPoint(curMesh.point(curTri[idx]), category);
    triBB.enclose(uv[idx]);
    vertexInList[idx]  = false;
    vertexCCIndex[idx] = -1;
  }
  //check if the bounding box overlap
  for (int idx = 0; idx < 3; idx++) {
    auto vertPos =
      std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
    if (vertPos != vertexList.end()) {
      vertexInList[idx]  = true;
      vertexCCIndex[idx] = vertPos - vertexList.begin();
    }
  }
  //check if the edges are in the list already
  for (int idx = 0; idx < 3; idx++) {
    edgeInList[idx] = true;
    if (vertexInList[0] && vertexInList[1]) {
      if (std::find(
            edgeList.begin(),
            edgeList.end(),
            EdgeIndex(vertexCCIndex[idx], vertexCCIndex[(idx + 1) % 3]))
          != edgeList.end()) {
        //edge has already been tested by another triangle
        edgeInList[idx] = false;
      }
    }
  }
  for (int triIdx = 0; triIdx < triAABBList.size(); triIdx++) {
    auto& projTriBB = triAABBList[triIdx];
    if (projTriBB.intersects(triBB)) {
      // check if the vertices are inside the triangle
      for (int idx = 0; idx < 3 && canProject; idx++) {
        if (!vertexInList[idx]) {
          if (isInsideTriangleUsingArea(uvList[uvTriList[triIdx][0]],
                                        uvList[uvTriList[triIdx][1]],
                                        uvList[uvTriList[triIdx][2]],
                                        uv[idx])) {
            canProject = false;
            return canProject;
          }
        }
      }
      // check if the triangle surface will occlude any vertices
      for (int idx = 0; idx < 3 && canProject; idx++) {
        auto& projUV = uvList[uvTriList[triIdx][idx]];
        if ((uv[0] != projUV) && (uv[1] != projUV) && (uv[2] != projUV)) {
          if (isInsideTriangleUsingArea(uv[0], uv[1], uv[2], projUV)) {
            canProject = false;
            return canProject;
          }
        }
      }
      // check if the edges intersect
      for (int idxProj = 0; idxProj < 3 && canProject; idxProj++) {
        auto& uv1Idx = uvTriList[triIdx][idxProj];
        auto& uv2Idx = uvTriList[triIdx][(idxProj + 1) % 3];
        auto& uv1    = uvList[uv1Idx];
        auto& uv2    = uvList[uv2Idx];
        for (int idx = 0; idx < 3 && canProject; idx++) {
          if (edgeInList[idx]) {
            bool testEdge = true;
            if (vertexInList[idx]) {
              if ((uv1Idx == vertexCCIndex[idx])
                  || (uv2Idx == vertexCCIndex[idx]))
                testEdge = false;
            }
            if (vertexInList[(idx + 1) % 3]) {
              if ((uv1Idx == vertexCCIndex[(idx + 1) % 3])
                  || (uv2Idx == vertexCCIndex[(idx + 1) % 3]))
                testEdge = false;
            }
            if (testEdge) {
              if (edgesCross(uv1, uv2, uv[idx], uv[(idx + 1) % 3])) {
                canProject = false;
                return canProject;
              }
            }
          }
        }
      }
    }
  }
  return canProject;
}

//============================================================================
bool
TextureParametrization::create_patches(
  TriangleMesh<MeshType>&                               mesh,
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& finalCCList,
  std::vector<int32_t>&       finalConnectedComponentsCategories,
  std::vector<Vec2<int>>&     bias,
  const VMCEncoderParameters& params,
  std::string                 keepFilesPathPrefix) {
  int           orientationCount = params.use45DegreeProjection ? 18 : 6;
  Vec3<double>* orientations =
    params.use45DegreeProjection ? PROJDIRECTION18 : PROJDIRECTION6;
  //create connected components and project them into patches (local UV)
  std::vector<int32_t> connectedComponentsCategories;
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>> connectedComponents;
  const auto                                           ccCount0 =
    ExtractConnectedComponentsByNormal(mesh,
                                       orientations,
                                       orientationCount,
                                       connectedComponentsCategories,
                                       &connectedComponents,
                                       params.useVertexCriteria,
                                       params.bUseSeedHistogram,
                                       params.strongGradientThreshold,
                                       params.maxCCAreaRatio,
                                       params.maxNumFaces,
                                       params.bFaceClusterMerge,
                                       params.lambdaRDMerge,
                                       params.check2DConnectivity,
                                       params.keepIntermediateFiles,
                                       keepFilesPathPrefix);
#if DEBUG_ORTHO_CREATE_PATCH
  std::cout << "Created " << connectedComponents.size() << " patches"
            << std::endl;
#endif

  //check for overlapping triangles, and break the connected component if necessary
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>> projectedCCList;
  std::vector<int32_t> projectedConnectedComponentsCategories;
  int                  numTotalProjectedTriangles = 0;

  int idxCC = 0;
  for (auto& ccComp : connectedComponents) {
    int32_t ccCategory = connectedComponentsCategories[idxCC++];
    //get triangle normal direction
    std::vector<Vec3<MeshType>> triangleNormalsCC;
    if ((params.adjustNormalDirection))
      ccComp->computeTriangleNormals(triangleNormalsCC);
    int               numProjectedTriangles = 0;
    std::vector<bool> triStatus;
    triStatus.resize(ccComp->triangles().size(), false);
    while (ccComp->triangles().size() > numProjectedTriangles) {
      std::shared_ptr<TriangleMesh<MeshType>> projectedCC;
      projectedCC.reset(new TriangleMesh<MeshType>);
      projectedCCList.push_back(projectedCC);
      //determine the best orientation for the connected components
      if ((numProjectedTriangles != 0) && (params.adjustNormalDirection)) {
        std::vector<double> dotProdNormal;
        dotProdNormal.resize(orientationCount, 0.0);
        std::vector<bool> bAllowOrientation;
        bAllowOrientation.resize(orientationCount, true);
        int triNormalIdx = 0;
        for (auto tri : ccComp->triangles()) {
          if (!triStatus[triNormalIdx]) {
            for (int i = 0; i < orientationCount; i++) {
              double dotProd =
                triangleNormalsCC[triNormalIdx] * orientations[i];
              if (dotProd > 0.0) dotProdNormal[i] += 1 / dotProd;
              else if (dotProd < 0.0) bAllowOrientation[i] = false;
            }
          }
          triNormalIdx++;
        }
        //now check the best category
        double bestScore = std::numeric_limits<double>::max();
        for (size_t j = 0; j < orientationCount; ++j) {
          if (bAllowOrientation[j] && (dotProdNormal[j] < bestScore)) {
            bestScore  = dotProdNormal[j];
            ccCategory = j;
          }
        }
      }
      projectedConnectedComponentsCategories.push_back(ccCategory);
      projectedCC->setMaterialLibrary(mesh.materialLibrary());
      auto& vertexList = projectedCC->points();
      auto& triList    = projectedCC->triangles();
      auto& uvList     = projectedCC->texCoords();
      auto& uvTriList  = projectedCC->texCoordTriangles();
      std::vector<vmesh::Box2<MeshType>> triAABBList;
      triAABBList.clear();
      std::vector<int64_t> edgeList;
      edgeList.clear();
      int triIdx = 0;
      for (auto tri : ccComp->triangles()) {
        if (triStatus[triIdx]) {
          triIdx++;
          continue;
        }
        //create 2D orthographic projection
        Vec2<MeshType>        uv[3];
        Vec3<MeshType>        vertex[3];
        bool                  vertexInList[3];
        int                   vertexCCIndex[3];
        bool                  edgeInList[3];
        vmesh::Box2<MeshType> triBB;
        for (int idx = 0; idx < 3; idx++) {
          vertex[idx] = ccComp->point(tri[idx]);
          uv[idx]     = projectPoint(ccComp->point(tri[idx]), ccCategory);
          triBB.enclose(uv[idx]);
          // assuming vertex is not in the list
          vertexInList[idx]  = false;
          vertexCCIndex[idx] = -1;
        }
#if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
        std::cout << "tri[" << triIdx << "]:{(" << uv[0][0] << "," << uv[0][1]
                  << "),(" << uv[1][0] << "," << uv[1][1] << "),(" << uv[2][0]
                  << "," << uv[2][1] << ")}" << std::endl;
#endif
        bool canProject = true;
        //check if triangle can be projected, skip this check for the first triangle
        if (!triAABBList.empty()) {
          //check if the vertices are in the list already
          for (int idx = 0; idx < 3; idx++) {
            auto vertPos =
              std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
            if (vertPos != vertexList.end()) {
              vertexInList[idx]  = true;
              vertexCCIndex[idx] = vertPos - vertexList.begin();
            }
          }
          //check if the edges are in the list already
          for (int idx = 0; idx < 3; idx++) {
            edgeInList[idx] = true;
            if (vertexInList[0] && vertexInList[1]) {
              if (std::find(edgeList.begin(),
                            edgeList.end(),
                            EdgeIndex(vertexCCIndex[idx],
                                      vertexCCIndex[(idx + 1) % 3]))
                  != edgeList.end()) {
                //edge has already been tested by another triangle
                edgeInList[idx] = false;
              }
            }
          }
          //check if the bounding box overlap
          for (int triIdx = 0; triIdx < triAABBList.size(); triIdx++) {
            auto& projTriBB = triAABBList[triIdx];
            if (projTriBB.intersects(triBB)) {
              // check if the vertices are inside the triangle
              for (int idx = 0; idx < 3 && canProject; idx++) {
                if (!vertexInList[idx]) {
                  if (isInsideTriangleUsingArea(uvList[uvTriList[triIdx][0]],
                                                uvList[uvTriList[triIdx][1]],
                                                uvList[uvTriList[triIdx][2]],
                                                uv[idx])) {
                    canProject = false;
#if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
                    std::cout << "[!canProject] New UV(" << uv[idx][0] << ","
                              << uv[idx][1] << ") is inside triangle["
                              << triIdx << "]" << std::endl;
#endif
                  }
                }
              }
              // check if the triangle surface will occlude any vertices
              for (int idx = 0; idx < 3 && canProject; idx++) {
                auto& projUV = uvList[uvTriList[triIdx][idx]];
                if ((uv[0] != projUV) && (uv[1] != projUV)
                    && (uv[2] != projUV)) {
                  if (isInsideTriangleUsingArea(uv[0], uv[1], uv[2], projUV)) {
                    canProject = false;
#if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
                    std::cout << "[!canProject] UV(" << projUV[0] << ","
                              << projUV[1] << ") is inside new triangle"
                              << std::endl;
#endif
                  }
                }
              }
              // check if the edges intersect
              for (int idxProj = 0; idxProj < 3 && canProject; idxProj++) {
                auto& uv1Idx = uvTriList[triIdx][idxProj];
                auto& uv2Idx = uvTriList[triIdx][(idxProj + 1) % 3];
                auto& uv1    = uvList[uv1Idx];
                auto& uv2    = uvList[uv2Idx];
                for (int idx = 0; idx < 3 && canProject; idx++) {
                  if (edgeInList[idx]) {
                    bool testEdge = true;
                    if (vertexInList[idx]) {
                      if ((uv1Idx == vertexCCIndex[idx])
                          || (uv2Idx == vertexCCIndex[idx]))
                        testEdge = false;
                    }
                    if (vertexInList[(idx + 1) % 3]) {
                      if ((uv1Idx == vertexCCIndex[(idx + 1) % 3])
                          || (uv2Idx == vertexCCIndex[(idx + 1) % 3]))
                        testEdge = false;
                    }
                    if (testEdge) {
                      if (edgesCross(uv1, uv2, uv[idx], uv[(idx + 1) % 3])) {
                        canProject = false;
#if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
                        std::cout << "[!canProject] edges [(" << uv1[0] << ","
                                  << uv1[1] << ")(" << uv2[0] << "," << uv2[1]
                                  << ")] and [(" << uv[idx][0] << ","
                                  << uv[idx][1] << ")(" << uv[(idx + 1) % 3][0]
                                  << "," << uv[(idx + 1) % 3][1] << ")] cross"
                                  << std::endl;
#endif
                      }
                    }
                  }
                }
              }
            }
          }
        }
        //add the triangle to the projected connected component
        if (canProject) {
          triStatus[triIdx] = true;
          numTotalProjectedTriangles++;
          numProjectedTriangles++;
          //inserting UV coordinates and triangle
          for (int idx = 0; idx < 3; idx++) {
            if (!vertexInList[idx]) {
              //add uv coordinates and uv triangle
              vertexCCIndex[idx] = uvList.size();
              uvList.push_back(uv[idx]);
              vertexList.push_back(vertex[idx]);
            }
          }
          uvTriList.push_back(vmesh::Triangle(
            vertexCCIndex[0], vertexCCIndex[1], vertexCCIndex[2]));
          triList.push_back(vmesh::Triangle(
            vertexCCIndex[0], vertexCCIndex[1], vertexCCIndex[2]));
          //inserting the edge
          for (int idx = 0; idx < 3; idx++) {
            auto edgeIdx =
              EdgeIndex(vertexCCIndex[idx], vertexCCIndex[(idx + 1) % 3]);
            //if (std::find(edgeList.begin(), edgeList.end(), edgeIdx) == edgeList.end())
            edgeList.push_back(edgeIdx);
          }
          //insert the bounding box
          triAABBList.push_back(triBB);
        }
        triIdx++;
      }
    }
  }
#if DEBUG_ORTHO_CREATE_PATCH
  std::cout << "Created " << projectedCCList.size() << " projected patches"
            << std::endl;
  {
    int                    ccIdx = 0;
    TriangleMesh<MeshType> meshDebug;
    double                 totalPerimeter = 0;
    double                 totalStretchL2 = 0;
    double                 minStretchL2   = std::numeric_limits<double>::max();
    double                 maxStretchL2   = std::numeric_limits<double>::min();
    for (auto cc : projectedCCList) {
      meshDebug.append(*cc);
#  if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
      if (params.keepIntermediateFiles) {
        auto prefixDEBUG =
          keepFilesPathPrefix + "_proj_CC#" + std::to_string(ccIdx);
        cc->save(prefixDEBUG + ".obj");
      }
      std::cout << "CC[" << ccIdx << "] -> perimeter(" << cc->perimeter()
                << "), stretchL2("
                << cc->stretchL2(projectedConnectedComponentsCategories[ccIdx])
                << ")" << std::endl;
#  endif
      totalPerimeter += cc->perimeter();
      auto l2 = cc->stretchL2(projectedConnectedComponentsCategories[ccIdx]);
      totalStretchL2 += l2;
      if (l2 > maxStretchL2) maxStretchL2 = l2;
      if (l2 < minStretchL2) minStretchL2 = l2;
      ccIdx++;
    }
    std::cout << "(" << ccIdx
              << " CC after projection) TOTAL PERIMETER: " << totalPerimeter
              << ", TOTAL STRETCH L2: " << totalStretchL2
              << ", AVG. STRETCH L2: " << totalStretchL2 / (double)ccIdx
              << ", MIN. STRETCH L2: " << minStretchL2
              << ", MAX. STRETCH L2: " << maxStretchL2 << std::endl;
    if (params.keepIntermediateFiles) {
      meshDebug.texCoords().clear();
      meshDebug.texCoordTriangles().clear();
      meshDebug.reserveTexCoords(meshDebug.triangleCount() * 3);
      meshDebug.reserveTexCoordTriangles(meshDebug.triangleCount());
      int idxCC       = 0;
      int idxTriangle = 0;
      int side        = 100;
      for (auto cc : projectedCCList) {
        float x =
          ((projectedConnectedComponentsCategories[idxCC] % 9) + 0.5) / (9.0);
        float y = 1
                  - ((projectedConnectedComponentsCategories[idxCC] / 9) + 0.5)
                      / (2.0);
        for (int idx = 0; idx < cc->triangles().size(); idx++) {
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoordTriangles().push_back(vmesh::Triangle(
            3 * idxTriangle, 3 * idxTriangle + 1, 3 * idxTriangle + 2));
          idxTriangle++;
        }
        idxCC++;
      }
      meshDebug.setMaterialLibrary(
        vmesh::basename(keepFilesPathPrefix + "_debug_categories.mtl"));
      meshDebug.save(keepFilesPathPrefix
                     + "_debug_categories_after_projection.obj");
    }
  }
#endif

  if (params.check2DConnectivity) {
    int idxCC = 0;
    for (auto& patch : projectedCCList) {
      int32_t ccCategory = projectedConnectedComponentsCategories[idxCC++];
      std::vector<int> partition;
      int              ccCount = ExtractConnectedComponentsByEdge(
        patch->texCoordTriangles(),
        patch->texCoordCount(),
        *patch,
        partition,
        !(params.iDeriveTextCoordFromPos == 3));
      for (int idxProjPatch = 0; idxProjPatch < ccCount; idxProjPatch++) {
        std::shared_ptr<TriangleMesh<MeshType>> finalCC;
        finalCC.reset(new TriangleMesh<MeshType>);
        ;
        finalCC->setMaterialLibrary(mesh.materialLibrary());
        auto& vertexList = finalCC->points();
        auto& triList    = finalCC->triangles();
        auto& uvList     = finalCC->texCoords();
        auto& uvTriList  = finalCC->texCoordTriangles();
        finalCCList.push_back(finalCC);
        for (int idxPartition = 0; idxPartition < partition.size();
             idxPartition++) {
          if (partition[idxPartition] == idxProjPatch) {
            Vec2<MeshType> uv[3];
            Vec3<MeshType> vertex[3];
            for (int idx = 0; idx < 3; idx++) {
              uv[idx] =
                patch->texCoord(patch->texCoordTriangle(idxPartition)[idx]);
              vertex[idx] = patch->point(patch->triangle(idxPartition)[idx]);
            }
            //inserting UV coordinates and triangle
            int uvIdx[3];
            for (int idx = 0; idx < 3; idx++) {
              auto uvPos = std::find(uvList.begin(), uvList.end(), uv[idx]);
              if (uvPos == uvList.end()) {
                //add uv coordinates and uv triangle
                uvIdx[idx] = uvList.size();
                uvList.push_back(uv[idx]);
              } else uvIdx[idx] = uvPos - uvList.begin();
            }
            uvTriList.push_back(vmesh::Triangle(uvIdx[0], uvIdx[1], uvIdx[2]));
            //inserting vertex and triangle
            int vertIdx[3];
            for (int idx = 0; idx < 3; idx++) {
              auto vertPos =
                std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
              if (vertPos == vertexList.end()) {
                //add uv coordinates and uv triangle
                vertIdx[idx] = vertexList.size();
                vertexList.push_back(vertex[idx]);
              } else vertIdx[idx] = vertPos - vertexList.begin();
            }
            triList.push_back(
              vmesh::Triangle(vertIdx[0], vertIdx[1], vertIdx[2]));
          }
        }
        if (params.applyReprojection) {
          //determine the best orientation for the connected components
          int                       oriCategory = ccCategory;
          std::vector<Vec3<double>> triangleNormalsCC;
          finalCC->computeTriangleNormals(triangleNormalsCC);
          std::vector<double> dotProdNormal;
          dotProdNormal.resize(orientationCount, 0.0);
          std::vector<double> bAllowOrientation;
          bAllowOrientation.resize(orientationCount, true);
          int triNormalIdx = 0;
          for (auto tri : finalCC->triangles()) {
            for (int i = 0; i < orientationCount; i++) {
              double dotProd =
                triangleNormalsCC[triNormalIdx] * orientations[i];
              if (dotProd > 0.0) dotProdNormal[i] += 1 / dotProd;
              else if (dotProd < 0.0) bAllowOrientation[i] = false;
            }
            triNormalIdx++;
          }
          //now check the best category
          double bestScore = std::numeric_limits<double>::max();
          for (size_t j = 0; j < orientationCount; ++j) {
            if (bAllowOrientation[j] && (dotProdNormal[j] < bestScore)) {
              bestScore  = dotProdNormal[j];
              ccCategory = j;
            }
          }
          if (ccCategory != oriCategory) {
            std::vector<bool> isProjectFlag(finalCC->pointCount());
            for (int fIdx = 0; fIdx < finalCC->triangleCount(); fIdx++) {
                auto tri = finalCC->triangle(fIdx);
                auto texTri = finalCC->texCoordTriangle(fIdx);
              //create 2D orthographic projection
              for (int idx = 0; idx < 3; idx++) {
                if (isProjectFlag[tri[idx]]) continue;
                auto& uv = finalCC->texCoord(texTri[idx]);
                uv       = projectPoint(finalCC->point(tri[idx]), ccCategory);
                isProjectFlag[tri[idx]] = true;
              }
            }
          }
        }
        finalConnectedComponentsCategories.push_back(ccCategory);
      }
    }
  } else {
    finalCCList = projectedCCList;
    finalConnectedComponentsCategories =
      projectedConnectedComponentsCategories;
  }
  if (params.mergeSmallCC) {
    auto tempCCList = finalCCList;
    auto tempConnectedComponentsCategories =
      finalConnectedComponentsCategories;
    finalCCList.clear();
    finalConnectedComponentsCategories.clear();
    for (int tempCCIdx = 0; tempCCIdx < tempCCList.size(); tempCCIdx++) {
      auto& tempCC = tempCCList[tempCCIdx];
      auto  vertex = tempCC->points();
      if (tempCC->triangleCount() > params.smallCCFaceNum) {
        finalCCList.push_back(tempCC);
        finalConnectedComponentsCategories.push_back(
          tempConnectedComponentsCategories[tempCCIdx]);
      } else {
        double maxDot = std::numeric_limits<double>::max();
        ;
        int ccidx = -1;
        for (int c = 0; c < tempCCList.size(); c++) {
          if (c == tempCCIdx || tempConnectedComponentsCategories[c] == -1) {
            continue;
          }
          auto& candidateCC = tempCCList[c];
          auto& vertexList  = candidateCC->points();
          auto& triList     = candidateCC->triangles();
          auto& uvList      = candidateCC->texCoords();
          auto& uvTriList   = candidateCC->texCoordTriangles();
          int   dupVertex   = 0;
          for (int idx = 0; idx < vertex.size(); idx++) {
            auto vertPos =
              std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
            if (vertPos != vertexList.end()) { dupVertex++; }
          }
          if (dupVertex > 1) {
            bool canProject =
              checkOcclusion(*candidateCC.get(),
                             tempConnectedComponentsCategories[c],
                             *tempCC.get());
            if (canProject) {
              std::vector<Vec3<MeshType>> triangleNormalsCC;
              tempCC->computeTriangleNormals(triangleNormalsCC);
              double dotProdNormal     = 0.0;
              bool   bAllowOrientation = true;
              int    triNormalIdx      = 0;
              for (auto tri : tempCC->triangles()) {
                double dotProd =
                  triangleNormalsCC[triNormalIdx]
                  * orientations[tempConnectedComponentsCategories[c]];
                if (dotProd > 0.0) dotProdNormal += 1 / dotProd;
                else if (dotProd < 0.0) bAllowOrientation = false;
                triNormalIdx++;
              }
              if (bAllowOrientation && (dotProdNormal < maxDot)) {
                maxDot = dotProdNormal;
                ccidx  = c;
              }
            }
          }
        }
        bool canMerge = true;
        if (canMerge && ccidx != -1) {
          auto& curTriList  = tempCC->triangles();
          auto& candidateCC = tempCCList[ccidx];
          auto& vertexList  = candidateCC->points();
          auto& triList     = candidateCC->triangles();
          auto& uvList      = candidateCC->texCoords();
          auto& uvTriList   = candidateCC->texCoordTriangles();
          for (auto tri : curTriList) {
            Vec2<MeshType> uv[3];
            Vec3<MeshType> vertex[3];
            for (int idx = 0; idx < 3; idx++) {
              vertex[idx] = tempCC->point(tri[idx]);
              uv[idx]     = projectPoint(tempCC->point(tri[idx]),
                                     tempConnectedComponentsCategories[ccidx]);
            }
            //inserting UV coordinates and triangle
            int uvIdx[3];
            for (int idx = 0; idx < 3; idx++) {
              auto uvPos = std::find(uvList.begin(), uvList.end(), uv[idx]);
              if (uvPos == uvList.end()) {
                //add uv coordinates and uv triangle
                uvIdx[idx] = uvList.size();
                uvList.push_back(uv[idx]);
              } else uvIdx[idx] = uvPos - uvList.begin();
            }
            uvTriList.push_back(vmesh::Triangle(uvIdx[0], uvIdx[1], uvIdx[2]));
            //inserting vertex and triangle
            int vertIdx[3];
            for (int idx = 0; idx < 3; idx++) {
              auto vertPos =
                std::find(vertexList.begin(), vertexList.end(), vertex[idx]);
              if (vertPos == vertexList.end()) {
                //add uv coordinates and uv triangle
                vertIdx[idx] = vertexList.size();
                vertexList.push_back(vertex[idx]);
              } else vertIdx[idx] = vertPos - vertexList.begin();
            }
            triList.push_back(
              vmesh::Triangle(vertIdx[0], vertIdx[1], vertIdx[2]));
            tempConnectedComponentsCategories[tempCCIdx] = -1;
          }
        } else {
          finalCCList.push_back(tempCC);
          finalConnectedComponentsCategories.push_back(
            tempConnectedComponentsCategories[tempCCIdx]);
        }
      }
    }
  }
  //now adjust the UV coordinates to remove the bias
  bias.resize(finalCCList.size());
  for (int finalCCIdx = 0; finalCCIdx < finalCCList.size(); finalCCIdx++) {
    auto& finalCC    = finalCCList[finalCCIdx];
    auto& ccCategory = finalConnectedComponentsCategories[finalCCIdx];
    auto& uvList     = finalCC->texCoords();
    auto  bbBox      = finalCC->texCoordBoundingBox();
    int   V1         = std::floor(bbBox.min[1]);
    V1               = Clamp(
      V1, 0, ((1 << params.bitDepthTexCoord) - 1));  // clamping uv values
    int U1;
    int projection = ccCategory;
    if ((projection == 0) || (projection == 4) || (projection == 5)
        || (projection == 6) || (projection == 9) || (projection == 11)
        || (projection == 12) || (projection == 14) || (projection == 15)) {
      U1 = std::ceil(bbBox.max[0]);
    } else U1 = std::floor(bbBox.min[0]);
    U1 = Clamp(
      U1, 0, ((1 << params.bitDepthTexCoord) - 1));  // clamping uv values
    bias[finalCCIdx] = Vec2<int>(U1, V1);
    for (auto& uv : uvList) {
      //clamping uv values -> this will be guaranteed by encoding
      uv[0] = Clamp(uv[0], 0.0, (double)((1 << params.bitDepthTexCoord) - 1));
      uv[1] = Clamp(uv[1], 0.0, (double)((1 << params.bitDepthTexCoord) - 1));
      //remove bbox y-bias
      uv[1] -= V1;
      // invert u-axis to keep same winding and remove x-bias
      if ((projection == 0) || (projection == 4) || (projection == 5)
          || (projection == 6) || (projection == 9) || (projection == 11)
          || (projection == 12) || (projection == 14) || (projection == 15)) {
        uv[0] = U1 - uv[0];
      } else uv[0] -= U1;
    }
  }
  std::cout << "Total of " << finalCCList.size() << " final patches"
            << std::endl;
#if DEBUG_ORTHO_CREATE_PATCH
  std::cout << "Total of " << finalCCList.size() << " final patches"
            << std::endl;
  {
    int                    ccIdx = 0;
    TriangleMesh<MeshType> meshDebug;
    double                 totalPerimeter = 0;
    double                 totalStretchL2 = 0;
    double                 minStretchL2   = std::numeric_limits<double>::max();
    double                 maxStretchL2   = std::numeric_limits<double>::min();
    for (auto cc : finalCCList) {
      meshDebug.append(*cc);
#  if DEBUG_ORTHO_CREATE_PATCH_VERBOSE
      if (params.keepIntermediateFiles) {
        auto prefixDEBUG =
          keepFilesPathPrefix + "_final_CC#" + std::to_string(ccIdx);
        cc->save(prefixDEBUG + ".obj");
      }
      std::cout << "CC[" << ccIdx << "] -> perimeter(" << cc->perimeter()
                << "), stretchL2("
                << cc->stretchL2(finalConnectedComponentsCategories[ccIdx])
                << ")" << std::endl;
#  endif
      totalPerimeter += cc->perimeter();
      auto l2 = cc->stretchL2(finalConnectedComponentsCategories[ccIdx]);
      totalStretchL2 += l2;
      if (l2 > maxStretchL2) maxStretchL2 = l2;
      if (l2 < minStretchL2) minStretchL2 = l2;
      ccIdx++;
    }
    std::cout << "( " << ccIdx << " CC ) TOTAL PERIMETER: " << totalPerimeter
              << ", TOTAL STRETCH L2: " << totalStretchL2
              << ", AVG. STRETCH L2: " << totalStretchL2 / (double)ccIdx
              << ", MIN. STRETCH L2: " << minStretchL2
              << ", MAX. STRETCH L2: " << maxStretchL2 << std::endl;
    if (params.keepIntermediateFiles) {
      meshDebug.texCoords().clear();
      meshDebug.texCoordTriangles().clear();
      meshDebug.reserveTexCoords(meshDebug.triangleCount() * 3);
      meshDebug.reserveTexCoordTriangles(meshDebug.triangleCount());
      int idxCC       = 0;
      int idxTriangle = 0;
      int side        = 100;
      for (auto cc : finalCCList) {
        float x =
          ((finalConnectedComponentsCategories[idxCC] % 9) + 0.5) / (9.0);
        float y =
          1 - ((finalConnectedComponentsCategories[idxCC] / 9) + 0.5) / (2.0);
        for (int idx = 0; idx < cc->triangles().size(); idx++) {
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoords().push_back(Vec2<float>(x, y));
          meshDebug.texCoordTriangles().push_back(vmesh::Triangle(
            3 * idxTriangle, 3 * idxTriangle + 1, 3 * idxTriangle + 2));
          idxTriangle++;
        }
        idxCC++;
      }
      meshDebug.setMaterialLibrary(
        vmesh::basename(keepFilesPathPrefix + "_debug_categories.mtl"));
      meshDebug.save(keepFilesPathPrefix + "_debug_categories_final.obj");
    }
  }
#endif
  return 0;
}

bool
TextureParametrization::chooseRawTriangle(
  const VMCEncoderParameters&                           params,
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
  TriangleMesh<MeshType>&                               meshProjectedCCs,
  chooseFaceFromPsnrInfo&                               chooseFaceFromPsnrInfo,
  std::vector<int>& targetTriIdxListAllNewCC,
  int32_t           frameIndexInAllGof,
  std::string       keepFilesPathPrefix) {
  //choose triangles with PSNR
  //sort by c0_PSNR2
  std::vector<faceInformation> faceInfoList_C0_first =
    chooseFaceFromPsnrInfo.faceInfoList;
  std::stable_sort(faceInfoList_C0_first.begin(),
                   faceInfoList_C0_first.end(),
                   [&](faceInformation a, faceInformation b) {
                     return (a.c0_PSNR2 < b.c0_PSNR2);
                   });
  int sampledPointCountThreshold =
    600;  //pick up triangle if sampledPointCount > sampledPointCountThreshold
  //pick up 45 degree triangle
  std::vector<double> dotProductMax(meshProjectedCCs.triangles().size(), -1);
  std::vector<int>    bestOrientationIndex(meshProjectedCCs.triangles().size(),
                                        -1);
  int                 orientationCount = 18;
  Vec3<double>*       orientations     = PROJDIRECTION18;
  int                 tricountPre      = 0;
  for (int idx = 0; idx < projectedCCList.size(); idx++) {
    auto& ccMesh = projectedCCList[idx];
    for (int triIdx = 0; triIdx < ccMesh->triangles().size(); triIdx++) {
      auto   triIdxAll    = tricountPre + triIdx;
      auto   tri          = ccMesh->triangle(triIdx);
      auto   p0           = ccMesh->point(tri[0]);
      auto   p1           = ccMesh->point(tri[1]);
      auto   p2           = ccMesh->point(tri[2]);
      auto   normal       = computeTriangleNormal(p0, p1, p2, true);
      size_t clusterIndex = 0;
      double bestScore    = normal * orientations[0];
      //calc dot for 45 degree
      for (size_t j = 1; j < orientationCount; ++j) {
        const double score = normal * orientations[j];
        if (score > bestScore) {
          bestScore    = score;
          clusterIndex = j;
        }
      }
      bestOrientationIndex[triIdxAll] = clusterIndex;
      dotProductMax[triIdxAll]        = bestScore;
    }
    tricountPre += ccMesh->triangleCount();
  }
  std::vector<bool> pickUpFlag(faceInfoList_C0_first.size(),
                               false);  //for debug
  double            dotProductMaxThreshold = 0;
  int               pickUpTriangleCount    = 0;
  for (int idx = 0; idx < faceInfoList_C0_first.size(); idx++) {
    int idxInProjectedCC =
      faceInfoList_C0_first[idx].faceID;  //faceID with index in CCs
    if (pickUpTriangleCount >= params.targetCountThreshold) continue;
    //45 degree and dot product limitation
    if (faceInfoList_C0_first[idx].c0_PSNR2 > 0
        && faceInfoList_C0_first[idx].sampledPointCount
             > sampledPointCountThreshold
        && bestOrientationIndex[idxInProjectedCC] > 5
        && dotProductMax[idxInProjectedCC] > dotProductMaxThreshold) {
      targetTriIdxListAllNewCC.push_back(idxInProjectedCC);
      pickUpFlag[idx] = true;
      pickUpTriangleCount++;
    }
  }
  //for DEBUG
  double minAll_C0              = std::numeric_limits<double>::max();
  double maxAll_C0              = std::numeric_limits<double>::min();
  double minPickUped_C0         = std::numeric_limits<double>::max();
  double maxPickUped_C0         = std::numeric_limits<double>::min();
  int    minAll_pointCount      = std::numeric_limits<int>::max();
  int    maxAll_pointCount      = std::numeric_limits<int>::min();
  int    minPickUped_pointCount = std::numeric_limits<int>::max();
  int    maxPickUped_pointCount = std::numeric_limits<int>::min();
  for (int idx = 0; idx < faceInfoList_C0_first.size(); idx++) {
    if (minAll_C0 > faceInfoList_C0_first[idx].c0_PSNR2)
      minAll_C0 = faceInfoList_C0_first[idx].c0_PSNR2;
    if (maxAll_C0 < faceInfoList_C0_first[idx].c0_PSNR2)
      maxAll_C0 = faceInfoList_C0_first[idx].c0_PSNR2;
    if (minAll_pointCount > faceInfoList_C0_first[idx].sampledPointCount)
      minAll_pointCount = faceInfoList_C0_first[idx].sampledPointCount;
    if (maxAll_pointCount < faceInfoList_C0_first[idx].sampledPointCount)
      maxAll_pointCount = faceInfoList_C0_first[idx].sampledPointCount;
    if (pickUpFlag[idx]) {
      if (minPickUped_C0 > faceInfoList_C0_first[idx].c0_PSNR2)
        minPickUped_C0 = faceInfoList_C0_first[idx].c0_PSNR2;
      if (maxPickUped_C0 < faceInfoList_C0_first[idx].c0_PSNR2)
        maxPickUped_C0 = faceInfoList_C0_first[idx].c0_PSNR2;
      if (minPickUped_pointCount
          > faceInfoList_C0_first[idx].sampledPointCount)
        minPickUped_pointCount = faceInfoList_C0_first[idx].sampledPointCount;
      if (maxPickUped_pointCount
          < faceInfoList_C0_first[idx].sampledPointCount)
        maxPickUped_pointCount = faceInfoList_C0_first[idx].sampledPointCount;
    }
  }
  std::cout << "Pickuped information" << std::endl;
  std::cout << "minAll_C0 : " << minAll_C0 << std::endl;
  std::cout << "maxAll_C0 : " << maxAll_C0 << std::endl;
  std::cout << "minPickUped_C0 : " << minPickUped_C0 << std::endl;
  std::cout << "maxPickUped_C0 : " << maxPickUped_C0 << std::endl;
  std::cout << "minAll_pointCount : " << minAll_pointCount << std::endl;
  std::cout << "maxAll_pointCount : " << maxAll_pointCount << std::endl;
  std::cout << "minPickUped_pointCount : " << minPickUped_pointCount
            << std::endl;
  std::cout << "maxPickUped_pointCount : " << maxPickUped_pointCount
            << std::endl;
  if (params.keepIntermediateFiles) {
    const auto  basePath = vmesh::dirname(keepFilesPathPrefix);
    std::string prefixSub =
      basePath + "met_sort_fr" + std::to_string(frameIndexInAllGof) + ".txt";
    std::ofstream outputfile_sort(prefixSub);
    outputfile_sort << "idxInSort faceID C0 C1 C2 points bestOrientationIndex "
                       "dotProductMax pickUped"
                    << std::endl;
    std::string prefixSub_pickOnly = basePath + "met_pickOnly_fr"
                                     + std::to_string(frameIndexInAllGof)
                                     + ".txt";
    std::ofstream outputfile_sort_pickOnly(prefixSub_pickOnly);
    outputfile_sort_pickOnly << "idxInSort faceID C0 C1 C2 points "
                                "bestOrientationIndex dotProductMax pickUped"
                             << std::endl;
    for (int idx = 0; idx < faceInfoList_C0_first.size(); idx++) {
      int idxInProjectedCC =
        faceInfoList_C0_first[idx].faceID;  //faceID with index in CCs
      outputfile_sort << idx << " " << faceInfoList_C0_first[idx].faceID << " "
                      << std::fixed << std::setprecision(10)
                      << faceInfoList_C0_first[idx].c0_PSNR2 << " "
                      << faceInfoList_C0_first[idx].c1_PSNR2 << " "
                      << faceInfoList_C0_first[idx].c2_PSNR2 << " "
                      << faceInfoList_C0_first[idx].sampledPointCount << " "
                      << bestOrientationIndex[idxInProjectedCC] << " "
                      << dotProductMax[idxInProjectedCC] << " "
                      << pickUpFlag[idx] << std::endl;
      if (pickUpFlag[idx])
        outputfile_sort_pickOnly
          << idx << " " << faceInfoList_C0_first[idx].faceID << " "
          << std::fixed << std::setprecision(10)
          << faceInfoList_C0_first[idx].c0_PSNR2 << " "
          << faceInfoList_C0_first[idx].c1_PSNR2 << " "
          << faceInfoList_C0_first[idx].c2_PSNR2 << " "
          << faceInfoList_C0_first[idx].sampledPointCount << " "
          << bestOrientationIndex[idxInProjectedCC] << " "
          << dotProductMax[idxInProjectedCC] << " " << pickUpFlag[idx]
          << std::endl;
    }
    outputfile_sort.close();
    outputfile_sort_pickOnly.close();
  }
  std::cout << "RawUVtriangleCount = " << targetTriIdxListAllNewCC.size()
            << std::endl;
  return 0;
}

bool
TextureParametrization::createChooseFaceFromPsnrInfo(
  chooseFaceFromPsnrInfo&                    chooseFaceFromPsnrInfo,
  const VMCEncoderParameters&                params,
  trasferInformation&                        trasferInfo,
  TriangleMesh<MeshType>&                    decimateTexture,
  TriangleMesh<MeshType>&                    meshProjectedCCs,
  std::vector<ConnectedComponent<MeshType>>& packedCCList,
  int32_t                                    frameIndexInAllGof,
  std::string                                keepFilesPathPrefix) {
  //// create mapping CCpack -> CCs ////
  std::vector<int> faceIdMappingCCpackToCCs(
    meshProjectedCCs.triangles().size(), -1);
  std::map<std::array<double, 9>, int32_t> map0;
  for (int triIdx = 0; triIdx < meshProjectedCCs.triangles().size();
       triIdx++) {
    //std::cout << "triIdx : " << triIdx << std::endl;
    auto                        tri    = meshProjectedCCs.triangle(triIdx);
    const auto&                 point0 = meshProjectedCCs.point(tri[0]);
    const auto&                 point1 = meshProjectedCCs.point(tri[1]);
    const auto&                 point2 = meshProjectedCCs.point(tri[2]);
    const std::array<double, 9> vertex0{point0[0],
                                        point0[1],
                                        point0[2],
                                        point1[0],
                                        point1[1],
                                        point1[2],
                                        point2[0],
                                        point2[1],
                                        point2[2]};
    map0[vertex0] = triIdx;
  }
  //add to newCC
  int tricountPre = 0;
  for (int idx = 0; idx < packedCCList.size(); idx++) {
    auto& ccMesh                 = packedCCList[idx];
    bool  targetTriangleIncluded = false;
    for (int triIdx = 0; triIdx < ccMesh.triangles().size(); triIdx++) {
      auto                        triIdxAll = tricountPre + triIdx;
      auto                        tri       = ccMesh.triangle(triIdx);
      const auto&                 point0    = ccMesh.point(tri[0]);
      const auto&                 point1    = ccMesh.point(tri[1]);
      const auto&                 point2    = ccMesh.point(tri[2]);
      const std::array<double, 9> vertex1{point0[0],
                                          point0[1],
                                          point0[2],
                                          point1[0],
                                          point1[1],
                                          point1[2],
                                          point2[0],
                                          point2[1],
                                          point2[2]};
      const auto                  it = map0.find(vertex1);
      if (it != map0.end()) {
        faceIdMappingCCpackToCCs[triIdxAll] = it->second;
      } else {
        std::cout << "not found" << std::endl;
      }
    }
    tricountPre += ccMesh.triangleCount();
  }
  chooseFaceFromPsnrInfo.faceIdMappingCCpackToCCs = faceIdMappingCCpackToCCs;

  ////texture transfer and generate psnr list ////
  vmesh::TriangleMesh<MeshType> sourceMesh;
  auto                          sourceMeshPath = trasferInfo.inputMeshPath;
  if (!sourceMesh.load(sourceMeshPath, frameIndexInAllGof)) {
    printf("fail to load mesh : %s frame %d\n",
           sourceMeshPath.c_str(),
           frameIndexInAllGof);
    exit(2);
  }
  std::vector<Frame<uint8_t>> sourceTexture;
  auto                        sourceTexturePath = trasferInfo.inputTexturePath;
  if (params.numTextures > 1) {
    vmesh::TriangleMesh<MeshType> sourceMesh;
    //auto                          sourceMeshPath = inputMeshPath_;
    auto sourceMeshPath = trasferInfo.inputMeshPath;
    // the names of textures is located in the material file indicated by the mesh, so we need to read the mesh again
    if (!sourceMesh.load(sourceMeshPath, frameIndexInAllGof)) {
      printf("fail to load mesh : %s frame %d\n",
             sourceMeshPath.c_str(),
             frameIndexInAllGof);
      exit(2);
    }
    sourceTexture.resize(sourceMesh.textureMapUrls().size());
    for (int i = 0; i < sourceMesh.textureMapUrls().size(); i++) {
      auto& url = sourceMesh.textureMapUrls()[i];
      if (!url.empty()) {
        sourceTexturePath =
          dirname(sourceMeshPath) + sourceMesh.textureMapUrls()[i];
        if (!sourceTexture[i].load(sourceTexturePath, frameIndexInAllGof)) {
          printf("fail to load texture : %s frame %d\n",
                 sourceTexturePath.c_str(),
                 frameIndexInAllGof);
          exit(2);
        };
      }
    }
  } else {
    sourceTexture.resize(1);
    if (!sourceTexture[0].load(sourceTexturePath, frameIndexInAllGof)) {
      printf("fail to load texture : %s frame %d\n",
             sourceTexturePath.c_str(),
             frameIndexInAllGof);
      exit(2);
    };
  }

  Frame<uint8_t> subTexture;
  auto           subTextureWidth  = params.texParamWidth;
  auto           subTextureHeight = params.texParamHeight;
  subTexture.resize(
    subTextureWidth, subTextureHeight, trasferInfo.sourceAttributeColourSpace);
  TransferColor  transferColor;
  auto&          refMesh        = sourceMesh;
  auto&          refTexture     = sourceTexture;
  auto&          targetMesh     = decimateTexture;
  auto&          targetTexture  = subTexture;
  bool           usePastTexture = false;
  Frame<uint8_t> pastTexture;
  Plane<uint8_t> occupancy;
  bool           enablePadding = true;
  enablePadding                = (params.numSubmesh == 1);
  int submeshIdx               = 0;
  if (!transferColor.transfer(refMesh,
                              refTexture,
                              targetMesh,
                              targetTexture,
                              usePastTexture,
                              pastTexture,
                              params,
                              occupancy,
                              enablePadding,
                              submeshIdx)) {
    printf("fail to transferTexture\n");
    exit(1);
  }
  if (params.keepIntermediateFiles) {
    const auto  basePath        = vmesh::dirname(keepFilesPathPrefix);
    std::string transferObjFile = basePath + "transfer_afterOrthoAtlas_fr"
                                  + std::to_string(frameIndexInAllGof)
                                  + ".obj";
    targetMesh.save(transferObjFile);
    std::string transferPngFile = basePath + "transfer_afterOrthoAtlas_fr"
                                  + std::to_string(frameIndexInAllGof)
                                  + ".png";
    char fnameEncTexture[1024];
    snprintf(
      fnameEncTexture, 1024, transferPngFile.c_str(), frameIndexInAllGof);
    targetTexture.save(fnameEncTexture);
  }
  //metrics per face
  VMCMetrics           metrics;
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
  std::vector<std::vector<double>> metricsResults;
  std::vector<Vec2<int>>           sampledPointCountList;
  metricsResults.resize(targetMesh.triangles().size());
  sampledPointCountList.resize(targetMesh.triangles().size());
  std::vector<Frame<uint8_t>> srcMaps(1);
  std::vector<Frame<uint8_t>> recMaps(1);
  srcMaps[0] = refTexture[0];
  recMaps[0] = targetTexture;
  metrics.computePerFace(refMesh,
                         targetMesh,
                         srcMaps,
                         recMaps,
                         metricParams,
                         metricsResults,
                         sampledPointCountList,
                         keepFilesPathPrefix,
                         params.keepIntermediateFiles);

  if (params.keepIntermediateFiles) {
    const auto  basePath = vmesh::dirname(keepFilesPathPrefix);
    std::string prefixSub =
      basePath + "met_all_fr" + std::to_string(frameIndexInAllGof) + ".txt";
    std::ofstream outputfile(prefixSub);
    outputfile << "faceID C0 C1 C2 points" << std::endl;
    for (int idxRecFace = 0; idxRecFace < targetMesh.triangles().size();
         idxRecFace++) {
      outputfile << faceIdMappingCCpackToCCs[idxRecFace] << " " << std::fixed
                 << std::setprecision(10) << metricsResults[idxRecFace][2]
                 << " " << metricsResults[idxRecFace][3] << " "
                 << metricsResults[idxRecFace][4] << " "
                 << sampledPointCountList[idxRecFace][0] << std::endl;
    }
    outputfile.close();
  }
  auto& faceInfoList = chooseFaceFromPsnrInfo.faceInfoList;
  faceInfoList.resize(targetMesh.triangles().size());
  for (int idxRecFace = 0; idxRecFace < targetMesh.triangles().size();
       idxRecFace++) {
    faceInformation faceInfoTmp;
    faceInfoTmp.faceID =
      faceIdMappingCCpackToCCs[idxRecFace];  //faceID with index in CCs
    faceInfoTmp.c0_PSNR2 = metricsResults[idxRecFace][2];
    faceInfoTmp.c1_PSNR2 = metricsResults[idxRecFace][3];
    faceInfoTmp.c2_PSNR2 = metricsResults[idxRecFace][4];
    faceInfoTmp.sampledPointCount =
      sampledPointCountList[idxRecFace][0];  //0:rec, 1: org
    faceInfoList[idxRecFace] = faceInfoTmp;
  }
  return 0;
}

void
TextureParametrization::separateTexCCs(
  std::vector<int32_t>&   projectedConnectedComponentsCategories,
  std::vector<Vec2<int>>& bias,
  std::vector<ConnectedComponent<MeshType>>& packedCCList,
  const VMCEncoderParameters&                params) {
  std::vector<ConnectedComponent<MeshType>> packedCCListUpdated;
  std::vector<int32_t>   projectedConnectedComponentsCategoriesUpdated;
  std::vector<Vec2<int>> biasUpdate;
  projectedConnectedComponentsCategoriesUpdated.reserve(packedCCList.size());
  biasUpdate.reserve(packedCCList.size());
  for (int idx = 0; idx < packedCCList.size(); idx++) {
    auto& ccMesh = packedCCList[idx];
    std::vector<std::shared_ptr<TriangleMesh<MeshType>>> connectedComponents;
    std::vector<int>                                     partition_sub;
    int                                                  ccCount0 =
      ExtractConnectedComponentsByEdge(ccMesh.texCoordTriangles(),
                                       ccMesh.texCoordCount(),
                                       ccMesh,
                                       partition_sub,
                                       !(params.iDeriveTextCoordFromPos == 3),
                                       &connectedComponents);
    for (int32_t c = 0; c < ccCount0; ++c) {
      const auto& connectedComponent = connectedComponents[c];
      //create new CCs
      projectedConnectedComponentsCategoriesUpdated.push_back(
        projectedConnectedComponentsCategories[idx]);
      biasUpdate.push_back(bias[idx]);
      vmesh::ConnectedComponent<MeshType> cc;
      //copy mesh
      cc                     = ccMesh;
      cc.triangles()         = connectedComponent->triangles();
      cc.points()            = connectedComponent->points();
      cc.texCoords()         = connectedComponent->texCoords();
      cc.texCoordTriangles() = connectedComponent->texCoordTriangles();
      packedCCListUpdated.push_back(cc);
    }
  }
  packedCCList = packedCCListUpdated;
  projectedConnectedComponentsCategories =
    projectedConnectedComponentsCategoriesUpdated;
  bias = biasUpdate;
}

void
TextureParametrization::patch_rasterization(
  vmesh::Plane<uint8_t>&        occupancyCC,
  ConnectedComponent<MeshType>& packedCC,
  const VMCEncoderParameters&   params) {
  auto geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  auto bbCC                   = packedCC.texCoordBoundingBox();
  int  widthOccCC =
    std::ceil((packedCC.getScale() * bbCC.max[0] + 2 * params.gutter)
              / (double)geometryVideoBlockSize);
  int heightOccCC =
    std::ceil((packedCC.getScale() * bbCC.max[1] + 2 * params.gutter)
              / (double)geometryVideoBlockSize);
  occupancyCC.resize(widthOccCC, heightOccCC);
  occupancyCC.fill(false);
  for (auto& tri : packedCC.texCoordTriangles()) {
    // get bounding box of projected triangle
    vmesh::Box2<MeshType> triBB(std::numeric_limits<MeshType>::max(),
                                std::numeric_limits<MeshType>::lowest());
    vmesh::Vec2<MeshType> uv[3];
    vmesh::Vec2<MeshType> gutterUV(params.gutter);
    for (int idx = 0; idx < 3; idx++) {
      uv[idx] = packedCC.getScale() * packedCC.texCoord(tri[idx]) + gutterUV;
      triBB.enclose(uv[idx]);
    }
    // rasterize projected triangle to create occupancy map
    int h0 = std::max(0,
                      (int)std::floor((triBB.min[1] - params.gutter)
                                      / geometryVideoBlockSize));
    int h1 = std::min(
      heightOccCC,
      (int)std::ceil((triBB.max[1] + params.gutter) / geometryVideoBlockSize));
    int w0 = std::max(0,
                      (int)std::floor((triBB.min[0] - params.gutter)
                                      / geometryVideoBlockSize));
    int w1 = std::min(
      widthOccCC,
      (int)std::ceil((triBB.max[0] + params.gutter) / geometryVideoBlockSize));
    for (int h = h0; h < h1; h++) {
      for (int w = w0; w < w1; w++) {
        if (!occupancyCC.get(h, w)) {
          bool emptyBlock = false;
          for (int hh = -params.gutter;
               hh < (geometryVideoBlockSize + params.gutter) && !emptyBlock;
               hh++) {
            for (int ww = -params.gutter;
                 ww < (geometryVideoBlockSize + params.gutter) && !emptyBlock;
                 ww++) {
              if (isInsideTriangleUsingArea(
                    uv[0],
                    uv[1],
                    uv[2],
                    Vec2<MeshType>(w * geometryVideoBlockSize + ww,
                                   h * geometryVideoBlockSize + hh)))
                emptyBlock = true;
            }
          }
          occupancyCC.set(h, w, emptyBlock);
        }
      }
    }
  }
}

void
TextureParametrization::patch_horizons(vmesh::Plane<uint8_t>& occupancyCC,
                                       std::vector<int>&      topHorizon,
                                       std::vector<int>&      bottomHorizon,
                                       std::vector<int>&      rightHorizon,
                                       std::vector<int>&      leftHorizon) {
  auto sizeU0 = occupancyCC.width();
  auto sizeV0 = occupancyCC.height();
  topHorizon.resize(sizeU0, 0);
  bottomHorizon.resize(sizeU0, 0);
  rightHorizon.resize(sizeV0, 0);
  leftHorizon.resize(sizeV0, 0);
  for (int j = 0; j < sizeU0; j++) {
    while (!occupancyCC.get(sizeV0 - 1 - topHorizon[j], j)
           && (topHorizon[j] < sizeV0 - 1))
      topHorizon[j]++;
  }
  for (int j = 0; j < sizeU0; j++) {
    while (!occupancyCC.get(bottomHorizon[j], j)
           && (bottomHorizon[j] < sizeV0 - 1))
      bottomHorizon[j]++;
  }
  for (int i = 0; i < sizeV0; i++) {
    while (!occupancyCC.get(i, sizeU0 - 1 - rightHorizon[i])
           && (rightHorizon[i] < sizeU0 - 1))
      rightHorizon[i]++;
  }
  for (int i = 0; i < sizeV0; i++) {
    while (!occupancyCC.get(i, leftHorizon[i])
           && (leftHorizon[i] < sizeU0 - 1))
      leftHorizon[i]++;
  }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
  std::cout << "Top Horizon :[";
  for (int i = 0; i < sizeU0; i++) { std::cout << topHorizon[i] << ","; }
  std::cout << "]" << std::endl;
  std::cout << "Bottom Horizon :[";
  for (int i = 0; i < sizeU0; i++) { std::cout << bottomHorizon[i] << ","; }
  std::cout << "]" << std::endl;
  std::cout << "Right Horizon :[";
  for (int i = 0; i < sizeV0; i++) { std::cout << rightHorizon[i] << ","; }
  std::cout << "]" << std::endl;
  std::cout << "Left Horizon :[";
  for (int i = 0; i < sizeV0; i++) { std::cout << leftHorizon[i] << ","; }
  std::cout << "]" << std::endl;
#endif
}

bool
TextureParametrization::pack_patch_flexible(
  vmesh::Plane<uint8_t>&        occupancy,
  vmesh::Plane<uint8_t>&        occupancyCC,
  ConnectedComponent<MeshType>& packedCC,
  const VMCEncoderParameters&   params,
  std::string                   keepFilesPathPrefix,
  bool                          invertDirection) {
  auto geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  int  occupancySizeV         = occupancy.height();
  int  occupancySizeU         = occupancy.width();
  int  widthOccCC             = occupancyCC.width();
  int  heightOccCC            = occupancyCC.height();
  //now place the patch in the empty area
  // U0,V0 search
  for (int vIdx = 0; vIdx < occupancySizeV; ++vIdx) {
    int v = invertDirection ? occupancySizeV - 1 - vIdx : vIdx;
    for (int u = 0; u < occupancySizeU; ++u) {
      // orientation search
      for (int orientation = 0; orientation < 4; orientation++) {
        bool fit = true;
        for (int vv = 0; vv < heightOccCC && fit; vv++) {
          for (int uu = 0; uu < widthOccCC && fit; uu++) {
            int newV, newU;
            switch (orientation) {
            case 0:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            case 1:  // 90 degree
              newV = v + uu;
              newU = u + (heightOccCC - 1 - vv);
              break;
            case 2:  // 180 degrees
              newV = v + (heightOccCC - 1 - vv);
              newU = u + (widthOccCC - 1 - uu);
              break;
            case 3:  // 270 degrees
              newV = v + (widthOccCC - 1 - uu);
              newU = u + vv;
              break;
            default:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            }
            if ((newV >= occupancySizeV) || (newV < 0)) fit = false;
            else if ((newU >= occupancySizeU) || (newU < 0)) fit = false;
            else if (occupancy.get(newV, newU) && occupancyCC.get(vv, uu))
              fit = false;
          }
        }
        if (fit) {
          //update the global occupancy map
          for (int vv = 0; vv < heightOccCC; vv++) {
            for (int uu = 0; uu < widthOccCC; uu++) {
              int newV, newU;
              switch (orientation) {
              case 0:  // no rotation
                newV = v + vv;
                newU = u + uu;
                break;
              case 1:  // 90 degree
                newV = v + uu;
                newU = u + (heightOccCC - 1 - vv);
                break;
              case 2:  // 180 degrees
                newV = v + (heightOccCC - 1 - vv);
                newU = u + (widthOccCC - 1 - uu);
                break;
              case 3:  // 270 degrees
                newV = v + (widthOccCC - 1 - uu);
                newU = u + vv;
                break;
              default:  // no rotation
                newV = v + vv;
                newU = u + uu;
                break;
              }
              if (!occupancy.get(newV, newU))
                occupancy.set(newV, newU, occupancyCC.get(vv, uu));
            }
          }
          //update the UV coordinates of the connected components
          // scale
          for (auto& uv : packedCC.texCoords()) { uv *= packedCC.getScale(); }
          // gutter
          for (auto& uv : packedCC.texCoords()) {
            uv += Vec2<MeshType>(params.gutter, params.gutter);
          }
          // rotation
          for (auto& uv : packedCC.texCoords()) {
            MeshType uu = uv[0];
            MeshType vv = uv[1];
            switch (orientation) {
            case 0:  // no rotation
              break;
            case 1:  // 90 degree
              uv[1] = uu;
              uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
              break;
            case 2:  // 180 degrees
              uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
              uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
              break;
            case 3:  // 270 degrees
              uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
              uv[0] = vv;
              break;
            default:  // no rotation
              uv[1] = vv;
              uv[0] = uu;
              break;
            }
          }
          packedCC.setOrientation(orientation);
          // translation
          for (auto& uv : packedCC.texCoords()) {
            uv += Vec2<MeshType>(u * geometryVideoBlockSize,
                                 v * geometryVideoBlockSize);
          }
          packedCC.setU0(u);
          packedCC.setV0(v);
          packedCC.setSizeU(widthOccCC);
          packedCC.setSizeV(heightOccCC);
#if DEBUG_ORTHO_PATCH_PACKING
          std::cout << "CC[" << packedCC.getIdxPatch()
                    << "] = (#tri: " << packedCC.triangles().size()
                    << ", #vert: " << packedCC.points().size()
                    << ", Area: " << packedCC.area()
                    << ", Perimeter: " << packedCC.perimeter()
                    << ", P: " << packedCC.getProjection()
                    << ", S: " << packedCC.getScale()
                    << ", O: " << packedCC.getOrientation()
                    << ", U0: " << packedCC.getU0()
                    << ", V0: " << packedCC.getV0()
                    << ", SizeU: " << packedCC.getSizeU()
                    << ", SizeV: " << packedCC.getSizeV() << ")" << std::endl;
          if (params.keepIntermediateFiles) {
            // dump of the current state of the occupancy
            vmesh::Frame<uint8_t> occupancyVideo;
            occupancyVideo.resize(
              occupancySizeU, occupancySizeV, vmesh::ColourSpace::BGR444p);
            occupancyVideo.plane(0) = occupancy;
            for (auto& pixel : occupancyVideo.plane(0).buffer()) {
              pixel *= 255;
            }
            int deltaVV = std::max(1, (heightOccCC - 1));
            for (int vv = 0; vv < heightOccCC; vv += deltaVV) {
              for (int uu = 0; uu < widthOccCC; uu++) {
                int newV, newU;
                switch (orientation) {
                case 0:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                case 1:  // 90 degree
                  newV = v + uu;
                  newU = u + (heightOccCC - 1 - vv);
                  break;
                case 2:  // 180 degrees
                  newV = v + (heightOccCC - 1 - vv);
                  newU = u + (widthOccCC - 1 - uu);
                  break;
                case 3:  // 270 degrees
                  newV = v + (widthOccCC - 1 - uu);
                  newU = u + vv;
                  break;
                default:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                }
                occupancyVideo.plane(1).set(newV, newU, 255);
              }
            }
            int deltaUU = std::max(1, (widthOccCC - 1));
            for (int vv = 0; vv < heightOccCC; vv++) {
              for (int uu = 0; uu < widthOccCC; uu += deltaUU) {
                int newV, newU;
                switch (orientation) {
                case 0:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                case 1:  // 90 degree
                  newV = v + uu;
                  newU = u + (heightOccCC - 1 - vv);
                  break;
                case 2:  // 180 degrees
                  newV = v + (heightOccCC - 1 - vv);
                  newU = u + (widthOccCC - 1 - uu);
                  break;
                case 3:  // 270 degrees
                  newV = v + (widthOccCC - 1 - uu);
                  newU = u + vv;
                  break;
                default:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                }
                occupancyVideo.plane(1).set(newV, newU, 255);
              }
            }
            auto prefix = keepFilesPathPrefix + "_occupancy"
                          + std::to_string(occupancySizeU) + "x"
                          + std::to_string(occupancySizeV) + "_bgrp";
            occupancyVideo.append(prefix + ".rgb");
          }
#endif
          return true;
        }
      }
    }
  }
  return false;
}

bool
TextureParametrization::pack_patch_flexible_and_temporal_stabilization(
  vmesh::Plane<uint8_t>&        occupancy,
  vmesh::Plane<uint8_t>&        occupancyCC,
  ConnectedComponent<MeshType>& packedCC,
  const VMCEncoderParameters&   params,
  std::string                   keepFilesPathPrefix,
  bool                          invertDirection,
  int                           startU,
  int                           startV,
  int&                          lastOccupiedU,
  int&                          lastOccupiedV,
  int&                          maxOccupiedU,
  int&                          maxOccupiedV) {
  auto geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  int  occupancySizeV         = occupancy.height();
  int  occupancySizeU         = occupancy.width();
  int  widthOccCC             = occupancyCC.width();
  int  heightOccCC            = occupancyCC.height();

  int tempV                     = occupancySizeV;
  int lastOccupiedUthisCategory = 0;
  int lastOccupiedVthisCategory = 0;

  std::cout << "start packing from U = " << startU << std::endl;
  std::cout << "start packing from V = " << startV << std::endl;

  //int orientationNum = 4;
  int orientationNum = 1;

  //now place the patch in the empty area
  // U0,V0 search
  for (int vIdx = startV; vIdx < occupancySizeV; ++vIdx) {
    int v = invertDirection ? occupancySizeV - 1 - vIdx : vIdx;
    for (int u = startU; u < occupancySizeU; ++u) {
      // orientation search
      for (int orientation = 0; orientation < orientationNum; orientation++) {
        bool fit = true;
        for (int vv = 0; vv < heightOccCC && fit; vv++) {
          for (int uu = 0; uu < widthOccCC && fit; uu++) {
            int newV, newU;
            switch (orientation) {
            case 0:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            case 1:  // 90 degree
              newV = v + uu;
              newU = u + (heightOccCC - 1 - vv);
              break;
            case 2:  // 180 degrees
              newV = v + (heightOccCC - 1 - vv);
              newU = u + (widthOccCC - 1 - uu);
              break;
            case 3:  // 270 degrees
              newV = v + (widthOccCC - 1 - uu);
              newU = u + vv;
              break;
            default:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            }
            if ((newV >= occupancySizeV) || (newV < 0)) fit = false;
            else if ((newU >= occupancySizeU) || (newU < 0)) fit = false;
            else if (occupancy.get(newV, newU) && occupancyCC.get(vv, uu))
              fit = false;
          }
        }
        if (fit) {
          //update the global occupancy map
          for (int vv = 0; vv < heightOccCC; vv++) {
            for (int uu = 0; uu < widthOccCC; uu++) {
              int newV, newU;
              switch (orientation) {
              case 0:  // no rotation
                newV = v + vv;
                newU = u + uu;
                break;
              case 1:  // 90 degree
                newV = v + uu;
                newU = u + (heightOccCC - 1 - vv);
                break;
              case 2:  // 180 degrees
                newV = v + (heightOccCC - 1 - vv);
                newU = u + (widthOccCC - 1 - uu);
                break;
              case 3:  // 270 degrees
                newV = v + (widthOccCC - 1 - uu);
                newU = u + vv;
                break;
              default:  // no rotation
                newV = v + vv;
                newU = u + uu;
                break;
              }
              //if (!occupancy.get(newV, newU))
              //    occupancy.set(newV, newU, occupancyCC.get(vv, uu));

              if (!occupancy.get(newV, newU)) {
                occupancy.set(newV, newU, occupancyCC.get(vv, uu));
                if (newV < tempV && lastOccupiedUthisCategory < newU) {
                  //the largest occupied u in the smallest occupied v
                  tempV                     = newV;
                  lastOccupiedUthisCategory = newU;
                }
                if (lastOccupiedVthisCategory < newV) {
                  //the largest occupied v
                  lastOccupiedVthisCategory = newV;
                }

                if (maxOccupiedU < newU) { maxOccupiedU = newU; }
                if (maxOccupiedV < newV) { maxOccupiedV = newV; }
              }
            }
          }
          //update the UV coordinates of the connected components
          // scale
          for (auto& uv : packedCC.texCoords()) { uv *= packedCC.getScale(); }
          // gutter
          for (auto& uv : packedCC.texCoords()) {
            uv += Vec2<MeshType>(params.gutter, params.gutter);
          }
          // rotation
          for (auto& uv : packedCC.texCoords()) {
            MeshType uu = uv[0];
            MeshType vv = uv[1];
            switch (orientation) {
            case 0:  // no rotation
              break;
            case 1:  // 90 degree
              uv[1] = uu;
              uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
              break;
            case 2:  // 180 degrees
              uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
              uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
              break;
            case 3:  // 270 degrees
              uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
              uv[0] = vv;
              break;
            default:  // no rotation
              uv[1] = vv;
              uv[0] = uu;
              break;
            }
          }
          packedCC.setOrientation(orientation);
          // translation
          for (auto& uv : packedCC.texCoords()) {
            uv += Vec2<MeshType>(u * geometryVideoBlockSize,
                                 v * geometryVideoBlockSize);
          }
          packedCC.setU0(u);
          packedCC.setV0(v);
          packedCC.setSizeU(widthOccCC);
          packedCC.setSizeV(heightOccCC);
#if DEBUG_ORTHO_PATCH_PACKING
          std::cout << "CC[" << packedCC.getIdxPatch()
                    << "] = (#tri: " << packedCC.triangles().size()
                    << ", #vert: " << packedCC.points().size()
                    << ", Area: " << packedCC.area()
                    << ", Perimeter: " << packedCC.perimeter()
                    << ", P: " << packedCC.getProjection()
                    << ", S: " << packedCC.getScale()
                    << ", O: " << packedCC.getOrientation()
                    << ", U0: " << packedCC.getU0()
                    << ", V0: " << packedCC.getV0()
                    << ", SizeU: " << packedCC.getSizeU()
                    << ", SizeV: " << packedCC.getSizeV() << ")" << std::endl;
          if (params.keepIntermediateFiles) {
            // dump of the current state of the occupancy
            vmesh::Frame<uint8_t> occupancyVideo;
            occupancyVideo.resize(
              occupancySizeU, occupancySizeV, vmesh::ColourSpace::BGR444p);
            occupancyVideo.plane(0) = occupancy;
            for (auto& pixel : occupancyVideo.plane(0).buffer()) {
              pixel *= 255;
            }
            int deltaVV = std::max(1, (heightOccCC - 1));
            for (int vv = 0; vv < heightOccCC; vv += deltaVV) {
              for (int uu = 0; uu < widthOccCC; uu++) {
                int newV, newU;
                switch (orientation) {
                case 0:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                case 1:  // 90 degree
                  newV = v + uu;
                  newU = u + (heightOccCC - 1 - vv);
                  break;
                case 2:  // 180 degrees
                  newV = v + (heightOccCC - 1 - vv);
                  newU = u + (widthOccCC - 1 - uu);
                  break;
                case 3:  // 270 degrees
                  newV = v + (widthOccCC - 1 - uu);
                  newU = u + vv;
                  break;
                default:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                }
                occupancyVideo.plane(1).set(newV, newU, 255);
              }
            }
            int deltaUU = std::max(1, (widthOccCC - 1));
            for (int vv = 0; vv < heightOccCC; vv++) {
              for (int uu = 0; uu < widthOccCC; uu += deltaUU) {
                int newV, newU;
                switch (orientation) {
                case 0:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                case 1:  // 90 degree
                  newV = v + uu;
                  newU = u + (heightOccCC - 1 - vv);
                  break;
                case 2:  // 180 degrees
                  newV = v + (heightOccCC - 1 - vv);
                  newU = u + (widthOccCC - 1 - uu);
                  break;
                case 3:  // 270 degrees
                  newV = v + (widthOccCC - 1 - uu);
                  newU = u + vv;
                  break;
                default:  // no rotation
                  newV = v + vv;
                  newU = u + uu;
                  break;
                }
                occupancyVideo.plane(1).set(newV, newU, 255);
              }
            }
            auto prefix = keepFilesPathPrefix + "_occupancy"
                          + std::to_string(occupancySizeU) + "x"
                          + std::to_string(occupancySizeV) + "_bgrp";
            occupancyVideo.append(prefix + ".rgb");
          }
#endif
          lastOccupiedU = lastOccupiedUthisCategory;
          lastOccupiedV = lastOccupiedVthisCategory;
          return true;
        }
      }
    }
  }
  return false;
}

bool
TextureParametrization::pack_patch_flexible_tetris(
  vmesh::Plane<uint8_t>&        occupancy,
  vmesh::Plane<uint8_t>&        occupancyCC,
  ConnectedComponent<MeshType>& packedCC,
  const VMCEncoderParameters&   params,
  std::string                   keepFilesPathPrefix,
  std::vector<int>&             horizon,
  std::vector<int>&             topHorizon,
  std::vector<int>&             bottomHorizon,
  std::vector<int>&             rightHorizon,
  std::vector<int>&             leftHorizon) {
  auto geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  int  occupancySizeV         = occupancy.height();
  int  occupancySizeU         = occupancy.width();
  int  widthOccCC             = occupancyCC.width();
  int  heightOccCC            = occupancyCC.height();
  //now place the patch in the empty area
  // U0,V0 search
  bool foundPos          = false;
  int  bestU             = -1;
  int  bestV             = -1;
  int  bestOrientation   = -1;
  int  best_wasted_space = (std::numeric_limits<int>::max)();
  int  minHorizon        = std::numeric_limits<int32_t>::max();
  int  maxHorizon        = 0;
  for (auto& horVal : horizon) {
    if (horVal < minHorizon) minHorizon = horVal;
    if (horVal > maxHorizon) maxHorizon = horVal;
  }
  maxHorizon = std::min(occupancySizeV - 1, maxHorizon + 1);
  // orientation search
  for (int orientation = 0; orientation < 4; orientation++) {
    int minV = std::numeric_limits<int32_t>::max();
    ;
    switch (orientation) {
    case 0:  // no rotation
      for (int idx = 0; idx < widthOccCC; idx++)
        minV = std::max<int32_t>(
          0, std::min<int32_t>(minV, minHorizon - bottomHorizon[idx]));
      break;
    case 1:  // 90 degree
      for (int idx = 0; idx < heightOccCC; idx++)
        minV = std::max<int32_t>(
          0, std::min<int32_t>(minV, minHorizon - leftHorizon[idx]));
      break;
    case 2:  // 180 degrees
      for (int idx = 0; idx < widthOccCC; idx++)
        minV = std::max<int32_t>(
          0,
          std::min<int32_t>(minV,
                            minHorizon - (heightOccCC - topHorizon[idx])));
      break;
    case 3:  // 270 degrees
      for (int idx = 0; idx < heightOccCC; idx++)
        minV = std::max<int32_t>(
          0,
          std::min<int32_t>(minV,
                            minHorizon - (widthOccCC - rightHorizon[idx])));
      break;
    default:  // no rotation
      for (int idx = 0; idx < widthOccCC; idx++)
        minV = std::max<int32_t>(
          0, std::min<int32_t>(minV, minHorizon - bottomHorizon[idx]));
      break;
    }
    for (int v = minV; v < maxHorizon + 1; ++v) {
      for (int u = 0; u < occupancySizeU; ++u) {
        //check if the patch bounding box fit the canvas
        int maxV, maxU;
        switch (orientation) {
        case 0:  // no rotation
          maxV = v + heightOccCC - 1;
          maxU = u + widthOccCC - 1;
          break;
        case 1:  // 90 degree
          maxV = v + widthOccCC - 1;
          maxU = u + heightOccCC - 1;
          break;
        case 2:  // 180 degrees
          maxV = v + heightOccCC - 1;
          maxU = u + widthOccCC - 1;
          break;
        case 3:  // 270 degrees
          maxV = v + widthOccCC - 1;
          maxU = u + heightOccCC - 1;
          break;
        default:  // no rotation
          maxV = v + heightOccCC - 1;
          maxU = u + widthOccCC - 1;
          break;
        }
        if ((maxV >= occupancySizeV) || (maxU >= occupancySizeU)) continue;
        //check if the patch is above the horizon
        bool isAboveHorizon = true;
        switch (orientation) {
        case 0:  // no rotation
          for (int idx = 0; idx < widthOccCC && isAboveHorizon; idx++) {
            if (v + bottomHorizon[idx] < horizon[u + idx]) {
              isAboveHorizon = false;
            }
          }
          break;
        case 1:  // 90 degree
          for (int idx = 0; idx < heightOccCC && isAboveHorizon; idx++) {
            if (v + leftHorizon[heightOccCC - 1 - idx] < horizon[u + idx]) {
              isAboveHorizon = false;
            }
          }
          break;
        case 2:  // 180 degrees
          for (int idx = 0; idx < widthOccCC && isAboveHorizon; idx++) {
            if (v + topHorizon[widthOccCC - 1 - idx] < horizon[u + idx]) {
              isAboveHorizon = false;
            }
          }
          break;
        case 3:  // 270 degrees
          for (int idx = 0; idx < heightOccCC && isAboveHorizon; idx++) {
            if (v + rightHorizon[idx] < horizon[u + idx]) {
              isAboveHorizon = false;
            }
          }
          break;
        default:  // no rotation
          for (int idx = 0; idx < widthOccCC && isAboveHorizon; idx++) {
            if (v + bottomHorizon[idx] < horizon[u + idx]) {
              isAboveHorizon = false;
            }
          }
          break;
        }  //check if the patch can fit on space above the horizon
        if (!isAboveHorizon) continue;
        bool fit = true;
        for (int vv = 0; vv < heightOccCC && fit; vv++) {
          for (int uu = 0; uu < widthOccCC && fit; uu++) {
            int newV, newU;
            switch (orientation) {
            case 0:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            case 1:  // 90 degree
              newV = v + uu;
              newU = u + (heightOccCC - 1 - vv);
              break;
            case 2:  // 180 degrees
              newV = v + (heightOccCC - 1 - vv);
              newU = u + (widthOccCC - 1 - uu);
              break;
            case 3:  // 270 degrees
              newV = v + (widthOccCC - 1 - uu);
              newU = u + vv;
              break;
            default:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            }
            if ((newV >= occupancySizeV) || (newV < 0)) fit = false;
            else if ((newU >= occupancySizeU) || (newU < 0)) fit = false;
            else if (occupancy.get(newV, newU) && occupancyCC.get(vv, uu))
              fit = false;
          }
        }
        if (fit) {
          //calculate the wasted space
          int wasted_space          = 0;
          int extension_horizon     = 0;
          int maxDelta              = 0;
          int wasted_space_external = 0;
          int wasted_space_internal = 0;
          int lambda = 100;  //--> bias towards the upper part of the canvas
          switch (orientation) {
          case 0:  // no rotation
            for (int idx = 0; idx < widthOccCC; idx++) {
              //check how much horizon is streched
              int delta =
                (v + heightOccCC - 1 - topHorizon[idx]) - horizon[u + idx];
              if (delta > 0) extension_horizon += delta;
              if ((delta > 0) && ((delta + horizon[u + idx]) > maxDelta))
                maxDelta = delta + horizon[u + idx];
              //from the horizon to the patch
              wasted_space_external +=
                v + bottomHorizon[idx] - horizon[u + idx];
              // calculating internal wasted space --> because of new block2patch
              // restriction, this area only contains locations for the local patch
              for (int idx2 = bottomHorizon[idx] + 1;
                   idx2 < heightOccCC - topHorizon[idx];
                   idx2++) {
                if (!occupancyCC.get(idx2, idx)) wasted_space_internal++;
              }
            }
            wasted_space =
              int(lambda * maxDelta + extension_horizon + wasted_space_external
                  + wasted_space_internal);
            break;
          case 1:  // 90 degree
            for (int idx = 0; idx < heightOccCC; idx++) {
              //check how much horizon is streched
              int delta =
                (v + widthOccCC - 1 - rightHorizon[heightOccCC - 1 - idx])
                - horizon[u + idx];
              if (delta > 0) extension_horizon += delta;
              if ((delta > 0) && ((delta + horizon[u + idx]) > maxDelta))
                maxDelta = delta + horizon[u + idx];
              //from the horizon to the patch
              wasted_space_external +=
                v + leftHorizon[heightOccCC - 1 - idx] - horizon[u + idx];
              // calculating internal wasted space
              for (int idx2 = int(widthOccCC - 1 - rightHorizon[idx]);
                   idx2 >= leftHorizon[idx];
                   idx2--) {
                if (!occupancyCC.get(idx, idx2)) wasted_space_internal++;
              }
            }
            wasted_space =
              int(lambda * maxDelta + extension_horizon + wasted_space_external
                  + wasted_space_internal);
            break;
          case 2:  // 180 degrees
            for (int idx = 0; idx < widthOccCC; idx++) {
              //check how much horizon is streched
              int delta =
                (v + heightOccCC - 1 - bottomHorizon[widthOccCC - 1 - idx])
                - horizon[u + idx];
              if (delta > 0) extension_horizon += delta;
              if ((delta > 0) && ((delta + horizon[u + idx]) > maxDelta))
                maxDelta = delta + horizon[u + idx];
              //from the horizon to the patch
              wasted_space_external +=
                v + topHorizon[widthOccCC - 1 - idx] - horizon[u + idx];
              // calculating internal wasted space
              for (int idx2 = int(heightOccCC - 1 - topHorizon[idx]);
                   idx2 >= bottomHorizon[idx];
                   idx2--) {
                if (!occupancyCC.get(idx2, idx)) wasted_space_internal++;
              }
            }
            wasted_space =
              int(lambda * maxDelta + extension_horizon + wasted_space_external
                  + wasted_space_internal);
            break;
          case 3:  // 270 degrees
            for (int idx = 0; idx < heightOccCC; idx++) {
              //check how much horizon is streched
              int delta =
                (v + widthOccCC - 1 - leftHorizon[idx]) - horizon[u + idx];
              if (delta > 0) extension_horizon += delta;
              if ((delta > 0) && ((delta + horizon[u + idx]) > maxDelta))
                maxDelta = delta + horizon[u + idx];
              //from the horizon to the patch
              wasted_space_external +=
                v + rightHorizon[idx] - horizon[u + idx];
              // calculating internal wasted space
              for (int idx2 = leftHorizon[idx] + 1;
                   idx2 < widthOccCC - rightHorizon[idx];
                   idx2++) {
                if (!occupancyCC.get(idx, idx2)) wasted_space_internal++;
              }
            }
            wasted_space =
              int(lambda * maxDelta + extension_horizon + wasted_space_external
                  + wasted_space_internal);
            break;
          default:  // no rotation
            for (int idx = 0; idx < widthOccCC; idx++) {
              //check how much horizon is streched
              int delta =
                (v + widthOccCC - 1 - topHorizon[idx]) - horizon[u + idx];
              if (delta > 0) extension_horizon += delta;
              if ((delta > 0) && ((delta + horizon[u + idx]) > maxDelta))
                maxDelta = delta + horizon[u + idx];
              //from the horizon to the patch
              wasted_space_external +=
                v + bottomHorizon[idx] - horizon[u + idx];
              // calculating internal wasted space --> because of new block2patch
              // restriction, this area only contains locations for the local patch
              for (int idx2 = bottomHorizon[idx] + 1;
                   idx2 < widthOccCC - topHorizon[idx];
                   idx2++) {
                if (!occupancyCC.get(idx2, idx)) wasted_space_internal++;
              }
            }
            wasted_space =
              int(lambda * maxDelta + extension_horizon + wasted_space_external
                  + wasted_space_internal);
            break;
          }
          //now save the position with less wasted space
          if (wasted_space < best_wasted_space) {
            best_wasted_space = wasted_space;
            bestU             = u;
            bestV             = v;
            bestOrientation   = orientation;
            foundPos          = true;
#if DEBUG_ORTHO_PATCH_PACKING
            if (params.keepIntermediateFiles) {
              // dump of the current state of the occupancy
              vmesh::Frame<uint8_t> occupancyVideo;
              occupancyVideo.resize(
                occupancySizeU, occupancySizeV, vmesh::ColourSpace::BGR444p);
              occupancyVideo.plane(0) = occupancy;
              for (auto& pixel : occupancyVideo.plane(0).buffer()) {
                pixel *= 255;
              }
              //copy patch
              //update the global occupancy map
              for (int vv = 0; vv < heightOccCC; vv++) {
                for (int uu = 0; uu < widthOccCC; uu++) {
                  int newV, newU;
                  switch (bestOrientation) {
                  case 0:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  case 1:  // 90 degree
                    newV = bestV + uu;
                    newU = bestU + (heightOccCC - 1 - vv);
                    break;
                  case 2:  // 180 degrees
                    newV = bestV + (heightOccCC - 1 - vv);
                    newU = bestU + (widthOccCC - 1 - uu);
                    break;
                  case 3:  // 270 degrees
                    newV = bestV + (widthOccCC - 1 - uu);
                    newU = bestU + vv;
                    break;
                  default:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  }
                  occupancyVideo.plane(1).set(
                    newV, newU, 255 * occupancyCC.get(vv, uu));
                }
              }
              int deltaVV = std::max(1, (heightOccCC - 1));
              for (int vv = 0; vv < heightOccCC; vv += deltaVV) {
                for (int uu = 0; uu < widthOccCC; uu++) {
                  int newV, newU;
                  switch (bestOrientation) {
                  case 0:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  case 1:  // 90 degree
                    newV = bestV + uu;
                    newU = bestU + (heightOccCC - 1 - vv);
                    break;
                  case 2:  // 180 degrees
                    newV = bestV + (heightOccCC - 1 - vv);
                    newU = bestU + (widthOccCC - 1 - uu);
                    break;
                  case 3:  // 270 degrees
                    newV = bestV + (widthOccCC - 1 - uu);
                    newU = bestU + vv;
                    break;
                  default:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  }
                  occupancyVideo.plane(1).set(newV, newU, 255);
                }
              }
              int deltaUU = std::max(1, (widthOccCC - 1));
              for (int vv = 0; vv < heightOccCC; vv++) {
                for (int uu = 0; uu < widthOccCC; uu += deltaUU) {
                  int newV, newU;
                  switch (bestOrientation) {
                  case 0:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  case 1:  // 90 degree
                    newV = bestV + uu;
                    newU = bestU + (heightOccCC - 1 - vv);
                    break;
                  case 2:  // 180 degrees
                    newV = bestV + (heightOccCC - 1 - vv);
                    newU = bestU + (widthOccCC - 1 - uu);
                    break;
                  case 3:  // 270 degrees
                    newV = bestV + (widthOccCC - 1 - uu);
                    newU = bestU + vv;
                    break;
                  default:  // no rotation
                    newV = bestV + vv;
                    newU = bestU + uu;
                    break;
                  }
                  occupancyVideo.plane(1).set(newV, newU, 255);
                }
              }
              for (int idx = 0; idx < occupancySizeU; idx++) {
                occupancyVideo.plane(0).set(horizon[idx], idx, 0);
                occupancyVideo.plane(1).set(horizon[idx], idx, 0);
                occupancyVideo.plane(2).set(horizon[idx], idx, 255);
              }
              auto prefix = keepFilesPathPrefix + "_patch"
                            + std::to_string(packedCC.getIdxPatch())
                            + "_occupancy" + std::to_string(occupancySizeU)
                            + "x" + std::to_string(occupancySizeV) + "_bgrp";
              occupancyVideo.append(prefix + ".rgb");
            }
#endif
          }
        }
      }
    }
  }
  if (foundPos) {
    //update the global occupancy map
    for (int vv = 0; vv < heightOccCC; vv++) {
      for (int uu = 0; uu < widthOccCC; uu++) {
        int newV, newU;
        switch (bestOrientation) {
        case 0:  // no rotation
          newV = bestV + vv;
          newU = bestU + uu;
          break;
        case 1:  // 90 degree
          newV = bestV + uu;
          newU = bestU + (heightOccCC - 1 - vv);
          break;
        case 2:  // 180 degrees
          newV = bestV + (heightOccCC - 1 - vv);
          newU = bestU + (widthOccCC - 1 - uu);
          break;
        case 3:  // 270 degrees
          newV = bestV + (widthOccCC - 1 - uu);
          newU = bestU + vv;
          break;
        default:  // no rotation
          newV = bestV + vv;
          newU = bestU + uu;
          break;
        }
        if (!occupancy.get(newV, newU))
          occupancy.set(newV, newU, occupancyCC.get(vv, uu));
      }
    }
    //update the UV coordinates of the connected components
    // scale
    for (auto& uv : packedCC.texCoords()) { uv *= packedCC.getScale(); }
    // gutter
    for (auto& uv : packedCC.texCoords()) {
      uv += Vec2<MeshType>(params.gutter, params.gutter);
    }
    // rotation
    for (auto& uv : packedCC.texCoords()) {
      MeshType uu = uv[0];
      MeshType vv = uv[1];
      switch (bestOrientation) {
      case 0:  // no rotation
        break;
      case 1:  // 90 degree
        uv[1] = uu;
        uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
        break;
      case 2:  // 180 degrees
        uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
        uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
        break;
      case 3:  // 270 degrees
        uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
        uv[0] = vv;
        break;
      default:  // no rotation
        uv[1] = vv;
        uv[0] = uu;
        break;
      }
    }
    packedCC.setOrientation(bestOrientation);
    // translation
    for (auto& uv : packedCC.texCoords()) {
      uv += Vec2<MeshType>(bestU * geometryVideoBlockSize,
                           bestV * geometryVideoBlockSize);
    }
    packedCC.setU0(bestU);
    packedCC.setV0(bestV);
    packedCC.setSizeU(widthOccCC);
    packedCC.setSizeV(heightOccCC);
#if DEBUG_ORTHO_PATCH_PACKING
    std::cout << "CC[" << packedCC.getIdxPatch()
              << "] = (#tri: " << packedCC.triangles().size()
              << ", #vert: " << packedCC.points().size()
              << ", Area: " << packedCC.area()
              << ", Perimeter: " << packedCC.perimeter()
              << ", P: " << packedCC.getProjection()
              << ", S: " << packedCC.getScale()
              << ", O: " << packedCC.getOrientation()
              << ", U0: " << packedCC.getU0() << ", V0: " << packedCC.getV0()
              << ", SizeU: " << packedCC.getSizeU()
              << ", SizeV: " << packedCC.getSizeV() << ")" << std::endl;
    if (params.keepIntermediateFiles) {
      // dump of the current state of the occupancy
      vmesh::Frame<uint8_t> occupancyVideo;
      occupancyVideo.resize(
        occupancySizeU, occupancySizeV, vmesh::ColourSpace::BGR444p);
      occupancyVideo.plane(0) = occupancy;
      for (auto& pixel : occupancyVideo.plane(0).buffer()) { pixel *= 255; }
      int deltaVV = std::max(1, (heightOccCC - 1));
      for (int vv = 0; vv < heightOccCC; vv += deltaVV) {
        for (int uu = 0; uu < widthOccCC; uu++) {
          int newV, newU;
          switch (bestOrientation) {
          case 0:  // no rotation
            newV = bestV + vv;
            newU = bestU + uu;
            break;
          case 1:  // 90 degree
            newV = bestV + uu;
            newU = bestU + (heightOccCC - 1 - vv);
            break;
          case 2:  // 180 degrees
            newV = bestV + (heightOccCC - 1 - vv);
            newU = bestU + (widthOccCC - 1 - uu);
            break;
          case 3:  // 270 degrees
            newV = bestV + (widthOccCC - 1 - uu);
            newU = bestU + vv;
            break;
          default:  // no rotation
            newV = bestV + vv;
            newU = bestU + uu;
            break;
          }
          occupancyVideo.plane(1).set(newV, newU, 255);
        }
      }
      int deltaUU = std::max(1, (widthOccCC - 1));
      for (int vv = 0; vv < heightOccCC; vv++) {
        for (int uu = 0; uu < widthOccCC; uu += deltaUU) {
          int newV, newU;
          switch (bestOrientation) {
          case 0:  // no rotation
            newV = bestV + vv;
            newU = bestU + uu;
            break;
          case 1:  // 90 degree
            newV = bestV + uu;
            newU = bestU + (heightOccCC - 1 - vv);
            break;
          case 2:  // 180 degrees
            newV = bestV + (heightOccCC - 1 - vv);
            newU = bestU + (widthOccCC - 1 - uu);
            break;
          case 3:  // 270 degrees
            newV = bestV + (widthOccCC - 1 - uu);
            newU = bestU + vv;
            break;
          default:  // no rotation
            newV = bestV + vv;
            newU = bestU + uu;
            break;
          }
          occupancyVideo.plane(1).set(newV, newU, 255);
        }
      }
      for (int idx = 0; idx < occupancySizeU; idx++) {
        occupancyVideo.plane(0).set(horizon[idx], idx, 0);
        occupancyVideo.plane(1).set(horizon[idx], idx, 0);
        occupancyVideo.plane(2).set(horizon[idx], idx, 255);
      }
      auto prefix = keepFilesPathPrefix + "_occupancy"
                    + std::to_string(occupancySizeU) + "x"
                    + std::to_string(occupancySizeV) + "_bgrp";
      occupancyVideo.append(prefix + ".rgb");
    }
#endif
    return true;
  }
  return false;
}

bool
TextureParametrization::pack_patch_flexible_with_temporal_stability(
  vmesh::Plane<uint8_t>&        occupancy,
  vmesh::Plane<uint8_t>&        occupancyCC,
  ConnectedComponent<MeshType>& packedCC,
  ConnectedComponent<MeshType>& matchedCC,
  const VMCEncoderParameters&   params,
  std::string                   keepFilesPathPrefix) {
  auto geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  int  occupancySizeV         = occupancy.height();
  int  occupancySizeU         = occupancy.width();
  int  widthOccCC             = occupancyCC.width();
  int  heightOccCC            = occupancyCC.height();
  // now place the patch in the empty area, maintaining the patch orientation
  int orientation = matchedCC.getOrientation();
  // spiral search to find the closest available position
  int x   = 0;
  int y   = 0;
  int end = (std::max)(occupancySizeU, occupancySizeV)
            * (std::max)(occupancySizeU, occupancySizeV) * 4;
  for (int i = 0; i < end; ++i) {
    // Translate coordinates and mask them out.
    int  u   = x + matchedCC.getU0();
    int  v   = y + matchedCC.getV0();
    bool fit = true;
    for (int vv = 0; vv < heightOccCC && fit; vv++) {
      for (int uu = 0; uu < widthOccCC && fit; uu++) {
        int newV, newU;
        switch (orientation) {
        case 0:  // no rotation
          newV = v + vv;
          newU = u + uu;
          break;
        case 1:  // 90 degree
          newV = v + uu;
          newU = u + (heightOccCC - 1 - vv);
          break;
        case 2:  // 180 degrees
          newV = v + (heightOccCC - 1 - vv);
          newU = u + (widthOccCC - 1 - uu);
          break;
        case 3:  // 270 degrees
          newV = v + (widthOccCC - 1 - uu);
          newU = u + vv;
          break;
        default:  // no rotation
          newV = v + vv;
          newU = u + uu;
          break;
        }
        if ((newV >= occupancySizeV) || (newV < 0)) fit = false;
        else if ((newU >= occupancySizeU) || (newU < 0)) fit = false;
        else if (occupancy.get(newV, newU) && occupancyCC.get(vv, uu))
          fit = false;
      }
    }
    if (fit) {
      //update the global occupancy map
      for (int vv = 0; vv < heightOccCC; vv++) {
        for (int uu = 0; uu < widthOccCC; uu++) {
          int newV, newU;
          switch (orientation) {
          case 0:  // no rotation
            newV = v + vv;
            newU = u + uu;
            break;
          case 1:  // 90 degree
            newV = v + uu;
            newU = u + (heightOccCC - 1 - vv);
            break;
          case 2:  // 180 degrees
            newV = v + (heightOccCC - 1 - vv);
            newU = u + (widthOccCC - 1 - uu);
            break;
          case 3:  // 270 degrees
            newV = v + (widthOccCC - 1 - uu);
            newU = u + vv;
            break;
          default:  // no rotation
            newV = v + vv;
            newU = u + uu;
            break;
          }
          if (!occupancy.get(newV, newU))
            occupancy.set(newV, newU, occupancyCC.get(vv, uu));
        }
      }
      //update the UV coordinates of the connected components
      // scale
      for (auto& uv : packedCC.texCoords()) { uv *= packedCC.getScale(); }
      // gutter
      for (auto& uv : packedCC.texCoords()) {
        uv += Vec2<MeshType>(params.gutter, params.gutter);
      }
      // rotation
      for (auto& uv : packedCC.texCoords()) {
        MeshType uu = uv[0];
        MeshType vv = uv[1];
        switch (orientation) {
        case 0:  // no rotation
          break;
        case 1:  // 90 degree
          uv[1] = uu;
          uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
          break;
        case 2:  // 180 degrees
          uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
          uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
          break;
        case 3:  // 270 degrees
          uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
          uv[0] = vv;
          break;
        default:  // no rotation
          uv[1] = vv;
          uv[0] = uu;
          break;
        }
      }
      packedCC.setOrientation(orientation);
      // translation
      for (auto& uv : packedCC.texCoords()) {
        uv += Vec2<MeshType>(u * geometryVideoBlockSize,
                             v * geometryVideoBlockSize);
      }
      packedCC.setU0(u);
      packedCC.setV0(v);
      packedCC.setSizeU(widthOccCC);
      packedCC.setSizeV(heightOccCC);
#if DEBUG_ORTHO_PATCH_PACKING
      std::cout << "CC[" << packedCC.getIdxPatch()
                << ",m:" << packedCC.getRefPatch()
                << "] = (#tri: " << packedCC.triangles().size()
                << ", #vert: " << packedCC.points().size()
                << ", Area: " << packedCC.area()
                << ", Perimeter: " << packedCC.perimeter()
                << ", P: " << packedCC.getProjection()
                << ", S: " << packedCC.getScale()
                << ", O: " << packedCC.getOrientation()
                << ", U0: " << packedCC.getU0() << ", V0: " << packedCC.getV0()
                << ", SizeU: " << packedCC.getSizeU()
                << ", SizeV: " << packedCC.getSizeV() << ")" << std::endl;
      if (params.keepIntermediateFiles) {
        // dump of the current state of the occupancy
        vmesh::Frame<uint8_t> occupancyVideo;
        occupancyVideo.resize(
          occupancySizeU, occupancySizeV, vmesh::ColourSpace::BGR444p);
        occupancyVideo.plane(0) = occupancy;
        for (auto& pixel : occupancyVideo.plane(0).buffer()) { pixel *= 255; }
        int deltaVV = std::max(1, (heightOccCC - 1));
        for (int vv = 0; vv < heightOccCC; vv += deltaVV) {
          for (int uu = 0; uu < widthOccCC; uu++) {
            int newV, newU;
            switch (orientation) {
            case 0:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            case 1:  // 90 degree
              newV = v + uu;
              newU = u + (heightOccCC - 1 - vv);
              break;
            case 2:  // 180 degrees
              newV = v + (heightOccCC - 1 - vv);
              newU = u + (widthOccCC - 1 - uu);
              break;
            case 3:  // 270 degrees
              newV = v + (widthOccCC - 1 - uu);
              newU = u + vv;
              break;
            default:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            }
            occupancyVideo.plane(1).set(newV, newU, 255);
          }
        }
        int deltaUU = std::max(1, (widthOccCC - 1));
        for (int vv = 0; vv < heightOccCC; vv++) {
          for (int uu = 0; uu < widthOccCC; uu += deltaUU) {
            int newV, newU;
            switch (orientation) {
            case 0:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            case 1:  // 90 degree
              newV = v + uu;
              newU = u + (heightOccCC - 1 - vv);
              break;
            case 2:  // 180 degrees
              newV = v + (heightOccCC - 1 - vv);
              newU = u + (widthOccCC - 1 - uu);
              break;
            case 3:  // 270 degrees
              newV = v + (widthOccCC - 1 - uu);
              newU = u + vv;
              break;
            default:  // no rotation
              newV = v + vv;
              newU = u + uu;
              break;
            }
            occupancyVideo.plane(1).set(newV, newU, 255);
          }
        }
        auto prefix = keepFilesPathPrefix + "_occupancy"
                      + std::to_string(occupancySizeU) + "x"
                      + std::to_string(occupancySizeV) + "_bgrp";
        occupancyVideo.append(prefix + ".rgb");
      }
#endif
      return true;
    } else {
      if (abs(x) <= abs(y) && (x != y || x >= 0)) {
        x += ((y >= 0) ? 1 : -1);
      } else {
        y += ((x >= 0) ? -1 : 1);
      }
    }
  }
  return false;
}

bool
TextureParametrization::pack_patches_with_patch_scale_and_rotation(
  TriangleMesh<MeshType>&                               mesh,
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
  std::vector<int32_t>&   projectedConnectedComponentsCategories,
  std::vector<Vec2<int>>& bias,
  std::vector<ConnectedComponent<MeshType>>& packedCCList,
  const VMCEncoderParameters&                params,
  double                                     scalingAdjustment,
  std::string                                keepFilesPathPrefix) {
  double texParamHeight         = params.texParamHeight;
  double texParamWidth          = params.texParamWidth;
  auto   geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  //pack patches into the atlas image (global UV)
  const double avgAreaNumVertRatio = mesh.area() / (double)mesh.pointCount();
  const auto   bbMesh              = mesh.boundingBox();
  double       frameScaleProjection =
    (double)(texParamHeight - 2 * params.gutter)
    / (double)(geometryVideoBlockSize
               * ceil((bbMesh.max - bbMesh.min).normInf()
                      / geometryVideoBlockSize));
  frameScaleProjection *=
    scalingAdjustment;  // leaving some room for adjustments
  vmesh::Plane<uint8_t> occupancy;
  int                   occupancySizeV =
    std::ceil((double)texParamHeight / (double)geometryVideoBlockSize);
  int occupancySizeU =
    std::ceil((double)texParamWidth / (double)geometryVideoBlockSize);
  occupancy.resize(occupancySizeU, occupancySizeV);
  occupancy.fill(false);
  int ccIdx = 0;
  // load UV coordinates
  for (int idx = 0; idx < projectedCCList.size(); idx++) {
    auto&                               ccMesh = projectedCCList[idx];
    vmesh::ConnectedComponent<MeshType> cc;
    //copy mesh
    cc.triangles()         = ccMesh->triangles();
    cc.points()            = ccMesh->points();
    cc.texCoords()         = ccMesh->texCoords();
    cc.texCoordTriangles() = ccMesh->texCoordTriangles();
    // set projection and initial scale
    cc.setProjection(projectedConnectedComponentsCategories[idx]);
    cc.setU1(bias[idx][0]);
    cc.setV1(bias[idx][1]);
    std::vector<double> frac = cc.fracFramePackingScale(frameScaleProjection);
    cc.setFrameScaleFrac(frac);
    cc.setScaleFrac(frac);
    packedCCList.push_back(cc);
  }
  //sort list to start packing from largest cc to smallest
  std::stable_sort(
    packedCCList.begin(),
    packedCCList.end(),
    [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
      return (a.area() > b.area());
    });
  // obtain the scaling of each connected component
  for (auto& packedCC : packedCCList) {
    if (params.bPatchScaling) {
      double ccAreaNumVerRatio =
        packedCC.area() / (double)packedCC.pointCount();
      if (ccAreaNumVerRatio < (avgAreaNumVertRatio * 0.5)) {
        //packedCC.setScale(packedCC.getScale() * 2.0);
        packedCC.setScale(packedCC.getScale() * 1.0);
      } else {
        //packedCC.setScale(packedCC.getScale() * 1.0);
        // reduce the size of the 
        std::vector<double> packScale = packedCC.fracFramePackingScale(params.packingScaling);
        double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
        packedCC.setScale(packedCC.getScale() * _pack);
      }
    } else {
      packedCC.setScale(packedCC.getScale() * 1.0);
    }
  }
  // pack loop
  for (int idxPack = 0; idxPack < packedCCList.size(); idxPack++) {
    auto& packedCC = packedCCList[idxPack];
    // set index and reference
    packedCC.setIdxPatch(idxPack);
    packedCC.setRefPatch(-1);
    //rasterize the patch
    vmesh::Plane<uint8_t> occupancyCC;
    patch_rasterization(occupancyCC, packedCC, params);
    //now place the patch in the empty area
    bool invertDirection = (packedCC.area() < (packedCCList[0].area() * 0.01))
                           && params.packSmallPatchesOnTop;
    if (!pack_patch_flexible(occupancy,
                             occupancyCC,
                             packedCC,
                             params,
                             keepFilesPathPrefix,
                             invertDirection)) {
      // do the cc again and reduce it's size
      std::vector<double> packScale =
        packedCC.fracFramePackingScale(params.packingScaling);
      double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
      packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Reducing patch scale by "
                << (1 - params.packingScaling) * 100 << "% ("
                << packedCC.getScale() << ")" << std::endl;
#endif
      if ((packedCC.getScale() / frameScaleProjection) < 0.5) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "Connected Component using small scale ("
                  << packedCC.getScale()
                  << "), better to repack all connected components with a new "
                     "adjusted scale (current adjustment: "
                  << scalingAdjustment << ")" << std::endl;
#endif
        packedCCList.clear();
        return false;
      }
      idxPack--;  //re-do the last element
    }
  }
  // add patches to mesh
  mesh.clear();
  if (params.iDeriveTextCoordFromPos == 3) {  // separate CCs
    separateTexCCs(
      projectedConnectedComponentsCategories, bias, packedCCList, params);
  }
  ccIdx = 0;
  for (auto& cc : packedCCList) {
    //now turn the coordinates into the [0,1] range
    for (auto& uv : cc.texCoords()) {
      uv[0] /= (occupancySizeU * geometryVideoBlockSize);
      uv[1] /= (occupancySizeV * geometryVideoBlockSize);
    }
    mesh.append(cc);
    if (params.useRawUV) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (cc.getProjection() == rawUvId) {
        std::cout << "set ucoords and vcoords for CCid " << ccIdx << std::endl;
        cc.setNumRawUVMinus1(cc.texCoordTriangleCount() * 3 - 1);
        int codedUVindex = 0;
        for (auto& uvTri : cc.texCoordTriangles()) {
          for (int i = 0; i < 3; i++) {
            auto uvidx = cc.texCoord(uvTri[i]);
            cc.setUcoord(codedUVindex, cc.texCoord(uvTri[i])[0]);
            cc.setVcoord(codedUVindex, cc.texCoord(uvTri[i])[1]);
            codedUVindex++;
          }
        }
      }
    }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
    if (params.keepIntermediateFiles) {
      auto prefix =
        keepFilesPathPrefix + "_final_packed_CC#" + std::to_string(ccIdx);
      cc.save(prefix + ".obj");
    }
    ccIdx++;
#endif
    ccIdx++;
  }
  ////now turn the coordinates into the [0,1] range
  //for (auto& uv : mesh.texCoords()) {
  //  uv[0] /= (occupancySizeU * geometryVideoBlockSize);
  //  uv[1] /= (occupancySizeV * geometryVideoBlockSize);
  //}
#if DEBUG_ORTHO_PATCH_PACKING
  if (params.keepIntermediateFiles) {
    mesh.save(keepFilesPathPrefix + "_orthoAtlas.obj");
  }
#endif
  return true;
}

bool
TextureParametrization::
  pack_patches_with_patch_scale_and_rotation_and_temporal_stabilization(
    TriangleMesh<MeshType>&                               mesh,
    std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
    std::vector<int32_t>&   projectedConnectedComponentsCategories,
    std::vector<Vec2<int>>& bias,
    std::vector<ConnectedComponent<MeshType>>& packedCCList,
    std::vector<ConnectedComponent<MeshType>>& previousFramePackedCCList,
    const VMCEncoderParameters&                params,
    double                                     scalingAdjustment,
    std::string                                keepFilesPathPrefix) {
  double texParamHeight         = params.texParamHeight;
  double texParamWidth          = params.texParamWidth;
  auto   geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  //pack patches into the atlas image (global UV)
  const double avgAreaNumVertRatio = mesh.area() / (double)mesh.pointCount();
  const auto   bbMesh              = mesh.boundingBox();
  double       frameScaleProjection =
    (double)(texParamHeight - 2 * params.gutter)
    / (double)(geometryVideoBlockSize
               * ceil((bbMesh.max - bbMesh.min).normInf()
                      / geometryVideoBlockSize));
  frameScaleProjection *=
    scalingAdjustment;  // leaving some room for adjustments
  vmesh::Plane<uint8_t> occupancy;
  int                   occupancySizeV =
    std::ceil((double)texParamHeight / (double)geometryVideoBlockSize);
  int occupancySizeU =
    std::ceil((double)texParamWidth / (double)geometryVideoBlockSize);
  occupancy.resize(occupancySizeU, occupancySizeV);
  occupancy.fill(false);
  int ccIdx = 0;
  //lists with the matched and unmatched patches
  std::vector<int> isMatched;
  isMatched.resize(projectedCCList.size(), -1);
  //find matches for the connected components of previous frames
  float thresholdIOU = 0.3F;  // volumes have to have at least 30% overlap
  for (int prevIdx = 0; prevIdx < previousFramePackedCCList.size();
       prevIdx++) {
    auto& previousCC = previousFramePackedCCList[prevIdx];
    float maxIou     = 0.0F;
    int   bestIdx    = -1;
    auto  prevBB     = previousCC.boundingBox();
    for (int idxCur = 0; idxCur < projectedCCList.size(); idxCur++) {
      auto& curCC = projectedCCList[idxCur];
      if ((isMatched[idxCur] >= 0)
          || (previousCC.getProjection()
              != projectedConnectedComponentsCategories[idxCur]))
        continue;
      auto   curBB           = curCC->boundingBox();
      auto   intersect       = curBB & prevBB;
      double intersectVolume = intersect.volume();
      double unionVolume = curBB.volume() + prevBB.volume() - intersectVolume;
      float  iou         = intersectVolume / unionVolume;
      if (iou > maxIou) {
        bestIdx = idxCur;
        maxIou  = iou;
      }
    }
    if (maxIou > thresholdIOU) { isMatched[bestIdx] = prevIdx; }
  }

  // packing matched patches
  std::vector<vmesh::ConnectedComponent<MeshType>> matchedPackedCCList;
  // load and scale UV coordinates
  for (int idx = 0; idx < isMatched.size(); idx++) {
    if (isMatched[idx] != -1) {
      vmesh::ConnectedComponent<MeshType> cc;
      //load mesh
      cc.triangles()         = projectedCCList[idx]->triangles();
      cc.points()            = projectedCCList[idx]->points();
      cc.texCoords()         = projectedCCList[idx]->texCoords();
      cc.texCoordTriangles() = projectedCCList[idx]->texCoordTriangles();
      //set projection and initial scale
      cc.setProjection(projectedConnectedComponentsCategories[idx]);
      cc.setU1(bias[idx][0]);
      cc.setV1(bias[idx][1]);
      std::vector<double> frac =
        cc.fracFramePackingScale(frameScaleProjection);
      cc.setFrameScaleFrac(frac);
      cc.setScaleFrac(frac);
      //set reference
      cc.setIdxPatch(idx);
      cc.setRefPatch(isMatched[idx]);
      matchedPackedCCList.push_back(cc);
    }
  }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
  std::cout << "Found " << matchedPackedCCList.size() << " matched patches"
            << std::endl;
#endif
  //sort list to have similar order as the reference
  std::stable_sort(
    matchedPackedCCList.begin(),
    matchedPackedCCList.end(),
    [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
      return (a.getRefPatch() > b.getRefPatch());
    });
  // obtain the scaling of each connected component
  for (auto& packedCC : matchedPackedCCList) {
    if (params.bPatchScaling) {
      double ccAreaNumVerRatio =
        packedCC.area() / (double)packedCC.pointCount();
      if (ccAreaNumVerRatio < (avgAreaNumVertRatio * 0.5)) {
          //packedCC.setScale(packedCC.getScale() * 2.0);
        packedCC.setScale(packedCC.getScale() * 1.0);
      } else {
          //packedCC.setScale(packedCC.getScale() * 1.0);
          // reduce the size of the 
          std::vector<double> packScale = packedCC.fracFramePackingScale(params.packingScaling);
          double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
          packedCC.setScale(packedCC.getScale() * _pack);
      }
    } else {
      packedCC.setScale(packedCC.getScale() * 1.0);
    }
  }
  // pack loop for matched patches
  while (matchedPackedCCList.size() > 0) {
    vmesh::ConnectedComponent<MeshType> packedCC = matchedPackedCCList.back();
    int                                 isMatchedPos = packedCC.getIdxPatch();
    packedCC.setIdxPatch(ccIdx);
    int   matchRefId = packedCC.getRefPatch();
    auto& matchedCC  = previousFramePackedCCList[matchRefId];
    matchedPackedCCList.pop_back();
    //rasterize the patch
    vmesh::Plane<uint8_t> occupancyCC;
    patch_rasterization(occupancyCC, packedCC, params);
    //now place the patch in the empty area
    if (!pack_patch_flexible_with_temporal_stability(occupancy,
                                                     occupancyCC,
                                                     packedCC,
                                                     matchedCC,
                                                     params,
                                                     keepFilesPathPrefix)) {
      // do the cc again and reduce it's size
      std::vector<double> packScale =
        packedCC.fracFramePackingScale(params.packingScaling);
      double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
      packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Reducing patch scale by "
                << (1 - params.packingScaling) * 100 << "% ("
                << packedCC.getScale() << ")" << std::endl;
#endif
      if ((packedCC.getScale() / frameScaleProjection) < 0.5) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "Connected Component using small scale ("
                  << packedCC.getScale()
                  << "), will try to pack without temporal stability "
                  << std::endl;
#endif
        isMatched[isMatchedPos] = -1;
      } else {
        packedCC.setIdxPatch(isMatchedPos);
        matchedPackedCCList.push_back(packedCC);
      }
    } else {
      packedCCList.push_back(packedCC);
      ccIdx++;
    }
  }

  //packing unmatched patches
  std::vector<vmesh::ConnectedComponent<MeshType>> unmatchedPackedCCList;
  // load  UV coordinates
  for (int idx = 0; idx < isMatched.size(); idx++) {
    if (isMatched[idx] == -1) {
      vmesh::ConnectedComponent<MeshType> cc;
      //load mesh
      cc.triangles()         = projectedCCList[idx]->triangles();
      cc.points()            = projectedCCList[idx]->points();
      cc.texCoords()         = projectedCCList[idx]->texCoords();
      cc.texCoordTriangles() = projectedCCList[idx]->texCoordTriangles();
      //set projection and initial scale
      cc.setProjection(projectedConnectedComponentsCategories[idx]);
      cc.setU1(bias[idx][0]);
      cc.setV1(bias[idx][1]);
      std::vector<double> frac =
        cc.fracFramePackingScale(frameScaleProjection);
      cc.setFrameScaleFrac(frac);
      cc.setScaleFrac(frac);
      //set reference
      cc.setIdxPatch(idx);
      cc.setRefPatch(-1);
      unmatchedPackedCCList.push_back(cc);
    }
  }
  //sort list to start packing from largest cc to smallest
  std::stable_sort(
    unmatchedPackedCCList.begin(),
    unmatchedPackedCCList.end(),
    [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
      return (a.area() > b.area());
    });
  // obtain the scaling of each connected component
  for (auto& packedCC : unmatchedPackedCCList) {
    if (params.bPatchScaling) {
      double ccAreaNumVerRatio =
        packedCC.area() / (double)packedCC.pointCount();
      if (ccAreaNumVerRatio < (avgAreaNumVertRatio * 0.5)) {
          //packedCC.setScale(packedCC.getScale() * 2.0);
          packedCC.setScale(packedCC.getScale() * 1.0);
      } else {
          //packedCC.setScale(packedCC.getScale() * 1.0);
          // reduce the size of the 
          std::vector<double> packScale = packedCC.fracFramePackingScale(params.packingScaling);
          double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
          packedCC.setScale(packedCC.getScale() * _pack);
      }
    } else {
      packedCC.setScale(packedCC.getScale() * 1.0);
    }
  }
  // pack loop for unmatched patches
  for (int idxPack = 0; idxPack < unmatchedPackedCCList.size(); idxPack++) {
    auto& packedCC = unmatchedPackedCCList[idxPack];
    // set index and reference
    packedCC.setIdxPatch(ccIdx);
    packedCC.setRefPatch(-1);
    //rasterize the patch
    vmesh::Plane<uint8_t> occupancyCC;
    patch_rasterization(occupancyCC, packedCC, params);
    //now place the patch in the empty area
    bool invertDirection = (packedCC.area() < (packedCCList[0].area() * 0.01))
                           && params.packSmallPatchesOnTop;
    if (!pack_patch_flexible(occupancy,
                             occupancyCC,
                             packedCC,
                             params,
                             keepFilesPathPrefix,
                             invertDirection)) {
      // do the cc again and reduce it's size
      std::vector<double> packScale =
        packedCC.fracFramePackingScale(params.packingScaling);
      double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
      packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Reducing patch scale by "
                << (1 - params.packingScaling) * 100 << "% ("
                << packedCC.getScale() << ")" << std::endl;
#endif
      if ((packedCC.getScale() / frameScaleProjection) < 0.5) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "Connected Component using small scale ("
                  << packedCC.getScale()
                  << "), better to repack all connected components with a new "
                     "adjusted scale (current adjustment: "
                  << scalingAdjustment << ")" << std::endl;
#endif
        packedCCList.clear();
        return false;
      }
      idxPack--;  //re-do the last element
    } else {
      packedCCList.push_back(packedCC);
      ccIdx++;
    }
  }

  // add patches to mesh
  mesh.clear();
  if (params.iDeriveTextCoordFromPos == 3) {  // separate CCs
    separateTexCCs(
      projectedConnectedComponentsCategories, bias, packedCCList, params);
  }
  ccIdx = 0;
  for (auto& cc : packedCCList) {
    //now turn the coordinates into the [0,1] range
    for (auto& uv : cc.texCoords()) {
      uv[0] /= (occupancySizeU * geometryVideoBlockSize);
      uv[1] /= (occupancySizeV * geometryVideoBlockSize);
    }
    mesh.append(cc);
    if (params.useRawUV) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (cc.getProjection() == rawUvId) {
        std::cout << "set ucoords and vcooeds for CCid" << ccIdx << std::endl;
        cc.setNumRawUVMinus1(cc.texCoordTriangleCount() * 3 - 1);
        int codedUVindex = 0;
        for (auto& uvTri : cc.texCoordTriangles()) {
          for (int i = 0; i < 3; i++) {
            auto uvidx = cc.texCoord(uvTri[i]);
            cc.setUcoord(codedUVindex, cc.texCoord(uvTri[i])[0]);
            cc.setVcoord(codedUVindex, cc.texCoord(uvTri[i])[1]);
            codedUVindex++;
          }
        }
      }
    }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
    if (params.keepIntermediateFiles) {
      auto prefix =
        keepFilesPathPrefix + "_final_packed_CC#" + std::to_string(ccIdx);
      cc.save(prefix + ".obj");
    }
    ccIdx++;
#endif
  }
  ////now turn the coordinates into the [0,1] range
  //for (auto& uv : mesh.texCoords()) {
  //  uv[0] /= (occupancySizeU * geometryVideoBlockSize);
  //  uv[1] /= (occupancySizeV * geometryVideoBlockSize);
  //}
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
  if (params.keepIntermediateFiles) {
    mesh.save(keepFilesPathPrefix + "_orthoAtlas.obj");
  }
#endif
  return true;
}

bool
TextureParametrization::tetris_packing_patches_with_patch_scale_and_rotation(
  TriangleMesh<MeshType>&                               mesh,
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
  std::vector<int32_t>&   projectedConnectedComponentsCategories,
  std::vector<Vec2<int>>& bias,
  std::vector<ConnectedComponent<MeshType>>& packedCCList,
  const VMCEncoderParameters&                params,
  double                                     scalingAdjustment,
  std::string                                keepFilesPathPrefix) {
  double texParamHeight         = params.texParamHeight;
  double texParamWidth          = params.texParamWidth;
  auto   geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  //pack patches into the atlas image (global UV)
  const double avgAreaNumVertRatio = mesh.area() / (double)mesh.pointCount();
  const auto   bbMesh              = mesh.boundingBox();
  double       frameScaleProjection =
    (double)(texParamHeight - 2 * params.gutter)
    / (double)(geometryVideoBlockSize
               * ceil((bbMesh.max - bbMesh.min).normInf()
                      / geometryVideoBlockSize));
  frameScaleProjection *=
    scalingAdjustment;  // leaving some room for adjustments
  vmesh::Plane<uint8_t> occupancy;
  int                   occupancySizeV =
    std::ceil((double)texParamHeight / (double)geometryVideoBlockSize);
  int occupancySizeU =
    std::ceil((double)texParamWidth / (double)geometryVideoBlockSize);
  occupancy.resize(occupancySizeU, occupancySizeV);
  occupancy.fill(false);
  std::vector<int> horizon;
  horizon.resize(occupancySizeU, 0);
#if DEBUG_ORTHO_PATCH_PACKING
  std::cout << "Horizon :[";
  for (int i = 0; i < occupancySizeU; i++) { std::cout << horizon[i] << ","; }
  std::cout << "]" << std::endl;
#endif
  int ccIdx = 0;
  // load UV coordinates
  double totalArea = 0;
  for (int idx = 0; idx < projectedCCList.size(); idx++) {
    auto&                               ccMesh = projectedCCList[idx];
    vmesh::ConnectedComponent<MeshType> cc;
    //copy mesh
    cc.triangles()         = ccMesh->triangles();
    cc.points()            = ccMesh->points();
    cc.texCoords()         = ccMesh->texCoords();
    cc.texCoordTriangles() = ccMesh->texCoordTriangles();
    // set projection and initial scale
    cc.setProjection(projectedConnectedComponentsCategories[idx]);
    cc.setU1(bias[idx][0]);
    cc.setV1(bias[idx][1]);
    std::vector<double> frac = cc.fracFramePackingScale(frameScaleProjection);
    cc.setFrameScaleFrac(frac);
    cc.setScaleFrac(frac);
    packedCCList.push_back(cc);
    auto ccBB = cc.texCoordBoundingBox();
    int  widthOccCC =
      std::ceil((cc.getScale() * ccBB.max[0] + 2 * params.gutter)
                / (double)geometryVideoBlockSize);
    int heightOccCC =
      std::ceil((cc.getScale() * ccBB.max[1] + 2 * params.gutter)
                / (double)geometryVideoBlockSize);
    totalArea += (widthOccCC) * (heightOccCC);
  }
  if (totalArea > (1.5 * (occupancySizeV * occupancySizeU))) {
#if DEBUG_ORTHO_PATCH_PACKING
    std::cout
      << "Canvas area (" << occupancySizeV * occupancySizeU
      << "), is not big enough to contain all patches at current scale ( "
      << scalingAdjustment << "), the total area needed is " << totalArea
      << std::endl;
#endif
    packedCCList.clear();
    return false;
  }
  //sort list to start packing from largest cc to smallest
  std::stable_sort(
    packedCCList.begin(),
    packedCCList.end(),
    [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
      return (a.area() > b.area());  // packing from largest to smallest patch
    });
  // obtain the scaling of each connected component
  for (auto& packedCC : packedCCList) {
    if (params.bPatchScaling) {
      double ccAreaNumVerRatio =
        packedCC.area() / (double)packedCC.pointCount();
      if (ccAreaNumVerRatio < (avgAreaNumVertRatio * 0.5)) {
          //packedCC.setScale(packedCC.getScale() * 2.0);
        packedCC.setScale(packedCC.getScale() * 1.0);
      } else {
          //packedCC.setScale(packedCC.getScale() * 1.0);
          // reduce the size of the 
          std::vector<double> packScale = packedCC.fracFramePackingScale(params.packingScaling);
          double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
          packedCC.setScale(packedCC.getScale() * _pack);
      }
    } else {
      packedCC.setScale(packedCC.getScale() * 1.0);
    }
  }
  // pack loop
  for (int idxPack = 0; idxPack < packedCCList.size(); idxPack++) {
    auto& packedCC = packedCCList[idxPack];
    // set index and reference
    packedCC.setIdxPatch(idxPack);
    packedCC.setRefPatch(-1);
    //rasterize the patch
    vmesh::Plane<uint8_t> occupancyCC;
    patch_rasterization(occupancyCC, packedCC, params);
    std::vector<int> topHorizon;
    std::vector<int> bottomHorizon;
    std::vector<int> rightHorizon;
    std::vector<int> leftHorizon;
    patch_horizons(
      occupancyCC, topHorizon, bottomHorizon, rightHorizon, leftHorizon);
    //now place the patch in the empty area
    bool invertDirection = (packedCC.area() < (packedCCList[0].area() * 0.01))
                           && params.packSmallPatchesOnTop;
    if (!pack_patch_flexible_tetris(occupancy,
                                    occupancyCC,
                                    packedCC,
                                    params,
                                    keepFilesPathPrefix,
                                    horizon,
                                    topHorizon,
                                    bottomHorizon,
                                    rightHorizon,
                                    leftHorizon)) {
      // do the cc again and reduce it's size
      std::vector<double> packScale =
        packedCC.fracFramePackingScale(params.packingScaling);
      double _pack = (double)packScale[0] / (double)std::pow(2, packScale[1]);
      packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Reducing patch scale by "
                << (1 - params.packingScaling) * 100 << "% ("
                << packedCC.getScale() << ")" << std::endl;
#endif
      if ((packedCC.getScale() / frameScaleProjection) < 0.25) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "Connected Component using small scale ("
                  << packedCC.getScale()
                  << "), better to repack all connected components with a new "
                     "adjusted scale (current adjustment: "
                  << scalingAdjustment << ")" << std::endl;
#endif
        packedCCList.clear();
        return false;
      }
      idxPack--;  //re-do the last element
    } else {
      //update the horizon
      int bestU           = packedCC.getU0();
      int sizeU           = packedCC.getSizeU();
      int bestV           = packedCC.getV0();
      int sizeV           = packedCC.getSizeV();
      int bestOrientation = packedCC.getOrientation();
      int newVal;
      switch (packedCC.getOrientation()) {
      case 0:  // no rotation
        for (int idx = 0; idx < sizeU; idx++) {
          newVal = int(bestV + sizeV - 1 - topHorizon[idx]);
          if (newVal > horizon[bestU + idx]) horizon[bestU + idx] = newVal;
        }
        break;
      case 1:  // 90 degree
        for (int idx = 0; idx < sizeV; idx++) {
          newVal = int(bestV + sizeU - 1 - rightHorizon[sizeV - 1 - idx]);
          if (newVal > horizon[bestU + idx]) horizon[bestU + idx] = newVal;
        }
        break;
      case 2:  // 180 degrees
        for (int idx = 0; idx < sizeU; idx++) {
          newVal = int(bestV + sizeV - 1 - bottomHorizon[sizeU - 1 - idx]);
          if (newVal > horizon[bestU + idx]) horizon[bestU + idx] = newVal;
        }
        break;
      case 3:  // 270 degrees
        for (int idx = 0; idx < sizeV; idx++) {
          newVal = int(bestV + sizeU - 1 - leftHorizon[idx]);
          if (newVal > horizon[bestU + idx]) horizon[bestU + idx] = newVal;
        }
        break;
      default:  // no rotation
        for (int idx = 0; idx < sizeU; idx++) {
          newVal = int(bestV + sizeV - 1 - topHorizon[idx]);
          if (newVal > horizon[bestU + idx]) horizon[bestU + idx] = newVal;
        }
        break;
      }  //check if the patch can fit on space above the horizon
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Horizon :[";
      for (int i = 0; i < occupancySizeU; i++) {
        std::cout << horizon[i] << ",";
      }
      std::cout << "]" << std::endl;
#endif
    }
  }
  // add patches to mesh
  mesh.clear();
  if (params.iDeriveTextCoordFromPos == 3) {  // separate CCs
    separateTexCCs(
      projectedConnectedComponentsCategories, bias, packedCCList, params);
  }
  ccIdx = 0;
  for (auto& cc : packedCCList) {
    //now turn the coordinates into the [0,1] range
    for (auto& uv : cc.texCoords()) {
      uv[0] /= (occupancySizeU * geometryVideoBlockSize);
      uv[1] /= (occupancySizeV * geometryVideoBlockSize);
    }
    mesh.append(cc);
    int rawUvId = params.use45DegreeProjection ? 18 : 6;
    if (cc.getProjection() == rawUvId) {
      std::cout << "set ucoords and vcooeds for CCid" << ccIdx << std::endl;
      cc.setNumRawUVMinus1(cc.texCoordTriangleCount() * 3 - 1);
      int codedUVindex = 0;
      for (auto& uvTri : cc.texCoordTriangles()) {
        for (int i = 0; i < 3; i++) {
          auto uvidx = cc.texCoord(uvTri[i]);
          cc.setUcoord(codedUVindex, cc.texCoord(uvTri[i])[0]);
          cc.setVcoord(codedUVindex, cc.texCoord(uvTri[i])[1]);
          codedUVindex++;
        }
      }
    }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
    if (params.keepIntermediateFiles) {
      auto prefix =
        keepFilesPathPrefix + "_final_packed_CC#" + std::to_string(ccIdx);
      cc.save(prefix + ".obj");
    }
    ccIdx++;
#endif
  }
  ////now turn the coordinates into the [0,1] range
  //for (auto& uv : mesh.texCoords()) {
  //  uv[0] /= (occupancySizeU * geometryVideoBlockSize);
  //  uv[1] /= (occupancySizeV * geometryVideoBlockSize);
  //}
#if DEBUG_ORTHO_PATCH_PACKING
  if (params.keepIntermediateFiles) {
    mesh.save(keepFilesPathPrefix + "_orthoAtlas.obj");
  }
#endif
  return true;
}

bool
TextureParametrization::
  projection_packing_patches_with_patch_scale_and_rotation(
    TriangleMesh<MeshType>&                               mesh,
    std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
    std::vector<int32_t>&   projectedConnectedComponentsCategories,
    std::vector<Vec2<int>>& bias,
    std::vector<ConnectedComponent<MeshType>>& packedCCList,
    const VMCEncoderParameters&                params,
    double                                     scalingAdjustment,
    std::string                                keepFilesPathPrefix) {
  auto   geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  double texParamHeight         = params.texParamHeight;
  double texParamWidth          = params.texParamWidth;
  //pack patches into the atlas image (global UV)
  const double avgAreaNumVertRatio = mesh.area() / (double)mesh.pointCount();
  const auto   bbMesh              = mesh.boundingBox();
  double       frameScaleProjection =
    (double)(texParamHeight - 2 * params.gutter)
    / (double)(geometryVideoBlockSize
               * ceil((bbMesh.max - bbMesh.min).normInf()
                      / geometryVideoBlockSize));
  frameScaleProjection *=
    scalingAdjustment;  // leaving some room for adjustments
  vmesh::Plane<uint8_t> occupancy;
  int                   occupancySizeV =
    std::ceil(texParamHeight / (double)geometryVideoBlockSize);
  int occupancySizeU =
    std::ceil(texParamWidth / (double)geometryVideoBlockSize);
  occupancy.resize(occupancySizeU, occupancySizeV);
  occupancy.fill(false);

  // check the number of categories that we need to pack
  auto categories = projectedConnectedComponentsCategories;
  std::sort(categories.begin(), categories.end());
  auto last = std::unique(categories.begin(), categories.end());
  categories.erase(last, categories.end());
  int numCategories = categories.size();

  //now allocate structures for each category
  std::vector<vmesh::Plane<uint8_t>> occupancyPerCategory;
  std::vector<std::vector<ConnectedComponent<MeshType>>>
                                     packedCCListPerCategory;
  std::vector<vmesh::Box2<MeshType>> bbPerCategory;
  std::vector<int>                   numOccupiedPos;
  occupancyPerCategory.resize(numCategories);
  packedCCListPerCategory.resize(numCategories);
  numOccupiedPos.resize(numCategories, 0);
  bbPerCategory.resize(numCategories);
  for (int i = 0; i < numCategories; i++) {
    occupancyPerCategory[i].resize(occupancySizeU, occupancySizeV);
    occupancyPerCategory[i].fill(false);
    bbPerCategory[i].min = std::numeric_limits<MeshType>::max();
    bbPerCategory[i].max = std::numeric_limits<MeshType>::min();
    // load connected component
    for (int idx = 0; idx < projectedCCList.size(); idx++) {
      if (projectedConnectedComponentsCategories[idx] == categories[i]) {
        auto&                               ccMesh = projectedCCList[idx];
        vmesh::ConnectedComponent<MeshType> cc;
        //copy mesh
        cc.triangles()         = ccMesh->triangles();
        cc.points()            = ccMesh->points();
        cc.texCoords()         = ccMesh->texCoords();
        cc.texCoordTriangles() = ccMesh->texCoordTriangles();
        // set projection and initial scale
        cc.setProjection(projectedConnectedComponentsCategories[idx]);
        cc.setU1(bias[idx][0]);
        cc.setV1(bias[idx][1]);
        std::vector<double> frac =
          cc.fracFramePackingScale(frameScaleProjection);
        cc.setFrameScaleFrac(frac);
        cc.setScaleFrac(frac);
        packedCCListPerCategory[i].push_back(cc);
      }
    }
    //sort list to start packing from smallest to largest area
    std::stable_sort(
      packedCCListPerCategory[i].begin(),
      packedCCListPerCategory[i].end(),
      [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
        return (a.area() > b.area());
      });
    // pack loop keeping the projection position
    for (int idx = 0; idx < packedCCListPerCategory[i].size(); idx++) {
      vmesh::ConnectedComponent<MeshType>& packedCC =
        packedCCListPerCategory[i][idx];
      packedCC.setIdxPatch(idx);
      packedCC.setRefPatch(categories[i]);
      vmesh::ConnectedComponent<MeshType> matchedCC;
      matchedCC.setOrientation(0);
      int tangentAxis, biTangentAxis, projAxis;
      int tangentAxisDir, biTangentAxisDir, projAxisDir;
      getProjectedAxis(categories[i],
                       tangentAxis,
                       biTangentAxis,
                       projAxis,
                       tangentAxisDir,
                       biTangentAxisDir,
                       projAxisDir);
      auto bb = packedCC.boundingBox();
      matchedCC.setU0(
        (packedCC.getScale() * bb.min[tangentAxis] + params.gutter)
        / (double)geometryVideoBlockSize);
      matchedCC.setV0(
        (packedCC.getScale() * bb.min[biTangentAxis] + params.gutter)
        / (double)geometryVideoBlockSize);
      //rasterize the patch
      vmesh::Plane<uint8_t> occupancyCC;
      patch_rasterization(occupancyCC, packedCC, params);
      //now place the patch in the empty area
      if (!pack_patch_flexible_with_temporal_stability(occupancyPerCategory[i],
                                                       occupancyCC,
                                                       packedCC,
                                                       matchedCC,
                                                       params,
                                                       keepFilesPathPrefix)) {
        if (!pack_patch_flexible(occupancyPerCategory[i],
                                 occupancyCC,
                                 packedCC,
                                 params,
                                 keepFilesPathPrefix,
                                 false)) {
          // do the cc again and reduce it's size
          std::vector<double> packScale =
            packedCC.fracFramePackingScale(params.packingScaling);
          double _pack =
            (double)packScale[0] / (double)std::pow(2, packScale[1]);
          packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
          std::cout << "Reducing patch scale by "
                    << (1 - params.packingScaling) * 100 << "% ("
                    << packedCC.getScale() << ")" << std::endl;
#endif
          if ((packedCC.getScale() / frameScaleProjection) < 0.5) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
            std::cout << "Connected Component using small scale ("
                      << packedCC.getScale()
                      << "), better to repack all connected components with a "
                         "new adjusted scale (current adjustment: "
                      << scalingAdjustment << ")" << std::endl;
#endif
            packedCCList.clear();
            return false;
          } else idx--;
        } else {
          bbPerCategory[i].enclose(
            Vec2<MeshType>(packedCC.getU0(), packedCC.getV0()));
          bbPerCategory[i].enclose(
            Vec2<MeshType>(packedCC.getU0() + packedCC.getSizeU(),
                           packedCC.getV0() + packedCC.getSizeV()));
        }
      } else {
        bbPerCategory[i].enclose(
          Vec2<MeshType>(packedCC.getU0(), packedCC.getV0()));
        bbPerCategory[i].enclose(
          Vec2<MeshType>(packedCC.getU0() + packedCC.getSizeU(),
                         packedCC.getV0() + packedCC.getSizeV()));
      }
    }
    //calculate the occupied area
    for (int h = 0; h < occupancySizeV; h++) {
      for (int w = 0; w < occupancySizeU; w++) {
        if (occupancyPerCategory[i].get(w, h)) numOccupiedPos[i]++;
      }
    }
  }
  // now merge all occupancy maps into a single atlas
  // place the categories with largest areas first
  std::vector<int> catOrder;
  catOrder.resize(numCategories);
  for (int i = 0; i < numCategories; i++) catOrder[i] = i;
  std::sort(catOrder.begin(), catOrder.end(), [&](int a, int b) {
    if (numOccupiedPos[a] > numOccupiedPos[b]) return true;
    else if (numOccupiedPos[a] == numOccupiedPos[b]) return (a > b);
    else return false;
  });
  // find the occupancy map bounding box
  for (int i = 0; i < numCategories; i++) {
    int                                 idx = catOrder[i];
    vmesh::ConnectedComponent<MeshType> packedCC;
    packedCC.setIdxPatch(idx);
    //create the occupancy for the group of patches
    vmesh::Plane<uint8_t> occupancyCC;
    int widthOccCC  = bbPerCategory[idx].max[0] - bbPerCategory[idx].min[0];
    int heightOccCC = bbPerCategory[idx].max[1] - bbPerCategory[idx].min[1];
    occupancyCC.resize(widthOccCC, heightOccCC);
    occupancyCC.fill(false);
    for (int h = bbPerCategory[idx].min[1]; h < bbPerCategory[idx].max[1];
         h++) {
      for (int w = bbPerCategory[idx].min[0]; w < bbPerCategory[idx].max[0];
           w++) {
        occupancyCC.set(h - bbPerCategory[idx].min[1],
                        w - bbPerCategory[idx].min[0],
                        occupancyPerCategory[idx].get(h, w));
      }
    }
    //now place the patch in the empty area
    if (!pack_patch_flexible(occupancy,
                             occupancyCC,
                             packedCC,
                             params,
                             keepFilesPathPrefix,
                             false)) {
      // do the packing again with reduce size
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Combined Connected Component using small scale ("
                << packedCCListPerCategory[idx][0].getScale()
                << "), better to repack all connected components with a new "
                   "adjusted scale (current adjustment: "
                << scalingAdjustment << ")" << std::endl;
#endif
      packedCCList.clear();
      return false;
    } else {
      // update all the positions with the new orientation (U0,V0)
      for (auto& cc : packedCCListPerCategory[idx]) {
        int newU0 = cc.getU0();
        int newV0 = cc.getV0();
        // remove the occupancy map corner
        for (auto& uv : cc.texCoords()) {
          uv -=
            Vec2<MeshType>(bbPerCategory[idx].min[0] * geometryVideoBlockSize,
                           bbPerCategory[idx].min[1] * geometryVideoBlockSize);
        }
        newU0 -= bbPerCategory[idx].min[0];
        newV0 -= bbPerCategory[idx].min[1];
        // now apply the latest packing results
        // rotation
        for (auto& uv : cc.texCoords()) {
          MeshType uu = uv[0];
          MeshType vv = uv[1];
          switch (packedCC.getOrientation()) {
          case 0:  // no rotation
            break;
          case 1:  // 90 degree
            uv[1] = uu;
            uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
            break;
          case 2:  // 180 degrees
            uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
            uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
            break;
          case 3:  // 270 degrees
            uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
            uv[0] = vv;
            break;
          default:  // no rotation
            break;
          }
        }
        MeshType uu0 = newU0;
        MeshType vv0 = newV0;
        switch (packedCC.getOrientation()) {
        case 0:  // no rotation
          break;
        case 1:  // 90 degree
          newV0 = uu0;
          newU0 = (heightOccCC - (cc.getSizeV() + vv0));
          break;
        case 2:  // 180 degrees
          newV0 = (heightOccCC - (cc.getSizeV() + vv0));
          newU0 = (widthOccCC - (cc.getSizeU() + uu0));
          break;
        case 3:  // 270 degrees
          newV0 = (widthOccCC - (cc.getSizeU() + uu0));
          newU0 = vv0;
          break;
        default:  // no rotation
          break;
        }
        cc.setOrientation(packedCC.getOrientation());
        // translation
        for (auto& uv : cc.texCoords()) {
          uv += Vec2<MeshType>(packedCC.getU0() * geometryVideoBlockSize,
                               packedCC.getV0() * geometryVideoBlockSize);
        }
        newU0 += packedCC.getU0();
        cc.setU0(newU0);
        newV0 += packedCC.getV0();
        cc.setV0(newV0);
        cc.setIdxPatch(packedCCList.size());
        packedCCList.push_back(cc);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "added CC[" << cc.getIdxPatch()
                  << "] = (#tri: " << cc.triangles().size()
                  << ", #vert: " << cc.points().size()
                  << ", P: " << cc.getProjection() << ", S: " << cc.getScale()
                  << ", O: " << cc.getOrientation() << ", U0: " << cc.getU0()
                  << ", V0: " << cc.getV0() << ", SizeU: " << cc.getSizeU()
                  << ", SizeV: " << cc.getSizeV() << ")" << std::endl;
#endif
      }
    }
  }

  // add patches to mesh
  mesh.clear();
  if (params.iDeriveTextCoordFromPos == 3) {  // separate CCs
    separateTexCCs(
      projectedConnectedComponentsCategories, bias, packedCCList, params);
  }
  int ccIdx = 0;
  for (auto& cc : packedCCList) {
    //now turn the coordinates into the [0,1] range
    for (auto& uv : cc.texCoords()) {
      uv[0] /= (occupancySizeU * geometryVideoBlockSize);
      uv[1] /= (occupancySizeV * geometryVideoBlockSize);
    }
    mesh.append(cc);
    if (params.useRawUV) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (cc.getProjection() == rawUvId) {
        std::cout << "set ucoords and vcooeds for CCid" << ccIdx << std::endl;
        cc.setNumRawUVMinus1(cc.texCoordTriangleCount() * 3 - 1);
        int codedUVindex = 0;
        for (auto& uvTri : cc.texCoordTriangles()) {
          for (int i = 0; i < 3; i++) {
            auto uvidx = cc.texCoord(uvTri[i]);
            cc.setUcoord(codedUVindex, cc.texCoord(uvTri[i])[0]);
            cc.setVcoord(codedUVindex, cc.texCoord(uvTri[i])[1]);
            codedUVindex++;
          }
        }
      }
    }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
    auto prefix = keepFilesPathPrefix + "_final_packed_connected_component_#"
                  + std::to_string(ccIdx);
    cc.save(prefix + ".obj");
    std::cout << "FINAL CC[" << ccIdx << "] -> perimeter(" << cc.perimeter()
              << "), stretchL2(" << cc.stretchL2(cc.getProjection()) << ")"
              << std::endl;
#endif
    ccIdx++;
  }
  ////now turn the coordinates into the [0,1] range
  //for (auto& uv : mesh.texCoords()) {
  //  uv[0] /= (occupancySizeU * geometryVideoBlockSize);
  //  uv[1] /= (occupancySizeV * geometryVideoBlockSize);
  //}
#if DEBUG_ORTHO_PATCH_PACKING
  if (params.keepIntermediateFiles) {
    mesh.save(keepFilesPathPrefix + "_hierarchical_packing_orthoAtlas.obj");
  }
#endif
  return true;
}

bool
TextureParametrization::projection_packing_patches_and_temporal_stabilization(
  TriangleMesh<MeshType>&                               mesh,
  std::vector<std::shared_ptr<TriangleMesh<MeshType>>>& projectedCCList,
  std::vector<int32_t>&   projectedConnectedComponentsCategories,
  std::vector<Vec2<int>>& bias,
  std::vector<ConnectedComponent<MeshType>>& packedCCList,
  const VMCEncoderParameters&                params,
  double                                     scalingAdjustment,
  std::string                                keepFilesPathPrefix,
  int32_t                                    frameIndex,
  std::vector<int>&                          catOrderGof,
  int&                                       frameMaxOccupiedU,
  int&                                       frameMaxOccupiedV) {
  auto   geometryVideoBlockSize = 1 << params.log2GeometryVideoBlockSize;
  double texParamHeight         = params.texParamHeight;
  double texParamWidth          = params.texParamWidth;
  //pack patches into the atlas image (global UV)
  const double avgAreaNumVertRatio = mesh.area() / (double)mesh.pointCount();
  const auto   bbMesh              = mesh.boundingBox();

  //double frameScaleProjection = (double)(params.texParamHeight - 2 * params.gutter) / (double)(geometryVideoBlockSize * ceil((bbMesh.max - bbMesh.min).normInf() / geometryVideoBlockSize));
  double frameScaleProjection = (double)(texParamHeight - 2 * params.gutter)
                                / (1 << params.bitDepthPosition);

  frameScaleProjection *=
    scalingAdjustment;  // leaving some room for adjustments
  vmesh::Plane<uint8_t> occupancy;
  //int occupancySizeV = std::ceil((double)params.texParamHeight / (double)geometryVideoBlockSize);
  //int occupancySizeU = std::ceil((double)params.texParamWidth / (double)geometryVideoBlockSize);
  int occupancySizeV =
    std::ceil(texParamHeight * 4.0 / (double)geometryVideoBlockSize);
  int occupancySizeU =
    std::ceil(texParamWidth * 4.0 / (double)geometryVideoBlockSize);

  occupancy.resize(occupancySizeU, occupancySizeV);
  occupancy.fill(false);

  // check the number of categories that we need to pack
  //auto categories = projectedConnectedComponentsCategories;
  //std::sort(categories.begin(), categories.end());
  //auto last = std::unique(categories.begin(), categories.end());
  //categories.erase(last, categories.end());
  //int numCategories = categories.size();
  int                  numCategories = params.use45DegreeProjection ? 18 : 6;
  std::vector<int32_t> categories;
  categories.resize(numCategories);
  for (int i = 0; i < numCategories; i++) categories[i] = i;

  int orientationCount = params.use45DegreeProjection ? 18 : 6;

  //now allocate structures for each category
  std::vector<vmesh::Plane<uint8_t>> occupancyPerCategory;
  std::vector<std::vector<ConnectedComponent<MeshType>>>
                                     packedCCListPerCategory;
  std::vector<vmesh::Box2<MeshType>> bbPerCategory;
  std::vector<int>                   numOccupiedPos;
  occupancyPerCategory.resize(numCategories);
  packedCCListPerCategory.resize(numCategories);
  numOccupiedPos.resize(numCategories, 0);
  bbPerCategory.resize(numCategories);
  for (int i = 0; i < numCategories; i++) {
    occupancyPerCategory[i].resize(occupancySizeU, occupancySizeV);
    occupancyPerCategory[i].fill(false);
    bbPerCategory[i].min = std::numeric_limits<MeshType>::max();
    bbPerCategory[i].max = std::numeric_limits<MeshType>::min();
    // load connected component
    for (int idx = 0; idx < projectedCCList.size(); idx++) {
      if (projectedConnectedComponentsCategories[idx] == categories[i]) {
        auto&                               ccMesh = projectedCCList[idx];
        vmesh::ConnectedComponent<MeshType> cc;
        //copy mesh
        cc.triangles()         = ccMesh->triangles();
        cc.points()            = ccMesh->points();
        cc.texCoords()         = ccMesh->texCoords();
        cc.texCoordTriangles() = ccMesh->texCoordTriangles();
        // set projection and initial scale
        cc.setProjection(projectedConnectedComponentsCategories[idx]);
        cc.setU1(bias[idx][0]);
        cc.setV1(bias[idx][1]);
        std::vector<double> frac =
          cc.fracFramePackingScale(frameScaleProjection);
        cc.setFrameScaleFrac(frac);
        cc.setScaleFrac(frac);
        packedCCListPerCategory[i].push_back(cc);
      }
    }
    //sort list to start packing from largest to smallest area
    std::stable_sort(
      packedCCListPerCategory[i].begin(),
      packedCCListPerCategory[i].end(),
      [&](ConnectedComponent<MeshType> a, ConnectedComponent<MeshType> b) {
        return (a.area() > b.area());
      });
    // pack loop keeping the projection position
    for (int idx = 0; idx < packedCCListPerCategory[i].size(); idx++) {
      vmesh::ConnectedComponent<MeshType>& packedCC =
        packedCCListPerCategory[i][idx];
      packedCC.setIdxPatch(idx);
      packedCC.setRefPatch(categories[i]);
      vmesh::ConnectedComponent<MeshType> matchedCC;
      matchedCC.setOrientation(0);
      int tangentAxis, biTangentAxis, projAxis;
      int tangentAxisDir, biTangentAxisDir, projAxisDir;
      getProjectedAxis(categories[i],
                       tangentAxis,
                       biTangentAxis,
                       projAxis,
                       tangentAxisDir,
                       biTangentAxisDir,
                       projAxisDir);
      auto bb = packedCC.boundingBox();

      //matchedCC.setU0((packedCC.getScale() * bb.min[tangentAxis] + params.gutter) / (double)geometryVideoBlockSize);
      //matchedCC.setV0((packedCC.getScale() * bb.min[biTangentAxis] + params.gutter) / (double)geometryVideoBlockSize);
      matchedCC.setU0((packedCC.getScale() * bb.min[tangentAxis])
                      / (double)geometryVideoBlockSize);
      matchedCC.setV0((packedCC.getScale() * bb.min[biTangentAxis])
                      / (double)geometryVideoBlockSize);
      //rasterize the patch
      vmesh::Plane<uint8_t> occupancyCC;
      patch_rasterization(occupancyCC, packedCC, params);
      //now place the patch in the empty area
      if (!pack_patch_flexible_with_temporal_stability(occupancyPerCategory[i],
                                                       occupancyCC,
                                                       packedCC,
                                                       matchedCC,
                                                       params,
                                                       keepFilesPathPrefix)) {
        if (!pack_patch_flexible(occupancyPerCategory[i],
                                 occupancyCC,
                                 packedCC,
                                 params,
                                 keepFilesPathPrefix,
                                 false)) {
          // do the cc again and reduce it's size
          std::vector<double> packScale =
            packedCC.fracFramePackingScale(params.packingScaling);
          double _pack =
            (double)packScale[0] / (double)std::pow(2, packScale[1]);
          packedCC.setScale(packedCC.getScale() * _pack);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
          std::cout << "Reducing patch scale by "
                    << (1 - params.packingScaling) * 100 << "% ("
                    << packedCC.getScale() << ")" << std::endl;
#endif
          if ((packedCC.getScale() / frameScaleProjection) < 0.5) {
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
            std::cout << "Connected Component using small scale ("
                      << packedCC.getScale()
                      << "), better to repack all connected components with a "
                         "new adjusted scale (current adjustment: "
                      << scalingAdjustment << ")" << std::endl;
#endif
            packedCCList.clear();
            return false;
          } else idx--;
        } else {
          bbPerCategory[i].enclose(
            Vec2<MeshType>(packedCC.getU0(), packedCC.getV0()));
          bbPerCategory[i].enclose(
            Vec2<MeshType>(packedCC.getU0() + packedCC.getSizeU(),
                           packedCC.getV0() + packedCC.getSizeV()));
        }
      } else {
        bbPerCategory[i].enclose(
          Vec2<MeshType>(packedCC.getU0(), packedCC.getV0()));
        bbPerCategory[i].enclose(
          Vec2<MeshType>(packedCC.getU0() + packedCC.getSizeU(),
                         packedCC.getV0() + packedCC.getSizeV()));
      }
    }
    //calculate the occupied area
    for (int h = 0; h < occupancySizeV; h++) {
      for (int w = 0; w < occupancySizeU; w++) {
        if (occupancyPerCategory[i].get(w, h)) numOccupiedPos[i]++;
      }
    }
    if (numOccupiedPos[i] == 0) {
      // there were no elements in this category, so we need to set the bounding box to zero
      bbPerCategory[i].min[0] = 0.0;
      bbPerCategory[i].min[1] = 0.0;
      bbPerCategory[i].max[0] = 0.0;
      bbPerCategory[i].max[1] = 0.0;
    }
  }
  // now merge all occupancy maps into a single atlas
  // place the categories with largest areas first
  std::vector<int> catOrder;
  catOrder.resize(numCategories);
  for (int i = 0; i < numCategories; i++) catOrder[i] = i;
  //std::sort(catOrder.begin(), catOrder.end(), [&](int a, int b) {
  //    if (numOccupiedPos[a] > numOccupiedPos[b])
  //        return true;
  //    else if (numOccupiedPos[a] == numOccupiedPos[b])
  //        return (a > b);
  //    else
  //        return false;
  //    });

  //sort only the first frames
  std::cout
    << "sort the categories with largest areas first for the first frames"
    << std::endl;
  ;
  if (!frameIndex) {
    std::sort(catOrder.begin(), catOrder.end(), [&](int a, int b) {
      //return (numOccupiedPos[a] > numOccupiedPos[b]);
      if (numOccupiedPos[a] > numOccupiedPos[b]) return true;
      else if (numOccupiedPos[a] == numOccupiedPos[b]) return (a > b);
      else return false;
    });

    std::vector<bool> flag(orientationCount, false);
    for (int i = 0; i < numCategories; i++) {
      catOrderGof[i]    = catOrder[i];
      flag[catOrder[i]] = true;
    }
    int count = numCategories;
    for (int i = 0; i < orientationCount; i++) {
      if (!flag[i]) {
        catOrderGof[count] = i;
        count++;
      }
    }
  } else {
    int count = 0;
    for (int i = 0; i < orientationCount; i++) {
      auto it = std::find(
        std::cbegin(categories), std::cend(categories), catOrderGof[i]);
      if (it != std::cend(categories)) {
        catOrder[count] = catOrderGof[i];
        count++;
      }
    }
  }
  // for check
  for (int i = 0; i < numCategories; i++) {
    std::cout << "catOrder[" << i << "]" << catOrder[i] << std::endl;
    ;
  }

  // find the occupancy map bounding box
  //avoid packing small patches betrween large patches
  int lastOccupiedU = 0;
  int lastOccupiedV = 0;
  frameMaxOccupiedU = 0;
  frameMaxOccupiedV = 0;

  int startU = 0;

  for (int i = 0; i < numCategories; i++) {
    int                                 idx = catOrder[i];
    vmesh::ConnectedComponent<MeshType> packedCC;
    packedCC.setIdxPatch(idx);
    //create the occupancy for the group of patches
    vmesh::Plane<uint8_t> occupancyCC;
    int widthOccCC  = bbPerCategory[idx].max[0] - bbPerCategory[idx].min[0];
    int heightOccCC = bbPerCategory[idx].max[1] - bbPerCategory[idx].min[1];
    occupancyCC.resize(widthOccCC, heightOccCC);
    occupancyCC.fill(false);
    for (int h = bbPerCategory[idx].min[1]; h < bbPerCategory[idx].max[1];
         h++) {
      for (int w = bbPerCategory[idx].min[0]; w < bbPerCategory[idx].max[0];
           w++) {
        occupancyCC.set(h - bbPerCategory[idx].min[1],
                        w - bbPerCategory[idx].min[0],
                        occupancyPerCategory[idx].get(h, w));
      }
    }
    //now place the patch in the empty area
    int startV = 0;
    if (numCategories == 6) {
      if (i != 5) {
        //stack horizontally
        //i = 0 - 4 start searching from lastOccupiedU
        startU = lastOccupiedU;
      } else {
        //stack vertically
        //i = 5 start searching from previous lastOccupiedU (not update startU)
        //i = 5 start searching from lastOccupiedV
        startV = lastOccupiedV;
      }
    }

    if (!pack_patch_flexible_and_temporal_stabilization(occupancy,
                                                        occupancyCC,
                                                        packedCC,
                                                        params,
                                                        keepFilesPathPrefix,
                                                        false,
                                                        startU,
                                                        startV,
                                                        lastOccupiedU,
                                                        lastOccupiedV,
                                                        frameMaxOccupiedU,
                                                        frameMaxOccupiedV)) {
      // do the packing again with reduce size
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
      std::cout << "Combined Connected Component using small scale ("
                << packedCCListPerCategory[idx][0].getScale()
                << "), better to repack all connected components with a new "
                   "adjusted scale (current adjustment: "
                << scalingAdjustment << ")" << std::endl;
#endif
      packedCCList.clear();
      return false;
    } else {
      // update all the positions with the new orientation (U0,V0)
      for (auto& cc : packedCCListPerCategory[idx]) {
        int newU0 = cc.getU0();
        int newV0 = cc.getV0();
        // remove the occupancy map corner
        for (auto& uv : cc.texCoords()) {
          uv -=
            Vec2<MeshType>(bbPerCategory[idx].min[0] * geometryVideoBlockSize,
                           bbPerCategory[idx].min[1] * geometryVideoBlockSize);
        }
        newU0 -= bbPerCategory[idx].min[0];
        newV0 -= bbPerCategory[idx].min[1];
        // now apply the latest packing results
        // rotation
        for (auto& uv : cc.texCoords()) {
          MeshType uu = uv[0];
          MeshType vv = uv[1];
          switch (packedCC.getOrientation()) {
          case 0:  // no rotation
            break;
          case 1:  // 90 degree
            uv[1] = uu;
            uv[0] = (heightOccCC * geometryVideoBlockSize - vv);
            break;
          case 2:  // 180 degrees
            uv[1] = (heightOccCC * geometryVideoBlockSize - vv);
            uv[0] = (widthOccCC * geometryVideoBlockSize - uu);
            break;
          case 3:  // 270 degrees
            uv[1] = (widthOccCC * geometryVideoBlockSize - uu);
            uv[0] = vv;
            break;
          default:  // no rotation
            break;
          }
        }
        MeshType uu0 = newU0;
        MeshType vv0 = newV0;
        switch (packedCC.getOrientation()) {
        case 0:  // no rotation
          break;
        case 1:  // 90 degree
          newV0 = uu0;
          newU0 = (heightOccCC - (cc.getSizeV() + vv0));
          break;
        case 2:  // 180 degrees
          newV0 = (heightOccCC - (cc.getSizeV() + vv0));
          newU0 = (widthOccCC - (cc.getSizeU() + uu0));
          break;
        case 3:  // 270 degrees
          newV0 = (widthOccCC - (cc.getSizeU() + uu0));
          newU0 = vv0;
          break;
        default:  // no rotation
          break;
        }
        cc.setOrientation(packedCC.getOrientation());
        // translation
        for (auto& uv : cc.texCoords()) {
          uv += Vec2<MeshType>(packedCC.getU0() * geometryVideoBlockSize,
                               packedCC.getV0() * geometryVideoBlockSize);
        }
        newU0 += packedCC.getU0();
        cc.setU0(newU0);
        newV0 += packedCC.getV0();
        cc.setV0(newV0);
        cc.setIdxPatch(packedCCList.size());
        packedCCList.push_back(cc);
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
        std::cout << "added CC[" << cc.getIdxPatch()
                  << "] = (#tri: " << cc.triangles().size()
                  << ", #vert: " << cc.points().size()
                  << ", P: " << cc.getProjection() << ", S: " << cc.getScale()
                  << ", O: " << cc.getOrientation() << ", U0: " << cc.getU0()
                  << ", V0: " << cc.getV0() << ", SizeU: " << cc.getSizeU()
                  << ", SizeV: " << cc.getSizeV() << ")" << std::endl;
#endif
      }
    }
  }

  // add patches to mesh
  mesh.clear();
  if (params.iDeriveTextCoordFromPos == 3) {  // separate CCs
    separateTexCCs(
      projectedConnectedComponentsCategories, bias, packedCCList, params);
  }
  int ccIdx = 0;
  for (auto& cc : packedCCList) {
    //now turn the coordinates into the [0,1] range
    for (auto& uv : cc.texCoords()) {
      uv[0] /= (occupancySizeU * geometryVideoBlockSize);
      uv[1] /= (occupancySizeV * geometryVideoBlockSize);
    }
    mesh.append(cc);
    if (params.useRawUV) {
      int rawUvId = params.use45DegreeProjection ? 18 : 6;
      if (cc.getProjection() == rawUvId) {
        std::cout << "set ucoords and vcooeds for CCid" << ccIdx << std::endl;
        cc.setNumRawUVMinus1(cc.texCoordTriangleCount() * 3 - 1);
        int codedUVindex = 0;
        for (auto& uvTri : cc.texCoordTriangles()) {
          for (int i = 0; i < 3; i++) {
            auto uvidx = cc.texCoord(uvTri[i]);
            cc.setUcoord(codedUVindex, cc.texCoord(uvTri[i])[0]);
            cc.setVcoord(codedUVindex, cc.texCoord(uvTri[i])[1]);
            codedUVindex++;
          }
        }
      }
    }
#if DEBUG_ORTHO_PATCH_PACKING_VERBOSE
    auto prefix = keepFilesPathPrefix + "_final_packed_connected_component_#"
                  + std::to_string(ccIdx);
    cc.save(prefix + ".obj");
    std::cout << "FINAL CC[" << ccIdx << "] -> perimeter(" << cc.perimeter()
              << "), stretchL2(" << cc.stretchL2(cc.getProjection()) << ")"
              << std::endl;
#endif
    ccIdx++;
  }
  ////now turn the coordinates into the [0,1] range
  //for (auto& uv : mesh.texCoords()) {
  //  uv[0] /= (occupancySizeU * geometryVideoBlockSize);
  //  uv[1] /= (occupancySizeV * geometryVideoBlockSize);
  //}
#if DEBUG_ORTHO_PATCH_PACKING
  if (params.keepIntermediateFiles) {
    mesh.save(keepFilesPathPrefix + "_hierarchical_packing_orthoAtlas.obj");
  }
#endif
  return true;
}

//============================================================================

template bool
TextureParametrization::generate<float>(const TriangleMesh<float>& decimate,
                                        TriangleMesh<float>& decimateTexture,
                                        size_t               texParamWidth,
                                        size_t               texParamHeight,
                                        const VMCEncoderParameters& params);

template bool
TextureParametrization::generate<double>(const TriangleMesh<double>& decimate,
                                         TriangleMesh<double>& decimateTexture,
                                         size_t                texParamWidth,
                                         size_t                texParamHeight,
                                         const VMCEncoderParameters& params);

//============================================================================

}  // namespace vmesh
