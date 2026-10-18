"""Read and write texture images as PNG with NumPy and zlib alone.

Non-interlaced 8- and 16-bit grayscale, gray+alpha, RGB, RGBA and 8-bit palette
images are decoded here, which covers what mesh tools write. Other PNG variants
and other image formats (JPEG, for example) are delegated to Pillow when it is
installed. Writing always produces a non-interlaced 8- or 16-bit PNG, so a
texture written here reads back bit-exactly.
"""

from __future__ import annotations

from pathlib import Path
import struct
import zlib

import numpy as np

_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
_COLOR_TYPES = {1: 0, 2: 4, 3: 2, 4: 6}
_MAX_PIXELS = 1 << 28


def _chunks(data: bytes, path: Path):
    if not data.startswith(_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file")
    offset = len(_SIGNATURE)
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"{path} contains a truncated PNG chunk")
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if len(body) != length or offset + 12 + length > len(data):
            raise ValueError(f"{path} contains a truncated PNG chunk")
        offset += 12 + length
        yield kind, body
        if kind == b"IEND":
            return
    raise ValueError(f"{path} has no PNG end chunk")


def _unfilter(raw: np.ndarray, height: int, stride: int, step: int, path: Path) -> np.ndarray:
    if raw.size != height * (stride + 1):
        raise ValueError(f"{path} has PNG image data of the wrong size")
    rows = raw.reshape(height, stride + 1)
    output = np.zeros((height, stride), dtype=np.uint8)
    previous = np.zeros(stride, dtype=np.uint8)
    for row in range(height):
        kind, line = rows[row, 0], rows[row, 1:]
        if kind == 0:
            current = line
        elif kind == 1:
            current = (np.cumsum(line.reshape(-1, step), axis=0, dtype=np.uint64) & 0xFF)
            current = current.astype(np.uint8).reshape(-1)
        elif kind == 2:
            current = line + previous
        elif kind in (3, 4):
            # Average and Paeth predict from reconstructed bytes on the left, so
            # they are inherently sequential within a row.
            values, above, current = line.tolist(), previous.tolist(), [0] * stride
            for index in range(stride):
                left = current[index - step] if index >= step else 0
                if kind == 3:
                    predictor = (left + above[index]) >> 1
                else:
                    upper_left = above[index - step] if index >= step else 0
                    estimate = left + above[index] - upper_left
                    left_distance = abs(estimate - left)
                    above_distance = abs(estimate - above[index])
                    corner_distance = abs(estimate - upper_left)
                    if left_distance <= above_distance and left_distance <= corner_distance:
                        predictor = left
                    elif above_distance <= corner_distance:
                        predictor = above[index]
                    else:
                        predictor = upper_left
                current[index] = (values[index] + predictor) & 0xFF
            current = np.asarray(current, dtype=np.uint8)
        else:
            raise ValueError(f"{path} uses unknown PNG filter type {kind}")
        output[row] = current
        previous = output[row]
    return output


def _read_builtin(path: Path) -> np.ndarray | None:
    data = path.read_bytes()
    header = palette = transparency = None
    compressed = []
    for kind, body in _chunks(data, path):
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = np.frombuffer(body, dtype=np.uint8).reshape(-1, 3)
        elif kind == b"tRNS":
            transparency = np.frombuffer(body, dtype=np.uint8)
        elif kind == b"IDAT":
            compressed.append(body)
    if header is None:
        raise ValueError(f"{path} has no PNG header")
    width, height, depth, color, _, _, interlace = header
    if (interlace or color not in _CHANNELS or depth not in (8, 16)
            or (color == 3 and (depth != 8 or palette is None))):
        return None
    if not width or not height or width * height > _MAX_PIXELS:
        raise ValueError(f"{path} PNG dimensions are outside limits")
    channels = _CHANNELS[color]
    step = channels * depth // 8
    raw = np.frombuffer(zlib.decompress(b"".join(compressed)), dtype=np.uint8)
    rows = _unfilter(raw, height, width * step, step, path)
    if depth == 16:
        pixels = rows.view(">u2").astype(np.uint16).reshape(height, width, channels)
    else:
        pixels = rows.reshape(height, width, channels)
    if color == 3:
        indices = pixels[:, :, 0]
        if indices.max(initial=0) >= len(palette):
            raise ValueError(f"{path} references a missing PNG palette entry")
        if transparency is not None:
            alpha = np.full(len(palette), 255, dtype=np.uint8)
            alpha[:len(transparency)] = transparency[:len(palette)]
            pixels = np.concatenate((palette, alpha[:, None]), axis=1)[indices]
        else:
            pixels = palette[indices]
    return np.ascontiguousarray(pixels)


def _png_depth(path: Path) -> int | None:
    with path.open("rb") as stream:
        prefix = stream.read(33)
    if not prefix.startswith(_SIGNATURE) or prefix[12:16] != b"IHDR":
        return None
    return prefix[24]


def read_image(path: str | Path) -> np.ndarray:
    """Read an image as ``(height, width, channels)`` uint8 or uint16 pixels.

    8-bit images use Pillow when it is installed, because the pure-Python PNG
    filters are slow on large textures. 16-bit PNG always uses the built-in
    reader, since Pillow narrows 16-bit color to 8 bits.
    """
    path = Path(path)
    try:
        from PIL import Image
    except ImportError:
        Image = None
    if path.suffix.lower() == ".png" and (Image is None or _png_depth(path) == 16):
        pixels = _read_builtin(path)
        if pixels is not None:
            return pixels
    if Image is None:
        raise ModuleNotFoundError(
            f"Reading {path.name} needs Pillow (python -m pip install pillow); "
            "the built-in reader covers non-interlaced 8/16-bit PNG only"
        )
    with Image.open(path) as image:
        if image.mode in ("P", "PA", "CMYK", "YCbCr", "LAB", "HSV", "1"):
            image = image.convert("RGBA" if "transparency" in image.info or image.mode == "PA" else "RGB")
        pixels = np.asarray(image)
    if pixels.ndim == 2:
        pixels = pixels[:, :, None]
    if pixels.dtype not in (np.uint8, np.uint16):
        if pixels.dtype.kind in "iu" and pixels.size and 0 <= pixels.min() and pixels.max() <= 65535:
            pixels = pixels.astype(np.uint16)
        else:
            raise ValueError(f"{path} has unsupported image sample type {pixels.dtype}")
    return np.ascontiguousarray(pixels)


def write_png(path: str | Path, pixels: np.ndarray) -> Path:
    """Write uint8 or uint16 ``(H, W, C)`` pixels losslessly as PNG."""
    path = Path(path)
    pixels = np.asarray(pixels)
    if pixels.ndim == 2:
        pixels = pixels[:, :, None]
    if pixels.ndim != 3 or pixels.shape[2] not in _COLOR_TYPES:
        raise ValueError(f"PNG pixels must have shape (H, W, 1..4); got {pixels.shape}")
    if pixels.dtype not in (np.uint8, np.uint16):
        raise TypeError(f"PNG stores uint8 or uint16 samples; got {pixels.dtype}")
    height, width, channels = pixels.shape
    depth = 8 * pixels.dtype.itemsize
    rows = np.ascontiguousarray(pixels.astype(">u2") if depth == 16 else pixels)
    rows = rows.view(np.uint8).reshape(height, -1)
    filtered = np.concatenate((np.zeros((height, 1), dtype=np.uint8), rows), axis=1)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, depth, _COLOR_TYPES[channels], 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(_SIGNATURE + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(filtered.tobytes(), 6))
                     + chunk(b"IEND", b""))
    return path
