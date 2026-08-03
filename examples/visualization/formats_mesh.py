"""Readers and writers for per-frame mesh files: `.obj`, `.ply`, and the rest.

Each reader returns `(positions, triangles, colors)` for a single frame, which
is the contract `frame_sources.FRAME_READERS` dispatches on. A frame with no
faces comes back with zero triangles.

`.obj` and `.ply` are parsed here with NumPy, so they need nothing beyond the
base install. Other formats (`.stl`, `.off`, `.glb`, `.gltf`) are delegated to
trimesh (`[tools]` extra), which is also the fallback for PLY variants the
built-in reader does not cover.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

BUILTIN_SUFFIXES = (".obj", ".ply")
TRIMESH_SUFFIXES = (".off", ".stl", ".glb", ".gltf")
SUFFIXES = BUILTIN_SUFFIXES + TRIMESH_SUFFIXES

NO_TRIANGLES = np.empty((0, 3), dtype=np.uint32)

# ----------------------------
# Wavefront OBJ
# ----------------------------
def read_obj(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read positions, triangles, and colors from a Wavefront `.obj` file.

    Handles `v` and `f` lines, `v/vt/vn` corner references, negative
    (relative) indices, and polygons, which are fan-triangulated. Materials,
    normals, groups, and texture coordinates are ignored. `.obj` has no
    standard vertex color, so colors are always None.
    """
    positions: list[tuple[str, str, str]] = []
    corners: list[tuple[int, int, int]] = []

    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if line.startswith("v "):
                fields = line.split()
                positions.append((fields[1], fields[2], fields[3]))
            elif line.startswith("f "):
                # "f 1/2/3 4//5 6" -> the vertex index is the first field.
                indices = [
                    int(field.partition("/")[0]) for field in line.split()[1:]
                ]
                # Negative indices count back from the vertices seen so far.
                count = len(positions)
                resolved = [
                    index - 1 if index > 0 else count + index for index in indices
                ]
                for corner in range(1, len(resolved) - 1):
                    corners.append(
                        (resolved[0], resolved[corner], resolved[corner + 1])
                    )

    if not positions:
        raise ValueError(f"{path} contains no vertices")
    return (
        np.array(positions, dtype=np.float32),
        np.array(corners, dtype=np.uint32).reshape(-1, 3),
        None,
    )


def write_obj(path: Path, positions: np.ndarray, triangles: np.ndarray) -> Path:
    """Write one mesh frame as a Wavefront `.obj` file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as stream:
        stream.write(f"# {len(positions)} vertices, {len(triangles)} triangles\n")
        np.savetxt(stream, positions, fmt="v %.6g %.6g %.6g")
        # OBJ indices are 1-based.
        np.savetxt(stream, np.asarray(triangles) + 1, fmt="f %d %d %d")
    return path


# ----------------------------
# PLY
# ----------------------------
# PLY property type -> NumPy dtype, covering both the short and long spellings.
_PLY_DTYPES = {
    "char": "i1", "int8": "i1",
    "uchar": "u1", "uint8": "u1",
    "short": "i2", "int16": "i2",
    "ushort": "u2", "uint16": "u2",
    "int": "i4", "int32": "i4",
    "uint": "u4", "uint32": "u4",
    "float": "f4", "float32": "f4",
    "double": "f8", "float64": "f8",
}


def _parse_ply_header(stream) -> tuple[str, list[dict[str, Any]], int]:
    """Return the format, the element list, and the header's byte length."""
    if stream.readline().strip() != b"ply":
        raise ValueError("not a PLY file (missing magic)")

    ply_format = ""
    elements: list[dict[str, Any]] = []
    while True:
        raw = stream.readline()
        if not raw:
            raise ValueError("truncated PLY header")
        fields = raw.split()
        if not fields:
            continue
        keyword = fields[0]
        if keyword == b"format":
            ply_format = fields[1].decode()
        elif keyword == b"element":
            elements.append(
                {"name": fields[1].decode(), "count": int(fields[2]), "properties": []}
            )
        elif keyword == b"property":
            if not elements:
                raise ValueError("PLY property outside of an element")
            if fields[1] == b"list":
                elements[-1]["properties"].append(
                    {
                        "name": fields[4].decode(),
                        "list": True,
                        "count_type": fields[2].decode(),
                        "value_type": fields[3].decode(),
                    }
                )
            else:
                elements[-1]["properties"].append(
                    {"name": fields[2].decode(), "list": False, "type": fields[1].decode()}
                )
        elif keyword == b"end_header":
            return ply_format, elements, stream.tell()


def _ply_dtype(name: str) -> np.dtype:
    if name not in _PLY_DTYPES:
        raise ValueError(f"unsupported PLY property type {name!r}")
    return np.dtype(_PLY_DTYPES[name])


def read_ply(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read positions, triangles, and colors from a `.ply` file.

    Supports `ascii` and `binary_little_endian`, the two formats the tools in
    this repository produce. Vertices need `x`, `y`, `z`; `red`/`green`/`blue`
    are picked up when present. Faces come from the first list property of a
    `face` element, and polygons are fan-triangulated. Point clouds — a `.ply`
    with no `face` element — return zero triangles.
    """
    with open(path, "rb") as stream:
        ply_format, elements, _offset = _parse_ply_header(stream)
        if ply_format == "binary_big_endian":
            raise ValueError(
                f"{path} is big-endian PLY; install trimesh to read it "
                "(python -m pip install -e '.[tools]')"
            )
        ascii_mode = ply_format == "ascii"

        positions = NO_TRIANGLES.astype(np.float32)
        colors: np.ndarray | None = None
        triangles = NO_TRIANGLES

        for element in elements:
            name = element["name"]
            count = element["count"]
            properties = element["properties"]

            if ascii_mode:
                rows = [stream.readline().split() for _ in range(count)]
            elif not any(prop["list"] for prop in properties):
                # Fixed-width binary element: one structured read.
                dtype = np.dtype(
                    [(prop["name"], _ply_dtype(prop["type"])) for prop in properties]
                )
                rows = np.frombuffer(stream.read(dtype.itemsize * count), dtype=dtype)
            else:
                rows = None  # read per-row below

            if name == "vertex":
                if ascii_mode:
                    order = [prop["name"] for prop in properties]
                    table = np.array(rows, dtype=np.float64)
                    columns = {key: table[:, i] for i, key in enumerate(order)}
                else:
                    columns = {key: rows[key] for key in rows.dtype.names}
                missing = {"x", "y", "z"} - set(columns)
                if missing:
                    raise ValueError(f"{path} vertices lack {sorted(missing)}")
                positions = np.stack(
                    [columns["x"], columns["y"], columns["z"]], axis=-1
                ).astype(np.float32)
                if {"red", "green", "blue"} <= set(columns):
                    colors = np.stack(
                        [columns["red"], columns["green"], columns["blue"]], axis=-1
                    ).astype(np.uint8)

            elif name == "face":
                corners: list[tuple[int, int, int]] = []
                list_property = next(
                    (prop for prop in properties if prop["list"]), None
                )
                if list_property is None:
                    raise ValueError(f"{path} face element has no list property")

                if ascii_mode:
                    for row in rows:
                        indices = [int(value) for value in row[1:]]
                        for corner in range(1, len(indices) - 1):
                            corners.append(
                                (indices[0], indices[corner], indices[corner + 1])
                            )
                else:
                    count_dtype = _ply_dtype(list_property["count_type"])
                    value_dtype = _ply_dtype(list_property["value_type"])
                    for _ in range(count):
                        vertices_in_face = int(
                            np.frombuffer(
                                stream.read(count_dtype.itemsize), dtype=count_dtype
                            )[0]
                        )
                        indices = np.frombuffer(
                            stream.read(value_dtype.itemsize * vertices_in_face),
                            dtype=value_dtype,
                        )
                        for corner in range(1, vertices_in_face - 1):
                            corners.append(
                                (
                                    int(indices[0]),
                                    int(indices[corner]),
                                    int(indices[corner + 1]),
                                )
                            )
                triangles = np.array(corners, dtype=np.uint32).reshape(-1, 3)

            elif not ascii_mode and rows is None:
                raise ValueError(
                    f"{path} has an unsupported binary list element {name!r}"
                )

    if len(positions) == 0:
        raise ValueError(f"{path} contains no vertices")
    return positions, triangles, colors


def write_ply(
    path: Path,
    positions: np.ndarray,
    triangles: np.ndarray | None = None,
    colors: np.ndarray | None = None,
) -> Path:
    """Write one frame as a binary little-endian `.ply` file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    positions = np.asarray(positions, dtype=np.float32)
    fields = [("x", "<f4"), ("y", "<f4"), ("z", "<f4")]
    if colors is not None:
        fields += [("red", "u1"), ("green", "u1"), ("blue", "u1")]

    vertices = np.empty(len(positions), dtype=np.dtype(fields))
    vertices["x"], vertices["y"], vertices["z"] = positions.T
    if colors is not None:
        colors = np.asarray(colors, dtype=np.uint8)
        vertices["red"], vertices["green"], vertices["blue"] = colors.T

    header = [
        "ply",
        "format binary_little_endian 1.0",
        f"element vertex {len(positions)}",
        "property float x",
        "property float y",
        "property float z",
    ]
    if colors is not None:
        header += [
            "property uchar red",
            "property uchar green",
            "property uchar blue",
        ]
    if triangles is not None and len(triangles) > 0:
        header += [
            f"element face {len(triangles)}",
            "property list uchar int vertex_indices",
        ]
    header.append("end_header")

    with open(path, "wb") as stream:
        stream.write(("\n".join(header) + "\n").encode("ascii"))
        stream.write(vertices.tobytes())
        if triangles is not None and len(triangles) > 0:
            faces = np.empty(
                len(triangles),
                dtype=np.dtype([("count", "u1"), ("indices", "<i4", 3)]),
            )
            faces["count"] = 3
            faces["indices"] = np.asarray(triangles, dtype=np.int32)
            stream.write(faces.tobytes())
    return path


# ----------------------------
# Per-frame mesh files
# ----------------------------
def read_with_trimesh(path: Path) -> tuple[np.ndarray, np.ndarray, None]:
    """Read any trimesh-supported mesh file, merging multi-part scenes."""
    try:
        import trimesh
    except ImportError:
        raise SystemExit(
            f"Reading {path.suffix} frames needs trimesh.\n"
            "Install it with: python -m pip install -e '.[tools]'\n"
            "(.obj and .ply folders work without it.)"
        ) from None

    loaded = trimesh.load(path, process=False)
    if isinstance(loaded, trimesh.Scene):
        parts = [
            part
            for part in loaded.geometry.values()
            if isinstance(part, trimesh.Trimesh)
        ]
        if not parts:
            raise ValueError(f"{path} contains no mesh geometry")
        loaded = trimesh.util.concatenate(parts)

    positions = np.asarray(loaded.vertices, dtype=np.float32)
    faces = getattr(loaded, "faces", None)
    triangles = (
        NO_TRIANGLES
        if faces is None or len(faces) == 0
        else np.asarray(faces, dtype=np.uint32)
    )
    return positions, triangles, None

