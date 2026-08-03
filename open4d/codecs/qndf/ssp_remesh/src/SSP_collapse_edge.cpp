#include "SSP_collapse_edge.h"
#include <igl/circulation.h>
#include <igl/edge_collapse_is_valid.h>
#include <always_try_never_care.h>
#include <vector>
#include <math.h>
#include <fstream>

void printVector(
  std::vector<int> vec)
  {
    for(int ii = 0; ii < vec.size(); ii++)
      std::cout << vec[ii] << ", ";
    std::cout << "\n";
  }

bool SSP_collapse_edge(
  const int e,
  const Eigen::RowVectorXd & p,
  /*const*/ std::vector<int> & Nsv,
  const std::vector<int> & Nsf,
  /*const*/ std::vector<int> & Ndv,
  const std::vector<int> & Ndf,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::MatrixXi & E,
  Eigen::VectorXi & EMAP,
  Eigen::MatrixXi & EF,
  Eigen::MatrixXi & EI,
  int & a_e1,
  int & a_e2,
  int & a_f1,
  int & a_f2,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM,
  single_collapse_data & data,
  Eigen::VectorXi & FIdx_onering_pre)
{
  // Assign this to 0 rather than, say, -1 so that deleted elements will get
  // draw as degenerate elements at vertex 0 (which should always exist and
  // never get collapsed to anything else since it is the smallest index)
  using namespace Eigen;
  using namespace std;
  using namespace igl;
  const int eflip = E(e,0)>E(e,1);
  // source and destination
  const int s = eflip?E(e,1):E(e,0);
  const int d = eflip?E(e,0):E(e,1);

  // if(!edge_collapse_is_valid(Nsv,Ndv))
  // {
  //   return false;
  // }

  vector<int> Nsv_alec = Nsv;
  vector<int> Ndv_alec = Ndv;
  if(!igl::edge_collapse_is_valid(Nsv_alec,Ndv_alec))
  {
    return false;
  }

  // ===================
  // Derek modifications:
  // ===================
  if ((decInfo.size()+1) % 100000 == 0)
    cout << "#collapses: " << decInfo.size()+1 << endl;
  bool isDebug = false;
  bool verbose = false;
  int vi = s;
  int vj = d;
  // int vi = E(e,0); // last one of Nsv
  // int vj = E(e,1); // last one of Ndv
  // {
  //   if (vj < vi)
  //   {
  //     int vtmp = vi;
  //     vi = vj;
  //     vj = vtmp;
  //   }
  // }
  // cout << "finish fliping Nsv, Ndv\n";

  // VectorXi FIdx_onering_pre, FIdx_onering_post;
  VectorXi FIdx_onering_post;
  MatrixXi F_onering_pre, F_onering_post;
  {
    // PROFC_NODE("dec: get 1-ring mesh");
    bool validEdge = get_collapse_onering_faces(V,F,vi,vj,Nsf,Ndf,
      FIdx_onering_pre,FIdx_onering_post,F_onering_pre,F_onering_post);
    if (validEdge == false)
    {
      return false;
    }
  }

  // get local mesh (V_pre, FUV_pre)
  MatrixXd V_pre;
  MatrixXi FUV_pre;
  VectorXi subsetVIdx;
  {
    // PROFC_NODE("dec: get local V");
    std::map<int, int> IM;
    remove_unreferenced_lessF(V,F_onering_pre,V_pre,FUV_pre,IM,subsetVIdx);
  }

  // get constraint vertices b for pre flattening
  // cout << "get constraint vertices b for pre flattening" << endl;
  VectorXi b(2);
  {
    // find vi (b(0)) and vj (b(1)) in subsetVIdx
    for (int ii=0; ii<subsetVIdx.size(); ii++){
      if (subsetVIdx(ii) == vi)
        b(0) = ii;
      else if (subsetVIdx(ii) == vj)
        b(1) = ii;
    }
    assert(b(0) < b(1));
  }

  // Post collapse:
  // get V_post
  MatrixXd V_post = V_pre;
  V_post.row(b(0)) = p;

  // get FUV_post
  MatrixXi FUV_post;
  VectorXi FUV_pre_keep;
  {
    // PROFC_NODE("dec: get local F");
    get_post_faces(FUV_pre, b(0), b(1), FUV_pre_keep, FUV_post);
  }

  if (isDebug) 
  {
    igl::writeOBJ("V_pre.obj", V_pre, FUV_pre);
    igl::writeOBJ("V_post.obj", V_post, FUV_post);
  }

  // get local Nsv, Ndv
  vector<int> Nsv_local, Ndv_local;
  {
    // PROFC_NODE("dec: get local Nv");
    int infVIdx = V.rows() - 1;

    // get local Nsv
    Nsv_local = Nsv;
    for (int ii=0; ii<Nsv_local.size(); ii++)
    {
      if (Nsv_local[ii] == infVIdx)
        Nsv_local[ii] = -1;
      else
      {
        for (int jj=0; jj<subsetVIdx.size(); jj++)
        {
          if (Nsv_local[ii] == subsetVIdx(jj))
            Nsv_local[ii] = jj;
        }
      }
    }

    // get local Ndv
    Ndv_local = Ndv;
    for (int ii=0; ii<Ndv_local.size(); ii++)
    {
      if (Ndv_local[ii] == infVIdx)
        Ndv_local[ii] = -1;
      else
      {
        for (int jj=0; jj<subsetVIdx.size(); jj++)
        {
          if (Ndv_local[ii] == subsetVIdx(jj))
            Ndv_local[ii] = jj;
        }
      }
    }
  }

  // joint flattening
  MatrixXd UV_pre, UV_post;
  {
    // PROFC_NODE("dec: joint lscm");
    bool isValid = true;
    isValid = joint_lscm(V_pre, FUV_pre, V_post, FUV_post, b(0), b(1), Nsv_local, Ndv_local, UV_pre, UV_post);
    if (!isValid)
      return false;
  }

  {
    if (FUV_pre.rows() <= 2)
    {
      if (verbose)
        cout << "too less faces" << endl;
      return false;
    }
  }
  // {
  //   PROFC_NODE("check triangle quality");
  //   // check UV_pre triangle quality
  //   for (int ii=0; ii<FIdx_onering_post.size(); ii++)
  //   {
  //     int fIdx = FIdx_onering_post(ii);
  //     int v0 = F(fIdx,0);
  //     int v1 = F(fIdx,1);
  //     int v2 = F(fIdx,2);
  //     double l0 = (V.row(v0) - V.row(v1)).norm();
  //     double l1 = (V.row(v1) - V.row(v2)).norm();
  //     double l2 = (V.row(v2) - V.row(v0)).norm();
  //     double x = (l0+l1+l2) / 2;
  //     double delta = sqrt(x * (x-l0) * (x-l1) * (x-l2));
  //     double triQ = 4 * sqrt(3) * delta / (l0*l0 + l1*l1 + l2*l2); 
  //     if (triQ < triangleQualityThreshold || isnan(triQ)) 
  //     {
  //       if (verbose)
  //         cout << "bad triangle quality" << endl;
  //       return false;
  //     }
  //   }
  // }
  // // TODO: check 3D triangle normal flip
  // if (collapsed == true)
  // {
  //   PROFC_NODE("check 3D face flip");
  //   MatrixXd FN_pre, FN_post;
  //   igl::per_face_normals(V_pre,FUV_pre,FN_pre);
  //   igl::per_face_normals(V_post,FUV_post,FN_post);

  //   for (int ii=0; ii<FUV_pre_keep.size(); ii++)
  //   {
  //     double dotProd = FN_pre.row(FUV_pre_keep(ii)).dot(FN_post.row(ii));
  //     // cout << "dot product: " << dotProd << endl;
  //     if (dotProd < 0.7) 
  //     {
  //       // if (verbose)
  //       // {
  //         cout << "3D face flip" << endl;
  //       // }
  //       collapsed = false;
  //       break;
  //     }
  //   }
  // }
  // if (verbose)
    // cout << "finish collapse checks\n";

  // single_collapse_data data;
  {
    data.b.resize(b.size());
    data.b = b;
    data.subsetVIdx.resize(subsetVIdx.size());
    data.subsetVIdx = subsetVIdx;
    data.V_pre.resize(V_pre.rows(), V_pre.cols()); data.V_pre = V_pre; // could be deleted
    data.V_post.resize(V_post.rows(), V_post.cols()); data.V_post = V_post; // could be deleted
    data.Nsv = Nsv_local; // could be deleted
    data.Ndv = Ndv_local; // could be deleted
    data.UV_pre.resize(UV_pre.rows(), UV_pre.cols()); data.UV_pre = UV_pre;
    data.UV_post.resize(UV_post.rows(), UV_post.cols()); data.UV_post = UV_post;
    data.FUV_pre.resize(FUV_pre.rows(), FUV_pre.cols()); data.FUV_pre = FUV_pre;
    data.FUV_post.resize(FUV_post.rows(), FUV_post.cols()); data.FUV_post = FUV_post;
    data.FIdx_pre = FIdx_onering_pre;
    data.FIdx_post = FIdx_onering_post;
  }
  // ===================
  // Derek modifications end
  // ===================

  // OVERLOAD: caller may have just computed this
  //
  // Important to grab neighbors of d before monkeying with edges
  const std::vector<int> & nV2Fd = (!eflip ? Nsf : Ndf);

  // The following implementation strongly relies on s<d
  assert(s<d && "s should be less than d");

  // move source and destination to placement
  V.row(s) = p;
  V.row(d) = p;

  // Helper function to replace edge and associate information with NULL
  const auto & kill_edge = [&E,&EI,&EF](const int e)
  {
    E(e,0) = IGL_COLLAPSE_EDGE_NULL;
    E(e,1) = IGL_COLLAPSE_EDGE_NULL;
    EF(e,0) = IGL_COLLAPSE_EDGE_NULL;
    EF(e,1) = IGL_COLLAPSE_EDGE_NULL;
    EI(e,0) = IGL_COLLAPSE_EDGE_NULL;
    EI(e,1) = IGL_COLLAPSE_EDGE_NULL;
  };

  // update edge info
  // for each flap
  const int m = F.rows();
  for(int side = 0;side<2;side++)
  {
    const int f = EF(e,side);
    const int v = EI(e,side);
    const int sign = (eflip==0?1:-1)*(1-2*side);
    // next edge emanating from d
    const int e1 = EMAP(f+m*((v+sign*1+3)%3));
    // prev edge pointing to s
    const int e2 = EMAP(f+m*((v+sign*2+3)%3));
    assert(E(e1,0) == d || E(e1,1) == d);
    assert(E(e2,0) == s || E(e2,1) == s);
    // face adjacent to f on e1, also incident on d
    const bool flip1 = EF(e1,1)==f;
    const int f1 = flip1 ? EF(e1,0) : EF(e1,1);
    assert(f1!=f);
    assert(F(f1,0)==d || F(f1,1)==d || F(f1,2) == d);
    // across from which vertex of f1 does e1 appear?
    const int v1 = flip1 ? EI(e1,0) : EI(e1,1);
    // Kill e1
    kill_edge(e1);
    // Kill f
    F(f,0) = IGL_COLLAPSE_EDGE_NULL;
    F(f,1) = IGL_COLLAPSE_EDGE_NULL;
    F(f,2) = IGL_COLLAPSE_EDGE_NULL;
    // map f1's edge on e1 to e2
    assert(EMAP(f1+m*v1) == e1);
    EMAP(f1+m*v1) = e2;
    // side opposite f2, the face adjacent to f on e2, also incident on s
    const int opp2 = (EF(e2,0)==f?0:1);
    assert(EF(e2,opp2) == f);
    EF(e2,opp2) = f1;
    EI(e2,opp2) = v1;
    // remap e2 from d to s
    E(e2,0) = E(e2,0)==d ? s : E(e2,0);
    E(e2,1) = E(e2,1)==d ? s : E(e2,1);
    if(side==0)
    {
      a_e1 = e1;
      a_f1 = f;
    }else
    {
      a_e2 = e1;
      a_f2 = f;
    }
  }

  // finally, reindex faces and edges incident on d. Do this last so asserts
  // make sense.
  //
  // Could actually skip first and last, since those are always the two
  // collpased faces. Nah, this is handled by (F(f,v) == d)
  //
  // Don't attempt to use Nde,Nse here because EMAP has changed
  {
    int p1 = -1;
    for(auto f : nV2Fd)
    {
      for(int v = 0;v<3;v++)
      {
        if(F(f,v) == d)
        {
          const int e1 = EMAP(f+m*((v+1)%3));
          const int flip1 = (EF(e1,0)==f)?1:0;
          assert( E(e1,flip1) == d || E(e1,flip1) == s);
          E(e1,flip1) = s;
          const int e2 = EMAP(f+m*((v+2)%3));
          // Skip if we just handled this edge (claim: this will be all except
          // for the first non-trivial face)
          if(e2 != p1)
          {
            const int flip2 = (EF(e2,0)==f)?0:1;
            assert( E(e2,flip2) == d || E(e2,flip2) == s);
            E(e2,flip2) = s;
          }

          F(f,v) = s;
          p1 = e1;
          break;
        }
      }
    }
  }
  // Finally, "remove" this edge and its information
  kill_edge(e);


  return true;
}

bool SSP_collapse_edge(
  const decimate_cost_and_placement_func & cost_and_placement,
  const decimate_pre_collapse_func       & pre_collapse,
  const decimate_post_collapse_func      & post_collapse,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F,
  Eigen::MatrixXi & E,
  Eigen::VectorXi & EMAP,
  Eigen::MatrixXi & EF,
  Eigen::MatrixXi & EI,
  min_heap< std::tuple<double,int,int> > & Q,
  Eigen::VectorXi & EQ,
  Eigen::MatrixXd & C,
  int & e,
  int & e1,
  int & e2,
  int & f1,
  int & f2,
  std::vector<single_collapse_data> & decInfo,
  std::vector<std::vector<int>> & decIM)
{
  using namespace Eigen;
  using namespace igl;
  using namespace std;

  std::tuple<double,int,int> p;
  while(true)
  {
    // Check if Q is empty
    if(Q.empty())
    {
      // no edges to collapse
      return false;
    }
    // pop from Q
    p = Q.top();
    if(std::get<0>(p) == std::numeric_limits<double>::infinity())
    {
      // min cost edge is infinite cost
      return false;
    }
    Q.pop();
    e = std::get<1>(p);
    // Check if matches timestamp
    if(std::get<2>(p) == EQ(e))
    {
      break;
    }
    // must be stale or dead.
    assert(std::get<2>(p)  < EQ(e) || EQ(e) == -1);
    // try again.
  }

  // Why is this computed up here?
  // If we just need original face neighbors of edge, could we gather that more
  // directly than gathering face neighbors of each vertex?
  std::vector<int> /*Nse,*/Nsf,Nsv;
  circulation(e, true,F,EMAP,EF,EI,/*Nse,*/Nsv,Nsf);
  std::vector<int> /*Nde,*/Ndf,Ndv;
  circulation(e, false,F,EMAP,EF,EI,/*Nde,*/Ndv,Ndf);

  bool collapsed = true;
  single_collapse_data data;
  VectorXi FIdx_onering_pre;
  if(pre_collapse(V,F,E,EMAP,EF,EI,Q,EQ,C,e))
  {
    collapsed = SSP_collapse_edge(
      e,C.row(e),
      Nsv,Nsf,Ndv,Ndf,
      V,F,E,EMAP,EF,EI,e1,e2,f1,f2,decInfo,decIM,data,FIdx_onering_pre);
  }else
  {
    collapsed = false;
  }
  
  // cout << "start post collapse" << endl;
  post_collapse(V,F,E,EMAP,EF,EI,Q,EQ,C,e,e1,e2,f1,f2,collapsed);
  if(collapsed)
  {
    // ===================
    // Derek modifications:
    // ===================
    decInfo.push_back(data);

    // contruct index map for fast query
    for (int ii = 0; ii < FIdx_onering_pre.size(); ii++)
      decIM[FIdx_onering_pre(ii)].push_back(decInfo.size() - 1);
    // ===================
    // Derek modifications end
    // ===================

    // Erase the two, other collapsed edges by marking their timestamps as -1
    EQ(e1) = -1;
    EQ(e2) = -1;
    // TODO: visits edges multiple times, ~150% more updates than should
    //
    // update local neighbors
    // loop over original face neighbors
    //
    // Can't use previous computed Nse and Nde because those refer to EMAP
    // before it was changed...
    std::vector<int> Nf;
    Nf.reserve( Nsf.size() + Ndf.size() ); // preallocate memory
    Nf.insert( Nf.end(), Nsf.begin(), Nsf.end() );
    Nf.insert( Nf.end(), Ndf.begin(), Ndf.end() );
    // https://stackoverflow.com/a/1041939/148668
    std::sort( Nf.begin(), Nf.end() );
    Nf.erase( std::unique( Nf.begin(), Nf.end() ), Nf.end() );
    // Collect all edges that must be updated
    std::vector<int> Ne;
    Ne.reserve(3*Nf.size());
    for(auto & n : Nf)
    {
      if(F(n,0) != IGL_COLLAPSE_EDGE_NULL ||
          F(n,1) != IGL_COLLAPSE_EDGE_NULL ||
          F(n,2) != IGL_COLLAPSE_EDGE_NULL)
      {
        for(int v = 0;v<3;v++)
        {
          // get edge id
          const int ei = EMAP(v*F.rows()+n);
          Ne.push_back(ei);
        }
      }
    }
    // Only process edge once
    std::sort( Ne.begin(), Ne.end() );
    Ne.erase( std::unique( Ne.begin(), Ne.end() ), Ne.end() );
    for(auto & ei : Ne)
    {
       // compute cost and potential placement
       double cost;
       RowVectorXd place;
       cost_and_placement(ei,V,F,E,EMAP,EF,EI,cost,place);
       // Increment timestamp
       EQ(ei)++;
       // Replace in queue
       Q.emplace(cost,ei,EQ(ei));
       C.row(ei) = place;
    }
    // cout << "end of a collapse" << endl;
  }else
  {
    // cout << "remove from queue" << endl;
    // reinsert with infinite weight (the provided cost function must **not**
    // have given this un-collapsable edge inf cost already)
    // Increment timestamp
    EQ(e)++;
    // Replace in queue
    Q.emplace(std::numeric_limits<double>::infinity(),e,EQ(e));
  }
  return collapsed;
}
