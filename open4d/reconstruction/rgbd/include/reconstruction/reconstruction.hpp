#pragma once

#include "data_type.hpp"
#include "open3d/Open3D.h"
#include "pipe.hpp"

class Reconstruction
{
public:
  struct Config
  {
    size_t nThreads = 1;
    float voxel_size = 0.01f;
    int block_count = 10000;
    float depth_max = 5.0f;
    float trunc_voxel_multiplier = 4.0f;
    float depth_scale = 1000.0f;
    double decimation = 0.0;
  };

  Reconstruction(
      PipeDataInCollection<ImageFrame>* const out,
      int device_count,
      PipeDataInCollectionOnce<CalibData>* const calib_data,
      PipeDataIn<MeshFrame>* const mesh_frame_out);

  ~Reconstruction();

  std::thread* get_reconstruction_thread();

  void run();

private:
  const Config config_;
  PipeDataInCollectionOnce<CalibData>* const calib_data_;
  int device_count_;
  std::thread thread_;

  PipeDataInCollection<ImageFrame>* const in_;
  PipeDataIn<MeshFrame>* const mesh_frame_out_;
};
