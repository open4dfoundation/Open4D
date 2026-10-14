"""Tests for the OBJ reader and writer that replaced `pytorch3d.io`."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.torch

torch = pytest.importorskip("torch")

from open4d.torch_ops import load_obj, save_obj  # noqa: E402


def test_round_trip_preserves_geometry(tmp_path):
    verts = torch.tensor(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 1]], dtype=torch.float32
    )
    faces = torch.tensor([[0, 1, 2], [1, 3, 2]], dtype=torch.int64)

    path = tmp_path / "mesh.obj"
    save_obj(path, verts, faces)
    back, back_faces, _ = load_obj(path)

    assert torch.allclose(back, verts, atol=1e-6)
    assert torch.equal(back_faces.verts_idx, faces)


def test_indices_are_one_based_on_disk(tmp_path):
    """An OBJ that used 0 as an index would be read as -1 by every other tool."""
    path = tmp_path / "mesh.obj"
    save_obj(
        path,
        torch.tensor([[0.0, 0, 0], [1, 0, 0], [0, 1, 0]]),
        torch.tensor([[0, 1, 2]], dtype=torch.int64),
    )
    assert "f 1 2 3" in path.read_text()


@pytest.mark.parametrize(
    "spelling", ["1 2 3", "1/1 2/2 3/3", "1//1 2//2 3//3", "1/1/1 2/2/2 3/3/3"]
)
def test_all_four_face_index_spellings_are_accepted(tmp_path, spelling):
    path = tmp_path / "mesh.obj"
    path.write_text(f"v 0 0 0\nv 1 0 0\nv 0 1 0\nf {spelling}\n")
    _, faces, _ = load_obj(path)
    assert torch.equal(faces.verts_idx, torch.tensor([[0, 1, 2]]))


def test_polygons_are_triangulated_as_a_fan(tmp_path):
    path = tmp_path / "quad.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n")
    _, faces, _ = load_obj(path)
    assert torch.equal(
        faces.verts_idx, torch.tensor([[0, 1, 2], [0, 2, 3]])
    )


def test_out_of_range_index_is_reported(tmp_path):
    path = tmp_path / "broken.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n")
    with pytest.raises(ValueError, match="references vertex 9"):
        load_obj(path)


def test_asking_for_textures_is_refused_rather_than_ignored(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
    with pytest.raises(NotImplementedError, match="geometry only"):
        load_obj(path, load_textures=True)


def test_device_and_dtype_are_honoured(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
    verts, faces, _ = load_obj(path, load_textures=0, dtype=torch.float64)
    assert verts.dtype == torch.float64
    assert faces.verts_idx.dtype == torch.int64


def test_negative_indices_whitespace_and_inline_comments(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_text("  v\t0 0 0\nv 1 0 0\nv 0 1 0\n f\t-3 -2 -1 # triangle\n")
    _, faces, _ = load_obj(path)
    torch.testing.assert_close(faces.verts_idx, torch.tensor([[0, 1, 2]]))


@pytest.mark.parametrize("dtype", [torch.float32, torch.float64])
def test_obj_round_trip_preserves_small_values_and_empty_faces(tmp_path, dtype):
    verts = torch.tensor([[1.23456789012345, 1e-12, -1234567.875]], dtype=dtype)
    path = tmp_path / "precise.obj"
    save_obj(path, verts, torch.empty((0, 3), dtype=torch.int64))
    actual, faces, _ = load_obj(path, dtype=dtype)
    assert torch.equal(actual, verts)
    assert faces.verts_idx.shape == (0, 3)
