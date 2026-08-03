#ifndef VECTOR_MOD_H
#define VECTOR_MOD_H

#include <Eigen/Core>

void vector_mod(
  const Eigen::VectorXi & input,
  const int & number,
  Eigen::VectorXi & input_mod_number);
#endif
