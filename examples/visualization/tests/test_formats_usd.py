from __future__ import annotations

import numpy as np
import pytest

pytest.importorskip("pxr")

import formats_usd
from open4d import Frame, TriangleMesh

pytestmark = pytest.mark.cpu


def frame(index, triangles=(), colors=None, positions=None):
    if positions is None:
        positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0]]
    return Frame(
        frame_index=index,
        timestamp=index / 30,
        geometry=TriangleMesh(
            np.asarray(positions, dtype=np.float32),
            np.asarray(triangles, dtype=np.uint32).reshape(-1, 3),
            colors=colors,
        ),
    )


def test_usd_round_trip_preserves_topology_and_colors_first_seen_later(tmp_path):
    colors = np.array(
        [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        dtype=np.float32,
    )
    frames = (frame(0), frame(1, [[0, 1, 2]], colors))
    path = formats_usd.write_usd_container(
        tmp_path / "mixed.usdc", (item for item in frames)
    )

    decoded = formats_usd.open_usd_sequence(path)

    assert decoded.metadata["prim_type"] == "Mesh"
    assert len(decoded[0].geometry.triangles) == 0
    assert decoded[0].geometry.colors is None
    np.testing.assert_array_equal(decoded[1].geometry.triangles, [[0, 1, 2]])
    np.testing.assert_allclose(decoded[1].geometry.colors, colors)


def test_usd_reader_rejects_an_explicit_zero_fps_override(tmp_path):
    path = formats_usd.write_usd_container(tmp_path / "frame.usdc", [frame(0)])

    with pytest.raises(ValueError, match="fps"):
        formats_usd.open_usd_sequence(path, fps=0)


def test_usd_writer_rejects_x_up_without_mislabelling_geometry(tmp_path):
    destination = tmp_path / "frame.usdc"

    with pytest.raises(ValueError, match="up_axis.*y.*z"):
        formats_usd.write_usd_container(destination, [frame(0)], up_axis="x")

    assert not destination.exists()


def test_usd_writer_round_trips_empty_geometry(tmp_path):
    destination = tmp_path / "frame.usdc"
    empty = frame(0, positions=np.empty((0, 3), dtype=np.float32))

    formats_usd.write_usd_container(destination, [empty])
    decoded = formats_usd.open_usd_sequence(destination)

    assert decoded[0].geometry.positions.shape == (0, 3)
    decoded.close()
