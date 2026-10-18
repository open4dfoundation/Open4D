"""Resumable, verified HTTPS transfers and inter-process download locks."""

from __future__ import annotations

import contextlib
import errno
import hashlib
import http.client
import os
import re
import time
import urllib.error
import urllib.request

from ._catalog import https_url
from ._download import _HTTPSRedirect

CHUNK = 1024 * 1024
TIMEOUT = 60


def _build_opener():
    return urllib.request.build_opener(_HTTPSRedirect())


def _open(url, start):
    https_url(url)
    headers = {"User-Agent": "Open4D-data/2", "Accept-Encoding": "identity"}
    if start:
        headers["Range"] = f"bytes={start}-"
    response = _build_opener().open(urllib.request.Request(url, headers=headers),
                                    timeout=TIMEOUT)
    https_url(response.geturl())
    return response


def sha256_file(path, prefix=None):
    digest = prefix or hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            digest.update(chunk)
    return digest


def fetch(url, partial, *, size, sha256, progress=None):
    """Download ``url`` into ``partial``, resuming from its current length.

    The server must honor ``Range`` with a matching ``Content-Range`` to
    resume; otherwise the transfer restarts. Interrupted transfers keep the
    partial file for the next attempt; a digest mismatch deletes it.
    """
    have = partial.stat().st_size if partial.exists() else 0
    if have > size:
        partial.unlink()
        have = 0
    if have < size:
        try:
            with _open(url, have) as response:
                status = getattr(response, "status", None) or response.getcode()
                if have and status == 206:
                    match = re.fullmatch(r"bytes (\d+)-\d+/(\d+|\*)",
                                         response.headers.get("Content-Range", "").strip())
                    if not match or int(match[1]) != have or match[2] not in ("*", str(size)):
                        raise ValueError(f"server returned an unexpected range for {url}")
                elif status == 200:
                    have = 0
                else:
                    raise ValueError(f"unexpected HTTP status {status} for {url}")
                count = have
                with partial.open("ab" if have else "wb") as output:
                    for chunk in iter(lambda: response.read(CHUNK), b""):
                        count += len(chunk)
                        if count > size:
                            raise ValueError(f"download exceeds its declared size: {url}")
                        output.write(chunk)
                        if progress:
                            progress(len(chunk))
        except urllib.error.HTTPError as error:
            raise OSError(f"HTTP {error.code} downloading {url}") from error
        except (http.client.HTTPException, urllib.error.URLError, TimeoutError,
                ConnectionError) as error:
            kept = partial.stat().st_size if partial.exists() else 0
            raise OSError(f"transfer failed ({error}); run the download again to resume "
                          f"from byte {kept}") from error
        if count < size:
            raise OSError(f"transfer ended at {count} of {size} bytes; run the download "
                          "again to resume")
    if sha256_file(partial).hexdigest() != sha256:
        partial.unlink()
        raise ValueError(f"SHA-256 mismatch for {url}; the partial file was removed")


def assemble(parts, target, *, size, sha256=None):
    """Concatenate verified parts, verifying the whole file's size and digest."""
    digest, count = hashlib.sha256(), 0
    with target.open("wb") as output:
        for part in parts:
            with part.open("rb") as stream:
                for chunk in iter(lambda: stream.read(CHUNK), b""):
                    count += len(chunk)
                    digest.update(chunk)
                    output.write(chunk)
    if count != size or (sha256 is not None and digest.hexdigest() != sha256):
        target.unlink()
        raise ValueError(f"assembled {target.name} does not match its declared size/digest")
    return digest.hexdigest()


def _try_lock(handle):
    if os.name == "nt":  # pragma: no cover - exercised on Windows only
        import msvcrt
        msvcrt.locking(handle, msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)


def _unlock(handle):
    if os.name == "nt":  # pragma: no cover
        import msvcrt
        os.lseek(handle, 0, os.SEEK_SET)
        msvcrt.locking(handle, msvcrt.LK_UNLCK, 1)
    else:
        import fcntl
        fcntl.flock(handle, fcntl.LOCK_UN)


@contextlib.contextmanager
def locked(path, *, timeout=None, poll=0.2):
    """Hold an exclusive lock on ``path`` across processes and threads.

    The lock file stays in place: removing it would let two waiters lock
    different inodes. ``timeout=None`` waits indefinitely.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
    deadline = None if timeout is None else time.monotonic() + timeout
    try:
        while True:
            try:
                _try_lock(handle)
                break
            except OSError as error:
                if error.errno not in (errno.EAGAIN, errno.EACCES, errno.EWOULDBLOCK,
                                       errno.EDEADLK):
                    raise
                if deadline is not None and time.monotonic() >= deadline:
                    raise TimeoutError(f"another download holds {path}") from error
                time.sleep(poll)
        try:
            yield
        finally:
            _unlock(handle)
    finally:
        os.close(handle)
