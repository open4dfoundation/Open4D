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
#include "transferColor.hpp"
#include "util/mesh.hpp"
#include "util/kdtree.hpp"
#include "encoder.hpp"

#include "mmSample.h"
#include "mmModel.h"
#include "mmQuantize.h"
#include "mmDequantize.h"
#include "mmCompare.h"
#include "mmIO.h"
#include "mmImage.h"

#include <nanoflann/nanoflann.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

namespace vmesh {

//============================================================================

template<typename T>
inline static T
clip(const T& n, const T& lower, const T& upper) {
  return (std::max)(lower, (std::min)(n, upper));
}

//============================================================================

/** Metaprogramming helper traits class for the L2_simple (Euclidean) metric */
struct metric_L2_Model_2 {
  template<class T, class DataSource>
  struct traits {
    typedef nanoflann::L2_Simple_Adaptor<T, DataSource, double> distance_t;
  };
};

/** A simple vector-of-vectors adaptor for nanoflann, without duplicating the storage.
  *  The i'th vector represents a point in the state space.
  *
  *  \tparam DIM If set to >0, it specifies a compile-time fixed dimensionality for the points in
 * the data set, allowing more compiler optimizations.
  *  \tparam num_t The type of the point coordinates (typically, double or float).
  *  \tparam Distance The distance metric to use: nanoflann::metric_L1, nanoflann::metric_L2,
 * nanoflann::metric_L2_Simple, etc.
  *  \tparam IndexType The type for indices in the KD-tree index (typically, size_t of int)
  */
template<class VectorOfVectorsType,
         typename num_t     = double,
         typename DistType  = double,
         int DIM            = -1,
         class Distance     = nanoflann::metric_L2,
         typename IndexType = size_t>
struct KDTreeModelAdaptor {
  typedef KDTreeModelAdaptor<VectorOfVectorsType,
                             num_t,
                             DistType,
                             DIM,
                             Distance>
    self_t;
  typedef
    typename Distance::template traits<num_t, self_t>::distance_t metric_t;
  typedef nanoflann::KDTreeSingleIndexAdaptor<metric_t, self_t, DIM, IndexType>
           index_t;
  index_t* index;
  /// Constructor: takes a const ref to the vector of vectors object with the data points
  KDTreeModelAdaptor() : index(NULL) {}
  void init(const int                  dimensionality,
            const VectorOfVectorsType& mat,
            const int                  leaf_max_size = 10) {
    m_data = mat;
    if (index) {
      delete index;
      index = NULL;
    }
    index =
      new index_t(dimensionality,
                  *this /* adaptor */,
                  nanoflann::KDTreeSingleIndexAdaptorParams(leaf_max_size));
    index->buildIndex();
  }

  /// Constructor: takes a const ref to the vector of vectors object with the data points
  KDTreeModelAdaptor(const int                  dimensionality,
                     const VectorOfVectorsType& mat,
                     const int                  leaf_max_size = 10)
    : index(NULL), m_data(mat) {
    if (index) {
      delete index;
      index = NULL;
    }
    index =
      new index_t(dimensionality,
                  *this /* adaptor */,
                  nanoflann::KDTreeSingleIndexAdaptorParams(leaf_max_size));
    index->buildIndex();
  }

  ~KDTreeModelAdaptor() { delete index; }

  const VectorOfVectorsType& m_data;
  inline void                query(const num_t* query_point,
                                   const size_t num_closest,
                                   IndexType*   out_indices,
                                   num_t*       out_distances_sq,
                                   const int    nChecks_IGNORED = 10) const {
    nanoflann::KNNResultSet<num_t, IndexType> resultSet(num_closest);
    resultSet.init(out_indices, out_distances_sq);
    index->findNeighbors(resultSet, query_point, nanoflann::SearchParams());
  }
  const self_t& derived() const { return *this; }
  self_t&       derived() { return *this; }
  // Must return the number of data points
  inline size_t kdtree_get_point_count() const {
    return m_data.getPositionCount();
  }

  // Returns the distance between the vector "p1[0:size-1]" and the data point with index "idx_p2"
  // stored in the class:
  inline DistType
  kdtree_distance(const num_t* p1, const size_t idx_p2, size_t size) const {
    DistType        s   = 0;
    const glm::vec3 pos = m_data.fetchPosition(idx_p2);
    for (size_t i = 0; i < size; i++) {
      const DistType d = (DistType)p1[i] - (DistType)pos[i];
      s += d * d;
    }
    return s;
  }

  // Returns the dim'th component of the idx'th point in the class:
  inline num_t kdtree_get_pt(const size_t idx, int dim) const {
    const glm::vec3 pos = m_data.fetchPosition(idx);
    return pos[dim];
  }

  // Optional bounding-box computation: return false to default to a standard bbox computation loop.
  //   Return true if the BBOX was already computed by the class and returned in "bb" so it can be
  //   avoided to redo it again.
  //   Look at bb.size() to find out the expected dimensionality (e.g. 2 or 3 for point clouds)
  template<class BBOX>
  bool kdtree_get_bbox(BBOX& /*bb*/) const {
    return false;
  }

  /** @} */

};  // end of KDTreeModelAdaptor

//============================================================================

typedef KDTreeModelAdaptor<mm::Model,
                           float,
                           float,
                           3,
                           metric_L2_Model_2,
                           size_t>
  KdTreeAdaptor;

//============================================================================

struct ModelQuery {
  glm::vec3 point;
  double    radius;
  size_t    nearestNeighborCount;
};

//============================================================================

class ModelResult {
public:
  ModelResult() = default;
  ~ModelResult() {
    indices_.clear();
    dist_.clear();
  }
  inline void resize(const size_t size) {
    indices_.resize(size);
    dist_.resize(size);
  }
  inline void reserve(const size_t size) {
    indices_.reserve(size);
    dist_.reserve(size);
  }
  inline size_t size() const {
    assert(indices_.size() == dist_.size());
    return indices_.size();
  }
  inline size_t  count() const { return size(); }
  inline size_t& indices(size_t index) { return indices_[index]; }
  inline double& dist(size_t index) { return dist_[index]; }
  inline size_t* indices() { return indices_.data(); }
  inline double* dist() { return dist_.data(); }
  inline void    pushBack(const std::pair<size_t, double>& value) {
    indices_.push_back(value.first);
    dist_.push_back(value.second);
  }
  inline void popBack() {
    indices_.pop_back();
    dist_.pop_back();
  }

private:
  std::vector<size_t> indices_;
  std::vector<double> dist_;
};

//============================================================================

class KdTreeModel {
public:
  KdTreeModel() : kdtree_(nullptr) {}
  KdTreeModel(const mm::Model& model) : kdtree_(nullptr) { init(model); }
  ~KdTreeModel() { clear(); }
  void init(const mm::Model& model) {
    clear();
    kdtree_ = new KdTreeAdaptor(3, model, 10);
  }
  void search(const glm::vec3& point,
              const size_t     num_results,
              ModelResult&     results) const {
    if (num_results != results.size()) { results.resize(num_results); }
    auto retSize =
      (static_cast<KdTreeAdaptor*>(kdtree_))
        ->index->knnSearch(
          &point[0], num_results, results.indices(), results.dist());
    assert(retSize == results.size());
  }
  void clear() {
    if (kdtree_ != nullptr) {
      delete (static_cast<KdTreeAdaptor*>(kdtree_));
      kdtree_ = nullptr;
    }
  }

private:
  void* kdtree_;
};

//============================================================================

template<typename T>
static void
convertToModel(const TriangleMesh<T>& src, mm::Model& dst) {
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
  if (!src.materialIdxs().empty()) {
    const auto& materialIdxs = src.materialIdxs();
    dst.triangleMatIdx.resize(materialIdxs.size());
    for (size_t i = 0; i < materialIdxs.size(); i++) {
      dst.triangleMatIdx[i] = materialIdxs[i];
    }
  }
}

template void convertToModel(const TriangleMesh<float>&, mm::Model&);
template void convertToModel(const TriangleMesh<double>&, mm::Model&);

//============================================================================

void
convertToImage(const Frame<uint8_t>& src, mm::Image& dst) {
  const int32_t width  = src.width();
  const int32_t height = src.height();
  const auto&   p0     = src.plane(0);
  const auto&   p1     = src.plane(1);
  const auto&   p2     = src.plane(2);
  dst.reset(width, height);
  for (int32_t v = 0; v < height; v++) {
    for (int32_t u = 0; u < width; u++) {
      dst.data[(v * width + u) * 3 + 2] = p0(v, u);
      dst.data[(v * width + u) * 3 + 1] = p1(v, u);
      dst.data[(v * width + u) * 3 + 0] = p2(v, u);
    }
  }
}

//============================================================================

void
convertFromImage(const mm::Image& src, Frame<uint8_t>& dst) {
  const int32_t width  = src.width;
  const int32_t height = src.height;
  dst.resize(width, height, ColourSpace::BGR444p);
  auto& p0 = dst.plane(0);
  auto& p1 = dst.plane(1);
  auto& p2 = dst.plane(2);
  for (int32_t v = 0; v < height; v++) {
    for (int32_t u = 0; u < width; u++) {
      p0(v, u) = src.data[(v * width + u) * 3 + 2];
      p1(v, u) = src.data[(v * width + u) * 3 + 1];
      p2(v, u) = src.data[(v * width + u) * 3 + 0];
    }
  }
}

//============================================================================

void
transferColorsSimple(mm::Model& source, mm::Model& target) {
  printf("transferColorsSimple \n");
  fflush(stdout);
  const size_t pointCountSource = source.getPositionCount();
  const size_t pointCountTarget = target.getPositionCount();
  if ((pointCountSource == 0u) || (pointCountTarget == 0u)
      || !source.hasColors()) {
    return;
  }
  KdTreeModel  kdtreeSource(source);
  ModelResult  result;
  const size_t numResultsMax   = 30;
  const size_t numResultsIncr  = 5;
  bool         bAverageNormals = true;
  for (size_t index = 0; index < pointCountTarget; ++index) {
    size_t numResults = numResultsIncr;
    do {
      numResults += numResultsIncr;
      kdtreeSource.search(target.fetchPosition(index), numResults, result);
    } while (result.dist(0) == result.dist(int(result.size()) - 1)
             && bAverageNormals
             && numResults + numResultsIncr <= numResultsMax);

    kdtreeSource.search(target.fetchPosition(index), numResults, result);

    std::vector<size_t> sameDistPoints;
    sameDistPoints.push_back(result.indices(0));
    for (size_t n = 1; n < numResults; n++) {
      if (fabs(result.dist(n) - result.dist(n - 1)) < 1e-20) {
        sameDistPoints.push_back(result.indices(n));
      } else {
        break;
      }
    }
    glm::vec3 color(0);
    int       num = 0;
    for (auto& i : sameDistPoints) {
      color += source.fetchColor(i);
      num += 1;
    }
    for (size_t k = 0; k < 3; ++k) {
      target.colors[3 * index + k] = color[k] / (float)num;
    }
  }
}

void
transferColorsWeighted(mm::Model&                  source,
                       mm::Model&                  target,
                       const size_t                numResults,
                       const VMCEncoderParameters& params,
                       int32_t                     targetTexVideoWidth,
                       int32_t                     targetTexVideoHeight) {
  printf("transferColorsWeighted \n");
  fflush(stdout);

  const size_t pointCountSource = source.getPositionCount();
  const size_t pointCountTarget = target.getPositionCount();
  if ((pointCountSource == 0u) || (pointCountTarget == 0u)
      || !source.hasColors()) {
    return;
  }

  // KdTree initialization with reference model
  KdTreeModel kdtreeSource(source);
  ModelResult kdtreeResult;

  // compute the sigma term for Gaussian filtering option
  const glm::vec3 boundingBox(params.maxPosition[0] - params.minPosition[0],
                              params.maxPosition[1] - params.minPosition[1],
                              params.maxPosition[2] - params.minPosition[2]);
  const float     boundingBoxSize =
    std::max(boundingBox.x, std::max(boundingBox.y, boundingBox.z));

  const float gridSize =
    params.textureTransferGridSize == 0
      ? std::max(targetTexVideoWidth, targetTexVideoHeight)
      : params.textureTransferGridSize;

  const float sigma =
    params.textureTransferSigma
        == 0  // 0: mean linear filter 1: mean Gaussian filtering
      ? 1.0
      : boundingBoxSize / (gridSize * params.textureTransferSigma);
  const double sigmaMul2Pow2 = 1. / (2. * pow(sigma, 2.));
  const float  distOffset    = 4.f;

  // Compute filtering
  for (size_t index = 0; index < pointCountTarget; ++index) {
    kdtreeSource.search(target.fetchPosition(index), numResults, kdtreeResult);
    glm::vec3 color(0);
    double    sum = 0;
    for (size_t i = 0; i < numResults; i++) {
      const float weight =
        params.textureTransferSigma == 0
          ? 1. / (kdtreeResult.dist(i) + distOffset)
          : std::exp((-pow(kdtreeResult.dist(i), 2.0)) * sigmaMul2Pow2);
      color += source.fetchColor(kdtreeResult.indices(i)) * weight;
      sum += weight;
    }
    for (size_t k = 0; k < 3; ++k) {
      target.colors[3 * index + k] = color[k] / sum;
    }
  }
}

bool
transferColors(const mm::Model& source,
               mm::Model&       target,
               const int32_t    searchRange,
               const bool       losslessAttribute,
               const int        numNeighborsColorTransferFwd,
               const int        numNeighborsColorTransferBwd,
               const bool       useDistWeightedAverageFwd,
               const bool       useDistWeightedAverageBwd,
               const bool       skipAvgIfIdenticalSourcePointPresentFwd,
               const bool       skipAvgIfIdenticalSourcePointPresentBwd,
               const double     distOffsetFwd,
               const double     distOffsetBwd,
               const double     maxGeometryDist2FwdParam,
               const double     maxGeometryDist2BwdParam,
               const double     maxColorDist2FwdParam,
               const double     maxColorDist2BwdParam,
               const bool       excludeColorOutlier,
               const double     thresholdColorOutlierDist) {
  printf("transferColors\n");
  const size_t pointCountSource = source.getPositionCount();
  const size_t pointCountTarget = target.getPositionCount();
  if ((pointCountSource == 0u) || (pointCountTarget == 0u)
      || !source.hasColors()) {
    return false;
  }
  const double maxGeometryDist2Fwd = (maxGeometryDist2FwdParam < 512)
                                       ? maxGeometryDist2FwdParam
                                       : std::numeric_limits<double>::max();
  const double maxGeometryDist2Bwd = (maxGeometryDist2BwdParam < 512)
                                       ? maxGeometryDist2BwdParam
                                       : std::numeric_limits<double>::max();
  const double maxColorDist2Fwd    = (maxColorDist2FwdParam < 512)
                                       ? maxColorDist2FwdParam
                                       : std::numeric_limits<double>::max();
  const double maxColorDist2Bwd    = (maxColorDist2BwdParam < 512)
                                       ? maxColorDist2BwdParam
                                       : std::numeric_limits<double>::max();

  KdTreeModel            kdtreeSource(source);
  std::vector<glm::vec3> refinedColors1;
  refinedColors1.resize(pointCountTarget);
  // ==========================================================================================
  //                                     Forward direction
  // ==========================================================================================
  // for each target point indexed by index, derive the refined color as
  // refinedColors1[index]
  ModelResult result;
  for (size_t index = 0; index < pointCountTarget; ++index) {
    kdtreeSource.search(
      target.fetchPosition(index), numNeighborsColorTransferFwd, result);
    // keep the points that satisfy geometry dist threshold
    while (true) {
      if (result.size() == 1) { break; }
      if (result.dist(int(result.size()) - 1) <= maxGeometryDist2Fwd) {
        break;
      }
      result.popBack();
    }
    bool isDone = false;
    if (skipAvgIfIdenticalSourcePointPresentFwd) {
      if (result.dist(0) < 0.0001) {
        refinedColors1[index] = source.fetchColor(result.indices(0));
        isDone                = true;
      }
    }
    if (!isDone) {
      int nNN = static_cast<int>(result.size());
      while (nNN > 0 && !isDone) {
        if (nNN == 1) {
          refinedColors1[index] = source.fetchColor(result.indices(0));
          isDone                = true;
        }
        if (!isDone) {
          std::vector<glm::vec3> colors;
          colors.resize(0);
          colors.resize(nNN);
          for (int i = 0; i < nNN; ++i) {
            colors[i] = source.fetchColor(result.indices(i));  //
          }
          double maxColorDist2 = std::numeric_limits<double>::min();
          for (int i = 0; i < nNN; ++i) {
            for (int j = i + 1; j < nNN; ++j) {
              const double dist2 =
                glm::dot(colors[i] - colors[j], colors[i] - colors[j]);
              if (dist2 > maxColorDist2) { maxColorDist2 = dist2; }
            }
          }
          if (maxColorDist2 <= maxColorDist2Fwd) {
            glm::vec3 refinedColor(0.0);
            if (useDistWeightedAverageFwd) {
              double sumWeights{0.0};
              for (int i = 0; i < nNN; ++i) {
                const double weight = 1 / (result.dist(i) + distOffsetFwd);
                auto         color  = source.fetchColor(result.indices(i));
                for (size_t kk = 0; kk < 3; kk++) { color[kk] *= weight; }
                refinedColor += color;
                sumWeights += weight;
              }
              refinedColor /= sumWeights;
              if (excludeColorOutlier) {
                glm::vec3 excludeOutlierRefinedColor(0.0);
                size_t    excludeCount = 0;
                sumWeights             = 0.0;
                for (int i = 0; i < nNN; ++i) {
                  double    dist        = 0.0;
                  glm::vec3 tmpColor    = source.fetchColor(result.indices(i));
                  glm::vec3 sourceColor = tmpColor;
                  dist                  = glm::dot(sourceColor - refinedColor,
                                  sourceColor - refinedColor);
                  if (dist > thresholdColorOutlierDist
                               * thresholdColorOutlierDist) {
                    excludeCount += 1;
                    continue;
                  }
                  const double weight = 1 / (result.dist(i) + distOffsetFwd);
                  auto         color  = source.fetchColor(result.indices(i));
                  for (size_t kk = 0; kk < 3; kk++) { color[kk] *= weight; }
                  excludeOutlierRefinedColor += color;
                  sumWeights += weight;
                }

                if (excludeCount != nNN && excludeCount != 0) {
                  for (size_t kk = 0; kk < 3; kk++) {
                    refinedColor[kk] =
                      excludeOutlierRefinedColor[kk] / sumWeights;
                  }
                }
              }
            } else {
              for (int i = 0; i < nNN; ++i) {
                refinedColor += source.fetchColor(result.indices(i));  //
              }
              refinedColor /= nNN;
            }
            refinedColors1[index] = refinedColor;
            isDone                = true;
          } else {
            --nNN;
          }
        }
      }
    }
  }
  kdtreeSource.clear();

  // ==========================================================================================
  //                                  Backward direction
  // ==========================================================================================
  // for each target point, derive a vector of source candidate points as
  // colorsDists2.
  // colorsDists2 is iteratively refined (by removing the farthest points) until
  // the
  // std of remaining colors in it is smaller than a threshold.

  struct DistColorFloat {
    double    dist;
    glm::vec3 color;
  };
  KdTreeModel                              kdtreeTarget(target);
  std::vector<std::vector<DistColorFloat>> refinedColorsDists2;
  refinedColorsDists2.resize(pointCountTarget);
  // populate refinedColorsDists2
  for (size_t index = 0; index < pointCountSource; ++index) {
    const auto color = source.fetchColor(index);
    kdtreeTarget.search(
      source.fetchPosition(index), numNeighborsColorTransferBwd, result);
    // keep the points that satisfy geometry dist threshold
    for (int i = 0; i < result.size(); ++i) {
      if (result.dist(i) <= maxGeometryDist2Bwd) {
        refinedColorsDists2[result.indices(i)].push_back(
          DistColorFloat{result.dist(i), color});
      }
    }
  }
  // sort refinedColorsDists2 according to distance
  for (size_t index = 0; index < pointCountTarget; ++index) {
    std::sort(refinedColorsDists2[index].begin(),
              refinedColorsDists2[index].end(),
              [](DistColorFloat& dc1, DistColorFloat& dc2) {
                return dc1.dist < dc2.dist;
              });
  }
  // compute centroid2
  for (size_t index = 0; index < pointCountTarget; ++index) {
    const auto color1 =
      refinedColors1[index];  // refined color derived in forward direction
    auto& colorsDists2 =
      refinedColorsDists2[index];  // set of candidate points
                                   // derived in backward
                                   // direction
    if (colorsDists2.empty() || losslessAttribute) {
      for (size_t kk = 0; kk < 3; kk++) {
        target.colors[index * 3 + kk] = color1[kk];
      }
    } else {
      bool            isDone = false;
      const glm::vec3 centroid1(color1[0], color1[1], color1[2]);
      glm::vec3       centroid2(0.0);
      if (skipAvgIfIdenticalSourcePointPresentBwd) {
        if (colorsDists2[0].dist < 0.0001) {
          auto temp = colorsDists2[0];
          colorsDists2.clear();
          colorsDists2.push_back(temp);
          for (int k = 0; k < 3; ++k) {
            centroid2[k] = colorsDists2[0].color[k];
          }
          isDone = true;
        }
      }
      if (!isDone) {
        int nNN = static_cast<int>(colorsDists2.size());
        while (nNN > 0 && !isDone) {
          nNN = static_cast<int>(colorsDists2.size());
          if (nNN == 1) {
            auto temp = colorsDists2[0];
            colorsDists2.clear();
            colorsDists2.push_back(temp);
            for (int k = 0; k < 3; ++k) {
              centroid2[k] = colorsDists2[0].color[k];
            }
            isDone = true;
          }
          if (!isDone) {
            std::vector<glm::vec3> colors;
            colors.resize(0);
            colors.resize(nNN);
            for (int i = 0; i < nNN; ++i) {
              for (int k = 0; k < 3; ++k) {
                colors[i][k] = double(colorsDists2[i].color[k]);
              }
            }
            double maxColorDist2 = std::numeric_limits<double>::min();
            for (int i = 0; i < nNN; ++i) {
              for (int j = i + 1; j < nNN; ++j) {
                const double dist2 =
                  glm::dot(colors[i] - colors[j], colors[i] - colors[j]);
                if (dist2 > maxColorDist2) { maxColorDist2 = dist2; }
              }
            }
            if (maxColorDist2 <= maxColorDist2Bwd) {
              for (size_t k = 0; k < 3; ++k) { centroid2[k] = 0; }
              if (useDistWeightedAverageBwd) {
                double sumWeights{0.0};
                for (auto& i : colorsDists2) {
                  const double weight = 1 / (sqrt(i.dist) + distOffsetBwd);
                  for (size_t k = 0; k < 3; ++k) {
                    centroid2[k] += (i.color[k] * weight);
                  }
                  sumWeights += weight;
                }
                centroid2 /= sumWeights;
                if (excludeColorOutlier) {
                  glm::vec3 excludeOutlierCentroid2(0.0);
                  size_t    excludeCount = 0;
                  sumWeights             = 0.0;
                  for (auto& i : colorsDists2) {
                    glm::vec3 sourceColor(i.color[0], i.color[1], i.color[2]);
                    double    dist = glm::dot(sourceColor - centroid2,
                                           sourceColor - centroid2);
                    if (dist > thresholdColorOutlierDist
                                 * thresholdColorOutlierDist) {
                      excludeCount += 1;
                      continue;
                    }
                    const double weight = 1 / (sqrt(i.dist) + distOffsetBwd);
                    for (size_t k = 0; k < 3; ++k) {
                      excludeOutlierCentroid2[k] += (i.color[k] * weight);
                    }
                    sumWeights += weight;
                  }

                  if (excludeCount != nNN && excludeCount != 0) {
                    for (int k = 0; k < 3; ++k) {
                      centroid2[k] = excludeOutlierCentroid2[k] / sumWeights;
                    }
                  }
                }
              } else {
                for (auto& coldist : colorsDists2) {
                  for (int k = 0; k < 3; ++k) {
                    centroid2[k] += coldist.color[k];
                  }
                }
                centroid2 /= colorsDists2.size();
              }
              isDone = true;
            } else {
              colorsDists2.pop_back();
            }
          }
        }
      }
      auto   H  = double(colorsDists2.size());
      double D2 = 0.0;
      for (const auto& color2dist : colorsDists2) {
        auto color2 = color2dist.color;
        for (size_t k = 0; k < 3; ++k) {
          const double d2 = centroid2[k] - color2[k];
          D2 += d2 * d2;
        }
      }
      const double r = double(pointCountTarget) / double(pointCountSource);
      const double delta2 =
        glm::dot(centroid2 - centroid1, centroid2 - centroid1);
      const double eps = 0.000001;

      const bool fixWeight = true;      // m42538
      if (fixWeight || delta2 > eps) {  // centroid2 != centroid1
        double w = 0.0;

        if (!fixWeight) {
          const double alpha = D2 / delta2;
          const double a     = H * r - 1.0;
          const double c     = alpha * r - 1.0;
          if (fabs(a) < eps) {
            w = -0.5 * c;
          } else {
            const double delta = 1.0 - a * c;
            if (delta >= 0.0) { w = (-1.0 + sqrt(delta)) / a; }
          }
        }
        const double oneMinusW = 1.0 - w;
        glm::vec3    color0;
        for (size_t k = 0; k < 3; ++k) {
          color0[k] = w * centroid1[k] + oneMinusW * centroid2[k];
        }
        const double rSource  = 1.0 / double(pointCountSource);
        const double rTarget  = 1.0 / double(pointCountTarget);
        const float  maxValue = std::numeric_limits<uint8_t>::max();
        double       minError = std::numeric_limits<double>::max();
        glm::vec3    bestColor(color0);
        glm::vec3    color;
        for (int32_t s1 = -searchRange; s1 <= searchRange; ++s1) {
          color[0] = clip(color0[0] + s1, 0.0f, maxValue);
          for (int32_t s2 = -searchRange; s2 <= searchRange; ++s2) {
            color[1] = clip(color0[1] + s2, 0.0f, maxValue);
            for (int32_t s3 = -searchRange; s3 <= searchRange; ++s3) {
              color[2] = clip(color0[2] + s3, 0.0f, maxValue);

              double e1 = 0.0;
              for (size_t k = 0; k < 3; ++k) {
                const double d = color[k] - color1[k];
                e1 += d * d;
              }
              e1 *= rTarget;

              double e2 = 0.0;
              for (const auto& color2dist : colorsDists2) {
                auto color2 = color2dist.color;
                for (size_t k = 0; k < 3; ++k) {
                  const double d = color[k] - color2[k];
                  e2 += d * d;
                }
              }
              e2 *= rSource;

              const double error = std::max(e1, e2);
              if (error < minError) {
                minError  = error;
                bestColor = color;
              }
            }
          }
        }
        for (size_t k = 0; k < 3; ++k) {
          target.colors[3 * index + k] = bestColor[k];
        }
      } else {  // centroid2 == centroid1
        for (size_t k = 0; k < 3; ++k) {
          target.colors[3 * index + k] = color1[k];
        }
      }
    }
  }
  return true;
}

//============================================================================

void
trace(const std::string& filename, const mm::Model& output) {
  std::cout << "Input model: " << filename << std::endl;
  std::cout << "  Vertices: " << output.vertices.size() / 3 << std::endl;
  std::cout << "  UVs: " << output.uvcoords.size() / 2 << std::endl;
  std::cout << "  Colors: " << output.colors.size() / 3 << std::endl;
  std::cout << "  Normals: " << output.normals.size() / 3 << std::endl;
  std::cout << "  Triangles: " << output.triangles.size() / 3 << std::endl;
  std::cout << "  Trianglesuv: " << output.trianglesuv.size() / 3 << std::endl;
}

//============================================================================

void
dequantize(mm::Model&                  input,
           mm::Model&                  output,
           const VMCEncoderParameters& params,
           const bool                  normalizeUV = true) {
  const glm::vec3 minPos(
    params.minPosition[0], params.minPosition[1], params.minPosition[2]);
  const glm::vec3 maxPos(
    params.maxPosition[0], params.maxPosition[1], params.maxPosition[2]);
  const glm::vec2 zeroVec2(0, 0);
  const glm::vec2 oneVec2(1, 1);
  const glm::vec3 zeroVec3(0, 0, 0);
  printf("De-quantizing \n");
  printf("  qp = %zu \n", params.bitDepthPosition);
  printf("  qt = %zu \n", params.bitDepthTexCoord);
  printf("  qn = 0 \n");
  printf("  qc = 0 \n");
  mm::Dequantize::dequantize(input,                    // input
                             output,                   // output
                             params.bitDepthPosition,  // qp
                             normalizeUV ? params.bitDepthTexCoord : 0,  // qt,
                             0,                                          // qn,
                             0,                                          // qc,
                             minPos,    // minPos
                             maxPos,    // maxPos
                             zeroVec2,  // minUv
                             oneVec2,   // maxUv
                             zeroVec3,  // minNrm
                             zeroVec3,  // maxNrm
                             zeroVec3,  // minCol
                             zeroVec3,  // maxCol
                             true,      // useFixedPoint,
                             false);    // colorSpaceConversion
}

//============================================================================

void
reindex(mm::Model&                  input,
        mm::Model&                  output,
        const VMCEncoderParameters& params) {
  const std::string sort = "oriented";
  printf("Reindex \n");
  printf("  sort = %s \n", sort.c_str());
  output.header   = input.header;
  output.comments = input.comments;
  mm::reorder(input, sort, output);
  // output.trianglesuv.clear();
}

//============================================================================

// we use ray tracing to process the result, we could also use a rasterization (might be faster)
void
meshToPcGrid(const mm::Model&                 input,
             mm::Model&                       output,
             const std::vector<mm::ImagePtr>& tex_map,
             const size_t                     resolution,
             const bool                       bilinear,
             const bool                       logProgress,
             bool                             useNormal,
             bool                             useFixedPoint,
             glm::vec3&                       minPos,
             glm::vec3&                       maxPos,
             const bool                       verbose = true) {
  // computes the bounding box of the vertices
  glm::vec3     minBox       = minPos;
  glm::vec3     maxBox       = maxPos;
  const int32_t fixedPoint16 = (1u << 16);
  glm::vec3     stepSize;
  if (minPos == maxPos) {
    mm::Geometry::computeBBox(input.vertices, minBox, maxBox);
    if (verbose) {
      std::cout << "Computing positions range" << std::endl;
      std::cout << "minBox = " << minBox[0] << "," << minBox[1] << ","
                << minBox[2] << std::endl;
      std::cout << "maxBox = " << maxBox[0] << "," << maxBox[1] << ","
                << maxBox[2] << std::endl;
      std::cout << "Transform bounding box to square box" << std::endl;
    }
    // hence sampling will be unform in the three dimensions
    //mm::Geometry::toCubicalBBox(
    //  minBox,
    //  maxBox);  // this will change the origin of the coordinate system (but it is just a translation)
    //stepSize = (maxBox - minBox) * (1.0F / (float)(resolution - 1));
    const glm::vec3 diag  = maxBox - minBox;
    float           range = std::max(std::max(diag.x, diag.y), diag.z);
    stepSize[0]           = range * (1.0F / (float)(resolution - 1));
    stepSize[1]           = range * (1.0F / (float)(resolution - 1));
    stepSize[2]           = range * (1.0F / (float)(resolution - 1));
  } else {
    if (verbose) {
      std::cout << "Using parameter positions range" << std::endl;
      std::cout << "minPos = " << minPos[0] << "," << minPos[1] << ","
                << minPos[2] << std::endl;
      std::cout << "maxPos = " << maxPos[0] << "," << maxPos[1] << ","
                << maxPos[2] << std::endl;
    }
    if (useFixedPoint) {
      // converting the values to a fixed point representation
      // minBox(FP16) will be used in AAPS -> shift
      for (int i = 0; i < 3; i++) {
        if (minBox[i] > 0)
          minBox[i] = (std::floor(minBox[i] * fixedPoint16)) / fixedPoint16;
        else
          minBox[i] = (-1) * (std::ceil(std::abs(minBox[i]) * fixedPoint16))
                      / fixedPoint16;
        if (maxBox[i] > 0) {
          maxBox[i] = std::ceil(maxPos[i] * fixedPoint16) / fixedPoint16;
        } else
          maxBox[i] = (-1) * (std::floor(std::abs(maxBox[i]) * fixedPoint16))
                      / fixedPoint16;
      }
    }
    if (verbose) {
      std::cout << "minBox = " << minBox[0] << "," << minBox[1] << ","
                << minBox[2] << std::endl;
      std::cout << "maxBox = " << maxBox[0] << "," << maxBox[1] << ","
                << maxBox[2] << std::endl;
    }
    const glm::vec3 diag  = maxBox - minBox;
    float           range = std::max(std::max(diag.x, diag.y), diag.z);
    stepSize[0]           = range * (1.0F / (float)(resolution - 1));
    stepSize[1]           = range * (1.0F / (float)(resolution - 1));
    stepSize[2]           = range * (1.0F / (float)(resolution - 1));
    if (useFixedPoint) {
      stepSize[0] = (std::ceil(stepSize[0] * fixedPoint16)) / fixedPoint16;
      stepSize[1] = (std::ceil(stepSize[1] * fixedPoint16)) / fixedPoint16;
      stepSize[2] = (std::ceil(stepSize[2] * fixedPoint16)) / fixedPoint16;
    }
  }

  if (verbose) {
    std::cout << "stepSize = " << stepSize[0] << "," << stepSize[1] << ","
              << stepSize[2] << std::endl;
  }

  // we will now sample between min and max over the three dimensions, using resolution
  // by throwing rays from the three orthogonal faces of the box XY, XZ, YZ

  // to prevent storing duplicate points, we use a ModelBuilder
  mm::ModelBuilder builder(output);

  size_t skipped = 0;  // number of degenerate triangles

  // for each triangle
  for (size_t triIdx = 0; triIdx < input.triangles.size() / 3; ++triIdx) {
    const auto& image = mm::getImage(tex_map, input.triangleMatIdx[triIdx]);

    if (logProgress)
      std::cout << '\r' << triIdx << "/" << input.triangles.size() / 3
                << std::flush;

    mm::Vertex v1, v2, v3;

    fetchTriangle(input,
                  triIdx,
                  input.uvcoords.size() != 0,
                  input.colors.size() != 0,
                  input.normals.size() != 0,
                  v1,
                  v2,
                  v3);

    // check if triangle is not degenerate
    if (mm::Geometry::triangleArea(v1.pos, v2.pos, v3.pos) < DBL_EPSILON) {
      ++skipped;
      continue;
    }

    // compute face normal
    glm::vec3 normal;
    mm::Geometry::triangleNormal(v1.pos, v2.pos, v3.pos, normal);

    // extract the triangle bbox
    glm::vec3 triMinBox, triMaxBox;
    mm::Geometry::triangleBBox(v1.pos, v2.pos, v3.pos, triMinBox, triMaxBox);

    // now find the Discrete range from global box to triangle box
    glm::vec3 lmin = glm::floor(
      (triMinBox - minBox)
      / stepSize);  // can lead to division by zero with flat box, handled later
    glm::vec3 lmax = glm::ceil((triMaxBox - minBox) / stepSize);  // idem
    glm::vec3 lcnt = lmax - lmin;

    // now we will send rays on this triangle from the discreet steps of this box
    // rayTrace from the three main axis

    // reordering the search to start with the direction closest to the triangle normal
    glm::ivec3 mainAxisVector(0, 1, 2);
    // we want to preserve invariance with existing references if useNormal is disabled
    // so we do the following reordering only if option is enabled
    if (useNormal) {
      if ((std::abs(normal[0]) >= std::abs(normal[1]))
          && (std::abs(normal[0]) >= std::abs(normal[2]))) {
        if (std::abs(normal[1]) >= std::abs(normal[2]))
          mainAxisVector = glm::ivec3(0, 1, 2);
        else mainAxisVector = glm::ivec3(0, 2, 1);
      } else {
        if ((std::abs(normal[1]) >= std::abs(normal[0]))
            && (std::abs(normal[1]) >= std::abs(normal[2]))) {
          if (std::abs(normal[0]) >= std::abs(normal[2]))
            mainAxisVector = glm::ivec3(1, 0, 2);
          else mainAxisVector = glm::ivec3(1, 2, 0);
        } else {
          if (std::abs(normal[0]) >= std::abs(normal[1]))
            mainAxisVector = glm::ivec3(2, 0, 1);
          else mainAxisVector = glm::ivec3(2, 1, 0);
        }
      }
    }
    int mainAxisMaxIndex =
      useNormal
        ? 1
        : 3;  // if useNormal is selected, we only need to check the first index

    for (int mainAxisIndex = 0; mainAxisIndex < mainAxisMaxIndex;
         ++mainAxisIndex) {
      glm::vec3::length_type mainAxis = mainAxisVector[mainAxisIndex];
      // axis swizzling
      glm::vec3::length_type secondAxis = 1;
      glm::vec3::length_type thirdAxis  = 2;
      if (mainAxis == 1) {
        secondAxis = 0;
        thirdAxis  = 2;
      } else if (mainAxis == 2) {
        secondAxis = 0;
        thirdAxis  = 1;
      }

      // skip this axis if box is null sized on one of the two other axis
      if (minBox[secondAxis] == maxBox[secondAxis]
          || minBox[thirdAxis] == maxBox[thirdAxis])
        continue;

      // let's throw from mainAxis prependicular plane
      glm::vec3 rayOrigin    = {0.0, 0.0, 0.0};
      glm::vec3 rayDirection = {0.0, 0.0, 0.0};

      // on the main axis
      if (stepSize[mainAxis] == 0.0F) {  // handle stepSize[axis]==0
        // add small thress to be sure ray intersect in positive t
        rayOrigin[mainAxis] = minBox[mainAxis] - 0.5F;
      } else {
        rayOrigin[mainAxis] =
          minBox[mainAxis] + lmin[mainAxis] * stepSize[mainAxis];
      }
      // on main axis from min to max
      rayDirection[mainAxis] = 1.0;

      // iterate the second axis with i
      for (size_t i = 0; i <= lcnt[secondAxis]; ++i) {
        // iterate the third axis with j
        for (size_t j = 0; j <= lcnt[thirdAxis]; ++j) {
          // create the ray, starting from the face of the triangle bbox
          rayOrigin[secondAxis] =
            minBox[secondAxis] + (lmin[secondAxis] + i) * stepSize[secondAxis];
          rayOrigin[thirdAxis] =
            minBox[thirdAxis] + (lmin[thirdAxis] + j) * stepSize[thirdAxis];

          //  triplet, x = t, y = u, z = v with t the parametric and (u,v) the barycentrics
          glm::vec3 res;

          // let' throw the ray toward the triangle
          if (mm::Geometry::evalRayTriangle(
                rayOrigin, rayDirection, v1.pos, v2.pos, v3.pos, res)) {
            // we convert the result into a point with color
            mm::Vertex v;
            v.pos       = rayOrigin + rayDirection * res[0];
            v.nrm       = normal;
            v.hasNormal = true;

            // compute the color fi any
            // use the texture map
            if (input.uvcoords.size() != 0) {
              // use barycentric coordinates to extract point UV
              glm::vec2 uv;
              mm::Geometry::triangleInterpolation(
                v1.uv, v2.uv, v3.uv, res.y, res.z, uv);

              // fetch the color from the map
              if (image->data != NULL) {
                if (bilinear) texture2D_bilinear(*image, uv, v.col);
                else texture2D(*image, uv, v.col);
              }

              v.hasColor   = true;
              v.hasUVCoord = true;
              v.uv         = uv;
              // v.col = v.col * rayDirection; --> for debugging, paints the color of the vertex according to the
              // direction
            }
            // use color per vertex
            else if (input.colors.size() != 0) {
              // compute pixel color using barycentric coordinates
              v.col = v1.col * (1.0f - res.y - res.z) + v2.col * res.y
                      + v3.col * res.z;
              v.hasColor = true;
            }

            // add the vertex
            builder.pushVertex(v);
          }
        }
      }
    }
  }
  if (logProgress) std::cout << std::endl;
  if (verbose) {
    if (skipped != 0)
      std::cout << "Skipped " << skipped << " degenerate triangles"
                << std::endl;
    if (builder.foundCount != 0)
      std::cout << "Skipped " << builder.foundCount << " duplicate vertices"
                << std::endl;
    std::cout << "Generated " << output.vertices.size() / 3 << " points"
              << std::endl;
  }
}

//============================================================================

// perform a reverse sampling of the texture map to generate mesh samples
// the color of the point is then using the texel color => no filtering
void
meshToPcMap(const mm::Model&                 input,
            mm::Model&                       output,
            const std::vector<mm::ImagePtr>& tex_map,
            bool                             logProgress) {
  if (input.uvcoords.size() == 0) {
    std::cerr << "Error: cannot back sample model, no UV coordinates"
              << std::endl;
    return;
  }
  // if ( tex_map.width <= 0 || tex_map.height <= 0 || tex_map.nbc < 3 || tex_map.data == NULL ) {
  //   std::cerr << "Error: cannot back sample model, no valid texture map" << std::endl;
  //   return;
  // }

  // to prevent storing duplicate points, we use a ModelBuilder
  mm::ModelBuilder builder(output);

  size_t skipped = 0;  // number of degenerate triangles

  // For each triangle
  for (size_t triIdx = 0; triIdx < input.triangles.size() / 3; ++triIdx) {
    const auto& image = mm::getImage(tex_map, input.triangleMatIdx[triIdx]);

    if (logProgress)
      std::cout << '\r' << triIdx << "/" << input.triangles.size() / 3
                << std::flush;

    mm::Vertex v1, v2, v3;

    mm::fetchTriangle(input,
                      triIdx,
                      input.uvcoords.size() != 0,
                      input.colors.size() != 0,
                      input.normals.size() != 0,
                      v1,
                      v2,
                      v3);

    // check if triangle is not degenerate
    if (mm::Geometry::triangleArea(v1.pos, v2.pos, v3.pos) < DBL_EPSILON) {
      ++skipped;
      continue;
    }

    // compute face normal
    glm::vec3 normal;
    mm::Geometry::triangleNormal(v1.pos, v2.pos, v3.pos, normal);

    // compute the UVs bounding box
    glm::vec2 uvMin = {FLT_MAX, FLT_MAX};
    glm::vec2 uvMax = {-FLT_MAX, -FLT_MAX};
    uvMin           = glm::min(v3.uv, glm::min(v2.uv, glm::min(v1.uv, uvMin)));
    uvMax           = glm::max(v3.uv, glm::max(v2.uv, glm::max(v1.uv, uvMax)));

    // find the integer coordinates covered in the map
    glm::i32vec2 intUvMin = {(image->width - 1) * uvMin.x,
                             (image->height - 1) * uvMin.y};
    glm::i32vec2 intUvMax = {(image->width - 1) * uvMax.x,
                             (image->height - 1) * uvMax.y};

    // loop over the box in image space
    // if a pixel center is in the triangle then backproject
    // and create a new point with the pixel color
    for (size_t i = intUvMin[0]; i <= intUvMax[0]; ++i) {
      for (size_t j = intUvMin[1]; j <= intUvMax[1]; ++j) {
        // the new vertex
        mm::Vertex v;
        // force to face normal
        v.hasNormal = true;
        v.nrm       = normal;
        // get the UV for the center of the pixel
        v.hasUVCoord = true;
        v.uv         = {(0.5F + i) / image->width, (0.5F + j) / image->height};

        // test if this pixelUV is in the triangle UVs
        glm::vec3 bary;  // the barycentrics if success
        if (mm::Geometry::getBarycentric(v.uv, v1.uv, v2.uv, v3.uv, bary)) {
          // revert pixelUV to find point in 3D
          mm::Geometry::triangleInterpolation(
            v1.pos, v2.pos, v3.pos, bary.x, bary.y, v.pos);

          // fetch the color
          if (image->data != NULL) { texture2D(*image, v.uv, v.col); }
          v.hasColor = true;

          // add to results
          builder.pushVertex(v);
        }
      }
    }
  }
  if (logProgress) std::cout << std::endl;
  if (skipped != 0)
    std::cout << "Skipped " << skipped << " degenerate triangles" << std::endl;
  if (builder.foundCount != 0)
    std::cout << "Skipped " << builder.foundCount << " duplicate vertices"
              << std::endl;
  std::cout << "Generated " << output.vertices.size() / 3 << " points"
            << std::endl;
}

//============================================================================

void
sample(const mm::Model&                 input,
       const std::vector<mm::ImagePtr>& map,
       mm::Model&                       output,
       const bool                       useMap,
       const VMCEncoderParameters&      params,
       int32_t                          targetTexVideoWidth,
       int32_t                          targetTexVideoHeight) {
  const bool hideProgress = true;
  if (!useMap) {
    const bool useFixedPoint = true;
    const bool useNormal     = true;
    const bool bilinear      = true;
    glm::vec3  minPos(
      params.minPosition[0], params.minPosition[1], params.minPosition[2]);
    glm::vec3 maxPos(
      params.maxPosition[0], params.maxPosition[1], params.maxPosition[2]);

    int32_t gridSize = params.textureTransferGridSize;
    if (params.textureTransferGridSize == 0)
      gridSize = (std::max)(targetTexVideoWidth, targetTexVideoHeight);

    printf(" Sampling in GRID mode \n");
    printf("   Grid Size       = %zu \n", gridSize);
    printf("   Use Normal      = %d \n", useNormal);
    printf("   Bilinear        = %d \n", bilinear);
    printf("   hideProgress    = %d \n", hideProgress);
    printf("   nbSamplesMin    = 0 \n");
    printf("   nbSamplesMax    = 0 \n");
    printf("   maxIterations   = 10 \n");
    printf("   using contrained mode with gridSize \n");
    meshToPcGrid(input,
                 output,
                 map,
                 gridSize,
                 bilinear,
                 !hideProgress,
                 useNormal,
                 useFixedPoint,
                 minPos,
                 maxPos);
  } else {
    printf(" Sampling in MAP mode \n");
    meshToPcMap(input, output, map, !hideProgress);
  }
}

//============================================================================

// we use ray tracing to process the result, we could also use a rasterization (might be faster)
void
meshToPcGrid(const mm::Model&                 input,
             mm::Model&                       output,
             const std::vector<mm::ImagePtr>& tex_map,
             const size_t                     resolution,
             const bool                       bilinear,
             const bool                       useNormal,
             bool                             useFixedPoint,
             glm::vec3&                       minPos,
             glm::vec3&                       maxPos) {
  // computes the bounding box of the vertices
  glm::vec3     minBox       = minPos;
  glm::vec3     maxBox       = maxPos;
  const int32_t fixedPoint16 = (1u << 16);
  if (useFixedPoint) {
    // converting the values to a fixed point representation
    // minBox(FP16) will be used in AAPS -> shift
    for (int i = 0; i < 3; i++) {
      if (minBox[i] > 0)
        minBox[i] = (std::floor(minBox[i] * fixedPoint16)) / fixedPoint16;
      else
        minBox[i] = (-1) * (std::ceil(std::abs(minBox[i]) * fixedPoint16))
                    / fixedPoint16;
      if (maxBox[i] > 0) {
        maxBox[i] = std::ceil(maxPos[i] * fixedPoint16) / fixedPoint16;
      } else
        maxBox[i] = (-1) * (std::floor(std::abs(maxBox[i]) * fixedPoint16))
                    / fixedPoint16;
    }
  }
  const glm::vec3 diag  = maxBox - minBox;
  float           range = std::max(std::max(diag.x, diag.y), diag.z);
  float           step  = range * (1.0F / (float)(resolution - 1));
  if (useFixedPoint) step = (std::ceil(step * fixedPoint16)) / fixedPoint16;
  const glm::vec3 stepSize(step, step, step);

  // we will now sample between min and max over the three dimensions, using resolution
  // by throwing rays from the three orthogonal faces of the box XY, XZ, YZ

  // to prevent storing duplicate points, we use a ModelBuilder
  mm::ModelBuilder builder(output);

  // for each triangle
  const glm::vec3::length_type secondAxisTable[3] = {1, 0, 0};
  const glm::vec3::length_type thirdAxisTable[3]  = {2, 2, 1};
  const size_t                 triangleCount      = input.triangles.size() / 3;
  for (size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
    const auto& image = mm::getImage(tex_map, input.triangleMatIdx[triIdx]);

    mm::Vertex v1, v2, v3;
    mm::fetchTriangle(input, triIdx, true, false, false, v1, v2, v3);

    // check if triangle is not degenerate
    if (mm::Geometry::triangleArea(v1.pos, v2.pos, v3.pos) < DBL_EPSILON) {
      continue;
    }

    // compute face normal
    glm::vec3 normal;
    mm::Geometry::triangleNormal(v1.pos, v2.pos, v3.pos, normal);

    // extract the triangle bbox
    glm::vec3 triMinBox, triMaxBox;
    mm::Geometry::triangleBBox(v1.pos, v2.pos, v3.pos, triMinBox, triMaxBox);

    // now find the Discrete range from global box to triangle box
    glm::vec3 lmin = glm::floor((triMinBox - minBox) / stepSize);
    // can lead to division by zero with flat box, handled later
    glm::vec3 lmax = glm::ceil((triMaxBox - minBox) / stepSize);  // idem
    glm::vec3 lcnt = lmax - lmin;

    // now we will send rays on this triangle from the discreet steps of this box
    // rayTrace from the three main axis

    // reordering the search to start with the direction closest to the triangle normal
    glm::ivec3 mainAxisVector(0, 1, 2);
    // we want to preserve invariance with existing references if useNormal is disabled
    // so we do the following reordering only if option is enabled
    if (useNormal) {
      if ((std::abs(normal[0]) >= std::abs(normal[1]))
          && (std::abs(normal[0]) >= std::abs(normal[2]))) {
        if (std::abs(normal[1]) >= std::abs(normal[2]))
          mainAxisVector = glm::ivec3(0, 1, 2);
        else mainAxisVector = glm::ivec3(0, 2, 1);
      } else {
        if ((std::abs(normal[1]) >= std::abs(normal[0]))
            && (std::abs(normal[1]) >= std::abs(normal[2]))) {
          if (std::abs(normal[0]) >= std::abs(normal[2]))
            mainAxisVector = glm::ivec3(1, 0, 2);
          else mainAxisVector = glm::ivec3(1, 2, 0);
        } else {
          if (std::abs(normal[0]) >= std::abs(normal[1]))
            mainAxisVector = glm::ivec3(2, 0, 1);
          else mainAxisVector = glm::ivec3(2, 1, 0);
        }
      }
    }
    const int mainAxisMaxIndex =
      useNormal
        ? 1
        : 3;  // if useNormal is selected, we only need to check the first index

    for (int mainAxisIndex = 0; mainAxisIndex < mainAxisMaxIndex;
         ++mainAxisIndex) {
      glm::vec3::length_type mainAxis = mainAxisVector[mainAxisIndex];
      // axis swizzling
      glm::vec3::length_type secondAxis = secondAxisTable[mainAxis];
      glm::vec3::length_type thirdAxis  = thirdAxisTable[mainAxis];

      // skip this axis if box is null sized on one of the two other axis
      if (minBox[secondAxis] == maxBox[secondAxis]
          || minBox[thirdAxis] == maxBox[thirdAxis])
        continue;

      // let's throw from mainAxis prependicular plane
      glm::vec3 rayOrigin    = {0.0, 0.0, 0.0};
      glm::vec3 rayDirection = {0.0, 0.0, 0.0};

      // on the main axis
      if (stepSize[mainAxis] == 0.0F) {  // handle stepSize[axis]==0
        // add small thress to be sure ray intersect in positive t
        rayOrigin[mainAxis] = minBox[mainAxis] - 0.5F;
      } else {
        rayOrigin[mainAxis] =
          minBox[mainAxis] + lmin[mainAxis] * stepSize[mainAxis];
      }
      // on main axis from min to max
      rayDirection[mainAxis] = 1.0;

      // iterate the second axis with i
      for (size_t i = 0; i <= lcnt[secondAxis]; ++i) {
        // iterate the third axis with j
        for (size_t j = 0; j <= lcnt[thirdAxis]; ++j) {
          // create the ray, starting from the face of the triangle bbox
          rayOrigin[secondAxis] =
            minBox[secondAxis] + (lmin[secondAxis] + i) * stepSize[secondAxis];
          rayOrigin[thirdAxis] =
            minBox[thirdAxis] + (lmin[thirdAxis] + j) * stepSize[thirdAxis];

          //  triplet, x = t, y = u, z = v with t the parametric and (u,v) the barycentrics
          glm::vec3 res;

          // let' throw the ray toward the triangle
          if (mm::Geometry::evalRayTriangle(
                rayOrigin, rayDirection, v1.pos, v2.pos, v3.pos, res)) {
            // we convert the result into a point with color
            mm::Vertex v;
            v.pos       = rayOrigin + rayDirection * res[0];
            v.nrm       = normal;
            v.hasNormal = true;

            // compute the color fi any use the texture map
            if (input.uvcoords.size() != 0) {
              // use barycentric coordinates to extract point UV
              glm::vec2 uv;
              mm::Geometry::triangleInterpolation(
                v1.uv, v2.uv, v3.uv, res.y, res.z, uv);

              // fetch the color from the map
              if (bilinear) mm::texture2D_bilinear(*image, uv, v.col);
              else mm::texture2D(*image, uv, v.col);
              v.hasColor = true;
              // v.col = v.col * rayDirection; --> for debugging, paints the color of the vertex according to the
              // direction
            }
            // add the vertex
            builder.pushVertex(v);
          }
        }
      }
    }
  }
}

//============================================================================

// perform a reverse sampling of the texture map to generate mesh samples
// the color of the point is then using the texel color => no filtering
void
mapSampling(const mm::Model&            source,
            const mm::Model&            target,
            Frame<uint8_t>&             map,
            Plane<uint8_t>&             ocm,
            const VMCEncoderParameters& params,
            int32_t                     targetTexVideoWidth,
            int32_t                     targetTexVideoHeight) {
  if (source.getPositionCount() == 0 || !source.hasColors()) {
    std::cerr << "Error: cannot back sample model, no color coordinates"
              << std::endl;
    return;
  }

  // KdTree initialization with reference model
  KdTreeModel  kdtreeSource(source);
  ModelResult  kdtreeResult;
  const size_t numResults = params.textureTransferMapNumPoints;

  // Compute the sigma term for Gaussian filtering option
  const glm::vec3 boundingBox(params.maxPosition[0] - params.minPosition[0],
                              params.maxPosition[1] - params.minPosition[1],
                              params.maxPosition[2] - params.minPosition[2]);
  const float     boundingBoxSize =
    std::max(boundingBox.x, std::max(boundingBox.y, boundingBox.z));

  const float gridSize =
    params.textureTransferGridSize == 0
      ? std::max(targetTexVideoWidth, targetTexVideoHeight)
      : params.textureTransferGridSize;

  // Sigma: 0: mean linear filter 1: mean Gaussian filtering
  const float sigma =
    params.textureTransferSigma == 0
      ? 1.0
      : boundingBoxSize / (gridSize * params.textureTransferSigma);
  const double sigmaMul2Pow2 = 1. / (2. * pow(sigma, 2.));
  const float  distOffset    = 4.f;

  // init output map
  const auto      width  = map.width();
  const auto      height = map.height();
  const glm::vec2 textureSize(width, height);
  fflush(stdout);
  std::vector<glm::vec3> colorMapVal;
  std::vector<int32_t>   colorMapNum;
  colorMapVal.resize(width * height, glm::vec3(0));
  colorMapNum.resize(width * height, 0);
  mm::Model        output;
  mm::ModelBuilder builder(output);
  // For each triangle
  const size_t triangleCount = target.triangles.size() / 3;
  for (size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
    mm::Vertex v1, v2, v3;
    mm::fetchTriangle(target, triIdx, true, false, false, v1, v2, v3);

    // check if triangle is not degenerate
    if (mm::Geometry::triangleArea(v1.pos, v2.pos, v3.pos) < DBL_EPSILON)
      continue;

    // compute the UVs bounding box
    glm::vec2 uvMin = {FLT_MAX, FLT_MAX};
    glm::vec2 uvMax = {-FLT_MAX, -FLT_MAX};
    uvMin           = glm::min(v3.uv, glm::min(v2.uv, glm::min(v1.uv, uvMin)));
    uvMax           = glm::max(v3.uv, glm::max(v2.uv, glm::max(v1.uv, uvMax)));

    // find the integer coordinates covered in the map
    glm::i32vec2 intUvMin, intUvMax;
    intUvMin = {(width - 1) * uvMin.x, (height - 1) * uvMin.y};
    intUvMax = {(width - 1) * uvMax.x, (height - 1) * uvMax.y};

    // loop over the box in image space
    // if a pixel center is in the triangle then backproject
    // and create a new point with the pixel color
    for (size_t i = intUvMin[0]; i <= intUvMax[0]; ++i) {
      for (size_t j = intUvMin[1]; j <= intUvMax[1]; ++j) {
        // get the UV for the center of the pixel
        glm::vec2 uv = {(0.5F + i) / width, (0.5F + j) / height};

        // test if this pixelUV is in the triangle UVs
        glm::vec3 bary;  // the barycentrics if success
        if (mm::Geometry::getBarycentric(uv, v1.uv, v2.uv, v3.uv, bary)) {
          // revert pixelUV to find point in 3D
          glm::vec3 pos;
          mm::Geometry::triangleInterpolation(
            v1.pos, v2.pos, v3.pos, bary.x, bary.y, pos);
          // // compute search
          kdtreeSource.search(pos, numResults, kdtreeResult);
          glm::vec3 color(0);
          double    sum = 0;
          for (size_t i = 0; i < numResults; i++) {
            const float weight =
              params.textureTransferSigma == 0
                ? 1. / (kdtreeResult.dist(i) + distOffset)
                : std::exp((-pow(kdtreeResult.dist(i), 2.0)) * sigmaMul2Pow2);
            color += source.fetchColor(kdtreeResult.indices(i)) * weight;
            sum += weight;
          }
          for (size_t k = 0; k < 3; ++k) color[k] = color[k] / sum;
          glm::ivec2 mapCoord;
          mm::mapCoordClamped(uv, textureSize, mapCoord);
          colorMapVal[mapCoord.y * width + mapCoord.x] += color;
          colorMapNum[mapCoord.y * width + mapCoord.x]++;
        }
      }
    }
  }

  // Fill ocm and output map
  ocm.resize(width, height);
  ocm.fill(uint8_t(0));
  map.fill(0);
  auto& p0 = map.plane(2);
  auto& p1 = map.plane(1);
  auto& p2 = map.plane(0);
  for (int v = 0; v < height; v++) {
    for (int u = 0; u < width; u++) {
      auto& num = colorMapNum[v * width + u];
      if (num > 0) {
        auto& c = colorMapVal[v * width + u];
        c /= (float)num;
        ocm.set(v, u, 1);
        p0.set(v, u, uint8_t(std::round(clip(c[0], 0.f, 255.f))));
        p1.set(v, u, uint8_t(std::round(clip(c[1], 0.f, 255.f))));
        p2.set(v, u, uint8_t(std::round(clip(c[2], 0.f, 255.f))));
      }
    }
  }
}

//============================================================================

void
updateUVMap(mm::Model&                  input,
            Frame<uint8_t>&             map,
            const VMCEncoderParameters& params,
            Plane<uint8_t>&             ocm) {
  const auto      width  = map.width();
  const auto      height = map.height();
  const glm::vec2 textureSize(width, height);
  trace("test", input);
  printf("update UV map image size = %d x %d \n", width, height);
  printf("input.getPositionCount() = %zu \n", input.getPositionCount());
  printf("input.vertices           = %zu \n", input.vertices.size());
  printf("input.colors             = %zu \n", input.colors.size());
  printf("input.uvcoords           = %zu \n", input.uvcoords.size());
  fflush(stdout);
  std::vector<glm::vec3> colorMapVal;
  std::vector<int32_t>   colorMapNum;
  colorMapVal.resize(width * height, glm::vec3(0));
  colorMapNum.resize(width * height, 0);
  for (size_t i = 0; i < input.getPositionCount(); i++) {
    auto       c  = input.fetchColor(i);
    auto       uv = input.fetchUv(i);
    glm::ivec2 mapCoord;
    mm::mapCoordClamped(uv, textureSize, mapCoord);
    colorMapVal[mapCoord.y * width + mapCoord.x] += c;
    colorMapNum[mapCoord.y * width + mapCoord.x]++;
  }
  ocm.resize(width, height);
  ocm.fill(uint8_t(0));
  map.fill(0);
  auto& p0 = map.plane(2);
  auto& p1 = map.plane(1);
  auto& p2 = map.plane(0);
  for (int v = 0; v < height; v++) {
    for (int u = 0; u < width; u++) {
      auto& num = colorMapNum[v * width + u];
      if (num > 0) {
        auto& c = colorMapVal[v * width + u];
        c /= (float)num;
        ocm.set(v, u, 1);
        p0.set(v, u, uint8_t(std::round(clip(c[0], 0.f, 255.f))));
        p1.set(v, u, uint8_t(std::round(clip(c[1], 0.f, 255.f))));
        p2.set(v, u, uint8_t(std::round(clip(c[2], 0.f, 255.f))));
      }
    }
  }
}

//============================================================================

int
TransferColor::transferColorByPointclouds(
  const TriangleMesh<MeshType>&      targetMesh,
  const std::vector<Frame<uint8_t>>& targetTexture,
  const TriangleMesh<MeshType>&      reconstructedMesh,
  Frame<uint8_t>&                    reconstructedTexture,
  Plane<uint8_t>&                    ocm,
  const VMCEncoderParameters&        params) {
  // Src
  mm::Model srcObj, srcDequantize, srcReindex, srcSample;
  convertToModel(targetMesh, srcObj);
  std::vector<mm::ImagePtr> srcMap;
  srcMap.resize(targetTexture.size());
  for (int i = 0; i < targetTexture.size(); i++) {
    srcMap[i] = mm::ImagePtr(new mm::Image());
    convertToImage(targetTexture[i], *srcMap[i]);
  }
  dequantize(srcObj, srcDequantize, params);
  srcObj.reset();
  reindex(srcDequantize, srcReindex, params);
  srcDequantize.reset();
  sample(srcReindex,
         srcMap,
         srcSample,
         params.textureTransferWithMapSource
           && (params.textureTransferMethod != 3),
         params,
         reconstructedTexture.width(),
         reconstructedTexture.height());
  srcReindex.reset();
  srcMap.clear();

  // Rec
  mm::Model                 recObj, recDequantize, recReindex, recSample;
  std::vector<mm::ImagePtr> recMap;
  recMap.resize(1);
  recMap[0] = mm::ImagePtr(new mm::Image());
  convertToModel(reconstructedMesh, recObj);
  dequantize(recObj, recDequantize, params, false);
  recObj.reset();
  reindex(recDequantize, recReindex, params);
  recDequantize.reset();

  if (params.textureTransferMethod == 3) {
    mapSampling(srcSample,
                recReindex,
                reconstructedTexture,
                ocm,
                params,
                reconstructedTexture.width(),
                reconstructedTexture.height());
    srcSample.reset();
    recReindex.reset();
  } else {
    mm::Model recSampleMapAndGrid;
    if (params.textureTransferWithMap) {
      recMap[0]->width  = reconstructedTexture.width();
      recMap[0]->height = reconstructedTexture.height();
    }
    sample(recReindex,
           recMap,
           recSample,
           params.textureTransferWithMap,
           params,
           reconstructedTexture.width(),
           reconstructedTexture.height());
    recReindex.reset();
    switch (params.textureTransferMethod) {
    case 0:
      transferColors(
        srcSample,                           // source
        recSample,                           // target
        0,                                   // searchRange
        false,                               // losslessAttribute,
        params.textureTransferMapNumPoints,  // numNeighborsColorTransferFwd
        1,                                   // numNeighborsColorTransferBwd
        false,                               // useDistWeightedAverageFwd
        false,                               // useDistWeightedAverageBwd
        false,       // skipAvgIfIdenticalSourcePointPresentFwd
        false,       // skipAvgIfIdenticalSourcePointPresentBwd
        4,           // distOffsetFwd
        4,           // distOffsetBwd
        1000,        // maxGeometryDist2Fwd
        1000,        // maxGeometryDist2Bwd
        1000 * 256,  // maxColorDist2Fwd
        1000 * 256,  // maxColorDist2Bwd
        false,       // excludeColorOutlier
        10);         // thresholdColorOutlierDist
      break;
    case 1: transferColorsSimple(srcSample, recSample); break;
    case 2:
      transferColorsWeighted(srcSample,
                             recSample,
                             params.textureTransferMapNumPoints,
                             params,
                             reconstructedTexture.width(),
                             reconstructedTexture.height());
      break;
    default:
      printf("textureTransferMethod not supported \n");
      exit(-1);
      break;
    }
    srcSample.reset();

    // update uv map
    updateUVMap(recSample, reconstructedTexture, params, ocm);
    recSample.reset();
  }
  return 0;
}

//============================================================================

static bool
computeTriangleMapping(vmesh::TriangleMesh<MeshType>&       targetMesh,
                       const vmesh::TriangleMesh<MeshType>& sourceMesh,
                       std::vector<int32_t>&                mapping) {
  struct TriangleCentroidKey {
    int32_t x;
    int32_t y;
    int32_t z;

    bool operator==(const TriangleCentroidKey& other) const {
      return x == other.x && y == other.y && z == other.z;
    }
  };
  struct TriangleCentroidKeyHash {
    size_t operator()(const TriangleCentroidKey& key) const {
      size_t hash = std::hash<int32_t>{}(key.x);
      hash ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9 + (hash << 6)
              + (hash >> 2);
      hash ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9 + (hash << 6)
              + (hash >> 2);
      return hash;
    }
  };
  const auto centroidKey = [](const auto& mesh, int32_t triangleIndex) {
    const auto&  tri  = mesh.triangle(triangleIndex);
    Vec3<double> cent = 0;
    for (int32_t k = 0; k < 3; ++k) { cent += mesh.point(tri[k]); }
    return TriangleCentroidKey{int32_t(cent[0]),
                               int32_t(cent[1]),
                               int32_t(cent[2])};
  };

  std::unordered_map<TriangleCentroidKey, int32_t, TriangleCentroidKeyHash>
    cent2tindex;
  for (int32_t t = 0; t < targetMesh.triangleCount(); t++) {
    cent2tindex[centroidKey(targetMesh, t)] = t;
  }

  mapping.resize(sourceMesh.triangleCount());
  for (int32_t t = 0; t < sourceMesh.triangleCount(); t++) {
    const auto& tri = sourceMesh.triangle(t);
    const auto  it  = cent2tindex.find(centroidKey(sourceMesh, t));
    if (it == cent2tindex.end()) {
      mapping.clear();
      return false;
    }
    int32_t tt = it->second;
    mapping[t] = tt;

    auto          ttri   = targetMesh.triangle(tt);
    auto          ttriUV = targetMesh.texCoordTriangle(tt);
    Vec3<int32_t> order;
    bool          reorder = false;
    if (sourceMesh.point(tri[0]) != targetMesh.point(ttri[0])) {
      reorder = true;
      if (sourceMesh.point(tri[0]) == targetMesh.point(ttri[1])) {
        if (sourceMesh.point(tri[1]) == targetMesh.point(ttri[2])) {
          order = Vec3<int32_t>(1, 2, 0);
        } else {
          order = Vec3<int32_t>(1, 0, 2);
        }
      } else if (sourceMesh.point(tri[1]) == targetMesh.point(ttri[1])) {
        order = Vec3<int32_t>(2, 1, 0);
      } else {
        order = Vec3<int32_t>(2, 0, 1);
      }
    } else if (sourceMesh.point(tri[1]) != targetMesh.point(ttri[1])) {
      reorder = true;
      order   = Vec3<int32_t>(0, 2, 1);
    }

    if (reorder) {
      targetMesh.setTriangle(
        tt, ttri[order[0]], ttri[order[1]], ttri[order[2]]);
      targetMesh.setTexCoordTriangle(
        tt, ttriUV[order[0]], ttriUV[order[1]], ttriUV[order[2]]);
    }

    const auto& tri2 = targetMesh.triangle(tt);
    if (sourceMesh.point(tri[0]) != targetMesh.point(tri2[0])
        || sourceMesh.point(tri[1]) != targetMesh.point(tri2[1])
        || sourceMesh.point(tri[2]) != targetMesh.point(tri2[2])) {
      if (reorder) {
        targetMesh.setTriangle(tt, ttri[0], ttri[1], ttri[2]);
        targetMesh.setTexCoordTriangle(tt, ttriUV[0], ttriUV[1], ttriUV[2]);
      }
      mapping.clear();
      return false;
    }
  }

  return true;
}

//----------------------------------------------------------------------------

static Vec3<MeshType>
computeNearestPointColour(
  const Vec3<MeshType>&                      point0,
  const std::vector<Frame<uint8_t>>&         targetTextures,
  const TriangleMesh<MeshType>&              targetMesh,
  const KdTree<MeshType>&                    kdtree,
  const StaticAdjacencyInformation<int32_t>& vertexToTriangleTarget,
  double&                                    minDist2) {
  const auto* const neighboursTarget = vertexToTriangleTarget.neighbours();
  const auto        nnCount          = 1;
  int32_t           index            = 0;
  MeshType          sqrDist          = NAN;
  nanoflann::KNNResultSet<MeshType, int32_t> resultSet(nnCount);
  resultSet.init(&index, &sqrDist);
  kdtree.query(point0.data(), resultSet);
  auto ruv             = targetMesh.texCoord(index);
  minDist2             = std::numeric_limits<MeshType>::max();
  const auto start     = vertexToTriangleTarget.neighboursStartIndex(index);
  const auto end       = vertexToTriangleTarget.neighboursEndIndex(index);
  int        tindexMin = -1;
  for (int j = start; j < end; ++j) {
    const auto tindex = neighboursTarget[j];
    assert(tindex < targetMesh.triangleCount());
    const auto&    tri = targetMesh.triangle(tindex);
    const auto&    pt0 = targetMesh.point(tri[0]);
    const auto&    pt1 = targetMesh.point(tri[1]);
    const auto&    pt2 = targetMesh.point(tri[2]);
    Vec3<MeshType> bcoord{};
    const auto cpoint = ClosestPointInTriangle(point0, pt0, pt1, pt2, &bcoord);
    //    assert(bcoord[0] >= 0.0 && bcoord[1] >= 0.0 && bcoord[2] >= 0.0);
    assert(fabs(1.0 - bcoord[0] - bcoord[1] - bcoord[2]) < 0.000001);
    const auto d2 = (cpoint - point0).norm2();
    if (d2 < minDist2) {
      minDist2          = d2;
      const auto& triUV = targetMesh.texCoordTriangle(tindex);
      const auto& uv0   = targetMesh.texCoord(triUV[0]);
      const auto& uv1   = targetMesh.texCoord(triUV[1]);
      const auto& uv2   = targetMesh.texCoord(triUV[2]);
      ruv               = bcoord[0] * uv0 + bcoord[1] * uv1 + bcoord[2] * uv2;
      tindexMin         = tindex;
    }
  }
  auto& targetTexture = targetTextures[targetMesh.materialIdx(tindexMin)];
  return targetTexture.bilinear(ruv[1], ruv[0]);
}

//----------------------------------------------------------------------------

int
TransferColor::transferColorByMeshes(
  const TriangleMesh<MeshType>&      targetMesh,
  const std::vector<Frame<uint8_t>>& targetTextures,
  const TriangleMesh<MeshType>&      reconstructedMesh,
  Frame<uint8_t>&                    reconstructedTexture,
  Plane<uint8_t>&                    occupancy,
  const std::vector<int32_t>&        srcTri2tgtTri,
  const VMCEncoderParameters&        params) {
  if (srcTri2tgtTri.empty()) {
    printf("transfer texture through 3D\n");
  } else {
    printf("transfer texture between UV\n");
  }
  fflush(stdout);

  StaticAdjacencyInformation<int32_t> vertexToTriangleTarget;
  const bool                           transferThrough3D =
    srcTri2tgtTri.empty();
  std::unique_ptr<KdTree<MeshType>> kdtree;
  if (transferThrough3D) {
    ComputeVertexToTriangle(
      targetMesh.triangles(), targetMesh.pointCount(), vertexToTriangleTarget);
    kdtree = std::make_unique<KdTree<MeshType>>(3, targetMesh.points(), 10);
  }

  const auto       oWidth         = reconstructedTexture.width();
  const auto       oHeight        = reconstructedTexture.height();
  const auto       oWidthMinus1   = oWidth - 1;
  const auto       oHeightMinus1  = oHeight - 1;
  auto&            oB             = reconstructedTexture.plane(0);
  auto&            oG             = reconstructedTexture.plane(1);
  auto&            oR             = reconstructedTexture.plane(2);
  const auto       sTriangleCount = reconstructedMesh.triangleCount();
  Plane<int32_t>   triangleMap;
  if (transferThrough3D) { triangleMap.resize(oWidth, oHeight); }
  occupancy.resize(oWidth, oHeight);
  occupancy.fill(uint8_t(0));

  struct RasterTriangle {
    Vec2<double> uv[3];
    Vec3<double> pos[3];
    double       inverseArea = 1.0;
    int32_t      i0          = 0;
    int32_t      i1          = -1;
    int32_t      j0          = 0;
    int32_t      j1          = -1;
  };

  std::vector<RasterTriangle> rasterTriangles(sTriangleCount);
  for (int32_t t = 0; t < sTriangleCount; ++t) {
    auto&       raster = rasterTriangles[t];
    const auto& triUV  = reconstructedMesh.texCoordTriangle(t);
    raster.uv[0]       = reconstructedMesh.texCoord(triUV[0]);
    raster.uv[1]       = reconstructedMesh.texCoord(triUV[1]);
    raster.uv[2]       = reconstructedMesh.texCoord(triUV[2]);
    const auto& triPos = reconstructedMesh.triangle(t);
    raster.pos[0]      = reconstructedMesh.point(triPos[0]);
    raster.pos[1]      = reconstructedMesh.point(triPos[1]);
    raster.pos[2]      = reconstructedMesh.point(triPos[2]);
    const auto area =
      (raster.uv[1] - raster.uv[0]) ^ (raster.uv[2] - raster.uv[0]);
    if (area <= 0.0) { continue; }
    raster.inverseArea = 1.0 / area;
    raster.i0          = oHeightMinus1;
    raster.i1          = 0;
    raster.j0          = oWidthMinus1;
    raster.j1          = 0;
    for (const auto& uv : raster.uv) {
      auto i    = int32_t(std::ceil(uv[1] * oHeightMinus1));
      auto j    = int32_t(std::ceil(uv[0] * oWidthMinus1));
      raster.i0 = std::min(raster.i0, i);
      raster.j0 = std::min(raster.j0, j);
      i         = int32_t(std::floor(uv[1] * oHeightMinus1));
      j         = int32_t(std::floor(uv[0] * oWidthMinus1));
      raster.i1 = std::max(raster.i1, i);
      raster.j1 = std::max(raster.j1, j);
    }
    raster.i0 = std::max(raster.i0, 0);
    raster.i1 = std::min(raster.i1, oHeightMinus1);
    raster.j0 = std::max(raster.j0, 0);
    raster.j1 = std::min(raster.j1, oWidthMinus1);
  }

  const auto rasterizeTriangleRow = [&](int32_t t, int32_t i) {
    const auto& raster = rasterTriangles[t];
    const auto& uv     = raster.uv;
    const auto& pos    = raster.pos;
    const auto  iarea  = raster.inverseArea;
    const auto  y      = double(i) / oHeightMinus1;
    const auto  ii     = oHeightMinus1 - i;
    for (int32_t j = raster.j0; j <= raster.j1; ++j) {
      const auto         x = double(j) / oWidthMinus1;
      const Vec2<double> uvP(x, y);
      auto               w0 = ((uv[2] - uv[1]) ^ (uvP - uv[1])) * iarea;
      auto               w1 = ((uv[0] - uv[2]) ^ (uvP - uv[2])) * iarea;
      auto               w2 = ((uv[1] - uv[0]) ^ (uvP - uv[0])) * iarea;
      if (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0) {
        Vec3<double> bgr;
        if (transferThrough3D) {
          const auto point0   = w0 * pos[0] + w1 * pos[1] + w2 * pos[2];
          double     minDist2 = NAN;
          bgr                 = computeNearestPointColour(point0,
                                          targetTextures,
                                          targetMesh,
                                          *kdtree,
                                          vertexToTriangleTarget,
                                          minDist2);
        } else {
          const auto& tindex   = srcTri2tgtTri[t];
          const auto& triUVtgt = targetMesh.texCoordTriangle(tindex);
          const auto& uv0      = targetMesh.texCoord(triUVtgt[0]);
          const auto& uv1      = targetMesh.texCoord(triUVtgt[1]);
          const auto& uv2      = targetMesh.texCoord(triUVtgt[2]);
          auto&       targetTexture =
            targetTextures[targetMesh.materialIdx(tindex)];
          const auto ruv = w0 * uv0 + w1 * uv1 + w2 * uv2;
          bgr            = targetTexture.bilinear(ruv[1], ruv[0]);
        }

        oB.set(ii, j, uint8_t(std::round(bgr[0])));
        oG.set(ii, j, uint8_t(std::round(bgr[1])));
        oR.set(ii, j, uint8_t(std::round(bgr[2])));
        occupancy.set(ii, j, uint8_t(255));
        if (transferThrough3D) { triangleMap.set(ii, j, t); }
      }
    }
  };

  int32_t workerCount = params.textureTransferThreadCount == 0
                          ? int32_t(std::thread::hardware_concurrency())
                          : params.textureTransferThreadCount;
  workerCount = std::max(1, std::min(workerCount, oHeight));

  if (workerCount > 1) {
    // Each output row has one writer. Keeping triangle indices in source order
    // preserves the serial last-triangle-wins rule for overlapping UVs.
    std::vector<std::vector<int32_t>> trianglesByRow(oHeight);
    for (int32_t t = 0; t < sTriangleCount; ++t) {
      const auto& raster = rasterTriangles[t];
      for (int32_t i = raster.i0; i <= raster.i1; ++i) {
        trianglesByRow[i].push_back(t);
      }
    }

    std::atomic<int32_t> nextRow(0);
    const auto worker = [&]() {
      for (;;) {
        const auto i = nextRow.fetch_add(1, std::memory_order_relaxed);
        if (i >= oHeight) { break; }
        for (const auto t : trianglesByRow[i]) {
          rasterizeTriangleRow(t, i);
        }
      }
    };

    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    for (int32_t i = 1; i < workerCount; ++i) {
      workers.emplace_back(worker);
    }
    worker();
    for (auto& thread : workers) { thread.join(); }
  } else {
    for (int32_t t = 0; t < sTriangleCount; ++t) {
      const auto& raster = rasterTriangles[t];
      for (int32_t i = raster.i0; i <= raster.i1; ++i) {
        rasterizeTriangleRow(t, i);
      }
    }
  }
  const int32_t shift[4][2] = {{-1, -0}, {0, -1}, {1, 0}, {0, 1}};
  for (int32_t it = 0;
       it < params.textureTransferPaddingBoundaryIterationCount;
       ++it) {
    const auto checkValue = uint8_t(255 - it);
    for (int32_t i = 0; i < oHeight; ++i) {
      for (int32_t j = 0; j < oWidth; ++j) {
        if (occupancy(i, j) != 0U) { continue; }
        double       minTriangleDist2 = std::numeric_limits<double>::max();
        Vec3<double> bgr(0.0);
        int32_t      count = 0;
        for (const auto* k : shift) {
          const auto i1 = i + k[0];
          const auto j1 = j + k[1];
          if (i1 < 0 || j1 < 0 || i1 >= oHeight || j1 >= oWidth
              || occupancy(i1, j1) != checkValue) {
            continue;
          }
          ++count;

          if (!transferThrough3D) {
            bgr +=
              Vec3<double>(oB.get(i1, j1), oG.get(i1, j1), oR.get(i1, j1));
          } else {
            const auto y = double(oHeightMinus1 - i) / oHeightMinus1;
            const auto x = double(j) / oWidthMinus1;

            const Vec2<double> uvP(x, y);

            const auto         t     = triangleMap(i1, j1);
            const auto&        triUV = reconstructedMesh.texCoordTriangle(t);
            const Vec2<double> uv[3] = {reconstructedMesh.texCoord(triUV[0]),
                                        reconstructedMesh.texCoord(triUV[1]),
                                        reconstructedMesh.texCoord(triUV[2])};

            const auto&        triPos = reconstructedMesh.triangle(t);
            const Vec3<double> pos[3] = {reconstructedMesh.point(triPos[0]),
                                         reconstructedMesh.point(triPos[1]),
                                         reconstructedMesh.point(triPos[2])};
            const auto         area   = (uv[1] - uv[0]) ^ (uv[2] - uv[0]);
            assert(area > 0.0);
            const auto iarea    = area > 0.0 ? 1.0 / area : 1.0;
            const auto w0       = ((uv[2] - uv[1]) ^ (uvP - uv[1])) * iarea;
            const auto w1       = ((uv[0] - uv[2]) ^ (uvP - uv[2])) * iarea;
            const auto w2       = ((uv[1] - uv[0]) ^ (uvP - uv[0])) * iarea;
            const auto point0   = w0 * pos[0] + w1 * pos[1] + w2 * pos[2];
            double     minDist2 = NAN;
            bgr += computeNearestPointColour(point0,
                                             targetTextures,
                                             targetMesh,
                                             *kdtree,
                                             vertexToTriangleTarget,
                                             minDist2);
            if (minDist2 < minTriangleDist2) {
              minTriangleDist2 = minDist2;
              triangleMap.set(i, j, t);
            }
          }
        }
        if (count != 0) {
          bgr /= count;
          oB.set(i, j, uint8_t(std::round(bgr[0])));
          oG.set(i, j, uint8_t(std::round(bgr[1])));
          oR.set(i, j, uint8_t(std::round(bgr[2])));
          occupancy.set(i, j, uint8_t(checkValue - 1));
        }
      }
    }
  }
  return 0;
}

//----------------------------------------------------------------------------

void
TransferColor::dilateTexture(Frame<uint8_t>&             reconstructedTexture,
                             Plane<uint8_t>&             occupancy,
                             const VMCEncoderParameters& params) {
  if (params.textureTransferPaddingDilateIterationCount != 0) {
    Frame<uint8_t> tmpTexture;
    Plane<uint8_t> tmpOccupancy;
    for (int32_t it = 0;
         it < params.textureTransferPaddingDilateIterationCount;
         ++it) {
      DilatePadding(reconstructedTexture, occupancy, tmpTexture, tmpOccupancy);
      DilatePadding(tmpTexture, tmpOccupancy, reconstructedTexture, occupancy);
    }
  }
  if (params.textureTransferPaddingMethod == PaddingMethod::PUSH_PULL) {
    PullPushPadding(reconstructedTexture, occupancy);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::SPARSE_LINEAR) {
    PullPushPadding(reconstructedTexture, occupancy);
    SparseLinearPadding(reconstructedTexture,
                        occupancy,
                        params.textureTransferPaddingSparseLinearThreshold);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::SMOOTHED_PUSH_PULL) {
    SmoothedPushPull(reconstructedTexture, occupancy);
  } else if (params.textureTransferPaddingMethod
             == PaddingMethod::HARMONIC_FILL) {
    HarmonicBackgroundFill(reconstructedTexture, occupancy);
  }
}

//----------------------------------------------------------------------------

void
TransferColor::dilateTexture(Frame<uint8_t>& reconstructedTexture,
                             Plane<uint8_t>& occupancy,
                             Frame<uint8_t>& pastReconstructedTexture,
                             const VMCEncoderParameters& params) {
  if (params.textureTransferPaddingDilateIterationCount != 0) {
    Frame<uint8_t> tmpTexture;
    Plane<uint8_t> tmpOccupancy;
    for (int32_t it = 0;
         it < params.textureTransferPaddingDilateIterationCount;
         ++it) {
      DilatePadding(reconstructedTexture, occupancy, tmpTexture, tmpOccupancy);
      DilatePadding(tmpTexture, tmpOccupancy, reconstructedTexture, occupancy);
    }
  }
  //now copy the background from the previous frame
  CopyBackground(reconstructedTexture, occupancy, pastReconstructedTexture);
}

//----------------------------------------------------------------------------

bool
TransferColor::transfer(const TriangleMesh<MeshType>&      sourceMesh,
                        const std::vector<Frame<uint8_t>>& sourceTexture,
                        TriangleMesh<MeshType>&            reconstructedMesh,
                        Frame<uint8_t>&             reconstructedTexture,
                        bool                        usePastTexture,
                        Frame<uint8_t>&             pastReconstructedTexture,
                        const VMCEncoderParameters& params,
                        Plane<uint8_t>&             occupancy,
                        bool                        enablePadding,
                        int32_t                     submeshIdx) {
  auto targetMesh = sourceMesh;
  bool textureTransfer3D =
    (!params.textureTransferBasedPointcloud) || params.textureTransferPreferUV;
  textureTransfer3D |= (params.numSubmesh != 1);

  if (textureTransfer3D) {
    // normalize texcoords so they are between 0.0 and 1.0
    const auto tcCount = targetMesh.texCoordCount();
    const auto uvScale = 1.0 / ((1 << params.bitDepthTexCoord) - 1);
    for (int32_t uvIndex = 0; uvIndex < tcCount; ++uvIndex) {
      targetMesh.setTexCoord(uvIndex, targetMesh.texCoord(uvIndex) * uvScale);
    }
  }
  if (params.invertOrientation) { reconstructedMesh.invertOrientation(); }

  std::vector<int32_t> srcTri2tgtTri;
  if (textureTransfer3D) {
    computeTriangleMapping(targetMesh, reconstructedMesh, srcTri2tgtTri);
    if (srcTri2tgtTri.empty()) {
      std::vector<int32_t> triangleToBaseMeshTriangle;
      int32_t              edgeThreshold                = 0;
      int32_t              bitshiftEdgeBasedSubdivision = 0;
      if (!params.increaseTopSubmeshSubdivisionCount) {
        targetMesh.subdivideMesh(
          params.textureTransferSamplingSubdivisionIterationCount,
          edgeThreshold,
          bitshiftEdgeBasedSubdivision,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          &triangleToBaseMeshTriangle);
      } else {
        targetMesh.subdivideMesh(
          submeshIdx == 0
            ? params.textureTransferSamplingSubdivisionIterationCount
                ? params.textureTransferSamplingSubdivisionIterationCount + 1
                : params.textureTransferSamplingSubdivisionIterationCount
            : params.textureTransferSamplingSubdivisionIterationCount,
          edgeThreshold,
          bitshiftEdgeBasedSubdivision,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          &triangleToBaseMeshTriangle);
      }
      //adjust the materialIdx
      auto oldMaterialIdxList = targetMesh.materialIdxs();
      targetMesh.materialIdxs().clear();
      targetMesh.materialIdxs().resize(targetMesh.triangleCount());
      for (int t = 0; t < targetMesh.triangleCount(); t++)
        targetMesh.materialIdxs()[t] =
          oldMaterialIdxList[triangleToBaseMeshTriangle[t]];
    }
  }
  if ((targetMesh.pointCount() == 0) || (reconstructedMesh.pointCount() == 0)
      || reconstructedTexture.width() <= 0
      || reconstructedTexture.height() <= 0) {
    return false;
  }

  const bool usePointclouds = !textureTransfer3D;
  //    params.textureTransferBasedPointcloud
  //    && (!(params.textureTransferPreferUV && (!srcTri2tgtTri.empty())));
  if (usePointclouds) {
    //numSubmesh==1
    transferColorByPointclouds(targetMesh,
                               sourceTexture,
                               reconstructedMesh,
                               reconstructedTexture,
                               occupancy,
                               params);
  } else {
    //numSubmesh>1
    transferColorByMeshes(targetMesh,
                          sourceTexture,
                          reconstructedMesh,
                          reconstructedTexture,
                          occupancy,
                          srcTri2tgtTri,
                          params);
  }
  if (usePastTexture) {
    dilateTexture(
      reconstructedTexture, occupancy, pastReconstructedTexture, params);
  } else if (enablePadding) {
    dilateTexture(reconstructedTexture, occupancy, params);
  }

  if (params.invertOrientation) { reconstructedMesh.invertOrientation(); }

  return true;
}

//============================================================================

}  // namespace vmesh
