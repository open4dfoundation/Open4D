"""Serve a sequence to a browser, if the optional streamer is installed.

The dependency runs one way. `open4d-streamer` imports `open4d` -- for the
`Representation` vocabulary a bundle declares its clips in -- and Open4D must
not import it back, or the two become one package that cannot be released
separately and the reconstruction tree ends up in the base wheel. So this is a
verb in the public API whose implementation lives outside it, imported on the
call rather than at module load, the same arrangement `open4d.visualize` has
with Qt.

What `stream` does is the two-step export-then-serve written as one line,
which is the common case:

    open4d.stream("capture.usdc")

Everything it hides is reachable directly -- `streamer.Bundle` to collect
several clips, `streamer.serve` to serve a bundle already on disk, and
`streamer.Link` to constrain the link so the rate means something.

What it accepts is checked here, against Open4D's own types, before anything is
imported or written: a mesh or point-cloud `Sequence`, a Gaussian sequence (a
QUEEN or 3DGStream `GaussianRun`, a Gaussian `NativeSequence`, Vega's decoded
frames, or a list of `GaussianSplats`), or a path `open4d.load` reads. A single
frame, or a representation no browser decodes, is a `TypeError` that says so.
"""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import Any, Sequence as TypingSequence

from .core import Frame, GaussianCloud, PointCloud, Sequence, TriangleMesh
from .gaussians import GaussianRun, GaussianSplats, NeuralGaussianFrame, VegaRun
from .native import NativeSequence

__all__ = ["StreamerDependencyError", "stream"]

#: Delivery, not interchange. Draco compresses this repository's mesh sequence
#: 12.9x, and a browser decodes it with the vendored WASM decoder; PLY at
#: 23 MB/s is an archive format that happens to be servable. `streamer.Rung`
#: parses the spec, so "draco@11", "klt" or "tsmc/draco" and a list of several
#: also work.
DEFAULT_RUNGS: tuple[str, ...] = ("draco",)

#: Gaussians stay in 3DGS PLY unless asked otherwise: ``.splat`` is several times
#: smaller but drops every view-dependent SH band, which a caller should choose.
DEFAULT_GAUSSIAN_RUNGS: tuple[str, ...] = ("ply",)

#: What ``rungs=None`` means, by representation. A path's representation is not
#: known until it is loaded, so the choice is handed to the bundle as a table.
_DEFAULTS = {
    "mesh": DEFAULT_RUNGS,
    "points": DEFAULT_RUNGS,
    "gaussians": DEFAULT_GAUSSIAN_RUNGS,
}

#: NativeSequence representations that decode to Gaussian frames: splats with
#: SH (QUEEN, 3DGStream), or Vega's, whose colour is a network that is baked.
GAUSSIAN_REPRESENTATIONS = frozenset(("gaussian_splats", "neural_gaussians"))

_PATH = "path"
_SEQUENCE = "sequence"
_GAUSSIANS = "gaussians"


class StreamerDependencyError(ImportError):
    """`open4d.stream` was called without the streamer package installed."""


def _require() -> Any:
    """The `streamer` package, or an error saying how to get it.

    The message names the source checkout rather than an extra, because there
    is no published `open4d-streamer` to resolve: the component is excluded
    from distribution while its provenance review is open (see
    ``THIRD_PARTY.md``). Advising ``pip install 'open4d[streamer]'`` would be
    advice that fails.
    """
    try:
        import streamer
    except ImportError as error:
        raise StreamerDependencyError(
            "open4d.stream needs the 'open4d-streamer' package, which is not "
            "part of the base install -- it imports open4d, so open4d cannot "
            "depend on it. From a source checkout:\n"
            "    python -m pip install -e open4d/streamer"
        ) from error
    return streamer


def kind_of(source: object) -> str:
    """``"path"``, ``"sequence"`` or ``"gaussians"``, or a `TypeError`.

    Checked against Open4D's types rather than by shape. The test this
    replaced was "has a length", which a single `GaussianSplats` frame passes
    -- its length is its Gaussian count -- as does a ReRF `NativeSequence`, and
    both then failed deep inside the exporter on an attribute neither has.
    """
    if isinstance(source, (str, os.PathLike)):
        return _PATH
    if isinstance(source, Sequence):
        return _SEQUENCE
    if isinstance(source, (GaussianRun, VegaRun)):
        return _GAUSSIANS
    if isinstance(source, NativeSequence):
        if source.representation in GAUSSIAN_REPRESENTATIONS:
            return _GAUSSIANS
        raise TypeError(
            f"a {source.codec} NativeSequence is {source.representation}, which no "
            "browser can decode; render it server-side with streamer.live or "
            "streamer.adopt"
        )
    single = (Frame, TriangleMesh, PointCloud, GaussianCloud, GaussianSplats,
              NeuralGaussianFrame)
    if isinstance(source, single):
        raise TypeError(
            f"open4d.stream streams sequences, and a {type(source).__name__} is "
            "one frame; wrap frames in an open4d.Sequence, or pass a list of "
            "GaussianSplats"
        )
    if isinstance(source, (list, tuple)) and source and all(
        isinstance(frame, (GaussianSplats, NeuralGaussianFrame)) for frame in source
    ):
        return _GAUSSIANS
    raise TypeError(
        f"open4d.stream cannot stream a {type(source).__name__}; expected an "
        "open4d.Sequence, a GaussianRun, a Gaussian NativeSequence, a list of "
        "GaussianSplats, or a path open4d.load reads"
    )


def browser_only(source: object) -> bool:
    """Whether ``source`` can only be streamed to a browser, never over TCP.

    The TCP transport sends mesh frames. A path, and every Gaussian form, has
    no TCP meaning, so `open4d.stream` routes them here without browser
    keywords; an unsupported source is left for whichever path reports it.
    """
    try:
        return kind_of(source) != _SEQUENCE
    except TypeError:
        return False


def _default_name(source: object) -> str | None:
    """A name taken from a source that has a path of its own, if it has one."""
    if isinstance(source, GaussianRun):
        return source.path.name
    if isinstance(source, (VegaRun, NativeSequence)):
        return Path(source.path).stem
    return None


def stream(
    source: Any,
    *,
    out_dir: Path | str | None = None,
    name: str | None = None,
    title: str | None = None,
    rungs: TypingSequence[str] | None = None,
    fps: float | None = None,
    score: bool = False,
    link: Any = None,
    monitor: Any = None,
    host: str = "127.0.0.1",
    port: int | None = None,
    open_browser: bool = True,
    block: bool = True,
) -> Any:
    """Export ``source`` as a bundle and serve it to a browser.

    ``source`` is a loaded `Sequence`, a Gaussian sequence (see the module
    docstring), or anything `open4d.load` reads. ``rungs`` is the quality
    ladder: the first is the rendition a client plays by default and the rest
    are what it can switch to, so a single-entry list is a fixed-quality stream
    and says so. Omitted, it is ``draco`` for meshes and points and ``ply`` for
    Gaussians.

    ``fps`` applies only to a source carrying no timing of its own: a path to
    bare frames, or Gaussian frames from a trainer's output. A loaded
    `Sequence` always has timestamps, so the bundle plays at their rate and
    ``fps`` is ignored rather than allowed to contradict them.

    ``score=True`` measures every rung's geometry against the source with
    `open4d.compare_sequences`, so `streamer.policy` and `streamer.playback`
    have a quality to choose by (``metric="point_psnr"``). Meshes and point
    clouds only.

    ``link`` (a `streamer.Link`) shapes what the server sends, and ``monitor``
    (a `streamer.Monitor`) is where it counts it; both go to `streamer.serve`.

    With ``out_dir`` omitted the bundle goes to a temporary directory, which is
    **not** deleted afterwards. An export is minutes of encoding and the
    directory is the artifact; removing it when the server stops would throw
    away the expensive half of the call. The path is on the returned server as
    ``server.bundle_dir``.

    Returns the running server, whose ``monitor`` counts what actually went
    over the wire. With ``block=True`` it runs until interrupted.
    """
    kind = kind_of(source)
    streamer = _require()
    loaded = kind != _PATH
    if name is None:
        name = _default_name(source)

    if loaded and name is None:
        raise ValueError(
            "streaming a Sequence needs a name: it has no path to take one "
            "from, and the name is what labels the pane in the viewer and "
            "what the frame directory is called"
        )

    temporary = out_dir is None
    if temporary:
        out_dir = Path(tempfile.mkdtemp(prefix="open4d-stream-"))
    out_dir = Path(out_dir).expanduser().resolve()
    if temporary:
        # Printed rather than only returned, because `serve(block=True)` does
        # not return until it is interrupted -- and a caller who never named
        # the directory would have no way to find it in the meantime. `serve`
        # prints its URL for the same reason.
        print(f"bundle: {out_dir}")

    if title is None:
        title = name or (None if loaded else Path(str(source)).stem) or out_dir.name

    ladder = _DEFAULTS if rungs is None else rungs
    session = streamer.Bundle(
        out_dir,
        title=title,
        source=None if loaded else source,
        # Trainer output and bare splat lists carry no timing, so ``fps`` is
        # theirs; a NativeSequence has timestamps, which win as for a Sequence.
        fps=fps if kind == _GAUSSIANS and not isinstance(source, NativeSequence) else None,
    )
    with session:
        if loaded:
            session.add(source, name=name, rungs=ladder, score=score)
        else:
            session.add_source(source, name=name, fps=fps, rungs=ladder, score=score)

    server = streamer.serve(
        out_dir,
        host=host,
        open_browser=open_browser,
        block=block,
        link=link,
        monitor=monitor,
        **({} if port is None else {"port": port}),
    )
    # Recorded on the server for the same reason `serve` records the monitor
    # there: with a temporary `out_dir` the caller never named the directory,
    # and the export is the expensive half of this call.
    server.bundle_dir = out_dir
    return server

