#ifndef MG_VCYCLE_H
#define MG_VCYCLE_H

#include <igl/slice.h>
#include <igl/slice_into.h>
#include <igl/parallel_for.h>
#include <igl/get_seconds.h>

#include <vector>
#include <iostream>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <profc.h>
#include <mg_data.h>

#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/Sparse>

template <typename DeriveddB, typename DeriveddU>
void mg_VCycle(
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver,
  const Eigen::PlainObjectBase<DeriveddB> & B,
  const int & preRelaxIter,
  const int & postRelaxIter,
  const int lv,
  Eigen::PlainObjectBase<DeriveddU> & u,
  std::vector<mg_data> & mg);

template <typename DeriveddU, typename DeriveddAU>
void A(
  const Eigen::PlainObjectBase<DeriveddU> & u, 
  const std::vector<mg_data> & mg, 
  const int & lv, 
  Eigen::PlainObjectBase<DeriveddAU> & Au);

template <typename DeriveddX, typename DeriveddRX>
void restrict(
  const Eigen::PlainObjectBase<DeriveddX> & x, 
  const std::vector<mg_data> & mg, 
  const int & lv,
  Eigen::PlainObjectBase<DeriveddRX> & Rx);

template <typename DerivedX, typename DerivedPX>
void prolong(
  const Eigen::PlainObjectBase<DerivedX> & x, 
  const std::vector<mg_data> & mg, 
  const int & lv,
  Eigen::PlainObjectBase<DerivedPX> & Px);

template <typename DerivedB, typename DerivedU>
void printErrorNorm(
  const int lv, 
  const std::string & actionStr, 
  const std::vector<mg_data> & mg, 
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const Eigen::PlainObjectBase<DerivedU> & u,
  const bool verbose);

template <typename DerivedB, typename DerivedU>
void relax(
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const int & lv,
  const int & iters,
  Eigen::PlainObjectBase<DerivedU> & u,
  std::vector<mg_data> & mg);

template <typename DerivedB, typename DerivedU>
void coarseSolve(
  const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> & solver,
  const Eigen::PlainObjectBase<DerivedB> & B, 
  const int & lv,
  Eigen::PlainObjectBase<DerivedU> & u,
  std::vector<mg_data> & mg);

#endif