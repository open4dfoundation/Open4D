# Scripts

Repository-level developer utilities live here. Run them from anywhere inside
the checkout; scripts resolve the repository root themselves.

- `setup_draco.sh` initializes and builds all three Draco submodules: the one
  backing the `codecs/draco` baseline, and TVMC's and TSMC's copies.
- `fetch_artifact.sh URL SHA256 DESTINATION` downloads an externally stored
  dataset or checkpoint and rejects it if its SHA-256 checksum does not match.
- `download_datasets.sh` is reserved for a future shared dataset registry.

Module-specific setup and pipeline commands remain in each module directory.
See `docs/artifacts.md` before adding datasets, checkpoints, or generated runs.
