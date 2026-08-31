"""Tests for the multi-scene wall demo's composition and framing logic.

Pure logic only — building real Scenes needs a prepared bitstream and CUDA.
"""
from __future__ import annotations

import math

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from orbitvega.eval_camera import (MIN_CAMERA_DISTANCE, playback_fov_deg,
                                                 safe_orbit_radius)
from orbitvega.wall_demo import compose


def test_compose_lays_out_every_tile_without_overlap():
    tile, n, cols = 32, 9, 3
    # each tile a distinct grey so we can find where it landed
    tiles = [np.full((tile, tile, 3), (i + 1) * 20, dtype=np.uint8) for i in range(n)]
    jpeg = compose(tiles, [[] for _ in range(n)], cols, tile, wall_fps=5.0)
    assert jpeg[:2] == b"\xff\xd8" and jpeg[-2:] == b"\xff\xd9"

    from io import BytesIO
    from PIL import Image
    img = np.asarray(Image.open(BytesIO(jpeg)).convert("RGB"))
    rows = math.ceil(n / cols)
    assert img.shape[:2] == (rows * tile, cols * tile)
    # sample the centre of each cell; values must increase in reading order
    seen = []
    for i in range(n):
        cy = (i // cols) * tile + tile // 2
        cx = (i % cols) * tile + tile // 2
        seen.append(int(img[cy, cx, 0]))
    assert seen == sorted(seen), f"tiles out of order: {seen}"
    assert len(set(seen)) == n, f"tiles overlapped or duplicated: {seen}"


def test_compose_handles_a_non_square_scene_count():
    tile, n, cols = 48, 5, 3          # 5 scenes -> 3x2 grid, last cell empty
    tiles = [np.full((tile, tile, 3), 200, dtype=np.uint8) for _ in range(n)]
    jpeg = compose(tiles, [[] for _ in range(n)], cols, tile, wall_fps=1.0)
    from io import BytesIO
    from PIL import Image
    img = np.asarray(Image.open(BytesIO(jpeg)).convert("RGB"))
    assert img.shape[:2] == (2 * tile, 3 * tile)
    # The unused sixth cell stays background rather than repeating a tile.
    # Sampled in its upper portion: the wall-wide footer is drawn along the
    # bottom of the canvas and would otherwise be what we measured.
    assert int(img[tile + tile // 4, 2 * tile + tile // 2, 0]) < 60


def test_compose_resizes_a_tile_that_arrives_at_the_wrong_size():
    tile = 24
    tiles = [np.full((10, 10, 3), 180, dtype=np.uint8)]      # smaller than the cell
    jpeg = compose(tiles, [[]], 1, tile, wall_fps=1.0)
    from io import BytesIO
    from PIL import Image
    img = np.asarray(Image.open(BytesIO(jpeg)).convert("RGB"))
    assert img.shape[:2] == (tile, tile)
    assert int(img[tile // 2, tile // 2, 0]) > 120


@pytest.mark.parametrize("extent", [0.5, 1.9, 4.0])
def test_each_scene_gets_a_camera_that_frames_its_own_bbox(extent):
    """Every tile sizes its camera from its own object, so a short subject is
    not left tiny just because another object in the wall is tall."""
    lo = torch.zeros(3)
    hi = torch.tensor([extent * 0.5, extent, extent * 0.5])
    radius = safe_orbit_radius(lo, hi)
    fov = playback_fov_deg(lo, hi, radius, fill=0.8)

    assert radius >= MIN_CAMERA_DISTANCE, "camera must clear the rasterizer's floor"
    # the subject's bounding sphere should span ~fill of the half-frame
    obj_radius = float((hi - lo).norm()) * 0.5
    subtended = math.degrees(2 * math.atan(obj_radius / radius))
    assert subtended / fov == pytest.approx(0.8, abs=0.05)
    assert 0 < fov <= 90.0
