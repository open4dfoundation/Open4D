#ifndef ALWAYS_TRY_NEVER_CARE_H
#define ALWAYS_TRY_NEVER_CARE_H
#include "decimate_func_types.h"
  // Outputs:
  //   always_try  function that always returns true
  //   never_care  fuction that is always a no-op
  void always_try_never_care(
    decimate_pre_collapse_func  & always_try,
    decimate_post_collapse_func & never_care);

#endif 

