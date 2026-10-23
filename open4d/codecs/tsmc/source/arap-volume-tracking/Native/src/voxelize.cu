// Solid voxelization by z-ray parity (replaces the brute-force column x triangle
// loop in MemoryEfficientVolumeGrid(TriangleMesh, int)).
//
// The C# code counts, for every column, the triangles hit by a vertical ray and
// marks cell z inside when an odd number of hit depths lie below zf[z]. Here the
// loop is inverted: each triangle visits only the columns under its footprint
// and flips one bit at the first cell above its hit depth; a prefix XOR along z
// then reproduces the parity count exactly.

#include "common.h"
#include "voxelize_core.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace arap {

thread_local std::string lastError;

struct Grid
{
    int X, Y, Z;
    int Wc;             // 32-bit words per column in the flip buffer
    float minX, minY, a;
    const float* qx;    // column center x, as VolumeGrid.Int2xyz computes it
    const float* qy;
    const float* zf;    // cell center z, accumulated as in the C# loop
};

#pragma nv_exec_check_disable
template <class FlipFn>
ARAP_HD void scatterTriangle(const float* pts, const int* tris, int t, const Grid& g, FlipFn flip)
{
    int i1 = tris[3 * t], i2 = tris[3 * t + 1], i3 = tris[3 * t + 2];
    float3_ p1 = { pts[4 * i1], pts[4 * i1 + 1], pts[4 * i1 + 2] };
    float3_ p2 = { pts[4 * i2], pts[4 * i2 + 1], pts[4 * i2 + 2] };
    float3_ p3 = { pts[4 * i3], pts[4 * i3 + 1], pts[4 * i3 + 2] };

    int x0, x1, y0, y1;
    columnRange(fminf(p1.x, fminf(p2.x, p3.x)), fmaxf(p1.x, fmaxf(p2.x, p3.x)), g.minX, g.a, g.X, x0, x1);
    columnRange(fminf(p1.y, fminf(p2.y, p3.y)), fmaxf(p1.y, fmaxf(p2.y, p3.y)), g.minY, g.a, g.Y, y0, y1);

    for (int x = x0; x <= x1; x++)
    {
        float qX = g.qx[x];
        for (int y = y0; y <= y1; y++)
        {
            float qY = g.qy[y];
            if (!pointInTriangle2(p1, p2, p3, qX, qY))
                continue;
            int k = firstCellAbove(g.zf, g.Z, intersectZ(p1, p2, p3, qX, qY));
            if (k < g.Z)
            {
                size_t col = (size_t)x * g.Y + y;
                flip(col * g.Wc + (k >> 5), 1u << (k & 31));
            }
        }
    }
}

// Turns one column of crossing flips into occupancy bits and ORs them into the
// packed output bit stream (columns are not word aligned in general).
#pragma nv_exec_check_disable
template <class OrFn>
ARAP_HD void emitColumn(const uint32_t* flip, size_t col, const Grid& g, OrFn orWord)
{
    uint32_t carry = 0;
    size_t bit0 = col * g.Z;
    for (int w = 0; w < g.Wc; w++)
    {
        uint32_t bits = prefixXor32(flip[col * g.Wc + w]) ^ carry;
        carry = (bits >> 31) ? 0xFFFFFFFFu : 0u;

        int valid = g.Z - 32 * w;
        if (valid < 32)
            bits &= (1u << valid) - 1u;
        if (bits == 0)
            continue;

        size_t pos = bit0 + 32 * (size_t)w;
        size_t word = pos >> 5;
        int shift = (int)(pos & 31);
        orWord(word, bits << shift);
        if (shift != 0)
        {
            uint32_t hi = bits >> (32 - shift);
            if (hi)
                orWord(word + 1, hi);
        }
    }
}

struct AtomicXorFlip
{
    uint32_t* flip;
    __device__ void operator()(size_t word, uint32_t mask) const { atomicXor(&flip[word], mask); }
};

struct AtomicOrOut
{
    uint32_t* out;
    __device__ void operator()(size_t word, uint32_t bits) const { atomicOr(&out[word], bits); }
};

struct HostXorFlip
{
    uint32_t* flip;
    void operator()(size_t word, uint32_t mask) const { flip[word] ^= mask; }
};

struct HostOrOut
{
    uint32_t* out;
    void operator()(size_t word, uint32_t bits) const { out[word] |= bits; }
};

__global__ void triangleCrossingsKernel(const float* pts, const int* tris, int nTris, Grid g, uint32_t* flip)
{
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= nTris)
        return;
    scatterTriangle(pts, tris, t, g, AtomicXorFlip{ flip });
}

__global__ void columnParityKernel(const uint32_t* flip, size_t nCols, Grid g, uint32_t* out)
{
    size_t col = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (col >= nCols)
        return;
    emitColumn(flip, col, g, AtomicOrOut{ out });
}

struct HostTables
{
    std::vector<float> qx, qy, zf;
};

static HostTables buildTables(float minX, float minY, float minZ, float a, int X, int Y, int Z)
{
    HostTables t;
    t.qx.resize(X);
    t.qy.resize(Y);
    t.zf.resize(Z);
    float half = a / 2;
    // VolumeGrid.Int2xyz: r.X += x * a + a / 2
    for (int x = 0; x < X; x++) t.qx[x] = minX + ((float)x * a + half);
    for (int y = 0; y < Y; y++) t.qy[y] = minY + ((float)y * a + half);
    // float zf = min.Z + a / 2; ... zf += a;
    float zf = minZ + half;
    for (int z = 0; z < Z; z++)
    {
        t.zf[z] = zf;
        zf += a;
    }
    return t;
}

static int voxelizeHost(const float* pts, const int* tris, int nTris, Grid g, unsigned char* out, size_t outBytes)
{
    size_t nCols = (size_t)g.X * g.Y;
    std::vector<uint32_t> flip(nCols * g.Wc, 0);
    for (int t = 0; t < nTris; t++)
        scatterTriangle(pts, tris, t, g, HostXorFlip{ flip.data() });

    std::vector<uint32_t> words((outBytes + 3) / 4, 0);
    for (size_t col = 0; col < nCols; col++)
        emitColumn(flip.data(), col, g, HostOrOut{ words.data() });

    std::memcpy(out, words.data(), outBytes);   // little-endian: word bit j == byte j/8 bit j%8
    return ARAP_OK;
}

static int voxelizeCuda(const float* pts, int nPoints, const int* tris, int nTris, Grid g,
                        const HostTables& tab, unsigned char* out, size_t outBytes)
{
    size_t nCols = (size_t)g.X * g.Y;
    size_t flipWords = nCols * g.Wc;
    size_t outWords = (outBytes + 3) / 4;

    float *dPts = nullptr, *dQx = nullptr, *dQy = nullptr, *dZf = nullptr;
    int* dTris = nullptr;
    uint32_t *dFlip = nullptr, *dOut = nullptr;

    auto release = [&]() {
        cudaFree(dPts); cudaFree(dTris); cudaFree(dQx); cudaFree(dQy);
        cudaFree(dZf); cudaFree(dFlip); cudaFree(dOut);
    };

    int rc = [&]() -> int {
        CUDA_TRY(cudaMalloc(&dPts, sizeof(float) * 4 * (size_t)nPoints));
        CUDA_TRY(cudaMalloc(&dTris, sizeof(int) * 3 * (size_t)nTris));
        CUDA_TRY(cudaMalloc(&dQx, sizeof(float) * g.X));
        CUDA_TRY(cudaMalloc(&dQy, sizeof(float) * g.Y));
        CUDA_TRY(cudaMalloc(&dZf, sizeof(float) * g.Z));
        CUDA_TRY(cudaMalloc(&dFlip, sizeof(uint32_t) * flipWords));
        CUDA_TRY(cudaMalloc(&dOut, sizeof(uint32_t) * outWords));

        CUDA_TRY(cudaMemcpy(dPts, pts, sizeof(float) * 4 * (size_t)nPoints, cudaMemcpyHostToDevice));
        CUDA_TRY(cudaMemcpy(dTris, tris, sizeof(int) * 3 * (size_t)nTris, cudaMemcpyHostToDevice));
        CUDA_TRY(cudaMemcpy(dQx, tab.qx.data(), sizeof(float) * g.X, cudaMemcpyHostToDevice));
        CUDA_TRY(cudaMemcpy(dQy, tab.qy.data(), sizeof(float) * g.Y, cudaMemcpyHostToDevice));
        CUDA_TRY(cudaMemcpy(dZf, tab.zf.data(), sizeof(float) * g.Z, cudaMemcpyHostToDevice));
        CUDA_TRY(cudaMemset(dFlip, 0, sizeof(uint32_t) * flipWords));
        CUDA_TRY(cudaMemset(dOut, 0, sizeof(uint32_t) * outWords));

        Grid dg = g;
        dg.qx = dQx;
        dg.qy = dQy;
        dg.zf = dZf;

        const int block = 256;
        if (nTris > 0)
            triangleCrossingsKernel<<<(nTris + block - 1) / block, block>>>(dPts, dTris, nTris, dg, dFlip);
        CUDA_TRY(cudaGetLastError());
        columnParityKernel<<<(unsigned)((nCols + block - 1) / block), block>>>(dFlip, nCols, dg, dOut);
        CUDA_TRY(cudaGetLastError());

        CUDA_TRY(cudaMemcpy(out, dOut, outBytes, cudaMemcpyDeviceToHost));
        return ARAP_OK;
    }();

    release();
    return rc;
}

} // namespace arap

extern "C" int arap_voxelize(const float* points, int nPoints, const int* tris, int nTris,
                             float minX, float minY, float minZ, float a,
                             int XRes, int YRes, int ZRes,
                             unsigned char* out, size_t outBytes, int backend)
{
    using namespace arap;
    lastError.clear();

    if (!points || !tris || !out || nPoints <= 0 || nTris < 0 || XRes <= 0 || YRes <= 0 || ZRes <= 0)
        return fail(ARAP_ERR_ARGS, "arap_voxelize: invalid arguments");
    size_t numBits = (size_t)XRes * YRes * ZRes;
    if (outBytes != (numBits + 7) / 8)
        return fail(ARAP_ERR_ARGS, "arap_voxelize: outBytes must be ceil(XRes*YRes*ZRes/8)");
    for (size_t i = 0; i < 3 * (size_t)nTris; i++)
        if (tris[i] < 0 || tris[i] >= nPoints)
            return fail(ARAP_ERR_ARGS, "arap_voxelize: triangle index out of range");

    HostTables tab = buildTables(minX, minY, minZ, a, XRes, YRes, ZRes);
    Grid g = { XRes, YRes, ZRes, (ZRes + 31) / 32, minX, minY, a, tab.qx.data(), tab.qy.data(), tab.zf.data() };

    if (backend == ARAP_BACKEND_HOST)
        return voxelizeHost(points, tris, nTris, g, out, outBytes);
    if (backend == ARAP_BACKEND_CUDA)
        return voxelizeCuda(points, nPoints, tris, nTris, g, tab, out, outBytes);
    return fail(ARAP_ERR_ARGS, "arap_voxelize: unknown backend");
}

extern "C" int arap_cuda_device_count(void)
{
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess)
        return 0;
    return n;
}

extern "C" const char* arap_last_error(void)
{
    return arap::lastError.c_str();
}
