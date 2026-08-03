#include "texture_mapping.hpp"

#include <Eigen/Core>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "open3d/Open3D.h"

#ifdef MESHREDUCE_HAS_CUDA_TEXTURE_MAPPING
#include "texture_mapping_cuda.hpp"
#endif

using namespace open3d;

uint32_t H_RES, V_RES;
uint32_t device_count;

namespace {

#ifdef MESHREDUCE_HAS_CUDA_TEXTURE_MAPPING
bool cuda_texture_mapping_enabled()
{
  const char* disabled = std::getenv("MESHREDUCE_DISABLE_CUDA_TEXTURE_MAPPING");
  return disabled == nullptr || std::string(disabled) == "0";
}

bool try_cuda_texture_mapping(
    const Eigen::MatrixXf& vertices,
    const Eigen::MatrixXi& triangles,
    const std::vector<core::Tensor>& intrinsic_list,
    const std::vector<core::Tensor>& extrinsic_list,
    const std::vector<cv::Mat>& depth_images,
    core::Tensor* output,
    std::string* error)
{
  if (intrinsic_list.empty() || intrinsic_list.size() != extrinsic_list.size()
      || intrinsic_list.size() != depth_images.size())
  {
    *error = "camera calibration and depth-image counts do not match";
    return false;
  }

  const int width = depth_images.front().cols;
  const int height = depth_images.front().rows;
  const int camera_count = static_cast<int>(intrinsic_list.size());
  std::vector<float> packed_vertices(
      static_cast<std::size_t>(vertices.rows()) * 3);
  std::vector<int> packed_triangles(
      static_cast<std::size_t>(triangles.rows()) * 3);
  std::vector<float> packed_intrinsics(
      static_cast<std::size_t>(camera_count) * 9);
  std::vector<float> packed_extrinsics(
      static_cast<std::size_t>(camera_count) * 16);
  std::vector<std::uint16_t> packed_depth(
      static_cast<std::size_t>(camera_count) * width * height);

#pragma omp parallel for schedule(static)
  for (Eigen::Index vertex = 0; vertex < vertices.rows(); ++vertex)
  {
    for (int axis = 0; axis < 3; ++axis)
    {
      packed_vertices[static_cast<std::size_t>(vertex) * 3 + axis]
          = vertices(vertex, axis);
    }
  }

#pragma omp parallel for schedule(static)
  for (Eigen::Index triangle = 0; triangle < triangles.rows(); ++triangle)
  {
    for (int corner = 0; corner < 3; ++corner)
    {
      packed_triangles[static_cast<std::size_t>(triangle) * 3 + corner]
          = triangles(triangle, corner);
    }
  }

  for (int camera = 0; camera < camera_count; ++camera)
  {
    const cv::Mat& depth = depth_images[camera];
    if (depth.type() != CV_16UC1 || depth.cols != width || depth.rows != height)
    {
      *error = "CUDA texture mapping requires equal-size CV_16UC1 depth images";
      return false;
    }

    const Eigen::MatrixXf intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(
            intrinsic_list[camera]);
    const Eigen::MatrixXf extrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(
            extrinsic_list[camera]);
    if (intrinsic.rows() < 3 || intrinsic.cols() < 3
        || extrinsic.rows() < 4 || extrinsic.cols() < 4)
    {
      *error = "CUDA texture mapping received an invalid calibration matrix";
      return false;
    }

    for (int row = 0; row < 3; ++row)
    {
      for (int column = 0; column < 3; ++column)
      {
        packed_intrinsics[
            static_cast<std::size_t>(camera) * 9 + row * 3 + column]
            = intrinsic(row, column);
      }
    }
    for (int row = 0; row < 4; ++row)
    {
      for (int column = 0; column < 4; ++column)
      {
        packed_extrinsics[
            static_cast<std::size_t>(camera) * 16 + row * 4 + column]
            = extrinsic(row, column);
      }
    }
    std::uint16_t* destination
        = packed_depth.data()
          + static_cast<std::size_t>(camera) * width * height;
#pragma omp parallel for schedule(static)
    for (int row = 0; row < height; ++row)
    {
      std::memcpy(
          destination + static_cast<std::size_t>(row) * width,
          depth.ptr<std::uint16_t>(row),
          static_cast<std::size_t>(width) * sizeof(std::uint16_t));
    }
  }

  return compute_texture_coordinates_cuda(
      packed_vertices.data(),
      static_cast<std::size_t>(vertices.rows()),
      packed_triangles.data(),
      static_cast<std::size_t>(triangles.rows()),
      packed_intrinsics.data(),
      packed_extrinsics.data(),
      packed_depth.data(),
      camera_count,
      width,
      height,
      50.0f,
      output->GetDataPtr<float>(),
      error);
}
#endif

} // namespace

void vertices_to_uv(
    Eigen::MatrixXf camera_intrinsic,
    Eigen::MatrixXf* triangle_vertices,
    Eigen::MatrixXd* uv)
{
  // Build [K | 0] explicitly. conservativeResize plus a fixed-size column
  // assignment triggered an aligned Eigen over-read with GCC 13.
  Eigen::Matrix<float, 3, 4> projection = Eigen::Matrix<float, 3, 4>::Zero();
  projection.block<3, 3>(0, 0) = camera_intrinsic.block<3, 3>(0, 0);

  Eigen::MatrixXf triangle_vertices_homo
      = triangle_vertices->rowwise().homogeneous();

  Eigen::MatrixXf float_uv;
  float_uv = projection * triangle_vertices_homo.transpose();
  Eigen::MatrixXf final_float_uv = float_uv.colwise().hnormalized().transpose();

  *uv = final_float_uv.cast<double>();
}

void multi_vertices_uv_to_triangle_uv(
    std::vector<Eigen::MatrixXd>* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    Eigen::VectorXi* min_indeces,
    std::vector<Eigen::Vector2d>* mesh_uvs)
{
  Eigen::MatrixXd shift_val(1, 2);
  shift_val << H_RES, 0;

  mesh_uvs->resize(
      static_cast<std::size_t>(triangle_indices_m->rows()) * 3);
#pragma omp parallel for schedule(static)
  for (int i = 0; i < triangle_indices_m->rows(); i++)
  {
    Eigen::Vector3i vertex_index = triangle_indices_m->row(i);

    int min_index_0 = (*min_indeces)(vertex_index(0));

    Eigen::Vector2d texture_uv_0
        = vertices_uv->at(min_index_0).row(vertex_index(0))
          + shift_val * min_index_0;
    Eigen::Vector2d texture_uv_1
        = vertices_uv->at(min_index_0).row(vertex_index(1))
          + shift_val * min_index_0;
    Eigen::Vector2d texture_uv_2
        = vertices_uv->at(min_index_0).row(vertex_index(2))
          + shift_val * min_index_0;

    texture_uv_0(0) /= (H_RES * device_count);
    texture_uv_0(1) /= V_RES;
    texture_uv_0(1) = abs(1.0 - texture_uv_0(1));
    texture_uv_1(0) /= (H_RES * device_count);
    texture_uv_1(1) /= V_RES;
    texture_uv_1(1) = abs(1.0 - texture_uv_1(1));
    texture_uv_2(0) /= (H_RES * device_count);
    texture_uv_2(1) /= V_RES;
    texture_uv_2(1) = abs(1.0 - texture_uv_2(1));

    (*mesh_uvs)[static_cast<std::size_t>(i) * 3 + 0] = texture_uv_0;
    (*mesh_uvs)[static_cast<std::size_t>(i) * 3 + 1] = texture_uv_1;
    (*mesh_uvs)[static_cast<std::size_t>(i) * 3 + 2] = texture_uv_2;
  }
}

void tensor_vertices_uv_to_triangle_uv(
    std::vector<Eigen::MatrixXd>* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    Eigen::VectorXi* min_indeces,
    core::Tensor* mesh_uvs)
{
  Eigen::MatrixXf shift_val(1, 2);
  shift_val << H_RES, 0;

  std::vector<Eigen::MatrixXf> float_vertices_uv;
  for (int i = 0; i < vertices_uv->size(); i++)
  {
    float_vertices_uv.push_back(vertices_uv->at(i).cast<float>());
  }
  float* texture_uvs_ptr = mesh_uvs->GetDataPtr<float>();

#pragma omp parallel for schedule(static)
  for (int i = 0; i < triangle_indices_m->rows(); i++)
  {
    Eigen::Vector3i vertex_index = triangle_indices_m->row(i);

    int min_index = (*min_indeces)(vertex_index(0));

    for (int j = 0; j < 3; j++)
    {
      Eigen::Vector2f texture_uv
          = float_vertices_uv.at(min_index).row(vertex_index(j))
            + shift_val * min_index;

      texture_uv(0) /= (H_RES * device_count);
      texture_uv(1) /= V_RES;
      texture_uv(1) = abs(1.0 - texture_uv(1));
      texture_uv(0) = texture_uv(0) - floor(texture_uv(0));
      texture_uv(1) = ceil(texture_uv(1)) - texture_uv(1);

      texture_uvs_ptr[i * 6 + j * 2 + 0] = texture_uv(0);
      texture_uvs_ptr[i * 6 + j * 2 + 1] = texture_uv(1);
    }
  }
}

void optimized_multi_cam_uv(
    t::geometry::TriangleMesh* mesh,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<cv::Mat>* cv_depth_img_list)
{

  H_RES = cv_depth_img_list->at(0).cols;
  V_RES = cv_depth_img_list->at(0).rows;
  device_count = intrinsic_list.size();
  std::cout << std::to_string(H_RES) << "x" << std::to_string(V_RES)
            << std::endl;

  std::vector<Eigen::MatrixXf> camera_intrinsic_list;
  for (uint32_t i = 0; i < device_count; i++)
  {
    Eigen::MatrixXf camera_intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_list.at(i));
    camera_intrinsic_list.push_back(camera_intrinsic);
  }

  core::Tensor triangle_indices = mesh->GetTriangleIndices();  // Int32
  core::Tensor triangle_vertices = mesh->GetVertexPositions(); // Float32

  Eigen::MatrixXi triangle_indices_m
      = core::eigen_converter::TensorToEigenMatrixXi(triangle_indices);
  Eigen::MatrixXf triangle_vertices_m
      = core::eigen_converter::TensorToEigenMatrixXf(triangle_vertices);

  const int num_triangles = triangle_indices_m.rows();
  core::Tensor tensor_mesh_uvs(
      {num_triangles, 3, 2}, core::Float32, core::Device("CPU:0"));

#ifdef MESHREDUCE_HAS_CUDA_TEXTURE_MAPPING
  if (cuda_texture_mapping_enabled())
  {
    std::string cuda_error;
    if (try_cuda_texture_mapping(
            triangle_vertices_m,
            triangle_indices_m,
            intrinsic_list,
            extrinsic_tf_list,
            *cv_depth_img_list,
            &tensor_mesh_uvs,
            &cuda_error))
    {
      mesh->SetTriangleAttr("texture_uvs", tensor_mesh_uvs);
      return;
    }

    static bool cuda_warning_emitted = false;
    if (!cuda_warning_emitted)
    {
      std::cerr << "CUDA texture mapping unavailable (" << cuda_error
                << "); using the OpenMP CPU fallback." << std::endl;
      cuda_warning_emitted = true;
    }
  }
#endif

  Eigen::MatrixXf z_error_m(triangle_vertices_m.rows(), device_count);
  std::vector<Eigen::MatrixXd> mesh_uvs(device_count);
  std::vector<Eigen::MatrixXf> all_triangle_vertices_tf;

  for (uint32_t i = 0; i < device_count; i++)
  {
    Eigen::MatrixXf triangle_vertices_tf;
    Eigen::MatrixXf eigen_intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_list.at(i));
    Eigen::MatrixXf eigen_extrinsics
        = core::eigen_converter::TensorToEigenMatrixXf(extrinsic_tf_list.at(i));
    transform_vertices(
        &triangle_vertices_m, eigen_extrinsics, &triangle_vertices_tf);
    all_triangle_vertices_tf.push_back(triangle_vertices_tf);

    vertices_to_uv(eigen_intrinsic, &triangle_vertices_tf, &(mesh_uvs.at(i)));

    Eigen::VectorXf z_error(triangle_vertices_tf.rows());
    multi_occlusion_test(
        &(cv_depth_img_list->at(i)),
        &triangle_vertices_tf,
        camera_intrinsic_list.at(i),
        &z_error);
    z_error_m.col(i) = z_error;
  }

  // Eigen::VectorXi min_indices(z_error_m.rows());
  // for (int i = 0; i < z_error_m.rows(); i++) {
  //     Eigen::MatrixXf::Index min_index;
  //     z_error_m.row(i).minCoeff(&min_indices);
  //     min_indices(i) = min_index;
  // }

  Eigen::VectorXi min_indices(z_error_m.rows());
  float max_threshold = 50; // Adjust the threshold as per your requirements

#pragma omp parallel for schedule(static)
  for (int i = 0; i < z_error_m.rows(); i++)
  {
    Eigen::MatrixXf::Index min_index
        = 0; // Start with the first column as the default index
    float min_value = z_error_m(i, 0);

    for (int j = 1; j < z_error_m.cols(); j++)
    {
      if (z_error_m(i, j) < max_threshold && min_value > max_threshold)
      {
        min_index = j;
        min_value = z_error_m(i, j);
      }
    }

    min_indices(i) = min_index;
  }

  tensor_vertices_uv_to_triangle_uv(
      &mesh_uvs, &triangle_indices_m, &min_indices, &tensor_mesh_uvs);

  mesh->SetTriangleAttr("texture_uvs", tensor_mesh_uvs);
}

void multi_occlusion_test(
    cv::Mat* cv_depth_img,
    Eigen::MatrixXf* float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic,
    Eigen::VectorXf* z_error)
{
  Eigen::MatrixXd uvs;
  vertices_to_uv(camera_intrinsic, float_vertices_tf, &uvs);

  Eigen::VectorXf vertex_z = float_vertices_tf->col(2) * 1000;
  Eigen::VectorXf depth_m(vertex_z.rows());

#pragma omp parallel for schedule(static)
  for (int i = 0; i < uvs.rows(); i++)
  {
    Eigen::Vector2d uv = uvs.row(i);
    if (uv(0) < 0 || uv(0) >= H_RES || uv(1) < 0 || uv(1) >= V_RES)
    {
      depth_m(i) = 1000000.0;
    }
    else
    {
      float depth_val
          = static_cast<float>(cv_depth_img->at<uint16_t>(uv(1), uv(0)));
      depth_m(i) = depth_val;
    }
  }

  *z_error = (vertex_z - depth_m).cwiseAbs();
}

void test_depth_optimized_multi_cam_uv(
    t::geometry::TriangleMesh* mesh,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<cv::Mat> depth_img_list,
    geometry::TriangleMesh* legacy_mesh)
{
  std::vector<Eigen::MatrixXf> camera_intrinsic_list;
  for (uint32_t i = 0; i < device_count; i++)
  {
    Eigen::MatrixXf camera_intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_list.at(i));
    camera_intrinsic_list.push_back(camera_intrinsic);
  }

  core::Tensor triangle_indices = mesh->GetTriangleIndices();  // Int32
  core::Tensor triangle_vertices = mesh->GetVertexPositions(); // Float32

  Eigen::MatrixXi triangle_indices_m
      = core::eigen_converter::TensorToEigenMatrixXi(triangle_indices);
  Eigen::MatrixXf triangle_vertices_m
      = core::eigen_converter::TensorToEigenMatrixXf(triangle_vertices);

  Eigen::MatrixXf z_error_m(triangle_vertices_m.rows(), device_count);
  std::vector<Eigen::MatrixXd> mesh_uvs(device_count);
  std::vector<Eigen::MatrixXf> all_triangle_vertices_tf;

  for (uint32_t i = 0; i < device_count; i++)
  {
    Eigen::MatrixXf triangle_vertices_tf;
    Eigen::MatrixXf eigen_intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_list.at(i));
    Eigen::MatrixXf eigen_extrinsics
        = core::eigen_converter::TensorToEigenMatrixXf(extrinsic_tf_list.at(i));
    transform_vertices(
        &triangle_vertices_m, eigen_extrinsics, &triangle_vertices_tf);
    all_triangle_vertices_tf.push_back(triangle_vertices_tf);

    vertices_to_uv(eigen_intrinsic, &triangle_vertices_tf, &(mesh_uvs.at(i)));

    Eigen::VectorXf z_error(triangle_vertices_tf.rows());
    test_depth_occlusion_test(
        &(depth_img_list.at(i)),
        &triangle_vertices_tf,
        camera_intrinsic_list.at(i),
        &z_error);
    z_error_m.col(i) = z_error;
  }

  Eigen::VectorXi min_indeces(z_error_m.rows());
#pragma omp parallel for schedule(static)
  for (int i = 0; i < z_error_m.rows(); i++)
  {
    Eigen::MatrixXf::Index min_index;
    z_error_m.row(i).minCoeff(&min_index);
    min_indeces(i) = min_index;
  }

  std::vector<Eigen::Vector2d> final_mesh_uvs;
  multi_vertices_uv_to_triangle_uv(
      &mesh_uvs, &triangle_indices_m, &min_indeces, &final_mesh_uvs);

  std::cout << final_mesh_uvs.size() << std::endl;

  *legacy_mesh = mesh->ToLegacy();
  legacy_mesh->triangle_uvs_ = final_mesh_uvs;
}

void test_depth_occlusion_test(
    cv::Mat* cv_depth_img,
    Eigen::MatrixXf* float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic,
    Eigen::VectorXf* z_error)
{
  Eigen::MatrixXd uvs;
  vertices_to_uv(camera_intrinsic, float_vertices_tf, &uvs);

  Eigen::VectorXf vertex_z = float_vertices_tf->col(2) * 1000;
  Eigen::VectorXf depth_m(vertex_z.rows());

#pragma omp parallel for schedule(static)
  for (int i = 0; i < uvs.rows(); i++)
  {
    Eigen::Vector2d uv = uvs.row(i);
    if (uv(0) < 0 || uv(0) > H_RES || uv(1) < 0 || uv(1) > V_RES)
    {
      depth_m(i) = 1000000.0;
    }
    else
    {
      float depth_val
          = static_cast<float>(cv_depth_img->at<uint16_t>(uv(1), uv(0)));
      depth_m(i) = depth_val;
      // std::cout << depth_val << std::endl;
    }
  }

  *z_error = (vertex_z - depth_m).cwiseAbs();
}

void vertices_uv_to_triangle_uv(
    Eigen::MatrixXd* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    core::Tensor* mesh_uvs)
{
  Eigen::MatrixXf float_vertices_uv = vertices_uv->cast<float>();

  int num_triangles = triangle_indices_m->rows();
  float* texture_uvs_ptr = mesh_uvs->GetDataPtr<float>();
#pragma omp parallel for schedule(static)
  for (int i = 0; i < num_triangles; i++)
  {
    Eigen::Vector3i xyz = triangle_indices_m->row(i);

    for (int j = 0; j < 3; j++)
    {
      texture_uvs_ptr[i * 6 + j * 2 + 0] = (float_vertices_uv)(xyz(j), 0);
      texture_uvs_ptr[i * 6 + j * 2 + 1] = (float_vertices_uv)(xyz(j), 1);
    }
  }
}

void normalize(Eigen::MatrixXd* uv_m, uint32_t num_cam)
{
  int col_num = 0;
  for (auto col : uv_m->colwise())
  {
    if (col_num == 0)
    {
      uv_m->col(col_num) = col.array() / (H_RES * num_cam);
    }
    else
    {
      uv_m->col(col_num) = col.array() / V_RES;
      uv_m->col(col_num) = abs(1.0 - col.array());
    }

    col_num++;
  }
}

void transform_vertices(
    Eigen::MatrixXf* triangle_vertices_m,
    Eigen::MatrixXf extrinsic_tf,
    Eigen::MatrixXf* transformed_vertices)
{
  Eigen::MatrixXf triangle_vertices_m_homo
      = triangle_vertices_m->rowwise().homogeneous();
  *transformed_vertices = (extrinsic_tf * triangle_vertices_m_homo.transpose())
                              .transpose()
                              .rowwise()
                              .hnormalized();
}

void mesh_uv_mapping(
    t::geometry::TriangleMesh* mesh,
    core::Tensor intrinsic_t,
    core::Tensor extrinsic_tf)
{
  core::Tensor triangle_indices = mesh->GetTriangleIndices();  // Int32
  core::Tensor triangle_vertices = mesh->GetVertexPositions(); // Float32

  Eigen::MatrixXi triangle_indices_m
      = core::eigen_converter::TensorToEigenMatrixXi(triangle_indices);
  Eigen::MatrixXf triangle_vertices_m
      = core::eigen_converter::TensorToEigenMatrixXf(triangle_vertices);

  Eigen::MatrixXf eigen_extrinsic_tf
      = core::eigen_converter::TensorToEigenMatrixXf(extrinsic_tf);
  Eigen::MatrixXf transformed_vertices;
  transform_vertices(
      &triangle_vertices_m, eigen_extrinsic_tf, &transformed_vertices);

  Eigen::MatrixXf eigen_intrinsic
      = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_t);
  Eigen::MatrixXd uv_m;

  vertices_to_uv(eigen_intrinsic, &transformed_vertices, &uv_m);

  normalize(&uv_m, device_count);

  int num_triangles = triangle_indices_m.rows();
  core::Tensor mesh_uvs(
      {num_triangles, 3, 2}, core::Float32, core::Device("CPU:0"));
  vertices_uv_to_triangle_uv(&uv_m, &triangle_indices_m, &mesh_uvs);

  mesh->SetTriangleAttr("texture_uvs", mesh_uvs);
}

float occlusion_test(
    t::geometry::Image depth_img,
    Eigen::MatrixXf float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic)
{
  float tolerance = 0.1 * 1000;

  Eigen::MatrixXd uv;
  vertices_to_uv(camera_intrinsic, &float_vertices_tf, &uv);

  Eigen::VectorXd max_uv = uv.colwise().maxCoeff();
  if ((max_uv(0) < 0 || max_uv(0) > H_RES)
      || (max_uv(1) < 0 || max_uv(1) > V_RES))
  {
    return 100000;
  }
  else
  {
    core::Tensor depth_val = depth_img.At(uv(0, 1), uv(0, 0));

    uint16_t* depth = (uint16_t*)depth_val.GetDataPtr();
    float vertex_z = float_vertices_tf(0, 2) * 1000;

    return abs(vertex_z - float(*depth));
  }
}

int calc_camera_triangle_angle(
    Eigen::Vector3d normal,
    Eigen::MatrixXf float_vertices,
    std::vector<Eigen::Vector3d> camera_pos,
    std::vector<t::geometry::Image>* depth_img_list,
    std::vector<Eigen::MatrixXf>* camera_intrinsic_list,
    std::vector<core::Tensor>* extrinsic_tf_list)
{
  std::vector<float> occlusion_result;

  for (int i = 0; i < camera_pos.size(); i++)
  {
    // find u and v in that camera view
    Eigen::MatrixXf float_vertices_tf;
    Eigen::MatrixXf eigen_extrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(
            extrinsic_tf_list->at(i));
    transform_vertices(&float_vertices, eigen_extrinsic, &float_vertices_tf);
    occlusion_result.push_back(occlusion_test(
        depth_img_list->at(i),
        float_vertices_tf,
        camera_intrinsic_list->at(i)));
  }

  int best_camera = std::distance(
      std::begin(occlusion_result),
      std::min_element(
          std::begin(occlusion_result), std::end(occlusion_result)));
  return best_camera;
}

void multi_cam_uv(
    geometry::TriangleMesh* cpu_mesh,
    std::vector<Eigen::Vector3d> camera_pos,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<t::geometry::Image> depth_img_list,
    geometry::TriangleMesh* legacy_mesh)
{

  std::vector<t::geometry::Image> depth_img_cpu_list;
  for (int i = 0; i < depth_img_list.size(); i++)
  {
    depth_img_cpu_list.push_back(
        depth_img_list.at(i).To(core::Device("CPU:0")));
  }

  std::vector<Eigen::MatrixXf> camera_intrinsic_list;
  for (int i = 0; i < intrinsic_list.size(); i++)
  {
    Eigen::MatrixXf camera_intrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(intrinsic_list.at(i));
    camera_intrinsic_list.push_back(camera_intrinsic);
  }

  std::vector<Eigen::Vector3i> triangles = cpu_mesh->triangles_;
  std::vector<Eigen::Vector3d> triangle_vertices = cpu_mesh->vertices_;

  cpu_mesh->ComputeVertexNormals();

  std::vector<Eigen::Vector3d> triangle_normals = cpu_mesh->triangle_normals_;

  std::vector<Eigen::Vector2d> triangle_uv;
  for (int i = 0; i < triangles.size(); i++)
  {

    // for (int i = 0; i < 1; i++) {
    // std::cout << i << std::endl;
    Eigen::Vector3d normal = triangle_normals.at(i);
    Eigen::Vector3i vertex_index = triangles.at(i);

    Eigen::MatrixXd vertices(3, 3);
    vertices << triangle_vertices.at(vertex_index(0)),
        triangle_vertices.at(vertex_index(1)),
        triangle_vertices.at(vertex_index(2));

    Eigen::MatrixXf float_vertices = vertices.cast<float>().transpose();

    // select best camera
    int best_camera = calc_camera_triangle_angle(
        normal,
        float_vertices,
        camera_pos,
        &depth_img_cpu_list,
        &camera_intrinsic_list,
        &extrinsic_tf_list);

    // find u and v in that camera view
    Eigen::MatrixXf eigen_extrinsic
        = core::eigen_converter::TensorToEigenMatrixXf(
            extrinsic_tf_list.at(best_camera));
    Eigen::MatrixXf float_vertices_tf;
    transform_vertices(&float_vertices, eigen_extrinsic, &float_vertices_tf);

    Eigen::MatrixXd uv;
    vertices_to_uv(
        camera_intrinsic_list.at(best_camera), &float_vertices_tf, &uv);

    // shift to correct camera image
    Eigen::MatrixXd shift_val(3, 2);
    shift_val << H_RES, 0, H_RES, 0, H_RES, 0;

    shift_val *= best_camera;
    uv += shift_val;

    for (int i = 0; i < uv.rows(); i++)
    {
      triangle_uv.push_back(uv.row(i));
    }
  }

  core::Tensor uv_tensor = core::eigen_converter::EigenVector2dVectorToTensor(
      triangle_uv, core::Float32, core::Device("CPU:0"));

  Eigen::MatrixXd uv_m
      = core::eigen_converter::TensorToEigenMatrixXd(uv_tensor);

  // normalize the value
  normalize(&uv_m, intrinsic_list.size());

  std::vector<Eigen::Vector2d> mesh_uvs;
  for (int i = 0; i < uv_m.rows(); i++)
  {
    mesh_uvs.push_back(uv_m.row(i));
  }

  cpu_mesh->triangle_uvs_ = mesh_uvs;
}
