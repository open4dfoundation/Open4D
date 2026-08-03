#include "SimpleMeshIO.h"
#include <fstream>
#include <sstream>

namespace SimpleMeshIO {


bool ReadOBJ(const std::string& filename, SimpleMesh::Mesh& mesh) {
    std::ifstream in(filename);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        if (line.substr(0, 2) == "v ") {
            // Parse vertex line: v x y z
            char v; float x, y, z;
            iss >> v >> x >> y >> z;
            mesh.vertices.emplace_back(x, y, z);
        } else if (line.substr(0, 2) == "f ") {
            // Parse face line: f i j k
            char f; int v1, v2, v3;
            iss >> f >> v1 >> v2 >> v3;
            mesh.triangles.push_back({v1 - 1, v2 - 1, v3 - 1});  // OBJ is 1-based
        }
    }

    in.close();
    return true;
}


std::vector<int> LoadTriangleIndicesFlat(const std::string& path) {
    std::vector<int> triangleIndices;
    std::ifstream in(path);
    if (!in.is_open()) return triangleIndices;

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("f ", 0) == 0) {
            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            std::string v1, v2, v3;
            if (!(iss >> v1 >> v2 >> v3)) continue;

            // Parse each vertex index, ignoring texture/normal data if present
            auto parseIndex = [](const std::string& token) {
                size_t slash = token.find('/');
                return std::stoi(token.substr(0, slash)) - 1;
            };

            triangleIndices.push_back(parseIndex(v1));
            triangleIndices.push_back(parseIndex(v2));
            triangleIndices.push_back(parseIndex(v3));
        }
    }

    return triangleIndices;
}


bool WriteOBJ(const std::string& filename, const SimpleMesh::Mesh& mesh) {
    std::ofstream out(filename);
    if (!out.is_open()) return false;

    // Write vertices
    for (const auto& v : mesh.vertices)
        out << "v " << v.x() << " " << v.y() << " " << v.z() << "\n";

    // Write triangle faces (1-based index)
    for (const auto& tri : mesh.triangles)
        out << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";

    out.close();
    return true;
}

}  // namespace SimpleMeshIO
