from __future__ import annotations

from open4d.codec._npz import NumPyZipCodec

import builtins

import numpy as np
import pytest

from open4d import (
    Frame, MemoryFrameProvider, Sequence, TriangleMesh,
    compare_meshes, compare_sequences, load, save,
)
from open4d import metrics
from open4d.demo import mesh_sequence

pytestmark = pytest.mark.cpu


def mesh(height=0.0, scale=1.0, extra_vertex=False):
    positions = [[0, 0, height], [scale, 0, height], [0, scale, height]]
    if extra_vertex:
        positions.append([scale / 2, scale / 2, height])
    return TriangleMesh(positions, [[0, 1, 2]])


def sequence(meshes, timestamps=None):
    times = timestamps if timestamps is not None else np.arange(len(meshes)) / 30
    return Sequence(MemoryFrameProvider([
        Frame(index, float(time), geometry)
        for index, (geometry, time) in enumerate(zip(meshes, times))
    ]))


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


@pytest.mark.parametrize("metric", ["point", "plane"])
def test_public_comparison_measures_a_known_displacement(metric):
    result = compare_meshes(mesh(), mesh(height=0.25), metric=metric)
    assert result.metric == metric
    assert result.symmetric_rms == pytest.approx(0.25)
    assert result.hausdorff == pytest.approx(0.25)
    np.testing.assert_allclose(result.decoded_distances, 0.25)
    np.testing.assert_allclose(result.reference_distances, 0.25)
    assert result.peak == pytest.approx(np.sqrt(2))
    assert result.symmetric_psnr_db == pytest.approx(20 * np.log10(np.sqrt(2) / 0.25))


def test_sequence_weights_frames_equally_and_uses_one_peak():
    reference = sequence([mesh(), mesh(scale=2, extra_vertex=True)])
    decoded = sequence([mesh(height=0.25), mesh(height=0.5, scale=2, extra_vertex=True)])
    result = compare_sequences(reference, decoded)
    rms = np.sqrt((0.25 ** 2 + 0.5 ** 2) / 2)
    assert result.symmetric_rms == pytest.approx(rms)
    assert result.hausdorff == pytest.approx(0.5)
    assert result.worst_frame == 1
    assert result.timestamps == reference.timestamps
    assert result.peak == pytest.approx(np.sqrt(8))
    assert result.symmetric_psnr_db == pytest.approx(20 * np.log10(np.sqrt(8) / rms))
    for frame, error in zip(result.frames, (0.25, 0.5)):
        assert frame.peak == result.peak
        assert frame.forward.psnr_db == pytest.approx(20 * np.log10(np.sqrt(8) / error))
        assert frame.backward.psnr_db == frame.forward.psnr_db
    assert not reference.closed and not decoded.closed


def test_comparison_after_codec_round_trip(tmp_path):
    with mesh_sequence(side=4, frames=3) as reference:
        artifact = save(reference, tmp_path / "wave.o4d", codec=NumPyZipCodec())
        with load(artifact, codec=NumPyZipCodec()) as decoded:
            result = compare_sequences(reference, decoded, peak=10)
    assert result.symmetric_rms == 0
    assert result.symmetric_psnr_db == float("inf")
    assert result.peak == 10
    assert all(frame.peak == 10 for frame in result.frames)


def test_sequence_rejects_dropped_frames_and_empty_inputs():
    with pytest.raises(ValueError, match="same frame count"):
        compare_sequences(sequence([mesh(), mesh()]), sequence([mesh()]))
    with pytest.raises(ValueError, match="at least one frame"):
        compare_sequences(sequence([]), sequence([]))


def test_timestamp_tolerance_is_absolute_and_can_be_overridden():
    reference = sequence([mesh(), mesh()], [1_000_000, 1_000_001])
    decoded = sequence([mesh(), mesh()], [1_000_000, 1_000_001.01])
    with pytest.raises(ValueError, match="timestamps differ at frame 1"):
        compare_sequences(reference, decoded)
    assert compare_sequences(reference, decoded, timestamp_tolerance=0.02).symmetric_rms == 0
    near = sequence([mesh(), mesh()], [1_000_000, 1_000_001.0000005])
    assert compare_sequences(reference, near).symmetric_rms == 0
    with pytest.raises(ValueError, match="timestamps differ"):
        compare_sequences(reference, near, timestamp_tolerance=0)


@pytest.mark.parametrize("value", [-1, float("inf"), float("nan")])
def test_invalid_peak_and_tolerance_are_rejected(value):
    reference = sequence([mesh()])
    with pytest.raises(ValueError, match="peak"):
        compare_meshes(mesh(), mesh(), peak=value)
    with pytest.raises(ValueError, match="peak"):
        compare_sequences(reference, reference, peak=value)
    with pytest.raises(ValueError, match="timestamp_tolerance"):
        compare_sequences(reference, reference, timestamp_tolerance=value)


def test_empty_mesh_is_rejected_in_either_direction():
    empty = TriangleMesh(np.empty((0, 3)), np.empty((0, 3), dtype=np.uint32))
    for reference, decoded in [(empty, mesh()), (mesh(), empty)]:
        with pytest.raises(ValueError, match="at least one vertex"):
            compare_meshes(reference, decoded)


def test_nonzero_error_with_zero_peak_has_undefined_psnr():
    reference = TriangleMesh([[0.0, 0, 0]], np.empty((0, 3), dtype=np.uint32))
    decoded = TriangleMesh([[0.0, 0, 0], [1.0, 0, 0]], np.empty((0, 3), dtype=np.uint32))
    result = compare_meshes(reference, decoded)
    assert np.isnan(result.symmetric_psnr_db)
    assert np.isnan(compare_sequences(sequence([reference]), sequence([decoded])).symmetric_psnr_db)


@pytest.mark.parametrize("missing", [True, False])
def test_scipy_import_errors_preserve_the_cause(monkeypatch, missing):
    original = builtins.__import__
    failure = (
        ModuleNotFoundError("No module named scipy", name="scipy")
        if missing else ImportError("broken SciPy binary")
    )

    def without_scipy(name, *args, **kwargs):
        if name.startswith("scipy"):
            raise failure
        return original(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", without_scipy)
    message = r"open4d\[metrics\]" if missing else "broken SciPy binary"
    with pytest.raises(ImportError, match=message) as caught:
        compare_meshes(mesh(), mesh())
    if not missing:
        assert caught.value is failure


# Nearest neighbours
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

    result = metrics.nearest_neighbors(queries, reference)
    assert result.distances == pytest.approx(brute_force(queries, reference))


@pytest.mark.parametrize("name", sorted(POINT_SETS))
def test_reported_index_is_the_point_that_was_measured(name):
    rng = np.random.default_rng(1)
    make_queries, make_reference = POINT_SETS[name]
    queries, reference = make_queries(rng), make_reference(rng)

    result = metrics.nearest_neighbors(queries, reference)
    measured = np.linalg.norm(queries - reference[result.indices], axis=1)
    assert measured == pytest.approx(result.distances)


def test_duplicate_reference_vertices_report_a_real_index():
    reference = np.zeros((5, 3))
    result = metrics.nearest_neighbors(np.ones((1, 3)), reference)
    assert result.distances[0] == pytest.approx(np.sqrt(3.0))
    assert 0 <= result.indices[0] < len(reference)


def test_empty_queries_return_empty_arrays():
    result = metrics.nearest_neighbors(np.empty((0, 3)), np.zeros((3, 3)))
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
        metrics.nearest_neighbors(queries, reference)


# Normals and point-to-plane
def test_vertex_normals_of_a_plane_point_along_its_axis():
    positions, triangles = grid_mesh()
    normals = metrics.vertex_normals(positions, triangles)

    assert np.linalg.norm(normals, axis=1) == pytest.approx(1.0)
    assert np.abs(normals[:, 2]) == pytest.approx(1.0)
    assert normals[:, :2] == pytest.approx(0.0)


def test_vertex_normals_are_zero_where_undefined():
    positions, triangles = grid_mesh()
    loose = np.vstack([positions, [[5.0, 5.0, 5.0]]])
    normals = metrics.vertex_normals(loose, triangles)

    assert normals[-1] == pytest.approx(0.0)  # touched by no triangle
    assert metrics.vertex_normals(positions, np.empty((0, 3))) == pytest.approx(
        0.0
    )


def test_point_to_plane_ignores_a_slide_along_the_surface():
    positions, triangles = grid_mesh(side=12)
    normals = metrics.vertex_normals(positions, triangles)
    # A shift small enough that the nearest vertex is a neighbour on the plane.
    slid = positions + [0.04, 0.0, 0.0]

    tangential = metrics.point_to_plane(slid, positions, normals)
    straight = metrics.point_to_point(slid, positions)

    assert tangential == pytest.approx(0.0, abs=1e-12)
    assert np.mean(straight) > 0.03


def test_point_to_plane_measures_offset_along_the_normal():
    positions, triangles = grid_mesh(side=12)
    normals = metrics.vertex_normals(positions, triangles)
    lifted = positions + [0.0, 0.0, 0.02]

    assert metrics.point_to_plane(lifted, positions, normals) == pytest.approx(
        0.02
    )


def test_point_to_plane_falls_back_where_the_normal_is_undefined():
    reference = np.zeros((1, 3))
    queries = np.array([[0.0, 0.0, 0.5]])
    normals = np.zeros((1, 3))

    assert metrics.point_to_plane(queries, reference, normals) == pytest.approx(
        0.5
    )


def test_point_to_plane_rejects_mismatched_normals():
    with pytest.raises(ValueError, match="one row per reference vertex"):
        metrics.point_to_plane(
            np.zeros((2, 3)), np.zeros((3, 3)), np.zeros((2, 3))
        )


# The metric against closed-form answers
def test_quantization_error_matches_its_closed_form():
    """Uniform rounding error has RMS step * sqrt(3/12) and bound step * sqrt(3)/2.

    Points are spaced so each decoded point's nearest reference is its source.
    """
    step = 0.05
    reference = np.random.default_rng(4).random((4000, 3)) * 10.0
    decoded = np.round(reference / step) * step

    distances = metrics.point_to_point(decoded, reference)
    expected_rms = step * np.sqrt(3.0 / 12.0)

    assert np.sqrt(np.mean(distances ** 2)) == pytest.approx(expected_rms, rel=0.05)
    assert distances.max() <= step * np.sqrt(3.0) / 2.0 + 1e-12


def test_psnr_follows_the_definition():
    distances = np.full(10, 0.5)
    summary = metrics.DirectionalError.summarize(distances, peak=10.0)

    assert summary.rms == pytest.approx(0.5)
    assert summary.mean == pytest.approx(0.5)
    assert summary.maximum == pytest.approx(0.5)
    assert summary.psnr_db == pytest.approx(10.0 * np.log10(100.0 / 0.25))


def test_identical_meshes_have_no_error_and_infinite_psnr():
    positions, triangles = grid_mesh()
    mesh = TriangleMesh(positions, triangles)
    result = metrics.compare_meshes(mesh, mesh)

    assert result.decoded_distances == pytest.approx(0.0)
    assert result.reference_distances == pytest.approx(0.0)
    assert result.symmetric_rms == pytest.approx(0.0)
    assert result.forward.psnr_db == np.inf
    assert result.symmetric_psnr_db == np.inf


def test_psnr_is_undefined_rather_than_perfect_without_a_scale():
    summary = metrics.DirectionalError.summarize(np.full(4, 0.25), peak=0.0)
    assert np.isnan(summary.psnr_db)


def test_symmetric_figures_take_the_worse_direction():
    positions, triangles = grid_mesh(side=12)
    keep = positions[:, 0] < 0.5
    partial = positions[keep]

    result = metrics.compare_meshes(
        TriangleMesh(positions, triangles),
        TriangleMesh(partial, np.empty((0, 3), dtype=np.uint32)),
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
    assert metrics.bounding_box_diagonal(positions) == pytest.approx(5.0)
    assert metrics.bounding_box_diagonal(np.empty((0, 3))) == 0.0


def test_compare_meshes_rejects_an_unknown_metric():
    positions, triangles = grid_mesh()
    with pytest.raises(ValueError, match="metric must be"):
        metrics.compare_meshes(
            TriangleMesh(positions, triangles), TriangleMesh(positions, triangles), metric="hausdorff"
        )
