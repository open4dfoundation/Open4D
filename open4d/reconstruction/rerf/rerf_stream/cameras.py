"""Where the cameras are, and where a ray starts and stops.

Two things a render needs that the bitstream does not carry: a viewpoint, and
the near and far planes to march between. Both come from the corpus ReRF was
trained on, and both must match what the trainer used -- a near plane that
disagrees moves where sampling begins and quietly costs quality.
"""
from __future__ import annotations

import json
import math
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


def _ring(corpus_dir):
    """The training rig's circle: centre, radius, up axis, mean focal length.

    ORBIT's corpus puts its eight cameras on one horizontal ring at a fixed
    radius, which is what makes a denser orbit a matter of interpolation rather
    than invention. Measured rather than assumed -- if a corpus ever arrives
    with cameras off a single ring this raises instead of quietly producing an
    orbit that does not match the views it claims to extend.
    """
    cameras = training_cameras(corpus_dir)
    positions = np.array([camera.c2w[:3, 3] for camera in cameras])
    centre = positions.mean(axis=0)
    radii = np.linalg.norm(positions - centre, axis=1)
    if radii.ptp() > 0.02 * radii.mean():
        raise ValueError(
            f"{corpus_dir}'s cameras are not on one ring (radii "
            f"{radii.min():.3f} to {radii.max():.3f}); an orbit through them "
            "would not pass through the views it is extending"
        )
    # The up axis is the one the ring does not span.
    up_axis = int(np.argmin(positions.var(axis=0)))
    focal = float(np.mean([camera.fx for camera in cameras]))
    return cameras, centre, float(radii.mean()), up_axis, focal


def orbit_cameras(corpus_dir, count: int = 36, *, height: float = 0.0):
    """``count`` cameras evenly spaced around the training ring.

    This is what a browser gets instead of a free camera for a representation
    it cannot decode. A neural field has no geometry to send, so "look around"
    becomes a dense set of prepared viewpoints: quantised, but a 10-degree step
    reads as orbiting rather than as cutting between cameras.

    Generated in the corpus's **normalised** frame, matching the extrinsics in
    ``cams_*.json``, because that is the frame the model is trained and
    rendered in. :func:`orbit_rig` converts the same orbit to world
    coordinates for a bundle's rig, where a geometry method has to line up.

    Every view shares one focal length -- the ring's mean -- so the framing
    holds steady while orbiting. The training cameras' own focals differ by up
    to 8%, and inheriting that would make the subject breathe as the view
    moved.
    """
    if count < 3:
        raise ValueError("an orbit needs at least 3 views")
    cameras, centre, radius, up_axis, focal = _ring(corpus_dir)
    first = cameras[0]
    plane = [axis for axis in range(3) if axis != up_axis]

    # Start where camera 0 is, so view 0 of the orbit is the view that already
    # exists -- which is what makes the orbit checkable against a real render.
    offset = first.c2w[:3, 3] - centre
    start = math.atan2(offset[plane[1]], offset[plane[0]])

    up = np.zeros(3)
    up[up_axis] = 1.0
    made = []
    for index in range(count):
        angle = start + 2.0 * math.pi * index / count
        position = np.array(centre, dtype=np.float64)
        position[plane[0]] += radius * math.cos(angle)
        position[plane[1]] += radius * math.sin(angle)
        position[up_axis] += height

        forward = centre - position
        forward /= np.linalg.norm(forward)
        down = -up
        right = np.cross(down, forward)
        right /= np.linalg.norm(right)

        c2w = np.eye(4)
        c2w[:3, 0], c2w[:3, 1], c2w[:3, 2] = right, down, forward
        c2w[:3, 3] = position
        made.append(Camera(
            camera_id=index, width=first.width, height=first.height,
            fx=focal, fy=focal,
            cx=(first.width - 1) * 0.5, cy=(first.height - 1) * 0.5,
            c2w=c2w,
        ))
    return made


def orbit_rig(corpus_dir, count: int = 36, *, height: float = 0.0) -> dict:
    """The same orbit as a bundle ``scenes`` entry, in world coordinates.

    A bundle's rig is what lets a viewer put every method at one pose, so it
    has to be in the shared world frame rather than the frame this model
    happens to be normalised into. The corpus records both:
    ``normalised = (world - centre) * scale``, with rotations unchanged.
    """
    with open(Path(corpus_dir) / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    centre = np.asarray(manifest["world_centre"], dtype=np.float64)
    scale = float(manifest["world_scale"])

    poses = []
    for camera in orbit_cameras(corpus_dir, count, height=height):
        position = camera.c2w[:3, 3] / scale + centre
        poses.append({
            "position": position.tolist(),
            "right": camera.c2w[:3, 0].tolist(),
            "down": camera.c2w[:3, 1].tolist(),
            "forward": camera.c2w[:3, 2].tolist(),
        })
    height_px = int(manifest["height"])
    return {
        "width": int(manifest["width"]),
        "height": height_px,
        "fov_y": float(2.0 * np.arctan(
            height_px * 0.5 / orbit_cameras(corpus_dir, count)[0].fy)),
        "bounds_min": list(manifest["world_bounds_min"]),
        "bounds_max": list(manifest["world_bounds_max"]),
        "poses": poses,
    }
