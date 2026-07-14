#ifndef INTERSECT_ORDERED_H
#define INTERSECT_ORDERED_H

#include <Eigen/Core>
#include <vector>

void intersect_ordered(
  const Eigen::VectorXi & A,
  const Eigen::VectorXi & B,
  Eigen::VectorXi & C,
  Eigen::VectorXi & AinB);

// template <class M>
// void remove_vector_element(
//   const int & idxToRemove,
//   M & vec);

#endif