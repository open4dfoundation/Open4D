#include "kinect_cameras.hpp"

#include <jsoncpp/json/json.h>
#include <signal.h>

#include <string>
#include <thread>

#include "kinect_capture.hpp"
#include "util.hpp"

#define COLOR_EXPOSURE_USEC 50000
#define POWERLINE_FREQ 2

static std::atomic<bool> stop_cameras = false;

static void sigint_handler(int)
{
  stop_cameras = true;
}

KinectCameras::KinectCameras(
    PipeDataInCollection<ImageFrame>* out,
    PipeDataInCollectionOnce<CalibData>* calibOut,
    std::vector<std::string>* serial_nums,
    int device_count,
    std::string master_serial,
    int width,
    int height,
    Json::Value jsonConf_calibration,
    bool enable_depth)
  : out_(out)
  , calib_data_out_(calibOut)
  , conf_({(uint32_t)device_count, master_serial, width, height})
  , calibration_config_(jsonConf_calibration)
  , enable_depth_(enable_depth)
{
  signal(SIGINT, sigint_handler);

  for (uint32_t i = 0; i < conf_.nCams; i++)
  {
    this->threads_.push_back(
        std::thread(&KinectCameras::run, this, i, &(serial_nums->at(i))));
  }
}

KinectCameras::~KinectCameras()
{
  for (auto& t : this->threads_)
  {
    t.join();
  }
  std::cerr << "Cameras stopped\n";
}

static int32_t getSyncTiming(size_t idx, size_t nCams)
{
  return 160 * idx - 80 * (nCams - 1);
}

void KinectCameras::run(uint32_t idx, std::string* serial_num)
{
  std::unique_ptr<KinectCapture> cap
      = std::make_unique<KinectCapture>(idx, this->enable_depth_);

  cap->set_resolution(this->conf_.width, this->conf_.height);
  std::cout << "set resolution: " << this->conf_.width << "x"
            << this->conf_.height << std::endl;

  if (this->enable_depth_)
  {
    if (this->conf_.nCams == 1)
    {
      cap->config.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;
    }
    else
    {
      cap->device.set_color_control(
        K4A_COLOR_CONTROL_EXPOSURE_TIME_ABSOLUTE,
        K4A_COLOR_CONTROL_MODE_MANUAL,
        COLOR_EXPOSURE_USEC);

      cap->device.set_color_control(
          K4A_COLOR_CONTROL_POWERLINE_FREQUENCY,
          K4A_COLOR_CONTROL_MODE_MANUAL,
          POWERLINE_FREQ);

      cap->device.set_color_control(
          K4A_COLOR_CONTROL_WHITEBALANCE, K4A_COLOR_CONTROL_MODE_AUTO, 0);

      if (cap->serial_num.compare(this->conf_.master) == 0)
      {
        std::cout << "master detected" << std::endl;
        cap->config.wired_sync_mode = K4A_WIRED_SYNC_MODE_MASTER;
        cap->config.subordinate_delay_off_master_usec = 0;
        cap->config.depth_delay_off_color_usec
            = getSyncTiming(idx, this->conf_.nCams);
      }
      else
      {
        cap->config.wired_sync_mode = K4A_WIRED_SYNC_MODE_SUBORDINATE;
        cap->config.subordinate_delay_off_master_usec = 0;
        cap->config.depth_delay_off_color_usec
            = getSyncTiming(idx, this->conf_.nCams);
      }
    }
  }
  else
  {
    cap->config.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;
  }

  // Load calibration data
  CalibData calib_data = calibration_loader(
      this->calibration_config_, idx, cap->serial_num, this->calib_data_out_);
  std::cout << "loaded calibration data: " << cap->serial_num << std::endl;
  *serial_num = cap->serial_num;

  //  cap->get_factory_calibration();
  cap->load_intrinsic_calibration(
      calib_data.eigen_intrinsic,
      calib_data.eigen_optimal_intrinsic,
      calib_data.distortion_params);
  cap->init_undistort_map(false);

  cap->device.start_cameras(&cap->config);

  cap->capture_frame();

  while (!stop_cameras)
  {
    cap->capture_frame();

    open3d::t::geometry::Image o3d_color;
    open3d::t::geometry::Image o3d_depth;

//    if (this->enable_depth_)
//    {
//      o3d_color = open3d::core::Tensor(
//          reinterpret_cast<const uint8_t*>(cap->cv_color_img_rectified.data),
//          {cap->cv_color_img.rows, cap->cv_color_img.cols, 3},
//          open3d::core::UInt8,
//          CPU_DEVICE);
//
//      o3d_depth = open3d::core::Tensor(
//          reinterpret_cast<const uint16_t*>(cap->cv_depth_img_rectified.data),
//          {cap->cv_depth_img.rows, cap->cv_depth_img.cols, 1},
//          open3d::core::UInt16,
//          CPU_DEVICE);
//    }

    this->out_->put(
        idx,
        {.cv_colImg = cap->cv_color_img_rectified,
         .cv_depImg = cap->cv_depth_img_rectified,
         .o3d_colImg = o3d_color,
         .o3d_depImg = o3d_depth});
  }
}

std::vector<std::thread>* KinectCameras::get_cam_threads()
{
  return &(this->threads_);
}
