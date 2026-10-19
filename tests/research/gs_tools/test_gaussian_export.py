"""The 3DGS exporter and the `.splat` delivery format."""

from __future__ import annotations

import json

import numpy as np
import pytest
from open4d.core import GaussianCloud, Representation

from gs_tools.io import ply, splat
from gs_tools.methods import gaussian

pytestmark = pytest.mark.cpu


def raw_gaussians(count: int = 5, *, n_rest: int = 0) -> dict:
    """Arguments for `ply.write`, i.e. raw 3DGS training parameters."""
    rng = np.random.default_rng(count)
    fields = {
        "xyz": rng.normal(size=(count, 3)).astype(np.float32),
        "scale_raw": rng.normal(size=(count, 3)).astype(np.float32) - 2.0,
        "rot_raw": rng.normal(size=(count, 4)).astype(np.float32),
        "opacity_raw": rng.normal(size=(count, 1)).astype(np.float32),
        "sh_dc": rng.normal(size=(count, 3)).astype(np.float32) * 0.1,
    }
    if n_rest:
        fields["sh_rest"] = rng.normal(size=(count, n_rest, 3)).astype(np.float32) * 0.01
    return fields


def queen_run(root, frames=(1, 2, 3), *, n_rest: int = 0):
    for frame in frames:
        target = root / "frames" / f"{frame:04d}"
        target.mkdir(parents=True, exist_ok=True)
        ply.write(target / "point_cloud.ply", **raw_gaussians(5, n_rest=n_rest))
    return root


def gstream_run(root, frames=(2, 3)):
    for frame in frames:
        target = root / f"frame{frame:06d}" / "point_cloud" / "iteration_150"
        target.mkdir(parents=True, exist_ok=True)
        ply.write(target / "point_cloud.ply", **raw_gaussians(4))
    return root


# ------------------------------------------------------------- the exporter ---


def test_exports_a_queen_run(tmp_path):
    out = tmp_path / "bundle"
    title, clips, _ = gaussian.build_clips(queen_run(tmp_path / "run"), out)
    assert len(clips) == 1
    clip = clips[0]
    assert clip.representation == "gaussians"
    assert len(clip.frames) == 3
    assert clip.counts == [5, 5, 5]
    for path in clip.frames:
        assert (out / path).is_file()


def test_exports_a_gstream_run_keeping_its_frame_numbers(tmp_path):
    out = tmp_path / "bundle"
    _, clips, _ = gaussian.build_clips(gstream_run(tmp_path / "run"), out)
    assert [name.split("_")[-1] for name in
            (path.rsplit("/", 1)[-1].removesuffix(".ply") for path in clips[0].frames)
            ] == ["0002", "0003"]


def test_the_clip_can_be_explored_because_gaussians_have_geometry(tmp_path):
    _, clips, _ = gaussian.build_clips(queen_run(tmp_path / "run"), tmp_path / "b")
    assert Representation(clips[0].representation).has_geometry is True


def test_frames_option_truncates(tmp_path):
    _, clips, _ = gaussian.build_clips(
        queen_run(tmp_path / "run"),
        tmp_path / "b",
        gaussian.GaussianExportOptions(frames=2),
    )
    assert len(clips[0].frames) == 2


def test_scene_and_method_can_be_set_to_line_up_with_another_method(tmp_path):
    _, clips, _ = gaussian.build_clips(
        queen_run(tmp_path / "run"),
        tmp_path / "b",
        gaussian.GaussianExportOptions(scene="basketball", method="queen"),
    )
    assert (clips[0].scene, clips[0].method) == ("basketball", "queen")


def test_a_guessed_scene_name_says_so_in_the_notes(tmp_path):
    _, clips, _ = gaussian.build_clips(queen_run(tmp_path / "myrun"), tmp_path / "b")
    assert clips[0].scene == "myrun"
    assert any("taken from the directory" in note for note in clips[0].notes)


def test_ply_frames_are_copied_byte_for_byte(tmp_path):
    """Re-encoding would only add a chance to get an activation or SH order wrong."""
    run = queen_run(tmp_path / "run", frames=(1,), n_rest=15)
    out = tmp_path / "b"
    _, clips, _ = gaussian.build_clips(run, out)
    original = (run / "frames" / "0001" / "point_cloud.ply").read_bytes()
    assert (out / clips[0].frames[0]).read_bytes() == original


def test_sh_degree_is_reported_for_a_copied_run(tmp_path):
    _, clips, _ = gaussian.build_clips(
        queen_run(tmp_path / "run", frames=(1,), n_rest=15), tmp_path / "b"
    )
    assert clips[0].detail["sh_degrees"] == [3]
    assert any("sh_degree 3" in note for note in clips[0].notes)


def test_refuses_something_that_is_not_a_gaussian_run(tmp_path):
    (tmp_path / "empty").mkdir()
    with pytest.raises(ValueError, match="not a 3DGS run"):
        gaussian.build_clips(tmp_path / "empty", tmp_path / "b")


def test_refuses_an_unknown_frame_format(tmp_path):
    with pytest.raises(ValueError, match="unknown frame format"):
        gaussian.build_clips(
            queen_run(tmp_path / "run"),
            tmp_path / "b",
            gaussian.GaussianExportOptions(frame_format="obj"),
        )


def test_export_writes_a_readable_bundle(tmp_path):
    out = gaussian.export(queen_run(tmp_path / "run"), tmp_path / "bundle")
    index = json.loads((out / "view.json").read_text())
    assert index["clips"][0]["representation"] == "gaussians"


# -------------------------------------------------------------- .splat form ---


def test_splat_export_is_much_smaller_and_says_what_it_dropped(tmp_path):
    run = queen_run(tmp_path / "run", frames=(1,), n_rest=15)
    _, as_ply, _ = gaussian.build_clips(run, tmp_path / "p")
    _, as_splat, _ = gaussian.build_clips(
        run, tmp_path / "s", gaussian.GaussianExportOptions(frame_format="splat")
    )
    ply_size = (tmp_path / "p" / as_ply[0].frames[0]).stat().st_size
    splat_size = (tmp_path / "s" / as_splat[0].frames[0]).stat().st_size
    assert splat_size < ply_size
    assert as_splat[0].frames[0].endswith(".splat")
    assert any("degree 0 is dropped" in note for note in as_splat[0].notes)


def test_a_splat_frame_is_exactly_32_bytes_per_gaussian(tmp_path):
    _, clips, _ = gaussian.build_clips(
        queen_run(tmp_path / "run", frames=(1,)),
        tmp_path / "b",
        gaussian.GaussianExportOptions(frame_format="splat"),
    )
    path = tmp_path / "b" / clips[0].frames[0]
    assert path.stat().st_size == 5 * splat.SPLAT_BYTES
    assert splat.count(path) == 5


def test_splat_round_trip_keeps_position_and_scale_exactly(tmp_path):
    """Those are float32 in both forms; only colour, opacity and rotation quantise."""
    ply.write(tmp_path / "f.ply", **raw_gaussians(64))
    cloud = splat.from_ply(tmp_path / "f.ply")
    back = splat.decode(splat.encode(cloud))
    assert np.array_equal(back.positions, cloud.positions)
    assert np.array_equal(back.scales, cloud.scales)
    assert np.abs(back.opacities - cloud.opacities).max() <= 1 / 255 + 1e-6
    assert np.abs(back.rotations - cloud.rotations).max() <= 1 / 128 + 1e-6
    assert np.abs(back.colors - cloud.colors).max() <= 1 / 255 + 1e-6


def test_from_ply_activates_exactly_once(tmp_path):
    fields = raw_gaussians(16)
    ply.write(tmp_path / "f.ply", **fields)
    cloud = splat.from_ply(tmp_path / "f.ply")
    assert np.allclose(cloud.scales, np.exp(fields["scale_raw"]), atol=1e-6)
    expected = 1 / (1 + np.exp(-fields["opacity_raw"].reshape(-1)))
    assert np.allclose(cloud.opacities, expected, atol=1e-6)
    assert np.allclose(np.linalg.norm(cloud.rotations, axis=1), 1.0, atol=1e-5)


def test_encode_requires_a_gaussian_cloud():
    with pytest.raises(TypeError, match="GaussianCloud"):
        splat.encode({"positions": []})


def test_encode_requires_colour():
    cloud = GaussianCloud(
        positions=np.zeros((1, 3), dtype=np.float32),
        scales=np.ones((1, 3), dtype=np.float32),
        rotations=np.asarray([[1, 0, 0, 0]], dtype=np.float32),
        opacities=np.asarray([0.5], dtype=np.float32),
    )
    with pytest.raises(ValueError, match="no colors"):
        splat.encode(cloud)


def test_count_rejects_a_file_that_is_not_a_splat(tmp_path):
    path = tmp_path / "bad.splat"
    path.write_bytes(bytes(33))
    with pytest.raises(ValueError, match="not a .splat"):
        splat.count(path)


# ----------------------------------------------------------- the PLY reader ---


def test_ply_reader_handles_the_extra_int_column_queen_writes(tmp_path):
    """QUEEN adds `property int vertex_id`; assuming all-float32 misreads every row."""
    fields = raw_gaussians(4)
    source = tmp_path / "plain.ply"
    ply.write(source, **fields)
    original = ply.read(source)

    # Rebuild the same file with an int column appended to each vertex.
    header, _, payload = source.read_bytes().partition(b"end_header\n")
    header = header.replace(b"property float rot_3\n",
                            b"property float rot_3\nproperty int vertex_id\n")
    stride = len(payload) // 4
    rows = [payload[i * stride:(i + 1) * stride] for i in range(4)]
    mixed = header + b"end_header\n" + b"".join(
        row + index.to_bytes(4, "little") for index, row in enumerate(rows)
    )
    target = tmp_path / "mixed.ply"
    target.write_bytes(mixed)

    parsed = ply.read(target)
    assert parsed["count"] == 4
    assert np.array_equal(parsed["xyz"], original["xyz"])
    assert np.array_equal(parsed["rot_raw"], original["rot_raw"])
