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
"""

from __future__ import annotations

import tempfile
from pathlib import Path
from typing import TYPE_CHECKING, Any, Sequence as TypingSequence

if TYPE_CHECKING:  # pragma: no cover - import cycle only matters to a checker
    from open4d.core import Sequence

__all__ = ["StreamerDependencyError", "stream"]

#: Delivery, not interchange. Draco compresses this repository's mesh sequence
#: 12.9x, and a browser decodes it with the vendored WASM decoder; PLY at
#: 23 MB/s is an archive format that happens to be servable. `streamer.Rung`
#: parses the spec, so "draco@11" and a list of several also work.
DEFAULT_RUNGS: tuple[str, ...] = ("draco",)


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


def stream(
    source: "Sequence | Path | str",
    *,
    out_dir: Path | str | None = None,
    name: str | None = None,
    title: str | None = None,
    rungs: TypingSequence[str] = DEFAULT_RUNGS,
    fps: float | None = None,
    host: str = "127.0.0.1",
    port: int | None = None,
    open_browser: bool = True,
    block: bool = True,
) -> Any:
    """Export ``source`` as a bundle and serve it to a browser.

    ``source`` is either a loaded `Sequence` or anything `open4d.load` reads.
    ``rungs`` is the quality ladder: the first is the rendition a client plays
    by default and the rest are what it can switch to, so a single-entry list
    is a fixed-quality stream and says so.

    With ``out_dir`` omitted the bundle goes to a temporary directory, which is
    **not** deleted afterwards. An export is minutes of encoding and the
    directory is the artifact; removing it when the server stops would throw
    away the expensive half of the call. The path is on the returned server as
    ``server.bundle_dir``.

    Returns the running server, whose ``monitor`` counts what actually went
    over the wire. With ``block=True`` it runs until interrupted.
    """
    streamer = _require()
    loaded = _is_sequence(source)

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

    session = streamer.Bundle(
        out_dir,
        title=title,
        source=None if loaded else source,
        fps=int(fps) if fps else 30,
    )
    with session:
        if loaded:
            session.add(source, name=name, rungs=rungs)
        else:
            session.add_source(source, name=name, fps=fps, rungs=rungs)

    server = streamer.serve(
        out_dir,
        host=host,
        open_browser=open_browser,
        block=block,
        **({} if port is None else {"port": port}),
    )
    # Recorded on the server for the same reason `serve` records the monitor
    # there: with a temporary `out_dir` the caller never named the directory,
    # and the export is the expensive half of this call.
    server.bundle_dir = out_dir
    return server


def _is_sequence(source: object) -> bool:
    """Whether ``source`` is an in-memory sequence rather than a path to one.

    Structural rather than an `isinstance` against `open4d.core.Sequence`:
    `SequenceView` and anything else satisfying the same protocol should be
    streamable, and the two things this has to tell apart -- a sequence and a
    path -- differ clearly enough that a length and an index is the whole test.
    """
    return not isinstance(source, (str, Path)) and hasattr(source, "__len__")
