#include "texture_mapping_cuda.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace {

template <typename T>
class DeviceBuffer
{
 public:
  DeviceBuffer() = default;
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  ~DeviceBuffer()
  {
    if (data_ != nullptr)
    {
      cudaFree(data_);
    }
  }

  bool allocate(std::size_t count, std::string* error)
  {
    if (count == 0)
    {
      return true;
    }
    const cudaError_t status
        = cudaMalloc(reinterpret_cast<void**>(&data_), count * sizeof(T));
    if (status != cudaSuccess)
    {
      set_error("cudaMalloc", status, error);
      return false;
    }
    return true;
  }

  T* get() { return data_; }
  const T* get() const { return data_; }

 private:
  static void set_error(
      const char* operation, cudaError_t status, std::string* error)
  {
    if (error != nullptr)
    {
      std::ostringstream message;
      message << operation << " failed: " << cudaGetErrorString(status);
      *error = message.str();
    }
  }

  T* data_ = nullptr;
};

bool check_cuda(
    cudaError_t status, const char* operation, std::string* error)
{
  if (status == cudaSuccess)
  {
    return true;
  }
  if (error != nullptr)
  {
    std::ostringstream message;
    message << operation << " failed: " << cudaGetErrorString(status);
    *error = message.str();
  }
  return false;
}

template <typename T>
bool copy_to_device(
    DeviceBuffer<T>* destination,
    const T* source,
    std::size_t count,
    std::string* error)
{
  if (!destination->allocate(count, error))
  {
    return false;
  }
  if (count == 0)
  {
    return true;
  }
  return check_cuda(
      cudaMemcpy(
          destination->get(),
          source,
          count * sizeof(T),
          cudaMemcpyHostToDevice),
      "cudaMemcpy(host to device)",
      error);
}

__global__ void project_and_select_camera(
    const float* vertices,
    std::size_t vertex_count,
    const float* intrinsics,
    const float* extrinsics,
    const std::uint16_t* depth_images,
    int camera_count,
    int width,
    int height,
    float visibility_threshold_mm,
    float* projected_uvs,
    int* selected_cameras)
{
  const std::size_t vertex
      = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (vertex >= vertex_count)
  {
    return;
  }

  const float x = vertices[vertex * 3 + 0];
  const float y = vertices[vertex * 3 + 1];
  const float z = vertices[vertex * 3 + 2];

  int selected_camera = 0;
  float selected_error = INFINITY;

  for (int camera = 0; camera < camera_count; ++camera)
  {
    const float* extrinsic = extrinsics + camera * 16;
    float tx = extrinsic[0] * x + extrinsic[1] * y
               + extrinsic[2] * z + extrinsic[3];
    float ty = extrinsic[4] * x + extrinsic[5] * y
               + extrinsic[6] * z + extrinsic[7];
    float tz = extrinsic[8] * x + extrinsic[9] * y
               + extrinsic[10] * z + extrinsic[11];
    const float tw = extrinsic[12] * x + extrinsic[13] * y
                     + extrinsic[14] * z + extrinsic[15];
    if (tw != 0.0f && tw != 1.0f)
    {
      tx /= tw;
      ty /= tw;
      tz /= tw;
    }

    const float* intrinsic = intrinsics + camera * 9;
    const float denominator
        = intrinsic[6] * tx + intrinsic[7] * ty + intrinsic[8] * tz;
    float u = NAN;
    float v = NAN;
    float z_error = INFINITY;

    if (denominator != 0.0f)
    {
      u = (intrinsic[0] * tx + intrinsic[1] * ty + intrinsic[2] * tz)
          / denominator;
      v = (intrinsic[3] * tx + intrinsic[4] * ty + intrinsic[5] * tz)
          / denominator;

      if (isfinite(u) && isfinite(v) && u >= 0.0f && u < width
          && v >= 0.0f && v < height)
      {
        const int pixel_x = static_cast<int>(u);
        const int pixel_y = static_cast<int>(v);
        const std::size_t depth_index
            = (static_cast<std::size_t>(camera) * height + pixel_y) * width
              + pixel_x;
        z_error = fabsf(
            tz * 1000.0f - static_cast<float>(depth_images[depth_index]));
      }
    }

    const std::size_t uv_index
        = (static_cast<std::size_t>(camera) * vertex_count + vertex) * 2;
    projected_uvs[uv_index + 0] = u;
    projected_uvs[uv_index + 1] = v;

    // Preserve the existing CPU behavior: camera zero wins when visible;
    // otherwise, select the first later camera within the threshold.
    if (camera == 0)
    {
      selected_error = z_error;
    }
    else if (z_error < visibility_threshold_mm
             && selected_error > visibility_threshold_mm)
    {
      selected_camera = camera;
      selected_error = z_error;
    }
  }

  selected_cameras[vertex] = selected_camera;
}

__global__ void generate_triangle_uvs(
    const int* triangle_indices,
    std::size_t triangle_count,
    std::size_t vertex_count,
    const float* projected_uvs,
    const int* selected_cameras,
    int camera_count,
    int width,
    int height,
    float* output_uvs)
{
  const std::size_t corner
      = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (corner >= triangle_count * 3)
  {
    return;
  }

  const std::size_t triangle = corner / 3;
  const int vertex = triangle_indices[corner];
  const int selection_vertex = triangle_indices[triangle * 3];
  if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertex_count
      || selection_vertex < 0
      || static_cast<std::size_t>(selection_vertex) >= vertex_count)
  {
    output_uvs[corner * 2 + 0] = NAN;
    output_uvs[corner * 2 + 1] = NAN;
    return;
  }

  const int camera = selected_cameras[selection_vertex];
  const std::size_t uv_index
      = (static_cast<std::size_t>(camera) * vertex_count + vertex) * 2;
  float u = (projected_uvs[uv_index + 0] + width * camera)
            / static_cast<float>(width * camera_count);
  float v = fabsf(
      1.0f - projected_uvs[uv_index + 1] / static_cast<float>(height));

  u -= floorf(u);
  v = ceilf(v) - v;
  output_uvs[corner * 2 + 0] = u;
  output_uvs[corner * 2 + 1] = v;
}

} // namespace

bool compute_texture_coordinates_cuda(
    const float* vertices_xyz,
    std::size_t vertex_count,
    const int* triangle_indices,
    std::size_t triangle_count,
    const float* camera_intrinsics_3x3,
    const float* camera_extrinsics_4x4,
    const std::uint16_t* depth_images,
    int camera_count,
    int width,
    int height,
    float visibility_threshold_mm,
    float* output_triangle_uvs,
    std::string* error)
{
  if (error != nullptr)
  {
    error->clear();
  }
  if (vertices_xyz == nullptr || triangle_indices == nullptr
      || camera_intrinsics_3x3 == nullptr
      || camera_extrinsics_4x4 == nullptr || depth_images == nullptr
      || output_triangle_uvs == nullptr || vertex_count == 0
      || triangle_count == 0 || camera_count <= 0 || width <= 0 || height <= 0)
  {
    if (error != nullptr)
    {
      *error = "invalid or empty texture-mapping input";
    }
    return false;
  }

  int device_count = 0;
  if (!check_cuda(
          cudaGetDeviceCount(&device_count), "cudaGetDeviceCount", error)
      || device_count == 0)
  {
    if (device_count == 0 && error != nullptr && error->empty())
    {
      *error = "no CUDA device is available";
    }
    return false;
  }

  int requested_device = 0;
  if (const char* value = std::getenv("MESHREDUCE_CUDA_DEVICE"))
  {
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end != value && *end == '\0' && parsed >= 0 && parsed < device_count)
    {
      requested_device = static_cast<int>(parsed);
    }
  }
  if (!check_cuda(
          cudaSetDevice(requested_device), "cudaSetDevice", error))
  {
    return false;
  }

  DeviceBuffer<float> device_vertices;
  DeviceBuffer<int> device_triangles;
  DeviceBuffer<float> device_intrinsics;
  DeviceBuffer<float> device_extrinsics;
  DeviceBuffer<std::uint16_t> device_depth;
  DeviceBuffer<float> device_projected_uvs;
  DeviceBuffer<int> device_selected_cameras;
  DeviceBuffer<float> device_output_uvs;

  const std::size_t projected_uv_count
      = static_cast<std::size_t>(camera_count) * vertex_count * 2;
  const std::size_t output_uv_count = triangle_count * 3 * 2;
  if (!copy_to_device(
          &device_vertices, vertices_xyz, vertex_count * 3, error)
      || !copy_to_device(
          &device_triangles,
          triangle_indices,
          triangle_count * 3,
          error)
      || !copy_to_device(
          &device_intrinsics,
          camera_intrinsics_3x3,
          static_cast<std::size_t>(camera_count) * 9,
          error)
      || !copy_to_device(
          &device_extrinsics,
          camera_extrinsics_4x4,
          static_cast<std::size_t>(camera_count) * 16,
          error)
      || !copy_to_device(
          &device_depth,
          depth_images,
          static_cast<std::size_t>(camera_count) * width * height,
          error)
      || !device_projected_uvs.allocate(projected_uv_count, error)
      || !device_selected_cameras.allocate(vertex_count, error)
      || !device_output_uvs.allocate(output_uv_count, error))
  {
    return false;
  }

  constexpr int block_size = 256;
  const int vertex_blocks
      = static_cast<int>((vertex_count + block_size - 1) / block_size);
  project_and_select_camera<<<vertex_blocks, block_size>>>(
      device_vertices.get(),
      vertex_count,
      device_intrinsics.get(),
      device_extrinsics.get(),
      device_depth.get(),
      camera_count,
      width,
      height,
      visibility_threshold_mm,
      device_projected_uvs.get(),
      device_selected_cameras.get());
  if (!check_cuda(
          cudaGetLastError(), "project_and_select_camera launch", error))
  {
    return false;
  }

  const std::size_t corner_count = triangle_count * 3;
  const int triangle_blocks
      = static_cast<int>((corner_count + block_size - 1) / block_size);
  generate_triangle_uvs<<<triangle_blocks, block_size>>>(
      device_triangles.get(),
      triangle_count,
      vertex_count,
      device_projected_uvs.get(),
      device_selected_cameras.get(),
      camera_count,
      width,
      height,
      device_output_uvs.get());
  if (!check_cuda(
          cudaGetLastError(), "generate_triangle_uvs launch", error)
      || !check_cuda(
          cudaMemcpy(
              output_triangle_uvs,
              device_output_uvs.get(),
              output_uv_count * sizeof(float),
              cudaMemcpyDeviceToHost),
          "cudaMemcpy(device to host)",
          error))
  {
    return false;
  }

  return true;
}
