#include "mg_VCycle.h"

template <typename DerivedB, typename DerivedU>
void mg_VCycle(
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver,
  const Eigen::PlainObjectBase<DerivedB> & B,
  const int & preRelaxIter,
  const int & postRelaxIter,
  const int lv,
  Eigen::PlainObjectBase<DerivedU> & u,
  std::vector<mg_data> & mg)
{
  // template class
  typedef Eigen::Matrix<typename DerivedB::Scalar,Eigen::Dynamic,Eigen::Dynamic> MatrixXB;
  typedef Eigen::Matrix<typename DerivedU::Scalar,Eigen::Dynamic,Eigen::Dynamic> MatrixXU;
  

  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  int nLvs = mg.size();

  // start multigrid
  printErrorNorm(lv, "Initial", mg, B, u,verbose);

  // at the deepest level
  if (lv == (mg.size()-1))
  {
    coarseSolve(solver, B,lv,u,mg);
    printErrorNorm(lv, "Coarsest solve", mg, B, u,verbose);
    return;
  }

  // pre relaxation
  relax(B,lv,preRelaxIter,u,mg);
  printErrorNorm(lv, "Pre relaxation", mg, B, u,verbose);

  // start correction scheme 
  MatrixXU Au;
  A(u, mg, lv, Au);
  MatrixXB r = B - Au; // residual
  MatrixXB rc; // restricted residual
  restrict(r, mg, lv, rc);

  MatrixXU uc(rc.rows(), rc.cols());
  uc.setZero();
  mg_VCycle(solver, rc,preRelaxIter,postRelaxIter,lv+1,uc,mg);

  // update u
  MatrixXU puc; // prolonged solution
  prolong(uc, mg, lv, puc);
  u = u + puc;
  printErrorNorm(lv, "Coarse-grid correction", mg, B, u,verbose);

  // post relaxation
  relax(B,lv,postRelaxIter,u,mg);
  printErrorNorm(lv, "Post relaxation", mg, B, u,verbose);
}


template <typename DerivedU, typename DerivedAU>
void A(
  const Eigen::PlainObjectBase<DerivedU> & u, 
  const std::vector<mg_data> & mg, 
  const int & lv, 
  Eigen::PlainObjectBase<DerivedAU> & Au)
{
  Au = mg[lv].A * u;  
}

template <typename DerivedX, typename DerivedRX>
void restrict(
  const Eigen::PlainObjectBase<DerivedX> & x, 
  const std::vector<mg_data> & mg, 
  const int & lv,
  Eigen::PlainObjectBase<DerivedRX> & Rx)
{
  // PROFC_NODE("MG: restrict");
  Rx = mg[lv+1].PT * x;
}

template <typename DerivedX, typename DerivedPX>
void prolong(
  const Eigen::PlainObjectBase<DerivedX> & x, 
  const std::vector<mg_data> & mg, 
  const int & lv,
  Eigen::PlainObjectBase<DerivedPX> & Px)
{
  // PROFC_NODE("MG: prolong");
  Px = mg[lv+1].P * x;
}

template <typename DerivedB, typename DerivedU>
void printErrorNorm(
  const int lv, 
  const std::string & actionStr, 
  const std::vector<mg_data> & mg, 
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const Eigen::PlainObjectBase<DerivedU> & u,
  const bool verbose)
{
  if (verbose)
  {
    double rnorm = (B - mg[lv].A * u).norm();
    std::printf("%-5d ", lv);
    std::cout << actionStr;
    std::printf(" %.4e\n", rnorm);
  }
}


template <typename DerivedB, typename DerivedU>
void relax(
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const int & lv,
  const int & iters,
  Eigen::PlainObjectBase<DerivedU> & u,
  std::vector<mg_data> & mg)
{
  PROFC_NODE("MG: relaxation");
  using namespace std;
  using namespace Eigen;

  int dim = u.cols();
  int numV = mg[lv].A.rows();

  // // implement gauss seidel (no precomputation)
  // for (int iter = 0; iter < iters; iter++) {
  //       for (int ri = 0; ri < dim; ri++) {
  //           for (int colIdx = 0; colIdx < mg[lv].A.rows(); colIdx++) {
  //               // Eigen Sparse matrices are column major
  //               // For convenience, we compute z' * (L+U)' instead of (L+U) * z
  //               double sum = 0;
  //               for (Eigen::SparseMatrix<double>::InnerIterator it(mg[lv].A, colIdx); it; ++it) {
  //                   if (it.row() != colIdx) {
  //                       sum += it.value() * u(it.row(), ri);
  //                   }
  //               }
  //               u(colIdx, ri) = (B(colIdx, ri) - sum) / mg[lv].A_diag(colIdx);
  //           }
  //       }
  //   }

  // if statement is necessary here: boost the performance
  if (dim == 1) {
      for (int iter = 0; iter < iters; iter++) {
          for (int colIdx = 0; colIdx < mg[lv].A.rows(); colIdx++) {
              // Eigen Sparse matrices are column major
              // For convenience, we compute z' * (L+U)' instead of (L+U) * z
              double sum = 0;
              for (Eigen::SparseMatrix<double>::InnerIterator it(mg[lv].A, colIdx); it; ++it) {
                  if (it.row() != colIdx) {
                      sum += it.value() * u(it.row());
                  }
              }
              u(colIdx) = (B(colIdx) - sum) / mg[lv].A_diag(colIdx);
          }
      }
  }
  else {
      for (int iter = 0; iter < iters; iter++) {
          for (int ri = 0; ri < dim; ri++) {
              for (int colIdx = 0; colIdx < mg[lv].A.rows(); colIdx++) {
                  // Eigen Sparse matrices are column major
                  // For convenience, we compute z' * (L+U)' instead of (L+U) * z
                  double sum = 0;
                  for (Eigen::SparseMatrix<double>::InnerIterator it(mg[lv].A, colIdx); it; ++it) {
                      if (it.row() != colIdx) {
                          sum += it.value() * u(it.row(), ri);
                      }
                  }
                  u(colIdx, ri) = (B(colIdx, ri) - sum) / mg[lv].A_diag(colIdx);
              }
          }
      }
  }
}


template <typename DerivedB, typename DerivedU>
void coarseSolve(
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver,
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const int & lv,
  Eigen::PlainObjectBase<DerivedU> & u,
  std::vector<mg_data> & mg) // TODO: add solver as an output argument
{
  typedef Eigen::Matrix<typename DerivedU::Scalar,Eigen::Dynamic,Eigen::Dynamic> MatrixXU;

  // PROFC_NODE("MG: coarse solve");
  using namespace Eigen;
  using namespace std;
  assert(lv == (mg.size()-1)); // this has to be the last level

  // SimplicialLDLT<SparseMatrix<double>> solver;
  // solver.compute(mg[lv].A);

  MatrixXU delta_u = solver.solve(B);
  u = u + delta_u;
}

template void mg_VCycle<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, int const&, int const&, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, std::vector<mg_data, std::allocator<mg_data> >&);
