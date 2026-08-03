#include "cotmatrix_dense.h"

void cotmatrix_dense(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & A)
{
  using namespace Eigen;
  
  // dense cotmatrix
	MatrixXd C;
  igl::cotmatrix_entries(V,F,C);

	// get edge
	MatrixXi edges;
	edges.resize(3,2);
	edges << 
		1,2,
		2,0,
		0,1;
	
	// init dense cotmatrix A
	A.resize(V.rows(), V.rows());
	A.setZero();

  // Loop over triangles
  for(int ii = 0; ii < F.rows(); ii++)
  {
    // loop over edges of element
    for(int e = 0;e<edges.rows();e++)
    {
      int source = F(ii,edges(e,0));
      int dest = F(ii,edges(e,1));
			A(source,dest) += C(ii,e);
			A(dest,source) += C(ii,e);
			A(source,source) -= C(ii,e);
			A(dest,dest) -= C(ii,e);
    }
  }
}
