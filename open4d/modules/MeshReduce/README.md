# MeshReduce: Scalable and Bandwidth Efficient 3D Scene Capture

## Hardware Components
- Microsoft Azure Kinect cameras

## Software Environment
Open3D 0.18.0

OpenCV 4.5

Eigen3.4

Google Draco 1.5.6 or newer (`sudo apt install libdraco-dev draco` on Ubuntu)

## Working applications

`dual_camera_fusion` is the depth-only one/two-camera CUDA TSDF tool.

`rgbd_streamer` implements the repository's usable single-camera path:

```
K4A-compatible RGB-D camera -> capture producer thread -> CUDA TSDF
-> triangle extraction -> optional QEM -> projective UVs
-> PLY plus textured OBJ plus Draco -> optional MRD1/MRD2 TCP frame
```

Build and run:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target rgbd_streamer -j2
./build/app/rgbd_streamer config.rgbd.json
```

### Texture-mapping acceleration

The reconstruction library uses a custom CUDA path for multi-camera vertex
projection, depth-occlusion testing, camera selection, and triangle-UV
generation. If CUDA cannot be initialized or the depth inputs are incompatible,
the same operation falls back to OpenMP-parallel CPU loops.

CUDA kernels and OpenMP are enabled by default. They can be configured
independently:

```bash
CUDACXX=/usr/local/cuda/bin/nvcc cmake -S . -B build \
  -DMESHREDUCE_ENABLE_CUDA_KERNELS=ON \
  -DMESHREDUCE_ENABLE_OPENMP=ON
cmake --build build --target sensor_client -j8
ctest --test-dir build --output-on-failure
```

Set `MESHREDUCE_DISABLE_CUDA_TEXTURE_MAPPING=1` at runtime to force the OpenMP
fallback for comparison. `MESHREDUCE_CUDA_DEVICE` selects the CUDA device
(default `0`), and `OMP_NUM_THREADS` controls the CPU worker count.

The output prefix is configured in `config.rgbd.json`. The application writes
geometry as PLY and a textured OBJ/MTL/PNG set. Set `network.enabled` to `true`
to listen for one TCP receiver. `tools/receive_mesh_frame.py` is a protocol
validator and example receiver. The `draco` config block controls compression;
when enabled, the application writes `<prefix>.drc`. Set `network.format` to
`"draco"` for compressed MRD2 geometry or `"raw"` for the legacy MRD1 frame.

The default Draco settings use 11 position bits, 8 normal bits, 10 UV bits,
and encoder/decoder speed 5. Lower speed values improve compression at the cost
of more CPU time. Validate a generated file with:

```bash
draco_decoder -i /tmp/meshreduce_rgbd.drc -o /tmp/decoded.obj
```

### MRD1 protocol

The fixed 44-byte header is eleven unsigned 32-bit integers in network byte
order: magic `MRD1`, version, vertex count, face count, position bytes, normal
bytes, index bytes, UV bytes, texture width, texture height, and texture bytes.
Payloads follow in that order. Numeric mesh payloads are little-endian float32
or uint32; texture data is tightly packed RGB8. UV data contains three UV pairs
per face. A Unity receiver must parse this protocol and create a mesh and RGB
texture from the five payload blocks.

### MRD2 protocol (Draco)

The fixed 28-byte header is seven unsigned 32-bit integers in network byte
order: magic `MRD2`, version, Draco byte count, texture width, texture height,
texture byte count, and texture encoding (`1` = tightly packed RGB8). The Draco
payload comes first, followed by the RGB texture. The Draco mesh contains
position, normal, triangle connectivity, and per-vertex texture-coordinate
attributes. This compresses geometry only; H.264 texture compression remains a
separate future integration.
