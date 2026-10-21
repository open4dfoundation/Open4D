"""Run native mesh tools in a separate Python environment."""

from __future__ import annotations

import importlib
import json
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def _run(command, cwd=None):
    subprocess.run([str(item) for item in command], cwd=cwd, check=True)


def _file(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"native pipeline produced no {path.name}")
    return path


def _editor(root):
    candidates = sorted(
        (root / "tvm-editing/TVMEditor.Test/bin/Release").glob("*/TVMEditor.Test.dll"),
        key=lambda path: tuple(int(part) for part in re.findall(r"\d+", path.parent.name)),
    )
    if not candidates:
        raise RuntimeError(f"TVM editor is not built; run {root / 'setup.sh'}")
    return candidates[-1]


def _tracking_config(destination, source, centers, settings):
    root = ET.Element("Config")
    values = {
        "firstIndex": 0, "lastIndex": settings["frames"] - 1,
        "inDir": source, "fileNamePrefix": "mesh_", "outDir": centers,
        "volumeGridResolution": settings["grid_resolution"],
        "pointCount": settings["num_centers"], "gradientThreshold": 0.0001,
        "smoothSigma": 0.125, "smoothSigma2": 0.125,
        "falloffStrength": 0.05, "applySmooth": 1, "applyLloyd": 1,
    }
    for key, value in values.items():
        ET.SubElement(root, key).text = str(value)
    ET.ElementTree(root).write(destination, encoding="utf-8", xml_declaration=True)


def _prepare(settings):
    import numpy as np

    root = Path(settings["backend"])
    source = Path(settings["input"])
    work = source.parent / "pipeline"
    kind = "TVMC" if settings["codec"] == "tvmc" else "tsmc"
    tools = work / kind
    tools.mkdir(parents=True)
    for script in (root / kind).glob("*.py"):
        shutil.copy2(script, tools / script.name)
    meshes = work / "arap-volume-tracking/data/open4d"
    shutil.copytree(source, meshes)
    centers = work / "centers"
    centers.mkdir()
    editor = _editor(root)
    if settings.get("centers"):
        files = sorted(Path(settings["centers"]).glob("*.xyz"),
                       key=lambda path: tuple(int(part) for part in re.findall(r"\d+", path.stem)))
        if len(files) != settings["frames"]:
            raise ValueError("centers must contain one .xyz file per input frame")
        for index, source_file in enumerate(files):
            shutil.copy2(source_file, centers / f"mesh_{index:03d}.xyz")
    else:
        tracker = root / "arap-volume-tracking/bin/Client.dll"
        if not tracker.is_file():
            raise RuntimeError(f"volume tracker is not built; run {root / 'setup.sh'} or pass centers=")
        config = work / "tracking.xml"
        _tracking_config(config, meshes, centers, settings)
        _run([settings["dotnet"], tracker, config], work / "arap-volume-tracking")
    tracked = list(centers.glob("*.xyz"))
    if len(tracked) != settings["frames"]:
        raise RuntimeError(f"tracking produced {len(tracked)} center files, expected {settings['frames']}")
    for path in tracked:
        if np.loadtxt(path).shape != (settings["num_centers"], 3):
            raise ValueError(f"expected {settings['num_centers']} XYZ centers in {path}")
    return work, tools, meshes, centers, editor


def _fit(settings):
    work, tools, meshes, centers, editor = _prepare(settings)
    python, frames = settings["python"], settings["frames"]
    common = ["--dataset", "open4d", "--num_frames", frames,
              "--num_centers", settings["num_centers"]]
    indexed = ["--firstIndex", 0, "--lastIndex", frames - 1]
    build = work / "tvm-editing/TVMEditor.Test/bin/Release/net5.0"
    data = build / f"Data/open4d_{settings['num_centers']}"
    output = build / f"output/open4d_{settings['num_centers']}"
    os.environ["TSMC_EDITOR_BUILD"] = "TVMEditor.Test/bin/Release/net5.0"
    _run([python, tools / "get_reference_center.py", *common,
          "--centers_dir", centers, "--random_state", 0], tools)
    _run([python, tools / "get_transformation.py", *common,
          "--centers_dir", centers, *indexed], tools)
    # TVMC calls its general deformation implementation through a dataset preset.
    profile = "basketball" if settings["codec"] == "tvmc" else "open4d"
    _run([settings["dotnet"], editor, profile, 1, 0, frames - 1, data, output], work)
    reference = data / "reference_mesh/decimated_reference_mesh.obj"
    _run([python, tools / "extract_reference_mesh.py", *common,
          "--inputDir", output / "output", "--outputDir", reference.parent,
          *indexed, "--key", settings["key_frame"]], tools)
    _file(reference)
    _run([settings["dotnet"], editor, profile, 2, 0, frames - 1, data, output], work)
    if settings["codec"] == "tsmc":
        native_tools = work / "draco/build"
        native_tools.mkdir(parents=True)
        for name in ("encoder", "decoder"):
            suffix = ".exe" if os.name == "nt" else ""
            shutil.copy2(settings[name], native_tools / f"draco_{name}{suffix}")
    _run([python, tools / "get_displacements.py", *common,
          "--target_mesh_path", meshes, *indexed], tools)
    return tools, reference, output / "reference"


def _tvmc_encode(settings, reference, displacements):
    import numpy as np
    import open3d as o3d
    from scipy.spatial import cKDTree

    output = Path(settings["output"])
    encoded = output / "reference.drc"
    decoded_path = displacements / "decoded_reference.obj"
    _run([settings["encoder"], "-i", reference, "-o", encoded, "-qp", 14, "-cl", 7])
    _run([settings["decoder"], "-i", _file(encoded), "-o", decoded_path])
    original = o3d.io.read_triangle_mesh(str(reference))
    decoded = o3d.io.read_triangle_mesh(str(_file(decoded_path)))
    original = original.subdivide_midpoint(number_of_iterations=1)
    decoded = decoded.subdivide_midpoint(number_of_iterations=1)
    if not len(decoded.vertices):
        raise RuntimeError("decoded reference mesh is empty")
    _, reference_indices = cKDTree(np.asarray(original.vertices)).query(np.asarray(decoded.vertices))
    for index in range(settings["frames"]):
        stream = output / f"displacement_{index:06d}.drc"
        ply = reference.parent / f"dis_open4d_{index:03d}.ply"
        decoded_ply = displacements / f"decoded_{index:03d}.ply"
        _run([settings["encoder"], "-point_cloud", "-i", _file(ply), "-o", stream,
              "-qp", settings["quantization"], "-cl", 10])
        _run([settings["decoder"], "-i", _file(stream), "-o", decoded_ply])
        offsets = np.loadtxt(displacements / f"displacements_open4d_{index:03d}.txt", ndmin=2)
        points = np.asarray(o3d.io.read_point_cloud(str(_file(decoded_ply))).points)
        if not len(points):
            raise RuntimeError(f"decoded displacement {index} is empty")
        # The research evaluator recovers this order from the original offsets.
        _, indices = cKDTree(points).query(offsets[reference_indices])
        np.save(output / f"displacement_{index:06d}.npy", indices.astype(np.uint32))


def _save_entropy_model(delta_path, destination):
    import numpy as np

    delta = np.load(delta_path, allow_pickle=False)
    if (not isinstance(delta, np.ndarray) or delta.ndim != 2 or not delta.size
            or delta.dtype.kind not in "fiu" or not np.isfinite(delta).all()):
        raise ValueError("invalid entropy input matrix")
    rounded = np.round(delta.astype(np.float64) * 10000)
    if np.any(rounded < -(2**31)) or np.any(rounded >= 2**31):
        raise ValueError("entropy values exceed int32 range")
    quantized = rounded.astype(np.int32)
    minimum, maximum = int(quantized.min()), int(quantized.max())
    if minimum == maximum:
        minimum, maximum = (minimum - 1, maximum) if maximum == 2**31 - 1 else (minimum, maximum + 1)
    if maximum - minimum >= 2**24:
        raise ValueError("entropy range exceeds the ANS model's 24-bit precision")
    means = np.array([np.mean(column) for column in quantized.T])
    stds = np.array([np.std(column) for column in quantized.T])
    np.savez(destination, shape=quantized.shape, minimum=minimum,
             maximum=maximum, means=means, stds=np.where(stds > 0, stds, 1.0),
             scaling_factor=10000)


def _decode_entropy(stream, model_path):
    import numpy as np

    compressed = np.load(stream, allow_pickle=False)
    if not isinstance(compressed, np.ndarray) or compressed.ndim != 1 or compressed.dtype != np.uint32:
        raise ValueError("entropy stream must be a uint32 vector")
    with np.load(model_path, allow_pickle=False) as saved:
        shape_array = saved["shape"]
        if shape_array.shape != (2,) or shape_array.dtype.kind not in "iu" or np.any(shape_array <= 0):
            raise ValueError("invalid entropy-model dimensions")
        shape = tuple(int(value) for value in shape_array)
        bounds = [saved["minimum"], saved["maximum"]]
        if any(value.shape != () or value.dtype.kind not in "iu" for value in bounds):
            raise ValueError("invalid entropy-model range")
        minimum, maximum = map(int, bounds)
        if not -(2**31) <= minimum < maximum < 2**31 or maximum - minimum >= 2**24:
            raise ValueError("invalid entropy-model range")
        means, stds = saved["means"], saved["stds"]
        if means.shape != (shape[1],) or stds.shape != means.shape:
            raise ValueError("entropy-model parameters do not match its dimensions")
        if (means.dtype.kind not in "fiu" or stds.dtype.kind not in "fiu"
                or not np.isfinite(means).all() or not np.isfinite(stds).all() or np.any(stds <= 0)):
            raise ValueError("entropy-model means and positive deviations must be finite")
        scaling = saved["scaling_factor"]
        if scaling.shape != () or scaling.dtype.kind not in "fiu" or not np.isfinite(scaling) or scaling <= 0:
            raise ValueError("entropy-model scaling factor must be finite and positive")
        means = np.tile(means.astype(np.float64), shape[0])
        stds = np.tile(stds.astype(np.float64), shape[0])
        scaling = float(scaling)
    import constriction

    model = constriction.stream.model.QuantizedGaussian(minimum, maximum)
    decoder = constriction.stream.stack.AnsCoder(compressed)
    result = decoder.decode(model, means, stds)
    if not decoder.is_empty():
        raise ValueError("entropy stream has unused data")
    return result.reshape(shape) / scaling


def _tsmc_encode(settings, tools, reference, displacements):
    output = Path(settings["output"])
    decoded_reference = reference.parent / "others/decoded_decimated_reference_mesh.obj"
    _run([settings["python"], tools / "compress_displacements.py", "--dataset", "open4d",
          "--num_frames", settings["frames"], "--num_eigenvectors", settings["components"],
          "--displacement_path", displacements, "--output_path", displacements,
          "--firstIndex", 0, "--lastIndex", settings["frames"] - 1,
          "--reference_mesh_path", _file(decoded_reference)], tools)
    shutil.copy2(_file(reference.parent / "decimated_reference_mesh.drc"), output / "reference.drc")
    for name in ("B_matrix.txt", "T_matrix.txt", "delta_trajectories_encoded.npy"):
        shutil.copy2(_file(displacements / name), output / name)
    _save_entropy_model(displacements / "delta_trajectories.npy", output / "entropy_model.npz")


def encode(settings):
    tools, reference, displacements = _fit(settings)
    if settings["codec"] == "tvmc":
        _tvmc_encode(settings, reference, displacements)
    else:
        _tsmc_encode(settings, tools, reference, displacements)


def decode(settings):
    import numpy as np
    import open3d as o3d

    source, output = Path(settings["input"]), Path(settings["output"])
    reference = output.parent / "reference.obj"
    _run([settings["decoder"], "-i", _file(source / "reference.drc"), "-o", reference])
    mesh = o3d.io.read_triangle_mesh(str(_file(reference)))
    mesh = mesh.subdivide_midpoint(number_of_iterations=1)
    vertices = np.asarray(mesh.vertices).copy()
    if not len(vertices):
        raise RuntimeError("decoded reference mesh is empty")
    trajectories = None
    if settings["codec"] == "tsmc":
        import cupy as cp
        import cupyx.scipy.sparse

        sys.path.insert(0, str(Path(settings["backend"]) / "tsmc"))
        from util import build_mv_laplacian_gpu_fast, solve_sparse_least_squares_cg

        delta = _decode_entropy(source / "delta_trajectories_encoded.npy", source / "entropy_model.npz")
        mesh.compute_adjacency_list()
        anchors = np.linspace(0, len(vertices) - 1, 2000, dtype=int)
        laplacian = build_mv_laplacian_gpu_fast(mesh, anchors)
        coefficients = solve_sparse_least_squares_cg(
            cupyx.scipy.sparse.csr_matrix(laplacian), cp.asarray(delta),
            maxiter=500, tol=1e-6,
        )
        basis = np.loadtxt(source / "B_matrix.txt", ndmin=2)
        mean = np.loadtxt(source / "T_matrix.txt", ndmin=2)
        trajectories = (cp.asnumpy(coefficients) @ basis + mean).reshape(len(vertices), settings["frames"], 3)
    for index in range(settings["frames"]):
        if trajectories is None:
            ply = output.parent / f"displacement_{index:06d}.ply"
            _run([settings["decoder"], "-i", _file(source / f"displacement_{index:06d}.drc"), "-o", ply])
            offsets = np.asarray(o3d.io.read_point_cloud(str(_file(ply))).points)
            mapping = np.load(source / f"displacement_{index:06d}.npy", allow_pickle=False)
            if (mapping.shape != (len(vertices),) or mapping.dtype.kind not in "ui"
                    or np.any(mapping < 0) or np.any(mapping >= len(offsets))):
                raise ValueError(f"invalid displacement mapping for frame {index}")
            offsets = offsets[mapping]
        else:
            offsets = trajectories[:, index]
        if offsets.shape != vertices.shape or not np.isfinite(offsets).all():
            raise ValueError(f"invalid reconstructed displacement for frame {index}")
        mesh.vertices = o3d.utility.Vector3dVector(vertices + offsets)
        path = output / f"frame_{index:06d}.obj"
        if not o3d.io.write_triangle_mesh(str(path), mesh, write_vertex_normals=False,
                                        write_vertex_colors=False, write_triangle_uvs=False):
            raise RuntimeError(f"could not write decoded frame {index}")


def main():
    action, request = sys.argv[1:]
    settings = json.loads(Path(request).read_text(encoding="utf-8"))
    modules = ["numpy", "open3d", "scipy"]
    if action == "encode":
        modules += ["trimesh", "sklearn"]
    if settings["codec"] == "tsmc":
        modules += ["cupy", "constriction", "point_cloud_utils", "cv2", "skimage", "matplotlib", "tqdm"]
    try:
        for module in modules:
            importlib.import_module(module)
    except ImportError as error:
        raise RuntimeError(
            f"{settings['codec']} dependencies are missing in {sys.executable}; "
            f"run {Path(settings['backend']) / 'setup.sh'} or pass python= for its environment: {error}"
        ) from error
    if settings["codec"] == "tsmc":
        import cupy as cp

        try:
            devices = cp.cuda.runtime.getDeviceCount()
        except cp.cuda.runtime.CUDARuntimeError as error:
            raise RuntimeError("TSMC requires a working CUDA GPU and driver") from error
        if not devices:
            raise RuntimeError("TSMC requires a CUDA GPU")
    if action == "encode":
        encode(settings)
    elif action == "decode":
        decode(settings)
    else:
        raise ValueError(f"unknown action: {action}")


if __name__ == "__main__":
    main()
