#pragma once

#include <vector>
#include <array>
#include <unordered_set>
#include <Eigen/Core>

namespace SimpleMesh {

class Mesh {
public:
    // Required for proper memory alignment when using Eigen in containers
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    // Aligned vertex storage
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> vertices;
    std::vector<Eigen::Vector3i> triangles;
    std::vector<std::unordered_set<int>> adjacency_list;

    /**
     * @brief RemoveDuplicatedVertices: Clean up duplicate mesh vertices.
     * @param threshold: The distance threshold to remove doubled vertices. Default is 1e-rf.
     */
    void RemoveDuplicatedVertices(double threshold = 1e-6f);

    /**
     * @brief ComputeAdjacencyList: Compute the edge adjacency list for the mesh.
     */
    void ComputeAdjacencyList();

    /**
     * @brief SubdivideMidpoint: Subdivide the mesh once.
     */
    void SubdivideMidpoint();

    Mesh() = default;
};

}  // namespace SimpleMesh
