#include "reconstruction.hpp"

#include <signal.h>

#include <thread>

#include "open3d/Open3D.h"
#include "pipe.hpp"
#include "texture_mapping.hpp"

using namespace open3d;

static sig_atomic_t stop_reconstruction = 0;

static void sigint_handler(int)
{
  stop_reconstruction = 1;
}

Reconstruction::Reconstruction(
    PipeDataInCollection<ImageFrame>* const in,
    int device_count,
    PipeDataInCollectionOnce<CalibData>* const calib_data,
    PipeDataIn<MeshFrame>* const mesh_frame_out)
  : in_(in)
  , device_count_(device_count)
  , calib_data_(calib_data)
  , mesh_frame_out_(mesh_frame_out)
{
  signal(SIGINT, sigint_handler);

  this->thread_ = std::thread(&Reconstruction::run, this);
}

std::thread* Reconstruction::get_reconstruction_thread()
{
  return &(this->thread_);
}

void Reconstruction::run()
{
  std::vector<CalibData> calib = this->calib_data_->fetch_all();

  std::vector<open3d::core::Tensor> extrinsic_tf_list(this->device_count_),
      intrinsic_list(this->device_count_);
  for (size_t i = 0; i < this->device_count_; i++)
  {
    extrinsic_tf_list[i] = std::move(calib.at(i).o3d_extrinsic);
    intrinsic_list[i] = std::move(calib.at(i).o3d_intrinsic);
  }

  t::geometry::TriangleMesh mesh;
  std::vector<ImageFrame> cam_frame(4);

  try
  {
    while (1)
    {
      t::geometry::VoxelBlockGrid voxel_grid(
          {"tsdf", "weight"},
          {core::Dtype::Float32, core::Dtype::Float32},
          {{1}, {1}},
          this->config_.voxel_size,
          16,
          this->config_.block_count,
          GPU_DEVICE);

      for (int i = 0; i < this->device_count_; i++)
      {
        cam_frame.at(i) = this->in_->fetch(i);
        t::geometry::Image depth_img = cam_frame.at(i).o3d_depImg;
        std::cout << "reconstruction received image" << std::endl;

        core::Tensor frustum_block_coords
            = voxel_grid.GetUniqueBlockCoordinates(
                depth_img,
                calib.at(i).o3d_intrinsic,
                calib.at(i).o3d_extrinsic,
                this->config_.depth_scale,
                this->config_.depth_max,
                this->config_.trunc_voxel_multiplier);

        voxel_grid.Integrate(
            frustum_block_coords,
            depth_img,
            calib.at(i).o3d_intrinsic,
            calib.at(i).o3d_extrinsic,
            this->config_.depth_scale,
            this->config_.depth_max,
            this->config_.trunc_voxel_multiplier);
      }

      mesh = voxel_grid.ExtractTriangleMesh(0, -1).To(CPU_DEVICE);

      std::vector<cv::Mat> cvColImgs(this->device_count_),
          cvDepImgs(this->device_count_);
      for (size_t i = 0; i < this->device_count_; i++)
      {
        cvColImgs[i] = std::move(cam_frame.at(i).cv_colImg);
        cvDepImgs[i] = std::move(cam_frame.at(i).cv_depImg);
      }

      cv::Mat stitched_image;
      cv::hconcat(cvColImgs, stitched_image);

      mesh.RemoveVertexAttr("normals");

      // mesh = mesh.SimplifyQuadricDecimation(0.25);

      optimized_multi_cam_uv(
          &mesh, intrinsic_list, extrinsic_tf_list, &cvDepImgs);

      mesh_frame_out_->put({std::move(mesh), std::move(stitched_image)});
    }
  }
  catch (InTerminatedException&)
  {
  }
}
