"""Validate the built documentation, source coverage and local navigation."""
from __future__ import annotations

import argparse
from html.parser import HTMLParser
import json
from pathlib import Path
import sys
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.ids = set()
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if values.get("id"):
            self.ids.add(values["id"])
        for name in ("href", "src"):
            if values.get(name):
                self.links.append(values[name])


def check(site: Path):
    site = site.resolve()
    import yaml
    root = Path(__file__).resolve().parents[1]
    base = urlsplit(yaml.safe_load((root / "mkdocs.yml").read_text())["site_url"]).path
    pages = {path.resolve(): Page(path.read_text()) for path in site.rglob("*.html")}
    errors = []
    for path, page in pages.items():
        for link in page.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            local = unquote(url.path)
            if local.startswith("/"):
                if not local.startswith(base):
                    errors.append(f"{path.relative_to(site)}: target outside site base {link}")
                    continue
                target = (site / local[len(base):]).resolve()
            else:
                target = (path.parent / local).resolve() if local else path
            if target.is_dir():
                target /= "index.html"
            if not target.is_relative_to(site) or not target.is_file():
                errors.append(f"{path.relative_to(site)}: missing target {link}")
            elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].ids:
                errors.append(f"{path.relative_to(site)}: missing anchor {link}")
    # Import the real build hook so source coverage stays tied to the same tree.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "docs"))
    from hooks import source_documents
    catalog = json.loads((site / "documentation.json").read_text())["documents"]
    actual = {record["source"] for record in catalog}
    expected = set(source_documents())
    if actual != expected or len(actual) != len(catalog):
        errors.append(f"Documentation coverage differs: missing={sorted(expected-actual)}, extra={sorted(actual-expected)}")
    index = json.loads((site / "search/search_index.json").read_text())["docs"]
    locations = {record["location"].split("#")[0] for record in index}
    for route in ("tutorials/core-roundtrip/", "tutorials/rgbd-capture/", "codecs/tvmc/", "library/"):
        if route not in locations:
            errors.append(f"Search does not index {route}")
    if (site / "assets").exists():
        errors.append("Unreviewed source media was copied into the site")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"Documentation passed: {len(pages)} HTML pages, {len(catalog)} source documents, local links/anchors and search coverage")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path)
    check(parser.parse_args().site)
