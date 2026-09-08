"""Importing frames a method exported in another interpreter.

`streamer.adopt` exists because some methods cannot be driven from this package
at all -- ReRF's entropy coder is a Python 3.8 binary, and this needs 3.10 --
so the handoff is a directory plus a sidecar. These check the handoff, since a
malformed one is otherwise discovered as a pane that plays for two seconds and
then 404s.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

from streamer import adopt, bundle

pytestmark = pytest.mark.cpu


def an_export(root, *, clips=2, frames=3, name="obj", missing=False):
    root.mkdir(parents=True, exist_ok=True)
    entries = []
    for index in range(clips):
        clip = f"{name}-cam{index:02d}"
        paths = []
        for frame in range(frames):
            relative = f"{clip}/frame_{frame:04d}.jpg"
            paths.append(relative)
            if missing and frame == frames - 1 and index == 0:
                continue                      # the export died partway through
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"\xff\xd8\xff\xd9")
        entries.append({
            "name": clip, "method": "rerf", "camera": index, "frames": paths,
            "notes": ["a note the renderer wanted shown"],
            "detail": {"view": index},
        })
    (root / "clips.json").write_text(json.dumps({
        "format": "rerf-clips", "version": 1, "scene": "basketball",
        "representation": "pixels", "clips": entries,
    }))
    return root


def a_bundle(root):
    root.mkdir(parents=True, exist_ok=True)
    bundle.write(root, title="t", source="s", fps=24,
                 scenes={"basketball": {"poses": []}},
                 clips=[bundle.Clip(name="already", representation="mesh",
                                    frames=["already/f.ply"])])
    return root


def test_frames_are_copied_into_the_bundle(tmp_path):
    """A bundle has to be servable and fetchable whole, so a clip may not point
    outside it."""
    export = an_export(tmp_path / "export")
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)

    index = bundle.read(root)
    imported = [c for c in index["clips"] if c["name"].startswith("obj-")]
    assert len(imported) == 2
    for clip in imported:
        for relative in clip["frames"]:
            assert (root / relative).is_file(), relative


def test_the_scene_and_representation_come_from_the_sidecar(tmp_path):
    export = an_export(tmp_path / "export")
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)
    clip = next(c for c in bundle.read(root)["clips"] if c["name"] == "obj-cam00")
    assert clip["scene"] == "basketball"
    assert clip["representation"] == "pixels"
    assert clip["camera"] == 0
    assert clip["notes"] == ["a note the renderer wanted shown"]


def test_existing_clips_survive(tmp_path):
    export = an_export(tmp_path / "export")
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)
    names = [c["name"] for c in bundle.read(root)["clips"]]
    assert names[0] == "already"
    assert bundle.read(root)["fps"] == 24


def test_a_half_written_export_is_refused(tmp_path):
    """Better than importing it: a clip whose frames are partly there plays and
    then 404s, and the manifest says nothing is wrong."""
    export = an_export(tmp_path / "export", missing=True)
    root = a_bundle(tmp_path / "view")
    with pytest.raises(FileNotFoundError, match="did not finish"):
        adopt.adopt(export, root)
    # And nothing was added.
    assert [c["name"] for c in bundle.read(root)["clips"]] == ["already"]


def test_re_exporting_needs_replace(tmp_path):
    export = an_export(tmp_path / "export")
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)
    with pytest.raises(ValueError, match="already has a clip named"):
        adopt.adopt(export, root)
    adopt.adopt(export, root, replace=True)
    names = [c["name"] for c in bundle.read(root)["clips"]]
    assert names.count("obj-cam00") == 1


def test_a_missing_sidecar_names_the_exporter(tmp_path):
    root = a_bundle(tmp_path / "view")
    (tmp_path / "empty").mkdir()
    with pytest.raises(FileNotFoundError, match="rerf_stream.export"):
        adopt.adopt(tmp_path / "empty", root)


def test_an_unknown_format_is_refused(tmp_path):
    export = an_export(tmp_path / "export")
    payload = json.loads((export / "clips.json").read_text())
    payload["version"] = 99
    (export / "clips.json").write_text(json.dumps(payload))
    root = a_bundle(tmp_path / "view")
    with pytest.raises(ValueError, match="not a format this understands"):
        adopt.adopt(export, root)


def test_an_export_with_no_clips_is_refused(tmp_path):
    export = an_export(tmp_path / "export")
    (export / "clips.json").write_text(json.dumps({
        "format": "rerf-clips", "version": 1, "clips": []}))
    with pytest.raises(ValueError, match="lists no clips"):
        adopt.adopt(export, a_bundle(tmp_path / "view"))


# ------------------------------------------------------------ the scene rig ---


def a_rig(stations=3):
    return {
        "width": 1280, "height": 960, "fov_y": 0.7,
        "bounds_min": [-1.0, 0.0, -1.0], "bounds_max": [1.0, 2.0, 1.0],
        "poses": [
            {"position": [float(n), 0.0, 0.0], "right": [1.0, 0.0, 0.0],
             "down": [0.0, 1.0, 0.0], "forward": [0.0, 0.0, 1.0]}
            for n in range(stations)
        ],
    }


def with_rig(export, rig):
    payload = json.loads((export / "clips.json").read_text())
    payload["rig"] = rig
    (export / "clips.json").write_text(json.dumps(payload))
    return export


def test_the_rig_is_installed_for_a_scene_the_bundle_did_not_know(tmp_path):
    """Without one, a viewer cannot offer station selection, so the scene's
    panes are shown but not comparable to each other by pose."""
    export = with_rig(an_export(tmp_path / "export"), a_rig())
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)

    scenes = bundle.read(root)["scenes"]
    assert "basketball" in scenes
    assert len(scenes["basketball"]["poses"]) == 3
    assert scenes["basketball"]["scene"] == "basketball"


def test_an_existing_rig_is_not_overwritten(tmp_path):
    """A rig that came from a geometry method is the authority: that is the
    output which has to line up in 3D, and silently replacing it would move
    every other method's camera."""
    root = a_bundle(tmp_path / "view")
    index = bundle.read(root)
    bundle.write(
        root, title=index["title"], source=index["source"],
        clips=[bundle.Clip(**c) for c in index["clips"]], fps=index["fps"],
        scenes={"basketball": dict(a_rig(stations=8), scene="basketball",
                                   origin="from-geometry")},
    )
    adopt.adopt(with_rig(an_export(tmp_path / "export"), a_rig(stations=3)), root)

    scene = bundle.read(root)["scenes"]["basketball"]
    assert len(scene["poses"]) == 8
    assert scene["origin"] == "from-geometry"


def test_an_export_without_a_rig_still_imports(tmp_path):
    """Not every method knows the rig, and clips are worth having regardless."""
    export = an_export(tmp_path / "export")
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)
    assert bundle.read(root)["scenes"] == {"basketball": {"poses": []}}
    assert any(c["name"] == "obj-cam00" for c in bundle.read(root)["clips"])


def test_installing_the_rig_keeps_the_clips_that_were_there(tmp_path):
    """It rewrites the manifest, so the pre-existing clips have to survive."""
    export = with_rig(an_export(tmp_path / "export"), a_rig())
    root = a_bundle(tmp_path / "view")
    adopt.adopt(export, root)
    names = [c["name"] for c in bundle.read(root)["clips"]]
    assert "already" in names
    assert len(names) == 3


# ------------------------------------------------ adopting a whole bundle ---
# `gs-tools export` writes a bundle, not a `clips.json` sidecar, so building one
# bundle from several exporters used to need a hand-rolled merge. A bundle is a
# superset of the sidecar, so it is read here instead.


def _bundle_at(directory: Path, clips, scenes=None) -> Path:
    for clip in clips:
        for relative in clip.frames:
            target = directory / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"x" * 16)
    return bundle.write(directory, title="staged", source="somewhere",
                   clips=list(clips), scenes=scenes or {})


def test_a_staged_bundle_can_be_adopted(tmp_path):
    target = tmp_path / "target"
    _bundle_at(target, [bundle.Clip(name="a", representation="pixels", scene="s",
                               method="rerf", camera=0,
                               frames=["a/frame_0000.jpg"])])
    staged = tmp_path / "staged"
    _bundle_at(staged, [bundle.Clip(name="v", representation="gaussians", scene="s",
                               method="vega", counts=[57907],
                               frames=["v/frame_0000.splat"])])

    adopt.adopt(staged, target)
    index = bundle.read(target)
    names = {clip["name"]: clip for clip in index["clips"]}
    assert set(names) == {"a", "v"}
    # Copied, not referenced: a bundle has to be servable and movable whole.
    assert (target / "v/frame_0000.splat").is_file()
    # And the fields a sidecar does not have survive.
    assert names["v"]["counts"] == [57907]
    assert names["v"]["representation"] == "gaussians"


def test_a_bundle_s_clips_keep_their_own_representation(tmp_path):
    """A sidecar states one representation for the whole export; a bundle
    carries a mix, and taking the top-level value would relabel a point cloud
    as pixels -- which sends it to the image decoder and blanks the pane."""
    target = tmp_path / "target"
    _bundle_at(target, [bundle.Clip(name="keep", representation="pixels", scene="s",
                               frames=["keep/frame_0000.jpg"])])
    staged = tmp_path / "staged"
    _bundle_at(staged, [
        bundle.Clip(name="px", representation="pixels", scene="s", method="rerf",
               camera=0, frames=["px/frame_0000.jpg"]),
        bundle.Clip(name="pts", representation="points", scene="s", method="rerf",
               frames=["pts/frame_0000.ply"]),
    ])

    adopt.adopt(staged, target)
    got = {clip["name"]: clip["representation"] for clip in bundle.read(target)["clips"]}
    assert got["px"] == "pixels"
    assert got["pts"] == "points"


def test_a_directory_with_neither_names_both(tmp_path):
    with pytest.raises(FileNotFoundError, match="neither clips.json nor a bundle"):
        adopt.read_export(tmp_path)


def test_a_bundle_brings_a_rig_for_each_of_its_scenes(tmp_path):
    """A sidecar names one rig for one scene; a bundle carries several."""
    target = tmp_path / "target"
    _bundle_at(target, [bundle.Clip(name="keep", representation="pixels", scene="a",
                               frames=["keep/frame_0000.jpg"])])
    staged = tmp_path / "staged"
    rig = {"width": 8, "height": 8, "fov_y": 1.0,
           "poses": [{"position": [0, 0, 1], "right": [1, 0, 0],
                      "down": [0, 1, 0], "forward": [0, 0, -1]}]}
    _bundle_at(
        staged,
        [bundle.Clip(name="a1", representation="gaussians", scene="a",
                frames=["a1/frame_0000.splat"]),
         bundle.Clip(name="b1", representation="gaussians", scene="b",
                frames=["b1/frame_0000.splat"])],
        scenes={"a": dict(rig), "b": dict(rig)},
    )

    adopt.adopt(staged, target)
    scenes = bundle.read(target)["scenes"]
    assert set(scenes) == {"a", "b"}
    assert all(len(scene["poses"]) == 1 for scene in scenes.values())


# ------------------------------------- a camera has to index a real rig ---


def test_a_clip_numbered_past_its_rig_is_refused(tmp_path):
    """Reachable by import order alone.

    An export installs a rig only into a scene that has none, so whichever
    lands first wins. A Vega export brings the corpus's 8-camera capture rig; a
    ReRF orbit export brings 216 stations. Import them the other way round and
    the orbit clips are numbered against a rig that stops at 7 -- and most
    Compare stations show nothing, silently, because an empty pane is a
    legitimate state.
    """
    target = tmp_path / "target"
    small = {"width": 8, "height": 8, "fov_y": 1.0,
             "poses": [{"position": [0, 0, 1], "right": [1, 0, 0],
                        "down": [0, 1, 0], "forward": [0, 0, -1]}]}
    _bundle_at(target, [bundle.Clip(name="keep", representation="gaussians",
                               scene="s", frames=["keep/frame_0000.splat"])],
               scenes={"s": small})
    staged = tmp_path / "staged"
    _bundle_at(staged, [bundle.Clip(name="cam05", representation="pixels", scene="s",
                               method="rerf", camera=5,
                               frames=["cam05/frame_0000.jpg"])])

    with pytest.raises(ValueError, match="camera 5, but scene 's' has 1 rig pose"):
        adopt.adopt(staged, target)


def test_a_scene_with_no_rig_is_explore_only_not_an_error(tmp_path):
    """No rig means no station selection, which the viewer reports. Only a rig
    that exists and is too short is a mismatch."""
    target = tmp_path / "target"
    _bundle_at(target, [bundle.Clip(name="keep", representation="gaussians",
                               scene="s", frames=["keep/frame_0000.splat"])])
    staged = tmp_path / "staged"
    _bundle_at(staged, [bundle.Clip(name="cam09", representation="pixels", scene="s",
                               method="rerf", camera=9,
                               frames=["cam09/frame_0000.jpg"])])

    adopt.adopt(staged, target)          # no raise
    assert len(bundle.read(target)["clips"]) == 2
