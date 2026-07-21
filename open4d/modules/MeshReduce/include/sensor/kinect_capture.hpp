#pragma once

#include <Eigen/Core>
#include <k4a/k4a.hpp>
#include <opencv2/opencv.hpp>
#include <string>

class KinectCapture
{
public:
  k4a::device device;

  k4a::image colorImage;
  k4a::image depthImage;
  k4a::image xyzImage;

  cv::Mat cv_color_img;
  cv::Mat cv_depth_img;

  cv::Mat cv_color_img_rectified;
  cv::Mat cv_depth_img_rectified;

  std::string serial_num;
  k4a_device_configuration_t config;

  int count = 0;

  KinectCapture(uint32_t device_idx, bool enable_depth)
  {
    this->enable_depth_ = enable_depth;
    init_device(device_idx);

    this->calibration_ = this->device.get_calibration(
        this->config.depth_mode, this->config.color_resolution);

    this->transformation_ = k4a::transformation(this->calibration_);
    this->calibration_type_ = K4A_CALIBRATION_TYPE_COLOR;
  }

  ~KinectCapture()
  {
    this->device.close();
  }

  void init_device(uint32_t device_idx);

  void load_intrinsic_calibration(
      Eigen::Matrix3f intrinsics,
      Eigen::Matrix3f optimal_intrinsics,
      std::vector<float> distortion_params);

  void set_resolution(int width, int height);

  void get_factory_calibration();

  void init_undistort_map(bool use_factory_calibration);

  void capture_frame();

  void tf_depth_image();

  void depth_to_pcl();

private:
  k4a::calibration calibration_;
  k4a::capture capture_;
  k4a::transformation transformation_;
  k4a_calibration_type_t calibration_type_;

  bool enable_depth_;

  Eigen::Matrix3f intrinsic_ = Eigen::Matrix3f::Identity(3, 3);
  Eigen::Matrix3f optimal_intrinsic_ = Eigen::Matrix3f::Identity(3, 3);
  std::vector<float> distortion_params_;
  Eigen::Matrix4f pose_ = Eigen::Matrix4f::Identity(4, 4);

  int width_;
  int height_;

  cv::Mat map_1_;
  cv::Mat map_2_;
};
