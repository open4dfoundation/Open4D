"""Enforce adapter licensing and minimum-core compatibility boundaries."""

from io import BytesIO
from pathlib import Path
import configparser
import tarfile
import zipfile

import pytest
from check_plugin_contents import check_archive

pytestmark = pytest.mark.cpu
ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "plugins/klt"


def metadata_wheel(tmp_path, *, implementation=False, dependency="open4d<0.4,>=0.3.0"):
    path = tmp_path / "adapter.whl"
    root = "open4d_klt-0.3.0.dist-info"
    metadata = ("Metadata-Version: 2.4\nName: open4d-klt\nVersion: 0.3.0\n"
                f"License-Expression: MIT\nRequires-Dist: {dependency}\n")
    with zipfile.ZipFile(path, "w") as archive:
        for name in ("WHEEL", "RECORD", "top_level.txt"):
            archive.writestr(f"{root}/{name}", "")
        archive.writestr(f"{root}/METADATA", metadata)
        archive.writestr(f"{root}/entry_points.txt", "[open4d.codecs]\nklt = open4d.codec._klt:KLT_CODEC\n")
        archive.writestr(f"{root}/licenses/LICENSE", (PROJECT / "LICENSE").read_bytes())
        if implementation:
            archive.writestr("research/checkpoint.pt", b"restricted fixture")
    return path


def test_metadata_only_wheel_passes(tmp_path):
    assert check_archive(metadata_wheel(tmp_path), PROJECT) == []


def test_implementation_or_weight_in_adapter_wheel_is_rejected(tmp_path):
    assert "implementation" in " ".join(check_archive(metadata_wheel(tmp_path, implementation=True), PROJECT))


def test_old_core_cannot_satisfy_adapter_requirement(tmp_path):
    errors = check_archive(metadata_wheel(tmp_path, dependency="open4d>=0.2.0"), PROJECT)
    assert "matching 0.3 core" in " ".join(errors)


def test_source_archive_link_is_rejected(tmp_path):
    path = tmp_path / "adapter.tar.gz"
    with tarfile.open(path, "w:gz") as archive:
        for filename in ("LICENSE", "pyproject.toml"):
            payload = (PROJECT / filename).read_bytes()
            info = tarfile.TarInfo(f"adapter/{filename}")
            info.size = len(payload)
            archive.addfile(info, BytesIO(payload))
        info = tarfile.TarInfo("adapter/PKG-INFO")
        archive.addfile(info, BytesIO())
        link = tarfile.TarInfo("adapter/source")
        link.type = tarfile.SYMTYPE
        link.linkname = "../restricted-source"
        archive.addfile(link)
    assert "links or special" in " ".join(check_archive(path, PROJECT))
