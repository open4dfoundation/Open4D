"""Open4D's Gaussian sequences written as bundle clips."""

from __future__ import annotations

import json

import numpy as np
import pytest
from open4d import GaussianSplats, NeuralGaussianFrame

from streamer import bundle, gaussians, session

pytestmark = pytest.mark.cpu


def splats(offset: float = 0.0, degree: int = 1) -> GaussianSplats:
    count = 4
    rng = np.random.default_rng(7)
    return GaussianSplats(
        positions=np.arange(count * 3, dtype=np.float32).reshape(count, 3) + offset,
        scales=np.full((count, 3), 0.25, dtype=np.float32),
        rotations=np.tile(np.array([1, 0, 0, 0], dtype=np.float32), (count, 1)),
        opacities=np.linspace(0.1, 0.9, count, dtype=np.float32),
        spherical_harmonics=rng.normal(size=(count, (degree + 1) ** 2, 3)),
    )


class _Appearance:
    """Stands in for Vega's colour network: one colour per direction."""

    def __init__(self):
        self.calls = []

    def colors(self, directions):
        self.calls.append(np.asarray(directions))
        return np.full((len(directions), 3), 0.75, dtype=np.float32)


def neural_frame(appearance) -> NeuralGaussianFrame:
    frame = splats()
    return NeuralGaussianFrame(
        positions=frame.positions, scales=frame.scales,
        rotations=frame.rotations, opacities=frame.opacities,
        appearance=appearance,
    )


def test_a_ply_frame_reads_back_as_the_same_splats(tmp_path):
    pytest.importorskip("plyfile")
    from open4d import load_gaussians

    original = splats(degree=2)
    clip = gaussians.from_frames([original], tmp_path, name="run")
    assert clip.representation == "gaussians"
    assert clip.detail["sh_degrees"] == [2]
    restored = load_gaussians(tmp_path / clip.frames[0])
    for field in ("positions", "scales", "rotations", "opacities",
                  "spherical_harmonics"):
        np.testing.assert_allclose(getattr(restored, field), getattr(original, field),
                                   rtol=1e-5, atol=1e-6)


def test_a_splat_frame_is_32_bytes_a_gaussian_and_says_what_it_drops(tmp_path):
    clip = gaussians.from_frames([splats(), splats(1.0)], tmp_path, name="run",
                                 frame_format="splat")
    assert [frame.endswith(".splat") for frame in clip.frames] == [True, True]
    assert (tmp_path / clip.frames[0]).stat().st_size == 4 * 32
    assert any("degree 0" in note for note in clip.notes)
    payload = np.frombuffer((tmp_path / clip.frames[1]).read_bytes(), dtype=np.uint8)
    positions = payload.reshape(4, 32)[:, :12].copy().view(np.float32)
    np.testing.assert_array_equal(positions, splats(1.0).positions)


def test_bounds_and_counts_span_every_frame(tmp_path):
    clip = gaussians.from_frames([splats(), splats(5.0)], tmp_path, name="run")
    assert clip.counts == [4, 4]
    assert clip.bounds_min == [0.0, 1.0, 2.0]
    assert clip.bounds_max == [14.0, 15.0, 16.0]


def test_a_neural_frame_is_baked_from_one_direction(tmp_path):
    appearance = _Appearance()
    clip = gaussians.from_frames([neural_frame(appearance)], tmp_path, name="vega")
    assert len(appearance.calls) == 1
    np.testing.assert_allclose(np.linalg.norm(appearance.calls[0], axis=1), 1.0)
    assert clip.detail["sh_degrees"] == [0]
    assert any("baked" in note for note in clip.notes)


def test_an_unknown_frame_format_is_refused(tmp_path):
    with pytest.raises(ValueError, match="unknown Gaussian frame format"):
        gaussians.from_frames([splats()], tmp_path, name="run", frame_format="draco")


def test_a_list_must_hold_only_gaussian_frames():
    with pytest.raises(TypeError, match="only GaussianSplats"):
        gaussians.frames_of([splats(), "frame.ply"])


def test_a_neural_field_native_sequence_is_refused():
    from open4d import NativeSequence

    native = NativeSequence.__new__(NativeSequence)
    native.codec, native.representation = "rerf", "neural_field"
    with pytest.raises(TypeError, match="streamer.live"):
        gaussians.frames_of(native)


def test_a_gaussian_ladder_becomes_variants(tmp_path):
    with session.Bundle(tmp_path, fps=12) as clips:
        clip = clips.add([splats(), splats(1.0)], name="run", rungs=["ply", "splat"])
    assert [variant["name"] for variant in clip.variants] == ["splat"]
    assert clip.variants[0]["detail"] == {"frame_format": "splat"}
    index = json.loads((tmp_path / bundle.INDEX_NAME).read_text())
    assert index["fps"] == 12
    assert index["clips"][0]["representation"] == "gaussians"


def test_a_mesh_format_is_not_a_gaussian_rung():
    with pytest.raises(ValueError, match="unknown Gaussian frame format"):
        session.parse_rung("draco", "gaussians")


def test_gaussians_cannot_be_scored_geometrically(tmp_path):
    with pytest.raises(ValueError, match="streamer.metrics"):
        session.Bundle(tmp_path).add([splats()], name="run", score=True)


@pytest.mark.parametrize("spec, keep", [("splat@25%", 0.25), ("ply@50%", 0.5),
                                        ("ply@100%", 1.0)])
def test_a_gaussian_rung_can_keep_a_share_of_its_gaussians(spec, keep):
    rung = session.parse_rung(spec, "gaussians")
    assert rung.keep == keep and rung.id == spec


@pytest.mark.parametrize("spec", ["splat@25", "splat@0%", "splat@150%", "ply@x%"])
def test_a_bad_share_is_refused(spec):
    with pytest.raises(ValueError, match="keep"):
        session.parse_rung(spec, "gaussians")


def test_keeping_a_share_keeps_the_most_significant(tmp_path):
    frame = splats()
    clip = gaussians.from_frames([frame], tmp_path, name="run",
                                 frame_format="splat", keep=0.5)
    assert clip.counts == [2]
    assert clip.detail["keep"] == 0.5
    payload = np.frombuffer((tmp_path / clip.frames[0]).read_bytes(), dtype=np.uint8)
    positions = payload.reshape(2, 32)[:, :12].copy().view(np.float32)
    # Equal scales, so opacity decides: the last two are the most opaque.
    np.testing.assert_array_equal(positions, frame.positions[2:])


def test_shares_become_variants_with_their_own_sizes(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add([splats()], name="run", rungs=["splat", "splat@50%"])
    assert clip.variants[0]["detail"] == {"frame_format": "splat", "keep": 0.5}
    assert clip.variants[0]["bytes"] == 2 * 32
