#include "SSP_decimate.h"

bool SSP_decimate(
    const Eigen::MatrixXd & VO,
    const Eigen::MatrixXi & FO,
    const size_t & tarF,
    const int & dec_type,
    Eigen::MatrixXd & V,
    Eigen::MatrixXi & F,
    Eigen::VectorXi & IMF,
    Eigen::VectorXi & IM,
    std::vector<single_collapse_data> & decInfo,
    std::vector<std::vector<int>> & decIM,
    Eigen::VectorXi & FIM)
{
  using namespace std;
  bool returnVal = true;

  Eigen::VectorXi BI;
  if (!igl::is_vertex_manifold(FO, BI) || !igl::is_edge_manifold(FO)) {
    cout << "input mesh is not manifold\n";
    return false;
  }

  switch (dec_type)
  {
    case 1: // mid point 
      cout << "uniform decimation \n";
      returnVal = SSP_midpoint(VO,FO,tarF, V,F,IMF, IM, decInfo, decIM, FIM);
      break;
    case 2: // vertex removal
      cout << "vertex removal \n";
      returnVal = SSP_vertexRemoval(VO,FO,tarF, V,F,IMF, IM, decInfo, decIM, FIM);
      break;
    default:
      cout << "qslim \n";
      returnVal = SSP_qslim(VO,FO,tarF, V,F,IMF, IM, decInfo, decIM, FIM);
  }
  return returnVal;
}
