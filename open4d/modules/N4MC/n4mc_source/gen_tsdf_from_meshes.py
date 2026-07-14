"""
Generate N4MC TSDF-Def tensors (TSDF_128/data/*.npz) directly from a mesh
sequence, without nvdiffrast / render.py.

This reproduces exactly what Step 2 (data_processing.normalize_mesh_to_unit_cube)
+ Step 3 (optimize_tsdf_offset.opt_fmc, the *initial* SDF) produce:

  - normalize each mesh into the unit cube ([-1,1] on its largest axis)
  - build the same voxel grid via fmc.construct_voxel_grid(res) and scale it by 2
  - signed distance from pcu.signed_distance_to_mesh, sign-fixed on the last point
  - truncate/normalize:  clip(sdf / (2*2/res), -1, 1)
  - sdf -> (res+1, res+1, res+1, 1),  offset -> zeros (res+1, res+1, res+1, 3)

The only thing skipped vs. full Step 3 is the differentiable-rendering refinement
of sdf/offset. The result is a valid TSDF-Def dataset the auto-decoder can train on.
"""
import os
import argparse
import numpy as np
import torch
import trimesh
import point_cloud_utils as pcu
from natsort import natsorted

from fmc import construct_voxel_grid


def normalize_to_unit_cube(v):
    mn, mx = v.min(0), v.max(0)
    center = (mn + mx) / 2
    scale = (mx - mn).max() / 2
    return (v - center) / scale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mesh_dir', required=True, help='folder of .obj/.ply frames')
    ap.add_argument('--save_path', required=True, help='output dataset dir (e.g. ./TSDF_128)')
    ap.add_argument('--num_frames', type=int, default=0, help='0 = all frames found')
    ap.add_argument('--voxel_grid_res', type=int, default=127)
    ap.add_argument('--exts', default='.obj,.ply,.stl')
    args = ap.parse_args()

    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    res = args.voxel_grid_res

    exts = tuple(args.exts.split(','))
    files = natsorted([f for f in os.listdir(args.mesh_dir) if f.lower().endswith(exts)])
    if args.num_frames:
        files = files[:args.num_frames]
    assert files, f'no meshes with {exts} in {args.mesh_dir}'

    # same grid the model trains on: verts in [-0.5,0.5] -> *2 -> [-1,1]
    verts, _ = construct_voxel_grid(res, device)
    query = (verts * 2).cpu().numpy().astype(np.float32)   # (res+1)^3 x 3
    trunc = 2 * 2 / res                                    # TSDF truncation band

    os.makedirs(os.path.join(args.save_path, 'data'), exist_ok=True)
    os.makedirs(os.path.join(args.save_path, 'data', 'TSDF'), exist_ok=True)

    for i, fn in enumerate(files):
        m = trimesh.load_mesh(os.path.join(args.mesh_dir, fn), process=False)
        v = normalize_to_unit_cube(np.asarray(m.vertices, dtype=np.float32))
        f = np.asarray(m.faces, dtype=np.int32)

        sdf, _, _ = pcu.signed_distance_to_mesh(query, v.astype(np.float32), f)
        if sdf[-1] < 0:          # last grid point is a far corner -> must be outside
            sdf = -sdf
        sdf = np.clip(sdf / trunc, -1, 1).astype(np.float32)

        sdf_grid = sdf.reshape(res + 1, res + 1, res + 1, 1)
        offset_grid = np.zeros((res + 1, res + 1, res + 1, 3), dtype=np.float32)

        np.savez_compressed(os.path.join(args.save_path, 'data', '%04d.npz' % i),
                            sdf=sdf_grid, offset=offset_grid)
        np.savez_compressed(os.path.join(args.save_path, 'data', 'TSDF', '%04d.npz' % i),
                            sdf=sdf_grid)
        occ = int((np.abs(sdf_grid) < 1).sum())
        print(f'[{i+1}/{len(files)}] {fn}: sdf[{sdf.min():.3f},{sdf.max():.3f}] '
              f'band_voxels={occ} -> {i:04d}.npz')

    print(f'done: wrote {len(files)} frames to {os.path.join(args.save_path, "data")}')


if __name__ == '__main__':
    main()
