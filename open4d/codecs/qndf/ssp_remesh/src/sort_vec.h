#ifndef SORT_VEC_H
#define SORT_VEC_H

#include<algorithm>
#include<Eigen/Core>

void sort_vec(
  const Eigen::VectorXd & vec, 
  Eigen::VectorXd & sorted_vec,  
  Eigen::VectorXi & ind);

void sort_vec(
  const Eigen::VectorXi & vec, 
  Eigen::VectorXi & sorted_vec,  
  Eigen::VectorXi & ind);

#endif