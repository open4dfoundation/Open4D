#include "SimpleMesh.h"
#include <unordered_map>

namespace SimpleMesh {


void Mesh::ComputeAdjacencyList() {
    adjacency_list.clear();
    adjacency_list.resize(vertices.size());

    for (const auto& tri : triangles) {
        int i0 = tri[0];
        int i1 = tri[1];
        int i2 = tri[2];

        // For each vertex in the triangle, insert the other two as neighbors
        adjacency_list[i0].insert(i1);
        adjacency_list[i0].insert(i2);

        adjacency_list[i1].insert(i0);
        adjacency_list[i1].insert(i2);

        adjacency_list[i2].insert(i0);
        adjacency_list[i2].insert(i1);
    }
}


void Mesh::RemoveDuplicatedVertices(double threshold) {
    std::vector<int> old_to_new(vertices.size(), -1);
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> unique_vertices;

    for (size_t i = 0; i < vertices.size(); ++i) {
        bool found = false;
        for (size_t j = 0; j < unique_vertices.size(); ++j) {
            // If two vertices are close enough, treat them as duplicates
            if ((vertices[i] - unique_vertices[j]).norm() < threshold) {
                old_to_new[i] = static_cast<int>(j);
                found = true;
                break;
            }
        }
        // If not found, insert as a new unique vertex
        if (!found) {
            old_to_new[i] = static_cast<int>(unique_vertices.size());
            unique_vertices.push_back(vertices[i]);
        }
    }

    // Update triangle indices to match remapped vertex list
    for (auto& tri : triangles) {
        tri[0] = old_to_new[tri[0]];
        tri[1] = old_to_new[tri[1]];
        tri[2] = old_to_new[tri[2]];
    }

    vertices = std::move(unique_vertices);
}


void Mesh::SubdivideMidpoint() {
    // Helper struct for hashing edges
    struct Edge {
        int v0, v1;
        Edge(int a, int b) {
            v0 = std::min(a, b);  // Ensure consistent ordering
            v1 = std::max(a, b);
        }
        bool operator==(const Edge& other) const {
            return v0 == other.v0 && v1 == other.v1;
        }
    };

    struct EdgeHash {
        std::size_t operator()(const Edge& e) const {
            return std::hash<int>()(e.v0) ^ (std::hash<int>()(e.v1) << 1);
        }
    };

    std::unordered_map<Edge, int, EdgeHash> edge_to_midpoint;  // Cache for midpoint vertices
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> new_vertices = vertices;
    std::vector<Eigen::Vector3i> new_triangles;

    for (const auto& tri : triangles) {
        int v0 = tri[0], v1 = tri[1], v2 = tri[2];

        // Lambda to compute (or reuse) midpoint index for an edge
        auto get_midpoint = [&](int a, int b) -> int {
            Edge edge(a, b);
            auto it = edge_to_midpoint.find(edge);
            if (it != edge_to_midpoint.end()) {
                return it->second;
            }
            Eigen::Vector3d midpoint = 0.5 * (new_vertices[a] + new_vertices[b]);
            int new_index = static_cast<int>(new_vertices.size());
            new_vertices.push_back(midpoint);
            edge_to_midpoint[edge] = new_index;
            return new_index;
        };

        // Compute midpoints for each triangle edge
        int m01 = get_midpoint(v0, v1);
        int m12 = get_midpoint(v1, v2);
        int m20 = get_midpoint(v2, v0);

        // Create 4 new triangles from original triangle
        new_triangles.emplace_back(v0, m01, m20);
        new_triangles.emplace_back(v1, m12, m01);
        new_triangles.emplace_back(v2, m20, m12);
        new_triangles.emplace_back(m01, m12, m20);
    }

    vertices = std::move(new_vertices);
    triangles = std::move(new_triangles);
    RemoveDuplicatedVertices();  // Clean up any accidental duplicate midpoints
}

}  // namespace SimpleMesh
