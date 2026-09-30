#include <jsoncpp/json/json.h>

#include <fstream>
#include <string>
#include <thread>

#include "pipe.hpp"
#include "reconstruction/reconstruction.hpp"
#include "sensor/kinect_cameras.hpp"
#include "streaming/network_stream.hpp"
#include "streaming/sender_to_unity.hpp"
#include "unordered_map"

static std::atomic<bool> stop_consumer = false;

static void sigint_handler(int)
{
  stop_consumer = true;
}

void image_consumer(PipeDataInCollection<ImageFrame>* dImageFrame)
{
  while (!stop_consumer)
  {
    for (size_t idx = 0; idx < dImageFrame->get_size(); idx++)
    {
      ImageFrame frame = dImageFrame->fetch(idx);

      std::cout << "fetch image" << std::endl;

      // Process the frame here

      int key = cv::waitKey(1);

      if (key == 'q')
      {
        break;
      }
    }
  }
}

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cerr << "usage: " << argv[0] << " <config-json>\n";
    return 1;
  }

  Json::Value jsonConf;
  {
    std::ifstream fs(argv[1]);
    if (!(fs >> jsonConf))
      std::cerr << "Error reading config\n";
  }
  int device_count = jsonConf["kinect_config"]["device_count"].asInt();
  std::cout << jsonConf["kinect_config"]["master_serial"].asString()
            << std::endl;

  PipeDataInCollection<ImageFrame>* dImageFrame
      = new PipeDataInCollection<ImageFrame>(device_count);
  PipeDataInCollectionOnce<CalibData>* dCalibData
      = new PipeDataInCollectionOnce<CalibData>(device_count);

  std::vector<std::string> serial_nums(device_count);
  KinectCameras* cameras = new KinectCameras(
      dImageFrame,
      dCalibData,
      &serial_nums,
      jsonConf["kinect_config"]["device_count"].asInt(),
      jsonConf["kinect_config"]["master_serial"].asString(),
      jsonConf["kinect_config"]["camera_resolution"]["width"].asInt(),
      jsonConf["kinect_config"]["camera_resolution"]["height"].asInt(),
      jsonConf["device_calibration"],
      true);

  PipeDataIn<MeshFrame>* dMeshFrame = new PipeDataIn<MeshFrame>();
  Reconstruction* reconstruction = new Reconstruction(
      dImageFrame,
      jsonConf["kinect_config"]["device_count"].asInt(),
      dCalibData,
      dMeshFrame);

  NetworkListener* listener = new NetworkListener(
      jsonConf["SenderToUnity"]["host"].asCString(),
      jsonConf["SenderToUnity"]["port"].asUInt());

  SenderToUnity* unity_sender
      = new SenderToUnity(listener->accept(), dMeshFrame);
  // std::thread consumer_thread(&image_consumer, dImageFrame);

  for (auto& thread : *cameras->get_cam_threads())
  {
    thread.join();
  }

  std::thread* thread = reconstruction->get_reconstruction_thread();
  thread->join();

  std::cout << "finish" << std::endl;

  return 0;
}
