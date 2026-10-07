"""Receiving a bundle: the other half of the server's job.

The server sends files; this pulls them. It exists because the common case is
awkward without it: a bundle is produced on the machine with the GPU, and the
person who wants to look at it is on a laptop somewhere else. Streaming it over
a tunnel works for a look, but the geometry clips are heavy -- a 30-frame
Gaussian clip is over 100 MB as PLY -- so anyone returning to the same bundle
twice wants a local copy, and a local copy is just the manifest plus every path
it names.

It reads only `view.json`, so it needs to know nothing about representations:
the manifest already lists every frame, relative to the bundle root. That is the
same property that lets the client play a bundle it did not write.

Not a sync tool. Existing files of the right size are skipped, and a partial
file is continued from where it stopped rather than started again -- that is
the whole policy.

Resumption is the reason this cares about the transport at all. A 30-frame
Gaussian clip is over 100 MB; a tunnel that drops halfway through one frame
used to mean fetching that frame again from zero. With a byte range it costs
only what was actually missed.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Callable, Iterable

from . import bundle
from .monitor import Monitor

#: Read size for streaming a body to disk. Large enough that a 100 MB frame is
#: not a million syscalls, small enough not to matter in memory.
CHUNK = 1 << 20


@dataclass(frozen=True)
class FetchResult:
    """What a fetch did."""

    root: Path
    fetched: tuple[str, ...]
    skipped: tuple[str, ...]
    bytes: int

    @property
    def paths(self) -> tuple[str, ...]:
        return self.fetched + self.skipped


def _get(url: str, timeout: float) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.read()


def _download(url: str, destination: Path, timeout: float) -> int:
    """Stream ``url`` to ``destination``, returning bytes written this call.

    Written to a temporary neighbour and moved into place, so an interrupted
    transfer leaves no short file that the size check would later mistake for a
    complete one.

    A ``.partial`` left by an earlier attempt is *continued*, by asking for the
    bytes after it with a ``Range`` header. Three things have to be true for
    that to be safe, and all three are checked rather than assumed:

    * the server has to answer ``206`` -- a ``200`` means it ignored the range
      and is sending the whole file, so the partial is discarded and this
      becomes a plain download rather than appending a second copy;
    * the range has to start where the partial ends, which is what was asked
      for and is verified against ``Content-Range``;
    * on any failure the partial survives, so the next attempt can try again.

    The file is only moved into place once the transfer completes, so a
    ``.partial`` is always exactly the prefix that has arrived.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(destination.name + ".partial")
    have = partial.stat().st_size if partial.is_file() else 0

    request = urllib.request.Request(url)
    if have:
        request.add_header("Range", f"bytes={have}-")

    written = 0
    with urllib.request.urlopen(request, timeout=timeout) as response:
        resuming = have > 0 and response.status == 206
        if resuming:
            content_range = response.headers.get("Content-Range", "")
            if not content_range.startswith(f"bytes {have}-"):
                raise ValueError("invalid Content-Range for resumed download")
        if have and not resuming:
            # The server sent the whole file despite the range. Start over
            # rather than append: the alternative is a corrupt file that is
            # exactly the right size for the check to accept.
            have = 0
        with open(partial, "ab" if resuming else "wb") as handle:
            while True:
                block = response.read(CHUNK)
                if not block:
                    break
                handle.write(block)
                written += len(block)
    partial.replace(destination)
    return written


def _remote_size(url: str, timeout: float) -> int | None:
    """Content-Length from a HEAD, or None when the server will not say."""
    request = urllib.request.Request(url, method="HEAD")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            length = response.headers.get("Content-Length")
            return int(length) if length is not None else None
    except (urllib.error.URLError, ValueError, OSError):
        return None


def frame_paths(index: dict) -> tuple[str, ...]:
    """Every frame path a manifest names, in order, without duplicates."""
    seen: dict[str, None] = {}
    for clip in index.get("clips", []):
        packed = clip.get("sequence")
        paths = [packed["url"]] if packed else clip.get("frames", [])
        for variant in clip.get("variants", []):
            paths = [*paths, *variant.get("frames", [])]
        for path in paths:
            seen.setdefault(path, None)
    return tuple(seen)


def _safe_target(root: Path, path: str) -> Path:
    if (not isinstance(path, str) or not path or "\\" in path or "\x00" in path
            or PurePosixPath(path).is_absolute() or PureWindowsPath(path).drive
            or any(part in {"", ".", ".."} for part in path.split("/"))
            or path == bundle.INDEX_NAME or path.endswith(".partial")):
        raise ValueError(f"unsafe bundle path: {path!r}")
    target = root / path
    for candidate in (target, target.with_name(target.name + ".partial")):
        if root not in candidate.resolve().parents or candidate.is_symlink():
            raise ValueError(f"unsafe bundle path: {path!r}")
    return target


def fetch(
    url: str,
    destination: Path | str,
    *,
    timeout: float = 30.0,
    monitor: Monitor | None = None,
    progress: Callable[[str, int, int], None] | None = None,
    only: Iterable[str] | None = None,
) -> FetchResult:
    """Copy the bundle served at ``url`` into ``destination``.

    ``only`` restricts the transfer to those clip names, which is how you take
    one method off a 225-clip bundle instead of all of it. ``monitor`` records
    the same counters the server keeps, so a transfer can be measured from
    either end. ``progress`` is called as ``(path, done, total)``.
    """
    base = url if url.endswith("/") else url + "/"
    root = Path(destination).expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)

    index = json.loads(_get(base + bundle.INDEX_NAME, timeout))
    if only is not None:
        wanted = set(only)
        index["clips"] = [
            clip for clip in index.get("clips", []) if clip.get("name") in wanted
        ]
        missing = wanted - {clip.get("name") for clip in index["clips"]}
        if missing:
            raise KeyError(f"no clip named {', '.join(sorted(missing))} in {base}")

    paths = frame_paths(index)
    targets = {path: _safe_target(root, path) for path in paths}
    index_path = root / bundle.INDEX_NAME
    if index_path.is_symlink():
        raise ValueError("unsafe bundle manifest path")
    fetched: list[str] = []
    skipped: list[str] = []
    total_bytes = 0
    for position, path in enumerate(paths, start=1):
        target = targets[path]
        source = base + urllib.parse.quote(path)
        expected = _remote_size(source, timeout) if target.is_file() else None
        if target.is_file() and expected is not None and target.stat().st_size == expected:
            skipped.append(path)
        else:
            # `written` counts what crossed the wire, not the file's size: a
            # resumed frame reports only the tail, which is what a measurement
            # of this transfer should say.
            written = _download(source, target, timeout)
            total_bytes += written
            fetched.append(path)
            if monitor is not None:
                monitor.record(path, 200, written, 0.0)
        if progress is not None:
            progress(path, position, len(paths))

    # Advertise the new bundle only once every referenced file is available.
    index_path.write_text(json.dumps(index, indent=2) + "\n")

    return FetchResult(
        root=root,
        fetched=tuple(fetched),
        skipped=tuple(skipped),
        bytes=total_bytes,
    )
