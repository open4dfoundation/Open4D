#include "query_fine_to_coarse.h"

void query_fine_to_coarse(
  const std::vector<single_collapse_data> & decInfo,
  const Eigen::VectorXi & IM,
  const std::vector<std::vector<int>> & decIM,
  const Eigen::VectorXi & FIM,
  Eigen::MatrixXd & BC,
  Eigen::MatrixXi & BF,
  Eigen::VectorXi & FIdx)
{
  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  int numQuery = BF.rows();
  int decLength = decInfo.size();

  VectorXi queryCounts(BC.rows());
  queryCounts.setZero();

  // for (int qIdx=0; qIdx<numQuery; qIdx++)
  igl::parallel_for(
    numQuery,
    [&verbose, &FIdx, &BC, &BF, &decIM, &decInfo, &queryCounts](const int qIdx)
  {
    // print progress
    // if (qIdx % 10000 == 0) 
      // cout << "fine to coarse : " << qIdx << "/" << BF.rows() << endl;

    // go through the decInfo 
    int dIdx = -1;

    while (true)
    {
      // get query FIdx 
      int queryFIdx = FIdx(qIdx);

      // find the dIdx
      vector<int> dIdxList = decIM[queryFIdx];
      bool if_find_dIdx = false;
      for (int ii=0; ii<dIdxList.size(); ii++)
      {
        if (dIdx < dIdxList[ii])
        {
          dIdx = dIdxList[ii];
          if_find_dIdx = true;
          break;
        }
      }

      // if not dIdx left, break
      if (!if_find_dIdx)
        break;

      ///////// start query /////////
      if (verbose)
        cout << "qIdx: " << qIdx << ", dIdx: " << dIdx << endl; 

      // get vi and vj
      int vi = decInfo[dIdx].subsetVIdx(decInfo[dIdx].b(0));
      int vj = decInfo[dIdx].subsetVIdx(decInfo[dIdx].b(1));

      VectorXi f = BF.row(qIdx);

      // find f in subsetVIdx
      int v0, v1, v2; // such that subsetVIdx(v0) == f(0)
      {
        // PROFC_NODE("query: find local vIdx");
        VectorXi v0_vec, v1_vec, v2_vec;
        igl::find((decInfo[dIdx].subsetVIdx.array() == f(0)).eval(), v0_vec);
        igl::find((decInfo[dIdx].subsetVIdx.array() == f(1)).eval(), v1_vec);
        igl::find((decInfo[dIdx].subsetVIdx.array() == f(2)).eval(), v2_vec);

        assert(v0_vec.size() == 1);
        assert(v1_vec.size() == 1);
        assert(v2_vec.size() == 1);
      
        v0 = v0_vec(0);
        v1 = v1_vec(0);
        v2 = v2_vec(0);
      }

      // get query UV
      VectorXd queryUV = 
          BC(qIdx,0) * decInfo[dIdx].UV_pre.row(v0)  
        + BC(qIdx,1) * decInfo[dIdx].UV_pre.row(v1)
        + BC(qIdx,2) * decInfo[dIdx].UV_pre.row(v2);

      Eigen::MatrixXd B;
      {
        // PROFC_NODE("query: compute barycentric");
        compute_barycentric(queryUV, decInfo[dIdx].UV_post, decInfo[dIdx].FUV_post,B);
        if (verbose)
          cout << "B: \n" << B << endl; 
      }

      // snap to the closest one
      // TODO: update this snapping operation (currently it is not robust)
      VectorXd distToValid = -B.rowwise().minCoeff();
      double minD = 1.0;
      int idxToFUV;
      for (int bb=0;bb<distToValid.size(); bb++)
      { 
        if (distToValid(bb) < minD)
        {
          minD = distToValid(bb);
          idxToFUV = bb;
        }
      }

      // avoid numerical error of barycentric coordinate
      B(idxToFUV, 0) = max(0.0, B(idxToFUV, 0));
      B(idxToFUV, 1) = max(0.0, B(idxToFUV, 1));
      B(idxToFUV, 2) = max(0.0, B(idxToFUV, 2));
      B.row(idxToFUV) = B.row(idxToFUV).array() / B.row(idxToFUV).sum();

      BC.row(qIdx) = B.row(idxToFUV);

      BF(qIdx, 0) = decInfo[dIdx].subsetVIdx(decInfo[dIdx].FUV_post(idxToFUV,0));
      BF(qIdx, 1) = decInfo[dIdx].subsetVIdx(decInfo[dIdx].FUV_post(idxToFUV,1));
      BF(qIdx, 2) = decInfo[dIdx].subsetVIdx(decInfo[dIdx].FUV_post(idxToFUV,2));
      FIdx(qIdx) = decInfo[dIdx].FIdx_post(idxToFUV);
      
    }
  }
  ,1000);
  
  if (verbose)
    cout << "finish query points\n";

  // convert vertex indices from VO to V
  {
    VectorXi indexMap(IM.maxCoeff()+1);
    indexMap.setZero();
    for (int ii=0; ii<IM.size(); ii++)
      indexMap(IM(ii)) = ii;

    // change BF to be on coarse mesh indices
    for (int r=0; r<BF.rows(); r++){
      for (int c=0; c<BF.cols(); c++){
        BF(r,c) = indexMap(BF(r,c));
      }
    }
  }

  // convert face indices
  {
    for (int ii=0; ii<FIdx.size(); ii++){
      FIdx(ii) = FIM(FIdx(ii));
    }
  }



}