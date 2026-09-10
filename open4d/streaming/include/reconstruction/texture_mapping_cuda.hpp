#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Computes normalized UVs for all triangle corners. Matrices are row-major,
// vertices and triangle indices are packed xyz, and depth images are packed
// camera-major as camera_count contiguous width*height planes.
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
    std::string* error);
