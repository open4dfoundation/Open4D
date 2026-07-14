#include "get_prolong.h"

void get_prolong(
	const Eigen::MatrixXd & VO,
	const Eigen::MatrixXi & FO,
  const int & tarF,
	const int & dec_type,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::SparseMatrix<double> & P)
{
  using namespace Eigen;
	using namespace std;

  // decimate 
	VectorXi IM, FIM;
	vector<single_collapse_data> decInfo;
	vector<vector<int>> decIM;
	VectorXi J; 
	SSP_decimate(VO,FO,tarF, dec_type, V,F,J, IM, decInfo, decIM, FIM);

  // get indices to query 
	MatrixXd BC(VO.rows(),3); BC.setZero();
	MatrixXi BF(VO.rows(),3); BF.setZero();
	VectorXi FIdx(VO.rows()); FIdx.setZero();
	// get all the find point barycentric (looks like [1,0,0])
	for (int fIdx=0; fIdx<FO.rows(); fIdx++)
	{
		for (int ii = 0; ii<FO.cols(); ii++)
		{
			int vIdx = FO(fIdx,ii);
			if (BC.row(vIdx).sum() == 0.0)
			{
				BC(vIdx,ii) = 1;
				BF.row(vIdx) = FO.row(fIdx);
				FIdx(vIdx) = fIdx;	
			}
		}
	}

  // query fine vertices to the coarse
  query_fine_to_coarse(decInfo, IM, decIM, FIM, BC, BF, FIdx);

  // assemble P
  vector<Triplet<double>> IJV;
  IJV.reserve(BC.rows() * BC.cols());

  for (int c = 0; c < BC.cols(); c++)
  {
    for (int r =0; r < BC.rows(); r++)
    {
      IJV.push_back(Triplet<double>(r, BF(r,c), BC(r,c)));
    }
  }
  P.resize(VO.rows(), V.rows());
  P.setFromTriplets(IJV.begin(), IJV.end());
}

void get_prolong_block(
	const Eigen::MatrixXd & VO,
	const Eigen::MatrixXi & FO,
  const int & tarF,
	const int & dec_type,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::SparseMatrix<double> & P)
{
  using namespace Eigen;
	using namespace std;

  // decimate 
	VectorXi IM, FIM;
	vector<single_collapse_data> decInfo;
	vector<vector<int>> decIM;
	VectorXi J; 
	SSP_decimate(VO,FO,tarF, dec_type, V,F,J, IM, decInfo, decIM, FIM);

  // get indices to query 
	MatrixXd BC(VO.rows(),3); BC.setZero();
	MatrixXi BF(VO.rows(),3); BF.setZero();
	VectorXi FIdx(VO.rows()); FIdx.setZero();
	// get all the find point barycentric (looks like [1,0,0])
	for (int fIdx=0; fIdx<FO.rows(); fIdx++)
	{
		for (int ii = 0; ii<FO.cols(); ii++)
		{
			int vIdx = FO(fIdx,ii);
			if (BC.row(vIdx).sum() == 0.0)
			{
				BC(vIdx,ii) = 1;
				BF.row(vIdx) = FO.row(fIdx);
				FIdx(vIdx) = fIdx;	
			}
		}
	}

  // query fine vertices to the coarse
  query_fine_to_coarse(decInfo, IM, decIM, FIM, BC, BF, FIdx);

	// assemble P
  vector<Triplet<double>> IJV;
  IJV.reserve(3 * BC.rows() * BC.cols());

  for (int c = 0; c < BC.cols(); c++)
  {
    for (int r =0; r < BC.rows(); r++)
    {
      IJV.push_back(Triplet<double>(3*r  , 3*BF(r,c)  , BC(r,c)));
			IJV.push_back(Triplet<double>(3*r+1, 3*BF(r,c)+1, BC(r,c)));
			IJV.push_back(Triplet<double>(3*r+2, 3*BF(r,c)+2, BC(r,c)));
    }
  }
  P.resize(3*VO.rows(), 3*V.rows());
  P.setFromTriplets(IJV.begin(), IJV.end());
}