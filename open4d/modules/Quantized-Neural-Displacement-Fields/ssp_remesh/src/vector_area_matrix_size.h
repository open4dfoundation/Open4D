#ifndef VECTOR_AREA_MATRIX_SIZE_H
#define VECTOR_AREA_MATRIX_SIZE_H

#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/Sparse>

#include <igl/boundary_facets.h>
#include <vector>
#include <iostream>

void vector_area_matrix_size(
  const Eigen::MatrixXi & F,
  const int nV,
  Eigen::MatrixXd & A);

#endif