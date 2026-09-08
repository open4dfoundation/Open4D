"""A ReRF frame as an explicit point cloud.

ReRF's own repository does this, which I had missed: ``tools/vis_volume.py``
thresholds the density grid, takes the occupied voxels, and colours them --

    xyz = np.stack((alpha > thres).nonzero(), -1)
    color = rgb[xyz[:,0], xyz[:,1], xyz[:,2]]

-- so "a neural field has no geometry to send" was wrong. It has geometry the
moment you are willing to commit to a threshold.

Upstream reads that volume out of a *training checkpoint*
(``run.py --export_coarse_only``, which loads ``coarse_last_0.tar``). There are
none on this machine. This module does the same two operations on a frame
decoded from the **bitstream** instead, which is what a receiver actually has,
so a point cloud is available for every frame of a stream rather than only for
runs whose training artefacts survived.

Two differences from upstream's tool, both deliberate:

* Voxel centres come from ``linspace(xyz_min, xyz_max, shape)``, the convention
  ``lib/dvgo.py`` itself samples against. ``vis_volume.py`` uses
  ``xyz / shape * (max - min) + min``, which is off by half a voxel -- fine for
  a viewer, not for geometry that has to line up with another method's.
* Colour comes from the rgb network, because ``k0`` here is 12 feature channels
  and not RGB. That network is view-dependent, so the colour has to be baked at
  one direction and frozen -- the same compromise Vega's ``.splat`` export
  makes, and worth stating for the same reason.

**What it costs.** Measured on `g_basketball` frame 0, projected into training
camera 0 and scored against the photograph: the point cloud reaches 31.3 dB
where the ray-march of the same frame reaches 45.5 dB. Thresholding a
continuous density field into occupied-or-not discards the soft edges the
volume render integrates over, and no choice of threshold buys them back. So
this is a real representation of the same reconstruction and a visibly coarser
one -- which is the trade for a camera the browser can aim anywhere.
"""
from __future__ import annotations

import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

#: Density above which a voxel is called occupied. 0.35 measured best of
#: 0.2/0.35/0.5 against the photograph -- lower keeps a halo of near-empty
#: voxels, higher eats the surface.
THRESHOLD = 0.35


@dataclass(frozen=True)
class PointCloud:
    """Occupied voxels of one frame, in the corpus's world frame."""

    xyz: np.ndarray               # [N, 3] float32
    rgb: np.ndarray               # [N, 3] uint8

    @property
    def count(self) -> int:
        return int(self.xyz.shape[0])

    @property
    def bounds(self) -> tuple[list[float], list[float]]:
        return self.xyz.min(axis=0).tolist(), self.xyz.max(axis=0).tolist()


def _world_transform(corpus_dir) -> tuple[np.ndarray, float]:
    """``world = normalised / scale + centre``, as the corpus records it.

    The model is trained and decoded in a normalised frame; a bundle's rig and
    every other method's geometry are in world coordinates. Skipping this puts
    a correct point cloud in the wrong place, which looks like a broken
    reconstruction rather than a missing transform.
    """
    with open(Path(corpus_dir) / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    return (np.asarray(manifest["world_centre"], dtype=np.float64),
            float(manifest["world_scale"]))


def point_cloud(model, corpus_dir, *, threshold: float = THRESHOLD,
                azimuth: float = 0.0) -> PointCloud:
    """The occupied voxels of the frame currently installed in ``model``.

    ``azimuth`` is the direction, in degrees about the world up axis, that
    colour is baked at. There is one colour per point and the network's is
    view-dependent, so some direction has to be chosen; 0 matches what the
    Vega export does.
    """
    import torch

    with torch.no_grad():
        alpha = model.activate_density(model.density).squeeze()
        keep = alpha > threshold
        if not bool(keep.any()):
            raise ValueError(
                f"no voxel has density above {threshold}; the frame decoded to "
                "an empty grid, or the threshold is above its maximum "
                f"({float(alpha.max()):.4f})"
            )
        # The renderer's own voxel centres (`lib/dvgo.py`'s upsample path), so
        # a point sits where a ray would have sampled it.
        axes = [
            torch.linspace(float(model.xyz_min[axis]), float(model.xyz_max[axis]),
                           alpha.shape[axis], device=alpha.device)
            for axis in range(3)
        ]
        grid = torch.stack(torch.meshgrid(*axes, indexing="ij"), -1)
        xyz = grid[keep]

        # Colour: k0 features at each point, plus the encoded view direction,
        # through the rgb net -- the same path `dvgo.forward` takes.
        k0 = model.grid_sampler_new(xyz, model.k0)
        angle = math.radians(azimuth)
        direction = torch.tensor(
            [math.sin(angle), 0.0, math.cos(angle)],
            dtype=torch.float32, device=xyz.device,
        ).expand_as(xyz)
        frequencies = model.viewfreq.to(xyz.device)
        embedded = (direction.unsqueeze(-1) * frequencies).flatten(-2)
        embedded = torch.cat([direction, embedded.sin(), embedded.cos()], -1)
        rgb = torch.sigmoid(model.rgbnet(torch.cat([k0, embedded], -1)))

        points = xyz.double().cpu().numpy()
        colours = rgb.clamp(0, 1).cpu().numpy()

    centre, scale = _world_transform(corpus_dir)
    points = points / scale + centre
    return PointCloud(
        xyz=points.astype(np.float32),
        rgb=np.rint(colours * 255.0).clip(0, 255).astype(np.uint8),
    )


def rasterise(cloud: PointCloud, camera, *, radius: int = 2) -> np.ndarray:
    """The cloud drawn into ``camera``, nearest point winning each pixel.

    Deliberately crude -- a square splat and a depth test, no blending and no
    per-point size. It exists to put a number on what thresholding the density
    field costs, not to be the renderer; the browser's point renderer is a
    different one, so the figure this produces is the cost of the
    *representation* rather than of any particular rasteriser. Said in the clip
    note for the same reason.
    """
    world_to_camera = np.linalg.inv(camera.c2w)
    local = cloud.xyz @ world_to_camera[:3, :3].T + world_to_camera[:3, 3]
    depth = local[:, 2]
    in_front = depth > 1e-6
    u = np.rint(camera.fx * local[in_front, 0] / depth[in_front] + camera.cx)
    v = np.rint(camera.fy * local[in_front, 1] / depth[in_front] + camera.cy)
    u, v = u.astype(np.int64), v.astype(np.int64)
    z = depth[in_front]
    colour = cloud.rgb[in_front].astype(np.float32) / 255.0

    canvas = np.zeros((camera.height, camera.width, 3), np.float32)
    nearest = np.full((camera.height, camera.width), np.inf, np.float32)
    for du in range(-radius, radius + 1):
        for dv in range(-radius, radius + 1):
            uu, vv = u + du, v + dv
            inside = ((uu >= 0) & (uu < camera.width)
                      & (vv >= 0) & (vv < camera.height))
            iu, iv, iz, ic = uu[inside], vv[inside], z[inside], colour[inside]
            # Far to near, so a nearer point overwrites a farther one and the
            # comparison below does not depend on input order.
            order = np.argsort(-iz)
            iu, iv, iz, ic = iu[order], iv[order], iz[order], ic[order]
            closer = iz < nearest[iv, iu]
            canvas[iv[closer], iu[closer]] = ic[closer]
            nearest[iv[closer], iu[closer]] = iz[closer]
    return canvas


def fidelity(player, cloud: PointCloud, *, view: int = 0) -> dict:
    """What this frame's point cloud scores against the photograph, and the
    ray-march of the same frame for comparison.

    Measured per subject rather than quoted from one. The note used to carry
    basketball's 31.3 dB on every subject's clip, which states a measurement
    of one reconstruction as a fact about another -- and the spread turns out
    to matter, since how much of a density field clears the threshold varies a
    lot between subjects.
    """
    from .cameras import captured_image, psnr, training_cameras

    camera = training_cameras(player.corpus_dir)[view]
    photograph = captured_image(player.corpus_dir, 0, view,
                                background=player.background)
    return {
        "points_psnr": round(float(psnr(rasterise(cloud, camera), photograph)), 2),
        "march_psnr": round(float(psnr(player.render(camera), photograph)), 2),
        "against": f"training camera {view}, frame 0",
    }


def write_ply(path: Path | str, cloud: PointCloud) -> int:
    """Binary little-endian PLY, the one dialect the client's parser reads.

    float x/y/z and uchar red/green/blue: 15 bytes a point. Written by hand
    rather than through a library so this package keeps depending on nothing
    the decoder does not already need.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (
        "ply\n"
        "format binary_little_endian 1.0\n"
        f"element vertex {cloud.count}\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\n"
        "end_header\n"
    ).encode("ascii")
    # One structured array rather than a row loop: 110k points a frame, thirty
    # frames, and the loop was the slowest part of the export.
    packed = np.empty(cloud.count, dtype=np.dtype([
        ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
        ("r", "u1"), ("g", "u1"), ("b", "u1"),
    ]))
    packed["x"], packed["y"], packed["z"] = cloud.xyz.T
    packed["r"], packed["g"], packed["b"] = cloud.rgb.T
    with open(path, "wb") as out:
        out.write(header)
        out.write(packed.tobytes())
    return path.stat().st_size


def read_ply(path: Path | str) -> PointCloud:
    """Read back what :func:`write_ply` wrote, for checking a round trip."""
    data = Path(path).read_bytes()
    marker = data.index(b"end_header\n") + len(b"end_header\n")
    header = data[:marker].decode("ascii")
    count = next(int(line.split()[2]) for line in header.splitlines()
                 if line.startswith("element vertex"))
    packed = np.frombuffer(data, offset=marker, count=count, dtype=np.dtype([
        ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
        ("r", "u1"), ("g", "u1"), ("b", "u1"),
    ]))
    return PointCloud(
        xyz=np.stack([packed["x"], packed["y"], packed["z"]], axis=-1),
        rgb=np.stack([packed["r"], packed["g"], packed["b"]], axis=-1),
    )


def export(player, out_dir, *, name: str, scene: str,
           threshold: float = THRESHOLD, azimuth: float = 0.0,
           frames: int = 0, measure: bool = True) -> dict:
    """Every frame of ``player`` as a point-cloud clip, plus its sidecar.

    One clip rather than one per viewpoint, which is the whole point: this is
    geometry, so the client aims the camera itself and there is nothing to
    pre-render. Written in the same ``rerf-clips`` shape ``export.py`` uses, so
    ``streamer.adopt`` takes it without knowing it came from a different
    extractor.
    """
    import time

    out = Path(out_dir)
    clip = f"{name}-rerf-points"
    wanted = frames if frames > 0 else len(player.frames)
    began = time.time()
    paths, counts, bounds, total = [], [], [], 0
    scored = None
    for frame in player.play(loop=False):
        if frame.index >= wanted:
            break
        cloud = point_cloud(frame.model, player.corpus_dir,
                            threshold=threshold, azimuth=azimuth)
        if scored is None and measure:
            # Frame 0 only: the point is what the conversion costs, and paying
            # for a ray-march of every frame to average it would double the
            # export for a figure that moves by tenths.
            scored = fidelity(player, cloud)
            print(f"  fidelity: points {scored['points_psnr']} dB, "
                  f"march {scored['march_psnr']} dB "
                  f"({scored['against']})", flush=True)
        relative = f"{clip}/frame_{frame.index:04d}.ply"
        total += write_ply(out / relative, cloud)
        paths.append(relative)
        counts.append(cloud.count)
        bounds.append(cloud.bounds)
        print(f"  frame {frame.index:3d}  {cloud.count:7d} points", flush=True)

    if not paths:
        raise ValueError("the bitstream yielded no frames")
    lows = [min(b[0][axis] for b in bounds) for axis in range(3)]
    highs = [max(b[1][axis] for b in bounds) for axis in range(3)]
    payload = {
        "format": "rerf-clips",
        "version": 1,
        "scene": scene,
        "representation": "points",
        "source": str(player.path),
        "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "bitstream_bytes": player.bitstream_bytes,
        "clips": [{
            "name": clip,
            # "rerf", not "rerf-points": this is the same method, read out a
            # different way. A separate method name put a 3D model in a row
            # beside the images and left the row called "rerf" resolving to a
            # pre-rendered view -- so selecting ReRF still got you a picture,
            # which is exactly what having geometry was supposed to fix.
            #
            # `clipFor` then does the right thing per mode with no special
            # case: Explore takes the geometry, because a free camera can
            # rasterise it anywhere, and Compare takes the ray-march at the
            # station, because at a fixed pose the march is 14 dB better.
            "method": "rerf",
            "camera": None,
            "frames": paths,
            "counts": counts,
            "bounds_min": lows,
            "bounds_max": highs,
            "notes": [
                f"ReRF's density field thresholded at {threshold} and read out "
                "as points — the operation upstream's tools/vis_volume.py "
                "does, taken from the bitstream instead of a training "
                "checkpoint, so it is available for every frame a receiver has",
                "this is geometry, so the camera is genuinely free: the browser "
                "rasterises whatever viewpoint is asked for, with no "
                "pre-rendered viewpoints and nothing to snap to",
                (
                    f"coarser than the ray-march of the same frame — "
                    f"{scored['points_psnr']} dB against the photograph where "
                    f"the march reaches {scored['march_psnr']} — because a "
                    "threshold discards the soft edges a volume render "
                    "integrates over. Scored with a nearest-z point splat, so "
                    "it is the cost of the representation rather than of any "
                    "particular renderer"
                    if scored else
                    "coarser than the ray-march of the same frame, because a "
                    "threshold discards the soft edges a volume render "
                    "integrates over; export without --no-measure to score it"
                ),
                f"colour baked at azimuth {azimuth:g}° and frozen: ReRF's "
                "colour comes from a view-dependent network and a point's does "
                "not",
            ],
            "variants": [],
            "detail": {
                "source": str(player.path),
                "renderer": "rerf_stream.geometry",
                "threshold": threshold,
                "colour_azimuth": azimuth,
                "depicts": "appearance",
                "grid": "x".join(str(int(v)) for v in player.model.world_size),
                **({"fidelity": scored} if scored else {}),
            },
        }],
    }
    (out / "clips.json").write_text(json.dumps(payload, indent=2) + "\n")
    seconds = time.time() - began
    print(f"\n{len(paths)} frames, {total / 1e6:.1f} MB in {seconds:.0f}s "
          f"({total / len(paths) / 1e6:.2f} MB a frame, "
          f"{sum(counts) // len(counts)} points on average)")
    print(f"  wrote {out / 'clips.json'}")
    return payload


def main(argv=None) -> int:
    import argparse
    import shutil

    from .bitstream import BitstreamPlayer

    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True)
    parser.add_argument("--compression-path", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--name", default="rerf",
                        help="clip name prefix, e.g. the object's name")
    parser.add_argument("--scene", required=True,
                        help="the subject this reconstructs, shared with other methods")
    parser.add_argument("--threshold", type=float, default=THRESHOLD,
                        help="density above which a voxel becomes a point")
    parser.add_argument("--azimuth", type=float, default=0.0,
                        help="direction, in degrees, that colour is baked at")
    parser.add_argument("--frames", type=int, default=0, help="0 means all of them")
    parser.add_argument("--no-measure", action="store_true",
                        help="skip scoring the cloud against the photograph, "
                             "which costs one extra ray-march")
    parser.add_argument("--no-pca", action="store_true")
    parser.add_argument("--pca_chs", default="7,13")
    parser.add_argument("--group-size", type=int, default=0)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)

    out = Path(args.out).expanduser().resolve()
    if out.exists() and args.overwrite:
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    player = BitstreamPlayer(
        args.config, args.compression_path,
        pca=not args.no_pca,
        pca_channels=[int(c) for c in args.pca_chs.split(",")],
        group_size=args.group_size,
    )
    export(player, out, name=args.name, scene=args.scene,
           threshold=args.threshold, azimuth=args.azimuth,
           frames=args.frames, measure=not args.no_measure)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
