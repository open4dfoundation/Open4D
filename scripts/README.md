# Scripts

Repository-level developer utilities live here. Unless noted below, they can
run from anywhere inside the checkout and resolve the repository root.

- `benchmark_io.py` times validated, in-process calls to the public sequence
  API. Run it in an environment where Open4D is installed, or use
  `PYTHONPATH=. python scripts/benchmark_io.py --json` from the checkout root.
  With the `[tools]` extra it also measures OFF, STL, GLB, and glTF decoding.
- `benchmark_codec.py` measures a complete in-process encode, lazy decoder open,
  and validated full decode. Pass `--source 4d_files/Rafa_Approves_hd_4k` to
  benchmark real OBJ frames instead of its generated moving grid. Compare the
  five reference codecs with `--codecs raw,deflate,bzip2,lzma,rle`.
- `smoke_installed_io.py` is the package CI smoke test. It deliberately imports
  an installed wheel and loads a real OBJ from outside the checkout.

- `setup_draco.sh` initializes and builds all three Draco submodules: the one
  backing the `codecs/draco` baseline, and TVMC's and TSMC's copies.
- `fetch_artifact.sh URL SHA256 DESTINATION` downloads an externally stored
  dataset or checkpoint and rejects it if its SHA-256 checksum does not match.
- `download_datasets.sh` is reserved for a future shared dataset registry.

Module-specific setup and pipeline commands remain in each module directory.
See `docs/artifacts.md` before adding datasets, checkpoints, or generated runs.
