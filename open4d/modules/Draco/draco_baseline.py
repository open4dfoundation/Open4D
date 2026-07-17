"""Draco mesh-compression baseline for Open4D.

Thin driver around Google Draco's ``draco_encoder`` / ``draco_decoder`` binaries
that encodes a folder of ``.obj`` frames at one or more quantization-position
(``-qp``) settings, decodes them back to ``.obj``, and reports mean encode /
decode times. Promoted out of ``N4MC`` into a self-contained module.

The Draco binaries are built from the vendored ``draco/`` submodule via
``setup_draco.sh``. Override their location with ``--draco_bin_dir`` or the
``DRACO_BIN_DIR`` environment variable.

Example
-------
    python draco_baseline.py \
        --input_dir /data/combined_scaled/gt \
        --encode_root outputs/encode --decode_root outputs/decode \
        --qp_min 7 --qp_max 8 --num_frames 100
"""

import argparse
import os
import re
import subprocess
import time

# Default: binaries built next to this module by setup_draco.sh
_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN_DIR = os.environ.get(
    "DRACO_BIN_DIR", os.path.join(_MODULE_DIR, "draco", "build")
)

_ENCODE_TIME_RE = re.compile(r"\((\d+) ms to encode\)")
_DECODE_TIME_RE = re.compile(r"\((\d+) ms to decode\)")


def encode_frames(input_dir, encode_root, encoder, qps, num_frames):
    """Encode every .obj in input_dir at each qp; return {qp: mean_ms}."""
    obj_files = sorted(f for f in os.listdir(input_dir) if f.endswith(".obj"))
    obj_files = obj_files[:num_frames]
    results = {}
    for qp in qps:
        output_dir = os.path.join(encode_root, f"qp_{qp}")
        os.makedirs(output_dir, exist_ok=True)
        times = []
        for obj_file in obj_files:
            input_path = os.path.join(input_dir, obj_file)
            output_path = os.path.join(output_dir, obj_file.replace(".obj", f"_qp_{qp}.drc"))
            result = subprocess.run(
                [encoder, "-i", input_path, "-o", output_path, "-qp", str(qp)],
                capture_output=True, text=True,
            )
            match = _ENCODE_TIME_RE.search(result.stdout)
            if match:
                times.append(int(match.group(1)))
        mean_time = sum(times) / len(times) if times else float("nan")
        results[qp] = mean_time
        print(f"[encode] qp={qp}: mean {mean_time:.2f} ms over {len(obj_files)} frames -> {output_dir}")
    return results


def decode_frames(encode_root, decode_root, decoder, qps, num_frames):
    """Decode every .drc back to .obj at each qp; return {qp: mean_ms}."""
    results = {}
    for qp in qps:
        input_dir = os.path.join(encode_root, f"qp_{qp}")
        output_dir = os.path.join(decode_root, f"qp_{qp}")
        os.makedirs(output_dir, exist_ok=True)
        drc_files = sorted(f for f in os.listdir(input_dir) if f.endswith(".drc"))
        drc_files = drc_files[:num_frames]
        times = []
        for drc_file in drc_files:
            input_path = os.path.join(input_dir, drc_file)
            output_path = os.path.join(
                output_dir, drc_file.replace(f"_qp_{qp}.drc", f"_qp_{qp}_decoded.obj"))
            result = subprocess.run(
                [decoder, "-i", input_path, "-o", output_path],
                capture_output=True, text=True,
            )
            match = _DECODE_TIME_RE.search(result.stdout)
            if match:
                times.append(int(match.group(1)))
        mean_time = sum(times) / len(times) if times else float("nan")
        results[qp] = mean_time
        print(f"[decode] qp={qp}: mean {mean_time:.2f} ms over {len(drc_files)} frames -> {output_dir}")
    return results


def parse_args():
    parser = argparse.ArgumentParser(description="Draco mesh-compression baseline")
    parser.add_argument("--input_dir", type=str, required=True,
                        help="Directory of ground-truth .obj frames to encode")
    parser.add_argument("--encode_root", type=str, required=True,
                        help="Output root for encoded .drc files (per-qp subdirs)")
    parser.add_argument("--decode_root", type=str, required=True,
                        help="Output root for decoded .obj files (per-qp subdirs)")
    parser.add_argument("--draco_bin_dir", type=str, default=DEFAULT_BIN_DIR,
                        help="Directory containing draco_encoder/draco_decoder "
                             "(default: ./draco/build or $DRACO_BIN_DIR)")
    parser.add_argument("--qp_min", type=int, default=7, help="Minimum quantization bits (inclusive)")
    parser.add_argument("--qp_max", type=int, default=8, help="Maximum quantization bits (exclusive)")
    parser.add_argument("--num_frames", type=int, default=100, help="Number of frames to process")
    return parser.parse_args()


def main():
    args = parse_args()
    encoder = os.path.join(args.draco_bin_dir, "draco_encoder")
    decoder = os.path.join(args.draco_bin_dir, "draco_decoder")
    for binary in (encoder, decoder):
        if not os.path.exists(binary):
            raise FileNotFoundError(
                f"Draco binary not found: {binary}. Build it with ./setup_draco.sh "
                f"or set --draco_bin_dir / DRACO_BIN_DIR.")

    qps = range(args.qp_min, args.qp_max)
    encode_frames(args.input_dir, args.encode_root, encoder, qps, args.num_frames)
    decode_frames(args.encode_root, args.decode_root, decoder, qps, args.num_frames)


if __name__ == "__main__":
    main()
