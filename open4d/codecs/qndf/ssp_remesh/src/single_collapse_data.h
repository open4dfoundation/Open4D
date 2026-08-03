#ifndef SINGEL_COLLAPSE_DATA_H
#define SINGEL_COLLAPSE_DATA_H

#include <vector>
#include <Eigen/Core>

struct single_collapse_data
{
  Eigen::VectorXi b, FIdx_pre, FIdx_post;
  Eigen::VectorXi subsetVIdx;
  Eigen::MatrixXd UV_pre, UV_post, V_pre, V_post;
  Eigen::MatrixXi FUV_pre, FUV_post;  
  std::vector<int> Nsv, Ndv;
};

#endif
