

#pragma once
#include <chrono>
#include <program-options-lite/program_options_lite.h>
#include <iostream>
#include <fstream>
#include <queue>
#include <memory>
#include <numeric>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <iostream>
#include <fstream>
#include "encoder.hpp"
#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "util/mesh.hpp"
#include "util/matrix.hpp"
#include "util/mesh.hpp"
#include "util/mutablepriorityheap.hpp"
#include "util/triangle.hpp"
#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <Eigen/StdVector>
#include <Eigen/Geometry>
#include <cmath>
#include <memory>
#include <tuple>
#include <vector>
#include <util/kdtree.hpp>
#include "geometryDecimate.hpp"
#include "triangleMeshDecimator.hpp"

#include <optional>
#include <random>
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Eigen {
typedef Eigen::Matrix<double, 6, 6, Eigen::DontAlign> Matrix6d_u;
typedef Eigen::Matrix<double, 4, 4, Eigen::DontAlign> Matrix4d_u;

}  // namespace Eigen

typedef std::vector<Eigen::Vector2i> CorSet;

void TransformVertices(const Eigen::Matrix4d&         transformation,
                       vmesh::TriangleMesh<MeshType>& mesh);

class RegResult {
public:
  RegResult(
    const Eigen::Matrix4d& transformation = Eigen::Matrix4d::Identity())
    : transformation_(transformation), inlier_rmse_(0.0), fitness_(0.0) {}
  ~RegResult() {}

public:
  Eigen::Matrix4d_u transformation_;
  CorSet            cor_set_;
  CorSet            cor_set_ori_;
  double            inlier_rmse_;
  double            fitness_;
};

static RegResult computeRegResult(const vmesh::TriangleMesh<MeshType>& source,
                                  const vmesh::KdTree<MeshType>& target_kdtree,
                                  double                 max_cor_distance,
                                  const Eigen::Matrix4d& transformation);

double          ComputeRMSE(const vmesh::TriangleMesh<MeshType>& source,
                            const vmesh::TriangleMesh<MeshType>& target,
                            const CorSet&                        corres);
Eigen::Matrix4d ComputeTrans(const vmesh::TriangleMesh<MeshType>& source,
                             const vmesh::TriangleMesh<MeshType>& target,
                             const CorSet&                        corres);

RegResult
Registration(const vmesh::TriangleMesh<MeshType>& source,
             const vmesh::TriangleMesh<MeshType>& target,
             double                               max_cor_distance,
             const Eigen::Matrix4d& init = Eigen::Matrix4d::Identity());

class DisjointSet {
public:
  explicit DisjointSet(int size = 0)
    : size_(size), numbers_(size, 0), subsets_(size) {
    assert(size_ >= 0);

    for (int i = 0; i < size_; ++i) { subsets_[i] = i; }
  }

public:
  void Reset(int size) {
    assert(size >= 0);
    size_ = size;
    numbers_.assign(size_, 0);
    subsets_.resize(size_);
    for (int i = 0; i < size_; ++i) { subsets_[i] = i; }
  }

  int Find(int i) const {
    assert(i >= 0 && i < size_);

    while (i != subsets_[i]) {
      subsets_[i] = subsets_[subsets_[i]];
      i           = subsets_[i];
    }
    return i;
  }

  int Link(int i, int j) {
    assert(subsets_[i] == i);
    assert(subsets_[j] == j);
    assert(i != j);

    numbers_[j] += numbers_[i] + 1;
    subsets_[i] = j;
    return j;
  }

  int Union(int i, int j) {
    int a = Find(i);
    int b = Find(j);
    if (a != b) {
      if (numbers_[a] > numbers_[b]) {
        numbers_[b] += numbers_[a] + 1;
        subsets_[a] = b;
        return b;
      } else {
        numbers_[a] += numbers_[b] + 1;
        subsets_[b] = a;
        return a;
      }
    }

    return a;
  }

  void ToClusters(std::vector<std::vector<int>>* clusters) const {
    assert(clusters);
    clusters->clear();

    int              n_clusters = 0;
    std::vector<int> map(size_, -1);
    for (int i = 0; i < size_; ++i) {
      int p = Find(i);
      if (map[p] == -1) map[p] = n_clusters++;
    }

    clusters->resize(n_clusters);
    for (int i = 0; i < size_; ++i) {
      int t = map[Find(i)];
      (*clusters)[t].push_back(i);
    }
  }

  int Number(int i) const {
    assert(i >= 0 && i < size_);

    return numbers_[Find(i)] + 1;
  }

  int  size() const { return size_; }
  bool empty() const { return size_ == 0; }

  int                      size_;
  std::vector<int>         numbers_;
  mutable std::vector<int> subsets_;
};

class SupervoxelSeg {
public:
  SupervoxelSeg() {}

  ~SupervoxelSeg() {}

public:
  void setCloud(const std::vector<vmesh::Vec3<MeshType>> points,
                const std::vector<vmesh::Vec3<MeshType>> colors) {
    points_   = points;
    colors_   = colors;
    n_points_ = static_cast<int>(points_.size());

    set_.Reset(n_points_);
    supervoxels_.resize(n_points_);
    for (int i = 0; i < n_points_; ++i) { supervoxels_[i] = i; }

    sizes1_.resize(n_points_, 1);
    sizes2_.resize(n_points_, 1);
    number_of_supervoxels_ = n_points_;

    metricMode = 1;
  }

  void FindNeighbor(int nnCount) {
    vmesh::KdTree<MeshType> kdtreeT(3, points_, 10);

    adjacents_.resize(n_points_);
    for (int i = 0; i < n_points_; ++i) {
      auto point = points_[i];

      std::vector<int32_t>  indexes(nnCount);
      std::vector<MeshType> sqrDists(nnCount);

      kdtreeT.query(point.data(), nnCount, indexes.data(), sqrDists.data());

      adjacents_[i] = indexes;
    }

    neighbors_ = adjacents_;

    std::vector<double> dis(n_points_, DBL_MAX);
    for (int i = 0; i < n_points_; ++i) {
      for (int j : adjacents_[i]) {
        if (i != j) { dis[i] = std::min(dis[i], metric(i, j)); }
      }
    }
    lambda_ = std::max(DBL_EPSILON, Median(dis.begin(), dis.end())) / 2.;
  }

  void setMetricMode(int i) { metricMode = i; }

  void setMatchedLabel(std::vector<int> matchedLabels,
                       std::vector<int> matchedIndexs) {
    std::vector<vmesh::Vec3<MeshType>> subPoints;
    subPoints.resize(matchedIndexs.size());
    for (int i = 0; i < matchedIndexs.size(); ++i) {
      subPoints[i] = points_[matchedIndexs[i]];
    }
    labels_.resize(n_points_);
    vmesh::KdTree<MeshType> Kdtree_sub(3, subPoints, 10);
    for (int32_t idx = 0; idx < points_.size(); ++idx) {
      auto                  pointA  = points_[idx];
      int                   nnCount = 1;
      std::vector<int32_t>  indexes(nnCount);
      std::vector<MeshType> sqrDists(nnCount);
      Kdtree_sub.query(
        pointA.data(), nnCount, indexes.data(), sqrDists.data());
      labels_[idx] = matchedLabels[indexes[0]];
    }
  }

  double metric(int i, int j) {
    double dis1, dis2 = 0;

    if (metricMode == 0) {
      vmesh::Vec3<MeshType> colori = colors_[i];
      vmesh::Vec3<MeshType> colorj = colors_[j];

      dis2 = std::abs((colori[0] - colorj[0]))
             + std::abs((colori[1] - colorj[1]))
             + std::abs((colori[2] - colorj[2]));
      return dis2;
    } else {
      vmesh::Vec3<MeshType> pointi = points_[i];
      vmesh::Vec3<MeshType> pointj = points_[j];
      dis1 = (pointi[0] - pointj[0]) * (pointi[0] - pointj[0])
             + (pointi[1] - pointj[1]) * (pointi[1] - pointj[1])
             + (pointi[2] - pointj[2]) * (pointi[2] - pointj[2]);
      return dis1;
    }
  }

  template<typename Iterator>
  const typename std::iterator_traits<Iterator>::value_type
  Median(Iterator first, Iterator last) {
    assert(first != last);

    typedef typename std::iterator_traits<Iterator>::value_type T;

    std::vector<T> values(first, last);
    std::nth_element(
      values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
  }

  void set_n_sup(int n_supervoxels) { n_supervoxels_ = n_supervoxels; }

  std::vector<int> getLabels() { return labels_; }

  void Find_superpixels() {
    std::vector<int>  queue1(n_points_);
    std::vector<bool> visited(n_points_, false);
    for (;; lambda_ *= 1.2) {
      if (supervoxels_.size() <= 1) break;

      for (int i : supervoxels_) {
        if (adjacents_[i].empty()) continue;

        visited[i] = true;
        int front = 0, back = 1;
        queue1[front++] = i;
        for (int j : adjacents_[i]) {
          j = set_.Find(j);
          if (!visited[j]) {
            visited[j]     = true;
            queue1[back++] = j;
          }
        }

        std::vector<int> adjacent;
        while (front < back) {
          int j = queue1[front++];

          double loss        = sizes1_[j] * metric(i, j);
          double improvement = lambda_ - loss;
          if (improvement > 0.0) {
            set_.Link(j, i);

            sizes1_[i] += sizes1_[j];

            for (int k : adjacents_[j]) {
              k = set_.Find(k);
              if (!visited[k]) {
                visited[k]     = true;
                queue1[back++] = k;
              }
            }

            adjacents_[j].clear();
            if (--number_of_supervoxels_ == n_supervoxels_) break;
          } else {
            adjacent.push_back(j);
          }
        }
        adjacents_[i].swap(adjacent);

        for (int j = 0; j < back; ++j) { visited[queue1[j]] = false; }
        if (number_of_supervoxels_ == n_supervoxels_) break;
      }

      number_of_supervoxels_ = 0;
      for (int i : supervoxels_) {
        if (set_.Find(i) == i) { supervoxels_[number_of_supervoxels_++] = i; }
      }
      supervoxels_.resize(number_of_supervoxels_);

      if (number_of_supervoxels_ == n_supervoxels_) break;
    }
    labels_.resize(n_points_);
    for (int i = 0; i < n_points_; ++i) { labels_[i] = set_.Find(i); }
  }

  void Relabel() {
    std::vector<int> map(n_points_);
    for (int i = 0; i < supervoxels_.size(); ++i) { map[supervoxels_[i]] = i; }
    for (int i = 0; i < n_points_; ++i) { labels_[i] = map[labels_[i]]; }
  }

  int n_supervoxels_;
  int number_of_supervoxels_;
  int n_points_;

  std::vector<int> sizes1_;
  std::vector<int> sizes2_;

  std::vector<int>                   labels_;
  DisjointSet                        set_;
  std::vector<int>                   supervoxels_;
  std::vector<int>                   runOrder;
  std::vector<std::vector<int32_t>>  adjacents_;
  std::vector<std::vector<int32_t>>  neighbors_;
  std::vector<vmesh::Vec3<MeshType>> points_;
  std::vector<vmesh::Vec3<MeshType>> colors_;
  double                             lambda_;

  int metricMode;
};

class Mesh2PC {
public:
  Mesh2PC(vmesh::TriangleMesh<MeshType>& mesh,
          vmesh::Frame<uint8_t>&         texture) {
    mesh_    = mesh;
    texture_ = texture;
  }

  ~Mesh2PC() {}

public:
  void AdaptiveSample(double uvScale) {
    samples_.resize(mesh_.triangleCount());
    colors_.resize(mesh_.triangleCount());
    for (int i = 0; i < mesh_.triangleCount(); ++i) {
      vmesh::Vec3<int> Tri   = mesh_.triangle(i);
      vmesh::Vec3<int> TriUV = mesh_.texCoordTriangle(i);

      vmesh::Vec3<double> tri_origin  = mesh_.point(Tri[0]);
      vmesh::Vec3<double> tri_vector1 = mesh_.point(Tri[1]);
      vmesh::Vec3<double> tri_vector2 = mesh_.point(Tri[2]);

      vmesh::Vec2<double> uv_origin  = mesh_.texCoord(TriUV[0]);
      vmesh::Vec2<double> uv_vector1 = mesh_.texCoord(TriUV[1]);
      vmesh::Vec2<double> uv_vector2 = mesh_.texCoord(TriUV[2]);

      samples_[i] = (tri_origin + tri_vector1 + tri_vector2);
      samples_[i] /= 3;

      vmesh::Vec2<double> ruv = (uv_origin + uv_vector1 + uv_vector2) / 3;
      colors_[i] = texture_.fetch(ruv[1] * uvScale, ruv[0] * uvScale);
    }
  }

  void UniformSample(double uvScale, int SampleCount) {
    samples_.resize(SampleCount);
    colors_.resize(SampleCount);

    std::vector<double> face_weight;
    mesh_.computeTriangleAreas(face_weight);

    std::vector<double> weight_cum(face_weight.size());
    std::partial_sum(
      face_weight.begin(), face_weight.end(), weight_cum.begin());

    std::mt19937 rng;
    rng.seed(10);

    std::uniform_real_distribution<double> dist(0.0, weight_cum.back());
    std::vector<int>                       face_index(SampleCount);
    for (int i = 0; i < SampleCount; ++i) {
      double pick = dist(rng);
      face_index[i] =
        std::lower_bound(weight_cum.begin(), weight_cum.end(), pick)
        - weight_cum.begin();
    }

    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    for (int i = 0; i < SampleCount; ++i) {
      vmesh::Vec3<int> Tri   = mesh_.triangle(face_index[i]);
      vmesh::Vec3<int> TriUV = mesh_.texCoordTriangle(face_index[i]);

      vmesh::Vec3<double> tri_origin  = mesh_.point(Tri[0]);
      vmesh::Vec3<double> tri_vector1 = mesh_.point(Tri[1]) - tri_origin;
      vmesh::Vec3<double> tri_vector2 = mesh_.point(Tri[2]) - tri_origin;

      vmesh::Vec2<double> uv_origin  = mesh_.texCoord(TriUV[0]);
      vmesh::Vec2<double> uv_vector1 = mesh_.texCoord(TriUV[1]) - uv_origin;
      vmesh::Vec2<double> uv_vector2 = mesh_.texCoord(TriUV[2]) - uv_origin;

      double sampleLenthX = dist01(rng);
      double sampleLenthY = dist01(rng);

      if (sampleLenthX + sampleLenthY > 1.0) {
        sampleLenthX = std::abs(1 - sampleLenthX);
        sampleLenthY = std::abs(1 - sampleLenthY);
      }

      samples_[i] =
        tri_origin + tri_vector1 * sampleLenthX + tri_vector2 * sampleLenthY;

      vmesh::Vec2<double> ruv =
        uv_origin + uv_vector1 * sampleLenthX + uv_vector2 * sampleLenthY;
      colors_[i] = texture_.fetch(ruv[1] * uvScale, ruv[0] * uvScale);
    }
  }

  vmesh::TriangleMesh<MeshType> mesh_;
  vmesh::Frame<uint8_t>         texture_;

  std::vector<vmesh::Vec3<double>> samples_;
  std::vector<vmesh::Vec3<double>> colors_;
};

/// \class Temporal_consist
///
/// Class that contains the Temporal_consist results.
class Fast_Temporal_consist {
public:
  Fast_Temporal_consist(std::vector<vmesh::Vec3<MeshType>>& ref_points,
                        std::vector<vmesh::Vec3<MeshType>>& cur_points,
                        std::vector<int>                    labels,
                        int                                 n_supervoxels) {
    ref_points_    = ref_points;
    cur_points_    = cur_points;
    labels_        = labels;
    n_supervoxels_ = n_supervoxels;

    cur_voxel_.resize(n_supervoxels_);
    ref_voxel_.resize(n_supervoxels_);

    TransMatices.resize(n_supervoxels_);
  }
  ~Fast_Temporal_consist() {}
  void matchcor(int adaptive) {
    for (int i = 0; i < ref_points_.size(); ++i) {
      ref_voxel_[labels_[i]].push_back(i);
    }
    for (int i = ref_points_.size(); i < labels_.size(); ++i) {
      cur_voxel_[labels_[i]].push_back(i - ref_points_.size());
    }

    for (int i = 0; i < n_supervoxels_; ++i) {
      if ((cur_voxel_[i].size() == 0) || (ref_voxel_[i].size() == 0)) {
        continue;
      }

      vmesh::TriangleMesh<MeshType> cur_subMesh, ref_subMesh;
      for (int j : cur_voxel_[i]) { cur_subMesh.addPoint(cur_points_[j]); }
      for (int j : ref_voxel_[i]) { ref_subMesh.addPoint(ref_points_[j]); }

      Eigen::Matrix4d init_trans;
      init_trans << 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0.0, 0.0,
        0.0, 1.0;

      RegResult    reg_result(init_trans);
      const double max_cor_distance = 500;
      reg_result =
        Registration(ref_subMesh, cur_subMesh, max_cor_distance, init_trans);

      double dis1 =
        reg_result.transformation_(0, 3) * reg_result.transformation_(0, 3);
      dis1 +=
        reg_result.transformation_(1, 3) * reg_result.transformation_(1, 3);
      dis1 +=
        reg_result.transformation_(2, 3) * reg_result.transformation_(2, 3);
      dis1 = sqrt(dis1);

      TransMatices[i] = reg_result.transformation_;

      TransformVertices(reg_result.transformation_, ref_subMesh);
      vmesh::KdTree<MeshType> Kdtree_sub_cur(3, cur_subMesh.points(), 10);
      for (int32_t idx = 0; idx < ref_subMesh.pointCount(); ++idx) {
        auto                  pointA  = ref_subMesh.point(idx);
        int                   nnCount = 1;
        std::vector<int32_t>  indexes(nnCount);
        std::vector<MeshType> sqrDists(nnCount);
        Kdtree_sub_cur.query(
          pointA.data(), nnCount, indexes.data(), sqrDists.data());
        if (adaptive) {
          if (sqrDists[0] < 20 * dis1 / 400) {
            cor_set_.push_back(
              Eigen::Vector2i(ref_voxel_[i][idx], cur_voxel_[i][indexes[0]]));
          }
        } else {
          if (sqrDists[0] < 20) {
            cor_set_.push_back(
              Eigen::Vector2i(ref_voxel_[i][idx], cur_voxel_[i][indexes[0]]));
          }
        }
      }
    }
  }

public:
  std::vector<vmesh::Vec3<MeshType>> ref_points_;
  std::vector<vmesh::Vec3<MeshType>> cur_points_;
  std::vector<int>                   labels_;
  int                                n_supervoxels_;

  std::vector<std::vector<int>> cur_voxel_;
  std::vector<std::vector<int>> ref_voxel_;

  CorSet            cor_key_set_;
  CorSet            cor_set_;
  std::vector<bool> refTags_;
  std::vector<bool> curTags_;

  std::vector<Eigen::Matrix4d_u> TransMatices;
};

void FastInterBaseGeneration_hard_deform(
  vmesh::TriangleMesh<MeshType>& frame_reference,
  vmesh::TriangleMesh<MeshType>& ref_reference,
  vmesh::TriangleMesh<MeshType>& frame_base,
  vmesh::TriangleMesh<MeshType>& ref_base,
  vmesh::Frame<uint8_t>&         curTexture,
  vmesh::Frame<uint8_t>&         refTexture,
  double                         uvScale);

void FastInterBaseGeneration_1(vmesh::TriangleMesh<MeshType>& frame_reference,
                               vmesh::TriangleMesh<MeshType>& ref_reference,
                               vmesh::TriangleMesh<MeshType>& frame_base,
                               vmesh::TriangleMesh<MeshType>& ref_base,
                               vmesh::Frame<uint8_t>&         curTexture,
                               vmesh::Frame<uint8_t>&         refTexture,
                               double                         uvScale);

void FastInterBaseGeneration_1(vmesh::TriangleMesh<MeshType>& frame_reference,
                               vmesh::TriangleMesh<MeshType>& ref_reference,
                               vmesh::TriangleMesh<MeshType>& frame_base,
                               vmesh::TriangleMesh<MeshType>& ref_base);
