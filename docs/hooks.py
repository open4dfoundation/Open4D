"""Build the website from versioned guides, executable tutorials and source links.

Markdown stays versioned with its owning component. Every source document has a
site page or a catalog link; upstream documentation and unreviewed media remain
at their original source under their own terms.
"""
from __future__ import annotations

import ast
import configparser
import importlib
import inspect
import json
import posixpath
from pathlib import Path
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit

from mkdocs.structure.files import File
try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib

ROOT = Path(__file__).resolve().parents[1]
REPO = "https://github.com/open4dfoundation/Open4D/blob/main/"
LINK = re.compile(r"(!?\[[^\]]*\]\()(<[^>]+>|[^\s)]+)(\))")
SOURCE_ROUTES = {}
PAGE_SOURCES = {}
GENERATED = set()
SUBMODULES = {}
PUBLIC = {
    "open4d": ["load", "save", "encode", "decode", "reconstruct", "stream", "send", "receive", "visualize", "available_codecs"],
    "open4d.core": ["Sequence", "Frame", "TriangleMesh", "PointCloud", "GaussianCloud"],
    "open4d.codec": ["codec_info", "register_codec", "inspect_o4d", "pack_o4d", "unpack_o4d"],
    "open4d.reconstruction": ["available_methods", "MultiviewCapture"],
    "open4d.reconstruction.rgbd": ["RGBDCapture", "load_capture", "reconstruct"],
    "open4d.data": ["DatasetSpec", "DownloadableDataset", "SyntheticMeshSequence", "SyntheticGaussianSequence"],
    "open4d.visualization": ["Visualizer", "ViewerOptions", "render_gif"],
    "open4d.ml": ["train", "TrainingHistory"],
}
GUIDES = {
    "README.md": "overview.md", "THIRD_PARTY.md": "licenses.md",
    "CONTRIBUTING.md": "community/contributing.md", "CODE_OF_CONDUCT.md": "community/code-of-conduct.md",
    "SECURITY.md": "community/security.md", "CHANGELOG.md": "community/changelog.md",
    "open4d/reconstruction/rgbd/README.md": "guides/rgbd.md",
    "open4d/streamer/README.md": "guides/browser-streaming.md",
    "open4d/integrations/open3d/README.md": "guides/open3d.md",
    "integrations/unity/README.md": "guides/unity.md",
    "examples/visualization/README.md": "guides/visualization.md",
}
TUTORIALS = {
    "core_roundtrip": ("core-roundtrip", "Mesh, point clouds and Gaussians"),
    "data_cache": ("data-cache", "Cache and download licensed data"),
    "rgbd_capture": ("rgbd-capture", "Calibrated RGB-D reconstruction"),
    "train_generic": ("generic-training", "Train a generic model"),
    "tcp_stream": ("tcp-stream", "Send and record a TCP stream"),
    "browser_stream": ("browser-stream", "Serve a browser bundle"),
    "compare_visualization": ("visualization", "Compare a decoded sequence"),
}
CODEC_SETUPS = {
    "klt": ("Mesh", "OPEN4D_KLT_ROOT", "CUDA is optional; CPU is supported.", "Acquire the KLT source and install the adapter's runtime extra. Use codec='klt' on a mesh Sequence; configure resolution and num_components explicitly for your quality target."),
    "n4mc": ("Mesh", "OPEN4D_N4MC_ROOT", "CPU or CUDA; Metal convolution support is limited.", "Acquire the N4MC source and install the runtime extra. encode trains the shared TSDF model from the supplied mesh sequence; epochs, resolution and device are caller choices. No checkpoint is bundled."),
    "qndf": ("Mesh", "OPEN4D_QNDF_ROOT / OPEN4D_QNDF_INT8_ROOT; OPEN4D_RESEARCH_ROOT", "CPU or CUDA, with Open3D.", "Acquire QNDF under its original terms and install the runtime extra. The adapter carries coarse meshes and displacement models. qndf-int8 uses this same adapter package with the separate qndf_int8 source tree and quantized weights."),
    "vdmc": ("Mesh", "OPEN4D_VDMC_ENCODER / OPEN4D_VDMC_DECODER", "Configured native executables are required.", "Build the pinned V-DMC checkout or use independently acquired compatible binaries. Set encoder and decoder paths, then select codec='vdmc'. O4D retains the native sequence.vmesh payload; it is a custom container, not general MPEG interchange."),
    "faster_vdmc": ("Mesh", "OPEN4D_FASTER_VDMC_ENCODER / OPEN4D_FASTER_VDMC_DECODER", "Configured native executables are required.", "Build the pinned Faster V-DMC checkout. Configure its own encoder and decoder paths and select codec='faster_vdmc'; do not silently substitute the ordinary V-DMC binaries."),
    "tvmc": ("Mesh", "OPEN4D_TVMC_ROOT / OPEN4D_TVMC_PYTHON; DRACO_ENCODER / DRACO_DECODER", "Native Draco tools and the licensed tracker runtime are required.", "Acquire TVMC independently under its redistribution restrictions, build its recorded tracker/Draco tooling, and configure the native programs. Use OPEN4D_NATIVE_TIMEOUT to bound execution. Geometry-only codec output does not promise UV/image preservation."),
    "tsmc": ("Mesh", "OPEN4D_TSMC_ROOT / OPEN4D_TSMC_PYTHON; DRACO_ENCODER / DRACO_DECODER", "Native tools and a prepared tracker runtime are required.", "Acquire and prepare the pinned TSMC source and native tools. The runtime's optional tracking dependencies are separate from the MIT registration package. Check the backend guide for encoder/decoder configuration and bounded native execution."),
    "vega": ("Native Gaussian model", "OPEN4D_VEGA_ROOT / OPEN4D_VEGA_PYTHON; runtime= (Vega directory), python=", "The native model and its CUDA renderer are optional dependencies.", "Import an existing Vega run or explicitly train using the acquired runtime. Preserve native Gaussian geometry and view-dependent color instead of describing an array export as neural compression. Use native.decode with the configured runtime."),
    "queen": ("Native Gaussian model", "OPEN4D_GS_ROOT / OPEN4D_QUEEN_PYTHON; OPEN4D_QUEEN_ROOT for native.decode", "CUDA reconstruction/training uses separately acquired QUEEN and gs_tools.", "Install the registration adapter, acquire the QUEEN runtime under NVIDIA's terms, and prepare a calibrated capture. For reconstruction, runtime= points to gs_tools with queen as its sibling; native.decode runtime= points to queen itself. reconstruct(method='queen') trains into an owned output directory or O4D; import_native packages an existing run. No weights are downloaded implicitly."),
    "3dgstream": ("Native Gaussian model", "OPEN4D_GS_ROOT / OPEN4D_3DGSTREAM_PYTHON; OPEN4D_3DGSTREAM_ROOT for native.decode", "CUDA reconstruction/training uses separately acquired 3DGStream and gs_tools.", "For reconstruction, runtime= points to gs_tools with 3dgstream as its sibling; native.decode runtime= points to 3dgstream itself. Prepare the recorded native runtime and calibrated capture, then use reconstruct(method='3dgstream') or import_native on a genuine run. The browser bundle is a delivery representation, distinct from the compressed native model."),
    "rerf": ("Native radiance field", "OPEN4D_RERF_ROOT / OPEN4D_RERF_PYTHON; runtime=, python=", "The acquired NeVo/ReRF runtime may use a separate Python/CUDA environment.", "Import a genuine trained ReRF/NeVo run, then decode with its selected runtime and interpreter. A radiance field is not a Gaussian cloud. A learned ORBIT model and native decode remain required for real-data validation; an analytic fixture is not such evidence."),
}
HOME = '''<div class="home-intro"><div>
<h1>4D geometry.<br>One Python workflow.</h1>
<p>Load, encode, reconstruct and stream mesh, point-cloud and Gaussian sequences. Start with the NumPy core, then add the methods your project needs.</p>
<p><a href="tutorials/core-roundtrip/">Run your first tutorial</a> &nbsp; <a href="requirements/">Installation guide</a></p>
</div><div class="home-code"><p>A synthetic mesh sequence, without a dataset download.</p>
<pre><code class="language-python">import open4d as o4d

sample = o4d.data.SyntheticMeshSequence()
with o4d.load(sample.path) as sequence:
    o4d.encode(sequence, "motion.o4d", codec="npz")</code></pre>
</div></div>
<h2 id="choose-your-workflow">Choose your workflow</h2>
<div class="home-paths">
<a href="tutorials/"><h3>Learn by running</h3><p>Seven executable tutorials, from a lossless roundtrip to calibrated reconstruction, model training and live delivery.</p></a>
<a href="codecs/"><h3>Choose a codec</h3><p>Core NumPy profiles and setup guides for every separately installable research adapter.</p></a>
<a href="guides/rgbd/"><h3>Reconstruct a capture</h3><p>Prepare synchronized, calibrated RGB-D inputs or configure a native Gaussian reconstruction method.</p></a>
<a href="guides/visualization/"><h3>Inspect and compare</h3><p>Frame playback, explicit textures and a shared-camera view of a baseline and decoded sequence.</p></a>
<a href="guides/browser-streaming/"><h3>Deliver a sequence</h3><p>Decoded mesh frames over TCP, or adaptive browser bundles through the optional streamer package.</p></a>
<a href="library/"><h3>Explore the wiki</h3><p>Find all source documentation, integration guides, upstream manuals and project policies.</p></a>
</div>
<div class="home-note">Version 0.3 is a release candidate. Downloadable core and adapter archives are built in CI; research runtimes, datasets and weights retain their own terms. <a href="licenses/">Check licenses and release status.</a></div>
<h2 id="reference">Keep the reference close</h2>
<p><a href="api/">Python guide</a> · <a href="reference/">Generated API reference</a> · <a href="artifacts/">Datasets and models</a> · <a href="layout/">Repository layout</a> · <a href="community/contributing/">Contribute</a></p>
'''


def source_documents():
    """Enumerate tracked Markdown, or the same source tree in a git archive."""
    result = subprocess.run(["git", "ls-files", "-z", "--", "*.md"], cwd=ROOT, capture_output=True, text=True)
    names = result.stdout.split("\0") if result.returncode == 0 else [p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*.md")]
    ignored = {".git", ".codex", ".impeccable", "node_modules", "__pycache__", ".venv", ".pytest_cache", "build", "dist"}
    return sorted({name for name in names if name and (ROOT / name).is_file() and not (set(Path(name).parts) & ignored) and Path(name).name != "AGENTS.md"})


def _source_url(path):
    for location, item in SUBMODULES.items():
        if path == location or path.startswith(location + "/"):
            suffix = path[len(location):].lstrip("/")
            kind = "blob" if suffix else "tree"
            return f"{item['repository']}/{kind}/{item['revision']}/" + quote(suffix, safe="/")
    return REPO + quote(path, safe="/")


def _submodules():
    metadata = configparser.ConfigParser()
    metadata.read(ROOT / ".gitmodules")
    result = subprocess.run(["git", "ls-files", "--stage"], cwd=ROOT, capture_output=True, text=True, check=True)
    pins = {}
    for line in result.stdout.splitlines():
        mode, revision, rest = line.split(" ", 2)
        if mode == "160000":
            pins[rest.split("\t", 1)[1]] = revision
    return {metadata[section]["path"]: {"repository": metadata[section]["url"].removesuffix(".git"), "revision": pins[metadata[section]["path"]]} for section in metadata.sections()}


def _source_links(markdown, source, page_route=None):
    def replace(match):
        prefix, target, suffix = match.groups()
        parsed = urlsplit(target.strip("<>"))
        if parsed.scheme or parsed.netloc or not parsed.path:
            return match.group(0)
        local = (source.parent / unquote(parsed.path)).resolve()
        try:
            relative = local.relative_to(ROOT).as_posix()
        except ValueError:
            return match.group(0)
        route = SOURCE_ROUTES.get(relative)
        if route is None:
            try:
                candidate = local.relative_to(ROOT / "docs").as_posix()
            except ValueError:
                candidate = None
            if candidate in GENERATED:
                route = candidate
        if route and not prefix.startswith("!"):
            origin = posixpath.dirname(page_route or SOURCE_ROUTES.get(source.relative_to(ROOT).as_posix(), "index.md")) or "."
            url = posixpath.relpath(route, origin)
        else:
            url = _source_url(relative)
        if parsed.query:
            url += "?" + parsed.query
        if parsed.fragment:
            url += "#" + parsed.fragment
        # Preserve access to media without redistributing unreviewed assets.
        if prefix.startswith("!"):
            prefix = prefix[1:]
        return prefix + url + suffix
    return LINK.sub(replace, markdown)


def _reference():
    lines = ["# API reference", "", "Generated from public signatures and docstrings in this revision.", ""]
    for module_name, names in PUBLIC.items():
        module = importlib.import_module(module_name)
        lines += [f"## {module_name}", ""]
        for name in names:
            obj = getattr(module, name)
            try:
                signature = str(inspect.signature(obj))
            except (TypeError, ValueError):
                signature = ""
            lines += [f"### {name}", "", f"```python\n{module_name}.{name}{signature}\n```", "", inspect.getdoc(obj) or "", ""]
            if inspect.isclass(obj):
                for method_name in ("frame", "from_folder", "from_colmap", "create_window", "add_geometry", "update_geometry", "poll_events", "destroy_window", "stream", "close"):
                    method = getattr(obj, method_name, None)
                    if method is not None:
                        lines += [f"#### {name}.{method_name}", "", f"```python\n{method_name}{inspect.signature(method)}\n```", "", inspect.getdoc(method) or "", ""]
    return "\n".join(lines)


def on_files(files, config):
    global REPO
    ref = __import__("os").environ.get("GITHUB_HEAD_REF") or __import__("os").environ.get("GITHUB_SHA")
    if not ref:
        result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        ref = result.stdout.strip() if result.returncode == 0 else "main"
    REPO = config.repo_url.rstrip("/") + "/blob/" + ref + "/"
    SOURCE_ROUTES.clear(); PAGE_SOURCES.clear(); GENERATED.clear()
    SUBMODULES.clear(); SUBMODULES.update(_submodules())
    SOURCE_ROUTES.update({f"docs/{p.name}": p.name for p in (ROOT / "docs").glob("*.md")})
    SOURCE_ROUTES.update(GUIDES)
    def add(route, text, source=None):
        GENERATED.add(route)
        if source:
            PAGE_SOURCES[route] = ROOT / source
        files.append(File.generated(config, route, content=text))
    add("index.md", HOME)
    for source, route in GUIDES.items():
        if not (ROOT / source).is_file():
            raise ValueError(f"Missing documentation source: {source}")
        add(route, (ROOT / source).read_text(), source)
    add("reference.md", _reference())
    tutorial_index = ["# Tutorials", "", "Run these examples in a fresh environment. The first tutorial needs only the NumPy core; the later guides make every optional dependency, display, download and runtime explicit.", "", "| Tutorial | Runnable source |", "| --- | --- |"]
    for module, (slug, title) in TUTORIALS.items():
        path = f"examples/tutorials/{module}.py"
        source = (ROOT / path).read_text()
        doc = ast.get_docstring(ast.parse(source)) or ""
        add(f"tutorials/{slug}.md", f"# {title}\n\n{doc}\n\n## Run and inspect\n\n[View the runnable Python source]({_source_url(path)}). Run `--help` for the command-line options.\n\n## Complete example\n\n```python\n{source}\n```\n", path)
        tutorial_index.append(f"| [{title}]({slug}.md) | [{module}.py]({_source_url(path)}) |")
    tutorial_index += ["", "## More examples", "", "The existing [visualization examples](../guides/visualization.md) cover frame sources, comparisons, color maps and OpenUSD. The [Python guide](../api.md#tutorials) connects these workflows to the API."]
    add("tutorials/index.md", "\n".join(tutorial_index))
    codec_index = ["# Codecs and reconstruction methods", "", "The NumPy core works without research implementations. Install a selected adapter and independently acquire its runtime under the original terms. An adapter registration is not a backend, a dataset or a model checkpoint.", "", "## Core NumPy profiles", "", "`npz`, `raw`, `deflate`, `bzip2`, `lzma` and `rle` preserve mesh, point-cloud and Gaussian frame arrays. Start with the [core roundtrip tutorial](../tutorials/core-roundtrip.md); see the [O4D format](../api.md#o4d-format) for versioning and bounds.", "", "## Research adapters", "", "| Adapter | Representation | Setup guide |", "| --- | --- | --- |"]
    for name, (representation, variable, device, instructions) in CODEC_SETUPS.items():
        metadata = tomllib.loads((ROOT / f"plugins/{name}/pyproject.toml").read_text())
        project = metadata["project"]
        identifiers = list(project.get("entry-points", {}).get("open4d.codecs", {}))
        extras = project.get("optional-dependencies", {}).get("runtime", [])
        backend = f"codecs/{name}/README.md"
        if not (ROOT / backend).is_file():
            backend = f"codecs/{name}"
        content = f"# {name}\n\n## Install the adapter\n\nInstall the matching core candidate, then the downloaded `{project['name']}` wheel, or install from the checkout:\n\n```bash\npython -m pip install ./plugins/{name}\n```\n\nAdapter identifiers: {', '.join('`'+i+'`' for i in identifiers)}. The package contains MIT registration metadata; it does not contain the external runtime or weights.\n\n## Prepare the runtime\n\n{instructions}\n\nConfiguration: `{variable}`. {device}\n\n"
        if extras:
            content += f"For declared optional Python requirements use `python -m pip install './plugins/{name}[runtime]'`. Native programs and upstream source remain a separate installation.\n\n"
        content += f"## Backend guide and terms\n\n[Open the complete backend source guide]({_source_url(backend)}) and [review the component ledger](../licenses.md) before acquiring or redistributing it. [Installation and native builds](../requirements.md) documents shared environments and source-root overrides; the [Python codec guide](../api.md#codecs) documents selection and output types.\n"
        add(f"codecs/{name}.md", content)
        codec_index.append(f"| `{project['name']}` | {representation} | [{name}]({name}.md) |")
    codec_index += ["", "QUEEN and 3DGStream also register reconstruction methods. The built-in `rgbd` method accepts the `tsdf` alias; its [capture guide](../guides/rgbd.md) requires calibration, synchronization and aligned images. `ml` contains shared training utilities; neural encoders remain codecs."]
    add("codecs/index.md", "\n".join(codec_index))
    layout = """# Repository layout

The Python namespace contains the interoperable API. Research implementations live outside that namespace and remain excluded from the core wheel and source distribution.

```text
.github/                 CI, checks and documentation deployment
open4d/                  distributable Python library
  codec/                 O4D wrappers and plugin interfaces
  core/                  meshes, point clouds, Gaussians and sequences
  io/                    geometry and sequence readers/writers
  reconstruction/        registry, calibrated captures and built-in RGB-D
  streamer/              separately distributed browser streamer
  torch_ops/             shared tensor operations
  ml/                    generic user-model training utilities
  transport/             decoded-array TCP transport
  visualization/         playback, comparisons and live viewer
  integrations/          Open3D interoperability
codecs/                  independently acquired research source and native tools
plugins/                 separately installable MIT registration packages
scripts/                 builds, validation and development tools
tests/                   first-party Python unit/integration tests and fixtures
examples/                runnable tutorials, notebooks and visualizer examples
docs/                    versioned documentation sources and website theme
integrations/            Unity examples and their dependencies
```

Folders are ordinary visible directories; `torch_ops` remains a valid Python package name. Existing public modules such as `metrics`, `gaussians`, `native` and `orbit` remain available. Upstream tests and the browser research-study harness remain with their component, so their original setup and license context is preserved. Local untracked experiments are not part of a distribution.

## Documentation ownership

The homepage, wiki index, tutorial pages, adapter setup pages and API reference are generated during the documentation build. Guides stay versioned with the code in `docs/` or their owning component; they are not silently removed after appearing on the website. [All source Markdown](library/index.md) remains accessible. Root README and project policies remain available to package users and GitHub contributors.
"""
    add("layout.md", layout)
    documents = source_documents()
    rows = ["# Documentation library", "", "Every Markdown document tracked in this repository is accounted for here. Project-authored guides are readable on the site; research and vendored manuals remain linked to their source under their own terms. Upstream submodule documentation is linked at its pinned revision below. No useful guide or license is discarded to simplify navigation.", "", "## Project guides", "", "| Document | Read |", "| --- | --- |"]
    records = []
    for path in documents:
        route = SOURCE_ROUTES.get(path)
        records.append({"source": path, "route": route, "url": _source_url(path), "mode": "page" if route else "source"})
        if route:
            rows.append(f"| `{path}` | [Read guide]({posixpath.relpath(route, 'library')}) |")
    rows += ["", "## Research, upstream and component documentation", "", "These links include setup notes, integration manuals and license notices. Linked content is not republished or granted new rights by this website.", "", "| Source document | Original location |", "| --- | --- |"]
    for record in records:
        if not record["route"]:
            rows.append(f"| `{record['source']}` | [View source]({record['url']}) |")
    rows += ["", "## Pinned upstream manuals", "", "These directories contain the full upstream documentation at the exact source revision used by this checkout. They are linked rather than copied into the MIT documentation build.", "", "| Component directory | Upstream documentation |", "| --- | --- |"]
    for path, item in SUBMODULES.items():
        rows.append(f"| `{path}` | [Browse revision `{item['revision'][:12]}`]({_source_url(path)}) |")
    add("library/index.md", "\n".join(rows))
    add("documentation.json", json.dumps({"schema": 1, "documents": records, "submodules": SUBMODULES}, indent=2))
    return files


def on_page_markdown(markdown, page, config, files):
    source = PAGE_SOURCES.get(page.file.src_uri, ROOT / "docs" / page.file.src_uri)
    return _source_links(markdown, source, page.file.src_uri)


def on_page_context(context, page, config, nav):
    import open4d
    context["open4d_version"] = open4d.__version__
    context["open4d_home"] = page.file.src_uri == "index.md"
    source = PAGE_SOURCES.get(page.file.src_uri)
    if source:
        page.edit_url = _source_url(source.relative_to(ROOT).as_posix())
    elif page.file.src_uri in GENERATED:
        page.edit_url = _source_url("docs/hooks.py")
    return context
