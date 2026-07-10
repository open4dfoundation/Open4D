#pragma once

#include "SimpleMesh.h"
#include <string>

namespace SimpleMeshIO {
    /**
     * @brief ReadOBJ: Read an obj file and apply it into a Mesh object.
     * @param filename: The file path for the .obj
     * @param mesh: The simple mesh object to store the read .obj
     * @return A boolean representing the function's success.
     */
    bool ReadOBJ(const std::string& filename, SimpleMesh::Mesh& mesh);

    /**
     * @brief WriteOBJ: Write a new .obj file from a simple mesh object.
     * @param filename: A string representing the file path where we are storing the .obj
     * @param mesh: The simple mesh object that we are saving.
     * @return A boolean representing the function's success.
     */
    bool WriteOBJ(const std::string& filename, const SimpleMesh::Mesh& mesh);

    /**
     * @brief LoadTriangleIndicesFlat: Get a flat vector representing the .obj's triangle sets.
     * @param path: A string representing the file path for the .obj
     * @return A vector of integers representing the triangle sets for the .obj mesh.
     */
    std::vector<int> LoadTriangleIndicesFlat(const std::string& path);
}
