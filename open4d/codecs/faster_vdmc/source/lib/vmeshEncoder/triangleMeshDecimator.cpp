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

#include "triangleMeshDecimator.hpp"

#include "util/matrix.hpp"
#include "util/mesh.hpp"
#include "util/misc.hpp"
#include "util/mutablepriorityheap.hpp"
#include "util/triangle.hpp"

namespace vmesh {

//============================================================================

using Real = double;

//----------------------------------------------------------------------------

struct Quadratic {
  Quadratic() = default;
  Quadratic(Real nx, Real ny, Real nz, Real d) { init(nx, ny, nz, d); }
  Quadratic(const Quadratic& other) { copy(other); }
  Quadratic& operator=(const Quadratic& rhs) {
    copy(rhs);
    return *this;
  }
  ~Quadratic() = default;

  const Real& operator[](const int i) const {
    assert(i < 10);
    return _data[i];
  }

  Real& operator[](const int i) {
    assert(i < 10);
    return _data[i];
  }

  void copy(const Quadratic& other) {
    for (int i = 0; i < 10; ++i) { _data[i] = other._data[i]; }
  }

  void zero() {
    for (double& i : _data) { i = Real(0); }
  }

  void init(Real nx, Real ny, Real nz, Real d) {
    _data[0] = nx * nx;  // a2
    _data[1] = nx * ny;  // ab
    _data[2] = nx * nz;  // ac
    _data[3] = nx * d;   // ad
    _data[4] = ny * ny;  // b2
    _data[5] = ny * nz;  // bc
    _data[6] = ny * d;   // bd
    _data[7] = nz * nz;  // c2
    _data[8] = nz * d;   // cd
    _data[9] = d * d;    // d2
  }

  friend Quadratic operator+(const Quadratic& lhs, const Quadratic& rhs) {
    Quadratic res{};
    for (int i = 0; i < 10; ++i) {
      res._data[i] = lhs._data[i] + rhs._data[i];
    }
    return res;
  }

  Quadratic& operator+=(const Quadratic& rhs) {
    for (int i = 0; i < 10; ++i) { _data[i] += rhs._data[i]; }
    return *this;
  }

  Quadratic& operator-=(const Quadratic& rhs) {
    for (int i = 0; i < 10; ++i) { _data[i] -= rhs._data[i]; }
    return *this;
  }

  Quadratic& operator+=(double rhs) {
    for (double& i : _data) { i += rhs; }
    return *this;
  }

  Quadratic& operator-=(double rhs) {
    for (double& i : _data) { i -= rhs; }
    return *this;
  }

  Quadratic& operator*=(double rhs) {
    for (double& i : _data) { i *= rhs; }
    return *this;
  }

  bool minDistPoint(Vec3<double>& point) const {
    Mat3<double> M{};
    Mat3<double> iM{};
    M[0][0] = _data[0];
    M[0][1] = _data[1];
    M[0][2] = _data[2];
    M[1][0] = _data[1];
    M[1][1] = _data[4];
    M[1][2] = _data[5];
    M[2][0] = _data[2];
    M[2][1] = _data[5];
    M[2][2] = _data[7];
    if (M.inverse(iM)) {
      point = iM * Vec3<double>(-_data[3], -_data[6], -_data[8]);
      return true;
    }
    return false;
  }

  double operator()(const Vec3<double>& point) const {
    const double x   = point[0];
    const double y   = point[1];
    const double z   = point[2];
    const double xx  = x * x;
    const double yy  = y * y;
    const double zz  = z * z;
    const double x2  = 2.0 * x;
    const double y2  = 2.0 * y;
    const double z2  = 2.0 * z;
    const double xy2 = x2 * y;
    const double xz2 = x2 * z;
    const double yz2 = y2 * z;
    return _data[0] * xx + _data[1] * xy2 + _data[2] * xz2 + _data[3] * x2
           + _data[4] * yy + _data[5] * yz2 + _data[6] * y2 + _data[7] * zz
           + _data[8] * z2 + _data[9];
  }

  Real _data[10]{};
};

//============================================================================

template<typename T, int N>
class SmallVector {
public:
  SmallVector() {
    static_assert(N > 0, "initial size should be strictly higher than 0");
    _data      = _data0;
    _size      = 0;
    _allocated = N;
  }

  SmallVector(const SmallVector& other) { copy(other); }

  SmallVector& operator=(const SmallVector& rhs) {
    copy(rhs);
    return *this;
  }

  ~SmallVector() = default;

  void copy(const SmallVector& other) {
    resize(other.size());
    std::copy(other.data(), other.data() + other.size(), data());
  }

  const T* data() const { return _data; }
  T*       data() { return _data; }

  const T& operator[](const int i) const {
    assert(i < size());
    return _data[i];
  }

  T& operator[](const int i) {
    assert(i < size());
    return _data[i];
  }

  int size() const { return _size; }

  void reserve(const int sz) {
    if (sz >= _allocated) {
      std::unique_ptr<T[]> ndata(new T[sz]);
      std::copy(_data, _data + _size, ndata.get());
      _allocated = sz;
      _data1.swap(ndata);
      _data = _data1.get();
      return;
    }
  }

  void resize(const int sz) {
    if (sz > _allocated) { reserve(sz); }
    _size = sz;
  }

  void push_back(const T& v) {
    if (_size == _allocated) { reserve(2 * _allocated); }
    _data[_size++] = v;
  }

  int find(const T& v) const {
    for (int i = 0; i < _size; ++i) {
      if (_data[i] == v) { return i; }
    }
    return -1;
  }

  void erase(const T& v) {
    const auto i0 = find(v);
    if (i0 != -1) {
      --_size;
      for (int i = i0; i < _size; ++i) { _data[i] = _data[i + 1]; }
    }
  }

private:
  int                  _size{};
  int                  _allocated{};
  T*                   _data;
  T                    _data0[N];
  std::unique_ptr<T[]> _data1;
};

//----------------------------------------------------------------------------

using SmallIntVec = SmallVector<int, 8>;

//============================================================================

class TriangleMeshDecimatorImpl {
  struct DVertex {
    Vec3<double> _pos{};
    Quadratic    _q{};
    int          _tag{};
    SmallIntVec  _triangles;
    SmallIntVec  _edges;
    bool         _valid{};
  };

  struct DTriangle {
    int          _tag{};
    Vec3<int>    _indices{};
    Vec3<double> _normal{};
    bool         _zeroArea{};
    bool         _valid{};
#if SIMPLIFY_MESH_TRACK_POINTS
    std::vector<int32_t> _trackedPointsIndexes;
#endif  // SIMPLIFY_MESH_TRACK_POINTS
  };

  struct DEdge {
    int    heapPosition() const { return _heapPosition; }
    double heapKey() const { return _heapKey; }
    void   setHeapPosition(int hpos) { _heapPosition = hpos; }
    double setHeapKey(double hkey) { return _heapKey = hkey; }

    int opposite(int v) const {
      assert(v == _vindex0 || v == _vindex1);
      return v == _vindex0 ? _vindex1 : _vindex0;
    }

    double       _heapKey;
    Vec3<double> _pos;
    int          _vindex0;
    int          _vindex1;
    int          _heapPosition;
    bool         _valid;
  };

public:
  TriangleMeshDecimatorImpl() { clear(); }
  TriangleMeshDecimatorImpl(const TriangleMeshDecimatorImpl&) = delete;
  TriangleMeshDecimatorImpl&
  operator=(const TriangleMeshDecimatorImpl&) = delete;
  ~TriangleMeshDecimatorImpl()                = default;

  Error decimate(const double*                          points,
                 int                                    pointCount,
                 const int*                             triangles,
                 int                                    triangleCount,
                 bool                                   trackFlag,
                 const TriangleMeshDecimatorParameters& params) {
    if ((triangles == nullptr) || (points == nullptr) || (pointCount == 0)
        || (triangleCount == 0)) {
      if (triangles == nullptr) {
        printf("triangles == nullptr ");
        fflush(stdout);
      }
      if (points == nullptr) {
        printf("points == nullptr ");
        fflush(stdout);
      }
      if (pointCount == 0) {
        printf("pointCount == 0 ");
        fflush(stdout);
      }
      if (triangleCount == 0) {
        printf("triangleCount == 0 ");
        fflush(stdout);
      }
      return Error::UNEXPECTED_INPUT_DATA;
    }
    init(points, pointCount, triangles, triangleCount, params);
    decimate(params, trackFlag);
    return Error::OK;
  }

  Error decimatedMesh(double* dpoints,
                      int     dpointCount,
                      int32_t* dtracktris,
                      int32_t* dmodifytris,
                      int*    dtriangles,
                      bool     trackFlag,
                      int     dtriangleCount) {
    std::vector<int32_t> vmap;
    std::vector<std::vector<int32_t>> dvaildVindexToDeletedTri;
    std::vector<int32_t>              tmap;
    if (trackFlag) {
      const auto           tritCount0 = int(_triangles.size());
      tmap.resize(tritCount0, -1);
    }
    Error res = decimatedPoints(dpoints,
                                dpointCount,
                                dtracktris,
                                dmodifytris,
                                dvaildVindexToDeletedTri,
                                trackFlag,
                                vmap);
    if (res != Error::OK) { return res; }

    if (dtriangleCount < _triangleCount || (dtriangles == nullptr)) {
      return Error::UNEXPECTED_INPUT_DATA;
    }

    auto* const triangles = reinterpret_cast<Vec3<int>*>(dtriangles);
    int         tcount    = 0;
    int         count     = 0;
    for (const auto& triangle : _triangles) {
      if (triangle._valid) {
        auto&      tri = triangles[tcount++];
        const auto i   = triangle._indices[0];
        const auto j   = triangle._indices[1];
        const auto k   = triangle._indices[2];
        tri[0]         = vmap[i];
        tri[1]         = vmap[j];
        tri[2]         = vmap[k];
        assert(tri[0] >= 0 && tri[0] < _pointCount);
        assert(tri[1] >= 0 && tri[1] < _pointCount);
        assert(tri[2] >= 0 && tri[2] < _pointCount);
        if (trackFlag) tmap[count] = tcount;
      }
      ++count;
    }

    if (trackFlag) {
      for (int v = 0; v < _trackedMesh.pointCount(); v++) {
        auto& trackTriangle = _trackedMesh.tracks();
        auto& tindex        = trackTriangle[v];
        if (tindex != -1) {
          if (tmap[tindex] != -1) {
            tindex = tmap[tindex];
          } else {
            tindex = -1;
          }
        }
      }
    }
    assert(tcount == _triangleCount);
    return Error::OK;
  }

  int decimatedTriangleCount() const { return _triangleCount; }
  int decimatedPointCount() const { return _pointCount; }

#if SIMPLIFY_MESH_TRACK_POINTS

  Error trackedPoints(double* tpoints,
                      int*    tindexes,
                      int*    ttracks,
                      bool    trackFlag,
                      int     pointCount) const {
    if (pointCount > _trackedMesh.pointCount()) {
      return Error::UNEXPECTED_INPUT_DATA;
    }

    if (tpoints != nullptr) {
      for (int i = 0, j = 0; i < pointCount; ++i, j += 3) {
        const auto point = _trackedMesh.point(i) * _scale + _center;
        tpoints[j]       = point[0];
        tpoints[j + 1]   = point[1];
        tpoints[j + 2]   = point[2];
      }
    }

    if (trackFlag) {
      if (ttracks != nullptr) {
        for (int i = 0; i < pointCount; ++i) {
          const auto track = _trackedMesh.track(i);
          if (track != -1) {
            ttracks[i] = track - 1;
          } else {
            ttracks[i] = -1;
          }
        }
      }
    }

    if (tindexes != nullptr) {
      for (int i = 0; i < pointCount; ++i) {
        const auto tindex = _trackedPointToTriangle[i];
        assert(tindex >= -1);
        if (tindex == -1 && _vertices[i]._triangles.size() > 0) {
          tindexes[i] = _vertices[i]._triangles[0];
        } else {
          tindexes[i] = tindex;
        }
      }
    }

    return Error::OK;
  }

  int trackedPointCount() const { return _trackedMesh.pointCount(); }
#endif  // SIMPLIFY_MESH_TRACK_POINTS

  void clear() {
    _pointCount    = 0;
    _triangleCount = 0;
    _vertices.clear();
    _triangles.clear();
    _edges.clear();
    _heap.clear();
  }

private:
  Error decimatedPoints(double* const         dpoints,
                        int                   dpointCount,
                  int32_t*                           dtracktris,
                  int32_t*                           dmodifytris,
                  std::vector<std::vector<int32_t>>& dvaildVindexToDeletedTri,
                  bool                               trackFlag,
                        std::vector<int32_t>& vmap) {
    if (dpointCount < _pointCount || (dpoints == nullptr)) {
      return Error::UNEXPECTED_INPUT_DATA;
    }

    auto* const points      = reinterpret_cast<Vec3<double>*>(dpoints);
    const auto  pointCount0 = int(_vertices.size());
    vmap.resize(pointCount0, -1);
    int vcount = 0;
    for (int v = 0; v < pointCount0; ++v) {
      if (_vertices[v]._valid) {
        const auto& vertex = _vertices[v];
        vmap[v]            = vcount;
        points[vcount]     = vertex._pos * _scale + _center;
        ++vcount;
      }
    }

    if (trackFlag) {
      dvaildVindexToDeletedTri.resize(vcount);
      for (int v = 0; v < pointCount0; ++v) {
        auto map = vmap[v];
        if (map != -1) {
          auto tri = _vaildVindexToDeletedTri[v];
          for (int t : tri) dvaildVindexToDeletedTri[map].push_back(t);
        }
      }

      auto* const tracktris =
        reinterpret_cast<std::vector<int32_t>*>(dtracktris);

      for (int v = 0; v < vcount; ++v) {
        const auto& deltedtri = dvaildVindexToDeletedTri[v];
        if (deltedtri.size()) {
          for (int t : deltedtri) tracktris[v].push_back(t);
        }
      }

      std::vector<std::vector<int32_t>> dvaildVindexToModifyTri;
      dvaildVindexToModifyTri.resize(vcount);

      for (int v = 0; v < pointCount0; ++v) {
        if (_vaildVindexToModifyTri[v].size()) {
          for (int t : _vaildVindexToModifyTri[v]) {
            if (_triangles[t]._valid) {
              auto map = vmap[v];
              if (std::find(dvaildVindexToModifyTri[map].begin(),
                            dvaildVindexToModifyTri[map].end(),
                            t)
                  == dvaildVindexToModifyTri[map].end())
                dvaildVindexToModifyTri[map].push_back(t);
            }
          }
        }
      }
      auto* const modifytris =
        reinterpret_cast<std::vector<int32_t>*>(dmodifytris);

      for (int v = 0; v < vcount; ++v) {
        const auto& modifytri = dvaildVindexToModifyTri[v];
        if (modifytri.size()) {
          for (int t : modifytri) modifytris[v].push_back(t);
        }
      }
    }
    assert(vcount == _pointCount);
    return Error::OK;
  }

  Error init(const double*                          points,
             int                                    pointCount,
             const int*                             triangles,
             int                                    triangleCount,
             const TriangleMeshDecimatorParameters& params) {
    if (pointCount == 0 || triangleCount == 0) {
      return Error::UNEXPECTED_INPUT_DATA;
    }

    clear();
    _pointCount    = pointCount;
    _triangleCount = triangleCount;
    _vertices.resize(pointCount);
    _triangles.resize(triangleCount);
    _center = 0.0;
#if SIMPLIFY_MESH_TRACK_POINTS
    _trackedMesh.resizePoints(pointCount);
    _trackedMesh.resizeTriangles(triangleCount);
    _trackedPointToTriangle.resize(pointCount);
#endif  // SIMPLIFY_MESH_TRACK_POINTS

    for (int v = 0, c = 0; v < _pointCount; ++v, c += 3) {
      auto& vertex = _vertices[v];
      auto& pos    = vertex._pos;
      pos[0]       = points[c];
      pos[1]       = points[c + 1];
      pos[2]       = points[c + 2];
      _center += pos;
      vertex._valid = true;
      vertex._q.zero();
    }

    _center /= _pointCount;
    double radius = 0.0;
    for (int v = 0; v < _pointCount; ++v) {
      radius = std::max(radius, (_vertices[v]._pos - _center).norm2());
    }

    radius            = std::sqrt(radius);
    _scale            = radius == 0.0 ? 1.0 : radius;
    const auto iscale = 1.0 / _scale;
    for (int v = 0; v < _pointCount; ++v) {
      _vertices[v]._pos = (_vertices[v]._pos - _center) * iscale;
#if SIMPLIFY_MESH_TRACK_POINTS
      _trackedMesh.setPoint(v, _vertices[v]._pos);
      _trackedPointToTriangle[v] = -1;
#endif  // SIMPLIFY_MESH_TRACK_POINTS
    }

    for (int t = 0, c = 0; t < _triangleCount; ++t, c += 3) {
      auto& triangle       = _triangles[t];
      triangle._valid      = true;
      const auto i         = triangles[c];
      const auto j         = triangles[c + 1];
      const auto k         = triangles[c + 2];
      triangle._indices[0] = i;
      triangle._indices[1] = j;
      triangle._indices[2] = k;
      auto& vi             = _vertices[i];
      auto& vj             = _vertices[j];
      auto& vk             = _vertices[k];
      vi._triangles.push_back(t);
      vj._triangles.push_back(t);
      vk._triangles.push_back(t);

      const auto& posi   = vi._pos;
      const auto& posj   = vj._pos;
      const auto& posk   = vk._pos;
      auto        normal = computeTriangleNormal(posi, posj, posk, false);

      const auto area = normal.norm();
      if (area > 0.0) {
        normal /= area;
        triangle._normal   = normal;
        triangle._zeroArea = false;
      } else {
        triangle._zeroArea = true;
        triangle._normal   = 0.0;
      }

      Quadratic q(normal[0], normal[1], normal[2], -(normal * posi));
      if (params.areaWeightedQuadratics) { q *= area; }

      vi._q += q;
      vj._q += q;
      vk._q += q;

#if SIMPLIFY_MESH_TRACK_POINTS
      _trackedMesh.setTriangle(t, i, j, k);
#endif  // SIMPLIFY_MESH_TRACK_POINTS
    }

#if SIMPLIFY_MESH_TRACK_POINTS
    _trackedMesh.computeNormals();
    ComputeVertexToTriangle(_trackedMesh.triangles(),
                            _trackedMesh.pointCount(),
                            _initialVertexToTriangle);
#endif  // SIMPLIFY_MESH_TRACK_POINTS

    SmallIntVec vadj;
    int         edgeCount = 0;
    for (int vindex0 = 0; vindex0 < _pointCount; ++vindex0) {
      computeAdjacentVertices(vindex0, vadj);
      for (int i = 0, count = vadj.size(); i < count; ++i) {
        const auto vindex1 = vadj[i];
        edgeCount += static_cast<int>(vindex1 > vindex0);
      }
    }
    _edges.resize(edgeCount);

    SmallIntVec tadj;
    for (int vindex0 = 0, eindex = 0; vindex0 < _pointCount; ++vindex0) {
      computeAdjacentVertices(vindex0, vadj);
      for (int i = 0, count = vadj.size(); i < count; ++i) {
        const auto vindex1 = vadj[i];
        assert(vindex1 >= 0 && vindex1 < _pointCount);
        if (vindex1 <= vindex0) { continue; }

        auto& edge = _edges[eindex];
        edge.setHeapPosition(-1);
        edge._valid   = true;
        edge._vindex0 = vindex0;
        edge._vindex1 = vindex1;
        _vertices[vindex0]._edges.push_back(eindex);
        _vertices[vindex1]._edges.push_back(eindex);
        computeEdgeAdjacentTriangles(vindex0, vindex1, tadj);

        auto& v0 = _vertices[vindex0];
        auto& v1 = _vertices[vindex1];
        if (tadj.size() == 1) {
          if (params.boundaryWeight > 0.0) {
            const auto& pos0    = v0._pos;
            const auto& pos1    = v1._pos;
            const auto  delta   = pos1 - pos0;
            const auto& tnormal = _triangles[tadj[0]]._normal;
            auto        normal  = delta ^ tnormal;
            const auto  ilength = normal.norm();
            if (ilength > 0.0) {
              normal /= ilength;
              Quadratic q(normal[0], normal[1], normal[2], -(normal * pos0));
              if (params.areaWeightedQuadratics) { q *= delta.norm2(); }
              q *= params.boundaryWeight;
              v0._q += q;
              v1._q += q;
            }
          }
        }
        ++eindex;
      }
    }

    for (auto& edge : _edges) { computeEdgeCollapseCost(edge, params); }

    return Error::OK;
  }

  void decimate(const TriangleMeshDecimatorParameters& params,bool trackFlag);

  void computeEdgeCollapseCost(DEdge&                                 edge,
                               const TriangleMeshDecimatorParameters& params);

  double computeCost(const Quadratic&                       q,
                     const Vec3<double>&                    pos,
                     int32_t                                vindex0,
                     int32_t                                vindex1,
                     const TriangleMeshDecimatorParameters& params);

  void computeAdjacentVertices(int vindex, SmallIntVec& vadj) {
    vadj.resize(0);
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto& triangle        = _triangles[tadj[i]]._indices;
      _vertices[triangle[0]]._tag = 1;
      _vertices[triangle[1]]._tag = 1;
      _vertices[triangle[2]]._tag = 1;
    }

    _vertices[vindex]._tag = 0;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto& triangle = _triangles[tadj[i]]._indices;
      for (int j = 0; j < 3; ++j) {
        const auto index  = triangle[j];
        auto&      vertex = _vertices[index];
        if (vertex._tag == 1) {
          vadj.push_back(index);
          vertex._tag = 0;
        }
      }
    }
  }

#if SIMPLIFY_MESH_TRACK_POINTS
  void resetIncidentTrianglesTrackedPoints(const int32_t vindex) {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      _triangles[tadj[i]]._trackedPointsIndexes.resize(0);
    }
  }

  void appendIncidentTrianglesTrackedPoints(
    const SmallIntVec&    tadj,
    std::vector<int32_t>& trackedPointsIndexes) const {
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto tindex = tadj[i];
      trackedPointsIndexes.insert(
        trackedPointsIndexes.end(),
        _triangles[tindex]._trackedPointsIndexes.begin(),
        _triangles[tindex]._trackedPointsIndexes.end());
    }
  }

  int32_t trackPoint(const int32_t                          vindex,
                     const SmallIntVec&                     tadj,
                     Vec3<double>&                          tpoint0,
                     const TriangleMeshDecimatorParameters& params) const {
    const auto tpoint  = _trackedMesh.point(vindex);
    const auto tnormal = _trackedMesh.normal(vindex);
    double     minDist = std::numeric_limits<double>::max();
    int32_t    tindex0 = -1;
    for (int32_t i = 0, count = tadj.size(); i < count; ++i) {
      const auto tindex = tadj[i];
      assert(_triangles[tindex]._valid);
      const auto& tri      = _triangles[tindex]._indices;
      const auto& posi     = _vertices[tri[0]]._pos;
      const auto& posj     = _vertices[tri[1]]._pos;
      const auto& posk     = _vertices[tri[2]]._pos;
      const auto  normal   = computeTriangleNormal(posi, posj, posk, true);
      const auto  cosAngle = tnormal * normal;
      if (cosAngle <= params.trackedPointNormalFlipThreshold) { continue; }

      const auto cpoint = ClosestPointInTriangle(tpoint,
                                                 _vertices[tri[0]]._pos,
                                                 _vertices[tri[1]]._pos,
                                                 _vertices[tri[2]]._pos);
      const auto dist   = (tpoint - cpoint).norm2();
      // compute closest point
      if (dist < minDist) {
        minDist = dist;
        tpoint0 = cpoint;
        tindex0 = tindex;
      }
    }

    return tindex0;
  }
#endif  // SIMPLIFY_MESH_TRACK_POINTS

  void
  computeEdgeAdjacentTriangles(int vindex0, int vindex1, SmallIntVec& tadj) {
    tadj.resize(0);
    tagAdjacentTriangles(vindex0, 0);
    tagAdjacentTriangles(vindex1, 1);
    const auto& tadj0 = _vertices[vindex0]._triangles;
    for (int i = 0, count = tadj0.size(); i < count; ++i) {
      const auto tindex = tadj0[i];
      if (_triangles[tindex]._tag != 0) { tadj.push_back(tindex); }
    }
  }

  void tagAdjacentTriangles(int vindex, int tag) {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      _triangles[tadj[i]]._tag = tag;
    }
  }

  void incrementTagAdjacentTriangles(int vindex) {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      ++_triangles[tadj[i]]._tag;
    }
  }

  void extractAdjacentTriangles(int vindex, int tag, SmallIntVec& adj) {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto tindex = tadj[i];
      if (_triangles[tindex]._tag == tag) { adj.push_back(tindex); }
    }
  }

  void extractAdjacentTriangles(int          vindex,
                                int          tag0,
                                int          tag1,
                                SmallIntVec& adj0,
                                SmallIntVec& adj1) {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto tindex = tadj[i];
      if (_triangles[tindex]._tag == tag0) {
        adj0.push_back(tindex);
      } else if (_triangles[tindex]._tag == tag1) {
        adj1.push_back(tindex);
      }
    }
  }

  void computeAdjacentTrianglesQuality(int                 vindex,
                                       const Vec3<double>& pos,
                                       double&             angleQuality,
                                       double& normalQuality) const {
    const auto& tadj = _vertices[vindex]._triangles;
    for (int i = 0, count = tadj.size(); i < count; ++i) {
      const auto  tindex   = tadj[i];
      const auto& triangle = _triangles[tindex];
      if (triangle._tag != 1 || triangle._zeroArea) { continue; }
      int j = 0;
      int k = 0;
      if (triangle._indices[0] == vindex) {
        j = triangle._indices[1];
        k = triangle._indices[2];
      } else if (triangle._indices[1] == vindex) {
        j = triangle._indices[2];
        k = triangle._indices[0];
      } else {
        j = triangle._indices[0];
        k = triangle._indices[1];
      }
      const auto& normal0 = triangle._normal;
      const auto  e0      = _vertices[j]._pos - pos;
      const auto  e1      = _vertices[k]._pos - pos;
      const auto  e2      = _vertices[j]._pos - _vertices[k]._pos;
      const auto  maxEdgeLength2 =
        std::max(std::max(e0.norm2(), e1.norm2()), e2.norm2());
      auto       normal1      = e0 ^ e1;
      const auto triangleArea = normal1.norm();
      if (triangleArea > 0.0) { normal1 /= triangleArea; }
      angleQuality =
        std::min(angleQuality,
                 maxEdgeLength2 > 0.0 ? triangleArea / maxEdgeLength2 : 0.0);
      normalQuality = std::min(normalQuality, normal1 * normal0);
    }
  }

#if SIMPLIFY_MESH_TRACK_POINTS
  bool preservesTrackedTriangleNormalsOrientation(
    const DEdge&                           edge,
    const TriangleMeshDecimatorParameters& params) {
    const auto  vindex0 = edge._vindex0;
    const auto  vindex1 = edge._vindex1;
    const auto& tadj0   = _vertices[vindex0]._triangles;
    SmallIntVec modified;
    SmallIntVec deleted;
    for (int i = 0, count = tadj0.size(); i < count; ++i) {
      const auto  tindex = tadj0[i];
      const auto& tri    = _triangles[tindex]._indices;
      if (tri[0] != vindex1 && tri[1] != vindex1 && tri[2] != vindex1) {
        modified.push_back(tindex);
      }
    }

    const auto& tadj1 = _vertices[vindex1]._triangles;
    for (int i = 0, count = tadj1.size(); i < count; ++i) {
      const auto  tindex = tadj1[i];
      const auto& tri    = _triangles[tindex]._indices;
      if (tri[0] != vindex0 && tri[1] != vindex0 && tri[2] != vindex0) {
        modified.push_back(tindex);
      } else {
        deleted.push_back(tindex);
      }
    }

    if (modified.size() == 0) { return true; }

    bool trackedPointNormalInverted = false;
    if (!trackedPointNormalInverted) {
      const auto pos0         = _vertices[vindex0]._pos;
      const auto pos1         = _vertices[vindex1]._pos;
      _vertices[vindex0]._pos = edge._pos;
      _vertices[vindex1]._pos = edge._pos;
      std::vector<int32_t> trackedPointsIndexes;
      appendIncidentTrianglesTrackedPoints(modified, trackedPointsIndexes);
      appendIncidentTrianglesTrackedPoints(deleted, trackedPointsIndexes);

      if (_trackedPointToTriangle[vindex0] == -1) {
        trackedPointsIndexes.push_back(vindex0);
      }
      if (_trackedPointToTriangle[vindex1] == -1) {
        trackedPointsIndexes.push_back(vindex1);
      }

      const auto trackedPointCount = int32_t(trackedPointsIndexes.size());
      std::vector<Vec3<double>> trackedPointsPositions(trackedPointCount);
      for (int32_t p = 0; p < trackedPointCount; ++p) {
        const auto vindex = trackedPointsIndexes[p];
        assert(vindex >= 0 && vindex < _trackedMesh.pointCount());
        trackedPointsPositions[p] = _trackedMesh.point(vindex);
        Vec3<double> tpoint0{};
        const auto   tindex0 = trackPoint(vindex, modified, tpoint0, params);
        if (tindex0 >= 0) {
          _trackedMesh.setPoint(vindex, tpoint0);
        } else {
          trackedPointNormalInverted = true;
        }
      }

      for (const auto vindex : trackedPointsIndexes) {
        if (trackedPointNormalInverted) { break; }

        const auto& tadj = _initialVertexToTriangle.neighbours();
        const auto  start =
          _initialVertexToTriangle.neighboursStartIndex(vindex);
        const auto end = _initialVertexToTriangle.neighboursEndIndex(vindex);
        for (int j = start; j < end; ++j) {
          const auto tindex = tadj[j];
          if (_triangles[tindex]._zeroArea) { continue; }

          const auto& tri      = _trackedMesh.triangle(tindex);
          const auto& posi     = _trackedMesh.point(tri[0]);
          const auto& posj     = _trackedMesh.point(tri[1]);
          const auto& posk     = _trackedMesh.point(tri[2]);
          const auto  normal   = computeTriangleNormal(posi, posj, posk, true);
          const auto  cosAngle = normal * _triangles[tindex]._normal;
          if (cosAngle <= params.trackedTriangleFlipThreshold) {
            trackedPointNormalInverted = true;
            break;
          }
        }
      }

      for (int32_t p = 0; p < trackedPointCount; ++p) {
        _trackedMesh.setPoint(trackedPointsIndexes[p],
                              trackedPointsPositions[p]);
      }
      _vertices[vindex0]._pos = pos0;
      _vertices[vindex1]._pos = pos1;
    }

    return !trackedPointNormalInverted;
  }
#endif  // SIMPLIFY_MESH_TRACK_POINTS

  bool isSafe(const DEdge&                           edge,
              const TriangleMeshDecimatorParameters& params) {
#if SIMPLIFY_MESH_TRACK_POINTS
    if (params.preserveTrackedTriangleNormalsOrientation
        && !preservesTrackedTriangleNormalsOrientation(edge, params)) {
      return false;
    }
#endif  // SIMPLIFY_MESH_TRACK_POINTS
    return true;
  }

  int _pointCount{};
  int _triangleCount{};

  double       _scale{};
  Vec3<double> _center{};

  std::vector<DVertex>       _vertices;
  std::vector<DTriangle>     _triangles;
  std::vector<DEdge>         _edges;
  MutablePriorityHeap<DEdge> _heap;

#if SIMPLIFY_MESH_TRACK_POINTS
  TriangleMesh<double>                _trackedMesh;
  std::vector<int32_t>                _trackedPointToTriangle;
  StaticAdjacencyInformation<int32_t> _initialVertexToTriangle;
  std::vector<std::vector<int32_t>>   _vaildVindexToDeletedTri;
  std::vector<std::vector<int32_t>>   _vaildVindexToModifyTri;
#endif  // SIMPLIFY_MESH_TRACK_POINTS
};

//============================================================================

void
PrintProgress(double progress) {
  const int barWidth = 70;
  std::cout << "\t [";
  int pos = barWidth * progress;
  for (int i = 0; i < barWidth; ++i) {
    if (i < pos) {
      std::cout << '=';
    } else if (i == pos) {
      std::cout << '>';
    } else {
      std::cout << ' ';
    }
  }
  std::cout << "] " << int(progress * 100.0) << " %\r";
  std::cout.flush();
  std::cout << '\n';
}

//============================================================================

void
TriangleMeshDecimatorImpl::decimate(
  const TriangleMeshDecimatorParameters& params,
  bool trackFlag) {
  SmallIntVec  modified0;
  SmallIntVec  modified1;
  SmallIntVec  deleted;
  const auto   maxError       = -params.maxError;
  const double triangleCount0 = _triangleCount;
  const double progressStep   = 0.1;
  double       lastProgress   = -1.0;
  bool         stop           = false;
  if (trackFlag) {
    _trackedMesh.resizeTracks(_trackedMesh.pointCount());
    _vaildVindexToDeletedTri.resize(_pointCount);
    _vaildVindexToModifyTri.resize(_pointCount);
  }
  while (!stop && _triangleCount > params.triangleCount
         && _pointCount > params.vertexCount) {
    double progress = 1.0 - _triangleCount / triangleCount0;
    if (progress > lastProgress + progressStep) {
      lastProgress = progress;
#if PREPROCESSING_VERBOSE
      PrintProgress(progress);
#endif
    }

    if (DEdge* const edge = _heap.extract()) {
      if (edge->_heapKey < maxError) {
        stop = true;
        break;
      }

      if (!isSafe(*edge, params)) {
        edge->setHeapKey(std::numeric_limits<double>::lowest());
        _heap.insert(*edge);
      } else {
        const auto vindex0 = edge->_vindex0;
        const auto vindex1 = edge->_vindex1;
        if (_vertices[vindex0]._valid && _vertices[vindex1]._valid) {
          modified0.resize(0);
          modified1.resize(0);
          deleted.resize(0);
          tagAdjacentTriangles(vindex1, 0);
          tagAdjacentTriangles(vindex0, 1);
          incrementTagAdjacentTriangles(vindex1);
          extractAdjacentTriangles(vindex0, 1, 2, modified0, deleted);
          extractAdjacentTriangles(vindex1, 1, modified1);

#if SIMPLIFY_MESH_TRACK_POINTS
          std::vector<int32_t> trackedPointsIndexes;
          appendIncidentTrianglesTrackedPoints(modified0,
                                               trackedPointsIndexes);
          appendIncidentTrianglesTrackedPoints(modified1,
                                               trackedPointsIndexes);
          appendIncidentTrianglesTrackedPoints(deleted, trackedPointsIndexes);

          if (_trackedPointToTriangle[vindex0] == -1) {
            trackedPointsIndexes.push_back(vindex0);
          }
          if (_trackedPointToTriangle[vindex1] == -1) {
            trackedPointsIndexes.push_back(vindex1);
          }
#endif  // SIMPLIFY_MESH_TRACK_POINTS

          auto&       eadj0 = _vertices[vindex0]._edges;
          const auto& eadj1 = _vertices[vindex1]._edges;
          SmallIntVec vadj;
          for (int i = 0, count = eadj0.size(); i < count; ++i) {
            vadj.push_back(_edges[eadj0[i]].opposite(vindex0));
          }

          for (int i = 0, count = eadj1.size(); i < count; ++i) {
            const auto eindex = eadj1[i];
            auto&      cedge  = _edges[eindex];
            const auto vindex = cedge.opposite(vindex1);
            assert(vindex >= 0);
            if (vindex == vindex0) {  // collapse edge
              _vertices[vindex]._edges.erase(eindex);
              _heap.remove(cedge);
              cedge._valid = false;
            } else {
              const auto eindex0 = vadj.find(vindex);
              if (eindex0 != -1) {  // edge already exists
                _vertices[vindex]._edges.erase(eindex);
                _heap.remove(cedge);
                cedge._valid = false;
              } else {  // keep edge and update it
                cedge._vindex0 = vindex0;
                cedge._vindex1 = vindex;
                eadj0.push_back(eindex);
              }
            }
          }

          if (trackFlag) {
            for (int i = 0, count = deleted.size(); i < count; ++i) {
              const auto tindex = deleted[i];
              _vaildVindexToDeletedTri[vindex0].push_back(tindex);
            }
            auto& deleted0 = _vaildVindexToDeletedTri[vindex1];
            if (deleted0.size()) {
              for (int t : deleted0)
                _vaildVindexToDeletedTri[vindex0].push_back(t);
              deleted0.clear();
            }

            for (int i = 0, count = modified0.size(); i < count; ++i) {
              const auto tindex0 = modified0[i];
              if (std::find(_vaildVindexToModifyTri[vindex0].begin(),
                            _vaildVindexToModifyTri[vindex0].end(),
                            tindex0)
                  == _vaildVindexToModifyTri[vindex0].end())
                _vaildVindexToModifyTri[vindex0].push_back(tindex0);
            }
            for (int i = 0, count = modified1.size(); i < count; ++i) {
              const auto tindex1 = modified1[i];
              if (std::find(_vaildVindexToModifyTri[vindex0].begin(),
                            _vaildVindexToModifyTri[vindex0].end(),
                            tindex1)
                  == _vaildVindexToModifyTri[vindex0].end())
                _vaildVindexToModifyTri[vindex0].push_back(tindex1);
            }

            auto& modify = _vaildVindexToModifyTri[vindex1];
            if (!modify.empty()) {
              for (int t : modify)
                _vaildVindexToModifyTri[vindex0].push_back(t);
              modify.clear();
            }
          }

          // delete triangles
          for (int i = 0, count = deleted.size(); i < count; ++i) {
            const auto tindex   = deleted[i];
            auto&      triangle = _triangles[tindex];
            triangle._valid     = false;
            _vertices[triangle._indices[0]]._triangles.erase(tindex);
            _vertices[triangle._indices[1]]._triangles.erase(tindex);
            _vertices[triangle._indices[2]]._triangles.erase(tindex);
          }
          _triangleCount -= deleted.size();

          // update modified triangles
          auto& tadj0 = _vertices[vindex0]._triangles;
          for (int i = 0, count = modified1.size(); i < count; ++i) {
            const auto tindex   = modified1[i];
            auto&      triangle = _triangles[tindex];
            for (int k = 0; k < 3; ++k) {
              if (triangle._indices[k] == vindex1) {
                triangle._indices[k] = vindex0;
              }
            }
            tadj0.push_back(tindex);
          }

          auto& v0 = _vertices[vindex0];
          auto& v1 = _vertices[vindex1];

          // delete v1
          v1._valid = false;
          --_pointCount;

          // update v0 position and quadratic
          v0._pos = edge->_pos;
          v0._q += v1._q;

          const auto tcount = int32_t(_vertices[vindex0]._triangles.size());
          if (tcount > 0) {
#if SIMPLIFY_MESH_TRACK_POINTS
            resetIncidentTrianglesTrackedPoints(vindex0);
            for (const auto vindex : trackedPointsIndexes) {
              Vec3<double> tpoint0{};
              const auto   tindex0 = trackPoint(
                vindex, _vertices[vindex0]._triangles, tpoint0, params);
              assert(tindex0 >= 0);
              _trackedPointToTriangle[vindex] = tindex0;
              _trackedMesh.setPoint(vindex, tpoint0);
              if (trackFlag) _trackedMesh.setTrack(vindex, tindex0);
              _triangles[tindex0]._trackedPointsIndexes.push_back(vindex);
            }
#endif  // SIMPLIFY_MESH_TRACK_POINTS

            // update edge collapse
            for (int i = 0, count = eadj0.size(); i < count; ++i) {
              computeEdgeCollapseCost(_edges[eadj0[i]], params);
            }

            for (int32_t i = 0; i < tcount; ++i) {
              const auto  tindex = _vertices[vindex0]._triangles[i];
              const auto& tri    = _triangles[tindex]._indices;
              int32_t     a      = 0;
              int32_t     b      = 0;
              if (tri[0] == vindex0) {
                a = tri[1];
                b = tri[2];
              } else if (tri[1] == vindex0) {
                a = tri[2];
                b = tri[0];
              } else {
                a = tri[1];
                b = tri[0];
              }
              for (int32_t j = 0, ecount = _vertices[a]._edges.size();
                   j < ecount;
                   ++j) {
                const auto eindex = _vertices[a]._edges[j];
                auto&      edgeAB = _edges[eindex];
                if ((edgeAB._vindex0 == a && edgeAB._vindex1 == b)
                    || (edgeAB._vindex0 == b && edgeAB._vindex1 == a)) {
                  computeEdgeCollapseCost(edgeAB, params);
                }
              }
            }
          }
        }
      }
    }
  }
}

//----------------------------------------------------------------------------

double
TriangleMeshDecimatorImpl::computeCost(
  const Quadratic&                       q,
  const Vec3<double>&                    pos,
  const int32_t                          vindex0,
  const int32_t                          vindex1,
  const TriangleMeshDecimatorParameters& params) {
  double     error                      = q(pos);
  const bool penalizeTriangleFlip       = params.triangleFlipPenalty > 0.0;
  const bool penalizeElangatedTriangles = params.angleQualityThreshold > 0.0;
  if (penalizeTriangleFlip || penalizeElangatedTriangles) {
    double normalQuality = 1.0;
    double angleQuality  = params.angleQualityThreshold;
    computeAdjacentTrianglesQuality(vindex0, pos, angleQuality, normalQuality);
    computeAdjacentTrianglesQuality(vindex1, pos, angleQuality, normalQuality);

    if (penalizeElangatedTriangles) {
      const auto eps = 1.0e-7;
      error /= (eps + angleQuality);
    }

    if (penalizeTriangleFlip && normalQuality < params.triangleFlipThreshold) {
      error += (params.triangleFlipThreshold - normalQuality)
               * params.triangleFlipPenalty;
    }
  }

  return error;
}

//----------------------------------------------------------------------------

void
TriangleMeshDecimatorImpl::computeEdgeCollapseCost(
  DEdge&                                 edge,
  const TriangleMeshDecimatorParameters& params) {
  const auto vindex0 = edge._vindex0;
  const auto vindex1 = edge._vindex1;
  auto&      v0      = _vertices[vindex0];
  auto&      v1      = _vertices[vindex1];
  const auto q       = v0._q + v1._q;

  // tag incident triangles
  tagAdjacentTriangles(vindex0, 0);
  tagAdjacentTriangles(vindex1, 1);
  incrementTagAdjacentTriangles(vindex0);

  // compute best positon for merged point
  auto pos   = v0._pos;
  auto error = computeCost(q, v0._pos, vindex0, vindex1, params);

  const auto errorV1 = computeCost(q, v1._pos, vindex0, vindex1, params);
  if (errorV1 < error) {
    error = errorV1;
    pos   = v1._pos;
  }

  if (params.vertexPlacement == VertexPlacement::END_POINTS_OR_MID_POINT
      || params.vertexPlacement == VertexPlacement::OPTIMAL) {
    const auto posMidPoint(0.5 * (v0._pos + v1._pos));
    const auto errorMidPoint =
      computeCost(q, posMidPoint, vindex0, vindex1, params);
    if (errorMidPoint < error) {
      error = errorMidPoint;
      pos   = posMidPoint;
    }
  }

  Vec3<double> posOptimal{};
  if (params.vertexPlacement == VertexPlacement::OPTIMAL
      && q.minDistPoint(posOptimal)) {
    const auto errorOptimal =
      computeCost(q, posOptimal, vindex0, vindex1, params);
    if (errorOptimal < error) {
      error = errorOptimal;
      pos   = posOptimal;
    }
  }
  edge._pos = pos;

  edge.setHeapKey(-error);
  if (edge.heapPosition() == -1) {
    _heap.insert(edge);
  } else {
    _heap.update(edge);
  }
}

//============================================================================

TriangleMeshDecimator::TriangleMeshDecimator()
  : _pimpl(new TriangleMeshDecimatorImpl) {}

TriangleMeshDecimator::~TriangleMeshDecimator() = default;

Error
TriangleMeshDecimator::decimate(
  const double*                          points,
  int                                    pointCount,
  const int*                             triangles,
  int                                    triangleCount,
  bool                                   trackFlag,
  const TriangleMeshDecimatorParameters& params) {
  return _pimpl->decimate(
    points, pointCount, triangles, triangleCount, trackFlag, params);
}

Error
TriangleMeshDecimator::decimatedMesh(double* dpoints,
                                     int     dpointCount,
                                     int32_t* dtracktris,
                                     int32_t* dmodifytris,
                                     int*    dtriangles,
                                     bool trackFlag,
                                     int     dtriangleCount) const {
  return _pimpl->decimatedMesh(
    dpoints, dpointCount, dtracktris, dmodifytris, dtriangles,trackFlag, dtriangleCount);
}

int
TriangleMeshDecimator::decimatedTriangleCount() const {
  return _pimpl->decimatedTriangleCount();
}

int
TriangleMeshDecimator::decimatedPointCount() const {
  return _pimpl->decimatedPointCount();
}

#if SIMPLIFY_MESH_TRACK_POINTS
int
TriangleMeshDecimator::trackedPointCount() const {
  return _pimpl->trackedPointCount();
}

Error
TriangleMeshDecimator::trackedPoints(double* tpoints,
                                     int*    tindexes,
                                     int*    ttracks,
                                     bool    trackFlag,
                                     int     pointCount) const {
  return _pimpl->trackedPoints(
    tpoints, tindexes, ttracks, trackFlag, pointCount);
}
#endif  // SIMPLIFY_MESH_TRACK_POINTS

//============================================================================

}  // namespace vmesh
