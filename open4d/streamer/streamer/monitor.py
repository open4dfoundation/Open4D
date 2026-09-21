"""What actually went over the wire.

A streaming project should not have to guess at its own transport. NeVo models
byte arrival offline -- a bandwidth trace gives queueing delay, a loss trace
gives drops -- and this is the live counterpart at the other end: what was
requested, how big it was, and how long it took. It is the same measurement,
taken rather than simulated, and it is what makes a claim about a
representation's cost checkable instead of asserted.

Deliberately counters and nothing more. No bitrate adaptation, no policy, no
history beyond a small ring for the recent tail: a monitor that decided things
would be a second scheduler, and there is not yet a second transport to adapt
between. Totals, a per-clip rollup, and the last few transfers answer "is this
bundle heavy, and where", which is the question actually being asked.

Thread-safe because the server is threaded: one handler thread per connection,
all recording into one instance.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class Transfer:
    """One completed response."""

    path: str
    status: int
    bytes: int
    seconds: float

    @property
    def bytes_per_second(self) -> float | None:
        """None rather than infinity when the transfer was too fast to time."""
        return self.bytes / self.seconds if self.seconds > 0 else None


def clip_of(path: str) -> str:
    """The clip a frame path belongs to.

    Bundle paths are ``<clip>/<frame>``, so the first segment is the clip. Used
    to roll bytes up per clip, which is the granularity a bundle's weight is
    actually discussed at -- "the Vega clip is 128 MB" rather than a list of 30
    frame sizes.
    """
    trimmed = path.lstrip("/").split("?", 1)[0]
    head, _, tail = trimmed.partition("/")
    return head if tail else ""


class Monitor:
    """Counters for one server's traffic."""

    def __init__(self, *, tail: int = 32) -> None:
        if tail < 0:
            raise ValueError("tail must be nonnegative")
        self._lock = threading.Lock()
        self._started = time.monotonic()
        self._requests = 0
        self._bytes = 0
        self._errors = 0
        self._by_clip: dict[str, dict[str, int]] = {}
        self._tail: deque[Transfer] = deque(maxlen=tail) if tail else deque(maxlen=1)
        self._keep_tail = tail > 0

    def record(self, path: str, status: int, size: int, seconds: float) -> Transfer:
        """Note one completed response and return it."""
        transfer = Transfer(path=path, status=int(status), bytes=int(size),
                            seconds=float(seconds))
        clip = clip_of(path)
        with self._lock:
            self._requests += 1
            self._bytes += transfer.bytes
            if transfer.status >= 400:
                self._errors += 1
            rollup = self._by_clip.setdefault(clip, {"requests": 0, "bytes": 0})
            rollup["requests"] += 1
            rollup["bytes"] += transfer.bytes
            if self._keep_tail:
                self._tail.append(transfer)
        return transfer

    def snapshot(self) -> dict[str, Any]:
        """A JSON-ready view of the counters.

        Taken under the lock so the totals and the rollup describe the same
        instant; a reader that saw bytes from one moment and per-clip figures
        from another would not add up, and that is exactly the kind of
        discrepancy that gets blamed on the measurement.
        """
        with self._lock:
            elapsed = time.monotonic() - self._started
            by_clip = {
                name: dict(counts)
                for name, counts in sorted(
                    self._by_clip.items(), key=lambda item: -item[1]["bytes"]
                )
            }
            tail = list(self._tail) if self._keep_tail else []
            requests, total, errors = self._requests, self._bytes, self._errors
        return {
            "requests": requests,
            "bytes": total,
            "errors": errors,
            "elapsed_seconds": round(elapsed, 3),
            "bytes_per_second": round(total / elapsed, 1) if elapsed > 0 else None,
            "by_clip": by_clip,
            "recent": [
                {
                    "path": item.path,
                    "status": item.status,
                    "bytes": item.bytes,
                    "seconds": round(item.seconds, 4),
                }
                for item in tail
            ],
        }

    def reset(self) -> None:
        """Zero the counters, e.g. between two measured playbacks."""
        with self._lock:
            self._started = time.monotonic()
            self._requests = 0
            self._bytes = 0
            self._errors = 0
            self._by_clip.clear()
            self._tail.clear()
