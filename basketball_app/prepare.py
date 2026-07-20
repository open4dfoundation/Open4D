"""Build comparison animations and metrics for the basketball codec app.

One-time asset builder. For the source reference and each codec (N4MC, QNDF,
TVMC, TSMC, Draco, KLT, V-DMC) it renders the 10-frame basketball sequence as a
shaded GIF and an error-heatmap GIF (decoded surface distance to the source,
shared scale), and writes shared surface metrics to metrics.json. Codecs that
expose a compressed bitstream (Draco's .drc, KLT coefficients, V-DMC's .vmesh)
also report compressed_kb.

Usage:
    python prepare.py            # build every asset
    python prepare.py --test     # render one shaded + one heatmap PNG to check
                                  # camera/orientation, then exit
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

import numpy as np
import trimesh
import open3d as o3d
from scipy.spatial import cKDTree
from PIL import Image
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import cm, colors as mcolors
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

APP = Path(__file__).resolve().parent
ROOT = Path(os.environ.get("OPEN4D_ROOT", APP.parent))
ASSETS = APP / "assets"

FRAMES = list(range(11, 21))          # basketball_player_fr0011 .. fr0020
HEAT_MAX_PCT = 1.5                    # shared heatmap scale (% of bbox diagonal)
RENDER_FACES = 20000                  # decimation target for rendering
SAMPLES = 50000                       # surface samples for metrics
ELEV, AZIM = 8, -80                   # fixed camera
FPS = 6

METHODS = ["N4MC", "QNDF", "TVMC", "TSMC", "Draco", "KLT", "VDMC"]
DRACO_QP = 11                        # Draco operating point shown in the app
COLORS = {
    "reference": "#B8C0CC",
    "N4MC": "#F6C453",
    "QNDF": "#FF746C",
    "TVMC": "#31C6A5",
    "TSMC": "#9A7DFF",
    "Draco": "#5B9BD5",
    "KLT": "#E08A3C",
    "VDMC": "#D6409F",
}

REF_DIR = ROOT / "open4d/modules/tvmc/arap-volume-tracking/data/basketball_player"


def ref_path(n: int) -> Path:
    return REF_DIR / f"basketball_player_fr{n:04d}.obj"


def method_path(method: str, n: int) -> Path | None:
    m = ROOT / "open4d/modules"
    if method == "N4MC":
        hits = sorted((m / "N4MC/outputs/basketball_sequence_n4mc/original_scale").glob(
            f"*fr{n:04d}*reconstructed.ply"))
        return hits[0] if hits else None
    if method == "QNDF":
        return m / ("Quantized-Neural-Displacement-Fields/outputs/basketball_sequence_qndf/"
                    f"basketball_player_fr{n:04d}/reconstruction_original_scale.obj")
    if method == "TVMC":
        return m / f"tvmc/TVMC/basketball_player_outputs/decoded_basketball_player_fr{n:04d}.obj"
    if method == "TSMC":
        return m / ("tsmc/outputs/basketball_sequence_tsmc/decoded/"
                    f"decoded_basketball_player_fr{n:04d}.obj")
    if method == "Draco":
        return m / (f"Draco/outputs/basketball_sequence_draco/decode/qp_{DRACO_QP}/"
                    f"basketball_player_fr{n:04d}_qp_{DRACO_QP}_decoded.obj")
    if method == "KLT":
        return m / ("KLT/outputs/basketball_sequence_klt/decoded/"
                    f"decoded_basketball_player_fr{n:04d}.obj")
    if method == "VDMC":
        return m / ("mpeg-vdmc-tm/outputs/basketball_sequence_vdmc/decoded/"
                    f"decoded_basketball_player_fr{n:04d}.obj")
    return None


# Codecs whose compressed bitstream is a single sequence-level file rather than
# one file per frame. Reported per-frame as (total size / frame count).
SEQ_BITSTREAM = {
    "VDMC": "mpeg-vdmc-tm/outputs/basketball_sequence_vdmc/compressed/basketball_sequence.vmesh",
}


def compressed_paths(method: str, n: int) -> list[Path]:
    """Files making up the per-frame compressed bitstream, when one exists.

    Only defined for codecs that write a compressed representation to disk:
    Draco's single .drc, and KLT's quantized coefficient indices + codebook
    metadata. The neural codecs don't expose a comparable per-frame bitstream
    here, so they return [] and are reported without a compressed size.

    Note: the KLT figure counts the per-frame coefficients + codebook but not the
    shared KLT basis, which is amortized across the sequence and not written to
    disk by klt.py.
    """
    m = ROOT / "open4d/modules"
    if method == "Draco":
        return [m / (f"Draco/outputs/basketball_sequence_draco/encode/qp_{DRACO_QP}/"
                     f"basketball_player_fr{n:04d}_qp_{DRACO_QP}.drc")]
    if method == "KLT":
        base = m / "KLT/outputs/basketball_sequence_klt/compressed"
        k = n - 11  # recon frame index for reference frame n
        return [base / f"{k:04d}_quantized_indices.zst",
                base / f"{k:04d}_quantized_metadata.npz"]
    return []


def load_mesh(path: Path) -> trimesh.Trimesh:
    mesh = trimesh.load(path, force="mesh", process=False)
    if not isinstance(mesh, trimesh.Trimesh) or len(mesh.faces) == 0:
        raise ValueError(f"not a triangle mesh: {path}")
    return mesh


def to_o3d(mesh: trimesh.Trimesh) -> o3d.geometry.TriangleMesh:
    return o3d.geometry.TriangleMesh(
        o3d.utility.Vector3dVector(np.asarray(mesh.vertices, dtype=float)),
        o3d.utility.Vector3iVector(np.asarray(mesh.faces, dtype=np.int32)),
    )


def decimate(mesh: trimesh.Trimesh, target: int) -> trimesh.Trimesh:
    if len(mesh.faces) <= target:
        return mesh
    reduced = to_o3d(mesh).simplify_quadric_decimation(target_number_of_triangles=target)
    reduced.remove_degenerate_triangles()
    reduced.remove_unreferenced_vertices()
    return trimesh.Trimesh(np.asarray(reduced.vertices), np.asarray(reduced.triangles),
                           process=False)


def error_pct(reference: trimesh.Trimesh, vertices: np.ndarray, diagonal: float) -> np.ndarray:
    """Distance from each query vertex to the reference surface, as % of bbox diagonal."""
    scene = o3d.t.geometry.RaycastingScene()
    scene.add_triangles(o3d.t.geometry.TriangleMesh.from_legacy(to_o3d(reference)))
    d = scene.compute_distance(o3d.core.Tensor(np.asarray(vertices, dtype=np.float32))).numpy()
    return d / diagonal * 100.0


def render(vertices: np.ndarray, faces: np.ndarray, bounds, color: str | None = None,
           face_error: np.ndarray | None = None) -> np.ndarray:
    tri = vertices[faces][:, :, [0, 2, 1]]  # data (x,y,z) -> plot (x,z,y): show Y-up upright
    e1, e2 = tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]
    normals = np.cross(e1, e2)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    light = np.array([0.4, -0.6, 0.7]); light /= np.linalg.norm(light)
    shade = np.clip(0.30 + 0.70 * np.abs(normals @ light), 0.0, 1.0)
    if face_error is not None:
        base = cm.turbo(np.clip(face_error / HEAT_MAX_PCT, 0.0, 1.0))[:, :3]
    else:
        base = np.tile(np.array(mcolors.to_rgb(color)), (len(faces), 1))
    rgb = np.clip(base * shade[:, None], 0.0, 1.0)

    (x0, y0, z0), (x1, y1, z1) = bounds
    fig = plt.figure(figsize=(3.0, 4.2), dpi=110)
    ax = fig.add_subplot(111, projection="3d")
    ax.add_collection3d(Poly3DCollection(
        tri, facecolors=np.c_[rgb, np.ones(len(rgb))], edgecolors="none"))
    ax.set_xlim(x0, x1); ax.set_ylim(z0, z1); ax.set_zlim(y0, y1)
    ax.set_box_aspect((x1 - x0, z1 - z0, y1 - y0))
    ax.view_init(elev=ELEV, azim=AZIM)
    ax.set_axis_off()
    fig.subplots_adjust(left=0, right=1, bottom=0, top=1)
    fig.canvas.draw()
    w, h = fig.canvas.get_width_height()
    img = np.frombuffer(fig.canvas.buffer_rgba(), np.uint8).reshape(h, w, 4)[..., :3].copy()
    plt.close(fig)
    return img


def save_gif(path: Path, frames: list[np.ndarray]) -> None:
    imgs = [Image.fromarray(f) for f in frames]
    imgs[0].save(path, save_all=True, append_images=imgs[1:],
                 duration=int(1000 / FPS), loop=0, disposal=2)


def frame_metrics(reference: trimesh.Trimesh, decoded: trimesh.Trimesh,
                  diagonal: float, decoded_path: Path) -> dict:
    rp, _ = trimesh.sample.sample_surface(reference, SAMPLES, seed=1)
    dp, _ = trimesh.sample.sample_surface(decoded, SAMPLES, seed=2)
    d1, _ = cKDTree(dp).query(rp, workers=-1)
    d2, _ = cKDTree(rp).query(dp, workers=-1)
    dd = np.concatenate([d1, d2])
    return {
        "chamfer_nrmse_pct": float(np.sqrt(np.mean(dd ** 2)) / diagonal * 100),
        "p95_pct": float(np.percentile(dd, 95) / diagonal * 100),
        "faces": int(len(decoded.faces)),
        "decoded_kb": round(decoded_path.stat().st_size / 1024, 1),
    }


def colorbar() -> None:
    fig, ax = plt.subplots(figsize=(6, 0.7), dpi=110)
    grad = np.linspace(0, 1, 256)[None, :]
    ax.imshow(grad, aspect="auto", cmap="turbo", extent=[0, HEAT_MAX_PCT, 0, 1])
    ax.set_yticks([])
    ax.set_xlabel("decoded-to-source distance  (% of bounding-box diagonal)")
    ax.set_title(f"error scale  ·  0 – {HEAT_MAX_PCT}%", fontsize=9)
    fig.subplots_adjust(left=0.03, right=0.97, bottom=0.55, top=0.72)
    fig.savefig(ASSETS / "colorbar.png")
    plt.close(fig)


def compute_bounds(refs: dict[int, trimesh.Trimesh]):
    allv = np.vstack([np.asarray(refs[n].vertices) for n in FRAMES])
    lo, hi = allv.min(0), allv.max(0)
    pad = (hi - lo) * 0.05
    return (tuple(lo - pad), tuple(hi + pad))


def build() -> None:
    ASSETS.mkdir(exist_ok=True)
    refs = {n: load_mesh(ref_path(n)) for n in FRAMES}
    bounds = compute_bounds(refs)

    print("reference ...", flush=True)
    frames = [render(np.asarray((r := decimate(refs[n], RENDER_FACES)).vertices),
                     np.asarray(r.faces), bounds, color=COLORS["reference"]) for n in FRAMES]
    save_gif(ASSETS / "reference.gif", frames)

    metrics = {}
    for method in METHODS:
        print(f"{method} ...", flush=True)
        shaded, heat, per, comp_kb = [], [], [], []
        for n in FRAMES:
            path = method_path(method, n)
            decoded, reference = load_mesh(path), refs[n]
            diagonal = float(np.linalg.norm(np.ptp(np.asarray(reference.vertices), axis=0)))
            r = decimate(decoded, RENDER_FACES)
            v, f = np.asarray(r.vertices), np.asarray(r.faces)
            verr = error_pct(reference, v, diagonal)
            shaded.append(render(v, f, bounds, color=COLORS[method]))
            heat.append(render(v, f, bounds, face_error=verr[f].mean(1)))
            per.append(frame_metrics(reference, decoded, diagonal, path))
            cps = compressed_paths(method, n)
            if cps and all(p.exists() for p in cps):
                comp_kb.append(sum(p.stat().st_size for p in cps) / 1024)
        save_gif(ASSETS / f"{method.lower()}.gif", shaded)
        save_gif(ASSETS / f"{method.lower()}_heat.gif", heat)
        keys = ("chamfer_nrmse_pct", "p95_pct", "faces", "decoded_kb")
        metrics[method] = {k: round(float(np.mean([p[k] for p in per])), 3) for k in keys}
        # True compressed bitstream size, when the codec exposes one (e.g. Draco .drc)
        if comp_kb:
            metrics[method]["compressed_kb"] = round(float(np.mean(comp_kb)), 3)
        # Sequence-level bitstream (e.g. V-DMC .vmesh): report total / frame count
        elif method in SEQ_BITSTREAM:
            seq = ROOT / "open4d/modules" / SEQ_BITSTREAM[method]
            if seq.exists():
                metrics[method]["compressed_kb"] = round(
                    seq.stat().st_size / 1024 / len(FRAMES), 3)

    (APP / "metrics.json").write_text(json.dumps(metrics, indent=2))
    colorbar()
    print("done ->", ASSETS, flush=True)


def test() -> None:
    ASSETS.mkdir(exist_ok=True)
    refs = {n: load_mesh(ref_path(n)) for n in FRAMES[:1]}
    bounds = compute_bounds({11: refs[11], **{n: refs[11] for n in FRAMES}})
    r = decimate(refs[11], RENDER_FACES)
    v, f = np.asarray(r.vertices), np.asarray(r.faces)
    Image.fromarray(render(v, f, bounds, color=COLORS["reference"])).save(ASSETS / "_test_shaded.png")
    diag = float(np.linalg.norm(np.ptp(np.asarray(refs[11].vertices), axis=0)))
    dec = decimate(load_mesh(method_path("QNDF", 11)), RENDER_FACES)
    dv, df = np.asarray(dec.vertices), np.asarray(dec.faces)
    verr = error_pct(refs[11], dv, diag)
    Image.fromarray(render(dv, df, bounds, face_error=verr[df].mean(1))).save(ASSETS / "_test_heat.png")
    print("wrote", ASSETS / "_test_shaded.png", "and", ASSETS / "_test_heat.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", action="store_true", help="render one shaded + one heatmap PNG and exit")
    args = ap.parse_args()
    test() if args.test else build()
