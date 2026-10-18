"""Load recorded RGB-D sequences for open4d.reconstruct.

Two common public layouts are read into an ``RGBDCapture``:

- ``redwood`` (Open3D's ``SampleRedwoodRGBDImages`` and the Redwood scenes):
  ``color/*.jpg|png`` and ``depth/*.png`` (16-bit millimetres), an optional
  ``rgbd.match`` pairing them, ``trajectory.log`` camera-to-world poses and a
  ``*.json`` PinholeCameraIntrinsic file;
- ``tum`` (TUM RGB-D benchmark): ``rgb.txt`` and ``depth.txt`` listing
  timestamped images (depth PNGs in 1/5000 m), and ``groundtruth.txt`` poses
  (``timestamp tx ty tz qx qy qz qw``), associated and interpolated at the
  depth timestamps.

Images must already be registered (colour aligned to depth, same size).
"""

from __future__ import annotations

import importlib
import json
import math
import re
from pathlib import Path

import numpy as np

from ._capture import RGBDCapture

LAYOUTS = ("redwood", "tum")
#: Depth units per metre each layout stores.
DEPTH_SCALES = {"redwood": 1000.0, "tum": 5000.0}
#: Published TUM RGB-D colour intrinsics (fx, fy, cx, cy) per Kinect.
TUM_INTRINSICS = {
    "freiburg1": (517.3, 516.5, 318.6, 255.3),
    "freiburg2": (520.9, 521.0, 325.1, 249.7),
    "freiburg3": (535.4, 539.2, 320.1, 247.6),
}
_IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg")


def _imread(path: Path, *, depth: bool) -> np.ndarray:
    """Read 16-bit depth or 8-bit RGB with OpenCV, else Pillow."""
    try:
        cv2 = importlib.import_module("cv2")
    except ModuleNotFoundError:
        cv2 = None
    if cv2 is not None:
        image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED if depth else cv2.IMREAD_COLOR)
        if image is None:
            raise ValueError(f"{path} is not a readable image")
        if not depth:
            image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    else:
        try:
            Image = importlib.import_module("PIL.Image")
        except ModuleNotFoundError as error:
            raise ModuleNotFoundError(
                "Reading RGB-D sequences needs OpenCV or Pillow. Install open4d[capture].",
                name="cv2") from error
        with Image.open(path) as source:
            image = np.asarray(source if depth else source.convert("RGB"))
    if depth:
        if image.ndim != 2 or image.dtype.kind not in "ui":
            raise ValueError(f"{path} is not a single-channel integer depth image")
        return image.astype(np.uint16, copy=False)
    return np.ascontiguousarray(image, dtype=np.uint8)


def _detect(folder: Path) -> str:
    if (folder / "depth.txt").is_file() and (folder / "rgb.txt").is_file():
        return "tum"
    if (folder / "depth").is_dir() and ((folder / "color").is_dir() or (folder / "image").is_dir()):
        return "redwood"
    raise ValueError(f"{folder} is neither a Redwood (color/, depth/) nor a TUM "
                     "(rgb.txt, depth.txt) RGB-D sequence; pass layout=")


def _images(folder: Path) -> list[Path]:
    return sorted(path for path in folder.iterdir() if path.suffix.lower() in _IMAGE_SUFFIXES)


def _inside(root: Path, name: str) -> Path:
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise ValueError(f"image path leaves the sequence folder {root}: {name}")
    if not path.is_file():
        raise FileNotFoundError(f"{root}: image {name} is missing")
    return path


def read_trajectory_log(path: Path) -> list[np.ndarray]:
    """Camera-to-world 4x4 poses from a Redwood/Open3D ``.log`` trajectory."""
    rows = [line.split() for line in Path(path).read_text().splitlines() if line.strip()]
    if len(rows) % 5:
        raise ValueError(f"{path}: a trajectory has one header and four matrix rows per pose")
    poses = []
    for start in range(0, len(rows), 5):
        try:
            matrix = np.array([[float(value) for value in row] for row in rows[start + 1:start + 5]])
        except ValueError as error:
            raise ValueError(f"{path}: pose {start // 5} is not numeric") from error
        if matrix.shape != (4, 4) or not np.isfinite(matrix).all():
            raise ValueError(f"{path}: pose {start // 5} is not a 4x4 matrix")
        poses.append(matrix)
    return poses


def _intrinsics_file(folder: Path):
    for path in sorted(folder.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(data, dict) and "intrinsic_matrix" in data:
            # Open3D stores the 3x3 matrix column-major.
            k = np.asarray(data["intrinsic_matrix"], dtype=np.float64).reshape(3, 3).T
            size = (int(data.get("width", 0)), int(data.get("height", 0)))
            return (k[0, 0], k[1, 1], k[0, 2], k[1, 2]), size, path.name
    return None


def _redwood(folder: Path):
    colour_dir = folder / "color" if (folder / "color").is_dir() else folder / "image"
    match = folder / "rgbd.match"
    if match.is_file():
        pairs = []
        for number, line in enumerate(match.read_text().splitlines()):
            if not line.strip():
                continue
            parts = line.split()
            if len(parts) != 2:
                raise ValueError(f"{match}: line {number + 1} must name a depth and a colour image")
            pairs.append((_inside(folder, parts[0]), _inside(folder, parts[1])))
    else:
        depths, colours = _images(folder / "depth"), _images(colour_dir)
        if len(depths) != len(colours):
            raise ValueError(f"{folder}: {len(depths)} depth images but {len(colours)} colour "
                             "images; add rgbd.match to pair them")
        pairs = list(zip(depths, colours))
    if not pairs:
        raise FileNotFoundError(f"{folder}: no depth images")
    poses = None
    source = "identity (no trajectory.log)"
    trajectory = folder / "trajectory.log"
    if trajectory.is_file():
        poses = read_trajectory_log(trajectory)
        if len(poses) != len(pairs):
            raise ValueError(f"{trajectory} has {len(poses)} poses for {len(pairs)} frames")
        source = "trajectory.log"
    found = _intrinsics_file(folder)
    return pairs, None, poses, source, found, {}


def _listing(path: Path):
    entries = []
    for number, line in enumerate(path.read_text().splitlines()):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            raise ValueError(f"{path}: line {number + 1} must be 'timestamp filename'")
        entries.append((float(parts[0]), parts[1]))
    if not entries:
        raise FileNotFoundError(f"{path} lists no images")
    return entries


def _quaternion_matrix(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def _slerp(a, b, fraction):
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    dot = float(np.dot(a, b))
    if dot < 0:
        b, dot = -b, -dot
    if dot > 0.9995:
        result = a + fraction * (b - a)
        return result / np.linalg.norm(result)
    angle = math.acos(dot)
    return (math.sin((1 - fraction) * angle) * a + math.sin(fraction * angle) * b) / math.sin(angle)


def _tum_pose(times, rows, when, max_gap):
    """Interpolate the ground-truth pose at a time, or None outside its coverage."""
    index = int(np.searchsorted(times, when))
    if index == 0 or index == len(times):
        if index < len(times) and abs(times[index] - when) <= 1e-9:
            index += 1
        else:
            return None
    before, after = times[index - 1], times[index]
    if after - before > max_gap:
        return None
    fraction = 0.0 if after == before else (when - before) / (after - before)
    translation = rows[index - 1, :3] + fraction * (rows[index, :3] - rows[index - 1, :3])
    pose = np.eye(4)
    pose[:3, :3] = _quaternion_matrix(_slerp(rows[index - 1, 3:], rows[index, 3:], fraction))
    pose[:3, 3] = translation
    return pose


def _tum(folder: Path, max_time_difference: float):
    depth = _listing(folder / "depth.txt")
    colour = _listing(folder / "rgb.txt")
    colour_times = np.array([stamp for stamp, _ in colour])
    truth = folder / "groundtruth.txt"
    rows = times = None
    if truth.is_file():
        values = [line.split() for line in truth.read_text().splitlines()
                  if line.strip() and not line.startswith("#")]
        try:
            table = np.array([[float(value) for value in row[:8]] for row in values])
        except ValueError as error:
            raise ValueError(f"{truth} is not 'timestamp tx ty tz qx qy qz qw' rows") from error
        if table.ndim != 2 or table.shape[1] != 8:
            raise ValueError(f"{truth} is not 'timestamp tx ty tz qx qy qz qw' rows")
        order = np.argsort(table[:, 0], kind="stable")
        times, rows = table[order, 0], table[order, 1:]
    pairs, stamps, poses = [], [], []
    unmatched = no_pose = 0
    for stamp, name in depth:
        nearest = int(np.argmin(np.abs(colour_times - stamp)))
        if abs(colour_times[nearest] - stamp) > max_time_difference:
            unmatched += 1
            continue
        pose = None
        if rows is not None:
            pose = _tum_pose(times, rows, stamp, max(10 * max_time_difference, 0.1))
            if pose is None:
                no_pose += 1
                continue
        pairs.append((_inside(folder, name), _inside(folder, colour[nearest][1])))
        stamps.append(stamp)
        poses.append(pose)
    if not pairs:
        raise ValueError(f"{folder}: no depth image has a colour image within "
                         f"{max_time_difference:g} s" + (" and a ground-truth pose" if rows is not None else ""))
    source = "groundtruth.txt (interpolated)" if rows is not None else "identity (no groundtruth.txt)"
    found = None
    match = re.search(r"freiburg([123])", folder.resolve().name)
    if match:
        name = f"freiburg{match[1]}"
        found = TUM_INTRINSICS[name], (0, 0), f"published TUM {name} intrinsics"
    skipped = {"depth_without_colour": unmatched, "depth_without_pose": no_pose}
    return pairs, stamps, None if rows is None else poses, source, found, skipped


def _select(count, frames):
    if frames is None:
        return list(range(count))
    if isinstance(frames, (str, bytes)) or not hasattr(frames, "__iter__"):
        raise TypeError("frames must be None, a range or a sequence of frame indices")
    selected = list(frames)
    if any(isinstance(value, bool) or not isinstance(value, (int, np.integer)) for value in selected):
        raise TypeError("frames must contain integer frame indices")
    selected = [int(value) for value in selected]
    if not selected:
        raise ValueError("frames selects no frames")
    if any(a >= b for a, b in zip(selected, selected[1:])):
        raise ValueError("frames must be strictly increasing")
    if selected[0] < 0 or selected[-1] >= count:
        raise IndexError(f"frames must lie in 0..{count - 1}")
    return selected


def load_sequence(folder, *, layout=None, intrinsics=None, depth_scale=None, frames=None,
                  static=False, max_time_difference=0.02) -> RGBDCapture:
    """Read a recorded single-camera RGB-D sequence (Redwood or TUM layout).

    layout is "redwood" or "tum" (detected when None). intrinsics are
    (fx, fy, cx, cy), or a TUM camera name such as "freiburg1"; by default
    they come from the Redwood intrinsics JSON or a TUM folder name.
    depth_scale is depth units per metre (Redwood 1000, TUM 5000 by default).
    frames selects associated frame indices (a range or increasing sequence).

    With static=False each image becomes one timestamp of a moving camera:
    depth (F, 1, H, W) and camera_poses (F, 1, 4, 4). static=True treats the
    sequence as views of a still scene and fuses them into one timestamp:
    depth (1, F, H, W) with one pose per view, which needs recorded poses.
    TUM timestamps are kept (seconds from the first selected frame); Redwood
    has none, so reconstruct spaces frames by fps. Colour must already be
    registered to depth.
    """
    folder = Path(folder).expanduser().resolve()
    if not folder.is_dir():
        raise FileNotFoundError(f"sequence folder not found: {folder}")
    layout = _detect(folder) if layout is None else layout
    if layout not in LAYOUTS:
        raise ValueError(f"layout must be one of {', '.join(LAYOUTS)}")
    if not isinstance(static, bool):
        raise TypeError("static must be bool")
    if layout == "redwood":
        pairs, stamps, poses, pose_source, found, skipped = _redwood(folder)
    else:
        pairs, stamps, poses, pose_source, found, skipped = _tum(folder, float(max_time_difference))
    selected = _select(len(pairs), frames)
    if isinstance(intrinsics, str):
        if intrinsics not in TUM_INTRINSICS:
            raise ValueError(f"unknown intrinsics {intrinsics!r}; choose from {', '.join(TUM_INTRINSICS)}")
        found, intrinsics = (TUM_INTRINSICS[intrinsics], (0, 0), f"published TUM {intrinsics} intrinsics"), None
    if intrinsics is not None:
        values = np.asarray(intrinsics, dtype=np.float64)
        if values.shape != (4,) or not np.isfinite(values).all() or np.any(values[:2] <= 0):
            raise ValueError("intrinsics must be (fx, fy, cx, cy) with positive focal lengths")
        intrinsic_source = "argument"
    elif found is not None:
        values, intrinsic_source = np.asarray(found[0], dtype=np.float64), found[2]
    else:
        raise ValueError(f"{folder}: no camera intrinsics found; pass intrinsics=(fx, fy, cx, cy)")
    scale = DEPTH_SCALES[layout] if depth_scale is None else depth_scale
    if isinstance(scale, bool) or not isinstance(scale, (int, float)) \
            or not math.isfinite(scale) or scale <= 0:
        raise ValueError("depth_scale must be a positive number")
    if static and poses is None:
        raise ValueError(f"static=True needs recorded poses; {folder} has {pose_source}")

    depth_images, colour_images = [], []
    for index in selected:
        depth_path, colour_path = pairs[index]
        depth = _imread(depth_path, depth=True)
        colour = _imread(colour_path, depth=False)
        if colour.shape[:2] != depth.shape:
            raise ValueError(f"{colour_path.name} is {colour.shape[1]}x{colour.shape[0]} but "
                             f"{depth_path.name} is {depth.shape[1]}x{depth.shape[0]}; colour "
                             "must be registered to depth")
        if depth_images and depth.shape != depth_images[0].shape:
            raise ValueError(f"{depth_path.name} changes the image size")
        depth_images.append(depth)
        colour_images.append(colour)
    depth = np.stack(depth_images)
    color = np.stack(colour_images)
    chosen = np.stack([np.eye(4) if poses is None else poses[index] for index in selected])
    timestamps = None
    if stamps is not None and not static:
        times = [stamps[index] for index in selected]
        timestamps = tuple(stamp - times[0] for stamp in times)
    provenance = {
        "loader": "sequence", "layout": layout, "folder": str(folder),
        "pose_source": pose_source, "intrinsics_source": intrinsic_source,
        "static": static, "skipped": skipped,
        "images": [[pairs[index][0].relative_to(folder).as_posix(),
                    pairs[index][1].relative_to(folder).as_posix()] for index in selected],
    }
    if static:
        return RGBDCapture(depth[None], color[None], np.broadcast_to(values, (len(selected), 4)).copy(),
                           chosen, None, (selected[0],), None,
                           tuple(f"view{index}" for index in selected),
                           depth_scale=float(scale), provenance=provenance)
    return RGBDCapture(depth[:, None], color[:, None], values[None], chosen[:, None], timestamps,
                       tuple(selected), None, ("camera",), depth_scale=float(scale),
                       provenance=provenance)
