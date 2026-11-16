// The tracking loop (ForwardTracking + MaxTransformDistSmoothWeights of the C#
// tracker) with every per-center step on the GPU:
//   frame 0   voxelize, place centers randomly, 100 Lloyd iterations, affinity
//             from spatial distance
//   frame i   update the affinity with frame i-1's motion, keep neighbours with
//             affinity > cutoff, extrapolate, ARAP pre-smoothing, then average
//             ARAP and Lloyd steps until no center moves more than the threshold

#include "tracker.hpp"
#include "kernels.h"

#include "dotnet_random.h"
#include "nearest.h"
#include "transform_distance.cuh"

#include <cuda_runtime.h>
#include <thrust/execution_policy.h>
#include <thrust/scan.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace arapcuda {
namespace {

#define CU(call)                                                                                     \
    do {                                                                                             \
        cudaError_t err_ = (call);                                                                   \
        if (err_ != cudaSuccess)                                                                     \
            throw std::runtime_error(std::string("arapcuda: " #call ": ") + cudaGetErrorString(err_)); \
    } while (0)

void checkNative(int rc)
{
    if (rc != ARAP_OK)
        throw std::runtime_error(std::string("arapcuda: ") + arap_last_error());
}

template <class T>
class DeviceArray
{
public:
    DeviceArray() = default;
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    ~DeviceArray() { cudaFree(ptr_); }

    void resize(size_t n)
    {
        if (n <= size_)
            return;
        cudaFree(ptr_);
        ptr_ = nullptr;
        size_ = 0;
        CU(cudaMalloc(&ptr_, n * sizeof(T)));
        size_ = n;
    }
    void upload(const T* src, size_t n) { resize(n); CU(cudaMemcpy(ptr_, src, n * sizeof(T), cudaMemcpyHostToDevice)); }
    void download(T* dst, size_t n) const { CU(cudaMemcpy(dst, ptr_, n * sizeof(T), cudaMemcpyDeviceToHost)); }
    T* get() const { return ptr_; }
    void swap(DeviceArray& other) noexcept { std::swap(ptr_, other.ptr_); std::swap(size_, other.size_); }

private:
    T* ptr_ = nullptr;
    size_t size_ = 0;
};

// ---------------------------------------------------------------- device math

// System.Numerics.Vector4.LengthSquared (dpps order); w is always 0 here
__host__ __device__ inline float lengthSquared(float4 d)
{
    return (d.x * d.x + d.y * d.y) + (d.z * d.z + d.w * d.w);
}

__host__ __device__ inline float4 sub(float4 a, float4 b) { return make_float4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
__host__ __device__ inline float4 add(float4 a, float4 b) { return make_float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }

// Eigen-decomposition of a symmetric 3 x 3 matrix by cyclic Jacobi rotations.
// On return A is diagonal (the eigenvalues) and the columns of V the eigenvectors.
__device__ void jacobiEigen(double A[3][3], double V[3][3])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            V[r][c] = r == c;
    for (int sweep = 0; sweep < 32; sweep++)
    {
        double off = A[0][1] * A[0][1] + A[0][2] * A[0][2] + A[1][2] * A[1][2];
        double diag = A[0][0] * A[0][0] + A[1][1] * A[1][1] + A[2][2] * A[2][2];
        if (off <= 1e-32 * diag)
            return;
        const int P[3] = { 0, 0, 1 }, Q[3] = { 1, 2, 2 };
        for (int k = 0; k < 3; k++)
        {
            int p = P[k], q = Q[k];
            if (A[p][q] == 0.0)
                continue;
            double theta = (A[q][q] - A[p][p]) / (2 * A[p][q]);
            double t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1));
            double c = 1 / sqrt(t * t + 1), s = t * c;
            for (int m = 0; m < 3; m++)
            {
                double amp = A[m][p], amq = A[m][q];
                A[m][p] = c * amp - s * amq;
                A[m][q] = s * amp + c * amq;
            }
            for (int m = 0; m < 3; m++)
            {
                double apm = A[p][m], aqm = A[q][m];
                A[p][m] = c * apm - s * aqm;
                A[q][m] = s * apm + c * aqm;
            }
            for (int m = 0; m < 3; m++)
            {
                double vmp = V[m][p], vmq = V[m][q];
                V[m][p] = c * vmp - s * vmq;
                V[m][q] = s * vmp + c * vmq;
            }
        }
    }
}

// Kabsch: the rotation R maximizing tr(R H) for H = sum (a - cA)(w (b - cB))^T,
// i.e. R = V U^T from H = U S V^T, with the last column flipped if det < 0
// (ARAP.GetTransform). With U' = [u1, u2, u1 x u2] that is R = V diag(1, 1, det V) U'^T.
__device__ void kabschRotation(const double H[9], double R[9])
{
    double M[3][3], V[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            M[r][c] = H[r] * H[c] + H[3 + r] * H[3 + c] + H[6 + r] * H[6 + c];   // H^T H
    jacobiEigen(M, V);

    int order[3] = { 0, 1, 2 };   // eigenvalues descending
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (M[order[j]][order[j]] > M[order[i]][order[i]])
            {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }
    double v[3][3];   // v[k] = k-th right singular vector
    for (int k = 0; k < 3; k++)
        for (int r = 0; r < 3; r++)
            v[k][r] = V[r][order[k]];

    double b[2][3];   // H v_k = s_k u_k
    for (int k = 0; k < 2; k++)
        for (int r = 0; r < 3; r++)
            b[k][r] = H[3 * r] * v[k][0] + H[3 * r + 1] * v[k][1] + H[3 * r + 2] * v[k][2];

    double n1 = sqrt(b[0][0] * b[0][0] + b[0][1] * b[0][1] + b[0][2] * b[0][2]);
    if (!(n1 > 1e-300))
    {
        for (int k = 0; k < 9; k++)
            R[k] = k % 4 == 0;
        return;
    }
    double u1[3] = { b[0][0] / n1, b[0][1] / n1, b[0][2] / n1 };
    double proj = u1[0] * b[1][0] + u1[1] * b[1][1] + u1[2] * b[1][2];
    double u2[3] = { b[1][0] - proj * u1[0], b[1][1] - proj * u1[1], b[1][2] - proj * u1[2] };
    double n2 = sqrt(u2[0] * u2[0] + u2[1] * u2[1] + u2[2] * u2[2]);
    if (!(n2 > 1e-12 * n1))
    {
        // rank 1: any direction orthogonal to u1
        int axis = fabs(u1[0]) < fabs(u1[1]) ? (fabs(u1[0]) < fabs(u1[2]) ? 0 : 2) : (fabs(u1[1]) < fabs(u1[2]) ? 1 : 2);
        double e[3] = { 0, 0, 0 };
        e[axis] = 1;
        u2[0] = u1[1] * e[2] - u1[2] * e[1];
        u2[1] = u1[2] * e[0] - u1[0] * e[2];
        u2[2] = u1[0] * e[1] - u1[1] * e[0];
        n2 = sqrt(u2[0] * u2[0] + u2[1] * u2[1] + u2[2] * u2[2]);
    }
    for (int r = 0; r < 3; r++)
        u2[r] /= n2;
    double u3[3] = { u1[1] * u2[2] - u1[2] * u2[1], u1[2] * u2[0] - u1[0] * u2[2], u1[0] * u2[1] - u1[1] * u2[0] };

    double detV = v[0][0] * (v[1][1] * v[2][2] - v[1][2] * v[2][1])
                - v[0][1] * (v[1][0] * v[2][2] - v[1][2] * v[2][0])
                + v[0][2] * (v[1][0] * v[2][1] - v[1][1] * v[2][0]);
    double d = detV < 0 ? -1.0 : 1.0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            R[3 * r + c] = v[0][r] * u1[c] + v[1][r] * u2[c] + d * v[2][r] * u3[c];
}

__device__ inline double warpAllSum(double x)
{
    for (int o = 16; o > 0; o >>= 1)
        x += __shfl_xor_sync(0xFFFFFFFFu, x, o);
    return x;
}

// --------------------------------------------------------------------- kernels

// One warp per center i: weighted rigid fit of i's neighbourhood from prev to
// cur (ARAP.GetTransform), the prediction R prev_i + t, and its offset from cur_i.
__global__ void arapKernel(int n, const int* rowPtr, const int* cols, const float* W,
                           const float4* prev, const float4* cur, float4* gradient, float* transforms)
{
    int i = (blockIdx.x * blockDim.x + threadIdx.x) / 32;
    int lane = threadIdx.x & 31;
    if (i >= n)
        return;
    int begin = rowPtr[i], end = rowPtr[i + 1];
    const float* w = W + (size_t)i * n;

    double s = 0, a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
    for (int k = begin + lane; k < end; k += 32)
    {
        int j = cols[k];
        double wj = w[j];
        float4 p = prev[j], q = cur[j];
        s += wj;
        a[0] += wj * p.x; a[1] += wj * p.y; a[2] += wj * p.z;
        b[0] += wj * q.x; b[1] += wj * q.y; b[2] += wj * q.z;
    }
    s = warpAllSum(s);
    for (int r = 0; r < 3; r++)
    {
        a[r] = warpAllSum(a[r]) / s;
        b[r] = warpAllSum(b[r]) / s;
    }

    double H[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    bool rotate = end - begin >= 2;   // no rotation for an isolated center
    if (rotate)
    {
        for (int k = begin + lane; k < end; k += 32)
        {
            int j = cols[k];
            double wj = w[j];
            float4 p = prev[j], q = cur[j];
            double da[3] = { p.x - a[0], p.y - a[1], p.z - a[2] };
            double db[3] = { wj * (q.x - b[0]), wj * (q.y - b[1]), wj * (q.z - b[2]) };
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    H[3 * r + c] += da[r] * db[c];
        }
        for (int k = 0; k < 9; k++)
            H[k] = warpAllSum(H[k]);
    }
    if (lane != 0)
        return;

    double R[9];
    if (rotate)
        kabschRotation(H, R);
    else
        for (int k = 0; k < 9; k++)
            R[k] = k % 4 == 0;

    float Rf[9], tf[3];
    for (int k = 0; k < 9; k++)
        Rf[k] = (float)R[k];
    for (int r = 0; r < 3; r++)
        tf[r] = (float)(b[r] - (R[3 * r] * a[0] + R[3 * r + 1] * a[1] + R[3 * r + 2] * a[2]));

    float4 p = prev[i], q = cur[i];
    float pred[3];
    for (int r = 0; r < 3; r++)
        pred[r] = (Rf[3 * r] * p.x + Rf[3 * r + 1] * p.y + Rf[3 * r + 2] * p.z) + tf[r];
    if (!(s > 0))
        pred[0] = pred[1] = pred[2] = NAN;
    gradient[i] = make_float4(pred[0] - q.x, pred[1] - q.y, pred[2] - q.z, 0.0f - q.w);

    float* out = transforms + 12 * (size_t)i;
    for (int k = 0; k < 9; k++)
        out[k] = Rf[k];
    for (int r = 0; r < 3; r++)
        out[9 + r] = tf[r];
}

// maxSp = max over frames of the squared distance between centers
__global__ void spatialMaxKernel(int n, const float4* pts, float* maxSp, bool first)
{
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    int i = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= n || j >= n)
        return;
    float d = i == j ? 0.0f : lengthSquared(sub(pts[min(i, j)], pts[max(i, j)]));
    size_t k = (size_t)i * n + j;
    if (first || d > maxSp[k])
        maxSp[k] = d;
}

// maxTf = max over frames of the transform distance
__global__ void transformMaxKernel(int n, const int* counts, const float* centers, const float* transforms, float* maxTf)
{
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    int i = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= n || j >= n)
        return;
    float d = arap::transformDistance(counts, centers, transforms, i, j);
    size_t k = (size_t)i * n + j;
    if (d > maxTf[k])
        maxTf[k] = d;
}

__global__ void affinityKernel(int n, const float* maxSp, const float* maxTf, float kSp, float kTf, float* W, bool first)
{
    size_t k = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= (size_t)n * n)
        return;
    W[k] = first ? (float)exp((double)(-kSp * maxSp[k]))
                 : expf(-kSp * maxSp[k]) * expf(-kTf * maxTf[k]);
}

__global__ void neighbourCountKernel(int n, const float* W, float cutoff, int* rowCount)
{
    __shared__ int count;
    if (threadIdx.x == 0)
        count = 0;
    __syncthreads();
    const float* w = W + (size_t)blockIdx.x * n;
    int c = 0;
    for (int j = threadIdx.x; j < n; j += blockDim.x)
        c += w[j] > cutoff;
    atomicAdd(&count, c);
    __syncthreads();
    if (threadIdx.x == 0)
        rowCount[blockIdx.x] = count;
}

// one warp per row, neighbours in increasing index order
__global__ void neighbourFillKernel(int n, const float* W, float cutoff, const int* rowPtr, int* cols)
{
    const float* w = W + (size_t)blockIdx.x * n;
    int lane = threadIdx.x;
    int base = rowPtr[blockIdx.x];
    for (int j0 = 0; j0 < n; j0 += 32)
    {
        int j = j0 + lane;
        bool keep = j < n && w[j] > cutoff;
        unsigned mask = __ballot_sync(0xFFFFFFFFu, keep);
        if (keep)
            cols[base + __popc(mask & ((1u << lane) - 1))] = j;
        base += __popc(mask);
    }
}

// Per-iteration status, read back once per iteration.
struct Status
{
    unsigned maxMove;   // float bits of the largest squared move (non-negative floats order like uints)
    int orphans;        // centers without cells (handled on the host)
    int nonFinite;
};

// Lloyd offset of center i: (centroid of its cells - p) * overRelax. False if it has no cells.
__device__ inline bool lloydOffset(const int* counts, const double* sums, int i, float4 p, float overRelax, float4& g)
{
    int c = counts[i];
    if (c == 0)
        return false;
    float4 d = sub(make_float4((float)(sums[3 * i] / c), (float)(sums[3 * i + 1] / c), (float)(sums[3 * i + 2] / c), 0.0f), p);
    g = make_float4(d.x * overRelax, d.y * overRelax, d.z * overRelax, d.w * overRelax);
    return true;
}

// frame 0 placement: p += Lloyd offset (over-relaxed)
__global__ void lloydStepKernel(int n, float4* pts, const int* counts, const double* sums, float overRelax,
                                Status* status, int* orphans)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    float4 g;
    if (lloydOffset(counts, sums, i, pts[i], overRelax, g))
        pts[i] = add(pts[i], g);
    else
        orphans[atomicAdd(&status->orphans, 1)] = i;
}

__device__ inline float4 combine(float4 p, float4 lloyd, float4 smooth, float wL, float wS, float wSum, Status* status)
{
    float4 A = add(p, lloyd), B = add(p, smooth);
    float4 next = make_float4((wL * A.x + wS * B.x) / wSum, (wL * A.y + wS * B.y) / wSum,
                              (wL * A.z + wS * B.z) / wSum, (wL * A.w + wS * B.w) / wSum);
    if (!isfinite(lengthSquared(sub(A, next))) || !isfinite(lengthSquared(sub(B, next))))
        atomicOr(&status->nonFinite, 1);
    else
        atomicMax(&status->maxMove, __float_as_uint(lengthSquared(sub(next, p))));
    return next;
}

// ForwardTracking.OptimizeFrame: average of the Lloyd and the ARAP prediction
__global__ void combineKernel(int n, float4* pts, const float4* smooth, const int* counts, const double* sums,
                              float wL, float wS, float wSum, Status* status, int* orphans)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    float4 p = pts[i], g;
    if (lloydOffset(counts, sums, i, p, 1.0f, g))
        pts[i] = combine(p, g, smooth[i], wL, wS, wSum, status);
    else
        orphans[atomicAdd(&status->orphans, 1)] = i;
}

__global__ void addKernel(int n, float4* pts, const float4* delta)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        pts[i] = add(pts[i], delta[i]);
}

// constant-velocity guess: cur = prev + (prev - prev2)
__global__ void extrapolateKernel(int n, float4* cur, const float4* prev, const float4* prev2)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        cur[i] = add(prev[i], sub(prev[i], prev2[i]));
}

// VolumeGrid.GetTransformDistances: moments of the cells nearest to each center
// plus the center itself (the C# sum q starts at 1, hence the + 1)
__global__ void momentsKernel(int n, const float4* pts, const int* counts, const double* sums, const double* second,
                              int* outCounts, float* outCenters)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    float4 q = pts[i];
    double sx = sums[3 * i] + q.x, sy = sums[3 * i + 1] + q.y, sz = sums[3 * i + 2] + q.z;
    const double* m = second + 6 * (size_t)i;
    double xx = m[0] + (double)q.x * q.x, xy = m[1] + (double)q.x * q.y, xz = m[2] + (double)q.x * q.z;
    double yy = m[3] + (double)q.y * q.y, yz = m[4] + (double)q.y * q.z, zz = m[5] + (double)q.z * q.z;
    float* c = outCenters + 13 * (size_t)i;
    c[0] = (float)(xx + yy + zz);
    c[1] = (float)(sx + 1); c[2] = (float)(sy + 1); c[3] = (float)(sz + 1);
    c[4] = (float)xx; c[5] = (float)xy; c[6] = (float)xz;
    c[7] = (float)xy; c[8] = (float)yy; c[9] = (float)yz;
    c[10] = (float)xz; c[11] = (float)yz; c[12] = (float)zz;
    outCounts[i] = counts[i] + 1;
}

unsigned blocksFor(int n, int threads = 256) { return (unsigned)((n + threads - 1) / threads); }

dim3 pairGrid(int n, dim3 block) { return dim3((n + block.x - 1) / block.x, (n + block.y - 1) / block.y); }

// ------------------------------------------------------------------ host side

struct Grid
{
    int X = 0, Y = 0, Z = 0;
    float minX = 0, minY = 0, minZ = 0, a = 0;
    std::vector<unsigned char> bits;
    ArapGrid* native = nullptr;

    Grid() = default;
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;
    ~Grid() { arap_grid_destroy(native); }

    bool occupied(int x, int y, int z) const
    {
        size_t index = ((size_t)x * Y + y) * Z + z;
        return (bits[index >> 3] >> (index & 7)) & 1;
    }
    // VolumeGrid.Int2xyz
    float4 cell(int x, int y, int z) const
    {
        return make_float4(minX + ((float)x * a + a / 2), minY + ((float)y * a + a / 2), minZ + ((float)z * a + a / 2), 0.0f);
    }
};

// MemoryEfficientVolumeGrid(TriangleMesh, maxRes): cubic cells, the longest
// bounding-box side split into maxRes cells, filled by ray parity.
std::unique_ptr<Grid> voxelize(const MeshView& mesh, int resolution)
{
    if (mesh.vertex_count <= 0 || mesh.triangle_count <= 0 || !mesh.vertices || !mesh.triangles)
        throw std::invalid_argument("arapcuda: the mesh needs vertices and triangles");
    for (size_t k = 0; k < 3 * (size_t)mesh.triangle_count; k++)
        if (mesh.triangles[k] < 0 || mesh.triangles[k] >= mesh.vertex_count)
            throw std::invalid_argument("arapcuda: triangle index out of range");

    std::vector<float4> points(mesh.vertex_count);
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int v = 0; v < mesh.vertex_count; v++)
    {
        const float* p = mesh.vertices + 3 * (size_t)v;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
            throw std::invalid_argument("arapcuda: vertex coordinates must be finite");
        points[v] = make_float4(p[0], p[1], p[2], 0.0f);
        for (int k = 0; k < 3; k++)
        {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }

    auto g = std::make_unique<Grid>();
    float size = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
    g->a = size / resolution;
    if (!(g->a > 0))
        throw std::invalid_argument("arapcuda: the mesh has an empty bounding box");
    g->minX = lo[0]; g->minY = lo[1]; g->minZ = lo[2];
    g->X = (int)std::ceil((double)((hi[0] - lo[0]) / g->a));
    g->Y = (int)std::ceil((double)((hi[1] - lo[1]) / g->a));
    g->Z = (int)std::ceil((double)((hi[2] - lo[2]) / g->a));
    if (g->X <= 0 || g->Y <= 0 || g->Z <= 0)
        throw std::invalid_argument("arapcuda: the mesh is flat (zero extent along an axis)");

    size_t bytes = ((size_t)g->X * g->Y * g->Z + 7) / 8;
    g->bits.assign(bytes, 0);
    checkNative(arap_voxelize(&points[0].x, mesh.vertex_count, mesh.triangles, mesh.triangle_count,
                              g->minX, g->minY, g->minZ, g->a, g->X, g->Y, g->Z, g->bits.data(), bytes));
    g->native = arap_grid_create(g->bits.data(), bytes, g->X, g->Y, g->Z, g->minX, g->minY, g->minZ, g->a);
    if (!g->native)
        throw std::runtime_error(std::string("arapcuda: ") + arap_last_error());
    return g;
}

// Center of the occupied cell nearest to q (only for centers that lost all their cells).
float4 nearestOccupied(const Grid& g, float4 q)
{
    float best = FLT_MAX;
    float4 result = make_float4(NAN, NAN, NAN, NAN);
    for (int x = 0; x < g.X; x++)
        for (int y = 0; y < g.Y; y++)
            for (int z = 0; z < g.Z; z++)
                if (g.occupied(x, y, z))
                {
                    float4 c = g.cell(x, y, z);
                    float d = lengthSquared(sub(q, c));
                    if (d < best)
                    {
                        best = d;
                        result = c;
                    }
                }
    return result;
}

// VolumeGrid.RandomSample + Perturb: distinct random occupied cells, jittered.
std::vector<float4> randomCenters(const Grid& g, int count, int seed)
{
    size_t occupiedCells = 0;
    for (unsigned char byte : g.bits)
        occupiedCells += __builtin_popcount(byte);
    if ((size_t)count > occupiedCells)
        throw std::invalid_argument("arapcuda: more centers than occupied voxels in the first frame "
                                    "(is the mesh closed? try a higher grid_resolution)");

    // The C# code keeps the samples in a HashSet<Triangle>, whose hash is
    // V1*1000 ^ V2*100 ^ V3 and whose equality ignores vertex order; reproduce
    // both so a seeded run picks the same cells.
    struct Cell { int x, y, z; };
    std::unordered_map<int, std::vector<Cell>> taken;
    auto contains = [&](const Cell& c) {
        auto it = taken.find((c.x * 1000) ^ (c.y * 100) ^ c.z);
        if (it == taken.end())
            return false;
        for (const Cell& s : it->second)
        {
            auto in = [&](int v) { return v == c.x || v == c.y || v == c.z; };
            if (in(s.x) && in(s.y) && in(s.z))
                return true;
        }
        return false;
    };

    std::vector<float4> pts(count);
    DotNetRandom rnd(seed);
    for (int i = 0; i < count; i++)
    {
        Cell c;
        do
        {
            c.x = rnd.next(g.X);
            c.y = rnd.next(g.Y);
            c.z = rnd.next(g.Z);
        } while (!g.occupied(c.x, c.y, c.z) || contains(c));
        taken[(c.x * 1000) ^ (c.y * 100) ^ c.z].push_back(c);
        pts[i] = g.cell(c.x, c.y, c.z);
    }

    DotNetRandom jitter(seed);
    float scale = g.a * 0.01f;
    for (float4& p : pts)
    {
        float dx = (float)jitter.next_double() - 0.5f;
        float dy = (float)jitter.next_double() - 0.5f;
        float dz = (float)jitter.next_double() - 0.5f;
        p = add(p, make_float4(dx * scale, dy * scale, dz * scale, 0.0f));
    }
    return pts;
}

} // namespace

// --------------------------------------------------------------------- Tracker

struct Tracker::Impl
{
    Params p;
    int frames = 0;
    int n;
    float kSp, kTf;

    std::unique_ptr<Grid> lastGrid;   // grid of the last tracked frame (for the pending affinity update)
    bool pendingUpdate = false;

    DeviceArray<float4> dPrev2, dPrev, dCur, dGrad;   // centers in frames i-2, i-1, i; ARAP offsets
    DeviceArray<float> dTransforms, dW, dMaxSp, dMaxTf, dCenters;
    DeviceArray<int> dRowPtr, dCols, dCounts, dOrphans;
    DeviceArray<Status> dStatus;

    explicit Impl(const Params& params) : p(params), n(params.centers)
    {
        if (p.centers < 1 || p.grid_resolution < 1 || p.max_iterations < 1 || p.init_iterations < 0 || p.presmooth_iterations < 0)
            throw std::invalid_argument("arapcuda: invalid parameters");
        if (!(p.smooth_sigma > 0) || !(p.smooth_sigma2 > 0) || !(p.apply_lloyd + p.apply_smooth > 0))
            throw std::invalid_argument("arapcuda: sigmas and term weights must be positive");
        // Config.loadConfigXML: affinity halves at sigma
        kSp = (float)(std::log(0.5) / -(p.smooth_sigma * p.smooth_sigma));
        kTf = (float)(std::log(0.5) / -(p.smooth_sigma2 * p.smooth_sigma2));

        CU(cudaSetDevice(p.device));
        size_t nn = (size_t)n * n;
        dPrev2.resize(n); dPrev.resize(n); dCur.resize(n); dGrad.resize(n);
        dTransforms.resize(12 * (size_t)n);
        dW.resize(nn); dMaxSp.resize(nn); dMaxTf.resize(nn);
        dCenters.resize(13 * (size_t)n);
        dRowPtr.resize(n + 1); dCols.resize(nn); dCounts.resize(n); dOrphans.resize(n);
        dStatus.resize(1);
        CU(cudaMemset(dMaxTf.get(), 0, nn * sizeof(float)));
    }

    arap::AssignResult assign(const Grid& grid, bool second)
    {
        arap::AssignResult r;
        checkNative(arap::assignDevice(grid.native, dCur.get(), n, second, &r));
        return r;
    }

    // ARAP offsets of all centers (prev -> cur) into dGrad, transforms into dTransforms
    void arapStep()
    {
        arapKernel<<<blocksFor(n * 32, 128), 128>>>(n, dRowPtr.get(), dCols.get(), dW.get(), dPrev.get(), dCur.get(),
                                                   dGrad.get(), dTransforms.get());
        CU(cudaGetLastError());
    }

    // Reads the iteration status; centers that lost all their cells move to the
    // nearest occupied cell instead of their (undefined) centroid.
    Status finishStep(const Grid& grid, bool combined)
    {
        Status st;
        dStatus.download(&st, 1);
        if (st.orphans > 0)
        {
            std::vector<int> idx(st.orphans);
            dOrphans.download(idx.data(), st.orphans);
            for (int i : idx)
            {
                float4 pt, smooth;
                CU(cudaMemcpy(&pt, dCur.get() + i, sizeof(float4), cudaMemcpyDeviceToHost));
                float4 lloyd = sub(nearestOccupied(grid, pt), pt);
                float4 next = add(pt, lloyd);
                if (combined)
                {
                    CU(cudaMemcpy(&smooth, dGrad.get() + i, sizeof(float4), cudaMemcpyDeviceToHost));
                    float4 A = add(pt, lloyd), B = add(pt, smooth);
                    float wL = p.apply_lloyd, wS = p.apply_smooth, wSum = wL + wS;
                    next = make_float4((wL * A.x + wS * B.x) / wSum, (wL * A.y + wS * B.y) / wSum,
                                       (wL * A.z + wS * B.z) / wSum, (wL * A.w + wS * B.w) / wSum);
                    if (!std::isfinite(lengthSquared(sub(A, next))) || !std::isfinite(lengthSquared(sub(B, next))))
                        st.nonFinite = 1;
                    else
                    {
                        float move = lengthSquared(sub(next, pt));
                        unsigned bits;
                        std::memcpy(&bits, &move, sizeof bits);
                        st.maxMove = std::max(st.maxMove, bits);
                    }
                }
                CU(cudaMemcpy(dCur.get() + i, &next, sizeof(float4), cudaMemcpyHostToDevice));
            }
        }
        if (st.nonFinite)
            throw std::runtime_error("arapcuda: non-finite center position in frame " + std::to_string(frames));
        return st;
    }

    void resetStatus() { CU(cudaMemsetAsync(dStatus.get(), 0, sizeof(Status))); }

    void neighbours()
    {
        neighbourCountKernel<<<n, 256>>>(n, dW.get(), p.arap_cutoff, dRowPtr.get());
        CU(cudaGetLastError());
        CU(cudaMemsetAsync(dRowPtr.get() + n, 0, sizeof(int)));
        thrust::exclusive_scan(thrust::device, dRowPtr.get(), dRowPtr.get() + n + 1, dRowPtr.get());
        neighbourFillKernel<<<n, 32>>>(n, dW.get(), p.arap_cutoff, dRowPtr.get(), dCols.get());
        CU(cudaGetLastError());
    }

    void affinity(bool first)
    {
        affinityKernel<<<(unsigned)(((size_t)n * n + 255) / 256), 256>>>(n, dMaxSp.get(), dMaxTf.get(), kSp, kTf, dW.get(), first);
        CU(cudaGetLastError());
    }

    void spatialMax(bool first)
    {
        dim3 block(32, 8);
        spatialMaxKernel<<<pairGrid(n, block), block>>>(n, dCur.get(), dMaxSp.get(), first);
        CU(cudaGetLastError());
    }

    // MaxTransformDistSmoothWeights.Update after frame i (dCur, grid and dTransforms of frame i)
    void updateAffinity(const Grid& grid)
    {
        spatialMax(false);
        arap::AssignResult r = assign(grid, true);
        momentsKernel<<<blocksFor(n), 256>>>(n, dCur.get(), r.counts, r.sums, r.second, dCounts.get(), dCenters.get());
        CU(cudaGetLastError());
        dim3 block(32, 8);
        transformMaxKernel<<<pairGrid(n, block), block>>>(n, dCounts.get(), dCenters.get(), dTransforms.get(), dMaxTf.get());
        CU(cudaGetLastError());
        affinity(false);
    }

    int firstFrame(const Grid& grid)
    {
        std::vector<float4> start = randomCenters(grid, n, p.seed);
        dCur.upload(start.data(), n);
        for (int it = 0; it < p.init_iterations; it++)
        {
            resetStatus();
            arap::AssignResult r = assign(grid, false);
            lloydStepKernel<<<blocksFor(n), 256>>>(n, dCur.get(), r.counts, r.sums, 1.8f, dStatus.get(), dOrphans.get());
            CU(cudaGetLastError());
            finishStep(grid, false);
        }
        spatialMax(true);
        affinity(true);
        return p.init_iterations;
    }

    int nextFrame(const Grid& grid)
    {
        neighbours();

        // Extrapolate: constant velocity (frame 1 starts from frame 0)
        dPrev2.swap(dPrev);
        dPrev.swap(dCur);
        if (frames > 1)
            extrapolateKernel<<<blocksFor(n), 256>>>(n, dCur.get(), dPrev.get(), dPrev2.get());
        else
            CU(cudaMemcpyAsync(dCur.get(), dPrev.get(), sizeof(float4) * n, cudaMemcpyDeviceToDevice));
        CU(cudaGetLastError());

        for (int it = 0; it < p.presmooth_iterations; it++)
        {
            arapStep();
            addKernel<<<blocksFor(n), 256>>>(n, dCur.get(), dGrad.get());
            CU(cudaGetLastError());
        }

        // ForwardTracking.OptimizeFrame
        const double thresholdSquared = p.gradient_threshold * p.gradient_threshold;
        const float wL = p.apply_lloyd, wS = p.apply_smooth, wSum = wL + wS;
        int it = 0;
        bool stop = false;
        while (!stop && it < p.max_iterations)
        {
            resetStatus();
            arapStep();
            arap::AssignResult r = assign(grid, false);
            combineKernel<<<blocksFor(n), 256>>>(n, dCur.get(), dGrad.get(), r.counts, r.sums, wL, wS, wSum,
                                                 dStatus.get(), dOrphans.get());
            CU(cudaGetLastError());
            Status st = finishStep(grid, true);
            float maxMove;
            std::memcpy(&maxMove, &st.maxMove, sizeof maxMove);
            stop = (double)maxMove < thresholdSquared;
            it++;
        }

        arapStep();   // final transforms prev -> cur, also used by the next affinity update
        return it;
    }

    Frame addFrame(const MeshView& mesh)
    {
        auto t0 = std::chrono::steady_clock::now();
        CU(cudaSetDevice(p.device));
        std::unique_ptr<Grid> grid = voxelize(mesh, p.grid_resolution);

        if (pendingUpdate)
        {
            updateAffinity(*lastGrid);
            pendingUpdate = false;
        }
        lastGrid.reset();

        Frame f;
        if (frames == 0)
        {
            f.iterations = firstFrame(*grid);
        }
        else
        {
            f.iterations = nextFrame(*grid);
            f.transforms.resize(12 * (size_t)n);
            dTransforms.download(f.transforms.data(), f.transforms.size());
            pendingUpdate = true;
        }
        lastGrid = std::move(grid);
        frames++;

        std::vector<float4> centers(n);
        dCur.download(centers.data(), n);
        f.centers.resize(3 * (size_t)n);
        for (int i = 0; i < n; i++)
        {
            f.centers[3 * i] = centers[i].x;
            f.centers[3 * i + 1] = centers[i].y;
            f.centers[3 * i + 2] = centers[i].z;
        }
        f.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return f;
    }
};

Tracker::Tracker(const Params& params) : impl_(std::make_unique<Impl>(params)) {}
Tracker::~Tracker() = default;
Tracker::Tracker(Tracker&&) noexcept = default;
Tracker& Tracker::operator=(Tracker&&) noexcept = default;

Frame Tracker::add_frame(const MeshView& mesh) { return impl_->addFrame(mesh); }
int Tracker::frame_count() const { return impl_->frames; }
const Params& Tracker::params() const { return impl_->p; }

int device_count()
{
    int n = 0;
    return cudaGetDeviceCount(&n) == cudaSuccess ? n : 0;
}

} // namespace arapcuda
