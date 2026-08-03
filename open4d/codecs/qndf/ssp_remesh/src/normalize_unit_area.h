#ifndef NORMALIZE_UNIT_AREA_H
#define NORMALIZE_UNIT_AREA_H

#include <iostream>
#include <Eigen/Core>
#include <igl/doublearea.h>

// Inputs:
//   V        a matrix of vertex positions
//   F        a matrix of face info
// Outputs: 
//   V        a matrix of vertex positions (in a unit box)
void normalize_unit_area(
	Eigen::MatrixXd & V,
	const Eigen::MatrixXi & F);
#endif