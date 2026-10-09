"""Load saved two-camera RGB-D captures for open4d.reconstruct.

A capture folder holds ``pair_<012d>/`` directories in one of two layouts:

- replay: ``metadata.json`` whose ``payloads`` name a zstd depth file
  (640x576 little-endian uint16 millimetres) and an MJPEG colour file
  (1280x720) for each camera serial, as written by the capture host and read
  by ``tools/replay_obp1_sender.py``;
- raw: ``ey_depth_u16le.raw``, ``j3_depth_u16le.raw``, ``ey_color.jpg`` and
  ``j3_color.jpg``, with ``metadata.json`` in the pair directory or in a
  ``pair_<012d>/`` directory under a separate ``metadata`` folder.

Depth is rectified and colour is aligned to each depth camera exactly as the
live receiver (``python/live_two_camera_fusion.py``) does it, using the packaged
copy of its projection code in ``_receiver``.
"""

from __future__ import annotations

import importlib
import json
import re
from dataclasses import dataclass, field
from functools import cache
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

from . import _receiver as rx

_PAIR = re.compile(r"pair_(\d{12})")
_RAW_FILES = ("ey_depth_u16le.raw", "j3_depth_u16le.raw", "ey_color.jpg", "j3_color.jpg")
_FACTORY = Path("source/work/calibration_stepwise/factory")
_TRANSFORM = Path("final_validated_fusion/j3_depth_to_ey_depth_refined.txt")


@dataclass(frozen=True, eq=False)
class RGBDCapture:
    """Rectified two-camera RGB-D frames, ready for ``open4d.reconstruct``.

    Depth is (F, 2, H, W) uint16 millimetres and color (F, 2, H, W, 3) uint8
    RGB aligned to each depth camera. Intrinsics are (2, 4) (fx, fy, cx, cy) of
    the rectified depth cameras, and camera_poses (2, 4, 4) camera-to-world in
    metres with camera 1's depth frame as the world. Timestamps are seconds
    from the first selected pair, or None when the capture has no metadata.
    """

    depth: NDArray = field(repr=False)
    color: NDArray = field(repr=False)
    intrinsics: NDArray
    camera_poses: NDArray
    timestamps: tuple[float, ...] | None
    pair_numbers: tuple[int, ...]
    sync_error_us: tuple[int, ...] | None
    serials: tuple[str, str]

    def __len__(self) -> int:
        return len(self.pair_numbers)


@cache
def _dependencies():
    names = ("cv2", "zstandard")
    try:
        for name in names:
            importlib.import_module(name)
    except ModuleNotFoundError as error:
        if error.name not in names:
            raise
        raise ModuleNotFoundError(
            f"Loading RGB-D captures needs {error.name}. Install open4d[capture].",
            name=error.name,
        ) from error


def _rigid(matrix, path):
    rotation = matrix[:3, :3]
    if not np.allclose(matrix[3], [0, 0, 0, 1]) or not np.allclose(
            rotation @ rotation.T, np.eye(3), atol=1e-3) or np.linalg.det(rotation) <= 0:
        raise ValueError(f"{path} is not a rigid transform")
    # Text calibration carries a few digits; snap to the nearest rotation.
    u, _, vt = np.linalg.svd(rotation)
    matrix = matrix.copy()
    matrix[:3, :3] = u @ vt
    matrix[3] = (0, 0, 0, 1)
    return matrix


def _pairs(folder):
    numbers = {}
    for path in folder.iterdir():
        match = _PAIR.fullmatch(path.name)
        if match and path.is_dir():
            numbers[int(match[1])] = path
    return numbers


def _select(numbers, frames, folder):
    if frames is None:
        return sorted(numbers)
    if isinstance(frames, (str, bytes)) or not hasattr(frames, "__iter__"):
        raise TypeError("frames must be None, a range or a sequence of pair numbers")
    selected = list(frames)
    if any(isinstance(value, bool) or not isinstance(value, (int, np.integer))
           for value in selected):
        raise TypeError("frames must contain integer pair numbers")
    selected = [int(value) for value in selected]
    if not selected:
        raise ValueError("frames selects no pairs")
    if any(a >= b for a, b in zip(selected, selected[1:])):
        raise ValueError("frames must be strictly increasing pair numbers")
    missing = [value for value in selected if value not in numbers]
    if missing:
        shown = ", ".join(map(str, missing[:5])) + (" ..." if len(missing) > 5 else "")
        raise FileNotFoundError(
            f"no pair_<012d> directory in {folder} for pair number(s) {shown}; "
            f"available: {min(numbers)}..{max(numbers)}")
    return selected


def _inside(pair_dir, name):
    path = (pair_dir / name).resolve()
    if not path.is_relative_to(pair_dir):
        raise ValueError(f"payload path leaves the capture pair {pair_dir}: {name}")
    if not path.is_file():
        raise FileNotFoundError(f"{pair_dir.name}: payload {name} is missing")
    return path


def _read(path, limit):
    with path.open("rb") as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path} exceeds {limit} bytes")
    return data


def _replay_images(pair_dir, metadata, serials):
    pair_dir = pair_dir.resolve()
    payloads = metadata["payloads"]
    if not isinstance(payloads, list) or not all(isinstance(entry, dict) for entry in payloads):
        raise ValueError(f"{pair_dir.name}: metadata payloads must be a list of objects")
    images = {}
    for entry in payloads:
        key = (entry.get("serial"), entry.get("stream_type"))
        if key[0] not in serials:
            continue
        if key in images:
            raise ValueError(f"{pair_dir.name}: duplicate payload for {key[0]}")
        if not isinstance(entry.get("file"), str):
            raise ValueError(f"{pair_dir.name}: payload for {key[0]} has no file")
        path = _inside(pair_dir, entry["file"])
        data = _read(path, rx.MAX_SINGLE_PAYLOAD)
        if "compressed_length" in entry and len(data) != entry["compressed_length"]:
            raise ValueError(f"{pair_dir.name}: {entry['file']} is {len(data)} bytes, "
                             f"metadata says {entry['compressed_length']}")
        if key[1] == rx.STREAM_DEPTH:
            if (entry.get("codec") != rx.CODEC_ZSTD
                    or entry.get("format") != rx.FORMAT_DEPTH16_LE
                    or (entry.get("width"), entry.get("height")) != (rx.WIDTH, rx.HEIGHT)
                    or entry.get("raw_length", rx.DEPTH_BYTES) != rx.DEPTH_BYTES):
                raise ValueError(f"{pair_dir.name}: {entry['file']} is not zstd 16-bit "
                                 f"{rx.WIDTH}x{rx.HEIGHT} depth")
            images[key] = _depth(_zstd(data, pair_dir, entry["file"]), path)
        elif key[1] == rx.STREAM_COLOR:
            if (entry.get("codec") != rx.CODEC_MJPEG
                    or (entry.get("width"), entry.get("height")) != (1280, 720)):
                raise ValueError(f"{pair_dir.name}: {entry['file']} is not 1280x720 MJPEG colour")
            images[key] = _color(data, path)
        else:
            raise ValueError(f"{pair_dir.name}: unknown stream_type {key[1]!r}")
    missing = [f"{serial} {kind}" for serial in serials
               for kind, stream in (("depth", rx.STREAM_DEPTH),
                                    ("colour", rx.STREAM_COLOR))
               if (serial, stream) not in images]
    if missing:
        found = sorted({str(entry.get("serial")) for entry in payloads})
        raise ValueError(f"{pair_dir.name}: no {', '.join(missing)} payload; metadata has "
                         f"serials {', '.join(found)}; pass serials=(camera1, camera2)")
    return [(images[(serial, rx.STREAM_DEPTH)], images[(serial, rx.STREAM_COLOR)])
            for serial in serials]


def _zstd(data, pair_dir, name):
    zstandard = importlib.import_module("zstandard")
    size = zstandard.frame_content_size(data)
    if size not in (zstandard.CONTENTSIZE_UNKNOWN, rx.DEPTH_BYTES):
        raise ValueError(f"{pair_dir.name}: {name} decompresses to {size} bytes, "
                         f"expected {rx.DEPTH_BYTES}")
    try:
        return zstandard.ZstdDecompressor().decompress(
            data, max_output_size=rx.DEPTH_BYTES, allow_extra_data=False)
    except zstandard.ZstdError as error:
        raise ValueError(f"{pair_dir.name}: {name} is not valid zstd depth: {error}") from error


def _depth(data, path):
    if len(data) != rx.DEPTH_BYTES:
        raise ValueError(f"{path} holds {len(data)} depth bytes, expected "
                         f"{rx.DEPTH_BYTES} ({rx.WIDTH}x{rx.HEIGHT} uint16)")
    return np.frombuffer(data, dtype="<u2").reshape(rx.HEIGHT, rx.WIDTH).copy()


def _color(data, path):
    try:
        return rx.CameraProjector.decode_color(data)
    except RuntimeError as error:
        raise ValueError(f"{path} is not a 1280x720 JPEG") from error


def _raw_images(pair_dir):
    missing = [name for name in _RAW_FILES if not (pair_dir / name).is_file()]
    if missing:
        raise FileNotFoundError(f"{pair_dir}: missing {', '.join(missing)}")
    return [(_depth(_read(pair_dir / f"{prefix}_depth_u16le.raw", rx.DEPTH_BYTES),
                    pair_dir / f"{prefix}_depth_u16le.raw"),
             _color(_read(pair_dir / f"{prefix}_color.jpg", 1 << 26),
                    pair_dir / f"{prefix}_color.jpg"))
            for prefix in ("ey", "j3")]


def _metadata(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"{path} is not valid JSON: {error}") from error


def _timestamp(metadata, serial):
    if "ey_timestamp_us" in metadata:
        return int(metadata["ey_timestamp_us"])
    for entry in metadata.get("payloads", ()):
        if (entry.get("serial"), entry.get("stream_type")) == (serial, rx.STREAM_DEPTH):
            return int(entry["device_timestamp_us"])
    return None


def load_capture(pairs, calibration, *, frames=None, metadata=None,
                 serials=None) -> RGBDCapture:
    """Read saved two-camera pairs and rectify them with the rig calibration.

    pairs is a folder of pair_<012d>/ directories in the replay or raw layout;
    calibration is the folder with source/work/calibration_stepwise/factory/
    {ey,j3}_factory_calibration.json and final_validated_fusion/
    j3_depth_to_ey_depth_refined.txt. frames selects pair numbers (a range or
    increasing sequence; default all). metadata is an optional folder of
    pair_<012d>/metadata.json for raw pairs. serials are the (camera 1, camera
    2) serials of replay payloads, defaulting to FOURD_CAMERA1_SERIAL and
    FOURD_CAMERA2_SERIAL as in the live receiver.

    Timestamps come from ey_timestamp_us (else camera 1's depth
    device_timestamp_us). If no pair has metadata, timestamps and sync_error_us
    are None and reconstruct spaces frames by its fps. All frames are held in
    memory: about 3.7 MB per pair.
    """
    pairs = Path(pairs).expanduser().resolve()
    calibration = Path(calibration).expanduser().resolve()
    metadata = None if metadata is None else Path(metadata).expanduser().resolve()
    if not pairs.is_dir():
        raise FileNotFoundError(f"capture folder not found: {pairs}")
    if metadata is not None and not metadata.is_dir():
        raise FileNotFoundError(f"metadata folder not found: {metadata}")
    files = {
        "camera 1 factory calibration": calibration / _FACTORY / "ey_factory_calibration.json",
        "camera 2 factory calibration": calibration / _FACTORY / "j3_factory_calibration.json",
        "camera 2 to camera 1 transform": calibration / _TRANSFORM,
    }
    missing = [f"  {label}: {path}" for label, path in files.items() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing calibration files:\n" + "\n".join(missing)
                                + "\nSee the RGB-D README for the calibration layout.")
    numbers = _pairs(pairs)
    if not numbers:
        raise FileNotFoundError(f"no pair_<012d> directories in {pairs}")
    selected = _select(numbers, frames, pairs)

    _dependencies()
    if serials is None:
        serials = rx.default_serials()
    serials = tuple(serials)
    if len(serials) != 2 or not all(isinstance(value, str) for value in serials) \
            or serials[0] == serials[1]:
        raise ValueError("serials must be two different camera serial strings")
    projectors = [rx.CameraProjector(files["camera 1 factory calibration"]),
                  rx.CameraProjector(files["camera 2 factory calibration"])]
    try:
        j3_to_ey = rx.load_transform(files["camera 2 to camera 1 transform"])
    except RuntimeError as error:
        raise ValueError(f"{files['camera 2 to camera 1 transform']}: {error}") from error
    # The live receiver integrates camera 2 with extrinsic inv(j3_to_ey), so
    # j3_to_ey is camera 2's camera-to-world pose in camera 1's depth frame.
    poses = np.stack([np.eye(4), _rigid(j3_to_ey, files["camera 2 to camera 1 transform"])])

    depth = np.empty((len(selected), 2, rx.HEIGHT, rx.WIDTH), dtype=np.uint16)
    color = np.empty((*depth.shape, 3), dtype=np.uint8)
    records = []
    for index, number in enumerate(selected):
        pair_dir = numbers[number]
        name = pair_dir.name
        own = pair_dir / "metadata.json"
        info = _metadata(own) if own.is_file() else None
        if info is not None and "payloads" in info:
            images = _replay_images(pair_dir, info, serials)
        else:
            images = _raw_images(pair_dir)
            if metadata is not None:
                path = metadata / name / "metadata.json"
                if not path.is_file():
                    raise FileNotFoundError(f"no metadata for {name}: {path}")
                info = _metadata(path)
        if info is not None and int(info.get("pair_number", number)) != number:
            raise ValueError(f"{name}: metadata pair_number is {info['pair_number']}")
        records.append(info)
        for camera, (projector, (raw_depth, bgr)) in enumerate(zip(projectors, images)):
            depth[index, camera], color[index, camera] = projector.prepare_from_bgr(raw_depth, bgr)

    timestamps = sync = None
    if any(info is not None for info in records):
        without = [selected[i] for i, info in enumerate(records) if info is None]
        if without:
            raise FileNotFoundError(f"pairs {without} have no metadata.json while others do")
        stamps = [_timestamp(info, serials[0]) for info in records]
        if None in stamps:
            raise ValueError(f"pair {selected[stamps.index(None)]} metadata has no "
                             "ey_timestamp_us or camera 1 depth device_timestamp_us")
        steps = np.diff(stamps)
        if np.any(steps <= 0):
            bad = selected[int(np.argmax(steps <= 0)) + 1]
            raise ValueError(f"timestamps do not increase at pair {bad}; select a "
                             "monotonic run with frames=")
        timestamps = tuple((stamp - stamps[0]) / 1e6 for stamp in stamps)
        if all("sync_error_us" in info for info in records):
            sync = tuple(int(info["sync_error_us"]) for info in records)

    intrinsics = np.array([[k[0, 0], k[1, 1], k[0, 2], k[1, 2]]
                           for k in (projector.depth_k for projector in projectors)])
    return RGBDCapture(depth, color, intrinsics, poses, timestamps, tuple(selected),
                       sync, serials)
