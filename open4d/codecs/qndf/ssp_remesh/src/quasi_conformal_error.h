#ifndef QUASI_CONFORMAL_ERROR_H
#define QUASI_CONFORMAL_ERROR_H

#include <Eigen/Core>
#include <math.h> 

void quasi_conformal_error(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const Eigen::MatrixXd & U, // UV
  Eigen::VectorXd & error);
#endif