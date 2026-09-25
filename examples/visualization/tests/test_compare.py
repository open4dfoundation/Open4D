"""Headless tests for comparison colours, frame pairing, and CLI output."""

from __future__ import annotations

import numpy as np
import pytest

import colormaps
import compare_frames
import compare_sequences as cli
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.io import _mesh as formats_mesh
from open4d.visualization._frames import UP_TO_Z

pytestmark = pytest.mark.cpu


# Fixtures and helpers
def grid_mesh(side: int = 6, height: float = 0.0) -> tuple[np.ndarray, np.ndarray]:
    """A triangulated square in the z = `height` plane, so normals are known."""
    axis = np.linspace(0.0, 1.0, side)
    x, y = np.meshgrid(axis, axis, indexing="ij")
    positions = np.column_stack(
        [x.ravel(), y.ravel(), np.full(x.size, height)]
    ).astype(np.float64)

    triangles = []
    for row in range(side - 1):
        for column in range(side - 1):
            corner = row * side + column
            triangles.append((corner, corner + 1, corner + side))
            triangles.append((corner + 1, corner + side + 1, corner + side))
    return positions, np.asarray(triangles, dtype=np.uint32)


def sequence_of(meshes, fps: float = 10.0) -> Sequence:
    """An in-memory `Sequence` over (positions, triangles) pairs."""
    frames = [
        Frame(
            frame_index=index,
            timestamp=index / fps,
            geometry=TriangleMesh(
                positions=np.asarray(positions, dtype=np.float32),
                triangles=np.asarray(triangles, dtype=np.uint32),
            ),
        )
        for index, (positions, triangles) in enumerate(meshes)
    ]
    return Sequence(MemoryFrameProvider(frames, metadata={"fps": fps}))


@pytest.fixture
def reference_sequence() -> Sequence:
    positions, triangles = grid_mesh()
    return sequence_of([(positions, triangles)] * 3)


@pytest.fixture
def shifted_sequence() -> Sequence:
    """Raise frame i by (i + 1) / 100 along the plane normal."""
    positions, triangles = grid_mesh()
    return sequence_of(
        [(positions + [0.0, 0.0, (index + 1) / 100.0], triangles) for index in range(3)]
    )


def obj_folder(path, meshes) -> object:
    """Write (positions, triangles) pairs as numbered `.obj` frames."""
    path.mkdir(parents=True, exist_ok=True)
    for index, (positions, triangles) in enumerate(meshes):
        formats_mesh.write_obj(path / f"frame_{index}.obj", positions, triangles)
    return path


# Colormaps
def test_the_ramp_is_monotone_in_lightness():
    luminance = colormaps.relative_luminance(colormaps.lookup_table())
    steps = np.diff(luminance)

    assert np.all(steps >= 0.0) or np.all(steps <= 0.0), (
        "the ramp reverses direction in lightness"
    )
    assert abs(luminance[-1] - luminance[0]) > 0.5


def test_lookup_table_shape_and_range():
    table = colormaps.lookup_table()
    assert table.shape == (colormaps.LUT_SIZE, 3)
    assert table.dtype == np.float32
    assert table.min() >= 0.0 and table.max() <= 1.0


def test_colorize_hits_both_ends_and_clamps_beyond_them():
    table = colormaps.lookup_table()
    values = np.array([-1.0, 0.0, 0.5, 1.0, 99.0])
    colors = colormaps.colorize(values, 0.0, 1.0)

    assert colors[0] == pytest.approx(table[0])   # below the floor
    assert colors[1] == pytest.approx(table[0])
    assert colors[3] == pytest.approx(table[-1])
    assert colors[4] == pytest.approx(table[-1])  # above the clamp
    assert colors.shape == (5, 3)


def test_colorize_marks_unmeasurable_vertices_off_the_ramp():
    table = colormaps.lookup_table()
    colors = colormaps.colorize(np.array([0.5, np.nan, np.inf]), 0.0, 1.0)

    assert colors[1] == pytest.approx(colormaps.NO_DATA)
    assert colors[2] == pytest.approx(colormaps.NO_DATA)
    assert not np.allclose(colors[1], table[0])
    assert not np.allclose(colors[1], table[-1])


def test_normalize_treats_a_degenerate_range_as_the_floor():
    assert colormaps.normalize(np.zeros(4), 0.0, 0.0) == pytest.approx(0.0)
    assert colormaps.normalize(np.ones(4), 1.0, 0.0) == pytest.approx(0.0)


def test_colorbar_strip_runs_from_the_bottom_of_the_ramp_to_the_top():
    strip = colormaps.colorbar_strip(64, 5)
    assert strip.shape == (5, 64, 3)
    assert strip.dtype == np.uint8

    table = colormaps.lookup_table()
    assert strip[0, 0] == pytest.approx((table[0] * 255).round(), abs=1)
    assert strip[0, -1] == pytest.approx((table[-1] * 255).round(), abs=1)
    # Every row is the same gradient.
    assert np.array_equal(strip[0], strip[-1])


# Pairing and per-frame comparison
def test_pairing_truncates_to_the_shorter_sequence_and_says_so():
    positions, triangles = grid_mesh()
    reference = sequence_of([(positions, triangles)] * 5)
    decoded = sequence_of([(positions, triangles)] * 3)

    pairs, truncated = compare_frames.pair_frames(reference, decoded)
    assert len(pairs) == 3
    assert truncated == (5, 3)


def test_pairing_reports_no_truncation_when_lengths_agree():
    positions, triangles = grid_mesh()
    sequence = sequence_of([(positions, triangles)] * 4)

    _pairs, truncated = compare_frames.pair_frames(sequence, sequence)
    assert truncated is None


def test_pairing_applies_stride():
    positions, triangles = grid_mesh()
    sequence = sequence_of([(positions, triangles)] * 6)

    pairs, _ = compare_frames.pair_frames(sequence, sequence, stride=2)
    assert [pair[0].frame_index for pair in pairs] == [0, 2, 4]


def test_pairing_rejects_a_zero_length_sequence():
    positions, triangles = grid_mesh()
    empty = sequence_of([])
    with pytest.raises(ValueError, match="at least one frame"):
        compare_frames.pair_frames(sequence_of([(positions, triangles)]), empty)


def test_the_up_axis_permutation_does_not_change_the_distances(
    reference_sequence, shifted_sequence
):
    plain = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, order=[0, 1, 2]
    )
    rotated = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, order=UP_TO_Z["y"]
    )

    for left, right in zip(plain.frames, rotated.frames):
        assert left.decoded_distances == pytest.approx(right.decoded_distances)
    # Rotation changes display coordinates, not measured distances.
    assert not np.allclose(
        plain.frames[0].decoded.positions, rotated.frames[0].decoded.positions
    )


def test_the_colour_scale_is_one_value_for_the_whole_sequence(
    reference_sequence, shifted_sequence
):
    comparison = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, percentile=None
    )
    assert comparison.clamp == pytest.approx(0.03)

    first = compare_frames.error_vertex_colors(
        comparison.frames[0], "decoded", comparison.clamp, shading=0.0
    )
    last = compare_frames.error_vertex_colors(
        comparison.frames[2], "decoded", comparison.clamp, shading=0.0
    )
    assert colormaps.relative_luminance(last[:, :3]).mean() > (
        colormaps.relative_luminance(first[:, :3]).mean()
    )


@pytest.mark.parametrize(
    "kwargs, expected",
    [
        ({"max_error": 0.5}, 0.5),
        ({"percentile": None}, 0.03),
        ({"percentile": 100.0}, 0.03),
    ],
)
def test_the_colour_scale_comes_from_the_requested_source(
    reference_sequence, shifted_sequence, kwargs, expected
):
    comparison = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, **kwargs
    )
    assert comparison.clamp == pytest.approx(expected)


def test_a_percentile_scale_sits_below_the_maximum(
    reference_sequence, shifted_sequence
):
    comparison = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, percentile=50.0
    )
    assert comparison.clamp < 0.03
    assert comparison.percentile == 50.0


def test_an_explicit_scale_is_not_labelled_as_a_clamp(
    reference_sequence, shifted_sequence
):
    comparison = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, max_error=0.5, percentile=99.0
    )
    assert comparison.percentile is None


def test_error_colours_are_rgba_and_opaque(reference_sequence, shifted_sequence):
    comparison = compare_frames.compare_sequences(reference_sequence, shifted_sequence)
    frame = comparison.frames[0]

    colors = compare_frames.error_vertex_colors(frame, "decoded", comparison.clamp)
    assert colors.shape == (len(frame.decoded.positions), 4)
    assert colors.dtype == np.float32
    assert colors[:, 3] == pytest.approx(1.0)
    assert colors[:, :3].min() >= 0.0 and colors[:, :3].max() <= 1.0


def test_zero_shading_leaves_the_ramp_untouched(reference_sequence, shifted_sequence):
    comparison = compare_frames.compare_sequences(reference_sequence, shifted_sequence)
    frame = comparison.frames[0]

    plain = compare_frames.error_vertex_colors(
        frame, "decoded", comparison.clamp, shading=0.0
    )
    expected = colormaps.colorize(
        frame.decoded_distances, 0.0, comparison.clamp
    )
    assert plain[:, :3] == pytest.approx(expected, abs=1e-6)

    shaded = compare_frames.error_vertex_colors(
        frame, "decoded", comparison.clamp, shading=1.0
    )
    assert not np.allclose(shaded[:, :3], expected)


def test_error_colours_can_be_asked_for_either_direction(
    reference_sequence, shifted_sequence
):
    comparison = compare_frames.compare_sequences(reference_sequence, shifted_sequence)
    frame = comparison.frames[0]

    assert len(frame.distances_for("decoded")) == len(frame.decoded.positions)
    assert len(frame.distances_for("reference")) == len(frame.reference.positions)
    with pytest.raises(ValueError, match="must be 'decoded' or 'reference'"):
        frame.distances_for("both")


def test_progress_is_reported_once_per_frame(reference_sequence, shifted_sequence):
    seen = []
    compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, progress=lambda done, total: seen.append((done, total))
    )
    assert seen == [(1, 3), (2, 3), (3, 3)]


def test_summary_finds_the_worst_frame(reference_sequence, shifted_sequence):
    comparison = compare_frames.compare_sequences(reference_sequence, shifted_sequence)
    summary = comparison.summary()

    assert summary.worst_frame == 2  # the largest offset
    assert summary.hausdorff == pytest.approx(0.03)
    assert summary.symmetric_rms > 0.0


@pytest.mark.parametrize(
    "kwargs, message",
    [
        ({"metric": "chamfer"}, "metric must be"),
        ({"stride": 0}, "stride must be at least 1"),
        ({"max_error": -1.0}, "max_error must be greater than zero"),
    ],
)
def test_compare_sequences_rejects_bad_arguments(
    reference_sequence, shifted_sequence, kwargs, message
):
    with pytest.raises(ValueError, match=message):
        compare_frames.compare_sequences(
            reference_sequence, shifted_sequence, **kwargs
        )


def test_point_to_plane_is_available_end_to_end(reference_sequence, shifted_sequence):
    comparison = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, metric="plane"
    )
    assert comparison.metric == "plane"
    # The shift is along the plane normal, so both metrics see all of it.
    assert comparison.frames[0].decoded_distances == pytest.approx(0.01)


# The command line
@pytest.fixture
def folders(tmp_path):
    """A reference folder and a decoded folder lifted 0.01 along the normal."""
    positions, triangles = grid_mesh()
    reference = obj_folder(tmp_path / "reference", [(positions, triangles)] * 3)
    decoded = obj_folder(
        tmp_path / "decoded",
        [(positions + [0.0, 0.0, 0.01], triangles)] * 3,
    )
    return reference, decoded


def run_cli(argv) -> int:
    args = cli.build_parser().parse_args([str(item) for item in argv])
    cli.validate(cli.build_parser(), args)
    return cli.run(args)


def test_info_reports_the_table_and_the_summary(folders, capsys):
    reference, decoded = folders
    assert run_cli([reference, decoded, "--info"]) == 0

    output = capsys.readouterr().out
    assert "point-to-point error, 3 frames" in output
    assert "sequence symmetric RMS" in output
    assert "worst frame" in output
    # Three data rows plus the header.
    assert output.count("\n      0  ") == 1
    assert "0.01" in output


def test_info_needs_no_gui(folders, monkeypatch, capsys):
    import builtins

    real_import = builtins.__import__

    def fail_on_qt(name, *args, **kwargs):
        # Block viewer imports as well as their Qt dependencies.
        if name.startswith(("PyQt6", "pyqtgraph", "OpenGL", "viewer_")):
            raise AssertionError(f"--info must not import {name}")
        return real_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", fail_on_qt)
    reference, decoded = folders
    assert run_cli([reference, decoded, "--info"]) == 0


def test_csv_has_one_row_per_frame_and_matching_numbers(folders, tmp_path, capsys):
    import csv as csv_module

    reference, decoded = folders
    target = tmp_path / "out" / "error.csv"
    assert run_cli([reference, decoded, "--info", "--csv", target]) == 0

    with open(target, encoding="utf-8", newline="") as stream:
        rows = list(csv_module.DictReader(stream))

    assert len(rows) == 3
    assert list(rows[0]) == list(cli.CSV_COLUMNS)
    assert float(rows[0]["symmetric_rms"]) == pytest.approx(0.01, rel=1e-3)
    assert float(rows[0]["hausdorff"]) == pytest.approx(0.01, rel=1e-3)
    assert int(rows[0]["reference_vertices"]) == 36
    assert int(rows[2]["frame"]) == 2


def test_the_metric_flag_reaches_the_report(folders, capsys):
    reference, decoded = folders
    run_cli([reference, decoded, "--info", "--metric", "plane"])
    assert "point-to-plane error" in capsys.readouterr().out


def test_a_length_mismatch_is_reported(tmp_path, capsys):
    positions, triangles = grid_mesh()
    reference = obj_folder(tmp_path / "reference", [(positions, triangles)] * 4)
    decoded = obj_folder(tmp_path / "decoded", [(positions, triangles)] * 2)

    run_cli([reference, decoded, "--info"])
    output = capsys.readouterr().out
    assert "lengths differ (4 reference, 2 decoded)" in output
    assert "compared the first 2" in output


def test_an_exact_match_is_labelled_as_such(tmp_path, capsys):
    positions, triangles = grid_mesh()
    folder = obj_folder(tmp_path / "same", [(positions, triangles)] * 2)

    run_cli([folder, folder, "--info"])
    output = capsys.readouterr().out
    assert "colour top : 0" in output
    assert "inf" in output  # PSNR of a perfect match


@pytest.mark.parametrize(
    "flags",
    [
        ["--stride", "0"],
        ["--fps", "0"],
        ["--max-error", "0"],
        ["--percentile", "0"],
        ["--percentile", "101"],
        ["--error-shading", "2"],
    ],
)
def test_the_cli_rejects_out_of_range_flags(folders, flags):
    reference, decoded = folders
    with pytest.raises(SystemExit):
        run_cli([reference, decoded, "--info", *flags])


def test_no_arguments_prints_the_help_and_exits(capsys):
    parser = cli.build_parser()
    args = parser.parse_args([])
    with pytest.raises(SystemExit):
        cli.validate(parser, args)
    assert "per-frame files" in capsys.readouterr().out


def test_the_pane_layout_is_reference_then_error():
    import viewer_compare_qt

    assert [(spec.which, spec.mode) for spec in viewer_compare_qt.PANES] == [
        ("reference", "shaded"),
        ("decoded", "error"),
    ]


def test_colourbar_ticks_label_a_clamp_only_when_clamping():
    import viewer_compare_qt

    clamped = viewer_compare_qt.colorbar_ticks(0.5, percentile=99.0)
    assert clamped[-1] == (1.0, "≥ 0.5")
    assert clamped[0] == (0.0, "0")

    exact = viewer_compare_qt.colorbar_ticks(0.5, percentile=None)
    assert exact[-1] == (1.0, "0.5")

    perfect = viewer_compare_qt.colorbar_ticks(0.0, percentile=99.0)
    assert "exact match" in perfect[-1][1]


def test_metrics_overlay_names_the_direction_it_shows():
    import viewer_compare_qt

    positions, triangles = grid_mesh()
    reference = sequence_of([(positions, triangles)] * 2)
    decoded = sequence_of([(positions + [0.0, 0.0, 0.01], triangles)] * 2)
    comparison = compare_frames.compare_sequences(reference, decoded)

    lines = viewer_compare_qt.metrics_lines(comparison, 0, 10.0)
    assert any("decoded → reference" in line for line in lines)


@pytest.mark.player
@pytest.mark.parametrize("width", [5, 6, 7])
def test_comparison_framebuffer_preserves_padded_rgb_rows(width):
    from types import SimpleNamespace
    QtGui = pytest.importorskip("PyQt6.QtGui")
    Image = pytest.importorskip("PIL.Image")
    import viewer_compare_qt

    expected = np.arange(width * 4 * 3, dtype=np.uint8).reshape(4, width, 3)
    framebuffer = QtGui.QImage(width, 4, QtGui.QImage.Format.Format_RGB888)
    for row in range(4):
        for column in range(width):
            framebuffer.setPixelColor(column, row, QtGui.QColor(*map(int, expected[row, column])))
    view = SimpleNamespace(grabFramebuffer=lambda: framebuffer)
    picture = viewer_compare_qt._framebuffer(view, SimpleNamespace(width=width, height=4), Image)
    np.testing.assert_array_equal(np.asarray(picture), expected)


@pytest.mark.player
@pytest.mark.slow
def test_comparison_gif_after_single_viewer(tmp_path, folders):
    import os
    import warnings
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from PIL import Image
    from open4d.demo import mesh_sequence
    from open4d.visualization import render_gif

    with mesh_sequence(side=3, frames=2) as source:
        render_gif(source, tmp_path / "single.gif", width=65, height=65, no_metrics=True)
    destination = tmp_path / "comparison.gif"
    args = cli.build_parser().parse_args([str(folders[0]), str(folders[1]),
                                         "--save", str(destination), "--width", "101",
                                         "--height", "101"])
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        cli.run(args)
    assert not [warning for warning in caught if issubclass(warning.category, RuntimeWarning)]
    with Image.open(destination) as image:
        assert image.n_frames > 1
        assert image.width == 210


def test_comparison_stride_keeps_playback_speed(folders):
    args = cli.build_parser().parse_args([str(folders[0]), str(folders[1]), "--stride", "2", "--info"])
    cli.run(args)
    assert args.fps == 15
def test_sparse_errors_do_not_collapse_the_color_scale():
    from types import SimpleNamespace
    from compare_frames import resolve_clamp

    distances = np.zeros(1000)
    distances[-1] = 1
    frames = [SimpleNamespace(decoded_distances=distances, reference_distances=distances)]
    assert resolve_clamp(frames, None, 99) > 0


def test_summary_psnr_uses_aggregate_error(reference_sequence, shifted_sequence):
    from compare_frames import compare_sequences, Comparison
    from open4d.metrics import SequenceComparison

    exact = compare_sequences(reference_sequence, reference_sequence)
    shifted = compare_sequences(reference_sequence, shifted_sequence)
    frames = [exact.frames[0], shifted.frames[0]]
    combined = Comparison(frames, "point", shifted.peak, shifted.clamp, 99, None)
    expected = SequenceComparison(tuple(f.error for f in frames), (0., 1.), shifted.peak, "point")
    assert np.isfinite(combined.summary().mean_psnr_db)
    assert combined.summary().mean_psnr_db == pytest.approx(expected.symmetric_psnr_db)
