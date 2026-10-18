"""The versioned asset catalog shipped as package data, and its validator.

Each record names exact bytes (per-file and per-part SHA-256), who publishes
them, the separate data/weights/code licenses with evidence, how they may be
obtained, and the conventions a loader needs. A record is information, not a
grant of rights: hosting and redistribution states are reviewed by people.
"""

from __future__ import annotations

import copy
import hashlib
import json
import re
import urllib.parse
from importlib import resources

SCHEMA = "open4d-data-catalog/1"
KINDS = ("mesh-sequence", "gaussian-sequence", "multiview-video", "checkpoint",
         "dataset-checkpoint-pair")
FORMATS = ("obj-sequence", "ply-gaussian-sequence", "queen-frames", "mesh-directory",
           "multiview-video")
HOSTING = ("open4d-hosted", "pending-upload", "upstream-download", "manual-acquisition")
REVIEW = ("approved", "pending-review", "not-reviewed", "rejected")
LICENSE_ROLES = ("data", "weights", "code")
ARCHIVES = (None, "tar", "zip")
DEFAULT_LIMITS = {"max_download_bytes": 1_073_741_824,
                  "max_extracted_bytes": 4_294_967_296, "max_files": 100_000}
_HF_HOSTS = {"huggingface.co", "hf.co"}
_ID = re.compile(r"[a-z0-9][a-z0-9-]*[a-z0-9]")
_VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
_DIGEST = re.compile(r"[a-f0-9]{64}")
_COMMIT = re.compile(r"[a-f0-9]{40}")
_METADATA = {
    "mesh-sequence": ("fps", "frame_count", "units", "coordinate_convention", "topology"),
    "gaussian-sequence": ("fps", "frame_count", "units", "coordinate_convention", "gaussian"),
    "multiview-video": ("fps", "frame_count", "units", "coordinate_convention", "cameras"),
    "checkpoint": ("fps", "frame_count", "units", "coordinate_convention", "gaussian"),
    "dataset-checkpoint-pair": (),
}
_LICENSES = {"mesh-sequence": ("data",), "gaussian-sequence": ("data",),
             "multiview-video": ("data",), "checkpoint": ("data", "weights", "code"),
             "dataset-checkpoint-pair": ()}


class CatalogError(ValueError):
    """The catalog, or one record in it, violates the schema."""


def https_url(url):
    """Reject non-HTTPS, credentialed, and mutable Hugging Face URLs."""
    parsed = urllib.parse.urlsplit(url) if isinstance(url, str) else None
    if (parsed is None or parsed.scheme != "https" or not parsed.hostname
            or parsed.username or parsed.password):
        raise ValueError(f"URL must be HTTPS without credentials: {url!r}")
    if parsed.hostname.lower() in _HF_HOSTS and "/resolve/" in parsed.path:
        hugging_face_revision(url)
    return url


def hugging_face_revision(url):
    """Return the full commit hash a Hugging Face file URL is pinned to.

    Branches, tags, short hashes and ``refs/pr/N`` move or are ambiguous, so
    only ``.../resolve/<40-hex commit>/<path>`` identifies immutable bytes.
    """
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or (parsed.hostname or "").lower() not in _HF_HOSTS:
        raise ValueError(f"not a Hugging Face HTTPS URL: {url!r}")
    if parsed.query not in ("", "download=true") or parsed.fragment:
        raise ValueError(f"Hugging Face URL has unexpected query or fragment: {url!r}")
    parts = [urllib.parse.unquote(part) for part in parsed.path.split("/")[1:]]
    if parts[:1] in (["datasets"], ["spaces"]):
        parts = parts[1:]
    if (len(parts) < 5 or parts[2] != "resolve" or any(
            part in ("", ".", "..") for part in parts)):
        raise ValueError(f"Hugging Face URL must be .../<repo>/resolve/<commit>/<file>: {url!r}")
    if not _COMMIT.fullmatch(parts[3]):
        raise ValueError(f"Hugging Face URL must pin a full 40-hex commit, not {parts[3]!r}: {url!r}")
    return parts[3]


def version_key(version):
    return tuple(int(part) for part in version.split("."))


def record_digest(record):
    """A stable identity for one record; caches are keyed by it."""
    return hashlib.sha256(json.dumps(record, sort_keys=True, separators=(",", ":"))
                          .encode()).hexdigest()


def load_catalog(path=None):
    """Read and validate the packaged catalog, or a catalog file at ``path``."""
    if path is None:
        text = resources.files("open4d.data").joinpath("catalog.json").read_text("utf-8")
    else:
        with open(path, encoding="utf-8") as stream:
            text = stream.read()
    catalog = json.loads(text)
    errors = validate_catalog(catalog)
    if errors:
        raise CatalogError("invalid data catalog:\n  " + "\n  ".join(errors))
    return catalog


def records(catalog=None):
    """Every record, deep-copied so callers cannot alter the loaded catalog."""
    return [copy.deepcopy(item) for item in (catalog or _packaged())["assets"]]


def find(asset_id, version=None, catalog=None):
    """The newest record for ``asset_id``, or the exact ``version``."""
    matches = [item for item in (catalog or _packaged())["assets"] if item["id"] == asset_id]
    if not matches:
        raise KeyError(f"unknown data asset {asset_id!r}; run 'open4d data list'")
    if version is None:
        return copy.deepcopy(max(matches, key=lambda item: version_key(item["version"])))
    for item in matches:
        if item["version"] == version:
            return copy.deepcopy(item)
    available = ", ".join(sorted((item["version"] for item in matches), key=version_key))
    raise KeyError(f"{asset_id} has no version {version!r}; available: {available}")


_CACHE = {}


def _packaged():
    if "catalog" not in _CACHE:
        _CACHE["catalog"] = load_catalog()
    return _CACHE["catalog"]


def limits(record):
    return {**DEFAULT_LIMITS, **record.get("limits", {})}


def total_size(record):
    return sum(item["size"] for item in record.get("files", ()))


# Validation ---------------------------------------------------------------

def _text(value):
    return isinstance(value, str) and bool(value.strip())


def _positive(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def _url(errors, where, value, *, optional=False):
    if value is None and optional:
        return
    try:
        https_url(value)
    except ValueError as error:
        errors.append(f"{where}: {error}")


def _check_file(errors, where, item, status):
    if not isinstance(item, dict):
        errors.append(f"{where}: must be an object")
        return
    unknown = set(item) - {"name", "url", "sha256", "size", "archive", "parts", "note"}
    if unknown:
        errors.append(f"{where}: unknown fields {sorted(unknown)}")
    if not isinstance(item.get("name"), str) or not _NAME.fullmatch(item["name"]):
        errors.append(f"{where}: name must be a safe single path component")
    if not _positive(item.get("size")):
        errors.append(f"{where}: size must be a positive integer")
    digest = item.get("sha256")
    if digest is not None and not (isinstance(digest, str) and _DIGEST.fullmatch(digest)):
        errors.append(f"{where}: sha256 must be 64 lowercase hex digits or null")
    if item.get("archive") not in ARCHIVES:
        errors.append(f"{where}: archive must be null, tar or zip")
    parts = item.get("parts")
    fetched = status in ("open4d-hosted", "upstream-download")
    if parts is not None:
        if not isinstance(parts, list) or len(parts) < 2:
            errors.append(f"{where}: parts must list at least two parts")
            return
        if item.get("url") is not None:
            errors.append(f"{where}: a multipart file has part URLs, not a whole-file URL")
        for index, part in enumerate(parts):
            here = f"{where}.parts[{index}]"
            if not isinstance(part, dict) or set(part) - {"url", "sha256", "size"}:
                errors.append(f"{here}: must have only url, sha256 and size")
                continue
            if not (isinstance(part.get("sha256"), str) and _DIGEST.fullmatch(part["sha256"])):
                errors.append(f"{here}: every part needs a sha256")
            if not _positive(part.get("size")):
                errors.append(f"{here}: size must be a positive integer")
            _url(errors, here, part.get("url"), optional=not fetched)
        if all(isinstance(part, dict) and _positive(part.get("size")) for part in parts) and (
                sum(part["size"] for part in parts) != item.get("size")):
            errors.append(f"{where}: part sizes must add up to the file size")
    else:
        _url(errors, where, item.get("url"), optional=not fetched)
        if fetched and digest is None:
            errors.append(f"{where}: a downloadable single file needs a sha256")
    if status == "pending-upload":
        if digest is None:
            errors.append(f"{where}: a pending upload must record the built file's sha256")
        if item.get("url") is not None or parts is not None:
            errors.append(f"{where}: a pending upload has no URL until it is published")


def _check_license(errors, where, value):
    if not isinstance(value, dict) or set(value) - {"id", "url", "evidence", "note"}:
        errors.append(f"{where}: must have id, url, evidence and optional note")
        return
    if not _text(value.get("id")):
        errors.append(f"{where}: license id is required")
    _url(errors, f"{where}.url", value.get("url"))
    _url(errors, f"{where}.evidence", value.get("evidence"))


def _check_checkpoint(errors, where, value):
    if not isinstance(value, dict):
        errors.append(f"{where}: must be an object")
        return
    for name in ("training_dataset", "architecture", "preprocessing", "split", "config",
                 "runtime", "evaluation"):
        if not value.get(name):
            errors.append(f"{where}.{name} is required")
    dataset = value.get("training_dataset") or {}
    if not (_text(dataset.get("id")) and _text(dataset.get("version"))):
        errors.append(f"{where}.training_dataset needs id and version")
    digest = dataset.get("digest")
    if digest is None:
        if not _text(dataset.get("digest_status")):
            errors.append(f"{where}.training_dataset: explain a missing digest in digest_status")
    elif not (isinstance(digest, str) and _DIGEST.fullmatch(digest)):
        errors.append(f"{where}.training_dataset.digest must be a sha256")
    architecture = value.get("architecture") or {}
    if not _text(architecture.get("name")):
        errors.append(f"{where}.architecture.name is required")
    _url(errors, f"{where}.architecture.repository", architecture.get("repository"))
    if not (isinstance(architecture.get("commit"), str) and _COMMIT.fullmatch(architecture["commit"])):
        errors.append(f"{where}.architecture.commit must be a full 40-hex commit")
    config = value.get("config") or {}
    if not _text(config.get("path")):
        errors.append(f"{where}.config.path is required")
    _url(errors, f"{where}.config.url", config.get("url"))
    _url(errors, f"{where}.evaluation.reference", (value.get("evaluation") or {}).get("reference"))


def _check_record(errors, item, index):
    where = f"assets[{index}]"
    if not isinstance(item, dict):
        errors.append(f"{where}: must be an object")
        return
    if isinstance(item.get("id"), str):
        where = f"{item['id']}@{item.get('version')}"
    if not (isinstance(item.get("id"), str) and _ID.fullmatch(item["id"])):
        errors.append(f"{where}: id must be lowercase letters, digits and hyphens")
    if not (isinstance(item.get("version"), str) and _VERSION.fullmatch(item["version"])):
        errors.append(f"{where}: version must be MAJOR.MINOR.PATCH")
    kind = item.get("kind")
    if kind not in KINDS:
        errors.append(f"{where}: kind must be one of {', '.join(KINDS)}")
        return
    for name in ("title", "description", "citation"):
        if not _text(item.get(name)):
            errors.append(f"{where}: {name} is required")
    if kind != "dataset-checkpoint-pair" and item.get("format") not in FORMATS:
        errors.append(f"{where}: format must be one of {', '.join(FORMATS)}")
    publisher = item.get("publisher") or {}
    if not _text(publisher.get("name")):
        errors.append(f"{where}: publisher.name is required")
    _url(errors, f"{where}: publisher.url", publisher.get("url"))
    licenses = item.get("licenses")
    if not isinstance(licenses, dict) or set(licenses) - set(LICENSE_ROLES):
        errors.append(f"{where}: licenses maps data, weights and code to license records")
        licenses = {}
    for role in _LICENSES[kind]:
        if role not in licenses:
            errors.append(f"{where}: a {kind} needs a separate {role} license")
    for role, value in licenses.items():
        _check_license(errors, f"{where}: licenses.{role}", value)
    if not isinstance(item.get("requires_license_acceptance"), bool):
        errors.append(f"{where}: requires_license_acceptance must be true or false")
    hosting = item.get("hosting") or {}
    status = hosting.get("status")
    if status not in HOSTING:
        errors.append(f"{where}: hosting.status must be one of {', '.join(HOSTING)}")
    review = item.get("redistribution") or {}
    if review.get("status") not in REVIEW:
        errors.append(f"{where}: redistribution.status must be one of {', '.join(REVIEW)}")
    if not _text(review.get("notes")):
        errors.append(f"{where}: redistribution.notes must record who reviewed what")
    if status == "open4d-hosted" and review.get("status") != "approved":
        errors.append(f"{where}: Open4D may host only assets whose redistribution is approved")
    if status in ("open4d-hosted", "pending-upload"):
        if hosting.get("provider") not in ("huggingface", "r2"):
            errors.append(f"{where}: hosting.provider must be huggingface or r2")
        if not _text(hosting.get("repository")):
            errors.append(f"{where}: hosting.repository is required")
    if status == "manual-acquisition":
        _url(errors, f"{where}: hosting.acquisition_url", hosting.get("acquisition_url"))
        if not _text(hosting.get("instructions")):
            errors.append(f"{where}: hosting.instructions are required for manual acquisition")
        expected = hosting.get("expected_entries", [])
        if not isinstance(expected, list) or not all(_text(entry) for entry in expected):
            errors.append(f"{where}: hosting.expected_entries must list glob patterns")
    if status == "upstream-download":
        _url(errors, f"{where}: hosting.release_url", hosting.get("release_url"))
    files = item.get("files")
    if kind == "dataset-checkpoint-pair":
        if files:
            errors.append(f"{where}: a pair references members and has no files")
        members = item.get("members") or {}
        for role in ("dataset", "checkpoint"):
            ref = members.get(role) or {}
            if not (_text(ref.get("id")) and _text(ref.get("version"))):
                errors.append(f"{where}: members.{role} needs id and version")
    else:
        if not isinstance(files, list) or (status != "manual-acquisition" and not files):
            errors.append(f"{where}: files must list at least one file")
            files = []
        names = [entry.get("name") for entry in files if isinstance(entry, dict)]
        if len(names) != len(set(names)):
            errors.append(f"{where}: file names must be unique")
        for number, entry in enumerate(files):
            _check_file(errors, f"{where}: files[{number}]", entry, status)
        if status == "open4d-hosted" and hosting.get("provider") == "huggingface":
            revision = hosting.get("revision")
            if not (isinstance(revision, str) and _COMMIT.fullmatch(revision)):
                errors.append(f"{where}: hosting.revision must be the full Hugging Face commit")
            for entry in files:
                for url in [entry.get("url")] + [part.get("url") for part in entry.get("parts") or ()]:
                    if url is None:
                        continue
                    try:
                        if hugging_face_revision(url) != revision:
                            errors.append(f"{where}: {url} is not pinned to hosting.revision")
                    except ValueError as error:
                        errors.append(f"{where}: {error}")
        metadata = item.get("metadata")
        if not isinstance(metadata, dict):
            errors.append(f"{where}: metadata must be an object")
            metadata = {}
        for name in _METADATA[kind]:
            if name not in metadata:
                errors.append(f"{where}: metadata.{name} is required (null when unknown)")
        gaussian = metadata.get("gaussian")
        if "gaussian" in _METADATA[kind] and gaussian is not None and not (
                isinstance(gaussian, dict) and "sh_degree" in gaussian
                and _text(gaussian.get("parameter_convention"))):
            errors.append(f"{where}: metadata.gaussian needs sh_degree and parameter_convention")
    if kind == "checkpoint" or "checkpoint" in item:
        _check_checkpoint(errors, f"{where}: checkpoint", item.get("checkpoint"))
    configured = item.get("limits", {})
    if not isinstance(configured, dict) or set(configured) - set(DEFAULT_LIMITS) or not all(
            _positive(value) for value in configured.values()):
        errors.append(f"{where}: limits may set positive {', '.join(DEFAULT_LIMITS)}")
    elif files and all(isinstance(entry, dict) and _positive(entry.get("size")) for entry in files):
        if status != "manual-acquisition" and total_size(item) > limits(item)["max_download_bytes"]:
            errors.append(f"{where}: files exceed the asset's max_download_bytes")


def validate_catalog(catalog):
    """Return every schema and cross-reference error; empty means valid."""
    errors = []
    if not isinstance(catalog, dict) or catalog.get("schema") != SCHEMA:
        return [f"catalog schema must be {SCHEMA!r}"]
    if not (isinstance(catalog.get("catalog_version"), str)
            and _VERSION.fullmatch(catalog["catalog_version"])):
        errors.append("catalog_version must be MAJOR.MINOR.PATCH")
    assets = catalog.get("assets")
    if not isinstance(assets, list) or not assets:
        return errors + ["assets must be a nonempty list"]
    for index, item in enumerate(assets):
        _check_record(errors, item, index)
    if errors:
        return errors
    known = {(item["id"], item["version"]): item for item in assets}
    if len(known) != len(assets):
        errors.append("each id and version pair must be unique")
    for item in assets:
        where = f"{item['id']}@{item['version']}"
        dataset = (item.get("checkpoint") or {}).get("training_dataset")
        if dataset is not None:
            target = known.get((dataset["id"], dataset["version"]))
            if target is None:
                errors.append(f"{where}: training dataset {dataset['id']}@{dataset['version']} "
                              "is not in the catalog")
            elif dataset.get("digest") is not None and dataset["digest"] not in {
                    entry.get("sha256") for entry in target["files"]}:
                errors.append(f"{where}: training dataset digest matches none of its files")
        if item["kind"] == "dataset-checkpoint-pair":
            members = item["members"]
            data = known.get((members["dataset"]["id"], members["dataset"]["version"]))
            model = known.get((members["checkpoint"]["id"], members["checkpoint"]["version"]))
            if data is None or model is None:
                errors.append(f"{where}: pair members must be catalog records")
            elif model["kind"] != "checkpoint":
                errors.append(f"{where}: members.checkpoint must be a checkpoint")
            else:
                trained = model["checkpoint"]["training_dataset"]
                if (trained["id"], trained["version"]) != (data["id"], data["version"]):
                    errors.append(f"{where}: the checkpoint was trained on "
                                  f"{trained['id']}@{trained['version']}, not this dataset")
    return errors
