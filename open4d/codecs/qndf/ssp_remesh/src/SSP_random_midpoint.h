#ifndef SSP_RANDOM_MIDPOINT_H
#define SSP_RANDOM_MIDPOINT_H
#include <Eigen/Core>
// #include <igl/decimate_func_types.h>
#include <igl/collapse_edge.h>
#include <igl/edge_flaps.h>
// #include <igl/always_try_never_care.h>
#include <igl/is_edge_manifold.h>
#include <igl/remove_unreferenced.h>
#include <igl/slice_mask.h>
#include <igl/slice.h>
#include <igl/connect_boundary_to_infinity.h>
#include <igl/parallel_for.h>
#include <igl/max_faces_stopping_condition.h>
#include <igl/shortest_edge_and_midpoint.h>

#include <SSP_random_collapse_edge.h>
#include <vector>
#include <single_collapse_data.h>
#include <always_try_never_care.h>
#include <decimate_func_types.h>

// Modified from libigl "decimate", see "SSP_decimate" for more info
// Assumes (V,F) is a manifold mesh (possibly with boundary) Collapses edges
// until desired number of faces is achieved. This uses default edge cost and
// merged vertex placement functions {edge length, edge midpoint}.
//
// Inputs:
//   V  #V by dim list of vertex positions
//   F  #F by 3 list of face indices into V.
//   max_m  desired number of output faces
// Outputs:
//   U  #U by dim list of output vertex posistions (can be same ref as V)
//   G  #G by 3 list of output face indices into U (can be same ref as G)
//   J  #G list of indices into F of birth face
//   I  #U list of indices into V of birth vertices
// Returns true if m was reached (otherwise #G > m)
using namespace igl;
bool SSP_random_midpoint(
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
  // Inputs:
  //   V  #V by dim list of vertex positions
  //   F  #F by 3 list of face indices into V.
  //   max_m  desired number of output faces
  // Outputs:
  //   U  #U by dim list of output vertex posistions (can be same ref as V)
  //   G  #G by 3 list of output face indices into U (can be same ref as G)
  //   J  #G list of indices into F of birth face
  // Returns true if m was reached (otherwise #G > m)
// bool SSP_decimate(
//   const Eigen::MatrixXd & V,
//   const Eigen::MatrixXi & F,
//   const size_t max_m,
//   Eigen::MatrixXd & U,
//   Eigen::MatrixXi & G,
//   Eigen::VectorXi & J);
  // Assumes a **closed** manifold mesh. See igl::connect_boundary_to_infinity
  // and igl::decimate in decimate.cpp
  // is handling meshes with boundary by connecting all boundary edges with
  // dummy facets to infinity **and** modifying the stopping criteria.
  //
  // Inputs:
  //   cost_and_placement  function computing cost of collapsing an edge and 3d
  //     position where it should be placed:
  //     cost_and_placement(V,F,E,EMAP,EF,EI,cost,placement);
  //   stopping_condition  function returning whether to stop collapsing edges
  //     based on current state. Guaranteed to be called after _successfully_
  //     collapsing edge e removing edges (e,e1,e2) and faces (f1,f2):
  //     bool should_stop =
  //       stopping_condition(V,F,E,EMAP,EF,EI,Q,Qit,C,e,e1,e2,f1,f2);
bool SSP_random_midpoint(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const decimate_cost_and_placement_func & cost_and_placement,
  const decimate_stopping_condition_func & stopping_condition,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J,
  Eigen::VectorXi & I,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM,
  Eigen::VectorXi & FIM);
  // Inputs:
  //   pre_collapse  callback called with index of edge whose collapse is about
  //     to be attempted (see collapse_edge)
  //   post_collapse  callback called with index of edge whose collapse was
  //     just attempted and a flag revealing whether this was successful (see
  //     collapse_edge)
bool SSP_random_midpoint(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const decimate_cost_and_placement_func & cost_and_placement,
  const decimate_stopping_condition_func & stopping_condition,
  const decimate_pre_collapse_func       & pre_collapse,
  const decimate_post_collapse_func      & post_collapse,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J,
  Eigen::VectorXi & I,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM,
  Eigen::VectorXi & FIM);
  // Inputs:
  //   EMAP #F*3 list of indices into E, mapping each directed edge to unique
  //     unique edge in E
  //   EF  #E by 2 list of edge flaps, EF(e,0)=f means e=(i-->j) is the edge of
  //     F(f,:) opposite the vth corner, where EI(e,0)=v. Similarly EF(e,1) "
  //     e=(j->i)
  //   EI  #E by 2 list of edge flap corners (see above).
bool SSP_random_midpoint(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const decimate_cost_and_placement_func & cost_and_placement,
    const decimate_stopping_condition_func & stopping_condition,
    const decimate_pre_collapse_func       & pre_collapse,
    const decimate_post_collapse_func      & post_collapse,
    const Eigen::MatrixXi & E,
    const Eigen::VectorXi & EMAP,
    const Eigen::MatrixXi & EF,
    const Eigen::MatrixXi & EI,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J,
    Eigen::VectorXi & I,
    std::vector<single_collapse_data> & decInfo,
    std::vector<std::vector<int>> & decIM,
    Eigen::VectorXi & FIM);

#endif

