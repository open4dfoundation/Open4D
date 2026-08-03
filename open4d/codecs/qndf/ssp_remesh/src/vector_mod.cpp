#include "vector_mod.h"

void vector_mod(
  const Eigen::VectorXi & input,
  const int & number,
  Eigen::VectorXi & input_mod_number)
  {
    input_mod_number.resize(input.size());
    input_mod_number = input.array() - (number * (input.array()/number));
  }