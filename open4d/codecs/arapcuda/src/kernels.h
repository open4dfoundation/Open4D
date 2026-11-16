// Internal: voxelization and the GPU-resident occupancy grid used by the tracker.
#pragma once

#include <stddef.h>

#define ARAP_OK 0
#define ARAP_ERR_CUDA 1
#define ARAP_ERR_ARGS 2

// Solid voxelization by z-ray parity on the GPU, bit-identical to
// MemoryEfficientVolumeGrid(TriangleMesh, int) in the C# code.
//   points    : nPoints x float4 (x, y, z, w)
//   tris      : nTris x int3
//   min*, a   : grid origin and cell size
//   out       : XRes*YRes*ZRes bits, index ((x*YRes)+y)*ZRes+z, LSB first;
//               outBytes must be ceil(XRes*YRes*ZRes / 8)
int arap_voxelize(const float* points, int nPoints, const int* tris, int nTris,
                  float minX, float minY, float minZ, float a,
                  int XRes, int YRes, int ZRes,
                  unsigned char* out, size_t outBytes);

// Occupancy grid resident on the GPU (one per frame).
typedef struct ArapGrid ArapGrid;

// bits: same layout as arap_voxelize's output. Returns NULL on failure.
ArapGrid* arap_grid_create(const unsigned char* bits, size_t nBytes,
                           int XRes, int YRes, int ZRes,
                           float minX, float minY, float minZ, float a);
void arap_grid_destroy(ArapGrid* grid);

// Message for the last error on this thread (empty string if none).
const char* arap_last_error(void);
