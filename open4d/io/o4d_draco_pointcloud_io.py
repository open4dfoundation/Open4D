"""
O4D container with Google Draco-compressed point clouds.

Codec id: CODEC_DRACO_POINTS = 3
Geometry: GEOM_POINTCLOUD = 2 (same as raw point cloud)

KFRM payload layout:
  fixed header  : frame_index(u32) timestamp(f64) draco_blob_size(u32)  [16 bytes]
  draco blob    : draco_blob_size bytes  (Draco encodes XYZ + optional RGB together)
"""

import io
import json
import os
import struct
from dataclasses import dataclass
from typing import Dict, Iterator, List, Optional, Tuple

import numpy as np

try:
    import DracoPy
except ImportError as _e:
    raise ImportError("DracoPy is required: pip install DracoPy") from _e

# ----------------------------
# Constants
# ----------------------------
CHUNK_HDR_STRUCT = struct.Struct("<4sQ")   # chunk_type(4s), payload_size(u64)
HEAD_MAGIC = b"O4D1"

GEOM_POINTCLOUD = 2
CODEC_DRACO_POINTS = 3       # Draco-compressed point cloud frames

HEAD_FIXED_STRUCT = struct.Struct("<4sHBBII")  # magic, version, geom, codec, flags, meta_size
VERSION = 1

# KFRM fixed for draco frames: frame_index(u32), timestamp(f64), draco_blob_size(u32)
DRACO_KFRM_FIXED = struct.Struct("<IdI")  # 4 + 8 + 4 = 16 bytes

INDX_COUNT_STRUCT = struct.Struct("<I")
INDX_ENTRY_STRUCT = struct.Struct("<IdQQ")  # frame_index, timestamp, chunk_offset, chunk_size


@dataclass(frozen=True)
class IndexEntry:
    frame_index: int
    timestamp: float
    chunk_offset: int
    chunk_size: int


# ----------------------------
# Low-level chunk helpers
# ----------------------------
def _write_chunk(f: io.BufferedWriter, chunk_type: bytes, payload: bytes) -> int:
    if len(chunk_type) != 4:
        raise ValueError("chunk_type must be 4 bytes")
    offset = f.tell()
    f.write(CHUNK_HDR_STRUCT.pack(chunk_type, len(payload)))
    f.write(payload)
    return offset


def _read_chunk_header(f: io.BufferedReader) -> Optional[Tuple[bytes, int, int]]:
    header_offset = f.tell()
    data = f.read(CHUNK_HDR_STRUCT.size)
    if not data:
        return None
    if len(data) != CHUNK_HDR_STRUCT.size:
        raise IOError("Truncated chunk header")
    chunk_type, payload_size = CHUNK_HDR_STRUCT.unpack(data)
    return chunk_type, payload_size, header_offset


# ----------------------------
# Writer
# ----------------------------
class O4DDracoPointCloudWriter:
    """
    Writes an .o4d file where each frame is Draco-compressed.

    Each keyframe compresses positions (and optional colors) into a single
    Draco blob, then stores it in a KFRM chunk.

    Parameters
    ----------
    path : str
        Output .o4d file path.
    quantization_bits : int
        Draco position quantization (1-30). Higher = more precision, larger file.
    compression_level : int
        Draco compression level (0-10). Higher = smaller file, slower encode.
    meta : dict, optional
        Arbitrary JSON metadata stored in the HEAD chunk.
    """

    def __init__(
        self,
        path: str,
        quantization_bits: int = 14,
        compression_level: int = 7,
        meta: Optional[Dict] = None,
    ):
        self.path = path
        self.quantization_bits = int(quantization_bits)
        self.compression_level = int(compression_level)
        self.meta = meta or {}
        self._f: Optional[io.BufferedWriter] = None
        self._index: List[IndexEntry] = []
        self._frame_counter = 0
        self._closed = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def open(self):
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        self._f = open(self.path, "wb")
        self._write_head()

    def _write_head(self):
        assert self._f is not None
        meta_out = dict(self.meta)
        meta_out.setdefault("codec", "draco")
        meta_out.setdefault("quantization_bits", self.quantization_bits)
        meta_out.setdefault("compression_level", self.compression_level)
        meta_bytes = json.dumps(meta_out).encode("utf-8")
        fixed = HEAD_FIXED_STRUCT.pack(
            HEAD_MAGIC, VERSION, GEOM_POINTCLOUD, CODEC_DRACO_POINTS, 0, len(meta_bytes)
        )
        _write_chunk(self._f, b"HEAD", fixed + meta_bytes)

    def write_keyframe(
        self,
        points_xyz: np.ndarray,
        colors_rgb: Optional[np.ndarray] = None,
        timestamp: Optional[float] = None,
        frame_index: Optional[int] = None,
    ):
        """
        Compress and store one point cloud frame.

        Parameters
        ----------
        points_xyz : ndarray, shape (N, 3), float32
        colors_rgb : ndarray, shape (N, 3), uint8, optional
        timestamp : float, optional
        frame_index : int, optional
        """
        if self._closed:
            raise RuntimeError("Writer is closed")
        assert self._f is not None

        if frame_index is None:
            frame_index = self._frame_counter
        if timestamp is None:
            timestamp = float(frame_index)

        pts = np.asarray(points_xyz, dtype=np.float32)
        if pts.ndim != 2 or pts.shape[1] != 3:
            raise ValueError("points_xyz must be shape (N, 3)")

        cols: Optional[np.ndarray] = None
        if colors_rgb is not None:
            cols = np.asarray(colors_rgb)
            if cols.shape != (pts.shape[0], 3):
                raise ValueError("colors_rgb must be shape (N, 3) matching points")
            cols = np.clip(cols, 0, 255).astype(np.uint8, copy=False)

        draco_blob: bytes = DracoPy.encode(
            pts,
            faces=None,
            colors=cols,
            quantization_bits=self.quantization_bits,
            compression_level=self.compression_level,
        )

        fixed = DRACO_KFRM_FIXED.pack(int(frame_index), float(timestamp), len(draco_blob))
        payload = fixed + draco_blob

        chunk_offset = self._f.tell()
        _write_chunk(self._f, b"KFRM", payload)
        chunk_size_total = CHUNK_HDR_STRUCT.size + len(payload)

        self._index.append(
            IndexEntry(int(frame_index), float(timestamp), int(chunk_offset), int(chunk_size_total))
        )
        self._frame_counter = max(self._frame_counter, int(frame_index) + 1)

    def close(self):
        if self._closed:
            return
        if self._f is None:
            self._closed = True
            return
        payload_parts = [INDX_COUNT_STRUCT.pack(len(self._index))]
        for e in self._index:
            payload_parts.append(
                INDX_ENTRY_STRUCT.pack(e.frame_index, e.timestamp, e.chunk_offset, e.chunk_size)
            )
        _write_chunk(self._f, b"INDX", b"".join(payload_parts))
        self._f.flush()
        self._f.close()
        self._f = None
        self._closed = True


# ----------------------------
# Reader
# ----------------------------
class O4DDracoPointCloudReader:
    """
    Reads an .o4d file written by O4DDracoPointCloudWriter.
    Decodes each frame on demand using Draco.
    """

    def __init__(self, path: str):
        self.path = path
        self._f: Optional[io.BufferedReader] = None
        self.meta: Dict = {}
        self.index: List[IndexEntry] = []
        self._index_by_frame: Dict[int, IndexEntry] = {}

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def open(self):
        self._f = open(self.path, "rb")
        self._read_head()
        self._read_index()

    def close(self):
        if self._f:
            self._f.close()
            self._f = None

    def _read_head(self):
        assert self._f is not None
        self._f.seek(0)
        hdr = _read_chunk_header(self._f)
        if hdr is None:
            raise IOError("Empty file")
        ctype, payload_size, _ = hdr
        if ctype != b"HEAD":
            raise IOError("Missing HEAD chunk")
        payload = self._f.read(payload_size)
        if len(payload) < HEAD_FIXED_STRUCT.size:
            raise IOError("Truncated HEAD payload")
        magic, version, geom, codec, flags, meta_size = HEAD_FIXED_STRUCT.unpack(
            payload[: HEAD_FIXED_STRUCT.size]
        )
        if magic != HEAD_MAGIC:
            raise IOError("Not an O4D file (bad magic)")
        if version != VERSION:
            raise IOError(f"Unsupported O4D version: {version}")
        if geom != GEOM_POINTCLOUD:
            raise IOError("Expected point cloud geometry")
        if codec != CODEC_DRACO_POINTS:
            raise IOError(
                f"Expected Draco point cloud codec ({CODEC_DRACO_POINTS}), got {codec}. "
                "Use O4DPointCloudReader for raw point clouds."
            )
        meta_bytes = payload[HEAD_FIXED_STRUCT.size : HEAD_FIXED_STRUCT.size + meta_size]
        self.meta = json.loads(meta_bytes.decode("utf-8")) if meta_size > 0 else {}

    def _read_index(self):
        assert self._f is not None
        f = self._f
        f.seek(0)
        last_indx_offset = None
        while True:
            hdr = _read_chunk_header(f)
            if hdr is None:
                break
            ctype, payload_size, hdr_offset = hdr
            f.seek(payload_size, io.SEEK_CUR)
            if ctype == b"INDX":
                last_indx_offset = hdr_offset
        if last_indx_offset is None:
            raise IOError("Missing INDX chunk")
        f.seek(last_indx_offset)
        ctype, payload_size, _ = _read_chunk_header(f)
        assert ctype == b"INDX"
        payload = f.read(payload_size)
        if len(payload) < INDX_COUNT_STRUCT.size:
            raise IOError("Invalid INDX payload")
        (n,) = INDX_COUNT_STRUCT.unpack(payload[: INDX_COUNT_STRUCT.size])
        pos = INDX_COUNT_STRUCT.size
        entries: List[IndexEntry] = []
        for _ in range(n):
            if pos + INDX_ENTRY_STRUCT.size > len(payload):
                raise IOError("Truncated INDX entries")
            fi, ts, off, sz = INDX_ENTRY_STRUCT.unpack(
                payload[pos : pos + INDX_ENTRY_STRUCT.size]
            )
            pos += INDX_ENTRY_STRUCT.size
            entries.append(IndexEntry(int(fi), float(ts), int(off), int(sz)))
        self.index = entries
        self._index_by_frame = {e.frame_index: e for e in entries}

    def __len__(self) -> int:
        return len(self.index)

    def iter_frames(self) -> Iterator[Tuple[int, float]]:
        """Yields (frame_index, timestamp) for all frames."""
        for e in self.index:
            yield e.frame_index, e.timestamp

    def get_frame(
        self, frame_index: int
    ) -> Tuple[np.ndarray, Optional[np.ndarray], float]:
        """
        Decode and return one frame.

        Returns
        -------
        points_xyz : ndarray, shape (N, 3), float32
        colors_rgb : ndarray, shape (N, 3), uint8, or None
        timestamp  : float
        """
        assert self._f is not None
        e = self._index_by_frame.get(int(frame_index))
        if e is None:
            raise KeyError(f"Frame {frame_index} not found")

        f = self._f
        f.seek(e.chunk_offset)
        hdr = _read_chunk_header(f)
        if hdr is None:
            raise IOError("Unexpected EOF")
        ctype, payload_size, _ = hdr
        if ctype != b"KFRM":
            raise IOError(f"Expected KFRM, got {ctype!r}")

        payload = f.read(payload_size)
        if len(payload) < DRACO_KFRM_FIXED.size:
            raise IOError("Truncated KFRM payload")

        fi, ts, draco_size = DRACO_KFRM_FIXED.unpack(payload[: DRACO_KFRM_FIXED.size])
        if int(fi) != int(frame_index):
            raise IOError("Index / frame mismatch")

        draco_blob = payload[DRACO_KFRM_FIXED.size : DRACO_KFRM_FIXED.size + draco_size]
        if len(draco_blob) != draco_size:
            raise IOError("Truncated Draco blob")

        pc = DracoPy.decode(draco_blob)
        points = np.asarray(pc.points, dtype=np.float32).reshape(-1, 3)

        colors: Optional[np.ndarray] = None
        if pc.colors is not None and len(pc.colors) > 0:
            colors = np.asarray(pc.colors, dtype=np.uint8).reshape(-1, 3)

        return points, colors, float(ts)
