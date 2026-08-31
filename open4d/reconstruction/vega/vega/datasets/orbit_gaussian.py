"""Loader for the ORBIT *Gaussian-training* corpus (multi-view RGB, no depth).

Dataset layout (see `<dataset_root>/dataset.json`, format
`orbit-rgb-gaussian-training`):
- One directory per scene object, each with a nerfstudio-style
  `transforms.json` holding shared OPENCV intrinsics, a sequence-wide
  bounding box, and one entry per (frame, view) with both an OpenGL
  (`transform_matrix`) and an OpenCV (`camera_to_world_opencv`) c2w matrix.
- Per frame, per view: an RGB PNG only. `contains_depth` and
  `contains_pointclouds` are both false, and the renders are composited over
  a black background.

That last point is the whole difference from `vega.datasets.orbit` (the RGBD
corpus), which gets a fused per-frame point cloud handed to it and only has
to convert points to Gaussians. Here there is no depth at all, so geometry
has to be recovered from the images. Rather than run a full from-scratch 3DGS
optimization per frame (30 frames x 9 objects of gradient descent, just to
produce the *input* to the part of the paper this baseline is actually about),
this loader exploits the two properties the corpus guarantees — a known,
pre-calibrated 8-camera rig and a pure black background — and recovers
geometry by classical **silhouette / space carving**:

1. Threshold each view into a foreground silhouette (the black background
   makes this exact, not a learned matte).
2. Carve a voxel grid over the object's known bounding box, keeping voxels
   that project inside the silhouette in at least `view_count - view_slack`
   views (the visual hull).
3. Keep only the hull's *shell* (voxels with an empty 6-neighbour), since
   interior voxels are never visible and would only inflate the Gaussian
   count.
4. Colour each shell voxel by averaging the views where it is the front-most
   surface (per-view z-buffer over the shell), so back-facing views don't
   bleed the wrong colour onto it.

The result is a dense, coloured, world-space point cloud per frame — exactly
the interface `vega.datasets.orbit.points_to_gaussians` already expects — so
everything downstream (segmentation, GOV structure, hierarchical color
encoding, dynamicity filtering, the rendering pipeline) is reused unmodified.

The visual hull is a superset of the true shape (concavities survive
carving), which is the known limitation of this reconstruction; with 8 views
around the subject it is tight enough to give Vega's encoder real,
temporally-coherent geometry to work on, which is what it needs. Because the
8 views sit on one horizontal ring, concavities that are only visible from
above or below (under a chin, a vertical gap between arm and torso) are the
ones carving cannot reach. `refine_iters > 0` mops up some of that
photometrically — see `refine_gaussians`.
"""
from __future__ import annotations

import dataclasses
import json
import math
from pathlib import Path

import numpy as np
import torch
from PIL import Image

from vega.cameras import Camera
from vega.datasets.orbit import points_to_gaussians
from vega.gaussians import GaussianSet
from vega.metrics import vega_loss
from vega.rasterize import render

MIN_RENDER_DISTANCE = 6.0
"""World units. This workstation's `diff_gaussian_rasterization` build culls
every Gaussian once the camera is nearer than ~3.5-4 units, whatever the
scene's scale (measurements in `orbitvega.eval_camera`). The
corpus's own rig sits only ~3.2 m from the subject, i.e. inside that dead
zone, so anything that renders from the rig cameras — `refine_gaussians`
below — has to work around it. Kept here rather than imported from
`orbitvega` so the engine does not depend on the harness adapter."""

DEFAULT_VOXEL_SIZE = 0.006          # 6 mm; ~67k shell points for a standing adult

DEFAULT_CARVE_SCALE = 0.5
"""Image scale the silhouettes and per-point colours are sampled at.

0.5 (2048x1536 of the corpus's native 4096x3072) rather than 0.25. Sharper
masks carve a slightly tighter hull, and colours are sampled from 4x the
pixels; measured on basketball frame 21 against the real views, with 2400
refine iters: 56.1 dB at 0.5 vs 55.0 dB at 0.25, for no measurable extra load
time (the cost is dominated by the refinement, not the image decode). Without
refinement the two are indistinguishable (26.0 vs 26.1 dB) — geometry error
dominates there, not colour sampling."""

DEFAULT_PRUNE_OPACITY = 0.01
"""Drop Gaussians whose refined opacity falls below this.

Refinement fades away Gaussians the visual hull created in places the real
surface is not — it cannot delete them, only make them invisible — so they go
on costing 28 bytes of non-colour attributes each and a slot in the colour
hash. Measured on basketball frame 21 at 2400 refine iters, scored against the
real views: pruning below 0.01 drops 7.5% of the Gaussians (69.1k -> 63.9k)
while PSNR is unchanged (56.18 -> 56.21 dB, SSIM identical to four decimals),
so it is free. Below 0.02 starts to cost (-0.25 dB) and 0.05 costs -1.65 dB.
Without refinement every opacity is the 0.8 initial value, so this prunes
nothing."""

DEFAULT_VIEW_SLACK = 0
"""How many of the 8 views may disagree with a voxel's silhouette before it is
carved away. 0 is the strict visual hull and is the better default on this
corpus: allowing one view to disagree inflates the hull exactly where it is
already weakest (the enclosed gap between arms, ball and torso), which shows
up as a smeared blob from any novel view. Measured on basketball frame 21,
scored against the 8 real views, slack 0 both looks better and is cheaper —
40.1 dB from 70.0k Gaussians vs 35.5 dB from 80.1k. Verified non-destructive
on all nine objects: the hull loses only 3-13% of its points and still covers
54-70% of every silhouette."""
DEFAULT_SILHOUETTE_THRESHOLD = 12 / 255.0
MAX_GRID_VOXELS = 48_000_000        # coarsen rather than OOM on an oversized bbox


def load_dataset_meta(dataset_root: str | Path) -> dict:
    with open(Path(dataset_root) / "dataset.json") as f:
        return json.load(f)


def object_names(meta: dict) -> list[str]:
    return [obj["name"] for obj in meta["objects"]]


def object_transforms_path(dataset_root: str | Path, scene_name: str) -> Path:
    """Path to the object's sequence-level `transforms.json`."""
    dataset_root = Path(dataset_root)
    meta = load_dataset_meta(dataset_root)
    for obj in meta["objects"]:
        if obj["name"] == scene_name:
            return dataset_root / obj["path"]
    raise KeyError(f"scene {scene_name!r} not found; available: {', '.join(object_names(meta))}")


def load_object_transforms(dataset_root: str | Path, scene_name: str) -> dict:
    """The object's sequence-level `transforms.json` (all frames x all views).

    Note its `frames[*].file_path` entries are relative to the object's own
    directory (`<dataset_root>/<object>/`), not to the dataset root — use
    `object_transforms_path(...).parent` as the base when resolving them.
    """
    with open(object_transforms_path(dataset_root, scene_name)) as f:
        return json.load(f)


def group_frames(transforms: dict) -> list[list[dict]]:
    """Group `transforms["frames"]` into per-timestep lists ordered by view id.

    Indexing is by the dataset's own 0-based `frame_index` rather than by the
    on-disk `frame_XXXXXX` directory name, because the latter carries the
    *source* frame number (e.g. UMA0 starts at 900) and differs per object.
    """
    by_frame: dict[int, list[dict]] = {}
    for entry in transforms["frames"]:
        by_frame.setdefault(int(entry["frame_index"]), []).append(entry)
    return [sorted(by_frame[k], key=lambda e: int(e["view_id"])) for k in sorted(by_frame)]


def even_frame_indices(n_available: int, n_frames: int) -> list[int]:
    """Evenly subsample `n_frames` of the 0-based frame indices available."""
    if n_frames >= n_available:
        return list(range(n_available))
    idx = np.linspace(0, n_available - 1, n_frames)
    return sorted({int(round(i)) for i in idx})


def build_camera(transforms: dict, entry: dict, image_scale: float, znear: float = 0.05,
                 zfar: float = 100.0, device: str = "cuda") -> Camera:
    """A `Camera` for one view of the rig.

    `camera_to_world_opencv` is used (not `transform_matrix`): the OpenCV
    convention (+Z forward, +Y down) is the one `vega.cameras` /
    `diff_gaussian_rasterization` expect, and it matches how the RGBD
    loader consumes that corpus's `camera_to_world`.
    """
    w_native, h_native = int(transforms["w"]), int(transforms["h"])
    fovx = 2 * math.atan(w_native / (2 * float(transforms["fl_x"])))
    fovy = 2 * math.atan(h_native / (2 * float(transforms["fl_y"])))

    M = np.asarray(entry["camera_to_world_opencv"], dtype=np.float64)
    R = M[:3, :3].astype(np.float32)      # camera-to-world rotation
    C = M[:3, 3].astype(np.float32)       # camera centre in world space
    T = (-R.T @ C).astype(np.float32)     # world-to-camera translation

    w = max(1, int(round(w_native * image_scale)))
    h = max(1, int(round(h_native * image_scale)))
    return Camera(R=R, T=T, fovx=fovx, fovy=fovy, width=w, height=h,
                  znear=znear, zfar=zfar, device=device)


def _load_views(base_dir: Path, entries: list[dict], scale: float, device: str):
    """Load one frame's views at `scale`, returning (images, w, h).

    images: (V, h, w, 3) float in [0, 1].
    """
    imgs = []
    w = h = None
    for entry in entries:
        path = base_dir / entry["file_path"].lstrip("./")
        img = Image.open(path).convert("RGB")
        if w is None:
            w = max(1, int(round(img.width * scale)))
            h = max(1, int(round(img.height * scale)))
        if (img.width, img.height) != (w, h):
            img = img.resize((w, h), Image.BILINEAR)
        arr = torch.from_numpy(np.array(img, copy=True)).to(device).float() / 255.0
        imgs.append(arr)
    return torch.stack(imgs), w, h


def _world_to_cam(entries: list[dict], device: str) -> torch.Tensor:
    mats = []
    for entry in entries:
        M = torch.as_tensor(entry["camera_to_world_opencv"], dtype=torch.float32, device=device)
        R, C = M[:3, :3], M[:3, 3]
        w2c = torch.eye(4, device=device)
        w2c[:3, :3] = R.T
        w2c[:3, 3] = -R.T @ C
        mats.append(w2c)
    return torch.stack(mats)


def _project(pts: torch.Tensor, w2c: torch.Tensor, fx: float, fy: float, cx: float, cy: float):
    """Pinhole-project world points into one view. Returns (u, v, z)."""
    cam = torch.addmm(w2c[:3, 3], pts, w2c[:3, :3].T)
    z = cam[:, 2]
    zc = z.clamp_min(1e-6)
    return fx * cam[:, 0] / zc + cx, fy * cam[:, 1] / zc + cy, z


def carve_frame(base_dir: Path, transforms: dict, entries: list[dict], *,
                bounds_min: np.ndarray, bounds_max: np.ndarray, device: str,
                voxel_size: float = DEFAULT_VOXEL_SIZE, carve_scale: float = 0.25,
                silhouette_threshold: float = DEFAULT_SILHOUETTE_THRESHOLD,
                view_slack: int = 0, bounds_margin: float = 0.05,
                chunk_voxels: int = 4_000_000):
    """Silhouette-carve one frame into a coloured world-space point cloud.

    `base_dir` is the object's directory, which `entries[*].file_path` is
    relative to.

    Returns (points (N, 3) float32, colors (N, 3) float32 in [0, 1]).
    """
    imgs, w, h = _load_views(base_dir, entries, carve_scale, device)
    masks = imgs.max(dim=3).values > silhouette_threshold        # (V, h, w)
    w2c = _world_to_cam(entries, device)
    n_views = imgs.shape[0]

    sx = w / float(transforms["w"])
    sy = h / float(transforms["h"])
    fx, fy = float(transforms["fl_x"]) * sx, float(transforms["fl_y"]) * sy
    cx, cy = float(transforms["cx"]) * sx, float(transforms["cy"]) * sy

    lo = torch.as_tensor(bounds_min, dtype=torch.float32, device=device) - bounds_margin
    hi = torch.as_tensor(bounds_max, dtype=torch.float32, device=device) + bounds_margin
    while True:
        dims = torch.clamp(((hi - lo) / voxel_size).long() + 1, min=2)
        if int(dims.prod()) <= MAX_GRID_VOXELS:
            break
        voxel_size *= 1.25    # bbox too large for this resolution; coarsen

    nx, ny, nz = (int(d) for d in dims)
    ix = torch.arange(nx, device=device)
    iy = torch.arange(ny, device=device)
    iz = torch.arange(nz, device=device)
    votes = torch.empty(nx * ny * nz, dtype=torch.uint8, device=device)

    # Vote per voxel over a z-major flattening, in slabs of whole z-planes so
    # the (potentially tens of millions of) voxel centres are never all
    # materialised at once.
    plane = nx * ny
    slab_planes = max(1, chunk_voxels // plane)
    for z0 in range(0, nz, slab_planes):
        z1 = min(nz, z0 + slab_planes)
        gz, gy, gx = torch.meshgrid(iz[z0:z1], iy, ix, indexing="ij")
        centres = lo + (torch.stack([gx.reshape(-1), gy.reshape(-1), gz.reshape(-1)], 1).float() + 0.5) * voxel_size
        hits = torch.zeros(centres.shape[0], dtype=torch.uint8, device=device)
        for v in range(n_views):
            u, vv, z = _project(centres, w2c[v], fx, fy, cx, cy)
            ui, vi = u.round().long(), vv.round().long()
            inside = (z > 0) & (ui >= 0) & (ui < w) & (vi >= 0) & (vi < h)
            sel = inside.nonzero(as_tuple=True)[0]
            hit = torch.zeros(centres.shape[0], dtype=torch.bool, device=device)
            hit[sel] = masks[v][vi[sel], ui[sel]]
            hits += hit.to(torch.uint8)
        votes[z0 * plane:z1 * plane] = hits
        del gz, gy, gx, centres, hits

    occupied = votes >= max(1, n_views - view_slack)
    lin = occupied.nonzero(as_tuple=True)[0]
    if lin.numel() == 0:
        raise RuntimeError("silhouette carving produced an empty volume — check "
                           "bounds/threshold for this object")
    vz = lin // plane
    vy = (lin % plane) // nx
    vx = lin % nx

    # Shell extraction: a voxel is on the surface if any 6-neighbour is empty
    # (out-of-grid counts as empty).
    neighbours = torch.zeros(lin.shape[0], dtype=torch.uint8, device=device)
    for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
        jx, jy, jz = vx + dx, vy + dy, vz + dz
        ok = (jx >= 0) & (jx < nx) & (jy >= 0) & (jy < ny) & (jz >= 0) & (jz < nz)
        j = (jz.clamp(0, nz - 1) * ny + jy.clamp(0, ny - 1)) * nx + jx.clamp(0, nx - 1)
        filled = torch.zeros_like(ok)
        filled[ok] = occupied[j[ok]]
        neighbours += filled.to(torch.uint8)
    shell = neighbours < 6
    idx = torch.stack([vx[shell], vy[shell], vz[shell]], dim=1).float()
    points = lo + (idx + 0.5) * voxel_size
    del votes, occupied

    # Visibility-aware colouring: per view, z-buffer the shell points and only
    # take colour from the views where a point is (within a voxel of) front-most.
    acc = torch.zeros(points.shape[0], 3, device=device)
    cnt = torch.zeros(points.shape[0], 1, device=device)
    for v in range(n_views):
        u, vv, z = _project(points, w2c[v], fx, fy, cx, cy)
        ui, vi = u.round().long(), vv.round().long()
        inside = (z > 0) & (ui >= 0) & (ui < w) & (vi >= 0) & (vi < h)
        pix = vi.clamp(0, h - 1) * w + ui.clamp(0, w - 1)
        depth = torch.where(inside, z, torch.full_like(z, float("inf")))
        zbuf = torch.full((h * w,), float("inf"), device=device)
        zbuf.scatter_reduce_(0, pix, depth, reduce="amin", include_self=True)
        front = inside & (depth <= zbuf[pix] + voxel_size * 1.5)
        sel = front.nonzero(as_tuple=True)[0]
        use = sel[masks[v][vi[sel], ui[sel]]]
        acc[use] += imgs[v][vi[use], ui[use]]
        cnt[use] += 1
    colors = torch.where(cnt > 0, acc / cnt.clamp_min(1.0), torch.full_like(acc, 0.5))

    return points.cpu().numpy(), colors.cpu().numpy()


def _uniform_scale(gs: GaussianSet, cameras: list[Camera], centre: torch.Tensor, k: float):
    """Scale scene and cameras about `centre` by `k`.

    A pinhole projection is invariant under scaling the world and the camera
    centres together, so the rendered image is unchanged — this only moves the
    cameras far enough out for the rasterizer to accept them (see
    `MIN_RENDER_DISTANCE`). Rotations, opacity and SH are scale-free; the
    Gaussians' extents scale with the positions.
    """
    scaled_gs = dataclasses.replace(
        gs,
        xyz=(centre + (gs.xyz - centre) * k).contiguous(),
        scale_raw=(gs.scale_raw + math.log(k)).contiguous(),
    )
    centre_np = centre.detach().cpu().numpy()
    scaled_cams = []
    for cam in cameras:
        eye = cam.camera_center.detach().cpu().numpy()
        new_eye = (centre_np + (eye - centre_np) * k).astype(np.float32)
        T = (-cam.R.T @ new_eye).astype(np.float32)
        scaled_cams.append(Camera(R=cam.R, T=T, fovx=cam.fovx, fovy=cam.fovy,
                                  width=cam.width, height=cam.height,
                                  znear=cam.znear, zfar=cam.zfar * max(k, 1.0),
                                  device=cam.device))
    return scaled_gs, scaled_cams


def refine_gaussians(gs: GaussianSet, cameras: list[Camera], images: torch.Tensor, *,
                     n_iters: int, extent: float, bg: torch.Tensor | None = None,
                     seed: int = 0, min_render_distance: float = MIN_RENDER_DISTANCE) -> GaussianSet:
    """Short 3DGS fit of `gs` to the frame's own real views, using the paper's
    loss (Eq. 2, `vega.metrics.vega_loss`).

    Optional, and off by default: carving alone already gives the encoder
    usable geometry, and this costs a full render+backward per iteration. It
    earns its keep on the concavities an 8-view horizontal ring cannot carve.

    Attributes only — no densification, cloning or pruning. The Gaussian count
    is what the encoder's rate model (§5.2) prices per frame and what
    `vega.filtering` reuses across a GOV, so holding it fixed here keeps the
    bitstream a function of `max_points_per_frame` alone.
    """
    if n_iters <= 0:
        return gs

    # The rig cameras are too close for this rasterizer to draw anything, which
    # would make every render black and every gradient zero — the refinement
    # would silently do nothing. Scale the whole setup out, fit there, scale
    # back; the images involved are identical either way.
    centre = gs.xyz.detach().mean(dim=0)
    nearest = min(float((cam.camera_center - centre).norm().item()) for cam in cameras)
    k = 1.0
    if nearest < min_render_distance:
        k = min_render_distance / max(nearest, 1e-6)
        gs, cameras = _uniform_scale(gs, cameras, centre, k)

    gs = gs.detach().requires_grad_(True)
    bg = torch.zeros(3, device=gs.xyz.device) if bg is None else bg
    opt = torch.optim.Adam([
        {"params": [gs.xyz], "lr": 1.6e-4 * max(extent * k, 1e-3)},
        {"params": [gs.scale_raw], "lr": 5e-3},
        {"params": [gs.rot_raw], "lr": 1e-3},
        {"params": [gs.opacity_raw], "lr": 5e-2},
        {"params": [gs.sh_dc], "lr": 2.5e-3},
        {"params": [gs.sh_rest], "lr": 1.25e-4},
    ], eps=1e-15)

    g = torch.Generator().manual_seed(seed)
    for _ in range(n_iters):
        v = int(torch.randint(len(cameras), (1,), generator=g).item())
        pred = render(cameras[v], gs, bg)["render"]
        loss = vega_loss(pred, images[v])
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()

    gs = gs.detach()
    if k != 1.0:
        gs, _ = _uniform_scale(gs, [], centre, 1.0 / k)
    return gs


def load_scene(dataset_root: str | Path, scene_name: str, frame_indices: list[int],
               device: str = "cuda", max_points_per_frame: int = 120_000,
               image_scale: float = 0.125, carve_scale: float = DEFAULT_CARVE_SCALE,
               voxel_size: float = DEFAULT_VOXEL_SIZE, view_slack: int = DEFAULT_VIEW_SLACK,
               silhouette_threshold: float = DEFAULT_SILHOUETTE_THRESHOLD,
               load_gt_images: bool = False, refine_iters: int = 0,
               prune_opacity: float = DEFAULT_PRUNE_OPACITY, seed: int = 0,
               verbose: bool = False):
    """Load `frame_indices` (0-based `frame_index` values) of `scene_name`.

    Returns:
        frames: list[GaussianSet] (object_id all zero — run segmentation next)
        cameras: list[Camera], the 8 fixed calibrated rig cameras (shared by
            all frames), built at `image_scale`
        gt_images: list[list[Tensor]]; gt_images[t][view] is a (3, H, W) real
            render, or an empty list per frame unless `load_gt_images` is set
            (Vega's encoder derives its own oracle by re-rendering each
            frame's attributes, so it does not need these, and at 4096x3072
            native they are large). `refine_iters > 0` needs them and loads
            them regardless, but still only returns them when asked.
        bounds_min, bounds_max: torch (3,) tensors, the object's known
            sequence-wide bounding box
    """
    dataset_root = Path(dataset_root)
    transforms_path = object_transforms_path(dataset_root, scene_name)
    base_dir = transforms_path.parent
    with open(transforms_path) as f:
        transforms = json.load(f)
    grouped = group_frames(transforms)
    n_available = len(grouped)
    for t in frame_indices:
        if not 0 <= t < n_available:
            raise IndexError(f"frame_index {t} out of range for {scene_name!r} "
                             f"(0..{n_available - 1})")

    # One rig shared by every frame is an assumption this loader leans on (the
    # cameras are built once and reused for refinement and for the returned
    # gt_images). It holds for this corpus; assert it rather than silently
    # using the first selected frame's rig if a future capture varies.
    rig = [entry["camera_to_world_opencv"] for entry in grouped[frame_indices[0]]]
    for t in frame_indices[1:]:
        other = [entry["camera_to_world_opencv"] for entry in grouped[t]]
        if other != rig:
            raise ValueError(
                f"{scene_name!r} frame_index {t} has different camera extrinsics than "
                f"frame_index {frame_indices[0]}; this loader assumes one fixed rig")
    cameras = [build_camera(transforms, entry, image_scale, device=device)
               for entry in grouped[frame_indices[0]]]
    bounds_min = np.asarray(transforms["bounds_min"], dtype=np.float32)
    bounds_max = np.asarray(transforms["bounds_max"], dtype=np.float32)

    rng = np.random.default_rng(seed)
    frames, gt_images = [], []
    for t in frame_indices:
        entries = grouped[t]
        pts, cols = carve_frame(
            base_dir, transforms, entries, bounds_min=bounds_min, bounds_max=bounds_max,
            device=device, voxel_size=voxel_size, carve_scale=carve_scale,
            silhouette_threshold=silhouette_threshold, view_slack=view_slack)
        n = pts.shape[0]
        if n > max_points_per_frame:
            keep = rng.choice(n, size=max_points_per_frame, replace=False)
            pts, cols = pts[keep], cols[keep]
        gs = points_to_gaussians(pts, cols, bounds_min, bounds_max, device)

        views = None
        if load_gt_images or refine_iters > 0:
            imgs, _, _ = _load_views(base_dir, entries, image_scale, device)
            views = torch.stack([imgs[v].permute(2, 0, 1).contiguous()
                                 for v in range(imgs.shape[0])])
        n_refined = None
        if refine_iters > 0:
            gs = refine_gaussians(gs, cameras, views, n_iters=refine_iters,
                                  extent=float(np.max(bounds_max - bounds_min)),
                                  seed=seed + t)
            if prune_opacity > 0.0:
                keep = gs.get_opacity.squeeze(1) >= prune_opacity
                if bool(keep.any()) and not bool(keep.all()):
                    n_refined = len(gs)
                    gs = gs.subset(keep)
        if verbose:
            extra = f" (+{refine_iters} refine iters)" if refine_iters else ""
            if n_refined is not None:
                extra += f", pruned {n_refined - len(gs)} transparent"
            print(f"      frame_index {t:3d}: {n} hull points -> {len(gs)} Gaussians{extra}",
                  flush=True)
        frames.append(gs)
        gt_images.append([views[v] for v in range(views.shape[0])] if load_gt_images else [])

    return (frames, cameras, gt_images,
            torch.as_tensor(bounds_min, device=device), torch.as_tensor(bounds_max, device=device))
