# Contributing to Open4D

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[dev]'
python -m pytest
```

The base package supports Python 3.10 through 3.13. Tests requiring optional
libraries skip when those libraries are absent. GPU, camera and interactive
viewer tests need their stated environment variables and hardware.

For research and reconstruction tests on Python 3.12:

```bash
python -m pip install -e '.[dev,klt,n4mc,qndf,open3d,gaussians]'
python -m pytest open4d/codec/tests/test_research_cpu.py -m 'not gpu'
python -m pytest open4d/streaming/tests integrations/open3d/tests
```

## Code

- Keep the base install NumPy-only and import optional dependencies when used.
- Put public mesh codec adapters in `open4d/codec`, Gaussian APIs in
  `open4d/gaussians.py`, and transport/reconstruction in `open4d/streaming`.
- Preserve the research implementations under `open4d/codecs` and
  `open4d/reconstruction`. Connect them through small adapters.
- A decoded artifact must work without the original input or hidden encoder
  files. Keep native backends separate from the installed Python package.
- Keep splats separate from `TriangleMesh`, and live iterators separate from
  finite `Sequence` objects.
- Add tests for changed behavior. Keep examples short; put benchmarks in `scripts`.

## Checks

```bash
python -m pytest
python -m compileall -q open4d/*.py open4d/streaming/*.py open4d/codec open4d/core open4d/io open4d/torch_ops open4d/visualization integrations/__init__.py integrations/open3d examples/visualization scripts
python scripts/check_markdown_links.py
python scripts/check_provenance.py
python scripts/check_release_gate.py --expect-blocked
python -m build
python scripts/check_wheel_contents.py dist/open4d-*.whl
python scripts/check_sdist_contents.py dist/open4d-*.tar.gz
```

The package checks enforce an explicit file list for wheels and source archives.
The compile check matches CI's supported Python paths; vendored tools such as
Eigen's historical Python 2 maintenance scripts are outside that check.
CI also installs the wheel outside the checkout and tests the installed API.
A pull request should describe the behavior changed, relevant tests, and known
limitations.

## Data and release

Keep datasets, training runs, checkpoints, logs and generated media outside the
repository. See [artifact handling](docs/artifacts.md). Small test fixtures need
a known source and license.

Preserve third-party licenses and update [THIRD_PARTY.md](THIRD_PARTY.md) when
adding or changing imported code, binaries, models or data. Do not publish a
package, release or repository bundle while that ledger has unresolved `BLOCK`
entries. The package file list does not grant redistribution rights.
