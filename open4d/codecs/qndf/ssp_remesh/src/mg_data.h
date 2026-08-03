#ifndef MG_DATA_H
#define MG_DATA_H

#include <vector>
#include <iostream>

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/Dense>

struct mg_data
{
	Eigen::MatrixXd V; // vertices
	Eigen::MatrixXi F; // faces
	Eigen::SparseMatrix<double> P_full; // full prolongation
	Eigen::SparseMatrix<double> A; // LHS for only unknown parts
	Eigen::VectorXd A_diag; // diagonal entries of A
	Eigen::SparseMatrix<double> P; // prolongation for only unknown parts 
	Eigen::SparseMatrix<double> PT; // prolongation transpose for only unknown parts 

	// Gauss Seidel relaxation precomputation
	std::vector<std::vector<int>> S;
	Eigen::VectorXi SV;
	Eigen::VectorXi SVI;
	Eigen::VectorXi SC;
	Eigen::VectorXi SCS;

	void reset()
	{
		V = Eigen::MatrixXd();
		P_full = Eigen::SparseMatrix<double>();
		A = Eigen::SparseMatrix<double>();
		A_diag = Eigen::VectorXd();
		// Ar = Eigen::SparseMatrix<double, Eigen::RowMajor>();
		P = Eigen::SparseMatrix<double>();
		PT = Eigen::SparseMatrix<double>();
		F = Eigen::MatrixXi();
		S.clear(); // reset the 2d array
		SV = Eigen::VectorXi();
		SVI = Eigen::VectorXi();
		SC = Eigen::VectorXi();
		SCS = Eigen::VectorXi();
	}
};

#endif