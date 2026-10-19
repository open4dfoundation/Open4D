"""The capture rig, read back from the corpus, as the thing every method shares.

Comparing two reconstructions means rendering them from the same place, and the
only place both were ever fit to is the rig that captured the scene. So the
canonical camera path here is not invented -- it is the eight ORBIT cameras,
read out of the corpus's own ``transforms.json``. Three things follow from that
choice, and all three are the reason for it:

* **Ground truth exists at every station.** The captured image for a pose is
  `frame_NNNNNN/images/view_NN.png`, so a comparison can put the real photograph
  next to each method's render instead of only comparing methods to each other.
* **No convention translation.** ``camera_to_world_opencv`` is already the
  (right, down, forward) frame the viewer's projection uses, so a pose can be
  handed to the renderer as-is. The sibling ``transform_matrix`` is the OpenGL
  form and is deliberately ignored.
* **Nothing has to be registered.** Vega's Gaussians are in ORBIT world
  coordinates already, and ReRF's normalised volume maps back through
  ``world_centre``/``world_scale`` in its corpus manifest, so both land in the
  frame these poses are expressed in.

The rig is eight coplanar cameras on a ring at subject height. That is a real
limitation on what a comparison can show -- neither silhouette carving nor a
photometric fit can recover what no camera saw -- so a path that climbs far off
that plane makes every method look broken for reasons that are the capture's,
not the method's. :func:`ring_path` stays on it.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np


@dataclass(frozen=True)
class Pose:
    """One camera station, in ORBIT world coordinates.

    The three axes are the columns of the OpenCV camera-to-world rotation, named
    for what they are on screen. `(right, down, forward)` is right-handed in that
    order, which is what the viewer's projection assumes.
    """

    position: tuple[float, float, float]
    right: tuple[float, float, float]
    down: tuple[float, float, float]
    forward: tuple[float, float, float]
    #: Rig view index when this pose is a captured camera; None for a synthesised one.
    view_id: int | None = None

    @classmethod
    def from_c2w(cls, matrix, view_id: int | None = None) -> "Pose":
        m = np.asarray(matrix, dtype=np.float64)
        return cls(
            position=tuple(float(v) for v in m[:3, 3]),
            right=tuple(float(v) for v in m[:3, 0]),
            down=tuple(float(v) for v in m[:3, 1]),
            forward=tuple(float(v) for v in m[:3, 2]),
            view_id=view_id,
        )

    def c2w(self) -> np.ndarray:
        matrix = np.eye(4)
        matrix[:3, 0] = self.right
        matrix[:3, 1] = self.down
        matrix[:3, 2] = self.forward
        matrix[:3, 3] = self.position
        return matrix

    def as_dict(self) -> dict[str, Any]:
        return {
            "position": list(self.position),
            "right": list(self.right),
            "down": list(self.down),
            "forward": list(self.forward),
            "view_id": self.view_id,
        }


@dataclass
class Rig:
    """A scene's capture rig and the volume it was pointed at."""

    scene: str
    width: int
    height: int
    fl_x: float
    fl_y: float
    cx: float
    cy: float
    poses: list[Pose] = field(default_factory=list)
    bounds_min: list[float] | None = None
    bounds_max: list[float] | None = None

    @property
    def centre(self) -> np.ndarray:
        if self.bounds_min is None or self.bounds_max is None:
            return np.mean([pose.position for pose in self.poses], axis=0)
        return (np.asarray(self.bounds_min) + np.asarray(self.bounds_max)) / 2.0

    @property
    def radius(self) -> float:
        """Mean distance from the volume centre to a camera."""
        centre = self.centre
        return float(np.mean([np.linalg.norm(np.asarray(p.position) - centre) for p in self.poses]))

    def fov_y(self) -> float:
        """Vertical field of view in radians, which is what the viewer takes."""
        return 2.0 * math.atan(0.5 * self.height / self.fl_y)

    def as_dict(self) -> dict[str, Any]:
        return {
            "scene": self.scene,
            "width": self.width,
            "height": self.height,
            "fov_y": self.fov_y(),
            "bounds_min": self.bounds_min,
            "bounds_max": self.bounds_max,
            "poses": [pose.as_dict() for pose in self.poses],
        }


def _first_transforms(scene_dir: Path) -> Path:
    """A `transforms.json` for the scene -- the rig is fixed across frames.

    Per-frame first, because that is where the corpus actually writes them; the
    object-level file `dataset.json` points at is the same rig.
    """
    frames = sorted(p for p in scene_dir.glob("frame_*") if p.is_dir())
    for frame in frames:
        candidate = frame / "transforms.json"
        if candidate.is_file():
            return candidate
    candidate = scene_dir / "transforms.json"
    if candidate.is_file():
        return candidate
    raise FileNotFoundError(f"no transforms.json under {scene_dir}")


def read_orbit_rig(scene_dir: Path | str) -> Rig:
    """The rig for one ORBIT object, from its own `transforms.json`."""
    scene_dir = Path(scene_dir).expanduser().resolve()
    data = json.loads(_first_transforms(scene_dir).read_text())
    views = sorted(data["frames"], key=lambda entry: entry.get("view_id", 0))
    poses = [
        # `camera_to_world_opencv`, not `transform_matrix`: the latter is the
        # OpenGL form (y up, -z forward) and would silently flip the image.
        Pose.from_c2w(entry["camera_to_world_opencv"], entry.get("view_id", index))
        for index, entry in enumerate(views)
    ]
    return Rig(
        scene=scene_dir.name,
        width=int(data["w"]),
        height=int(data["h"]),
        fl_x=float(data["fl_x"]),
        fl_y=float(data["fl_y"]),
        cx=float(data["cx"]),
        cy=float(data["cy"]),
        poses=poses,
        bounds_min=data.get("bounds_min"),
        bounds_max=data.get("bounds_max"),
    )


def ring_path(rig: Rig, samples: int) -> list[Pose]:
    """`samples` poses evenly spaced on the rig's own ring.

    Used when a comparison wants a smoother sweep than eight stations. It stays
    at the rig's radius and height on purpose: off that ring there is no captured
    image to compare against, and below or above it there is no captured
    *geometry* either -- see the module docstring.

    A sample that lands on a station keeps that station's `view_id`, so ground
    truth is still addressable wherever it exists.
    """
    if samples <= 0:
        raise ValueError("samples must be positive")
    centre = rig.centre
    positions = np.asarray([pose.position for pose in rig.poses])
    height = float(np.mean(positions[:, 1]))
    radius = float(np.mean(np.linalg.norm(positions[:, [0, 2]] - centre[[0, 2]], axis=1)))

    # Start where view 0 is, so sample 0 coincides with a real camera.
    first = positions[0] - centre
    start = math.atan2(float(first[0]), float(first[2]))
    # Follow the rig's own direction of travel, so a sweep matches view order.
    second = positions[1 % len(positions)] - centre
    step = math.atan2(float(second[0]), float(second[2])) - start
    step = (step + math.pi) % (2 * math.pi) - math.pi
    direction = 1.0 if step >= 0 else -1.0

    target = np.array([centre[0], height, centre[2]])
    stations = {index: pose for index, pose in enumerate(rig.poses)}
    out: list[Pose] = []
    for index in range(samples):
        # Land exactly on a station when the sampling divides the ring evenly.
        on_station, remainder = divmod(index * len(rig.poses), samples)
        if remainder == 0 and on_station in stations:
            out.append(stations[on_station])
            continue
        angle = start + direction * 2 * math.pi * index / samples
        eye = np.array([
            centre[0] + radius * math.sin(angle),
            height,
            centre[2] + radius * math.cos(angle),
        ])
        out.append(look_at(eye, target))
    return out


def look_at(eye, target, world_up=(0.0, 1.0, 0.0)) -> Pose:
    """A pose looking from `eye` at `target`, in the (right, down, forward) frame."""
    eye = np.asarray(eye, dtype=np.float64)
    forward = np.asarray(target, dtype=np.float64) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.asarray(world_up, dtype=np.float64))
    right /= np.linalg.norm(right)
    down = np.cross(forward, right)
    return Pose(
        position=tuple(float(v) for v in eye),
        right=tuple(float(v) for v in right),
        down=tuple(float(v) for v in down),
        forward=tuple(float(v) for v in forward),
    )
