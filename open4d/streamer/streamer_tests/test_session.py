"""Collecting clips into a bundle, and encoding a clip at several rungs."""

from __future__ import annotations

import json

import numpy as np
import pytest
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh

from streamer import bundle, session

pytestmark = pytest.mark.cpu


def mesh(offset: float = 0.0) -> TriangleMesh:
    return TriangleMesh(
        np.asarray(
            [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, offset]], dtype=np.float32
        ),
        np.asarray([[0, 1, 2], [1, 3, 2]], dtype=np.uint32),
    )


def sequence_of(count: int = 3) -> Sequence:
    return Sequence(
        MemoryFrameProvider(
            [Frame(i, i / 30, mesh(i * 0.1)) for i in range(count)]
        )
    )


@pytest.mark.parametrize(
    "spec, frame_format, bits",
    [
        ("ply", "ply", session.export.DRACO_QUANTIZATION_BITS),
        ("draco", "draco", session.export.DRACO_QUANTIZATION_BITS),
        ("draco@11", "draco", 11),
        ("  draco@8  ", "draco", 8),
    ],
)
def test_a_rung_spec_parses_to_a_format_and_a_quantisation(spec, frame_format, bits):
    rung = session.parse_rung(spec)
    assert rung.frame_format == frame_format
    assert rung.quantization_bits == bits


def test_a_parsed_rung_keeps_its_spec_as_the_name_a_consumer_asks_for():
    # Stable across clips, so "draco@11" of one pane and of another are the
    # same rung -- which is what `bundle.Variant.name` is for.
    assert session.parse_rung("draco@11").id == "draco@11"
    assert session.parse_rung(session.parse_rung("ply")).id == "ply"


def test_an_unknown_format_is_refused():
    with pytest.raises(ValueError, match="unknown frame format"):
        session.parse_rung("webm")


def test_quantising_a_format_that_does_not_quantise_is_refused():
    # Rather than ignored: a caller writing ply@11 believes they asked for
    # something smaller, and two identical rungs would hide that.
    with pytest.raises(ValueError, match="does not quantise"):
        session.parse_rung("ply@11")


def test_a_non_numeric_quantisation_is_refused():
    with pytest.raises(ValueError, match="non-numeric quantisation"):
        session.parse_rung("draco@high")


def test_the_index_is_written_when_the_block_ends(tmp_path):
    with session.Bundle(tmp_path, title="Capture") as clips:
        assert not clips.index_path.exists()
        clips.add(sequence_of(), name="capture")
    index = json.loads((tmp_path / bundle.INDEX_NAME).read_text())
    assert index["title"] == "Capture"
    assert [clip["name"] for clip in index["clips"]] == ["capture"]
    assert len(index["clips"][0]["frames"]) == 3


def test_a_failed_block_leaves_no_index(tmp_path):
    # Frames on disk with no manifest is a recoverable half-export. A manifest
    # promising clips that are not all there reads as missing frames instead.
    with pytest.raises(RuntimeError):
        with session.Bundle(tmp_path) as clips:
            clips.add(sequence_of(), name="capture")
            raise RuntimeError("producer died")
    assert not (tmp_path / bundle.INDEX_NAME).exists()


def test_a_bundle_with_no_clips_is_refused(tmp_path):
    with pytest.raises(ValueError, match="no clips"):
        session.Bundle(tmp_path).write()


def test_several_clips_land_in_one_index(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clips.add(sequence_of(), name="first")
        clips.add(sequence_of(), name="second")
    index = json.loads((tmp_path / bundle.INDEX_NAME).read_text())
    assert [clip["name"] for clip in index["clips"]] == ["first", "second"]


def test_extra_rungs_become_variants_of_one_clip(tmp_path):
    # Not sibling clips: a consumer has to be able to switch between them
    # mid-playback, and three clips would be three panes.
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(
            sequence_of(), name="capture", rungs=["draco", "draco@8", "ply"]
        )
    assert clip.detail["rung"] == "draco"
    assert [variant["name"] for variant in clip.variants] == ["draco@8", "ply"]
    index = json.loads((tmp_path / bundle.INDEX_NAME).read_text())
    assert len(index["clips"]) == 1


def test_every_rung_carries_the_same_number_of_frames(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(sequence_of(4), name="capture", rungs=["ply", "draco@8"])
    assert len(clip.frames) == 4
    for variant in clip.variants:
        assert len(variant["frames"]) == len(clip.frames)


def test_a_variant_records_the_bytes_it_measured(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(sequence_of(), name="capture", rungs=["ply", "draco@8"])
    measured = clip.variants[0]["bytes"]
    on_disk = sum(
        (tmp_path / frame).stat().st_size for frame in clip.variants[0]["frames"]
    )
    assert measured == on_disk > 0


def test_a_variant_leaves_quality_unscored(tmp_path):
    # This knows what a rung cost, not what it was worth. `metrics` fills that
    # in, and an unscored rung must not read as if its quality were known.
    with session.Bundle(tmp_path) as clips:
        clip = clips.add(sequence_of(), name="capture", rungs=["ply", "draco@8"])
    assert clip.variants[0]["quality"] == {}


def test_a_rung_listed_twice_is_refused(tmp_path):
    with pytest.raises(ValueError, match="listed twice"):
        session.Bundle(tmp_path).add(
            sequence_of(), name="capture", rungs=["ply", "ply"]
        )


def test_no_rungs_at_all_is_refused(tmp_path):
    with pytest.raises(ValueError, match="at least one rung"):
        session.Bundle(tmp_path).add(sequence_of(), name="capture", rungs=[])


def test_a_clip_from_another_exporter_can_be_added(tmp_path):
    with session.Bundle(tmp_path) as clips:
        clips.add(sequence_of(), name="capture")
        clips.add_clip(
            bundle.Clip(
                name="elsewhere", representation="pixels", frames=["a.jpg"]
            )
        )
    index = json.loads((tmp_path / bundle.INDEX_NAME).read_text())
    assert [clip["name"] for clip in index["clips"]] == ["capture", "elsewhere"]


def test_a_source_path_is_loaded_and_added(tmp_path):
    from open4d.io import write_sequence

    frames = tmp_path / "frames"
    write_sequence(sequence_of(), frames, format="ply", overwrite=True)

    out = tmp_path / "bundle"
    with session.Bundle(out) as clips:
        clip = clips.add_source(frames, name="loaded")
    assert clip.name == "loaded"
    assert len(clip.frames) == 3
