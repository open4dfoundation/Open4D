#pragma once

#include <open3d/Open3D.h>

#include <cmath>
#include <opencv2/opencv.hpp>

#define PI M_PI

using namespace open3d;

void vertices_to_uv(
    Eigen::MatrixXf camera_intrinsic,
    Eigen::MatrixXf* triangle_vertices,
    Eigen::MatrixXd* uv);

void vertices_uv_to_triangle_uv(
    Eigen::MatrixXd* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    core::Tensor* mesh_uvs);

void tensor_vertices_uv_to_triangle_uv(
    std::vector<Eigen::MatrixXd>* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    Eigen::VectorXi* min_indeces,
    core::Tensor* mesh_uvs);

void multi_vertices_uv_to_triangle_uv(
    std::vector<Eigen::MatrixXd>* vertices_uv,
    Eigen::MatrixXi* triangle_indices_m,
    Eigen::VectorXi* min_indeces,
    std::vector<Eigen::Vector2d>* mesh_uvs);

void normalize(Eigen::MatrixXd* uv_m, uint32_t num_cam);

void transform_vertices(
    Eigen::MatrixXf* triangle_vertices_m,
    Eigen::MatrixXf extrinsic_tf,
    Eigen::MatrixXf* transformed_vertices);

void mesh_uv_mapping(
    t::geometry::TriangleMesh* mesh,
    core::Tensor intrinsic_t,
    core::Tensor extrinsic_tf);

float occlusion_test(
    t::geometry::Image depth_img,
    Eigen::MatrixXf float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic);

int calc_camera_triangle_angle(
    Eigen::Vector3d normal,
    Eigen::MatrixXf float_vertices,
    std::vector<Eigen::Vector3d> camera_pos,
    std::vector<t::geometry::Image>* depth_img_list,
    std::vector<Eigen::MatrixXf>* camera_intrinsic_list,
    std::vector<core::Tensor>* extrinsic_tf_list);

void multi_occlusion_test(
    cv::Mat* cv_depth_img,
    Eigen::MatrixXf* float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic,
    Eigen::VectorXf* z_error);

void optimized_multi_cam_uv(
    t::geometry::TriangleMesh* mesh,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<cv::Mat>* cv_depth_img_list);

void test_depth_optimized_multi_cam_uv(
    t::geometry::TriangleMesh* mesh,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<cv::Mat> depth_img_list,
    geometry::TriangleMesh* legacy_mesh);

void test_depth_occlusion_test(
    cv::Mat* cv_depth_img,
    Eigen::MatrixXf* float_vertices_tf,
    Eigen::MatrixXf camera_intrinsic,
    Eigen::VectorXf* z_error);

void multi_cam_uv(
    geometry::TriangleMesh* cpu_mesh,
    std::vector<Eigen::Vector3d> camera_pos,
    std::vector<core::Tensor> intrinsic_list,
    std::vector<core::Tensor> extrinsic_tf_list,
    std::vector<t::geometry::Image> depth_img_list,
    geometry::TriangleMesh* legacy_mesh);
