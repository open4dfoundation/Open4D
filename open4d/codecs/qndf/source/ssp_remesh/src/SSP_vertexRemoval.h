#ifndef SSP_VERTEXREMOVAL_H
#define SSP_VERTEXREMOVAL_H

#include <Eigen/Core>
#include <vector>

#include <SSP_midpoint.h>
#include <SSP_vertexRemoval_optimal_collapse_edge_callbacks.h>

// #include <igl/collapse_edge.h>
#include <igl/connect_boundary_to_infinity.h>
// #include <igl/decimate.h>
#include <igl/edge_flaps.h>
#include <igl/is_edge_manifold.h>
#include <igl/max_faces_stopping_condition.h>
#include <igl/per_vertex_point_to_plane_quadrics.h>
// #include <igl/qslim_optimal_collapse_edge_callbacks.h>
#include <igl/quadric_binary_plus_operator.h>
#include <igl/remove_unreferenced.h>
#include <igl/slice.h>
#include <igl/slice_mask.h>
#include <igl/find.h>
#include <igl/unique.h>

#include <single_collapse_data.h>
#include <compute_vertex_quadrics.h>

// Modified from libigl "qslim", see "SSP_decimate" for more info
// SSP_vertexRemoval (simplify) a triangle mesh using half-edge collapse, while storing information required for SSP
//
// Inputs:
//   V  #V by dim list of vertex positions. Assumes that vertices with
//     infinite coordinates are "points at infinity" being used to close up
//     boundary edges with faces. This allows special subspace quadrice for
//     boundary edges: There should never be more than one "point at
//     infinity" in a single triangle.
//   F  #F by 3 list of triangle indices into V
//   max_m  desired number of output faces
// Outputs:
//   U  #U by dim list of output vertex posistions (can be same ref as V)
//   G  #G by 3 list of output face indices into U (can be same ref as F)
//   J  #G list of indices into F of birth face
//   I  #U list of indices into V of birth vertices
bool SSP_vertexRemoval(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const size_t max_m,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J,
    Eigen::VectorXi & I,
    std::vector<single_collapse_data> & decInfo,
    std::vector<std::vector<int>> & decIM,
    Eigen::VectorXi & FIM);
#endif