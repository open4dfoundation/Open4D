#ifndef COMPUTE_BARYCENTRIC_h
#define COMPUTE_BARYCENTRIC_h

#include <Eigen/Core>
#include <iostream>
#include <math.h> 

// Compute the barycentric coordinate of a 2D query point "p" with respect to all the elements in a 2D triangle mesh (UV, F). 

// Inputs:
// p   location of the query point in 2D
// UV  #V-by-2 vertices of the 2D triangle mesh
// F   #F-by-3 faces of the 2D triangle mesh

// Outputs:
// B   #F-by-3 barycentric coordinates of p wrt each face F

void compute_barycentric(
  const Eigen::VectorXd & p,
  const Eigen::MatrixXd & UV,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & B);
#endif
