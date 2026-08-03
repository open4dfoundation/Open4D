#include "util.hpp"

#include <string>

#include "data_type.hpp"
#include "jsoncpp/json/json.h"
#include "open3d/Open3D.h"
#include "pipe.hpp"

CalibData calibration_loader(
    Json::Value jsonConf,
    uint32_t idx,
    std::string serial_num,
    PipeDataInCollectionOnce<CalibData>* const calib_data_out)
{
  Json::Value device_calibration = jsonConf[serial_num];
  Json::Value intrinsic = device_calibration["intrinsics"];
  Json::Value distortion = device_calibration["distortion"];
  Json::Value optimal_intrinsics = device_calibration["optimal_intrinsics"];
  Json::Value pose = device_calibration["pose"];

  CalibData calib_data = {
      .eigen_intrinsic = Eigen::Matrix3f::Identity(),
      .eigen_extrinsic = Eigen::Matrix4f::Identity(),
      .eigen_optimal_intrinsic = Eigen::Matrix3f::Identity(),
      .distortion_params = std::vector<float>(8),
      .o3d_intrinsic
      = open3d::core::Tensor::Eye(3, open3d::core::Dtype::Float64, CPU_DEVICE),
      .o3d_extrinsic
      = open3d::core::Tensor::Eye(4, open3d::core::Dtype::Float64, CPU_DEVICE)};

  // load intrinsic
  calib_data.eigen_intrinsic(0, 0) = intrinsic["fx"].asFloat();
  calib_data.eigen_intrinsic(1, 1) = intrinsic["fy"].asFloat();
  calib_data.eigen_intrinsic(0, 2) = intrinsic["cx"].asFloat();
  calib_data.eigen_intrinsic(1, 2) = intrinsic["cy"].asFloat();

  // load distortion
  calib_data.distortion_params.at(0) = distortion["k1"].asFloat();
  calib_data.distortion_params.at(1) = distortion["k2"].asFloat();
  calib_data.distortion_params.at(2) = distortion["p1"].asFloat();
  calib_data.distortion_params.at(3) = distortion["p2"].asFloat();
  calib_data.distortion_params.at(4) = distortion["k3"].asFloat();
  calib_data.distortion_params.at(5) = distortion["k4"].asFloat();
  calib_data.distortion_params.at(6) = distortion["k5"].asFloat();
  calib_data.distortion_params.at(7) = distortion["k6"].asFloat();

  // load optimal intrinsic
  calib_data.eigen_optimal_intrinsic(0, 0) = optimal_intrinsics["fx"].asFloat();
  calib_data.eigen_optimal_intrinsic(1, 1) = optimal_intrinsics["fy"].asFloat();
  calib_data.eigen_optimal_intrinsic(0, 2) = optimal_intrinsics["cx"].asFloat();
  calib_data.eigen_optimal_intrinsic(1, 2) = optimal_intrinsics["cy"].asFloat();

  // load pose
  if (jsonConf["use_identity_pose"] == false)
  {
    std::string pose_filepath = jsonConf["pose_calib_file_path"].asString();
    open3d::core::Tensor o3d_extrinsics = open3d::t::io::ReadNpy(pose_filepath);

    if (serial_num.compare("000954314612") == 0)
    {
      calib_data.o3d_extrinsic = o3d_extrinsics[0];
    }
    else if (serial_num.compare("000329792012") == 0)
    {
      calib_data.o3d_extrinsic = o3d_extrinsics[1].Clone();
    }
    else if (serial_num.compare("000092320412") == 0)
    {
      calib_data.o3d_extrinsic = o3d_extrinsics[2].Clone();
    }
    else
    {
      calib_data.o3d_extrinsic = o3d_extrinsics[3].Clone();
    }
  }

  open3d::core::Dtype o3d_dtype = open3d::core::Dtype::Float64;
  calib_data.o3d_intrinsic = open3d::core::eigen_converter::EigenMatrixToTensor(
                                 calib_data.eigen_optimal_intrinsic)
                                 .To(o3d_dtype);
  // calib_data.o3d_extrinsic =
  //     open3d::core::eigen_converter::EigenMatrixToTensor(calib_data.eigen_extrinsic).To(o3d_dtype);
  calib_data.eigen_extrinsic
      = open3d::core::eigen_converter::TensorToEigenMatrixXf(
          calib_data.o3d_extrinsic);

  calib_data_out->put(idx, calib_data);
  std::cout << calib_data.o3d_intrinsic.ToString() << std::endl;

  return calib_data;
}
