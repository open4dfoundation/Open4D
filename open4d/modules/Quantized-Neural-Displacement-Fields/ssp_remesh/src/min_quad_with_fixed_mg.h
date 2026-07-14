#ifndef MIN_QUAD_WITH_FIXED_MG
#define MIN_QUAD_WITH_FIXED_MG

#include <igl/slice.h>
#include <igl/slice_into.h>
#include <igl/diag.h>
#include <igl/is_symmetric.h>
#include <igl/setdiff.h>

#include <vector>
#include <iostream>
#include <math.h>

#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/Sparse>

#include <sort_vec.h>
#include <mg_data.h>
#include <mg_VCycle.h>

struct min_quad_with_fixed_mg_data
{
  int n;
  Eigen::VectorXi known; // known 
  Eigen::VectorXi unknown; // unknown
  Eigen::SparseMatrix<double> LHS, Auk;
  // RHS = -Auk * knownval + B_unknown
};

// the version withuot fixed values
void min_quad_with_fixed_mg_precompute( 
  const Eigen::SparseMatrix<double> & A,
  min_quad_with_fixed_mg_data & data,
  std::vector<mg_data> & mg,
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver);

template <typename DerivedRHS, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his);

template <typename DerivedRHS, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  const double & tolerance,
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his);

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
  std::vector<double> & r_his);

// the version with fixed values 
void min_quad_with_fixed_mg_precompute(
  const Eigen::SparseMatrix<double> & A,
  const Eigen::VectorXi & known,
  min_quad_with_fixed_mg_data & data,
  std::vector<mg_data> & mg,
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver);

template <typename DerivedRHS, typename DerivedKnownVal, typename DerivedZ0, typename DerivedZ>
bool min_quad_with_fixed_mg_solve(
  const min_quad_with_fixed_mg_data & data,
  const Eigen::PlainObjectBase<DerivedRHS> & RHS,
  const Eigen::PlainObjectBase<DerivedKnownVal> & known_val,
  const Eigen::PlainObjectBase<DerivedZ0> & z0, // initial guess
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver, // coarsest solve
  std::vector<mg_data> & mg,
  Eigen::PlainObjectBase<DerivedZ> & z,
  std::vector<double> & r_his);

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
  std::vector<double> & r_his);

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
  std::vector<double> & r_his);

#endif