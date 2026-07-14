#include "remove_vector_element.h"

void remove_vector_element(
  const int idxToRemove,
  Eigen::VectorXd & vec)
  {
    unsigned int numEles = vec.size() - 1;

    if( idxToRemove < numEles )
        vec.segment(idxToRemove,numEles-idxToRemove) = vec.segment(idxToRemove+1,numEles-idxToRemove);

    vec.conservativeResize(numEles);
  }

void remove_vector_element(
  const int idxToRemove,
  Eigen::VectorXi & vec)
  {
    unsigned int numEles = vec.size() - 1;

    if( idxToRemove < numEles )
        vec.segment(idxToRemove,numEles-idxToRemove) = vec.segment(idxToRemove+1,numEles-idxToRemove);

    vec.conservativeResize(numEles);
  }


// template <class M>
// void remove_vector_element(
//   const int & idxToRemove,
//   M & vec)
//   {
//     unsigned int numEles = vec.size() - 1;

//     if( idxToRemove < numEles )
//         vec.segment(idxToRemove,numEles-idxToRemove) = vec.segment(idxToRemove+1,numEles-idxToRemove);

//     vec.conservativeResize(numEles);
//   }
