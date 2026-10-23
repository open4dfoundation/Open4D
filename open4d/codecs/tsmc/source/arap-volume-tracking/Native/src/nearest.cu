// Nearest-point assignment of every occupied cell, with per-point moments.
// Replaces the KD-tree voxel scans in MemoryEfficientVolumeGrid.LloydGradient,
// LloydStats and GetPoints (+ the TransformDistance sums built from it).
//
// Cells are processed in 4 x 4 x 16 bricks, one thread block per brick. A brick
// first keeps only the points that can be nearest to some cell in it: with U the
// smallest farthest-distance from any point to the brick's box, a point whose
// nearest distance to the box exceeds U can never win. Each cell then searches
// just those candidates, so the result is the exact nearest point.

#include "common.h"

#include <cfloat>
#include <vector>

struct ArapGrid
{
    int X, Y, Z;
    float minX, minY, minZ, a;
    unsigned char* bits;      // device
    size_t nBytes;
    // per-call buffers, grown on demand
    int capacity;
    float4* points;
    int* counts;
    double* sums;
    double* second;
};

namespace arap {

constexpr int BX = 4, BY = 4, BZ = 16;
constexpr int BRICK = BX * BY * BZ;     // threads per block
constexpr int MAX_CANDIDATES = 1024;

// VolumeGrid.Int2xyz: r.X += x * a + a / 2 (float, no FMA)
__device__ __forceinline__ float cellCenter(float origin, int i, float a)
{
    return origin + ((float)i * a + a / 2);
}

__device__ __forceinline__ float axisMax(float c, float lo, float hi)
{
    float d = fmaxf(fabsf(c - lo), fabsf(c - hi));
    return d * d;
}

__device__ __forceinline__ float axisMin(float c, float lo, float hi)
{
    float d = c < lo ? lo - c : (c > hi ? c - hi : 0.0f);
    return d * d;
}

__device__ __forceinline__ double warpSum(double v)
{
    for (int o = 16; o > 0; o >>= 1)
        v += __shfl_down_sync(0xFFFFFFFFu, v, o);
    return v;
}

template <bool SECOND>
__global__ void __launch_bounds__(BRICK)
assignKernel(const unsigned char* __restrict__ bits, int X, int Y, int Z,
             float minX, float minY, float minZ, float a,
             const float4* __restrict__ pts, int n,
             int* counts, double* sums, double* second)
{
    __shared__ unsigned int sU;          // float bits; non-negative floats order like uints
    __shared__ int sCount;
    __shared__ int sCand[MAX_CANDIDATES];

    int bricksX = (X + BX - 1) / BX, bricksY = (Y + BY - 1) / BY;
    int b = blockIdx.x;
    int x0 = (b % bricksX) * BX;
    int y0 = ((b / bricksX) % bricksY) * BY;
    int z0 = (b / (bricksX * bricksY)) * BZ;

    int t = threadIdx.x;
    int x = x0 + t / (BY * BZ);
    int y = y0 + (t / BZ) % BY;
    int z = z0 + t % BZ;

    bool occupied = false;
    if (x < X && y < Y && z < Z)
    {
        size_t index = ((size_t)x * Y + y) * Z + z;
        occupied = (bits[index >> 3] >> (index & 7)) & 1;
    }
    if (!__syncthreads_or(occupied))
        return;

    // bounding box of the brick's cell centers
    float loX = cellCenter(minX, x0, a), hiX = cellCenter(minX, min(x0 + BX, X) - 1, a);
    float loY = cellCenter(minY, y0, a), hiY = cellCenter(minY, min(y0 + BY, Y) - 1, a);
    float loZ = cellCenter(minZ, z0, a), hiZ = cellCenter(minZ, min(z0 + BZ, Z) - 1, a);

    if (t == 0)
    {
        sU = __float_as_uint(FLT_MAX);
        sCount = 0;
    }
    __syncthreads();

    float localU = FLT_MAX;
    for (int i = t; i < n; i += BRICK)
    {
        float4 c = pts[i];
        localU = fminf(localU, axisMax(c.x, loX, hiX) + axisMax(c.y, loY, hiY) + axisMax(c.z, loZ, hiZ));
    }
    atomicMin(&sU, __float_as_uint(localU));
    __syncthreads();

    // slack covers float rounding of the box distances vs. the per-cell distances
    float U = __uint_as_float(sU) * 1.0001f + 1e-12f;
    for (int i = t; i < n; i += BRICK)
    {
        float4 c = pts[i];
        if (axisMin(c.x, loX, hiX) + axisMin(c.y, loY, hiY) + axisMin(c.z, loZ, hiZ) <= U)
        {
            int slot = atomicAdd(&sCount, 1);
            if (slot < MAX_CANDIDATES)
                sCand[slot] = i;
        }
    }
    __syncthreads();
    int nCand = sCount;
    bool fullScan = nCand > MAX_CANDIDATES;

    int best = -1;
    float px = 0, py = 0, pz = 0;
    if (occupied)
    {
        px = cellCenter(minX, x, a);
        py = cellCenter(minY, y, a);
        pz = cellCenter(minZ, z, a);
        float bestD = FLT_MAX;
        int count = fullScan ? n : nCand;
        for (int k = 0; k < count; k++)
        {
            int i = fullScan ? k : sCand[k];
            float4 c = pts[i];
            float dx = px - c.x, dy = py - c.y, dz = pz - c.z, dw = 0.0f - c.w;
            // Vector4.LengthSquared (dpps): (x*x + y*y) + (z*z + w*w)
            float d = (dx * dx + dy * dy) + (dz * dz + dw * dw);
            if (d < bestD || (d == bestD && i < best))
            {
                bestD = d;
                best = i;
            }
        }
    }

    // Accumulate. Cells in a warp usually share their nearest point, so reduce
    // across the warp first and let one lane do the atomics.
    double v[10] = { occupied ? 1.0 : 0.0, px, py, pz, 0, 0, 0, 0, 0, 0 };
    if (SECOND && occupied)
    {
        double dx = px, dy = py, dz = pz;
        v[4] = dx * dx; v[5] = dx * dy; v[6] = dx * dz;
        v[7] = dy * dy; v[8] = dy * dz; v[9] = dz * dz;
    }
    if (!occupied)
        v[1] = v[2] = v[3] = 0.0;

    unsigned occMask = __ballot_sync(0xFFFFFFFFu, occupied);
    if (occMask == 0)
        return;
    int leaderBest = __shfl_sync(0xFFFFFFFFu, best, __ffs(occMask) - 1);
    bool uniform = __all_sync(0xFFFFFFFFu, !occupied || best == leaderBest);

    constexpr int NV = SECOND ? 10 : 4;
    if (uniform)
    {
        for (int k = 0; k < NV; k++)
            v[k] = warpSum(v[k]);
        if ((t & 31) == 0)
        {
            atomicAdd(&counts[leaderBest], (int)v[0]);
            for (int k = 0; k < 3; k++)
                atomicAdd(&sums[3 * leaderBest + k], v[1 + k]);
            if (SECOND)
                for (int k = 0; k < 6; k++)
                    atomicAdd(&second[6 * leaderBest + k], v[4 + k]);
        }
    }
    else if (occupied)
    {
        atomicAdd(&counts[best], 1);
        for (int k = 0; k < 3; k++)
            atomicAdd(&sums[3 * best + k], v[1 + k]);
        if (SECOND)
            for (int k = 0; k < 6; k++)
                atomicAdd(&second[6 * best + k], v[4 + k]);
    }
}

static int ensureCapacity(ArapGrid* g, int n)
{
    if (n <= g->capacity)
        return ARAP_OK;
    cudaFree(g->points); cudaFree(g->counts); cudaFree(g->sums); cudaFree(g->second);
    g->points = nullptr; g->counts = nullptr; g->sums = nullptr; g->second = nullptr;
    g->capacity = 0;
    CUDA_TRY(cudaMalloc(&g->points, sizeof(float4) * n));
    CUDA_TRY(cudaMalloc(&g->counts, sizeof(int) * n));
    CUDA_TRY(cudaMalloc(&g->sums, sizeof(double) * 3 * n));
    CUDA_TRY(cudaMalloc(&g->second, sizeof(double) * 6 * n));
    g->capacity = n;
    return ARAP_OK;
}

static int assign(ArapGrid* g, const float* points, int n, int* counts, double* sums, double* second)
{
    int rc = ensureCapacity(g, n);
    if (rc != ARAP_OK)
        return rc;

    CUDA_TRY(cudaMemcpy(g->points, points, sizeof(float4) * n, cudaMemcpyHostToDevice));
    CUDA_TRY(cudaMemset(g->counts, 0, sizeof(int) * n));
    CUDA_TRY(cudaMemset(g->sums, 0, sizeof(double) * 3 * n));
    if (second)
        CUDA_TRY(cudaMemset(g->second, 0, sizeof(double) * 6 * n));

    unsigned blocks = (unsigned)(((g->X + BX - 1) / BX) * (size_t)((g->Y + BY - 1) / BY) * ((g->Z + BZ - 1) / BZ));
    if (second)
        assignKernel<true><<<blocks, BRICK>>>(g->bits, g->X, g->Y, g->Z, g->minX, g->minY, g->minZ, g->a,
                                              g->points, n, g->counts, g->sums, g->second);
    else
        assignKernel<false><<<blocks, BRICK>>>(g->bits, g->X, g->Y, g->Z, g->minX, g->minY, g->minZ, g->a,
                                               g->points, n, g->counts, g->sums, nullptr);
    CUDA_TRY(cudaGetLastError());

    CUDA_TRY(cudaMemcpy(counts, g->counts, sizeof(int) * n, cudaMemcpyDeviceToHost));
    CUDA_TRY(cudaMemcpy(sums, g->sums, sizeof(double) * 3 * n, cudaMemcpyDeviceToHost));
    if (second)
        CUDA_TRY(cudaMemcpy(second, g->second, sizeof(double) * 6 * n, cudaMemcpyDeviceToHost));
    return ARAP_OK;
}

} // namespace arap

extern "C" ArapGrid* arap_grid_create(const unsigned char* bits, size_t nBytes,
                                      int XRes, int YRes, int ZRes,
                                      float minX, float minY, float minZ, float a)
{
    using namespace arap;
    lastError.clear();
    size_t numBits = (size_t)XRes * YRes * ZRes;
    if (!bits || XRes <= 0 || YRes <= 0 || ZRes <= 0 || nBytes != (numBits + 7) / 8)
    {
        fail(ARAP_ERR_ARGS, "arap_grid_create: invalid arguments");
        return nullptr;
    }

    ArapGrid* g = new ArapGrid{ XRes, YRes, ZRes, minX, minY, minZ, a, nullptr, nBytes, 0, nullptr, nullptr, nullptr, nullptr };
    cudaError_t err = cudaMalloc(&g->bits, nBytes);
    if (err == cudaSuccess)
        err = cudaMemcpy(g->bits, bits, nBytes, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        fail(ARAP_ERR_CUDA, std::string("arap_grid_create: ") + cudaGetErrorString(err));
        arap_grid_destroy(g);
        return nullptr;
    }
    return g;
}

extern "C" void arap_grid_destroy(ArapGrid* g)
{
    if (!g)
        return;
    cudaFree(g->bits);
    cudaFree(g->points); cudaFree(g->counts); cudaFree(g->sums); cudaFree(g->second);
    delete g;
}

extern "C" int arap_grid_assign(ArapGrid* g, const float* points, int nPoints,
                                int* counts, double* sums, double* second)
{
    using namespace arap;
    lastError.clear();
    if (!g || !points || !counts || !sums || nPoints <= 0)
        return fail(ARAP_ERR_ARGS, "arap_grid_assign: invalid arguments");
    return assign(g, points, nPoints, counts, sums, second);
}
