"""Calibrated multiview inputs for separately installed reconstruction methods."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True, eq=False)
class MultiviewCapture:
    """Image paths, 3x3 intrinsics, and camera-to-world poses in metres.

    COLMAP reconstructions have an arbitrary scale: ``from_colmap`` requires
    ``scale_to_meters`` explicitly. This input container does not reconstruct,
    calibrate, undistort, or infer image timing. It is intended for installed
    reconstruction plugins that explicitly support calibrated multiview
    inputs. The built-in RGB-D method accepts ``RGBDCapture`` instead; the
    native QUEEN runner requires its own on-disk scene layout and does not
    accept this container directly.
    """

    images: tuple[Path, ...]
    intrinsics: np.ndarray
    camera_poses: np.ndarray

    def __post_init__(self):
        paths = tuple(Path(path).expanduser().resolve() for path in self.images)
        if not paths or any(not path.is_file() for path in paths):
            raise ValueError("images must contain existing image files")
        count = len(paths)
        arrays = {}
        for name, shape in (("intrinsics", (count, 3, 3)),
                            ("camera_poses", (count, 4, 4))):
            value = np.array(getattr(self, name), dtype=np.float64, copy=True)
            if value.shape != shape or not np.isfinite(value).all():
                raise ValueError(f"{name} must have finite shape {shape}")
            value.flags.writeable = False
            arrays[name] = value
        k, poses = arrays["intrinsics"], arrays["camera_poses"]
        if np.any(k[:, 0, 0] <= 0) or np.any(k[:, 1, 1] <= 0) or not np.allclose(
                k[:, 2], (0, 0, 1)):
            raise ValueError("intrinsics must have positive focal lengths and row [0, 0, 1]")
        rotations = poses[:, :3, :3]
        if not np.allclose(poses[:, 3], (0, 0, 0, 1)) or not np.allclose(
                rotations @ rotations.transpose(0, 2, 1), np.eye(3), atol=1e-6
        ) or np.any(np.linalg.det(rotations) <= 0):
            raise ValueError("camera_poses must be rigid camera-to-world transforms")
        object.__setattr__(self, "images", paths)
        for name, value in arrays.items():
            object.__setattr__(self, name, value)

    def __len__(self):
        return len(self.images)

    @classmethod
    def from_colmap(cls, model, images, *, scale_to_meters):
        """Read COLMAP text ``cameras.txt`` / ``images.txt`` and image files.

        Export binary models to text with COLMAP first. Only undistorted
        PINHOLE and SIMPLE_PINHOLE cameras are accepted; distortion-bearing
        models must be undistorted by COLMAP, not silently discarded here.
        """
        scale = float(scale_to_meters)
        if isinstance(scale_to_meters, bool) or not np.isfinite(scale) or scale <= 0:
            raise ValueError("scale_to_meters must be finite and positive")
        model, root = Path(model).expanduser(), Path(images).expanduser().resolve()
        cameras = {}
        for line in (model / "cameras.txt").read_text().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split()
            identifier, kind = int(parts[0]), parts[1]
            params = list(map(float, parts[4:]))
            if kind == "PINHOLE" and len(params) == 4:
                fx, fy, cx, cy = params
            elif kind == "SIMPLE_PINHOLE" and len(params) == 3:
                fx, cx, cy = params
                fy = fx
            else:
                raise ValueError(f"unsupported COLMAP camera {kind}; undistort to PINHOLE")
            if identifier in cameras:
                raise ValueError("duplicate COLMAP camera id")
            cameras[identifier] = np.array([[fx, 0, cx], [0, fy, cy], [0, 0, 1]])
        paths, intrinsics, poses, ids = [], [], [], set()
        lines = (model / "images.txt").read_text().splitlines()
        expect_pose = True
        for line in lines:
            if line.lstrip().startswith("#"):
                continue
            if not expect_pose:
                observations = line.split()
                if len(observations) % 3:
                    raise ValueError("invalid COLMAP observation record")
                try:
                    for offset in range(0, len(observations), 3):
                        xy = tuple(map(float, observations[offset:offset+2]))
                        if not np.isfinite(xy).all():
                            raise ValueError("nonfinite observation")
                        int(observations[offset+2])
                except ValueError as error:
                    raise ValueError("invalid COLMAP observation record") from error
                expect_pose = True  # observation line may legitimately be empty
                continue
            if not line.strip():
                continue
            parts = line.split(maxsplit=9)
            if len(parts) != 10:
                raise ValueError("invalid COLMAP image pose record")
            image_id, camera_id = int(parts[0]), int(parts[8])
            if image_id in ids:
                raise ValueError("duplicate COLMAP image id")
            ids.add(image_id)
            q = np.array(list(map(float, parts[1:5])))
            norm = np.linalg.norm(q)
            if not np.isfinite(norm) or norm == 0:
                raise ValueError("COLMAP quaternion must be finite and nonzero")
            w, x, y, z = q / norm
            rotation = np.array([
                [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
            ])
            pose = np.eye(4)
            pose[:3, :3] = rotation.T
            pose[:3, 3] = -rotation.T @ np.array(list(map(float, parts[5:8]))) * scale
            path = (root / parts[9]).resolve()
            if not path.is_relative_to(root):
                raise ValueError("COLMAP image path leaves the image folder")
            if camera_id not in cameras:
                raise ValueError(f"missing COLMAP camera {camera_id}")
            paths.append(path)
            intrinsics.append(cameras[camera_id])
            poses.append(pose)
            expect_pose = False
        if not expect_pose:
            raise ValueError("COLMAP image pose is missing its observation line")
        return cls(tuple(paths), np.asarray(intrinsics), np.asarray(poses))
