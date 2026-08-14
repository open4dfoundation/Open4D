# Contributing and reproducibility

Open4D combines a small platform, imported research systems, hardware software,
and restrictive third-party material. A good contribution is not merely code
that works on its author's machine; it makes its support boundary and evidence
clear without destabilizing unrelated research.

## Support tiers

Use these tiers in issues, tests, and reviews:

| Tier | Examples | Expected automation |
| --- | --- | --- |
| CPU core | `open4d.core`, base install, built-in OBJ/PLY | Python 3.10–3.13 on every change |
| CPU optional | OpenUSD, viewer non-GUI logic, Torch CPU | marked jobs with their extras |
| Open3D | adapter and Open3D-backed operations | Python 3.10–3.12; 3.13 unsupported upstream |
| Research CPU/toolchain | Draco, TVMC/TSMC stages, .NET/CMake | small golden/dry-run jobs where licensed and practical |
| GPU | neural codecs, CUDA TSDF, Gaussian methods | dedicated runner/manual matrix with exact CUDA/driver |
| Hardware/UI | cameras, Quest/Unity, interactive GL/WebRTC | documented manual acceptance until runners exist |
| External reference | MPEG V-DMC | exact upstream pin/build/test record |

Do not make the NumPy-only package import a heavy optional dependency at module
import time. Fail when the optional feature is called and show the exact extra
or setup command.

## Environments

### Lightweight core

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e .
python -m pytest -q open4d/core/tests examples/visualization/tests
```

Install extras only for the work you are testing:

```bash
python -m pip install -e '.[player]'
python -m pip install -e '.[usd]'
python -m pip install -e '.[tools]'
python -m pip install -e '.[open3d]'
```

### Shared codec Python environment

```bash
conda env create -f environment.yml
conda activate open4d
python -m pip install -e .
```

This environment is Python 3.12, NumPy 1.26.4, Open3D 0.19, and PyTorch 2.7.0.
Machine-built GPU extensions are documented in `requirements-gpu.txt`; install
only those required by your codec. `.NET 10`, CMake, CUDA toolkits, Blender,
camera SDKs, Unity, and vendor drivers are external toolchains, not things pip
can make reproducible.

Always record `python --version`, relevant package versions, OS/architecture,
CPU/GPU, driver/CUDA, compiler/CMake/.NET, and the Open4D commit. “CUDA 12” is
not precise enough when an extension must match PyTorch's exact CUDA build.

## Test tiers and collection

The intended root markers are:

- `cpu`: base deterministic tests;
- `open3d`: optional adapter/BVH behavior;
- `torch`: Torch CPU or GPU-aware behavior;
- `gpu`: requires a suitable GPU/driver/toolkit;
- `hardware`: cameras, headset, or other physical device;
- `slow`: unsuitable for normal per-change feedback.

Until root collection is hardened, invoke known suites explicitly. Several
research scripts are named `test.py`, `decoder_test.py`, or `model_test.py` but
perform local-dataset/GPU experiments and must not be collected by default.
Rename or configure them only after preserving their intended commands.

For a new feature, cover at least:

- normal behavior and the smallest meaningful fixture;
- empty/one-frame/boundary values;
- malformed data and actionable errors;
- lifecycle cleanup and failure propagation;
- laziness: prove which access decodes which frames;
- exact round-trip fields or explicit unsupported failures;
- deterministic results or documented tolerances/seeds.

Codec tests must hide the source and encoder intermediates before fresh-process
decode. If the decoder still succeeds by reading an undeclared original file,
the test has not validated a codec artifact.

## Artifact and data rules

Follow the repository's [artifact policy](../../artifacts.md): do not commit
local datasets, training runs, checkpoints, decoded outputs, logs, or benchmark
work directories. A committed binary fixture must be small, licensed,
indispensable, and documented beside its consumer.

Prefer a pinned download with SHA-256:

```bash
./scripts/fetch_artifact.sh \
  https://stable.example/open4d/example.tar.zst \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  open4d/codecs/example/datasets/example.tar.zst
```

Record origin, revision, license, checksum, size, unpack command, and exact
consumer. Never replace a tracked historical artifact casually: migrate it to
durable storage with provenance and a reviewed compatibility plan.

Every publishable experiment should emit the run manifest described in
[the platform chapter](platform.md#run-manifest-v1). Report encoded payload,
container/filesystem, and wire size separately. Include all decoder state in
the encoded accounting.

## License and provenance gate

The root MIT license does not erase component terms. Examples in the audited
tree include GPL-3.0 QNDF, a TVMC non-commercial/no-redistribution agreement,
NVIDIA/non-commercial Gaussian code, other third-party licenses, prebuilt
libraries, imported data, and paper assets.

Before adding or distributing any third-party material:

1. record origin and exact revision;
2. identify the license file and applicable paths;
3. record modification/patch history;
4. identify whether source, binary, model, dataset, paper, and output rights
   differ;
5. get maintainer/legal review for compatibility and distribution scope;
6. keep the material outside the lightweight wheel unless explicitly approved.

No package release should be published until the exact wheel allowlist and the
TVMC/QNDF/copied-upstream/binary/dataset boundaries are resolved. This handbook
describes risks; it is not legal advice.

## Ownership and safe boundaries

- Shared platform contributors own core, supported I/O/metrics/container
  contracts, manifests, packaging, and reusable adapters.
- Codec owners preserve scientific semantics and approve changes to training,
  encoded formats, and codec-local metrics.
- RGB-D owners approve hardware/calibration/reconstruction changes.
- Ryan owns QUEEN, 3DGStream, Gaussian training/quantization, CUDA rasterizers,
  Gaussian semantics, upstream pins, correctness, and performance.

Do not perform broad formatting, dependency, or conversion refactors across a
research tree. First add parity evidence around the behavior you intend to
reuse. In Gaussian directories, stop at the requested interface/handoff and
seek Ryan's review.

## Review checklist

### Shared API or data format

- Is the use case represented by a real provider/consumer?
- Are defaults, optional dependencies, units, coordinate systems, timestamps,
  topology, and failure semantics explicit?
- Is unsupported data rejected rather than silently dropped?
- Is there a compatibility/version story and a tiny golden fixture?
- Does base import remain NumPy-only?

### Codec or reconstruction change

- Are algorithm changes separated from Open4D adapter changes?
- Can decode/replay run from declared artifacts without original state?
- Are output coordinates/time/metadata and all side information documented?
- Are size, quality, speed, memory, and latency measured separately?
- Are seeds, tool revisions, environment, and artifact hashes in the manifest?

### Documentation/status change

- Does it name audit commit, date, evidence level, and environment?
- Does it separate upstream claims from Open4D verification?
- Do commands and relative links resolve from their stated directory?
- Does a green/complete label have an automated command behind it?

## Definition of done for a new component

A component is not `COMPLETE` until:

1. clean setup from documented prerequisites succeeds;
2. a redistributable or generated sample is available with provenance;
3. one command runs the supported end-to-end path;
4. a fresh process consumes only declared artifacts;
5. the supported Open4D boundary is used;
6. deterministic automated tests and manual matrices pass as appropriate;
7. artifacts, output formats, limits, and cleanup are documented;
8. license/provenance and exact distribution scope are approved;
9. a run manifest proves revision, configuration, hashes, bytes, timings, and
   metric version.
