"""Use a data cache and explicitly request a licensed HTTPS asset.

Run ``python examples/tutorials/data_cache.py --cache /tmp/o4d-cache`` for
synthetic MIT mesh and Gaussian examples. They materialize only on first path
access and validate cached files on each use; nothing is downloaded by default.
For external data, explicitly add ``--url``, ``--sha256``, ``--license`` and
``--license-url``. These describe independent asset rights, not the downloader's
MIT license. Model checkpoints need their own specification and provenance.
Archive traversal, links, byte/count limits and checksums are enforced.
"""

import argparse
import json
from pathlib import Path

import open4d as o4d


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--url", help="explicit HTTPS download URL; omitted means no network")
    parser.add_argument("--sha256")
    parser.add_argument("--license")
    parser.add_argument("--license-url")
    parser.add_argument("--name", default="external-example")
    parser.add_argument("--filename", default="asset")
    parser.add_argument("--kind", choices=("mesh", "gaussian", "model"), default="mesh")
    parser.add_argument("--archive", choices=("zip", "tar"))
    args = parser.parse_args()
    if args.url:
        required = (args.sha256, args.license, args.license_url)
        if not all(required):
            parser.error("--url requires --sha256, --license and --license-url")
        specification = o4d.data.DatasetSpec(args.name, args.url, args.sha256,
                         args.license, args.license_url, args.kind,
                         filename=args.filename, archive=args.archive)
        asset = o4d.data.DownloadableDataset(specification, cache_dir=args.cache)
        print(json.dumps({"path": str(asset.path), "kind": specification.kind,
                          "license": specification.license, "sha256": specification.sha256}))
    else:
        if any((args.sha256, args.license, args.license_url, args.archive)):
            parser.error("external metadata requires --url")
        samples = (o4d.data.SyntheticMeshSequence(cache_dir=args.cache),
                   o4d.data.SyntheticGaussianSequence(cache_dir=args.cache))
        print(json.dumps({"synthetic": True, "license": "MIT",
                          "paths": [str(sample.path) for sample in samples]}, indent=2))


if __name__ == "__main__":
    main()
