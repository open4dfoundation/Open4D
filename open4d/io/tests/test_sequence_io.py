from __future__ import annotations

import builtins
from pathlib import Path

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh
from open4d.io import (
    AmbiguousFormatError,
    DecodeError,
    MissingDependencyError,
    SourceNotFoundError,
    UnsupportedFeatureError,
    UnsupportedFormatError,
    available_formats,
    inspect_sequence,
    open_sequence,
    write_sequence,
)

pytestmark = pytest.mark.cpu


@pytest.mark.parametrize("demo", [False, True])
def test_empty_directory_created_at_publication_is_preserved(tmp_path, monkeypatch, demo):
    from open4d.demo import mesh_sequence, write_demo
    from open4d.io import _api

    destination = tmp_path / "sequence"
    write_frame = _api._write_frame
    exists = Path.exists
    ready = False
    competing_inode = None

    def finish_frame(*args, **kwargs):
        nonlocal ready
        result = write_frame(*args, **kwargs)
        ready = True
        return result

    def create_after_check(path):
        nonlocal competing_inode
        found = exists(path)
        if path == destination and ready and not found:
            path.mkdir()
            competing_inode = path.stat().st_ino
        return found

    monkeypatch.setattr(_api, "_write_frame", finish_frame)
    monkeypatch.setattr(Path, "exists", create_after_check)
    with pytest.raises(FileExistsError):
        if demo:
            write_demo(destination, side=2, frames=1)
        else:
            write_sequence(mesh_sequence(side=2, frames=1), destination)
    assert competing_inode is not None
    assert destination.stat().st_ino == competing_inode
    assert list(destination.iterdir()) == []
    assert list(tmp_path.iterdir()) == [destination]


def test_directory_overwrite_rolls_back_after_publication_failure(tmp_path, monkeypatch):
    from open4d.demo import mesh_sequence

    destination = tmp_path / "sequence"
    destination.mkdir()
    (destination / "keep.txt").write_text("old data")
    original = Path.replace

    def fail_final_rename(source, target):
        if Path(target) == destination:
            raise OSError("publication failed")
        return original(source, target)

    monkeypatch.setattr(Path, "replace", fail_final_rename)
    with pytest.raises(OSError, match="publication failed"):
        write_sequence(mesh_sequence(side=2, frames=1), destination, overwrite=True)
    assert (destination / "keep.txt").read_text() == "old data"


@pytest.mark.parametrize("concurrent_content", [False, True])
def test_failed_directory_publication_cleans_only_empty_reservation(
    tmp_path, monkeypatch, concurrent_content,
):
    from open4d.demo import mesh_sequence

    destination = tmp_path / "sequence"
    rename = Path.rename

    def fail_final_rename(source, target):
        if Path(target) == destination:
            if concurrent_content:
                destination.mkdir(exist_ok=True)
                (destination / "keep.txt").write_text("other writer")
            raise OSError("publication failed")
        return rename(source, target)

    monkeypatch.setattr(Path, "rename", fail_final_rename)
    with pytest.raises(OSError, match="publication failed"):
        write_sequence(mesh_sequence(side=2, frames=1), destination)
    if concurrent_content:
        assert (destination / "keep.txt").read_text() == "other writer"
        assert list(tmp_path.iterdir()) == [destination]
    else:
        assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("file_output", [False, True])
def test_output_created_during_export_is_not_overwritten(tmp_path, monkeypatch, file_output):
    from open4d.demo import mesh_sequence
    from open4d.io import _api

    destination = tmp_path / ("sequence.ply" if file_output else "sequence")
    original = _api._write_frame

    def create_competing_output(*args, **kwargs):
        result = original(*args, **kwargs)
        if file_output:
            destination.write_bytes(b"other writer")
        else:
            destination.mkdir()
            (destination / "keep.txt").write_text("other writer")
        return result

    monkeypatch.setattr(_api, "_write_frame", create_competing_output)
    with pytest.raises(FileExistsError):
        write_sequence(mesh_sequence(side=2, frames=1), destination, allow_lossy=file_output)
    if file_output:
        assert destination.read_bytes() == b"other writer"
    else:
        assert (destination / "keep.txt").read_text() == "other writer"


def test_obj_directory_preserves_float32_positions(tmp_path):
    positions = np.array([[1.2345678, -0.12345678, 3.402823e20]], dtype=np.float32)
    mesh = TriangleMesh(positions, np.empty((0, 3), dtype=np.uint32))
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0, mesh)]))

    output = write_sequence(sequence, tmp_path / "obj", format="obj")

    with open_sequence(output) as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.positions, positions)


def test_obj_writer_accepts_array_like_inputs(tmp_path):
    from open4d.io import _mesh

    path = _mesh.write_obj(tmp_path / "lists.obj", [[0., 0, 0], [1., 0, 0], [0., 1, 0]], [[0, 1, 2]])
    np.testing.assert_array_equal(open_sequence(path)[0].geometry.triangles, [[0, 1, 2]])


def test_ply_directory_preserves_frames_with_no_surface(tmp_path):
    empty = TriangleMesh(np.empty((0, 3), dtype=np.float32), np.empty((0, 3), dtype=np.uint32))
    visible = TriangleMesh(np.ones((1, 3), dtype=np.float32), np.empty((0, 3), dtype=np.uint32))
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0, empty), Frame(1, 0.1, visible)]))

    output = write_sequence(sequence, tmp_path / "ply", format="ply")

    with open_sequence(output) as decoded:
        assert decoded.timestamps == (0, 0.1)
        assert decoded[0].geometry.positions.shape == (0, 3)
        np.testing.assert_array_equal(decoded[1].geometry.positions, visible.positions)


def test_ascii_ply_accepts_explicit_zero_vertex_count(tmp_path):
    path = tmp_path / "empty.ply"
    path.write_text("ply\nformat ascii 1.0\nelement vertex 0\nproperty float x\n"
                    "property float y\nproperty float z\nend_header\n", encoding="ascii")

    with open_sequence(path) as decoded:
        assert decoded[0].geometry.positions.shape == (0, 3)


@pytest.mark.parametrize("count,match", [(3, "truncated"), (-1, "negative")])
def test_ply_missing_vertex_bytes_are_not_an_empty_frame(tmp_path, count, match):
    path = tmp_path / "broken.ply"
    path.write_bytes(f"ply\nformat binary_little_endian 1.0\nelement vertex {count}\n"
                     "property float x\nproperty float y\nproperty float z\nend_header\n".encode())

    with pytest.raises(DecodeError, match=match):
        open_sequence(path)[0]


def write_obj(path, x=0.0):
    path.write_text(
        f"v {x} 0 0\nv {x + 1} 0 0\nv {x} 1 0\nf 1 2 3\n",
        encoding="utf-8",
    )


def test_obj_handles_indentation_tabs_comments_and_relative_indices(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_text("  v\t0 0 0\nv 1 0 0\nv 0 1 0\n f\t-3 -2 -1 # triangle\n")
    mesh = open_sequence(path)[0].geometry
    np.testing.assert_array_equal(mesh.triangles, [[0, 1, 2]])


@pytest.mark.parametrize("face", ["0 1 2", "-3 1 2", "1 2", "1 2 4"])
def test_obj_rejects_invalid_indices_even_if_more_vertices_follow(tmp_path, face):
    path = tmp_path / "broken.obj"
    path.write_text(f"v 0 0 0\nv 1 0 0\nf {face}\nv 0 1 0\n")
    with pytest.raises(DecodeError):
        open_sequence(path)[0]


def test_ply_face_indices_are_not_confused_with_texture_coordinate_lists(tmp_path):
    path = tmp_path / "textured.ply"
    path.write_text("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
                    "property float y\nproperty float z\nelement face 1\n"
                    "property list uchar float texcoord\nproperty list uchar int vertex_indices\n"
                    "end_header\n0 0 0\n1 0 0\n0 1 0\n6 0 0 1 0 0 1 3 0 1 2\n")
    np.testing.assert_array_equal(open_sequence(path)[0].geometry.triangles, [[0, 1, 2]])


@pytest.mark.parametrize("declaration,match", [
    ("format gibberish 1.0", "format"),
    ("format ascii 1.1", "format"),
    ("format ascii 1.0\nelement face 0\nproperty list float int vertex_indices", "integer"),
])
def test_invalid_ply_header_is_rejected(tmp_path, declaration, match):
    path = tmp_path / "invalid.ply"
    path.write_text(f"ply\n{declaration}\nelement vertex 0\nproperty float x\n"
                    "property float y\nproperty float z\nend_header\n")
    with pytest.raises(DecodeError, match=match):
        open_sequence(path)[0]


def test_single_file_is_a_lazy_one_frame_sequence(tmp_path, monkeypatch):
    path = tmp_path / "mesh_41.obj"
    write_obj(path)
    real_open = builtins.open
    reads = []

    def recording_open(file, *args, **kwargs):
        reads.append(file)
        return real_open(file, *args, **kwargs)

    monkeypatch.setattr(builtins, "open", recording_open)
    sequence = open_sequence(path)
    assert reads == []
    assert len(sequence) == 1
    frame = sequence[0]
    assert frame.frame_index == 0
    assert frame.geometry.positions.dtype.name == "float32"
    assert frame.geometry.triangles.dtype.name == "uint32"
    assert reads == [path]


def test_directory_order_timing_and_source_indices(tmp_path):
    write_obj(tmp_path / "frame_10.obj", x=10)
    write_obj(tmp_path / "frame_2.obj", x=2)

    sequence = open_sequence(tmp_path, fps=4)

    assert [frame.frame_index for frame in sequence] == [2, 10]
    assert sequence.timestamps == (0.0, 0.25)
    assert sequence.fps == pytest.approx(4.0)
    assert sequence.topology is TopologyMode.UNKNOWN


def test_relative_source_survives_a_working_directory_change(tmp_path, monkeypatch):
    source = tmp_path / "source"
    source.mkdir()
    write_obj(source / "frame.obj")
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(tmp_path)

    sequence = open_sequence("source")
    monkeypatch.chdir(elsewhere)

    assert len(sequence[0].geometry.triangles) == 1


def test_inspection_does_not_decode_geometry(tmp_path):
    (tmp_path / "broken_7.obj").write_text("not geometry", encoding="utf-8")

    info = inspect_sequence(tmp_path)

    assert info.frame_count == 1
    assert info.format == "obj"
    assert info.fps == 30.0
    assert info.timing_source == "default"
    with pytest.raises(DecodeError, match="broken_7.obj"):
        open_sequence(tmp_path)[0]


def test_non_object_sequence_manifest_is_a_decode_error(tmp_path):
    (tmp_path / "open4d.sequence.json").write_text("[]", encoding="utf-8")

    for operation in (open_sequence, inspect_sequence):
        with pytest.raises(DecodeError, match="manifest root must be an object"):
            operation(tmp_path)


def test_ascii_ply_colors_are_normalized_without_optional_dependencies(tmp_path):
    path = tmp_path / "frame.ply"
    path.write_text(
        "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
        "property float y\nproperty float z\nproperty uchar red\n"
        "property uchar green\nproperty uchar blue\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n"
        "0 0 0 255 0 0\n1 0 0 0 255 0\n0 1 0 0 0 255\n3 0 1 2\n",
        encoding="ascii",
    )

    mesh = open_sequence(path)[0].geometry

    assert mesh.colors.dtype.name == "float32"
    assert mesh.colors.min() == 0.0
    assert mesh.colors.max() == 1.0


def test_ascii_ply_preserves_floating_point_colors(tmp_path):
    path = tmp_path / "float_colors.ply"
    path.write_text(
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\n"
        "property float y\nproperty float z\nproperty float red\n"
        "property float green\nproperty float blue\nend_header\n"
        "0 0 0 1.0 0.5 0.0\n",
        encoding="ascii",
    )

    colors = open_sequence(path)[0].geometry.colors

    assert colors[0] == pytest.approx([1.0, 0.5, 0.0])


def test_face_less_ply_frames_do_not_share_a_mutable_index_buffer(tmp_path):
    path = tmp_path / "points.ply"
    path.write_text(
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\n"
        "property float y\nproperty float z\nend_header\n0 0 0\n",
        encoding="ascii",
    )

    first = open_sequence(path)[0].geometry
    second = open_sequence(path)[0].geometry

    assert first.triangles is not second.triangles


def test_mixed_directory_requires_an_explicit_format(tmp_path):
    write_obj(tmp_path / "frame_1.obj")
    (tmp_path / "frame_1.ply").write_bytes(b"ply\n")

    with pytest.raises(AmbiguousFormatError, match="mixes frame formats"):
        open_sequence(tmp_path)
    assert len(open_sequence(tmp_path, format="obj")) == 1


def test_source_and_format_errors_are_typed(tmp_path):
    with pytest.raises(SourceNotFoundError):
        open_sequence(tmp_path / "missing")
    unsupported = tmp_path / "frame.xyz"
    unsupported.touch()
    with pytest.raises(UnsupportedFormatError, match="No reader"):
        open_sequence(unsupported)
    with pytest.raises(UnsupportedFormatError, match="Unsupported format"):
        open_sequence(tmp_path, format="made-up")


def test_explicit_format_can_open_an_extensionless_file(tmp_path):
    path = tmp_path / "frame_data"
    write_obj(path)

    frame = open_sequence(path, format="obj")[0]

    assert len(frame.geometry.triangles) == 1


@pytest.mark.parametrize(
    "face_properties, face_row",
    (
        (
            "property uchar material\n"
            "property list uchar int vertex_indices\n",
            "7 3 0 1 2\n",
        ),
        (
            "property list uchar int vertex_indices\n"
            "property uchar material\n",
            "3 0 1 2 7\n",
        ),
    ),
)
def test_ascii_ply_face_list_respects_declared_property_order(
    tmp_path, monkeypatch, face_properties, face_row
):
    path = tmp_path / "frame.ply"
    path.write_text(
        (
            "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
            "property float y\nproperty float z\nelement face 1\n"
        )
        + face_properties
        + "end_header\n0 0 0\n1 0 0\n0 1 0\n"
        + face_row,
        encoding="ascii",
    )

    def fallback(fallback_path):
        pytest.fail(f"valid face properties should not need trimesh: {fallback_path}")

    monkeypatch.setattr("open4d.io._mesh.read_with_trimesh", fallback)

    triangles = open_sequence(path)[0].geometry.triangles
    np.testing.assert_array_equal(triangles, [[0, 1, 2]])


def test_binary_ply_face_list_respects_declared_property_order(tmp_path, monkeypatch):
    path = tmp_path / "frame.ply"
    header = (
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty uchar material\n"
        "property list uchar int vertex_indices\n"
        "property float confidence\nend_header\n"
    ).encode("ascii")
    vertices = np.array(
        [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
        dtype=np.dtype([("x", "<f4"), ("y", "<f4"), ("z", "<f4")]),
    )
    face = np.array(
        [(7, 3, [0, 1, 2], 0.5)],
        dtype=np.dtype(
            [
                ("material", "u1"),
                ("count", "u1"),
                ("indices", "<i4", 3),
                ("confidence", "<f4"),
            ]
        ),
    )
    path.write_bytes(header + vertices.tobytes() + face.tobytes())

    def fallback(fallback_path):
        pytest.fail(f"valid face properties should not need trimesh: {fallback_path}")

    monkeypatch.setattr("open4d.io._mesh.read_with_trimesh", fallback)

    triangles = open_sequence(path)[0].geometry.triangles
    np.testing.assert_array_equal(triangles, [[0, 1, 2]])


def test_malformed_ply_is_a_decode_error_not_a_missing_dependency(tmp_path):
    path = tmp_path / "broken.ply"
    path.write_bytes(b"ply\n")

    with pytest.raises(DecodeError, match="truncated PLY header"):
        open_sequence(path)[0]


def test_ascii_ply_rejects_a_mismatched_face_list_count(tmp_path):
    path = tmp_path / "bad_face.ply"
    path.write_text(
        "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\n"
        "property float y\nproperty float z\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n"
        "0 0 0\n1 0 0\n0 1 0\n0 0 1\n3 0 1 2 3\n",
        encoding="ascii",
    )

    with pytest.raises(DecodeError, match="declares 3 indices but contains 4"):
        open_sequence(path)[0]


def test_fps_and_options_are_validated(tmp_path):
    write_obj(tmp_path / "frame.obj")
    for value in (0, -1, float("nan"), float("inf")):
        with pytest.raises(ValueError, match="fps"):
            open_sequence(tmp_path, fps=value)
    with pytest.raises(TypeError, match="fps"):
        open_sequence(tmp_path, fps=True)
    with pytest.raises(UnsupportedFeatureError, match="unknown"):
        open_sequence(tmp_path, options={"unknown": True})


def test_optional_reader_import_is_lazy_and_actionable(tmp_path, monkeypatch):
    path = tmp_path / "frame.off"
    path.touch()

    real_import = builtins.__import__

    def without_trimesh(name, *args, **kwargs):
        if name == "trimesh":
            raise ImportError("not installed")
        return real_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", without_trimesh)
    sequence = open_sequence(path)
    with pytest.raises(MissingDependencyError, match=r"pip install 'open4d\[tools\]'"):
        sequence[0]


def test_available_formats_names_optional_dependencies():
    formats = {info.id: info for info in available_formats()}
    assert formats["obj"].dependency_extra is None
    assert formats["ply"].dependency_extra is None
    assert formats["off"].dependency_extra == "tools"
    assert all(info.readable and info.writable for info in formats.values())


def test_write_sequence_is_format_independent_and_reopenable(tmp_path):
    frames = [
        Frame(index + 41, 1.25 + index / 24, TriangleMesh(
            np.array([[index, 0, 0], [index + 1, 0, 0], [index, 1, 0]], dtype=np.float32),
            [[0, 1, 2]],
        ), metadata={"source_id": f"camera-{index}"})
        for index in range(2)
    ]
    source = Sequence(MemoryFrameProvider(
        frames,
        metadata={"capture_id": "rafa"},
        topology=TopologyMode.FIXED,
        has_constant_vertex_count=True,
        has_vertex_correspondence=True,
    ))

    for format in ("obj", "ply"):
        output = write_sequence(source, tmp_path / format, format=format)
        assert (output / "open4d.sequence.json").is_file()
        info = inspect_sequence(output, format=format)
        decoded = open_sequence(output, format=format)
        with pytest.raises(UnsupportedFeatureError, match="manifest timestamps"):
            open_sequence(output, format=format, fps=24)
        assert len(decoded) == 2
        assert info.timing_source == "manifest"
        assert decoded.metadata == source.metadata
        assert decoded.topology is TopologyMode.FIXED
        assert decoded.has_constant_vertex_count is True
        assert decoded.has_vertex_correspondence is True
        for expected, actual in zip(source, decoded, strict=True):
            assert actual.frame_index == expected.frame_index
            assert actual.timestamp == expected.timestamp
            assert actual.metadata == expected.metadata
            np.testing.assert_allclose(actual.geometry.positions, expected.geometry.positions)
            np.testing.assert_array_equal(actual.geometry.triangles, expected.geometry.triangles)


def test_directory_manifest_preserves_reversed_view_timing_policy(tmp_path):
    frames = [
        Frame(index, float(index), TriangleMesh(
            [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]
        ))
        for index in range(2)
    ]
    source = Sequence(MemoryFrameProvider(frames))[::-1]

    decoded = open_sequence(write_sequence(source, tmp_path / "reversed"))

    assert decoded.allow_nonmonotonic_timestamps is True
    assert decoded.timestamps == (1.0, 0.0)


def test_single_file_export_requires_explicit_lossy_policy(tmp_path):
    source = Sequence(MemoryFrameProvider([
        Frame(41, 1.25, TriangleMesh(
            [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]
        ), metadata={"camera": "left"})
    ], metadata={"capture": "rafa"}, topology=TopologyMode.UNKNOWN))

    with pytest.raises(UnsupportedFeatureError, match="temporal identity"):
        write_sequence(source, tmp_path / "frame.obj")
    assert not (tmp_path / "frame.obj").exists()

    output = write_sequence(source, tmp_path / "frame.obj", allow_lossy=True)
    assert output.is_file()


def test_ply_round_trip_preserves_float_rgba_colors(tmp_path):
    colors = np.array([
        [0.125, 0.25, 0.5, 0.75],
        [0.9, 0.8, 0.7, 0.6],
        [0.01, 0.02, 0.03, 0.04],
    ], dtype=np.float32)
    source = Sequence(MemoryFrameProvider([Frame(
        0, 0, TriangleMesh(
            [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]], colors=colors
        )
    )]))

    decoded = open_sequence(write_sequence(source, tmp_path / "rgba", format="ply"))

    np.testing.assert_array_equal(decoded[0].geometry.colors, colors)


@pytest.mark.parametrize("format", ("off", "glb", "gltf"))
def test_trimesh_color_export_requires_explicit_lossy_policy(tmp_path, format):
    source = Sequence(MemoryFrameProvider([Frame(
        0, 0, TriangleMesh(
            np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
            [[0, 1, 2]],
            colors=[[0.125, 0.25, 0.5], [0.9, 0.8, 0.7], [0.01, 0.02, 0.03]],
        )
    )]))
    destination = tmp_path / format

    with pytest.raises(UnsupportedFeatureError, match="color export.*lossy"):
        write_sequence(source, destination, format=format)

    assert not destination.exists()


def test_writer_rejects_empty_sequence_without_touching_destination(tmp_path):
    source = Sequence(MemoryFrameProvider([]))
    destination = tmp_path / "frames"
    destination.mkdir()
    sentinel = destination / "keep.txt"
    sentinel.write_text("existing", encoding="utf-8")

    with pytest.raises(UnsupportedFeatureError, match="empty"):
        write_sequence(source, destination, overwrite=True)

    assert sentinel.read_text(encoding="utf-8") == "existing"


def test_writer_rejects_silent_field_loss_and_cleans_partial_output(tmp_path):
    source = Sequence(MemoryFrameProvider([Frame(
        0, 0, TriangleMesh(
            [[0.0, 0, 0], [1.0, 0, 0], [0, 1.0, 0]], [[0, 1, 2]],
            attributes={"label": [1, 2, 3]},
        ),
    )]))
    destination = tmp_path / "frames"

    with pytest.raises(UnsupportedFeatureError, match="label"):
        write_sequence(source, destination, format="obj", allow_lossy=True)

    assert not destination.exists()
