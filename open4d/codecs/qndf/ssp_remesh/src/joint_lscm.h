#ifndef JOINT_LSCM_H
#define JOINT_LSCM_H

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <igl/writeOBJ.h>
#include <igl/boundary_loop.h>
#include <igl/vector_area_matrix.h>
#include <igl/cotmatrix.h>
#include <igl/repdiag.h>
#include <igl/slice_into.h>
#include <igl/min_quad_with_fixed.h>
#include <igl/slice.h>
#include <igl/setdiff.h>
#include <igl/internal_angles.h>

#include <get_post_faces.h>
#include <vector_area_matrix_size.h>
#include <mqwf_dense.h>
#include <mqwf_dense_data.h>
#include <remove_vector_element.h>
#include <cotmatrix_dense.h>
#include <quasi_conformal_error.h>

#include <profc.h>
#include <vector>
#include <iostream>
#include <stdlib.h>
#include <limits>
#include <cmath>

// Compute a joinit Least-squares conformal map parametrization for the edge onering mesh before an edge collapse (V_pre, FUV_pre)and the vertex onering mesh after the collapse (V_post, FUV_post). We constrain both meshes to have the same boundary curve for bijectivity
// 
// Inputs:
// V_pre    #V_pre by 3 list of mesh vertex positions
// FUV_pre  #F_pre by 3 list of mesh faces 
// V_post   #V_post by 3 list of mesh vertex positions
// FUV_post #F_post by 3 list of mesh faces 
// vi       one of the vertex indices of the edge to be collapsed
// vj       the other vertex index of the edge to be collapsed
// Nsv      ordered neighboring vertex indices of vi 
// Ndv      ordered neighboring vertex indices of vj
// 
// Outputs:
// UV_pre    #V_pre by 2 list of UV mesh vertex positions of V_pre
// UV_post   #V_post by 2 list of UV mesh vertex positions of V_post
bool joint_lscm(
  const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const std::vector<int> & Nsv,
  const std::vector<int> & Ndv,
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post);

bool check_valid_UV_lscm(
  const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXd & UV_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXd & UV_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const std::vector<int> & Nsv,
  const std::vector<int> & Ndv);

void flatten(
	const Eigen::MatrixXd & Vjoint_pre,
  const Eigen::MatrixXi & Fjoint_pre,
  const Eigen::MatrixXd & Vjoint_post,
  const Eigen::MatrixXi & Fjoint_post,
	const Eigen::VectorXi & b_UV,
	const Eigen::VectorXd & bc_UV,
	const int & nVjoint,
	const bool & isDebug,
	Eigen::VectorXd & UVjoint_flat);

void joint_lscm_case0(
	const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const Eigen::VectorXi & bdLoop,
  const bool & verbose,
	const bool & isDebug, 
  const Eigen::VectorXi & onBd,
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post);

void joint_lscm_case1(
	const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const Eigen::VectorXi & bdLoop,
  const bool & verbose,
	const bool & isDebug,
  const Eigen::VectorXi & onBd, 
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post);

void joint_lscm_case2(
	const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const Eigen::VectorXi & bdLoop,
  const bool & verbose,
	const bool & isDebug,
  const Eigen::VectorXi & onBd, 
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post);

void case2_constraint3_snap1(
  const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const Eigen::VectorXi & bdLoop,
  const bool & verbose,
	const bool & isDebug,
  const Eigen::VectorXi & onBd, 
  const int & snapIdx,
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post
);

void case2_constraint4(
  const Eigen::MatrixXd & V_pre,
  const Eigen::MatrixXi & FUV_pre,
  const Eigen::MatrixXd & V_post,
  const Eigen::MatrixXi & FUV_post,
  const int & vi,
  const int & vj,
  const Eigen::VectorXi & bdLoop,
  const bool & verbose,
	const bool & isDebug,
  const Eigen::VectorXi & onBd, 
  Eigen::MatrixXd & UV_pre,
  Eigen::MatrixXd & UV_post
);

// void joint_lscm_case3(
// 	const Eigen::MatrixXd & V_pre,
//   const Eigen::MatrixXi & FUV_pre,
//   const Eigen::MatrixXd & V_post,
//   const Eigen::MatrixXi & FUV_post,
//   const int & vi,
//   const int & vj,
//   const Eigen::VectorXi & bdLoop,
//   const bool & verbose,
// 	const bool & isDebug,
//   const Eigen::VectorXi & onBd, 
//   const int & v_flapCorner,
//   Eigen::MatrixXd & UV_pre,
//   Eigen::MatrixXd & UV_post);


#endif