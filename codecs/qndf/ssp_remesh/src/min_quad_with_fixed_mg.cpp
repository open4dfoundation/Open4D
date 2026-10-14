#include "min_quad_with_fixed_mg.h"

void min_quad_with_fixed_mg_precompute(
  const Eigen::SparseMatrix<double> & A,
  min_quad_with_fixed_mg_data & data,
  std::vector<mg_data> & mg, 
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver)
{
  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  // start min quad with fixed manipulation
	assert(igl::is_symmetric(A,1.0)); // input matrix is symmetric

  // store in data
  data.LHS = A;
  data.n = A.rows();

  // mg precomputation
  // compute mg_mqwf so that it only contains unknown
  mg[0].A = data.LHS;
  for (int lv = 1; lv < mg.size(); lv++)
  {
    mg[lv].A = mg[lv].PT * mg[lv-1].A * mg[lv].P;
  }
  if (verbose)
    cout << "done getting mg matrices\n";
    

  // add small diagonal to avoid semidefinite issues (Is it required?)
  {
    int lv = mg.size() -1 ;
    for(int ii=0; ii<mg[lv].A.rows(); ii++)
      mg[lv].A.coeffRef(ii,ii) += 1e-12; 
  }

  // store diagonal values
  for (int lv = 0; lv < mg.size(); lv++) {
      mg[lv].A_diag = mg[lv].A.diagonal();
  }

  if (verbose)
    cout << "finish getting matrices\n";

  // prefactorize for coarsest solve
  SparseMatrix<double> Ac = mg[mg.size()-1].A;
  solver.compute(Ac);
  if (verbose)
    cout << "finish coarse solve prefactorization\n";
}

template <typename DerivedRHS, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  return min_quad_with_fixed_mg_solve(data, RHS, z0, solver, 1e-3, mg, z, r_his);
}

template <typename DerivedRHS, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  const double & tolerance,
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  return min_quad_with_fixed_mg_solve(data, RHS, z0, solver, tolerance, 20, mg, z, r_his);
}

template <typename DerivedRHS, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  const double & tolerance,
  const int & maxIter,
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  // get initial solution
  // z.resize(z0.rows(), z0.cols());
  z = z0;
  
  // residual history
  double residual;
  int preRelaxIter = 2;
  int postRelaxIter = 2;
  r_his.clear();
  r_his.reserve(maxIter); // initialize a large enough size
  {
    // PROFC_NODE("MG: total VCycle");
    for (int iter = 0; iter < maxIter; iter++)
    {
      residual = (RHS - mg[0].A*z).norm();
      cout << "MG iteration: " << iter << ", residual: " << residual << endl;
      r_his.push_back(residual);
      if (residual < tolerance)
      {
        break;  
      }
      if (verbose)
      {
        cout << "============\n";
        cout << "Iteration: " << iter << endl;
        cout << "============\n";
      }
      PROFC_NODE("MG: total VCycle");
      mg_VCycle(solver, RHS,preRelaxIter,postRelaxIter,0,z,mg);
    }
  }
  cout << "residual norm: " << r_his[r_his.size()-1] << endl;
  if (verbose)
    cout << "finish V cycle\n";
  
  if (residual > tolerance)
    return false;
  else
    return true;
}

void min_quad_with_fixed_mg_precompute(
  const Eigen::SparseMatrix<double> & A,
  const Eigen::VectorXi & known,
  min_quad_with_fixed_mg_data & data,
  std::vector<mg_data> & mg, 
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver)
{
  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  // start min quad with fixed manipulation
	assert(igl::is_symmetric(A,1.0)); // input matrix is symmetric
  if (verbose)
    cout << "done checking symmetry\n";
	

  int nV = A.rows();
	// get indices of unknown
	VectorXi unknown,_; 
  VectorXi seq = VectorXi::LinSpaced(nV,0,nV-1);
  igl::setdiff(seq,known,unknown,_);

  if (verbose)
    cout << "done getting unknown indices\n";

	// deal with LHS
  {
    // PROFC_NODE("MG_pre: slice matrices");
    SparseMatrix<double> A_unknown;
    igl::slice(A,unknown,unknown,A_unknown);

    SparseMatrix<double> A_unknown_known;
    igl::slice(A,unknown,known,A_unknown_known);
    if (verbose)
      cout << "done getting Auk\n";

    data.LHS = A_unknown; // LHS = A_unknown
    data.Auk = A_unknown_known; // RHS = -data.Auk * knownval - B_unknown
    data.n = A.rows();
    data.known = known;
    data.unknown = unknown;
  }

  // mg precomputation
  {
    // PROFC_NODE("MG_pre: build hierarchy");
    // re-organize P so that it only contains unknowns
    igl::slice(mg[1].P_full,data.unknown,1,mg[1].P);
    for (int lv=1; lv<mg.size(); lv++)
    {
      // get the columns that we want to keep
      // mg[lv].P.prune(0.0);
      std::vector<int> keepIdx_vec;
      keepIdx_vec.reserve(mg[lv].P.cols());
      for (int cIdx=0; cIdx<mg[lv].P.cols(); cIdx++)
      {
        // check the max value per column is bigger than threshold
        for (Eigen::SparseMatrix<double,Eigen::ColMajor>::InnerIterator it(mg[lv].P,cIdx); it; ++it)
        {
          if (it.value() > 1e-15)
          {
            keepIdx_vec.push_back(cIdx);
            break;
          }
        }
      }

      // slice the P to P(:, keepIdx)
      if (keepIdx_vec.size() < mg[lv].P.cols())
      {
        VectorXi keepIdx = Map<VectorXi, Eigen::Unaligned>(keepIdx_vec.data(), keepIdx_vec.size());

        SparseMatrix<double> Ptmp = mg[lv].P; 
        igl::slice(Ptmp,keepIdx,2,mg[lv].P);

        if (lv < (mg.size()-1))
          igl::slice(mg[lv+1].P_full,keepIdx,1,mg[lv+1].P);
      }
      else
      {
        break;
      }
    }

    // compute mg_mqwf so that it only contains unknown
    mg[0].A = data.LHS;
    for (int lv = 1; lv < mg.size(); lv++)
    {
      mg[lv].PT = mg[lv].P.transpose();
      mg[lv].A = mg[lv].PT * mg[lv-1].A * mg[lv].P;
    }
  }

  if (verbose)
    cout << "done getting mg matrices\n";



  // add small diagonal to avoid numerical issues
  {
    int lv = mg.size() -1;
    for(int ii=0; ii<mg[lv].A.rows(); ii++)
      mg[lv].A.coeffRef(ii,ii) += 1e-12; // add small diagonal values 
  }

  // store diagonal values
  for (int lv = 0; lv < mg.size(); lv++) {
      mg[lv].A_diag = mg[lv].A.diagonal();
  }

  if (verbose)
    cout << "finish getting matrices\n";

  // prefactorize for coarsest solve
  // SimplicialLDLT<SparseMatrix<double>> solver;
  SparseMatrix<double> Ac = mg[mg.size()-1].A;
  solver.compute(Ac);
  if (verbose)
    cout << "finish coarse solve prefactorization\n";
}

template <typename DerivedRHS, typename DerivedKnownVal, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedKnownVal> & known_val,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  return min_quad_with_fixed_mg_solve(data, RHS, known_val, z0, solver, 1e-3, mg, z, r_his);
}

template <typename DerivedRHS, typename DerivedKnownVal, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedKnownVal> & known_val,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  const double & tolerance,
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  return min_quad_with_fixed_mg_solve(data, RHS, known_val, z0, solver, tolerance, 20, mg, z, r_his);
}

template <typename DerivedRHS, typename DerivedKnownVal, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedKnownVal> & known_val,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  const double & tolerance,
  const int & maxIter,
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his)
{
  // template class
  typedef Eigen::Matrix<typename DerivedZ0::Scalar,Eigen::Dynamic,Eigen::Dynamic> MatrixXZ0;
  typedef Eigen::Matrix<typename DerivedRHS::Scalar,Eigen::Dynamic,Eigen::Dynamic> MatrixXRHS;

  using namespace std;
  using namespace Eigen;
  bool verbose = false;

  // get initial solution
  MatrixXZ0 z_unknown;
  igl::slice(z0,data.unknown,1,z_unknown);
  if (verbose)
    cout << "finish getting initial guess\n";

  // get RHS unknown
  MatrixXRHS RHS_unknown;
  igl::slice(RHS,data.unknown,1,RHS_unknown);
  RHS_unknown = RHS_unknown - data.Auk * known_val;
  if (verbose)
    cout << "finish getting RHS\n";
  
  // residual history
  double residual;
  int preRelaxIter = 2;
  int postRelaxIter = 2;
  r_his.clear();
  r_his.reserve(maxIter); // initialize a large enough size
  {
    PROFC_NODE("MG: total VCycle");
    for (int iter = 0; iter < maxIter; iter++)
    {
      residual = (RHS_unknown - mg[0].A*z_unknown).norm();
      // cout << "MG iteration: " << iter << ", residual: " << residual << endl;
      cout << residual << endl;
      r_his.push_back(residual);
      if (residual < tolerance)
      {
        break;  
      }
      if (verbose)
      {
        cout << "============\n";
        cout << "Iteration: " << iter << endl;
        cout << "============\n";
      }
      mg_VCycle(solver, RHS_unknown,preRelaxIter,postRelaxIter,0,z_unknown,mg);
    }
  }
  cout << "residual norm: " << r_his[r_his.size()-1] << endl;
  if (verbose)
    cout << "finish V cycle\n";

  z.resize(z0.rows(),z0.cols());
  igl::slice_into(z_unknown,data.unknown,1,z);
  igl::slice_into(known_val,data.known,1,z);
  
  if (residual > tolerance)
    return false;
  else
    return true;
}

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, double const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, std::vector<double, std::allocator<double> >&);

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, double const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, std::vector<double, std::allocator<double> >&);

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, std::vector<double, std::allocator<double> >&);

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, double const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, std::vector<double, std::allocator<double> >&);

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, std::vector<double, std::allocator<double> >&);

template bool min_quad_with_fixed_mg_solve<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(min_quad_with_fixed_mg_data const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::SimplicialLDLT<Eigen::SparseMatrix<double, 0, int>, 1, Eigen::AMDOrdering<int> > const&, double const&, std::vector<mg_data, std::allocator<mg_data> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, std::vector<double, std::allocator<double> >&);
