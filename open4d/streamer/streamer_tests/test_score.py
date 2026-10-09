"""Rungs scored against the sequence they came from, and codec rungs."""

from __future__ import annotations

import json

import numpy as np
import pytest
from open4d import Frame, MemoryFrameProvider, PointCloud, Sequence, TriangleMesh

import open4d
from streamer import bundle, policy, score, session

pytestmark = pytest.mark.cpu


def grid(offset: float = 0.0) -> TriangleMesh:
    xs, ys = np.meshgrid(np.linspace(0, 1, 6), np.linspace(0, 1, 6))
    positions = np.column_stack([xs.ravel(), ys.ravel(),
                                 np.full(36, offset)]).astype(np.float32)
    triangles = []
    for row in range(5):
        for column in range(5):
            a = row * 6 + column
            triangles += [[a, a + 1, a + 6], [a + 1, a + 7, a + 6]]
    return TriangleMesh(positions, np.asarray(triangles, dtype=np.uint32))


def meshes(count: int = 3, jitter: float = 0.0) -> Sequence:
    frames = []
    for index in range(count):
        mesh = grid(index * 0.1)
        if jitter:
            mesh = TriangleMesh(mesh.positions + jitter, mesh.triangles)
        frames.append(Frame(index, index / 30, mesh))
    return Sequence(MemoryFrameProvider(frames))


def points(count: int = 3) -> Sequence:
    return Sequence(MemoryFrameProvider([
        Frame(index, index / 30, PointCloud(grid(index * 0.1).positions))
        for index in range(count)
    ]))


def test_a_lossless_rung_scores_the_cap_not_infinity(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture", rungs=["ply"], score=True)
    assert clip.detail["quality"][score.METRIC] == score.LOSSLESS_DB
    # Strict JSON: a browser's JSON.parse rejects Infinity.
    json.loads((tmp_path / bundle.INDEX_NAME).read_text(),
               parse_constant=lambda name: pytest.fail(f"{name} in view.json"))


def test_coarser_quantisation_scores_lower(tmp_path):
    pytest.importorskip("DracoPy")
    pytest.importorskip("scipy")
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture",
                         rungs=["ply", "draco@14", "draco@6"], score=True)
    fine, coarse = (variant["quality"][score.METRIC] for variant in clip.variants)
    assert score.LOSSLESS_DB > fine > coarse
    assert clip.variants[1]["quality"]["hausdorff"] > 0


def test_point_clouds_are_scored_too(tmp_path):
    pytest.importorskip("DracoPy")
    pytest.importorskip("scipy")
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(points(), name="cloud", rungs=["ply", "draco@6"], score=True)
    assert clip.representation == "points"
    assert clip.detail["quality"][score.METRIC] == score.LOSSLESS_DB
    assert clip.variants[0]["quality"][score.METRIC] < score.LOSSLESS_DB


def test_fidelity_is_open4ds_compare_sequences(tmp_path):
    pytest.importorskip("scipy")
    reference, decoded = meshes(), meshes(jitter=0.01)
    expected = open4d.compare_sequences(reference, decoded)
    measured = score.fidelity(reference, decoded)
    assert measured[score.METRIC] == pytest.approx(expected.symmetric_psnr_db)
    assert measured["point_rms"] == pytest.approx(expected.symmetric_rms)


def test_a_scored_bundle_gives_policy_a_ladder_to_choose_from(tmp_path):
    pytest.importorskip("DracoPy")
    pytest.importorskip("scipy")
    with session.Bundle(tmp_path) as clips:
        clips.add(meshes(), name="capture", rungs=["ply", "draco@6"], score=True)
    (ladder,) = policy.measured_rungs(tmp_path)
    cheap, dear = ladder
    assert cheap.variant == "draco@6" and dear.variant is None
    chosen = policy.choose([ladder], budget=dear.bits_per_second * 2,
                           metric=score.METRIC)
    assert chosen.choices[0].variant is None
    chosen = policy.choose([ladder], budget=cheap.bits_per_second * 1.01,
                           metric=score.METRIC)
    assert chosen.choices[0].variant == "draco@6"


# ------------------------------------------------------------ codec rungs ---


@pytest.mark.parametrize("spec, codec, frame_format, bits", [
    ("klt", "klt", "ply", 14),
    ("klt/draco", "klt", "draco", 14),
    ("tsmc/draco@11", "tsmc", "draco", 11),
])
def test_a_codec_rung_names_the_codec_and_its_delivery(spec, codec, frame_format, bits):
    rung = session.parse_rung(spec)
    assert (rung.id, rung.codec, rung.frame_format, rung.quantization_bits) == (
        spec, codec, frame_format, bits)


@pytest.mark.parametrize("spec, message", [
    ("nope", "unknown frame format or codec"),
    ("vega", "unknown frame format or codec"),
    ("klt@11", "quantises the codec"),
    ("klt/webm", "unknown frame format"),
    ("klt/ply@11", "does not quantise"),
])
def test_a_bad_codec_rung_is_refused(spec, message):
    with pytest.raises(ValueError, match=message):
        session.parse_rung(spec)


@pytest.fixture
def fake_codec(monkeypatch):
    """`open4d.encode`/`decode` replaced by a codec that shifts every vertex."""
    calls = []

    def encode(sequence, destination, *, codec):
        calls.append(codec)
        destination.write_bytes(b"x" * 123)
        return destination

    def decode(path):
        return meshes(jitter=0.05)

    monkeypatch.setattr(open4d, "encode", encode)
    monkeypatch.setattr(open4d, "decode", decode)
    return calls


def test_a_codec_rung_serves_the_decoded_frames(tmp_path, fake_codec):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture", rungs=["ply", "klt", "klt/ply"])
    # One encode per codec, however many rungs share it.
    assert fake_codec == ["klt"]
    variant = clip.variants[0]
    assert variant["name"] == "klt"
    assert variant["detail"]["codec"] == "klt"
    assert variant["detail"]["codec_bytes"] == 123
    assert variant["bytes"] == sum((tmp_path / f).stat().st_size
                                   for f in variant["frames"])
    with open4d.load(tmp_path / variant["frames"][0]) as served:
        np.testing.assert_allclose(served[0].geometry.positions,
                                   meshes(jitter=0.05)[0].geometry.positions)


def test_a_codec_rung_is_scored_as_the_codec_left_it(tmp_path, fake_codec):
    pytest.importorskip("scipy")
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture", rungs=["ply", "klt"], score=True)
    assert clip.variants[0]["quality"][score.METRIC] < score.LOSSLESS_DB


def test_a_codec_default_rendition_says_where_its_frames_came_from(tmp_path, fake_codec):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture", rungs=["klt"])
    assert clip.detail["codec"] == "klt"
    assert any("decoded on the server" in note for note in clip.notes)


def test_rungs_may_be_chosen_by_representation(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(meshes(), name="capture",
                         rungs={"mesh": ["ply"], "gaussians": ["splat"]})
    assert clip.detail["rung"] == "ply"
    with pytest.raises(ValueError, match="no rungs given for points"):
        session.Bundle(tmp_path / "other").add(points(), name="c",
                                               rungs={"mesh": ["ply"]})


def test_a_codec_rung_encodes_geometry_only(tmp_path, monkeypatch):
    seen = []

    def encode(sequence, destination, *, codec):
        seen.append(sequence[0].geometry)
        destination.write_bytes(b"x")
        return destination

    monkeypatch.setattr(open4d, "encode", encode)
    monkeypatch.setattr(open4d, "decode", lambda path: meshes())
    coloured = Sequence(MemoryFrameProvider([
        Frame(i, i / 30, TriangleMesh(grid().positions, grid().triangles,
                                      colors=np.ones((36, 3), dtype=np.float32)))
        for i in range(2)
    ]))
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(coloured, name="capture", rungs=["klt"])
    assert seen[0].colors is None
    np.testing.assert_array_equal(seen[0].positions, grid().positions)
    assert any("geometry only" in note for note in clip.notes)
