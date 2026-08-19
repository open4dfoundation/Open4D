from __future__ import annotations

import builtins

import pytest

from open4d import TopologyMode
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
)

pytestmark = pytest.mark.cpu


def write_obj(path, x=0.0):
    path.write_text(
        f"v {x} 0 0\nv {x + 1} 0 0\nv {x} 1 0\nf 1 2 3\n",
        encoding="utf-8",
    )


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


def test_unusual_ply_face_layout_uses_the_trimesh_fallback(tmp_path, monkeypatch):
    path = tmp_path / "frame.ply"
    path.write_text(
        "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
        "property float y\nproperty float z\nelement face 1\n"
        "property list uchar int vertex_indices\nproperty uchar material\n"
        "end_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2 7\n",
        encoding="ascii",
    )
    calls = []

    def fallback(fallback_path):
        calls.append(fallback_path)
        return ([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], [[0, 1, 2]], None)

    monkeypatch.setattr("open4d.io._mesh.read_with_trimesh", fallback)

    assert len(open_sequence(path)[0].geometry.triangles) == 1
    assert calls == [path]


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
