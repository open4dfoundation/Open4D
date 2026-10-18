#!/usr/bin/env python3
"""Documentation-specific deploy gate: the built site republishes no unreviewed material.

This gate is independent of the package release gate. It passes only when the
built site (1) contains text, markup, script and style files only, with no
``assets`` media directory; (2) embeds no images, video, audio, frames or
objects; (3) renders no page whose source lies in a ``BLOCK`` component of
``THIRD_PARTY.md``; and (4) contains no file byte-identical to a file of a
``BLOCK`` component. Upstream and research documentation must stay linked at
its source, as ``docs/hooks.py`` does, instead of being copied into the site.
"""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
import sys

from check_release_exclusions import ROOT, collect_evidence, read_site


ALLOWED_SUFFIXES = {".html", ".css", ".js", ".json", ".xml"}
ALLOWED_FILES = {"sitemap.xml.gz"}
EMBEDDING_TAGS = {"img", "picture", "video", "audio", "source", "track", "embed", "object", "iframe"}


class _Embeds(HTMLParser):
    def __init__(self, text: str):
        super().__init__(convert_charrefs=True)
        self.embeds: list[str] = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        if tag in EMBEDDING_TAGS:
            values = dict(attrs)
            self.embeds.append(f"<{tag} {values.get('src') or values.get('data') or values.get('srcset') or ''}>".strip())


def check_site(site: Path, root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    if (site / "assets").exists():
        errors.append("site contains the unreviewed assets directory")
    artifact = read_site(site)
    errors.extend(artifact.errors)
    for member in artifact.members:
        if member.data is None:
            continue
        relative = Path(member.name)
        if relative.suffix not in ALLOWED_SUFFIXES and relative.name not in ALLOWED_FILES:
            errors.append(f"site contains a media or binary file: {member.name}")
        if relative.suffix == ".html":
            for embed in _Embeds(member.data.decode("utf-8", "replace")).embeds:
                errors.append(f"{member.name}: embeds media {embed}")
    ledger = (root / "THIRD_PARTY.md").read_text(encoding="utf-8")
    evidence = collect_evidence([artifact], ledger, root)
    for component in evidence["components"]:
        if component["decision"] != "BLOCK":
            continue
        detail = component["artifacts"].get(site.name, {})
        for page in detail.get("paths", []):
            errors.append(f"{page.removeprefix('page:')} renders source from BLOCK component "
                          f"{component['component']}; link it at its source instead")
        for match in detail.get("content", []):
            errors.append(f"site file copies BLOCK component {component['component']}: {match}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("site", type=Path)
    args = parser.parse_args()
    errors = check_site(args.site.resolve())
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Documentation content gate passed: {args.site}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
