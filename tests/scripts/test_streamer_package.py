from __future__ import annotations

from io import BytesIO
from pathlib import Path
import tarfile
import zipfile

import pytest

from scripts.check_streamer_package import (
    DRACO_DIGESTS, LICENSE_FILES, PROJECT, VENDOR, check_archive,
)

pytestmark = pytest.mark.cpu


def package_files():
    files = {path.relative_to(PROJECT).as_posix(): path.read_bytes()
             for path in (PROJECT / "streamer").rglob("*.py")}
    for name in ("streamer/client/viewer.html", "streamer/client/worker.js",
                 *(VENDOR + item for item in (*DRACO_DIGESTS, "NOTICE", "README.md"))):
        files[name] = (PROJECT / name).read_bytes()
    return files


def metadata():
    return ("Metadata-Version: 2.4\nName: open4d-streamer\nVersion: 0.1.0\n"
            "License-Expression: MIT AND Apache-2.0\n"
            + "".join(f"License-File: {name}\n" for name in LICENSE_FILES)
            + "\n").encode()


def wheel(tmp_path, changes=None):
    archive = tmp_path / "open4d_streamer-0.1.0-py3-none-any.whl"
    files = package_files()
    root = "open4d_streamer-0.1.0.dist-info/"
    files[root + "METADATA"] = metadata()
    for name in LICENSE_FILES:
        files[root + "licenses/" + name] = (PROJECT / name).read_bytes()
    files.update(changes or {})
    with zipfile.ZipFile(archive, "w") as package:
        for name, data in files.items():
            package.writestr(name, data)
    return archive


def test_wheel_checks_runtime_and_pinned_upstream_licenses(tmp_path):
    assert check_archive(wheel(tmp_path)) == []


def test_checker_rejects_research_content_and_changed_decoder(tmp_path):
    archive = wheel(tmp_path, {"streamer/study/unlicensed.js": b"study",
                              VENDOR + "draco_decoder.wasm": b"modified decoder"})
    errors = check_archive(archive)
    assert any("excluded research/test" in error for error in errors)
    assert any("Draco upstream digest mismatch" in error for error in errors)


def test_checker_rejects_missing_notice_in_wheel_license_metadata(tmp_path):
    archive = wheel(tmp_path)
    replacement = tmp_path / "missing-notice.whl"
    with zipfile.ZipFile(archive) as source, zipfile.ZipFile(replacement, "w") as target:
        for item in source.infolist():
            if not item.filename.endswith("dist-info/licenses/" + VENDOR + "NOTICE"):
                target.writestr(item.filename, source.read(item))
    assert any("wheel license attribution" in error for error in check_archive(replacement))


def test_sdist_rejects_study_independently_of_wheel(tmp_path):
    files = package_files()
    for name in ("LICENSE", "README.md", "pyproject.toml", "MANIFEST.in"):
        files[name] = (PROJECT / name).read_bytes()
    files["PKG-INFO"] = metadata()
    files["study/system/WebClient/unlicensed.js"] = b"study"
    archive = tmp_path / "open4d_streamer-0.1.0.tar.gz"
    with tarfile.open(archive, "w:gz") as package:
        for name, data in files.items():
            info = tarfile.TarInfo("open4d_streamer-0.1.0/" + name)
            info.size = len(data)
            package.addfile(info, BytesIO(data))
    assert any("excluded research/test" in error for error in check_archive(archive))
