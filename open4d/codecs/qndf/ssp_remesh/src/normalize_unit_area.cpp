#include "normalize_unit_area.h"

void normalize_unit_area(
	Eigen::MatrixXd & V,
	const Eigen::MatrixXi & F)
{
    using namespace Eigen;
    using namespace std;

    VectorXd FA;
    igl::doublearea(V,F,FA);

    double scale = sqrt(FA.sum() / 2);
    V /=  scale; 

    // VectorXd FA_scaled;
    // igl::doublearea(V,F,FA_scaled);
    // cout << FA_scaled.sum() / 2 << endl;

    // for (int ii=0; ii<V.cols(); ii++)
    //     V.col(ii) = V.col(ii).array() - U.col(ii).mean();
     V.col(0) = V.col(0).array() - V.col(0).mean();
     V.col(1) = V.col(1).array() - V.col(1).mean();
     V.col(2) = V.col(2).array() - V.col(2).minCoeff();
}