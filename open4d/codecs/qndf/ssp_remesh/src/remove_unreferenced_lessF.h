#ifndef REMOVE_UNREFERENCED_LESSF_H
#define REMOVE_UNREFERENCED_LESSF_H

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <algorithm>
#include <vector>
#include <iostream>
#include <igl/slice.h>
#include <map>

  // Remove unreferenced vertices from V, updating F accordingly
  //
  // Input:
  //   V  #V by dim list of mesh vertex positions
  //   F  #F by ss list of simplices (Values of -1 are quitely skipped)
  // Outputs:
  //   RV  #RV by dim list of mesh vertex positions
  //   RF  #RF by ss list of simplices
  //   IM   #V by 1 list of indices such that: NF = IM(F) and NT = IM(T)
  //      and V(find(IM<=size(NV,1)),:) = NV
  //   subsetVIdx  #RV by 1 list, such that NV = V(J,:)
  //   
  //  Note that this is fast if F.rows() is small, if not, we should use "igl::remove_unreferenced"

void remove_unreferenced_lessF(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & RV,
  Eigen::MatrixXi & RF,
  std::map<int, int> & IM,
  Eigen::VectorXi & subsetVIdx);

#endif