"""Camera modes: image sizes, crops, colour scaling and depth units.

A mode says how a camera's factory calibration, which is normalized by the
sensor resolution, maps onto the images a capture actually stores. The capture
loader reads it from ``camera_mode.json`` in the calibration folder (or from
``load_capture(camera_mode=...)``) instead of assuming one Orbbec setting.
"""

from __future__ import annotations

import json
import math
from collections.abc import Mapping
from dataclasses import asdict, dataclass
from pathlib import Path

from ._k4a import COLOR_MODES, DEPTH_MODES

#: File in the calibration folder that selects the capture's camera mode.
MODE_FILE = "camera_mode.json"
DEFAULT_MODE = "femto-bolt-nfov-unbinned-720p"


def _size(value, name):
    if (not isinstance(value, (list, tuple)) or len(value) != 2
            or any(isinstance(item, bool) or not isinstance(item, int) or item <= 0
                   for item in value)):
        raise ValueError(f"camera mode {name} must be two positive integers")
    return (int(value[0]), int(value[1]))


def _offset(value, name):
    if (not isinstance(value, (list, tuple)) or len(value) != 2
            or any(isinstance(item, bool) or not isinstance(item, (int, float))
                   or not math.isfinite(item) or item < 0 for item in value)):
        raise ValueError(f"camera mode {name} must be two nonnegative numbers")
    return (float(value[0]), float(value[1]))


@dataclass(frozen=True)
class CameraMode:
    """How stored depth and colour images relate to the factory calibration.

    depth_size and color_size are the stored (width, height). The depth
    calibration is scaled to sensor / depth_binning pixels and then cropped by
    depth_crop, as the Azure Kinect SDK does for its depth modes. Colour is
    scaled to color_binned and cropped by color_crop; when those are None it is
    scaled to the stored width and centre-cropped vertically, which matches the
    SDK's colour resolutions for its 4:3 colour calibration. depth_scale is
    stored depth units per metre (1000 for millimetres).
    """

    name: str
    depth_size: tuple[int, int]
    depth_crop: tuple[float, float]
    color_size: tuple[int, int]
    depth_binning: int = 1
    color_binned: tuple[int, int] | None = None
    color_crop: tuple[float, float] | None = None
    depth_scale: float = 1000.0

    def __post_init__(self):
        if not isinstance(self.name, str) or not self.name:
            raise ValueError("camera mode name must be a nonempty string")
        object.__setattr__(self, "depth_size", _size(self.depth_size, "depth_size"))
        object.__setattr__(self, "color_size", _size(self.color_size, "color_size"))
        object.__setattr__(self, "depth_crop", _offset(self.depth_crop, "depth_crop"))
        if self.depth_binning not in (1, 2) or isinstance(self.depth_binning, bool):
            raise ValueError("camera mode depth_binning must be 1 or 2")
        if (self.color_binned is None) != (self.color_crop is None):
            raise ValueError("camera mode color_binned and color_crop go together")
        if self.color_binned is not None:
            object.__setattr__(self, "color_binned", _size(self.color_binned, "color_binned"))
            object.__setattr__(self, "color_crop", _offset(self.color_crop, "color_crop"))
        scale = self.depth_scale
        if isinstance(scale, bool) or not isinstance(scale, (int, float)) \
                or not math.isfinite(scale) or scale <= 0:
            raise ValueError("camera mode depth_scale must be a positive number")
        object.__setattr__(self, "depth_scale", float(scale))

    @property
    def depth_bytes(self) -> int:
        return self.depth_size[0] * self.depth_size[1] * 2

    def to_dict(self) -> dict:
        return {key: list(value) if isinstance(value, tuple) else value
                for key, value in asdict(self).items()}

    @classmethod
    def from_dict(cls, data: Mapping) -> "CameraMode":
        if not isinstance(data, Mapping):
            raise TypeError("camera mode must be a mapping")
        if "preset" in data:
            extra = set(data) - {"preset", "depth_scale"}
            if extra:
                raise ValueError(f"camera mode preset takes only depth_scale; got {sorted(extra)}")
            mode = preset(data["preset"])
            if "depth_scale" in data:
                mode = CameraMode(**{**mode.to_dict(), "depth_scale": data["depth_scale"]})
            return mode
        known = set(cls.__dataclass_fields__)
        unknown = set(data) - known
        if unknown:
            raise ValueError(f"unknown camera mode field(s): {', '.join(sorted(unknown))}")
        values = {key: tuple(value) if isinstance(value, list) else value
                  for key, value in data.items()}
        return cls(**values)


def k4a_mode(depth: str, color: str, *, depth_scale: float = 1000.0,
             explicit_color: bool = False) -> CameraMode:
    """An Azure Kinect depth mode and colour resolution as a CameraMode.

    explicit_color copies the SDK's colour table (which assumes a 4096x3072
    colour calibration); otherwise colour is derived from the sensor size.
    """
    if depth not in DEPTH_MODES:
        raise ValueError(f"unknown depth mode {depth!r}; choose from {', '.join(DEPTH_MODES)}")
    if color not in COLOR_MODES:
        raise ValueError(f"unknown colour resolution {color!r}; choose from {', '.join(COLOR_MODES)}")
    binned, crop, size = DEPTH_MODES[depth]
    color_binned, color_crop, color_size = COLOR_MODES[color]
    return CameraMode(
        name=f"k4a-{depth.replace('_', '-')}-{color}",
        depth_size=size, depth_crop=crop, color_size=color_size,
        depth_binning=1024 // binned[0], depth_scale=depth_scale,
        color_binned=color_binned if explicit_color else None,
        color_crop=color_crop if explicit_color else None,
    )


def _presets():
    modes = {DEFAULT_MODE: CameraMode(
        # The tested Orbbec Femto Bolt rig: NFOV unbinned depth and 720p colour.
        name=DEFAULT_MODE, depth_size=(640, 576), depth_crop=(192.0, 180.0),
        color_size=(1280, 720))}
    for depth in DEPTH_MODES:
        for color in COLOR_MODES:
            mode = k4a_mode(depth, color)
            modes[mode.name] = mode
    return modes


PRESETS = _presets()


def preset(name: str) -> CameraMode:
    """A named camera mode; see PRESETS."""
    try:
        return PRESETS[name]
    except (KeyError, TypeError):
        raise ValueError(f"unknown camera mode preset {name!r}; choose from "
                         f"{', '.join(sorted(PRESETS))}") from None


def resolve_mode(value=None, calibration: Path | None = None) -> tuple[CameraMode, str]:
    """The camera mode to use and where it came from.

    value may be a CameraMode, a preset name or a mapping. Without one, the
    calibration folder's camera_mode.json is read, else the tested default.
    """
    if isinstance(value, CameraMode):
        return value, "argument"
    if isinstance(value, str):
        return preset(value), "argument"
    if isinstance(value, Mapping):
        return CameraMode.from_dict(value), "argument"
    if value is not None:
        raise TypeError("camera_mode must be None, a preset name, a mapping or a CameraMode")
    if calibration is not None and (Path(calibration) / MODE_FILE).is_file():
        path = Path(calibration) / MODE_FILE
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise ValueError(f"{path} is not valid JSON: {error}") from error
        try:
            return CameraMode.from_dict(data), str(path)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{path}: {error}") from error
    return PRESETS[DEFAULT_MODE], "default"
