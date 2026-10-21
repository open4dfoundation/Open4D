#include "intersect_ordered.h"

void intersect_ordered(
  const Eigen::VectorXi & A,
  const Eigen::VectorXi & B,
  Eigen::VectorXi & C,
  Eigen::VectorXi & AinB)
  {
    using namespace Eigen;
    VectorXi tmpC(A.size() > B.size() ? A.size() : B.size());
    VectorXi tmpAinB(A.size() > B.size() ? A.size() : B.size());

    // count of intersects
    int c = 0;

    // Loop over A
    for(int i = 0;i<A.size();i++)
    {
      // Loop over B
      for(int j = 0;j<B.size();j++)
      {
        if(A(i) == B(j))
        {
          tmpC(c) = A(i);
          tmpAinB(c) = j;
          c++;
        }
      }
    }

    // resize output
    C.resize(c);
    AinB.resize(c);

    // Loop over intersects
    for(int i = 0;i<c;i++)
    {
      C(i) = tmpC(i);
      AinB(i) = tmpAinB(i);
    }

  }
