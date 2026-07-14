#include "SSP_random_midpoint.h"

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
  Eigen::VectorXi & FIM)
{
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
  Eigen::VectorXi EMAP;
  Eigen::MatrixXi E,EF,EI;
  edge_flaps(FO,E,EMAP,EF,EI);
  // decimate will not work correctly on non-edge-manifold meshes. By extension
  // this includes meshes with non-manifold vertices on the boundary since these
  // will create a non-manifold edge when connected to infinity.
  {
    Eigen::Array<bool,Eigen::Dynamic,Eigen::Dynamic> BF;
    Eigen::Array<bool,Eigen::Dynamic,1> BE;
    if(!is_edge_manifold(FO,E.rows(),EMAP,BF,BE))
    {
      return false;
    }
  }
  decimate_pre_collapse_func always_try;
  decimate_post_collapse_func never_care;
  always_try_never_care(always_try,never_care);
  bool ret = SSP_random_midpoint(
    VO,
    FO,
    shortest_edge_and_midpoint,
    max_faces_stopping_condition(m,orig_m,max_m),
    always_try,
    never_care,
    E,
    EMAP,
    EF,
    EI,
    U,
    G,
    J,
    I,
    decInfo, decIM, FIM);
  const Eigen::Array<bool,Eigen::Dynamic,1> keep = (J.array()<orig_m);
  igl::slice_mask(Eigen::MatrixXi(G),keep,1,G);
  igl::slice_mask(Eigen::VectorXi(J),keep,1,J);
  Eigen::VectorXi _1,I2;
  igl::remove_unreferenced(Eigen::MatrixXd(U),Eigen::MatrixXi(G),U,G,_1,I2);
  igl::slice(Eigen::VectorXi(I),I2,1,I);
  return ret;
}

bool SSP_random_midpoint(
  const Eigen::MatrixXd & OV,
  const Eigen::MatrixXi & OF,
  const decimate_cost_and_placement_func & cost_and_placement,
  const decimate_stopping_condition_func & stopping_condition,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J,
  Eigen::VectorXi & I,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM,
  Eigen::VectorXi & FIM)
{
  decimate_pre_collapse_func always_try;
  decimate_post_collapse_func never_care;
  always_try_never_care(always_try,never_care);
  return SSP_random_midpoint(
    OV,OF,cost_and_placement,stopping_condition,always_try,never_care,U,G,J,I,decInfo, decIM, FIM);
}

bool SSP_random_midpoint(
  const Eigen::MatrixXd & OV,
  const Eigen::MatrixXi & OF,
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
  Eigen::VectorXi & FIM)
{
  Eigen::VectorXi EMAP;
  Eigen::MatrixXi E,EF,EI;
  edge_flaps(OF,E,EMAP,EF,EI);
  return SSP_random_midpoint(
    OV,OF,
    cost_and_placement,stopping_condition,pre_collapse,post_collapse,
    E,EMAP,EF,EI,
    U,G,J,I,decInfo, decIM, FIM);
}

bool SSP_random_midpoint(
  const Eigen::MatrixXd & OV,
  const Eigen::MatrixXi & OF,
  const decimate_cost_and_placement_func & cost_and_placement,
  const decimate_stopping_condition_func & stopping_condition,
  const decimate_pre_collapse_func       & pre_collapse,
  const decimate_post_collapse_func      & post_collapse,
  const Eigen::MatrixXi & OE,
  const Eigen::VectorXi & OEMAP,
  const Eigen::MatrixXi & OEF,
  const Eigen::MatrixXi & OEI,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J,
  Eigen::VectorXi & I,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM,
  Eigen::VectorXi & FIM)
{
  // Decimate 1
  using namespace Eigen;
  using namespace std;
  using namespace igl;
  // Working copies
  Eigen::MatrixXd V = OV;
  Eigen::MatrixXi F = OF;
  VectorXi EMAP;
  MatrixXi E,EF,EI;
  edge_flaps(F,E,EMAP,EF,EI);
  {
    Eigen::Array<bool,Eigen::Dynamic,Eigen::Dynamic> BF;
    Eigen::Array<bool,Eigen::Dynamic,1> BE;
    if(!is_edge_manifold(F,E.rows(),EMAP,BF,BE))
    {
      return false;
    }
  }

  igl::min_heap<std::tuple<double,int,int> > Q;
  // Could reserve with https://stackoverflow.com/a/29236236/148668
  Eigen::VectorXi EQ = Eigen::VectorXi::Zero(E.rows());
  // If an edge were collapsed, we'd collapse it to these points:
  MatrixXd C(E.rows(),V.cols());
  // Pushing into a vector then using constructor was slower. Maybe using
  // std::move + make_heap would squeeze out something?
  
  // Separating the cost/placement evaluation from the Q filling is a
  // performance hit for serial but faster if we can parallelize the
  // cost/placement.
  {
    Eigen::VectorXd costs(E.rows());
    igl::parallel_for(E.rows(),[&](const int e)
    {
      double cost = e;
      RowVectorXd p(1,3);
      cost_and_placement(e,V,F,E,EMAP,EF,EI,cost,p);
      C.row(e) = p;
      costs(e) = cost;
    },10000);
    for(int e = 0;e<E.rows();e++)
    {
      Q.emplace(costs(e),e,0);
    }
  }


  int prev_e = -1;
  bool clean_finish = false;

  while(true)
  {
    if(Q.empty())
    {
      break;
    }
    if(std::get<0>(Q.top()) == std::numeric_limits<double>::infinity())
    {
      // min cost edge is infinite cost
      break;
    }
    int e,e1,e2,f1,f2;
    if(SSP_random_collapse_edge(
      cost_and_placement, pre_collapse, post_collapse,
      V,F,E,EMAP,EF,EI,Q,EQ,C,e,e1,e2,f1,f2,decInfo,decIM))
    {
      if(stopping_condition(V,F,E,EMAP,EF,EI,Q,EQ,C,e,e1,e2,f1,f2))
      {
        clean_finish = true;
        break;
      }
    }else
    {
      if(prev_e == e)
      {
        assert(false && "Edge collapse no progress... bad stopping condition?");
        break;
      }
      // Edge was not collapsed... must have been invalid. collapse_edge should
      // have updated its cost to inf... continue
    }
    prev_e = e;
  }
  // remove all IGL_COLLAPSE_EDGE_NULL faces
  MatrixXi F2(F.rows(),3);
  J.resize(F.rows());
  FIM.resize(F.rows()); 
  FIM.setZero(); 
  int m = 0;
  for(int f = 0;f<F.rows();f++)
  {
    if(
      F(f,0) != IGL_COLLAPSE_EDGE_NULL || 
      F(f,1) != IGL_COLLAPSE_EDGE_NULL || 
      F(f,2) != IGL_COLLAPSE_EDGE_NULL)
    {
      F2.row(m) = F.row(f);
      J(m) = f;
      FIM(f) = m;
      m++;
    }
  }
  F2.conservativeResize(m,F2.cols());
  J.conservativeResize(m);
  VectorXi _1;
  igl::remove_unreferenced(V,F2,U,G,_1,I);
  return clean_finish;
}
