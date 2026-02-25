#pragma once

#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <unordered_set>
#include "SimpleMesh.h"


namespace TVMUtil
{
/**
* @brief SolveLeastSquares: Solves a sparse least squares problem using the Least Squares Conjugate Gradient method.
*        Solves for X in L_star * X ≈ D_hat using Eigen's LSCG solver, typically where
*        L_star is the Laplacian + anchor constraint matrix.
* @param L_star: The constraint matrix (Laplacian + anchors).
* @param D_hat: A matrex with the loaded d_hat displacements.
* @param maxIter: An integer to set the maximum number of solver iterations.
* @param tol: A double to set the solver's convergence tolerance.
* @return Eigen::MatrixXd Solution matrix X of size (n x k).
*/
Eigen::MatrixXd SolveLeastSquares(const Eigen::SparseMatrix<double>& L_star,
                                  const Eigen::MatrixXd& D_hat,
                                  int maxIter,
                                  double tol);

/**
 * @brief ComputeMeanValueWeights: Computes the mean value weight matrix used in Laplacian construction.
 *        Computes weights between connected vertices based on mean value coordinates,
 *        used to construct the Laplacian operator for mesh smoothing and reconstruction.
 * @param vertices: A List of mesh vertices as Eigen::Vector3d.
 * @param adjacency_list: Adjacency list mapping each vertex to its neighbors.
 * @return A sparse matrix with the calculated weights.
 */
Eigen::SparseMatrix<double> ComputeMeanValueWeights(const std::vector<Eigen::Vector3d,
                                                    Eigen::aligned_allocator<Eigen::Vector3d>>& vertices,
                                                    const std::vector<std::unordered_set<int>>& adjacency_list);


/**
 * @brief BuildLaplacianMatrix: Builds a Laplacian matrix with optional anchor constraints.
 *       Constructs the L* matrix by stacking the Laplacian matrix L with anchor constraint matrix A.
 * @param mesh: Input simple mesh object containing vertices and adjacency list.
 * @param anchorIndices: Integer vector with the indices of anchor vertices to be constrained.
 * @return A matrix with the combined L* matrix (L stacked with A).
 */
Eigen::SparseMatrix<double> BuildLaplacianMatrix(const SimpleMesh::Mesh& mesh,
                                                 const std::vector<int>& anchorIndices);

/**
 * @brief ApplyTMatrixOffset: Applies per-frame global translation offsets stored in the TMatrix
 *                            to a frame's displacement matrix.
 * @param vertexDisplacements: Matrix of displacements of shape (numVertices, 3 * numFrames).
 * @param frameTranslations: Matrix of frame-wise translations of shape (1, 3 * numFrames).
 * @return Eigen::MatrixXd: Updated displacement matrix with T_matrix offsets applied.
 */
Eigen::MatrixXd ApplyTMatrixOffset(const Eigen::MatrixXd& vertexDisplacements,
                                   const Eigen::MatrixXd& frameTranslations);

}// namespace TVMUtil


