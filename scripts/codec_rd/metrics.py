"""Geometry and image quality metrics for codec rate-distortion studies.

Mesh metrics compare surfaces, not vertices. Both meshes are sampled uniformly
by area with a fixed seed, so different tessellations of the same surface score
alike. Definitions follow MPEG point-cloud practice (pc_error / mmetric):

* D1 (point-to-point): for every sample ``a`` of one surface, the squared
  distance to the nearest sample of the other. D2 (point-to-plane): that error
  vector projected onto the *reference* surface normal, in both directions, as
  pc_error does with normals supplied for the original only.
* Each direction gives an MSE; the symmetric MSE is the worse (larger) of the
  two, and ``PSNR = 10 log10(peak^2 / MSE)``. The peak is a caller choice; the
  harness uses the bounding-box diagonal of the reference (see ``peak`` in the
  configuration). Exact matches have infinite PSNR, reported as ``None`` with
  ``exact: true`` because JSON has no infinity.
* Chamfer: ``mean(d_ab) + mean(d_ba)`` (L1) and ``mean(d_ab^2) + mean(d_ba^2)``
  (L2), from the same nearest-sample distances.
* Hausdorff: the larger one-sided maximum over a separate, denser sampling plus
  all vertices. With point_cloud_utils the distance is to the other *surface*
  (exact point-to-triangle), so the estimate is a lower bound that converges
  with more samples; without it, distances go to the other surface's samples.
  The method used is recorded.
* Normal consistency: mean absolute cosine between each sample's face normal
  and its nearest counterpart's, averaged over both directions (1 is perfect).
"""

from __future__ import annotations

import math
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

import numpy as np


def _tree(points):
    try:
        from scipy.spatial import cKDTree
    except ModuleNotFoundError as error:
        if error.name != "scipy":
            raise
        return None
    return cKDTree(points)


def nearest(queries: np.ndarray, reference: np.ndarray, tree=None) -> tuple[np.ndarray, np.ndarray]:
    """Euclidean distance and index of each query's nearest reference point."""
    tree = tree if tree is not None else _tree(reference)
    if tree is not None:
        distances, indices = tree.query(queries, k=1, workers=-1)
        return np.asarray(distances, dtype=np.float64), np.asarray(indices, dtype=np.int64)
    if len(queries) * len(reference) > 4e9:
        raise RuntimeError("dense metrics without SciPy are limited to small samples")
    distances = np.empty(len(queries))
    indices = np.empty(len(queries), dtype=np.int64)
    for start in range(0, len(queries), 512):
        block = queries[start:start + 512, None] - reference[None]
        squared = np.einsum("ijk,ijk->ij", block, block)
        indices[start:start + 512] = squared.argmin(1)
        distances[start:start + 512] = np.sqrt(squared.min(1))
    return distances, indices


def face_geometry(positions, triangles) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Corner positions, unit normals and areas of every triangle."""
    positions = np.asarray(positions, dtype=np.float64)
    triangles = np.asarray(triangles, dtype=np.int64).reshape(-1, 3)
    corners = positions[triangles]
    cross = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    doubled = np.linalg.norm(cross, axis=1)
    normals = np.divide(cross, doubled[:, None], out=np.zeros_like(cross), where=doubled[:, None] > 0)
    return corners, normals, doubled / 2


def sample_surface(positions, triangles, count: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    """Area-uniform random surface samples and their face normals."""
    corners, normals, areas = face_geometry(positions, triangles)
    total = areas.sum()
    if not len(areas) or not np.isfinite(total) or total <= 0:
        raise ValueError("mesh has no triangle area to sample")
    generator = np.random.default_rng(seed)
    cumulative = np.cumsum(areas) / total
    faces = np.searchsorted(cumulative, generator.random(count), side="right")
    faces = np.minimum(faces, len(areas) - 1)
    first, second = generator.random(count), generator.random(count)
    reflected = first + second > 1
    first[reflected], second[reflected] = 1 - first[reflected], 1 - second[reflected]
    chosen = corners[faces]
    points = (chosen[:, 0] + first[:, None] * (chosen[:, 1] - chosen[:, 0])
              + second[:, None] * (chosen[:, 2] - chosen[:, 0]))
    return points, normals[faces]


def bounding_box_diagonal(*point_sets) -> float:
    lower = np.min([np.min(np.asarray(points, dtype=np.float64), axis=0) for points in point_sets], axis=0)
    upper = np.max([np.max(np.asarray(points, dtype=np.float64), axis=0) for points in point_sets], axis=0)
    return float(np.linalg.norm(upper - lower))


def psnr(mse: float, peak: float) -> float | None:
    if mse <= 0:
        return None
    return float(10 * math.log10(peak * peak / mse))


def _surface_distance(points, positions, triangles):
    """Exact point-to-surface distances when point_cloud_utils is available."""
    try:
        import point_cloud_utils as pcu
    except ModuleNotFoundError:
        return None
    _, _, areas = face_geometry(positions, triangles)
    triangles = np.asarray(triangles)[areas > 0]
    distances, _, _ = pcu.closest_points_on_mesh(
        np.ascontiguousarray(points, dtype=np.float64),
        np.ascontiguousarray(positions, dtype=np.float64),
        np.ascontiguousarray(triangles, dtype=np.int32),
    )
    return np.asarray(distances, dtype=np.float64)


def _closest_surface(points, mesh):
    try:
        import point_cloud_utils as pcu
    except ModuleNotFoundError:
        return None
    vertices = np.ascontiguousarray(mesh.positions, dtype=np.float64)
    faces = np.ascontiguousarray(mesh.triangles, dtype=np.int32)
    _, normals, areas = face_geometry(vertices, faces)
    # Native quantization may collapse a triangle. PCU's barycentric result
    # for a degenerate face can be NaN even when its distance is finite.
    valid = areas > 0
    faces = np.ascontiguousarray(faces[valid])
    normals = normals[valid]
    if not len(faces):
        raise ValueError("mesh has no nondegenerate surface for distance metrics")
    distances, ids, weights = pcu.closest_points_on_mesh(
        np.ascontiguousarray(points, dtype=np.float64), vertices, faces)
    closest = np.einsum("nij,ni->nj", vertices[faces[ids]], weights)
    if not np.isfinite(distances).all() or not np.isfinite(closest).all():
        raise ValueError("surface distance computation returned nonfinite values")
    return np.asarray(distances), closest, normals[ids]


def hausdorff(reference, decoded, *, samples: int, seed: int, exact: bool = True) -> tuple[float, str]:
    """Symmetric Hausdorff estimate and the method used to obtain it."""
    sides = []
    method = None
    for index, (source, other) in enumerate(((reference, decoded), (decoded, reference))):
        points, _ = sample_surface(source.positions, source.triangles, samples, seed + 7919 + index)
        points = np.concatenate((points, np.asarray(source.positions, dtype=np.float64)))
        distances = _surface_distance(points, other.positions, other.triangles) if exact else None
        if distances is None:
            target, _ = sample_surface(other.positions, other.triangles, samples, seed + 104729 + index)
            target = np.concatenate((target, np.asarray(other.positions, dtype=np.float64)))
            distances, _ = nearest(points, target)
            method = "samples+vertices to samples+vertices (nearest neighbour)"
        else:
            method = "samples+vertices to surface (point_cloud_utils)"
        sides.append(float(distances.max()))
    return max(sides), method


def mesh_errors(reference, decoded, *, peak: float, samples: int = 100_000,
                hausdorff_samples: int | None = None, seed: int = 0,
                exact_hausdorff: bool = True) -> dict:
    """All surface metrics for one reference/decoded TriangleMesh pair."""
    if peak <= 0 or not math.isfinite(peak):
        raise ValueError("peak must be finite and positive")
    if (np.array_equal(reference.positions, decoded.positions)
            and np.array_equal(reference.triangles, decoded.triangles)):
        return {"exact": True, "peak": peak, "samples": 0, "d1_mse": 0.0, "d2_mse": 0.0,
                "d1_psnr_db": None, "d2_psnr_db": None, "chamfer_l1": 0.0, "chamfer_l2": 0.0,
                "hausdorff": 0.0, "hausdorff_method": "identical arrays",
                "normal_consistency": 1.0, "normal_consistency_oriented": 1.0,
                "surface_d1_mse": 0., "surface_d2_mse": 0.,
                "surface_d1_psnr_db": None, "surface_d2_psnr_db": None,
                "surface_method": "identical arrays"}
    ref_points, ref_normals = sample_surface(reference.positions, reference.triangles, samples, seed)
    dec_points, dec_normals = sample_surface(decoded.positions, decoded.triangles, samples, seed + 1)
    d_ab, i_ab = nearest(ref_points, dec_points)
    d_ba, i_ba = nearest(dec_points, ref_points)
    # D2 always projects on the reference (original) surface normal.
    plane_ab = np.einsum("ij,ij->i", dec_points[i_ab] - ref_points, ref_normals)
    plane_ba = np.einsum("ij,ij->i", dec_points - ref_points[i_ba], ref_normals[i_ba])
    d1 = {"ab": float(np.mean(d_ab ** 2)), "ba": float(np.mean(d_ba ** 2))}
    d2 = {"ab": float(np.mean(plane_ab ** 2)), "ba": float(np.mean(plane_ba ** 2))}
    cosine_ab = np.einsum("ij,ij->i", ref_normals, dec_normals[i_ab])
    cosine_ba = np.einsum("ij,ij->i", dec_normals, ref_normals[i_ba])
    distance, method = hausdorff(reference, decoded, samples=hausdorff_samples or samples,
                                 seed=seed, exact=exact_hausdorff)
    result = {
        "exact": False, "peak": peak, "samples": samples,
        "d1_mse": max(d1.values()), "d1_mse_reference_to_decoded": d1["ab"],
        "d1_mse_decoded_to_reference": d1["ba"],
        "d2_mse": max(d2.values()), "d2_mse_reference_to_decoded": d2["ab"],
        "d2_mse_decoded_to_reference": d2["ba"],
        "chamfer_l1": float(d_ab.mean() + d_ba.mean()),
        "chamfer_l2": d1["ab"] + d1["ba"],
        "hausdorff": distance, "hausdorff_method": method,
        "hausdorff_samples": hausdorff_samples or samples,
        "normal_consistency": float((np.abs(cosine_ab).mean() + np.abs(cosine_ba).mean()) / 2),
        "normal_consistency_oriented": float((cosine_ab.mean() + cosine_ba.mean()) / 2),
    }
    result["d1_psnr_db"] = psnr(result["d1_mse"], peak)
    result["d2_psnr_db"] = psnr(result["d2_mse"], peak)
    # Independent point sampling has a spacing floor which can hide small
    # quantization errors. Report exact point-to-triangle distances separately
    # so high-quality rate-distortion points remain distinguishable. These
    # surface metrics are not labelled as MPEG pc_error's point-set metrics.
    closest_ab = _closest_surface(ref_points, decoded)
    closest_ba = _closest_surface(dec_points, reference)
    if closest_ab is not None and closest_ba is not None:
        da, pa, _ = closest_ab
        db, pb, nb = closest_ba
        result["surface_d1_mse"] = max(float(np.mean(da ** 2)), float(np.mean(db ** 2)))
        result["surface_d2_mse"] = max(
            float(np.mean(np.einsum("ij,ij->i", pa - ref_points, ref_normals) ** 2)),
            float(np.mean(np.einsum("ij,ij->i", dec_points - pb, nb) ** 2)))
        for name in ("d1", "d2"):
            result[f"surface_{name}_psnr_db"] = psnr(result[f"surface_{name}_mse"], peak)
        result["surface_method"] = "area samples to exact triangles (point_cloud_utils)"
    return result


def summarize_frames(frames: list[dict]) -> dict:
    """Sequence summary: MPEG-style mean of per-frame PSNR plus pooled values."""
    if not frames:
        return {"frames": 0}
    summary = {"frames": len(frames), "exact_frames": sum(bool(f.get("exact")) for f in frames)}
    for name in ("d1", "d2"):
        values = [f[f"{name}_psnr_db"] for f in frames if f.get(f"{name}_psnr_db") is not None]
        summary[f"{name}_psnr_db_mean"] = float(np.mean(values)) if values else None
        summary[f"{name}_psnr_db_min"] = float(np.min(values)) if values else None
        pooled = float(np.mean([f[f"{name}_mse"] for f in frames]))
        summary[f"{name}_mse_mean"] = pooled
        summary[f"{name}_psnr_db_of_mean_mse"] = psnr(pooled, frames[0]["peak"])
        if all(f"surface_{name}_mse" in f for f in frames):
            pooled_surface = float(np.mean([f[f"surface_{name}_mse"] for f in frames]))
            summary[f"surface_{name}_mse_mean"] = pooled_surface
            summary[f"surface_{name}_psnr_db_of_mean_mse"] = psnr(pooled_surface, frames[0]["peak"])
    for name in ("chamfer_l1", "chamfer_l2", "normal_consistency", "normal_consistency_oriented"):
        summary[f"{name}_mean"] = float(np.mean([f[name] for f in frames]))
    summary["hausdorff_max"] = float(np.max([f["hausdorff"] for f in frames]))
    summary["hausdorff_mean"] = float(np.mean([f["hausdorff"] for f in frames]))
    summary["peak"] = frames[0]["peak"]
    return summary


# ------------------------------------------------------------------ mmetric


_MMETRIC_PATTERNS = {
    "d1_psnr_db": re.compile(r"mseF,PSNR \(p2point\)\s*:\s*([-+0-9.eE]+|inf)"),
    "d2_psnr_db": re.compile(r"mseF,PSNR \(p2plane\)\s*:\s*([-+0-9.eE]+|inf)"),
    "d1_mse": re.compile(r"mseF\s+\(p2point\)\s*:\s*([-+0-9.eE]+)"),
    "d2_mse": re.compile(r"mseF\s+\(p2plane\)\s*:\s*([-+0-9.eE]+)"),
}


def parse_mmetric(output: str) -> dict:
    """Read pc_error-style summary lines printed by MPEG mmetric."""
    result = {}
    for name, pattern in _MMETRIC_PATTERNS.items():
        matches = pattern.findall(output)
        if matches:
            value = float(matches[-1])
            result[name] = value if math.isfinite(value) else None
    return result


def run_mmetric(binary: str, arguments: list[str], reference, decoded, *, peak: float,
                timeout: float | None = 600) -> dict:
    """Run a pinned mmetric binary on OBJ exports of one frame pair.

    ``arguments`` is a template; ``{reference}``, ``{decoded}`` and ``{peak}``
    are substituted. Verify the template against the pinned mmetric version.
    """
    with tempfile.TemporaryDirectory(prefix="open4d-mmetric-") as directory:
        paths = {}
        for name, mesh in (("reference", reference), ("decoded", decoded)):
            path = Path(directory) / f"{name}.obj"
            write_obj(path, mesh.positions, mesh.triangles)
            paths[name] = str(path)
        command = [binary, *(item.format(peak=repr(float(peak)), **paths) for item in arguments)]
        completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout,
                                   check=False)
    output = completed.stdout + completed.stderr
    result = parse_mmetric(output)
    result.update(command=shlex.join(command), returncode=completed.returncode,
                  output_tail=output[-2000:])
    return result


def write_obj(path: Path, positions, triangles) -> None:
    positions = np.asarray(positions, dtype=np.float64)
    triangles = np.asarray(triangles, dtype=np.int64) + 1
    with Path(path).open("w", encoding="ascii") as stream:
        np.savetxt(stream, positions, fmt="v %.9g %.9g %.9g")
        np.savetxt(stream, triangles, fmt="f %d %d %d")


# ------------------------------------------------------------------- images


def _gaussian_window(size: int = 11, sigma: float = 1.5) -> np.ndarray:
    axis = np.arange(size) - size // 2
    line = np.exp(-(axis ** 2) / (2 * sigma ** 2))
    line /= line.sum()
    return line


def _filter(image: np.ndarray, line: np.ndarray) -> np.ndarray:
    """Separable zero-padded 'same' filtering, as 3DGS's conv2d SSIM does."""
    pad = len(line) // 2
    padded = np.pad(image, ((pad, pad), (pad, pad), (0, 0)))
    rows = sum(line[i] * padded[i:i + image.shape[0]] for i in range(len(line)))
    return sum(line[i] * rows[:, i:i + image.shape[1]] for i in range(len(line)))


def image_errors(rendered: np.ndarray, truth: np.ndarray) -> dict:
    """PSNR (8-bit, QUEEN's convention; float, 3DGStream's) and 3DGS SSIM.

    Inputs are (H, W, 3) floats in [0, 1].
    """
    rendered = np.clip(np.asarray(rendered, dtype=np.float64), 0, 1)
    truth = np.clip(np.asarray(truth, dtype=np.float64), 0, 1)
    if rendered.shape != truth.shape:
        raise ValueError(f"image shapes differ: {rendered.shape} != {truth.shape}")
    quantized = np.mean((np.round(rendered * 255) - np.round(truth * 255)) ** 2)
    floating = np.mean((rendered - truth) ** 2)
    line = _gaussian_window()
    mu_a, mu_b = _filter(rendered, line), _filter(truth, line)
    var_a = _filter(rendered * rendered, line) - mu_a ** 2
    var_b = _filter(truth * truth, line) - mu_b ** 2
    cov = _filter(rendered * truth, line) - mu_a * mu_b
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    ssim = ((2 * mu_a * mu_b + c1) * (2 * cov + c2)) / ((mu_a ** 2 + mu_b ** 2 + c1) * (var_a + var_b + c2))
    return {
        "psnr_db": None if quantized == 0 else float(20 * math.log10(255 / math.sqrt(quantized))),
        "psnr_float_db": None if floating == 0 else float(-10 * math.log10(floating)),
        "ssim": float(ssim.mean()),
    }


def lpips_function(network: str = "vgg"):
    """Return an LPIPS callable on (H, W, 3) arrays, or a reason it is missing."""
    try:
        import lpips
        import torch
    except ModuleNotFoundError as error:
        return None, f"lpips unavailable: {error.name} is not installed"
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = lpips.LPIPS(net=network, verbose=False).to(device).eval()

    def measure(rendered, truth):
        def tensor(value):
            value = torch.as_tensor(np.asarray(value, dtype=np.float32), device=device)
            return value.permute(2, 0, 1)[None] * 2 - 1
        with torch.inference_mode():
            return float(model(tensor(rendered), tensor(truth)).item())

    return measure, f"lpips {getattr(lpips, '__version__', 'unknown')} ({network})"
