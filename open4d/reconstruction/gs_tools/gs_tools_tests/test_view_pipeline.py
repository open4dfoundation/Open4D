"""The parts of `gs-tools export` / `gs-tools view` that need no GPU.

Deliberately torch-free and CUDA-free, so this runs on any machine: what it
covers is the format and detection logic, which is where a mistake is silent.
The decode paths themselves (Vega's colour model, ReRF's entropy coder) can only
be exercised on the training host, in two different environments, against data
that is not in the repository -- so they are checked there, by hand, and what is
pinned here is everything around them.
"""

from __future__ import annotations

import json
import urllib.request
from pathlib import Path

import numpy as np
import pytest

from gs_tools import outputs
from streamer import bundle

from gs_tools.io import ply
from gs_tools.methods import rerf


# --------------------------------------------------------------------- PLY ---
def _gaussians(n: int, *, seed: int = 0) -> dict:
    rng = np.random.default_rng(seed)
    return {
        "xyz": rng.normal(size=(n, 3)).astype(np.float32),
        "scale_raw": rng.normal(-4, 1, size=(n, 3)).astype(np.float32),
        "rot_raw": rng.normal(size=(n, 4)).astype(np.float32),
        "opacity_raw": rng.normal(size=(n, 1)).astype(np.float32),
        "sh_dc": rng.normal(size=(n, 3)).astype(np.float32),
    }


def test_ply_round_trip_is_exact(tmp_path):
    data = _gaussians(97)
    path = ply.write(tmp_path / "frame.ply", **data)
    read = ply.read(path)
    assert read["count"] == 97
    assert read["sh_degree"] == 0
    for key, expected in data.items():
        np.testing.assert_array_equal(read[key], expected)


def test_ply_round_trip_carries_higher_sh_bands(tmp_path):
    data = _gaussians(31, seed=1)
    rng = np.random.default_rng(2)
    # Degree 3 is 15 bands per channel, the shape both upstream trainers write.
    rest = rng.normal(size=(31, 15, 3)).astype(np.float32)
    path = ply.write(tmp_path / "frame.ply", **data, sh_rest=rest)
    read = ply.read(path)
    assert read["sh_degree"] == 3
    np.testing.assert_array_equal(read["sh_rest"], rest)


def test_ply_attribute_order_matches_upstream():
    # The order is what every other 3DGS reader indexes by name against; a
    # reordering here would still round-trip through `read` and break everywhere
    # else, so it is pinned rather than derived.
    names = ply.attribute_names(0)
    assert names[:9] == ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    assert names[9:] == ["opacity", "scale_0", "scale_1", "scale_2",
                         "rot_0", "rot_1", "rot_2", "rot_3"]
    assert ply.attribute_names(15)[9:12] == ["f_rest_0", "f_rest_1", "f_rest_2"]


def test_sh_dc_is_the_inverse_of_rgb():
    rgb = np.linspace(0, 1, 30, dtype=np.float32).reshape(10, 3)
    np.testing.assert_allclose(ply.sh_dc_to_rgb(ply.rgb_to_sh_dc(rgb)), rgb, atol=1e-6)


def test_ply_count_reads_only_the_header(tmp_path):
    path = ply.write(tmp_path / "frame.ply", **_gaussians(5))
    assert ply.count(path) == 5


def test_ply_write_rejects_mismatched_row_counts(tmp_path):
    data = _gaussians(10)
    data["opacity_raw"] = data["opacity_raw"][:9]
    with pytest.raises(ValueError, match="9 rows"):
        ply.write(tmp_path / "frame.ply", **data)


def test_ply_read_rejects_ascii(tmp_path):
    path = tmp_path / "ascii.ply"
    path.write_text("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nend_header\n0\n")
    with pytest.raises(ValueError, match="ascii"):
        ply.read(path)


# ----------------------------------------------------------------- outputs ---
def test_detect_vega_catalog(tmp_path):
    (tmp_path / "catalog.json").write_text(json.dumps({
        "baseline": "Vega-ORBIT",
        "objects": [{"name": "dancer", "dir": "dancer", "frame_count": 30}],
    }))
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.VEGA_CATALOG
    assert found.detail["objects"] == ["dancer"]
    assert "vega-catalog" in outputs.describe(found)


def test_detect_vega_bitstream(tmp_path):
    (tmp_path / "color_model.pt").write_bytes(b"")
    (tmp_path / "manifest.json").write_text(json.dumps({"frames": [{"frame_idx": 0}]}))
    assert outputs.detect(tmp_path).kind is outputs.Kind.VEGA_BITSTREAM


def test_detect_vega_scene_export(tmp_path):
    (tmp_path / "scene_manifest.json").write_text(json.dumps(
        {"layout": "row", "frames": [{"frame_idx": 0, "file": "frame_0000.pt"}], "objects": []}))
    (tmp_path / "frame_0000.pt").write_bytes(b"")
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.VEGA_SCENE_EXPORT
    assert found.detail["layout"] == "row"


def _rerf_bitstream(root: Path, *, frames: int = 4, pca: bool = True, group_size: int = 4) -> Path:
    """A ReRF bitstream's headers, written the way `codec/compress.py` writes them."""
    root.mkdir(parents=True, exist_ok=True)
    (root / "model_kwargs.json").write_text(json.dumps({"xyz_min": [0, 0, 0], "xyz_max": [1, 1, 1]}))
    (root / "rgb_net.tar").write_bytes(b"")
    for index in range(frames):
        key = index % group_size == 0
        if key or not pca:
            entries = [{"origin_size": [13, 8, 16, 8], "quality": 99}]
        else:
            entries = [{"origin_size": [7, 8, 16, 8], "quality": 99},
                       {"origin_size": [6, 8, 16, 8], "quality": 98}]
        (root / f"header_{index}.json").write_text(json.dumps({"headers": entries}))
    return root


def test_detect_rerf_bitstream(tmp_path):
    found = outputs.detect(_rerf_bitstream(tmp_path / "rerf"))
    assert found.kind is outputs.Kind.RERF_BITSTREAM
    assert found.detail["frames"] == 4


def test_detect_rerf_run_lists_bitstreams_and_renders(tmp_path):
    _rerf_bitstream(tmp_path / "rerf")
    (tmp_path / "config.py").write_text(
        "expname = 'g_x'\nbasedir = '/runs'\ndata = dict(\n    datadir='/corpus/x',\n)\n")
    render = tmp_path / "render_360_rerf_4"
    render.mkdir()
    for index in range(4):
        (render / f"{index:03d}.jpg").write_bytes(b"")
        (render / f"{index:03d}_depth.jpg").write_bytes(b"")
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.RERF_RUN
    assert found.detail["expname"] == "g_x"
    # Indented, because it is nested inside `data = dict(...)`.
    assert found.detail["datadir"] == "/corpus/x"
    assert found.detail["bitstreams"] == ["rerf"]
    assert found.detail["renders"] == ["render_360_rerf_4"]


def test_detect_image_sequence_ignores_depth_companions(tmp_path):
    for index in range(3):
        (tmp_path / f"{index:03d}.jpg").write_bytes(b"")
        (tmp_path / f"{index:03d}_depth.jpg").write_bytes(b"")
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.IMAGE_SEQUENCE
    assert found.detail["frames"] == 3


def test_detect_gaussian_run(tmp_path):
    for iteration in (7000, 30000):
        target = tmp_path / "point_cloud" / f"iteration_{iteration}"
        target.mkdir(parents=True)
        ply.write(target / "point_cloud.ply", **_gaussians(3))
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.GAUSSIAN_RUN
    assert found.detail["iterations"] == [7000, 30000]


def test_detect_unknown_and_missing(tmp_path):
    assert outputs.detect(tmp_path).kind is outputs.Kind.UNKNOWN
    assert not outputs.detect(tmp_path / "nope").viewable


# ------------------------------------------------------------------ bundle ---
def test_bundle_round_trip(tmp_path):
    clip = bundle.Clip(name="dancer", representation="gaussians", frames=["dancer/frame_0000.ply"],
                       counts=[12], bounds_min=[0, 0, 0], bounds_max=[1, 1, 1],
                       notes=["colour baked"])
    bundle.write(tmp_path, title="Vega — dancer", source="/somewhere", clips=[clip], fps=24)
    index = bundle.read(tmp_path)
    assert index["version"] == bundle.VERSION
    assert index["fps"] == 24
    assert index["clips"][0]["notes"] == ["colour baked"]
    found = outputs.detect(tmp_path)
    assert found.kind is outputs.Kind.BUNDLE
    assert found.detail["frames"] == 1


def test_bundle_read_of_a_plain_directory_is_empty(tmp_path):
    assert bundle.read(tmp_path) == {}


# -------------------------------------------------------------------- ReRF ---
def test_bitstream_info_infers_the_pca_split_and_group_size(tmp_path):
    info = rerf.bitstream_info(_rerf_bitstream(tmp_path / "rerf", frames=6, group_size=3))
    assert info["frames"] == 6
    assert info["key_frames"] == [0, 3]
    assert info["group_size"] == 3
    assert info["pca"] is True
    assert info["pca_chs"] == (7, 13)
    assert info["feature_dim"] == 13
    assert info["quality"] == [99, 98]
    assert info["grid"] == [8, 16, 8]


def test_bitstream_info_without_pca(tmp_path):
    info = rerf.bitstream_info(_rerf_bitstream(tmp_path / "rerf", frames=3, pca=False))
    assert info["pca"] is False
    assert info["pca_chs"] == ()
    # Every frame looks like a key frame with PCA off, so the group is one frame.
    assert info["group_size"] == 1


def test_bitstream_info_needs_a_bitstream(tmp_path):
    with pytest.raises(FileNotFoundError, match="model_kwargs.json"):
        rerf.bitstream_info(tmp_path)


def test_render_command_passes_the_inferred_codec_configuration(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf", frames=6, group_size=3)
    info = rerf.bitstream_info(root)
    config = tmp_path / "config.py"
    config.write_text("expname = 'g_x'\n")
    command = rerf.render_command(config, root, 6, info, rerf.RerfRenderOptions())
    assert command[1:4] == ["-m", "orbitnevo.rerf_cli", "rerf_render.py"]
    # Upstream defaults --frame_num to 20000 and derives group_size from it, so
    # omitting either silently changes which frames decode as key frames.
    for flag, value in (("--render_360", "6"), ("--frame_num", "6"),
                        ("--group_size", "3"), ("--pca_chs", "7,13")):
        assert command[command.index(flag) + 1] == value
    assert "--pca" in command


def test_render_command_honours_overrides(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf", frames=6, group_size=3)
    info = rerf.bitstream_info(root)
    config = tmp_path / "config.py"
    config.write_text("expname = 'g_x'\n")
    options = rerf.RerfRenderOptions(pca=False, group_size=2)
    command = rerf.render_command(config, root, 6, info, options)
    assert "--pca" not in command
    assert command[command.index("--group_size") + 1] == "2"


def test_render_refuses_without_permission_and_says_how(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf")
    (tmp_path / "config.py").write_text("expname = 'g_x'\n")
    with pytest.raises(RuntimeError, match="--render was not given"):
        rerf.render(tmp_path, root, rerf.RerfRenderOptions())


def test_render_refuses_more_frames_than_were_compressed(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf", frames=4)
    with pytest.raises(ValueError, match="4 compressed frames"):
        rerf.render(tmp_path, root, rerf.RerfRenderOptions(frames=10, render=True))


def test_render_reports_a_missing_corpus(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf")
    (tmp_path / "config.py").write_text("data = dict(\n    datadir='/nonexistent/corpus',\n)\n")
    with pytest.raises(FileNotFoundError, match="corpus /nonexistent/corpus"):
        rerf.render(tmp_path, root, rerf.RerfRenderOptions(render=True))


def test_collect_separates_colour_from_depth(tmp_path):
    images = tmp_path / "render_360_rerf_3"
    images.mkdir()
    for index in range(3):
        (images / f"{index:03d}.jpg").write_bytes(b"colour")
        (images / f"{index:03d}_depth.jpg").write_bytes(b"depth")
    clips = rerf.collect(images, tmp_path / "out", "run-rerf", rerf.RerfRenderOptions())
    assert [clip.name for clip in clips] == ["run-rerf", "run-rerf-depth"]
    assert all(clip.representation == "pixels" for clip in clips)
    assert (tmp_path / "out" / "run-rerf" / "frame_0000.jpg").read_bytes() == b"colour"
    assert (tmp_path / "out" / "run-rerf-depth" / "frame_0000.jpg").read_bytes() == b"depth"

    only_colour = rerf.collect(images, tmp_path / "out2", "run-rerf",
                               rerf.RerfRenderOptions(depth=False))
    assert [clip.name for clip in only_colour] == ["run-rerf"]


def test_export_of_an_image_sequence_needs_no_render(tmp_path):
    images = tmp_path / "render_360_rerf_2"
    images.mkdir()
    for index in range(2):
        (images / f"{index:03d}.jpg").write_bytes(b"colour")
    out = rerf.export(images, tmp_path / "out", rerf.RerfRenderOptions())
    index = bundle.read(out)
    assert index["title"].startswith("ReRF")
    assert index["clips"][0]["frames"] == ["render_360_rerf_2/frame_0000.jpg",
                                           "render_360_rerf_2/frame_0001.jpg"]


def test_export_needs_a_bitstream_choice_when_a_run_holds_several(tmp_path):
    for name in ("rerf", "rerf_whitebg"):
        _rerf_bitstream(tmp_path / name)
    (tmp_path / "config.py").write_text("expname = 'g_x'\n")
    with pytest.raises(ValueError, match="--bitstream"):
        rerf.export(tmp_path, tmp_path / "out", rerf.RerfRenderOptions())


# -------------------------------------------------------------------- view ---
def test_serve_hands_out_the_viewer_and_the_bundle(tmp_path):
    from streamer import server as view

    frame = ply.write(tmp_path / "dancer" / "frame_0000.ply", **_gaussians(4))
    bundle.write(tmp_path, title="t", source="s", clips=[bundle.Clip(
        name="dancer", representation="gaussians",
        frames=[str(frame.relative_to(tmp_path))], counts=[4])])

    server = view.serve(tmp_path, port=0, block=False)
    base = f"http://127.0.0.1:{server.server_address[1]}"
    try:
        with urllib.request.urlopen(f"{base}/", timeout=10) as response:
            assert response.headers["Content-Type"].startswith("text/html")
            assert b"gs-tools view" in response.read()
        with urllib.request.urlopen(f"{base}/view.json", timeout=10) as response:
            assert json.loads(response.read())["clips"][0]["name"] == "dancer"
        with urllib.request.urlopen(f"{base}/dancer/frame_0000.ply", timeout=10) as response:
            # text/html here is the failure mode: the browser's PLY parse would
            # succeed on the bytes but the fetch would be flagged as a mismatch.
            assert response.headers["Content-Type"] == "application/octet-stream"
            assert response.read()[:3] == b"ply"
    finally:
        server.shutdown()
        server.server_close()


def test_serve_refuses_a_directory_that_is_not_a_bundle(tmp_path):
    from streamer import server as view

    with pytest.raises(FileNotFoundError, match="view.json"):
        view.serve(tmp_path, port=0, block=False)
