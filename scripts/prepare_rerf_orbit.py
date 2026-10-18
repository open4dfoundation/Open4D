"""Prepare calibrated ORBIT RGB views for ReRF without changing source data."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from open4d.orbit import load_orbit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--scene", default="basketball")
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--holdout-view", type=int, default=0)
    args = parser.parse_args()
    if min(args.frames, args.width) < 1:
        parser.error("frames and width must be positive")
    scene = load_orbit(args.source, args.scene).select(args.frames)
    target = args.destination.resolve()
    target.mkdir(parents=True, exist_ok=False)
    width = min(args.width, scene.cameras[0].width)
    scale = width / scene.cameras[0].width
    height = round(scene.cameras[0].height * scale)
    paths = []
    cameras = []
    for camera in scene.cameras:
        cameras.append({"view_id": camera.view_id, "width": width, "height": height,
                        "fx": camera.fx * scale, "fy": camera.fy * scale,
                        "cx": camera.cx * scale, "cy": camera.cy * scale,
                        "c2w": camera.camera_to_world.tolist()})
    for frame in range(len(scene)):
        entries = []
        for view, camera in enumerate(scene.cameras):
            image_path = scene.image_path(frame, view)
            image = Image.open(image_path).convert("RGB").resize((width, height), Image.Resampling.LANCZOS)
            color = target / f"rgb_{frame:04d}_{camera.view_id:02d}.png"
            mask = target / f"mask_{frame:04d}_{camera.view_id:02d}.png"
            image.save(color)
            Image.fromarray(np.full((height, width, 3), 255, np.uint8)).save(mask)
            paths.append({"source": str(image_path), "source_sha256": hashlib.sha256(image_path.read_bytes()).hexdigest(),
                          "prepared": color.name, "sha256": hashlib.sha256(color.read_bytes()).hexdigest()})
            if camera.view_id == args.holdout_view:
                continue
            intrinsic = [[camera.fx * scale, 0, camera.cx * scale],
                         [0, camera.fy * scale, camera.cy * scale], [0, 0, 1]]
            entries.append({"file": str(color), "mask": str(mask), "intrinsic": intrinsic,
                            "extrinsic": camera.camera_to_world.tolist()})
        if not entries:
            raise ValueError("the holdout excludes every camera")
        (target / f"cams_{frame}.json").write_text(json.dumps({"frames": entries}, indent=2))
    (target / "bbox.json").write_text(json.dumps({"xyz_min": scene.bounds_min.tolist(), "xyz_max": scene.bounds_max.tolist()}))
    metadata = {"source": str(scene.path), "source_frames": list(scene.source_frames),
                "scene": scene.name, "width": width, "height": height, "frames": len(scene),
                "holdout_view": args.holdout_view, "cameras": cameras, "images": paths,
                "camera_convention": "OpenCV camera-to-world; inverse_y=True in ReRF"}
    (target / "capture.json").write_text(json.dumps(metadata, indent=2))
    print(target)


if __name__ == "__main__":
    main()
