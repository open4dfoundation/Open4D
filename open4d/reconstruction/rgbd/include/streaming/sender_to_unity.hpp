#pragma once

#include <thread>
#include <vector>

#include "data_type.hpp"
#include "network_stream.hpp"
#include "pipe.hpp"

class SenderToUnity
{
public:
  SenderToUnity(NetworkStream&& ns, PipeDataIn<MeshFrame>* src);
  ~SenderToUnity() = default;

  void fetch();

  void sendMeshes(const std::vector<Mesh>& meshes);

  void sendTexture(const cv::Mat* texture);

private:
  NetworkStream ns_;
  PipeDataIn<MeshFrame>* const src_;
  std::thread thread_;
};
