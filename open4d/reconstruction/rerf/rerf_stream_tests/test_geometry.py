"""ReRF's density field, read out as points.

Upstream does this in `tools/vis_volume.py`; `rerf_stream.geometry` does the
same two operations on a frame decoded from the bitstream instead of from a
training checkpoint. These tests cover the parts that do not need a GPU -- the
PLY container and the world transform -- plus a marked CUDA test that takes a
real frame through the whole path.

The PLY dialect matters more than it looks: the client's parser reads binary
little-endian with float x/y/z and uchar colour, and a header it cannot parse
is a pane that stays empty.
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from rerf_stream.bitstream import BitstreamPlayer
from rerf_stream.geometry import (
    PointCloud, _world_transform, point_cloud, read_ply, write_ply,
)

# Same fixture and skip rule as `test_bitstream.py`: the decode needs a real
# encoded bitstream and a GPU, and neither is present everywhere.
RUN = Path("/media/frozzzen/LocalDisk/nevo_runs/g_basketball")
CONFIG, BITSTREAM = RUN / "config.py", RUN / "rerf"

requires_bitstream = pytest.mark.skipif(
    not (CONFIG.is_file() and (BITSTREAM / "header_0.json").is_file()),
    reason="needs an encoded ReRF bitstream on this machine",
)


@pytest.fixture(scope="module")
def first_frame():
    """One decoded frame, shared: the decode costs seconds and a CUDA context.

    The frame holds the player's own model rather than a copy, so every test
    using this sees the same installed grid -- which is fine here because none
    of them advance the stream.
    """
    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    return player, next(iter(player.play(loop=False)))


def _cloud(count: int = 5) -> PointCloud:
    # Values that survive neither a truncation nor a byte-order slip unnoticed.
    xyz = np.array(
        [[0.5, -1.25, 2.0], [3.0, 4.5, -6.25], [1e-3, 1e3, 0.0],
         [-0.75, 0.125, 7.5], [2.25, -3.5, 1.75]], dtype=np.float32)[:count]
    rgb = np.array([[0, 128, 255], [255, 0, 1], [7, 7, 7], [1, 2, 3],
                    [254, 253, 252]], dtype=np.uint8)[:count]
    return PointCloud(xyz=xyz, rgb=rgb)


def test_a_ply_round_trips_exactly(tmp_path):
    cloud = _cloud()
    write_ply(tmp_path / "f.ply", cloud)
    back = read_ply(tmp_path / "f.ply")
    # float32 in, float32 out: no tolerance is needed and none is given, so a
    # precision loss anywhere in the writer shows up here.
    assert np.array_equal(back.xyz, cloud.xyz)
    assert np.array_equal(back.rgb, cloud.rgb)


def test_the_ply_is_the_dialect_the_client_parses(tmp_path):
    size = write_ply(tmp_path / "f.ply", _cloud())
    data = (tmp_path / "f.ply").read_bytes()
    header, body = data.split(b"end_header\n", 1)
    text = header.decode("ascii")

    assert text.startswith("ply\n")
    # The client refuses anything else, by name.
    assert "format binary_little_endian 1.0" in text
    assert "element vertex 5" in text
    for line in ("property float x", "property float y", "property float z",
                 "property uchar red", "property uchar green",
                 "property uchar blue"):
        assert line in text
    # 3 floats + 3 bytes a point, and nothing else: a stride that disagrees
    # with the header decodes as noise rather than failing.
    assert len(body) == 5 * (3 * 4 + 3)
    assert size == len(data)


def test_the_first_point_is_where_the_header_says(tmp_path):
    write_ply(tmp_path / "f.ply", _cloud())
    data = (tmp_path / "f.ply").read_bytes()
    at = data.index(b"end_header\n") + len(b"end_header\n")
    x, y, z, r, g, b = struct.unpack_from("<fffBBB", data, at)
    assert (x, y, z) == (0.5, -1.25, 2.0)
    assert (r, g, b) == (0, 128, 255)


def test_a_cloud_reports_its_size_and_extent():
    cloud = _cloud()
    assert cloud.count == 5
    low, high = cloud.bounds
    assert low == pytest.approx([-0.75, -3.5, -6.25])
    assert high == pytest.approx([3.0, 1e3, 7.5])


def test_the_world_transform_is_the_one_the_corpus_records(tmp_path):
    (tmp_path / "nevo_corpus.json").write_text(json.dumps({
        "world_centre": [2.0, -1.0, 15.0], "world_scale": 4.0,
        "width": 1280, "height": 960,
    }))
    centre, scale = _world_transform(tmp_path)
    assert centre.tolist() == [2.0, -1.0, 15.0]
    assert scale == 4.0
    # `world = normalised / scale + centre`, which is exactly what
    # `cameras.orbit_rig` applies to the poses. The two have to agree or the
    # points and the rig describe different places.
    normalised = np.array([1.0, 0.0, -2.0])
    assert (normalised / scale + centre).tolist() == [2.25, -1.0, 14.5]


def test_a_missing_corpus_manifest_says_so(tmp_path):
    with pytest.raises(FileNotFoundError):
        _world_transform(tmp_path)


# ------------------------------------------------------- a real frame ---


@requires_bitstream
def test_a_decoded_frame_yields_points_where_the_render_puts_them(first_frame):
    """The whole path, on a real frame.

    The check that matters is *placement*: a point cloud in the wrong frame is
    a correct extraction somewhere else, which reads as a broken
    reconstruction. Held against the ray-marched render's own bounding box.
    """
    player, frame = first_frame
    cloud = point_cloud(frame.model, player.corpus_dir, threshold=0.35)

    assert cloud.count > 10_000, "a thresholded human should not be a handful of points"
    assert cloud.xyz.dtype == np.float32 and cloud.rgb.dtype == np.uint8
    assert np.isfinite(cloud.xyz).all()

    low, high = cloud.bounds
    extent = [high[axis] - low[axis] for axis in range(3)]
    # A standing figure: taller than it is wide, and roughly life-sized. This
    # is what catches a missing or doubled world scale, which is otherwise
    # invisible -- the cloud looks right and is the wrong size.
    assert 1.0 < extent[1] < 3.0, f"height {extent[1]:.2f} m is not a person"
    assert extent[1] > extent[0] and extent[1] > extent[2]

    # Colour actually varies. A constant grey means the rgb net was queried
    # wrongly and every point got the network's bias.
    assert cloud.rgb.std(axis=0).min() > 5


@requires_bitstream
def test_a_higher_threshold_keeps_fewer_points(first_frame):
    player, frame = first_frame
    counts = [
        point_cloud(frame.model, player.corpus_dir, threshold=t).count
        for t in (0.2, 0.35, 0.5)
    ]
    assert counts[0] > counts[1] > counts[2]


@requires_bitstream
def test_a_threshold_above_the_field_is_refused(first_frame):
    """Rather than writing an empty clip.

    An empty PLY parses and renders as nothing, so the pane would be blank with
    the page reporting success -- the failure mode this codebase keeps hitting.
    """
    player, frame = first_frame
    with pytest.raises(ValueError, match="no voxel has density above"):
        point_cloud(frame.model, player.corpus_dir, threshold=1.5)


@requires_bitstream
def test_colour_depends_on_the_azimuth_it_was_baked_at(first_frame):
    """Which is why the clip's notes say what it was baked at.

    ReRF's colour comes from a view-dependent network. If these two agreed, the
    network would be being queried with a direction it ignores, and the note
    would be describing a choice that was not being made.
    """
    player, frame = first_frame
    front = point_cloud(frame.model, player.corpus_dir, azimuth=0.0)
    behind = point_cloud(frame.model, player.corpus_dir, azimuth=180.0)
    assert np.array_equal(front.xyz, behind.xyz)      # same geometry
    assert not np.array_equal(front.rgb, behind.rgb)  # different colour
