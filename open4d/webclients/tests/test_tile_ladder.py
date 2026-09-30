"""The catalogue-backed ladder: faithful where it can be, loud where it cannot.

These build a synthetic prepared corpus on disk rather than leaning on
`ORBIT_vivo_tiles`, so the invariants hold on any machine.
"""

import json

import pytest

from open4d.webclients.tile_ladder import (
    SourceCorpusUnavailable,
    TileCatalogLadder,
)


def write_catalog(root, *, objects=None, representations=None, version=2):
    levels = representations if representations is not None else [
        {"id": 0, "name": "level_1", "width": 1280, "height": 960, "point_ratio": 1.0},
        {"id": 1, "name": "level_2", "width": 640, "height": 480, "point_ratio": 0.25},
    ]
    entries = objects if objects is not None else [
        {
            "object_id": 0, "name": "dancer",
            "source_start_frame": 1, "source_frame_count": 300,
            "master_loop_frames": 600,
            "bounds_min": [-0.6, 2.1, -2.1], "bounds_max": [0.8, 3.9, -0.9],
        },
        {
            "object_id": 3, "name": "thomas",
            "source_start_frame": 618, "source_frame_count": 600,
            "master_loop_frames": 600,
            "bounds_min": [-1.0, 0.0, -1.0], "bounds_max": [1.0, 2.0, 1.0],
        },
    ]
    for level in levels:
        (root / str(level["name"])).mkdir(parents=True, exist_ok=True)
    (root / "catalog.json").write_text(json.dumps({
        "version": version, "baseline": "ViVo-ORBIT", "grid": 4,
        "tile_count_per_object": 64,
        "objects": entries, "representations": levels,
    }), encoding="utf-8")
    return root


def test_reads_levels_and_objects_from_the_catalogue(tmp_path):
    ladder = TileCatalogLadder(write_catalog(tmp_path))

    assert [obj.name for obj in ladder.objects] == ["dancer", "thomas"]
    assert [rep.name for rep in ladder.representations] == ["level_1", "level_2"]
    assert ladder.highest.manifest.width == 1280
    assert ladder.highest.manifest.height == 960
    assert ladder.representation(1).point_ratio == pytest.approx(0.25)
    assert ladder.object(1, 3).name == "thomas"
    assert ladder.describe()["source"] == (
        "prepared tile catalogue (no RGB-D manifests)")


def test_source_frame_matches_the_manifest_arithmetic(tmp_path):
    ladder = TileCatalogLadder(write_catalog(tmp_path))
    dancer = ladder.objects[0]
    # source_start_frame + (master_frame % source_frame_count)
    assert dancer.source_frame(0) == 1
    assert dancer.source_frame(299) == 300
    assert dancer.source_frame(300) == 1
    assert dancer.source_frame(700) == 101


def test_absent_capture_rig_raises_instead_of_inventing_one(tmp_path):
    ladder = TileCatalogLadder(write_catalog(tmp_path))
    dancer = ladder.objects[0]
    # The whole point: a baseline that needs the real rig must fail loudly
    # rather than receive fabricated calibration or a path that does not exist.
    with pytest.raises(SourceCorpusUnavailable):
        dancer.cameras
    with pytest.raises(SourceCorpusUnavailable):
        dancer.image_paths(tmp_path, 0, 0)
    with pytest.raises(SourceCorpusUnavailable):
        dancer.pointcloud_path(tmp_path, 0, 0)
    with pytest.raises(SourceCorpusUnavailable):
        ladder.highest.manifest.stream_count


def test_calibration_hash_is_labelled_and_tracks_bounds(tmp_path):
    first = TileCatalogLadder(write_catalog(tmp_path / "a"))
    digest = first.highest.manifest.calibration_hash
    # Never mistakable for a DatasetManifest hash, which is bare hex.
    assert digest.startswith("tiles-nocal:")

    moved = [
        {
            "object_id": 0, "name": "dancer",
            "source_start_frame": 1, "source_frame_count": 300,
            "master_loop_frames": 600,
            "bounds_min": [-0.6, 2.1, -2.1], "bounds_max": [0.8, 3.9, -0.5],
        },
    ]
    second = TileCatalogLadder(write_catalog(tmp_path / "b", objects=moved))
    assert second.highest.manifest.calibration_hash != digest


def test_block_size_divides_every_declared_level(tmp_path):
    ladder = TileCatalogLadder(write_catalog(tmp_path))
    for rep in ladder.representations:
        assert rep.manifest.block_size == 16
        assert rep.manifest.width % 16 == 0
        assert rep.manifest.height % 16 == 0

    ragged = [{"id": 0, "name": "level_1", "width": 1290, "height": 960,
               "point_ratio": 1.0}]
    with pytest.raises(ValueError, match="divisible by the block size"):
        TileCatalogLadder(write_catalog(tmp_path / "ragged",
                                        representations=ragged))


def test_rejects_a_catalogue_it_cannot_read_faithfully(tmp_path):
    # An older catalogue predates the width/height/point_ratio fields, so
    # there is nothing to fall back on and guessing them would be fabrication.
    with pytest.raises(ValueError, match="catalogue version"):
        TileCatalogLadder(write_catalog(tmp_path / "v1", version=1))

    # Representation 0 must be the highest quality: the servers index the
    # ladder assuming that order and would silently cap quality if it slipped.
    inverted = [
        {"id": 0, "name": "level_1", "width": 640, "height": 480, "point_ratio": 0.25},
        {"id": 1, "name": "level_2", "width": 1280, "height": 960, "point_ratio": 1.0},
    ]
    with pytest.raises(ValueError, match="highest-quality"):
        TileCatalogLadder(write_catalog(tmp_path / "inv",
                                        representations=inverted))

    # A level in the catalogue with no directory means catalogue and tiles
    # have diverged; serving would 404 per frame instead of failing at start.
    root = write_catalog(tmp_path / "gone")
    (root / "level_2").rmdir()
    with pytest.raises(FileNotFoundError):
        TileCatalogLadder(root)


def test_objects_must_agree_on_the_master_loop(tmp_path):
    mismatched = [
        {"object_id": 0, "name": "a", "source_start_frame": 1,
         "source_frame_count": 300, "master_loop_frames": 600,
         "bounds_min": [0, 0, 0], "bounds_max": [1, 1, 1]},
        {"object_id": 1, "name": "b", "source_start_frame": 1,
         "source_frame_count": 300, "master_loop_frames": 900,
         "bounds_min": [0, 0, 0], "bounds_max": [1, 1, 1]},
    ]
    with pytest.raises(ValueError, match="master_loop_frames"):
        TileCatalogLadder(write_catalog(tmp_path, objects=mismatched))
