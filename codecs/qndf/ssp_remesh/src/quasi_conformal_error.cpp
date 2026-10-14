#include "quasi_conformal_error.h"

void quasi_conformal_error(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const Eigen::MatrixXd & U, // UV
  Eigen::VectorXd & error)
{
  // reference: "Texture Mapping Progressive Meshes"
  using namespace Eigen;
  using namespace std;

  error.resize(F.rows());
  for (int ff = 0; ff<F.rows(); ff++)
  {
    double s1 = U(F(ff,0),0);
    double s2 = U(F(ff,1),0);
    double s3 = U(F(ff,2),0);

    double t1 = U(F(ff,0),1);
    double t2 = U(F(ff,1),1);
    double t3 = U(F(ff,2),1);

    VectorXd q1 = V.row(F(ff,0));
    VectorXd q2 = V.row(F(ff,1));
    VectorXd q3 = V.row(F(ff,2));

    double A = ((s2-s1)*(t3-t1) - (s3-s1)*(t2-t1)) / 2;
    VectorXd Ss = (q1*(t2-t3) + q2*(t3-t1) + q3*(t1-t2)) / (2*A);
    VectorXd St = (q1*(s3-s2) + q2*(s1-s3) + q3*(s2-s1)) / (2*A);

    double a = Ss.transpose() * Ss;
    double b = Ss.transpose() * St;
    double c = St.transpose() * St;

    double sigma = sqrt( (a+c+sqrt((a-c)*(a-c) + 4*b*b)) / 2 );
    double gamma = sqrt( (a+c-sqrt((a-c)*(a-c) + 4*b*b)) / 2 );

    error(ff) = sigma / gamma;
  }
}