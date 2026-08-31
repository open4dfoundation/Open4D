"""Viewports to score neural visibility from.

Two sources, both producing :class:`nevo.cameras.Camera` in the corpus's
normalised frame:

:func:`sample_viewports`
    Synthetic viewports drawn around the content. The NeVo paper's importance
    CDF is measured "with each frame tested against 300 different viewports"
    (section 3.2), and no recorded trajectory exists for the ORBIT objects, so
    this is what the CDF is built on.

:func:`read_quest_trace` / :func:`trace_viewports`
    A real 6DoF trace in this repo's Quest logger format
    (``system/QuestClient/.../ViewportTraceLogger.cs``), which is what the
    end-to-end simulation replays. Positions are metres in the composed scene
    frame and rotations are yaw/pitch/roll degrees; both the measured pose and
    the pose the on-device predictor guessed a window earlier are recorded, so
    a trace supplies the *predicted* viewport the edge would have fetched for
    and the *actual* viewport the client reprojects to.
"""
from __future__ import annotations

import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

import numpy as np

from .cameras import Camera, look_at_c2w

TRACE_FEATURES = ("x", "y", "z", "yaw", "pitch", "roll")


@dataclass(frozen=True)
class ViewportSpread:
    """Where a viewer is assumed to be, relative to the content.

    Defaults describe someone walking around a body-scale subject on a headset:
    a little inside to a little outside the capture rig, eye height ranging
    from below the subject's chest to looking down on it, and always roughly
    facing it.
    """

    radius_scale: tuple = (0.75, 1.45)
    elevation_degrees: tuple = (-25.0, 55.0)
    #: Fraction of the bbox half-extent the look-at point may wander by, so
    #: the rays are not all funnelled through one point.
    aim_jitter: float = 0.25


def sample_viewports(
    count: int,
    xyz_min: Sequence[float],
    xyz_max: Sequence[float],
    *,
    reference_radius: float,
    width: int,
    height: int,
    focal: float,
    spread: ViewportSpread = ViewportSpread(),
    up_axis: int = 1,
    seed: int = 0,
) -> List[Camera]:
    """Draw ``count`` viewports around the content bounding box."""
    if count < 1:
        raise ValueError("count must be positive")
    lower = np.asarray(xyz_min, dtype=np.float64)
    upper = np.asarray(xyz_max, dtype=np.float64)
    centre = (lower + upper) * 0.5
    half_extent = (upper - lower) * 0.5
    rng = np.random.default_rng(seed)

    up = np.zeros(3)
    up[up_axis] = 1.0
    plane = [axis for axis in range(3) if axis != up_axis]

    cameras: List[Camera] = []
    for index in range(count):
        radius = reference_radius * rng.uniform(*spread.radius_scale)
        azimuth = rng.uniform(0.0, 2.0 * math.pi)
        elevation = math.radians(rng.uniform(*spread.elevation_degrees))
        direction = np.zeros(3)
        direction[plane[0]] = math.cos(azimuth) * math.cos(elevation)
        direction[plane[1]] = math.sin(azimuth) * math.cos(elevation)
        direction[up_axis] = math.sin(elevation)
        aim = centre + rng.uniform(-1.0, 1.0, size=3) * half_extent * spread.aim_jitter
        eye = centre + direction * radius
        cameras.append(
            Camera(
                camera_id=index,
                width=width,
                height=height,
                fx=focal,
                fy=focal,
                cx=(width - 1) * 0.5,
                cy=(height - 1) * 0.5,
                c2w=look_at_c2w(eye, aim, up),
            )
        )
    return cameras


def downscale(cameras: Iterable[Camera], factor: int) -> List[Camera]:
    """Shrink every camera by an integer factor, intrinsics included.

    Importance is a max over the samples that land in a voxel, so it is far
    less ray-density-sensitive than a rendered image -- halving the resolution
    cuts the marching cost 4x. It is not free, though: thin structures can stop
    being hit at all, which biases the CDF towards "unimportant". Verify before
    relying on it.
    """
    if factor < 1:
        raise ValueError("factor must be >= 1")
    if factor == 1:
        return list(cameras)
    scaled = []
    for camera in cameras:
        width = max(1, camera.width // factor)
        height = max(1, camera.height // factor)
        scaled.append(
            Camera(
                camera_id=camera.camera_id,
                width=width,
                height=height,
                fx=camera.fx / factor,
                fy=camera.fy / factor,
                cx=(camera.cx + 0.5) / factor - 0.5,
                cy=(camera.cy + 0.5) / factor - 0.5,
                c2w=camera.c2w,
            )
        )
    return scaled


# --------------------------------------------------------------- Quest traces
@dataclass
class ViewportTrace:
    """A recorded 6DoF trajectory plus the predictions made during it."""

    times: np.ndarray                    # [N] seconds since the run started
    actual: np.ndarray                   # [N, 6] x y z yaw pitch roll
    predicted: np.ndarray                # [N, 6], NaN where no prediction resolved
    predicted_target: np.ndarray         # [N] seconds the prediction aimed at, NaN if none

    def __len__(self) -> int:
        return int(self.times.shape[0])

    @property
    def prediction_error(self) -> np.ndarray:
        """Translational error in metres, NaN where unresolved."""
        return np.linalg.norm(self.actual[:, :3] - self.predicted[:, :3], axis=-1)


def read_quest_trace(path, streaming_only: bool = True) -> ViewportTrace:
    """Read this repo's Quest viewport CSV.

    Columns come from ``ViewportTraceLogger.CsvHeader``. ``streaming_only``
    keeps the rows recorded while content was actually being streamed, which is
    the same filter ``system/QuestClient/Tools/plot_viewport_traces.py`` applies.
    """
    times: List[float] = []
    actual: List[List[float]] = []
    predicted: List[List[float]] = []
    targets: List[float] = []
    with open(Path(path), newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if streaming_only and row.get("streaming") != "1":
                continue
            times.append(float(row["t_s"]))
            actual.append([float(row[name]) for name in TRACE_FEATURES])
            if row.get("pred_valid") == "1":
                predicted.append([float(row["pred_" + name]) for name in TRACE_FEATURES])
                targets.append(float(row["pred_target_s"]))
            else:
                predicted.append([math.nan] * len(TRACE_FEATURES))
                targets.append(math.nan)
    if not times:
        raise ValueError(f"{path} holds no usable rows")
    return ViewportTrace(
        np.asarray(times, dtype=np.float64),
        np.asarray(actual, dtype=np.float64),
        np.asarray(predicted, dtype=np.float64),
        np.asarray(targets, dtype=np.float64),
    )


CONVENTIONS = ("unity", "right_handed")


def pose_to_c2w(pose: Sequence[float], convention: str = "unity") -> np.ndarray:
    """OpenCV camera-to-world for one ``(x, y, z, yaw, pitch, roll)`` trace row.

    Rotations are degrees, applied yaw (Y) then pitch (X) then roll (Z), which
    is Unity's ``Quaternion.Euler`` order and what ``ViewportPredictor``
    interpolates in.

    ``convention`` says what frame the *trace* is in:

    ``unity``
        Left-handed, Y up, +Z forward -- the frame a Unity ``Transform``
        reports, and therefore what ``ViewportTraceLogger`` writes. Mapping it
        into the right-handed frame this codebase renders in flips Z in the
        world (``W``) and, separately, flips Y in the camera's own axes to get
        OpenCV's y-down (``L``): ``c2w = W . R . L``. Both flips are needed;
        applying either alone leaves a mirrored (determinant -1) rotation that
        renders a plausible but laterally-inverted view.
    ``right_handed``
        Y up, -Z forward (glTF / the frame ``scene_layout.json`` describes for
        the composed ORBIT scene). No world flip; the camera's own axes still
        move, since OpenCV wants y down and z forward.

    Which one a given trace needs depends on where it was recorded, and no
    recorded trace exists in this repo to settle it -- so it is an explicit
    argument rather than a silent default baked into the pipeline. It is also
    only half the job: putting a headset pose into a *corpus* frame needs the
    object's placement in the scene too (``scene_layout.json``), which arrives
    with the end-to-end simulation.
    """
    if convention not in CONVENTIONS:
        raise ValueError(f"convention must be one of {CONVENTIONS}")
    x, y, z, yaw, pitch, roll = (float(v) for v in pose)
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    rotation_y = np.asarray(((cy, 0.0, sy), (0.0, 1.0, 0.0), (-sy, 0.0, cy)))
    rotation_x = np.asarray(((1.0, 0.0, 0.0), (0.0, cp, -sp), (0.0, sp, cp)))
    rotation_z = np.asarray(((cr, -sr, 0.0), (sr, cr, 0.0), (0.0, 0.0, 1.0)))
    rotation = rotation_y @ rotation_x @ rotation_z

    position = np.asarray((x, y, z))
    if convention == "unity":
        # OpenCV local axes expressed in Unity local axes: right stays, down is
        # -up, forward stays (+Z). Then flip the world's Z to make it
        # right-handed. det = (-1) * (+1) * (-1) = +1.
        rotation = np.diag((1.0, 1.0, -1.0)) @ rotation @ np.diag((1.0, -1.0, 1.0))
        position = position * np.asarray((1.0, 1.0, -1.0))
    else:
        # Already right-handed with -Z forward, so only the camera's own axes
        # move: down is -up and forward is -backward. det = +1, no world flip.
        rotation = rotation @ np.diag((1.0, -1.0, -1.0))

    c2w = np.eye(4)
    c2w[:3, :3] = rotation
    c2w[:3, 3] = position
    return c2w


def trace_viewports(
    trace: ViewportTrace,
    *,
    width: int,
    height: int,
    focal: float,
    centre: Sequence[float],
    scale: float,
    use_predicted: bool = False,
    convention: str = "unity",
    indices: Optional[Sequence[int]] = None,
) -> List[Camera]:
    """Turn trace rows into cameras in the corpus's normalised frame.

    ``centre``/``scale`` are ``nevo_corpus.json``'s ``world_centre`` and
    ``world_scale``: the same world -> normalised map the training extrinsics
    went through.
    """
    poses = trace.predicted if use_predicted else trace.actual
    rows = range(len(trace)) if indices is None else indices
    origin = np.asarray(centre, dtype=np.float64)
    cameras: List[Camera] = []
    for index in rows:
        pose = poses[index]
        if not np.all(np.isfinite(pose)):
            continue
        c2w = pose_to_c2w(pose, convention)
        c2w[:3, 3] = (c2w[:3, 3] - origin) * scale
        cameras.append(
            Camera(
                camera_id=int(index),
                width=width,
                height=height,
                fx=focal,
                fy=focal,
                cx=(width - 1) * 0.5,
                cy=(height - 1) * 0.5,
                c2w=c2w,
            )
        )
    if not cameras:
        raise ValueError("no rows in the trace carried a finite pose")
    return cameras
