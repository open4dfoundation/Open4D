#ifndef COTMATRIX_DENSE_H
#define COTMATRIX_DENSE_H

#include <igl/cotmatrix_entries.h>

#include <Eigen/Core>

// compute cotangen laplacian based on
// "Polygon Laplacian Made Simple" [Bunge et al. 2020]
//
// Note: this outputs a dense cotmatrix, suitable for extremely small meshes
//
// Inputs:
//   V  #V by 3 list of mesh vertex positions
//   F  #F by 3 list of mesh elements 
// Outputs:
//   A  #V by #V laplacian matrix
void cotmatrix_dense(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & A);

#endif