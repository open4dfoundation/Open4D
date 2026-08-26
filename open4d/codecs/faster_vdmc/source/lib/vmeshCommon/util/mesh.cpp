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

#include "util/mesh.hpp"
#include "checksum.hpp"
#include <tinyply.h>

namespace vmesh {

//============================================================================
Vec3<double> PROJDIRECTION6[6] = {
  Vec3<double>(1.0, 0.0, 0.0),   // 0 (x)*
  Vec3<double>(0.0, 1.0, 0.0),   // 1 (y)
  Vec3<double>(0.0, 0.0, 1.0),   // 2 (z)
  Vec3<double>(-1.0, 0.0, 0.0),  // 3 (-x)
  Vec3<double>(0.0, -1.0, 0.0),  // 4 (-y)*
  Vec3<double>(0.0, 0.0, -1.0),  // 5 (-z)*
  //* indicates that the winding of the triangle must be inverted
};

Vec3<double> PROJDIRECTION18[18] = {
  //no rotation,
  Vec3<double>(1.0, 0.0, 0.0),   // 0  (x)*
  Vec3<double>(0.0, 1.0, 0.0),   // 1  (y)
  Vec3<double>(0.0, 0.0, 1.0),   // 2  (z)
  Vec3<double>(-1.0, 0.0, 0.0),  // 3 (-x)
  Vec3<double>(0.0, -1.0, 0.0),  // 4 (-y)*
  Vec3<double>(0.0, 0.0, -1.0),  // 5 (-z)*
  //45-degree rotation around Y axis
  Vec3<double>(sqrt(2) / 2, 0.0, sqrt(2) / 2),    // 6  (x') -> 0  (x)*
  Vec3<double>(-sqrt(2) / 2, 0.0, sqrt(2) / 2),   // 7  (z') -> 2  (z)
  Vec3<double>(-sqrt(2) / 2, 0.0, -sqrt(2) / 2),  // 8 (-x') -> 3 (-x)
  Vec3<double>(sqrt(2) / 2, 0.0, -sqrt(2) / 2),   // 9 (-z') -> 5 (-z)*
  //45-degree rotation around X axis
  Vec3<double>(0.0, sqrt(2) / 2, sqrt(2) / 2),    // 10  (y') -> 1  (y)
  Vec3<double>(0.0, sqrt(2) / 2, -sqrt(2) / 2),   // 11 (-z') -> 5 (-z)*
  Vec3<double>(0.0, -sqrt(2) / 2, -sqrt(2) / 2),  // 12 (-y') -> 4 (-y)*
  Vec3<double>(0.0, -sqrt(2) / 2, sqrt(2) / 2),   // 13  (z') -> 2  (z)
  //45-degree rotation around Z axis
  Vec3<double>(sqrt(2) / 2, sqrt(2) / 2, 0.0),    // 14  (x') -> 0  (x)*
  Vec3<double>(sqrt(2) / 2, -sqrt(2) / 2, 0.0),   // 15 (-y') -> 4 (-y)*
  Vec3<double>(-sqrt(2) / 2, -sqrt(2) / 2, 0.0),  // 16 (-x') -> 3 (-x)
  Vec3<double>(-sqrt(2) / 2, sqrt(2) / 2, 0.0),   // 17  (y') -> 1  (y)
  //* indicates that the winding of the triangle must be inverted
};

//============================================================================

template<typename T, typename D>
void
templateConvert(std::shared_ptr<tinyply::PlyData> src, std::vector<T>& dst) {
  const size_t   numBytes = src->buffer.size_bytes();
  std::vector<D> data;
  dst.resize(src->count);
  const size_t length = dst[0].size();
  data.resize(src->count * length);
  std::memcpy(data.data(), src->buffer.get(), numBytes);
  for (size_t i = 0, idx = 0; i < src->count; i++)
    for (size_t j = 0; j < length; j++, idx++) dst[i][j] = data[idx];
}

template<typename T>
void
set(std::shared_ptr<tinyply::PlyData> src,
    std::vector<T>&                   dst,
    std::string                       name) {
  if (src) {
    switch (src->t) {
    case tinyply::Type::INT8: templateConvert<T, int8_t>(src, dst); break;
    case tinyply::Type::UINT8: templateConvert<T, uint8_t>(src, dst); break;
    case tinyply::Type::INT16: templateConvert<T, int16_t>(src, dst); break;
    case tinyply::Type::UINT16: templateConvert<T, uint16_t>(src, dst); break;
    case tinyply::Type::INT32: templateConvert<T, int32_t>(src, dst); break;
    case tinyply::Type::UINT32: templateConvert<T, uint32_t>(src, dst); break;
    case tinyply::Type::FLOAT32: templateConvert<T, float>(src, dst); break;
    case tinyply::Type::FLOAT64: templateConvert<T, double>(src, dst); break;
    default:
      printf("ERROR: PLY type not supported: %s \n", name.c_str());
      fflush(stdout);
      exit(-1);
      break;
    }
  }
}
//============================================================================
template<typename T>
T
TriangleMesh<T>::perimeter() const {
  T                                   perimeter = T(0);
  StaticAdjacencyInformation<int32_t> vertexToTriangle;
  ComputeVertexToTriangle(_coordIndex, _coord.size(), vertexToTriangle);
  std::vector<int8_t> vtags(_coord.size());
  std::vector<int8_t> ttags(_coordIndex.size());
  std::vector<int8_t> isBoundaryVertex;
  const auto          vertexCount = int32_t(vertexToTriangle.size());
  if (vertexCount == 0) { return T(0); }
  isBoundaryVertex.resize(vertexCount);
  std::fill(isBoundaryVertex.begin(), isBoundaryVertex.end(), int8_t(0));
  std::vector<int32_t> vadj;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    ComputeAdjacentVertices(
      vindex0, _coordIndex, vertexToTriangle, vtags, vadj);
    for (int vindex1 : vadj) {
      if (vindex1 <= vindex0) { continue; }
      if (ComputeEdgeAdjacentTriangleCount(
            vindex0, vindex1, vertexToTriangle, ttags)
          == 1) {
        isBoundaryVertex[vindex0] = int8_t(1);
        isBoundaryVertex[vindex1] = int8_t(1);
        perimeter += (_coord[vindex0] - _coord[vindex1]).norm();
      }
    }
  }
  return perimeter;
}

//============================================================================

template<typename T>
bool
TriangleMesh<T>::load(const std::string& fileName, const int32_t frameIndex) {
  return load(expandNum(fileName, frameIndex));
}

//============================================================================

template<typename T>
bool
TriangleMesh<T>::load(const std::string& fileName) {
  auto ext = extension(fileName);
  if (ext == "obj") return loadFromOBJ(fileName);
  if (ext == "ply") return loadFromPLY(fileName);
  if (ext == "vmb") return loadFromVMB(fileName);
  printf("Can't read extension type: %s \n", fileName.c_str());
  return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::save(const std::string& fileName, const bool binary) const {
#if PREPROCESSING_VERBOSE
  printf("Save %s \n", fileName.c_str());
#endif
  auto ext = extension(fileName);
  if (ext == "obj") return saveToOBJ(fileName);
  if (ext == "ply") return saveToPLY(fileName, binary);
  if (ext == "vmb") return saveToVMB(fileName);
  printf("Can't read extension type: %s \n", fileName.c_str());
  return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::loadMaterials(const std::string&        fileName,
                               std::vector<std::string>& materialNames,
                               std::vector<std::string>& textureMapUrls) {
  std::ifstream fin_materials(fileName);
  if (fin_materials.is_open()) {
    std::string              line;
    std::vector<std::string> tokens;
    Material<T>              currentMaterial;
    int                      matIdx = -1;
    while (getline(fin_materials, line)) {
      size_t prev = 0;
      size_t pos  = 0;
      tokens.resize(0);
      while ((pos = line.find_first_of(" ,;:/", prev)) != std::string::npos) {
        if (pos > prev) { tokens.push_back(line.substr(prev, pos - prev)); }
        prev = pos + 1;
      }
      if (prev < line.length()) {
        tokens.push_back(line.substr(prev, std::string::npos));
      }
      if (tokens.size() == 0) continue;
      if (tokens[0] == "newmtl") {
        if (tokens.size() < 2) {
          printf("newmtl line is missing the name: %s \n", line.c_str());
        } else if (tokens.size() == 2)
          currentMaterial.name = tokens[1];  //play it safe
        else
          currentMaterial.name = line.substr(
            7);  //space in the name, get everything after "newmtl "
        matIdx++;
        materialNames.push_back(currentMaterial.name);
        textureMapUrls.push_back("");

      } else if (tokens[0] == "Ka") {
        if (tokens.size() >= 4) {
          currentMaterial.ambiant =
            Vec3<double>((float)atof(tokens[1].c_str()),
                         (float)atof(tokens[2].c_str()),
                         (float)atof(tokens[3].c_str()));
        }
      } else if (tokens[0] == "Kd") {
        if (tokens.size() >= 4) {
          currentMaterial.diffuse =
            Vec3<double>((float)atof(tokens[1].c_str()),
                         (float)atof(tokens[2].c_str()),
                         (float)atof(tokens[3].c_str()));
        }
      } else if (tokens[0] == "Ks") {
        if (tokens.size() >= 4) {
          currentMaterial.specular =
            Vec3<double>((float)atof(tokens[1].c_str()),
                         (float)atof(tokens[2].c_str()),
                         (float)atof(tokens[3].c_str()));
        }
      } else if ((tokens[0] == "d") || (tokens[0] == "Tr")) {
        if (tokens.size() < 2) {
          currentMaterial.transparency = (float)atof(tokens[1].c_str());
        }
      } else if (tokens[0] == "Ns") {
        if (tokens.size() < 2) {
          currentMaterial.specularExponent = (float)atof(tokens[1].c_str());
        }
      } else if (tokens[0] == "illum") {
        if (tokens.size() < 2) {
          currentMaterial.illumination = (float)atof(tokens[1].c_str());
        }
      } else if (tokens[0] == "map_Kd") {
        std::string textureName;
        if (tokens.size() == 2) {
          //the tex name is the last one (after any option)
          textureName = tokens[tokens.size() - 1];
        }
        currentMaterial.texture = textureName;
        textureMapUrls[matIdx]  = textureName;
      }
    }
    fin_materials.close();
    return true;
  } else {
    printf("Could not open material file: %s \n", fileName.c_str());
  }

  return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::loadFromOBJ(const std::string& fileName) {
  std::ifstream fin(fileName);
  if (fin.is_open()) {
    std::string              line;
    std::vector<std::string> tokens;
    int                      currentMatIdx = 0;
    clear();
    while (getline(fin, line)) {
      size_t prev = 0;
      size_t pos  = 0;
      tokens.resize(0);
      while ((pos = line.find_first_of(" ,;:/", prev)) != std::string::npos) {
        if (pos > prev) { tokens.push_back(line.substr(prev, pos - prev)); }
        prev = pos + 1;
      }
      if (line.empty()) continue;
      if (prev < line.length()) {
        tokens.push_back(line.substr(prev, std::string::npos));
      }
      if (tokens[0] == "v" && tokens.size() >= 4) {
        const Vec3<T> pt(stod(tokens[1]), stod(tokens[2]), stod(tokens[3]));
        _coord.push_back(pt);
      }
      if (tokens[0] == "vt" && tokens.size() >= 3) {
        const Vec2<T> uv(stod(tokens[1]), stod(tokens[2]));
        _texCoord.push_back(uv);
      }
      if (tokens[0] == "vn" && tokens.size() >= 4) {
        const Vec3<T> normal(
          stod(tokens[1]), stod(tokens[2]), stod(tokens[3]));
        _normal.push_back(normal);
      } else if (tokens[0] == "f") {
        if (tokens.size() >= 10) {
          _coordIndex.emplace_back(
            stoi(tokens[1]) - 1, stoi(tokens[4]) - 1, stoi(tokens[7]) - 1);
          _texCoordIndex.emplace_back(
            stoi(tokens[2]) - 1, stoi(tokens[5]) - 1, stoi(tokens[8]) - 1);
          _normalIndex.emplace_back(
            stoi(tokens[3]) - 1, stoi(tokens[6]) - 1, stoi(tokens[9]) - 1);
        } else if (tokens.size() == 7) {
          _coordIndex.emplace_back(
            stoi(tokens[1]) - 1, stoi(tokens[3]) - 1, stoi(tokens[5]) - 1);
          _texCoordIndex.emplace_back(
            stoi(tokens[2]) - 1, stoi(tokens[4]) - 1, stoi(tokens[6]) - 1);
        } else if (tokens.size() == 4) {
          _coordIndex.emplace_back(
            stoi(tokens[1]) - 1, stoi(tokens[2]) - 1, stoi(tokens[3]) - 1);
        }
        _materialIdx.push_back(currentMatIdx);
      } else if (tokens[0] == "mtllib" && tokens.size() >= 2) {
        if (tokens.size() > 2)
          _mtllib = line.substr(7);  //get everything after "mtllib "
        else _mtllib = tokens[1];
        // getting the material names and texture URLs
        loadMaterials(
          dirname(fileName) + _mtllib, _materialNames, _textureMapUrls);
      } else if (tokens[0] == "usemtl" && tokens.size() >= 2) {
        std::string materialName = tokens[1];
        // update the material index for the triangle
        for (int i = 0; i < _materialNames.size(); i++) {
          if (_materialNames[i] == materialName) currentMatIdx = i;
        }
      }
    }
    fin.close();
    // print("Load: " + fileName);
    return true;
  }
  printf("Error loading file: %s \n", fileName.c_str());

  return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::saveToOBJ(const std::string& fileName) const {
  std::ofstream fout(fileName);
  if (fout.is_open()) {
    const int32_t ptCount    = pointCount();
    const int32_t tcCount    = texCoordCount();
    const int32_t nCount     = normalCount();
    const int32_t triCount   = triangleCount();
    const int32_t tcTriCount = texCoordTriangleCount();
    const int32_t nTriCount  = normalTriangleCount();

    assert(nTriCount == 0 || nTriCount == triCount);
    assert(tcTriCount == 0 || tcTriCount == triCount);
#if PREPROCESSING_VERBOSE
    //print("Save: " + fileName);
#endif
    if (!_mtllib.empty()) { fout << "mtllib " << _mtllib << '\n'; }
    const auto hasColours =
      !_colour.empty() && _colour.size() == _coord.size();
    const auto hasDisplacements =
      !_disp.empty() && _disp.size() == _coord.size();
    for (int32_t pointIndex = 0; pointIndex < ptCount; ++pointIndex) {
      const auto& pt = point(pointIndex);
      fout << "v " << T(pt.x()) << ' ' << T(pt.y()) << ' ' << T(pt.z());
      if (hasColours) {
        const auto& c = colour(pointIndex);
        fout << ' ' << T(c.x()) << ' ' << T(c.y()) << ' ' << T(c.z());
      }
      if (hasDisplacements) {
        const auto& d = displacement(pointIndex);
        fout << ' ' << T(d.x()) << ' ' << T(d.y()) << ' ' << T(d.z());
      }
      fout << '\n';
    }

    for (int32_t uvIndex = 0; uvIndex < tcCount; ++uvIndex) {
      const auto& uv = texCoord(uvIndex);
      fout << "vt " << T(uv.x()) << ' ' << T(uv.y()) << '\n';
    }

    for (int32_t nIndex = 0; nIndex < nCount; ++nIndex) {
      const auto& n = normal(nIndex);
      fout << "vn " << T(n.x()) << ' ' << T(n.y()) << ' ' << T(n.z()) << '\n';
    }

    if (tcTriCount == 0 && nTriCount == 0) {
      for (int32_t triangleIndex = 0; triangleIndex < triCount;
           ++triangleIndex) {
        if (triangleIndex < materialIdxCount()) {
          auto matIdx = _materialIdx[triangleIndex];
          if (matIdx < _materialNames.size())
            fout << "usemtl " << _materialNames[matIdx] << '\n';
        }
        const auto& tri = triangle(triangleIndex);
        const auto  i   = (tri.x() + 1);
        const auto  j   = (tri.y() + 1);
        const auto  k   = (tri.z() + 1);

        //assert(i != j && i != k && j != k);
        fout << "f " << i << ' ' << j << ' ' << k << '\n';
      }
    } else if (nTriCount && tcTriCount == 0) {
      for (int32_t triangleIndex = 0; triangleIndex < triCount;
           ++triangleIndex) {
        if (triangleIndex < materialIdxCount()) {
          auto matIdx = _materialIdx[triangleIndex];
          if (matIdx < _materialNames.size())
            fout << "usemtl " << _materialNames[matIdx] << '\n';
        }
        const auto& tri = triangle(triangleIndex);
        const auto  i0  = (tri.x() + 1);
        const auto  j0  = (tri.y() + 1);
        const auto  k0  = (tri.z() + 1);

        const auto& ntri = normalTriangle(triangleIndex);
        const auto  i1   = (ntri.x() + 1);
        const auto  j1   = (ntri.y() + 1);
        const auto  k1   = (ntri.z() + 1);

        //assert(i0 != j0 && i0 != k0 && j0 != k0);
        assert(i1 != j1 && i1 != k1 && j1 != k1);
        fout << "f " << i0 << "//" << i1 << ' ' << j0 << "//" << j1 << ' '
             << k0 << "//" << k1 << '\n';
      }
    } else if (tcTriCount && nTriCount == 0) {
      for (int32_t triangleIndex = 0; triangleIndex < triCount;
           ++triangleIndex) {
        if (triangleIndex < materialIdxCount()) {
          auto matIdx = _materialIdx[triangleIndex];
          if (matIdx < _materialNames.size())
            fout << "usemtl " << _materialNames[matIdx] << '\n';
        }
        const auto& tri = triangle(triangleIndex);
        const auto  i0  = (tri.x() + 1);
        const auto  j0  = (tri.y() + 1);
        const auto  k0  = (tri.z() + 1);

        const auto& uvtri = texCoordTriangle(triangleIndex);
        const auto  i1    = (uvtri.x() + 1);
        const auto  j1    = (uvtri.y() + 1);
        const auto  k1    = (uvtri.z() + 1);

        //assert(i0 != j0 && i0 != k0 && j0 != k0);
        assert(i1 != j1 && i1 != k1 && j1 != k1);
        fout << "f " << i0 << '/' << i1 << ' ' << j0 << '/' << j1 << ' ' << k0
             << '/' << k1 << '\n';
      }
    } else {
      for (int32_t triangleIndex = 0; triangleIndex < triCount;
           ++triangleIndex) {
        if (triangleIndex < materialIdxCount()) {
          auto matIdx = _materialIdx[triangleIndex];
          if (matIdx < _materialNames.size())
            fout << "usemtl " << _materialNames[matIdx] << '\n';
        }
        const auto& tri = triangle(triangleIndex);
        const auto  i0  = (tri.x() + 1);
        const auto  j0  = (tri.y() + 1);
        const auto  k0  = (tri.z() + 1);

        const auto& uvtri = texCoordTriangle(triangleIndex);
        const auto  i1    = (uvtri.x() + 1);
        const auto  j1    = (uvtri.y() + 1);
        const auto  k1    = (uvtri.z() + 1);

        const auto& ntri = normalTriangle(triangleIndex);
        const auto  i2   = (ntri.x() + 1);
        const auto  j2   = (ntri.y() + 1);
        const auto  k2   = (ntri.z() + 1);

        //assert(i0 != j0 && i0 != k0 && j0 != k0);
        assert(i1 != j1 && i1 != k1 && j1 != k1);
        assert(i2 != j2 && i2 != k2 && j2 != k2);
        fout << "f " << i0 << '/' << i1 << '/' << i2 << ' ' << j0 << '/' << j1
             << '/' << j2 << ' ' << k0 << '/' << k1 << '/' << k2 << '\n';
      }
    }
    fout.close();
    return true;
  }
  return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::saveToOBJUsingFidAsColor(const std::string& fileName) {
  texCoords().clear();
  reserveTexCoords(triangleCount() * 3);
  texCoordTriangles().clear();
  reserveTexCoordTriangles(triangleCount());
  int   side         = 100;
  float numIndPerRow = 50.0;
  auto  maxNumFaceId =
    faceIds().size()
       ? *std::max_element(faceIds().begin(), faceIds().end()) + 1
       : 0;
  float totalRows = std::ceil((float)maxNumFaceId / numIndPerRow);
  for (int idx = 0; idx < faceIds().size(); idx++) {
    float x = ((faceId(idx) % (int)numIndPerRow) + 0.5) / (numIndPerRow);
    float y = 1 - ((faceId(idx) / numIndPerRow) + 0.5) / (totalRows);
    texCoords().push_back(Vec2<float>(x, y));
    texCoords().push_back(Vec2<float>(x, y));
    texCoords().push_back(Vec2<float>(x, y));
    texCoordTriangles().push_back(
      vmesh::Triangle(3 * idx, 3 * idx + 1, 3 * idx + 2));
  }
  vmesh::Frame<uint8_t> outputTexture;
  outputTexture.resize(
    numIndPerRow * side, totalRows * side, vmesh::ColourSpace::BGR444p);
  auto&                R = outputTexture.plane(0);
  auto&                G = outputTexture.plane(1);
  auto&                B = outputTexture.plane(2);
  std::vector<uint8_t> RedChannel;
  std::vector<uint8_t> GreenChannel;
  std::vector<uint8_t> BlueChannel;
  for (int i = 0; i < totalRows * numIndPerRow; i++) {
    RedChannel.push_back(rand() % 255);
    GreenChannel.push_back(rand() % 255);
    BlueChannel.push_back(rand() % 255);
  }
  for (int h = 0; h < totalRows; h++) {
    for (int w = 0; w < numIndPerRow; w++) {
      for (int i = 0; i < side; i++) {
        for (int j = 0; j < side; j++) {
          R.set(i + h * side, j + w * side, RedChannel[h * numIndPerRow + w]);
          G.set(
            i + h * side, j + w * side, GreenChannel[h * numIndPerRow + w]);
          B.set(i + h * side, j + w * side, BlueChannel[h * numIndPerRow + w]);
        }
      }
    }
  }
  outputTexture.save(fileName + ".png");
  vmesh::Material<double> material;
  material.texture = vmesh::basename(fileName + ".png");
  material.save(fileName + ".mtl");
  setMaterialLibrary(vmesh::basename(fileName + ".mtl"));
  save(fileName + ".obj");
  //now remove the texture coordinates
  texCoords().clear();
  texCoordTriangles().clear();
  setMaterialLibrary("");
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::saveToPLY(const std::string& fileName,
                           const bool         binary) const {
  std::filebuf fb;
  fb.open(fileName, binary ? std::ios::out | std::ios::binary : std::ios::out);
  std::ostream outstream(&fb);
  if (outstream.fail()) {
    throw std::runtime_error("failed to open " + fileName);
    return false;
  }
  tinyply::Type type;
  if (std::is_same<T, double>::value) type = tinyply::Type::FLOAT64;
  else if (std::is_same<T, float>::value) type = tinyply::Type::FLOAT32;
  else if (std::is_same<T, unsigned char>::value) type = tinyply::Type::UINT8;
  else {
    throw std::runtime_error("saveToPLY: type not supported");
    exit(-1);
  }

#define CAST_UINT8(ptr) \
  reinterpret_cast<const uint8_t*>(reinterpret_cast<const void*>(ptr.data()))
  tinyply::PlyFile ply;
  auto&            str = ply.get_comments();
  str.push_back("generated by mpeg-vmesh-tm + tinyply");
  str.push_back("TextureFile " + _mtllib);
  str.push_back("###");
  str.push_back("# Coord:        " + std::to_string(pointCount()));
  str.push_back("# Colour:       " + std::to_string(colourCount()));
  str.push_back("# Normals:      " + std::to_string(normalCount()));
  str.push_back("# TexCoord:     " + std::to_string(texCoordCount()));
  str.push_back("# Triangles:    " + std::to_string(triangleCount()));
  str.push_back("# TexTriangles: " + std::to_string(texCoordTriangleCount()));
  str.push_back("###");

  // Vertices
  ply.add_properties_to_element("vertex",
                                {"x", "y", "z"},
                                type,
                                _coord.size(),
                                CAST_UINT8(_coord),
                                tinyply::Type::INVALID,
                                0);

  if ((!_colour.empty()) && _colour.size() == _coord.size())
    ply.add_properties_to_element("vertex",
                                  {"red", "green", "blue"},
                                  type,
                                  _colour.size(),
                                  CAST_UINT8(_colour),
                                  tinyply::Type::INVALID,
                                  0);

  if ((!_normal.empty()) && _normal.size() == _coord.size())
    ply.add_properties_to_element("vertex",
                                  {"nx", "ny", "nz"},
                                  type,
                                  _normal.size(),
                                  CAST_UINT8(_normal),
                                  tinyply::Type::INVALID,
                                  0);

  // Faces
  ply.add_properties_to_element("face",
                                {"vertex_indices"},
                                tinyply::Type::INT32,
                                _coordIndex.size(),
                                CAST_UINT8(_coordIndex),
                                tinyply::Type::UINT8,
                                3);

  // Face texture coordinate
  std::vector<Vec3<Vec2<T>>> uvCoords;
  if (!_texCoordIndex.empty() && !_texCoord.empty()) {
    const size_t triCount = triangleCount();
    uvCoords.resize(triCount);
    for (size_t i = 0; i < triCount; i++)
      for (size_t j = 0; j < 3; j++)
        uvCoords[i][j] = _texCoord[_texCoordIndex[i][j]];
    ply.add_properties_to_element("face",
                                  {"texcoord"},
                                  type,
                                  triCount,
                                  CAST_UINT8(uvCoords),
                                  tinyply::Type::UINT8,
                                  6);
  }
  ply.write(outstream, binary);
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::loadFromPLY(const std::string& fileName) {
  std::unique_ptr<std::istream> file;
  file.reset(new std::ifstream(fileName, std::ios::binary));
  if (!file || file->fail()) {
    printf("failed to open: %s \n", fileName.c_str());
    return false;
  }
  tinyply::PlyFile ply;
  ply.parse_header(*file);

  std::shared_ptr<tinyply::PlyData> coords, normals, colours, texcoords,
    triangles, texTriangles;
  try {
    coords = ply.request_properties_from_element("vertex", {"x", "y", "z"});
  } catch (const std::exception&) {}
  try {
    normals =
      ply.request_properties_from_element("vertex", {"nx", "ny", "nz"});
  } catch (const std::exception&) {}
  try {
    colours =
      ply.request_properties_from_element("vertex", {"red", "green", "blue"});
  } catch (const std::exception&) {}
  try {
    colours = ply.request_properties_from_element("vertex", {"r", "g", "b"});
  } catch (const std::exception&) {}
  try {
    texcoords = ply.request_properties_from_element(
      "vertex", {"texture_u", "texture_v"});
  } catch (const std::exception&) {}
  try {
    triangles =
      ply.request_properties_from_element("face", {"vertex_indices"}, 3);
  } catch (const std::exception&) {}
  try {
    texTriangles =
      ply.request_properties_from_element("face", {"texcoord"}, 6);
  } catch (const std::exception&) {}

  ply.read(*file);
  set(coords, _coord, "vertices");
  set(normals, _normal, "normals");
  set(colours, _colour, "colors");
  set(texcoords, _texCoord, "uvcoords");
  set(triangles, _coordIndex, "triangles");
  if (texTriangles) {
    const auto                 nbBytes  = texTriangles->buffer.size_bytes();
    const auto                 triCount = texTriangles->count;
    std::vector<Vec3<Vec2<T>>> uvCoords;
    uvCoords.resize(triCount);
    switch (texTriangles->t) {
    case tinyply::Type::FLOAT32: {
      std::vector<float> data;
      data.resize(texTriangles->count * 6);
      std::memcpy(data.data(), texTriangles->buffer.get(), nbBytes);
      for (size_t i = 0, idx = 0; i < triCount; i++)
        for (size_t j = 0; j < 3; j++)
          for (size_t k = 0; k < 2; k++, idx++) uvCoords[i][j][k] = data[idx];
      break;
    }
    case tinyply::Type::FLOAT64: {
      std::vector<double> data;
      data.resize(texTriangles->count * 6);
      std::memcpy(data.data(), texTriangles->buffer.get(), nbBytes);
      for (size_t i = 0, idx = 0; i < triCount; i++)
        for (size_t j = 0; j < 3; j++)
          for (size_t k = 0; k < 2; k++, idx++) uvCoords[i][j][k] = data[idx];
      break;
    }
    default:
      printf("ERROR: PLY only supports texcoord type: double or float \n");
      fflush(stdout);
      exit(-1);
      break;
    }
    _texCoordIndex.resize(triCount);
    _texCoord.clear();
    for (size_t i = 0; i < triCount; i++) {
      for (size_t j = 0; j < 3; j++) {
        size_t index = 0;
        auto   it =
          std::find(_texCoord.begin(), _texCoord.end(), uvCoords[i][j]);
        if (it == _texCoord.end()) {
          index = _texCoord.size();
          _texCoord.push_back(uvCoords[i][j]);
        } else {
          index = std::distance(_texCoord.begin(), it);
        }
        _texCoordIndex[i][j] = index;
      }
    }
  }
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::saveToVMB(const std::string& fileName) const {
  // Compute checksum
  Checksum checksum;
  auto     strChecksum = checksum.getChecksum(*this);
  // print("Save: " + fileName);
  // std::cout << "# Checksum:     " << strChecksum << "\n";

  // Get data type
  std::string type = "float";
  if (std::is_same<T, double>::value) type = "double";
  else if (std::is_same<T, float>::value) type = "float";
  else if (std::is_same<T, unsigned char>::value) type = "uint8";
  else {
    throw std::runtime_error("saveToVMB: type not supported");
    exit(-1);
  }

  // Write header
  std::ofstream fout(fileName, std::ofstream::binary | std::ofstream::out);
  if (!fout.is_open()) { return false; }
  fout << "#VMB" << std::endl;
  fout << "Type:     " << type << "\n";
  fout << "Coord:    " << pointCount() << "\n";
  fout << "Colour:   " << colourCount() << "\n";
  fout << "Normals:  " << normalCount() << "\n";
  fout << "TexCoord: " << texCoordCount() << "\n";
  fout << "FaceId:   " << faceIdCount() << "\n";
  fout << "Disp:     " << displacementCount() << "\n";
  fout << "Faces:    " << triangleCount() << "\n";
  fout << "TexFaces: " << texCoordTriangleCount() << "\n";
  fout << "NrmFaces: " << normalTriangleCount() << "\n";
  fout << "Mtllib:   " << _mtllib << "\n";
  fout << "Checksum: " << strChecksum << "\n";
  fout << "end_header \n";

  // Write Data

  // Vertex coordinates
  if (!_coord.empty())
    fout.write(reinterpret_cast<const char*>(_coord.data()),
               _coord.size() * sizeof(T) * 3);
  if (!_colour.empty())
    fout.write(reinterpret_cast<const char*>(_colour.data()),
               _colour.size() * sizeof(T) * 3);
  if (!_normal.empty())
    fout.write(reinterpret_cast<const char*>(_normal.data()),
               _normal.size() * sizeof(T) * 3);
  if (!_texCoord.empty())
    fout.write(reinterpret_cast<const char*>(_texCoord.data()),
               _texCoord.size() * sizeof(T) * 2);
  if (!_faceId.empty())
    fout.write(reinterpret_cast<const char*>(_faceId.data()),
               _faceId.size() * sizeof(decltype(_faceId.at(0))));
  if (!_disp.empty())
    fout.write(reinterpret_cast<const char*>(_disp.data()),
               _disp.size() * sizeof(T) * 3);

  // Face indices
  if (!_coordIndex.empty())
    fout.write(reinterpret_cast<const char*>(_coordIndex.data()),
               _coordIndex.size() * sizeof(int) * 3);
  if (!_normalIndex.empty())
    fout.write(reinterpret_cast<const char*>(_normalIndex.data()),
               _normalIndex.size() * sizeof(int) * 3);
  if (!_texCoordIndex.empty())
    fout.write(reinterpret_cast<const char*>(_texCoordIndex.data()),
               _texCoordIndex.size() * sizeof(int) * 3);

  fout.close();
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
bool
TriangleMesh<T>::loadFromVMB(const std::string& fileName) {
  std::ifstream ifs(fileName, std::ifstream::binary | std::ifstream::in);
  if (!ifs.is_open()) { return false; }
  std::string line;
  getline(ifs, line);
  if (line.rfind("#VMB", 0) != 0) {
    printf("ERROR: Read VMB %s can't read file format\n", fileName.c_str());
    exit(-1);
  }

  // Get data type
  std::string type = "float";
  if (std::is_same<T, double>::value) type = "double";
  else if (std::is_same<T, float>::value) type = "float";
  else if (std::is_same<T, unsigned char>::value) type = "uint8";
  else {
    throw std::runtime_error("loadFromVMB: type not supported");
    exit(-1);
  }

  // Read header
  std::string readChecksum;
  for (; getline(ifs, line);) {
    if (line.rfind("end_header", 0) == 0) break;
    else if (line.rfind("#", 0) == 0) continue;
    else {
      std::stringstream ss(line);
      std::string       name, value;
      ss >> name >> value;
      if (name == "Coord:") _coord.resize(std::stoi(value));
      else if (name == "Colour:") _colour.resize(std::stoi(value));
      else if (name == "Normals:") _normal.resize(std::stoi(value));
      else if (name == "TexCoord:") _texCoord.resize(std::stoi(value));
      else if (name == "FaceId:") _faceId.resize(std::stoi(value));
      else if (name == "Disp:") _disp.resize(std::stoi(value));
      else if (name == "Faces:") _coordIndex.resize(std::stoi(value));
      else if (name == "TexFaces:") _texCoordIndex.resize(std::stoi(value));
      else if (name == "NrmTFaces:") _normalIndex.resize(std::stoi(value));
      else if (name == "Checksum:") readChecksum = value;
      else if (name == "Type:") {
        if (type != value) {
          printf("ERROR: Read VMB %s data type not correct\n",
                 fileName.c_str());
          exit(-1);
        }
      }
    }
  }

  // Read data

  // Vertex coordinates
  if (!_coord.empty())
    ifs.read(reinterpret_cast<char*>(_coord.data()),
             _coord.size() * sizeof(T) * 3);
  if (!_colour.empty())
    ifs.read(reinterpret_cast<char*>(_colour.data()),
             _colour.size() * sizeof(T) * 3);
  if (!_normal.empty())
    ifs.read(reinterpret_cast<char*>(_normal.data()),
             _normal.size() * sizeof(T) * 3);
  if (!_texCoord.empty())
    ifs.read(reinterpret_cast<char*>(_texCoord.data()),
             _texCoord.size() * sizeof(T) * 2);
  if (!_faceId.empty())
    ifs.read(reinterpret_cast<char*>(_faceId.data()),
             _faceId.size() * sizeof(decltype(_faceId.at(0))));
  if (!_disp.empty())
    ifs.read(reinterpret_cast<char*>(_disp.data()),
             _disp.size() * sizeof(T) * 3);

  // Face indices
  if (!_coordIndex.empty())
    ifs.read(reinterpret_cast<char*>(_coordIndex.data()),
             _coordIndex.size() * sizeof(int) * 3);
  if (!_normalIndex.empty())
    ifs.read(reinterpret_cast<char*>(_normalIndex.data()),
             _normalIndex.size() * sizeof(int) * 3);
  if (!_texCoordIndex.empty())
    ifs.read(reinterpret_cast<char*>(_texCoordIndex.data()),
             _texCoordIndex.size() * sizeof(int) * 3);
  ifs.close();

  // print("Load: " + fileName);

  // Compare checksums
  if (!readChecksum.empty()) {
    Checksum checksum;
    auto     computeChecksum = checksum.getChecksum(*this);
    if (computeChecksum != readChecksum) {
      printf("Error: Read/compute checksum are not the same: %s %s \n",
             readChecksum.c_str(),
             computeChecksum.c_str());
      fflush(stdout);
      exit(-1);
    } else {
      printf("Checksums match: %s \n", readChecksum.c_str());
    }
  }
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
void
TriangleMesh<T>::invertOrientation() {
  const auto hasTriangles         = triangleCount() > 0;
  const auto hasTexCoordTriangles = texCoordTriangleCount() > 0;
  const auto hasNormalTriangles   = normalTriangleCount() > 0;
  for (int32_t tindex = 0, tcount = triangleCount(); tindex < tcount;
       ++tindex) {
    if (hasTriangles) {
      assert(tindex < triangleCount());
      const auto& tri = triangle(tindex);
      setTriangle(tindex, tri[1], tri[0], tri[2]);
    }

    if (hasTexCoordTriangles) {
      assert(tindex < texCoordTriangleCount());
      const auto& tri = texCoordTriangle(tindex);
      setTexCoordTriangle(tindex, tri[1], tri[0], tri[2]);
    }

    if (hasNormalTriangles) {
      assert(tindex < normalTriangleCount());
      const auto& tri = normalTriangle(tindex);
      setNormalTriangle(tindex, tri[1], tri[0], tri[2]);
    }
  }
}

//----------------------------------------------------------------------------

template<typename T>
void
TriangleMesh<T>::append(const TriangleMesh<T>& mesh) {
  const auto offsetPositions         = pointCount();
  const auto offsetTexCoord          = texCoordCount();
  const auto offsetNormal            = normalCount();
  const auto offsetColor             = colourCount();
  const auto offsetTriangles         = triangleCount();
  const auto offsetTexCoordTriangles = texCoordTriangleCount();
  const auto offsetNormalTriangles   = normalTriangleCount();
  const auto offsetFaceId            = faceIdCount();
  const auto offsetMaterialIdx       = materialIdxCount();
  const auto offsetTrackIdx          = tracktriCount();
  const auto offsetModifyIdx         = modifytriCount();

  const auto posCount         = mesh.pointCount();
  const auto tcCount          = mesh.texCoordCount();
  const auto nCount           = mesh.normalCount();
  const auto cCount           = mesh.colourCount();
  const auto triCount         = mesh.triangleCount();
  const auto texCoordTriCount = mesh.texCoordTriangleCount();
  const auto normalTriCount   = mesh.normalTriangleCount();
  const auto faceIdCount      = mesh.faceIdCount();
  const auto materialIdxCount = mesh.materialIdxCount();
  const auto setTrackIdxCount  = mesh.tracktriCount();
  const auto setModifyIdxCount = mesh.modifytriCount();

  resizePoints(offsetPositions + posCount);
  for (int v = 0; v < posCount; ++v) {
    setPoint(offsetPositions + v, mesh.point(v));
  }

  resizeTexCoords(offsetTexCoord + tcCount);
  for (int v = 0; v < tcCount; ++v) {
    setTexCoord(offsetTexCoord + v, mesh.texCoord(v));
  }

  resizeNormals(offsetNormal + nCount);
  for (int v = 0; v < nCount; ++v) {
    setNormal(offsetNormal + v, mesh.normal(v));
  }
  resizeColours(offsetColor + cCount);
  for (int v = 0; v < cCount; ++v) {
    setColour(offsetColor + v, mesh.colour(v));
  }
  resizeFaceIds(offsetFaceId + faceIdCount);
  int maxValFaceId = 0;
  if (faceIdCount > 0)
    maxValFaceId =
      *std::max_element(mesh.faceIds().begin(), mesh.faceIds().end());
  for (int v = 0; v < faceIdCount; ++v) {
    setFaceId(offsetFaceId + v, mesh.faceId(v) + maxValFaceId + 1);
  }

  resizeMaterialsIdxs(offsetMaterialIdx + materialIdxCount);
  for (int t = 0; t < materialIdxCount; ++t) {
    setMaterialIdx(offsetMaterialIdx + t, mesh.materialIdx(t));
  }

  resizeTriangles(offsetTriangles + triCount);
  for (int t = 0; t < triCount; ++t) {
    const auto tri = mesh.triangle(t) + offsetPositions;
    setTriangle(offsetTriangles + t, tri);
  }

  resizeTexCoordTriangles(offsetTexCoordTriangles + texCoordTriCount);
  for (int t = 0; t < texCoordTriCount; ++t) {
    const auto tri = mesh.texCoordTriangle(t) + offsetTexCoord;
    setTexCoordTriangle(offsetTexCoordTriangles + t, tri);
  }

  resizeNormalTriangles(offsetNormalTriangles + normalTriCount);
  for (int t = 0; t < normalTriCount; ++t) {
    const auto tri = mesh.normalTriangle(t) + offsetNormal;
    setNormalTriangle(offsetNormalTriangles + t, tri);
  }

  resizeTracktris(offsetTrackIdx + setTrackIdxCount);
  for (int t = 0; t < setTrackIdxCount; ++t) {
    setTracktri(offsetTrackIdx + t, mesh.tracktri(t));
  }

  resizeModifytris(offsetModifyIdx + setModifyIdxCount);
  for (int t = 0; t < setModifyIdxCount; ++t) {
    setModifytri(offsetModifyIdx + t, mesh.modifytri(t));
  }
}

//----------------------------------------------------------------------------

template<typename T>
void
TriangleMesh<T>::subdivideMesh(
  int32_t                            iterationCount,
  const int32_t                      edgeLengthThreshold,
  const int32_t                      bitshiftEdgeBasedSubdivision,
  std::vector<SubdivisionMethod>*    subdivisionMethod,
  std::vector<SubdivisionLevelInfo>* infoLevelOfDetails,
  std::vector<int64_t>*              coordEdges,
  std::vector<int64_t>*              texCoordEdges,
  std::vector<int32_t>*              triangleToBaseMeshTriangle,
  int*                               baseEdgesCount,
  int*                               baseTexEdgesCount,
  std::vector<Vec3<int32_t>>*        triangleBaseEdge,
  std::vector<Vec3<int32_t>>*        texTriangleBaseEdge,
  std::vector<int32_t>*              submeshFrameLodMap,
  std::vector<int32_t>*              submeshFrameChildToParentMap,
  std::vector<TriangleMesh<T>>*      refMesh) {
  bool usePrevTri = false;
  std::vector<SubdivisionMethod> defaultSubdivisionMethod;
  std::vector<Triangle>          prevTexCoordTriangles;
  std::vector<Triangle>          prevTriangles;
  if (subdivisionMethod == nullptr) {
    defaultSubdivisionMethod.assign(iterationCount,
                                    SubdivisionMethod::MID_POINT);
    subdivisionMethod = &defaultSubdivisionMethod;
  } else if (iterationCount > 0) {
    for (int32_t it = 0; it < iterationCount; ++it) {
      if (subdivisionMethod->size() > it) {
        if ((*subdivisionMethod)[it] >= SubdivisionMethod::LOOP
            && (*subdivisionMethod)[it] <= SubdivisionMethod::PYTHAG) {
          usePrevTri = true;
        } else if ((*subdivisionMethod)[it] < SubdivisionMethod::MID_POINT
                   || (*subdivisionMethod)[it]
                        >= SubdivisionMethod::METHOD_NUM) {
          (*subdivisionMethod)[it] = SubdivisionMethod::MID_POINT;
        }
      } else {
        subdivisionMethod->push_back(SubdivisionMethod::MID_POINT);
      }
    }
  }
  if (usePrevTri) {
    prevTexCoordTriangles = texCoordTriangles();
    prevTriangles = triangles();
  }
  if (triangleToBaseMeshTriangle != nullptr) {
    auto&      triToBaseMeshTri = *triangleToBaseMeshTriangle;
    const auto tCount0          = this->triangleCount();
    triToBaseMeshTri.resize(tCount0);
    for (int32_t t = 0; t < tCount0; ++t) { triToBaseMeshTri[t] = t; }
  }

  if (infoLevelOfDetails != nullptr) {
    infoLevelOfDetails->resize(iterationCount + 1);
    auto& infoLevelOfDetail                 = (*infoLevelOfDetails)[0];
    infoLevelOfDetail.pointCount            = pointCount();
    infoLevelOfDetail.triangleCount         = triangleCount();
    infoLevelOfDetail.texCoordCount         = texCoordCount();
    infoLevelOfDetail.texCoordTriangleCount = texCoordTriangleCount();
  }

  if (iterationCount <= 0) { return; }

  std::vector<std::vector<int32_t>>  positionRefs(iterationCount);
  std::vector<std::vector<Vec3<T>>>  refGeoms(iterationCount);
  std::vector<std::vector<Triangle>> refTris(iterationCount);
  std::vector<Vec3<T>> prev_points(points().begin(), points().end());
  auto& refGeom = prev_points;  // refGeom --> {PositionRef...} arrays in 11.2.3.2
  const auto tCount = triangleCount() * std::pow(4, iterationCount - 1);
  if (triangleCount() && pointCount()) {
    const auto vCount = pointCount() * std::pow(4, iterationCount - 1);
    reservePoints(4 * vCount);
    reserveTriangles(4 * tCount);
    StaticAdjacencyInformation<int32_t> vertexToTriangle;
    StaticAdjacencyInformation<int32_t> vertexToEdge;
    std::vector<int8_t>                 vtags;
    vertexToTriangle.reserve(vCount);
    vertexToEdge.reserve(vCount);
    vtags.reserve(vCount);
    if (coordEdges == nullptr) {
      std::vector<int64_t> edges(4 * vCount, int64_t(-1));
      int                  prevPointCount = pointCount();

      if (iterationCount > 0 && edgeLengthThreshold == 0)
        initializeSubdiv(prevPointCount,
                         triangles(),
                         true,
                         valences(),
                         vertexToTriangle,
                         vertexToEdge,
                         vtags,
                         edges);
      for (int32_t it = 0; it < iterationCount; ++it) {
        int32_t     edgeThr = edgeLengthThreshold;
        if (edgeLengthThreshold > 0) {
          refGeom = refMesh ? (*refMesh)[it].points() : refGeom;
          if (refMesh == nullptr) {
            refGeoms[it] = refGeom;
            refTris[it] = triangles();
          }
          if (it + 1 < iterationCount - 1) edgeThr = 0;
          if (it == 0) {
            auto thr = (iterationCount - 1 > 0) ? 0 : edgeThr;
            initializeSubdivWithEdgeLengthThreshold<T>(prevPointCount,
                                                       refGeom,
                                                       triangles(),
                                                       true,
                                                       valences(),
                                                       vertexToTriangle,
                                                       vertexToEdge,
                                                       vtags,
                                                       edges,
                                                       thr,
                                                       bitshiftEdgeBasedSubdivision,
                                                       nullptr);
          } else {
            //compute vertexToEdge
            ComputeVertexToTriangle(triangles(), prevPointCount, vertexToTriangle);

            vertexToEdge.resize(edges.size());

            for (int32_t e = prevPointCount; e < edges.size(); ++e) {
              auto vindex0 = edges[e] >> 32;
              auto vindex1 = edges[e] & 0xFFFFFFFF;
              vertexToEdge.incrementNeighbourCount(vindex0, 1);
              vertexToEdge.incrementNeighbourCount(vindex1, 1);
              vertexToEdge.incrementNeighbourCount(e, 2);
            }

            vertexToEdge.updateShift();

            for (int32_t e = prevPointCount; e < edges.size(); ++e) {
              auto vindex0 = edges[e] >> 32;
              auto vindex1 = edges[e] & 0xFFFFFFFF;
              vertexToEdge.addNeighbour(vindex0, e);
              vertexToEdge.addNeighbour(vindex1, e);
            }
          }
          prevTriangles = triangles();
        }
        if ((*subdivisionMethod)[it] == SubdivisionMethod::MID_POINT) {
          SubdivideMidPoint<Vec3<T>, T>(points(),
                                        it,
                                        prevPointCount,
                                        edges,
                                        submeshFrameLodMap,
                                        submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::LOOP) {
          SubdivideLoop<Vec3<T>, T>(points(),
                                    it,
                                    prevPointCount,
                                    edges,
                                    prevTriangles,
                                    vertexToTriangle,
                                    submeshFrameLodMap,
                                    submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::NORMAL) {
          resizeNormals(points().size());
          computeNormals(prevTriangles);
          SubdivideNormal<Vec3<T>, T>(points(),
                                      normals(),
                                      it,
                                      prevPointCount,
                                      edges,
                                      submeshFrameLodMap,
                                      submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::PYTHAG) {
          SubdivideMidPointPythagMeans<Vec3<T>, T>(points(),
                                                   it,
                                                   prevPointCount,
                                                   edges,
                                                   prevTriangles,
                                                   vertexToTriangle,
                                                   submeshFrameLodMap,
                                                   submeshFrameChildToParentMap);
        }
        if ((*subdivisionMethod)[it] >= SubdivisionMethod::MID_POINT
            && (*subdivisionMethod)[it] < SubdivisionMethod::METHOD_NUM) {
          if (edgeLengthThreshold > 0) {
            AdaptiveUpdateEdgeAndFace<T>(triangles(),
                                         iterationCount,
                                         edgeThr,
                                         bitshiftEdgeBasedSubdivision,
                                         it,
                                         prevPointCount,
                                         edges,
                                         triangleToBaseMeshTriangle,
                                         nullptr,
                                         refGeom,
                                         vertexToEdge);
          }
          else {
            UpdateEdgeAndFace(triangles(),
                              iterationCount,
                              it,
                              prevPointCount,
                              edges,
                              triangleToBaseMeshTriangle,
                              prevTriangles);
          }
        }
        if (infoLevelOfDetails != nullptr) {
          auto& infoLevelOfDetail         = (*infoLevelOfDetails)[it + 1];
          infoLevelOfDetail.pointCount    = pointCount();
          infoLevelOfDetail.triangleCount = triangleCount();
        }
        // printf("Subdivision [%d] : num of edges = %d\n", it, edges.size());
      }
    } else {
      coordEdges->resize(4 * vCount, int64_t(-1));
      int32_t tCount0        = triangleCount();
      int     prevPointCount = pointCount();

      if (iterationCount > 0 && edgeLengthThreshold == 0)
        initializeSubdiv(prevPointCount,
                         triangles(),
                         true,
                         valences(),
                         vertexToTriangle,
                         vertexToEdge,
                         vtags,
                         *coordEdges);
      for (int32_t it = 0; it < iterationCount; ++it) {
        int32_t edgeThr = edgeLengthThreshold;
        if (edgeLengthThreshold > 0) {
          refGeom = refMesh ? (*refMesh)[it].points() : refGeom;
          if (refMesh == nullptr) {
            refGeoms[it] = refGeom;
            refTris[it] = triangles();
          }
          if (it + 1 < iterationCount - 1) edgeThr = 0;
          if (it == 0) {
            auto thr = (iterationCount - 1 > 0) ? 0 : edgeThr;
            initializeSubdivWithEdgeLengthThreshold<T>(prevPointCount,
                                                       refGeom,
                                                       triangles(),
                                                       true,
                                                       valences(),
                                                       vertexToTriangle,
                                                       vertexToEdge,
                                                       vtags,
                                                       *coordEdges,
                                                       thr,
                                                       bitshiftEdgeBasedSubdivision,
                                                       nullptr);
          } else {
            //compute vertexToEdge
            ComputeVertexToTriangle(triangles(), prevPointCount, vertexToTriangle);

            vertexToEdge.resize((*coordEdges).size());

            for (int32_t e = prevPointCount; e < (*coordEdges).size(); ++e) {
              auto vindex0 = (*coordEdges)[e] >> 32;
              auto vindex1 = (*coordEdges)[e] & 0xFFFFFFFF;
              vertexToEdge.incrementNeighbourCount(vindex0, 1);
              vertexToEdge.incrementNeighbourCount(vindex1, 1);
              vertexToEdge.incrementNeighbourCount(e, 2);
            }

            vertexToEdge.updateShift();

            for (int32_t e = prevPointCount; e < (*coordEdges).size(); ++e) {
              auto vindex0 = (*coordEdges)[e] >> 32;
              auto vindex1 = (*coordEdges)[e] & 0xFFFFFFFF;
              vertexToEdge.addNeighbour(vindex0, e);
              vertexToEdge.addNeighbour(vindex1, e);
            }
          }
          prevTriangles = triangles();
        }
        if ((*subdivisionMethod)[it] == SubdivisionMethod::MID_POINT) {
          SubdivideMidPoint<Vec3<T>, T>(points(),
                                        it,
                                        prevPointCount,
                                        *coordEdges,
                                        submeshFrameLodMap,
                                        submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::LOOP) {
          SubdivideLoop<Vec3<T>, T>(points(),
                                    it,
                                    prevPointCount,
                                    *coordEdges,
                                    prevTriangles,
                                    vertexToTriangle,
                                    submeshFrameLodMap,
                                    submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::NORMAL) {
          resizeNormals(points().size());
          computeNormals(prevTriangles);
          SubdivideNormal<Vec3<T>, T>(points(),
                                      normals(),
                                      it,
                                      prevPointCount,
                                      *coordEdges,
                                      submeshFrameLodMap,
                                      submeshFrameChildToParentMap);
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::PYTHAG) {
          SubdivideMidPointPythagMeans<Vec3<T>, T>(points(),
                                                   it,
                                                   prevPointCount,
                                                   *coordEdges,
                                                   prevTriangles,
                                                   vertexToTriangle,
                                                   submeshFrameLodMap,
                                                   submeshFrameChildToParentMap);
        }
        if ((*subdivisionMethod)[it] >= SubdivisionMethod::MID_POINT
          && (*subdivisionMethod)[it] < SubdivisionMethod::METHOD_NUM) {
          if (edgeLengthThreshold > 0) {
            AdaptiveUpdateEdgeAndFace<T>(triangles(),
                                         iterationCount,
                                         edgeThr,
                                         bitshiftEdgeBasedSubdivision,
                                         it,
                                         prevPointCount,
                                         *coordEdges,
                                         triangleToBaseMeshTriangle,
                                         nullptr,
                                         refGeom,
                                         vertexToEdge);
          }
          else {
            UpdateEdgeAndFace(triangles(),
                              iterationCount,
                              it,
                              prevPointCount,
                              *coordEdges,
                              triangleToBaseMeshTriangle,
                              prevTriangles);
          }
        }
        if (infoLevelOfDetails != nullptr) {
          auto& infoLevelOfDetail         = (*infoLevelOfDetails)[it + 1];
          infoLevelOfDetail.pointCount    = pointCount();
          infoLevelOfDetail.triangleCount = triangleCount();
        }
        // printf("Subdivision [%d] : num of edges = %d\n", it, coordEdges->size());
      }
    }
  }
  if (texCoordTriangleCount() && texCoordCount()) {
    const auto tcCount = texCoordCount() * std::pow(4, iterationCount - 1);
    reserveTexCoordTriangles(4 * tCount);
    reserveTexCoords(4 * tcCount);
    StaticAdjacencyInformation<int32_t> vertexToTriangleUV;
    StaticAdjacencyInformation<int32_t> vertexToEdgeUV;
    std::vector<int8_t>                 vtagsUV;
    vertexToTriangleUV.reserve(tcCount);
    vertexToEdgeUV.reserve(tcCount);
    vtagsUV.reserve(tcCount);
    std::vector<Vec2<T>> texNormals         = {};
    if (texCoordEdges == nullptr) {
      std::vector<int64_t> edges(4 * tcCount, int64_t(-1));
      int                  prevPointCount = texCoordCount();

      if (iterationCount > 0 && edgeLengthThreshold == 0)
        initializeSubdiv(prevPointCount,
                         texCoordTriangles(),
                         false,
                         valences(),
                         vertexToTriangleUV,
                         vertexToEdgeUV,
                         vtagsUV,
                         edges);
      if (edgeLengthThreshold > 0) {
        if (refMesh) {
          for (int32_t it = 0; it < iterationCount; ++it) {
            refGeoms[it] = ((*refMesh)[it]).points();
            refTris[it]  = ((*refMesh)[it]).triangles();
          }
        }
      }
      for (int32_t it = 0; it < iterationCount; ++it) {
        auto& positionRef = positionRefs[it];
        int32_t edgeThr = edgeLengthThreshold;
        if (edgeLengthThreshold > 0) {
          positionRef.resize(texCoordCount(), -1);
          auto&       Uvs     = texCoords();
          auto&       tris    = texCoordTriangles();
          const auto& trisRef = refTris[it];
          for (int t = 0; t < texCoordTriangleCount(); t++) {
            auto& tri = tris[t];
            const auto& triRef = trisRef[t];
            for (int i = 0; i < 3; i++) {
              auto texIdx = tri[i];
              auto verIdx = triRef[i];
              if (it > 0) {
                positionRef[texIdx] = verIdx;
              }
              else {
                if (positionRef[texIdx] == -1) {
                  positionRef[texIdx] = verIdx;
                }
                else if (point(positionRef[texIdx]) != point(verIdx)) {
                  // uvidx is updated
                  int32_t newIdx  = positionRef.size();
                  int32_t newIdx2 = Uvs.size();
                  positionRef.push_back(verIdx);
                  Uvs.push_back(texCoord(texIdx));
                  assert(newIdx == newIdx2);
                  tri[i] = newIdx;
                }
              }
            }
          }
          prevPointCount = texCoordCount();
          if (it + 1 < iterationCount - 1) edgeThr = 0;
          if (it == 0) {
            auto thr = (iterationCount - 1 > 0) ? 0 : edgeThr;
            initializeSubdivWithEdgeLengthThreshold<T>(prevPointCount,
                                                       refGeoms[it],
                                                       texCoordTriangles(),
                                                       false,
                                                       valences(),
                                                       vertexToTriangleUV,
                                                       vertexToEdgeUV,
                                                       vtagsUV,
                                                       edges,
                                                       thr,
                                                       bitshiftEdgeBasedSubdivision,
                                                       &positionRef);
          } else {
            //compute vertexToEdge
            ComputeVertexToTriangle(texCoordTriangles(), prevPointCount, vertexToTriangleUV);

            vertexToEdgeUV.resize(edges.size());

            for (int32_t e = prevPointCount; e < edges.size(); ++e) {
              auto vindex0 = edges[e] >> 32;
              auto vindex1 = edges[e] & 0xFFFFFFFF;
              vertexToEdgeUV.incrementNeighbourCount(vindex0, 1);
              vertexToEdgeUV.incrementNeighbourCount(vindex1, 1);
              vertexToEdgeUV.incrementNeighbourCount(e, 2);
            }

            vertexToEdgeUV.updateShift();

            for (int32_t e = prevPointCount; e < edges.size(); ++e) {
              auto vindex0 = edges[e] >> 32;
              auto vindex1 = edges[e] & 0xFFFFFFFF;
              vertexToEdgeUV.addNeighbour(vindex0, e);
              vertexToEdgeUV.addNeighbour(vindex1, e);
            }
          }
          prevTexCoordTriangles = texCoordTriangles();
        }
        if ((*subdivisionMethod)[it] == SubdivisionMethod::MID_POINT
            || (*subdivisionMethod)[it] == SubdivisionMethod::NORMAL
            || (*subdivisionMethod)[it] == SubdivisionMethod::PYTHAG) {
          SubdivideMidPoint<Vec2<T>, T>(texCoords(),
                                        it,
                                        prevPointCount,
                                        edges,
                                        nullptr,
                                        nullptr);
          
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::LOOP) {
          SubdivideLoop<Vec2<T>, T>(texCoords(),
                                    it,
                                    prevPointCount,
                                    edges,
                                    prevTexCoordTriangles,
                                    vertexToTriangleUV,
                                    nullptr,
                                    nullptr);
        }
        if ((*subdivisionMethod)[it] >= SubdivisionMethod::MID_POINT
          && (*subdivisionMethod)[it] < SubdivisionMethod::METHOD_NUM) {
          if (edgeLengthThreshold > 0) {
            AdaptiveUpdateEdgeAndFace<T>(texCoordTriangles(),
                                         iterationCount,
                                         edgeThr,
                                         bitshiftEdgeBasedSubdivision,
                                         it,
                                         prevPointCount,
                                         edges,
                                         nullptr,
                                         &positionRef,
                                         refGeoms[it],
                                         vertexToEdgeUV);
          } else {
            UpdateEdgeAndFace(texCoordTriangles(),
                              iterationCount,
                              it,
                              prevPointCount,
                              edges,
                              nullptr,
                              prevTexCoordTriangles);
          }
        }
        if (infoLevelOfDetails != nullptr) {
          auto& infoLevelOfDetail         = (*infoLevelOfDetails)[it + 1];
          infoLevelOfDetail.texCoordCount = texCoordCount();
          infoLevelOfDetail.texCoordTriangleCount = texCoordTriangleCount();
        }
      }
    } else {
      texCoordEdges->resize(4 * tcCount, int64_t(-1));
      int32_t tCount0        = texCoordTriangleCount();
      int     prevPointCount = texCoordCount();

      if (iterationCount > 0 && edgeLengthThreshold == 0)
        initializeSubdiv(prevPointCount,
                         texCoordTriangles(),
                         false,
                         valences(),
                         vertexToTriangleUV,
                         vertexToEdgeUV,
                         vtagsUV,
                         *texCoordEdges);
      if (edgeLengthThreshold > 0) {
        if (refMesh) {
          for (int32_t it = 0; it < iterationCount; ++it) {
            refGeoms[it] = ((*refMesh)[it]).points();
            refTris[it]  = ((*refMesh)[it]).triangles();
          }
        }
      }
      for (int32_t it = 0; it < iterationCount; ++it) {
        auto& positionRef = positionRefs[it];
        int32_t edgeThr = edgeLengthThreshold;
        if (edgeLengthThreshold > 0) {
          positionRef.resize(texCoordCount(), -1);
          auto&       Uvs     = texCoords();
          auto&       tris    = texCoordTriangles();
          const auto& trisRef = refTris[it];
          for (int t = 0; t < texCoordTriangleCount(); t++) {
            auto&       tri    = tris[t];
            const auto& triRef = trisRef[t];
            for (int i = 0; i < 3; i++) {
              auto texIdx = tri[i];
              auto verIdx = triRef[i];
              if (it > 0) {
                positionRef[texIdx] = verIdx;
              }
              else {
                if (positionRef[texIdx] == -1) {
                  positionRef[texIdx] = verIdx;
                }
                else if (point(positionRef[texIdx]) != point(verIdx)) {
                  // uvidx is updated
                  int32_t newIdx  = positionRef.size();
                  int32_t newIdx2 = Uvs.size();
                  positionRef.push_back(verIdx);
                  Uvs.push_back(texCoord(texIdx));
                  assert(newIdx == newIdx2);
                  tri[i] = newIdx;
                }
              }
            }
          }
          prevPointCount = texCoordCount();
          if (it + 1 < iterationCount - 1) edgeThr = 0;
          if (it == 0) {
            auto thr = (iterationCount - 1 > 0) ? 0 : edgeThr;
            initializeSubdivWithEdgeLengthThreshold<T>(prevPointCount,
                                                       refGeoms[it],
                                                       texCoordTriangles(),
                                                       false,
                                                       valences(),
                                                       vertexToTriangleUV,
                                                       vertexToEdgeUV,
                                                       vtagsUV,
                                                       *texCoordEdges,
                                                       thr,
                                                       bitshiftEdgeBasedSubdivision,
                                                       &positionRef);
          } else {
            //compute vertexToEdge
            ComputeVertexToTriangle(texCoordTriangles(), prevPointCount, vertexToTriangleUV);

            vertexToEdgeUV.resize((*texCoordEdges).size());

            for (int32_t e = prevPointCount; e < (*texCoordEdges).size(); ++e) {
              auto vindex0 = (*texCoordEdges)[e] >> 32;
              auto vindex1 = (*texCoordEdges)[e] & 0xFFFFFFFF;
              vertexToEdgeUV.incrementNeighbourCount(vindex0, 1);
              vertexToEdgeUV.incrementNeighbourCount(vindex1, 1);
              vertexToEdgeUV.incrementNeighbourCount(e, 2);
            }

            vertexToEdgeUV.updateShift();

            for (int32_t e = prevPointCount; e < (*texCoordEdges).size(); ++e) {
              auto vindex0 = (*texCoordEdges)[e] >> 32;
              auto vindex1 = (*texCoordEdges)[e] & 0xFFFFFFFF;
              vertexToEdgeUV.addNeighbour(vindex0, e);
              vertexToEdgeUV.addNeighbour(vindex1, e);
            }
          }
          prevTexCoordTriangles = texCoordTriangles();
        }
        if ((*subdivisionMethod)[it] == SubdivisionMethod::MID_POINT
            || (*subdivisionMethod)[it] == SubdivisionMethod::NORMAL
            || (*subdivisionMethod)[it] == SubdivisionMethod::PYTHAG) {
          SubdivideMidPoint<Vec2<T>, T>(texCoords(),
                                        it,
                                        prevPointCount,
                                        *texCoordEdges,
                                        nullptr,
                                        nullptr);
          
        } else if ((*subdivisionMethod)[it] == SubdivisionMethod::LOOP) {
          SubdivideLoop<Vec2<T>, T>(texCoords(),
                                    it,
                                    prevPointCount,
                                    *texCoordEdges,
                                    prevTexCoordTriangles,
                                    vertexToTriangleUV,
                                    nullptr,
                                    nullptr);
        }
        if ((*subdivisionMethod)[it] >= SubdivisionMethod::MID_POINT
          && (*subdivisionMethod)[it] < SubdivisionMethod::METHOD_NUM) {
          if (edgeLengthThreshold > 0) {
            AdaptiveUpdateEdgeAndFace<T>(texCoordTriangles(),
                                         iterationCount,
                                         edgeThr,
                                         bitshiftEdgeBasedSubdivision,
                                         it,
                                         prevPointCount,
                                         *texCoordEdges,
                                         nullptr,
                                         &positionRef,
                                         refGeoms[it],
                                         vertexToEdgeUV);
          } else {
            UpdateEdgeAndFace(texCoordTriangles(),
                              iterationCount,
                              it,
                              prevPointCount,
                              *texCoordEdges,
                              nullptr,
                              prevTexCoordTriangles);
          }
        }
        if (infoLevelOfDetails != nullptr) {
          auto& infoLevelOfDetail         = (*infoLevelOfDetails)[it + 1];
          infoLevelOfDetail.texCoordCount = texCoordCount();
          infoLevelOfDetail.texCoordTriangleCount = texCoordTriangleCount();
        }
      }
    }
  }
}

//============================================================================

template class TriangleMesh<float>;
template class TriangleMesh<double>;
template class TriangleMesh<int32_t>;

//============================================================================

}  // namespace vmesh
