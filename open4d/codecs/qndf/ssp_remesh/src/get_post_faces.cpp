#include "get_post_faces.h"

void get_post_faces(
	const Eigen::MatrixXi & F_pre,
	const int & vi,
	const int & vj,
  Eigen::VectorXi & f_keep,
	Eigen::MatrixXi & F_post)
{
  using namespace std;
  using namespace Eigen;

  // find the faces to keep
	vector<int> f_keep_vec;
	f_keep_vec.reserve(F_pre.rows());
	for (int ii=0; ii<F_pre.rows(); ii++)
	{
		int v0 = F_pre(ii,0);
		int v1 = F_pre(ii,1);
		int v2 = F_pre(ii,2);
		if ((v0!=vi && v1!=vi && v2!=vi)  ||
				(v0!=vj && v1!=vj && v2!=vj)) // if not contain vi or not contain vj
			f_keep_vec.push_back(ii);
	}

  f_keep.resize(f_keep_vec.size());
	f_keep = Map<VectorXi, Unaligned>(f_keep_vec.data(), f_keep_vec.size());
  
  igl::slice(F_pre,f_keep,1,F_post);
  for (int r=0; r<F_post.rows(); r++){
    for (int c=0; c<F_post.cols(); c++){
      if (F_post(r,c) == vj)
        F_post(r,c) = vi;
    }
  }
  assert(F_pre.rows()-2 <= F_post.rows());
}