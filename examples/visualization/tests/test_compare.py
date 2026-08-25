"""Tests for the comparison viewer's measurement, colouring, and CLI.

Everything here runs headless. The GL viewer itself is exercised by
`compare_sequences.py --save`, which needs a graphical session; what is tested
here is every decision that determines what that window shows — the distances,
the scale, the colours, and the reported numbers.

Three things are checked against something other than themselves, because a
metric that only agrees with itself is not evidence:

- the nearest-neighbour search against brute force, written out longhand;
- the error of a known quantizer against its closed-form RMS and bound;
- the colour ramp's monotonicity against computed Rec. 709 luminance.
"""

from __future__ import annotations

import numpy as np
import pytest

import colormaps
import compare_frames
import compare_sequences as cli
import mesh_metrics
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.io import _mesh as formats_mesh
from open4d.visualization._frames import UP_TO_Z

pytestmark = pytest.mark.cpu


# ----------------------------
# Fixtures and helpers
# ----------------------------
def brute_force(queries: np.ndarray, reference: np.ndarray) -> np.ndarray:
    """The definition of nearest-neighbour distance, written out."""
    if len(queries) == 0:
        return np.empty(0)
    delta = queries[:, None, :] - reference[None, :, :]
    return np.min(np.linalg.norm(delta, axis=2), axis=1)


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
    """The reference lifted along the plane normal, by a different amount each frame.

    Frame *i* is offset by (i + 1) / 100, so the frames are genuinely unequal and
    a per-frame colour rescale would be visible as all three looking the same.
    """
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


# ----------------------------
# Nearest neighbours
# ----------------------------
POINT_SETS = {
    "uniform": (lambda r: r.random((120, 3)), lambda r: r.random((200, 3))),
    "clustered": (
        lambda r: r.normal(0.0, 0.01, (80, 3)),
        lambda r: r.normal(0.0, 0.01, (90, 3)),
    ),
    "disjoint": (lambda r: r.random((40, 3)) + 50.0, lambda r: r.random((40, 3))),
    "flat sheet": (
        lambda r: np.column_stack([r.random((60, 2)), np.zeros(60)]),
        lambda r: np.column_stack([r.random((70, 2)), np.zeros(70)]),
    ),
    "collinear": (
        lambda r: np.column_stack([r.random(50), np.zeros((50, 2))]),
        lambda r: np.column_stack([r.random(60), np.zeros((60, 2))]),
    ),
    "single reference": (lambda r: r.random((30, 3)), lambda r: np.zeros((1, 3))),
    "duplicate references": (
        lambda r: r.random((40, 3)),
        lambda r: np.repeat(r.random((4, 3)), 10, axis=0),
    ),
    "mixed scale": (
        lambda r: r.random((60, 3)) * 1000.0,
        lambda r: r.random((60, 3)) * 0.001,
    ),
}


@pytest.mark.parametrize("name", sorted(POINT_SETS))
def test_nearest_neighbors_matches_brute_force(name):
    rng = np.random.default_rng(0)
    make_queries, make_reference = POINT_SETS[name]
    queries, reference = make_queries(rng), make_reference(rng)

    result = mesh_metrics.nearest_neighbors(queries, reference)
    assert result.distances == pytest.approx(brute_force(queries, reference))


@pytest.mark.parametrize("name", sorted(POINT_SETS))
def test_reported_index_is_the_point_that_was_measured(name):
    """The distance and the index have to describe the same reference point."""
    rng = np.random.default_rng(1)
    make_queries, make_reference = POINT_SETS[name]
    queries, reference = make_queries(rng), make_reference(rng)

    result = mesh_metrics.nearest_neighbors(queries, reference)
    measured = np.linalg.norm(queries - reference[result.indices], axis=1)
    assert measured == pytest.approx(result.distances)


def test_duplicate_reference_vertices_report_a_real_index():
    reference = np.zeros((5, 3))
    result = mesh_metrics.nearest_neighbors(np.ones((1, 3)), reference)
    assert result.distances[0] == pytest.approx(np.sqrt(3.0))
    assert 0 <= result.indices[0] < len(reference)


def test_empty_queries_return_empty_arrays():
    result = mesh_metrics.nearest_neighbors(np.empty((0, 3)), np.zeros((3, 3)))
    assert len(result.distances) == 0
    assert len(result.indices) == 0


@pytest.mark.parametrize(
    "queries, reference, message",
    [
        (np.zeros((2, 2)), np.zeros((3, 3)), "queries must have shape"),
        (np.zeros((2, 3)), np.zeros((3, 2)), "reference must have shape"),
        (np.zeros((2, 3)), np.zeros((0, 3)), "reference is empty"),
    ],
)
def test_nearest_neighbors_rejects_bad_input(queries, reference, message):
    with pytest.raises(ValueError, match=message):
        mesh_metrics.nearest_neighbors(queries, reference)


# ----------------------------
# Normals and point-to-plane
# ----------------------------
def test_vertex_normals_of_a_plane_point_along_its_axis():
    positions, triangles = grid_mesh()
    normals = mesh_metrics.vertex_normals(positions, triangles)

    assert np.linalg.norm(normals, axis=1) == pytest.approx(1.0)
    assert np.abs(normals[:, 2]) == pytest.approx(1.0)
    assert normals[:, :2] == pytest.approx(0.0)


def test_vertex_normals_are_zero_where_undefined():
    positions, triangles = grid_mesh()
    loose = np.vstack([positions, [[5.0, 5.0, 5.0]]])
    normals = mesh_metrics.vertex_normals(loose, triangles)

    assert normals[-1] == pytest.approx(0.0)  # touched by no triangle
    assert mesh_metrics.vertex_normals(positions, np.empty((0, 3))) == pytest.approx(
        0.0
    )


def test_point_to_plane_ignores_a_slide_along_the_surface():
    """Error tangential to the reference surface is not depth error.

    A plane shifted within its own plane still lies on that plane, so
    point-to-plane sees nothing while point-to-point sees the whole shift.
    """
    positions, triangles = grid_mesh(side=12)
    normals = mesh_metrics.vertex_normals(positions, triangles)
    # A shift small enough that the nearest vertex is a neighbour on the plane.
    slid = positions + [0.04, 0.0, 0.0]

    tangential = mesh_metrics.point_to_plane(slid, positions, normals)
    straight = mesh_metrics.point_to_point(slid, positions)

    assert tangential == pytest.approx(0.0, abs=1e-12)
    assert np.mean(straight) > 0.03


def test_point_to_plane_measures_offset_along_the_normal():
    positions, triangles = grid_mesh(side=12)
    normals = mesh_metrics.vertex_normals(positions, triangles)
    lifted = positions + [0.0, 0.0, 0.02]

    assert mesh_metrics.point_to_plane(lifted, positions, normals) == pytest.approx(
        0.02
    )


def test_point_to_plane_falls_back_where_the_normal_is_undefined():
    """Without a normal there is no plane, so the honest answer is the distance."""
    reference = np.zeros((1, 3))
    queries = np.array([[0.0, 0.0, 0.5]])
    normals = np.zeros((1, 3))

    assert mesh_metrics.point_to_plane(queries, reference, normals) == pytest.approx(
        0.5
    )


def test_point_to_plane_rejects_mismatched_normals():
    with pytest.raises(ValueError, match="one row per reference vertex"):
        mesh_metrics.point_to_plane(
            np.zeros((2, 3)), np.zeros((3, 3)), np.zeros((2, 3))
        )


# ----------------------------
# The metric against closed-form answers
# ----------------------------
def test_quantization_error_matches_its_closed_form():
    """A known quantizer has a known RMS error, and the metric should find it.

    Rounding each coordinate to a multiple of `step` gives an offset uniform on
    +/- step/2 per axis, so the 3-D RMS is step * sqrt(3/12) and no offset can
    exceed step * sqrt(3)/2. The points are spread far enough apart that the
    nearest reference vertex is the one each point came from.
    """
    step = 0.05
    reference = np.random.default_rng(4).random((4000, 3)) * 10.0
    decoded = np.round(reference / step) * step

    distances = mesh_metrics.point_to_point(decoded, reference)
    expected_rms = step * np.sqrt(3.0 / 12.0)

    assert np.sqrt(np.mean(distances ** 2)) == pytest.approx(expected_rms, rel=0.05)
    assert distances.max() <= step * np.sqrt(3.0) / 2.0 + 1e-12


def test_psnr_follows_the_definition():
    distances = np.full(10, 0.5)
    summary = mesh_metrics.DirectionalError.summarize(distances, peak=10.0)

    assert summary.rms == pytest.approx(0.5)
    assert summary.mean == pytest.approx(0.5)
    assert summary.maximum == pytest.approx(0.5)
    assert summary.psnr_db == pytest.approx(10.0 * np.log10(100.0 / 0.25))


def test_identical_meshes_have_no_error_and_infinite_psnr():
    positions, triangles = grid_mesh()
    result = mesh_metrics.compare_meshes(positions, triangles, positions, triangles)

    assert result.decoded_distances == pytest.approx(0.0)
    assert result.reference_distances == pytest.approx(0.0)
    assert result.symmetric_rms == pytest.approx(0.0)
    assert result.forward.psnr_db == np.inf
    assert result.symmetric_psnr_db == np.inf


def test_psnr_is_undefined_rather_than_perfect_without_a_scale():
    """A degenerate reference has no bounding box, so PSNR has no peak.

    Reporting `inf` here would read as a perfect match when in fact the error is
    nonzero and only the scale is missing.
    """
    summary = mesh_metrics.DirectionalError.summarize(np.full(4, 0.25), peak=0.0)
    assert np.isnan(summary.psnr_db)


def test_symmetric_figures_take_the_worse_direction():
    """Deleting geometry is invisible in one direction and obvious in the other."""
    positions, triangles = grid_mesh(side=12)
    keep = positions[:, 0] < 0.5
    partial = positions[keep]

    result = mesh_metrics.compare_meshes(
        positions, triangles, partial, np.empty((0, 3), dtype=np.uint32)
    )

    # Every surviving vertex sits exactly on the reference.
    assert result.forward.rms == pytest.approx(0.0)
    # The half that was dropped has nothing near it.
    assert result.backward.rms > 0.1
    assert result.symmetric_rms == result.backward.rms
    assert result.hausdorff == result.backward.maximum
    assert result.symmetric_psnr_db == result.backward.psnr_db


def test_bounding_box_diagonal():
    positions = np.array([[0.0, 0.0, 0.0], [3.0, 4.0, 0.0]])
    assert mesh_metrics.bounding_box_diagonal(positions) == pytest.approx(5.0)
    assert mesh_metrics.bounding_box_diagonal(np.empty((0, 3))) == 0.0


def test_compare_meshes_rejects_an_unknown_metric():
    positions, triangles = grid_mesh()
    with pytest.raises(ValueError, match="metric must be"):
        mesh_metrics.compare_meshes(
            positions, triangles, positions, triangles, metric="hausdorff"
        )


# ----------------------------
# Colormaps
# ----------------------------
def test_the_ramp_is_monotone_in_lightness():
    """The property that makes a sequential ramp readable, checked not eyeballed."""
    luminance = colormaps.relative_luminance(colormaps.lookup_table())
    steps = np.diff(luminance)

    assert np.all(steps >= 0.0) or np.all(steps <= 0.0), (
        "the ramp reverses direction in lightness"
    )
    # And it must actually travel, or magnitude has nowhere to show.
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
    """An exact match gives a zero-width scale; it must not divide by zero."""
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


# ----------------------------
# Pairing and per-frame comparison
# ----------------------------
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
    """Reorienting for the viewer is rigid, so it must not move the measurement."""
    plain = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, order=[0, 1, 2]
    )
    rotated = compare_frames.compare_sequences(
        reference_sequence, shifted_sequence, order=UP_TO_Z["y"]
    )

    for left, right in zip(plain.frames, rotated.frames):
        assert left.decoded_distances == pytest.approx(right.decoded_distances)
    # ...but the geometry handed to the viewer really was permuted.
    assert not np.allclose(
        plain.frames[0].decoded.positions, rotated.frames[0].decoded.positions
    )


def test_the_colour_scale_is_one_value_for_the_whole_sequence(
    reference_sequence, shifted_sequence
):
    """Per-frame rescaling would make frames incomparable; assert it does not happen.

    Frame 2's offset is three times frame 0's, so its colours must come out
    brighter on the shared scale rather than identical.
    """
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
    """The point of a percentile: one stray vertex must not set the scale."""
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
    """The claim that lightness is data alone, checked at the boundary."""
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


# ----------------------------
# The command line
# ----------------------------
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
    """`--info` must not import the viewer: it is the form used over ssh."""
    import builtins

    real_import = builtins.__import__

    def fail_on_qt(name, *args, **kwargs):
        # The viewer module too, not just Qt: it is what pulls Qt in, and
        # catching it here is what keeps the import inside `run` below the
        # `--info` return.
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
