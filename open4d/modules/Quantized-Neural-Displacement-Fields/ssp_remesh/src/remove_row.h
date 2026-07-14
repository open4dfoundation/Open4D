#ifndef REMOVE_ROW_H
#define REMOVE_ROW_H

#include <Eigen/Core>

void remove_row(
  const int rowToRemove,
  Eigen::MatrixXd & matrix);

void remove_row(
  const int rowToRemove,
  Eigen::MatrixXi & matrix);
  
#endif