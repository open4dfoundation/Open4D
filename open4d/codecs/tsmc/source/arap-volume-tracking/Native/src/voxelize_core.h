// Per-triangle / per-column voxelization logic shared by the CUDA kernels and
// the host backend. The arithmetic mirrors TriangleMesh.PointInTriangle2 and
// TriangleMesh.IntersectZ operation by operation (float differences widened to
// double for the inside test, float for the depth), so results are bit-identical
// as long as nothing is contracted into FMAs (-fmad=false / -ffp-contract=off).
#pragma once

#include <stdint.h>

#ifdef __CUDACC__
#define ARAP_HD __host__ __device__ __forceinline__
#else
#define ARAP_HD inline
#endif

namespace arap {

struct float3_ { float x, y, z; };

ARAP_HD bool pointInTriangle2(float3_ p1, float3_ p2, float3_ p3, float qX, float qY)
{
    double e1x = (double)(p2.x - p1.x);
    double e1y = (double)(p2.y - p1.y);
    double e2x = (double)(p3.x - p1.x);
    double e2y = (double)(p3.y - p1.y);
    double qx = (double)(qX - p1.x);
    double qy = (double)(qY - p1.y);
    double a = e1x * e2y - e1y * e2x;
    // Math.Abs(a) < double.Epsilon; double.Epsilon is the smallest denormal,
    // so this only rejects a == 0
    if (a == 0.0)
        return false;
    double alpha = (e1x * qy - e1y * qx) / a;
    if (alpha < 0)
        return false;
    double beta = (qx * e2y - qy * e2x) / a;
    if (beta < 0)
        return false;
    if ((1 - alpha - beta) < 0)
        return false;
    return true;
}

ARAP_HD float intersectZ(float3_ p1, float3_ p2, float3_ p3, float qX, float qY)
{
    float v1x = p2.x - p1.x, v1y = p2.y - p1.y, v1z = p2.z - p1.z;
    float v2x = p3.x - p1.x, v2y = p3.y - p1.y, v2z = p3.z - p1.z;
    // Vector3.Cross
    float nx = v1y * v2z - v1z * v2y;
    float ny = v1z * v2x - v1x * v2z;
    float nz = v1x * v2y - v1y * v2x;
    // Vector3.Dot (dpps sums ((x + y) + z))
    float d = -((p1.x * nx + p1.y * ny) + p1.z * nz);
    return ((-d - nx * qX) - ny * qY) / nz;
}

// First cell k with depth < zf[k] (zf is non-decreasing), or ZRes if none.
// NaN depths compare false everywhere and yield ZRes, like InsideTest2.
ARAP_HD int firstCellAbove(const float* zf, int ZRes, float depth)
{
    int lo = 0, hi = ZRes;
    while (lo < hi)
    {
        int mid = (lo + hi) >> 1;
        if (depth < zf[mid]) hi = mid; else lo = mid + 1;
    }
    return lo;
}

// Column index range [lo, hi] whose centers c[i] may lie in [vmin, vmax].
// One extra column on each side absorbs rounding in the inside test; the exact
// test is still applied to every candidate column.
ARAP_HD void columnRange(float vmin, float vmax, float origin, float a, int res, int& lo, int& hi)
{
    float fl = (vmin - origin) / a - 0.5f;
    float fh = (vmax - origin) / a - 0.5f;
    if (!(fl == fl) || !(fh == fh) || fl < -1e9f || fh > 1e9f)
    {
        lo = 0; hi = res - 1;   // non-finite input: fall back to every column
        return;
    }
    lo = (int)floorf(fl) - 1;
    hi = (int)ceilf(fh) + 1;
    if (lo < 0) lo = 0;
    if (hi > res - 1) hi = res - 1;
}

// Inclusive prefix XOR of the bits of w (bit j becomes XOR of bits 0..j).
ARAP_HD uint32_t prefixXor32(uint32_t w)
{
    w ^= w << 1;
    w ^= w << 2;
    w ^= w << 4;
    w ^= w << 8;
    w ^= w << 16;
    return w;
}

} // namespace arap
