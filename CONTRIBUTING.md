# Contributing to Open4D

Open4D combines a lightweight Python package with independent research
systems. A component is complete only when setup is reproducible, a licensed
sample runs end to end, automated tests pass, outputs are documented, and the
component works through its supported Open4D interface.

## Release safety

Do not publish a PyPI package, GitHub release, container, dataset, model,
native plugin, or repository bundle while `THIRD_PARTY.md` contains unresolved
`BLOCK` entries. The explicit Python package list is technical containment; it
is not legal approval to publish.

## Contribution lanes

- **Lightweight platform:** `open4d.core`, `open4d.torch_ops`, the Open3D
  adapter, examples, documentation, and CPU tests.
- **Research codecs:** each directory under `open4d/codecs/` is an independent
  experimental environment. Preserve the paper implementation and integrate
  through narrow adapters.
- **RGB-D reconstruction and transport:** hardware, CUDA, and network work
  under `open4d/reconstruction/rgbd/`. Prefer synthetic or recorded tests.
- **Gaussian reconstruction:** treat the imported research directories as
  isolated components and avoid unrelated refactors.
- **Unity/XR:** experimental native integration with unresolved binary and
  fixture provenance.

Do not combine unrelated lanes in one pull request or opportunistically clean
up copied research code.

## Lightweight setup

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e '.[dev]'
python -m pytest
```

The base package supports Python 3.10–3.13. Open3D is tested on 3.10–3.12.
Optional test tiers are explicit:

```bash
python -m pip install -e '.[dev,open3d]'
python -m pytest -m open3d integrations/open3d/tests

python -m pip install -e '.[dev,torch]'
python -m pytest -m torch open4d/torch_ops/tests
```

Registered markers are `cpu`, `open3d`, `torch`, `gpu`, `hardware`, and
`slow`. Tests needing GPUs, physical hardware, local datasets, or graphical
sessions must not run in the default tier.

## Required checks

Run the checks relevant to your change:

```bash
python -m pytest
python -m compileall -q open4d integrations examples/visualization scripts
bash -n scripts/*.sh
python scripts/check_markdown_links.py
python scripts/check_provenance.py
python scripts/check_release_gate.py --expect-blocked
python -m build
python scripts/check_wheel_contents.py dist/open4d-*.whl
python scripts/check_sdist_contents.py dist/open4d-*.tar.gz
```

## Public boundary rules

- Keep the base install NumPy-only and import optional dependencies lazily.
- Preserve `TriangleMesh -> Frame -> FrameProvider -> Sequence` layering and
  canonical dtype normalization.
- Do not call a codec artifact self-contained until it decodes in a fresh
  process without original geometry or undeclared encoder intermediates.
- Keep finite `Sequence` separate from future live-stream semantics.
- Report payload, container, filesystem, and wire bytes separately.
- Do not force point clouds, TSDF volumes, or Gaussian splats into
  `TriangleMesh`.

## Data and provenance

Follow [`docs/artifacts.md`](docs/artifacts.md). Do not add datasets, training
runs, checkpoints, decoded geometry, logs, or generated videos. A deliberately
small fixture needs a known license, provenance, checksum, and a test that
requires it.

Any copied code, submodule, model, dataset, binary, paper, or media addition
must update [`THIRD_PARTY.md`](THIRD_PARTY.md) with its immutable source,
revision, modifications, license, distribution constraints, and release
decision.

## Pull-request evidence

A pull request must state the behavior changed, commands and environment used,
input/output formats, compatibility impact, provenance impact, and remaining
limitations. Documentation status claims must name their commit, audit date,
environment, and verification command.
