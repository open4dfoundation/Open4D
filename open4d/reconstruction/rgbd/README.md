# RGB-D reconstruction

Fuses two synchronized RGB-D camera streams into live point clouds and CUDA
meshes on an Ubuntu GPU host, with browser playback via Open3D WebRTC.

```text
2 cameras -> Windows capture host -> SSH tunnel -> Ubuntu receiver (fuse + CUDA TSDF) -> Open3D WebRTC -> browser
```

The point cloud updates on every camera pair; the mesh is rebuilt
periodically. From Python, `open4d.load_rgbd_capture()` and
`open4d.reconstruct()` run the same reconstruction on saved captures (see
[Without the live cameras](#without-the-live-cameras)).

## Setup

Hardware and the tested configuration are in the [repository README](../../../README.md).

```bash
python3 -m venv .venv && source .venv/bin/activate
python -m pip install numpy opencv-python zstandard
# plus a CUDA-enabled Open3D build (the pip wheel may not include CUDA)
python -c "import open3d as o3d; print(o3d.__version__, o3d.core.cuda.is_available())"
```

From an Open4D checkout, `python -m pip install -e '.[capture]'` installs the
Python capture dependencies, including Open3D and Zstandard. The CUDA check
above must return `True` for GPU reconstruction.

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

From Python, load saved pairs and reconstruct one mesh per pair:

```python
import open4d

capture = open4d.load_rgbd_capture(
    "/abs/path/to/saved-pairs",        # pair_<012d>/ directories
    "/abs/path/to/calibration",
    frames=range(84, 91),              # pair numbers; omit for all
)
sequence = open4d.reconstruct(capture, refine_poses=True, device="cuda")
open4d.save(sequence, "two_camera.usdc")
```

The loader reads either saved layout: replay pairs (`metadata.json` plus the
`.zst` depth and `.jpg` colour payloads it lists), or raw pairs
(`{ey,j3}_depth_u16le.raw` and `{ey,j3}_color.jpg`). For raw pairs, pass
`metadata=` the folder holding their `pair_<012d>/metadata.json`; without any
metadata, frames are spaced by `fps=`. Depth is rectified and colour aligned;
camera 1's depth frame defines world coordinates. All selected pairs stay in
memory (about 3.7 MB each). Install
`open4d[capture]` for OpenCV and Zstandard; an installed wheel is enough, since
rectification uses a packaged copy of the live receiver's projection code.

`refine_poses=True` corrects camera 2's calibrated pose once, with ICP on the
first pair, and records the result in `sequence.metadata`. Both cameras must see
static structure, such as floors or walls; subject-only captures can misalign
despite higher ICP fitness. Recalibrate if the cameras have moved.
`device="cuda"` (or `"cuda:1"`, `"cpu"`) uses Open3D's tensor TSDF; omit it
for the CPU `ScalableTSDFVolume`. Tensor meshes are coloured by projecting
their vertices into the images; vertices that no camera sees within the
truncation band, such as silhouette edges, take the mean colour of their
coloured neighbours. Frame times come from the cameras'
timestamps.

Without `device=`, the same capture and settings give bit-identical meshes
across runs, processes and OpenMP thread counts, and PLY or USDC export reloads
them exactly. The tensor TSDF gives the same surface, but its vertex and
triangle order can change between runs.

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

- `python/protocol.py`: OBP1 packet definitions. Invalid fields raise
  `ProtocolError`, a peer that disconnects mid-frame raises `EOFError`, and a
  stall raises `TimeoutError`, after which the connection must be dropped
- `tools/receive_mesh_frame.py`, `tools/receive_live_stream.py`: reference receivers for MRD1/2 and MRD3
- [`docs/artifacts.md`](../../../docs/artifacts.md): generated-data policy (outputs are git-ignored)
