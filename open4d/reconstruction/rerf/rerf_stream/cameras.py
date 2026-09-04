"""Where the cameras are, and where a ray starts and stops.

Two things a render needs that the bitstream does not carry: a viewpoint, and
the near and far planes to march between. Both come from the corpus ReRF was
trained on, and both must match what the trainer used -- a near plane that
disagrees moves where sampling begins and quietly costs quality.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple

import numpy as np


@dataclass(frozen=True)
class Camera:
    """A pinhole camera.

    ``c2w`` is OpenCV camera-to-world (x right, y down, z forward), which is
    what ReRF's NHR loader expects when the config sets ``inverse_y=True``.
    """

    camera_id: int
    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    c2w: np.ndarray

    @property
    def intrinsic_matrix(self) -> np.ndarray:
        return np.asarray(
            ((self.fx, 0.0, self.cx), (0.0, self.fy, self.cy), (0.0, 0.0, 1.0)),
            dtype=np.float64,
        )

    def scaled(self, factor: float) -> "Camera":
        """The same view at a fraction of the resolution.

        Every intrinsic scales with the image, so the field of view is
        unchanged and only the ray count moves. That is the one knob that
        trades quality for frame rate without touching the bitstream -- and
        scaling the resolution *without* the intrinsics would zoom in instead,
        which looks like a working speed-up until it is compared against
        anything.
        """
        import dataclasses

        if factor == 1.0:
            return self
        return dataclasses.replace(
            self,
            width=max(16, int(round(self.width * factor))),
            height=max(16, int(round(self.height * factor))),
            fx=self.fx * factor,
            fy=self.fy * factor,
            cx=self.cx * factor,
            cy=self.cy * factor,
        )


def training_cameras(corpus_dir, frame_index: int = 0) -> List[Camera]:
    """Every camera the trainer saw for one frame, in the trainer's own order.

    ``load_NHR`` sorts views by file path before stacking, so that order *is*
    what a view index means everywhere else.

    Image dimensions come from the PNG header rather than a decode: the
    intrinsics in ``cams_*.json`` carry no size, and a camera whose width and
    height disagree with its principal point renders an offset image.
    """
    from PIL import Image

    path = Path(corpus_dir) / ("cams_%d.json" % frame_index)
    with open(path) as handle:
        entries = sorted(json.load(handle)["frames"], key=lambda item: item["file"])
    cameras = []
    for view, entry in enumerate(entries):
        intrinsic = np.asarray(entry["intrinsic"], dtype=np.float64)
        width, height = Image.open(entry["file"]).size
        cameras.append(
            Camera(
                camera_id=view,
                width=int(width),
                height=int(height),
                fx=float(intrinsic[0, 0]),
                fy=float(intrinsic[1, 1]),
                cx=float(intrinsic[0, 2]),
                cy=float(intrinsic[1, 2]),
                c2w=np.asarray(entry["extrinsic"], dtype=np.float64),
            )
        )
    return cameras


def captured_image(corpus_dir, frame_index: int, view: int, *, background: float):
    """The photograph a rendered view is scored against, over ``background``.

    ``lib.load_data`` composites ``rgb * alpha + bg * (1 - alpha)`` before the
    trainer ever sees a pixel, so anything compared against a render has to be
    composited the same way -- and onto the *same* background the config sets.
    Scoring a black-background render against a white composite gives about
    0.3 dB and reads as a broken decoder rather than a broken comparison.
    """
    from PIL import Image

    path = Path(corpus_dir) / ("cams_%d.json" % frame_index)
    with open(path) as handle:
        entries = sorted(json.load(handle)["frames"], key=lambda item: item["file"])
    entry = entries[view]
    rgb = np.asarray(Image.open(entry["file"]).convert("RGB"), dtype=np.float32) / 255.0
    alpha = (
        np.asarray(Image.open(entry["mask"]).convert("L"), dtype=np.float32)[..., None]
        / 255.0
    )
    return rgb * alpha + float(background) * (1.0 - alpha)


def inward_near_far(corpus_dir) -> Tuple[float, float]:
    """Reproduce ``lib.load_data.inward_nearfar_heuristic`` for a corpus.

    Read off ``cams_0.json`` rather than by loading the corpus: the heuristic
    only looks at camera positions, and decoding 48 views of every frame to
    learn two scalars costs a minute and several gigabytes.
    """
    with open(Path(corpus_dir) / "cams_0.json") as handle:
        entries = json.load(handle)["frames"]
    # Sorted by file path, matching the order the trainer stacked them in.
    positions = np.asarray(
        [
            np.asarray(entry["extrinsic"], dtype=np.float64)[:3, 3]
            for entry in sorted(entries, key=lambda item: item["file"])
        ]
    )
    distance = np.linalg.norm(positions[:, None] - positions, axis=-1)
    far = float(distance.max() * 1.4)
    return far * 0.05, far


def psnr(prediction: np.ndarray, truth: np.ndarray) -> float:
    """Peak signal-to-noise ratio, in dB, between two [0, 1] images."""
    error = float(np.mean((prediction.astype(np.float64) - truth.astype(np.float64)) ** 2))
    if error <= 0.0:
        return float("inf")
    return float(-10.0 * np.log10(error))


def capture_rig(corpus_dir) -> dict:
    """The rig, in world coordinates, as a bundle's ``scenes`` entry wants it.

    A bundle groups clips by subject and lets a viewer pick a *station* -- one
    physical camera -- so that every method's pane shows the same pose. That
    needs the rig described once per scene, in the shared world frame rather
    than the normalised one the network is trained in, because a method whose
    output is geometry has to line up with it.

    ``c2w_world`` is OpenCV camera-to-world, so its columns are the camera's
    axes: right, down, forward. Read off the corpus manifest rather than
    recomputed, so these are the poses the renders were actually taken at.
    """
    with open(Path(corpus_dir) / "nevo_corpus.json") as handle:
        manifest = json.load(handle)

    poses = []
    for entry in sorted(manifest["cameras"], key=lambda item: int(item["camera_id"])):
        c2w = np.asarray(entry["c2w_world"], dtype=np.float64)
        poses.append({
            "position": c2w[:3, 3].tolist(),
            "right": c2w[:3, 0].tolist(),
            "down": c2w[:3, 1].tolist(),
            "forward": c2w[:3, 2].tolist(),
        })

    first = manifest["cameras"][0]
    height = int(manifest["height"])
    return {
        "width": int(manifest["width"]),
        "height": height,
        # From the intrinsics these views were rendered with, so a geometry
        # method added to this scene later frames the subject identically.
        "fov_y": float(2.0 * np.arctan(height * 0.5 / float(first["fy"]))),
        "bounds_min": list(manifest["world_bounds_min"]),
        "bounds_max": list(manifest["world_bounds_max"]),
        "poses": poses,
    }
