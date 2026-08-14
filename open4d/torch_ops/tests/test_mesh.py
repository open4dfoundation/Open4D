"""Tests for the PyTorch3D replacements.

These functions replaced a compiled library, so agreeing with themselves proves
nothing. Each one is checked against something independent: closed-form geometry
where it exists, an exhaustive search written out longhand where it does not,
and the documented PyTorch3D definition where the point is to match a convention
rather than to be correct in the abstract.
"""

from __future__ import annotations

import math

import pytest

pytestmark = pytest.mark.torch

torch = pytest.importorskip("torch")

from open4d.torch_ops import (  # noqa: E402
    Meshes,
    chamfer_distance,
    face_normals,
    point_face_distance,
    point_face_distance_bruteforce,
    sample_points_from_mesh,
    vertex_normals,
)


def unit_square():
    """Two triangles spanning the unit square in the z=0 plane, facing +z."""
    verts = torch.tensor(
        [[0.0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=torch.float64
    )
    faces = torch.tensor([[0, 1, 2], [0, 2, 3]], dtype=torch.int64)
    return verts, faces


def octahedron():
    verts = torch.tensor(
        [[1.0, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]],
        dtype=torch.float64,
    )
    faces = torch.tensor(
        [[0, 2, 4], [2, 1, 4], [1, 3, 4], [3, 0, 4],
         [2, 0, 5], [1, 2, 5], [3, 1, 5], [0, 3, 5]],
        dtype=torch.int64,
    )
    return verts, faces


# ----------------------------
# Normals, against known geometry
# ----------------------------
def test_face_normals_of_a_flat_square_all_point_up():
    verts, faces = unit_square()
    normals = face_normals(verts, faces)
    assert torch.allclose(normals, torch.tensor([[0.0, 0, 1]] * 2, dtype=torch.float64))


def test_vertex_normals_of_an_octahedron_point_away_from_the_centre():
    """Every vertex of a centred octahedron must have a normal along its own axis."""
    verts, faces = octahedron()
    normals = vertex_normals(verts, faces)
    assert torch.allclose(normals, verts, atol=1e-9)


def test_normals_are_unit_length():
    verts, faces = octahedron()
    for normals in (vertex_normals(verts, faces), face_normals(verts, faces)):
        assert torch.allclose(
            normals.norm(dim=1), torch.ones(len(normals), dtype=torch.float64)
        )


def test_vertex_normals_are_area_weighted():
    """PyTorch3D weights each face's contribution by area; a lopsided pair must
    lean toward the larger face rather than averaging the two evenly."""
    verts = torch.tensor(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 10]], dtype=torch.float64
    )
    # Shared vertex 0: a small triangle in z=0 and a large one in y=0.
    faces = torch.tensor([[0, 1, 2], [0, 3, 1]], dtype=torch.int64)
    normal = vertex_normals(verts, faces)[0]
    small, large = face_normals(verts, faces)
    assert (normal - large).norm() < (normal - small).norm()


# ----------------------------
# Chamfer, against brute force and closed form
# ----------------------------
def brute_force_chamfer(a, b):
    """The definition, written out with Python loops."""
    total = 0.0
    for p in a:
        total += min(float(((p - q) ** 2).sum()) for q in b)
    return total


def test_chamfer_matches_brute_force():
    torch.manual_seed(0)
    a = torch.rand(1, 25, 3, dtype=torch.float64)
    b = torch.rand(1, 17, 3, dtype=torch.float64)

    expected = brute_force_chamfer(a[0], b[0]) + brute_force_chamfer(b[0], a[0])
    got, _ = chamfer_distance(a, b, point_reduction="sum", batch_reduction="mean")
    assert math.isclose(float(got), expected, rel_tol=1e-9)


def test_chamfer_of_identical_sets_is_zero():
    a = torch.rand(1, 20, 3, dtype=torch.float64)
    got, _ = chamfer_distance(a, a.clone())
    assert float(got) == pytest.approx(0.0, abs=1e-12)


def test_chamfer_uses_squared_distance():
    """Two single points 3 apart: bidirectional squared distance is 9 + 9."""
    a = torch.tensor([[[0.0, 0, 0]]], dtype=torch.float64)
    b = torch.tensor([[[3.0, 0, 0]]], dtype=torch.float64)
    got, _ = chamfer_distance(a, b, point_reduction="sum", batch_reduction="sum")
    assert float(got) == pytest.approx(18.0)


def test_chamfer_single_directional_is_half_of_a_symmetric_pair():
    a = torch.tensor([[[0.0, 0, 0]]], dtype=torch.float64)
    b = torch.tensor([[[3.0, 0, 0]]], dtype=torch.float64)
    got, _ = chamfer_distance(
        a, b, point_reduction="sum", batch_reduction="sum", single_directional=True
    )
    assert float(got) == pytest.approx(9.0)


def test_chamfer_is_differentiable():
    """n4mc backpropagates through this, so a gradient must actually arrive."""
    a = torch.rand(1, 8, 3, dtype=torch.float64, requires_grad=True)
    b = torch.rand(1, 6, 3, dtype=torch.float64)
    loss, _ = chamfer_distance(a, b)
    loss.backward()
    assert a.grad is not None and a.grad.abs().sum() > 0


def test_chamfer_rejects_unbatched_input():
    with pytest.raises(ValueError, match=r"\(N, P, 3\)"):
        chamfer_distance(torch.rand(5, 3), torch.rand(5, 3))


# ----------------------------
# Point-to-face distance, against closed form and brute force
# ----------------------------
def test_distance_to_a_plane_is_the_perpendicular_drop():
    """A point above the unit square: the answer is its height, squared."""
    verts, faces = unit_square()
    points = torch.tensor([[0.5, 0.5, 2.0]], dtype=torch.float64)
    squared, index = point_face_distance_bruteforce(points, verts, faces)
    assert float(squared[0]) == pytest.approx(4.0)
    assert int(index[0]) in (0, 1)


def test_distance_off_the_edge_lands_on_the_boundary():
    """Beyond the square, the nearest point is a corner, not the plane."""
    verts, faces = unit_square()
    points = torch.tensor([[2.0, 2.0, 0.0]], dtype=torch.float64)
    squared, _ = point_face_distance_bruteforce(points, verts, faces)
    # Nearest surface point is the corner (1, 1, 0).
    assert float(squared[0]) == pytest.approx(2.0)


def test_points_on_the_surface_are_at_zero_distance():
    verts, faces = octahedron()
    points, _ = sample_points_from_mesh(verts, faces, 200)
    squared, _ = point_face_distance_bruteforce(points, verts, faces)
    assert float(squared.max()) == pytest.approx(0.0, abs=1e-12)


def optimised_closest(points, verts, faces):
    """The true distance to the surface, found by constrained optimisation.

    Squared distance to a triangle is a convex quadratic over a convex domain
    (barycentric u, v >= 0 with u + v <= 1), so the minimiser is the global
    optimum and one start suffices. This shares no logic with the region
    classification it is checking -- it never asks which region a point is in.
    """
    numpy = pytest.importorskip("numpy")
    optimize = pytest.importorskip("scipy.optimize")

    tri = verts[faces].numpy()
    query = points.numpy()
    best = numpy.full(len(query), numpy.inf)
    for i, p in enumerate(query):
        for a, b, c in tri:
            def squared(uv, a=a, b=b, c=c, p=p):
                q = a + uv[0] * (b - a) + uv[1] * (c - a)
                return float(((p - q) ** 2).sum())

            found = optimize.minimize(
                squared, (1 / 3, 1 / 3), method="SLSQP",
                bounds=[(0.0, 1.0), (0.0, 1.0)],
                constraints=[{"type": "ineq", "fun": lambda uv: 1.0 - uv[0] - uv[1]}],
                options={"ftol": 1e-14, "maxiter": 500},
            )
            best[i] = min(best[i], found.fun)
    return torch.from_numpy(best)


def test_bruteforce_matches_a_constrained_optimiser():
    """The region logic has six branches; random queries should exercise them."""
    torch.manual_seed(2)
    verts, faces = octahedron()
    points = (torch.rand(40, 3, dtype=torch.float64) - 0.5) * 4
    squared, _ = point_face_distance_bruteforce(points, verts, faces)
    assert torch.allclose(squared, optimised_closest(points, verts, faces), atol=1e-9)


def test_returned_index_is_the_face_that_was_closest():
    torch.manual_seed(3)
    verts, faces = octahedron()
    points = (torch.rand(30, 3, dtype=torch.float64) - 0.5) * 3
    squared, index = point_face_distance_bruteforce(points, verts, faces)
    for i in range(len(points)):
        one_face = faces[index[i] : index[i] + 1]
        alone, _ = point_face_distance_bruteforce(points[i : i + 1], verts, one_face)
        assert float(alone[0]) == pytest.approx(float(squared[i]), abs=1e-12)


def test_accelerated_path_matches_bruteforce():
    """The Open3D BVH must agree with the exact scan it replaces."""
    pytest.importorskip("open3d")
    torch.manual_seed(4)
    verts, faces = octahedron()
    points = ((torch.rand(200, 3, dtype=torch.float64) - 0.5) * 4)

    fast, _ = point_face_distance(points, verts, faces)
    slow, _ = point_face_distance_bruteforce(points, verts, faces)
    # Open3D computes in float32, so agreement is to single precision.
    assert torch.allclose(fast, slow, atol=1e-5, rtol=1e-4)


# ----------------------------
# Surface sampling
# ----------------------------
def test_sampled_points_lie_on_the_surface():
    verts, faces = octahedron()
    points, _ = sample_points_from_mesh(verts, faces, 500)
    # Every face of this octahedron satisfies |x|+|y|+|z| == 1.
    assert torch.allclose(
        points.abs().sum(dim=1), torch.ones(500, dtype=torch.float64), atol=1e-9
    )


def test_sampling_is_area_weighted_not_face_uniform():
    """One triangle 100x the area of the other should take ~99% of the samples."""
    verts = torch.tensor(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0], [100, 0, 0], [0, 100, 0]],
        dtype=torch.float64,
    )
    faces = torch.tensor([[0, 1, 2], [0, 3, 4]], dtype=torch.int64)
    torch.manual_seed(5)
    points, _ = sample_points_from_mesh(verts, faces, 4000)
    on_large = (points.sum(dim=1) > 1.0).double().mean()
    assert 0.97 < float(on_large) < 1.0


def test_sampled_normals_are_returned_and_normalized():
    verts, faces = octahedron()
    normals = vertex_normals(verts, faces)
    points, sampled = sample_points_from_mesh(verts, faces, 100, normals=normals)
    assert sampled is not None
    assert torch.allclose(
        sampled.norm(dim=1), torch.ones(100, dtype=torch.float64), atol=1e-9
    )


# ----------------------------
# The Meshes stand-in
# ----------------------------
def test_meshes_round_trips_what_it_was_given():
    verts, faces = octahedron()
    mesh = Meshes(verts=[verts], faces=[faces])
    assert len(mesh) == 1
    assert torch.equal(mesh.verts_list()[0], verts)
    assert torch.equal(mesh.faces_list()[0], faces)


def test_meshes_packed_normals_match_the_free_functions():
    verts, faces = octahedron()
    mesh = Meshes(verts=[verts], faces=[faces])
    assert torch.allclose(mesh.verts_normals_packed(), vertex_normals(verts, faces))
    assert torch.allclose(mesh.faces_normals_packed(), face_normals(verts, faces))


def test_meshes_rejects_mismatched_lists():
    verts, faces = octahedron()
    with pytest.raises(ValueError, match="same length"):
        Meshes(verts=[verts, verts], faces=[faces])
