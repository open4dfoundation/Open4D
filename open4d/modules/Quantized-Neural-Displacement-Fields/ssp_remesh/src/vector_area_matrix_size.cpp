#include "vector_area_matrix_size.h"

void vector_area_matrix_size(
  const Eigen::MatrixXi & F,
  const int nV,
  Eigen::MatrixXd & A)
{
  using namespace Eigen;
  using namespace std;

  // number of vertices
  MatrixXi E;
  igl::boundary_facets(F,E);

  // //Prepare a vector of triplets to set the matrix
  // vector<Triplet<double> > tripletList;
  // tripletList.reserve(4*E.rows());

  // for(int k = 0; k < E.rows(); k++)
  // {
	// 	int i = E(k,0);
	// 	int j = E(k,1);
  //       tripletList.push_back(Triplet<double>(i+n, j, -0.25));
  //       tripletList.push_back(Triplet<double>(j, i+n, -0.25));
  //       tripletList.push_back(Triplet<double>(i, j+n, 0.25));
  //       tripletList.push_back(Triplet<double>(j+n, i, 0.25));
  // }

  // //Set A from triplets (Eigen will sum triplets with same coordinates)
  // A.resize(n * 2, n * 2);
  // A.setFromTriplets(tripletList.begin(), tripletList.end());

  A.resize(nV * 2, nV * 2);
  A.setZero();

  for(int k = 0; k < E.rows(); k++)
  {
		int i = E(k,0);
		int j = E(k,1);
    A(i+nV, j) -= 0.25;
    A(j, i+nV) -= 0.25;
    A(i, j+nV) += 0.25;
    A(j+nV, i) += 0.25;
  }

}
