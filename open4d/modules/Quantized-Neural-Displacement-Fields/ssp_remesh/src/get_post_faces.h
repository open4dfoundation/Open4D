#ifndef GET_POST_FACES_H
#define GET_POST_FACES_H

#include <vector>
#include <iostream>
#include <igl/slice.h>

#include <Eigen/Core>
#include <Eigen/Dense>

void get_post_faces(
	const Eigen::MatrixXi & F_pre,
	const int & vi,
	const int & vj,
  Eigen::VectorXi & f_keep,
	Eigen::MatrixXi & F_post);

#endif