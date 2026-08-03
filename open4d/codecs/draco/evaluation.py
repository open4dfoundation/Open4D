"""Evaluate Draco decodes against ground-truth meshes.

Computes bitrate, D1/D2 point-to-point/point-to-plane PSNR, and depth/color SSIM
+ PSNR (via rendered viewpoints) for each Draco quantization setting. Promoted
out of ``N4MC`` into this module; reuses the copied ``util.py`` / ``metrics.py``.

Example
-------
    python evaluation.py \
        --gt_path /data/combined_scaled \
        --encode_root outputs/encode --decode_root outputs/decode \
        --qp_min 7 --qp_max 8 --num_frames 10
"""

import argparse
import os

import numpy as np
import open3d as o3d

from util import evaluate_meshes, compute_bitrate, evaluate_psnr, load_mesh_list, select_viewpoints


def evaluate(args):
    fps = args.fps
    num_frames = args.num_frames
    qps = range(args.qp_min, args.qp_max)

    print("\n=== Evaluating Draco ===\n")
    qps_result, bitrates = [], []
    d1_psnrs, d2_psnrs = [], []
    ssim_depths, ssim_colors, psnr_depths, psnr_colors = [], [], [], []

    for qp in qps:
        print(f"\n--- Draco QP {qp} ---")
        encode_path = os.path.join(args.encode_root, f"qp_{qp}")
        decode_path = os.path.join(args.decode_root, f"qp_{qp}")

        # Bitrate from encoded .drc sizes
        total_bits = 0
        drc_files = sorted(f for f in os.listdir(encode_path) if f.endswith(".drc"))
        for drc_file in drc_files[:num_frames]:
            total_bits += os.path.getsize(os.path.join(encode_path, drc_file)) * 8
        bitrate = compute_bitrate(total_bits, num_frames, fps) / 1000

        # D1/D2 PSNR
        (avg_d1, max_d1, min_d1, avg_d2, max_d2, min_d2) = evaluate_psnr(
            args.gt_path, decode_path, num_frames, mode="default")

        # SSIM / PSNR over rendered viewpoints
        ssim_depth, ssim_color, psnr_depth, psnr_color = [], [], [], []
        gt_meshes = load_mesh_list(args.gt_path, "default")
        rec_meshes = load_mesh_list(decode_path, "default")

        out_dir = os.path.join(decode_path, "SSIM")
        os.makedirs(out_dir, exist_ok=True)
        num_views = 4

        for gt_file, rec_file in zip(gt_meshes[:num_frames], rec_meshes[:num_frames]):
            gt_mesh = o3d.io.read_triangle_mesh(gt_file)
            rec_mesh = o3d.io.read_triangle_mesh(rec_file)
            gt_mesh.compute_vertex_normals()
            rec_mesh.compute_vertex_normals()

            view_files_exist = all(
                os.path.exists(f"{out_dir}/view_{j:02d}.json") for j in range(num_views))
            if not view_files_exist:
                viewpoints = select_viewpoints(rec_mesh, gt_mesh, num_views=num_views)
                for j, cam in enumerate(viewpoints):
                    o3d.io.write_pinhole_camera_parameters(f"{out_dir}/view_{j:02d}.json", cam)
            else:
                viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{j:02d}.json")
                              for j in range(num_views)]

            d, c, pd, pc = evaluate_meshes(
                gt_mesh, rec_mesh, viewpoints,
                output_dir=os.path.join(out_dir, "renderings"))
            ssim_depth.append(d)
            ssim_color.append(c)
            psnr_depth.append(pd)
            psnr_color.append(pc)

        qps_result.append(qp)
        bitrates.append(bitrate)
        d1_psnrs.append(avg_d1)
        d2_psnrs.append(avg_d2)
        ssim_depths.append(np.mean(ssim_depth))
        ssim_colors.append(np.mean(ssim_color))
        psnr_depths.append(np.mean(psnr_depth))
        psnr_colors.append(np.mean(psnr_color))

    print("\n=== Draco Final Results ===")
    print("Draco_qps =", qps_result)
    print("Draco_bitrates =", [round(x, 3) for x in bitrates])
    print("Draco_d1_psnrs =", [round(x, 3) for x in d1_psnrs])
    print("Draco_d2_psnrs =", [round(x, 3) for x in d2_psnrs])
    print("Draco_ssim_depths =", [round(x, 3) for x in ssim_depths])
    print("Draco_ssim_colors =", [round(x, 3) for x in ssim_colors])
    print("Draco_psnr_depths =", [round(x, 3) for x in psnr_depths])
    print("Draco_psnr_colors =", [round(x, 3) for x in psnr_colors])


def parse_args():
    parser = argparse.ArgumentParser(description="Evaluate Draco decodes vs ground truth")
    parser.add_argument("--gt_path", type=str, required=True,
                        help="Ground-truth dataset root (contains gt meshes + SSIM viewpoints)")
    parser.add_argument("--encode_root", type=str, required=True,
                        help="Root of encoded .drc files (per-qp subdirs), from draco_baseline.py")
    parser.add_argument("--decode_root", type=str, required=True,
                        help="Root of decoded .obj files (per-qp subdirs), from draco_baseline.py")
    parser.add_argument("--qp_min", type=int, default=7)
    parser.add_argument("--qp_max", type=int, default=8)
    parser.add_argument("--num_frames", type=int, default=10)
    parser.add_argument("--fps", type=int, default=30)
    return parser.parse_args()


if __name__ == "__main__":
    evaluate(parse_args())
