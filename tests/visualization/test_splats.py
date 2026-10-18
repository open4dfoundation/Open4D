from __future__ import annotations

import numpy as np
import pytest

from open4d import GaussianSplats, NeuralGaussianFrame
from open4d.core import Frame, GaussianCloud, MemoryFrameProvider, Sequence
from open4d.visualization import Visualizer, visualize
from open4d.visualization._frames import (
    UP_TO_Z,
    GaussianFrame,
    axis_matrix,
    gaussian_covariances,
    to_render_frame,
)
from open4d.visualization._splats import (
    NeuralColors,
    SplatBuffer,
    SplatsUnsupported,
    back_to_front,
    camera_eye,
    capabilities,
    evaluate_sh,
    view_directions,
)

pytestmark = pytest.mark.cpu


def full(upper):
    """(N, 6) upper triangles back to (N, 3, 3) matrices."""
    rows, columns = np.triu_indices(3)
    matrices = np.zeros((len(upper), 3, 3))
    matrices[:, rows, columns] = upper
    matrices[:, columns, rows] = upper
    return matrices


def random_gaussians(count, seed=0):
    rng = np.random.default_rng(seed)
    return (rng.normal(size=(count, 3)), rng.uniform(0.05, 1.0, (count, 3)),
            rng.normal(size=(count, 4)), rng.uniform(0.1, 1.0, count))


def test_covariance_is_rotation_times_squared_scales():
    sqrt_half = np.sqrt(0.5)
    covariances = full(gaussian_covariances(
        [[1., 2, 3], [2., 1, 1]], [[1., 0, 0, 0], [sqrt_half, 0, 0, sqrt_half]]))
    np.testing.assert_allclose(covariances[0], np.diag([1, 4, 9]), atol=1e-6)
    # 90 degrees about z turns the long x axis onto y.
    np.testing.assert_allclose(covariances[1], np.diag([1, 4, 1]), atol=1e-6)


def test_covariance_matches_an_independent_rotation():
    transform = pytest.importorskip("scipy.spatial.transform")
    _, scales, quaternions, _ = random_gaussians(64)
    quaternions /= np.linalg.norm(quaternions, axis=1, keepdims=True)
    # SciPy takes scalar-last quaternions; Open4D stores (w, x, y, z).
    rotation = transform.Rotation.from_quat(quaternions[:, [1, 2, 3, 0]]).as_matrix()
    expected = rotation @ (scales[:, :, None] ** 2 * np.eye(3)) @ rotation.transpose(0, 2, 1)
    np.testing.assert_allclose(full(gaussian_covariances(scales, quaternions)), expected,
                               rtol=1e-5, atol=1e-6)


@pytest.mark.parametrize("up", ["x", "y", "z"])
def test_axis_transform_rotates_covariance_with_positions(up):
    positions, scales, quaternions, opacities = random_gaussians(32, seed=1)
    cloud = GaussianCloud(positions, scales, quaternions, opacities)
    order = UP_TO_Z[up]
    axes = axis_matrix(order)
    frame = to_render_frame(Frame(0, 0.0, cloud), order)
    original = full(gaussian_covariances(cloud.scales, cloud.rotations))
    np.testing.assert_allclose(frame.positions, cloud.positions @ axes.T)
    np.testing.assert_allclose(full(frame.covariances), axes @ original @ axes.T,
                               rtol=1e-5, atol=1e-6)
    # The density at a rotated sample equals the density at the original one.
    sample = cloud.positions + 0.3
    before = np.einsum("ni,nij,nj->n", sample - cloud.positions, np.linalg.inv(original),
                       sample - cloud.positions)
    offset = sample @ axes.T - frame.positions
    after = np.einsum("ni,nij,nj->n", offset, np.linalg.inv(full(frame.covariances)), offset)
    np.testing.assert_allclose(after, before, rtol=1e-3)


def test_frame_conversion_preserves_scale_rotation_and_appearance():
    positions, scales, quaternions, opacities = random_gaussians(8, seed=2)
    sh = np.random.default_rng(3).normal(size=(8, 9, 3)).astype(np.float32)
    splats = GaussianSplats(positions=positions, scales=scales, rotations=quaternions,
                            opacities=opacities, spherical_harmonics=sh)
    frame = to_render_frame(GaussianFrame(4, 0.5, splats), [0, 1, 2])
    assert (frame.frame_index, frame.timestamp, frame.representation) == (4, 0.5, "gaussians")
    assert not frame.is_mesh
    # Eigen-decomposition recovers the activated scales and rotation axes.
    eigenvalues, eigenvectors = np.linalg.eigh(full(frame.covariances))
    np.testing.assert_allclose(eigenvalues, np.sort(splats.scales ** 2, axis=1), rtol=1e-4)
    transform = pytest.importorskip("scipy.spatial.transform")
    rotation = transform.Rotation.from_quat(splats.rotations[:, [1, 2, 3, 0]]).as_matrix()
    longest = rotation[np.arange(8), :, np.argmax(splats.scales, axis=1)]
    np.testing.assert_allclose(np.abs((eigenvectors[:, :, 2] * longest).sum(axis=1)), 1, rtol=1e-4)
    np.testing.assert_allclose(frame.opacities, splats.opacities)
    np.testing.assert_allclose(frame.colors, np.clip(0.5 + 0.28209479177387814 * sh[:, 0], 0, 1))
    assert frame.spherical_harmonics is sh or np.array_equal(frame.spherical_harmonics, sh)

    flat = GaussianSplats(positions=positions, scales=scales, rotations=quaternions,
                          opacities=opacities, spherical_harmonics=sh[:, :1])
    assert to_render_frame(GaussianFrame(0, 0, flat), [0, 1, 2]).spherical_harmonics is None

    appearance = object()
    neural = NeuralGaussianFrame(positions=positions, scales=scales, rotations=quaternions,
                                 opacities=opacities, appearance=appearance)
    converted = to_render_frame(GaussianFrame(0, 0, neural), UP_TO_Z["y"])
    assert converted.appearance is appearance and converted.colors is None
    np.testing.assert_array_equal(converted.source_axes, axis_matrix(UP_TO_Z["y"]))


def real_sh_reference(degree, directions):
    """Graphdeco's basis from SciPy's complex SH, which includes Condon-Shortley."""
    special = pytest.importorskip("scipy.special")
    x, y, z = directions.T
    polar, azimuth = np.arccos(np.clip(z, -1, 1)), np.arctan2(y, x)
    columns = []
    for band in range(degree + 1):
        for order in range(-band, band + 1):
            value = special.sph_harm_y(band, abs(order), polar, azimuth)
            if order > 0:
                columns.append(np.sqrt(2) * value.real)
            elif order < 0:
                columns.append(np.sqrt(2) * value.imag)
            else:
                columns.append(value.real)
    return np.stack(columns, axis=1)


@pytest.mark.parametrize("degree", [0, 1, 2, 3])
def test_sh_evaluation_matches_scipy_real_harmonics(degree):
    rng = np.random.default_rng(degree)
    directions = rng.normal(size=(200, 3))
    directions /= np.linalg.norm(directions, axis=1, keepdims=True)
    coefficients = rng.normal(size=(200, (degree + 1) ** 2, 3))
    basis = real_sh_reference(degree, directions)
    expected = np.einsum("nk,nkc->nc", basis, coefficients)
    np.testing.assert_allclose(evaluate_sh(coefficients, directions), expected,
                               rtol=1e-4, atol=1e-4)


def test_sh_evaluation_rejects_partial_bands():
    with pytest.raises(ValueError, match="bands"):
        evaluate_sh(np.zeros((1, 5, 3)), np.zeros((1, 3)))


def look_at(eye, target=(0, 0, 0), up=(0, 0, 1)):
    """Row-major view matrix with the camera looking down its -Z."""
    eye, target, up = (np.asarray(value, dtype=np.float64) for value in (eye, target, up))
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, up)
    right /= np.linalg.norm(right)
    true_up = np.cross(right, forward)
    view = np.eye(4)
    view[0, :3], view[1, :3], view[2, :3] = right, true_up, -forward
    view[:3, 3] = -view[:3, :3] @ eye
    return view.astype(np.float32)


def test_sort_is_back_to_front_and_follows_the_camera():
    positions = np.array([[0, 0, 1], [0, 0, -2], [0, 0, 0.5], [0, 0, 3]], dtype=np.float32)
    above = look_at((0.1, 0, 10))
    np.testing.assert_allclose(camera_eye(above), (0.1, 0, 10), atol=1e-5)
    assert back_to_front(positions, above).tolist() == [1, 2, 0, 3]
    below = look_at((0.1, 0, -10))
    assert back_to_front(positions, below).tolist() == [3, 0, 2, 1]


def test_buffer_resorts_and_recolors_only_when_the_camera_changes():
    positions = np.array([[0., 0, 1], [0., 0, -1]])
    sh = np.zeros((2, 4, 3), np.float32)
    sh[:, 2, 0] = 1.0                    # red follows the view direction's z
    splats = GaussianSplats(positions=positions, scales=np.full((2, 3), 0.1),
                            rotations=[[1., 0, 0, 0]] * 2, opacities=[0.5, 0.75],
                            spherical_harmonics=sh)
    frame = to_render_frame(GaussianFrame(0, 0, splats), [0, 1, 2])
    buffer = SplatBuffer()
    buffer.set_frame(frame, frame.colors)
    above = look_at((0.01, 0, 10))
    rows = buffer.instances(above)
    np.testing.assert_allclose(rows[:, :3], positions[::-1])        # far one first
    np.testing.assert_allclose(rows[:, 12], [0.75, 0.5])
    np.testing.assert_allclose(rows[:, 3:9], frame.covariances[::-1])
    directions = view_directions(rows[:, :3], camera_eye(above), None)
    expected = np.clip(evaluate_sh(sh, directions) + 0.5, 0, 1)
    np.testing.assert_allclose(rows[:, 9:12], expected, atol=1e-6)
    assert np.all(rows[:, 9] < 0.5)       # looking down -z darkens red
    assert buffer.instances(above) is None
    below = buffer.instances(look_at((0.01, 0, -10)))
    np.testing.assert_allclose(below[:, :3], positions)
    assert np.all(below[:, 9] > 0.5)


def test_buffer_handles_empty_and_changing_counts():
    buffer = SplatBuffer()
    for count in (3, 0, 7):
        positions, scales, quaternions, opacities = random_gaussians(count)
        frame = to_render_frame(Frame(0, 0, GaussianCloud(positions, scales, quaternions, opacities)),
                                [0, 1, 2])
        buffer.set_frame(frame, np.ones((count, 3), np.float32))
        rows = buffer.instances(look_at((0.01, 0, 10)))
        assert rows.shape == (count, 13) and len(buffer) == count


def test_up_axis_rotates_view_directions_back_into_source_axes():
    order = UP_TO_Z["y"]
    axes = axis_matrix(order)
    source = np.array([[0.2, 0.3, 0.9]], dtype=np.float32)
    plot = source @ axes.T
    np.testing.assert_allclose(view_directions(plot, np.zeros(3), axes),
                               source / np.linalg.norm(source), rtol=1e-6)


def test_neural_colors_are_cached_per_frame_not_per_camera():
    calls = []

    class Appearance:
        def colors(self, directions):
            calls.append(directions.copy())
            return np.full((len(directions), 3), 0.25, np.float32)

    positions, scales, quaternions, opacities = random_gaussians(5)
    frames = [to_render_frame(GaussianFrame(index, 0, NeuralGaussianFrame(
        positions=positions, scales=scales, rotations=quaternions, opacities=opacities,
        appearance=Appearance())), [0, 1, 2]) for index in range(6)]
    cache = NeuralColors(size=4)
    np.testing.assert_allclose(cache(frames[0], np.array([0., 0, 10])), 0.25)
    cache(frames[0], np.array([5., 5, 5]))               # camera moved: cached
    assert len(calls) == 1
    np.testing.assert_allclose(np.linalg.norm(calls[0], axis=1), 1, rtol=1e-6)
    for frame in frames[1:]:
        cache(frame, np.zeros(3) + 10)
    cache(frames[0], np.zeros(3) + 10)                   # evicted by the bound
    assert len(calls) == 7


class Context:
    def __init__(self, version, es=False, extensions=()):
        self._version, self._es, self._extensions = version, es, set(extensions)

    def format(self):
        return self

    def version(self):
        return self._version

    def isOpenGLES(self):
        return self._es

    def hasExtension(self, name):
        return name in self._extensions


@pytest.mark.parametrize("context, reason", [
    (Context((2, 0), es=True), "no instancing"),
    (Context((2, 1)), "instanced_arrays"),
    (Context((2, 1), extensions={b"GL_ARB_instanced_arrays"}), "draw_instanced"),
])
def test_contexts_without_instancing_select_the_point_fallback(context, reason):
    pytest.importorskip("OpenGL")
    with pytest.raises(SplatsUnsupported, match=reason):
        capabilities(context)


@pytest.fixture
def backend(monkeypatch):
    from open4d.visualization import _live_qt

    class Window:
        def __init__(self, options):
            self.is_open, self.options, self.frames, self.displayed = True, options, {}, []

        def set_geometry(self, key, frame, texture, *, reset_camera):
            self.frames[key] = frame
            self.displayed.append(key)

        def poll_events(self):
            return self.is_open

        def render(self):
            pass

        def close(self):
            self.is_open = False

    monkeypatch.setattr(_live_qt, "LiveWindow", Window)
    return Window


def splat_list(count=3, frames=2):
    positions, scales, quaternions, opacities = random_gaussians(count)
    return [GaussianSplats(positions=positions + index, scales=scales, rotations=quaternions,
                           opacities=opacities, spherical_harmonics=np.zeros((count, 4, 3)))
            for index in range(frames)]


def test_visualizer_accepts_gaussian_splats_and_rejects_textures(backend):
    splats = splat_list()
    with Visualizer(up="y") as viewer:
        viewer.add_geometry(splats[0])
        frame = viewer._backend.frames[False, "geometry"]
        assert frame.covariances.shape == (3, 6)
        np.testing.assert_array_equal(frame.source_axes, axis_matrix(UP_TO_Z["y"]))
        with pytest.raises(ValueError, match="texture"):
            viewer.update_geometry(splats[1], texture=np.zeros((2, 2, 3), np.uint8))
        assert viewer.stream(iter(splats)) == 2


def test_gaussian_lists_play_as_timestamped_comparisons(backend):
    current, baseline = splat_list(frames=3), splat_list(frames=3)
    with Visualizer() as viewer:
        window = viewer._backend
        original = window.poll_events

        def close_after_two_pairs():
            if len(window.displayed) >= 4:
                window.is_open = False
            return original()

        window.poll_events = close_after_two_pairs
        viewer.play(current, baseline=baseline, fps=1000)
        assert window.frames[False, "geometry"].timestamp == window.frames[True, "geometry"].timestamp
        assert window.frames[True, "geometry"].representation == "gaussians"
    with Visualizer() as viewer:
        with pytest.raises(ValueError, match="equal frame counts"):
            viewer.play(current, baseline=baseline[:2])


def test_visualize_reaches_the_player_with_gaussian_frames(monkeypatch):
    from open4d.visualization import _qt

    seen = []
    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(_qt, "play", lambda frames, options: seen.append((frames, options)))
    visualize(splat_list(frames=4), stride=2, fps=20)
    frames, options = seen[0]
    assert len(frames) == 2 and options.fps == 20
    assert frames[1].frame_index == 2 and frames[1].timestamp == pytest.approx(0.1)
    assert frames[1].covariances is not None
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0, GaussianCloud(*random_gaussians(2)))]))
    visualize(sequence)
    assert seen[1][0][0].covariances.shape == (2, 6)
