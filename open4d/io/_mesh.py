"""Readers and writers for per-frame mesh files: `.obj`, `.ply`, and the rest.

Each reader returns `(positions, triangles, colors)` for a single frame. A
frame with no faces comes back with zero triangles.

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

def _empty_triangles() -> np.ndarray:
    return np.empty((0, 3), dtype=np.uint32)


class UnsupportedPlyVariant(ValueError):
    """A valid PLY feature requires a more complete optional reader."""

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
                    {
                        "name": fields[2].decode(),
                        "list": False,
                        "type": fields[1].decode(),
                    }
                )
        elif keyword == b"end_header":
            return ply_format, elements, stream.tell()


def _ply_dtype(name: str) -> np.dtype:
    if name not in _PLY_DTYPES:
        raise UnsupportedPlyVariant(f"unsupported PLY property type {name!r}")
    return np.dtype(_PLY_DTYPES[name]).newbyteorder("<")


def _ascii_face_indices(
    row: list[bytes], properties: list[dict[str, Any]], path: Path
) -> list[int]:
    """Read the first face list while consuming every property in order."""
    if len(properties) == 1 and properties[0]["list"] and row:
        declared = int(row[0])
        if len(row) != declared + 1:
            raise ValueError(
                f"{path} face declares {declared} indices "
                f"but contains {len(row) - 1}"
            )

    cursor = 0
    selected: list[int] | None = None
    for prop in properties:
        if cursor >= len(row):
            raise ValueError(f"{path} contains a truncated ASCII face")
        if not prop["list"]:
            cursor += 1
            continue

        value_count = int(row[cursor])
        cursor += 1
        if value_count < 0 or cursor + value_count > len(row):
            available = max(0, len(row) - cursor)
            raise ValueError(
                f"{path} face declares {value_count} values for "
                f"{prop['name']!r} but contains {available}"
            )
        values = row[cursor : cursor + value_count]
        cursor += value_count
        if selected is None:
            selected = [int(value) for value in values]

    if cursor != len(row):
        raise ValueError(
            f"{path} face contains {len(row) - cursor} undeclared value(s)"
        )
    assert selected is not None
    return selected


def _read_exact(stream: Any, byte_count: int, path: Path) -> bytes:
    data = stream.read(byte_count)
    if len(data) != byte_count:
        raise ValueError(f"{path} contains a truncated binary PLY element")
    return data


def _binary_face_indices(
    stream: Any, properties: list[dict[str, Any]], path: Path
) -> np.ndarray:
    """Read the first face list while consuming every binary property."""
    selected: np.ndarray | None = None
    for prop in properties:
        if not prop["list"]:
            dtype = _ply_dtype(prop["type"])
            _read_exact(stream, dtype.itemsize, path)
            continue

        count_dtype = _ply_dtype(prop["count_type"])
        value_count = int(
            np.frombuffer(
                _read_exact(stream, count_dtype.itemsize, path),
                dtype=count_dtype,
                count=1,
            )[0]
        )
        if value_count < 0:
            raise ValueError(
                f"{path} face declares a negative list count for {prop['name']!r}"
            )
        value_dtype = _ply_dtype(prop["value_type"])
        values = np.frombuffer(
            _read_exact(stream, value_dtype.itemsize * value_count, path),
            dtype=value_dtype,
            count=value_count,
        )
        if selected is None:
            selected = values

    assert selected is not None
    return selected


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
            raise UnsupportedPlyVariant(
                f"{path} is big-endian PLY; install trimesh to read it "
                "(python -m pip install 'open4d[tools]')"
            )
        ascii_mode = ply_format == "ascii"

        positions = np.empty((0, 3), dtype=np.float32)
        colors: np.ndarray | None = None
        triangles = _empty_triangles()

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
                    color_names = ["red", "green", "blue"]
                    if "alpha" in columns:
                        color_names.append("alpha")
                    color_values = np.stack(
                        [columns[name] for name in color_names], axis=-1
                    )
                    color_properties = {
                        prop["name"]: prop for prop in properties if not prop["list"]
                    }
                    color_kinds = {
                        np.issubdtype(
                            _ply_dtype(color_properties[name]["type"]), np.integer
                        )
                        for name in color_names
                    }
                    if len(color_kinds) != 1:
                        raise UnsupportedPlyVariant(
                            f"{path} mixes integer and floating-point color properties"
                        )
                    if color_kinds == {True}:
                        if np.any(color_values < 0) or np.any(color_values > 255):
                            raise ValueError(
                                f"{path} integer colors must be in [0, 255]"
                            )
                        colors = color_values.astype(np.uint8)
                    else:
                        colors = color_values.astype(np.float32)

            elif name == "face":
                corners: list[tuple[int, int, int]] = []
                list_properties = [prop for prop in properties if prop["list"]]
                if not list_properties:
                    raise UnsupportedPlyVariant(
                        f"{path} face elements must contain a list property"
                    )

                if ascii_mode:
                    for row in rows:
                        indices = _ascii_face_indices(row, properties, path)
                        for corner in range(1, len(indices) - 1):
                            corners.append(
                                (indices[0], indices[corner], indices[corner + 1])
                            )
                else:
                    for _ in range(count):
                        indices = _binary_face_indices(stream, properties, path)
                        for corner in range(1, len(indices) - 1):
                            corners.append(
                                (
                                    int(indices[0]),
                                    int(indices[corner]),
                                    int(indices[corner + 1]),
                                )
                            )
                triangles = np.array(corners, dtype=np.uint32).reshape(-1, 3)

            elif not ascii_mode and rows is None:
                raise UnsupportedPlyVariant(
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
    color_names = []
    if colors is not None:
        colors = np.asarray(colors, dtype=np.float32)
        color_names = ["red", "green", "blue"]
        if colors.shape[1] == 4:
            color_names.append("alpha")
        fields += [(name, "<f4") for name in color_names]

    vertices = np.empty(len(positions), dtype=np.dtype(fields))
    vertices["x"], vertices["y"], vertices["z"] = positions.T
    if colors is not None:
        for column, name in enumerate(color_names):
            vertices[name] = colors[:, column]

    header = [
        "ply",
        "format binary_little_endian 1.0",
        f"element vertex {len(positions)}",
        "property float x",
        "property float y",
        "property float z",
    ]
    if colors is not None:
        header += [f"property float {name}" for name in color_names]
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
def read_with_trimesh(
    path: Path,
) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read any trimesh-supported mesh file, merging multi-part scenes."""
    try:
        import trimesh
    except ImportError:
        raise ModuleNotFoundError(
            f"Reading {path.suffix} frames needs trimesh. Install it with: "
            "python -m pip install 'open4d[tools]' (.obj and .ply work without it.)"
        ) from None

    loaded = trimesh.load(path, process=False)
    if isinstance(loaded, trimesh.Scene):
        # Bake node transforms and repeated instances before concatenation.
        loaded = (
            loaded.to_mesh()
            if hasattr(loaded, "to_mesh")
            else loaded.dump(concatenate=True)
        )
    if not isinstance(loaded, trimesh.Trimesh):
        raise ValueError(f"{path} contains no mesh geometry")

    positions = np.asarray(loaded.vertices, dtype=np.float32)
    faces = getattr(loaded, "faces", None)
    triangles = (
        _empty_triangles()
        if faces is None or len(faces) == 0
        else np.asarray(faces, dtype=np.uint32)
    )
    colors = None
    visual = getattr(loaded, "visual", None)
    if getattr(visual, "kind", None) == "vertex":
        colors = np.asarray(visual.vertex_colors)
        valid_shape = (
            colors.ndim == 2
            and colors.shape[0] == len(positions)
            and colors.shape[1] in (3, 4)
        )
        if not valid_shape:
            raise ValueError(f"{path} has invalid Trimesh vertex colors {colors.shape}")
    return positions, triangles, colors


def write_with_trimesh(
    path: Path,
    positions: np.ndarray,
    triangles: np.ndarray,
    colors: np.ndarray | None = None,
) -> Path:
    """Write one frame through the optional trimesh exporter."""
    try:
        import trimesh
    except ImportError:
        raise ModuleNotFoundError(
            f"Writing {path.suffix} frames needs trimesh. Install it with: "
            "python -m pip install 'open4d[tools]'"
        ) from None
    kwargs = {}
    if colors is not None:
        kwargs["vertex_colors"] = np.rint(np.clip(colors, 0, 1) * 255).astype(np.uint8)
    mesh = trimesh.Trimesh(
        vertices=positions, faces=triangles, process=False, **kwargs
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".gltf":
        payload = trimesh.exchange.gltf.export_gltf(
            mesh.scene(), embed_buffers=True
        )["model.gltf"]
        path.write_bytes(payload)
    else:
        mesh.export(path)
    return path
