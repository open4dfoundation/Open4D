import io
import json
import os
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple, Iterator

import numpy as np

# ----------------------------
# Constants / enums
# ----------------------------
CHUNK_HDR_STRUCT = struct.Struct("<4sQ")  # type, payload_size
HEAD_MAGIC = b"O4D1"

GEOM_MESH = 1
GEOM_POINTCLOUD = 2

CODEC_RAW_MESH = 1
CODEC_RAW_POINTS = 1  # codec id within pointcloud mode (kept 1 for "raw")

HEAD_FIXED_STRUCT = struct.Struct("<4sHBBII")  # magic, version, geom, codec, flags, meta_size
VERSION = 1

# KFRM fixed header for pointcloud: frame_index(u32), timestamp(f64), n(u32), has_color(u8), pad(3)
PC_KFRM_FIXED_STRUCT = struct.Struct("<IdIB3s")  # 4 + 8 + 4 + 1 + 3 = 20 bytes

INDX_COUNT_STRUCT = struct.Struct("<I")
INDX_ENTRY_STRUCT = struct.Struct("<IdQQ")


@dataclass(frozen=True)
class IndexEntry:
    frame_index: int
    timestamp: float
    chunk_offset: int
    chunk_size: int


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
class O4DPointCloudWriter:
    """
    Writes .o4d container with:
      - HEAD
      - many KFRM chunks (raw point cloud)
      - INDX at end
    """

    def __init__(self, path: str, meta: Optional[Dict] = None):
        self.path = path
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
        meta_bytes = json.dumps(self.meta).encode("utf-8")
        fixed = HEAD_FIXED_STRUCT.pack(
            HEAD_MAGIC,
            VERSION,
            GEOM_POINTCLOUD,
            CODEC_RAW_POINTS,
            0,  # flags reserved
            len(meta_bytes),
        )
        payload = fixed + meta_bytes
        _write_chunk(self._f, b"HEAD", payload)

    def write_keyframe(
        self,
        points_xyz: np.ndarray,
        colors_rgb: Optional[np.ndarray] = None,
        timestamp: Optional[float] = None,
        frame_index: Optional[int] = None,
    ):
        """
        points_xyz: (N,3) float32/float64
        colors_rgb: optional (N,3) uint8 or int in [0,255]
        """
        if self._closed:
            raise RuntimeError("Writer is closed")
        assert self._f is not None

        if frame_index is None:
            frame_index = self._frame_counter
        if timestamp is None:
            timestamp = float(frame_index)

        P = np.asarray(points_xyz)
        if P.ndim != 2 or P.shape[1] != 3:
            raise ValueError("points_xyz must be shape (N,3)")
        P = P.astype(np.float32, copy=False)
        n = P.shape[0]

        has_color = 0
        C_bytes = b""
        if colors_rgb is not None:
            C = np.asarray(colors_rgb)
            if C.shape != (n, 3):
                raise ValueError("colors_rgb must be shape (N,3) matching points")
            if C.dtype != np.uint8:
                # clamp + cast
                C = np.clip(C, 0, 255).astype(np.uint8)
            has_color = 1
            C_bytes = C.tobytes(order="C")

        fixed = PC_KFRM_FIXED_STRUCT.pack(int(frame_index), float(timestamp), int(n), int(has_color), b"\x00\x00\x00")
        payload = fixed + P.tobytes(order="C") + C_bytes

        chunk_offset = self._f.tell()
        _write_chunk(self._f, b"KFRM", payload)
        chunk_size_total = CHUNK_HDR_STRUCT.size + len(payload)

        self._index.append(IndexEntry(int(frame_index), float(timestamp), int(chunk_offset), int(chunk_size_total)))
        self._frame_counter = max(self._frame_counter, int(frame_index) + 1)

    def close(self):
        if self._closed:
            return
        if self._f is None:
            self._closed = True
            return

        payload_parts = [INDX_COUNT_STRUCT.pack(len(self._index))]
        for e in self._index:
            payload_parts.append(INDX_ENTRY_STRUCT.pack(e.frame_index, e.timestamp, e.chunk_offset, e.chunk_size))
        payload = b"".join(payload_parts)
        _write_chunk(self._f, b"INDX", payload)

        self._f.flush()
        self._f.close()
        self._f = None
        self._closed = True


# ----------------------------
# Reader
# ----------------------------
class O4DPointCloudReader:
    """
    Reads .o4d point cloud container (raw points, v1).
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
        self._read_index_linear()

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
        if len(payload) != payload_size:
            raise IOError("Truncated HEAD payload")

        if len(payload) < HEAD_FIXED_STRUCT.size:
            raise IOError("Invalid HEAD payload")

        magic, version, geom, codec, flags, meta_size = HEAD_FIXED_STRUCT.unpack(payload[:HEAD_FIXED_STRUCT.size])
        if magic != HEAD_MAGIC:
            raise IOError("Not an O4D file (bad magic)")
        if version != VERSION:
            raise IOError(f"Unsupported O4D version: {version}")
        if geom != GEOM_POINTCLOUD:
            raise IOError("This reader only supports point cloud geometry")
        if codec != CODEC_RAW_POINTS:
            raise IOError("This reader only supports raw-points codec (v1)")

        meta_bytes = payload[HEAD_FIXED_STRUCT.size : HEAD_FIXED_STRUCT.size + meta_size]
        if len(meta_bytes) != meta_size:
            raise IOError("Truncated HEAD metadata")
        self.meta = json.loads(meta_bytes.decode("utf-8")) if meta_size > 0 else {}

    def _read_index_linear(self):
        """
        v1 simple: scan forward and keep the last INDX chunk.
        """
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
        if len(payload) != payload_size:
            raise IOError("Truncated INDX payload")

        if len(payload) < INDX_COUNT_STRUCT.size:
            raise IOError("Invalid INDX payload")
        (n,) = INDX_COUNT_STRUCT.unpack(payload[:INDX_COUNT_STRUCT.size])
        pos = INDX_COUNT_STRUCT.size

        entries: List[IndexEntry] = []
        for _ in range(n):
            if pos + INDX_ENTRY_STRUCT.size > len(payload):
                raise IOError("Invalid INDX entries")
            frame_index, timestamp, offset, chunk_size = INDX_ENTRY_STRUCT.unpack(payload[pos : pos + INDX_ENTRY_STRUCT.size])
            pos += INDX_ENTRY_STRUCT.size
            entries.append(IndexEntry(int(frame_index), float(timestamp), int(offset), int(chunk_size)))

        self.index = entries
        self._index_by_frame = {e.frame_index: e for e in entries}

    def __len__(self) -> int:
        return len(self.index)

    def iter_frames(self) -> Iterator[Tuple[int, float]]:
        for e in self.index:
            yield e.frame_index, e.timestamp

    def get_frame(self, frame_index: int) -> Tuple[np.ndarray, Optional[np.ndarray], float]:
        """
        Returns (points_xyz float32 (N,3), colors_rgb uint8 (N,3) or None, timestamp)
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
            raise IOError(f"Expected KFRM at offset {e.chunk_offset}, got {ctype!r}")

        payload = f.read(payload_size)
        if len(payload) != payload_size:
            raise IOError("Truncated KFRM payload")

        if len(payload) < PC_KFRM_FIXED_STRUCT.size:
            raise IOError("Invalid KFRM payload")

        fi, ts, n, has_color, _pad = PC_KFRM_FIXED_STRUCT.unpack(payload[:PC_KFRM_FIXED_STRUCT.size])
        if int(fi) != int(frame_index):
            raise IOError("Index/frame mismatch")

        pos = PC_KFRM_FIXED_STRUCT.size
        p_bytes = int(n) * 3 * 4
        if pos + p_bytes > len(payload):
            raise IOError("Invalid point payload size")
        points = np.frombuffer(payload, dtype=np.float32, count=int(n) * 3, offset=pos).reshape((int(n), 3))
        pos += p_bytes

        colors = None
        if has_color:
            c_bytes = int(n) * 3
            if pos + c_bytes > len(payload):
                raise IOError("Invalid color payload size")
            colors = np.frombuffer(payload, dtype=np.uint8, count=int(n) * 3, offset=pos).reshape((int(n), 3))

        return points.copy(), (colors.copy() if colors is not None else None), float(ts)
