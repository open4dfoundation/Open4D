#include "mqwf_dense.h"

void mqwf_dense_precompute(
  const Eigen::MatrixXd & A,
  const Eigen::MatrixXi & known,
  mqwf_dense_data & data)
{
  using namespace std;
  using namespace Eigen;

  // get number of variables
  int n = A.rows();

  // set know indices
  data.k.resize(known.size());
  data.k = known;

  // get unknown indices
  data.u.resize(n - data.k.size());
  {
    VectorXi known_sort;
    igl::sort(known,1,true,known_sort);
    VectorXi tmp = VectorXi::LinSpaced(n, 0, n-1);
    auto it = std::set_difference(tmp.data(), tmp.data() + tmp.size(), 
                              known_sort.data(), known_sort.data() + known_sort.size(), 
                              data.u.data());
    data.u.conservativeResize(std::distance(data.u.data(), it));
  }
  assert((data.u.size() + data.k.size()) == n);

  // get matrices
  MatrixXd Auu, Auk, Aku;
  Auu.resize(data.u.size(), data.u.size());
  igl::slice(A, data.u, data.u, Auu);
  igl::slice(A, data.u, data.k, Auk);
  igl::slice(A, data.k, data.u, Aku);

  // save data
  data.Auu_pref = Auu.ldlt();
  // data.Auu_pref = Auu.llt();
  data.Auk_plus_AkuT = Auk + Aku.transpose();
  data.n = n;
};

// template<typename Derived>
// void mqwf_dense_solve(
//   const mqwf_dense_data & data,
//   const Eigen::MatrixBase<Derived> & RHS,
//   const Eigen::MatrixBase<Derived> & known_val,
//   Eigen::MatrixBase<Derived> & sol)
// {
  // using namespace std;
  // using namespace Eigen;

  // // get column : in order to do B = A(i,:)
  // VectorXi col_colon = VectorXi::LinSpaced(RHS.cols(), 0, RHS.cols()-1);

  // // construct the reduced system
  // // data.Auu * sol = RHS_reduced
  // MatrixBase<Derived> RHS_reduced;
  // if (data.k.size() == 0) // no know values
  //   RHS_reduced = -RHS;
  // else
  // {
  //   MatrixBase<Derived> RHS_unknown;
  //   igl::slice(RHS,data.u,col_colon,RHS_unknown);
  //   RHS_reduced = -0.5 * data.Auk_plus_AkuT*known_val - RHS_unknown;
  // }

  // // solve
  // sol.resize(data.n,RHS.cols());
  
  // MatrixBase<Derived> sol_unknown;
  // sol_unknown.resize(data.u.size(), RHS.cols());
  // sol_unknown = data.Auu_pref.solve(RHS_reduced);
  // igl::slice_into(sol_unknown,data.u,col_colon,sol); // sol(unknown,:) = sol_unknown
  // igl::slice_into(known_val,  data.k,col_colon, sol); // sol(known,:) = known_val
// }

void mqwf_dense_solve(
  const mqwf_dense_data & data,
  const Eigen::VectorXd & RHS,
  const Eigen::VectorXd & known_val,
  Eigen::VectorXd & sol)
{
  using namespace std;
  using namespace Eigen;

  // get column : in order to do B = A(i,:)
  VectorXi col_colon = VectorXi::LinSpaced(RHS.cols(), 0, RHS.cols()-1);

  // construct the reduced system
  // data.Auu * sol = RHS_reduced
  VectorXd RHS_reduced;
  if (data.k.size() == 0) // no know values
    RHS_reduced = -RHS;
  else
  {
    VectorXd RHS_unknown;
    igl::slice(RHS,data.u,col_colon,RHS_unknown);
    RHS_reduced = -0.5 * data.Auk_plus_AkuT*known_val - RHS_unknown;
  }

  // solve
  sol.resize(data.n,RHS.cols());
  
  VectorXd sol_unknown;
  sol_unknown.resize(data.u.size(), RHS.cols());
  sol_unknown = data.Auu_pref.solve(RHS_reduced);
  igl::slice_into(sol_unknown,data.u,col_colon,sol); // sol(unknown,:) = sol_unknown
  igl::slice_into(known_val,  data.k,col_colon,sol); // sol(known,:) = known_val

}