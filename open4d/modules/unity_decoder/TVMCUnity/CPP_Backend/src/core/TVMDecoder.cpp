#include "TVMDecoder.h"
#include "SimpleMesh.h"
#include "SimpleMeshIO.h"
#include "MatrixIO.h"
#include "TVMLogger.h"
#include "TVMUtil.h"
#include <filesystem>
#include <cmath>
#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <vector>
#include <stdexcept>

namespace TVMDecoder {

Decoder::Decoder(const std::string& name) {
    decoderName = name;
}

Decoder::~Decoder() {
    Clear();
}

void Decoder::Clear() {
    LOG_INFO("[Decoder] 🔄 Clearing decoder state: %s", decoderName.c_str());
    decodedFrames.clear(); decodedFrames.shrink_to_fit();
    decodedVertexBuffer.clear(); decodedVertexBuffer.shrink_to_fit();
    referenceVertexBuffer.clear(); referenceVertexBuffer.shrink_to_fit();
    triangleIndicesFlat.clear(); triangleIndicesFlat.shrink_to_fit();
    anchor_indices.clear(); anchor_indices.shrink_to_fit();

    Eigen::MatrixXd().swap(dHat);
    Eigen::MatrixXd().swap(bMatrix);
    Eigen::MatrixXd().swap(tMatrix);
    Eigen::MatrixXd().swap(S_hat);
    Eigen::MatrixXd().swap(T_hat);
    Eigen::MatrixXd().swap(tMean);
    Eigen::SparseMatrix<double>().swap(l_star);

    decodedReferenceMesh.vertices.clear();
    decodedReferenceMesh.vertices.shrink_to_fit();
    decodedReferenceMesh.triangles.clear();
    decodedReferenceMesh.triangles.shrink_to_fit();
    decodedReferenceMesh.adjacency_list.clear();
    decodedReferenceMesh.adjacency_list.shrink_to_fit();

    totalFrames = 0;
    verticesPerFrame = 0;
    isDecoded = false;

    LOG_INFO("[Decoder] ✅ Clear complete for decoder: %s", decoderName.c_str());
}


void Decoder::LoadSequence(const std::string& directoryPath) {

    if (directoryPath.empty()) {
        LOG_ERROR("[Decoder] ❌ File path is empty!");
    }
    LOG_INFO("[Decoder] Paths received: %s", directoryPath.c_str());
    std::filesystem::path dirPath(directoryPath);
    std::string meshFile = (dirPath / "decoded_decimated_reference_mesh_subdivided.obj").string();
    std::string dHatFile = (dirPath / "delta_trajectories.bin").string();
    std::string tMatrixFile = (dirPath / "T_matrix.txt").string();
    std::string bMatrixFile = (dirPath / "B_matrix.txt").string();

    try {
        // Load reference mesh
        SimpleMeshIO::ReadOBJ(meshFile, decodedReferenceMesh);
        triangleIndicesFlat = SimpleMeshIO::LoadTriangleIndicesFlat(meshFile);
        decodedReferenceMesh.ComputeAdjacencyList();
        LOG_INFO("[Decoder] ✅ Loaded reference mesh");

        // Load dHat
        dHat = MatrixIO::LoadDeltaTrajectories(dHatFile);
        LOG_INFO("[DEBUG] dHat from file - first values: %f, %f, %f", dHat(0,0), dHat(0,1), dHat(0,2));
        LOG_INFO("[DEBUG] dHat from file - row 100: %f, %f, %f", dHat(100,0), dHat(100,1), dHat(100,2));
        LOG_INFO("[Decoder] ✅ Loaded dHat: %d x %d",
                 static_cast<int>(dHat.rows()), static_cast<int>(dHat.cols()));

        // Load matrices
        bMatrix = MatrixIO::loadtxt(bMatrixFile);
        LOG_INFO("[Decoder] ✅ Loaded B_matrix");

        tMatrix = MatrixIO::loadtxt(tMatrixFile);
        LOG_INFO("[Decoder] ✅ Loaded T_matrix");

    } catch (const std::exception& e) {
        LOG_ERROR("[Decoder] ❌ Failed to load sequence: %s", e.what());
    }
}
bool Decoder::DecodeSequence(){
    int refCount = decodedReferenceMesh.vertices.size();
    int totalRows = dHat.rows();
    int anchorCount = totalRows - refCount;
    anchor_indices.clear();
    for (int i = 0; i < anchorCount; ++i) {
        anchor_indices.push_back(std::round(i * (decodedReferenceMesh.vertices.size() - 1.0) / (anchorCount - 1.0)));
    }
    Eigen::MatrixXd D_regular = dHat.topRows(refCount);
    Eigen::MatrixXd D_anchor = dHat.bottomRows(anchorCount);
    l_star = TVMUtil::BuildLaplacianMatrix(decodedReferenceMesh, anchor_indices);
    LOG_INFO("[Decoder] ✅ Constructed L_star");

    Eigen::MatrixXd rhs(l_star.rows(), dHat.cols());
    rhs.topRows(refCount) = D_regular;
    rhs.bottomRows(anchorCount) = D_anchor;

    S_hat = TVMUtil::SolveLeastSquares(l_star, rhs, 500, 1e-6);
    return ProcessLoadedData();
}

// Common processing logic after data is loaded
bool Decoder::ProcessLoadedData() {
    LOG_INFO("[Decoder] Loaded Matrix Shapes:");
    LOG_INFO("  S_hat: %d x %d", static_cast<int>(S_hat.rows()), static_cast<int>(S_hat.cols()));
    LOG_INFO("  B_matrix: %d x %d", static_cast<int>(bMatrix.rows()), static_cast<int>(bMatrix.cols()));
    LOG_INFO("  T_matrix: %d x %d", static_cast<int>(tMatrix.rows()), static_cast<int>(tMatrix.cols()));

    // Compute S * B + T
    Eigen::MatrixXd sb = S_hat * bMatrix;
    T_hat = TVMUtil::ApplyTMatrixOffset(sb, tMatrix);

    // Set sequence values
    totalFrames = bMatrix.cols() / 3;
    verticesPerFrame = decodedReferenceMesh.vertices.size();
    decodedVertexBuffer.resize(totalFrames * verticesPerFrame * 3);
    decodedFrames.clear();
    decodedFrames.reserve(totalFrames);

    // Cache all frame displacements
    for (int i = 0; i < totalFrames; ++i) {
        int colStart = i * 3;
        Eigen::MatrixXd disp = T_hat.block(0, colStart, verticesPerFrame, 3);

        std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> frameDisplacements(verticesPerFrame);
        for (int v = 0; v < verticesPerFrame; ++v) {
            frameDisplacements[v] = disp.row(v).transpose();
            size_t offset = (i * verticesPerFrame + v) * 3;
            decodedVertexBuffer[offset + 0] = frameDisplacements[v].x();
            decodedVertexBuffer[offset + 1] = frameDisplacements[v].y();
            decodedVertexBuffer[offset + 2] = frameDisplacements[v].z();
        }
        decodedFrames.push_back(std::move(frameDisplacements));
    }

    LOG_INFO("[Decoder] ✅ Decoded and cached %d frames", totalFrames);

    // Store reference vertices
    referenceVertexBuffer.clear();
    referenceVertexBuffer.reserve(verticesPerFrame * 3);
    for (const auto& v : decodedReferenceMesh.vertices) {
        referenceVertexBuffer.push_back(v.x());
        referenceVertexBuffer.push_back(v.y());
        referenceVertexBuffer.push_back(v.z());
    }

    isDecoded = true;
    return true;
}

std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>
Decoder::ApplyDisplacementToFrame(int frameIndex) const {
    if (!isDecoded) {
        throw std::runtime_error("Sequence hasn't been loaded yet.");
    }
    if (frameIndex < 0 || frameIndex >= decodedFrames.size()) {
        throw std::out_of_range("Invalid frame index in ApplyDisplacementToFrame");
    }

    const auto& baseVerts = decodedReferenceMesh.vertices;
    const auto& displacements = decodedFrames[frameIndex];

    if (baseVerts.size() != displacements.size()) {
        throw std::runtime_error("Mismatch in base and displacement vector sizes.");
    }

    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> deformedVerts(baseVerts.size());

    for (size_t i = 0; i < baseVerts.size(); ++i) {
        deformedVerts[i] = baseVerts[i] + displacements[i];
    }

    return deformedVerts;
}

} // namespace TVMDecoder
