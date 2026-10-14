"""OBJ independent UV indices survive loading, storage and transport."""

from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

import open4d
from open4d.io import DecodeError
from open4d.io._mesh import read_obj, read_obj_with_uv
from open4d.transport import receive, send


POSITIONS = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
UVS = "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"


def obj(tmp_path, content):
    path = tmp_path / "mesh.obj"
    path.write_text(content)
    return path


def test_aligned_uv_load_keeps_legacy_tuple_and_does_not_follow_materials(tmp_path):
    path = obj(tmp_path, "mtllib ../../private.mtl\n" + POSITIONS + UVS + "f 1/1 2/2 3/3 4/4\n")
    assert len(read_obj(path)) == 3
    with open4d.load(path) as sequence:
        geometry = sequence[0].geometry
    np.testing.assert_array_equal(geometry.triangles, [[0, 1, 2], [0, 2, 3]])
    np.testing.assert_array_equal(geometry.texture_coordinates, [[0, 0], [1, 0], [1, 1], [0, 1]])
    assert geometry.texture_coordinates.dtype == np.float32


def test_legacy_geometry_only_reader_ignores_undeclared_uv_references(tmp_path):
    path = obj(tmp_path, POSITIONS + "f 1/1 2/2 3/3\n")
    _, triangles, colors = read_obj(path)
    np.testing.assert_array_equal(triangles, [[0, 1, 2]])
    assert colors is None
    with pytest.raises(ValueError, match="references texture coordinate"):
        read_obj_with_uv(path)


def test_seams_and_negative_texture_indices_keep_triangle_corner_uvs(tmp_path):
    path = obj(tmp_path, POSITIONS + UVS + "vt .5 .5\n" +
               "f -4/-5 -3/-4 -2/-3 -1/-2\nf 1/5 3/3 4/4\n")
    with open4d.load(path) as sequence:
        geometry = sequence[0].geometry
    np.testing.assert_array_equal(geometry.triangles, [[0, 1, 2], [0, 2, 3], [0, 2, 3]])
    assert geometry.positions.shape == (4, 3)  # UV seams do not split geometry topology.
    np.testing.assert_array_equal(geometry.texture_coordinates,
        [[[0, 0], [1, 0], [1, 1]], [[0, 0], [1, 1], [0, 1]], [[.5, .5], [1, 1], [0, 1]]])


def test_negative_uv_indices_are_relative_to_each_face_not_final_uv_count(tmp_path):
    path = obj(tmp_path, POSITIONS + "vt .2 .3\nvt .4 .5\nvt .6 .7\n" +
               "f 1/-3 2/-2 3/-1\nvt .9 .8\n")
    _, triangles, _, uv = read_obj_with_uv(path)
    np.testing.assert_allclose(uv[triangles] if uv.ndim == 2 else uv,
                               [[[.2, .3], [.4, .5], [.6, .7]]])


@pytest.mark.parametrize("faces,error", [
    ("f 1/1 2/2 3\n", "mixes assigned and missing"),
    ("f 1/1 2/2 3/3\nf 1 3 4\n", "mixes assigned and missing"),
    ("f 1/0 2/2 3/3\n", "invalid OBJ texture index"),
    ("f 1/-5 2/2 3/3\n", "invalid OBJ texture index"),
    ("f 1/5 2/2 3/3\n", "references texture coordinate 5"),
    ("f 1/nope 2/2 3/3\n", "invalid OBJ texture index"),
    ("f 1/1/1/1 2/2 3/3\n", "invalid OBJ corner reference"),
])
def test_invalid_or_partial_uv_references_fail_actionably(tmp_path, faces, error):
    path = obj(tmp_path, POSITIONS + UVS + faces)
    with open4d.load(path) as sequence, pytest.raises(DecodeError, match=error):
        sequence[0]


@pytest.mark.parametrize("vt", ["vt\n", "vt nan 0\n", "vt 0 inf\n", "vt nope 0\n",
                              "vt 1e300 0\n", "vt 0 0 1\n"])
def test_invalid_or_unsupported_texture_records_are_not_silently_discarded(tmp_path, vt):
    path = obj(tmp_path, POSITIONS + vt + "f 1/1 2/1 3/1\n")
    with open4d.load(path) as sequence, pytest.raises(DecodeError, match="texture"):
        sequence[0]


def test_unused_texture_records_and_untextured_normal_references_remain_untextured(tmp_path):
    path = obj(tmp_path, POSITIONS + UVS + "vn 0 0 1\nf 1//1 2//1 3//1\n")
    with open4d.load(path) as sequence:
        assert sequence[0].geometry.texture_coordinates is None


@pytest.mark.parametrize("seam", [False, True])
def test_loaded_uv_survives_npz_and_tcp(tmp_path, seam):
    path = obj(tmp_path, POSITIONS + UVS + ("vt .5 .5\nf 1/5 2/2 3/3\nf 1/1 3/3 4/4\n"
                                          if seam else "f 1/1 2/2 3/3 4/4\n"))
    with open4d.load(path) as original:
        expected = original[0].geometry.texture_coordinates.copy()
        output = open4d.encode(original, tmp_path / "texture.o4d", codec="npz")
    with open4d.load(output) as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.texture_coordinates, expected)
        with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
            future = pool.submit(send, decoded, *receiver.address, realtime=False)
            received = list(receiver)
            assert future.result(timeout=3) == 1
    np.testing.assert_array_equal(received[0].geometry.texture_coordinates, expected)


@pytest.mark.parametrize("seam", [False, True])
def test_loaded_uv_survives_usdc(tmp_path, seam):
    pytest.importorskip("pxr", reason="UV OpenUSD preservation requires optional usd-core")
    path = obj(tmp_path, POSITIONS + UVS + ("vt .5 .5\nf 1/5 2/2 3/3\nf 1/1 3/3 4/4\n"
                                          if seam else "f 1/1 2/2 3/3 4/4\n"))
    with open4d.load(path) as original:
        expected = original[0].geometry.texture_coordinates.copy()
        open4d.save(original, tmp_path / "texture.usdc")
    with open4d.load(tmp_path / "texture.usdc") as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.texture_coordinates, expected)
