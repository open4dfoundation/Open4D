"""What the viewer actually received, scored against the real thing.

`monitor` answers how many bytes moved. This answers the other half, and the
one a comparison needs: how good was the picture. Without it a bundle can show
two methods side by side but cannot say which is better, and "which is better"
is the question the whole platform exists to make answerable.

**It works on a bundle, not on a method.** Any method whose output reaches a
bundle can be scored the same way, by the same code, against the same
reference -- which is the only arrangement under which two numbers are
comparable. A metric shipped inside each method would be nine metrics.

How a pair is found: a scene's clips each name a ``method`` and a ``camera``
station. The clip whose method is ``captured`` at a station is the reference --
it is the photograph, not a reconstruction -- and every other clip at that same
station is scored against it. Same subject, same instant, same pose, which is
what the scene and camera fields are for.

What it cannot do yet, stated rather than silently skipped:

* **Geometry clips.** A Gaussian or mesh clip has no pixels until something
  renders it from the reference's pose. That is a renderer, not a metric, and
  it belongs with whatever can rasterise the representation. Those clips are
  reported as unmeasured, with the reason.
* **Live clips.** Nothing to score frame-by-frame against; a stream has no
  frame list.

SSIM is implemented here rather than imported. `streamer` depends on `open4d`
and nothing else, and a metric is a poor reason to add scikit-image to a
package whose job is transport. It is the Wang et al. (2004) formulation with a
Gaussian window, and ``streamer_tests/test_metrics.py`` holds it to
scikit-image's implementation to 1e-4 wherever that library happens to be
installed -- so the shortcut is checked rather than trusted.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import bundle, representations

#: The method whose clips are the reference rather than a reconstruction.
REFERENCE_METHOD = "captured"

#: What a clip's pixels are of, read from ``detail["depicts"]``. Only a clip
#: depicting the scene's *appearance* is attempting to reproduce the reference
#: photograph, so only that one can be scored against it.
#:
#: This exists because the alternative is worse than useless. A depth map
#: scored against a colour photograph comes out at 0.4 dB -- which reads as a
#: method that catastrophically failed, rather than as a comparison that was
#: never meaningful. "Not applicable" has to be expressible.
APPEARANCE = "appearance"


def depicts(clip) -> str:
    """What a clip's pixels are of. Absent means appearance, so older bundles
    and any method that never thought about this are measured as before."""
    return (clip.get("detail") or {}).get("depicts", APPEARANCE)

#: Score every Nth frame by default. A 30-frame clip at 5 gives six samples,
#: which is enough for a mean that does not move between runs and cheap enough
#: to score 200 clips. Deterministic, not random: a research number that
#: changes when you re-run it is not a number.
EVERY = 5

# SSIM constants, from the paper and matching scikit-image's defaults.
K1, K2 = 0.01, 0.03
WINDOW, SIGMA, TRUNCATE = 11, 1.5, 3.5


def psnr(prediction, truth, *, data_range: float = 1.0) -> float:
    """Peak signal-to-noise ratio in dB. Infinite for identical images."""
    import numpy as np

    error = float(np.mean((np.asarray(prediction, np.float64)
                           - np.asarray(truth, np.float64)) ** 2))
    if error <= 0.0:
        return float("inf")
    return float(10.0 * math.log10(data_range ** 2 / error))


def _gaussian_kernel(sigma: float, truncate: float):
    import numpy as np

    radius = int(truncate * sigma + 0.5)
    x = np.arange(-radius, radius + 1, dtype=np.float64)
    kernel = np.exp(-(x ** 2) / (2.0 * sigma ** 2))
    return kernel / kernel.sum()


def _blur_scipy(image):
    """The same blur via scipy, when it happens to be installed.

    Measured on a 1280x960 colour frame: 287 ms against 696 ms for the numpy
    fallback, because scipy's filter is compiled and this one builds large
    sliding-window views. Identical output -- ``sigma`` and ``truncate`` give
    the same 11-tap kernel, and ``reflect`` is what the fallback pads with --
    and ``test_both_blur_backends_agree`` holds them together.

    Not a declared dependency: `streamer` requires `open4d` and nothing else,
    so this is used when present and skipped when not.
    """
    from scipy.ndimage import gaussian_filter

    sigma = (SIGMA, SIGMA, 0) if image.ndim == 3 else (SIGMA, SIGMA)
    return gaussian_filter(image, sigma=sigma, truncate=TRUNCATE, mode="reflect")


def _blur_numpy(image, kernel):
    """Separable Gaussian blur with reflect padding, as scipy's default does.

    Two decisions here are both about speed, on images where it matters -- a
    1280x960 frame is 1.2 million pixels and one SSIM needs five blurs of it.

    A 2-D Gaussian is the outer product of two 1-D ones, so this is two dot
    products over sliding windows rather than one 2-D convolution: 2n
    multiplies per pixel instead of n squared.

    And a colour image is blurred in one pass with the channel axis carried
    along, rather than once per channel. Same arithmetic, a third of the Python
    and a third of the passes over memory.

    ``image`` is ``[H, W]`` or ``[H, W, C]``; the result has the same shape.
    """
    import numpy as np
    from numpy.lib.stride_tricks import sliding_window_view

    radius = (len(kernel) - 1) // 2
    spatial = ((radius, radius), (radius, radius))
    padding = spatial + (((0, 0),) if image.ndim == 3 else ())
    # "symmetric", not numpy's "reflect": scipy's `reflect` -- what
    # scikit-image's SSIM filters with -- repeats the edge sample
    # (a b c d -> d c b a | a b c d), whereas numpy's `reflect` omits it
    # (a b c d -> d c b | a b c d). The two agree everywhere except a
    # five-pixel border, which is why this was invisible on a 1280x960 frame
    # at 1e-8 and only showed up on a 40x56 one.
    padded = np.pad(image, padding, mode="symmetric")
    rows = sliding_window_view(padded, len(kernel), axis=1) @ kernel
    return sliding_window_view(rows, len(kernel), axis=0) @ kernel


def _blur_for(image):
    """The fastest blur available, as a one-argument callable."""
    try:
        import scipy.ndimage  # noqa: F401
    except ImportError:
        kernel = _gaussian_kernel(SIGMA, TRUNCATE)
        return lambda array: _blur_numpy(array, kernel)
    return _blur_scipy


def ssim(prediction, truth, *, data_range: float = 1.0) -> float:
    """Structural similarity, Gaussian-windowed, averaged over channels.

    Colour images are scored per channel and averaged, which is what
    scikit-image does with ``channel_axis``. The border is cropped by the
    window radius because a window that hangs off the edge is measuring the
    padding.
    """
    import numpy as np

    a = np.asarray(prediction, np.float64)
    b = np.asarray(truth, np.float64)
    if a.shape != b.shape:
        raise ValueError(f"shapes differ: {a.shape} vs {b.shape}")

    blur = _blur_for(a)
    c1, c2 = (K1 * data_range) ** 2, (K2 * data_range) ** 2

    ux, uy = blur(a), blur(b)
    uxx, uyy, uxy = blur(a * a), blur(b * b), blur(a * b)
    # Population covariance, not sample: scikit-image's
    # use_sample_covariance=False, which is what the paper specifies.
    vx, vy = uxx - ux * ux, uyy - uy * uy
    vxy = uxy - ux * uy

    numerator = (2.0 * ux * uy + c1) * (2.0 * vxy + c2)
    denominator = (ux * ux + uy * uy + c1) * (vx + vy + c2)
    similarity = numerator / denominator

    # Cropped by the window radius, because a window hanging off the edge is
    # measuring the padding. Then averaged over everything left, which for a
    # colour image means over the channels too -- the same figure
    # scikit-image's `channel_axis` produces.
    pad = (WINDOW - 1) // 2
    return float(similarity[pad:-pad, pad:-pad].mean())


def _load(path: Path, size=None):
    """A frame as float [0, 1], resized to ``size`` if it does not match."""
    import numpy as np
    from PIL import Image

    image = Image.open(path).convert("RGB")
    if size is not None and image.size != size:
        image = image.resize(size, Image.LANCZOS)
    return np.asarray(image, np.float32) / 255.0


@dataclass
class ClipScore:
    """One rendition of one reconstruction clip, scored against its reference."""

    scene: str
    method: str
    clip: str
    #: Which rung this is. ``None`` for the clip's default rendition, which is
    #: what a clip with a single quality level has.
    variant: str | None
    reference: str
    camera: int | None
    frames: int
    psnr: float
    ssim: float
    worst_psnr: float
    #: Set when the clip and its reference are different sizes and one was
    #: resampled to match. Worth surfacing: a resize is not free, and a
    #: comparison across scenes where only some were resized is not level.
    resized: str | None = None

    def as_dict(self) -> dict[str, Any]:
        payload = {
            "scene": self.scene, "method": self.method, "clip": self.clip,
            "variant": self.variant,
            "reference": self.reference, "camera": self.camera,
            "frames": self.frames, "psnr": round(self.psnr, 3),
            "ssim": round(self.ssim, 5), "worst_psnr": round(self.worst_psnr, 3),
        }
        if self.resized:
            payload["resized"] = self.resized
        return payload


@dataclass
class Report:
    """Everything that was scored, and everything that could not be."""

    scores: list = field(default_factory=list)
    unmeasured: list = field(default_factory=list)

    def by_scene(self) -> dict:
        grouped: dict = {}
        for score in self.scores:
            grouped.setdefault(score.scene, []).append(score)
        return grouped

    def by_method(self) -> dict:
        grouped: dict = {}
        for score in self.scores:
            grouped.setdefault(score.method, []).append(score)
        return grouped

    def as_dict(self) -> dict[str, Any]:
        return {
            "scores": [score.as_dict() for score in self.scores],
            "unmeasured": self.unmeasured,
        }


def _references(clips) -> dict:
    """``(scene, camera) -> clip name`` for every reference clip."""
    found = {}
    for clip in clips:
        if clip.get("method") == REFERENCE_METHOD and clip.get("frames"):
            found[(clip.get("scene"), clip.get("camera"))] = clip["name"]
    return found


def measure(
    bundle_dir: Path | str,
    *,
    scene: str | None = None,
    every: int = EVERY,
    limit: int | None = None,
) -> Report:
    """Score every measurable clip in a bundle against its reference."""
    if every < 1:
        raise ValueError("every must be at least 1")
    root = Path(bundle_dir).expanduser().resolve()
    index = bundle.read(root)
    if not index:
        raise FileNotFoundError(f"{root} has no {bundle.INDEX_NAME}")

    from PIL import Image

    clips = index.get("clips", [])
    references = _references(clips)
    report = Report()

    for clip in clips:
        name = clip["name"]
        if scene is not None and clip.get("scene") != scene:
            continue
        if clip.get("method") == REFERENCE_METHOD:
            continue
        if clip.get("stream"):
            report.unmeasured.append(
                {"clip": name, "why": "a live stream has no frame list to score"}
            )
            continue
        subject = depicts(clip)
        if subject != APPEARANCE:
            report.unmeasured.append({
                "clip": name,
                "why": f"depicts {subject}, not the scene's appearance, so the "
                       "captured photograph is not a reference for it",
            })
            continue
        spec = representations.spec(clip["representation"])
        if spec.has_geometry:
            report.unmeasured.append({
                "clip": name,
                "why": f"{clip['representation']} has no pixels until something "
                       "renders it from the reference's pose",
            })
            continue
        reference = references.get((clip.get("scene"), clip.get("camera")))
        if reference is None:
            report.unmeasured.append({
                "clip": name,
                "why": f"no {REFERENCE_METHOD} clip at scene "
                       f"{clip.get('scene')!r} camera {clip.get('camera')}",
            })
            continue

        truth_frames = next(
            entry["frames"] for entry in clips if entry["name"] == reference
        )
        sampled = list(range(0, min(len(clip["frames"]), len(truth_frames)), every))
        if limit:
            sampled = sampled[:limit]
        if not sampled:
            report.unmeasured.append({"clip": name, "why": "no overlapping frames"})
            continue

        size = Image.open(root / truth_frames[0]).size
        rendered_size = Image.open(root / clip["frames"][0]).size
        resized = (
            None if rendered_size == size
            else f"{rendered_size[0]}x{rendered_size[1]} -> {size[0]}x{size[1]}"
        )

        # Every rendition, not just the default: a ladder whose rungs have no
        # measured quality is a ladder nothing can choose sensibly between.
        renditions = [(None, clip["frames"], resized)]
        for rung in bundle.variants_of(clip):
            rung_size = Image.open(root / rung.frames[0]).size
            renditions.append((
                rung.name, rung.frames,
                None if rung_size == size
                else f"{rung_size[0]}x{rung_size[1]} -> {size[0]}x{size[1]}",
            ))

        for rung_name, frames, note in renditions:
            peaks, structures = [], []
            for position in sampled:
                if position >= len(frames):
                    break
                prediction = _load(root / frames[position], size)
                truth = _load(root / truth_frames[position])
                peaks.append(psnr(prediction, truth))
                structures.append(ssim(prediction, truth))
            if not peaks:
                continue
            finite = [value for value in peaks if math.isfinite(value)]
            report.scores.append(ClipScore(
                scene=clip.get("scene"), method=clip.get("method"), clip=name,
                variant=rung_name, reference=reference,
                camera=clip.get("camera"), frames=len(peaks),
                psnr=sum(finite) / len(finite) if finite else float("inf"),
                ssim=sum(structures) / len(structures),
                worst_psnr=min(peaks),
                resized=note,
            ))

    return report


def _mean(values) -> float:
    values = list(values)
    return sum(values) / len(values) if values else float("nan")


def render_table(report: Report, *, per_clip: bool = False) -> str:
    """The report as text: per scene and method, or per clip."""
    lines = []
    def rung(score):
        return score.variant or "default"

    if per_clip:
        lines.append(f"{'clip':<34}{'method':<11}{'rung':<9}{'cam':>4}"
                     f"{'PSNR':>8}{'SSIM':>9}{'worst':>8}")
        lines.append("-" * 83)
        for score in report.scores:
            lines.append(
                f"{score.clip:<34}{score.method:<11}{rung(score):<9}"
                f"{'' if score.camera is None else score.camera:>4}"
                f"{score.psnr:>8.2f}{score.ssim:>9.4f}{score.worst_psnr:>8.2f}"
            )
    else:
        # Rolled up per rung as well as per method: the whole point of a ladder
        # is what each step costs and gains, and a mean across rungs would
        # average that away into one meaningless number.
        lines.append(f"{'scene':<18}{'method':<11}{'rung':<9}{'clips':>6}"
                     f"{'PSNR':>8}{'SSIM':>9}{'worst':>8}")
        lines.append("-" * 69)
        grouped: dict = {}
        for score in report.scores:
            grouped.setdefault(
                (str(score.scene), score.method, rung(score)), []
            ).append(score)
        for (scene, method, name), group in sorted(grouped.items()):
            lines.append(
                f"{scene:<18}{method:<11}{name:<9}{len(group):>6}"
                f"{_mean(s.psnr for s in group):>8.2f}"
                f"{_mean(s.ssim for s in group):>9.4f}"
                f"{min(s.worst_psnr for s in group):>8.2f}"
            )
        lines.append("-" * 69)
        overall: dict = {}
        for score in report.scores:
            overall.setdefault((score.method, rung(score)), []).append(score)
        for (method, name), group in sorted(overall.items()):
            lines.append(
                f"{'all':<18}{method:<11}{name:<9}{len(group):>6}"
                f"{_mean(s.psnr for s in group):>8.2f}"
                f"{_mean(s.ssim for s in group):>9.4f}"
                f"{min(s.worst_psnr for s in group):>8.2f}"
            )

    # A lower rung is smaller by design, so only flag a resize on a default
    # rendition -- there it means two methods were compared at different sizes,
    # which is the thing worth knowing.
    resized = [score for score in report.scores
               if score.resized and score.variant is None]
    if resized:
        lines.append("")
        lines.append(f"{len(resized)} clip(s) were resampled to their reference's "
                     "size; those numbers are not level with the rest:")
        for score in resized[:5]:
            lines.append(f"  {score.clip}: {score.resized}")
    if report.unmeasured:
        lines.append("")
        lines.append(f"{len(report.unmeasured)} clip(s) not measured:")
        seen = {}
        for entry in report.unmeasured:
            seen.setdefault(entry["why"], []).append(entry["clip"])
        for why, names in seen.items():
            lines.append(f"  {len(names)}x {why}")
            lines.append(f"       e.g. {names[0]}")
    return "\n".join(lines)


def write_back(bundle_dir: Path | str, report: Report) -> Path:
    """Store measured quality into the bundle, beside the bytes.

    This is what closes the loop. An exporter knows a rung's *size* -- it wrote
    the files -- but not its quality, because quality is a comparison against a
    reference the exporter has no opinion about. So the rung arrives with
    ``bytes`` filled in and ``quality`` empty, and something has to put the
    other half in. Until it does, a chooser reading the ladder can see what
    each rung costs and not what it buys, which is half a decision.

    A rung's own ``quality`` mapping holds it. The default rendition has no
    rung entry to put it in, so it goes in the clip's ``detail["quality"]`` --
    the same shape, one level up.

    Only clips this report actually scored are touched: a partial run should
    fill in what it measured and leave the rest alone rather than blanking it.
    """
    root = Path(bundle_dir).expanduser().resolve()
    index = bundle.read(root)
    if not index:
        raise FileNotFoundError(f"{root} has no {bundle.INDEX_NAME}")

    scored: dict = {}
    for score in report.scores:
        scored[(score.clip, score.variant)] = {
            "psnr": round(score.psnr, 3), "ssim": round(score.ssim, 5),
        }

    clips = []
    for entry in index.get("clips", []):
        clip = bundle.Clip(**entry)
        measured = scored.get((clip.name, None))
        if measured:
            clip.detail = dict(clip.detail, quality=measured)
        if clip.variants:
            updated = []
            for raw in clip.variants:
                found = scored.get((clip.name, raw.get("name")))
                updated.append(dict(raw, quality=found) if found else raw)
            clip.variants = updated
        clips.append(clip)

    return bundle.write(
        root,
        title=index.get("title", root.name),
        source=index.get("source", str(root)),
        clips=clips,
        fps=index.get("fps", 30),
        scenes=index.get("scenes") or {},
        detail=index.get("detail"),
    )


def main(argv=None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("bundle", help="a bundle directory holding view.json")
    parser.add_argument("--scene", help="only this scene")
    parser.add_argument("--every", type=int, default=EVERY,
                        help="score every Nth frame; deterministic, so the number "
                             "does not move between runs")
    parser.add_argument("--limit", type=int, help="at most this many frames per clip")
    parser.add_argument("--per-clip", action="store_true",
                        help="one row per clip instead of a per-method rollup")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument("--write", action="store_true",
                        help="store the measured quality into the bundle, beside each "
                             "rung's byte count, so a chooser can read both")
    args = parser.parse_args(argv)

    report = measure(args.bundle, scene=args.scene, every=args.every,
                     limit=args.limit)
    if args.write:
        write_back(args.bundle, report)
        print(f"wrote quality for {len(report.scores)} renditions into "
              f"{args.bundle}\n")
    if args.json:
        print(json.dumps(report.as_dict(), indent=2))
    else:
        print(render_table(report, per_clip=args.per_clip))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
