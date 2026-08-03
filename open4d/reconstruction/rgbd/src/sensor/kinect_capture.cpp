#include "kinect_capture.hpp"

#include <Eigen/Core>
#include <chrono>
#include <k4a/k4a.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/opencv.hpp>

#include "cv_convert_util.hpp"

using namespace std;

void KinectCapture::init_device(uint32_t device_idx)
{
  this->device = k4a::device::open(device_idx);
  this->serial_num = this->device.get_serialnum();

  this->config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
  this->config.camera_fps = K4A_FRAMES_PER_SECOND_30;
  this->config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
  this->config.color_resolution = K4A_COLOR_RESOLUTION_1080P;
  this->config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
  this->config.synchronized_images_only = true;

  if (!this->enable_depth_)
  {
    this->config.depth_mode = K4A_DEPTH_MODE_OFF;
    this->config.synchronized_images_only = false;
  }
}

void KinectCapture::load_intrinsic_calibration(
    Eigen::Matrix3f intrinsics,
    Eigen::Matrix3f optimal_intrinsics,
    std::vector<float> distortion_params)
{
  this->intrinsic_ = intrinsics;
  this->optimal_intrinsic_ = optimal_intrinsics;
  this->distortion_params_ = distortion_params;
}

void KinectCapture::set_resolution(int width, int height)
{
  this->width_ = width;
  this->height_ = height;
}

void KinectCapture::get_factory_calibration()
{
  k4a_calibration_intrinsic_parameters_t::_param intrinsic_param
      = this->calibration_.color_camera_calibration.intrinsics.parameters.param;

  this->intrinsic_(0, 0) = intrinsic_param.fx;
  this->intrinsic_(1, 1) = intrinsic_param.fy;
  this->intrinsic_(0, 2) = intrinsic_param.cx;
  this->intrinsic_(1, 2) = intrinsic_param.cy;

  this->distortion_params_
      = {intrinsic_param.k1,
         intrinsic_param.k2,
         intrinsic_param.p1,
         intrinsic_param.p2,
         intrinsic_param.k3,
         intrinsic_param.k4,
         intrinsic_param.k5,
         intrinsic_param.k6};

  this->init_undistort_map(true);
}

void KinectCapture::init_undistort_map(bool use_factory_calibration)
{
  cv::Mat intrinsic;
  cv::Mat optimal_intrinsic;
  cv::eigen2cv(this->intrinsic_, intrinsic);

  if (use_factory_calibration)
  {
    optimal_intrinsic = cv::getOptimalNewCameraMatrix(
        intrinsic,
        this->distortion_params_,
        cv::Size(this->width_, this->height_),
        0.0,
        cv::Size(this->width_, this->height_));
  }
  else
  {
    cv::eigen2cv(this->optimal_intrinsic_, optimal_intrinsic);
  }

  cv::initUndistortRectifyMap(
      intrinsic,
      this->distortion_params_,
      cv::Mat(),
      optimal_intrinsic,
      cv::Size(this->width_, this->height_),
      CV_32FC1,
      this->map_1_,
      this->map_2_);
}

void KinectCapture::capture_frame()
{
  this->device.get_capture(
      &this->capture_, std::chrono::milliseconds{K4A_WAIT_INFINITE});

  if (!this->enable_depth_)
  {
    this->colorImage = this->capture_.get_color_image();

    while (!this->colorImage.is_valid())
    {
      this->device.get_capture(
          &this->capture_, std::chrono::milliseconds{K4A_WAIT_INFINITE});
      this->colorImage = this->capture_.get_color_image();
    }

    this->cv_color_img = k4a::get_mat(this->colorImage);

    // Undistort
    cv::remap(
        this->cv_color_img,
        this->cv_color_img_rectified,
        this->map_1_,
        this->map_2_,
        cv::INTER_LINEAR);
    // cv::imwrite(this->serial_num + "_color.png",
    // this->cv_color_img_rectified); exit(0);
  }
  else
  {
    this->colorImage = this->capture_.get_color_image();
    this->depthImage = this->capture_.get_depth_image();

    while (!this->colorImage.is_valid() || !this->depthImage.is_valid())
    {
      this->device.get_capture(
          &this->capture_, std::chrono::milliseconds{K4A_WAIT_INFINITE});
      this->colorImage = this->capture_.get_color_image();
      this->depthImage = this->capture_.get_depth_image();
    }

    this->cv_color_img = k4a::get_mat(this->colorImage);
    this->depthImage
        = this->transformation_.depth_image_to_color_camera(this->depthImage);
    this->cv_depth_img = k4a::get_mat(this->depthImage);

    // Undistort
    cv::remap(
        this->cv_color_img,
        this->cv_color_img_rectified,
        this->map_1_,
        this->map_2_,
        cv::INTER_LINEAR);
    cv::remap(
        this->cv_depth_img,
        this->cv_depth_img_rectified,
        this->map_1_,
        this->map_2_,
        cv::INTER_NEAREST);
  }
}

void KinectCapture::tf_depth_image()
{
  this->depthImage = k4a::image::create(
      K4A_IMAGE_FORMAT_DEPTH16,
      this->colorImage.get_width_pixels(),
      this->colorImage.get_height_pixels(),
      this->colorImage.get_width_pixels() * (int)sizeof(uint16_t));

  this->depthImage
      = this->transformation_.depth_image_to_color_camera(this->depthImage);
}

void KinectCapture::depth_to_pcl()
{
  this->xyzImage = k4a::image::create(
      K4A_IMAGE_FORMAT_CUSTOM,
      this->depthImage.get_width_pixels(),
      this->depthImage.get_height_pixels(),
      this->depthImage.get_width_pixels() * 3 * (int)sizeof(int16_t));

  this->xyzImage = this->transformation_.depth_image_to_point_cloud(
      this->depthImage, this->calibration_type_);
}
