"""Train each experiment once, cold-decode, and compare actual rate and geometry."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

import numpy as np

from open4d.codecs import encode_sequence, decode_sequence
from open4d.codecs._npz import NumPyZipCodec
from open4d.core import Sequence, MemoryFrameProvider
from open4d.io import open_sequence
from .metrics import mesh_errors, summarize_frames, bounding_box_diagonal


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def sequence_fingerprint(sequence):
    """Hash logical inputs, excluding NPZ ZIP timestamps that vary per write."""
    from open4d.codecs._metadata import _json_value
    from open4d.codecs._mesh_payload import texture_arrays
    from open4d.codecs._npz import _GEOMETRIES
    value = hashlib.sha256()
    def record(item):
        value.update(json.dumps(_json_value(item, "benchmark input"), sort_keys=True,
                                separators=(",", ":")).encode())
    record({"metadata": dict(sequence.metadata), "topology": sequence.topology.value,
            "correspondence": sequence.has_vertex_correspondence,
            "constant_vertices": sequence.has_constant_vertex_count})
    for frame in sequence:
        geometry = frame.geometry
        kind = geometry.representation.value
        texture, pixels = texture_arrays(getattr(geometry, "texture", None))
        record({"index": frame.frame_index, "time": frame.timestamp, "metadata": dict(frame.metadata),
                "representation": kind, "domains": dict(getattr(geometry, "attribute_domains", {})),
                "texture": texture})
        arrays = {name: getattr(geometry, name) for name in _GEOMETRIES[kind][1]}
        arrays.update({"attribute:" + name: array for name, array in geometry.attributes.items()})
        arrays.update(pixels)
        for name, array in sorted(arrays.items()):
            if array is None:
                record([name, None])
            else:
                array = np.ascontiguousarray(array)
                record([name, array.dtype.str, array.shape])
                value.update(array.tobytes())
    return value.hexdigest()


def code_provenance(checkout):
    """Identify dirty checkouts and source snapshots without a .git directory."""
    paths = [p for root in (checkout / "open4d", checkout / "scripts/codec_rd")
             for p in root.rglob("*") if p.suffix in (".py", ".json") and p.is_file()]
    paths.append(checkout / "scripts/benchmark_codec.py")
    hashes = {str(p.relative_to(checkout)): digest(p) for p in sorted(paths)}
    tree = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=checkout, capture_output=True, text=True)
    status = subprocess.run(["git", "status", "--porcelain", "--untracked-files=normal"],
                            cwd=checkout, capture_output=True, text=True)
    return {"git_revision": revision.stdout.strip() if revision.returncode == 0 else None,
            "git_status": status.stdout.splitlines() if status.returncode == 0 else None,
            "source_sha256": tree, "files": hashes}


def run_worker(request, directory, timeout):
    import psutil
    path = directory / f"{request['operation']}-request.json"
    path.write_text(json.dumps(request, indent=2))
    peak = samples = 0
    started = time.monotonic()
    with (directory / f"{request['operation']}.log").open("w") as log:
        child = subprocess.Popen([sys.executable, "-m", "scripts.codec_rd.worker", str(path)],
                                 cwd=directory, stdout=log, stderr=subprocess.STDOUT)
        parent = psutil.Process(child.pid)
        try:
            while child.poll() is None:
                if time.monotonic() - started > timeout:
                    raise TimeoutError(f"codec worker exceeded {timeout}s")
                try:
                    processes = [parent, *parent.children(recursive=True)]
                    rss = 0
                    for process in processes:
                        try:
                            rss += process.memory_info().rss
                        except psutil.Error:
                            pass
                    peak = max(peak, rss)
                    samples += 1
                except psutil.Error:
                    pass
                time.sleep(.02)
            if child.returncode:
                raise RuntimeError(f"{request['operation']} failed; see {directory / (request['operation'] + '.log')}")
        except BaseException:
            try:
                for process in parent.children(recursive=True):
                    process.kill()
            except psutil.Error:
                pass
            child.kill()
            child.wait()
            raise
    report = json.loads(Path(request["report"]).read_text())
    report.update(process_tree_peak_rss_bytes=peak, rss_samples=samples,
                  rss_sampling_interval_seconds=.02, process_seconds=time.monotonic() - started)
    Path(request["report"]).write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path,
                        help="JSON list of {id, codec, seed, encode, decode} experiments")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--stride", type=int, default=1)
    parser.add_argument("--fps", type=float, help="input timing for sources without stored timestamps")
    parser.add_argument("--samples", type=int, default=10000)
    parser.add_argument("--timeout", type=float, default=86400)
    parser.add_argument("--geometry-only", action="store_true", help="explicitly remove appearance for geometry profiles")
    parser.add_argument("--resume", action="store_true", help="reuse matching saved encoder outputs and cold-decode again")
    args = parser.parse_args(argv)
    if min(args.frames, args.stride, args.samples, args.fps or 30, args.timeout) <= 0:
        parser.error("frame count, stride, samples, fps and timeout must be positive")
    experiments = json.loads(args.config.read_text())
    if not isinstance(experiments, list) or not experiments:
        parser.error("config must be a nonempty experiment list")
    identifiers = [item.get("id", "") for item in experiments]
    if len(set(identifiers)) != len(identifiers) or not all(re.fullmatch(r"[A-Za-z0-9_.-]+", s) and s not in (".", "..") for s in identifiers):
        parser.error("experiment ids must be unique safe names")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=args.resume)
    checkout = Path(__file__).resolve().parents[2]
    os.environ["PYTHONPATH"] = str(checkout) + os.pathsep + os.environ.get("PYTHONPATH", "")
    with open_sequence(args.source, fps=args.fps) as original:
        fps = args.fps or original.fps or 30.
        frames = tuple(original[::args.stride][:args.frames])
        if args.geometry_only:
            from open4d.core import Frame, TriangleMesh
            frames = tuple(Frame(f.frame_index, f.timestamp, TriangleMesh(f.geometry.positions, f.geometry.triangles), f.metadata) for f in frames)
        source = Sequence(MemoryFrameProvider(frames, metadata=original.metadata,
                                              topology=original.topology,
                                              has_vertex_correspondence=original.has_vertex_correspondence))
    if not frames:
        raise ValueError("source contains no frames")
    input_path = output / "input.o4d"
    if args.resume and input_path.exists():
        with decode_sequence(input_path, codec=NumPyZipCodec("raw")) as saved_source:
            if sequence_fingerprint(source) != sequence_fingerprint(saved_source):
                raise ValueError("resume source differs from the saved reference input")
    else:
        input_path = encode_sequence(source, input_path, codec=NumPyZipCodec("raw"))
    source_hash = digest(input_path)
    peak = bounding_box_diagonal(*(frame.geometry.positions for frame in frames))
    rate_duration = (frames[-1].timestamp - frames[0].timestamp + args.stride / fps)
    provenance = code_provenance(checkout)
    results = []
    for experiment in experiments:
        directory = output / experiment["id"]
        directory.mkdir(exist_ok=args.resume)
        artifact = directory / "sequence.o4d"
        common = {"codec": experiment["codec"], "seed": experiment.get("seed", 0), "artifact": str(artifact)}
        encode_request = {**common, "operation": "encode", "input": str(input_path),
                             "options": experiment.get("encode", {}),
                             "stage_directory": str(directory / "stages"),
                             "report": str(directory / "encode.json")}
        if args.resume and artifact.is_file() and (directory / "encode.json").is_file():
            saved = json.loads((directory / "encode-request.json").read_text())
            if saved != encode_request:
                raise ValueError(f"resume encoder options differ for {experiment['id']}")
            encode = json.loads((directory / "encode.json").read_text())
            encode["reused_saved_artifact"] = True
        else:
            encode = run_worker(encode_request, directory, args.timeout)
        decoded_path = directory / "decoded.o4d"
        decode = run_worker({**common, "operation": "decode", "decoded": str(decoded_path),
                             "options": experiment.get("decode", {}),
                             "deny_paths": [str(input_path), str(args.source.resolve()), str(directory / "stages")],
                             "report": str(directory / "decode.json")}, directory, args.timeout)
        metrics = []
        with decode_sequence(decoded_path, codec=NumPyZipCodec("raw")) as decoded:
            if len(decoded) != len(frames):
                raise ValueError("cold decode changed frame count")
            for reference, actual in zip(frames, decoded, strict=True):
                if actual.timestamp != reference.timestamp:
                    raise ValueError("cold decode changed timestamps")
                value = mesh_errors(reference.geometry, actual.geometry, peak=peak,
                                    samples=args.samples, seed=experiment.get("seed", 0), exact_hausdorff=False)
                metrics.append({"frame_index": reference.frame_index, "timestamp": reference.timestamp, **value})
        stage_metrics = []
        from open4d.core import TriangleMesh
        for record in encode["stages"]:
            with np.load(record["path"], allow_pickle=False) as arrays:
                mesh = TriangleMesh(arrays["positions"], arrays["triangles"])
            reference = frames[record["frame"]].geometry
            stage_metrics.append({"frame": record["frame"], "stage": record["stage"],
                **mesh_errors(reference, mesh, peak=peak, samples=args.samples,
                              seed=experiment.get("seed", 0), exact_hausdorff=False)})
        row = {"id": experiment["id"], "codec": experiment["codec"], "options": experiment,
               "source_sha256": source_hash, "artifact_sha256": digest(artifact),
               "artifact_bytes": artifact.stat().st_size,
               "rate_duration_seconds": rate_duration,
               "bitrate_bits_per_second": 8 * artifact.stat().st_size / rate_duration,
               "encode": encode, "decode": decode, "quality": summarize_frames(metrics), "frames": metrics,
               "stage_quality": stage_metrics,
               "cold_decode": "fresh process; Python file access to source, staged input and training stages denied"}
        (directory / "result.json").write_text(json.dumps(row, indent=2, allow_nan=False) + "\n")
        results.append(row)
        (output / "results.json").write_text(json.dumps({"code": provenance, "platform": platform.platform(),
            "source": str(args.source.resolve()), "frames": len(frames), "geometry_only": args.geometry_only,
            "results": results}, indent=2, allow_nan=False) + "\n")
        print(f"{experiment['id']}: {artifact.stat().st_size} bytes, encode {encode['seconds']:.3f}s, decode {decode['seconds']:.3f}s", flush=True)


if __name__ == "__main__":
    main()
