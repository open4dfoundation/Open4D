#ifndef MG_PRECOMPUTE_BLOCK_H
#define MG_PRECOMPUTE_BLOCK_H

#include <vector>
#include <iostream>
#include <math.h>

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/Dense>

#include <mg_data.h>
#include <get_prolong.h>

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
  std::vector<mg_data> & mg);

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
	const int & dec_type,
  std::vector<mg_data> & mg);

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
	const float & ratio,
	const int & nVCoarsest,
	const int & dec_type,
  std::vector<mg_data> & mg);
#endif