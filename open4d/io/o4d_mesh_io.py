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

CODEC_RAW_MESH = 1

# HEAD payload: magic(4) version(u16) geom(u8) codec(u8) flags(u32) meta_size(u32) meta_bytes
HEAD_FIXED_STRUCT = struct.Struct("<4sHBBII")  # magic, version, geom, codec, flags, meta_size

# KFRM fixed header: frame_index(u32), timestamp(f64), nv(u32), nf(u32)
KFRM_FIXED_STRUCT = struct.Struct("<IdII")  # I, d, I, I

# INDX: num_frames(u32) then entries: frame_index(u32), timestamp(f64), offset(u64), chunk_size(u64)
INDX_COUNT_STRUCT = struct.Struct("<I")
INDX_ENTRY_STRUCT = struct.Struct("<IdQQ")

VERSION = 1


@dataclass(frozen=True)
class IndexEntry:
    frame_index: int
    timestamp: float
    chunk_offset: int
    chunk_size: int


def _write_chunk(f: io.BufferedWriter, chunk_type: bytes, payload: bytes) -> int:
    """Write a chunk and return file offset where chunk header starts."""
    if len(chunk_type) != 4:
        raise ValueError("chunk_type must be 4 bytes")
    offset = f.tell()
    f.write(CHUNK_HDR_STRUCT.pack(chunk_type, len(payload)))
    f.write(payload)
    return offset


def _read_chunk_header(f: io.BufferedReader) -> Optional[Tuple[bytes, int, int]]:
    """Return (type, payload_size, header_offset) or None if EOF."""
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
class O4DMeshWriter:
    """
    Writes .o4d container with:
      - HEAD
      - many KFRM chunks (raw mesh)
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
            GEOM_MESH,
            CODEC_RAW_MESH,
            0,  # flags reserved
            len(meta_bytes),
        )
        payload = fixed + meta_bytes
        _write_chunk(self._f, b"HEAD", payload)

    def write_keyframe(
        self,
        vertices: np.ndarray,
        faces: np.ndarray,
        timestamp: Optional[float] = None,
        frame_index: Optional[int] = None,
    ):
        """
        vertices: (N,3) float32/float64
        faces: (M,3) int/uint32
        """
        if self._closed:
            raise RuntimeError("Writer is closed")
        assert self._f is not None

        if frame_index is None:
            frame_index = self._frame_counter
        if timestamp is None:
            timestamp = float(frame_index)

        v = np.asarray(vertices)
        fcs = np.asarray(faces)

        if v.ndim != 2 or v.shape[1] != 3:
            raise ValueError("vertices must be shape (N,3)")
        if fcs.ndim != 2 or fcs.shape[1] != 3:
            raise ValueError("faces must be shape (M,3)")

        v = v.astype(np.float32, copy=False)
        fcs = fcs.astype(np.uint32, copy=False)

        nv = v.shape[0]
        nf = fcs.shape[0]

        fixed = KFRM_FIXED_STRUCT.pack(int(frame_index), float(timestamp), int(nv), int(nf))
        payload = fixed + v.tobytes(order="C") + fcs.tobytes(order="C")

        chunk_offset = self._f.tell()
        _write_chunk(self._f, b"KFRM", payload)

        chunk_size_total = (CHUNK_HDR_STRUCT.size + len(payload))
        self._index.append(IndexEntry(int(frame_index), float(timestamp), int(chunk_offset), int(chunk_size_total)))

        self._frame_counter = max(self._frame_counter, int(frame_index) + 1)

    def close(self):
        if self._closed:
            return
        if self._f is None:
            self._closed = True
            return

        # write INDX
        entries = self._index
        payload_parts = [INDX_COUNT_STRUCT.pack(len(entries))]
        for e in entries:
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
class O4DMeshReader:
    """
    Reads .o4d container written by O4DMeshWriter.
    Builds index from INDX chunk at end.
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
        self._read_index_from_end()

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
        if geom != GEOM_MESH:
            raise IOError("This reader only supports mesh geometry")
        if codec != CODEC_RAW_MESH:
            raise IOError("This reader only supports raw-mesh codec (v1)")
        # flags currently unused

        meta_bytes = payload[HEAD_FIXED_STRUCT.size : HEAD_FIXED_STRUCT.size + meta_size]
        if len(meta_bytes) != meta_size:
            raise IOError("Truncated HEAD metadata")
        self.meta = json.loads(meta_bytes.decode("utf-8")) if meta_size > 0 else {}

    def _read_index_from_end(self):
        """
        Find the last INDX chunk by scanning backwards in a small-ish window.
        For v1 simplicity: assume INDX is the last chunk.
        """
        assert self._f is not None
        f = self._f
        f.seek(0, io.SEEK_END)
        file_size = f.tell()
        if file_size < CHUNK_HDR_STRUCT.size:
            raise IOError("File too small")

        # Read last chunk header
        # We don't know payload size without reading header, but header is at (end - (8+4) - payload) which we don't know.
        # So: in v1 we assume INDX is last chunk and we locate it by linear scan from start if needed.
        # Practical approach: scan forward once; still fast for research-scale files.
        f.seek(0)
        last_indx_offset = None
        while True:
            here = f.tell()
            hdr = _read_chunk_header(f)
            if hdr is None:
                break
            ctype, payload_size, hdr_offset = hdr
            # skip payload
            f.seek(payload_size, io.SEEK_CUR)
            if ctype == b"INDX":
                last_indx_offset = hdr_offset

        if last_indx_offset is None:
            raise IOError("Missing INDX chunk")

        # Read INDX payload
        f.seek(last_indx_offset)
        ctype, payload_size, _ = _read_chunk_header(f)  # consumes header
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
            e = IndexEntry(int(frame_index), float(timestamp), int(offset), int(chunk_size))
            entries.append(e)

        self.index = entries
        self._index_by_frame = {e.frame_index: e for e in entries}

    def get_frame(self, frame_index: int) -> Tuple[np.ndarray, np.ndarray, float]:
        """
        Returns (vertices (N,3) float32, faces (M,3) uint32, timestamp)
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

        if len(payload) < KFRM_FIXED_STRUCT.size:
            raise IOError("Invalid KFRM payload")
        fi, ts, nv, nf = KFRM_FIXED_STRUCT.unpack(payload[:KFRM_FIXED_STRUCT.size])
        if int(fi) != int(frame_index):
            # not fatal, but indicates corrupted index
            raise IOError("Index/frame mismatch")

        pos = KFRM_FIXED_STRUCT.size
        v_bytes = int(nv) * 3 * 4
        f_bytes = int(nf) * 3 * 4

        if pos + v_bytes + f_bytes > len(payload):
            raise IOError("Invalid KFRM data sizes")

        vertices = np.frombuffer(payload, dtype=np.float32, count=int(nv) * 3, offset=pos).reshape((int(nv), 3))
        pos += v_bytes
        faces = np.frombuffer(payload, dtype=np.uint32, count=int(nf) * 3, offset=pos).reshape((int(nf), 3))

        return vertices.copy(), faces.copy(), float(ts)

    def __len__(self) -> int:
        return len(self.index)

    def iter_frames(self) -> Iterator[Tuple[int, float]]:
        """Yields (frame_index, timestamp) for available frames."""
        for e in self.index:
            yield e.frame_index, e.timestamp
