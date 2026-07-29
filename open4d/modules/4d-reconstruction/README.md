# 4D Reconstruction: Scalable and Bandwidth Efficient 3D Scene Capture

This module was formerly named `MeshReduce`.

For the two remotely attached Orbbec Femto Bolt cameras used by this system,
see [`REMOTE_TWO_CAMERA.md`](REMOTE_TWO_CAMERA.md). It documents the calibrated
Windows-capture to GPU-reconstruction path and its output artifacts.

## Hardware Components
- Microsoft Azure Kinect cameras
- Orbbec Femto Bolt cameras through the K4A compatibility wrapper

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

## Live streaming

The lab setup uses two Femto Bolt cameras connected to a Windows capture
machine. Windows pairs the hardware-synchronized color and depth frames and
sends them to the GPU machine over TCP. The receiver checks the timestamps,
applies the calibrated J3-to-EY transform, and places both depth images in one
coordinate system.

Two outputs are maintained:

- A fused point cloud for responsive viewing.
- A TSDF mesh rebuilt from a short window of recent frames.

The browser viewer and the bounded reconstruction command use the same camera
calibration and fusion code. The viewer keeps running until it is stopped. The
bounded command exits after a chosen number of pairs and writes a PLY mesh,
point cloud, and JSON report under `output/two-camera-fusion`.

```bash
MAX_PAIRS=30 ./tools/run_remote_two_camera_fusion.sh
```

The Windows sender must already be running, and its SSH data tunnel must map
Windows `127.0.0.1:17000` to the same address and port on this machine. Only one
receiver can own port 17000 at a time. Stop the browser receiver before running
the bounded command, then restart it afterward.

See [`REMOTE_TWO_CAMERA.md`](REMOTE_TWO_CAMERA.md) for paths and saved-sequence
reconstruction.

## Writing your own implementation

A replacement sender or receiver does not need to use the browser code, but it
must preserve the capture contract:

1. Select the cameras by serial number, not USB enumeration order.
2. Start J3 as the subordinate and EY as the primary, with a 160 microsecond
   subordinate delay.
3. Pair frames using device timestamps. Do not pair them by host arrival time.
4. Send one frame containing EY color, EY depth, J3 color, and J3 depth.
5. Preserve the serial number, device timestamp, image dimensions, codec, and
   payload checksum for every image.
6. Reject a pair when the adjusted camera timestamp difference exceeds 3
   milliseconds.
7. Transform J3 depth geometry into the EY depth coordinate frame before
   combining or integrating it.

The current transport uses the `OBP1` frame format. Color is camera-produced
1280x720 MJPEG. Depth is 640x576 little-endian `uint16`, compressed losslessly
with Zstandard. The receiver returns an `OBA1` acknowledgement for each accepted
pair.

For a simple implementation, first decode and validate all four payloads. Build
one point cloud per camera from the depth intrinsics, transform the J3 cloud
with the calibrated matrix, and concatenate the clouds. Once that result is
correct, feed both registered RGB-D images into a shared TSDF volume to obtain
a connected mesh. Keep the calibration matrix external to the program so it
can be replaced without recompiling.

The existing module launchers are useful as reference points:

- `tools/run_remote_two_camera_fusion.sh` shows the required calibration and
  reconstruction arguments.
- `tools/reconstruct_saved_two_camera.py` shows how to reproduce a fused mesh
  without a live sender.
- `tools/receive_live_stream.py` is a small example for the module's separate
  MRD3 Draco/JPEG mesh-stream format.

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
