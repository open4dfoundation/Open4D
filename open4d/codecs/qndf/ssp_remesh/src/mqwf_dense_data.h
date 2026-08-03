#ifndef MQWF_DENSE_DATA_H
#define MQWF_DENSE_DATA_H

#include <Eigen/Core>
#include <Eigen/Dense>

struct mqwf_dense_data
{
  int n;
  Eigen::VectorXi k; // known 
  Eigen::VectorXi u; // unknown
  Eigen::LDLT<Eigen::MatrixXd> Auu_pref;
  Eigen::MatrixXd Auk_plus_AkuT;
};

#endif