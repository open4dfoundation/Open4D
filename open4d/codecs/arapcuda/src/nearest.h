// Internal: nearest-center assignment on device-resident points.
#pragma once

#include "kernels.h"

#include <cuda_runtime.h>

namespace arap {

struct AssignResult
{
    const int* counts;     // n
    const double* sums;    // 3n
    const double* second;  // 6n, or null
};

// Assigns every occupied cell to its nearest point (ties to the lower index) and
// accumulates per-point counts, sums and, if second, second moments into the
// grid's device buffers (valid until the next call on this grid).
// Asynchronous on the default stream.
int assignDevice(ArapGrid* g, const float4* points, int n, bool second, AssignResult* out);

} // namespace arap
