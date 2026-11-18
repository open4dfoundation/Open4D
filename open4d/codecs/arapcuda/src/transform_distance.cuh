// Transform distance between two rigid transforms, measured over the cells of
// one center (TransformDistance.GetTransformDistance in the C# tracker).
//
// D(i, j) = | 2 qtq_i + 2 d.(R_i s_i) - 2 d.(R_j s_i) + n_i d.d - 2 <R_i^T R_j, Q_i> | / n_i
// with d = t_i - t_j, s_i = sum q, Q_i = sum q q^T, qtq_i = sum q^T q over the cells
// of center i. The terms are large and nearly cancel, so they are evaluated in
// double.
#pragma once

#include <cuda_runtime.h>

namespace arap {

constexpr int CENTER_STRIDE = 13;     // qtq, s[3], Q[9] (row major)
constexpr int TRANSFORM_STRIDE = 12;  // R[9] (row major), t[3]

__host__ __device__ inline float transformDistance(const int* counts, const float* centers, const float* transforms,
                                                   int i, int j)
{
    const float* c = centers + (size_t)CENTER_STRIDE * i;
    const float* A = transforms + (size_t)TRANSFORM_STRIDE * i;
    const float* B = transforms + (size_t)TRANSFORM_STRIDE * j;

    // identical transforms are exactly zero apart (the formula would leave rounding noise)
    bool same = true;
    for (int k = 0; k < TRANSFORM_STRIDE; k++)
        same = same && A[k] == B[k];
    if (same)
        return 0.0f;

    double qtq = c[0];
    double s[3] = { c[1], c[2], c[3] };
    double d[3] = { (double)A[9] - B[9], (double)A[10] - B[10], (double)A[11] - B[11] };

    double green = 0, blue = 0;
    for (int r = 0; r < 3; r++)
    {
        double ra = A[3 * r] * s[0] + A[3 * r + 1] * s[1] + A[3 * r + 2] * s[2];
        double rb = B[3 * r] * s[0] + B[3 * r + 1] * s[1] + B[3 * r + 2] * s[2];
        green += d[r] * ra;
        blue += d[r] * rb;
    }

    // <R_i^T R_j, Q_i>_F = sum_ab (sum_k R_i[k][a] R_j[k][b]) Q[a][b]
    double pink = 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
        {
            double rtr = (double)A[a] * B[b] + (double)A[3 + a] * B[3 + b] + (double)A[6 + a] * B[6 + b];
            pink += rtr * c[4 + 3 * a + b];
        }

    double n = counts[i];
    double res = 2 * qtq + 2 * green - 2 * blue + n * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - 2 * pink;
    return (float)fabs(res / n);
}

} // namespace arap
