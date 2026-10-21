"""Readers and writers for per-frame mesh files: `.obj`, `.ply`, and the rest.

The legacy readers return `(positions, triangles, colors)` for a single frame.
`read_obj_with_uv` additionally preserves texture coordinates, and
`read_obj_with_texture` also loads the `map_Kd` images its materials name. A
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
def read_obj(path: Path, *, dtype=np.float32) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read positions, triangles, and colors from a Wavefront `.obj` file.

    Compatibility reader returning three values. For UV-preserving loading,
    use `read_obj_with_uv` or the public `open4d.load` API. Materials, normals,
    groups and texture images are not loaded. Colors are always None.
    """
    positions, triangles, colors, _ = _read_obj_arrays(path, dtype=dtype, preserve_uv=False)
    return positions, triangles, colors


def read_obj_with_uv(path: Path, *, dtype=np.float32):
    """Read OBJ arrays plus 2D UVs without following material/image paths.

    Vertex and UV indices are independent, including negative relative indices.
    Polygon fans retain each corner's UV. A one-to-one vertex/UV mapping uses
    per-vertex UV storage; seams use per-triangle-corner storage. Faces that mix
    assigned and missing UVs are rejected rather than silently losing texture.
    """
    return _read_obj_arrays(path, dtype=dtype, preserve_uv=True)


def read_obj_with_texture(path: Path, *, dtype=np.float32):
    """Read OBJ arrays, UVs and the texture images its materials reference.

    Returns ``(positions, triangles, colors, texture_coordinates, texture)``.
    ``mtllib`` and ``map_Kd`` paths are resolved relative to the file that
    names them and must stay inside that file's directory; absolute or escaping
    paths are rejected rather than followed. Faces take the material of the
    preceding ``usemtl``. A mesh with no materials returns ``texture=None``.
    """
    from open4d.core import MeshTexture, TextureImage

    positions, triangles, colors, uv, materials, libraries = _read_obj_arrays(
        path, dtype=dtype, preserve_uv=True, materials=True
    )
    if not libraries and not any(materials):
        return positions, triangles, colors, uv, None
    maps: dict[str, Path] = {}
    for library in libraries:
        maps.update(_read_mtl(_contained(path.parent, library, path)))
    used = list(dict.fromkeys(name for name in materials if name is not None))
    if not used:
        if not maps:
            return positions, triangles, colors, uv, None
        if len(maps) != 1:
            raise ValueError(f"{path} defines several textured materials but assigns none with usemtl")
        used = list(maps)
        materials = [used[0]] * len(materials)
    if any(name is None for name in materials):
        raise ValueError(f"{path} mixes faces with and without usemtl materials")
    missing = [name for name in used if name not in maps]
    if missing:
        raise ValueError(f"{path} uses materials without a map_Kd texture: {', '.join(missing)}")
    if uv is None:
        raise ValueError(f"{path} assigns texture materials but has no texture coordinates")
    images = tuple(
        TextureImage(_read_texture_image(maps[name]), name=maps[name].name) for name in used
    )
    index = {name: number for number, name in enumerate(used)}
    ids = np.array([index[name] for name in materials], dtype=np.int32)
    texture = MeshTexture(images, None if len(used) == 1 else ids,
                          tuple(_material_name(name) for name in used))
    return positions, triangles, colors, uv, texture


def _material_name(name: str) -> str:
    return "".join("_" if character.isspace() else character for character in name)


def _contained(directory: Path, reference: str, owner: Path) -> Path:
    relative = Path(reference.replace(chr(92), "/"))
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"{owner} references {reference!r} outside its directory; "
                         "copy the file next to it and use a relative name")
    resolved = (directory / relative).resolve()
    if directory.resolve() not in resolved.parents:
        raise ValueError(f"{owner} references {reference!r} outside its directory")
    if not resolved.is_file():
        raise FileNotFoundError(f"{owner} references missing file {reference!r}")
    return resolved


def _read_mtl(path: Path) -> dict[str, Path]:
    maps: dict[str, Path] = {}
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            fields = line.partition("#")[0].split()
            if not fields:
                continue
            if fields[0] == "newmtl" and len(fields) >= 2:
                current = " ".join(fields[1:])
            elif fields[0] == "map_Kd" and current is not None and len(fields) >= 2:
                # Options such as -s or -o would transform the lookup; refuse
                # them rather than carry the image without its transform.
                if any(field.startswith("-") for field in fields[1:-1]):
                    raise ValueError(f"{path} uses map_Kd options, which are not supported")
                maps[current] = _contained(path.parent, fields[-1], path)
    return maps


def _read_texture_image(path: Path) -> np.ndarray:
    from ._png import read_image

    return read_image(path)


def _read_obj_arrays(path: Path, *, dtype, preserve_uv, materials=False):
    positions: list[tuple[str, str, str]] = []
    corners: list[tuple[int, int, int]] = []
    coordinates: list[tuple[float, float]] = []
    uv_corners: list[tuple[int | None, int | None, int | None]] = []
    face_materials: list[str | None] = []
    libraries: list[str] = []
    material = None

    def index(value, count, label):
        try:
            result = int(value)
        except ValueError as error:
            raise ValueError(f"{path} contains invalid OBJ {label} index {value!r}") from error
        if result == 0 or result < -count:
            raise ValueError(f"{path} contains invalid OBJ {label} index {result}")
        return result - 1 if result > 0 else count + result

    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            fields = line.partition("#")[0].split()
            if not fields:
                continue
            if fields[0] == "v":
                if len(fields) < 4:
                    raise ValueError(f"{path} contains an incomplete vertex")
                positions.append((fields[1], fields[2], fields[3]))
            elif fields[0] == "mtllib" and materials and len(fields) >= 2:
                libraries.extend(fields[1:])
            elif fields[0] == "usemtl" and materials:
                material = " ".join(fields[1:]) or None
            elif fields[0] == "vt" and preserve_uv:
                if not 2 <= len(fields) <= 4:
                    raise ValueError(f"{path} contains an invalid OBJ texture coordinate")
                try:
                    values = tuple(map(float, fields[1:]))
                except ValueError as error:
                    raise ValueError(f"{path} contains invalid OBJ texture coordinates") from error
                if not all(np.isfinite(value) for value in values):
                    raise ValueError(f"{path} contains nonfinite OBJ texture coordinates")
                if len(values) == 3 and values[2] != 0:
                    raise ValueError(f"{path} uses 3D OBJ texture coordinates; convert them to 2D UVs")
                coordinates.append((values[0], values[1] if len(values) > 1 else 0.0))
            elif fields[0] == "f":
                if len(fields) < 4:
                    raise ValueError(f"{path} contains a face with fewer than three vertices")
                resolved, uv_indices = [], []
                for field in fields[1:]:
                    components = field.split("/")
                    if (preserve_uv and len(components) > 3) or not components[0]:
                        raise ValueError(f"{path} contains invalid OBJ corner reference {field!r}")
                    resolved.append(index(components[0], len(positions), "vertex"))
                    uv_indices.append(index(components[1], len(coordinates), "texture")
                                      if preserve_uv and len(components) > 1 and components[1] else None)
                for corner in range(1, len(resolved) - 1):
                    corners.append(
                        (resolved[0], resolved[corner], resolved[corner + 1])
                    )
                    uv_corners.append((uv_indices[0], uv_indices[corner], uv_indices[corner+1]))
                    face_materials.append(material)

    if not positions:
        raise ValueError(f"{path} contains no vertices")
    if corners and max(max(face) for face in corners) >= len(positions):
        raise ValueError(f"{path} references vertex {max(max(face) for face in corners) + 1} "
                         f"but declares {len(positions)}")
    vertices = np.array(positions, dtype=dtype)
    if not np.isfinite(vertices).all():
        raise ValueError(f"{path} contains nonfinite vertex coordinates")
    triangles = np.array(corners, dtype=np.uint32).reshape(-1, 3)
    texture_coordinates = None
    if any(uv is not None for face in uv_corners for uv in face):
        if any(uv is None for face in uv_corners for uv in face):
            raise ValueError(f"{path} mixes assigned and missing OBJ texture coordinates; "
                             "assign UVs to every face corner before loading")
        references = np.array(uv_corners, dtype=np.int64)
        if references.max() >= len(coordinates):
            raise ValueError(f"{path} references texture coordinate {references.max()+1} "
                             f"but declares {len(coordinates)}")
        with np.errstate(over="ignore"):
            uv_values = np.array(coordinates, dtype=dtype)
        if not np.isfinite(uv_values).all():
            raise ValueError(f"{path} contains OBJ texture coordinates outside the requested dtype")
        if len(coordinates) == len(vertices) and np.array_equal(references, triangles):
            texture_coordinates = uv_values
        else:
            # Preserve position topology: separate UV references need not split
            # vertices, and a shared position may carry different seam UVs.
            texture_coordinates = uv_values[references]
    if materials:
        return vertices, triangles, None, texture_coordinates, face_materials, libraries
    return vertices, triangles, None, texture_coordinates


def write_obj(
    path: Path,
    positions: np.ndarray,
    triangles: np.ndarray,
    texture_coordinates: np.ndarray | None = None,
    *,
    texture=None,
    uv_format: str | None = None,
) -> Path:
    """Write one mesh frame as a Wavefront `.obj` file.

    Per-vertex ``(N, 2)`` or per-corner ``(M, 3, 2)`` texture coordinates are
    written as ``vt`` records with independent UV indices, so seams keep their
    corner UVs. A `MeshTexture` additionally writes ``<stem>.mtl`` and one PNG
    per material next to the OBJ (``<stem>_<material>.png``) and assigns faces
    with ``usemtl``. ``uv_format`` overrides the ``vt`` number format, for
    example ``"%d"`` for tools that read integer texture coordinates.
    """
    path = Path(path)
    positions = np.asarray(positions)
    triangles = np.asarray(triangles).reshape(-1, 3)
    path.parent.mkdir(parents=True, exist_ok=True)
    uv_rows = uv_indices = None
    if texture_coordinates is not None:
        texture_coordinates = np.asarray(texture_coordinates)
        if texture_coordinates.ndim == 2:
            uv_rows, uv_indices = texture_coordinates, triangles
        else:
            uv_rows, inverse = np.unique(
                texture_coordinates.reshape(-1, 2), axis=0, return_inverse=True
            )
            uv_indices = inverse.reshape(-1, 3)
    if texture is not None and uv_rows is None:
        raise ValueError("an OBJ texture needs texture coordinates")
    with open(path, "w", encoding="utf-8") as stream:
        stream.write(f"# {len(positions)} vertices, {len(triangles)} triangles\n")
        if texture is not None:
            stream.write(f"mtllib {path.stem}.mtl\n")
        precision = 17 if positions.dtype.itemsize > 4 else 9
        np.savetxt(stream, positions, fmt="v " + " ".join([f"%.{precision}g"] * 3))
        if uv_rows is None:
            # OBJ indices are 1-based.
            np.savetxt(stream, triangles + 1, fmt="f %d %d %d")
            return path
        number = uv_format or ("%.17g" if uv_rows.dtype.itemsize > 4 else "%.9g")
        np.savetxt(stream, uv_rows, fmt=f"vt {number} {number}")
        corners = np.stack(
            (triangles.astype(np.int64) + 1, np.asarray(uv_indices, dtype=np.int64) + 1), axis=-1
        ).reshape(len(triangles), 6)
        if texture is None:
            np.savetxt(stream, corners, fmt="f %d/%d %d/%d %d/%d")
            return path
        materials = texture.face_materials(len(triangles))
        bounds = [*np.flatnonzero(np.r_[True, materials[1:] != materials[:-1]]), len(triangles)]
        for begin, end in zip(bounds[:-1], bounds[1:]):
            if begin == end:
                continue
            stream.write(f"usemtl {texture.material_names[materials[begin]]}\n")
            np.savetxt(stream, corners[begin:end], fmt="f %d/%d %d/%d %d/%d")
    _write_materials(path, texture)
    return path


def _write_materials(path: Path, texture) -> None:
    from ._png import write_png

    lines = []
    for name, image in zip(texture.material_names, texture.images):
        if image.pixels.dtype not in (np.uint8, np.uint16):
            raise ValueError("OBJ/MTL texture export needs uint8 or uint16 pixels; "
                             "float32 textures are carried by the npz codec only")
        filename = f"{path.stem}_{name}.png"
        write_png(path.parent / filename, image.pixels)
        lines += [f"newmtl {name}", "Ka 1 1 1", "Kd 1 1 1", "Ks 0 0 0", f"map_Kd {filename}", ""]
    (path.parent / f"{path.stem}.mtl").write_text("\n".join(lines), encoding="utf-8")


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
            if (ply_format or len(fields) != 3 or fields[2] != b"1.0"
                    or fields[1] not in (b"ascii", b"binary_little_endian", b"binary_big_endian")):
                raise ValueError("invalid PLY format declaration")
            ply_format = fields[1].decode()
        elif keyword == b"element":
            count = int(fields[2])
            if count < 0:
                raise ValueError("PLY element count cannot be negative")
            elements.append(
                {"name": fields[1].decode(), "count": count, "properties": []}
            )
        elif keyword == b"property":
            if not elements:
                raise ValueError("PLY property outside of an element")
            if fields[1] == b"list":
                if _ply_dtype(fields[2].decode()).kind not in "iu":
                    raise ValueError("PLY list counts must have an integer type")
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
            if not ply_format:
                raise ValueError("PLY header has no format declaration")
            return ply_format, elements, stream.tell()


def _ply_dtype(name: str) -> np.dtype:
    if name not in _PLY_DTYPES:
        raise UnsupportedPlyVariant(f"unsupported PLY property type {name!r}")
    return np.dtype(_PLY_DTYPES[name]).newbyteorder("<")


def _ascii_face_indices(
    row: list[bytes], properties: list[dict[str, Any]], path: Path
) -> list[int]:
    """Read vertex indices while consuming every property in order."""
    index_property = _face_index_property(properties)
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
        if prop is index_property:
            selected = [int(value) for value in values]

    if cursor != len(row):
        raise ValueError(
            f"{path} face contains {len(row) - cursor} undeclared value(s)"
        )
    assert selected is not None
    return selected


def _face_index_property(properties):
    lists = [prop for prop in properties if prop["list"]]
    if not lists:
        raise UnsupportedPlyVariant("PLY faces need a vertex index list")
    selected = next((prop for prop in lists if prop["name"] in ("vertex_indices", "vertex_index")), lists[0])
    if _ply_dtype(selected["value_type"]).kind not in "iu":
        raise ValueError("PLY vertex indices must have an integer type")
    return selected


def _read_exact(stream: Any, byte_count: int, path: Path) -> bytes:
    data = stream.read(byte_count)
    if len(data) != byte_count:
        raise ValueError(f"{path} contains a truncated binary PLY element")
    return data


def _binary_face_indices(
    stream: Any, properties: list[dict[str, Any]], path: Path
) -> np.ndarray:
    """Read vertex indices while consuming every binary property."""
    index_property = _face_index_property(properties)
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
        if prop is index_property:
            selected = values

    assert selected is not None
    return selected


def read_ply(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read positions, triangles, and colors from a `.ply` file.

    Supports `ascii` and `binary_little_endian`, the two formats the tools in
    this repository produce. Vertices need `x`, `y`, `z`; `red`/`green`/`blue`
    are picked up when present. Faces use `vertex_indices` or `vertex_index`,
    falling back to the first integer list. Polygons are fan-triangulated.
    Files with no `face` element return zero triangles.
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
        found_vertices = False
        vertex_count = next((element["count"] for element in elements if element["name"] == "vertex"), 0)

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
                rows = np.frombuffer(_read_exact(stream, dtype.itemsize * count, path), dtype=dtype)
            else:
                rows = None  # read per-row below

            if name == "vertex":
                found_vertices = True
                if ascii_mode:
                    order = [prop["name"] for prop in properties]
                    table = np.array(rows, dtype=np.float64).reshape(count, len(order))
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
                        if any(index < 0 or index >= vertex_count for index in indices):
                            raise ValueError(f"{path} contains out-of-range vertex indices")
                        for corner in range(1, len(indices) - 1):
                            corners.append(
                                (indices[0], indices[corner], indices[corner + 1])
                            )
                else:
                    for _ in range(count):
                        indices = _binary_face_indices(stream, properties, path)
                        if np.any(indices < 0) or np.any(indices >= vertex_count):
                            raise ValueError(f"{path} contains out-of-range vertex indices")
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

    if not found_vertices:
        raise ValueError(f"{path} contains no vertex element")
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
