#ifndef GET_COLLAPSE_ONERING_FACES_H
#define GET_COLLAPSE_ONERING_FACES_H

#include <igl/vertex_triangle_adjacency.h>
#include <igl/slice.h>
#include <igl/unique.h>
#include <igl/writeOBJ.h>
#include <igl/resolve_duplicated_faces.h>
#include <get_post_faces.h>

#include <vector>
#include <iostream>
#include <math.h>

#include <Eigen/Core>
#include <Eigen/Dense>

#define IGL_COLLAPSE_EDGE_NULL 0
bool get_collapse_onering_faces(
  const Eigen::MatrixXd & V,
	const Eigen::MatrixXi & F,
	const int & vi,
	const int & vj,
	const std::vector<int> & Nsf,
	const std::vector<int> & Ndf,
	Eigen::VectorXi & FIdx_ring_pre,
	Eigen::VectorXi & FIdx_ring_post,
	Eigen::MatrixXi & F_ring_pre,
	Eigen::MatrixXi & F_ring_post);

#endif