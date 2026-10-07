"""Read ORBIT multi-view captures and prepare them for Gaussian reconstruction.

An ORBIT corpus is a folder with ``dataset.json`` and one folder per object. An
object has ``transforms.json``, whose entries give each image's ``frame_index``,
``view_id``, ``source_frame`` and ``camera_to_world_opencv`` pose, and images at
``frame_NNNNNN/images/view_KK.png``. Cameras use OpenCV axes: x right, y down,
z forward, with translation in scene units.
"""

from __future__ import annotations

import json
import math
import shutil
import warnings
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

#: Images wider than this are downscaled; 3DGS applies the same limit by default.
DEFAULT_MAX_WIDTH = 1600
#: Silhouette threshold on 8-bit intensity for the black-background renders.
_SILHOUETTE = 12
#: Initial points written for the first frame.
_POINTS = 100_000


@dataclass(frozen=True, eq=False)
class OrbitCamera:
    """One fixed camera of the capture rig, in pixels and scene units."""

    view_id: int
    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    camera_to_world: NDArray = field(repr=False)

    @property
    def center(self) -> NDArray:
        return self.camera_to_world[:3, 3]


@dataclass(frozen=True, eq=False)
class OrbitScene:
    """One ORBIT object: a fixed rig of cameras and a contiguous run of frames."""

    name: str
    path: Path
    cameras: tuple[OrbitCamera, ...]
    source_frames: tuple[int, ...]
    bounds_min: NDArray = field(repr=False)
    bounds_max: NDArray = field(repr=False)
    background: str | None = None
    _images: tuple[tuple[Path, ...], ...] = field(default=(), repr=False)

    def __len__(self) -> int:
        return len(self.source_frames)

    def image_path(self, frame: int, view: int) -> Path:
        """The PNG for a frame position and a camera position in ``cameras``."""
        return self._images[frame][view]

    def image(self, frame: int, view: int) -> NDArray:
        """Read one image as (H, W, 3) uint8."""
        return np.asarray(_open_image(self.image_path(frame, view)).convert("RGB"))

    def select(self, frames: int | slice | range) -> OrbitScene:
        """A scene restricted to some frames, for example ``scene.select(10)``."""
        if isinstance(frames, bool) or not isinstance(frames, (int, slice, range)):
            raise TypeError("frames must be a count, slice or range")
        if isinstance(frames, int):
            if frames < 1:
                raise ValueError("frames must be positive")
            frames = slice(0, frames)
        positions = range(len(self))[frames] if isinstance(frames, slice) else frames
        positions = list(positions)
        if not positions or any(p not in range(len(self)) for p in positions):
            raise ValueError(f"frames must select existing frames of {len(self)}")
        if any(b - a != 1 for a, b in zip(positions, positions[1:])):
            raise ValueError("frames must be contiguous and increasing")
        return OrbitScene(self.name, self.path, self.cameras,
                          tuple(self.source_frames[p] for p in positions),
                          self.bounds_min, self.bounds_max, self.background,
                          tuple(self._images[p] for p in positions))


def _open_image(path: Path):
    try:
        from PIL import Image
    except ModuleNotFoundError as exc:
        if exc.name != "PIL":
            raise
        raise ImportError("Reading ORBIT images requires pip install 'open4d[gaussians]'") from exc
    return Image.open(path)


def is_orbit(path: str | Path) -> bool:
    """Whether a folder is an ORBIT corpus or object."""
    path = Path(path)
    if (path / "dataset.json").is_file():
        try:
            meta = json.loads((path / "dataset.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        return isinstance(meta, dict) and str(meta.get("format", "")).startswith("orbit-")
    if not (path / "transforms.json").is_file():
        return False
    try:
        meta = json.loads((path / "transforms.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    entries = meta.get("frames") if isinstance(meta, dict) else None
    return bool(entries) and isinstance(entries[0], dict) and "camera_to_world_opencv" in entries[0]


def objects(path: str | Path) -> tuple[str, ...]:
    """Object names listed by a corpus's ``dataset.json``."""
    meta = _corpus(Path(path))
    return tuple(entry["name"] for entry in meta["objects"])


def _corpus(root: Path) -> dict:
    meta = json.loads((root / "dataset.json").read_text(encoding="utf-8"))
    if not isinstance(meta, dict) or not str(meta.get("format", "")).startswith("orbit-"):
        raise ValueError(f"{root} is not an ORBIT corpus")
    entries = meta.get("objects")
    if not isinstance(entries, list) or not all(
            isinstance(e, dict) and isinstance(e.get("name"), str) and isinstance(e.get("path"), str)
            for e in entries):
        raise ValueError("ORBIT dataset.json must list objects with a name and path")
    return meta


def _inside(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if root.resolve() not in path.parents:
        raise ValueError(f"ORBIT path leaves its folder: {relative}")
    return path


def load_orbit(path: str | Path, name: str | None = None) -> OrbitScene:
    """Read an ORBIT object folder, or object ``name`` of a corpus folder."""
    path = Path(path).expanduser().resolve()
    background = None
    if (path / "dataset.json").is_file():
        meta = _corpus(path)
        names = [entry["name"] for entry in meta["objects"]]
        if name is None:
            raise ValueError(f"{path} is an ORBIT corpus; pass one of: {', '.join(names)}")
        if name not in names:
            raise KeyError(f"no ORBIT object {name!r}; choose from: {', '.join(names)}")
        transforms = _inside(path, meta["objects"][names.index(name)]["path"])
        background = meta.get("background")
        folder = transforms.parent
    else:
        if name is not None:
            raise ValueError("name selects an object of a corpus; this is an object folder")
        transforms, folder = path / "transforms.json", path
        parent = path.parent / "dataset.json"
        if parent.is_file():
            try:
                background = _corpus(path.parent).get("background")
            except ValueError:
                pass
    if not transforms.is_file():
        raise FileNotFoundError(f"No ORBIT transforms.json in {folder}")
    return _scene(folder, json.loads(transforms.read_text(encoding="utf-8")), name or folder.name,
                  background)


def _scene(folder: Path, meta: dict, name: str, background: str | None) -> OrbitScene:
    try:
        width, height = int(meta["w"]), int(meta["h"])
        fx, fy, cx, cy = (float(meta[key]) for key in ("fl_x", "fl_y", "cx", "cy"))
        bounds_min = np.asarray(meta["bounds_min"], dtype=np.float64)
        bounds_max = np.asarray(meta["bounds_max"], dtype=np.float64)
        entries = meta["frames"]
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"ORBIT transforms.json in {folder} is missing camera fields") from error
    if any(abs(float(meta.get(key, 0.0))) > 1e-9 for key in ("k1", "k2", "k3", "p1", "p2")):
        raise ValueError("ORBIT images must be undistorted; this capture has lens distortion")
    if (width < 1 or height < 1 or min(fx, fy) <= 0 or bounds_min.shape != (3,)
            or bounds_max.shape != (3,) or np.any(bounds_max <= bounds_min)):
        raise ValueError("ORBIT intrinsics or bounds are invalid")
    frames: dict[int, dict[int, dict]] = {}
    for entry in entries:
        try:
            frames.setdefault(int(entry["frame_index"]), {})[int(entry["view_id"])] = entry
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("ORBIT frame entries need frame_index, view_id and a pose") from error
    order = sorted(frames)
    if not order or order != list(range(order[0], order[0] + len(order))):
        raise ValueError("ORBIT frames must be contiguous")
    views = sorted(frames[order[0]])
    if any(sorted(frames[index]) != views for index in order):
        raise ValueError("every ORBIT frame must have the same views")
    cameras = []
    for view in views:
        pose = np.asarray(frames[order[0]][view]["camera_to_world_opencv"], dtype=np.float64)
        rotation = pose[:3, :3] if pose.shape == (4, 4) else None
        if (rotation is None or not np.isfinite(pose).all() or not np.allclose(pose[3], [0, 0, 0, 1])
                or not np.allclose(rotation @ rotation.T, np.eye(3), atol=1e-5)
                or not np.isclose(np.linalg.det(rotation), 1, atol=1e-5)):
            raise ValueError(f"ORBIT view {view} has no rigid camera_to_world_opencv pose")
        if any(not np.allclose(np.asarray(frames[index][view]["camera_to_world_opencv"]), pose,
                               atol=1e-6) for index in order):
            raise ValueError("ORBIT cameras must stay fixed; this capture moves them")
        cameras.append(OrbitCamera(view, width, height, fx, fy, cx, cy, pose))
    images = tuple(tuple(_inside(folder, frames[index][view]["file_path"]) for view in views)
                   for index in order)
    source = tuple(int(frames[index][views[0]].get("source_frame", index)) for index in order)
    return OrbitScene(name, folder.resolve(), tuple(cameras), source, bounds_min, bounds_max,
                      background, images)


# ---------------------------------------------------------------- preparation


def _scaled(scene: OrbitScene, max_width: int) -> tuple[int, int, float]:
    camera = scene.cameras[0]
    scale = min(1.0, max_width / camera.width)
    return max(1, round(camera.width * scale)), max(1, round(camera.height * scale)), scale


def _held_out(scene: OrbitScene, test_views) -> tuple[int, ...]:
    """Normalise held-out view ids; ``None`` means the first camera."""
    if test_views is None:
        return (scene.cameras[0].view_id,)
    if isinstance(test_views, (str, bytes)) or not hasattr(test_views, "__iter__"):
        raise TypeError("test_views must be a sequence of view ids")
    held = tuple(test_views)
    views = [camera.view_id for camera in scene.cameras]
    if any(isinstance(v, bool) or v not in views for v in held) or len(set(held)) != len(held):
        raise ValueError(f"test_views must be distinct views of the rig: {views}")
    if len(views) - len(held) < 2:
        raise ValueError("at least two views must remain for training")
    return held


def check(scene: OrbitScene, method: str, *, max_width: int = DEFAULT_MAX_WIDTH,
          test_views=None, initial_points: str = "bounds") -> None:
    """Reject ORBIT input that QUEEN or 3DGStream cannot represent."""
    if method not in ("queen", "3dgstream"):
        raise ValueError("ORBIT preparation supports method 'queen' or '3dgstream'")
    if initial_points not in ("bounds", "carve"):
        raise ValueError("initial_points must be 'bounds' or 'carve'")
    if initial_points == "carve" and scene.background != "black":
        raise ValueError("carving needs silhouettes; this corpus does not declare a black background")
    if isinstance(max_width, bool) or not isinstance(max_width, int) or max_width < 16:
        raise ValueError("max_width must be an integer of at least 16 pixels")
    _held_out(scene, test_views)
    if len(scene.cameras) < 3:
        raise ValueError("ORBIT reconstruction needs at least three views")
    if len(scene) < 2:
        raise ValueError("ORBIT reconstruction needs at least two frames")
    for camera in scene.cameras:
        # Both methods read one field of view per axis around the image centre.
        if (not math.isclose(camera.fx, camera.fy, rel_tol=1e-6)
                or abs(camera.cx - (camera.width - 1) / 2) > 0.5
                or abs(camera.cy - (camera.height - 1) / 2) > 0.5):
            raise ValueError("QUEEN and 3DGStream need square pixels and a centred principal point")
        if (camera.width, camera.height) != (scene.cameras[0].width, scene.cameras[0].height):
            raise ValueError("ORBIT views must share one image size")


def prepare(scene: OrbitScene, destination: str | Path, *, method: str,
            max_width: int = DEFAULT_MAX_WIDTH, test_views=None,
            initial_points: str = "bounds", seed: int = 0) -> Path:
    """Write an ORBIT scene in the input layout QUEEN or 3DGStream reads.

    ``test_views`` are held out for evaluation: the first camera by default,
    any distinct view ids, or none (``()``) to train on every view. QUEEN gets
    its DyNeRF layout (``camKK/images``, ``poses_bounds.npy``) with the held-out
    views first, as ``cam00`` onwards; pass their count to QUEEN's
    ``test_indices``. 3DGStream gets one Blender-style folder per frame, with
    the held-out views in ``transforms_test.json``. Its upstream needs a test
    split, so with none held out that file repeats one training view, whose
    scores are then not held out. Images wider than ``max_width`` are downscaled.
    Initial points fill the object's bounds. ``initial_points="carve"`` instead
    samples the first frame's visual hull from black-background silhouettes,
    leaving out the held-out view: on ORBIT it reconstructs the object itself
    more accurately, but the hull's excess volume leaves haze in the background
    and many more Gaussians.
    """
    check(scene, method, max_width=max_width, test_views=test_views, initial_points=initial_points)
    held = _held_out(scene, test_views)
    destination = Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    width, height, scale = _scaled(scene, max_width)
    order = sorted(range(len(scene.cameras)), key=lambda i: scene.cameras[i].view_id not in held)
    destination.mkdir(parents=True)
    try:
        _write_layout(scene, destination, method, width, height, scale, order, held,
                      initial_points, seed)
    except BaseException:
        shutil.rmtree(destination, ignore_errors=True)
        raise
    return destination


def _write_layout(scene: OrbitScene, destination: Path, method: str, width: int, height: int,
                  scale: float, order: list[int], held: tuple[int, ...], initial_points: str,
                  seed: int) -> None:
    def resized(frame: int, view: int):
        image = _open_image(scene.image_path(frame, view)).convert("RGB")
        if image.size != (width, height):
            from PIL import Image
            image = image.resize((width, height), Image.LANCZOS)
        return image

    points, colors = _initial_points(scene, np.random.default_rng(seed), held, initial_points)
    focal = scene.cameras[0].fx * width / scene.cameras[0].width
    center = (scene.bounds_min + scene.bounds_max) / 2
    radius = float(np.linalg.norm(scene.bounds_max - scene.bounds_min)) / 2
    if method == "queen":
        rows = []
        for slot, position in enumerate(order):
            camera = scene.cameras[position]
            c2w = camera.camera_to_world
            # LLFF columns are down, right, back; QUEEN converts them to OpenGL.
            llff = np.column_stack([c2w[:3, 1], c2w[:3, 0], -c2w[:3, 2], c2w[:3, 3],
                                    [height, width, focal]])
            distance = float(np.linalg.norm(camera.center - center))
            rows.append(np.concatenate([llff.ravel(),
                                        [max(distance - 2 * radius, 0.01), distance + 2 * radius]]))
            folder = destination / f"cam{slot:02d}" / "images"
            folder.mkdir(parents=True)
            for frame in range(len(scene)):
                resized(frame, position).save(folder / f"{frame:04d}.png")
        np.save(destination / "poses_bounds.npy", np.asarray(rows))
        # QUEEN renders frame t from path camera t; its default spiral assumes
        # forward-facing cameras, so give it an orbit around the object.
        np.save(destination / "render_path.npy", _orbit_path(scene, len(scene)))
        # QUEEN reads points3D_downsample2.ply when "dynerf" appears in the path.
        for relative in ("colmap/dense/workspace/fused.ply", "points3D_downsample2.ply"):
            _write_points(destination / relative, points, colors)
    else:
        angle = 2 * math.atan(width / (2 * focal))
        for frame in range(len(scene)):
            folder = destination / f"frame{frame:06d}"
            (folder / "images").mkdir(parents=True)
            entries = []
            for position in order:
                camera = scene.cameras[position]
                stem = f"view_{camera.view_id:02d}"
                resized(frame, position).save(folder / "images" / f"{stem}.png")
                gl = camera.camera_to_world @ np.diag([1.0, -1.0, -1.0, 1.0])
                entries.append({"file_path": f"images/{stem}", "transform_matrix": gl.tolist()})
            # With nothing held out, upstream still needs a test split.
            test = entries[:len(held)] or entries[:1]
            for split, chosen in (("train", entries[len(held):]), ("test", test)):
                (folder / f"transforms_{split}.json").write_text(json.dumps(
                    {"camera_angle_x": angle, "frames": chosen}, indent=1), encoding="utf-8")
            _write_points(folder / "points3d.ply", points, colors)
    (destination / "orbit.json").write_text(json.dumps({
        "scene": scene.name, "source": str(scene.path), "method": method,
        "source_frames": list(scene.source_frames), "test_views": list(held),
        "views": [scene.cameras[i].view_id for i in order], "size": [width, height],
        "scale": scale, "initial_points": initial_points, "initial_point_count": len(points),
    }, indent=1), encoding="utf-8")


def _orbit_path(scene: OrbitScene, count: int, degrees_per_frame: float = 3.0) -> NDArray:
    """A level orbit at the rig's median radius and height, starting at the first camera.

    Returned as (count, 3, 4) camera-to-world matrices with OpenGL axes, the
    layout QUEEN's renderer reads.
    """
    center = (scene.bounds_min + scene.bounds_max) / 2
    up = -np.mean([camera.camera_to_world[:3, 1] for camera in scene.cameras], axis=0)
    up /= np.linalg.norm(up)
    offsets = np.array([camera.center - center for camera in scene.cameras])
    heights = offsets @ up
    flat = offsets - heights[:, None] * up
    radius, height = float(np.median(np.linalg.norm(flat, axis=1))), float(np.median(heights))
    first = flat[0] / np.linalg.norm(flat[0])
    side = np.cross(up, first)
    poses = []
    for step in range(count):
        angle = math.radians(degrees_per_frame * step)
        eye = center + height * up + radius * (math.cos(angle) * first + math.sin(angle) * side)
        forward = center - eye
        forward /= np.linalg.norm(forward)
        right = np.cross(-up, forward)
        right /= np.linalg.norm(right)
        down = np.cross(forward, right)
        opencv = np.column_stack([right, down, forward, eye])
        poses.append(opencv * [1.0, -1.0, -1.0, 1.0])
    return np.asarray(poses)


def _write_points(path: Path, points: NDArray, colors: NDArray) -> None:
    dtype = [(n, "<f4") for n in ("x", "y", "z", "nx", "ny", "nz")] + \
            [(n, "u1") for n in ("red", "green", "blue")]
    data = np.zeros(len(points), dtype)
    data["x"], data["y"], data["z"] = points.T
    data["red"], data["green"], data["blue"] = colors.T
    path.parent.mkdir(parents=True, exist_ok=True)
    header = ("ply\nformat binary_little_endian 1.0\n"
              f"element vertex {len(data)}\n"
              + "".join(f"property float {n}\n" for n in ("x", "y", "z", "nx", "ny", "nz"))
              + "".join(f"property uchar {n}\n" for n in ("red", "green", "blue"))
              + "end_header\n")
    path.write_bytes(header.encode("ascii") + data.tobytes())


def _initial_points(scene: OrbitScene, rng: np.random.Generator, held: tuple[int, ...],
                    mode: str, count: int = _POINTS) -> tuple[NDArray, NDArray]:
    """Fill the bounds, or carve the first frame's visual hull.

    Held-out views are not carved, so they never shape the model they score.
    """
    lo, hi = scene.bounds_min, scene.bounds_max
    pad = 0.05 * (hi - lo)
    lo, hi = lo - pad, hi + pad
    if mode == "carve":
        carved = _carve(scene, lo, hi, rng, count, held)
        if carved is not None:
            return carved
        warnings.warn(f"silhouette carving of {scene.name} kept too few points; "
                      "initialising from the bounds", RuntimeWarning, stacklevel=3)
    points = rng.uniform(lo, hi, size=(count, 3))
    return points.astype(np.float32), np.full((count, 3), 128, np.uint8)


def _carve(scene: OrbitScene, lo: NDArray, hi: NDArray, rng: np.random.Generator,
           count: int, held: tuple[int, ...]) -> tuple[NDArray, NDArray] | None:
    volume = float(np.prod(hi - lo))
    voxel = (volume / 2_000_000) ** (1 / 3)
    shape = np.maximum(np.ceil((hi - lo) / voxel).astype(int), 1)
    axes = [lo[i] + (np.arange(shape[i]) + 0.5) * voxel for i in range(3)]
    centers = np.stack(np.meshgrid(*axes, indexing="ij"), -1).reshape(-1, 3)
    inside = np.ones(len(centers), bool)
    seen = np.zeros(len(centers), np.int32)
    color = np.zeros((len(centers), 3), np.float64)
    lit = np.zeros(len(centers), np.int32)
    from PIL import Image, ImageFilter
    for position, camera in enumerate(scene.cameras):
        if camera.view_id in held:
            continue
        image = _open_image(scene.image_path(0, position)).convert("RGB")
        factor = max(1, camera.width // 512)
        small = image.resize((camera.width // factor, camera.height // factor), Image.BOX)
        rgb = np.asarray(small)
        foreground = rgb.max(-1) > _SILHOUETTE
        # A one-pixel margin keeps rounding at the silhouette edge from carving the object.
        mask = np.asarray(Image.fromarray((foreground * 255).astype(np.uint8))
                          .filter(ImageFilter.MaxFilter(3))) > 0
        world_to_camera = np.linalg.inv(camera.camera_to_world)
        local = centers @ world_to_camera[:3, :3].T + world_to_camera[:3, 3]
        z = local[:, 2]
        with np.errstate(divide="ignore", invalid="ignore"):
            u = (camera.fx * local[:, 0] / z + camera.cx + 0.5) / factor
            v = (camera.fy * local[:, 1] / z + camera.cy + 0.5) / factor
        visible = (z > 0) & (u >= 0) & (v >= 0) & (u < mask.shape[1]) & (v < mask.shape[0])
        column = np.clip(u[visible].astype(int), 0, mask.shape[1] - 1)
        row = np.clip(v[visible].astype(int), 0, mask.shape[0] - 1)
        hit = np.zeros(len(centers), bool)
        hit[np.flatnonzero(visible)] = mask[row, column]
        inside &= hit | ~visible
        seen += visible
        # Colour only from pixels on the object, not the black margin around it.
        on_object = foreground[row, column]
        index = np.flatnonzero(visible)[on_object]
        color[index] += rgb[row[on_object], column[on_object]]
        lit[index] += 1
    inside &= seen >= 2
    grid = inside.reshape(shape)
    padded = np.pad(grid, 1)
    interior = np.ones_like(grid)
    for axis in range(3):
        for step in (-1, 1):
            interior &= np.roll(padded, step, axis)[1:-1, 1:-1, 1:-1]
    surface = (grid & ~interior).reshape(-1)
    chosen = np.flatnonzero(surface)
    if len(chosen) < 1000:
        return None
    chosen = rng.choice(chosen, size=count, replace=len(chosen) < count)
    points = centers[chosen] + rng.uniform(-voxel / 2, voxel / 2, size=(count, 3))
    colors = np.clip(color[chosen] / np.maximum(lit[chosen, None], 1), 0, 255).astype(np.uint8)
    return points.astype(np.float32), colors
