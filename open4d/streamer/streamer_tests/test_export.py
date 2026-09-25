"""Turning an `open4d.Sequence` into a bundle, for any representation it holds."""

from __future__ import annotations

import json

import numpy as np
import pytest
from open4d import (
    Frame,
    MemoryFrameProvider,
    PointCloud,
    Representation,
    Sequence,
    TriangleMesh,
)

from streamer import bundle, export

pytestmark = pytest.mark.cpu


def mesh(offset: float = 0.0) -> TriangleMesh:
    return TriangleMesh(
        np.asarray(
            [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, offset]], dtype=np.float32
        ),
        np.asarray([[0, 1, 2], [1, 3, 2]], dtype=np.uint32),
        colors=np.asarray(
            [[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0]], dtype=np.float32
        ),
    )


def points(count: int = 20) -> PointCloud:
    rng = np.random.default_rng(count)
    return PointCloud(rng.random((count, 3)).astype(np.float32))


def sequence_of(geometries) -> Sequence:
    return Sequence(
        MemoryFrameProvider(
            [Frame(i, i / 30, geometry) for i, geometry in enumerate(geometries)]
        )
    )


# -------------------------------------------------------- representation ---


def test_a_mesh_sequence_becomes_a_mesh_clip(tmp_path):
    clip = export.from_sequence(
        sequence_of([mesh(0.0), mesh(0.5)]), tmp_path, name="wave"
    )
    assert clip.representation == "mesh"
    assert len(clip.frames) == 2
    assert clip.counts == [4, 4]


def test_a_point_sequence_becomes_a_points_clip(tmp_path):
    clip = export.from_sequence(sequence_of([points(20)]), tmp_path, name="cloud")
    assert clip.representation == "points"
    assert clip.counts == [20]


def test_the_representation_comes_from_the_geometry_not_the_extension(tmp_path):
    """Both write `.ply`; only the geometry says which representation it is."""
    m = export.from_sequence(sequence_of([mesh()]), tmp_path, name="m")
    p = export.from_sequence(sequence_of([points()]), tmp_path, name="p")
    assert m.frames[0].endswith(".ply") and p.frames[0].endswith(".ply")
    assert m.representation != p.representation


def test_both_representations_have_geometry_so_both_can_be_explored(tmp_path):
    for geometry in (mesh(), points()):
        clip = export.from_sequence(
            sequence_of([geometry]), tmp_path, name=f"c{id(geometry)}"
        )
        assert Representation(clip.representation).has_geometry is True


def test_representation_of_refuses_an_empty_sequence():
    with pytest.raises(ValueError, match="no frames"):
        export.representation_of(sequence_of([]))


# ---------------------------------------------------------------- clip ---


def test_frames_are_written_and_listed_in_order(tmp_path):
    clip = export.from_sequence(
        sequence_of([mesh(0.0), mesh(0.5), mesh(1.0)]), tmp_path, name="wave"
    )
    assert clip.frames == sorted(clip.frames)
    for path in clip.frames:
        assert (tmp_path / path).is_file()


def test_bounds_span_every_frame_not_just_the_first(tmp_path):
    """A moving subject whose bounds came from frame 0 gets clipped on playback."""
    clip = export.from_sequence(
        sequence_of([mesh(0.0), mesh(5.0)]), tmp_path, name="wave"
    )
    assert clip.bounds_max[2] == pytest.approx(5.0)
    assert clip.bounds_min[2] == pytest.approx(0.0)


def test_scene_and_method_default_to_the_clip_name_and_representation(tmp_path):
    clip = export.from_sequence(sequence_of([mesh()]), tmp_path, name="wave")
    assert clip.scene == "wave"
    assert clip.method == "mesh"


def test_scene_can_be_set_to_share_a_viewport(tmp_path):
    clip = export.from_sequence(
        sequence_of([mesh()]), tmp_path, name="wave", scene="basketball", method="tvmc"
    )
    assert (clip.scene, clip.method) == ("basketball", "tvmc")


def test_two_clips_of_the_same_name_do_not_overwrite_each_other(tmp_path):
    first = export.from_sequence(sequence_of([mesh()]), tmp_path, name="same")
    second = export.from_sequence(sequence_of([mesh(), mesh()]), tmp_path, name="same")
    assert first.name != second.name
    assert len(first.frames) == 1 and len(second.frames) == 2


def test_the_result_is_a_bundle_the_server_accepts(tmp_path):
    clip = export.from_sequence(sequence_of([mesh()]), tmp_path, name="wave")
    bundle.write(tmp_path, title="t", source="s", clips=[clip])
    index = bundle.read(tmp_path)
    assert index["clips"][0]["representation"] == "mesh"
    assert index["version"] == bundle.VERSION


# -------------------------------------------------------------- from_source ---


def test_from_source_round_trips_through_open4d_load(tmp_path):
    """Whatever `open4d.load` reads becomes viewable, which is the whole point."""
    source = tmp_path / "frames"
    written = export.from_sequence(
        sequence_of([mesh(0.0), mesh(1.0)]), source, name="in"
    )
    frame_dir = source / written.name

    out = export.from_source(frame_dir, tmp_path / "bundle", fps=10)
    index = json.loads((out / "view.json").read_text())
    assert len(index["clips"]) == 1
    assert index["clips"][0]["representation"] == "mesh"
    assert len(index["clips"][0]["frames"]) == 2
    assert index["fps"] == 30  # The source manifest's timing takes precedence.
    assert any("open4d.load" in note for note in index["clips"][0]["notes"])


def test_export_keeps_fractional_source_rate(tmp_path):
    from open4d.io import write_sequence

    source = Sequence(MemoryFrameProvider([Frame(i, i / 23.976, mesh()) for i in range(3)]))
    path = write_sequence(source, tmp_path / "frames")
    out = export.from_source(path, tmp_path / "bundle", fps=10)
    assert bundle.read(out)["fps"] == pytest.approx(23.976)
