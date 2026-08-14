# Open4D contributor handbook: v0.2-dev

Open4D is a research workspace for **time-varying 3D geometry**: capturing or
reconstructing a scene, representing its frames, compressing it, transporting
or storing it, decoding it, measuring the result, and playing it back. Here,
“4D” means three spatial dimensions changing over time. It does not mean that a
mesh has a mysterious fourth spatial axis.

This snapshot describes commit
`96b8c7bbb48e2a8d231684639cfc57799ca6666d`, audited on 2026-08-13. The short
version of the audit is:

- Open4D already has useful research implementations and a sound small core.
- The common platform is not yet the boundary used by the codecs or
  reconstruction systems.
- No major pipeline meets the project's strict definition of complete yet.
- The fastest path forward is to make one small, reproducible vertical slice,
  then adapt the other methods to the same boundaries.

That audit remains frozen for reproducibility. The live gap between the
audited baseline and the roadmap is tracked in
[current implementation status](implementation-status.md).

> **Complete means:** setup is reproducible, a licensed sample runs end to end,
> automated tests pass, outputs are documented, and the component works through
> its supported Open4D interface.

## What you should understand first

Open4D contains several different jobs that are easy to confuse:

```text
real scene or files
        |
        v
capture / reconstruction  -- makes geometry from camera measurements
        |
        v
representation            -- says what one frame and a sequence mean
        |
        v
compression               -- reduces the bytes needed to store/transmit it
        |
        v
container or transport    -- packages bytes on disk or moves them over a network
        |
        v
decode / playback         -- recovers and displays geometry over time
        |
        v
evaluation                -- measures size, speed, latency, and distortion
```

A codec is not a network protocol. A USD file is not a codec. A TSDF is not a
triangle mesh. A Gaussian-splat renderer is not a mesh viewer. The project will
become coherent by giving these jobs explicit boundaries, not by forcing every
method into one data structure.

## Current maturity

The strongest verified pieces are the NumPy-backed `TriangleMesh`, `Frame`,
finite lazy `Sequence`, example loaders/comparison tools/viewers, and the
Open3D adapter. The recorded audit has 106 passing core/visualization tests and
5 passing Open3D tests on Python 3.12. The codec and RGB-D trees contain useful
standalone pipelines, but none consumes and produces the shared `Sequence`
interface. `apps/` is still a README-only scaffold.

Important cautions before publishing or redistributing anything:

- root packaging metadata says MIT, while discoverable namespace packages and
  imported source include components with GPL, non-commercial, or
  no-redistribution terms;
- the root submodules were uninitialized in the audited checkout;
- tracked historical datasets, results, binaries, and papers make the checkout
  about 1.2 GB;
- Gaussian-splat work is owned by Ryan and is intentionally outside general
  cleanup or refactoring.

See the historical [component status register](status.md) and then the
[current implementation status](implementation-status.md) before relying on a
feature.

## Fast learning path

Read these in order if you want to contribute heavily:

1. [3D/4D primer](primer.md) — the vocabulary and representations.
2. [System architecture](architecture.md) — how the jobs should connect and how
   they connect today.
3. [Repository and feature map](repository-map.md) — where each job lives.
4. [Core, I/O, USD, and metrics](platform.md) — the shared boundary new work
   should use.
5. [Codec guide](codecs.md) — what every compression approach is trying to do.
6. [Reconstruction and streaming](reconstruction-streaming.md) — cameras,
   TSDF fusion, transports, Unity, and Gaussian splats.
7. [Component status](status.md) — evidence at the audited baseline.
8. [Current implementation status](implementation-status.md) — what exists in
   the working branch and the ranked remaining work.
9. [Contributing and reproducibility](contributing.md) — environments, tests,
   artifacts, review rules, and ownership.
10. [Dependency-ordered roadmap](roadmap.md) — the prioritized 90-day plan and
   post-90-day backlog.
11. [Glossary](glossary.md) — a quick reference while reading code and papers.

## First commands

Clone with submodules if you intend to build codecs; the lightweight core does
not require them:

```bash
git clone --recurse-submodules https://github.com/open4dfoundation/Open4D.git
cd Open4D
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e .
python -m pytest -q open4d/core/tests examples/visualization/tests
```

The default interpreter support advertised by the package is Python 3.10–3.13.
The shared codec environment is Python 3.12 because Open3D 0.19 has no Python
3.13 wheel. Install a viewer only when you have a graphical session:

```bash
python -m pip install -e '.[player]'
python examples/visualization/visualize_sequence.py path/to/frames --info
```

Sequence loaders currently live under `examples/visualization`; the proposed
`open4d.io.open_sequence` public API is roadmap work and is not implemented in
the audited baseline.

## How to choose a first contribution

Use the [roadmap](roadmap.md), not directory size, to choose work. Good early
contributions strengthen shared contracts, tests, packaging, provenance, and a
small Draco vertical slice. Avoid broad refactors inside research trees. In
particular, do not change `open4d/reconstruction/queen/`,
`open4d/reconstruction/3dgstream/`, or define a Gaussian representation without
Ryan's review.

If you only remember one design rule, remember this one:

```text
finite files/decoders -> FrameProvider -> Sequence -> metrics/viewers/adapters
live capture          -> future LiveSource -> recording/snapshot -> Sequence
```

Keeping the finite and live contracts separate prevents timing, buffering, and
failure semantics from leaking into a simple random-access container.
