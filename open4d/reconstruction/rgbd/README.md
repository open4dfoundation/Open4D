# RGB-D reconstruction

Two synchronized RGB-D cameras send frames to an Ubuntu GPU machine. That
machine fuses them into a live point cloud and CUDA meshes, and you watch the
result in a browser. This module was formerly called `MeshReduce`.

```text
2 cameras -> Windows capture host -> SSH tunnel -> Ubuntu receiver (fuse + CUDA TSDF) -> Open3D WebRTC -> browser
```

The point cloud updates on every camera pair; the mesh is rebuilt
periodically. From Python, `open4d.reconstruct(source, method="rgbd")` runs
the same reconstruction on saved captures.

## Setup

Hardware and the tested configuration are in the [repository README](../../../README.md).

```bash
python3 -m venv .venv && source .venv/bin/activate
python -m pip install numpy opencv-python zstandard
# plus a CUDA-enabled Open3D build (the pip wheel may not include CUDA)
python -c "import open3d as o3d; print(o3d.__version__, o3d.core.cuda.is_available())"   # want True
```

**Calibration.** Camera 1 is the reference frame. Export each camera's
factory calibration, calibrate camera 2 against camera 1, and lay out the files
like this (`EY` = camera 1 and `J3` = camera 2, legacy names). This module only
reads calibration; it doesn't create it. Recalibrate if you move either camera.

```text
<calibration-dir>/
├── source/work/calibration_stepwise/factory/{ey,j3}_factory_calibration.json
└── final_validated_fusion/j3_depth_to_ey_depth_refined.txt
```

## Run a live session

Start these in order.

**1. Ubuntu receiver**, run from this directory:

```bash
export FOURD_CALIBRATION_DIR=/abs/path/to/calibration
export PYTHON=/path/to/python-with-open3d
export FOURD_CUDA_DEVICE=0
export FOURD_CAMERA1_SERIAL=... FOURD_CAMERA2_SERIAL=...
DISPLAY_MODE=pointcloud MESH_WINDOW=1 ./tools/run_browser_viewer.sh
# -> listening on 127.0.0.1:17000, viewer on 127.0.0.1:8888
```

**2. Windows data tunnel.** Close Orbbec Viewer first.

```powershell
ssh -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 -N `
  -L 127.0.0.1:17000:127.0.0.1:17000 user@gpu-host
Test-NetConnection 127.0.0.1 -Port 17000     # TcpTestSucceeded : True
```

**3. Windows camera sender.** Use any sender that speaks OBP1 (packet format
in `python/protocol.py`). The tested sender is a separate companion script, so
copy it to the capture host and set your camera serials in it.

```powershell
& C:\path\to\python.exe C:\path\to\windows_sender.py `
  --sdk-bin "C:\path\to\OrbbecSDK-K4A-Wrapper\bin" `
  --host 127.0.0.1 --port 17000 --fps 5 --report sender_report.json
```

Keep the default 5 FPS: at higher rates the receiver can't prepare point clouds
fast enough and replaces frames. The Ubuntu log should show `Sender connected`
and a rising `processed_pairs`.

**4. Browser tunnel**, from the machine you're watching on:

```bash
ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:18888:127.0.0.1:8888 user@gpu-host
```

Then open <http://127.0.0.1:18888/>. If the page loads but nothing moves, the
sender isn't connected. To stop, press Ctrl+C in the sender first, then close
the tunnels and the receiver. If everything runs on one machine, skip both
tunnels and open <http://127.0.0.1:8888/>.

## Options

| Variable | Values |
|---|---|
| `DISPLAY_MODE` | `pointcloud` (motion), `mesh` (still scenes), `auto` (points until the first mesh), `both` (debugging) |
| `MESH_WINDOW` | how many recent camera pairs go into each mesh: `1` for live, `7` for a still scene |
| `MESH_FUSION_MODE` | `independent-merge` (default; one TSDF per camera, then merge) or `shared-tsdf` (one volume; usually cleaner) |
| `MESH_MERGE_MODE` | `concatenate` (keep every triangle) or `weld` (merge vertices within `MESH_WELD_RADIUS`, metres) |

Merging does no cropping, decimation or hole filling, so overlapping views can
show doubled surfaces. The MeshReduce paper's overlap removal and stitching
step isn't implemented yet.

Output goes to `output/two-camera-fusion/`:
`latest_live_full_scene_{pointcloud,mesh}.ply`, `live_fusion_report.json` and
`live_browser.log`. A healthy mesh log shows `"backend": "CUDA:0"`.

## Without the live cameras

```bash
# replay saved pairs through the live receiver (start step 1 first)
"$PYTHON" tools/replay_obp1_sender.py --captures-root /abs/path/to/saved-pairs --fps 2

# bounded run from the remote sender, or one-shot reconstruction of a saved sequence
MAX_PAIRS=30 ./tools/run_remote_two_camera_fusion.sh
FOURD_CAPTURE_ROOT=/path/to/capture-data ./tools/reconstruct_saved_two_camera.py
```

## Native C++ pipeline

For K4A-compatible cameras plugged directly into this machine, or recorded
`.mkv` files. It writes PLY, textured OBJ, Draco and stage metrics.

```bash
sudo apt install -y build-essential cmake ninja-build git \
  libopencv-dev libeigen3-dev libjsoncpp-dev libdraco-dev draco
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rgbd_streamer dual_camera_fusion -j"$(nproc)"
./build/app/rgbd_streamer <config>.json        # one camera or .mkv
./build/app/dual_camera_fusion config.dual.json
ctest --test-dir build --output-on-failure
```

## Files

- `python/protocol.py`: OBP1 packet definitions
- `tools/receive_mesh_frame.py`, `tools/receive_live_stream.py`: reference receivers for MRD1/2 and MRD3
- [`docs/artifacts.md`](../../../docs/artifacts.md): generated-data policy (outputs are git-ignored)
