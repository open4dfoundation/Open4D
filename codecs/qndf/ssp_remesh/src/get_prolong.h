#ifndef GET_PROLONG_H
#define GET_PROLONG_H

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <vector>
#include <iostream>

#include <SSP_decimate.h>
#include <single_collapse_data.h>
#include <query_fine_to_coarse.h>

// decimate the model with successive self-parameterization and compute the prolongation (aka interpolation) operator from V to Vf
//
// Inputs:
//   Vf    #Vf-by-3 input vertex positions 
//   Ff    #Ff-by-3 input triangle indices
//   tarF  desired number of output faces
//   dec_type decimation type (0:qslim, 1:midpoint, 2:vertex removal)
//
// Outputs:
//   V     #V by 3 output vertex posistions
//   F     #F by 3 ooutput face indices 
//   P     #Vf by #V linear interpolation operator 

void get_prolong(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
  const int & tarF,
  const int & dec_type,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::SparseMatrix<double> & P);

// the same function as "get_prolong", but the output prolongation P is a 3#Vf by 3#V matrix

void get_prolong_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
  const int & tarF,
  const int & dec_type,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::SparseMatrix<double> & P);
#endif