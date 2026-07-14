#include "remove_unreferenced_lessF.h"

void remove_unreferenced_lessF(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & RV,
  Eigen::MatrixXi & RF,
  std::map<int, int> & IM,
  Eigen::VectorXi & subsetVIdx)
{    
  using namespace std;
  using namespace Eigen;

  // compute the unique vertex indices (same is igl::unique, but faster)
  {
    // sort all the indices
    VectorXi F_flap  = Map<const VectorXi>(F.data(), F.size());
    sort(F_flap.data(), F_flap.data()+F_flap.size());

    // extract the unique ones
    vector<int> subsetVIdx_vec;
    subsetVIdx_vec.reserve(F_flap.size());
    for (int ii = 0; ii<F_flap.size(); ii++)
    {
      if (ii == 0)
        subsetVIdx_vec.push_back(F_flap(ii));
      else
      {
        if (subsetVIdx_vec[subsetVIdx_vec.size() - 1] != F_flap(ii))
          subsetVIdx_vec.push_back(F_flap(ii));
      }
    }

    // std::vector to Eigen::Vector
    subsetVIdx.resize(subsetVIdx_vec.size());
    subsetVIdx = Map<VectorXi, Unaligned>(subsetVIdx_vec.data(), subsetVIdx_vec.size());
  }

  // assemble the sparse vector index map
  {
    IM.clear();
    for(int ii = 0; ii < subsetVIdx.size(); ii++)
      IM[subsetVIdx(ii)] = ii;
  }

  // get output mesh
  RF.resize(F.rows(), F.cols());
  {
    for (int r=0; r<RF.rows(); r++)
    {
      for (int c=0; c<RF.cols(); c++)
      {
        RF(r,c) = IM[F(r,c)];
      }
    }
  }
  
  igl::slice(V,subsetVIdx,1,RV);

  
}