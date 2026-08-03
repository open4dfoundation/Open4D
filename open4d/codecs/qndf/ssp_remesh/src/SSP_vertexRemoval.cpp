#include "SSP_vertexRemoval.h"

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
    Eigen::VectorXi & FIM)
{
  using namespace igl;
  using namespace std;
  decInfo.reserve(F.rows() - max_m); // reserve a big enough size for decInfo
  decIM.reserve(F.rows()); // reserve a big enough size for decIM
  for (int ii=0; ii<F.rows(); ii++)
    decIM.push_back(vector<int>());

  // Original number of faces
  const int orig_m = F.rows();
  // Tracking number of faces
  int m = F.rows();
  typedef Eigen::MatrixXd DerivedV;
  typedef Eigen::MatrixXi DerivedF;
  DerivedV VO;
  DerivedF FO;
  igl::connect_boundary_to_infinity(V,F,VO,FO);
  // decimate will not work correctly on non-edge-manifold meshes. By extension
  // this includes meshes with non-manifold vertices on the boundary since these
  // will create a non-manifold edge when connected to infinity.

  if(!is_edge_manifold(FO))
  {
    return false;
  }
  Eigen::VectorXi EMAP;
  Eigen::MatrixXi E,EF,EI;
  edge_flaps(FO,E,EMAP,EF,EI);
  // Quadrics per vertex
  typedef std::tuple<Eigen::MatrixXd,Eigen::RowVectorXd,double> Quadric;
  std::vector<Quadric> quadrics;
  // compute_vertex_quadrics(VO,FO,EMAP,EF,EI,quadrics);
  per_vertex_point_to_plane_quadrics(VO,FO,EMAP,EF,EI,quadrics);
  // State variables keeping track of edge we just collapsed
  int v1 = -1;
  int v2 = -1;
  // Callbacks for computing and updating metric
  decimate_cost_and_placement_func cost_and_placement;
  decimate_pre_collapse_func       pre_collapse;
  decimate_post_collapse_func      post_collapse;
  SSP_vertexRemoval_optimal_collapse_edge_callbacks(
    E,quadrics,v1,v2, cost_and_placement, pre_collapse,post_collapse);
  // Call to greedy decimator
  bool ret = SSP_midpoint(
    VO, FO,
    cost_and_placement,
    max_faces_stopping_condition(m,orig_m,max_m),
    pre_collapse,
    post_collapse,
    E, EMAP, EF, EI,
    U, G, J, I, decInfo, decIM,FIM);
  // Remove phony boundary faces and clean up
  const Eigen::Array<bool,Eigen::Dynamic,1> keep = (J.array()<orig_m);
  igl::slice_mask(Eigen::MatrixXi(G),keep,1,G);
  igl::slice_mask(Eigen::VectorXi(J),keep,1,J);
  Eigen::VectorXi _1,I2;
  igl::remove_unreferenced(Eigen::MatrixXd(U),Eigen::MatrixXi(G),U,G,_1,I2);
  igl::slice(Eigen::VectorXi(I),I2,1,I);

  return ret;
}
