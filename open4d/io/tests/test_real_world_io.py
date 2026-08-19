"""Integration tests that exercise real files through the public API."""

from __future__ import annotations

import os
from pathlib import Path
import struct

import numpy as np
import pytest

import open4d.io
from open4d.io import inspect_sequence, open_sequence

pytestmark = pytest.mark.cpu


def _grid(side: int) -> tuple[np.ndarray, np.ndarray]:
    axis = np.linspace(-1.0, 1.0, side, dtype=np.float32)
    x, y = np.meshgrid(axis, axis, indexing="xy")
    positions = np.column_stack(
        (x.ravel(), y.ravel(), np.sin(x * 3).ravel() * 0.05)
    ).astype(np.float32)
    cells = np.arange((side - 1) ** 2, dtype=np.uint32)
    row, column = np.divmod(cells, side - 1)
    corner = row * side + column
    triangles = np.column_stack(
        (corner, corner + 1, corner + side, corner + 1,
         corner + side + 1, corner + side)
    ).reshape(-1, 3)
    return positions, triangles


def _write_binary_ply(
    path: Path, positions: np.ndarray, triangles: np.ndarray, colors: np.ndarray
) -> None:
    vertices = np.empty(
        len(positions),
        dtype=np.dtype(
            [("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
             ("red", "u1"), ("green", "u1"), ("blue", "u1")]
        ),
    )
    vertices["x"], vertices["y"], vertices["z"] = positions.T
    vertices["red"], vertices["green"], vertices["blue"] = colors.T
    faces = np.empty(
        len(triangles), dtype=np.dtype([("count", "u1"), ("indices", "<i4", 3)])
    )
    faces["count"] = 3
    faces["indices"] = triangles
    header = (
        "ply\nformat binary_little_endian 1.0\n"
        f"element vertex {len(positions)}\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\n"
        f"element face {len(triangles)}\n"
        "property list uchar int vertex_indices\nend_header\n"
    ).encode("ascii")
    path.write_bytes(header + vertices.tobytes() + faces.tobytes())


def test_obj_with_real_world_records_and_relative_indices(tmp_path):
    path = tmp_path / "textured_quad.obj"
    path.write_text(
        "# exporter-style OBJ\nmtllib material.mtl\no panel\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 0 1\nusemtl surface\ns 1\n"
        "f -4/1/1 -3/2/1 -2/3/1 -1/4/1\n",
        encoding="utf-8",
    )

    info = inspect_sequence(path)
    frame = open_sequence(path)[0]

    assert info.frame_count == 1 and info.format == "obj"
    np.testing.assert_array_equal(
        frame.geometry.triangles, [[0, 1, 2], [0, 2, 3]]
    )
    assert frame.metadata["file"] == path.name


def test_independently_written_binary_ply_round_trips_geometry_and_color(tmp_path):
    positions, triangles = _grid(32)
    colors = np.column_stack(
        (
            np.arange(len(positions), dtype=np.uint32) % 256,
            np.full(len(positions), 127),
            np.arange(len(positions), dtype=np.uint32)[::-1] % 256,
        )
    ).astype(np.uint8)
    path = tmp_path / "grid_binary.ply"
    _write_binary_ply(path, positions, triangles, colors)

    mesh = open_sequence(path)[0].geometry

    np.testing.assert_array_equal(mesh.positions, positions)
    np.testing.assert_array_equal(mesh.triangles, triangles)
    np.testing.assert_allclose(mesh.colors, colors.astype(np.float32) / 255.0)


def test_thousand_frame_directory_stays_lazy_and_supports_random_access(
    tmp_path, monkeypatch
):
    template = tmp_path / "template.obj"
    template.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
    frames = tmp_path / "frames"
    frames.mkdir()
    for index in range(1000):
        os.link(template, frames / f"frame_{index:06d}.obj")

    reads = []
    real_open = open

    def recording_open(path, *args, **kwargs):
        reads.append(path)
        return real_open(path, *args, **kwargs)

    monkeypatch.setattr("builtins.open", recording_open)
    sequence = open_sequence(frames, fps=60)

    assert len(sequence) == 1000
    assert sequence.timestamps[-1] == pytest.approx(999 / 60)
    assert reads == []
    assert sequence[731].frame_index == 731
    assert len(reads) == 1


def test_io_package_has_no_process_or_shell_execution_path():
    root = Path(open4d.io.__file__).parent
    source = "\n".join(path.read_text(encoding="utf-8") for path in root.glob("*.py"))

    for forbidden in ("subprocess", "os.system", "os.popen", "shell=True"):
        assert forbidden not in source


@pytest.mark.slow
def test_real_trimesh_glb_and_big_endian_ply_paths(tmp_path):
    trimesh = pytest.importorskip("trimesh")

    glb_path = tmp_path / "instanced.glb"
    source_mesh = trimesh.Trimesh(
        vertices=[[0, 0, 0], [1, 0, 0], [0, 1, 0]],
        faces=[[0, 1, 2]],
        vertex_colors=[[255, 0, 0, 255], [0, 255, 0, 255], [0, 0, 255, 255]],
        process=False,
    )
    scene = trimesh.Scene()
    transform = np.eye(4)
    transform[0, 3] = 5.0
    scene.add_geometry(source_mesh, node_name="translated", transform=transform)
    glb_path.write_bytes(scene.export(file_type="glb"))

    glb = open_sequence(glb_path)[0].geometry

    assert glb.positions[:, 0].min() == pytest.approx(5.0)
    assert glb.colors is not None and glb.colors.shape == (3, 4)

    ply_path = tmp_path / "big_endian.ply"
    header = (
        "ply\nformat binary_big_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
    ).encode("ascii")
    vertices = b"".join(
        struct.pack(">fff", *point)
        for point in ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    )
    ply_path.write_bytes(header + vertices + struct.pack(">Biii", 3, 0, 1, 2))

    big_endian = open_sequence(ply_path)[0].geometry

    assert len(big_endian.positions) == 3
    np.testing.assert_array_equal(big_endian.triangles, [[0, 1, 2]])
