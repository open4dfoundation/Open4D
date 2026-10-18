"""One encoder or decoder process; the decoder receives no source sequence."""

import hashlib
from importlib import metadata
import json
from pathlib import Path
import random
import sys
import time

import numpy as np

from open4d.codec import encode_sequence, decode_sequence
from open4d.codec._npz import NumPyZipCodec
from open4d.core import Sequence, MemoryFrameProvider
from scripts.benchmark_codec import _BASELINES


def main(request_path):
    request = json.loads(Path(request_path).read_text())
    seed = request["seed"]
    random.seed(seed)
    np.random.seed(seed)
    gpu = {}
    try:
        import torch
    except ImportError:
        torch = None
    if torch is not None:
        torch.manual_seed(seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(seed)
            torch.cuda.reset_peak_memory_stats()
    denied = [Path(p).resolve() for p in request.get("deny_paths", [])]
    def audit(event, args):
        if event == "open" and args and isinstance(args[0], (str, bytes)):
            path = Path(args[0]).resolve()
            if any(path == root or path.is_relative_to(root) for root in denied):
                raise PermissionError(f"cold decoder attempted to read original inputs: {path}")
    if denied:
        sys.addaudithook(audit)
    selected = _BASELINES.get(request["codec"], request["codec"])
    phases, rates = [], []
    stage_paths = []
    def stage(kind, ordinal, value):
        if kind == "phase":
            phases.append({"frame": ordinal, "name": value[0], "seconds": value[1]})
            if value[0] in ("preprocessing", "training", "packing"):
                print(json.dumps(phases[-1]), flush=True)
        elif kind == "rate":
            rates.append({"frame": ordinal, **value})
        elif kind == "mesh":
            label, mesh = value
            path = Path(request["stage_directory"]) / f"{ordinal:06d}-{label}.npz"
            path.parent.mkdir(exist_ok=True, parents=True)
            np.savez(path, positions=mesh.positions, triangles=mesh.triangles)
            stage_paths.append({"frame": ordinal, "stage": label, "path": str(path)})
    started = time.perf_counter()
    if request["operation"] == "encode":
        with decode_sequence(request["input"], codec=NumPyZipCodec("raw")) as sequence:
            options = dict(request.get("options", {}))
            if "checkpoint_artifacts" in options:
                # JSON object keys are strings; the codec API takes epochs.
                options["checkpoint_artifacts"] = {
                    int(epoch): str(Path(request["artifact"]).parent / path)
                    for epoch, path in options["checkpoint_artifacts"].items()
                }
            if request["codec"] in ("klt", "n4mc", "qndf", "qndf-int8"):
                options["stage_hook"] = stage
            encode_sequence(sequence, request["artifact"], codec=selected, **options)
        count = None
        opened = None
    else:
        sequence = decode_sequence(request["artifact"], codec=selected, **request.get("options", {}))
        opened = time.perf_counter() - started
        with sequence:
            frames = tuple(sequence)
            count = len(frames)
            # Timed consumption finishes before writing the validation copy.
            finished = time.perf_counter()
            copy = Sequence(MemoryFrameProvider(frames, metadata=sequence.metadata,
                            topology=sequence.topology,
                            has_vertex_correspondence=sequence.has_vertex_correspondence))
            encode_sequence(copy, request["decoded"], codec=NumPyZipCodec("raw"), overwrite=True)
    if torch is not None and torch.cuda.is_available():
        torch.cuda.synchronize()
        gpu = {"torch_peak_allocated_bytes": torch.cuda.max_memory_allocated(),
               "torch_peak_reserved_bytes": torch.cuda.max_memory_reserved()}
    if request["operation"] == "encode":
        finished = time.perf_counter()
    research_sources = {}
    for module in tuple(sys.modules.values()):
        name = getattr(module, "__file__", None)
        if name and ("/codecs/" in name or "/reconstruction/n4mc/" in name):
            path = Path(name)
            if path.suffix == ".py" and path.is_file():
                research_sources[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    versions = {}
    for distribution in ("scipy", "point-cloud-utils", "open3d", "DracoPy"):
        try:
            versions[distribution] = metadata.version(distribution)
        except metadata.PackageNotFoundError:
            pass
    report = {"operation": request["operation"], "seconds": finished - started,
              "open_seconds": opened, "frames": count, "seed": seed,
              "phases": phases, "rate_estimates": rates, "stages": stage_paths,
              "gpu": gpu, "python": sys.version,
              "numpy": np.__version__, "torch": None if torch is None else torch.__version__,
              "dependencies": versions, "loaded_research_sources": research_sources}
    Path(request["report"]).write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
