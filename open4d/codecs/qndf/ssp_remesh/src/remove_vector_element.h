#ifndef REMOVE_VECTOR_ELEMENT_H
#define REMOVE_VECTOR_ELEMENT_H

#include <Eigen/Core>

void remove_vector_element(
  const int idxToRemove,
  Eigen::VectorXd & vec);

void remove_vector_element(
  const int idxToRemove,
  Eigen::VectorXi & vec);

// template <class M>
// void remove_vector_element(
//   const int & idxToRemove,
//   M & vec);

#endif