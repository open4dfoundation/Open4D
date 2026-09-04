"""Importing frames a method exported in another interpreter.

`streamer.adopt` exists because some methods cannot be driven from this package
at all -- ReRF's entropy coder is a Python 3.8 binary, and this needs 3.10 --
so the handoff is a directory plus a sidecar. These check the handoff, since a
malformed one is otherwise discovered as a pane that plays for two seconds and
then 404s.
"""
from __future__ import annotations

import json

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
