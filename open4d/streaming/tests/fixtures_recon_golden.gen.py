"""Golden reconstruction results, produced by the Python implementation.

Runs `ReconstructionState` over a synthetic keyframe + two delta frames with a
hand-built point cloud, exercising motion blocks, removal blocks and residual
append together. Draco is bypassed via an injected decoder so this measures the
delta model, not the codec.

Regenerate:
  <env-python> tests/fixtures_recon_golden.gen.py > tests/fixtures_recon_golden.json
"""
import json, sys
sys.path.insert(0, '/home/ryan/4DVideoStreaming')
import numpy as np
from baselines.DeltaStream.orbitstream.protocol import (
    ConnectionHeader, HeaderObject, StreamMode, Frame, FrameRecord, FrameType,
    MotionRecord)
from baselines.DeltaStream.orbitstream.manifest import CameraCalibration
from baselines.DeltaStream.orbitstream.reconstruction import (
    ReconstructionState, PointCloud)

WIDTH, HEIGHT, BLOCK = 64, 48, 16
cam0 = CameraCalibration(camera_id=0, width=WIDTH, height=HEIGHT,
                         fx=50.0, fy=50.0, cx=32.0, cy=24.0,
                         camera_to_world=((1,0,0,0.0),(0,1,0,0.0),(0,0,1,0.0),(0,0,0,1)))
# A second camera with a real rotation+translation, so worldClouds is exercised.
cam1 = CameraCalibration(camera_id=1, width=WIDTH, height=HEIGHT,
                         fx=50.0, fy=50.0, cx=32.0, cy=24.0,
                         camera_to_world=((0,0,1,1.0),(0,1,0,2.0),(-1,0,0,3.0),(0,0,0,1)))
header = ConnectionHeader(mode=StreamMode.DELTASTREAM, width=WIDTH, height=HEIGHT,
                          fps_num=30000, fps_den=1000, block_size=BLOCK,
                          objects=(HeaderObject(1, "objA", 100, (cam0, cam1)),),
                          calibration_hash="recon")

def grid_cloud(seed, n=40):
    """Points spread across the image at varied depths, deterministic."""
    rng = np.random.default_rng(seed)
    z = rng.uniform(0.5, 3.0, n).astype(np.float32)
    u = rng.uniform(0, WIDTH, n).astype(np.float32)
    v = rng.uniform(0, HEIGHT, n).astype(np.float32)
    x = (u - 32.0) * z / 50.0
    y = (v - 24.0) * z / 50.0
    pos = np.column_stack((x, y, z)).astype(np.float32)
    col = rng.integers(0, 256, (n, 3)).astype(np.uint8)
    return PointCloud(pos, col)

key0 = grid_cloud(1, 40)
key1 = grid_cloud(2, 30)
res0 = grid_cloud(3, 5)
res1 = grid_cloud(4, 7)

payloads = {}
def payload(tag, cloud):
    payloads[tag] = cloud
    return tag.encode()

frames = [
    Frame(0, 1, 2, FrameType.KEYFRAME, (
        FrameRecord(1, 0, payload('k0', key0), (), (), key0.point_count),
        FrameRecord(1, 1, payload('k1', key1), (), (), key1.point_count))),
    Frame(1, 3, 4, FrameType.DELTA, (
        FrameRecord(1, 0, payload('r0', res0), (5, 6),
                    (MotionRecord(3, 0.01, -0.02, 0.03, 24, 24),), res0.point_count),
        FrameRecord(1, 1, b'', (), (), 0))),
    Frame(2, 5, 6, FrameType.DELTA, (
        FrameRecord(1, 0, b'', (0, 1, 2), (), 0),
        FrameRecord(1, 1, payload('r1', res1), (),
                    (MotionRecord(1, -0.05, 0.0, 0.1, 40, 8),), res1.point_count))),
]

def decode(blob):
    return payloads[blob.decode()] if blob else PointCloud.empty()

state = ReconstructionState(header)
steps = []
for frame in frames:
    # The Python apply() insists every declared stream appears; our frames do.
    worlds = state.apply(frame, decode)
    steps.append({
        str(obj_id): {
            "count": int(cloud.point_count),
            "positions": [round(float(v), 5) for v in cloud.positions.ravel()],
            "colors": [int(v) for v in cloud.colors.ravel()],
        } for obj_id, cloud in sorted(worlds.items())
    })

print(json.dumps({
    "header": {
        "width": WIDTH, "height": HEIGHT, "blockSize": BLOCK,
        "objects": [{"objectId": 1, "name": "objA", "loopFrames": 100,
                     "cameras": [
                        {"cameraId": 0, "fx": 50.0, "fy": 50.0, "cx": 32.0, "cy": 24.0,
                         "cameraToWorldRowMajor": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]},
                        {"cameraId": 1, "fx": 50.0, "fy": 50.0, "cx": 32.0, "cy": 24.0,
                         "cameraToWorldRowMajor": [0,0,1,1, 0,1,0,2, -1,0,0,3, 0,0,0,1]}]}],
    },
    "payloads": {tag: {"positions": [round(float(v),5) for v in c.positions.ravel()],
                       "colors": [int(v) for v in c.colors.ravel()]}
                 for tag, c in payloads.items()},
    "frames": [{
        "frameId": f.frame_id,
        "frameType": "keyframe" if f.frame_type is FrameType.KEYFRAME else "delta",
        "records": [{
            "objectId": r.object_id, "cameraId": r.camera_id,
            "payloadTag": r.draco.decode() if r.draco else "",
            "removalBlocks": list(r.removal_blocks),
            "motions": [{"blockIndex": m.block_index, "deltaX": round(m.delta_x,5),
                         "deltaY": round(m.delta_y,5), "deltaZ": round(m.delta_z,5),
                         "sourceX": m.source_x, "sourceY": m.source_y}
                        for m in r.motions],
            "pointCount": r.point_count} for r in f.records]} for f in frames],
    "expected": steps,
}, indent=1))
