# Data and artifact policy

Open4D keeps source code, small configuration files, and deliberately selected
paper fixtures in Git. Local datasets, training runs, checkpoints, decoded
meshes, benchmark jobs, and logs do not belong in the source repository.

## Local locations

Use the existing module-local conventions for runtime data:

- `open4d/modules/<module>/datasets/` for downloaded or private datasets
- `open4d/modules/<module>/outputs/` for training and evaluation outputs
- `open4d/modules/<module>/experiments/` for per-run working directories
- `open4d/modules/<module>/checkpoints/` for model weights
- module-specific runtime `data/` directories for downloaded sequences; TVMC's
  ARAP input datasets, for example, live under
  `open4d/modules/tvmc/arap-volume-tracking/data/`
- `benchmark_app/data/`, `benchmark_app/outputs/`, and `benchmark_app/runs/`
  for dashboard inputs, reference outputs, and jobs

These locations are ignored by the root `.gitignore`. Do not force-add their
contents.

## What may be committed

A binary fixture may be committed only when it is small, has a clear license,
is required by a test or minimal example, and is documented next to the code
that consumes it. Prefer download scripts with checksums for datasets and
published model weights.

Every benchmark result intended for publication should include a compact JSON
manifest containing the source dataset and frame range, method revision,
configuration, environment, measured encoded byte count, timing, and quality
metrics. Generated geometry should live in external artifact storage and be
referenced by a stable URL and checksum.

Use the repository helper to fetch an externally stored artifact into one of
the ignored local directories:

```bash
./scripts/fetch_artifact.sh \
  https://artifacts.example.org/open4d/example.tar.zst \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  open4d/modules/example/datasets/example.tar.zst
```

Record the real URL, SHA-256 checksum, license, and unpacking instructions in
the consuming module's README or setup script. Never use an unverified mutable
URL as the only record of a research input.

## Existing historical artifacts

Some module imports predate this policy and already contain tracked datasets,
reconstructions, checkpoints, compiled libraries, and paper assets. They remain
in history for now. Migrating them requires choosing durable external storage
and preserving provenance; it should be handled as a separate, reviewed change
rather than deleting research results opportunistically.
