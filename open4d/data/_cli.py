"""``open4d data list|info|download``: browse and fetch catalog assets."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import _catalog
from ._assets import (LicenseNotAccepted, ManualAcquisitionRequired, catalog, describe, get)


def _positive(value):
    try:
        result = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a positive integer") from error
    if result < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return result


def register(commands):
    """Add the ``data`` command group to the top-level subparsers."""
    data = commands.add_parser(
        "data", help="list, describe and download catalog datasets and checkpoints",
        description="Browse the versioned Open4D data catalog. Records keep data, weights "
        "and code licenses separate; restricted assets are acquired upstream by you.")
    actions = data.add_subparsers(dest="data_command", required=True)
    listing = actions.add_parser("list", help="list catalog assets")
    listing.add_argument("--kind", choices=_catalog.KINDS, help="only this kind of asset")
    listing.add_argument("--json", action="store_true", help="print JSON records")
    info = actions.add_parser("info", help="show licenses, provenance and acquisition steps")
    download = actions.add_parser(
        "download", help="download and verify an asset, or register a manual copy",
        description="Fetch checksum-pinned files into the cache (OPEN4D_DATA_DIR or "
        "~/.open4d/data). Interrupted downloads resume when run again.")
    for command in (info, download):
        command.add_argument("asset", help="catalog id, for example open4d-synthetic-mesh")
        command.add_argument("--version", help="exact record version (default: newest)")
    info.add_argument("--json", action="store_true", help="print the catalog record")
    download.add_argument("--path", type=Path,
                          help="register a manually acquired local copy (manual-acquisition)")
    download.add_argument("--accept-license", action="store_true",
                          help="confirm you have read and accept the listed licenses")
    download.add_argument("--cache-dir", type=Path, help="cache folder (default: OPEN4D_DATA_DIR)")
    download.add_argument("--max-download-bytes", type=_positive,
                          help="raise or lower the asset's download limit")
    download.add_argument("--max-extracted-bytes", type=_positive,
                          help="raise or lower the asset's extraction limit")
    download.add_argument("--lock-timeout", type=float,
                          help="seconds to wait for another download of the same asset")
    data.set_defaults(handler=run)
    return data


def _size(record):
    total = _catalog.total_size(record)
    if not record.get("files"):
        return "-"
    for unit in ("B", "kB", "MB", "GB", "TB"):
        if total < 1000 or unit == "TB":
            return f"{total:.0f} {unit}" if unit == "B" else f"{total:.1f} {unit}"
        total /= 1000


def _list(args):
    records = catalog(args.kind)
    if args.json:
        print(json.dumps(records, indent=2))
        return 0
    rows = [("ID", "VERSION", "KIND", "HOSTING", "LICENSES", "SIZE")]
    for record in records:
        licenses = ",".join(f"{role}={value['id']}" for role, value in record["licenses"].items())
        rows.append((record["id"], record["version"], record["kind"],
                     record["hosting"]["status"], licenses or "(members)", _size(record)))
    widths = [max(len(row[column]) for row in rows) for column in range(len(rows[0]) - 1)]
    for row in rows:
        print("  ".join(cell.ljust(width) for cell, width in zip(row, widths)) + "  " + row[-1])
    return 0


def run(args):
    """Execute a parsed ``open4d data`` command and return its exit status."""
    try:
        if args.data_command == "list":
            return _list(args)
        record = _catalog.find(args.asset, args.version)
        if args.data_command == "info":
            print(json.dumps(record, indent=2) if args.json else describe(record))
            return 0
        asset = get(args.asset, args.version, cache_dir=args.cache_dir)
        if record["kind"] == "dataset-checkpoint-pair":
            paths = asset.download(
                dataset_path=args.path, accept_license=args.accept_license,
                max_download_bytes=args.max_download_bytes,
                max_extracted_bytes=args.max_extracted_bytes, lock_timeout=args.lock_timeout)
            for role, path in paths.items():
                print(f"{role}: {path}")
            return 0
        if record["hosting"]["status"] == "manual-acquisition":
            path = asset.download(local_path=args.path)
            print(f"Registered {record['id']}@{record['version']}: {path}")
            return 0
        if args.path is not None:
            raise ValueError(f"{record['id']} is {record['hosting']['status']}; --path is "
                             "only for manually acquired assets")
        path = asset.download(accept_license=args.accept_license,
                              max_download_bytes=args.max_download_bytes,
                              max_extracted_bytes=args.max_extracted_bytes,
                              lock_timeout=args.lock_timeout)
        print(f"Verified {record['id']}@{record['version']}: {path}")
        return 0
    except ManualAcquisitionRequired as error:
        print(error)
        print("open4d: manual acquisition required; rerun with --path", file=sys.stderr)
        return 1
    except LicenseNotAccepted as error:
        print(describe(record))
        print(f"open4d: {str(error).splitlines()[0]}; rerun with --accept-license",
              file=sys.stderr)
        return 1
    except KeyError as error:
        print(f"open4d: {error.args[0]}", file=sys.stderr)
        return 1
    except (OSError, ValueError, TimeoutError) as error:
        print(f"open4d: {error}", file=sys.stderr)
        return 1
