"""The format Vega's Gaussian frames are written in.

`gs-tools export --method vega` wrote 3DGS PLY only, so the `.splat` clips in
the demo bundle came from an ad-hoc conversion that was not reproducible from
the tool. `--frame-format splat` now reaches this exporter too.

For Vega specifically the choice is nearly free: this export bakes colour to a
single band before writing, so there are no spherical-harmonic coefficients
above degree 0 for `.splat` to drop. About half the bytes for nothing
structural -- which is why the note says so rather than leaving a reader to
assume a loss.

Tested through `_write_frame` rather than through the exporter: the full path
needs Vega's own modules and CUDA, and the format decision does not.
"""
from __future__ import annotations

import numpy as np
import pytest

from gs_tools import io
from gs_tools.io import ply, splat
from gs_tools.methods import gaussian, vega

pytestmark = pytest.mark.cpu


def _fields(count: int = 64) -> dict:
    rng = np.random.default_rng(7)
    return {
        "xyz": rng.normal(size=(count, 3)).astype(np.float32),
        "scale_raw": rng.normal(size=(count, 3)).astype(np.float32),
        "rot_raw": rng.normal(size=(count, 4)).astype(np.float32),
        "opacity_raw": rng.normal(size=(count, 1)).astype(np.float32),
        "sh_dc": ply.rgb_to_sh_dc(rng.random((count, 3)).astype(np.float32)),
    }


def test_the_format_list_is_defined_once():
    """Two exporters offer the choice, and two tuples of names is one that goes
    stale -- adding a format would have to reach both."""
    assert vega.FORMATS is io.GAUSSIAN_FORMATS
    assert gaussian.FORMATS is io.GAUSSIAN_FORMATS


def test_ply_is_still_the_default(tmp_path):
    path = vega._write_frame(tmp_path / "frame_0000",
                             vega.VegaExportOptions(), **_fields())
    assert path.suffix == ".ply"
    assert path.is_file()


def test_splat_is_written_and_the_ply_is_not_left_behind(tmp_path):
    fields = _fields()
    path = vega._write_frame(tmp_path / "frame_0000",
                             vega.VegaExportOptions(frame_format="splat"),
                             **fields)
    assert path.suffix == ".splat"
    # Keeping both would double the export for a file no client asks for.
    assert not (tmp_path / "frame_0000.ply").exists()
    # 32 bytes a Gaussian, exactly -- the client reads the count from the size.
    assert path.stat().st_size == 32 * len(fields["xyz"])
    assert splat.count(path) == len(fields["xyz"])


def test_splat_is_about_half_the_bytes(tmp_path):
    fields = _fields()
    as_ply = vega._write_frame(tmp_path / "a", vega.VegaExportOptions(), **fields)
    as_splat = vega._write_frame(
        tmp_path / "b", vega.VegaExportOptions(frame_format="splat"), **fields)
    ratio = as_ply.stat().st_size / as_splat.stat().st_size
    # Measured on the demo bundle: 3.85 MB a frame against 1.85. The bound is
    # loose because a PLY header is a fixed cost and these frames are small.
    assert 1.5 < ratio < 3.0, ratio


def test_position_and_scale_survive_the_conversion_exactly(tmp_path):
    """What `.splat` quantises is colour, opacity and rotation. Position and
    scale are float32 either way, so a geometry comparison against another
    method is not being made against rounded coordinates."""
    fields = _fields()
    path = vega._write_frame(tmp_path / "frame_0000",
                             vega.VegaExportOptions(frame_format="splat"),
                             **fields)
    cloud = splat.decode(path.read_bytes())
    assert np.array_equal(cloud.positions, fields["xyz"])
    assert np.allclose(cloud.scales, np.exp(fields["scale_raw"]), rtol=1e-6)


def test_an_unknown_format_is_refused_before_anything_is_written(tmp_path):
    with pytest.raises(ValueError, match="not one of ply, splat"):
        vega._write_frame(tmp_path / "frame_0000",
                          vega.VegaExportOptions(frame_format="obj"), **_fields())
    assert not list(tmp_path.iterdir())


def test_the_clip_says_what_the_format_costs():
    """A reader should not have to know what `.splat` drops."""
    assert vega._format_notes(vega.VegaExportOptions()) == []
    notes = vega._format_notes(vega.VegaExportOptions(frame_format="splat"))
    assert len(notes) == 1
    note = notes[0]
    assert "32 bytes" in note
    # The claim that matters: nothing structural, because colour is already
    # degree 0 here. Saying only "smaller" would understate it, and saying
    # "lossless" would overstate it.
    assert "nothing structural is dropped" in note
    assert "quantised to 8 bits" in note
    assert "position and scale are exact" in note
