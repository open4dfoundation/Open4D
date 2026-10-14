#!/usr/bin/env python3
"""Build downloadable adapter packages outside the source tree and verify them."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from check_plugin_contents import check_archive

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--plugin", action="append", help="directory name under plugins; repeat to select")
    args = parser.parse_args()
    output = args.out_dir.expanduser().resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("adapter build output must be outside the repository")
    output.mkdir(parents=True, exist_ok=True)
    projects = {path.name: path for path in (ROOT / "plugins").iterdir() if (path / "pyproject.toml").is_file()}
    names = sorted(args.plugin or projects)
    unknown = set(names) - projects.keys()
    if unknown:
        parser.error(f"unknown adapter packages: {sorted(unknown)}")
    for name in names:
        project = projects[name]
        with tempfile.TemporaryDirectory(prefix="open4d-plugin-build-") as directory:
            copied = Path(directory) / name
            copied.mkdir()
            for filename in ("LICENSE", "pyproject.toml"):
                shutil.copyfile(project / filename, copied / filename)
            artifacts = Path(directory) / "dist"
            subprocess.run([sys.executable, "-m", "build", "--outdir", str(artifacts), str(copied)], check=True)
            for archive in sorted(artifacts.iterdir()):
                errors = check_archive(archive, project)
                if errors:
                    parser.exit(1, "\n".join(errors) + "\n")
                shutil.copyfile(archive, output / archive.name)
                print(f"Verified adapter: {archive.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
