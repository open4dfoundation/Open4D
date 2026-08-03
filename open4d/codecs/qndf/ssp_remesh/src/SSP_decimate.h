#ifndef SSP_DECIMATE_H
#define SSP_DECIMATE_H

#include <igl/is_vertex_manifold.h>
#include <igl/is_edge_manifold.h>

#include <Eigen/Core>
#include <iostream>
#include <vector>
#include <single_collapse_data.h>

#include <SSP_qslim.h>
#include <SSP_vertexRemoval.h>
#include <SSP_midpoint.h>

// Decimate the model with Successive Self-Parameterization (SSP)
//
// Inputs:
//   VO    #VO-by-3 input vertex positions 
//   FO    #FO-by-3 list of triangle indices
//   tarF  desired number of output faces
//   dec_type decimation type (0:qslim, 1:midpoint, 2:vertex removal)
//
// Outputs:
//   V     #V by 3 output vertex posistions
//   F     #F by 3 ooutput face indices 
//   IMF   #F list of indices into FO of birth face
//   IM    #V list of indices into VO of birth vertices
//   decInfo  SSP decimation information (for query purposes)
//   decIM    SSP decimation information (for query purposes)
//   FIM      SSP decimation information (for query purposes)
bool SSP_decimate(
    const Eigen::MatrixXd & VO,
    const Eigen::MatrixXi & FO,
    const size_t & tarF,
    const int & dec_type, 
    Eigen::MatrixXd & V,
    Eigen::MatrixXi & F,
    Eigen::VectorXi & IMF,
    Eigen::VectorXi & IM,
    std::vector<single_collapse_data> & decInfo,
    std::vector<std::vector<int>> & decIM,
    Eigen::VectorXi & FIM);
#endif