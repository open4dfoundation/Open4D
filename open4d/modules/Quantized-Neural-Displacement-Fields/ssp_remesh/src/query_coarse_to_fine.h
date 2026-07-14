#ifndef QUERY_COARSE_TO_FINE_H
#define QUERY_COARSE_TO_FINE_H

#include <Eigen/Core>
#include <vector>
#include <single_collapse_data.h>
#include <vector_mod.h>
#include <compute_barycentric.h>
#include <fstream>

#include <igl/min.h>
#include <igl/find.h>
#include <igl/setunion.h>
#include <igl/unique.h>
#include <igl/parallel_for.h>

void query_coarse_to_fine(
  const std::vector<single_collapse_data> & decInfo,
  const Eigen::VectorXi & IM,
  const std::vector<std::vector<int>> & decIM,
  const Eigen::VectorXi & IMF,
  Eigen::MatrixXd & BC,
  Eigen::MatrixXi & BF,
  Eigen::VectorXi & FIdx);
#endif