// C API of the native ARAP volume tracking accelerator (libarapcuda.so).
// Called from Framework/Cuda/ArapCuda.cs via P/Invoke.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARAP_OK 0
#define ARAP_ERR_CUDA 1
#define ARAP_ERR_ARGS 2

#define ARAP_BACKEND_HOST 0   // same algorithm as the kernels, run on the CPU
#define ARAP_BACKEND_CUDA 1

// Solid voxelization by z-ray parity, bit-identical to
// MemoryEfficientVolumeGrid(TriangleMesh, int) in the C# code.
//   points    : nPoints x float4 (x, y, z, w) - System.Numerics.Vector4 layout
//   tris      : nTris x int3 (V1, V2, V3)     - Framework.Triangle layout
//   min*, a   : grid origin and cell size
//   out       : XRes*YRes*ZRes bits, index ((x*YRes)+y)*ZRes+z, LSB first;
//               outBytes must be ceil(XRes*YRes*ZRes / 8)
int arap_voxelize(const float* points, int nPoints, const int* tris, int nTris,
                  float minX, float minY, float minZ, float a,
                  int XRes, int YRes, int ZRes,
                  unsigned char* out, size_t outBytes, int backend);

// Occupancy grid resident on the GPU (one per frame; reused by every Lloyd
// iteration and the affinity update of that frame).
typedef struct ArapGrid ArapGrid;

// bits: same layout as arap_voxelize's output. Returns NULL on failure.
ArapGrid* arap_grid_create(const unsigned char* bits, size_t nBytes,
                           int XRes, int YRes, int ZRes,
                           float minX, float minY, float minZ, float a);
void arap_grid_destroy(ArapGrid* grid);

// Assigns every occupied cell (center as VolumeGrid.Int2xyz) to its nearest
// point and accumulates, per point i:
//   counts[i]             number of cells
//   sums[3i..3i+2]        sum of cell centers (x, y, z)
//   second[6i..6i+5]      sum of xx, xy, xz, yy, yz, zz   (only if second != NULL)
// Ties go to the lower point index. Sums are accumulated in double, so their
// order (and hence the last bits) is not deterministic.
int arap_grid_assign(ArapGrid* grid, const float* points, int nPoints,
                     int* counts, double* sums, double* second);

int arap_cuda_device_count(void);

// Message for the last error on this thread (empty string if none).
const char* arap_last_error(void);

#ifdef __cplusplus
}
#endif
