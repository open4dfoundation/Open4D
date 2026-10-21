#!/usr/bin/env python3
"""Prove a package index serves exactly the locally built distributions.

For every wheel and source archive in ``--dist``, query the index's JSON API,
require the same filename set for that project version with identical SHA-256
digests, download each file from the index and re-check its bytes. The
downloaded copies (``--download-to``) are what the publish workflow installs in
fresh environments, so smoke tests exercise the files the index actually serves.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time
from typing import Callable
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import urlopen

from check_release_gate import distribution_digests


INDEXES = {
    "testpypi": ("https://test.pypi.org", {"test-files.pythonhosted.org"}),
    "pypi": ("https://pypi.org", {"files.pythonhosted.org"}),
}
DIST_NAME = re.compile(r"(?P<name>[A-Za-z0-9_.]+)-(?P<version>[^-]+?)(?:-[^-]+-[^-]+-[^-]+\.whl|\.tar\.gz)")


class NotYetAvailable(Exception):
    """The index has not (yet) published the release files."""


def fetch_url(url: str) -> bytes:
    with urlopen(url, timeout=60) as response:  # noqa: S310 - fixed https hosts
        return response.read()


def project_version(filename: str) -> tuple[str, str]:
    match = DIST_NAME.fullmatch(filename)
    if not match:
        raise ValueError(f"not a distribution filename: {filename}")
    return re.sub(r"[-_.]+", "-", match["name"]).lower(), match["version"]


def index_files(index: str, project: str, version: str,
                fetch: Callable[[str], bytes] = fetch_url) -> dict[str, tuple[str, str]]:
    """Return ``{filename: (sha256, url)}`` for one project version on the index."""
    base, _ = INDEXES[index]
    try:
        payload = json.loads(fetch(f"{base}/pypi/{project}/{version}/json"))
    except HTTPError as error:
        if error.code == 404:
            raise NotYetAvailable(f"{project} {version} is not on {index}") from error
        raise
    return {item["filename"]: (item["digests"]["sha256"], item["url"]) for item in payload["urls"]}


def verify(dist: Path, index: str, download_to: Path | None = None,
           fetch: Callable[[str], bytes] = fetch_url) -> list[str]:
    """Compare the index with ``dist``; download verified copies when requested."""
    local, errors = distribution_digests(dist)
    if not local:
        errors.append(f"no distributions found in {dist}")
    projects: dict[tuple[str, str], set[str]] = {}
    for filename in local:
        projects.setdefault(project_version(filename), set()).add(filename)
    _, hosts = INDEXES[index]
    for (project, version), filenames in sorted(projects.items()):
        remote = index_files(index, project, version, fetch)
        missing = filenames - remote.keys()
        if missing:
            raise NotYetAvailable(f"{index} lacks {sorted(missing)}")
        for filename in sorted(remote.keys() - filenames):
            errors.append(f"{index} serves an unapproved file for {project} {version}: {filename}")
        for filename in sorted(filenames):
            digest, url = remote[filename]
            if digest != local[filename]:
                errors.append(f"{index} digest differs from the built artifact: {filename}")
                continue
            if download_to is None:
                continue
            parts = urlsplit(url)
            if parts.scheme != "https" or parts.hostname not in hosts:
                errors.append(f"{index} file URL is not on an expected host: {url}")
                continue
            data = fetch(url)
            if hashlib.sha256(data).hexdigest() != digest:
                errors.append(f"downloaded bytes differ from the built artifact: {filename}")
                continue
            download_to.mkdir(parents=True, exist_ok=True)
            (download_to / filename).write_bytes(data)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--index", choices=sorted(INDEXES), required=True)
    parser.add_argument("--dist", type=Path, required=True, help="directory of the built distributions")
    parser.add_argument("--download-to", type=Path, help="store digest-verified copies from the index")
    parser.add_argument("--attempts", type=int, default=20, help="retries while the index propagates")
    parser.add_argument("--delay", type=float, default=15.0, help="seconds between attempts")
    args = parser.parse_args()
    for attempt in range(1, args.attempts + 1):
        try:
            errors = verify(args.dist, args.index, args.download_to)
            break
        except (NotYetAvailable, URLError, TimeoutError) as error:
            if attempt == args.attempts:
                errors = [f"index did not serve the release: {error}"]
                break
            print(f"Waiting for {args.index} ({attempt}/{args.attempts}): {error}", file=sys.stderr)
            time.sleep(args.delay)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"{args.index} serves the exact built distributions from {args.dist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
