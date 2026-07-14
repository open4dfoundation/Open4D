#include "sort_vec.h"

void sort_vec(
  const Eigen::VectorXd & vec, 
  Eigen::VectorXd & sorted_vec,  
  Eigen::VectorXi & ind)
{
  // https://www.programmersought.com/article/343692646/
  using namespace Eigen;
  using namespace std;

  ind=VectorXi::LinSpaced(vec.size(),0,vec.size()-1);//[0 1 2 3 ... N-1]
  auto rule=[vec](int i, int j)->bool{
    return vec(i)<vec(j);
  }; // regular expression, as a predicate of sort
  std::sort(ind.data(),ind.data()+ind.size(),rule);
  //The data member function returns a pointer to the first element of VectorXd, similar to begin()
  sorted_vec.resize(vec.size());
  for(int i=0;i<vec.size();i++){
    sorted_vec(i)=vec(ind(i));
  }
}

void sort_vec(
  const Eigen::VectorXi & vec, 
  Eigen::VectorXi & sorted_vec,  
  Eigen::VectorXi & ind)
{
  // https://www.programmersought.com/article/343692646/
  using namespace Eigen;
  using namespace std;

  ind=VectorXi::LinSpaced(vec.size(),0,vec.size()-1);//[0 1 2 3 ... N-1]
  auto rule=[vec](int i, int j)->bool{
    return vec(i)<vec(j);
  }; // regular expression, as a predicate of sort
  std::sort(ind.data(),ind.data()+ind.size(),rule);
  //The data member function returns a pointer to the first element of VectorXd, similar to begin()
  sorted_vec.resize(vec.size());
  for(int i=0;i<vec.size();i++){
    sorted_vec(i)=vec(ind(i));
  }
}