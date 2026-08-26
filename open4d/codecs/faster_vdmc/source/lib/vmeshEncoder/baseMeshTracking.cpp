

#pragma once
#include "baseMeshTracking.hpp"
#include <iomanip>

// Function to round a value to 5 decimal places
double
roundTo5Decimals(double value) {
  return std::round(value * 1e5) / 1e5;
}

namespace Eigen {
typedef Eigen::Matrix<double, 6, 6, Eigen::DontAlign> Matrix6d_u;
typedef Eigen::Matrix<double, 4, 4, Eigen::DontAlign> Matrix4d_u;
}  // namespace Eigen

typedef std::vector<Eigen::Vector2i> CorSet;

void
TransformVertices(const Eigen::Matrix4d&         transformation,
                  vmesh::TriangleMesh<MeshType>& mesh) {
  for (int32_t v = 0, vcount = mesh.pointCount(); v < vcount; ++v) {
    auto&           point0 = mesh.point(v);
    Eigen::Vector4d new_point =
      transformation * Eigen::Vector4d(point0[0], point0[1], point0[2], 1.0);
    auto point = new_point.head<3>() / new_point(3);
    point0[0]  = point(0);
    point0[1]  = point(1);
    point0[2]  = point(2);
  }
}

static RegResult
computeRegResult(const vmesh::TriangleMesh<MeshType>& source,
                 const vmesh::KdTree<MeshType>&       target_kdtree,
                 double                               max_cor_distance,
                 const Eigen::Matrix4d&               transformation) {
  RegResult result(transformation);
  if (max_cor_distance <= 0.0) { return result; }
  double error2         = 0.0;
  double error2_private = 0.0;
  CorSet cor_set_private;

  for (int i = 0; i < (int)source.pointCount(); i++) {
    const auto&           point   = source.point(i);
    int                   nnCount = 1;
    std::vector<int32_t>  indices(nnCount);
    std::vector<MeshType> dists(nnCount);
    target_kdtree.query(point.data(), nnCount, indices.data(), dists.data());

    if (std::sqrt(dists[0]) < max_cor_distance) {
      error2_private += dists[0];
      cor_set_private.push_back(Eigen::Vector2i(i, indices[0]));
    }
  }
  for (int i = 0; i < (int)cor_set_private.size(); i++) {
    result.cor_set_.push_back(cor_set_private[i]);
  }
  error2 += error2_private;

  if (result.cor_set_.empty()) {
    result.fitness_     = 0.0;
    result.inlier_rmse_ = 0.0;
  } else {
    size_t corres_number = result.cor_set_.size();
    result.fitness_      = (double)corres_number / (double)source.pointCount();
    result.inlier_rmse_  = std::sqrt(error2 / (double)corres_number);
  }
  return result;
}

double
ComputeRMSE(const vmesh::TriangleMesh<MeshType>& source,
            const vmesh::TriangleMesh<MeshType>& target,
            const CorSet&                        corres) {
  if (corres.empty()) return 0.0;
  double err = 0.0;
  for (const auto& c : corres) {
    auto            ver_source = source.point(c[0]);
    auto            ver_target = target.point(c[1]);
    Eigen::Vector3d point_source(ver_source[0], ver_source[1], ver_source[2]);
    Eigen::Vector3d point_target(ver_target[0], ver_target[1], ver_target[2]);
    err += (point_source - point_target).squaredNorm();
  }
  return std::sqrt(err / (double)corres.size());
}
Eigen::Matrix4d
ComputeTrans(const vmesh::TriangleMesh<MeshType>& source,
             const vmesh::TriangleMesh<MeshType>& target,
             const CorSet&                        corres) {
  if (corres.empty()) return Eigen::Matrix4d::Identity();
  Eigen::MatrixXd source_mat(3, corres.size());
  Eigen::MatrixXd target_mat(3, corres.size());
  for (size_t i = 0; i < corres.size(); i++) {
    auto            ver_source = source.point(corres[i][0]);
    auto            ver_target = target.point(corres[i][1]);
    Eigen::Vector3d point_source(ver_source[0], ver_source[1], ver_source[2]);
    Eigen::Vector3d point_target(ver_target[0], ver_target[1], ver_target[2]);
    source_mat.block<3, 1>(0, i) = point_source;
    target_mat.block<3, 1>(0, i) = point_target;
  }
  return Eigen::umeyama(source_mat, target_mat, 0);
}

RegResult
Registration(const vmesh::TriangleMesh<MeshType>& source,
             const vmesh::TriangleMesh<MeshType>& target,
             double                               max_cor_distance,
             const Eigen::Matrix4d&               init) {
  Eigen::Matrix4d               transformation = init;
  vmesh::KdTree<MeshType>       kdtree(3, target.points(), 10);
  vmesh::TriangleMesh<MeshType> pcd;
  pcd.convert1(source);
  if (!init.isIdentity()) { TransformVertices(init, pcd); }
  RegResult result;
  result = computeRegResult(pcd, kdtree, max_cor_distance, transformation);
  for (int i = 0; i < 50; i++) {
    Eigen::Matrix4d update = ComputeTrans(pcd, target, result.cor_set_);
    transformation         = update * transformation;
    TransformVertices(update, pcd);
    RegResult backup = result;
    result = computeRegResult(pcd, kdtree, max_cor_distance, transformation);
    if (std::abs(backup.fitness_ - result.fitness_) < 1e-6
        && std::abs(backup.inlier_rmse_ - result.inlier_rmse_) < 1e-6) {
      break;
    }
  }
  return result;
}

void
FastInterBaseGeneration_1(vmesh::TriangleMesh<MeshType>& frame_reference,
                          vmesh::TriangleMesh<MeshType>& ref_reference,
                          vmesh::TriangleMesh<MeshType>& frame_base,
                          vmesh::TriangleMesh<MeshType>& ref_base,
                          vmesh::Frame<uint8_t>&         curTexture,
                          vmesh::Frame<uint8_t>&         refTexture,
                          double                         uvScale) {
  std::cout << "Step1: Sample mesh to point cloud: " << std::endl;

  Mesh2PC Mesh2Point1(ref_reference, refTexture);
  Mesh2Point1.AdaptiveSample(uvScale);
  auto samples1 = Mesh2Point1.samples_;
  auto colors1  = Mesh2Point1.colors_;

  Mesh2PC Mesh2Point2(frame_reference, curTexture);
  Mesh2Point2.AdaptiveSample(uvScale);
  auto samples2 = Mesh2Point2.samples_;
  auto colors2  = Mesh2Point2.colors_;

  std::vector<vmesh::Vec3<double>> samples, colors;

  samples.reserve(samples1.size() + samples2.size());
  samples.insert(samples.end(), samples1.begin(), samples1.end());
  samples.insert(samples.end(), samples2.begin(), samples2.end());

  colors.reserve(samples1.size() + samples2.size());
  colors.insert(colors.end(), colors1.begin(), colors1.end());
  colors.insert(colors.end(), colors2.begin(), colors2.end());

  std::cout << "Step2: corse segmentation: " << std::endl;

  SupervoxelSeg SupSeg1;
  SupSeg1.setCloud(samples1, colors1);
  SupSeg1.FindNeighbor(20);
  SupSeg1.setMetricMode(1);
  SupSeg1.set_n_sup(100);
  SupSeg1.Find_superpixels();
  SupSeg1.Relabel();

  std::vector<int> matchedLabels, matchedIndexs;
  for (int i = 0; i < samples1.size(); ++i) {
    matchedLabels.push_back(SupSeg1.labels_[i]);
    matchedIndexs.push_back(i);
  }

  std::cout << "Step3: refined segmentation: " << std::endl;

  SupervoxelSeg SupSeg2;
  SupSeg2.setCloud(samples2, colors2);
  SupSeg2.FindNeighbor(20);
  SupSeg2.setMetricMode(1);
  SupSeg2.setMatchedLabel(matchedLabels, matchedIndexs);

  std::vector<int> labels;
  labels.reserve(SupSeg1.labels_.size() + SupSeg2.labels_.size());
  labels.insert(labels.end(), SupSeg1.labels_.begin(), SupSeg1.labels_.end());
  labels.insert(labels.end(), SupSeg2.labels_.begin(), SupSeg2.labels_.end());

  Fast_Temporal_consist FTCon(
    samples1, samples2, labels, SupSeg1.n_supervoxels_);
  FTCon.matchcor(0);

  for (int i = 0; i < FTCon.TransMatices.size(); ++i) {
    auto& Trans = FTCon.TransMatices[i];
    for (int i = 0; i < Trans.rows(); ++i) {
      for (int j = 0; j < Trans.cols(); ++j) {
        Trans(i, j) = roundTo5Decimals(Trans(i, j));
      }
    }
  }

  std::cout << "Step4: transform: " << std::endl;

  std::vector<int>                   subLabels;
  std::vector<vmesh::Vec3<MeshType>> subPoints;
  subLabels.resize(FTCon.cor_set_.size());
  subPoints.resize(FTCon.cor_set_.size());

  for (int i = 0; i < FTCon.cor_set_.size(); ++i) {
    subPoints[i] = samples1[FTCon.cor_set_[i](0)];
    subLabels[i] = SupSeg1.labels_[FTCon.cor_set_[i](0)];
  }

  vmesh::KdTree<MeshType> Kdtree_sub(3, subPoints, 10);
  frame_base.convert(ref_base);

  for (int32_t idx = 0; idx < frame_base.pointCount(); ++idx) {
    auto&                 pointA  = frame_base.point(idx);
    int                   nnCount = 1;
    std::vector<int32_t>  indexes(nnCount);
    std::vector<MeshType> sqrDists(nnCount);
    Kdtree_sub.query(pointA.data(), nnCount, indexes.data(), sqrDists.data());
    auto Trans1 = FTCon.TransMatices[subLabels[indexes[0]]];

    Eigen::Vector4d new_point =
      Trans1 * Eigen::Vector4d(pointA[0], pointA[1], pointA[2], 1.0);
    auto pointB = new_point.head<3>() / new_point(3);

    pointA[0] = pointB(0);
    pointA[1] = pointB(1);
    pointA[2] = pointB(2);
  }

  std::cout << "Finish. " << std::endl;
}

void
FastInterBaseGeneration_1(vmesh::TriangleMesh<MeshType>& frame_reference,
                          vmesh::TriangleMesh<MeshType>& ref_reference,
                          vmesh::TriangleMesh<MeshType>& frame_base,
                          vmesh::TriangleMesh<MeshType>& ref_base) {
  std::cout << "Step1: Sample mesh to point cloud: " << std::endl;

  auto                             samples1 = ref_reference.points();
  std::vector<vmesh::Vec3<double>> colors1;

  auto                             samples2 = frame_reference.points();
  std::vector<vmesh::Vec3<double>> colors2;

  std::vector<vmesh::Vec3<double>> samples, colors;

  samples.reserve(samples1.size() + samples2.size());
  samples.insert(samples.end(), samples1.begin(), samples1.end());
  samples.insert(samples.end(), samples2.begin(), samples2.end());

  colors.reserve(samples1.size() + samples2.size());
  colors.insert(colors.end(), colors1.begin(), colors1.end());
  colors.insert(colors.end(), colors2.begin(), colors2.end());

  std::cout << "Step2: corse segmentation: " << std::endl;

  SupervoxelSeg SupSeg1;
  SupSeg1.setCloud(samples1, colors1);
  SupSeg1.FindNeighbor(20);
  SupSeg1.setMetricMode(1);
  SupSeg1.set_n_sup(100);
  SupSeg1.Find_superpixels();
  SupSeg1.Relabel();

  std::vector<int> matchedLabels, matchedIndexs;
  for (int i = 0; i < samples1.size(); ++i) {
    matchedLabels.push_back(SupSeg1.labels_[i]);
    matchedIndexs.push_back(i);
  }

  std::cout << "Step3: refined segmentation: " << std::endl;

  SupervoxelSeg SupSeg2;
  SupSeg2.setCloud(samples2, colors2);
  SupSeg2.FindNeighbor(20);
  SupSeg2.setMetricMode(1);
  SupSeg2.setMatchedLabel(matchedLabels, matchedIndexs);

  std::vector<int> labels;
  labels.reserve(SupSeg1.labels_.size() + SupSeg2.labels_.size());
  labels.insert(labels.end(), SupSeg1.labels_.begin(), SupSeg1.labels_.end());
  labels.insert(labels.end(), SupSeg2.labels_.begin(), SupSeg2.labels_.end());

  Fast_Temporal_consist FTCon(
    samples1, samples2, labels, SupSeg1.n_supervoxels_);
  FTCon.matchcor(0);

  for (int i = 0; i < FTCon.TransMatices.size(); ++i) {
    auto& Trans = FTCon.TransMatices[i];
    for (int i = 0; i < Trans.rows(); ++i) {
      for (int j = 0; j < Trans.cols(); ++j) {
        Trans(i, j) = roundTo5Decimals(Trans(i, j));
      }
    }
  }

  std::cout << "Step4: transform: " << std::endl;

  std::vector<int>                   subLabels;
  std::vector<vmesh::Vec3<MeshType>> subPoints;
  subLabels.resize(FTCon.cor_set_.size());
  subPoints.resize(FTCon.cor_set_.size());

  for (int i = 0; i < FTCon.cor_set_.size(); ++i) {
    subPoints[i] = samples1[FTCon.cor_set_[i](0)];
    subLabels[i] = SupSeg1.labels_[FTCon.cor_set_[i](0)];
  }

  vmesh::KdTree<MeshType> Kdtree_sub(3, subPoints, 10);
  frame_base.convert(ref_base);

  for (int32_t idx = 0; idx < frame_base.pointCount(); ++idx) {
    auto&                 pointA  = frame_base.point(idx);
    int                   nnCount = 1;
    std::vector<int32_t>  indexes(nnCount);
    std::vector<MeshType> sqrDists(nnCount);
    Kdtree_sub.query(pointA.data(), nnCount, indexes.data(), sqrDists.data());
    auto Trans1 = FTCon.TransMatices[subLabels[indexes[0]]];

    Eigen::Vector4d new_point =
      Trans1 * Eigen::Vector4d(pointA[0], pointA[1], pointA[2], 1.0);
    auto pointB = new_point.head<3>() / new_point(3);

    pointA[0] = pointB(0);
    pointA[1] = pointB(1);
    pointA[2] = pointB(2);
  }

  std::cout << "Finish. " << std::endl;
}

void
FastInterBaseGeneration_hard_deform(
  vmesh::TriangleMesh<MeshType>& frame_reference,
  vmesh::TriangleMesh<MeshType>& ref_reference,
  vmesh::TriangleMesh<MeshType>& frame_base,
  vmesh::TriangleMesh<MeshType>& ref_base,
  vmesh::Frame<uint8_t>&         curTexture,
  vmesh::Frame<uint8_t>&         refTexture,
  double                         uvScale) {
  std::cout << "Step1: Sample mesh to point cloud: " << std::endl;

  Mesh2PC Mesh2Point1(ref_reference, refTexture);
  //    Mesh2Point1.AdaptiveSample(uvScale);
  Mesh2Point1.UniformSample(uvScale, 80000);
  auto samples1 = Mesh2Point1.samples_;
  auto colors1  = Mesh2Point1.colors_;

  Mesh2PC Mesh2Point2(frame_reference, curTexture);
  //    Mesh2Point2.AdaptiveSample(uvScale);
  Mesh2Point2.UniformSample(uvScale, 80000);
  auto samples2 = Mesh2Point2.samples_;
  auto colors2  = Mesh2Point2.colors_;

  std::vector<vmesh::Vec3<double>> samples, colors;

  samples.reserve(samples1.size() + samples2.size());
  samples.insert(samples.end(), samples1.begin(), samples1.end());
  samples.insert(samples.end(), samples2.begin(), samples2.end());

  colors.reserve(samples1.size() + samples2.size());
  colors.insert(colors.end(), colors1.begin(), colors1.end());
  colors.insert(colors.end(), colors2.begin(), colors2.end());

  std::cout << "Step2: corse segmentation: " << std::endl;

  SupervoxelSeg SupSeg;
  SupSeg.setCloud(samples, colors);
  SupSeg.setMetricMode(0);
  SupSeg.FindNeighbor(30);
  SupSeg.set_n_sup(20);
  SupSeg.Find_superpixels();
  SupSeg.Relabel();

  Fast_Temporal_consist FTCon1(
    samples1, samples2, SupSeg.labels_, SupSeg.n_supervoxels_);
  FTCon1.matchcor(0);

  SupervoxelSeg SupSeg1;
  SupSeg1.setCloud(samples1, colors1);
  SupSeg1.FindNeighbor(20);
  SupSeg1.setMetricMode(1);
  SupSeg1.set_n_sup(100);
  SupSeg1.Find_superpixels();
  SupSeg1.Relabel();

  std::vector<int> matchedLabels, matchedIndexs;
  for (int i = 0; i < FTCon1.cor_set_.size(); ++i) {
    matchedLabels.push_back(SupSeg1.labels_[FTCon1.cor_set_[i](0)]);
    matchedIndexs.push_back(FTCon1.cor_set_[i](1));
  }

  std::cout << "Step3: refined segmentation: " << std::endl;

  SupervoxelSeg SupSeg2;
  SupSeg2.setCloud(samples2, colors2);
  SupSeg2.FindNeighbor(20);
  SupSeg2.setMetricMode(1);
  SupSeg2.setMatchedLabel(matchedLabels, matchedIndexs);

  std::vector<int> labels;
  labels.reserve(SupSeg1.labels_.size() + SupSeg2.labels_.size());
  labels.insert(labels.end(), SupSeg1.labels_.begin(), SupSeg1.labels_.end());
  labels.insert(labels.end(), SupSeg2.labels_.begin(), SupSeg2.labels_.end());

  Fast_Temporal_consist FTCon(
    samples1, samples2, labels, SupSeg1.n_supervoxels_);
  FTCon.matchcor(0);

  std::cout << "Step4: transform: " << std::endl;

  std::vector<int>                   subLabels;
  std::vector<vmesh::Vec3<MeshType>> subPoints;
  subLabels.resize(FTCon.cor_set_.size());
  subPoints.resize(FTCon.cor_set_.size());

  for (int i = 0; i < FTCon.cor_set_.size(); ++i) {
    subPoints[i] = samples1[FTCon.cor_set_[i](0)];
    subLabels[i] = SupSeg1.labels_[FTCon.cor_set_[i](0)];
  }

  for (int i = 0; i < FTCon.TransMatices.size(); ++i) {
    auto& Trans = FTCon.TransMatices[i];
    for (int i = 0; i < Trans.rows(); ++i) {
      for (int j = 0; j < Trans.cols(); ++j) {
        Trans(i, j) = roundTo5Decimals(Trans(i, j));
      }
    }
  }

  vmesh::KdTree<MeshType> Kdtree_sub(3, subPoints, 10);
  frame_base.convert(ref_base);

  for (int32_t idx = 0; idx < frame_base.pointCount(); ++idx) {
    auto&                 pointA  = frame_base.point(idx);
    int                   nnCount = 1;
    std::vector<int32_t>  indexes(nnCount);
    std::vector<MeshType> sqrDists(nnCount);
    Kdtree_sub.query(pointA.data(), nnCount, indexes.data(), sqrDists.data());
    auto Trans1 = FTCon.TransMatices[subLabels[indexes[0]]];

    Eigen::Vector4d new_point =
      Trans1 * Eigen::Vector4d(pointA[0], pointA[1], pointA[2], 1.0);
    auto pointB = new_point.head<3>() / new_point(3);

    pointA[0] = pointB(0);
    pointA[1] = pointB(1);
    pointA[2] = pointB(2);
  }

  std::cout << "Finish. " << std::endl;
}

const double max_weight = std::numeric_limits<double>::infinity();

double
Distance(Eigen::Vector3d a, Eigen::Vector3d b) {
  return sqrt(pow(a(0) - b(0), 2) + pow(a(1) - b(1), 2) + pow(a(2) - b(2), 2));
}

bool
find_element_in_vector(int               index,
                       std::vector<int>& vec,
                       int&              element_position) {
  std::vector<int>::iterator it;
  it               = std::find(vec.begin(), vec.end(), index);
  element_position = it - vec.begin();
  return it != vec.end();
}
