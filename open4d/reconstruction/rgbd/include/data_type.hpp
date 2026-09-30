#pragma once

#include <Eigen/Core>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <vector>

#include "open3d/Open3D.h"

inline const open3d::core::Device GPU_DEVICE{"CUDA:0"};
inline const open3d::core::Device CPU_DEVICE{"CPU:0"};

inline std::mutex mtxO3d;

struct CalibData
{
  Eigen::Matrix3f eigen_intrinsic;
  Eigen::Matrix4f eigen_extrinsic;
  Eigen::Matrix3f eigen_optimal_intrinsic;
  std::vector<float> distortion_params;

  open3d::core::Tensor o3d_intrinsic;
  open3d::core::Tensor o3d_extrinsic;
};

struct ImageFrame
{
  cv::Mat cv_colImg, cv_depImg;
  open3d::t::geometry::Image o3d_colImg, o3d_depImg;
};

struct ImageFrameGpu
{
  cv::Mat cvColImg, cvDepImg;
  open3d::t::geometry::Image colImg, depImg;
};

struct SyncdCalibImage
{
  open3d::t::geometry::Image color;
  open3d::t::geometry::Image depth;
};

using Mesh = open3d::t::geometry::TriangleMesh;
using Texture = cv::Mat;

struct MeshFrame
{
  Mesh mesh;
  Texture texture;
};
