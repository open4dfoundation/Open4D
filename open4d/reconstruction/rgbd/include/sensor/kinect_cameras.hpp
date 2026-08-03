#pragma once

#include <jsoncpp/json/json.h>

#include <string>
#include <thread>

#include "data_type.hpp"
#include "pipe.hpp"

class KinectCameras
{
public:
  struct Config
  {
    uint32_t nCams;
    std::string master;
    int width;
    int height;
  };

  KinectCameras(
      PipeDataInCollection<ImageFrame>* out,
      PipeDataInCollectionOnce<CalibData>* calibOut,
      std::vector<std::string> *serial_nums,
      int device_count,
      std::string master_serial,
      int width,
      int height,
      Json::Value jsonConf_calibration,
      bool enable_depth);

  ~KinectCameras();

  std::vector<std::thread>* get_cam_threads();

private:
  PipeDataInCollection<ImageFrame>* const out_;
  PipeDataInCollectionOnce<CalibData>* const calib_data_out_;
  const Config conf_;
  Json::Value calibration_config_;
  bool enable_depth_;

  std::vector<std::thread> threads_;

  void run(uint32_t idx, std::string *serial_num);
};
