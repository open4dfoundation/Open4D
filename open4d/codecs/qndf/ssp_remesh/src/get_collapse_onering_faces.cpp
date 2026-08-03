#include "get_collapse_onering_faces.h"

bool get_collapse_onering_faces(
  const Eigen::MatrixXd & V,
	const Eigen::MatrixXi & F,
	const int & vi,
	const int & vj,
	const std::vector<int> & Nsf,
	const std::vector<int> & Ndf,
	Eigen::VectorXi & FIdx_ring_pre,
	Eigen::VectorXi & FIdx_ring_post,
	Eigen::MatrixXi & F_ring_pre,
	Eigen::MatrixXi & F_ring_post)
{
  using namespace std;
	using namespace Eigen;
	bool isDebug = true;

	// make sure this is not a virtual edge
	if (isinf(V(vi,0)) || isinf(V(vj,0))) 
	{
		cout << "this is virtual edge\n";
		return false;
	}
	// make sure this is a valid edge
	if (vi == vj) 
	{
		cout << "this isn't a valid edge\n";
		return false;
	}

	// get local face indices
	vector<int> FIdx_ring_pre_duplicated;
	FIdx_ring_pre_duplicated.reserve(Nsf.size() + Ndf.size());

	// process Nsf  
	for (int ii = 0; ii<Nsf.size(); ii++)
	{
		// cout << Nsf[ii] << endl;
		int fIdx = Nsf[ii];
		int v0 = F(fIdx,0);
		int v1 = F(fIdx,1);
		int v2 = F(fIdx,2);
		bool is_in_vij_NF = true;
		if (   v0==IGL_COLLAPSE_EDGE_NULL 
				&& v1==IGL_COLLAPSE_EDGE_NULL
				&& v2==IGL_COLLAPSE_EDGE_NULL) // the invalid face
			is_in_vij_NF = false;
		if (isinf(V(v0,0)) || isinf(V(v1,0)) || isinf(V(v2,0))) // if this face contains virtual vertex
			is_in_vij_NF = false;
		if (v0!=vi && v1!=vi && v2!=vi && v0!=vj && v1!=vj && v2!=vj) // not conatin vi or vj
			is_in_vij_NF = false;
		if (is_in_vij_NF == true)
			FIdx_ring_pre_duplicated.push_back(fIdx);
	}

	// process Ndf  
	for (int ii = 0; ii<Ndf.size(); ii++)
	{
		// cout << Nsf[ii] << endl;
		int fIdx = Ndf[ii];
		int v0 = F(fIdx,0);
		int v1 = F(fIdx,1);
		int v2 = F(fIdx,2);
		bool is_in_vij_NF = true;
		if (   v0==IGL_COLLAPSE_EDGE_NULL 
				&& v1==IGL_COLLAPSE_EDGE_NULL
				&& v2==IGL_COLLAPSE_EDGE_NULL) // the invalid face
			is_in_vij_NF = false;
		if (isinf(V(v0,0)) || isinf(V(v1,0)) || isinf(V(v2,0))) // if this face contains virtual vertex
			is_in_vij_NF = false;
		if (v0!=vi && v1!=vi && v2!=vi && v0!=vj && v1!=vj && v2!=vj) // not conatin vi or vj
			is_in_vij_NF = false;
		if (is_in_vij_NF == true)
			FIdx_ring_pre_duplicated.push_back(fIdx);
	}

	// std vector to eigen vector
	VectorXi tmp;
	tmp.resize(FIdx_ring_pre_duplicated.size());
	tmp = Map<VectorXi, Unaligned>(FIdx_ring_pre_duplicated.data(), FIdx_ring_pre_duplicated.size());

	// extract unique eles
	igl::unique(tmp, FIdx_ring_pre);
	igl::slice(F,FIdx_ring_pre,1,F_ring_pre);

	// find the faces to keep
  VectorXi f_keep;
  get_post_faces(F_ring_pre, vi,vj, f_keep, F_ring_post);

  igl::slice(FIdx_ring_pre,f_keep,1,FIdx_ring_post);
	// if (FIdx_ring_pre.size() == FIdx_ring_post.size())
	// {
	// 	cout << "======\n";
	// 	cout << "F_ring_pre: \n" << F_ring_pre << endl;
	// 	cout << "F_ring_post: \n" << F_ring_post << endl;
	// 	cout << "vi: " << vi << ", vj: " << vj << endl;
	// 	cout << "FIdx_ring_pre: " << FIdx_ring_pre.transpose() << endl;
	// 	cout << "FIdx_ring_post: " << FIdx_ring_post.transpose() << endl;
	// }

	// // get adjacent face indices to either vi or vj
	// vector<int> vij_NF;
	// vij_NF.reserve(30); // reserve a big enough size for it
	// for (int fIdx=0; fIdx<F.rows(); fIdx++)
	// {
	// 	int v0 = F(fIdx,0);
	// 	int v1 = F(fIdx,1);
	// 	int v2 = F(fIdx,2);
	// 	bool is_in_vij_NF = true;
	// 	if (   v0==IGL_COLLAPSE_EDGE_NULL 
	// 		  && v1==IGL_COLLAPSE_EDGE_NULL
	// 		  && v2==IGL_COLLAPSE_EDGE_NULL) // the invalid face
	// 		is_in_vij_NF = false;
	// 	if (isinf(V(v0,0)) || isinf(V(v1,0)) || isinf(V(v2,0))) // if this face contains virtual vertex
	// 		is_in_vij_NF = false;
	// 	if (v0!=vi && v1!=vi && v2!=vi && v0!=vj && v1!=vj && v2!=vj) // not conatin vi or vj
	// 		is_in_vij_NF = false;
	// 	if (is_in_vij_NF == true)
	// 		vij_NF.push_back(fIdx);
	// }

	// // assign it to FIdx_ring_pre and F_ring_pre
	// FIdx_ring_pre.resize(vij_NF.size());
	// FIdx_ring_pre = Map<VectorXi, Unaligned>(vij_NF.data(), vij_NF.size());
	// igl::slice(F,FIdx_ring_pre,1,F_ring_pre);

	// // find the faces to keep
  // VectorXi f_keep;
  // get_post_faces(F_ring_pre, vi,vj, f_keep, F_ring_post);
  // igl::slice(FIdx_ring_pre,f_keep,1,FIdx_ring_post);

	return true;
}
