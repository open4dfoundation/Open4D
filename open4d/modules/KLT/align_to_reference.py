"""Align KLT reconstructions to a reference sequence's world coordinates.

KLT reconstructs meshes in the normalized TSDF cube (marching-cubes grid space),
so they are uniformly scaled and offset relative to the original-scale reference
meshes. Because a reconstruction and its reference frame are the same object,
a per-frame similarity transform recovered from their bounding boxes (uniform
scale to match the box diagonal, translation to match the box center) places the
reconstruction back into the reference frame without absorbing surface error.

Frame mapping: reconstruction ``mesh_{k:04d}.obj`` (k = 0,1,...) corresponds to
reference ``{ref_prefix}{ref_start + k:04d}.obj``.

Example
-------
    python align_to_reference.py \
        --recon_dir outputs/basketball_sequence_klt/reconstructed_meshes \
        --ref_dir ../tvmc/arap-volume-tracking/data/basketball_player \
        --ref_prefix basketball_player_fr --ref_start 11 --num_frames 10 \
        --out_dir outputs/basketball_sequence_klt/decoded \
        --out_prefix decoded_basketball_player_fr
"""

import argparse
import os

import numpy as np
import trimesh


def load(path):
    return trimesh.load(path, force="mesh", process=False)


def bbox_similarity(src_v, dst_v):
    """Uniform scale + translation mapping src bbox onto dst bbox."""
    s_lo, s_hi = src_v.min(0), src_v.max(0)
    d_lo, d_hi = dst_v.min(0), dst_v.max(0)
    scale = float(np.linalg.norm(d_hi - d_lo) / np.linalg.norm(s_hi - s_lo))
    t = (d_lo + d_hi) / 2 - scale * (s_lo + s_hi) / 2
    return scale, t


def main():
    ap = argparse.ArgumentParser(description="Align KLT reconstructions to reference world scale")
    ap.add_argument("--recon_dir", required=True, help="Dir of reconstructed mesh_XXXX.obj")
    ap.add_argument("--ref_dir", required=True, help="Dir of reference meshes")
    ap.add_argument("--ref_prefix", default="basketball_player_fr")
    ap.add_argument("--ref_start", type=int, default=11, help="Reference frame index for recon 0")
    ap.add_argument("--num_frames", type=int, default=10)
    ap.add_argument("--out_dir", required=True, help="Output dir for aligned decoded meshes")
    ap.add_argument("--out_prefix", default="decoded_basketball_player_fr")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for k in range(args.num_frames):
        recon_path = os.path.join(args.recon_dir, f"mesh_{k:04d}.obj")
        ref_path = os.path.join(args.ref_dir, f"{args.ref_prefix}{args.ref_start + k:04d}.obj")
        if not (os.path.exists(recon_path) and os.path.exists(ref_path)):
            print(f"skip frame {k}: missing {recon_path if not os.path.exists(recon_path) else ref_path}")
            continue
        recon, ref = load(recon_path), load(ref_path)
        scale, t = bbox_similarity(np.asarray(recon.vertices), np.asarray(ref.vertices))
        aligned = recon.copy()
        aligned.vertices = np.asarray(recon.vertices) * scale + t
        out_path = os.path.join(args.out_dir, f"{args.out_prefix}{args.ref_start + k:04d}.obj")
        aligned.export(out_path)
        print(f"frame {k:02d} -> {os.path.basename(out_path)}  (scale={scale:.4f})")
    print("done ->", args.out_dir)


if __name__ == "__main__":
    main()
