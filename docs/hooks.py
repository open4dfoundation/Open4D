"""Build wiki and API reference from existing sources, without saved reports.

Only first-party docs and docstrings enter the site. Documentation media stays
excluded until its rights have been reviewed in THIRD_PARTY.md.
"""

from __future__ import annotations

import importlib
import inspect
from pathlib import Path
import re
from urllib.parse import urlsplit

from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[1]
REPO = "https://github.com/open4dfoundation/Open4D/blob/main/"
LINK = re.compile(r"(!?\[[^\]]*\]\()([^\s)]+)(\))")
PUBLIC = {
    "open4d": ["load", "save", "encode", "decode", "reconstruct", "stream", "send", "receive", "visualize", "available_codecs"],
    "open4d.core": ["Sequence", "Frame", "TriangleMesh", "PointCloud", "GaussianCloud"],
    "open4d.codec": ["register_codec", "inspect_o4d", "pack_o4d", "unpack_o4d"],
    "open4d.reconstruction": ["available_methods", "MultiviewCapture"],
    "open4d.reconstruction.rgbd": ["RGBDCapture", "load_capture", "reconstruct"],
    "open4d.data": ["DatasetSpec", "DownloadableDataset", "SyntheticMeshSequence", "SyntheticGaussianSequence"],
    "open4d.visualization": ["Visualizer", "ViewerOptions", "render_gif"],
    "open4d.ml": ["train", "TrainingHistory"],
}


def _source_links(markdown: str, source: Path) -> str:
    """Keep wiki links local and route repository-only files to source."""
    def replace(match):
        prefix, target, suffix = match.groups()
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            return match.group(0)
        local = (source.parent / parsed.path).resolve()
        if local.parent == ROOT / "docs" and local.suffix == ".md":
            return prefix + local.name + ("#" + parsed.fragment if parsed.fragment else "") + suffix
        try:
            relative = local.relative_to(ROOT).as_posix()
        except ValueError:
            return match.group(0)
        return prefix + REPO + relative + ("#" + parsed.fragment if parsed.fragment else "") + suffix
    return LINK.sub(replace, markdown)


def on_files(files, config):
    """Add virtual pages only to MkDocs' build inventory."""
    overview = _source_links((ROOT / "README.md").read_text(), ROOT / "README.md")
    files.append(File.generated(config, "index.md", content=overview))
    licenses = _source_links((ROOT / "THIRD_PARTY.md").read_text(), ROOT / "THIRD_PARTY.md")
    files.append(File.generated(config, "licenses.md", content=licenses))
    reference = ["# API reference", "", "Generated from the public Python signatures and docstrings in this revision.", ""]
    for module_name, names in PUBLIC.items():
        module = importlib.import_module(module_name)
        reference.extend([f"## {module_name}", ""])
        for name in names:
            obj = getattr(module, name)
            try:
                signature = str(inspect.signature(obj))
            except (TypeError, ValueError):
                signature = ""
            reference.extend([f"### {name}", "", f"```python\n{module_name}.{name}{signature}\n```", "", inspect.getdoc(obj) or "", ""])
            if inspect.isclass(obj):
                for method_name in ("from_folder", "from_colmap", "create_window", "add_geometry", "update_geometry", "poll_events", "destroy_window", "stream", "close"):
                    method = getattr(obj, method_name, None)
                    if method is not None:
                        reference.extend([f"#### {name}.{method_name}", "", f"```python\n{method_name}{inspect.signature(method)}\n```", "", inspect.getdoc(method) or "", ""])
    files.append(File.generated(config, "reference.md", content="\n".join(reference)))
    return files


def on_page_markdown(markdown, page, config, files):
    source = ROOT / "docs" / page.file.src_path
    return _source_links(markdown, source)


def on_page_context(context, page, config, nav):
    import open4d
    context["open4d_version"] = open4d.__version__
    if page.file.src_path == "index.md":
        page.edit_url = REPO + "README.md"
    elif page.file.src_path == "licenses.md":
        page.edit_url = REPO + "THIRD_PARTY.md"
    elif page.file.src_path == "reference.md":
        page.edit_url = REPO + "open4d/__init__.py"
    return context
