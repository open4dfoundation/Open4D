# Changelog

Open4D follows [Semantic Versioning](https://semver.org/). Before 1.0, minor
versions may change the public API.

## [0.2.0] - Unreleased

### Added

- Whole-sequence API: `open4d.load`, `save`, `unload`, `encode`, `decode`,
  `reconstruct`, `stream`, `send`, `receive` and `visualize`.
- Sequence I/O for PLY and OBJ frame folders, single meshes and OpenUSD
  (`.usd`, `.usda`, `.usdc`, `.usdz`) through `open4d.io.open_sequence`,
  `write_sequence` and `inspect_sequence`. Exported folders carry a versioned
  `open4d.sequence.json` with timestamps, metadata and topology declarations.
- VMESH, a standalone container for native codec payloads with per-codec
  profiles, SHA-256 payload hashes and sequence timing. `inspect_vmesh`,
  `pack_vmesh` and `unpack_vmesh` work without decoding. A custom `VMESH`
  USD prim preserves a compressed artifact exactly inside `.usdc`.
- Twelve public codecs, listed by `open4d.available_codecs()`: `vdmc`,
  `faster_vdmc`, `tvmc`, `tsmc`, `klt`, `n4mc`, `qndf`, `qndf-int8`, `vega`,
  `queen`, `3dgstream` and `rerf`. `faster_vdmc` is a new submodule with
  parallel encoding and faster deterministic decoding.
- `open4d.NativeSequence` and `open4d.import_native` for Gaussian and
  neural-field methods, which keep their native compressed models.
- `open4d.migrate_legacy` converts older research exports and packed browser
  clips to VMESH.
- Point clouds and Gaussians as frame types: `PointCloud`, `GaussianCloud`,
  `GaussianSplats`, `load_gaussians`, `NeuralGaussianFrame` and
  `open4d.Representation`.
- Gaussian reconstruction with `open4d.reconstruct(..., method="queen")` or
  `method="3dgstream"` through the shared `gs_tools` runtime, returning a
  `GaussianRun`. ORBIT captures load with `open4d.load_orbit`.
- RGB-D reconstruction from depth arrays or saved two-camera captures
  (`open4d.load_rgbd_capture`, which works from an installed wheel), with
  optional pose refinement on CUDA. The default CPU reconstruction is
  bit-identical across runs and processes.
- `open4d.compare_meshes` and `open4d.compare_sequences` (point-to-point and
  point-to-plane error, Hausdorff distance and PSNR) with the `metrics` extra.
  `compare_sequences` also accepts paths, such as a frame folder and a `.vmesh`.
- Decoded-mesh TCP transport (`send`/`receive`) and browser streaming through
  the separate `open4d-streamer` package: bundles, quality ladders, Draco
  rungs, scoring, link shaping, monitoring and a Gaussian player.
- `Receiver.record()` collects a TCP stream into a `Sequence` for
  `open4d.save`, `Receiver.stats` reports frames, bytes, frame rate and bit
  rate, and `Receiver.close()` stops a waiting receiver from another thread.
- Vega and ReRF baselines under `open4d/reconstruction`.
- The `open4d` command (`demo`, `inspect`, `view`) and `python -m open4d`.
  `open4d inspect` reads any `.vmesh`, or VMESH prim in USD, from its header
  without a codec backend; `--decode` also reports mesh geometry.
- A browser user-study app for comparing streaming methods under one network
  trace (`open4d/streamer/study`).
- Notebooks for the codec cycle (`examples/open4d_sequence_codec.ipynb`,
  `examples/vmesh/`) and a scored streaming example.
- Optional extras: `tools`, `metrics`, `player`, `open3d`, `usd`, `torch`,
  `klt`, `n4mc`, `qndf`, `gaussians`, `capture` and `all`.
- Release safety: an explicit package list with wheel and source-archive
  content checks, a third-party provenance ledger, a manual release gate, and
  CI for Python 3.10 to 3.13 on Linux, macOS and Windows.
- GitHub issue forms, a Code of Conduct, a trusted-publishing workflow for
  TestPyPI and PyPI that refuses to run while the release gate is blocked, and
  a single `CI passed` check for branch protection.

### Changed

- Meshes are stored in canonical dtypes (`float32` positions, `uint32`
  triangles) rather than the producer's.
- The base install needs only NumPy; codecs, viewers and readers import their
  optional dependencies when used and name the extra to install.
- The default TCP stream address is `127.0.0.1:47004`. Pass `port=7000` to
  talk to a 0.1 receiver.
- `open4d.stream` sends to the browser streamer for paths, Gaussian inputs and
  browser options, and keeps TCP behavior for frame iterables and explicit
  host/port arguments.
- RGB-D code moved to `open4d/reconstruction/rgbd`, TCP transport to
  `open4d/transport`, and the browser study to `open4d/streamer/study`.
- Research codecs that run in Python are loaded from a source checkout; set
  `OPEN4D_RESEARCH_ROOT` when using an installed package.
- PyTorch3D is no longer required.

### Fixed

- The TCP receiver rejects malformed dtype strings, deeply nested headers and
  non-finite metadata with `ValueError` instead of leaking other exceptions,
  reports a sender reset as `EOFError`, and refuses a second sender instead of
  silently dropping its frames.

- The OBP1 capture protocol raises `ProtocolError` for out-of-range fields and
  non-ASCII serials instead of `struct.error` or `UnicodeEncodeError`, and
  `encode_ack` keeps an explicit timestamp of zero.

### Removed

- The `apps/` placeholder, the NeVo baseline (replaced by ReRF) and the
  dataset download script.
- Draco from the public codec choices. It remains a streaming rung and a
  research baseline.
- Historical N4MC training outputs and generated documentation.

## [0.1.0] - 2026-08-05

### Added

- The core data model: `Frame`, `TriangleMesh`, `Sequence`, `SequenceView`,
  `FrameProvider`, `MemoryFrameProvider` and `TopologyMode`.
- Example scripts under `examples/visualization` that read PLY, OBJ and USD
  frame folders and play them in a Qt viewer with GIF export.
- Research codec trees with their own environments and scripts: TVMC, TSMC,
  N4MC, QNDF, QNDF-INT8, KLT, Draco and the MPEG V-DMC reference submodule.
- RGB-D capture and reconstruction programs and an Open3D integration.
- A Unity integration with a C++ TVMC decoder backend and C# playback.

[0.2.0]: https://github.com/open4dfoundation/Open4D/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/open4dfoundation/Open4D/releases/tag/v0.1.0
