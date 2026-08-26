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

#include <cassert>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <array>
#include <algorithm>

#include "util/box.hpp"
#include "util/misc.hpp"
#include "util/sparsematrix.hpp"
#include "util/triangle.hpp"

#define DISPL_AC_TEMP_SETTING 1
#define DISPL_INVQUANT_FIXEDP 6
#define DEBUG_NONE_TRANSFORM 0
#define DEBUG_INTERPOLATED_NORMALS 1
#define SUBDIVCOUNTCONSISTENCY 1
namespace vmesh {

//============================================================================

enum GeometryCodecId {
  DRACO                  = 0,
  MPEG                   = 1,
  UNKNOWN_GEOMETRY_CODEC = 255
};

//============================================================================

static std::istream&
operator>>(std::istream& in, GeometryCodecId& val) {
  std::string str;
  in >> str;
  val = GeometryCodecId::UNKNOWN_GEOMETRY_CODEC;
  printf("GeometryCodecId: str = %s \n", str.c_str());
  printf("GeometryCodecId: here \n");
  if (str == "DRACO") { val = GeometryCodecId::DRACO; }
  if (str == "MPEG") { val = GeometryCodecId::MPEG; }
  if (val == GeometryCodecId::UNKNOWN_GEOMETRY_CODEC) {
    in.setstate(std::ios::failbit);
  }
  return in;
}

//============================================================================

enum class SubdivisionMethod {
  MID_POINT  = 0,
  LOOP       = 1,
  NORMAL     = 2,
  PYTHAG     = 3,
  METHOD_NUM = 4,
};

//============================================================================

enum class TransformMethod {
  NONE_TRANSFORM = 0,
  LINEAR_LIFTING = 1,
};

//============================================================================

enum MeshAttribueType {  //table H-2
  ATTRIBUTE_COLOR        = 0,
  ATTRIBUTE_MATERIALID   = 1,
  ATTRIBUTE_TRANSPARENCY = 2,
  ATTRIBUTE_REFLECTION   = 3,
  ATTRIBUTE_NORMAL       = 4,
  ATTRIBUTE_TEXCOORD     = 5,
  ATTRIBUTE_FACEGROUPID  = 6,
  ATTRIBUTE_RESERVED     = 7,
};

//============================================================================

template<typename T>
class StaticAdjacencyInformation {
public:
  StaticAdjacencyInformation()                                  = default;
  StaticAdjacencyInformation(const StaticAdjacencyInformation&) = default;
  StaticAdjacencyInformation&
  operator=(const StaticAdjacencyInformation&) = default;
  ~StaticAdjacencyInformation()                = default;

  void clear() {
    _shift.resize(0);
    _neighbourCount.resize(0);
    _neighbours.resize(0);
  }

  int32_t size() const { return int32_t(_neighbourCount.size()); }
  void    reserve(const int32_t sz) {
    _shift.reserve(sz + 1);
    _neighbourCount.reserve(sz);
    _neighbours.reserve(6 * sz);
  }

  void resize(const int32_t sz) {
    _shift.resize(0);
    _neighbourCount.resize(0);
    _shift.resize(sz + 1, 0);
    _neighbourCount.resize(sz, 0);
  }

  void incrementSize() {
    auto currentSize = size();
    _shift.resize(currentSize + 2);
    _neighbourCount.resize(currentSize + 1);
  }

  void incrementNeighbourCount(const int32_t i) {
    assert(i < size());
    ++_shift[i + 1];
  }

  void incrementNeighbourCount(const int32_t i, const int32_t count) {
    assert(i < size());
    _shift[i + 1] += count;
  }

  void updateShift() {
    for (int32_t i = 1, count = int32_t(_shift.size()); i < count; ++i) {
      _shift[i] += _shift[i - 1];
    }
    _neighbours.resize(_shift.back());
  }

  int32_t maxNeighbourCount(const int32_t i) const {
    assert(i < size());
    assert(_shift[i + 1] >= _shift[i]);
    return _shift[i + 1] - _shift[i];
  }

  int32_t neighbourCount(const int32_t i) const {
    assert(i < size());
    return _neighbourCount[i];
  }

  void addNeighbour(const int32_t i, const T& neighbour) {
    assert(i >= 0 && i <= size());
    assert(neighbour >= 0);
    assert(_neighbourCount[i] < maxNeighbourCount(i));
    _neighbours[_shift[i] + _neighbourCount[i]++] = neighbour;
  }

  void updateNeighbour(const int32_t i, const T& neighbour) {
    assert(i >= 0 && i <= size());
    assert(neighbour >= 0);
    assert(_neighbourCount[i] < maxNeighbourCount(i));
    _neighbours[i] = neighbour;
  }

  int32_t neighboursStartIndex(const int32_t i) const {
    assert(i < size());
    return _shift[i];
  }

  int32_t neighboursEndIndex(const int32_t i) const {
    assert(i < size());
    return _shift[i] + _neighbourCount[i];
  }

  const T* neighbours() const { return _neighbours.data(); }
  T*       neighbours() { return _neighbours.data(); }

private:
  std::vector<int32_t> _shift;
  std::vector<int32_t> _neighbourCount;
  std::vector<T>       _neighbours;
};

//============================================================================

struct SubdivisionLevelInfo {
  int32_t pointCount            = -1;
  int32_t texCoordCount         = -1;
  int32_t triangleCount         = -1;
  int32_t texCoordTriangleCount = -1;
};

//============================================================================

template<typename T>
struct Material {
  std::string  name             = "material0000";
  std::string  texture          = "texture.png";
  Vec3<double> ambiant          = Vec3<double>(1.0);
  Vec3<double> diffuse          = Vec3<double>(1.0);
  Vec3<double> specular         = Vec3<double>(1.0);
  double       specularExponent = 0.1;
  double       transparency     = 1.0;
  int32_t      illumination     = 2;
  bool         save(const std::string& fileName) {
    std::ofstream fout(fileName);
    if (fout.is_open()) {
      fout << "newmtl " << name << '\n';
      fout << "Ka " << ambiant << '\n';
      fout << "Kd " << diffuse << '\n';
      fout << "Ks " << specular << '\n';
      fout << "Tr " << transparency << '\n';
      fout << "illum " << illumination << '\n';
      fout << "Ns " << specularExponent << '\n';
      if (texture != "") fout << "map_Kd " << texture << '\n';
      fout << '\n';
      fout.close();
      return true;
    }
    return false;
  }
  bool append(const std::string& fileName) {
    std::ofstream fout(fileName, std::ios::app);
    if (fout.is_open()) {
      fout << "newmtl " << name << '\n';
      fout << "Ka " << ambiant << '\n';
      fout << "Kd " << diffuse << '\n';
      fout << "Ks " << specular << '\n';
      fout << "Tr " << transparency << '\n';
      fout << "illum " << illumination << '\n';
      fout << "Ns " << specularExponent << '\n';
      if (texture != "") fout << "map_Kd " << texture << '\n';
      fout << '\n';
      fout.close();
      return true;
    }
    return false;
  }
};

//============================================================================

template<typename T>
class TriangleMesh {
public:
  TriangleMesh<T>& operator=(const TriangleMesh<T>& src) {
    _disp           = src._disp;
    _coord          = src._coord;
    _colour         = src._colour;
    _coordIndex     = src._coordIndex;
    _texCoord       = src._texCoord;
    _texCoordIndex  = src._texCoordIndex;
    _track          = src._track;
    _normal         = src._normal;
    _normalIndex    = src._normalIndex;
    _mtllib         = src._mtllib;
    _faceId         = src._faceId;
    _materialIdx    = src._materialIdx;
    _materialNames  = src._materialNames;
    _textureMapUrls = src._textureMapUrls;
    _valence        = src._valence;
    _tracktri       = src._tracktri;
    _modifytri      = src._modifytri;
    return *this;
  }

  void scaleTextureCoordinates(int qt) {
    const auto scale = qt > 0 ? 1.0 / ((1 << qt) - 1) : 1.0;
    for (auto& v : _texCoord) { v *= scale; }
  }
  const std::string& materialLibrary() const { return _mtllib; }
  std::string&       materialLibrary() { return _mtllib; }
  void setMaterialLibrary(const std::string& mtllib) { _mtllib = mtllib; }
  const std::vector<std::string>& materialNames() const {
    return _materialNames;
  }
  std::vector<std::string>& materialNames() { return _materialNames; }
  void setMaterialNames(const std::vector<std::string>& materialNames) {
    _materialNames = materialNames;
  }
  const std::vector<std::string>& textureMapUrls() const {
    return _textureMapUrls;
  }
  std::vector<std::string>& textureMapUrls() { return _textureMapUrls; }
  void setTextureMapUrls(const std::vector<std::string>& textureMapUrls) {
    _textureMapUrls = textureMapUrls;
  }
  int32_t trackCount() const { return int32_t(_track.size()); }
  int32_t tracktriCount() const { return int32_t(_tracktri.size()); }
  int32_t modifytriCount() const { return int32_t(_modifytri.size()); }

  int32_t displacementCount() const { return int32_t(_disp.size()); }
  int32_t pointCount() const { return int32_t(_coord.size()); }
  int32_t colourCount() const { return int32_t(_colour.size()); }
  int32_t triangleCount() const { return int32_t(_coordIndex.size()); }
  int32_t texCoordCount() const { return int32_t(_texCoord.size()); }

  int32_t texCoordTriangleCount() const {
    return int32_t(_texCoordIndex.size());
  }

  int32_t normalCount() const { return int32_t(_normal.size()); }
  int32_t normalTriangleCount() const { return int32_t(_normalIndex.size()); }

  int32_t faceIdCount() const { return int32_t(_faceId.size()); }
  int32_t materialIdxCount() const { return int32_t(_materialIdx.size()); }

  int32_t valenceCount() const { return int32_t(_valence.size()); }

  const Vec3<T>& displacement(const int32_t dispIndex) const {
    assert(dispIndex >= 0 && dispIndex < displacementCount());
    return _disp[dispIndex];
  }

  Vec3<T>& displacement(const int32_t dispIndex) {
    assert(dispIndex >= 0 && dispIndex < displacementCount());
    return _disp[dispIndex];
  }

  const Vec3<T>& point(const int32_t pointIndex) const {
    assert(pointIndex >= 0 && pointIndex < pointCount());
    return _coord[pointIndex];
  }

  Vec3<T>& point(const int32_t pointIndex) {
    assert(pointIndex >= 0 && pointIndex < pointCount());
    return _coord[pointIndex];
  }

  const Vec3<T>& colour(const int32_t colourIndex) const {
    assert(colourIndex >= 0 && colourIndex < colourCount());
    return _colour[colourIndex];
  }

  Vec3<T>& colour(const int32_t colourIndex) {
    assert(colourIndex >= 0 && colourIndex < colourCount());
    return _colour[colourIndex];
  }

  const Vec2<T>& texCoord(const int32_t texCoordIndex) const {
    assert(texCoordIndex >= 0 && texCoordIndex < texCoordCount());
    return _texCoord[texCoordIndex];
  }

  Vec2<T>& texCoord(const int32_t texCoordIndex) {
    assert(texCoordIndex >= 0 && texCoordIndex < texCoordCount());
    return _texCoord[texCoordIndex];
  }

  const Vec3<T>& normal(const int32_t normalIndex) const {
    assert(normalIndex >= 0 && normalIndex < normalCount());
    return _normal[normalIndex];
  }

  Vec3<T>& normal(const int32_t normalIndex) {
    assert(normalIndex >= 0 && normalIndex < normalCount());
    return _normal[normalIndex];
  }

  const int& faceId(const int32_t faceIdIndex) const {
    assert(faceIdIndex >= 0 && faceIdIndex < faceIdCount());
    return _faceId[faceIdIndex];
  }

  int& faceId(const int32_t faceIdIndex) {
    assert(faceIdIndex >= 0 && faceIdIndex < faceIdCount());
    return _faceId[faceIdIndex];
  }

  const int& materialIdx(const int32_t triangleIndex) const {
    assert(triangleIndex >= 0 && triangleIndex < materialIdxCount());
    return _materialIdx[triangleIndex];
  }

  int& materialIdx(const int32_t triangleIndex) {
    assert(triangleIndex >= 0 && triangleIndex < materialIdxCount());
    return _materialIdx[triangleIndex];
  }

  const int& valence(const int32_t valenceIndex) const {
    assert(valenceIndex >= 0 && valenceIndex < valenceCount());
    return _valence[valenceIndex];
  }

  int& valence(const int32_t valenceIndex) {
    assert(valenceIndex >= 0 && valenceIndex < valenceCount());
    return _valence[valenceIndex];
  }
  const int& track(const int32_t trackIndex) const {
    assert(trackIndex >= 0 && trackIndex < trackCount());
    return _track[trackIndex];
  }

  int& track(const int32_t trackIndex) {
    assert(trackIndex >= 0 && trackIndex < trackCount());
    return _track[trackIndex];
  }
  const std::vector<int>& tracktri(const int32_t trackTriIndex) const {
    assert(trackTriIndex >= 0 && trackTriIndex < tracktriCount());
    return _tracktri[trackTriIndex];
  }
  std::vector<int>& tracktri(const int32_t trackTriIndex) {
    assert(trackTriIndex >= 0 && trackTriIndex < tracktriCount());
    return _tracktri[trackTriIndex];
  }
  const std::vector<int>& modifytri(const int32_t modifyTriIndex) const {
    assert(modifyTriIndex >= 0 && modifyTriIndex < modifytriCount());
    return _modifytri[modifyTriIndex];
  }
  std::vector<int>& modifytri(const int32_t modifyTriIndex) {
    assert(modifyTriIndex >= 0 && modifyTriIndex < modifytriCount());
    return _modifytri[modifyTriIndex];
  }

  T area() const {
    T area = T(0);
    for (const auto& tri : _coordIndex) {
      const auto i = tri[0];
      const auto j = tri[1];
      const auto k = tri[2];
      const auto a = _coord[i];
      const auto b = _coord[j];
      const auto c = _coord[k];
      area +=
        computeTriangleArea(_coord[tri[0]], _coord[tri[1]], _coord[tri[2]]);
    }
    return area;
  }

  T projectedArea(int projIdx) const {
    T   projectedArea = T(0);
    int tangentAxis, biTangentAxis, projAxis;
    int tangentAxisDir, biTangentAxisDir, projAxisDir;
    getProjectedAxis(projIdx,
                     tangentAxis,
                     biTangentAxis,
                     projAxis,
                     tangentAxisDir,
                     biTangentAxisDir,
                     projAxisDir);
    for (const auto& tri : _coordIndex) {
      const auto i  = tri[0];
      const auto j  = tri[1];
      const auto k  = tri[2];
      auto       a  = _coord[i];
      auto       b  = _coord[j];
      auto       c  = _coord[k];
      auto       aP = projectPoint(a, projIdx);
      auto       bP = projectPoint(b, projIdx);
      auto       cP = projectPoint(c, projIdx);
      projectedArea += (aP[0] * (bP[1] - cP[1]) + bP[0] * (cP[1] - aP[1])
                        + cP[0] * (aP[1] - bP[1]))
                       / 2;
    }
    return projectedArea;
  }

  T perimeter() const;

  T stretchL2(int projIdx) const {
    int tangentAxis, biTangentAxis, projAxis;
    int tangentAxisDir, biTangentAxisDir, projAxisDir;
    getProjectedAxis(projIdx,
                     tangentAxis,
                     biTangentAxis,
                     projAxis,
                     tangentAxisDir,
                     biTangentAxisDir,
                     projAxisDir);
    T strechL2  = T(0);
    T areaTotal = T(0);
    for (const auto& tri : _coordIndex) {
      const auto i              = tri[0];
      const auto j              = tri[1];
      const auto k              = tri[2];
      auto       a              = _coord[i];
      auto       b              = _coord[j];
      auto       c              = _coord[k];
      Vec3<T>    normalTriangle = computeTriangleNormal(a, b, c, false);
      T          areaTriangle   = 0.5 * normalTriangle.norm();
      areaTotal += areaTriangle;
      if (areaTriangle == T(0.0)) {
        // degenerate 3D triangle, projection will not affect the projected area
        continue;
      }
      if ((normalTriangle * PROJDIRECTION18[projIdx]) > 0.0) {
        auto aP = projectPoint(a, projIdx);
        auto bP = projectPoint(b, projIdx);
        auto cP = projectPoint(c, projIdx);
        T    areaProjectedTriangle =
          (aP[0] * (bP[1] - cP[1]) + bP[0] * (cP[1] - aP[1])
           + cP[0] * (aP[1] - bP[1]))
          / 2;
        strechL2 += (0.5
                     + 0.5 * (areaTriangle / areaProjectedTriangle)
                         * (areaTriangle / areaProjectedTriangle))
                    * areaTriangle;
      } else {
        return std::numeric_limits<T>::max();
      }
    }
    if (areaTotal == T(0.0)) return 0.0;
    else return std::sqrt(strechL2 / areaTotal);
  }

  void computeTriangleNormals(std::vector<Vec3<T>>& normals,
                              const bool            normalize = true) const {
    const auto triCount = triangleCount();
    normals.resize(triCount);
    for (int32_t t = 0; t < triCount; ++t) {
      const auto& tri = triangle(t);
      normals[t]      = computeTriangleNormal(
        _coord[tri[0]], _coord[tri[1]], _coord[tri[2]], normalize);
    }
  }

  void computeTriangleAreas(std::vector<double>& areas) const {
    const auto triCount = triangleCount();
    areas.resize(triCount);
    for (int32_t t = 0; t < triCount; ++t) {
      const auto& tri = triangle(t);
      areas[t] =
        computeTriangleArea(_coord[tri[0]], _coord[tri[1]], _coord[tri[2]]);
    }
  }

  void computeProjectedTriangleAreas(std::vector<double>& areas,
                                     int                  projIdx) const {
    const auto triCount = triangleCount();
    areas.resize(triCount);
    for (int32_t t = 0; t < triCount; ++t) {
      const auto& tri     = triangle(t);
      Vec3<T>     vert[3] = {_coord[tri[0]], _coord[tri[1]], _coord[tri[2]]};
      auto        aP      = projectPoint(vert[0], projIdx);
      auto        bP      = projectPoint(vert[1], projIdx);
      auto        cP      = projectPoint(vert[2], projIdx);
      areas[t]            = (aP[0] * (bP[1] - cP[1]) + bP[0] * (cP[1] - aP[1])
                  + cP[0] * (aP[1] - bP[1]))
                 / 2;
    }
  }

  void computeNormals(const bool normalize = true) {
    const auto ptCount = pointCount();
    _normal.resize(ptCount);
    std::fill(_normal.begin(), _normal.end(), T(0));
    for (const auto& tri : _coordIndex) {
      const auto i = tri[0];
      const auto j = tri[1];
      const auto k = tri[2];
      const auto n =
        computeTriangleNormal(_coord[i], _coord[j], _coord[k], false);
      _normal[i] += n;
      _normal[j] += n;
      _normal[k] += n;
    }
    if (normalize) {
      for (auto& n : _normal) { n.normalize(); }
    }
  }

  void computeNormals(std::vector<Triangle>& prevTriangles,
                      const bool             normalize = true) {
    const auto ptCount = pointCount();
    _normal.resize(ptCount);
    std::fill(_normal.begin(), _normal.end(), T(0));
    for (const auto& tri : prevTriangles) {
      const auto i = tri[0];
      const auto j = tri[1];
      const auto k = tri[2];
      const auto n =
        computeTriangleNormal(_coord[i], _coord[j], _coord[k], false);
      _normal[i] += n;
      _normal[j] += n;
      _normal[k] += n;
    }
    if (normalize) {
      for (auto& n : _normal) { n.normalize(); }
    }
  }

  void computeUnitaryFaceNormals() {
    computeNormals();
    for (auto& n : _normal) { n.normalize(); }
  }

  Box3<T> dispBoundingBox() const {
    Box3<T> bbox;
    bbox.min = std::numeric_limits<T>::max();
    bbox.max = std::numeric_limits<T>::min();
    for (const auto& pt : _disp) {
      for (int32_t k = 0; k < 3; ++k) {
        bbox.min[k] = std::min(bbox.min[k], pt[k]);
        bbox.max[k] = std::max(bbox.max[k], pt[k]);
      }
    }
    return bbox;
  }

  Box3<T> boundingBox() const {
    Box3<T> bbox;
    bbox.min = std::numeric_limits<T>::max();
    bbox.max = std::numeric_limits<T>::min();
    for (const auto& pt : _coord) {
      for (int32_t k = 0; k < 3; ++k) {
        bbox.min[k] = std::min(bbox.min[k], pt[k]);
        bbox.max[k] = std::max(bbox.max[k], pt[k]);
      }
    }
    return bbox;
  }

  Box2<double> boundingBoxProjected(int projIdx) const {
    Box2<double> bbox;
    bbox.min = std::numeric_limits<double>::max();
    bbox.max = std::numeric_limits<double>::min();
    for (const auto& pt : _coord) {
      auto projPt = projectPoint(pt, projIdx);
      for (int32_t k = 0; k < 2; ++k) {
        bbox.min[k] = std::min(bbox.min[k], projPt[k]);
        bbox.max[k] = std::max(bbox.max[k], projPt[k]);
      }
    }
    return bbox;
  }

  Box2<T> texCoordBoundingBox() const {
    Box2<T> bbox;
    bbox.min = std::numeric_limits<T>::max();
    bbox.max = std::numeric_limits<T>::min();
    for (const auto& pt : _texCoord) {
      for (int32_t k = 0; k < 2; ++k) {
        bbox.min[k] = std::min(bbox.min[k], pt[k]);
        bbox.max[k] = std::max(bbox.max[k], pt[k]);
      }
    }
    return bbox;
  }

  Box3<T> normalsBoundingBox() const {
    Box3<T> bbox;
    bbox.min = std::numeric_limits<T>::max();
    bbox.max = std::numeric_limits<T>::min();
    for (const auto& normal : _normal) {
      for (int32_t k = 0; k < 3; ++k) {
        bbox.min[k] = std::min(bbox.min[k], normal[k]);
        bbox.max[k] = std::max(bbox.max[k], normal[k]);
      }
    }
    return bbox;
  }

  void invertOrientation();

  const std::vector<Vec3<T>>&   displacements() const { return _disp; }
  const std::vector<Vec3<T>>&   points() const { return _coord; }
  const std::vector<Vec3<T>>&   colours() const { return _colour; }
  const std::vector<Vec3<int>>& triangles() const { return _coordIndex; }
  const std::vector<Vec2<T>>&   texCoords() const { return _texCoord; }
  const std::vector<Vec3<int>>& texCoordTriangles() const {
    return _texCoordIndex;
  }
  const std::vector<Vec3<T>>&   normals() const { return _normal; }
  const std::vector<Vec3<int>>& normalTriangles() const {
    return _normalIndex;
  }
  const std::vector<int>& faceIds() const { return _faceId; }
  const std::vector<int>& materialIdxs() const { return _materialIdx; }
  const std::vector<int>& valences() const { return _valence; }
  const std::vector<int>& tracks() const { return _track; }
  const std::vector<std::vector<int32_t>>& tracktris() const {
    return _tracktri;
  }
  const std::vector<std::vector<int32_t>>& modifytris() const {
    return _modifytri;
  }

  std::vector<Vec3<T>>&   displacements() { return _disp; }
  std::vector<Vec3<T>>&   points() { return _coord; }
  std::vector<Vec3<T>>&   colours() { return _colour; }
  std::vector<Vec3<int>>& triangles() { return _coordIndex; }
  std::vector<Vec2<T>>&   texCoords() { return _texCoord; }
  std::vector<Vec3<int>>& texCoordTriangles() { return _texCoordIndex; }
  std::vector<Vec3<T>>&   normals() { return _normal; }
  std::vector<Vec3<int>>& normalTriangles() { return _normalIndex; }
  std::vector<int>&       faceIds() { return _faceId; }
  std::vector<int>&       materialIdxs() { return _materialIdx; }
  std::vector<int>&       valences() { return _valence; }
  std::vector<int>&                  tracks() { return _track; }
  std::vector<std::vector<int32_t>>& tracktris() { return _tracktri; }
  std::vector<std::vector<int32_t>>& modifytris() { return _modifytri; }


  Vec3<int> triangle(const int32_t triangleIndex) const {
    assert(triangleIndex >= 0 && triangleIndex < triangleCount());
    return _coordIndex[triangleIndex];
  }

  Vec3<int> texCoordTriangle(const int32_t texCoordTriangleIndex) const {
    assert(texCoordTriangleIndex >= 0
           && texCoordTriangleIndex < texCoordTriangleCount());
    return _texCoordIndex[texCoordTriangleIndex];
  }

  Vec3<int> normalTriangle(const int32_t normalTriangleIndex) const {
    assert(normalTriangleIndex >= 0
           && normalTriangleIndex < normalTriangleCount());
    return _normalIndex[normalTriangleIndex];
  }

  void setDisplacement(const int32_t dispIndex,
                       const T       dx,
                       const T       dy,
                       const T       dz) {
    assert(dispIndex >= 0 && dispIndex < displacementCount());
    auto& disp = _disp[dispIndex];
    disp[0]    = dx;
    disp[1]    = dy;
    disp[2]    = dz;
  }

  void setDisplacement(const int32_t dispIndex, const Vec3<T>& pt) {
    assert(dispIndex >= 0 && dispIndex < displacementCount());
    _disp[dispIndex] = pt;
  }

  void setPoint(const int32_t pointIndex, const T x, const T y, const T z) {
    assert(pointIndex >= 0 && pointIndex < pointCount());
    auto& pt = _coord[pointIndex];
    pt[0]    = x;
    pt[1]    = y;
    pt[2]    = z;
  }

  void setPoint(const int32_t pointIndex, const Vec3<T>& pt) {
    assert(pointIndex >= 0 && pointIndex < pointCount());
    _coord[pointIndex] = pt;
  }

   void setTrack(const int32_t trackIndex, const int triangle) {
    assert(trackIndex >= 0 && trackIndex < trackCount());
    _track[trackIndex] = triangle;
  }

  void setColour(const int32_t colourIndex, const T r, const T g, const T b) {
    assert(colourIndex >= 0 && colourIndex < colourCount());
    auto& rgb = _colour[colourIndex];
    rgb[0]    = r;
    rgb[1]    = g;
    rgb[2]    = b;
  }

  void setColour(const int32_t colourIndex, const Vec3<T>& rgb) {
    assert(colourIndex >= 0 && colourIndex < colourCount());
    _colour[colourIndex] = rgb;
  }

  void setTexCoord(const int32_t texCoordIndex, const T x, const T y) {
    assert(texCoordIndex >= 0 && texCoordIndex < texCoordCount());
    auto& pt = _texCoord[texCoordIndex];
    pt[0]    = x;
    pt[1]    = y;
  }

  void setTexCoord(const int32_t texCoordIndex, const Vec2<T>& texCoord) {
    assert(texCoordIndex >= 0 && texCoordIndex < texCoordCount());
    _texCoord[texCoordIndex] = texCoord;
  }

  void setNormal(const int32_t normalIndex, const T x, const T y, const T z) {
    assert(normalIndex >= 0 && normalIndex < normalCount());
    auto& normal = _normal[normalIndex];
    normal[0]    = x;
    normal[1]    = y;
    normal[2]    = z;
  }

  void setNormal(const int32_t normalIndex, const Vec3<T>& pt) {
    assert(normalIndex >= 0 && normalIndex < normalCount());
    _normal[normalIndex] = pt;
  }

  void setFaceId(const int32_t faceIdIndex, const T val) {
    assert(faceIdIndex >= 0 && faceIdIndex < faceIdCount());
    _faceId[faceIdIndex] = val;
  }
  void setMaterialIdx(const int32_t triangleIndex, const T val) {
    assert(triangleIndex >= 0 && triangleIndex < materialIdxCount());
    _materialIdx[triangleIndex] = val;
  }

  void setValence(const int32_t valenceIndex, const int val) {
    assert(valenceIndex >= 0 && valenceIndex < faceIdCount());
    _valence[valenceIndex] = val;
  }

  void addDisplacement(const Vec3<T>& disp) { _disp.push_back(disp); }
  void addDisplacement(const T dx, const T dy, const T dz) {
    _disp.push_back(Vec3<T>(dx, dy, dz));
  }

  void addPoint(const Vec3<T>& pt) { _coord.push_back(pt); }
  void addPoint(const T x, const T y, const T z) {
    _coord.push_back(Vec3<T>(x, y, z));
  }

  void addColour(const Vec3<T>& rgb) { _colour.push_back(rgb); }
  void addColour(const T r, const T g, const T b) {
    _colour.push_back(Vec3<T>(r, g, b));
  }

   void addTracktri(const std::vector<int32_t>& tt) { _tracktri.push_back(tt); }
  void addModifytri(const std::vector<int32_t>& mt) {
    _modifytri.push_back(mt);
  }

  void addTexCoord(const Vec2<T>& pt) { _texCoord.push_back(pt); }
  void addTexCoord(const T u, const T v) {
    _texCoord.push_back(Vec2<T>(u, v));
  }

  void addNormal(const Vec3<T>& pt) { _normal.push_back(pt); }
  void addNormal(const T x, const T y, const T z) {
    _normal.push_back(Vec3<T>(x, y, z));
  }

  void addFaceId(const int32_t val) { _faceId.push_back(val); }
  void addMaterialIdx(const int32_t val) { _materialIdx.push_back(val); }

  void addTriangle(const Triangle& tri) { _coordIndex.push_back(tri); }
  void addTriangle(const int32_t i, const int32_t j, const int32_t k) {
    _coordIndex.emplace_back(i, j, k);
  }

  void addTexCoordTriangle(const Triangle& tri) {
    _texCoordIndex.push_back(tri);
  }

  void addTexCoordTriangle(const int32_t i, const int32_t j, const int32_t k) {
    _texCoordIndex.emplace_back(i, j, k);
  }

  void addNormalTriangle(const Triangle& tri) { _normalIndex.push_back(tri); }
  void addNormalTriangle(const int32_t i, const int32_t j, const int32_t k) {
    _normalIndex.emplace_back(i, j, k);
  }

  void setTriangle(const int32_t triangleIndex,
                   const int32_t i,
                   const int32_t j,
                   const int32_t k) {
    assert(triangleIndex >= 0 && triangleIndex < triangleCount());
    auto& tri = _coordIndex[triangleIndex];
    tri[0]    = i;
    tri[1]    = j;
    tri[2]    = k;
  }

  void setTriangle(const int32_t triangleIndex, const Triangle& tri) {
    assert(triangleIndex >= 0 && triangleIndex < triangleCount());
    _coordIndex[triangleIndex] = tri;
  }

  void setTracktri(const int32_t               pointIndex,
                   const std::vector<int32_t>& track) {
    assert(pointIndex >= 0 && pointIndex < tracktriCount());
    for (int32_t t : track) _tracktri[pointIndex].push_back(t);
  }

  void setModifytri(const int32_t               pointIndex,
                    const std::vector<int32_t>& modify) {
    assert(pointIndex >= 0 && pointIndex < modifytriCount());
    for (int32_t t : modify) _modifytri[pointIndex].push_back(t);
  }

  void setTexCoordTriangle(const int32_t triangleIndex,
                           const int32_t i,
                           const int32_t j,
                           const int32_t k) {
    assert(triangleIndex >= 0 && triangleIndex < texCoordTriangleCount());
    auto& tri = _texCoordIndex[triangleIndex];
    tri[0]    = i;
    tri[1]    = j;
    tri[2]    = k;
  }

  void setTexCoordTriangle(const int32_t triangleIndex, const Triangle& tri) {
    assert(triangleIndex >= 0 && triangleIndex < texCoordTriangleCount());
    _texCoordIndex[triangleIndex] = tri;
  }

  void setNormalTriangle(const int32_t triangleIndex,
                         const int32_t i,
                         const int32_t j,
                         const int32_t k) {
    assert(triangleIndex >= 0 && triangleIndex < normalTriangleCount());
    auto& tri = _normalIndex[triangleIndex];
    tri[0]    = i;
    tri[1]    = j;
    tri[2]    = k;
  }

  void setNormalTriangle(const int32_t triangleIndex, const Triangle& tri) {
    assert(triangleIndex >= 0 && triangleIndex < normalTriangleCount());
    _normalIndex[triangleIndex] = tri;
  }

  void resizeDisplacements(const int32_t ptCount) {
    _disp.resize(ptCount, 0.0);
  }

  void resizePoints(const int32_t ptCount) { _coord.resize(ptCount, 0.0); }

  void resizeColours(const int32_t colourCount) {
    _colour.resize(colourCount, 0.0);
  }

  void resizeTexCoords(const int32_t texCoordCount) {
    _texCoord.resize(texCoordCount, 0.0);
  }

  void resizeNormals(const int32_t normalCount) {
    _normal.resize(normalCount, 0.0);
  }

  void resizeFaceIds(const int32_t faceIdCount) {
    _faceId.resize(faceIdCount, -1);
  }
  void resizeMaterialsIdxs(const int32_t materialIdxCount) {
    _materialIdx.resize(materialIdxCount, -1);
  }

  void resizeTriangles(const int32_t triCount) {
    _coordIndex.resize(triCount, -1);
  }

  void resizeTexCoordTriangles(const int32_t texCoordTriangleCount) {
    _texCoordIndex.resize(texCoordTriangleCount, -1);
  }

  void resizeNormalTriangles(const int32_t normalTriangleCount) {
    _normalIndex.resize(normalTriangleCount, -1);
  }

  void resizeValences(const int32_t valenceCount) {
    _valence.resize(valenceCount, 0);
  }

    void resizeTracks(const int32_t tracksCount) {
    _track.resize(tracksCount, -1);
  }

  void resizeTracktris(const int32_t trackCount) {
    _tracktri.resize(trackCount, std::vector<int32_t>());
  }

  void resizeModifytris(const int32_t trackCount) {
    _modifytri.resize(trackCount, std::vector<int32_t>());
  }

  void reserveDisplacements(const int32_t ptCount) { _disp.reserve(ptCount); }
  void reservePoints(const int32_t ptCount) { _coord.reserve(ptCount); }
  void reserveColours(const int32_t colourCount) {
    _colour.reserve(colourCount);
  }
  void reserveTriangles(const int32_t triCount) {
    _coordIndex.reserve(triCount);
  }

  void reserveTexCoords(const int32_t texCoordCount) {
    _texCoord.reserve(texCoordCount);
  }

  void reserveTexCoordTriangles(const int32_t texCoordTriangleCount) {
    _texCoordIndex.reserve(texCoordTriangleCount);
  }

  void reserveNormals(const int32_t normalCount) {
    _normal.reserve(normalCount);
  }

  void reserveNormalTriangles(const int32_t normalTriangleCount) {
    _normalIndex.reserve(normalTriangleCount);
  }

  void reserveFaceIds(const int32_t faceIdCount) {
    _faceId.reserve(faceIdCount);
  }

  void reserveValences(const int32_t valenceCount) {
    _valence.reserve(valenceCount);
  }

  void clear() {
    _disp.clear();
    _coord.clear();
    _colour.clear();
    _coordIndex.clear();
    _texCoord.clear();
    _texCoordIndex.clear();
    _track.clear();
    _normal.clear();
    _normalIndex.clear();
    _faceId.clear();
    _materialIdx.clear();
    _materialNames.clear();
    _textureMapUrls.clear();
    _valence.clear();
    _tracktri.clear();
    _modifytri.clear();
  }

  void append(const TriangleMesh<T>& mesh);

  void subdivideMesh(
    int32_t                            iterationCount,
    const int32_t                      edgeLengthThreshold          = 0,
    const int32_t                      bitshiftEdgeBasedSubdivision = 0,
    std::vector<SubdivisionMethod>*    subdivisionMethod            = nullptr,
    std::vector<SubdivisionLevelInfo>* infoLevelOfDetails           = nullptr,
    std::vector<int64_t>*              coordEdges                   = nullptr,
    std::vector<int64_t>*              texCoordEdges                = nullptr,
    std::vector<int32_t>*              triangleToBaseMeshTriangle   = nullptr,
    int*                               baseEdgesCount               = nullptr,
    int*                               baseTexEdgesCount            = nullptr,
    std::vector<Vec3<int32_t>>*        triangleBaseEdge             = nullptr,
    std::vector<Vec3<int32_t>>*        texTriangleBaseEdge          = nullptr,
    std::vector<int32_t>*              submeshFrameLodMap           = nullptr,
    std::vector<int32_t>*              submeshFrameChildToParentMap = nullptr,
    std::vector<TriangleMesh<T>>*      refMesh                      = nullptr);

  void
  computeTexCoordToPointMapping(std::vector<int32_t>& texCoordToPoint) const {
    const auto uvCount = texCoordCount();
    texCoordToPoint.resize(uvCount);
    std::fill(texCoordToPoint.begin(), texCoordToPoint.end(), -1);
    for (int32_t t = 0, tcount = triangleCount(); t < tcount; ++t) {
      const auto& tri   = triangle(t);
      const auto& triUV = texCoordTriangle(t);
      for (int32_t k = 0; k < 3; ++k) { texCoordToPoint[triUV[k]] = tri[k]; }
    }
  }
  template<typename D>
  void convert(const TriangleMesh<D>& src) {
    clear();

    _mtllib         = src.materialLibrary();
    _materialNames  = src.materialNames();
    _textureMapUrls = src.textureMapUrls();

    _coordIndex.resize(src.triangles().size());
    for (int i = 0; i < src.triangles().size(); i++) {
      _coordIndex[i] = src.triangle(i);
    }

    _texCoordIndex.resize(src.texCoordTriangles().size());
    for (int i = 0; i < src.texCoordTriangles().size(); i++) {
      _texCoordIndex[i] = src.texCoordTriangle(i);
    }

    _normalIndex.resize(src.normalTriangles().size());
    for (int i = 0; i < src.normalTriangles().size(); i++) {
      _normalIndex[i] = src.normalTriangle(i);
    }

    _disp.resize(src.displacements().size());
    for (int i = 0; i < src.displacements().size(); i++) {
      _disp[i] = (Vec3<T>)src.displacement(i);
    }

    _coord.resize(src.points().size());
    for (int i = 0; i < src.points().size(); i++) {
      _coord[i] = (Vec3<T>)src.point(i);
    }

    _colour.resize(src.colours().size());
    for (int i = 0; i < src.colours().size(); i++) {
      _colour[i] = (Vec3<T>)src.colour(i);
    }

    _texCoord.resize(src.texCoords().size());
    for (int i = 0; i < src.texCoords().size(); i++) {
      _texCoord[i] = (Vec2<T>)src.texCoord(i);
    }

    _normal.resize(src.normals().size());
    for (int i = 0; i < src.normals().size(); i++) {
      _normal[i] = (Vec3<T>)src.normal(i);
    }

    _faceId.resize(src.faceIds().size());
    for (int i = 0; i < src.faceIds().size(); i++) {
      _faceId[i] = src.faceId(i);
    }

    _materialIdx.resize(src.materialIdxs().size());
    for (int i = 0; i < src.materialIdxs().size(); i++) {
      _materialIdx[i] = src.materialIdx(i);
    }

    _valence.resize(src.valences().size());
    for (int i = 0; i < src.valences().size(); i++) {
      _valence[i] = src.valence(i);
    }

    _track.resize(src.tracks().size());
    for (int i = 0; i < src.tracks().size(); i++) { _track[i] = src.track(i); }

    _tracktri.resize(src.tracktris().size());
    for (int i = 0; i < src.tracktris().size(); i++) {
      _tracktri[i] = src.tracktri(i);
    }

    _modifytri.resize(src.modifytris().size());
    for (int i = 0; i < src.modifytris().size(); i++) {
      _modifytri[i] = src.modifytri(i);
    }
  }

  template<typename D>
  void convert1(const TriangleMesh<D>& src) {
    _coordIndex.resize(src.triangles().size());
    for (int i = 0; i < src.triangles().size(); i++) {
      _coordIndex[i] = src.triangle(i);
    }
    _coord.resize(src.points().size());
    for (int i = 0; i < src.points().size(); i++) {
      _coord[i] = (Vec3<T>)src.point(i);
    }
  }
  vmesh::TriangleMesh<MeshType>
  SelectByIndexPoint(const std::vector<int32_t>& index) {
    vmesh::TriangleMesh<MeshType> output;
    output.clear();
    output.setMaterialLibrary(materialLibrary());

    for (int32_t i : index) { output.addPoint(_coord[i]); }

    return output;
  }

  inline void print(std::string str, const int32_t logFaces = 0) const {
    printf("#####\n");
    printf("## %s \n", str.c_str());
    printf("## Coord:        %-6d \n", pointCount());
    printf("## Colour:       %-6d \n", colourCount());
    printf("## Normals:      %-6d \n", normalCount());
    printf("## TexCoord:     %-6d \n", texCoordCount());
    printf("## Disp:         %-6d \n", displacementCount());
    printf("## FaceId:       %-6d \n", faceIdCount());
    printf("## MaterialIdx:  %-6d \n", materialIdxCount());
    printf("## Triangles:    %-6d \n", triangleCount());
    printf("## TexTriangles: %-6d \n", texCoordTriangleCount());
    printf("## NrmTriangles: %-6d \n", normalTriangleCount());
    printf("## MTL:          %s \n", _mtllib.c_str());
    if (logFaces > 0) {
      printf("## Faces: \n");
      for (size_t i = 0; i < triangleCount(); i += logFaces) {
        printf(
          "## Tri %6zu: "
          "XYZ: (%9.5f %9.5f %9.5f)(%9.5f %9.5f %9.5f)(%9.5f %9.5f %9.5f) "
          "UV: (%9.5f %9.5f)(%9.5f %9.5f)(%9.5f %9.5f) ",
          "FaceId: (%d) ",
          "MaterialIdx: (%d)\n",
          i,
          (float)_coord[_coordIndex[i][0]][0],
          (float)_coord[_coordIndex[i][0]][1],
          (float)_coord[_coordIndex[i][0]][2],
          (float)_coord[_coordIndex[i][1]][0],
          (float)_coord[_coordIndex[i][1]][1],
          (float)_coord[_coordIndex[i][1]][2],
          (float)_coord[_coordIndex[i][2]][0],
          (float)_coord[_coordIndex[i][2]][1],
          (float)_coord[_coordIndex[i][2]][2],
          (float)_texCoord[_texCoordIndex[i][0]][0],
          (float)_texCoord[_texCoordIndex[i][0]][1],
          (float)_texCoord[_texCoordIndex[i][1]][0],
          (float)_texCoord[_texCoordIndex[i][1]][1],
          (float)_texCoord[_texCoordIndex[i][2]][0],
          (float)_texCoord[_texCoordIndex[i][2]][1],
          _faceId[i],
          _materialIdx[i]);
      }
    }
    printf("#####\n");
  }

  bool load(const std::string& fileName, const int32_t frameIndex);
  bool load(const std::string& fileName);
  bool save(const std::string& fileName, const bool binary = true) const;
  bool saveToOBJUsingFidAsColor(const std::string& fileName);

private:
  bool loadMaterials(const std::string&        fileName,
                     std::vector<std::string>& materialNames,
                     std::vector<std::string>& textureMapUrls);
  bool loadFromOBJ(const std::string& fileName);
  bool loadFromPLY(const std::string& fileName);
  bool loadFromVMB(const std::string& fileName);
  bool saveToOBJ(const std::string& fileName) const;
  bool saveToPLY(const std::string& fileName, const bool binary = true) const;
  bool saveToVMB(const std::string& fileName) const;

  std::vector<Vec3<T>>   _disp;
  std::vector<Vec3<T>>   _coord;
  std::vector<Vec3<T>>   _colour;
  std::vector<Vec3<int>> _coordIndex;
  std::vector<Vec2<T>>   _texCoord;
  std::vector<Vec3<int>> _texCoordIndex;
  std::vector<Vec3<T>>   _normal;
  std::vector<Vec3<int>> _normalIndex;
  std::vector<int>       _faceId;
  std::string            _mtllib;
  std::vector<int>       _materialIdx;  // material index for the triangle
  std::vector<std::string>
    _materialNames;  // list of material names from .mtl file
  std::vector<std::string>
    _textureMapUrls;  // list of texture urls from the optional material file(s)
  std::vector<int> _valence;
  std::vector<int>                  _track;
  std::vector<std::vector<int32_t>> _tracktri;
  std::vector<std::vector<int32_t>> _modifytri;
};

//============================================================================

template<typename T>
class ConnectedComponent : public TriangleMesh<T> {
public:
  std::vector<double> fracFramePackingScale(double val) {
    int    _iteration = 0;
    int    _bitDepth  = 40;
    double _fs        = val;
    while (_fs < std::pow(2, _bitDepth - 1)) {
      _fs *= 2;
      _iteration++;
    }
    _fs = (double)(int64_t)(_fs + 0.5);

    std::vector<double> _frac;
    _frac.push_back(_fs);
    _frac.push_back((double)_iteration);
    return _frac;
  }
  void setFaceId(int val) { faceId = val; }
  int  getFaceId() { return faceId; }
  void setProjection(int val) { projection = val; }
  int  getProjection() { return projection; }
  void setOrientation(int val) { orientation = val; }
  int  getOrientation() { return orientation; }
  void setU0(int val) { U0 = val; }
  int  getU0() { return U0; }
  void setV0(int val) { V0 = val; }
  int  getV0() { return V0; }
  void setU1(int val) { U1 = val; }
  int  getU1() { return U1; }
  void setV1(int val) { V1 = val; }
  int  getV1() { return V1; }
  void setSizeU(int val) { sizeU = val; }
  int  getSizeU() { return sizeU; }
  void setSizeV(int val) { sizeV = val; }
  int  getSizeV() { return sizeV; }
  void setScaleFrac(std::vector<double> frac) {
    upscale   = frac[0];
    downscale = frac[1];
    scale     = upscale / std::pow(2, downscale);
  }
  void setFrameScaleFrac(std::vector<double> frac) {
    frameUpscale   = frac[0];
    frameDownscale = frac[1];
    frameScale     = frameUpscale / std::pow(2, frameDownscale);
  }
  double       getFrameUpscale() { return frameUpscale; }
  double       getFrameDownscale() { return frameDownscale; }
  void         setScale(double val) { scale = val; }
  double       getScale() { return scale; }
  void         setFrameScale(double val) { frameScale = val; }
  double       getFrameScale() { return frameScale; }
  void         setIdxPatch(int val) { idxPatch = val; }
  int          getIdxPatch() { return idxPatch; }
  void         setRefPatch(int val) { refPatch = val; }
  int          getRefPatch() { return refPatch; }
  Vec2<double> convert(const Vec3<double>& coord,
                       int                 width,
                       int                 height,
                       double              gutter,
                       int                 occupancyResolution) {
    Vec2<double> retVal(0.0);
    Vec2<double> bias(0.0);
    double       maxU(0.0);
    // projection matrix
    int tangentAxis, biTangentAxis, projAxis;
    int tangentAxisDir, biTangentAxisDir, projAxisDir;
    getProjectedAxis(projection,
                     tangentAxis,
                     biTangentAxis,
                     projAxis,
                     tangentAxisDir,
                     biTangentAxisDir,
                     projAxisDir);
    retVal = projectPoint(coord, projection);
    //remove bbox y-bias
    retVal[1] -= V1;
    // invert u-axis to keep same winding and remove x-bias
    if ((projection == 0) || (projection == 4) || (projection == 5)
        || (projection == 6) || (projection == 9) || (projection == 11)
        || (projection == 12) || (projection == 14) || (projection == 15)) {
      retVal[0] = U1 - retVal[0];
    } else retVal[0] -= U1;
    // scale
    retVal *= scale;
    // gutter
    retVal += Vec2<double>(gutter, gutter);
    // rotation
    double uu = retVal[0];
    double vv = retVal[1];
    switch (orientation) {
    case 0:  // no rotation
      break;
    case 1:  // 90 degree
      retVal[1] = uu;
      retVal[0] = (sizeV * occupancyResolution - vv);
      break;
    case 2:  // 180 degrees
      retVal[1] = (sizeV * occupancyResolution - vv);
      retVal[0] = (sizeU * occupancyResolution - uu);
      break;
    case 3:  // 270 degrees
      retVal[1] = (sizeU * occupancyResolution - uu);
      retVal[0] = vv;
      break;
    default:  // no rotation
      retVal[1] = vv;
      retVal[0] = uu;
      break;
    }
    // translation
    retVal += Vec2<double>(U0 * occupancyResolution, V0 * occupancyResolution);
    // now turn the coordinates into the [0,1] range
    retVal[0] /= width;
    retVal[1] /= height;
    return retVal;
  }

  void setNumRawUVMinus1(int val) { numRawUVMinus1 = val; }
  void setUcoord(int idx, float val) {
    if (idx >= uCoords.size()) uCoords.resize(idx + 1);
    uCoords[idx] = val;
  }
  void setVcoord(int idx, float val) {
    if (idx >= vCoords.size()) vCoords.resize(idx + 1);
    vCoords[idx] = val;
  }
  void setUcoordQuant(int idx, float val) {
    if (idx >= uCoordsQuant.size()) uCoordsQuant.resize(idx + 1);
    uCoordsQuant[idx] = val;
  }
  void setVcoordQuant(int idx, float val) {
    if (idx >= vCoordsQuant.size()) vCoordsQuant.resize(idx + 1);
    vCoordsQuant[idx] = val;
  }
  auto  getNumRawUVMinus1() const { return numRawUVMinus1; }
  auto  getUcoord(int idx) const { return uCoords[idx]; }
  auto  getVcoord(int idx) const { return vCoords[idx]; }
  auto& getUcoords() { return uCoords; }
  auto& getVcoords() { return vCoords; }
  auto  getUcoordQuant(int idx) const { return uCoordsQuant[idx]; }
  auto  getVcoordQuant(int idx) const { return vCoordsQuant[idx]; }
  auto& getUcoordsQuant() { return uCoordsQuant; }
  auto& getVcoordsQuant() { return vCoordsQuant; }

private:
  // variable related to orthographic texture mapping
  int faceId;
  int projection =
    -1;  // Set the default value to -1 to avoid raw subpatches being used as a reference to inter subpatches;
  int orientation =
    -1;  // Set the default value to -1 to avoid raw subpatches being used as a reference to inter subpatches;
  int    U0;
  int    V0;
  int    U1;
  int    V1;
  int    sizeU;
  int    sizeV;
  double patchScale;
  double upscale;
  double downscale;
  double frameUpscale;
  double frameDownscale;
  double frameScale;
  double scale;
  int    idxPatch;
  int    refPatch;
  // variable related to raw subpatch
  size_t             numRawUVMinus1;
  std::vector<float> uCoords;
  std::vector<float> vCoords;
  std::vector<float> uCoordsQuant;
  std::vector<float> vCoordsQuant;
};

//============================================================================

template<typename T>
class MeshSequence {
public:
  MeshSequence(int f = 0) { resize(f); }
  MeshSequence(const MeshSequence&) = default;
  template<typename D>
  MeshSequence(const MeshSequence<D>& src) {
    *this = src;
  }
  MeshSequence& operator=(const MeshSequence&) = default;
  template<typename D>
  MeshSequence& operator=(const MeshSequence<D>& src) {
    resize(src.frameCount());
    for (int i = 0; i < src.frameCount(); i++) { _frames[i] = src.frame(i); }
    return *this;
  }
  ~MeshSequence() = default;

  bool load(const std::string& path,
            const int32_t      frameStart,
            const int32_t      frameCount) {
    printf("Loading a sequence of meshes from %4d to %4d \n",
           frameStart,
           frameStart + frameCount - 1);
    resize(frameCount);
    auto frameIndex = frameStart;
    for (auto& frame : _frames) {
      const auto name = vmesh::expandNum(path, frameIndex);
      if (!frame.load(name)) {
        printf("Error loading mesh %d: %s \n", frameIndex, name.c_str());
        return false;
      }
      frameIndex++;
    }
    return true;
  }

  bool save(const std::string& path, int32_t frameStart) {
    printf("Saving a sequence of meshes from %4d to %4zu \n",
           frameStart,
           frameStart + _frames.size() - 1);
    auto frameIndex = frameStart;
    for (auto& frame : _frames) {
      const auto name = vmesh::expandNum(path, frameIndex);
      if (!frame.save(name)) {
        printf("Error saving mesh %d: %s \n", frameIndex, name.c_str());
        return false;
      }
      frameIndex++;
    }
    return true;
  }

  TriangleMesh<T>& operator[](int frameIndex) { return _frames[frameIndex]; }
  const TriangleMesh<T>& operator[](int frameIndex) const {
    return _frames[frameIndex];
  }
  typename std::vector<TriangleMesh<T>>::iterator begin() {
    return _frames.begin();
  }
  typename std::vector<TriangleMesh<T>>::iterator end() {
    return _frames.end();
  }

  void resize(int f) { _frames.resize(f); }
  void clear() {
    for (auto& frame : _frames) { frame.clear(); }
    _frames.clear();
  }

  TriangleMesh<T>& frame(int frameIndex) {
    assert(frameIndex < frameCount());
    return _frames[frameIndex];
  }
  const TriangleMesh<T>& frame(int frameIndex) const {
    assert(frameIndex < frameCount());
    return _frames[frameIndex];
  }
  int frameCount() const { return int(_frames.size()); }

private:
  std::vector<TriangleMesh<T>> _frames;
};

//============================================================================

inline void
ComputeVertexToTriangle(
  const std::vector<Triangle>&         triangles,
  const int32_t                        vertexCount,
  StaticAdjacencyInformation<int32_t>& vertexToTriangle) {
  const auto triangleCount = int32_t(triangles.size());
  vertexToTriangle.resize(vertexCount);
  for (int32_t triangleIndex = 0; triangleIndex < triangleCount;
       ++triangleIndex) {
    const auto& tri = triangles[triangleIndex];
    assert(tri.i() >= 0 && tri.i() < vertexCount);
    assert(tri.j() >= 0 && tri.j() < vertexCount);
    assert(tri.k() >= 0 && tri.k() < vertexCount);
    vertexToTriangle.incrementNeighbourCount(tri.i());

    if (tri.j() != tri.i()) {  // check for degenerated case
      vertexToTriangle.incrementNeighbourCount(tri.j());
    }

    if (tri.k() != tri.i()
        && tri.k() != tri.j()) {  // check for degenerated case
      vertexToTriangle.incrementNeighbourCount(tri.k());
    }
  }
  vertexToTriangle.updateShift();

  for (int32_t triangleIndex = 0; triangleIndex < triangleCount;
       ++triangleIndex) {
    const auto& tri = triangles[triangleIndex];
    vertexToTriangle.addNeighbour(tri.i(), triangleIndex);
    if (tri.j() != tri.i()) {  // check for degenerated case
      vertexToTriangle.addNeighbour(tri.j(), triangleIndex);
    }

    if (tri.k() != tri.i()
        && tri.k() != tri.j()) {  // check for degenerated case
      vertexToTriangle.addNeighbour(tri.k(), triangleIndex);
    }
  }
}

//----------------------------------------------------------------------------

inline void
TagAdjacentTriangles(
  int32_t                                    vindex,
  const int8_t                               tag,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags) {
  assert(vindex < vertexToTriangle.size());
  const auto& tadj  = vertexToTriangle.neighbours();
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex);
  for (int i = start; i < end; ++i) { ttags[tadj[i]] = tag; }
}

//----------------------------------------------------------------------------

inline void
IncrementTagAdjacentTriangles(
  int32_t                                    vindex,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags) {
  assert(vindex < vertexToTriangle.size());
  const auto& tadj  = vertexToTriangle.neighbours();
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex);
  for (int i = start; i < end; ++i) { ++ttags[tadj[i]]; }
}

//----------------------------------------------------------------------------

inline int32_t
ComputeAdjacentVertexCount(
  int32_t                                    vindex,
  const std::vector<Triangle>&               triangles,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       vtags) {
  int32_t vcount = 0;
  assert(vindex < vertexToTriangle.size());
  const auto& tadj  = vertexToTriangle.neighbours();
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex);
  for (int i = start; i < end; ++i) {
    const auto& triangle = triangles[tadj[i]];
    vtags[triangle[0]]   = int8_t(1);
    vtags[triangle[1]]   = int8_t(1);
    vtags[triangle[2]]   = int8_t(1);
  }
  vtags[vindex] = 0;
  for (int i = start; i < end; ++i) {
    const auto& triangle = triangles[tadj[i]];
    for (int j = 0; j < 3; ++j) {
      const auto index = triangle[j];
      if (vtags[index] != 0) {
        ++vcount;
        vtags[index] = int8_t(0);
      }
    }
  }
  return vcount;
}

//----------------------------------------------------------------------------

inline void
ComputeAdjacentVertices(
  int32_t                                    vindex,
  const std::vector<Triangle>&               triangles,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       vtags,
  std::vector<int32_t>&                      vadj) {
  vadj.resize(0);
  assert(vindex < vertexToTriangle.size());
  const auto& tadj  = vertexToTriangle.neighbours();
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex);
  for (int i = start; i < end; ++i) {
    const auto& triangle = triangles[tadj[i]];
    vtags[triangle[0]]   = int8_t(1);
    vtags[triangle[1]]   = int8_t(1);
    vtags[triangle[2]]   = int8_t(1);
  }
  vtags[vindex] = 0;
  for (int i = start; i < end; ++i) {
    const auto& triangle = triangles[tadj[i]];
    for (int j = 0; j < 3; ++j) {
      const auto index = triangle[j];
      if (vtags[index] != 0) {
        vadj.push_back(index);
        vtags[index] = int8_t(0);
      }
    }
  }
}

//----------------------------------------------------------------------------

inline int32_t
ComputeAdjacentTrianglesCount(
  const Triangle&                            triangle,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags) {
  int32_t tcount = 0;
  TagAdjacentTriangles(triangle[0], 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(triangle[1], 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(triangle[2], 0, vertexToTriangle, ttags);
  const auto& tadj = vertexToTriangle.neighbours();
  for (int32_t k = 0; k < 3; ++k) {
    const auto vindex = triangle[k];
    assert(vindex < vertexToTriangle.size());
    const auto start = vertexToTriangle.neighboursStartIndex(vindex);
    const auto end   = vertexToTriangle.neighboursEndIndex(vindex);
    for (int i = start; i < end; ++i) {
      const auto tindex = tadj[i];
      if (ttags[tindex] == 0) {
        ttags[tindex] = int8_t(1);
        tcount++;
      }
    }
  }
  return tcount;
}

//----------------------------------------------------------------------------

inline void
ComputeAdjacentTriangles(
  const Triangle&                            triangle,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags,
  std::vector<int32_t>&                      adj) {
  adj.resize(0);
  TagAdjacentTriangles(triangle[0], 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(triangle[1], 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(triangle[2], 0, vertexToTriangle, ttags);
  const auto& tadj = vertexToTriangle.neighbours();
  for (int32_t k = 0; k < 3; ++k) {
    const auto vindex = triangle[k];
    assert(vindex < vertexToTriangle.size());
    const auto start = vertexToTriangle.neighboursStartIndex(vindex);
    const auto end   = vertexToTriangle.neighboursEndIndex(vindex);
    for (int i = start; i < end; ++i) {
      const auto tindex = tadj[i];
      if (ttags[tindex] == 0) {
        ttags[tindex] = int8_t(1);
        adj.push_back(tindex);
      }
    }
  }
}

//----------------------------------------------------------------------------

inline void
ComputeEdgeAdjacentTriangles(
  int32_t                                    vindex0,
  int32_t                                    vindex1,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags,
  std::vector<int32_t>&                      tadj) {
  tadj.resize(0);
  TagAdjacentTriangles(vindex0, 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(vindex1, 1, vertexToTriangle, ttags);
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex0);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex0);
  const auto& tadj0 = vertexToTriangle.neighbours();
  for (int i = start; i < end; ++i) {
    const auto tindex = tadj0[i];
    if (ttags[tindex] != 0) { tadj.push_back(tindex); }
  }
}

//----------------------------------------------------------------------------

inline int32_t
ComputeEdgeAdjacentTriangleCount(
  int32_t                                    vindex0,
  int32_t                                    vindex1,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       ttags) {
  TagAdjacentTriangles(vindex0, 0, vertexToTriangle, ttags);
  TagAdjacentTriangles(vindex1, 1, vertexToTriangle, ttags);
  const auto  start = vertexToTriangle.neighboursStartIndex(vindex0);
  const auto  end   = vertexToTriangle.neighboursEndIndex(vindex0);
  const auto& tadj0 = vertexToTriangle.neighbours();
  int32_t     count = 0;
  for (int i = start; i < end; ++i) {
    const auto tindex = tadj0[i];
    count += static_cast<int>(ttags[tindex] != 0);
  }
  return count;
}

//----------------------------------------------------------------------------

inline void
ComputeBoundaryVertices(
  const std::vector<Triangle>&               triangles,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int8_t>&                       vtags,
  std::vector<int8_t>&                       ttags,
  std::vector<int8_t>&                       isBoundaryVertex) {
  const auto vertexCount = int32_t(vertexToTriangle.size());
  if (vertexCount == 0) { return; }
  isBoundaryVertex.resize(vertexCount);
  std::fill(isBoundaryVertex.begin(), isBoundaryVertex.end(), int8_t(0));
  std::vector<int32_t> vadj;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    ComputeAdjacentVertices(vindex0, triangles, vertexToTriangle, vtags, vadj);
    for (int vindex1 : vadj) {
      if (vindex1 <= vindex0) { continue; }
      if (ComputeEdgeAdjacentTriangleCount(
            vindex0, vindex1, vertexToTriangle, ttags)
          == 1) {
        isBoundaryVertex[vindex0] = int8_t(1);
        isBoundaryVertex[vindex1] = int8_t(1);
      }
    }
  }
}

//----------------------------------------------------------------------------

inline void
collectAdjacentFaces(
  int32_t                                    vindex,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  const std::vector<int32_t>&                triToMeshTriRef,
  std::vector<int32_t>&                      vToTri) {
  const auto* neighbour  = vertexToTriangle.neighbours();
  const auto  neighStart = vertexToTriangle.neighboursStartIndex(vindex);
  const auto  neighEnd   = vertexToTriangle.neighboursEndIndex(vindex);
  for (int32_t i = neighStart; i < neighEnd; ++i) {
    const auto tindex = neighbour[i];
    const auto tri    = triToMeshTriRef[tindex];
    if (std::find(vToTri.begin(), vToTri.end(), tri) == vToTri.end()) {
      vToTri.push_back(tri);
    }
  }
}

//----------------------------------------------------------------------

template <typename T>
inline void add_unique(std::vector<T>& vec, const T& value) {
    if (std::find(vec.begin(), vec.end(), value) == vec.end()) {
        vec.push_back(value);
    }
}
//----------------------------------------------------------------------------

inline int64_t
EdgeIndex(int32_t i0, int32_t i1) {
  return (int64_t(std::min(i0, i1)) << 32) + std::max(i0, i1);
}

//----------------------------------------------------------------------------

inline void
transferVertexToEdgeInTriangles(
  std::vector<Triangle>&               triangles,
  StaticAdjacencyInformation<int32_t>& vertexToEdge,
  std::vector<int64_t>&                edges) {
  const auto triangleCount = int32_t(triangles.size());
  for (int t = 0; t < triangleCount; t++) {
    const auto tri = triangles[t];
    Triangle   tri0;
    for (int32_t k = 0; k < 3; ++k) {
      const auto  vindex0          = tri[k];
      const auto  vindex1          = tri[(k + 1) % 3];
      const auto  currentEdgeIndex = EdgeIndex(vindex0, vindex1);
      const auto& edgeIndexes      = vertexToEdge.neighbours();
      const auto  start = vertexToEdge.neighboursStartIndex(vindex0);
      const auto  end   = vertexToEdge.neighboursEndIndex(vindex0);
      for (int32_t n = start; n < end; ++n) {
        const auto eindex = edgeIndexes[n];
        if (edges[eindex] == currentEdgeIndex) {
          tri0[k] = eindex;
          break;
        }
      }
    }
    triangles[t] = tri0;
  }
}

//----------------------------------------------------------------------------

inline int32_t
edgeToVertexInTriangle(const Triangle        tri,
                       const int32_t         k,
                       std::vector<int64_t>& edges) {
  const auto edge0 = edges[tri[(k + 2) % 3]];
  const auto edge1 = edges[tri[k]];
  if ((edge0 >> 32) == (edge1 >> 32)
      || (edge0 >> 32) == (edge1 & 0xFFFFFFFF)) {
    return (int32_t)(edge0 >> 32);
  } else {
    return (int32_t)(edge0 & 0xFFFFFFFF);
  }
}

//----------------------------------------------------------------------------

inline void
initializeSubdiv(const int32_t                        vertexCount,
                 std::vector<Triangle>&               triangles,
                 bool                                 saveValences,
                 std::vector<int>&                    valences,
                 StaticAdjacencyInformation<int32_t>& vertexToTriangle,
                 StaticAdjacencyInformation<int32_t>& vertexToEdge,
                 std::vector<int8_t>&                 vtags,
                 std::vector<int64_t>&                edges) {
  ComputeVertexToTriangle(triangles, vertexCount, vertexToTriangle);

  vtags.resize(vertexCount);
  vertexToEdge.resize(vertexCount);
  if (saveValences) valences.resize(vertexCount);
  int32_t edgeCount = 0;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    const auto ncount =
      ComputeAdjacentVertexCount(vindex0, triangles, vertexToTriangle, vtags);
    vertexToEdge.incrementNeighbourCount(vindex0, ncount);
    edgeCount += ncount;
    if (saveValences) valences[vindex0] = ncount;
  }
  assert(!(edgeCount & 1));
  edgeCount /= 2;
  vertexToEdge.updateShift();
  // create edge list
  edges.resize(vertexCount + edgeCount);

  std::vector<int32_t> vadj;
  int32_t              vertexCounter = vertexCount;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    ComputeAdjacentVertices(vindex0, triangles, vertexToTriangle, vtags, vadj);
    for (const auto vindex1 : vadj) {
      if (vindex1 <= vindex0) { continue; }
      vertexToEdge.addNeighbour(vindex0, vertexCounter);
      vertexToEdge.addNeighbour(vindex1, vertexCounter);
      edges[vertexCounter] = (int64_t(vindex0) << 32) + vindex1;
      ++vertexCounter;
    }
  }
  transferVertexToEdgeInTriangles(triangles, vertexToEdge, edges);
}

//----------------------------------------------------------------------------

#define PACK_EDGE_INFO(_X_, _Y_) ((int64_t(_X_) << 32) + _Y_)
#define UNPACK_EDGE_INFO_1ST_ELEM(_X_) (_X_ >> 32)

//11.2.3.6 Update face information + 11.2.3.7 Update edge information
inline void
UpdateEdgeAndFace(std::vector<Triangle>& triangles,
                  int                    subdivisionIterationCount,
                  int                    iterationIndex,
                  int&                   prevPointCount,
                  std::vector<int64_t>&  edges,
                  std::vector<int32_t>*  triangleToBaseMeshTriangle,
                  std::vector<Triangle>& prevTriangles) {
  const auto   triangleCount = int32_t(triangles.size());
  const size_t pointCount = edges.size();
  const int    newPointSize =
    2 * (pointCount - prevPointCount) + 3 * triangleCount;

  const size_t newPointCount = pointCount + newPointSize;

  if (!prevTriangles.empty()) {
    prevTriangles.resize(triangles.size());
    std::copy(triangles.begin(), triangles.end(), prevTriangles.begin());
  }

  // update edges
  if (iterationIndex < subdivisionIterationCount - 1) {
    edges.resize(newPointCount);

    for (int32_t vindex = prevPointCount; vindex < pointCount; ++vindex) {
      int eindex = 2 * (vindex - prevPointCount) + pointCount;
      int vindex0 = edges[vindex] >> 32;
      int vindex1 = edges[vindex] & 0xFFFFFFFF;
      edges[eindex] = PACK_EDGE_INFO(vindex0, vindex);
      edges[eindex + 1] = PACK_EDGE_INFO(vindex1, vindex);
    }
    for (int32_t t = 0; t < triangleCount; ++t) {
      int eindex = 3 * t + 3 * pointCount - 2 * prevPointCount;
      for (int k = 0; k < 3; ++k) {
        int vindex0 = triangles[t][k];
        int vindex1 = triangles[t][(k + 1) % 3];
        int vmin = std::min(vindex0, vindex1);
        int vmax = std::max(vindex0, vindex1);
        edges[eindex + k] = PACK_EDGE_INFO(vmin, vmax);
      }
    }
  }

  // create triangles
  triangles.resize(triangleCount * 4);

  if (triangleToBaseMeshTriangle) {
    triangleToBaseMeshTriangle->resize(4 * triangleCount);
  }
  int32_t triangleCounter = triangleCount;
  for (int32_t t = 0; t < triangleCount; ++t) {
    const auto tri = triangles[t];
    if (iterationIndex < subdivisionIterationCount - 1) {
      Triangle tri0;
      int      subedges[3][2];
      for (int32_t k = 0; k < 3; ++k) {
        tri0[k] = 3 * (t - triangleCount) + k + newPointCount;
        int vindex = edgeToVertexInTriangle(tri, k, edges);
        int eindex0 = 2 * (tri[k] - prevPointCount) + pointCount;
        int eindex1 = eindex0 + 1;
        if (vindex != (int32_t)(edges[tri[k]] >> 32)) {
          std::swap(eindex0, eindex1);
        }
        subedges[k][0] = eindex0;
        subedges[k][1] = eindex1;
      }
      triangles[t] = tri0;
      triangles[triangleCounter++] =
        Triangle(subedges[0][0], tri0[2], subedges[2][1]);
      triangles[triangleCounter++] =
        Triangle(subedges[0][1], subedges[1][0], tri0[0]);
      triangles[triangleCounter++] =
        Triangle(tri0[1], subedges[1][1], subedges[2][0]);
    }
    else {
      triangles[triangleCounter++] =
        Triangle(edgeToVertexInTriangle(tri, 0, edges), tri[0], tri[2]);
      triangles[triangleCounter++] =
        Triangle(tri[0], edgeToVertexInTriangle(tri, 1, edges), tri[1]);
      triangles[triangleCounter++] =
        Triangle(tri[2], tri[1], edgeToVertexInTriangle(tri, 2, edges));
    }

    if (triangleToBaseMeshTriangle) {
      const auto tindexBaseMesh = (*triangleToBaseMeshTriangle)[t];
      (*triangleToBaseMeshTriangle)[triangleCounter - 3] = tindexBaseMesh;
      (*triangleToBaseMeshTriangle)[triangleCounter - 2] = tindexBaseMesh;
      (*triangleToBaseMeshTriangle)[triangleCounter - 1] = tindexBaseMesh;
    }
  }

  prevPointCount = pointCount;

  if (!prevTriangles.empty()) {
    const int originalSize = prevTriangles.size();
    prevTriangles.reserve(originalSize + triangleCount * 3);

    for (int32_t t = 0; t < triangleCount; ++t) {
      const auto& tri = prevTriangles[t];
      prevTriangles.push_back(
        Triangle(edgeToVertexInTriangle(tri, 0, edges), tri[0], tri[2]));
      prevTriangles.push_back(
        Triangle(tri[0], edgeToVertexInTriangle(tri, 1, edges), tri[1]));
      prevTriangles.push_back(
        Triangle(tri[2], tri[1], edgeToVertexInTriangle(tri, 2, edges)));
    }
  }
}

//----------------------------------------------------------------------------

template<typename T>
inline int32_t
distanceSquare(const Vec3<T>& vertex1, const Vec3<T>& vertex2, int32_t shift) {
  Vec3<int32_t> qv1, qv2;
  if (shift < 0) {
    shift *= -1;
    qv1 = Vec3<int32_t>(vertex1 / (1 << shift));
    qv2 = Vec3<int32_t>(vertex2 / (1 << shift));
  }
  else {
    qv1 = Vec3<int32_t>(vertex1 * (1 << shift));
    qv2 = Vec3<int32_t>(vertex2 * (1 << shift));
  }
  return (qv1 - qv2) * (qv1 - qv2);
}

template<typename T>
inline bool
edgeLengthCheck(const Vec3<T>& vertex1,
                const Vec3<T>& vertex2,
                int32_t        threshold,
                int32_t        shift) {
  int32_t distSquare = distanceSquare(vertex1, vertex2, shift);
  int32_t qThreshold = threshold;
  if (threshold > 0 && distSquare < 4 * qThreshold * qThreshold) {
    return false;
  }
  assert(distSquare >= 0);
  assert(4 * qThreshold * qThreshold >= 0);
  return true;
}

template<typename T>
inline void
initializeSubdivWithEdgeLengthThreshold(
  const int32_t                        vertexCount,
  const std::vector<Vec3<T>>&          points,
  std::vector<Triangle>&               triangles,
  bool                                 saveValences,
  std::vector<int>&                    valences,
  StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  StaticAdjacencyInformation<int32_t>& vertexToEdge,
  std::vector<int8_t>&                 vtags,
  std::vector<int64_t>&                edges,
  int32_t                              edgeThreshold,
  int32_t                              bitshiftEdgeBasedSubdivision,
  std::vector<int32_t>*                uvToVertex) {
  ComputeVertexToTriangle(triangles, vertexCount, vertexToTriangle);

  vtags.resize(vertexCount);
  vertexToEdge.resize(vertexCount);
  if (saveValences) valences.resize(vertexCount);
  int32_t edgeCount = 0;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    // const auto ncount =
    //   ComputeAdjacentVertexCount(vindex0, triangles, vertexToTriangle, vtags);
    int32_t ncount = 0;
    assert(vindex0 < vertexToTriangle.size());
    const auto& tadj  = vertexToTriangle.neighbours();
    const auto  start = vertexToTriangle.neighboursStartIndex(vindex0);
    const auto  end   = vertexToTriangle.neighboursEndIndex(vindex0);
    for (int i = start; i < end; ++i) {
      const auto& triangle = triangles[tadj[i]];
      for (int j = 0; j < 3; ++j) {
        auto geom1 =
          uvToVertex ? points[(*uvToVertex)[vindex0]] : points[vindex0];
        auto geom2 = uvToVertex ? points[(*uvToVertex)[triangle[j]]]
                                : points[triangle[j]];
        if (edgeThreshold == 0
            || edgeLengthCheck(
              geom1, geom2, edgeThreshold, bitshiftEdgeBasedSubdivision)) {
          vtags[triangle[j]] = int8_t(1);
        }
      }
    }
    vtags[vindex0] = 0;
    for (int i = start; i < end; ++i) {
      const auto& triangle = triangles[tadj[i]];
      for (int j = 0; j < 3; ++j) {
        const auto index = triangle[j];
        if (vtags[index] != 0) {
          ++ncount;
          vtags[index] = int8_t(0);
        }
      }
    }

    vertexToEdge.incrementNeighbourCount(vindex0, ncount);
    edgeCount += ncount;
    if (saveValences) valences[vindex0] = ncount;
  }
  assert(!(edgeCount & 1));
  edgeCount /= 2;
  vertexToEdge.updateShift();
  // create edge list
  edges.resize(vertexCount + edgeCount);

  std::vector<int32_t> vadj;
  int32_t              vertexCounter = vertexCount;
  for (int32_t vindex0 = 0; vindex0 < vertexCount; ++vindex0) {
    // ComputeAdjacentVertices(vindex0, triangles, vertexToTriangle, vtags, vadj);
    vadj.resize(0);
    assert(vindex0 < vertexToTriangle.size());
    const auto& tadj  = vertexToTriangle.neighbours();
    const auto  start = vertexToTriangle.neighboursStartIndex(vindex0);
    const auto  end   = vertexToTriangle.neighboursEndIndex(vindex0);
    for (int i = start; i < end; ++i) {
      const auto& triangle = triangles[tadj[i]];
      for (int j = 0; j < 3; ++j) {
        auto geom1 =
          uvToVertex ? points[(*uvToVertex)[vindex0]] : points[vindex0];
        auto geom2 = uvToVertex ? points[(*uvToVertex)[triangle[j]]]
                                : points[triangle[j]];
        if (edgeThreshold == 0
            || edgeLengthCheck(
              geom1, geom2, edgeThreshold, bitshiftEdgeBasedSubdivision)) {
          vtags[triangle[j]] = int8_t(1);
        }
      }
    }
    vtags[vindex0] = 0;
    for (int i = start; i < end; ++i) {
      const auto& triangle = triangles[tadj[i]];
      for (int j = 0; j < 3; ++j) {
        const auto index = triangle[j];
        if (vtags[index] != 0) {
          vadj.push_back(index);
          vtags[index] = int8_t(0);
        }
      }
    }

    for (const auto vindex1 : vadj) {
      if (vindex1 <= vindex0) { continue; }
      vertexToEdge.addNeighbour(vindex0, vertexCounter);
      vertexToEdge.addNeighbour(vindex1, vertexCounter);
      edges[vertexCounter] = (int64_t(vindex0) << 32) + vindex1;
      ++vertexCounter;
    }
  }
}

//11.2.3.8 Adaptive update face information + 11.2.3.9 Adaptive update edge information
template<typename T>
void
AdaptiveUpdateEdgeAndFace(std::vector<Triangle>& triangles,
                          int                    subdivisionIterationCount,
                          int32_t                edgeThreshold,
                          int32_t                bitshiftEdgeBasedSubdivision,
                          int                    iterationIndex,
                          int&                   prevPointCount,
                          std::vector<int64_t>&  edges,
                          std::vector<int32_t>*  triangleToBaseMeshTriangle,
                          std::vector<int32_t>*  uvToVertex,
                          std::vector<Vec3<T>>&  geoms,
                          StaticAdjacencyInformation<int32_t>& vertexToEdge) {
  int        pointCount    = edges.size();
  const auto triangleCount = int32_t(triangles.size());
  const auto r = T(1) / T(2);

  //update geometry info with Midpoint Subdivision
  if ((!uvToVertex) && (!geoms.empty() && geoms.size() < pointCount)) {
    geoms.resize(pointCount);
    for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
      int vindex0 = edges[vindex] >> 32;
      int vindex1 = edges[vindex] & 0xFFFFFFFF;
      geoms[vindex] = (geoms[vindex0] + geoms[vindex1]) * r;
    }
  }

  // compute next offset of triangle
  std::vector<int32_t> subdivPattern;
  std::vector<int32_t> splitEdge;
  std::vector<int32_t> nextOffsetEdge, nextOffsetTri;

  subdivPattern.resize(triangleCount);
  nextOffsetTri.resize(triangleCount + 1);

  //nextOffsetEdge[prevPointCount] = 0;
  //if (iterationIndex < subdivisionIterationCount - 1) {
  //  for (int32_t e = prevPointCount; e < pointCount; ++e) {
  //    auto vertex0 = edges[e] >> 32;
  //    auto vertex1 = edges[e] & 0xFFFFFFFF;
  //    Vec3<T> geom0 = uvToVertex ? geoms[(*uvToVertex)[vertex0]] : geoms[vertex0];
  //    Vec3<T> geom1 = uvToVertex ? geoms[(*uvToVertex)[vertex1]] : geoms[vertex1];
  //    Vec3<T> mid = uvToVertex ? geoms[(*uvToVertex)[edges[e]]] : geoms[edges[e]];
  //    splitEdge[e] = 0;
  //    splitEdge[e] += edgeLengthCheck(geom0, mid, edgeThreshold, bitshiftEdgeBasedSubdivision) ? 1 : 0;
  //    splitEdge[e] += edgeLengthCheck(geom1, mid, edgeThreshold, bitshiftEdgeBasedSubdivision) ? 1 : 0;
  //    nextOffsetEdge[e + 1] = nextOffsetEdge[e] + splitEdge[e];
  //  }
  //}
  nextOffsetTri[0] = 0;
  for (int32_t t = 0; t < triangleCount; ++t) {
    const auto tri   = triangles[t];
    subdivPattern[t] = 1;
    for (int k = 0; k < 3; ++k) {
      const auto  vindex0          = tri[k];
      const auto  vindex1          = tri[(k + 1) % 3];
      const auto  currentEdgeIndex = EdgeIndex(vindex0, vindex1);
      const auto& edgeIndexes      = vertexToEdge.neighbours();
      const auto  start = vertexToEdge.neighboursStartIndex(vindex0);
      const auto  end   = vertexToEdge.neighboursEndIndex(vindex0);
      for (int32_t n = start; n < end; ++n) {
        const auto eindex = edgeIndexes[n];
        if (edges[eindex] == currentEdgeIndex) {
          subdivPattern[t]++;
          break;
        }
      }
    }
    nextOffsetTri[t + 1] = nextOffsetTri[t] + subdivPattern[t];
  }

  // create triangles
  triangles.resize(nextOffsetTri[triangleCount]);

  if (triangleToBaseMeshTriangle) {
    triangleToBaseMeshTriangle->resize(nextOffsetTri[triangleCount]);
  }
  int32_t triangleCounter = triangleCount;
  for (int32_t t = 0; t < triangleCount; ++t) {
    const auto tri = triangles[t];
    Triangle   tri0(-1);
    for (int32_t k = 0; k < 3; ++k) {
      const auto  vindex0          = tri[k];
      const auto  vindex1          = tri[(k + 1) % 3];
      const auto  currentEdgeIndex = EdgeIndex(vindex0, vindex1);
      const auto& edgeIndexes      = vertexToEdge.neighbours();
      const auto  start = vertexToEdge.neighboursStartIndex(vindex0);
      const auto  end   = vertexToEdge.neighboursEndIndex(vindex0);
      for (int32_t n = start; n < end; ++n) {
        const auto eindex = edgeIndexes[n];
        if (edges[eindex] == currentEdgeIndex) {
          tri0[k] = eindex;
          break;
        }
      }
    }

    if (subdivPattern[t] == 2) {
      for (int32_t k = 0; k < 3; ++k) {
        if (tri0[k] >= 0) {
          triangles[t] = Triangle(tri0[k], tri[(k + 2) % 3], tri[k]);
          triangles[triangleCounter++] =
            Triangle(tri0[k], tri[(k + 1) % 3], tri[(k + 2) % 3]);
          break;
        }
      }
      if (triangleToBaseMeshTriangle) {
        const auto tindexBaseMesh = (*triangleToBaseMeshTriangle)[t];
        (*triangleToBaseMeshTriangle)[triangleCounter - 1] = tindexBaseMesh;
      }
    } else if (subdivPattern[t] == 3) {
      for (int32_t k = 0; k < 3; ++k) {
        if (tri0[k] >= 0 && tri0[(k + 1) % 3] >= 0) {
          T dist0, dist1;
          if (uvToVertex) {
            const auto r    = T(1) / T(2);
            auto       mid0 = (geoms[(*uvToVertex)[tri[k]]]
                         + geoms[(*uvToVertex)[tri[(k + 1) % 3]]])
                        * r;
            auto mid1 = (geoms[(*uvToVertex)[tri[(k + 1) % 3]]]
                         + geoms[(*uvToVertex)[tri[(k + 2) % 3]]])
                        * r;
            dist0 = distanceSquare(mid0,
                                   geoms[(*uvToVertex)[tri[(k + 2) % 3]]],
                                   bitshiftEdgeBasedSubdivision);
            dist1 = distanceSquare(mid1,
                                   geoms[(*uvToVertex)[tri[k]]],
                                   bitshiftEdgeBasedSubdivision);
          } else {
            const auto r    = T(1) / T(2);
            auto       mid0 = (geoms[tri[k]] + geoms[tri[(k + 1) % 3]]) * r;
            auto       mid1 =
              (geoms[tri[(k + 1) % 3]] + geoms[tri[(k + 2) % 3]]) * r;
            dist0 = distanceSquare(
              mid0, geoms[tri[(k + 2) % 3]], bitshiftEdgeBasedSubdivision);
            dist1 = distanceSquare(
              mid1, geoms[tri[k]], bitshiftEdgeBasedSubdivision);
          }
          if (dist0 <= dist1) {
            triangles[t] =
              Triangle(tri[(k + 2) % 3], tri0[k], tri0[(k + 1) % 3]);
            triangles[triangleCounter++] =
              Triangle(tri[k], tri0[k], tri[(k + 2) % 3]);
            triangles[triangleCounter++] =
              Triangle(tri0[k], tri[(k + 1) % 3], tri0[(k + 1) % 3]);
          } else {
            triangles[t] = Triangle(tri0[k], tri0[(k + 1) % 3], tri[k]);
            triangles[triangleCounter++] =
              Triangle(tri[k], tri0[(k + 1) % 3], tri[(k + 2) % 3]);
            triangles[triangleCounter++] =
              Triangle(tri0[k], tri[(k + 1) % 3], tri0[(k + 1) % 3]);
          }

          break;
        }
      }
      if (triangleToBaseMeshTriangle) {
        const auto tindexBaseMesh = (*triangleToBaseMeshTriangle)[t];
        (*triangleToBaseMeshTriangle)[triangleCounter - 2] = tindexBaseMesh;
        (*triangleToBaseMeshTriangle)[triangleCounter - 1] = tindexBaseMesh;
      }
    } else if (subdivPattern[t] == 4) {
      triangles[t]                 = tri0;
      triangles[triangleCounter++] = Triangle(tri[0], tri0[0], tri0[2]);
      triangles[triangleCounter++] = Triangle(tri0[0], tri[1], tri0[1]);
      triangles[triangleCounter++] = Triangle(tri0[2], tri0[1], tri[2]);

      if (triangleToBaseMeshTriangle) {
        const auto tindexBaseMesh = (*triangleToBaseMeshTriangle)[t];
        (*triangleToBaseMeshTriangle)[triangleCounter - 3] = tindexBaseMesh;
        (*triangleToBaseMeshTriangle)[triangleCounter - 2] = tindexBaseMesh;
        (*triangleToBaseMeshTriangle)[triangleCounter - 1] = tindexBaseMesh;
      }
    }
  }

  // update edges
  if (iterationIndex < subdivisionIterationCount - 1) {
    splitEdge.resize(2 * (pointCount - prevPointCount));
    nextOffsetEdge.resize(2 * (pointCount - prevPointCount) + 1);
    nextOffsetEdge[prevPointCount] = 0;
    for (int32_t e = prevPointCount; e < pointCount; ++e) {
      auto     vertex0 = edges[e] >> 32;
      auto     vertex1 = edges[e] & 0xFFFFFFFF;
      Vec3<T> geom0 =
        uvToVertex ? geoms[(*uvToVertex)[vertex0]] : geoms[vertex0];
      Vec3<T> geom1 =
        uvToVertex ? geoms[(*uvToVertex)[vertex1]] : geoms[vertex1];
      Vec3<T> mid = (geom0 + geom1) * r;
      splitEdge[2 * (e - prevPointCount)] =
        edgeLengthCheck(
          geom0, mid, edgeThreshold, bitshiftEdgeBasedSubdivision)
          ? 1
          : 0;
      splitEdge[2 * (e - prevPointCount) + 1] =
        edgeLengthCheck(
          geom1, mid, edgeThreshold, bitshiftEdgeBasedSubdivision)
          ? 1
          : 0;
      nextOffsetEdge[2 * (e - prevPointCount) + 1] =
        nextOffsetEdge[2 * (e - prevPointCount)]
        + splitEdge[2 * (e - prevPointCount)];
      nextOffsetEdge[2 * (e - prevPointCount) + 2] =
        nextOffsetEdge[2 * (e - prevPointCount) + 1]
        + splitEdge[2 * (e - prevPointCount) + 1];
    }

    std::vector<int32_t> tsplitEdge;
    std::vector<int32_t> tnextOffsetEdge;
    tsplitEdge.resize(nextOffsetTri[triangleCount] - triangleCount);
    tnextOffsetEdge.resize(nextOffsetTri[triangleCount] - triangleCount + 1);
    tnextOffsetEdge[0] = 0;
    for (int32_t t = 0; t < triangleCount; ++t) {
      for (int k = 0; k < subdivPattern[t] - 1; ++k) {
        int      vindex0 = triangles[t][k];
        int      vindex1 = triangles[t][(k + 1) % 3];
        Vec3<T> geom0, geom1;
        if (vindex0 < prevPointCount) {
          geom0 = uvToVertex ? geoms[(*uvToVertex)[vindex0]] : geoms[vindex0];
        } else {
          int  vindex00 = edges[vindex0] >> 32;
          int  vindex01 = edges[vindex0] & 0xFFFFFFFF;
          auto geom00 =
            uvToVertex ? geoms[(*uvToVertex)[vindex00]] : geoms[vindex00];
          auto geom01 =
            uvToVertex ? geoms[(*uvToVertex)[vindex01]] : geoms[vindex01];
          geom0 = (geom00 + geom01) * r;
        }
        if (vindex1 < prevPointCount) {
          geom1 = uvToVertex ? geoms[(*uvToVertex)[vindex1]] : geoms[vindex1];
        } else {
          int  vindex10 = edges[vindex1] >> 32;
          int  vindex11 = edges[vindex1] & 0xFFFFFFFF;
          auto geom10 =
            uvToVertex ? geoms[(*uvToVertex)[vindex10]] : geoms[vindex10];
          auto geom11 =
            uvToVertex ? geoms[(*uvToVertex)[vindex11]] : geoms[vindex11];
          geom1 = (geom10 + geom11) * r;
        }
        tsplitEdge[nextOffsetTri[t] - t + k] =
          edgeLengthCheck(
            geom0, geom1, edgeThreshold, bitshiftEdgeBasedSubdivision)
            ? 1
            : 0;
        ;
        tnextOffsetEdge[nextOffsetTri[t] - t + k + 1] =
          tnextOffsetEdge[nextOffsetTri[t] - t + k]
          + tsplitEdge[nextOffsetTri[t] - t + k];
      }
    }

    auto newPointCount =
      pointCount + nextOffsetEdge[2 * (pointCount - prevPointCount)]
      + tnextOffsetEdge[nextOffsetTri[triangleCount] - triangleCount];

    edges.resize(newPointCount);
    for (int32_t vindex = prevPointCount; vindex < pointCount; ++vindex) {
      for (int i = 0; i < 2; ++i) {
        if (!!splitEdge[2 * (vindex - prevPointCount) + i]) {
          int eindex =
            nextOffsetEdge[2 * (vindex - prevPointCount) + i] + pointCount;
          int vindex0 =
            (i == 0) ? edges[vindex] >> 32 : edges[vindex] & 0xFFFFFFFF;
          edges[eindex] = PACK_EDGE_INFO(vindex0, vindex);
        }
      }
    }
    for (int32_t t = 0; t < triangleCount; ++t) {
      for (int k = 0; k < subdivPattern[t] - 1; ++k) {
        int eindex = tnextOffsetEdge[nextOffsetTri[t] - t + k] + pointCount
                     + nextOffsetEdge[2 * (pointCount - prevPointCount)];
        int vindex0 = triangles[t][k];
        int vindex1 = triangles[t][(k + 1) % 3];
        if (tsplitEdge[nextOffsetTri[t] - t + k]) {
          int vmin      = std::min(vindex0, vindex1);
          int vmax      = std::max(vindex0, vindex1);
          edges[eindex] = PACK_EDGE_INFO(vmin, vmax);
        }
      }
    }
  }

  prevPointCount = pointCount;
}

//----------------------------------------------------------------------------

//11.2.3.2 Midpoint subdivision process
template<typename T1, typename T2>
void
SubdivideMidPoint(std::vector<T1>&       points,
                  int                    iterationIndex,
                  int&                   prevPointCount,
                  std::vector<int64_t>&  edges,
                  std::vector<int32_t>*  submeshFrameLodMap,
                  std::vector<int32_t>*  submeshFrameChildToParentMap) {
  const size_t pointCount    = edges.size();

  // create vertices
  points.resize(pointCount);
  if (submeshFrameLodMap) { submeshFrameLodMap->reserve(pointCount); }
  if (submeshFrameChildToParentMap) {
    submeshFrameChildToParentMap->reserve(pointCount * 2);
  }

  const auto r = T2(1) / T2(2);
  for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
    int vindex0    = edges[vindex] >> 32;
    int vindex1    = edges[vindex] & 0xFFFFFFFF;
    points[vindex] = (points[vindex0] + points[vindex1]) * r;
    if (submeshFrameLodMap) {
      submeshFrameLodMap->push_back(iterationIndex + 1);
    }
    if (submeshFrameChildToParentMap) {
      submeshFrameChildToParentMap->push_back(vindex0);
      submeshFrameChildToParentMap->push_back(vindex1);
    }
  }
}

//----------------------------------------------------------------------------

//11.2.3.5 Pythagorean means based subdivision process
template<typename T1, typename T2>
void
SubdivideMidPointPythagMeans(
  std::vector<T1>&       points,
  int                    iterationIndex,
  int&                   prevPointCount,
  std::vector<int64_t>&  edges,
  std::vector<Triangle>& prevTriangles,
  StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<int32_t>*  submeshFrameLodMap,
  std::vector<int32_t>*  submeshFrameChildToParentMap) {
  const size_t pointCount    = edges.size();
  std::vector<int8_t> ttags(prevTriangles.size());

  // create vertices
  points.resize(pointCount);
  if (submeshFrameLodMap) { submeshFrameLodMap->reserve(pointCount); }
  if (submeshFrameChildToParentMap) {
    submeshFrameChildToParentMap->reserve(pointCount * 2);
  }

  //For 3D input triangles
  if constexpr (std::is_same<T1, Vec3<T2>>::value) {
    ComputeVertexToTriangle(prevTriangles, prevPointCount, vertexToTriangle);
    if (!prevPointCount || !prevTriangles.size()
        || vertexToTriangle.size() != size_t(prevPointCount)) {
      return;
    }
  }

  const auto r = T2(1) / T2(2);
  for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
    int vindex0 = edges[vindex] >> 32;
    int vindex1 = edges[vindex] & 0xFFFFFFFF;
    if (submeshFrameLodMap) {
      submeshFrameLodMap->push_back(iterationIndex + 1);
    }
    if (submeshFrameChildToParentMap) {
      submeshFrameChildToParentMap->push_back(vindex0);
      submeshFrameChildToParentMap->push_back(vindex1);
    }

    //For 3D triangles
    if constexpr (std::is_same<T1, Vec3<T2>>::value) {
      //The harmonic and geometric means are only well defined for positive input values, or when both inputs have the same sign
      if ((points[vindex0][0] * points[vindex1][0] <= 0.0)
          || (points[vindex0][1] * points[vindex1][1] <= 0.0)
          || (points[vindex0][2] * points[vindex1][2] <= 0.0)) {
        //The new predicted point position will be the standard
        //midpoint prediction
        points[vindex] = (points[vindex0] + points[vindex1]) * r;
      } else {
        //Compute triangles adjacent to the current edge (vindex0, vindex1)
        std::vector<int32_t> tadj;
        ComputeEdgeAdjacentTriangles(
          vindex0, vindex1, vertexToTriangle, ttags, tadj);

        //If the current edge is not on a mesh boundary (i.e., it has 2 adjacent triangles)
        if (tadj.size() == 2) {
          //Get the adjacent triangles
          auto t0 = prevTriangles[tadj[0]];
          auto t1 = prevTriangles[tadj[1]];

          //Get the unit-norm normals of the adjacent triangles
          vmesh::Vec3<T2> tnorm1 =
            computeTriangleNormal(Vec3<T2>(points[t0[0]]),
                                  Vec3<T2>(points[t0[1]]),
                                  Vec3<T2>(points[t0[2]]),
                                  true);
          vmesh::Vec3<T2> tnorm2 =
            computeTriangleNormal(Vec3<T2>(points[t1[0]]),
                                  Vec3<T2>(points[t1[1]]),
                                  Vec3<T2>(points[t1[2]]),
                                  true);

          //Compute the dot product between the normal vectors of the
          //adjacent triangles
          double dotProd = tnorm1 * tnorm2;

          if (std::abs(dotProd) < 0.4) {
            //The new predicted point position will be the harmonic mean
            //of the edge endpoints
            points[vindex][0] =
              2.0 / (1.0 / points[vindex0][0] + 1.0 / points[vindex1][0]);
            points[vindex][1] =
              2.0 / (1.0 / points[vindex0][1] + 1.0 / points[vindex1][1]);
            points[vindex][2] =
              2.0 / (1.0 / points[vindex0][2] + 1.0 / points[vindex1][2]);

          } else if ((std::abs(dotProd) >= 0.4)
                     && (std::abs(dotProd) < 0.99)) {
            //The new predicted point position will be the geometric mean
            //of the edge endpoints
            points[vindex][0] =
              std::sqrt(points[vindex0][0] * points[vindex1][0]);
            points[vindex][1] =
              std::sqrt(points[vindex0][1] * points[vindex1][1]);
            points[vindex][2] =
              std::sqrt(points[vindex0][2] * points[vindex1][2]);

            //If the input values were both negative, make the corresponding output negative also
            if ((points[vindex0][0] < 0.0) && (points[vindex1][0] < 0.0)) {
              points[vindex][0] = -1.0 * points[vindex][0];
            }
            if ((points[vindex0][1] < 0.0) && (points[vindex1][1] < 0.0)) {
              points[vindex][1] = -1.0 * points[vindex][1];
            }
            if ((points[vindex0][2] < 0.0) && (points[vindex1][2] < 0.0)) {
              points[vindex][2] = -1.0 * points[vindex][2];
            }
          } else {
            //The new predicted point position will be the standard
            //midpoint prediction
            points[vindex] = (points[vindex0] + points[vindex1]) * r;
          }
        }
        //If the current edge is on a mesh boundary (i.e., it has 1 adjacent triangle)
        else if (tadj.size() == 1) {
          //The new predicted point position will be the standard
          //midpoint prediction
          points[vindex] = (points[vindex0] + points[vindex1]) * r;
        } else {
          //The new predicted point position will be the standard
          //midpoint prediction
          points[vindex] = (points[vindex0] + points[vindex1]) * r;
        }
      }
    }
    //For 2D texture triangles
    else if constexpr (std::is_same<T1, Vec2<T2>>::value) {
      //The new predicted point position will be the standard
      //midpoint prediction
      points[vindex] = (points[vindex0] + points[vindex1]) * r;
    }
  }
}

//----------------------------------------------------------------------------

//11.2.3.4 Normal subdivision process
template<typename T1, typename T2>
void
SubdivideNormal(std::vector<T1>&       points,
                std::vector<T1>&       normals,
                int                    iterationIndex,
                int&                   prevPointCount,
                std::vector<int64_t>&  edges,
                std::vector<int32_t>*  submeshFrameLodMap,
                std::vector<int32_t>*  submeshFrameChildToParentMap) {
  const size_t pointCount    = edges.size();

  const int        num_update_lod = 1;  // Update LoD0 only
  std::vector<T2>  offset;
  std::vector<int> count;
  if (iterationIndex < num_update_lod) {
    offset.resize(prevPointCount);
    std::fill(offset.begin(), offset.end(), T2(0));
    count.resize(prevPointCount);
    std::fill(count.begin(), count.end(), 0);
  }

  // create vertices
  points.resize(pointCount);
  if (submeshFrameLodMap) { submeshFrameLodMap->reserve(pointCount); }
  if (submeshFrameChildToParentMap) {
    submeshFrameChildToParentMap->reserve(pointCount * 2);
  }

  const auto r = T2(1) / T2(2);
  for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
    int vindex0 = edges[vindex] >> 32;
    int vindex1 = edges[vindex] & 0xFFFFFFFF;

    // normal-based refinement
    T2  d0 = (points[vindex0] - points[vindex1]) * normals[vindex0] * r;
    T2  d1 = (points[vindex1] - points[vindex0]) * normals[vindex1] * r;
    T2  refineWeight  = std::min(pow(2.0, iterationIndex - 4), 0.25);
    int normalAligned = (normals[vindex0] * normals[vindex1] > 0) ? 1 : 0;
    T1  nv            = (normals[vindex0] + normals[vindex1]) * r;
    nv.normalize();
    T1 refineVector = ((d0 * normals[vindex0] + d1 * normals[vindex1]) * nv)
                      * nv * refineWeight;
    T1 newPoint =
      (points[vindex0] + points[vindex1]) * r + refineVector * normalAligned;
    points[vindex] = newPoint;
    if (iterationIndex < num_update_lod) {
      offset[vindex0] += d0 * normalAligned;
      count[vindex0] += normalAligned;
      offset[vindex1] += d1 * normalAligned;
      count[vindex1] += normalAligned;
    }

    if (submeshFrameLodMap) {
      submeshFrameLodMap->push_back(iterationIndex + 1);
    }
    if (submeshFrameChildToParentMap) {
      submeshFrameChildToParentMap->push_back(vindex0);
      submeshFrameChildToParentMap->push_back(vindex1);
    }
  }

  if (iterationIndex < num_update_lod) {
    T2 refineWeight = pow(2, -5);
    for (int32_t vindex = 0; vindex < prevPointCount; vindex++) {
      T1 refineVector =
        count[vindex] == 0
          ? 0
          : (((offset[vindex] / count[vindex]) * normals[vindex])
             * refineWeight);
      points[vindex] -= refineVector;
    }
  }
}

//----------------------------------------------------------------------------

template<typename T>
inline T
calculateBeta(int neighbourCount) {
  static const std::array<double, 20> betaList = {
    0.0000000000000000000, 0.1875000000000000000, 0.1875000000000000000,
    0.1210937500000000000, 0.0840932189257828921, 0.0625000000000000000,
    0.0490249201910889013, 0.0400678098159402973, 0.0337850179292333436,
    0.0291777532480480203, 0.0256734766761676954, 0.0229266863992014756,
    0.0207192286042057368, 0.0189078057828722147, 0.0173949930162198702,
    0.0161125312963783796, 0.0150113193131492777, 0.0140552080419541169,
    0.0132170329611838808, 0.0124760061060220778};
  if (0 < neighbourCount && neighbourCount <= betaList.size())
    return T(betaList[neighbourCount - 1]);
  else {
    return T(0);
    //return (k > 3) ? (T(5) / T(8) - ((T(3) / T(8) + cos(T(2) * M_PI / (T)k) / T(4)) * (T(3) / T(8) + cos(T(2) * M_PI / (T)k) / T(4)))) / T(k) : T(3) / T(16);
  }
}

template<typename T>
bool
computeLoopOddMask(const int                            vindex0,
                   const int                            vindex1,
                   StaticAdjacencyInformation<int32_t>& vertexToTriangle,
                   std::vector<int8_t>&                 ttags,
                   const std::vector<Triangle>&         prevTriangles,
                   int8_t&                              adjVertexCount,
                   std::vector<int32_t>&                adjVertices,
                   std::vector<T>&                      adjWeights) {
  bool isBoundaryOrAbnormal = false;
  adjVertexCount            = 0;

  std::vector<int32_t> tadj;
  ComputeEdgeAdjacentTriangles(
    vindex0, vindex1, vertexToTriangle, ttags, tadj);

  if (tadj.size() == 2) {
    int32_t c = -1, d = -1;
    auto    t0 = prevTriangles[tadj[0]];
    auto    t1 = prevTriangles[tadj[1]];
    for (auto id = 0; id < 3; id++) {
      if (t0[id] != vindex1) c = (t0[id] != vindex0) ? t0[id] : c;
      if (t1[id] != vindex1)
        d = (t1[id] != vindex0 && t1[id] != c) ? t1[id] : d;
    }
    if (c >= 0 && d >= 0) {
      //Interior Odd Vertex: located within the mesh interior
      adjVertexCount = 4;

      adjVertices.clear();
      adjVertices.reserve(adjVertexCount);
      adjVertices.emplace_back(vindex0);
      adjVertices.emplace_back(vindex1);
      adjVertices.emplace_back(c);
      adjVertices.emplace_back(d);

      adjWeights.clear();
      adjWeights.reserve(adjVertexCount);
      adjWeights.emplace_back(T(0.375));
      adjWeights.emplace_back(T(0.375));
      adjWeights.emplace_back(T(0.125));
      adjWeights.emplace_back(T(0.125));
    } else isBoundaryOrAbnormal = true;
  } else isBoundaryOrAbnormal = true;
  if (isBoundaryOrAbnormal) {
    //Boundary or Abnormal Odd Vertex
    //Boundary Odd Vertex: located along the mesh boundary
    //Abnormal Odd Vertex: located within unusual configurations, such as a degenerate triangle
    adjVertexCount = 2;

    adjVertices.clear();
    adjVertices.reserve(adjVertexCount);
    adjVertices.emplace_back(vindex0);
    adjVertices.emplace_back(vindex1);

    adjWeights.clear();
    adjWeights.reserve(adjVertexCount);
    adjWeights.emplace_back(T(0.5));
    adjWeights.emplace_back(T(0.5));
  }
  return isBoundaryOrAbnormal;
}

template<typename T>
bool
computeLoopEvenMask(const int                            vindex,
                    StaticAdjacencyInformation<int32_t>& vertexToTriangle,
                    std::vector<int8_t>&                 vtags,
                    const std::vector<Triangle>&         prv_triangles,
                    int8_t&                              adjVertexCount,
                    std::vector<int32_t>&                adjVertices,
                    std::vector<T>&                      adjWeights) {
  bool isAbnormal = false;
  adjVertexCount  = 0;

  std::vector<int32_t> vadj;
  ComputeAdjacentVertices(
    vindex, prv_triangles, vertexToTriangle, vtags, vadj);
  const auto neighbourCount = static_cast<int32_t>(vadj.size());

  if (neighbourCount >= 2) {
    std::vector<int32_t> adjBoundaryVertices;
    std::vector<int8_t>  ttags(prv_triangles.size());
    for (const auto& adj : vadj) {
      if (ComputeEdgeAdjacentTriangleCount(
            vindex, adj, vertexToTriangle, ttags)
          == 1)
        adjBoundaryVertices.push_back(adj);
    }
    if (adjBoundaryVertices.size() != 2) {
      //Interior Even Vertex: located within the mesh interior
      adjVertexCount = static_cast<int8_t>(neighbourCount + 1);

      adjVertices.clear();
      adjVertices.reserve(adjVertexCount);
      adjWeights.clear();
      adjWeights.reserve(adjVertexCount);

      const auto beta = calculateBeta<T>(neighbourCount);
      if (beta > T(0)) {
        adjVertices.emplace_back(vindex);
        adjWeights.emplace_back(T(1) - T(neighbourCount) * beta);
        for (int i = 0; i < neighbourCount; ++i) {
          adjVertices.emplace_back(vadj[i]);
          adjWeights.emplace_back(beta);
        }
      } else isAbnormal = true;
    } else if (adjBoundaryVertices[0] != adjBoundaryVertices[1]) {
      //Boundary Even Vertex: located along the mesh boundary
      adjVertexCount = 3;

      adjVertices.clear();
      adjVertices.reserve(adjVertexCount);
      adjVertices.emplace_back(vindex);
      adjVertices.emplace_back(adjBoundaryVertices[0]);
      adjVertices.emplace_back(adjBoundaryVertices[1]);

      adjWeights.clear();
      adjWeights.reserve(adjVertexCount);
      adjWeights.emplace_back(T(0.75));
      adjWeights.emplace_back(T(0.125));
      adjWeights.emplace_back(T(0.125));
    } else isAbnormal = true;
  } else isAbnormal = true;

  if (isAbnormal) {
    //Abnormal Even Vertex: located within unusual configurations, such as a degenerate triangle
    adjVertexCount = 0;
  }
  return isAbnormal;
}

//11.2.3.3 Loop subdivision process
template<typename T1, typename T2>
void
SubdivideLoop(std::vector<T1>&       points,
              int                    iterationIndex,
              int&                   prevPointCount,
              std::vector<int64_t>&  edges,
              std::vector<Triangle>& prevTriangles,
              StaticAdjacencyInformation<int32_t>& vertexToTriangle,
              std::vector<int32_t>*  submeshFrameLodMap,
              std::vector<int32_t>*  submeshFrameChildToParentMap) {
  const size_t pointCount    = edges.size();

  std::vector<int8_t> ttags(prevTriangles.size());
  vertexToTriangle.resize(prevPointCount);

  ComputeVertexToTriangle(prevTriangles, prevPointCount, vertexToTriangle);
  if (!prevPointCount || !prevTriangles.size()
      || vertexToTriangle.size() != size_t(prevPointCount)) {
    return;
  }

  // create vertices
  points.resize(pointCount);
  if (submeshFrameLodMap) { submeshFrameLodMap->reserve(pointCount); }
  if (submeshFrameChildToParentMap) {
    submeshFrameChildToParentMap->reserve(pointCount * 2);
  }

  //1. Scan all edges and create a new vertex on each edge (called an "odd vertex")
  for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
    int                  vindex0 = edges[vindex] >> 32;
    int                  vindex1 = edges[vindex] & 0xFFFFFFFF;
    int8_t               adjVertexCount;
    std::vector<int32_t> adjVertices;
    std::vector<T2>      adjWeights;
    computeLoopOddMask<T2>(vindex0,
                           vindex1,
                           vertexToTriangle,
                           ttags,
                           prevTriangles,
                           adjVertexCount,
                           adjVertices,
                           adjWeights);
    points[vindex] = points[adjVertices[0]] * adjWeights[0];
    for (int j = 1; j < adjVertexCount; j++)
      points[vindex] = points[vindex] + points[adjVertices[j]] * adjWeights[j];
    if (submeshFrameLodMap) {
      submeshFrameLodMap->push_back(iterationIndex + 1);
    }
    if (submeshFrameChildToParentMap) {
      submeshFrameChildToParentMap->push_back(vindex0);
      submeshFrameChildToParentMap->push_back(vindex1);
    }
  }

  //2. Scan all existing vertices (called "even vertices") and update their coordinates
  std::vector<int8_t> vtags(prevPointCount);
  std::vector<T1> prev_points(prevPointCount);
  for (int32_t vindex = 0; vindex < prevPointCount; vindex++) {
    prev_points[vindex] = points[vindex];
  }
  for (int32_t vindex = 0; vindex < prevPointCount; vindex++) {
    int8_t               adjVertexCount;
    std::vector<int32_t> adjVertices;
    std::vector<T2>      adjWeights;
    computeLoopEvenMask<T2>(vindex,
                            vertexToTriangle,
                            vtags,
                            prevTriangles,
                            adjVertexCount,
                            adjVertices,
                            adjWeights);
    if (adjVertexCount > 0) {
      points[vindex] = prev_points[adjVertices[0]] * adjWeights[0];
      for (int j = 1; j < adjVertexCount; j++) {
        points[vindex] =
          points[vindex] + prev_points[adjVertices[j]] * adjWeights[j];
      }
    }
  }
}
//============================================================================

template<typename T>
bool
RemoveDuplicatedTriangles(
  const std::vector<Triangle>&               trianglesInput,
  const std::vector<Vec3<T>>&                triangleNormals,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  std::vector<Triangle>&                     trianglesOutput,
  std::vector<int32_t>&                      tmap,
  bool                                       trackFlag,
  const bool removeDegeneratedTriangles = true) {
  struct TriangleInfo {
    double  score;
    int32_t outIdx;
  };

  // Output index for the next unique triangle
  int32_t nextOutIdx = 0;
  trianglesOutput.resize(trianglesInput.size());

  const auto triangleCount = int32_t(trianglesInput.size());
  std::map<Triangle, TriangleInfo> uniqueTriangles;
  const auto*                      neighbours = vertexToTriangle.neighbours();
  if (trackFlag) { tmap.resize(trianglesInput.size()); }
  int                              count      = 0;
  for (int32_t t = 0; t < triangleCount; ++t) {
    const auto tri   = orderComponents(trianglesInput[t]);
    double     score = 0.0;
    for (int32_t k = 0; k < 3; ++k) {
      const auto vindex = tri[k];
      const auto start  = vertexToTriangle.neighboursStartIndex(vindex);
      const auto end    = vertexToTriangle.neighboursEndIndex(vindex);
      for (int32_t n = start; n < end; ++n) {
        score += triangleNormals[t] * triangleNormals[neighbours[n]];
      }
    }

    auto it = uniqueTriangles.find(tri);
    if (it == uniqueTriangles.end()) {
      if (trackFlag) {
        tmap[t] = count;
        count++;
      }
      
      if (!removeDegeneratedTriangles || !isDegenerate(tri)) {
        trianglesOutput[nextOutIdx] = trianglesInput[t];
        uniqueTriangles[tri]        = {score, nextOutIdx++};
      }
    } else if (score > it->second.score) {
      it->second.score                   = score;
      if (trackFlag) { tmap[t] = tmap[it->second.outIdx]; }
      trianglesOutput[it->second.outIdx] = trianglesInput[t];
    } else {
      if (trackFlag) { tmap[t] = tmap[it->second.outIdx]; }
    }
  }

  trianglesOutput.resize(uniqueTriangles.size());
  return true;
}

//----------------------------------------------------------------------------

template<typename T>
void
RemoveDegeneratedTriangles(TriangleMesh<T>& mesh) {
  const auto triangleCount = mesh.triangleCount();
  if (triangleCount <= 0) { return; }

  const auto hasTexCoords = mesh.texCoordTriangleCount() == triangleCount;
  const auto hasNormals   = mesh.normalTriangleCount() == triangleCount;
  std::vector<Triangle> triangles;
  std::vector<Triangle> texCoordTriangles;
  std::vector<Triangle> normalTriangles;
  triangles.reserve(triangleCount);

  if (hasTexCoords) { texCoordTriangles.reserve(triangleCount); }

  if (hasNormals) { normalTriangles.reserve(triangleCount); }

  for (int32_t tindex = 0; tindex < triangleCount; tindex++) {
    const auto& tri = mesh.triangle(tindex);
    if (tri[0] != tri[1] && tri[0] != tri[2] && tri[1] != tri[2]) {
      triangles.push_back(tri);
      if (hasTexCoords) {
        texCoordTriangles.push_back(mesh.texCoordTriangle(tindex));
      }
      if (hasNormals) {
        normalTriangles.push_back(mesh.normalTriangle(tindex));
      }
    }
  }

  std::swap(mesh.triangles(), triangles);
  if (hasTexCoords) { std::swap(mesh.texCoordTriangles(), texCoordTriangles); }

  if (hasNormals) { std::swap(mesh.normalTriangles(), normalTriangles); }
}

//----------------------------------------------------------------------------

template<typename T>
int32_t
UnifyVertices(const std::vector<Vec3<T>>&  pointsInput,
              const std::vector<Triangle>& trianglesInput,
              std::vector<Vec3<T>>&        pointsOutput,
              std::vector<Triangle>&       trianglesOutput,
              std::vector<int32_t>&        mapping) {
  const auto pointCount0    = int32_t(pointsInput.size());
  const auto triangleCount0 = int32_t(trianglesInput.size());
  pointsOutput.resize(0);
  pointsOutput.reserve(pointsInput.size());
  trianglesOutput.resize(0);
  trianglesOutput.reserve(trianglesInput.size());
  std::map<Vec3<T>, int32_t> uniquePoints;
  mapping.resize(pointCount0);

  int32_t pointCounter = 0;
  for (int32_t vindex = 0; vindex < pointCount0; ++vindex) {
    const auto& pt = pointsInput[vindex];
    const auto  it = uniquePoints.find(pt);
    if (it == uniquePoints.end()) {
      pointsOutput.push_back(pt);
      uniquePoints[pt] = pointCounter;
      mapping[vindex]  = pointCounter;
      ++pointCounter;
    } else {
      mapping[vindex] = it->second;
    }
  }
  assert(pointCounter <= pointCount0);

  trianglesOutput.resize(triangleCount0);
  for (int32_t tindex = 0; tindex < triangleCount0; ++tindex) {
    const auto& tri0 = trianglesInput[tindex];
    auto&       tri1 = trianglesOutput[tindex];
    for (int32_t k = 0; k < 3; ++k) {
      tri1[k] = mapping[tri0[k]];
      assert(tri1[k] >= 0 && tri1[k] < pointCounter);
    }
  }

  return pointCounter;
}

//----------------------------------------------------------------------------

template<typename T>
int32_t
UnifyVerticesInter(const std::vector<Vec3<T>>&  pointsInput,
                   const std::vector<Triangle>& trianglesInput,
                   std::vector<Vec3<T>>&        pointsOutput,
                   std::vector<Triangle>&       trianglesOutput,
                   std::vector<int32_t>&        mapping) {
  const auto pointCount0    = int32_t(pointsInput.size());
  const auto triangleCount0 = int32_t(trianglesInput.size());
  pointsOutput.resize(0);
  pointsOutput.reserve(pointsInput.size());
  trianglesOutput.resize(0);
  trianglesOutput.reserve(trianglesInput.size());

  std::map<int32_t, int32_t> uniqueIndices;
  int32_t                    pointCounter = 0;
  for (int32_t vindex0 = 0; vindex0 < pointCount0; ++vindex0) {
    const auto& pt      = pointsInput[vindex0];
    auto        vindex1 = mapping[vindex0];
    auto        it      = uniqueIndices.find(vindex1);
    if (it == uniqueIndices.end()) {
      uniqueIndices[vindex1] = vindex0;
      pointsOutput.push_back(pt);
      ++pointCounter;
    }
  }
  assert(pointCounter <= pointCount0);

  trianglesOutput.resize(triangleCount0);
  for (int32_t tindex = 0; tindex < triangleCount0; ++tindex) {
    const auto& tri0 = trianglesInput[tindex];
    auto&       tri1 = trianglesOutput[tindex];
    for (int32_t k = 0; k < 3; ++k) {
      tri1[k] = mapping[tri0[k]];
      assert(tri1[k] >= 0 && tri1[k] < pointCounter);
    }
  }
  return pointCounter;
}

//----------------------------------------------------------------------------

template<typename T>
int32_t
ExtractConnectedComponents(const std::vector<Triangle>& triangles,
                           const int32_t                vertexCount,
                           const TriangleMesh<T>&       mesh,
                           std::vector<int32_t>&        partition,
                           std::vector<int32_t>&        finalPosMapping,
                           std::vector<int32_t>&        oldToNewTriangleIndex,
                           bool                         trackFlag,
                           std::vector<std::shared_ptr<TriangleMesh<T>>>*
                             connectedComponents = nullptr) {
  if (connectedComponents) { connectedComponents->clear(); }

  if (triangles.empty()) { return 0; }

  const auto pointCount    = mesh.pointCount();
  const auto texCoordCount = mesh.texCoordCount();
  const auto normalCount   = mesh.normalCount();
  const auto triangleCount = int32_t(triangles.size());
  if (trackFlag) {
    oldToNewTriangleIndex.clear();
    oldToNewTriangleIndex.resize(triangleCount, -1);
  }
  assert(mesh.triangleCount() == 0 || mesh.triangleCount() == triangleCount);
  assert(mesh.texCoordTriangleCount() == 0
         || mesh.texCoordTriangleCount() == triangleCount);
  assert(mesh.normalTriangleCount() == 0
         || mesh.normalTriangleCount() == triangleCount);
  partition.resize(0);
  partition.resize(triangleCount, -1);

  StaticAdjacencyInformation<int32_t> vertexToTriangle;
  ComputeVertexToTriangle(triangles, vertexCount, vertexToTriangle);

  std::vector<int32_t> posMapping;
  std::vector<int32_t> texCoordMapping;
  std::vector<int32_t> normalMapping;
  std::vector<int32_t> triangleList;
  if (connectedComponents) {
    if (mesh.triangleCount()) {
      posMapping.resize(pointCount, -1);
      if (trackFlag) finalPosMapping.resize(pointCount, -1);
    }
    if (mesh.texCoordTriangleCount()) {
      texCoordMapping.resize(texCoordCount, -1);
    }
    if (mesh.normalTriangleCount()) { normalMapping.resize(normalCount, -1); }
  }

  const auto*          neighbours = vertexToTriangle.neighbours();
  std::vector<int32_t> lifo;
  lifo.reserve(triangleCount);
  int32_t ccCount = 0;
  for (int32_t triangleIndex = 0; triangleIndex < triangleCount;
       ++triangleIndex) {
    if (partition[triangleIndex] == -1) {
      const auto ccIndex       = ccCount++;
      partition[triangleIndex] = ccIndex;
      lifo.push_back(triangleIndex);

      std::shared_ptr<TriangleMesh<T>> ccMesh;
      if (connectedComponents) {
        ccMesh.reset(new TriangleMesh<T>);
        connectedComponents->push_back(ccMesh);
        ccMesh->setMaterialLibrary(mesh.materialLibrary());
        triangleList.resize(0);
      }

      int32_t ccPosCount      = 0;
      int32_t ccTexCoordCount = 0;
      int32_t ccNormalCount   = 0;
      while (!lifo.empty()) {
        const auto tIndex = lifo.back();
        lifo.pop_back();

        if (connectedComponents) {
          triangleList.push_back(tIndex);
          if (trackFlag)
            oldToNewTriangleIndex[tIndex] = ccMesh->triangleCount();

          if (mesh.triangleCount()) {
            const auto& tri = mesh.triangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = tri[k];
              if (posMapping[v] == -1) {
                posMapping[v] = ccPosCount++;
                if (trackFlag) finalPosMapping[v] = posMapping[v];
                ccMesh->addPoint(mesh.point(v));
              }
            }
            ccMesh->addTriangle(
              posMapping[tri[0]], posMapping[tri[1]], posMapping[tri[2]]);
          }

          if (mesh.texCoordTriangleCount()) {
            const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = texCoordTri[k];
              if (texCoordMapping[v] == -1) {
                texCoordMapping[v] = ccTexCoordCount++;
                ccMesh->addTexCoord(mesh.texCoord(v));
              }
            }
            ccMesh->addTexCoordTriangle(texCoordMapping[texCoordTri[0]],
                                        texCoordMapping[texCoordTri[1]],
                                        texCoordMapping[texCoordTri[2]]);
          }

          if (mesh.normalTriangleCount()) {
            const auto& normalTri = mesh.normalTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = normalTri[k];
              if (normalMapping[v] == -1) {
                normalMapping[v] = ccNormalCount++;
                ccMesh->addNormal(mesh.normal(v));
              }
            }

            ccMesh->addNormalTriangle(normalMapping[normalTri[0]],
                                      normalMapping[normalTri[1]],
                                      normalMapping[normalTri[2]]);
          }
        }

        const auto& tri = triangles[tIndex];
        for (int32_t k = 0; k < 3; ++k) {
          const auto v     = tri[k];
          const auto start = vertexToTriangle.neighboursStartIndex(v);
          const auto end   = vertexToTriangle.neighboursEndIndex(v);
          for (int32_t n = start; n < end; ++n) {
            const auto nIndex = neighbours[n];
            if (partition[nIndex] == -1) {
              partition[nIndex] = ccIndex;
              lifo.push_back(nIndex);
            }
          }
        }
      }

      if (connectedComponents) {
        for (const auto tIndex : triangleList) {
          if (mesh.triangleCount()) {
            const auto& tri = mesh.triangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) { posMapping[tri[k]] = -1; }
          }

          if (mesh.texCoordTriangleCount()) {
            const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              texCoordMapping[texCoordTri[k]] = -1;
            }
          }

          if (mesh.normalTriangleCount()) {
            const auto& normalTri = mesh.normalTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              normalMapping[normalTri[k]] = -1;
            }
          }
        }
      }
    }
  }

  return ccCount;
}

//----------------------------------------------------------------------------

template<typename T>
int32_t
ExtractConnectedComponentsByEdge(const std::vector<Triangle>& triangles,
                                 const int32_t                vertexCount,
                                 const TriangleMesh<T>&       mesh,
                                 std::vector<int32_t>&        partition,
                                 bool useVertexCriteria = false,
                                 std::vector<std::shared_ptr<TriangleMesh<T>>>*
                                   connectedComponents = nullptr) {
  if (connectedComponents) { connectedComponents->clear(); }

  if (triangles.empty()) { return 0; }

  const auto pointCount    = mesh.pointCount();
  const auto texCoordCount = mesh.texCoordCount();
  const auto normalCount   = mesh.normalCount();
  const auto triangleCount = int32_t(triangles.size());
  assert(mesh.triangleCount() == 0 || mesh.triangleCount() == triangleCount);
  assert(mesh.texCoordTriangleCount() == 0
         || mesh.texCoordTriangleCount() == triangleCount);
  assert(mesh.normalTriangleCount() == 0
         || mesh.normalTriangleCount() == triangleCount);
  partition.resize(0);
  partition.resize(triangleCount, -1);

  // creating the triangle to triangle structure
  // sharing either a vertex or an edge
  vmesh::StaticAdjacencyInformation<int32_t> triangleToTriangle;
  triangleToTriangle.resize(triangleCount);
  vmesh::StaticAdjacencyInformation<int32_t> vertexToTriangle;
  ComputeVertexToTriangle(triangles, vertexCount, vertexToTriangle);
  if (useVertexCriteria) {
    std::vector<int8_t> ttags;
    ttags.resize(triangleCount);
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      const auto ncount = ComputeAdjacentTrianglesCount(
        triangles[triIdx], vertexToTriangle, ttags);
      triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
    }
    triangleToTriangle.updateShift();
    std::vector<int32_t> tadj;
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      ComputeAdjacentTriangles(
        triangles[triIdx], vertexToTriangle, ttags, tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
    }
  } else {
    std::vector<int8_t> ttags;
    ttags.resize(triangleCount);
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      auto ncount = ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][0], triangles[triIdx][1], vertexToTriangle, ttags);
      ncount += ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][1], triangles[triIdx][2], vertexToTriangle, ttags);
      ncount += ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][2], triangles[triIdx][0], vertexToTriangle, ttags);
      triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
    }
    triangleToTriangle.updateShift();
    std::vector<int32_t> tadj;
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      ComputeEdgeAdjacentTriangles(triangles[triIdx][0],
                                   triangles[triIdx][1],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
      ComputeEdgeAdjacentTriangles(triangles[triIdx][1],
                                   triangles[triIdx][2],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
      ComputeEdgeAdjacentTriangles(triangles[triIdx][2],
                                   triangles[triIdx][0],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
    }
  }

  std::vector<int32_t> posMapping;
  std::vector<int32_t> texCoordMapping;
  std::vector<int32_t> normalMapping;
  std::vector<int32_t> triangleList;
  if (connectedComponents) {
    if (mesh.triangleCount()) { posMapping.resize(pointCount, -1); }
    if (mesh.texCoordTriangleCount()) {
      texCoordMapping.resize(texCoordCount, -1);
    }
    if (mesh.normalTriangleCount()) { normalMapping.resize(normalCount, -1); }
  }

  const auto*          neighbours = triangleToTriangle.neighbours();
  std::vector<int32_t> lifo;
  lifo.reserve(triangleCount);
  int32_t ccCount = 0;
  for (int32_t triangleIndex = 0; triangleIndex < triangleCount;
       ++triangleIndex) {
    if (partition[triangleIndex] == -1) {
      const auto ccIndex       = ccCount++;
      partition[triangleIndex] = ccIndex;
      lifo.push_back(triangleIndex);

      std::shared_ptr<TriangleMesh<T>> ccMesh;
      if (connectedComponents) {
        ccMesh.reset(new TriangleMesh<T>);
        connectedComponents->push_back(ccMesh);
        ccMesh->setMaterialLibrary(mesh.materialLibrary());
        triangleList.resize(0);
      }

      int32_t ccPosCount      = 0;
      int32_t ccTexCoordCount = 0;
      int32_t ccNormalCount   = 0;
      while (!lifo.empty()) {
        const auto tIndex = lifo.back();
        lifo.pop_back();

        if (connectedComponents) {
          triangleList.push_back(tIndex);

          if (mesh.triangleCount()) {
            const auto& tri = mesh.triangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = tri[k];
              if (posMapping[v] == -1) {
                posMapping[v] = ccPosCount++;
                ccMesh->addPoint(mesh.point(v));
              }
            }
            ccMesh->addTriangle(
              posMapping[tri[0]], posMapping[tri[1]], posMapping[tri[2]]);
          }

          if (mesh.texCoordTriangleCount()) {
            const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = texCoordTri[k];
              if (texCoordMapping[v] == -1) {
                texCoordMapping[v] = ccTexCoordCount++;
                ccMesh->addTexCoord(mesh.texCoord(v));
              }
            }
            ccMesh->addTexCoordTriangle(texCoordMapping[texCoordTri[0]],
                                        texCoordMapping[texCoordTri[1]],
                                        texCoordMapping[texCoordTri[2]]);
          }

          if (mesh.normalTriangleCount()) {
            const auto& normalTri = mesh.normalTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = normalTri[k];
              if (normalMapping[v] == -1) {
                normalMapping[v] = ccNormalCount++;
                ccMesh->addNormal(mesh.normal(v));
              }
            }

            ccMesh->addNormalTriangle(normalMapping[normalTri[0]],
                                      normalMapping[normalTri[1]],
                                      normalMapping[normalTri[2]]);
          }
        }

        const auto start = triangleToTriangle.neighboursStartIndex(tIndex);
        const auto end   = triangleToTriangle.neighboursEndIndex(tIndex);
        for (int32_t n = start; n < end; ++n) {
          const auto nIndex = neighbours[n];
          if (partition[nIndex] == -1) {
            partition[nIndex] = ccIndex;
            lifo.push_back(nIndex);
          }
        }
      }

      if (connectedComponents) {
        for (const auto tIndex : triangleList) {
          if (mesh.triangleCount()) {
            const auto& tri = mesh.triangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) { posMapping[tri[k]] = -1; }
          }

          if (mesh.texCoordTriangleCount()) {
            const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              texCoordMapping[texCoordTri[k]] = -1;
            }
          }

          if (mesh.normalTriangleCount()) {
            const auto& normalTri = mesh.normalTriangle(tIndex);
            for (int32_t k = 0; k < 3; ++k) {
              normalMapping[normalTri[k]] = -1;
            }
          }
        }
      }
    }
  }

  return ccCount;
}

//----------------------------------------------------------------------------
#define DEBUG_ORTHO_TRIANGLE_CATEGORY 0
#define DEBUG_ORTHO_TRIANGLE_CATEGORY_VERBOSE 0
#define DEBUG_ORTHO_CONNECTED_COMPONENT 0
#define DEBUG_ORTHO_CONNECTED_COMPONENT_VERBOSE 0
#define DEBUG_ORTHO_MERGE 0
#define DEBUG_ORTHO_MERGE_VERBOSE 0
template<typename T>
int32_t
ExtractConnectedComponentsByNormal(
  const TriangleMesh<T>&                         mesh,
  Vec3<double>*                                  orientations,
  int                                            orientationCount,
  std::vector<int32_t>&                          category,
  std::vector<std::shared_ptr<TriangleMesh<T>>>* connectedComponents = nullptr,
  bool                                           useVertexCriteria   = false,
  bool                                           bUseSeedHistogram   = true,
  double                                         strongGradientThreshold = 180,
  double                                         maxCCAreaRatio          = 1,
  int         maxNumFaces             = std::numeric_limits<int>::max(),
  bool        bFaceClusterMerge       = false,
  double      lambda                  = 1.0,
  bool        check2DConnectivity     = true,
  bool        keepIntermediateResults = false,
  std::string keepFilesPathPrefix     = {}) {
  if (connectedComponents) { connectedComponents->clear(); }

  const std::vector<Triangle>& triangles = mesh.triangles();
  if (triangles.empty()) { return 0; }

  const auto    pointCount    = mesh.pointCount();
  const int32_t vertexCount   = pointCount;
  const auto    texCoordCount = mesh.texCoordCount();
  const auto    normalCount   = mesh.normalCount();
  const auto    triangleCount = int32_t(triangles.size());
  assert(mesh.triangleCount() == triangleCount);
  assert(mesh.texCoordTriangleCount() == 0
         || mesh.texCoordTriangleCount() == triangleCount);
  assert(mesh.normalTriangleCount() == 0
         || mesh.normalTriangleCount() == triangleCount);
  // initializing partition
  std::vector<int32_t> partition;
  partition.resize(0);
  partition.resize(triangleCount, -1);
  // creating the triangle to triangle structure
  // sharing either a vertex or an edge
  vmesh::StaticAdjacencyInformation<int32_t> triangleToTriangle;
  triangleToTriangle.resize(triangleCount);
  vmesh::StaticAdjacencyInformation<int32_t> vertexToTriangle;
  ComputeVertexToTriangle(triangles, vertexCount, vertexToTriangle);
  if (useVertexCriteria) {
    std::vector<int8_t> ttags;
    ttags.resize(triangleCount);
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      const auto ncount = ComputeAdjacentTrianglesCount(
        triangles[triIdx], vertexToTriangle, ttags);
      triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
    }
    triangleToTriangle.updateShift();
    std::vector<int32_t> tadj;
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      ComputeAdjacentTriangles(
        triangles[triIdx], vertexToTriangle, ttags, tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
    }
  } else {
    std::vector<int8_t> ttags;
    ttags.resize(triangleCount);
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      auto ncount = ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][0], triangles[triIdx][1], vertexToTriangle, ttags);
      ncount += ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][1], triangles[triIdx][2], vertexToTriangle, ttags);
      ncount += ComputeEdgeAdjacentTriangleCount(
        triangles[triIdx][2], triangles[triIdx][0], vertexToTriangle, ttags);
      triangleToTriangle.incrementNeighbourCount(triIdx, ncount);
    }
    triangleToTriangle.updateShift();
    std::vector<int32_t> tadj;
    for (int32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
      ComputeEdgeAdjacentTriangles(triangles[triIdx][0],
                                   triangles[triIdx][1],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
      ComputeEdgeAdjacentTriangles(triangles[triIdx][1],
                                   triangles[triIdx][2],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
      ComputeEdgeAdjacentTriangles(triangles[triIdx][2],
                                   triangles[triIdx][0],
                                   vertexToTriangle,
                                   ttags,
                                   tadj);
      for (const auto triIdxNeighbor : tadj) {
        triangleToTriangle.addNeighbour(triIdx, triIdxNeighbor);
      }
    }
  }
  const auto* neighbours = triangleToTriangle.neighbours();

  // initializing mapping lists for conected components
  std::vector<int32_t> posMapping;
  std::vector<int32_t> texCoordMapping;
  std::vector<int32_t> normalMapping;
  std::vector<int32_t> triangleList;
  if (connectedComponents) {
    if (mesh.triangleCount()) { posMapping.resize(pointCount, -1); }
    if (mesh.texCoordTriangleCount()) {
      texCoordMapping.resize(texCoordCount, -1);
    }
    if (mesh.normalTriangleCount()) { normalMapping.resize(normalCount, -1); }
  }

  //get triangle normal direction and 3D area
  std::vector<Vec3<double>> triangleNormals;
  mesh.computeTriangleNormals(triangleNormals, true);
  std::vector<double> triangleAreas;
  mesh.computeTriangleAreas(triangleAreas);
  double totalArea = mesh.area();
  //categorize triangles according to their normals
  std::vector<int32_t> triangleNormalCategories;
  triangleNormalCategories.resize(triangleNormals.size());
  for (size_t tri = 0; tri < triangleNormals.size(); tri++) {
    auto& triNormal = triangleNormals[tri];
    if (triNormal == Vec3<double>(0.0)) {
      triangleNormalCategories[tri] = -1;
      continue;
    }
    size_t clusterIndex = 0;
    double bestScore    = triNormal * orientations[0];
    for (size_t j = 1; j < orientationCount; ++j) {
      const double score = triNormal * orientations[j];
      if (score > bestScore) {
        bestScore    = score;
        clusterIndex = j;
      }
    }
    triangleNormalCategories[tri] = clusterIndex;
  }
#if DEBUG_ORTHO_TRIANGLE_CATEGORY
  {
    TriangleMesh<double> meshDebug;
    double               totalPerimeter = 0;
    double               totalStretchL2 = 0;
    double               minStretchL2   = std::numeric_limits<double>::max();
    double               maxStretchL2   = std::numeric_limits<double>::min();
    int                  ccIdx          = 0;
    for (auto& tri : mesh.triangles()) {
      TriangleMesh<double> singleTriangle;
      singleTriangle.addPoint(mesh.point(tri[0]));
      singleTriangle.addPoint(mesh.point(tri[1]));
      singleTriangle.addPoint(mesh.point(tri[2]));
      singleTriangle.addTriangle(0, 1, 2);
      meshDebug.append(singleTriangle);
#  if DEBUG_ORTHO_TRIANGLE_CATEGORY_VERBOSE
      std::cout << "CC[" << ccIdx << "] -> perimeter("
                << singleTriangle.perimeter() << "), stretchL2("
                << singleTriangle.stretchL2(triangleNormalCategories[ccIdx])
                << ")" << std::endl;
#  endif
      totalPerimeter += singleTriangle.perimeter();
      auto l2 = singleTriangle.stretchL2(triangleNormalCategories[ccIdx]);
      totalStretchL2 += l2;
      if (l2 > maxStretchL2) maxStretchL2 = l2;
      if (l2 < minStretchL2) minStretchL2 = l2;
      ccIdx++;
    }
    std::cout << "(" << ccIdx << " CC)TOTAL PERIMETER: " << totalPerimeter
              << ", TOTAL STRETCH L2: " << totalStretchL2
              << ", AVG. STRETCH L2: " << totalStretchL2 / (double)ccIdx
              << ", MIN. STRETCH L2: " << minStretchL2
              << ", MAX. STRETCH L2: " << maxStretchL2 << std::endl;
    if (keepIntermediateResults) {
      meshDebug.reserveTexCoords(mesh.triangleCount() * 3);
      meshDebug.reserveTexCoordTriangles(mesh.triangleCount());
      int side = 100;
      for (int idx = 0; idx < triangleNormals.size(); idx++) {
        float x = ((triangleNormalCategories[idx] % 9) + 0.5) / (9.0);
        float y = 1 - ((triangleNormalCategories[idx] / 9) + 0.5) / (2.0);
        meshDebug.texCoords().push_back(Vec2<float>(x, y));
        meshDebug.texCoords().push_back(Vec2<float>(x, y));
        meshDebug.texCoords().push_back(Vec2<float>(x, y));
        meshDebug.texCoordTriangles().push_back(
          vmesh::Triangle(3 * idx, 3 * idx + 1, 3 * idx + 2));
      }
      vmesh::Frame<uint8_t> outputTexture;
      outputTexture.resize(9 * side, 2 * side, vmesh::ColourSpace::BGR444p);
      int RedChannel[18]   = {255,
                              0,
                              0,
                              0,
                              255,
                              255,
                              0,
                              100,
                              200,
                              255,
                              0,
                              100,
                              200,
                              200,
                              0,
                              100,
                              200,
                              100};
      int GreenChannel[18] = {0,
                              255,
                              0,
                              255,
                              0,
                              255,
                              0,
                              100,
                              200,
                              255,
                              200,
                              200,
                              100,
                              0,
                              0,
                              100,
                              200,
                              100};
      int BlueChannel[18]  = {0,
                              0,
                              255,
                              255,
                              255,
                              0,
                              0,
                              100,
                              200,
                              255,
                              0,
                              100,
                              200,
                              200,
                              100,
                              200,
                              100,
                              0};
      for (int h = 0; h < 2; h++) {
        for (int w = 0; w < 9; w++) {
          auto& R = outputTexture.plane(0);
          auto& G = outputTexture.plane(1);
          auto& B = outputTexture.plane(2);
          for (int i = 0; i < side; i++) {
            for (int j = 0; j < side; j++) {
              R.set(i + h * side, j + w * side, RedChannel[h * 9 + w]);
              G.set(i + h * side, j + w * side, GreenChannel[h * 9 + w]);
              B.set(i + h * side, j + w * side, BlueChannel[h * 9 + w]);
            }
          }
        }
      }
      outputTexture.save(keepFilesPathPrefix + "_debug_categories.png");
      vmesh::Material<double> material;
      material.texture =
        vmesh::basename(keepFilesPathPrefix + "_debug_categories.png");
      material.save(keepFilesPathPrefix + "_debug_categories.mtl");
      meshDebug.setMaterialLibrary(
        vmesh::basename(keepFilesPathPrefix + "_debug_categories.mtl"));
      meshDebug.save(keepFilesPathPrefix + "_debug_categories_INITIAL.obj");
    }
  }
#endif

  std::vector<int32_t> lifo;
  lifo.reserve(triangleCount);
  int32_t             ccCount = 0;
  std::vector<double> ccArea;
  ccArea.reserve(triangleCount);
  std::vector<double> ccNumTriangles;
  ccNumTriangles.reserve(triangleCount);
  std::vector<Vec3<double>> ccAvgNormal;
  ccAvgNormal.reserve(triangleCount);
  std::vector<std::vector<int>> ccTriangleList;
  ccTriangleList.reserve(triangleCount);
  std::vector<std::vector<int>> ccVertexList;
  ccVertexList.reserve(triangleCount);
  std::vector<double> ccPerimeter;
  ccPerimeter.reserve(triangleCount);
  std::vector<double> ccStretchL2;
  ccStretchL2.reserve(triangleCount);
  //create connected component by triangle edge/vertex sharing
  int numTriInConnectedComponents = 0;
  while (numTriInConnectedComponents < triangleCount) {
    //choose the triangle index that has the most occurances
    int32_t triangleIndex = 0;
    if (bUseSeedHistogram) {
      std::vector<int>     histogram;
      std::vector<int32_t> firstOccurance;
      std::vector<double>  maxDotProduct;
      histogram.resize(orientationCount, 0);
      firstOccurance.resize(orientationCount, -1);
      maxDotProduct.resize(orientationCount, -1000000);
      for (int32_t triIndex = 0; triIndex < triangleCount; ++triIndex) {
        if (partition[triIndex] == -1) {
          if (triangleNormalCategories[triIndex] == -1) {
            //degenerate triangle, all the categories are valid
            for (int32_t idx = 0; idx < orientationCount; ++idx) {
              histogram[idx]++;
              if (maxDotProduct[idx]
                  < (triangleNormals[triIndex] * orientations[idx])) {
                maxDotProduct[idx] =
                  (triangleNormals[triIndex] * orientations[idx]);
                firstOccurance[idx] = triIndex;
              }
            }
            continue;
          }
          histogram[triangleNormalCategories[triIndex]]++;
          if (maxDotProduct[triangleNormalCategories[triIndex]]
              < (triangleNormals[triIndex]
                 * orientations[triangleNormalCategories[triIndex]])) {
            maxDotProduct[triangleNormalCategories[triIndex]] =
              (triangleNormals[triIndex]
               * orientations[triangleNormalCategories[triIndex]]);
            firstOccurance[triangleNormalCategories[triIndex]] = triIndex;
          }
        }
      }
      triangleIndex = firstOccurance[0];
      int maxVal    = histogram[0];
      for (int i = 1; i < orientationCount; i++)
        if (histogram[i] > maxVal) {
          maxVal        = histogram[i];
          triangleIndex = firstOccurance[i];
        }
    } else {
      bool found = false;
      for (int32_t triIndex = 0; triIndex < triangleCount && !found;
           ++triIndex) {
        if (partition[triIndex] == -1) {
          triangleIndex = triIndex;
          found         = true;
        }
      }
    }
    //now add all the triangles connected to the seed
    const auto ccIndex       = ccCount++;
    partition[triangleIndex] = ccIndex;
    numTriInConnectedComponents++;
    int32_t ccCategory = triangleNormalCategories[triangleIndex];
    category.push_back(ccCategory);
    lifo.push_back(triangleIndex);
    ccArea.push_back(triangleAreas[triangleIndex]);
    ccNumTriangles.push_back(1);
    ccAvgNormal.push_back(triangleAreas[triangleIndex]
                          * triangleNormals[triangleIndex]);
    std::vector<int> triangleList;
    ccTriangleList.push_back(triangleList);
    std::vector<int> vertexList;
    ccVertexList.push_back(vertexList);
    ccTriangleList[ccIndex].push_back(triangleIndex);
    for (int idx = 0; idx < 3; idx++) {
      auto& vIdx      = mesh.triangles()[triangleIndex][idx];
      auto  findIndex = std::find(
        ccVertexList[ccIndex].begin(), ccVertexList[ccIndex].end(), vIdx);
      if (findIndex == ccVertexList[ccIndex].end())
        ccVertexList[ccIndex].push_back(vIdx);
    }
    ccPerimeter.push_back(0);
    ccStretchL2.push_back(0);
    std::shared_ptr<TriangleMesh<T>> ccMesh;
    if (connectedComponents) {
      ccMesh.reset(new TriangleMesh<T>);
      connectedComponents->push_back(ccMesh);
      ccMesh->setMaterialLibrary(mesh.materialLibrary());
      triangleList.resize(0);
    }
    int32_t ccPosCount      = 0;
    int32_t ccTexCoordCount = 0;
    int32_t ccNormalCount   = 0;
    while (!lifo.empty()) {
      const auto tIndex             = lifo.back();
      auto&      lastInsertedNormal = triangleNormals[tIndex];
      lifo.pop_back();

      if (connectedComponents) {
        triangleList.push_back(tIndex);

        if (mesh.triangleCount()) {
          const auto& tri = mesh.triangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) {
            const auto v = tri[k];
            if (posMapping[v] == -1) {
              posMapping[v] = ccPosCount++;
              ccMesh->addPoint(mesh.point(v));
            }
          }
          ccMesh->addTriangle(
            posMapping[tri[0]], posMapping[tri[1]], posMapping[tri[2]]);
        }

        if (mesh.texCoordTriangleCount()) {
          const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) {
            const auto v = texCoordTri[k];
            if (texCoordMapping[v] == -1) {
              texCoordMapping[v] = ccTexCoordCount++;
              ccMesh->addTexCoord(mesh.texCoord(v));
            }
          }
          ccMesh->addTexCoordTriangle(texCoordMapping[texCoordTri[0]],
                                      texCoordMapping[texCoordTri[1]],
                                      texCoordMapping[texCoordTri[2]]);
        }

        if (mesh.normalTriangleCount()) {
          const auto& normalTri = mesh.normalTriangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) {
            const auto v = normalTri[k];
            if (normalMapping[v] == -1) {
              normalMapping[v] = ccNormalCount++;
              ccMesh->addNormal(mesh.normal(v));
            }
          }

          ccMesh->addNormalTriangle(normalMapping[normalTri[0]],
                                    normalMapping[normalTri[1]],
                                    normalMapping[normalTri[2]]);
        }
      }

      const auto start = triangleToTriangle.neighboursStartIndex(tIndex);
      const auto end   = triangleToTriangle.neighboursEndIndex(tIndex);
      for (int32_t n = start; n < end; ++n) {
        const auto nIndex = neighbours[n];
        if (partition[nIndex] == -1) {
          //neighbor does not belong to any CC, let's check the face criteria
          bool addNeighbor = true;
          //If the connected face�s normal is closer to n (in terms of inter-vector angle) than to any other projection plane normal
          //category -1 indicates degenerate triangle, and is always allowed to be included at any projection direction
          if ((triangleNormalCategories[nIndex] != ccCategory)
              && (triangleNormalCategories[nIndex] != -1)) {
            addNeighbor = false;
          }
          //strong gradient avoidance
          auto&  currentNormal = triangleNormals[nIndex];
          double angle         = std::acos(
            currentNormal * lastInsertedNormal
            / (std::sqrt(currentNormal.norm2())
               * std::sqrt(
                 lastInsertedNormal
                   .norm2())));  //TODO: calculater the angle between two normals
          angle *= (180.0 / M_PI);
          if (angle > std::abs(strongGradientThreshold)) {
            addNeighbor = false;
          }
          //cluster cumulated area criterion
          double triArea = triangleAreas[nIndex];
          if ((triArea + ccArea[ccIndex]) > (totalArea * maxCCAreaRatio)) {
            addNeighbor = false;
          }
          //maximal face number
          if (ccNumTriangles[ccIndex] > maxNumFaces) { addNeighbor = false; }
          if (addNeighbor) {
            partition[nIndex] = ccIndex;
            numTriInConnectedComponents++;
            lifo.push_back(nIndex);
            ccArea[ccIndex] += triArea;
            ccNumTriangles[ccIndex]++;
            ccAvgNormal[ccIndex] += triArea * currentNormal;
            ccTriangleList[ccIndex].push_back(nIndex);
            for (int idx = 0; idx < 3; idx++) {
              auto& vIdx      = mesh.triangles()[nIndex][idx];
              auto  findIndex = std::find(ccVertexList[ccIndex].begin(),
                                         ccVertexList[ccIndex].end(),
                                         vIdx);
              if (findIndex == ccVertexList[ccIndex].end())
                ccVertexList[ccIndex].push_back(vIdx);
            }
          }
        }
      }
    }
    if (ccArea[ccIndex] != 0.0) ccAvgNormal[ccIndex] /= ccArea[ccIndex];
    ccPerimeter[ccIndex] = ccMesh->perimeter();
    ccStretchL2[ccIndex] = ccMesh->stretchL2(category[ccIndex]);
    if (connectedComponents) {
      for (const auto tIndex : triangleList) {
        if (mesh.triangleCount()) {
          const auto& tri = mesh.triangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) { posMapping[tri[k]] = -1; }
        }

        if (mesh.texCoordTriangleCount()) {
          const auto& texCoordTri = mesh.texCoordTriangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) {
            texCoordMapping[texCoordTri[k]] = -1;
          }
        }

        if (mesh.normalTriangleCount()) {
          const auto& normalTri = mesh.normalTriangle(tIndex);
          for (int32_t k = 0; k < 3; ++k) { normalMapping[normalTri[k]] = -1; }
        }
      }
    }
  }
#if DEBUG_ORTHO_CONNECTED_COMPONENT
  {
    int                  ccIdx = 0;
    TriangleMesh<double> meshDebug;
    double               totalPerimeter = 0;
    double               totalStretchL2 = 0;
    double               minStretchL2   = std::numeric_limits<double>::max();
    double               maxStretchL2   = std::numeric_limits<double>::min();
    for (auto cc : (*connectedComponents)) {
      meshDebug.append(*cc);
#  if DEBUG_ORTHO_CONNECTED_COMPONENT_VERBOSE
      if (keepIntermediateResults) {
        auto prefix =
          keepFilesPathPrefix + "_init_CC#" + std::to_string(ccIdx);
        cc->save(prefix + ".obj");
      }
      std::cout << "CC[" << ccIdx << "] -> perimeter(" << cc->perimeter()
                << "), stretchL2(" << cc->stretchL2(category[ccIdx]) << ")"
                << std::endl;
#  endif
      totalPerimeter += cc->perimeter();
      auto l2 = cc->stretchL2(category[ccIdx]);
      totalStretchL2 += l2;
      if (l2 > maxStretchL2) maxStretchL2 = l2;
      if (l2 < minStretchL2) minStretchL2 = l2;
      ccIdx++;
    }
    std::cout << "(" << ccIdx << " CC)TOTAL PERIMETER: " << totalPerimeter
              << ", TOTAL STRETCH L2: " << totalStretchL2
              << ", AVG. STRETCH L2: " << totalStretchL2 / (double)ccIdx
              << ", MIN. STRETCH L2: " << minStretchL2
              << ", MAX. STRETCH L2: " << maxStretchL2 << std::endl;
    if (keepIntermediateResults) {
      meshDebug.texCoords().clear();
      meshDebug.texCoordTriangles().clear();
      meshDebug.reserveTexCoords(meshDebug.triangleCount() * 3);
      meshDebug.reserveTexCoordTriangles(meshDebug.triangleCount());
      int idxCC       = 0;
      int idxTriangle = 0;
      int side        = 100;
      for (auto cc : (*connectedComponents)) {
        float x = ((category[idxCC] % 9) + 0.5) / (9.0);
        float y = 1 - ((category[idxCC] / 9) + 0.5) / (2.0);
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
                     + "_debug_categories_before_merge.obj");
    }
  }
#endif

  //FACE CLUSTER MERGE
  if (bFaceClusterMerge) {
#if DEBUG_ORTHO_MERGE_VERBOSE
    std::cout << "Number of connected components BEFORE merge:" << ccCount
              << std::endl;
#endif
    unsigned int* apActiveVertexIndexFlag =
      new unsigned int[mesh.pointCount()];
    std::vector<int> removeVertexList;
    std::vector<int> ccMerge;
    ccMerge.resize(ccCount, -1);
    std::vector<int> ccIndexSortedList;
    ccIndexSortedList.resize(ccCount);
    for (int ccIndexSorted = 0; ccIndexSorted < ccCount; ccIndexSorted++)
      ccIndexSortedList[ccIndexSorted] = ccIndexSorted;
    std::sort(ccIndexSortedList.begin(),
              ccIndexSortedList.end(),
              [&](int A, int B) -> bool {
                if (ccNumTriangles[A] == ccNumTriangles[B]) {
                  double maxDotA = ccAvgNormal[A] * orientations[0];
                  double maxDotB = ccAvgNormal[B] * orientations[0];
                  for (int i = 1; i < orientationCount; i++) {
                    double dotA = ccAvgNormal[A] * orientations[i];
                    double dotB = ccAvgNormal[B] * orientations[i];
                    if (dotA > maxDotA) maxDotA = dotA;
                    if (dotB > maxDotB) maxDotB = dotB;
                  }
                  if (maxDotA < maxDotB) return true;
                  else if (maxDotA == maxDotB) return (A > B);
                  else return false;
                } else {
                  if (ccNumTriangles[A] > ccNumTriangles[B]) return true;
                  else if (ccNumTriangles[A] == ccNumTriangles[B])
                    return (A > B);
                  else return false;
                };
              });
    while (ccIndexSortedList.size() > 0) {
      memset(apActiveVertexIndexFlag, 0, sizeof(int) * mesh.pointCount());
      removeVertexList.clear();

      int ccIndex = ccIndexSortedList.back();
      ccIndexSortedList.pop_back();
      //get the possible candidates for merging (connected components that share vertices/triangles)
      std::vector<int> ccCandidates;
      for (auto triIdx : ccTriangleList[ccIndex]) {
        const auto start = triangleToTriangle.neighboursStartIndex(triIdx);
        const auto end   = triangleToTriangle.neighboursEndIndex(triIdx);
        for (int32_t n = start; n < end; ++n) {
          const auto nIndex = partition[neighbours[n]];
          if (nIndex != ccIndex) {
            //add to the list of candidates
            if (std::find(ccCandidates.begin(), ccCandidates.end(), nIndex)
                == ccCandidates.end()) {
              if ((ccNumTriangles[nIndex]
                   < (maxNumFaces - ccNumTriangles[ccIndex]))
                  && (ccArea[nIndex]
                      < (totalArea * maxCCAreaRatio - ccArea[ccIndex]))) {
                ccCandidates.push_back(nIndex);
              }
            }
          }
        }
      }
      // merge with the candidate that reduces the cost variable initialization
      double       minCost            = std::numeric_limits<double>::max();
      int          mergeIdx           = -1;
      int          mergeOrientation   = -1;
      double       bestMergePerimeter = 0;
      double       bestMergeStretchL2 = 0;
      Vec3<double> bestNormal(0.0);
      // current connected component
      double noMergePerimeter = ccPerimeter[ccIndex];
      double noMergeStretchL2 = ccStretchL2[ccIndex];
      double noMergeCost      = noMergeStretchL2 + lambda * noMergePerimeter;
      TriangleMesh<double> mergeCCBase;
      std::vector<int64_t> mergeCCBaseEdgeList;
      mergeCCBase.reserveTriangles(ccTriangleList[ccIndex].size());
      mergeCCBase.reservePoints(3 * ccTriangleList[ccIndex].size());
      mergeCCBaseEdgeList.reserve(3 * ccTriangleList[ccIndex].size());
      for (auto& tri : ccTriangleList[ccIndex]) {
        int vIndex[3];
        for (int32_t k = 0; k < 3; ++k) {
          const auto v         = mesh.triangle(tri)[k];
          auto       posVertex = std::find(mergeCCBase.points().begin(),
                                     mergeCCBase.points().end(),
                                     mesh.point(v));
          if (posVertex == mergeCCBase.points().end()) {
            vIndex[k] = mergeCCBase.points().size();
            mergeCCBase.addPoint(mesh.point(v));
            apActiveVertexIndexFlag[v] = vIndex[k] + 1;
          } else {
            vIndex[k] = posVertex - mergeCCBase.points().begin();
          }
        }
        mergeCCBase.addTriangle(vIndex[0], vIndex[1], vIndex[2]);
        mergeCCBaseEdgeList.push_back(EdgeIndex(vIndex[0], vIndex[1]));
        mergeCCBaseEdgeList.push_back(EdgeIndex(vIndex[1], vIndex[2]));
        mergeCCBaseEdgeList.push_back(EdgeIndex(vIndex[2], vIndex[0]));
      }
      // now choose to merge with the neighbor that reduces the cost the most,
      for (auto& ccNeighborIndex : ccCandidates) {
        // neighboring cc (candidate for merge)
        double candPerimeter = ccPerimeter[ccNeighborIndex];
        double candStretchL2 = ccStretchL2[ccNeighborIndex];
        double candCost      = candStretchL2 + lambda * candPerimeter;
        // merged connected component
        TriangleMesh<double> mergeCC;
        mergeCC.reserveTriangles(ccTriangleList[ccIndex].size()
                                 + ccTriangleList[ccNeighborIndex].size());
        mergeCC.reservePoints(3 * ccTriangleList[ccIndex].size()
                              + 3 * ccTriangleList[ccNeighborIndex].size());

        // remove the previous result.
        for (auto v : removeVertexList) { apActiveVertexIndexFlag[v] = 0; }
        removeVertexList.clear();

        mergeCC                = mergeCCBase;
        double commonPerimeter = 0.0;
        for (auto& tri : ccTriangleList[ccNeighborIndex]) {
          int vIndex[3];
          int commonVertex = 0;
          for (int32_t k = 0; k < 3; ++k) {
            const auto v = mesh.triangle(tri)[k];
            if (apActiveVertexIndexFlag[v] == 0) {
              vIndex[k] = mergeCC.points().size();
              mergeCC.addPoint(mesh.point(v));
              apActiveVertexIndexFlag[v] = vIndex[k] + 1;
              removeVertexList.push_back(v);
            } else {
              vIndex[k] = apActiveVertexIndexFlag[v] - 1;
              if (vIndex[k] < mergeCCBase.points().size())
                commonVertex += 1 << k;  // vertex in common with Base CC
            }
          }
          mergeCC.addTriangle(vIndex[0], vIndex[1], vIndex[2]);
          if (commonVertex == 3) {
            // add edge 01
            auto edgeIndexA = EdgeIndex(vIndex[0], vIndex[1]);
            for (auto& baseCCEdgeIndex : mergeCCBaseEdgeList) {
              if (baseCCEdgeIndex == edgeIndexA) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[0]] - mergeCC.points()[vIndex[1]])
                    .norm();
                break;
              }
            }
          } else if (commonVertex == 5) {
            //add edge 20
            auto edgeIndexC = EdgeIndex(vIndex[2], vIndex[0]);
            for (auto& baseCCEdgeIndex : mergeCCBaseEdgeList) {
              if (baseCCEdgeIndex == edgeIndexC) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[2]] - mergeCC.points()[vIndex[0]])
                    .norm();
                break;
              }
            }
          } else if (commonVertex == 6) {
            //add edge 12
            auto edgeIndexB = EdgeIndex(vIndex[1], vIndex[2]);
            for (auto& baseCCEdgeIndex : mergeCCBaseEdgeList) {
              if (baseCCEdgeIndex == edgeIndexB) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[1]] - mergeCC.points()[vIndex[2]])
                    .norm();
                break;
              }
            }
          } else if (commonVertex == 7) {
            //this could be a combination of two or more edges, check which ones exist in the baseCC
            auto edgeIndexA = EdgeIndex(vIndex[0], vIndex[1]);
            auto edgeIndexB = EdgeIndex(vIndex[1], vIndex[2]);
            auto edgeIndexC = EdgeIndex(vIndex[2], vIndex[0]);
            for (auto& baseCCEdgeIndex : mergeCCBaseEdgeList) {
              if (baseCCEdgeIndex == edgeIndexA) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[0]] - mergeCC.points()[vIndex[1]])
                    .norm();
              } else if (baseCCEdgeIndex == edgeIndexB) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[1]] - mergeCC.points()[vIndex[2]])
                    .norm();
              } else if (baseCCEdgeIndex == edgeIndexC) {
                commonPerimeter +=
                  (mergeCC.points()[vIndex[2]] - mergeCC.points()[vIndex[0]])
                    .norm();
              }
            }
          }
        }
        //check the merge orientation by looking at the average normals
        Vec3<double> averageNormal =
          ccAvgNormal[ccIndex] * ccArea[ccIndex]
          + ccAvgNormal[ccNeighborIndex] * ccArea[ccNeighborIndex];
        double mergedArea = ccArea[ccIndex] + ccArea[ccNeighborIndex];
        averageNormal /= mergedArea;
        size_t clusterIndex = 0;
        double bestScore    = averageNormal * orientations[0];
        for (size_t j = 1; j < orientationCount; ++j) {
          const double score = averageNormal * orientations[j];
          if (score > bestScore) {
            bestScore    = score;
            clusterIndex = j;
          }
        }
        double mergePerimeter =
          noMergePerimeter + candPerimeter - 2 * commonPerimeter;
        double mergeStretchL2 = mergeCC.stretchL2(clusterIndex);
        double mergeCost      = mergeStretchL2 + lambda * mergePerimeter;

        // allows merge only if the merge cost is smaller then the separate cost
        if (noMergeCost + candCost > mergeCost) {
          if (mergeCost < minCost) {
            minCost            = mergeCost;
            mergeIdx           = ccNeighborIndex;
            mergeOrientation   = clusterIndex;
            bestMergePerimeter = mergePerimeter;
            bestMergeStretchL2 = mergeStretchL2;
            bestNormal         = averageNormal;
          }
        }
      }
      if (mergeIdx != -1) {
#if DEBUG_ORTHO_MERGE_VERBOSE
        std::cout << ccIndex << "->" << mergeIdx << "[";
        for (auto& ccNeighborIndex : ccCandidates)
          std::cout << ccNeighborIndex << ",";
        std::cout << "]"
                  << "(p:" << bestMergePerimeter << ",s:" << bestMergeStretchL2
                  << ",o:" << mergeOrientation << ",n:[" << bestNormal[0]
                  << "," << bestNormal[1] << "," << bestNormal[2] << "])"
                  << std::endl;
#endif
        // indicating that this index will be merged by updating merge list
        // and respective category
        ccMerge[ccIndex]   = mergeIdx;
        category[mergeIdx] = mergeOrientation;
        for (int i = 0; i < ccMerge.size(); i++)
          if ((ccMerge[i] == ccIndex) || (ccMerge[i] == mergeIdx)) {
            ccMerge[i]  = mergeIdx;
            category[i] = mergeOrientation;
          }
        //updating cc number of triangles
        ccNumTriangles[mergeIdx] += ccNumTriangles[ccIndex];
        ccNumTriangles[ccIndex] = 0;
        //updating area
        ccArea[mergeIdx] += ccArea[ccIndex];
        ccArea[ccIndex] = 0;
        //updating cc normal
        ccAvgNormal[mergeIdx] = bestNormal;
        ccAvgNormal[ccIndex]  = Vec3<double>(0.0);
        //updating cc perimeter
        ccPerimeter[ccIndex]  = 0;
        ccPerimeter[mergeIdx] = bestMergePerimeter;
        //updating cc stretchL2
        ccStretchL2[ccIndex]  = 0;
        ccStretchL2[mergeIdx] = bestMergeStretchL2;
        //updating triangle list
        for (auto tIdx : ccTriangleList[ccIndex])
          ccTriangleList[mergeIdx].push_back(tIdx);
        ccTriangleList[ccIndex].clear();
        //updating vertex list
        for (auto vIdx : ccVertexList[ccIndex]) {
          auto findIndex = std::find(ccVertexList[mergeIdx].begin(),
                                     ccVertexList[mergeIdx].end(),
                                     vIdx);
          if (findIndex == ccVertexList[mergeIdx].end())
            ccVertexList[mergeIdx].push_back(vIdx);
        }
        ccVertexList[ccIndex].clear();
        //updating partition structure
        for (int idx = 0; idx < triangleCount; idx++) {
          if (partition[idx] == ccIndex) { partition[idx] = mergeIdx; }
        }
      } else {
#if DEBUG_ORTHO_MERGE_VERBOSE
        std::cout << ccIndex << "->" << mergeIdx << "[";
        for (auto& ccNeighborIndex : /*ccNeighbour[ccIndex]*/ ccCandidates)
          std::cout << ccNeighborIndex << ",";
        std::cout << "]"
                  << "(p:" << bestMergePerimeter << ",s:" << bestMergeStretchL2
                  << ",o:" << mergeOrientation << ",n:[" << bestNormal[0]
                  << "," << bestNormal[1] << "," << bestNormal[2] << "])"
                  << std::endl;
#endif
      }
      //resort the list
      std::sort(ccIndexSortedList.begin(),
                ccIndexSortedList.end(),
                [&](int A, int B) -> bool {
                  if (ccNumTriangles[A] == ccNumTriangles[B]) {
                    double maxDotA = ccAvgNormal[A] * orientations[0];
                    double maxDotB = ccAvgNormal[B] * orientations[0];
                    for (int i = 1; i < orientationCount; i++) {
                      double dotA = ccAvgNormal[A] * orientations[i];
                      double dotB = ccAvgNormal[B] * orientations[i];
                      if (dotA > maxDotA) maxDotA = dotA;
                      if (dotB > maxDotB) maxDotB = dotB;
                    }
                    if (maxDotA < maxDotB) return true;
                    else if (maxDotA == maxDotB) return (A > B);
                    else return false;
                  } else {
                    if (ccNumTriangles[A] > ccNumTriangles[B]) return true;
                    else if (ccNumTriangles[A] == ccNumTriangles[B])
                      return (A > B);
                    else return false;
                  };
                });
    }
    if (connectedComponents) {
      for (int idx = 0; idx < ccCount; idx++) {
        if (ccMerge[idx] != -1) {
          //move all the triangles from cc[idx] to cc[ccMerge[idx]]
          auto& ccMeshDest = (*connectedComponents)[ccMerge[idx]];
          auto& ccMeshOrig = (*connectedComponents)[idx];
          for (auto tri : ccMeshOrig->triangles()) {
            int vIndex[3];
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = tri[k];
              vIndex[k]    = ccMeshDest->points().size();
              ccMeshDest->addPoint(ccMeshOrig->point(v));
            }
            ccMeshDest->addTriangle(vIndex[0], vIndex[1], vIndex[2]);
          }
          for (auto texCoordTri : ccMeshOrig->texCoordTriangles()) {
            int tIndex[3];
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = texCoordTri[k];
              tIndex[k]    = ccMeshDest->texCoords().size();
              ccMeshDest->addTexCoord(ccMeshOrig->texCoord(v));
            }
            ccMeshDest->addTexCoordTriangle(tIndex[0], tIndex[1], tIndex[2]);
          }
          for (auto normalTri : ccMeshOrig->normalTriangles()) {
            int nIndex[3];
            for (int32_t k = 0; k < 3; ++k) {
              const auto v = normalTri[k];
              nIndex[k]    = ccMeshDest->normals().size();
              ccMeshDest->addNormal(ccMeshOrig->normal(v));
            }
            ccMeshDest->addNormalTriangle(nIndex[0], nIndex[1], nIndex[2]);
          }
          ccMeshOrig->triangles().clear();
        }
      }
      int idxCC = 0;
      while (idxCC < connectedComponents->size()) {
        auto& cc = (*connectedComponents)[idxCC];
        if (cc->triangleCount() == 0) {
          std::swap((*connectedComponents)[idxCC],
                    (*connectedComponents)[connectedComponents->size() - 1]);
          connectedComponents->pop_back();
          std::swap(category[idxCC], category[category.size() - 1]);
          category.pop_back();
          ccCount--;
        }
        if (idxCC < connectedComponents->size()) {
          auto& ccSwapped = (*connectedComponents)[idxCC];
          if (ccSwapped->triangleCount() > 0) idxCC++;
        }
      }
    }
#if DEBUG_ORTHO_MERGE_VERBOSE
    std::cout << "Number of connected components AFTER merge:" << ccCount
              << std::endl;
#endif
    delete[] apActiveVertexIndexFlag;
  }
#if DEBUG_ORTHO_MERGE
  {
    int                  ccIdx = 0;
    TriangleMesh<double> meshDebug;
    double               totalPerimeter = 0;
    double               totalStretchL2 = 0;
    double               minStretchL2   = std::numeric_limits<double>::max();
    double               maxStretchL2   = std::numeric_limits<double>::min();
    for (auto cc : (*connectedComponents)) {
      meshDebug.append(*cc);
#  if DEBUG_ORTHO_MERGE_VERBOSE
      if (keepIntermediateResults) {
        auto prefix =
          keepFilesPathPrefix + "_merge_CC#" + std::to_string(ccIdx);
        cc->save(prefix + ".obj");
      }
      std::cout << "CC[" << ccIdx << "] -> perimeter(" << cc->perimeter()
                << "), stretchL2(" << cc->stretchL2(category[ccIdx]) << ")"
                << std::endl;
#  endif
      totalPerimeter += cc->perimeter();
      auto l2 = cc->stretchL2(category[ccIdx]);
      totalStretchL2 += l2;
      if (l2 > maxStretchL2) maxStretchL2 = l2;
      if (l2 < minStretchL2) minStretchL2 = l2;
      ccIdx++;
    }
    std::cout << "(" << ccIdx
              << " CC after merge) TOTAL PERIMETER: " << totalPerimeter
              << ", TOTAL STRETCH L2: " << totalStretchL2
              << ", AVG. STRETCH L2: " << totalStretchL2 / (double)ccIdx
              << ", MIN. STRETCH L2: " << minStretchL2
              << ", MAX. STRETCH L2: " << maxStretchL2 << std::endl;
    if (keepIntermediateResults) {
      meshDebug.texCoords().clear();
      meshDebug.texCoordTriangles().clear();
      meshDebug.reserveTexCoords(meshDebug.triangleCount() * 3);
      meshDebug.reserveTexCoordTriangles(meshDebug.triangleCount());
      int idxCC       = 0;
      int idxTriangle = 0;
      int side        = 100;
      for (auto cc : (*connectedComponents)) {
        float x = ((category[idxCC] % 9) + 0.5) / (9.0);
        float y = 1 - ((category[idxCC] / 9) + 0.5) / (2.0);
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
                     + "_debug_categories_after_merge.obj");
    }
  }
#endif

  return ccCount;
}

//----------------------------------------------------------------------------

template<typename T>
float
edgeFunction(const Vec2<T>& a, const Vec2<T>& b, const Vec2<T>& c) {
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

//----------------------------------------------------------------------------

template<typename T>
bool
isInsideTriangle(const Vec2<T> V0,
                 const Vec2<T> V1,
                 const Vec2<T> V2,
                 Vec2<T>       P,
                 float&        w0,
                 float&        w1,
                 float&        w2) {
  // check of if current pixel lies in triangle
  // checking the V0-V1 edge
  float w0_edge = edgeFunction(V1, V2, P);
  // checking the V1-V2 edge
  float w1_edge = edgeFunction(V2, V0, P);
  // checking the V2-V0 edge
  float w2_edge = edgeFunction(V0, V1, P);
  if ((w0_edge >= 0 && w1_edge >= 0 && w2_edge >= 0)
      || (w0_edge <= 0 && w1_edge <= 0 && w2_edge <= 0)) {
    // barycentric coordinates are the areas of the sub-triangles divided by the area of the main triangle
    float area = edgeFunction(V0, V1, V2);
    if (area == 0) {
      return false;
    } else {
      w0 = w0_edge / area;
      w1 = w1_edge / area;
      w2 = w2_edge / area;
    }
    return true;
  } else return false;
}

//----------------------------------------------------------------------------

template<typename T>
bool
isInsideTriangleUsingArea(const Vec2<T> A,
                          const Vec2<T> B,
                          const Vec2<T> C,
                          const Vec2<T> P) {
  float A1 = edgeFunction(B, C, P);
  float A2 = edgeFunction(C, A, P);
  float A3 = edgeFunction(A, B, P);
  if ((A1 >= 0 && A2 >= 0 && A3 >= 0) || (A1 <= 0 && A2 <= 0 && A3 <= 0)) {
    // barycentric coordinates are the areas of the sub-triangles divided by the area of the main triangle
    float AT = edgeFunction(A, B, C);
    if (AT == 0) { return false; }
    return true;
  } else return false;
}

//----------------------------------------------------------------------------

// Given three collinear points p, q, r, the function checks if
// point q lies on line segment 'pr'
template<typename T>
bool
onSegment(const Vec2<T> p, const Vec2<T> q, const Vec2<T> r) {
  if (q[0] <= std::max<T>(p[0], r[0]) && q[0] >= std::min<T>(p[0], r[0])
      && q[1] <= std::max<T>(p[1], r[1]) && q[1] >= std::min<T>(p[1], r[1]))
    return true;

  return false;
}

// To find orientation of ordered triplet (p, q, r).
// The function returns following values
// 0 --> p, q and r are collinear
// 1 --> Clockwise
// 2 --> Counterclockwise
template<typename T>
int
orientation(const Vec2<T> p, const Vec2<T> q, const Vec2<T> r) {
  // See https://www.geeksforgeeks.org/orientation-3-ordered-points/
  // for details of below formula.
  int val = (q[1] - p[1]) * (r[0] - q[0]) - (q[0] - p[0]) * (r[1] - q[1]);

  if (val == 0) return 0;  // collinear

  return (val > 0) ? 1 : 2;  // clock or counterclock wise
}

// The main function that returns true if line segment 'p1q1'
// and 'p2q2' intersect.
template<typename T>
bool
edgesCross(const Vec2<T> p1,
           const Vec2<T> q1,
           const Vec2<T> p2,
           const Vec2<T> q2) {
  // Find the four orientations needed for general and
  // special cases
  int o1 = orientation(p1, q1, p2);
  int o2 = orientation(p1, q1, q2);
  int o3 = orientation(p2, q2, p1);
  int o4 = orientation(p2, q2, q1);

  // General case
  if (o1 != o2 && o3 != o4) return true;

  // Special Cases
  // p1, q1 and p2 are collinear and p2 lies on segment p1q1
  if (o1 == 0 && onSegment(p1, p2, q1)) return true;

  // p1, q1 and q2 are collinear and q2 lies on segment p1q1
  if (o2 == 0 && onSegment(p1, q2, q1)) return true;

  // p2, q2 and p1 are collinear and p1 lies on segment p2q2
  if (o3 == 0 && onSegment(p2, p1, q2)) return true;

  // p2, q2 and q1 are collinear and q1 lies on segment p2q2
  if (o4 == 0 && onSegment(p2, q1, q2)) return true;

  return false;  // Doesn't fall in any of the above cases}
}
//----------------------------------------------------------------------------

template<typename T>
bool
sampleAndProjectTriangleAddSomeMap(int32_t                     dectri,
                                   std::vector<int32_t>&       vadjs,
                                   const std::vector<int32_t>& NeighVadjs,
                                   const TriangleMesh<T>&      decimate,
                                   const TriangleMesh<T>&      mapped,
                                   std::vector<Vec3<T>>& errorRayOrigin) {
  auto    tri    = decimate.triangle(dectri);
  Vec3<T> v1     = decimate.point(tri[0]);
  Vec3<T> v2     = decimate.point(tri[1]);
  Vec3<T> v3     = decimate.point(tri[2]);
  Vec3<T> edge1  = v2 - v1;
  Vec3<T> edge2  = v3 - v1;
  Vec3<T> normal = edge1.cross(edge2);
  normal.normalize();

  int errorRay   = 0;
  T   area       = (edge1.cross(edge2).norm()) / 2;
  int numSamples = static_cast<int>(std::sqrt(area) * 10);
  numSamples     = std::max(5, std::min(numSamples, 10));

  for (int i = 1; i <= numSamples; ++i) {
    for (int j = 1; j <= numSamples - i; ++j) {
      T u = static_cast<T>(i) / static_cast<T>(numSamples);
      T v = static_cast<T>(j) / static_cast<T>(numSamples);
      if (u + v > 1.0) continue;
      T       w                 = 1.0 - u - v;
      Vec3<T> rayOrigin         = u * v1 + v * v2 + w * v3;
      Vec3<T> directions[2]     = {normal, -normal};
      bool    foundIntersection = false;
      for (int32_t adjTri : vadjs) {
        for (const auto& dir : directions) {
          auto    maptri = mapped.triangle(adjTri);
          Vec3<T> mv0    = mapped.point(maptri[0]);
          Vec3<T> mv1    = mapped.point(maptri[1]);
          Vec3<T> mv2    = mapped.point(maptri[2]);
          Vec3<T> res;
          if (projectPointToTriangle(rayOrigin, dir, mv0, mv1, mv2, res)) {
            foundIntersection = true;
            break;
          }
        }
        if (foundIntersection) break;
      }
      if (!foundIntersection) {
        for (int32_t adjTri : NeighVadjs) {
          for (const auto& dir : directions) {
            auto    maptri = mapped.triangle(adjTri);
            Vec3<T> mv0    = mapped.point(maptri[0]);
            Vec3<T> mv1    = mapped.point(maptri[1]);
            Vec3<T> mv2    = mapped.point(maptri[2]);
            Vec3<T> res;
            if (projectPointToTriangle(rayOrigin, dir, mv0, mv1, mv2, res)) {
              foundIntersection = true;
              if (std::find(vadjs.begin(), vadjs.end(), adjTri) == vadjs.end())
                vadjs.push_back(adjTri);
              break;
            }
          }
          if (foundIntersection) break;
        }
        if (!foundIntersection) errorRayOrigin.push_back(rayOrigin);
      }
    }
  }
  if (errorRayOrigin.size()) {
    return false;
  } else {
    return true;
  }
}
//----------------------------------------------------------------------------------

template<typename T>
bool
sampleAndProjectTriangle(int32_t                     dectri,
                         const std::vector<int32_t>& vadjs,
                         const TriangleMesh<T>&      decimate,
                         const TriangleMesh<T>&      mapped) {
  auto    tri    = decimate.triangle(dectri);
  Vec3<T> v1     = decimate.point(tri[0]);
  Vec3<T> v2     = decimate.point(tri[1]);
  Vec3<T> v3     = decimate.point(tri[2]);
  Vec3<T> edge1  = v2 - v1;
  Vec3<T> edge2  = v3 - v1;
  Vec3<T> normal = edge1.cross(edge2);
  normal.normalize();

  T   area       = (edge1.cross(edge2).norm()) / 2;
  int numSamples = static_cast<int>(std::sqrt(area) * 10);

  numSamples = 4;
  for (int i = 1; i <= numSamples; ++i) {
    for (int j = 1; j <= numSamples - i; ++j) {
      T u = static_cast<T>(i) / static_cast<T>(numSamples);
      T v = static_cast<T>(j) / static_cast<T>(numSamples);
      if (u + v > 1.0) continue;
      T       w                 = 1.0 - u - v;
      Vec3<T> rayOrigin         = u * v1 + v * v2 + w * v3;
      Vec3<T> directions[2]     = {normal, -normal};
      bool    foundIntersection = false;
      for (int32_t adjTri : vadjs) {
        for (const auto& dir : directions) {
          auto    maptri = mapped.triangle(adjTri);
          Vec3<T> mv0    = mapped.point(maptri[0]);
          Vec3<T> mv1    = mapped.point(maptri[1]);
          Vec3<T> mv2    = mapped.point(maptri[2]);
          Vec3<T> res;
          if (projectPointToTriangle(rayOrigin, dir, mv0, mv1, mv2, res)) {
            foundIntersection = true;
            break;
          }
        }
        if (foundIntersection) break;
      }
      if (!foundIntersection) return false;
    }
  }
  return true;
}
//------------------------------------------------------------------------------------
template<typename T>
bool
projectPointToTriangle(const Vec3<T>& rayOrigin,
                       const Vec3<T>& rayDirection,
                       const Vec3<T>& v0,
                       const Vec3<T>& v1,
                       const Vec3<T>& v2,
                       Vec3<T>&       res,
                       T              epsilon = static_cast<T>(1e-6)) {
  Vec3<T> edge1 = v1 - v0;
  Vec3<T> edge2 = v2 - v0;
  Vec3<T> pvec  = rayDirection.cross(edge2);

  T det = edge1 * pvec;

  if (std::fabs(det) < epsilon) return false;

  T       inv_det = static_cast<T>(1.0) / det;
  Vec3<T> tvec    = rayOrigin - v0;

  T u = tvec * pvec * inv_det;
  if (u < 0.0 || u > 1.0) return false;

  Vec3<T> qvec = tvec.cross(edge1);
  T       v    = rayDirection * qvec * inv_det;
  if (v < 0.0 || (u + v) > 1.0) return false;

  T t = edge2 * qvec * inv_det;

  if (t < 0) return false;

  res = rayOrigin + t * rayDirection;

  return true;
}
//------------------------------------------------------------------------------

template<typename T>
void
extendVadjs(std::vector<int32_t>& vadjs, const TriangleMesh<T>& mapped) {
  StaticAdjacencyInformation<int32_t> vertexToTriangleMappedorg;
  std::vector<int32_t>                newvadjs = vadjs;
  ComputeVertexToTriangle(
    mapped.triangles(), mapped.pointCount(), vertexToTriangleMappedorg);
  const auto* neighboursMappedorg = vertexToTriangleMappedorg.neighbours();
  for (int32_t mapt : newvadjs) {
    auto tri = mapped.triangle(mapt);
    for (int k = 0; k < 3; k++) {
      auto       tindex = tri[k];
      const auto start =
        vertexToTriangleMappedorg.neighboursStartIndex(tindex);
      const auto end = vertexToTriangleMappedorg.neighboursEndIndex(tindex);
      for (int32_t n = start; n < end; ++n) {
        auto adjFace = neighboursMappedorg[n];
        if (std::find(vadjs.begin(), vadjs.end(), adjFace) == vadjs.end())
          vadjs.push_back(adjFace);
      }
    }
  }
}
//---------------------------------------------------------------------------------------

template<typename T>
void
findNeighbourVadjs(const std::vector<int32_t>& vadjs,
                   std::vector<int32_t>&       newvadjs,
                   const TriangleMesh<T>&      mapped) {
  StaticAdjacencyInformation<int32_t> vertexToTriangleMappedorg;
  ComputeVertexToTriangle(
    mapped.triangles(), mapped.pointCount(), vertexToTriangleMappedorg);
  const auto* neighboursMappedorg = vertexToTriangleMappedorg.neighbours();
  for (int32_t mapt : vadjs) {
    auto tri = mapped.triangle(mapt);
    for (int k = 0; k < 3; k++) {
      auto       tindex = tri[k];
      const auto start =
        vertexToTriangleMappedorg.neighboursStartIndex(tindex);
      const auto end = vertexToTriangleMappedorg.neighboursEndIndex(tindex);
      for (int32_t n = start; n < end; ++n) {
        auto adjFace = neighboursMappedorg[n];
        if (std::find(newvadjs.begin(), newvadjs.end(), adjFace)
            == newvadjs.end())
          newvadjs.push_back(adjFace);
      }
    }
  }
}
//---------------------------------------------------------------------------------------

template<typename T>
void
SmoothWithVertexConstraints(
  TriangleMesh<T>&                           mesh,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangle,
  const std::vector<int8_t>&                 isBoundaryVertex,
  std::vector<int8_t>&                       vtags,
  std::vector<int8_t>&                       ttags,
  const double                               smoothingCoefficient,
  const double                               maxError          = 0.001,
  const int32_t                              maxIterationCount = -1) {
  const auto pointCount    = mesh.pointCount();
  const auto triangleCount = mesh.triangleCount();
  if (!pointCount || !triangleCount || vertexToTriangle.size() != pointCount
      || vtags.size() != size_t(pointCount)
      || ttags.size() != size_t(triangleCount)) {
    return;
  }

  std::vector<int32_t> vadj;
  std::vector<int32_t> tadj;
  const auto           n = 2 * pointCount;
  SparseMatrix<T>      A(n, pointCount);

  for (int vindex0 = 0; vindex0 < pointCount; ++vindex0) {
    ComputeAdjacentVertices(
      vindex0, mesh.triangles(), vertexToTriangle, vtags, vadj);
    const auto neighbourCount = int32_t(vadj.size());

    if (isBoundaryVertex[vindex0] && neighbourCount) {
      int32_t boundaryNeighbourCount = 0;
      int32_t bneighbours[3];
      for (int i = 0; i < neighbourCount && boundaryNeighbourCount < 3; ++i) {
        const auto vindex1 = vadj[i];
        if (isBoundaryVertex[vindex1]) {
          bneighbours[boundaryNeighbourCount++] = vindex1;
        }
      }
      if (boundaryNeighbourCount == 2) {
        A.addElementInOrder(vindex0, bneighbours[0], -smoothingCoefficient);
        A.addElementInOrder(vindex0, bneighbours[1], -smoothingCoefficient);
        A.addElementInOrder(vindex0, vindex0, 2 * smoothingCoefficient);
      } else {
        A.addElementInOrder(vindex0, vindex0, T(0));
      }
    } else {
      for (int i = 0; i < neighbourCount; ++i) {
        A.addElementInOrder(vindex0, vadj[i], -smoothingCoefficient);
      }
      A.addElementInOrder(
        vindex0, vindex0, neighbourCount * smoothingCoefficient);
    }
  }
  for (int vindex0 = 0; vindex0 < pointCount; ++vindex0) {
    A.addElementInOrder(
      pointCount + vindex0, vindex0, /*weights ? weights[vindex0] :*/ T(1));
  }

  A.updatePointers();
  const auto At = A.transpose();
  const auto itCount =
    maxIterationCount == -1 ? pointCount * 2 : maxIterationCount;
  VecN<T> b(n);
  VecN<T> x(pointCount);
  VecN<T> y(n);
  VecN<T> p(pointCount);
  VecN<T> r(pointCount);
  VecN<T> q(pointCount);
  b = T(0);
  for (int32_t dim = 0; dim < 3; ++dim) {
    x = T(0);
    for (int32_t i = 0; i < pointCount; ++i) {
      b[pointCount + i] =
        /*weights ? weights[i] * mesh.point(i)[dim] :*/ mesh.point(i)[dim];
    }
    y     = b - A * x;
    r     = At * y;
    p     = r;
    T rtr = T(0);
    for (int32_t i = 0; i < pointCount; ++i) { rtr += r[i] * r[i]; }
    T       error = std::sqrt(rtr / pointCount);
    int32_t it    = 0;
    for (; it < itCount && error > maxError; ++it) {
      y       = A * p;
      q       = At * y;
      T pAtAp = T(0);
      for (int32_t i = 0; i < pointCount; ++i) { pAtAp += p[i] * q[i]; }
      const auto alpha = rtr / pAtAp;
      for (int32_t i = 0; i < pointCount; ++i) {
        x[i] += alpha * p[i];
        r[i] -= alpha * q[i];
      }
      T r1tr1 = T(0);
      for (int32_t i = 0; i < pointCount; ++i) { r1tr1 += r[i] * r[i]; }
      const T betha = r1tr1 / rtr;
      for (int32_t i = 0; i < pointCount; ++i) { p[i] = r[i] + betha * p[i]; }
      rtr   = r1tr1;
      error = std::sqrt(rtr / pointCount);
    }
    for (int32_t i = 0; i < pointCount; ++i) {
      Vec3<T>& point = mesh.point(i);
      point[dim]     = x[i];
    }
  }
}

//----------------------------------------------------------------------------

template<typename T>
void
SmoothWithVertexConstraints(TriangleMesh<T>& mesh,
                            const double     smoothingCoefficient,
                            const double     maxError          = 0.001,
                            const int32_t    maxIterationCount = -1) {
  const auto pointCount    = mesh.pointCount();
  const auto triangleCount = mesh.triangleCount();
  if (!pointCount || !triangleCount) { return; }
  StaticAdjacencyInformation<int32_t> vertexToTriangle;
  ComputeVertexToTriangle(
    mesh.triangles(), mesh.pointCount(), vertexToTriangle);

  const auto&         triangles = mesh.triangles();
  std::vector<int8_t> vtags(pointCount);
  std::vector<int8_t> ttags(triangleCount);
  std::vector<int8_t> isBoundaryVertex;
  ComputeBoundaryVertices(
    triangles, vertexToTriangle, vtags, ttags, isBoundaryVertex);

  std::vector<int32_t> vadj;
  std::vector<int32_t> tadj;
  const auto           n = 2 * pointCount;
  SparseMatrix<T>      A(n, pointCount);

  for (int vindex0 = 0; vindex0 < pointCount; ++vindex0) {
    ComputeAdjacentVertices(vindex0, triangles, vertexToTriangle, vtags, vadj);
    const auto neighbourCount = int32_t(vadj.size());

    if (isBoundaryVertex[vindex0] && neighbourCount) {
      int32_t boundaryNeighbourCount = 0;
      int32_t bneighbours[3];
      for (int i = 0; i < neighbourCount && boundaryNeighbourCount < 3; ++i) {
        const auto vindex1 = vadj[i];
        if (isBoundaryVertex[vindex1]) {
          bneighbours[boundaryNeighbourCount++] = vindex1;
        }
      }
      if (boundaryNeighbourCount == 2) {
        A.addElementInOrder(vindex0, bneighbours[0], -smoothingCoefficient);
        A.addElementInOrder(vindex0, bneighbours[1], -smoothingCoefficient);
        A.addElementInOrder(vindex0, vindex0, 2 * smoothingCoefficient);
      } else {
        A.addElementInOrder(vindex0, vindex0, T(0));
      }
    } else {
      for (int i = 0; i < neighbourCount; ++i) {
        A.addElementInOrder(vindex0, vadj[i], -smoothingCoefficient);
      }
      A.addElementInOrder(
        vindex0, vindex0, neighbourCount * smoothingCoefficient);
    }
  }
  for (int vindex0 = 0; vindex0 < pointCount; ++vindex0) {
    A.addElementInOrder(pointCount + vindex0, vindex0, T(1));
  }

  A.updatePointers();
  const auto At = A.transpose();
  const auto itCount =
    maxIterationCount == -1 ? pointCount * 2 : maxIterationCount;
  VecN<double> b(n);
  VecN<double> x(pointCount);
  VecN<double> y(n);
  VecN<double> p(pointCount);
  VecN<double> r(pointCount);
  VecN<double> q(pointCount);
  b = T(0);
  for (int32_t dim = 0; dim < 3; ++dim) {
    x = T(0);
    for (int32_t i = 0; i < pointCount; ++i) {
      b[pointCount + i] = mesh.point(i)[dim];
    }
    y     = b - A * x;
    r     = At * y;
    p     = r;
    T rtr = T(0);
    for (int32_t i = 0; i < pointCount; ++i) { rtr += r[i] * r[i]; }
    T       error = std::sqrt(rtr / pointCount);
    int32_t it    = 0;
    for (; it < itCount && error > maxError; ++it) {
      y       = A * p;
      q       = At * y;
      T pAtAp = T(0);
      for (int32_t i = 0; i < pointCount; ++i) { pAtAp += p[i] * q[i]; }
      const auto alpha = rtr / pAtAp;
      for (int32_t i = 0; i < pointCount; ++i) {
        x[i] += alpha * p[i];
        r[i] -= alpha * q[i];
      }
      T r1tr1 = T(0);
      for (int32_t i = 0; i < pointCount; ++i) { r1tr1 += r[i] * r[i]; }
      const T betha = r1tr1 / rtr;
      for (int32_t i = 0; i < pointCount; ++i) { p[i] = r[i] + betha * p[i]; }
      rtr   = r1tr1;
      error = std::sqrt(rtr / pointCount);
    }
    for (int32_t i = 0; i < pointCount; ++i) {
      Vec3<T>& point = mesh.point(i);
      point[dim]     = x[i];
    }
  }
}

//----------------------------------------------------------------------------

template<typename T>
int32_t
ComputeMidPointSubdivisionMatrix(int32_t                prevPointCount,
                                 std::vector<Triangle>& triangles,
                                 int subdivisionIterationCount,
                                 int iterationIndex,
                                 std::vector<int64_t>& edges,
                                 SparseMatrix<T>&      M) {
  const auto   triangleCount = int32_t(triangles.size());
  const size_t pointCount    = edges.size();
  const int    newPointSize =
    2 * (pointCount - prevPointCount) + 3 * triangleCount;

  const size_t newPointCount = pointCount + newPointSize;

  M.initialize(pointCount, prevPointCount);
  for (int32_t vindex0 = 0; vindex0 < prevPointCount; ++vindex0) {
    M.addElementInOrder(vindex0, vindex0, T(1));
  }

  // create element
  const auto r = T(1) / 2;
  for (int32_t vindex = prevPointCount; vindex < pointCount; vindex++) {
    int vindex0 = edges[vindex] >> 32;
    int vindex1 = edges[vindex] & 0xFFFFFFFF;
    M.addElementInOrder(vindex, vindex0, r);
    M.addElementInOrder(vindex, vindex1, r);
  }

  // update edges
  if (iterationIndex < subdivisionIterationCount - 1) {
    edges.resize(newPointCount);

    for (int32_t vindex = prevPointCount; vindex < pointCount; ++vindex) {
      int eindex        = 2 * (vindex - prevPointCount) + pointCount;
      int vindex0       = edges[vindex] >> 32;
      int vindex1       = edges[vindex] & 0xFFFFFFFF;
      edges[eindex]     = PACK_EDGE_INFO(vindex0, vindex);
      edges[eindex + 1] = PACK_EDGE_INFO(vindex1, vindex);
    }
    for (int32_t t = 0; t < triangleCount; ++t) {
      int eindex = 3 * t + 3 * pointCount - 2 * prevPointCount;
      for (int k = 0; k < 3; ++k) {
        int vindex0       = triangles[t][k];
        int vindex1       = triangles[t][(k + 1) % 3];
        int vmin          = std::min(vindex0, vindex1);
        int vmax          = std::max(vindex0, vindex1);
        edges[eindex + k] = PACK_EDGE_INFO(vmin, vmax);
      }
    }
  }

  // create triangles
  triangles.resize(triangleCount * 4);

  int32_t triangleCounter = triangleCount;
  for (int32_t t = 0; t < triangleCount; ++t) {
    const auto tri = triangles[t];
    if (iterationIndex < subdivisionIterationCount - 1) {
      Triangle tri0;
      int      subedges[3][2];
      for (int32_t k = 0; k < 3; ++k) {
        tri0[k]     = 3 * (t - triangleCount) + k + newPointCount;
        int vindex  = edgeToVertexInTriangle(tri, k, edges);
        int eindex0 = 2 * (tri[k] - prevPointCount) + pointCount;
        int eindex1 = eindex0 + 1;
        if (vindex != (int32_t)(edges[tri[k]] >> 32)) {
          std::swap(eindex0, eindex1);
        }
        subedges[k][0] = eindex0;
        subedges[k][1] = eindex1;
      }
      triangles[t] = tri0;
      triangles[triangleCounter++] =
        Triangle(subedges[0][0], tri0[2], subedges[2][1]);
      triangles[triangleCounter++] =
        Triangle(subedges[0][1], subedges[1][0], tri0[0]);
      triangles[triangleCounter++] =
        Triangle(tri0[1], subedges[1][1], subedges[2][0]);
    } else {
      triangles[triangleCounter++] =
        Triangle(edgeToVertexInTriangle(tri, 0, edges), tri[0], tri[2]);
      triangles[triangleCounter++] =
        Triangle(tri[0], edgeToVertexInTriangle(tri, 1, edges), tri[1]);
      triangles[triangleCounter++] =
        Triangle(tri[2], tri[1], edgeToVertexInTriangle(tri, 2, edges));
    }
  }

  M.updatePointers();
  return pointCount;
}

//----------------------------------------------------------------------------

template<typename T>
void
FitMidPointSubdivision(const TriangleMesh<T>& referenceMesh,
                       TriangleMesh<T>&       baseMesh,
                       const int32_t          subdivIterationCount = 1,
                       const T                maxError             = 0.0001,
                       const int32_t          maxIterationCount    = -1) {
  const auto vCount =
    baseMesh.pointCount() * std::pow(4, subdivIterationCount - 1);
  const auto tCount =
    baseMesh.triangleCount() * std::pow(4, subdivIterationCount - 1);
  std::vector<Triangle> triangles;
  triangles.reserve(4 * tCount);
  triangles.resize(baseMesh.triangleCount());
  std::copy(baseMesh.triangles().begin(),
            baseMesh.triangles().end(),
            triangles.begin());

  StaticAdjacencyInformation<int32_t> vertexToTriangle;
  StaticAdjacencyInformation<int32_t> vertexToEdge;
  std::vector<int8_t>                 vtags;

  vertexToTriangle.reserve(vCount);
  vertexToEdge.reserve(vCount);
  vtags.reserve(vCount);

  std::vector<SparseMatrix<T>> M(subdivIterationCount);
  std::vector<SparseMatrix<T>> Mt(subdivIterationCount);
  int32_t                      vertexCount1 = baseMesh.pointCount();
  std::vector<int64_t>         edges(4 * tCount, int64_t(-1));
  if (subdivIterationCount > 0) {
    initializeSubdiv(vertexCount1,
                     triangles,
                     false,
                     baseMesh.valences(),
                     vertexToTriangle,
                     vertexToEdge,
                     vtags,
                     edges);
  }
  for (int32_t s = 0; s < subdivIterationCount; ++s) {
    vertexCount1 = ComputeMidPointSubdivisionMatrix(
      vertexCount1, triangles, subdivIterationCount, s, edges, M[s]);
    Mt[s] = M[s].transpose();
  }

  const auto vertexCount0 = baseMesh.pointCount();
  const auto n            = M[subdivIterationCount - 1].rowCount();
  const auto itCount =
    maxIterationCount == -1 ? 2 * vertexCount0 : maxIterationCount;
  VecN<T> b(n) /*, q(vertexCount0), r(vertexCount0)*/;

  std::vector<VecN<T>> R(subdivIterationCount + 1);
  std::vector<VecN<T>> P(subdivIterationCount + 1);
  std::vector<VecN<T>> Q(subdivIterationCount + 1);
  std::vector<VecN<T>> X(subdivIterationCount + 1);
  auto&                x = X[0];
  x.resize(vertexCount0);

  b = T(0);
  for (int32_t dim = 0; dim < 3; ++dim) {
    for (int32_t v = 0; v < vertexCount0; ++v) {
      x[v] = baseMesh.point(v)[dim];
    }
    for (int32_t v = 0; v < n; ++v) { b[v] = referenceMesh.point(v)[dim]; }
    for (int32_t s = 0; s < subdivIterationCount; ++s) {
      X[s + 1] = M[s] * X[s];
    }
    R[subdivIterationCount] = b - X[subdivIterationCount];  // y = b - A x
    for (int32_t s = subdivIterationCount - 1; s >= 0; --s) {
      R[s] = Mt[s] * R[s + 1];
    }
    auto& r = R[0];  // r = At * y
    auto& p = P[0];
    p       = r;
    T rtr   = T(0);
    for (int32_t i = 0; i < vertexCount0; ++i) { rtr += r[i] * r[i]; }
    T error = std::sqrt(rtr / vertexCount0);
    //    std::cout << "0 -> " << error << '\n';

    int32_t it = 0;
    for (; it < itCount && error > maxError; ++it) {
      for (int32_t s = 0; s < subdivIterationCount; ++s) {
        P[s + 1] = M[s] * P[s];
      }

      Q[subdivIterationCount] = P[subdivIterationCount];  // y = A p
      for (int32_t s = subdivIterationCount - 1; s >= 0; --s) {
        Q[s] = Mt[s] * Q[s + 1];
      }
      auto& q = Q[0];  // q = At y

      T pAtAp = T(0);
      for (int32_t i = 0; i < vertexCount0; ++i) { pAtAp += p[i] * q[i]; }
      const auto alpha = rtr / pAtAp;
      for (int32_t i = 0; i < vertexCount0; ++i) {
        x[i] += alpha * p[i];
        r[i] -= alpha * q[i];
      }
      T r1tr1 = T(0);
      for (int32_t i = 0; i < vertexCount0; ++i) { r1tr1 += r[i] * r[i]; }
      const T betha = r1tr1 / rtr;
      for (int32_t i = 0; i < vertexCount0; ++i) {
        p[i] = r[i] + betha * p[i];
      }
      rtr   = r1tr1;
      error = std::sqrt(rtr / vertexCount0);
      //      std::cout << it << " -> "<< error << '\n';
    }
    for (int32_t i = 0; i < vertexCount0; ++i) {
      Vec3<T>& point = baseMesh.point(i);
      point[dim]     = x[i];
    }
  }
}

template<class T>
int8_t
computeCoherence(const T signal, const T signal_v1, const T signal_v2) {
  auto dotproduct_v1 = signal[0] * signal_v1[0] + signal[1] * signal_v1[1]
                       + signal[2] * signal_v1[2];
  auto dotproduct_v2 = signal[0] * signal_v2[0] + signal[1] * signal_v2[1]
                       + signal[2] * signal_v2[2];

  if (dotproduct_v1 > dotproduct_v2) {
    // printf("v1 is more coherent\n");
    return 0;
  } else {
    // printf("v2 is more coherent\n");
    return 1;
  }
}

template<class T1, class T2>
void
applyDirectionalWeights(std::vector<T1>&          signal,
                        int32_t                   v,
                        int32_t                   v1,
                        int32_t                   v2,
                        const std::vector<double> dirScale,
                        const T2                  predWeight,
                        const T2                  updateWeight1,
                        const T2                  updateWeight2,
						double                    scale1,
                        double                    scale2,
                        double                    scale3,
                        const bool                isPredStep,
                        const bool                isInverse) {
  auto d1  = updateWeight1 * signal[v];
  auto d2  = updateWeight2 * signal[v];
  auto ps1 = predWeight * signal[v1];
  auto ps2 = predWeight * signal[v2];

  auto mean_global = dirScale[0];
  auto std_global  = dirScale[1];

  if (isInverse) {
    auto signal_v_rec =
      isPredStep ? signal[v]
                 : (signal[v] + predWeight * (signal[v1] + signal[v2]));
    int8_t coherence = computeCoherence(signal_v_rec, signal[v1], signal[v2]);
    double z_score   = std::abs(signal_v_rec[0] - mean_global);
    auto   weight    = (z_score < std_global)         ? scale1
                       : (z_score < (2 * std_global)) ? scale2
                                                      : scale3;
    auto weight_normalized = pow((weight), (isPredStep == false ? 2 : 1)) - 1.;
    if (coherence <= 1) {
      if (isPredStep) {
        auto ps = coherence ? ps1 : ps2;
        signal[v] += weight_normalized * ps;
      } else {
        auto ps = coherence ? v1 : v2;
        auto d  = ps == v1 ? d1 : d2;
        signal[ps] -= weight_normalized * d;
      }
    }
  } else {
    auto signal_v1_rec = signal[v1] - d1;
    auto signal_v2_rec = signal[v2] - d2;
    auto signal_v_rec =
      isPredStep ? (signal[v] + predWeight * (signal[v1] + signal[v2]))
                 : (signal[v] + predWeight * (signal_v1_rec + signal_v2_rec));
    int8_t coherence =
      computeCoherence(signal_v_rec,
                       isPredStep ? signal[v1] : signal_v1_rec,
                       isPredStep ? signal[v2] : signal_v2_rec);
    double deviation       = isPredStep ? (signal[v][0] - mean_global)
                                        : (signal_v_rec[0] - mean_global);
    double z_score         = std::abs(deviation);
    auto   weight          = (z_score < std_global)         ? scale1
                             : (z_score < (2 * std_global)) ? scale2
                                                            : scale3;
    auto weight_normalized = pow((weight), (isPredStep == false ? 2 : 1)) - 1.;
    if (coherence <= 1) {
      if (isPredStep) {
        auto ps = coherence ? ps1 : ps2;
        signal[v] -= weight_normalized * ps;
      } else {
        auto ps = coherence ? v1 : v2;
        auto d  = ps == v1 ? d1 : d2;
        signal[ps] += weight_normalized * d;
      }
    }
  }
}

template<class T>
double
computeStd(const T signal) {
  float   sum   = 0.0f;
  int32_t count = 0;
  for (const auto& val : signal) {
    sum += val;
    ++count;
  }
  float mean     = sum / count;
  float variance = 0.0f;
  for (const auto& val : signal) { variance += (val - mean) * (val - mean); }
  variance /= count;
  double std_dev = std::sqrt(variance);
  return std_dev;
}
template<class T1>
void
computeForwardLinearLifting(
  std::vector<T1>&                                signal,
  const std::vector<vmesh::SubdivisionLevelInfo>& infoLevelOfDetails,
  const std::vector<int64_t>&                     edges,
  const std::vector<int>&                         valences,
  const bool                                      valenceUpdateWeightFlag,
  std::vector<double>*                            liftingdisplodmean,
  const bool                                      liftingOffset,
  const bool                                      skipUpdate,
  const bool                                      adaptivePredictionWeightFlag,
  const std::vector<uint32_t>&                    adaptivePredWeightNr,
  const std::vector<uint32_t>&                    adaptivePredWeightDrMinus1,
  const std::vector<uint32_t>&                    adaptivePredWeightNrDefault,
  const std::vector<uint32_t>& adaptivePredWeightDrMinus1Default,
  const bool                   adaptiveUpdateWeightFlag,
  const std::vector<uint32_t>& adaptiveUpdateWeightNr,
  const std::vector<uint32_t>& adaptiveUpdateWeightDrMinus1,
  std::vector<double>*         liftingdispstat,
  const bool                   dirlift,
  const uint32_t               dirScale1,
  const uint32_t               dirDeltaScale2,
  const uint32_t               dirDeltaScale3,
  const uint32_t               dirScaleDenoMinus1)
    {
  const auto lodCount = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  const auto rfmtCount = lodCount - 1;
  auto&      lodmean   = *liftingdisplodmean;
  lodmean.resize(rfmtCount);

  std::vector<double> predWeight;
  predWeight.resize(lodCount - 1, 0);
  for (int l = 0; l < lodCount - 1; l++) {
    if (adaptivePredictionWeightFlag) {
      predWeight[l] = (double)(adaptivePredWeightNr[l])
                      / (double)(adaptivePredWeightDrMinus1[l] + 1);
    } else {
      predWeight[l] = (double)(adaptivePredWeightNrDefault[l])
                      / (double)(adaptivePredWeightDrMinus1Default[l] + 1);
    }
  }
  std::vector<double> updateWeight;
  updateWeight.resize(lodCount - 1, 0);
  if (skipUpdate) {
    // updateWeight[l] = 0
  } else {
    for (int l = 0; l < lodCount - 1; l++) {
      if ((adaptiveUpdateWeightFlag == 1) || (l == 0)) {
        updateWeight[l] = (double)adaptiveUpdateWeightNr[l]
                          / (double)(adaptiveUpdateWeightDrMinus1[l] + 1);

      } else {
        updateWeight[l] = updateWeight[0];
      }
    }
  }
  
  double scale1 = (double)dirScale1 / (double)(dirScaleDenoMinus1+1);
  double scale2 = scale1 + ((double)dirDeltaScale2 / (double)(dirScaleDenoMinus1+1));
  double scale3 = scale2 + ((double)dirDeltaScale3 / (double)(dirScaleDenoMinus1+1));
  auto& dirliftstats = *liftingdispstat;
  if (dirlift) {
    dirliftstats.resize(2);
    auto                signal_copy = signal;
    std::vector<double> signal_copy_global;
    signal_copy_global.reserve(signal.size());
    for (int32_t it = rfmtCount - 1; it >= 0; --it) {
      const auto vcount0 = infoLevelOfDetails[it].pointCount;
      const auto vcount1 = infoLevelOfDetails[it + 1].pointCount;
      assert(vcount0 < vcount1 && vcount1 <= int32_t(signal.size()));
      for (int32_t v = vcount0; v < vcount1; ++v) {
        const auto edge = edges[v];
        const auto v1   = int32_t(edge & 0xFFFFFFFF);
        const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
        assert(v1 >= 0 && v1 <= vcount0);
        assert(v2 >= 0 && v2 <= vcount0);
        signal_copy[v][0] -= predWeight[it] * (signal[v1][0] + signal[v2][0]);
        signal_copy_global.push_back(signal_copy[v][0]);
      }
    }
    auto   std_global = computeStd(signal_copy_global);
    double sum_global = std::accumulate(
      signal_copy_global.begin(), signal_copy_global.end(), 0.0);
    double mean_global = sum_global / signal_copy_global.size();
    dirliftstats[0]    = mean_global;
    dirliftstats[1]    = std_global;
  }

  for (int32_t it = rfmtCount - 1; it >= 0; --it) {
    int        count   = 0;
    const auto vcount0 = infoLevelOfDetails[it].pointCount;
    const auto vcount1 = infoLevelOfDetails[it + 1].pointCount;
    assert(vcount0 <= vcount1 && vcount1 <= int32_t(signal.size()));
    // predict
    for (int32_t v = vcount0; v < vcount1; ++v) {
      const auto edge = edges[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      signal[v] -= predWeight[it] * (signal[v1] + signal[v2]);
      if (dirlift) {
        applyDirectionalWeights(signal,
                                v,
                                v1,
                                v2,
                                dirliftstats,
                                predWeight[it],
                                updateWeight[it],
                                updateWeight[it],
                                scale1,
                                scale2,
                                scale3,
                                true,
                                false);
      }
      lodmean[it] += signal[v][0];
      count++;
    }
    // update
    for (int32_t v = vcount0; !skipUpdate && v < vcount1; ++v) {
      const auto edge = edges[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);

      if (!valenceUpdateWeightFlag) {  // LoD adaptive X, valence X
        const auto d = updateWeight[it] * signal[v];
        signal[v1] += d;
        signal[v2] += d;
        if (dirlift) {
          applyDirectionalWeights(signal,
                                  v,
                                  v1,
                                  v2,
                                  dirliftstats,
                                  predWeight[it],
                                  updateWeight[it],
                                  updateWeight[it],
                                  scale1,
                                  scale2,
                                  scale3,
                                  false,
                                  false);
        }
      } else {  // LoD adaptive X, valence O
        double valenceWeight1 = 1.0F / 6.0F;
        double valenceWeight2 = 1.0F / 6.0F;
        if (v1 < infoLevelOfDetails[0].pointCount)
          valenceWeight1 = 1.0 / valences[v1];
        if (v2 < infoLevelOfDetails[0].pointCount)
          valenceWeight2 = 1.0 / valences[v2];
        const auto d1 = updateWeight[it] * valenceWeight1 * signal[v];
        const auto d2 = updateWeight[it] * valenceWeight2 * signal[v];
        signal[v1] += d1;
        signal[v2] += d2;
        if (dirlift) {
          applyDirectionalWeights(signal,
                                  v,
                                  v1,
                                  v2,
                                  dirliftstats,
                                  predWeight[it],
                                  updateWeight[it] * valenceWeight1,
                                  updateWeight[it] * valenceWeight2,
                                  scale1,
                                  scale2,
                                  scale3,                                  
                                  false,
                                  false);
        }
      }
    }

    if (liftingOffset && count != 0) {
      lodmean[it] = (lodmean[it] / (count));
      lodmean[it] = (double)((int32_t)(lodmean[it] * 100.0)) / 100.0;

      for (int32_t v = vcount0; v < vcount1; ++v) {
        const auto edge = edges[v];
        const auto v1   = int32_t(edge & 0xFFFFFFFF);
        const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
        assert(v1 >= 0 && v1 <= vcount0);
        assert(v2 >= 0 && v2 <= vcount0);
        signal[v][0] = signal[v][0] - (lodmean[it]);
      }
    }
  }
}

//----------------------------------------------------------------------------
template<class T1, class T2>
void
computeInverseLinearLifting(
  std::vector<T1>&                                signal,
  const std::vector<vmesh::SubdivisionLevelInfo>& infoLevelOfDetails,
  const std::vector<int64_t>&                     edges,
  const std::vector<int>&                         valences,
  const bool                                      valenceUpdateWeight,
  std::vector<double>                             liftingOffsetValues,
  const bool                                      liftingOffset,
  const std::vector<T2>                           predWeight,
  const std::vector<T2>                           updateWeight,
  const bool                                      skipUpdate,
  std::vector<double>                             liftingStatsValues,
  const bool                                      dirlift,
  const double               scale1,
  const double               scale2,
  const double               scale3) {
  printf("(computeInverseLinearLifting) ");
  for (int ii = 0; ii < predWeight.size(); ii++)
    printf("predWeight[%d] %f, ", ii, (double)predWeight[ii]);
  printf("skipUpdate: %d, ", (int)skipUpdate);
  for (int ii = 0; ii < updateWeight.size(); ii++)
    printf("updateWeight[%d] %f, ", ii, (double)updateWeight[ii]);
  printf("\n");
  fflush(stdout);
  printf("liftingOffset: %d\n", liftingOffset);
  printf("skipUpdate: %d\n", skipUpdate);
  for (int ii = 0; ii < liftingOffsetValues.size(); ii++)
    printf("liftingdisplodmean[%d] %f, ", ii, (double)liftingOffsetValues[ii]);
  printf("\n");
  printf("dirlift: %d\n", dirlift);
  printf("\n");
  for (int ii = 0; ii < liftingStatsValues.size(); ii++)
    printf("liftingdispstat[%d] %f, ", ii, (double)liftingStatsValues[ii]);
  printf("\n");

  const auto lodCount = int32_t(infoLevelOfDetails.size());
  assert(lodCount > 0);
  const auto rfmtCount = lodCount - 1;
  printf("valenceUpdateWeight: ");
  printf("%d ", valenceUpdateWeight);
  printf("\n");
  auto mean_global = liftingStatsValues[0];
  auto std_global  = liftingStatsValues[1];

  for (int32_t it = 0; it < rfmtCount; ++it) {
    const auto vcount0 = infoLevelOfDetails[it].pointCount;
    const auto vcount1 = infoLevelOfDetails[it + 1].pointCount;
    assert(vcount0 < vcount1 && vcount1 <= int32_t(signal.size()));
    if (liftingOffset) {
      for (int32_t v = vcount0; v < vcount1; ++v) {
        const auto edge = edges[v];
        const auto v1   = int32_t(edge & 0xFFFFFFFF);
        const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
        assert(v1 >= 0 && v1 <= vcount0);
        assert(v2 >= 0 && v2 <= vcount0);
        signal[v][0] = signal[v][0] + (liftingOffsetValues[it]);
      }
    }

    // update
    for (int32_t v = vcount0; !skipUpdate && v < vcount1; ++v) {
      const auto edge = edges[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      if (!valenceUpdateWeight) {
        const auto d = updateWeight[it] * signal[v];
        signal[v1] -= d;
        signal[v2] -= d;
        if (dirlift) {
          applyDirectionalWeights(signal,
                                  v,
                                  v1,
                                  v2,
                                  liftingStatsValues,
                                  predWeight[it],
                                  updateWeight[it],
                                  updateWeight[it],
                                  scale1,
                                  scale2,
                                  scale3,                                  
                                  false,
                                  true);
        }
      } else {
        double valenceWeight1 = 1.0F / 6.0F;
        double valenceWeight2 = 1.0F / 6.0F;
        if (v1 < infoLevelOfDetails[0].pointCount)
          valenceWeight1 = 1.0 / valences[v1];
        if (v2 < infoLevelOfDetails[0].pointCount)
          valenceWeight2 = 1.0 / valences[v2];
        const auto d1 = updateWeight[it] * valenceWeight1 * signal[v];
        const auto d2 = updateWeight[it] * valenceWeight2 * signal[v];
        signal[v1] -= d1;
        signal[v2] -= d2;
        if (dirlift) {
          applyDirectionalWeights(signal,
                                  v,
                                  v1,
                                  v2,
                                  liftingStatsValues,
                                  predWeight[it],
                                  updateWeight[it] * valenceWeight1,
                                  updateWeight[it] * valenceWeight2,
                                  scale1,
                                  scale2,
                                  scale3,
                                  false,
                                  true);
        }
      }
    }
    // predict
    for (int32_t v = vcount0; v < vcount1; ++v) {
      const auto edge = edges[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      signal[v] += predWeight[it] * (signal[v1] + signal[v2]);
      if (dirlift) {
        applyDirectionalWeights(signal,
                                v,
                                v1,
                                v2,
                                liftingStatsValues,
                                predWeight[it],
                                updateWeight[it],
                                updateWeight[it],
                                scale1,
                                scale2,
                                scale3,
                                true,
                                true);
      }
    }
  }
}
//----------------------------------------------------------------------------

template<class T1, class T2>
void
interpolateSubdivision(
  std::vector<T1>&                                signal,
  const std::vector<vmesh::SubdivisionLevelInfo>& infoLevelOfDetails,
  const std::vector<int64_t>&                     edges,
  const T2                                        weight1,
  const T2                                        weight2,
  const bool                                      normalize = false) {
  const auto lodCount = int32_t(infoLevelOfDetails.size());
  assert(lodCount >= 0);
  const auto rfmtCount = lodCount - 1;
  for (int32_t it = 0; it < rfmtCount; ++it) {
    const auto vcount0 = infoLevelOfDetails[it].pointCount;
    const auto vcount1 = infoLevelOfDetails[it + 1].pointCount;
    assert(vcount0 <= vcount1 && vcount1 <= int32_t(signal.size()));
    for (int32_t v = vcount0; v < vcount1; ++v) {
      const auto edge = edges[v];
      const auto v1   = int32_t(edge & 0xFFFFFFFF);
      const auto v2   = int32_t((edge >> 32) & 0xFFFFFFFF);
      assert(v1 >= 0 && v1 <= vcount0);
      assert(v2 >= 0 && v2 <= vcount0);
      auto& vec = signal[v];
      vec       = weight1 * signal[v1] + weight2 * signal[v2];
      if (normalize) { vec.normalize(); }
    }
  }
}

//============================================================================

}  // namespace vmesh
