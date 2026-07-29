# Remote two-camera Femto Bolt reconstruction

The two Femto Bolt cameras are USB-connected to the Windows capture host, not
to this GPU machine. The original `dual_camera_fusion` executable opens local
K4A-compatible USB devices and therefore cannot capture this installation
directly.

`tools/run_remote_two_camera_fusion.sh` is the module entry point for the
installed topology:

```
Windows synchronized RGB-D capture
  -> OBP1 TCP transport
  -> calibrated J3-depth to EY-depth transform
  -> shared TSDF reconstruction
  -> full-scene point cloud and triangle mesh
```

Run a bounded reconstruction:

```bash
cd /home/ryan/Open4D/open4d/modules/4d-reconstruction
MAX_PAIRS=30 ./tools/run_remote_two_camera_fusion.sh
```

Outputs:

- `output/two-camera-fusion/latest_live_full_scene_pointcloud.ply`
- `output/two-camera-fusion/latest_live_full_scene_mesh.ply`
- `output/two-camera-fusion/live_fusion_report.json`

The reconstruction excludes only invalid or out-of-range depth measurements.
It performs no spatial crop, connected-component filtering, geometry
decimation, or scene cleanup.

When the Windows sender is unavailable, reproduce a module-owned result from
the last saved synchronized sequence:

```bash
./tools/reconstruct_saved_two_camera.py
```

This writes `output/two-camera-fusion/saved_sequence_two_camera_fused_mesh.ply`
and `saved_sequence_fusion_report.json`.

If both cameras are later USB-connected directly to this GPU host, build and
run the native CUDA path:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target dual_camera_fusion -j2
./build/app/dual_camera_fusion config.dual.json
```
