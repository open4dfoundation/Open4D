"""Ladder built from a prepared tile catalogue instead of the RGB-D source.

Why this exists
---------------
`OrbitPointCloudLadder` describes the point-cloud levels by reading
``level_*/manifest.json`` out of the **source** RGB-D tree. That tree is the
input to `orbitvivo.prepare`, and once the tiles exist it is no longer needed
to serve them: `prepare` bakes every tile into Draco and records the occupancy,
byte sizes and sequence bounds in ``catalog.json``. So a corpus can be complete
and still unservable, purely because the source tree it was derived from has
been deleted -- which is the situation for `ORBIT_vivo_tiles` here.

What is faithful and what is refused
------------------------------------
Everything the ViVo and NAVA servers read at serve time is present in the
catalogue, and nothing here is synthesised:

======================================  ==========================================
needed                                  taken from
======================================  ==========================================
level name, width, height, point_ratio  ``catalog["representations"][i]``
object id, name, bounds                 ``catalog["objects"][i]``
source_start_frame, source_frame_count  ``catalog["objects"][i]``
master_loop_frames                      ``catalog["objects"][i]``
per-tile occupancy and byte sizes       ``catalog["objects"][i]["sequence_tiles"]``
======================================  ==========================================

The one thing the catalogue does **not** carry is the capture rig: the real
per-object `CameraCalibration` set. That is why this deliberately does not build
a `DatasetManifest`, which requires exactly four real cameras per object and
hashes them into a calibration digest. Fabricating four plausible cameras to
satisfy that invariant would put invented calibration into a structure that
callers are entitled to trust.

It costs nothing here, because ViVo and NAVA never use the real rig: both build
their connection header from ``_identity_camera(tile, width, height)``, one
synthetic camera per spatial cell. Any access to the real cameras, or to the
RGB-D frame paths, raises `SourceCorpusUnavailable` naming what is missing
rather than returning a path that does not exist.

MetaStream/DeltaStream and LiVo cannot use this. They index the real rig while
serving (``obj.cameras[camera_index]``) and read the source RGB-D frames, so for
them the absent tree is missing data, not missing metadata.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from baselines.ViVo.orbitvivo.ladder import Representation

CATALOG_NAME = "catalog.json"
SUPPORTED_CATALOG_VERSION = 2


class SourceCorpusUnavailable(RuntimeError):
    """Raised when a caller needs the RGB-D source this ladder does not have."""


@dataclass(frozen=True)
class TileObjectSequence:
    """The object fields a tile-served baseline uses, and no others.

    Deliberately not an `ObjectSequence`: that type promises RGB-D frame
    patterns and a four-camera rig, and promising them without having them is
    how a missing corpus turns into silently wrong output instead of an error.
    """

    object_id: int
    name: str
    source_start_frame: int
    source_frame_count: int
    master_loop_frames: int
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]

    def __post_init__(self) -> None:
        if self.object_id < 0 or not self.name:
            raise ValueError("object id and name are required")
        if self.source_frame_count <= 0 or self.master_loop_frames <= 0:
            raise ValueError(f"invalid loop lengths for {self.name}")
        if self.master_loop_frames % self.source_frame_count:
            raise ValueError(
                f"master loop {self.master_loop_frames} is not a whole number of "
                f"{self.source_frame_count}-frame source loops for {self.name}")
        if any(upper <= lower for lower, upper
               in zip(self.bounds_min, self.bounds_max)):
            raise ValueError(f"invalid sequence bounds for {self.name}")

    def source_frame(self, master_frame: int) -> int:
        """Identical arithmetic to `ObjectSequence.source_frame`."""
        return self.source_start_frame + (int(master_frame) % self.source_frame_count)

    @property
    def cameras(self):
        raise SourceCorpusUnavailable(
            f"the capture rig for {self.name} lived in the RGB-D manifests, which "
            "this catalogue-backed ladder does not have. ViVo and NAVA do not need "
            "it -- they build identity cameras per tile -- so a caller reaching "
            "here is a baseline that needs the source corpus restored.")

    def image_paths(self, *args, **kwargs):
        raise SourceCorpusUnavailable(
            f"no RGB-D frames for {self.name}: this ladder serves prepared Draco "
            "tiles only.")

    def pointcloud_path(self, *args, **kwargs):
        raise SourceCorpusUnavailable(
            f"no source point clouds for {self.name}: this ladder serves prepared "
            "Draco tiles only.")


@dataclass(frozen=True)
class TileManifest:
    """The `manifest` surface the servers touch: dimensions, rate, objects.

    The full attribute set both servers read is `width`, `height`, `fps`,
    `block_size`, `master_loop_frames`, `objects` and `calibration_hash`.
    """

    width: int
    height: int
    fps: float
    master_loop_frames: int
    objects: tuple[TileObjectSequence, ...]
    # DatasetManifest's default, which every prepared level was produced under
    # -- the constructor rejects dimensions that are not a multiple of it. The
    # field is an RGB-D residual-block size and a tile-served client never uses
    # it; it travels in the connection header, so it has to be the value the
    # corpus was prepared with rather than a fresh guess.
    block_size: int = 16

    def __post_init__(self) -> None:
        if self.width <= 0 or self.height <= 0 or self.fps <= 0:
            raise ValueError("invalid stream dimensions or rate")
        if self.block_size <= 0:
            raise ValueError("block size must be positive")
        if self.width % self.block_size or self.height % self.block_size:
            raise ValueError("dimensions must be divisible by the block size")

    @property
    def calibration_hash(self) -> str:
        """Digest of the calibration this ladder has, labelled as such.

        `DatasetManifest.calibration_hash` covers per-object bounds AND the
        four-camera rig. The rig is gone, so this cannot reproduce that value
        and must not look like it could: an unprefixed 64-hex string would be
        mistaken for one and silently fail to match. The `tiles-nocal:` prefix
        makes the difference legible in the connection header, and the digest
        still detects a corpus whose object set or bounds changed underneath a
        client.
        """
        calibration = [
            {
                "object_id": obj.object_id,
                "bounds_min": list(obj.bounds_min),
                "bounds_max": list(obj.bounds_max),
            }
            for obj in self.objects
        ]
        encoded = json.dumps(calibration, sort_keys=True,
                             separators=(",", ":")).encode()
        return f"tiles-nocal:{hashlib.sha256(encoded).hexdigest()}"

    @property
    def stream_count(self) -> int:
        raise SourceCorpusUnavailable(
            "stream_count counts RGB-D camera streams; a tile-served corpus has "
            "none.")


class TileCatalogLadder:
    """`OrbitPointCloudLadder`'s surface, sourced from ``catalog.json``.

    Same public members, so `ViVoServer` and `NavaServer` take it unchanged:
    `root`, `representations`, `highest`, `objects`, `representation`, `object`,
    `describe`.
    """

    def __init__(self, prepared_dir: Path | str, *, fps: float = 30.0):
        self.root = Path(prepared_dir).expanduser().resolve()
        catalog_path = self.root / CATALOG_NAME
        if not catalog_path.is_file():
            raise FileNotFoundError(catalog_path)
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))

        version = int(catalog.get("version", 0))
        if version != SUPPORTED_CATALOG_VERSION:
            raise ValueError(
                f"{catalog_path} is catalogue version {version}; this adapter "
                f"reads version {SUPPORTED_CATALOG_VERSION}. Earlier versions "
                "predate the per-representation width/height and point_ratio "
                "fields this needs, so there is nothing to fall back on.")

        levels = catalog.get("representations") or []
        if not levels:
            raise ValueError(f"{catalog_path} declares no representations")

        objects = tuple(
            TileObjectSequence(
                object_id=int(entry["object_id"]),
                name=str(entry["name"]),
                source_start_frame=int(entry["source_start_frame"]),
                source_frame_count=int(entry["source_frame_count"]),
                master_loop_frames=int(entry["master_loop_frames"]),
                bounds_min=tuple(float(v) for v in entry["bounds_min"]),
                bounds_max=tuple(float(v) for v in entry["bounds_max"]),
            )
            for entry in sorted(catalog["objects"], key=lambda e: int(e["object_id"]))
        )
        if not objects:
            raise ValueError(f"{catalog_path} declares no objects")
        if len({obj.object_id for obj in objects}) != len(objects):
            raise ValueError(f"duplicate object id in {catalog_path}")
        if len({obj.name for obj in objects}) != len(objects):
            raise ValueError(f"duplicate object name in {catalog_path}")

        # One master loop for the whole scene, as the manifest-backed ladder
        # also requires: objects that disagree cannot be advanced together.
        loops = {obj.master_loop_frames for obj in objects}
        if len(loops) != 1:
            raise ValueError(f"objects disagree on master_loop_frames: {sorted(loops)}")
        master_loop_frames = loops.pop()

        representations = []
        for index, level in enumerate(sorted(levels, key=lambda l: int(l["id"]))):
            representation_id = int(level["id"])
            if representation_id != index:
                raise ValueError(
                    f"representation ids must be contiguous from 0; got "
                    f"{representation_id} at position {index}")
            level_root = self.root / str(level["name"])
            if not level_root.is_dir():
                raise FileNotFoundError(
                    f"{level_root} is declared in {catalog_path} but absent; the "
                    "catalogue and the tiles have diverged")
            representations.append(Representation(
                representation_id,
                str(level["name"]),
                level_root,
                catalog_path,
                TileManifest(
                    width=int(level["width"]),
                    height=int(level["height"]),
                    fps=float(fps),
                    master_loop_frames=master_loop_frames,
                    objects=objects,
                ),
                float(level["point_ratio"]),
            ))
        # Highest quality first, matching the manifest-backed ladder's contract.
        if representations[0].point_ratio != max(
                value.point_ratio for value in representations):
            raise ValueError(
                "representation 0 must be the highest-quality level; "
                f"point_ratios are {[v.point_ratio for v in representations]}")
        self.representations = tuple(representations)
        self.catalog = catalog

    @property
    def highest(self) -> Representation:
        return self.representations[0]

    @property
    def objects(self) -> tuple[TileObjectSequence, ...]:
        return self.highest.manifest.objects

    def representation(self, representation_id: int) -> Representation:
        if not 0 <= int(representation_id) < len(self.representations):
            raise IndexError(f"invalid representation {representation_id}")
        return self.representations[int(representation_id)]

    def object(self, representation_id: int, object_id: int) -> TileObjectSequence:
        for obj in self.representation(representation_id).manifest.objects:
            if obj.object_id == object_id:
                return obj
        raise KeyError(object_id)

    def describe(self) -> dict:
        return {
            "root": str(self.root),
            "source": "prepared tile catalogue (no RGB-D manifests)",
            "representations": [
                {
                    "id": value.representation_id,
                    "name": value.name,
                    "manifest": str(value.manifest_path),
                    "width": value.manifest.width,
                    "height": value.manifest.height,
                    "point_ratio": value.point_ratio,
                }
                for value in self.representations
            ],
        }
