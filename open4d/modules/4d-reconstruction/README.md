# 4D Reconstruction

Two synchronized RGB-D cameras stream to an Ubuntu GPU machine for live
point-cloud fusion and CUDA mesh reconstruction. The result is viewed in a web
browser.

This module was formerly named `MeshReduce`. The original C++ reconstruction
applications are still included.

## Data path

```text
Camera 1 + Camera 2
  -> capture host (RGB JPEG + compressed depth)
  -> SSH tunnel
  -> Ubuntu receiver
       - decode both frames
       - transform camera 2 into camera 1 coordinates
       - display a live fused point cloud
       - reconstruct and merge CUDA meshes
  -> Open3D WebRTC
  -> browser
```

The point cloud updates for every processed camera pair. The mesh is rebuilt
periodically and is not a video stream.

Networking and frame preparation run on the CPU. CUDA handles TSDF integration
and mesh extraction. This is not a GPUDirect or RDMA pipeline.

## Requirements

- Two hardware-synchronized RGB-D cameras
- A capture host with the camera SDK and an OBP1-compatible sender
- A reconstruction host with Python 3.10+, OpenCV, NumPy, Zstandard, and
  Open3D
- An NVIDIA GPU and CUDA-enabled Open3D for GPU mesh reconstruction
- Intrinsic calibration for both cameras
- A rigid transform from camera 2 into camera 1 coordinates

The tested cameras are Orbbec Femto Bolts on a Windows capture host using the
Orbbec K4A wrapper. Other cameras can be used if the sender produces the same
RGB-D packet format and the receiver constants match their resolutions.

Create a Python environment on the reconstruction host:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install numpy opencv-python zstandard
```

Install a CUDA-enabled Open3D build in that environment. The standard pip wheel
may not include CUDA. Verify the installation:

```bash
python -c \
  "import open3d as o3d; print(o3d.__version__, o3d.core.cuda.is_available())"
```

The second value should be `True` for GPU meshes.

## Tested environment

The following setup was validated from July 28–30, 2026. It is a reference
configuration, not a minimum requirement.

### Hardware and software

| Component | Tested configuration |
|---|---|
| Capture computer | Dell Inspiron 14 Plus 7440 |
| Capture OS | Windows 11 Home 25H2 x64, build `26200.8875` |
| Capture CPU | Intel Core Ultra 9 185H, 16 cores / 22 logical processors |
| Capture memory | 31.46 GiB usable LPDDR, 6400 MT/s |
| Capture GPU | Integrated Intel Arc Graphics; no NVIDIA GPU |
| Cameras | Two Orbbec Femto Bolt RGB-D cameras, firmware 1.0.9 |
| Camera connection | Separate USB 3 SuperSpeed ports on the same Intel xHCI controller |
| Synchronization | Orbbec Sync Hub Pro; primary/subordinate wiring; 160 us subordinate delay |
| Camera software | Orbbec K4A Wrapper 1.10.5, Orbbec SDK 1.10.28, Python 3.13 |
| Capture network | Intel Wi-Fi 6E AX211, 287 Mb/s reported link, routed through a VPN |
| Reconstruction OS | Ubuntu 24.04-family x86-64, kernel `7.0.0-28-generic` |
| Reconstruction GPU | NVIDIA GeForce RTX 4090 with CUDA-enabled Open3D; tested on `CUDA:1` |
| Reconstruction network | 10 Gb/s Ethernet interface; end-to-end rate was limited by the Windows Wi-Fi/VPN route |

The reconstruction host's CPU model, system-memory capacity, GPU-memory
capacity, and exact CUDA driver version were not saved in the preserved test
report, so they are not inferred here.

### Validated operating point

| Measurement | Result |
|---|---:|
| RGB per camera | 1280 x 720 MJPEG |
| Depth per camera | 640 x 576 NFOV-unbinned, zstd level 3 |
| Live capture rate | 5 synchronized pairs/s |
| Transport reliability | 120/120 pairs received; zero queue drops, pair gaps, CRC errors, protocol errors, or socket timeouts |
| Absolute camera sync error | 160 us median; 1,160 us p95 and maximum |
| Nominal active-stream rate | approximately 27.7 Mb/s |
| Unadjusted one-way wall-clock latency | 84.01 ms median; includes clock offset between hosts |
| Browser-receiver rate in a bounded run | 4.15 processed pairs/s |
| Live display point cloud | approximately 76,000 colored points per update |
| Validated WebRTC mesh | 457,609 vertices / 833,237 triangles |
| Warm CUDA independent-mesh reconstruction | approximately 0.315 s; 607,960 vertices / 1,104,008 triangles |
| Stereo calibration reprojection RMS | 0.2835 px across 15 accepted checkerboard poses |
| Refined depth registration | 0.585 overlap fitness; 10.63 mm inlier RMSE |

The 5 FPS result is the validated profile for this Wi-Fi/VPN path. A 15 FPS
stop-and-wait transport attempt sustained only about four transmitted pairs per
second and dropped older queued pairs. Higher capture rates therefore require
a faster path or further receiver and transport optimization.

During separate single-camera 15 FPS stability tests on the Windows capture
computer, the recorder used the following resources. These figures include
startup and are not measurements of the complete dual-camera pipeline.

| Camera run | System CPU average / maximum | GPU-engine average / maximum | Recorder working set average / maximum |
|---|---:|---:|---:|
| Camera 1 | 12.97% / 19.35% | 3.29% / 5.27% | 153.36 / 213.55 MiB |
| Camera 2 | 11.07% / 12.18% | 2.76% / 4.43% | 148.62 / 208.49 MiB |

### Use your own cameras

1. Choose camera 1 as the reference coordinate system.
2. Configure the sender with your two serial numbers and sync roles.
3. Export the depth/color factory calibration for each camera.
4. Calibrate camera 2 relative to camera 1.
5. Store the files in this layout:

```text
<calibration-dir>/
├── source/work/calibration_stepwise/factory/
│   ├── ey_factory_calibration.json
│   └── j3_factory_calibration.json
└── final_validated_fusion/
    └── j3_depth_to_ey_depth_refined.txt
```

`EY` and `J3` are legacy labels for camera 1 and camera 2. They do not need to
match your serial numbers. This module consumes calibration files but does not
currently generate them. Recalibrate after moving either camera.

## Start a live session

Start these components in order.

### 1. Ubuntu receiver

```bash
cd /path/to/Open4D/open4d/modules/4d-reconstruction

export FOURD_CALIBRATION_DIR=/absolute/path/to/calibration
export PYTHON=/path/to/python-with-open3d
export FOURD_CUDA_DEVICE=0
export FOURD_CAMERA1_SERIAL=your-camera-1-serial
export FOURD_CAMERA2_SERIAL=your-camera-2-serial

DISPLAY_MODE=pointcloud \
MESH_WINDOW=1 \
./tools/run_browser_viewer.sh
```

Expected:

```text
Live fusion receiver listening on 127.0.0.1:17000
Browser viewer ready through Ubuntu 127.0.0.1:8888
```

Leave this terminal open.

### 2. Windows data tunnel

Close Orbbec Viewer and anything else using the cameras. In PowerShell:

```powershell
$GpuUser = "your-user"
$GpuHost = "192.168.1.50"

ssh -o ExitOnForwardFailure=yes `
  -o ServerAliveInterval=30 `
  -o ServerAliveCountMax=3 `
  -N `
  -L 127.0.0.1:17000:127.0.0.1:17000 `
  "$GpuUser@$GpuHost"
```

The command stays quiet after login. Leave it running.

In another PowerShell window, check the tunnel:

```powershell
Test-NetConnection 127.0.0.1 -Port 17000
```

Expected:

```text
TcpTestSucceeded : True
```

### 3. Windows camera sender

Start an OBP1-compatible sender and point it at the local end of the tunnel.
The receiver and protocol are included in this module; the tested Windows
capture sender is currently a separate companion script. Copy that script and
`protocol.py` to the capture host, or provide another sender that implements
OBP1. For the tested Python sender:

```powershell
$Python = "C:\path\to\python.exe"
$Sender = "C:\path\to\windows_sender.py"
$Report = "C:\path\to\sender_report.json"

& $Python $Sender `
  --sdk-bin "C:\path\to\OrbbecSDK-K4A-Wrapper\bin" `
  --host 127.0.0.1 `
  --port 17000 `
  --fps 5 `
  --report $Report
```

The companion sender must be configured with your camera serial numbers before
use. A healthy sender prints:

```text
{"produced_pairs": 30, "sync_error_us": ..., "queue_dropped": 0, ...}
```

Expected on Ubuntu:

```text
Sender connected from 127.0.0.1
{"processed_pairs": 10, "latest_pair": 10, "live_points": ...}
```

The sender defaults to 5 FPS. Higher input rates currently cause frame
replacement during CPU point-cloud preparation.

### 4. Browser tunnel

On the viewing machine:

```bash
GPU_USER=your-user
GPU_HOST=192.168.1.50

ssh -N \
  -o ExitOnForwardFailure=yes \
  -L 127.0.0.1:18888:127.0.0.1:8888 \
  "$GPU_USER@$GPU_HOST"
```

Open [http://127.0.0.1:18888/](http://127.0.0.1:18888/).

If the page opens but the scene does not move, the Windows sender is not
connected. The Ubuntu log must show `Sender connected` and increasing
`processed_pairs`.

To stop, press `Ctrl+C` in the Windows sender first. Then stop the Windows data
tunnel and Ubuntu receiver.

If capture, reconstruction, and viewing all run on the same host, skip both SSH
tunnels. Send to `127.0.0.1:17000` and open
`http://127.0.0.1:8888/`.

## Viewer modes

Set `DISPLAY_MODE` when starting the browser viewer:

| Value | Display |
|---|---|
| `pointcloud` | Latest fused point cloud; use this for motion |
| `auto` | Point cloud until the first mesh, then mesh only |
| `mesh` | Mesh only; use this for stationary scenes |
| `both` | Point cloud and mesh together; useful for debugging but can look doubled |

Recommended live view:

```bash
DISPLAY_MODE=pointcloud MESH_WINDOW=1 ./tools/run_browser_viewer.sh
```

Recommended stationary mesh:

```bash
DISPLAY_MODE=mesh MESH_WINDOW=7 ./tools/run_browser_viewer.sh
```

## Fusion modes

The live point cloud always transforms J3 points into EY coordinates and
concatenates the two aligned point sets.

The default mesh path is `independent-merge`:

1. Reconstruct EY in its own CUDA TSDF volume.
2. Reconstruct J3 in a second CUDA TSDF volume.
3. Transform the J3 mesh into EY coordinates.
4. Merge the partial meshes.

Controls:

```bash
MESH_FUSION_MODE=independent-merge  # separate per-camera meshes
MESH_FUSION_MODE=shared-tsdf        # both cameras in one TSDF volume

MESH_MERGE_MODE=concatenate         # keep every triangle
MESH_MERGE_MODE=weld                # merge nearby vertices
MESH_WELD_RADIUS=0.003              # metres
```

`concatenate` does not crop, decimate, remove components, or fill holes. It can
show two nearby surfaces where the camera views overlap. `shared-tsdf` usually
looks cleaner.

The full MeshReduce paper merger removes redundant overlap with raycasting and
then stitches boundaries. That step is not implemented yet.

## Output

The browser launcher writes to `output/two-camera-fusion/`:

| File | Contents |
|---|---|
| `latest_live_full_scene_pointcloud.ply` | latest fused point cloud |
| `latest_live_full_scene_mesh.ply` | latest mesh |
| `live_fusion_report.json` | receiver and synchronization summary |
| `live_browser.log` | startup, frame, and mesh logs |

A successful mesh log includes:

```text
"backend": "CUDA:0"
"fusion_mode": "independent-merge"
"partial_vertices": [<camera-1>, <camera-2>]
"vertices": <sum-of-partials>
```

For `concatenate`, the final vertex count equals the sum of the two partial
counts.

## Replay without cameras

Start the Ubuntu browser receiver as above. In a second Ubuntu terminal:

```bash
cd /path/to/Open4D/open4d/modules/4d-reconstruction

"$PYTHON" \
  tools/replay_obp1_sender.py \
  --captures-root /absolute/path/to/saved-pairs \
  --fps 2
```

This exercises transport, reconstruction, and browser rendering with recorded
frames. It is not a live camera feed.

For a one-shot saved reconstruction:

```bash
export FOURD_CAPTURE_ROOT=/path/to/capture-data
./tools/reconstruct_saved_two_camera.py
```

## Original C++ pipeline

The original application accepts a live K4A-compatible camera or recorded
`.mkv`, then writes PLY, textured OBJ, Draco geometry, and stage metrics.

Dependencies: CUDA 12.x, CUDA-enabled Open3D 0.18, OpenCV, Eigen, jsoncpp,
Draco, the Azure Kinect SDK or Orbbec K4A wrapper, CMake, and Ninja.

```bash
sudo apt install -y build-essential cmake ninja-build git \
  libopencv-dev libeigen3-dev libjsoncpp-dev libdraco-dev draco

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rgbd_streamer -j"$(nproc)"
./build/app/rgbd_streamer config.playback.json
```

Expected final output:

```text
mesh faces before_qem=... after_qem=... vertices=...
wrote output/playback/mesh.ply, .obj, .mtl, and _texture.png
MESHREDUCE_METRICS {...}
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Troubleshooting

**Browser opens but nothing moves:** check
`output/two-camera-fusion/live_browser.log`. The sender is live only when the
log shows `Sender connected` and increasing `processed_pairs`.

**`Hardware MFT failed to start`:** close Orbbec Viewer and other camera
programs. If it continues, place the cameras on separate USB 3 controllers.

**Port 17000 is busy:** another receiver is running. Find it with:

```bash
ss -ltnp | grep :17000
```

**Mesh looks doubled:** avoid `DISPLAY_MODE=both`. If the mesh alone is
doubled, use `MESH_FUSION_MODE=shared-tsdf` or implement overlap removal.

**Moving people leave several silhouettes:** use `MESH_WINDOW=1`.

**Walls are missing:** the cameras returned no valid depth for those pixels.
Distant, dark, reflective, and occluded surfaces commonly produce holes.

**CUDA is inactive:**

```bash
"$PYTHON" -c \
  "import open3d as o3d; print(o3d.core.cuda.is_available())"
```

Expected output is `True`.

## Related files

- [`docs/artifacts.md`](../../../docs/artifacts.md): generated-data policy
- `python/protocol.py`: OBP1 packet definitions
- `tools/receive_mesh_frame.py`: MRD1/MRD2 reference receiver
- `tools/receive_live_stream.py`: MRD3 reference receiver

Generated datasets and output directories are git-ignored.
