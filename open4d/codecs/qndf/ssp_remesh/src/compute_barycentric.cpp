#include "compute_barycentric.h"

void compute_barycentric(
  const Eigen::VectorXd & p,
  const Eigen::MatrixXd & UV,
  const Eigen::MatrixXi & F,
  Eigen::MatrixXd & B)
{
  using namespace Eigen;
  using namespace std;

  bool isDebug = true;

  assert(p.size() == 2);
  assert(UV.cols() == 2);

  MatrixXd a, b, c; // a = UV(F(:,0),:)
  a.resize(F.rows(), 2);
  b.resize(F.rows(), 2);
  c.resize(F.rows(), 2);
  {
    VectorXi F0 = F.col(0);
    VectorXi F1 = F.col(1);
    VectorXi F2 = F.col(2);
    for (int ii=0; ii<F.rows(); ii++)
    {
      a.row(ii) = UV.row(F0(ii));
      b.row(ii) = UV.row(F1(ii));
      c.row(ii) = UV.row(F2(ii));
    }
  }

  MatrixXd v0 = b - a;
  MatrixXd v1 = c - a;
  MatrixXd v2 = -a;
  v2.col(0) = v2.col(0).array() + p(0);
  v2.col(1) = v2.col(1).array() + p(1);

  // cout << "-a: " << -a << endl;
  // cout << "v2: " << v2 << endl;
  // cout << "p: " << p << endl;

  VectorXd d00 = v0.col(0).cwiseProduct(v0.col(0)) + v0.col(1).cwiseProduct(v0.col(1));
  VectorXd d01 = v0.col(0).cwiseProduct(v1.col(0)) + v0.col(1).cwiseProduct(v1.col(1));
  VectorXd d11 = v1.col(0).cwiseProduct(v1.col(0)) + v1.col(1).cwiseProduct(v1.col(1));
  VectorXd d20 = v2.col(0).cwiseProduct(v0.col(0)) + v2.col(1).cwiseProduct(v0.col(1));
  VectorXd d21 = v2.col(0).cwiseProduct(v1.col(0)) + v2.col(1).cwiseProduct(v1.col(1));
  VectorXd denom = d00.cwiseProduct(d11) - d01.cwiseProduct(d01);
  VectorXd v = (d11.cwiseProduct(d20) - d01.cwiseProduct(d21)).array() / denom.array();
  VectorXd w = (d00.cwiseProduct(d21) - d01.cwiseProduct(d20)).array() / denom.array();
  // VectorXd u = 1.0f - v.array() - w.array();
  VectorXd u = 1.0f - (v + w).array();

  B.resize(F.rows(),3);

  B.col(0) = u;
  B.col(1) = v;
  B.col(2) = w;

  if (isDebug)
  {
    for (int ii = 0; ii<u.size(); ii++)
    {
      if (isnan(u(ii)) || isnan(v(ii)) || isnan(w(ii)))
      {
        cout << "barycentric coordinate has nan, B = \n" << B <<endl;
      }
      assert(!isnan(u(ii)));
      assert(!isnan(v(ii)));
      assert(!isnan(w(ii)));
    }
  }
  
}
