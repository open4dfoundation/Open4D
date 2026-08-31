"""Build a static page for eyeballing everything this baseline produces.

Numbers in a JSON file are easy to be wrong about quietly. This renders the
things worth looking at directly:

1. **Corpus** -- the prepared views and their mattes, per object and frame.
   Catches a bad crop, an inverted mask, a mis-ordered rig.
2. **Reload** -- a trained frame rendered at a training viewpoint, beside the
   image it was trained on. Catches a checkpoint reassembled wrongly.
3. **Filtering** -- the same viewpoint rendered with the whole feature grid and
   with everything below an importance threshold dropped, plus the amplified
   difference. This is what the CDF is actually claiming.
4. **Importance CDF** -- the Figure 7 plots and the numbers behind them.
5. **Rate-distortion** -- what a frame actually costs in ReRF's own encoder at
   each threshold, against the quality delivered at the captured camera. The
   axis a comparison against another representation is made on.

    python -m orbitnevo.report --out ~/nevo_report --serve 8752

Runs in the ``nevo`` environment; sections 2 and 3 need a trained sequence and
a GPU and are skipped (with a note on the page) when there is none.
"""
from __future__ import annotations

import argparse
import csv
import dataclasses
import datetime
import glob
import html
import json
import re
import shutil
import sys
from pathlib import Path

import numpy as np

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from nevo import rerf_env  # noqa: E402

THUMBNAIL_WIDTH = 260
VIEW_WIDTH = 420


def _save(image, path: Path, width: int) -> str:
    """Write a JPEG scaled to ``width`` and return its path relative to the page."""
    from PIL import Image

    if isinstance(image, np.ndarray):
        array = np.clip(image, 0.0, 1.0) if image.dtype != np.uint8 else image
        if array.dtype != np.uint8:
            array = (array * 255.0).astype(np.uint8)
        handle = Image.fromarray(array)
    else:
        handle = image
    handle = handle.convert("RGB")
    height = max(1, round(handle.height * width / handle.width))
    handle = handle.resize((width, height), Image.LANCZOS)
    path.parent.mkdir(parents=True, exist_ok=True)
    handle.save(path, quality=88)
    # Relative to the page, not to the assets directory: index.html sits beside
    # `assets/`, so a bare filename resolves to the wrong place and every
    # thumbnail renders as a broken image.
    for parent in path.parents:
        if parent.name == "assets":
            return str(path.relative_to(parent.parent))
    return path.name


def _pick(values: list, count: int) -> list:
    """An evenly spaced subset of ``values``, always including the first.

    Used to thin the corpus contact sheet. A 48-camera rig over two frames over
    a dozen objects is several hundred thumbnails, which is not a page anyone
    can read -- and the point of the section is to spot a bad crop or an
    inverted matte, which a spread of six views shows as well as all of them.
    """
    if count <= 0 or len(values) <= count:
        return list(values)
    step = len(values) / count
    return [values[int(index * step)] for index in range(count)]


def _output_clips(output_root, assets: Path, width: int) -> list:
    """NeVo's own output, as player clips.

    Only the visibility-filtered conditions ``render_frames.py`` produced -- the
    stream NeVo would actually send. Plain ReRF and the captured camera are
    deliberately left out: they are the *comparison*, and they are already shown
    frame-by-frame with their numbers in the rate-distortion section, which is
    the honest place for a comparison. The player is for watching the output.
    """
    from PIL import Image

    clips = []
    for root in output_root:
        for manifest_path in sorted(Path(root).expanduser().glob("*/manifest.json")):
            with open(manifest_path) as handle:
                manifest = json.load(handle)
            directory = manifest_path.parent
            name = manifest["name"]
            conditions = list(manifest.get("conditions") or [
                {"name": "render", "prefix": "frame", "label": "reconstruction",
                 "threshold": None, "kept_fraction": 1.0}
            ])
            conditions = [c for c in conditions if c.get("threshold") is not None]
            for condition in conditions:
                target = assets / "play" / f"{name}_{condition['name']}"
                frames = []
                for frame in manifest["frames"]:
                    source = directory / f"{condition['prefix']}_{frame:03d}.png"
                    if not source.is_file():
                        continue
                    out = target / "rgb" / f"v00_f{frame:03d}.jpg"
                    if not out.is_file():
                        _save(Image.open(source), out, width)
                    frames.append(frame)
                if not frames:
                    continue
                kept = condition.get("kept_fraction")
                # Plain text, not markup: the player writes this with
                # textContent, so an HTML entity would show up verbatim.
                detail = (
                    f" \u2014 {kept * 100:.0f}% of non-empty blocks delivered"
                    if kept is not None and condition.get("threshold") is not None
                    else ""
                )
                clips.append(
                    {
                        "name": f"{name} \u2014 {condition['label']}",
                        "path": f"assets/play/{name}_{condition['name']}",
                        "frames": frames,
                        "views": [0],
                        "source": f"camera {manifest['view']}{detail}",
                        "size": [manifest["width"], manifest["height"]],
                        "no_matte": True,
                    }
                )
    return clips


def _player_section(corpora, assets: Path, views_shown: int, width: int,
                    output_clips=()) -> str:
    """A plain playback of the prepared corpus: pick an object and a camera, press play.

    This is an *input* check. A still tells you a crop is centred; only motion
    tells you it is centred on every frame, that the sequence is in order, and
    that the matte does not flicker. Nothing here is rendered or trained --
    these are the exact PNGs handed to ReRF, scaled down.
    """
    from PIL import Image

    clips = list(output_clips)
    if not corpora and not clips:
        return "<p class='none'>Nothing rendered or prepared yet.</p>"

    for corpus in corpora:
        with open(corpus / "nevo_corpus.json") as handle:
            manifest = json.load(handle)
        name = manifest["object"]
        frames = sorted(
            int(path.name) for path in (corpus / "image").iterdir() if path.name.isdigit()
        )
        views = _pick(list(range(len(manifest["cameras"]))), views_shown)
        target = assets / "play" / name
        for view in views:
            for frame in frames:
                for kind, folder in (("image", "rgb"), ("mask", "matte")):
                    source = corpus / kind / str(frame) / ("img_%04d.png" % view)
                    if not source.is_file():
                        continue
                    out = target / folder / f"v{view:02d}_f{frame:03d}.jpg"
                    if not out.is_file():
                        _save(Image.open(source), out, width)
        clips.append(
            {
                "name": name,
                "path": f"assets/play/{name}",
                "frames": frames,
                "views": views,
                "source": manifest["source"],
                "size": [manifest["width"], manifest["height"]],
            }
        )

    options = "".join(
        f"<option value='{html.escape(clip['name'])}'>{html.escape(clip['name'])}</option>"
        for clip in clips
    )
    return (
        "<p class='hint'>Pick a clip and press play. <b>&lt;name&gt; &mdash; render</b> is "
        "NeVo/ReRF's reconstruction, one rendered frame per input frame -- that is the output "
        "to compare against another representation. <b>&mdash; capture</b> is the real camera "
        "for the same frames. The bare object names are the prepared input ReRF trained on.</p>"
        "<div class='player'>"
        "<div class='stage'><img id='stage-image' alt='corpus playback'>"
        "<div id='stage-loading' class='loading'>loading&hellip;</div></div>"
        "<div class='controls'>"
        "<div class='row'>"
        "<button id='play' class='primary'>&#9654;&nbsp;play</button>"
        f"<label>object <select id='clip'>{options}</select></label>"
        "<label>camera <select id='view'></select></label>"
        "<label>fps <input id='fps' type='range' min='1' max='30' value='10'>"
        "<span id='fps-value' class='mono'>10</span></label>"
        "<label><input id='matte' type='checkbox'> show matte</label>"
        "</div>"
        "<div class='row'>"
        "<label class='grow'>frame <input id='frame' type='range' min='0' max='0' value='0'>"
        "<span id='frame-value' class='mono'>0</span></label>"
        "</div>"
        "<p class='meta' id='clip-note'>&nbsp;</p>"
        "</div></div>"
        f"<script>window.NEVO_CLIPS = {json.dumps(clips)};</script>"
    )


def _corpus_section(corpora: list[Path], assets: Path, frames_shown: int,
                    views_shown: int) -> str:
    from PIL import Image

    if not corpora:
        return "<p class='none'>No prepared corpus found.</p>"
    blocks = []
    for corpus in corpora:
        with open(corpus / "nevo_corpus.json") as handle:
            manifest = json.load(handle)
        # Two corpus roots can hold the same object (the prepared multi-view
        # set and a mesh re-render of it), so the asset names have to carry the
        # root or one silently overwrites the other's thumbnails.
        name = manifest["object"]
        slug = f"{corpus.parent.name}_{name}"
        available = sorted(
            int(p.name) for p in (corpus / "image").iterdir() if p.name.isdigit()
        )
        picked = _pick(available, frames_shown)
        views = _pick(list(range(len(manifest["cameras"]))), views_shown)
        rows = []
        for frame in picked:
            cells = []
            for view in views:
                image = corpus / "image" / str(frame) / ("img_%04d.png" % view)
                mask = corpus / "mask" / str(frame) / ("img_%04d.png" % view)
                if not image.is_file():
                    continue
                rgb = _save(Image.open(image), assets / f"{slug}_f{frame}_v{view}.jpg",
                            THUMBNAIL_WIDTH)
                alpha = _save(Image.open(mask), assets / f"{slug}_f{frame}_v{view}_m.jpg",
                              THUMBNAIL_WIDTH)
                cells.append(
                    f"<figure class='swap' data-a='{rgb}' data-b='{alpha}'>"
                    f"<img src='{rgb}' loading='lazy'>"
                    f"<figcaption>view {view}</figcaption></figure>"
                )
            rows.append(f"<h4>frame {frame}</h4><div class='strip'>{''.join(cells)}</div>")
        crop = manifest.get("crop_windows")
        detail = (
            f"crop {crop[0][2]}x{crop[0][3]} of {manifest['source_size'][0]}x"
            f"{manifest['source_size'][1]}"
            if crop
            else f"{manifest.get('azimuths', '?')} azimuths x "
                 f"{len(manifest.get('elevations', []))} elevations"
        )
        total_views = len(manifest["cameras"])
        subset = "" if len(views) == total_views else (
            f" &middot; showing {len(views)} of {total_views} views"
        )
        blocks.append(
            f"<section class='object'><h3>{html.escape(name)} "
            f"<span class='meta'>({html.escape(corpus.parent.name)})</span></h3>"
            f"<p class='meta'>{manifest['source']} &middot; {total_views} views "
            f"&middot; {manifest['frames']} frames &middot; {detail} &rarr; "
            f"{manifest['width']}x{manifest['height']} &middot; foreground "
            f"{(manifest['mean_foreground_fraction'] or 0) * 100:.1f}%{subset}</p>"
            f"{''.join(rows)}</section>"
        )
    return (
        "<p class='hint'>Hover a thumbnail to swap between the render and its matte. "
        "The matte should hug the subject with no halo and no holes.</p>"
        + "".join(blocks)
    )


def _reload_and_filter_section(runs: list[Path], assets: Path, args) -> tuple[str, str]:
    from nevo.blocks import BlockGrid
    from nevo.filtering import preview
    from nevo.importance import ImportanceConfig, ImportanceScorer
    from nevo.cameras import look_at_c2w
    from nevo.render import psnr, render_view, training_view
    from nevo.sequence import ReRFSequence

    reload_blocks = []
    filter_blocks = []
    for config_path in runs:
        try:
            sequence = ReRFSequence(config_path)
            trained = sequence.available_frames()
        except Exception as error:  # a half-written run should not sink the page
            reload_blocks.append(
                f"<section class='object'><h3>{html.escape(config_path.stem)}</h3>"
                f"<p class='none'>could not open: {html.escape(str(error))}</p></section>"
            )
            continue
        if not trained:
            continue
        name = sequence.cfg.expname
        for frame_index in trained[: args.frames_checked]:
            frame = sequence.frame(frame_index)
            kind = "I" if frame.is_key_frame else "P"
            camera, truth = training_view(sequence, frame_index, args.view)
            rendered = render_view(sequence, frame, camera)
            score = psnr(rendered, truth)
            left = _save(rendered, assets / f"{name}_reload_{frame_index}_r.jpg", VIEW_WIDTH)
            right = _save(truth, assets / f"{name}_reload_{frame_index}_t.jpg", VIEW_WIDTH)
            reload_blocks.append(
                f"<section class='object'><h3>{html.escape(name)} &middot; frame "
                f"{frame_index} ({kind})</h3>"
                f"<p class='meta'>rendered from the reloaded checkpoint vs. the training "
                f"image &middot; <b>{score:.2f} dB</b></p>"
                f"<div class='strip'>"
                f"<figure><img src='{left}'><figcaption>reloaded</figcaption></figure>"
                f"<figure><img src='{right}'><figcaption>ground truth</figcaption></figure>"
                f"</div></section>"
            )

            if frame_index != trained[0]:
                continue

            # A training view says nothing about reconstruction quality -- the
            # model was fit to it. Render a viewpoint the rig never saw, halfway
            # between two of its cameras, so the cost of a sparse rig (this
            # corpus has 8 views on one horizontal ring) is visible rather than
            # implied.
            novel = _between_rig_cameras(sequence, camera, look_at_c2w)
            if novel is not None:
                path = _save(
                    render_view(sequence, frame, novel),
                    assets / f"{name}_novel.jpg",
                    VIEW_WIDTH,
                )
                reload_blocks.append(
                    f"<section class='object'><h3>{html.escape(name)} &middot; novel view"
                    f"</h3><p class='meta'>a viewpoint midway between two rig cameras, "
                    f"which the model never saw. Floaters and smeared geometry show up "
                    f"here, not in the training view above.</p>"
                    f"<div class='strip'><figure><img src='{path}'>"
                    f"<figcaption>novel view</figcaption></figure></div></section>"
                )
            scorer = ImportanceScorer(
                sequence, frame, ImportanceConfig(block_size=args.block_size)
            )
            scores = scorer.score(camera)
            grid = BlockGrid(frame.grid_shape, args.block_size)
            cards = []
            for threshold in args.thresholds:
                result = preview(
                    sequence, frame, camera, scores, grid, threshold, scorer.occupancy
                )
                full = _save(result.full, assets / f"{name}_filter_full.jpg", VIEW_WIDTH)
                filtered = _save(
                    result.filtered,
                    assets / f"{name}_filter_{threshold}.jpg",
                    VIEW_WIDTH,
                )
                # The difference is invisible at 1x when the filter is working,
                # which is the point -- amplify it so it can be judged.
                amplified = _save(
                    np.clip(result.difference * args.difference_gain, 0.0, 1.0),
                    assets / f"{name}_filter_{threshold}_d.jpg",
                    VIEW_WIDTH,
                )
                cards.append(
                    f"<div class='card'><p class='meta'>threshold <b>{threshold}</b> "
                    f"&middot; dropped <b>{result.dropped_fraction * 100:.1f}%</b> of "
                    f"{result.total_blocks} non-empty blocks &middot; "
                    f"SSIM <b>{result.ssim:.4f}</b> &middot; {result.psnr:.2f} dB</p>"
                    f"<div class='strip'>"
                    f"<figure><img src='{full}'><figcaption>all voxels</figcaption></figure>"
                    f"<figure><img src='{filtered}'><figcaption>filtered</figcaption></figure>"
                    f"<figure><img src='{amplified}'>"
                    f"<figcaption>difference x{args.difference_gain}</figcaption></figure>"
                    f"</div></div>"
                )
            filter_blocks.append(
                f"<section class='object'><h3>{html.escape(name)} &middot; frame "
                f"{frame_index}, {args.block_size}<sup>3</sup> blocks</h3>"
                + "".join(cards)
                + "</section>"
            )
    reload_html = "".join(reload_blocks) or "<p class='none'>No trained sequence found.</p>"
    filter_html = "".join(filter_blocks) or "<p class='none'>No trained sequence found.</p>"
    return reload_html, filter_html


def _sweep_section(results: list[Path]) -> str:
    """Table of the threshold sweep: what each threshold drops, and what it costs."""
    rows = []
    for path in sorted(results):
        with open(path) as handle:
            report = json.load(handle)
        for row in report["rows"]:
            verdict = (
                "<td class='ok'>lossless</td>" if row["visually_lossless"]
                else "<td class='bad'>degraded</td>"
            )
            rows.append(
                "<tr>"
                f"<td>{html.escape(report['object'])}</td>"
                f"<td>{report['block_size']}<sup>3</sup></td>"
                f"<td>{row['threshold']}</td>"
                f"<td>{row['dropped_mean'] * 100:.1f}%</td>"
                f"<td>{row['ssim_mean']:.4f}</td>"
                f"<td>{row['ssim_min']:.4f}</td>"
                f"<td>{row['psnr_mean']:.1f}</td>"
                f"{verdict}</tr>"
            )
    if not rows:
        return "<p class='none'>No threshold sweep found.</p>"
    return (
        "<p class='hint'>Each row filters with that viewport's own scores and renders, "
        "against the same viewport rendered from the whole grid. <b>lossless</b> means the "
        "<em>worst</em> viewport still cleared SSIM 0.98, the bar the paper cites. The paper "
        "fits its threshold to that bar rather than fixing it at 0.025, so the row that "
        "matters is the last lossless one.</p>"
        "<table><thead><tr><th>object</th><th>block</th><th>threshold</th><th>dropped</th>"
        "<th>SSIM</th><th>worst SSIM</th><th>PSNR</th><th></th></tr></thead><tbody>"
        + "".join(rows)
        + "</tbody></table>"
    )


def _rd_per_frame(directory: Path) -> dict:
    """``{(threshold, frame): (psnr, ssim)}`` from the sweep's per-row CSV."""
    path = directory / "results.csv"
    if not path.exists():
        return {}
    scores = {}
    with open(path) as handle:
        for row in csv.DictReader(handle):
            key = (float(row["threshold"]), int(row["frame"]))
            scores[key] = (float(row["psnr"]), float(row["ssim"]))
    return scores


def _rd_strip(directory: Path, summary: dict, assets: Path, height: int = 340) -> str:
    """Two strips: how well the frame reconstructs, then what filtering costs it.

    Both are cropped to ``scoring_box_tlbr`` -- the same subject bounding box the
    PSNR/SSIM/LPIPS in the table were computed over. At full frame the subject is
    5-9% of the pixels, so an uncropped thumbnail shows neither the artefacts nor
    the region the numbers describe.

    The first strip pairs frame 0 with frame 1 deliberately. Frame 0 is the
    I-frame; frame 1 is the first P-frame, which codes a residual over a
    motion-compensated predecessor and so fails differently -- on a held-out
    camera, much harder.
    """
    from PIL import Image

    renders = directory / "renders"
    if not renders.is_dir():
        return ""
    scores = _rd_per_frame(directory)
    box = summary.get("scoring_box_tlbr")

    def cell(path: Path, key: str, label: str, frame: int, threshold=None) -> str:
        image = Image.open(path)
        if box:
            top, left, bottom, right = box
            pad = 12
            image = image.crop((max(left - pad, 0), max(top - pad, 0),
                                min(right + pad, image.width),
                                min(bottom + pad, image.height)))
        width = max(1, round(image.width * height / image.height))
        # The filename comes from ``key``, never from ``label``: a label carries
        # spaces, "%" and "=", and a "%" in a URL path is an escape prefix.
        slug = re.sub(r"[^a-z0-9]+", "_", key.lower()).strip("_")
        src = _save(image, assets / f"{summary['object']}_rd_f{frame}_{slug}.jpg", width)
        if threshold is not None and (threshold, frame) in scores:
            psnr, ssim = scores[(threshold, frame)]
            label = f"{label} · {psnr:.1f} dB / {ssim:.3f}"
        return (f"<figure><img src='{src}' alt='{html.escape(label)}'>"
                f"<figcaption>{html.escape(label)}</figcaption></figure>")

    frames = (summary.get("frames") or [])[:2]
    thresholds = [row["threshold"] for row in summary["per_threshold"]]
    base = thresholds[0] if thresholds else 0.0
    out = []

    cells = []
    for frame in frames:
        reference = renders / f"reference_f{frame:03d}.png"
        rendered = renders / f"t{base}_f{frame:03d}.png"
        kind = "I-frame" if frame == 0 else "P-frame"
        if reference.exists():
            cells.append(cell(reference, "captured", f"captured · f{frame} ({kind})", frame))
        if rendered.exists():
            cells.append(
                cell(rendered, "rerf", f"ReRF · f{frame} ({kind})", frame, base)
            )
    if cells:
        out.append("<h4>Reconstruction &mdash; unfiltered ReRF against the camera</h4>"
                   "<div class='strip'>" + "".join(cells) + "</div>")

    frame = frames[0] if frames else 0
    dropped = {row["threshold"]: 1.0 - row["kept_fraction"] for row in summary["per_threshold"]}
    cells = [
        cell(renders / f"t{threshold}_f{frame:03d}.png",
             f"t{threshold}",
             f"t={threshold} · {dropped[threshold] * 100:.0f}% dropped",
             frame, threshold)
        for threshold in thresholds
        if (renders / f"t{threshold}_f{frame:03d}.png").exists()
    ]
    if cells:
        out.append(f"<h4>Filtering &mdash; frame {frame} at each threshold</h4>"
                   "<div class='strip'>" + "".join(cells) + "</div>")
    return "".join(out)


def _rd_section(results_roots, assets: Path) -> str:
    """Rate-distortion: real ReRF bytes on one axis, delivered quality on the other."""
    summaries = []
    for root in results_roots:
        for path in sorted(Path(root).expanduser().glob("*/summary.json")):
            with open(path) as handle:
                summary = json.load(handle)
            if summary.get("per_threshold"):
                summaries.append((path.parent, summary))
    if not summaries:
        return "<p class='none'>No rate-distortion sweep found.</p>"

    blocks = []
    for directory, summary in summaries:
        held_out = summary.get("held_out")
        badge = (
            "<span class='ok'>held out</span>" if held_out
            else "<span class='bad'>in the training set</span>"
        )
        base = summary["per_threshold"][0]["bytes_per_frame"]
        rows = []
        for row in summary["per_threshold"]:
            saved = 1.0 - row["bytes_per_frame"] / base if base else 0.0
            rows.append(
                "<tr>"
                f"<td>{row['threshold']}</td>"
                f"<td>{row['kept_fraction'] * 100:.1f}%</td>"
                f"<td>{row['bytes_per_frame'] / 1000:.1f}</td>"
                f"<td>{saved * 100:.1f}%</td>"
                f"<td>{row['mbps_at_30fps']:.1f}</td>"
                f"<td>{row['psnr']:.2f}</td>"
                f"<td>{row['ssim']:.4f}</td>"
                f"<td>{row['lpips']:.4f}</td></tr>"
            )
        note = ""
        if not held_out:
            note = ("<p class='meta bad'>Camera "
                    f"{summary['eval_view']} was trained on, so these quality numbers "
                    "measure how well the NeRF memorised its own training image. The bytes "
                    "are unaffected.</p>")
        blocks.append(
            f"<h3>{html.escape(summary['object'])} &middot; camera "
            f"{summary['eval_view']}, {badge}</h3>"
            f"<p class='meta'>{len(summary['frames'])} frames &middot; "
            f"{summary['block_size']}<sup>3</sup> blocks &middot; encoder quality "
            f"{summary['quality']} &middot; startup "
            f"{summary['startup_bytes']['total_bytes'] / 1000:.0f} kB (the colour MLP, "
            "sent once, not per frame)</p>"
            + note +
            "<table><thead><tr><th>threshold</th><th>blocks kept</th><th>kB/frame</th>"
            "<th>bytes saved</th><th>Mbps @30fps</th><th>PSNR</th><th>SSIM</th>"
            "<th>LPIPS</th></tr></thead><tbody>"
            + "".join(rows) + "</tbody></table>"
            + _rd_strip(directory, summary, assets)
        )
    return (
        "<p class='hint'>Bytes are ReRF's own encoder run over the retained blocks, plus the "
        "block mask and the P-frames' motion vectors &mdash; measured, not estimated. Note "
        "that dropping blocks does not save bytes proportionally: this encoder is sub-linear "
        "in block count, so the <b>blocks kept</b> and <b>bytes saved</b> columns disagree, "
        "and the second is the one a network sees.</p>"
        "<p class='hint'>Quality here is scored against the <em>captured camera</em> on the "
        "subject's bounding box, which is stricter than section 4's filtered-vs-unfiltered "
        "comparison: errors the NeRF already had no longer cancel out, and the identical "
        "white background no longer dilutes the average.</p>"
        + "".join(blocks)
    )


def _subject_box(images, pad: int = 24):
    """Union bounding box of the non-white pixels across ``images``."""
    box = None
    for image in images:
        array = np.asarray(image.convert("RGB"))
        rows = np.where((array < 250).any(axis=(1, 2)))[0]
        cols = np.where((array < 250).any(axis=(0, 2)))[0]
        if not rows.size or not cols.size:
            continue
        here = (rows[0], cols[0], rows[-1], cols[-1])
        box = here if box is None else (
            min(box[0], here[0]), min(box[1], here[1]),
            max(box[2], here[2]), max(box[3], here[3]),
        )
    if box is None:
        return None
    height, width = np.asarray(images[0]).shape[:2]
    return (max(int(box[0]) - pad, 0), max(int(box[1]) - pad, 0),
            min(int(box[2]) + pad, height), min(int(box[3]) + pad, width))


def _rerf_section(runs_root, assets: Path, height: int = 300,
                  frames_shown: int = 6) -> str:
    """ReRF's own bitstream, decoded and rendered by its own ``rerf_render.py``.

    This is the baseline NeVo is measured *against*, produced entirely by
    upstream: ``codec/compress.py`` writes the bitstream, ``rerf_render.py``
    decodes it and renders a 360 orbit. Nothing in ``nevo/`` participates, which
    is the point -- these bytes and these pixels are ReRF's, not ours.
    """
    from PIL import Image

    blocks, rows = [], []
    for root in runs_root:
        for run in sorted(Path(root).expanduser().glob("*/rerf")):
            directory = run.parent
            headers = sorted(run.glob("header_*.json"))
            if not headers:
                continue
            frames = len(headers)
            startup = sum(
                (run / name).stat().st_size
                for name in ("rgb_net.tar", "model_kwargs.json")
                if (run / name).exists()
            )
            stream = sum(
                path.stat().st_size for path in run.iterdir()
                if path.is_file() and path.name not in ("rgb_net.tar", "model_kwargs.json")
            )
            checkpoints = sum(p.stat().st_size for p in directory.glob("*.tar"))
            per_frame = stream / frames
            rows.append(
                "<tr>"
                f"<td>{html.escape(directory.name)}</td>"
                f"<td>{frames}</td>"
                f"<td>{(stream + startup) / 1e6:.1f} MB</td>"
                f"<td>{per_frame / 1000:.1f}</td>"
                f"<td>{per_frame * 8 * 30 / 1e6:.1f}</td>"
                f"<td>{checkpoints / 2 ** 30:.1f} GB</td>"
                f"<td>{checkpoints / (stream + startup):.0f}&times;</td></tr>"
            )

            rendered = sorted(
                path for directory_360 in directory.glob("render_360_rerf_*")
                for path in directory_360.glob("*.jpg")
                if not path.stem.endswith("_depth")
            )
            if not rendered:
                continue
            picked = _pick(rendered, min(frames_shown, len(rendered)))
            opened = [Image.open(path) for path in picked]
            box = _subject_box(opened)
            figures = []
            for path, image in zip(picked, opened):
                if box:
                    top, left, bottom, right = box
                    image = image.crop((left, top, right, bottom))
                width = max(1, round(image.width * height / image.height))
                src = _save(image, assets / f"rerf_{directory.name}_{path.stem}.jpg", width)
                figures.append(
                    f"<figure><img src='{src}' alt='frame {path.stem}'>"
                    f"<figcaption>frame {path.stem}</figcaption></figure>"
                )
            depth = next(iter(sorted(
                path for directory_360 in directory.glob("render_360_rerf_*")
                for path in directory_360.glob("*_depth.jpg")
            )), None)
            if depth is not None:
                image = Image.open(depth)
                if box:
                    top, left, bottom, right = box
                    image = image.crop((left, top, right, bottom))
                width = max(1, round(image.width * height / image.height))
                src = _save(image, assets / f"rerf_{directory.name}_depth.jpg", width)
                figures.append(
                    f"<figure><img src='{src}' alt='depth'>"
                    f"<figcaption>depth (frame {depth.stem.split('_')[0]})</figcaption>"
                    "</figure>"
                )
            blocks.append(
                f"<h3>{html.escape(directory.name)}</h3>"
                f"<p class='meta'>{frames} frames &middot; "
                f"{(stream + startup) / 1e6:.1f} MB of bitstream &middot; "
                f"{per_frame * 8 * 30 / 1e6:.1f} Mbps at 30 fps</p>"
                "<div class='strip'>" + "".join(figures) + "</div>"
            )

    if not rows:
        return "<p class='none'>No ReRF bitstream found. Run codec/compress.py.</p>"
    return (
        "<p class='hint'>Produced entirely by upstream ReRF: "
        "<code>codec/compress.py</code> writes the bitstream (PCA on, "
        "<code>--pca_chs 7,13</code>, quality 99) and <code>rerf_render.py</code> "
        "decodes it and renders a 360&deg; orbit. Nothing in <code>nevo/</code> is "
        "involved, so these are ReRF's own bytes and pixels &mdash; the baseline "
        "NeVo is measured against. <b>150&ndash;234 Mbps</b> matches the paper's "
        "\"150+ Mbps\" for ReRF.</p>"
        "<p class='hint'>The ratio column is the point of the exercise: the "
        "bitstream is ~700&ndash;1000&times; smaller than the training checkpoints "
        "it came from, because the checkpoints are dense fp32 grids that are ~90% "
        "empty and carry no quantisation, DCT or entropy coding.</p>"
        "<table><thead><tr><th>run</th><th>frames</th><th>bitstream</th>"
        "<th>kB/frame</th><th>Mbps @30fps</th><th>checkpoints</th><th>ratio</th>"
        "</tr></thead><tbody>" + "".join(rows) + "</tbody></table>"
        "<p class='hint'>Framing is <code>rerf_render.py</code>'s own synthesised "
        "orbit at 1920&times;1080, cropped to the subject here for legibility. It is "
        "not the corpus camera, so these pixels are not comparable frame-for-frame "
        "with the sections above.</p>"
        + "".join(blocks)
    )


def _between_rig_cameras(sequence, camera, look_at_c2w):
    """A camera halfway between two of the corpus rig's, at the same radius."""
    try:
        with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
            manifest = json.load(handle)
    except OSError:
        return None
    positions = np.asarray(
        [np.asarray(entry["c2w_normalised"])[:3, 3] for entry in manifest["cameras"]]
    )
    if len(positions) < 2:
        return None
    centre = (np.asarray(manifest["xyz_min"]) + np.asarray(manifest["xyz_max"])) * 0.5
    radius = float(np.linalg.norm(positions[0] - centre))
    midpoint = (positions[0] + positions[1]) * 0.5
    offset = midpoint - centre
    eye = centre + offset / np.linalg.norm(offset) * radius
    return dataclasses.replace(
        camera, c2w=look_at_c2w(eye, centre, np.asarray((0.0, 1.0, 0.0)))
    )


def _cdf_section(results: list[Path], assets: Path) -> str:
    rows = []
    plots = []
    for path in sorted(results):
        with open(path) as handle:
            payload = json.load(handle)
        report = payload["report"]
        summary = [s for s in report["summaries"] if s["pooling"] == "per-viewport"][0]
        below = summary["fraction_below"]
        # `.get` on the derived stats: result files written before a diagnostic
        # existed should still show up in the table rather than sink the page.
        never_hit = summary.get("never_hit_fraction")
        rows.append(
            "<tr>"
            f"<td>{html.escape(report['object'])}</td>"
            f"<td>{summary['block_size']}<sup>3</sup></td>"
            f"<td>{summary['assignment']}</td>"
            f"<td>{summary['frames']}</td>"
            f"<td>{summary['viewports_per_frame']}</td>"
            f"<td>{summary['occupied_blocks']:,}</td>"
            f"<td>{below['0.01'] * 100:.1f}%</td>"
            f"<td class='hi'>{below['0.025'] * 100:.1f}%</td>"
            f"<td>{below['0.05'] * 100:.1f}%</td>"
            f"<td>{never_hit * 100:.1f}%</td>" if never_hit is not None else "<td>&mdash;</td>"
            f"<td>{summary['quantiles']['0.5']:.4f}</td>"
            "</tr>"
        )
        image = path.with_suffix(".png")
        if image.is_file():
            target = assets / image.name
            shutil.copyfile(image, target)
            plots.append(
                f"<figure class='plot'><img src='assets/{target.name}'>"
                f"<figcaption>{html.escape(image.stem)}</figcaption></figure>"
            )
    if not rows:
        return "<p class='none'>No CDF results found.</p>"
    return (
        "<p class='hint'>Per-viewport pooling: every (voxel, viewport) pair is one sample. "
        "The highlighted column is the paper's 0.025 threshold.</p>"
        "<table><thead><tr><th>object</th><th>block</th><th>assignment</th><th>frames</th>"
        "<th>viewports</th><th>non-empty</th><th>&lt;0.01</th><th>&lt;0.025</th>"
        "<th>&lt;0.05</th><th>never hit</th><th>median</th></tr></thead><tbody>"
        + "".join(rows)
        + "</tbody></table><div class='strip wrap'>"
        + "".join(plots)
        + "</div>"
    )


STYLE = """
:root { color-scheme: light dark; --line: #8883; --hi: #f0b90020; }
* { box-sizing: border-box; }
body { margin: 0 auto; padding: 2rem 1.5rem 6rem; max-width: 1500px;
       font: 15px/1.55 ui-sans-serif, system-ui, -apple-system, sans-serif; }
h1 { font-size: 1.5rem; margin: 0 0 .2rem; }
h2 { font-size: 1.15rem; margin: 2.5rem 0 .6rem; padding-bottom: .3rem;
     border-bottom: 1px solid var(--line); }
h3 { font-size: 1rem; margin: 1.4rem 0 .3rem; }
h4 { font-size: .85rem; font-weight: 500; opacity: .65; margin: .8rem 0 .3rem; }
p.meta, p.hint, p.none { font-size: .85rem; opacity: .75; margin: .2rem 0 .6rem; }
p.none { font-style: italic; }
.strip { display: flex; gap: .5rem; overflow-x: auto; padding-bottom: .4rem; }
.strip.wrap { flex-wrap: wrap; overflow: visible; }
figure { margin: 0; flex: 0 0 auto; }
figure img { display: block; border: 1px solid var(--line); border-radius: 4px;
             background: #7772; }
figcaption { font-size: .75rem; opacity: .6; text-align: center; margin-top: .15rem; }
.card { border: 1px solid var(--line); border-radius: 6px; padding: .7rem .8rem;
        margin-bottom: .7rem; }
table { border-collapse: collapse; font-size: .85rem; margin: .5rem 0 1.2rem; }
th, td { border: 1px solid var(--line); padding: .25rem .55rem; text-align: right; }
th:first-child, td:first-child, td:nth-child(3) { text-align: left; }
thead th { font-weight: 600; opacity: .8; }
td.hi { background: var(--hi); font-weight: 600; }
td.ok { color: #1a7f37; text-align: left; }
td.bad { color: #b3261e; text-align: left; }
p.meta.bad { color: #b3261e; opacity: 1; font-weight: 500; }
nav a { margin-right: 1rem; font-size: .85rem; }
.mono { font-variant-numeric: tabular-nums; font-family: ui-monospace, monospace;
        font-size: .8rem; opacity: .75; }
.player { border: 1px solid var(--line); border-radius: 8px; overflow: hidden;
          max-width: 560px; }
.stage { position: relative; background: #7771; display: flex;
         justify-content: center; align-items: center; min-height: 200px; }
.stage img { display: block; width: 100%; height: auto; }
.loading { position: absolute; font-size: .8rem; opacity: .6; }
.loading.hidden { display: none; }
.controls { padding: .6rem .8rem .8rem; border-top: 1px solid var(--line); }
.controls .row { display: flex; gap: .9rem; align-items: center; flex-wrap: wrap;
                 margin-bottom: .45rem; font-size: .82rem; }
.controls .row:last-of-type { margin-bottom: 0; }
.controls label { display: flex; gap: .35rem; align-items: center; }
.controls label.grow { flex: 1 1 240px; }
.controls label.grow input[type=range] { flex: 1; }
button { font: inherit; font-size: .82rem; padding: .25rem .7rem; cursor: pointer;
         border: 1px solid var(--line); border-radius: 5px; background: #7771;
         color: inherit; }
button.primary { min-width: 5.6rem; }
button.on { background: #4a90d922; border-color: #4a90d9; font-weight: 600; }
"""

PLAYER_SCRIPT = """
(function () {
  const clips = window.NEVO_CLIPS || [];
  if (!clips.length) return;

  const image = document.getElementById('stage-image');
  const loading = document.getElementById('stage-loading');
  const clipPicker = document.getElementById('clip');
  const viewPicker = document.getElementById('view');
  const frameSlider = document.getElementById('frame');
  const frameLabel = document.getElementById('frame-value');
  const playButton = document.getElementById('play');
  const fpsSlider = document.getElementById('fps');
  const fpsLabel = document.getElementById('fps-value');
  const matte = document.getElementById('matte');
  const note = document.getElementById('clip-note');

  let clip = clips[0];
  let view = clip.views[0];
  let frame = 0;
  let playing = false;
  let lastTick = 0;

  const source = (index) =>
    clip.path + '/' + (matte.checked && !clip.no_matte ? 'matte' : 'rgb') + '/v' +
    String(view).padStart(2, '0') + '_f' +
    String(clip.frames[index]).padStart(3, '0') + '.jpg';

  // Decode the whole clip before playing. At 10 fps a browser fetching each
  // frame on demand shows a blank stage for the first pass, which is exactly
  // when you are looking for a hitch in the input.
  function preload() {
    loading.classList.remove('hidden');
    let done = 0;
    const total = clip.frames.length;
    return Promise.all(clip.frames.map((_, index) => new Promise((resolve) => {
      const probe = new Image();
      const finish = () => {
        done += 1;
        loading.textContent = 'loading ' + Math.round((done / total) * 100) + '%';
        resolve();
      };
      probe.onload = finish;
      probe.onerror = finish;
      probe.src = source(index);
    }))).then(function () { loading.classList.add('hidden'); });
  }

  function paint() {
    image.src = source(frame);
    frameLabel.textContent = clip.frames[frame];
  }

  function load(next) {
    clip = next;
    view = clip.views.indexOf(view) >= 0 ? view : clip.views[0];
    viewPicker.textContent = '';
    for (const candidate of clip.views) {
      const option = document.createElement('option');
      option.value = String(candidate);
      option.textContent = 'camera ' + candidate;
      viewPicker.appendChild(option);
    }
    viewPicker.value = String(view);
    // A rendered clip is one camera with no matte; hiding the controls that do
    // not apply is clearer than leaving them to 404.
    viewPicker.parentElement.style.display = clip.views.length > 1 ? '' : 'none';
    matte.parentElement.style.display = clip.no_matte ? 'none' : '';
    frame = 0;
    frameSlider.max = String(clip.frames.length - 1);
    frameSlider.value = '0';
    note.textContent = clip.source + ' \\u2014 ' + clip.frames.length +
      ' frames, ' + clip.size[0] + 'x' + clip.size[1] + ' as prepared';
    paint();
    preload();
  }

  function tick(now) {
    if (!playing) return;
    if (now - lastTick >= 1000 / Number(fpsSlider.value)) {
      lastTick = now;
      frame = (frame + 1) % clip.frames.length;
      frameSlider.value = String(frame);
      paint();
    }
    requestAnimationFrame(tick);
  }

  playButton.addEventListener('click', function () {
    playing = !playing;
    playButton.innerHTML = playing ? '&#10073;&#10073;&nbsp;pause' : '&#9654;&nbsp;play';
    playButton.className = playing ? 'primary on' : 'primary';
    if (playing) { lastTick = 0; requestAnimationFrame(tick); }
  });
  frameSlider.addEventListener('input', function () {
    frame = Number(frameSlider.value); paint();
  });
  fpsSlider.addEventListener('input', function () { fpsLabel.textContent = fpsSlider.value; });
  matte.addEventListener('change', function () { paint(); preload(); });
  viewPicker.addEventListener('change', function () {
    view = Number(viewPicker.value); paint(); preload();
  });
  clipPicker.addEventListener('change', function () {
    const found = clips.find(function (item) { return item.name === clipPicker.value; });
    if (found) load(found);
  });

  load(clip);
})();
"""

SCRIPT = """
(function () {
  const clips = window.NEVO_CLIPS || [];
  if (!clips.length) return;

  const image = document.getElementById('stage-image');
  const loading = document.getElementById('stage-loading');
  const clipPicker = document.getElementById('clip');
  const frameSlider = document.getElementById('frame');
  const azimuthSlider = document.getElementById('azimuth');
  const frameLabel = document.getElementById('frame-value');
  const azimuthLabel = document.getElementById('azimuth-value');
  const conditionRow = document.getElementById('conditions');
  const conditionNote = document.getElementById('condition-note');
  const playButton = document.getElementById('play');
  const fpsSlider = document.getElementById('fps');
  const fpsLabel = document.getElementById('fps-value');
  const autoOrbit = document.getElementById('auto-orbit');

  let clip = clips[0];
  let condition = clip.conditions[0];
  let frame = 0;
  let azimuth = 0;
  let playing = false;
  let lastTick = 0;

  const source = (aClip, aCondition, frameIndex, azimuthIndex) =>
    `${aClip.path}/${aCondition}/f${String(aClip.frames[frameIndex]).padStart(3, '0')}` +
    `_a${String(azimuthIndex).padStart(2, '0')}.jpg`;

  // Decode every cell up front. The grid is a few hundred small JPEGs, and a
  // player that stutters on the first pass through a viewpoint is useless for
  // judging whether filtering is visible.
  function preload(aClip) {
    loading.classList.remove('hidden');
    const urls = [];
    for (const aCondition of aClip.conditions)
      for (let f = 0; f < aClip.frames.length; f += 1)
        for (let a = 0; a < aClip.azimuths; a += 1)
          urls.push(source(aClip, aCondition, f, a));
    let done = 0;
    return Promise.all(urls.map((url) => new Promise((resolve) => {
      const probe = new Image();
      const finish = () => {
        done += 1;
        loading.textContent = `loading ${Math.round((done / urls.length) * 100)}%`;
        resolve();
      };
      probe.onload = finish;
      probe.onerror = finish;
      probe.src = url;
    }))).then(() => loading.classList.add('hidden'));
  }

  function paint() {
    image.src = source(clip, condition, frame, azimuth);
    frameLabel.textContent = clip.frames[frame];
    azimuthLabel.textContent = `${Math.round((azimuth / clip.azimuths) * 360)}\u00b0`;
  }

  function describe() {
    if (condition === 'full') {
      conditionNote.textContent =
        `every non-empty feature voxel delivered \u2014 ${clip.block_size}\u00b3 blocks`;
      return;
    }
    const stat = (clip.stats || {})[condition];
    conditionNote.textContent = stat
      ? `importance \u2265 ${condition}: ${(stat.dropped_mean * 100).toFixed(1)}% of ` +
        `non-empty ${clip.block_size}\u00b3 blocks dropped, ` +
        `${stat.psnr_mean.toFixed(1)} dB against the full render`
      : `importance \u2265 ${condition}`;
  }

  function buildConditions() {
    conditionRow.textContent = '';
    for (const name of clip.conditions) {
      const button = document.createElement('button');
      button.textContent = name === 'full' ? 'all voxels' : `filtered \u2265 ${name}`;
      button.className = name === condition ? 'on' : '';
      button.addEventListener('click', () => {
        condition = name;
        for (const other of conditionRow.children) other.className = '';
        button.className = 'on';
        describe();
        paint();
      });
      conditionRow.appendChild(button);
    }
  }

  function load(aClip) {
    clip = aClip;
    condition = clip.conditions.includes(condition) ? condition : clip.conditions[0];
    frame = 0;
    azimuth = 0;
    frameSlider.max = String(clip.frames.length - 1);
    frameSlider.value = '0';
    azimuthSlider.max = String(clip.azimuths - 1);
    azimuthSlider.value = '0';
    buildConditions();
    describe();
    paint();
    preload(clip);
  }

  function tick(now) {
    if (!playing) return;
    const interval = 1000 / Number(fpsSlider.value);
    if (now - lastTick >= interval) {
      lastTick = now;
      frame = (frame + 1) % clip.frames.length;
      frameSlider.value = String(frame);
      if (autoOrbit.checked) {
        azimuth = (azimuth + 1) % clip.azimuths;
        azimuthSlider.value = String(azimuth);
      }
      paint();
    }
    requestAnimationFrame(tick);
  }

  playButton.addEventListener('click', () => {
    playing = !playing;
    playButton.innerHTML = playing ? '&#10073;&#10073;&nbsp;pause' : '&#9654;&nbsp;play';
    playButton.className = playing ? 'primary on' : 'primary';
    if (playing) { lastTick = 0; requestAnimationFrame(tick); }
  });
  frameSlider.addEventListener('input', () => { frame = Number(frameSlider.value); paint(); });
  azimuthSlider.addEventListener('input', () => {
    azimuth = Number(azimuthSlider.value); paint();
  });
  fpsSlider.addEventListener('input', () => { fpsLabel.textContent = fpsSlider.value; });
  clipPicker.addEventListener('change', () => {
    const found = clips.find((item) => item.name === clipPicker.value);
    if (found) load(found);
  });

  // Drag to orbit: one full sweep of the image width covers the whole ring.
  let dragging = false;
  let dragStartX = 0;
  let dragStartAzimuth = 0;
  const beginDrag = (x) => { dragging = true; dragStartX = x; dragStartAzimuth = azimuth; };
  const moveDrag = (x) => {
    if (!dragging) return;
    const span = image.clientWidth || 1;
    const steps = Math.round(((x - dragStartX) / span) * clip.azimuths);
    azimuth = ((dragStartAzimuth + steps) % clip.azimuths + clip.azimuths) % clip.azimuths;
    azimuthSlider.value = String(azimuth);
    paint();
  };
  image.addEventListener('pointerdown', (event) => {
    beginDrag(event.clientX);
    image.setPointerCapture(event.pointerId);
  });
  image.addEventListener('pointermove', (event) => moveDrag(event.clientX));
  image.addEventListener('pointerup', () => { dragging = false; });
  image.addEventListener('pointercancel', () => { dragging = false; });

  load(clip);
})();
"""

SCRIPT = """
for (const figure of document.querySelectorAll('figure.swap')) {
  const image = figure.querySelector('img');
  const caption = figure.querySelector('figcaption');
  const label = caption.textContent;
  figure.addEventListener('mouseenter', () => {
    image.src = figure.dataset.b; caption.textContent = label + ' (matte)';
  });
  figure.addEventListener('mouseleave', () => {
    image.src = figure.dataset.a; caption.textContent = label;
  });
}
"""


def build(args) -> Path:
    out_dir = Path(args.out).expanduser().resolve()
    assets = out_dir / "assets"
    if assets.exists() and args.clean:
        # Only the thumbnails. `player/` is built by a separate tool and is the
        # expensive artefact here (hundreds of renders); wiping it on every
        # report rebuild would be a trap.
        shutil.rmtree(assets)
    assets.mkdir(parents=True, exist_ok=True)

    corpora = sorted(
        path.parent
        for root in args.corpus_root
        for path in Path(root).expanduser().glob("*/nevo_corpus.json")
    )
    runs = sorted(Path(args.config_dir).expanduser().glob("*.py"))
    results = [
        Path(path)
        for root in args.results_root
        for path in glob.glob(str(Path(root).expanduser() / "*" / "importance_cdf_*.json"))
    ]
    sweeps = [
        Path(path)
        for root in args.results_root
        for path in glob.glob(str(Path(root).expanduser() / "*" / "filter_sweep_*.json"))
    ]

    print(f"corpora: {[c.name for c in corpora]}", flush=True)
    print(f"runs: {[r.stem for r in runs]}", flush=True)
    print(f"cdf results: {len(results)}, threshold sweeps: {len(sweeps)}", flush=True)

    output_clips = _output_clips(args.output_root, assets, VIEW_WIDTH)
    print(f"rendered clips: {[clip['name'] for clip in output_clips]}", flush=True)
    # No corpus clips: the player shows NeVo's output, and the prepared corpus
    # has its own section below.
    player_html = _player_section([], assets, args.player_views, args.player_width,
                                  output_clips)
    corpus_html = _corpus_section(corpora, assets, args.frames_shown, args.views_shown)
    if args.skip_render:
        reload_html = filter_html = "<p class='none'>Skipped (--skip-render).</p>"
    else:
        rerf_env.activate()
        try:
            reload_html, filter_html = _reload_and_filter_section(runs, assets, args)
        except Exception as error:
            # Rendering wants a GPU, and on this box the report is often built
            # while training is using both. Losing two sections is a much better
            # outcome than losing the page, especially since the other three
            # need no GPU at all.
            note = (
                f"<p class='none'>Could not render: {html.escape(type(error).__name__)}: "
                f"{html.escape(str(error).splitlines()[0])}<br>"
                f"Re-run once a GPU is free, or pass <code>--skip-render</code>.</p>"
            )
            print(f"render sections failed: {type(error).__name__}: {error}", flush=True)
            reload_html = filter_html = note
    cdf_html = _cdf_section(results, assets)
    sweep_html = _sweep_section(sweeps)
    rd_html = _rd_section(args.results_root, assets)
    rerf_html = _rerf_section(args.runs_root, assets)

    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>NeVo baseline &mdash; output check</title><style>{STYLE}</style></head><body>
<h1>NeVo baseline &mdash; output check</h1>
<p class="meta">generated {stamp} &middot; the paper's section 3.2 (neural visibility,
visibility-aware filtering) measured end to end, in bytes and in delivered quality.
Sections 3.3 (loss recovery) and 3.4 (reprojection) are not built.</p>
<nav><a href="#play">1. Player</a><a href="#corpus">2. Corpus</a><a href="#reload">3. Reload</a>
<a href="#filter">4. Filtering</a><a href="#cdf">5. Importance CDF</a><a href="#sweep">6. Threshold sweep</a>
<a href="#rd">7. Rate&ndash;distortion</a><a href="#rerf">8. ReRF bitstream</a></nav>

<h2 id="play">1. Player &mdash; NeVo's output</h2>
{player_html}

<h2 id="corpus">2. Corpus &mdash; what ReRF was trained on</h2>
{corpus_html}

<h2 id="reload">3. Reload &mdash; did step 1 rebuild the frame correctly?</h2>
<p class="hint">A correctly reassembled checkpoint lands near the PSNR the trainer
logged. A mis-wired one still renders, just not this scene.</p>
{reload_html}

<h2 id="filter">4. Filtering &mdash; what does dropping the low-importance voxels cost?</h2>
<p class="hint">Dropped blocks are written back the way ReRF's decoder fills a block
that never arrived (raw density &minus;4.1, zero features), not zeroed. The difference
image is amplified; at 1x it is invisible, which is the claim being checked.</p>
{filter_html}

<h2 id="cdf">5. Importance CDF</h2>
{cdf_html}

<h2 id="sweep">6. Threshold sweep &mdash; how far can filtering go?</h2>
{sweep_html}

<h2 id="rd">7. Rate&ndash;distortion &mdash; bytes against delivered quality</h2>
{rd_html}

<h2 id="rerf">8. ReRF itself &mdash; its own bitstream, decoded and rendered</h2>
{rerf_html}
<script>{PLAYER_SCRIPT}</script>
<script>{SCRIPT}</script></body></html>
"""
    index = out_dir / "index.html"
    index.write_text(page)
    print(f"wrote {index}", flush=True)
    return index


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--out", default="~/nevo_report")
    parser.add_argument("--corpus-root", nargs="+", default=["~/nevo_data_g"],
                        help="prepared corpora to show; pass several to compare")
    parser.add_argument("--config-dir",
                        default=str(MODULE_ROOT / "rerf/configs/nevo"))
    parser.add_argument("--results-root", nargs="+", default=["~/nevo_results"])
    parser.add_argument("--runs-root", nargs="+", default=["~/nevo_runs"],
                        help="trained runs; read for ReRF's own bitstream and renders")
    parser.add_argument("--output-root", nargs="+", default=["~/nevo_output"],
                        help="rendered reconstruction frames from render_frames.py")
    parser.add_argument("--frames-shown", type=int, default=2)
    parser.add_argument("--views-shown", type=int, default=6,
                        help="views per object in the contact sheet; 0 = all")
    parser.add_argument("--player-views", type=int, default=4,
                        help="cameras selectable in the player; 0 = all")
    parser.add_argument("--player-width", type=int, default=360)
    parser.add_argument("--frames-checked", type=int, default=2,
                        help="trained frames to reload-check per object")
    parser.add_argument("--view", type=int, default=0, help="which training view to render")
    parser.add_argument("--block-size", type=int, default=8)
    parser.add_argument("--thresholds", type=float, nargs="+", default=[0.025, 0.2],
                        help="0.025 is the figure the paper is usually quoted by; 0.2 is "
                             "what its own SSIM-target fitting actually lands on for this "
                             "content. Showing both is the point.")
    parser.add_argument("--difference-gain", type=float, default=8.0)
    parser.add_argument("--skip-render", action="store_true")
    parser.add_argument("--clean", action="store_true",
                        help="rebuild the thumbnails from scratch; leaves player/ alone")
    parser.add_argument("--serve", type=int, default=0, metavar="PORT")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    index = build(args)
    if args.serve:
        import http.server
        import socket

        directory = str(index.parent)

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *handler_args, **handler_kwargs):
                super().__init__(*handler_args, directory=directory, **handler_kwargs)

            def end_headers(self):
                # Regenerating the report renames assets. A browser holding a
                # cached index.html then asks for files that no longer exist and
                # shows a page of broken images, which looks exactly like the
                # pipeline having produced nothing. Never cache.
                self.send_header("Cache-Control", "no-store, must-revalidate")
                super().end_headers()

            def log_message(self, *_):
                pass

        http.server.ThreadingHTTPServer.allow_reuse_address = True
        with http.server.ThreadingHTTPServer(("0.0.0.0", args.serve), Handler) as server:
            host = socket.gethostbyname(socket.gethostname())
            print(f"serving {directory} at http://{host}:{args.serve}/  (ctrl-c to stop)",
                  flush=True)
            server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
