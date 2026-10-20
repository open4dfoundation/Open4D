# Contributing to Open4D

Participation follows the [Code of Conduct](CODE_OF_CONDUCT.md). Use the
issue forms for bugs and feature requests, and report vulnerabilities as
described in [SECURITY.md](SECURITY.md).

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
python -m pytest tests/codecs/test_research_cpu.py -m 'not gpu'
python -m pytest tests/reconstruction/rgbd tests/io/test_open3d.py
```

## Code

- Keep the base install NumPy-only and import optional dependencies when used.
- Put public mesh codec adapters in `open4d/codecs/<name>`, Gaussian APIs in
  `open4d/gaussians.py`, RGB-D reconstruction in `open4d/reconstruction/rgbd`,
  and TCP frame transport in `open4d/transport`.
- Preserve research implementations and their notices under `open4d/codecs/<name>/source/`.
  Keep built-in reconstruction and its registry in `open4d/reconstruction`;
  connect external methods through separately installable adapters.
- A decoded artifact must work without the original input or hidden encoder
  files. Keep native backends separate from the installed Python package.
- Keep splats separate from `TriangleMesh`, and live iterators separate from
  finite `Sequence` objects.
- Add tests for changed behavior. Put benchmarks in `scripts`; examples and
  tutorials live with the wiki in the website repository.

## Checks

```bash
python -m pytest
python -m compileall -q open4d/*.py open4d/reconstruction/*.py open4d/reconstruction/rgbd/*.py open4d/transport open4d/codecs/*.py open4d/codecs/*/__init__.py open4d/codecs/_backends open4d/core open4d/io open4d/torch_ops open4d/visualization scripts
python scripts/check_markdown_links.py
python scripts/check_provenance.py
python scripts/check_release_gate.py --check-ledger
python -m build
python scripts/check_wheel_contents.py dist/open4d-*.whl
python scripts/check_sdist_contents.py dist/open4d-*.tar.gz
python -m twine check --strict dist/*
```

The wiki, examples and tutorials live in the
[website repository](https://github.com/open4dfoundation/open4dfoundation.github.io)
under `wiki-src/`, which builds the API reference from an Open4D checkout.
Update the wiki and public docstrings with code changes. Research plugin registrations use
`open4d.codecs` and `open4d.reconstruction` entry points; their packages may not
include upstream research code, weights, datasets or binaries without a
separate reviewed distribution basis.

The package checks enforce an explicit file list for wheels and source archives.
The compile check matches CI's supported Python paths; vendored tools such as
Eigen's historical Python 2 maintenance scripts are outside that check.
CI also installs the wheel outside the checkout and tests the installed API.
It separately installs the source archive and exercises the installed extras,
including RGB-D capture loading, reconstruction, OpenUSD, and GIF rendering.
A pull request should describe the behavior changed, relevant tests, and known
limitations.

## Codec experiments

`python -m scripts.codec_rd --source /path/to/frames --config /tmp/experiments.json
--output /tmp/new-run --frames 300 --samples 10000 --geometry-only` trains each
experiment once, then decodes in a fresh process. The JSON configuration is a
list of objects with `id`, `codec`, `seed`, `encode` and `decode` options. Encode
options can include `checkpoint_artifacts`, for example
`{"100": "epoch-100.o4d"}`. Output includes actual artifact bytes, frame timing,
phase durations, sampled process-tree RSS, Torch memory peaks and geometry
metrics. `--resume` reuses matching encoder outputs after verifying logical
inputs and options; it still cold-decodes again.

`python -m scripts.codec_rd.evaluate /tmp/new-run --samples 10000` recomputes
quality and cold-decodes intermediate checkpoints without retraining. Independent
point-set D1/D2 and exact point-to-triangle surface metrics are reported
separately. Sampled Hausdorff is labelled as an estimate. The decoder's source
access check uses Python audit hooks, not an operating-system sandbox. Keep
experiment outputs outside the repository.

## Data and release

Keep datasets, training runs, checkpoints, logs and generated media outside the
repository. See [artifact handling](https://open4dfoundation.github.io/wiki/artifacts/). Small test fixtures need
a known source and license.

Preserve third-party licenses and update [THIRD_PARTY.md](THIRD_PARTY.md) when
adding or changing imported code, binaries, models or data. Do not publish a
package, release or repository bundle while that ledger has unresolved `BLOCK`
entries. The package file list does not grant redistribution rights.
