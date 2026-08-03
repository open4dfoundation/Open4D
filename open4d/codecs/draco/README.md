# Draco

Google [Draco](https://github.com/google/draco) mesh-compression baseline for
Open4D. Draco is the standard geometry codec used to benchmark the neural codecs
(N4MC, QNDF, TVMC, TSMC). This module wraps Draco's `draco_encoder` /
`draco_decoder` binaries into a per-frame encode → decode → evaluate pipeline and
was promoted out of `N4MC`.

Google's Draco C++ source is vendored here as the `draco/` git submodule, pinned
to the same commit used by the TVMC and TSMC modules.

## Layout

```
Draco/
├── draco/               # google/draco submodule (C++ source)
├── draco_baseline.py    # encode + decode a folder of .obj frames at chosen -qp
├── evaluation.py        # bitrate + D1/D2 PSNR + depth/color SSIM vs ground truth
├── util.py              # evaluation + metric helpers (copied from N4MC)
├── metrics.py           # D1/D2 PSNR (copied from N4MC)
├── setup_draco.sh       # init submodule + cmake build
├── requirements.txt
└── README.md
```

## Setup

Build the Draco binaries (from the repo root or this directory):

```bash
./setup_draco.sh          # git submodule update --init + cmake build
```

This produces `draco/build/draco_encoder` and `draco/build/draco_decoder`. If you
already built Draco elsewhere (e.g. `tvmc/draco/build`), point the scripts at it
with `--draco_bin_dir` or `export DRACO_BIN_DIR=/path/to/build`.

For the Python evaluation dependencies:

```bash
python -m pip install -r requirements.txt
```

## Usage

Encode + decode a folder of ground-truth `.obj` frames at quantization bits
`-qp 7`:

```bash
python draco_baseline.py \
    --input_dir /path/to/gt \
    --encode_root outputs/encode --decode_root outputs/decode \
    --qp_min 7 --qp_max 8 --num_frames 100
```

`--qp_min` / `--qp_max` define an inclusive/exclusive range, so you can sweep
several rate points in one run (e.g. `--qp_min 7 --qp_max 12`).

Evaluate the decodes against the ground truth (bitrate, D1/D2 PSNR, SSIM):

```bash
python evaluation.py \
    --gt_path /path/to/combined_scaled \
    --encode_root outputs/encode --decode_root outputs/decode \
    --qp_min 7 --qp_max 8 --num_frames 10
```

## Notes

- Do not commit datasets, `.drc`/`.obj` outputs, renderings, or `outputs/`.
- `util.py` / `metrics.py` are copied (not imported) from N4MC so the module runs
  independently; keep them in sync if the N4MC originals change.
