#include "texture_mapping_cuda.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool nearly_equal(float left, float right, float tolerance = 1e-5f)
{
  return std::fabs(left - right) <= tolerance;
}

} // namespace

int main()
{
  // A unit square at z=1 projected by a simple pinhole camera.
  const std::vector<float> vertices{
      0.0f, 0.0f, 1.0f,
      1.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f,
      0.0f, 1.0f, 1.0f};
  const std::vector<int> triangles{0, 1, 2, 0, 2, 3};
  const std::vector<float> intrinsic{
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  const std::vector<float> extrinsic{
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f};
  const std::vector<std::uint16_t> depth(16, 1000);
  std::vector<float> output(triangles.size() * 2);
  std::string error;

  if (!compute_texture_coordinates_cuda(
          vertices.data(),
          vertices.size() / 3,
          triangles.data(),
          triangles.size() / 3,
          intrinsic.data(),
          extrinsic.data(),
          depth.data(),
          1,
          4,
          4,
          50.0f,
          output.data(),
          &error))
  {
    std::cerr << error << '\n';
    return 1;
  }

  const std::vector<float> expected{
      0.0f, 0.0f,
      0.25f, 0.0f,
      0.25f, 0.25f,
      0.0f, 0.0f,
      0.25f, 0.25f,
      0.0f, 0.25f};
  for (std::size_t i = 0; i < output.size(); ++i)
  {
    if (!nearly_equal(output[i], expected[i]))
    {
      std::cerr << "UV mismatch at " << i << ": expected " << expected[i]
                << ", got " << output[i] << '\n';
      return 2;
    }
  }

  // Camera zero is outside the depth threshold, so camera one must supply
  // the UVs. Its atlas tile starts at normalized u=0.5.
  std::vector<float> two_camera_intrinsics;
  std::vector<float> two_camera_extrinsics;
  two_camera_intrinsics.insert(
      two_camera_intrinsics.end(), intrinsic.begin(), intrinsic.end());
  two_camera_intrinsics.insert(
      two_camera_intrinsics.end(), intrinsic.begin(), intrinsic.end());
  two_camera_extrinsics.insert(
      two_camera_extrinsics.end(), extrinsic.begin(), extrinsic.end());
  two_camera_extrinsics.insert(
      two_camera_extrinsics.end(), extrinsic.begin(), extrinsic.end());
  std::vector<std::uint16_t> two_camera_depth(32, 1000);
  std::fill(two_camera_depth.begin(), two_camera_depth.begin() + 16, 0);

  if (!compute_texture_coordinates_cuda(
          vertices.data(),
          vertices.size() / 3,
          triangles.data(),
          triangles.size() / 3,
          two_camera_intrinsics.data(),
          two_camera_extrinsics.data(),
          two_camera_depth.data(),
          2,
          4,
          4,
          50.0f,
          output.data(),
          &error))
  {
    std::cerr << error << '\n';
    return 3;
  }
  if (!nearly_equal(output[0], 0.5f)
      || !nearly_equal(output[2], 0.625f))
  {
    std::cerr << "camera-selection atlas offset is incorrect: "
              << output[0] << ", " << output[2] << '\n';
    return 4;
  }

  std::cout << "CUDA texture mapping test passed\n";
  return 0;
}
