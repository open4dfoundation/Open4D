# Data and artifact policy

Open4D keeps source code, small configuration files, and deliberately selected
paper fixtures in Git. Local datasets, training runs, checkpoints, decoded
meshes, benchmark jobs, and logs do not belong in the source repository.

## Local locations

Use the existing component-local conventions for runtime data:

- `open4d/codecs/<codec>/datasets/` for downloaded or private datasets
- `open4d/codecs/<codec>/outputs/` for training and evaluation outputs
- `open4d/codecs/<codec>/experiments/` for per-run working directories
- `open4d/codecs/<codec>/checkpoints/` for model weights
- codec-specific runtime `data/` directories for downloaded sequences; TVMC's
  ARAP input datasets, for example, live under
  `open4d/codecs/tvmc/arap-volume-tracking/data/`

Reconstruction data and output locations:

- `open4d/reconstruction/<component>/data/` and `datasets/` for inputs
- `open4d/reconstruction/<component>/output/` and `outputs/` for runs
- `open4d/reconstruction/<component>/{logs,checkpoints}/` for the rest

For weights inside a Git submodule, use that submodule's ignore rules.

These locations are ignored by the root `.gitignore`. Do not force-add their
contents.

## What may be committed

A binary fixture may be committed only when it is small, has a clear license,
is required by a test or minimal example, and is documented next to the code
that consumes it. Prefer download scripts with checksums for datasets and
published model weights.

Keep benchmark results, audit reports and run summaries outside the repository.
Store run manifests, generated geometry and logs in external artifact storage;
record dataset, revision, configuration, environment and checksums there.

Use the repository helper to fetch an externally stored artifact into one of
the ignored local directories:

```bash
./scripts/fetch_artifact.sh \
  https://artifacts.example.org/open4d/example.tar.zst \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  open4d/codecs/example/datasets/example.tar.zst
```

Record the real URL, SHA-256 checksum, license, and unpacking instructions in
the consuming component's README or setup script. Never use an unverified mutable
URL as the only record of a research input.

## Existing historical artifacts

Some component imports predate this policy and still contain compiled
libraries and small fixtures. The TSMC, TVMC, 3DGStream and Unity example
datasets and the paper PDFs were removed from the tree but remain in Git
history. 3DGStream's NTC checkpoint stays because Gaussian reconstruction loads
it by default.
