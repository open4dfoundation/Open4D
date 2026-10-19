# Data and artifact policy

Open4D keeps source code, small configuration files, and deliberately selected
paper fixtures in Git. Local datasets, training runs, checkpoints, decoded
meshes, benchmark jobs, and logs do not belong in the source repository.

## Python data API

Example data is created or downloaded on first access to `.path`, outside the
wheel. The default cache is `~/.open4d/data`; set `OPEN4D_DATA_DIR` or pass
`cache_dir=` to choose another location.

```python
import open4d as o4d

sample = o4d.data.SyntheticMeshSequence()
with o4d.load(sample.path) as meshes:
    print(meshes.frame(0).geometry.positions.shape)

with o4d.data.SyntheticGaussianSequence().load() as gaussians:
    o4d.encode(gaussians, "synthetic-gaussians.o4d", codec="npz")
```

These tiny MIT examples are generated from deterministic source. They are
synthetic, not captures, pretrained neural outputs or the restricted TVMC
basketball dataset. No research data or trained checkpoint is bundled.

For an externally hosted asset, construct `DatasetSpec(name=..., url=...,
sha256=..., license=..., license_url=..., kind=..., filename=..., archive=...)`
then access `DownloadableDataset(spec).path`. URLs must use HTTPS and the
SHA-256 must identify the exact bytes. `kind` is `mesh`, `gaussian` or `model`;
an archive may be `zip` or `tar`. A model has its own specification and rights,
separate from its training dataset and implementation. The caller must use
actual license evidence; supplying a label does not grant rights.

Downloads are staged and published atomically after checksum verification.
Archive paths, links, duplicate members and special files are rejected, and
download/extraction sizes and file counts are bounded. Every cache use verifies
the source and extracted contents against the pinned archive. A corrupted cache
raises an error; remove that dataset's cache directory to fetch it again.

Hosting on an HTTPS service such as a Hub or archival repository uses the same
contract. This PR does not create a public data host or claim unverified model
lineage. Only rights-cleared data and independently reviewed weights should be
added to a public catalog.

## Shared machine learning utilities

Neural methods that encode `.o4d` register as codec plugins. `open4d.ml` holds
only shared training utilities; it imports Torch when training is called, and
contains no research models or checkpoints:

```python
import torch
from open4d.ml import train

model = torch.nn.Linear(1, 1)
x = torch.linspace(-1, 1, 32).reshape(-1, 1)
history = train(model, [(x, 2 * x + 1)],
                optimizer=torch.optim.SGD(model.parameters(), lr=0.2),
                loss_fn=torch.nn.functional.mse_loss, epochs=100)
print(history.losses[-1])
```

This is a generic synthetic regression example, not N4MC training. Codec
architectures, representation-specific losses and training inputs remain in
their own plugins and environments.

## Local locations

Use the existing component-local conventions for runtime data:

- `open4d/codecs/<codec>/source/datasets/` for downloaded or private datasets
- `open4d/codecs/<codec>/source/outputs/` for training and evaluation outputs
- `open4d/codecs/<codec>/source/experiments/` for per-run working directories
- `open4d/codecs/<codec>/source/checkpoints/` for model weights
- codec-specific runtime `data/` directories for downloaded sequences; TVMC's
  ARAP input datasets, for example, live under
  `open4d/codecs/tvmc/source/arap-volume-tracking/data/`

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
  codecs/example/datasets/example.tar.zst
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
