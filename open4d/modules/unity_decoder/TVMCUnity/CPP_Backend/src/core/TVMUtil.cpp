#include "TVMUtil.h"
#include "SimpleMesh.h"
#include "TVMLogger.h"
#include <cmath>
#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <unordered_set>
#include <algorithm>

namespace TVMUtil {
Eigen::MatrixXd SolveLeastSquares(const Eigen::SparseMatrix<double>& L_star,
                                  const Eigen::MatrixXd& D_hat,
                                  int maxIter,
                                  double tol) {
    const int numCols = D_hat.cols();     // Typically 4
    const int numVars = L_star.cols();    // Number of vertices

    //Make new solver with desired parameters
    Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<double>> solver;
    solver.setMaxIterations(maxIter);
    solver.setTolerance(tol);

    Eigen::MatrixXd X(numVars, numCols);

    for (int i = 0; i < numCols; ++i) { //Loop and solve sparse squares for each row.
        solver.compute(L_star);
        if (solver.info() != Eigen::Success) {
            throw std::runtime_error("LSCG failed to initialize on column " + std::to_string(i));
        }

        X.col(i) = solver.solve(D_hat.col(i));
        if (solver.info() != Eigen::Success) {
            LOG_ERROR("[LSCG] Solver failed on column ", i);
            throw std::runtime_error("LSCG solve failed on column " + std::to_string(i));
        }
        //Print stats. Typical error ~ 9e-07.
        LOG_INFO( "[LSCG-DOUBLE] Column ", i, " - Iterations: ",solver.iterations(), ", Estimated error: ", solver.error());
    }

    return X;
}

//Compute the mean value weights for the lapacian matrix
Eigen::SparseMatrix<double> TVMUtil::ComputeMeanValueWeights(
    const std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>& vertices,
    const std::vector<std::unordered_set<int>>& adjacency_list)
{
    int n = vertices.size();
    std::vector<Eigen::Triplet<double>> triplets;
    for (int i = 0; i < n; ++i) {
        //Sort neighbors to ensure deterministic ordering
        auto neighbors = std::vector<int>(adjacency_list[i].begin(), adjacency_list[i].end());
        std::sort(neighbors.begin(), neighbors.end());

        int degree = neighbors.size();
        if (degree < 2) continue;

        for (int j = 0; j < degree; ++j) {
            int curr = neighbors[j];
            int prev = neighbors[(j - 1 + degree) % degree];
            int next = neighbors[(j + 1) % degree];
            Eigen::Vector3d u = (vertices[curr] - vertices[i]).normalized();
            Eigen::Vector3d u1 = (vertices[prev] - vertices[i]).normalized();
            Eigen::Vector3d u2 = (vertices[next] - vertices[i]).normalized();
            double angle1 = std::acos(std::clamp(u.dot(u1), -1.0, 1.0));
            double angle2 = std::acos(std::clamp(u.dot(u2), -1.0, 1.0));
            double w = (std::tan(angle1 / 2) + std::tan(angle2 / 2)) / (vertices[curr] - vertices[i]).norm();
            if (std::isfinite(w)) triplets.emplace_back(i, curr, w);
        }
    }
    Eigen::SparseMatrix<double> W(n, n);
    W.setFromTriplets(triplets.begin(), triplets.end());
    return W;
}

//Build the laplacian matrix.
Eigen::SparseMatrix<double> TVMUtil::BuildLaplacianMatrix(
    const SimpleMesh::Mesh& mesh,
    const std::vector<int>& anchorIndices) {

    const int numVertices = static_cast<int>(mesh.vertices.size());
    const auto& adjList = mesh.adjacency_list;

    LOG_INFO("[LAPLACIAN] Building Laplacian for mesh with ", numVertices, " vertices\n");
    LOG_INFO( "[LAPLACIAN] Anchor count: ", anchorIndices.size());

    //Convert mesh vertices to aligned Eigen vector
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> vertices;
    vertices.reserve(numVertices);
    for (const auto& v : mesh.vertices)
        vertices.emplace_back(v.x(), v.y(), v.z());

    //Compute mean value weight matrix W
    Eigen::SparseMatrix<double> W = ComputeMeanValueWeights(vertices, adjList);
    LOG_INFO("[LAPLACIAN] Weight matrix W: ", W.rows(), " x ", W.cols(),", nnz = ", W.nonZeros());

    //Normalize W by row sums → D^-1 * W
    Eigen::VectorXd rowSums = W * Eigen::VectorXd::Ones(numVertices);

    std::vector<Eigen::Triplet<double>> dTriplets;
    for (int i = 0; i < numVertices; ++i) {
        if (rowSums[i] > 1e-8f)
            dTriplets.emplace_back(i, i, 1.0f / rowSums[i]);
    }

    Eigen::SparseMatrix<double> D_inv(numVertices, numVertices);
    D_inv.setFromTriplets(dTriplets.begin(), dTriplets.end());

    //Laplacian L = I - D^-1 * W
    Eigen::SparseMatrix<double> I(numVertices, numVertices);
    I.setIdentity();

    Eigen::SparseMatrix<double> L = I - D_inv * W;
    LOG_INFO("[LAPLACIAN] Laplacian matrix L: ", L.rows(), " x ", L.cols() ,", nnz = ", L.nonZeros());

    //Construct anchor constraint matrix A
    Eigen::SparseMatrix<double> A(static_cast<int>(anchorIndices.size()), numVertices);
    std::vector<Eigen::Triplet<double>> anchorTriplets;
    for (int i = 0; i < static_cast<int>(anchorIndices.size()); ++i) {
        if (anchorIndices[i] >= 0 && anchorIndices[i] < numVertices) {
            anchorTriplets.emplace_back(i, anchorIndices[i], 1.0f);
        } else {
            LOG_WARN("[WARNING] Anchor index out of range: ", anchorIndices[i]);
        }
    }
    A.setFromTriplets(anchorTriplets.begin(), anchorTriplets.end());
    LOG_INFO("[LAPLACIAN] Anchor matrix A: ", A.rows(), " x ", A.cols(), ", nnz = ", A.nonZeros());

    //Stack L and A vertically → L* = [L; A]
    const int totalRows = L.rows() + A.rows();
    std::vector<Eigen::Triplet<double>> LstarTriplets;
    LstarTriplets.reserve(L.nonZeros() + A.nonZeros());

    for (int k = 0; k < L.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(L, k); it; ++it) {
            LstarTriplets.emplace_back(it.row(), it.col(), it.value());
        }
    }
    for (int k = 0; k < A.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, k); it; ++it) {
            LstarTriplets.emplace_back(it.row() + L.rows(), it.col(), it.value());
        }
    }

    Eigen::SparseMatrix<double> L_star(totalRows, numVertices);
    L_star.setFromTriplets(LstarTriplets.begin(), LstarTriplets.end());
    L_star.makeCompressed();

    LOG_INFO("[LAPLACIAN] Final L* matrix: ", L_star.rows(), " x ", L_star.cols(), ", nnz = ", L_star.nonZeros());

    return L_star;
}

//Apply frame offsets stored in TMatrix
Eigen::MatrixXd TVMUtil::ApplyTMatrixOffset(
    const Eigen::MatrixXd& vertexDisplacements,
    const Eigen::MatrixXd& frameTranslations)
{
    Eigen::MatrixXd result = vertexDisplacements;
    int numFrames = frameTranslations.cols() / 3;
    int numVertices = vertexDisplacements.rows();

    //Make sure the provided displacements are the right shape.
    if (frameTranslations.rows() != 1 || frameTranslations.cols()  != vertexDisplacements.cols()) {
        throw std::runtime_error("T_matrix must be shape (1, 3 * numFrames)");
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        int colStart = frame * 3;
        Eigen::Vector3d offset( //Collecting the frame offset for the frame.
            frameTranslations(0, colStart + 0),
            frameTranslations(0, colStart + 1),
            frameTranslations(0, colStart + 2));

        for (int v = 0; v < numVertices; ++v) { //Applying offset to each vertice.
            result(v, colStart + 0) += offset.x();
            result(v, colStart + 1) += offset.y();
            result(v, colStart + 2) += offset.z();
        }
    }

    return result;
}
}//TVMUtil namespace
