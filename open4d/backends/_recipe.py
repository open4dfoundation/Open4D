"""Versioned backend recipes: schema validation, pin checks and host profiles.

Recipes are JSON data shipped inside this package. They describe how to
acquire and build a research runtime; they contain no runtime code. Every
source names a full commit and every shipped patch, lock or overlay file a
SHA-256. A field that cannot be pinned is ``null`` and listed in
``unresolved``, which makes ``install`` refuse rather than guess.
"""

from __future__ import annotations

from hashlib import sha256
import json
import os
from pathlib import Path
import platform
import re

SCHEMA = "open4d.backend-recipe/v1"
DATA = Path(__file__).resolve().parent
RECIPES = DATA / "recipes"
COMMIT = re.compile(r"[0-9a-f]{40}")
DIGEST = re.compile(r"[0-9a-f]{64}")
NAME = re.compile(r"[a-z0-9][a-z0-9_]*")
SAFE_RELATIVE = re.compile(r"[A-Za-z0-9_.+\-/]+")
MODES = ("build", "attach-only")
PLACEHOLDERS = {"root", "jobs", "python", "host_python", "dotnet", "tools"}


class RecipeError(ValueError):
    """A recipe is malformed or cannot be installed as requested."""


def _relative(value, where):
    if (not isinstance(value, str) or not SAFE_RELATIVE.fullmatch(value) or value.startswith("/")
            or any(part in ("", "..") for part in value.split("/"))):
        raise RecipeError(f"{where} must be a relative path inside the install, got {value!r}")
    return value


def _keys(mapping, where, required, optional=()):
    if not isinstance(mapping, dict):
        raise RecipeError(f"{where} must be an object")
    missing = set(required) - set(mapping)
    unknown = set(mapping) - set(required) - set(optional)
    if missing:
        raise RecipeError(f"{where} is missing {sorted(missing)}")
    if unknown:
        raise RecipeError(f"{where} has unknown fields {sorted(unknown)}")


def _strings(value, where):
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise RecipeError(f"{where} must be a list of strings")
    return value


def _pinned(value, pattern, where, unresolved):
    if value is None:
        unresolved.append(f"{where} is not pinned")
    elif not isinstance(value, str) or not pattern.fullmatch(value):
        raise RecipeError(f"{where} must be a full lowercase hash or null, got {value!r}")


def _data_file(record, where, unresolved):
    _relative(record["file"], f"{where}.file")
    _pinned(record["sha256"], DIGEST, f"{where}.sha256", unresolved)
    path = DATA / record["file"]
    if not path.is_file():
        raise RecipeError(f"{where} names missing package data {record['file']}")
    if record["sha256"] is not None and file_digest(path) != record["sha256"]:
        raise RecipeError(f"{where} {record['file']} does not match its recorded SHA-256")


def file_digest(path: Path) -> str:
    digest = sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _template(argument, where):
    for name in re.findall(r"\{([^}]*)\}", argument):
        if name not in PLACEHOLDERS:
            raise RecipeError(f"{where} uses unknown placeholder {{{name}}}")


def _profile(recipe, name, profile):
    where = f"{recipe['name']}:{name}"
    _keys(profile, where, ("platform", "unresolved"),
          ("description", "commands", "build_tools", "python", "sources", "files", "patches",
           "steps", "outputs", "register", "weights", "cuda"))
    unresolved = list(_strings(profile["unresolved"], f"{where}.unresolved"))
    _keys(profile["platform"], f"{where}.platform", ("system", "machine"))
    _strings(profile["platform"]["machine"], f"{where}.platform.machine")
    for command, spec in (profile.get("commands") or {}).items():
        if not isinstance(spec, str):
            raise RecipeError(f"{where}.commands.{command} must be a version specification")
        version_satisfies("0", spec, validate_only=True)
    for key in ("build_tools", "python"):
        value = profile.get(key)
        if value is None:
            continue
        required = ("file", "sha256") if key == "build_tools" else ("version", "file", "sha256")
        _keys(value, f"{where}.{key}", required, ("index_url", "extra_index_urls"))
        _data_file(value, f"{where}.{key}", unresolved)
        _strings(value.get("extra_index_urls", []), f"{where}.{key}.extra_index_urls")
    seen = set()
    for index, source in enumerate(profile.get("sources", [])):
        label = f"{where}.sources[{index}]"
        _keys(source, label, ("id", "url", "commit", "path"), ("ref", "sparse", "license", "note"))
        if not NAME.fullmatch(source["id"]) or source["id"] in seen:
            raise RecipeError(f"{label}.id must be a unique lowercase identifier")
        seen.add(source["id"])
        _pinned(source["commit"], COMMIT, f"{label} ({source['id']}) commit", unresolved)
        _relative(source["path"], f"{label}.path")
        for path in source.get("sparse") or []:
            _relative(path, f"{label}.sparse")
    for kind in ("files", "patches"):
        for index, record in enumerate(profile.get(kind, [])):
            label = f"{where}.{kind}[{index}]"
            _keys(record, label, ("file", "sha256", "target"), ("description",))
            _data_file(record, label, unresolved)
            _relative(record["target"], f"{label}.target")
    for index, step in enumerate(profile.get("steps", [])):
        label = f"{where}.steps[{index}]"
        _keys(step, label, ("name", "argv"), ("cwd", "env", "retries"))
        for argument in _strings(step["argv"], f"{label}.argv"):
            _template(argument, label)
        for value in (step.get("env") or {}).values():
            _template(value, label)
        if "cwd" in step:
            _template(step["cwd"], label)
    for index, output in enumerate(profile.get("outputs", [])):
        _keys(output, f"{where}.outputs[{index}]", ("path",), ("executable",))
        _relative(output["path"], f"{where}.outputs[{index}].path")
    register = profile.get("register", {})
    _keys(register, f"{where}.register", (), ("python", "environment", "tools"))
    for component, path in recipe["components"].items():
        _relative(path, f"{recipe['name']}.components.{component}")
    for index, weight in enumerate(profile.get("weights", [])):
        label = f"{where}.weights[{index}]"
        _keys(weight, label, ("id", "url", "sha256", "path", "license", "optional"), ("size", "note"))
        _relative(weight["path"], f"{label}.path")
        if weight["sha256"] is None and not weight["optional"]:
            unresolved.append(f"{label} ({weight['id']}) has no SHA-256")
        elif weight["sha256"] is not None and not DIGEST.fullmatch(weight["sha256"]):
            raise RecipeError(f"{label}.sha256 must be a full lowercase SHA-256")
    return unresolved


def validate(recipe: dict) -> dict:
    """Check a recipe and return its unresolved items per profile."""
    _keys(recipe, "recipe", ("schema", "name", "recipe_version", "summary", "mode", "adapter",
                             "license", "components", "profiles"),
          ("attach", "smoke", "provenance"))
    if recipe["schema"] != SCHEMA:
        raise RecipeError(f"unsupported recipe schema {recipe['schema']!r}")
    if not NAME.fullmatch(recipe["name"]):
        raise RecipeError(f"invalid backend name {recipe['name']!r}")
    if type(recipe["recipe_version"]) is not int or recipe["recipe_version"] < 1:
        raise RecipeError("recipe_version must be a positive integer")
    if recipe["mode"] not in MODES:
        raise RecipeError(f"mode must be one of {MODES}")
    _keys(recipe["adapter"], "adapter", ("group", "entry_points", "distributions", "versions"))
    _strings(recipe["adapter"]["entry_points"], "adapter.entry_points")
    _strings(recipe["adapter"]["distributions"], "adapter.distributions")
    version_satisfies("0", recipe["adapter"]["versions"], validate_only=True)
    _keys(recipe["license"], "license", ("ledger", "component", "decision", "terms", "restrictions"))
    _strings(recipe["license"]["restrictions"], "license.restrictions")
    if not isinstance(recipe["components"], dict) or not recipe["components"]:
        raise RecipeError("components must map runtime names to install paths")
    if recipe.get("attach") is not None:
        _keys(recipe["attach"], "attach", ("components",), ("python",))
        for component, rule in recipe["attach"]["components"].items():
            _keys(rule, f"attach.components.{component}", ("candidates", "markers"))
    if recipe.get("smoke") is not None:
        _keys(recipe["smoke"], "smoke", ("kind",),
              ("codec", "frames", "subdivisions", "encode", "decode", "modules", "timeout", "note"))
        if recipe["smoke"]["kind"] not in ("roundtrip", "imports"):
            raise RecipeError("smoke.kind must be roundtrip or imports")
    if recipe["mode"] == "attach-only" and recipe["profiles"]:
        for name, profile in recipe["profiles"].items():
            if profile.get("steps") or profile.get("sources"):
                raise RecipeError(f"attach-only recipe {recipe['name']} must not build ({name})")
    return {name: _profile(recipe, name, profile) for name, profile in recipe["profiles"].items()}


def names() -> list[str]:
    return sorted(path.stem for path in RECIPES.glob("*.json"))


def load(name: str, path: str | os.PathLike[str] | None = None) -> dict:
    """Load and validate one packaged (or explicitly given) recipe."""
    source = Path(path) if path is not None else RECIPES / f"{name}.json"
    if path is None and (not NAME.fullmatch(name) or not source.is_file()):
        raise RecipeError(f"no backend recipe named {name!r}; choose from {', '.join(names())}")
    try:
        recipe = json.loads(source.read_text(encoding="utf-8"))
    except ValueError as error:
        raise RecipeError(f"{source} is not valid JSON: {error}") from error
    unresolved = validate(recipe)
    recipe["_unresolved"] = unresolved
    recipe["_sha256"] = file_digest(source)
    recipe["_path"] = str(source)
    return recipe


def host_profile_names(recipe: dict) -> list[str]:
    """Profiles whose platform matches this machine."""
    system, machine = platform.system(), platform.machine()
    return [name for name, profile in recipe["profiles"].items()
            if profile["platform"]["system"] == system and machine in profile["platform"]["machine"]]


def profile(recipe: dict, name: str | None) -> tuple[str, dict]:
    if not recipe["profiles"]:
        raise RecipeError(f"{recipe['name']} has no installable profile; it is attach-only")
    if name is None:
        matches = host_profile_names(recipe)
        if not matches:
            raise RecipeError(f"{recipe['name']} has no profile for this host; choose --profile from "
                              f"{', '.join(recipe['profiles'])}")
        name = matches[0]
    if name not in recipe["profiles"]:
        raise RecipeError(f"{recipe['name']} has no profile {name!r}; choose from {', '.join(recipe['profiles'])}")
    return name, recipe["profiles"][name]


def _version(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in re.findall(r"\d+", text)[:4])


def version_satisfies(version: str, spec: str, *, validate_only: bool = False) -> bool:
    """A small subset of PEP 440 specifiers: >=, >, <=, <, ==, != and X.Y.* ."""
    result = True
    for clause in (part.strip() for part in spec.split(",")):
        match = re.fullmatch(r"(>=|<=|==|!=|>|<)?\s*([0-9]+(?:\.[0-9]+)*)(\.\*)?|\*", clause)
        if not match:
            raise RecipeError(f"unsupported version specification {spec!r}")
        if validate_only or clause == "*":
            continue
        operator, target, wildcard = match.group(1) or "==", match.group(2), match.group(3)
        have, want = _version(version), _version(target)
        if wildcard:
            ok = have[:len(want)] == want
            ok = not ok if operator == "!=" else ok
        else:
            width = max(len(have), len(want))
            have, want = have + (0,) * (width - len(have)), want + (0,) * (width - len(want))
            ok = {">=": have >= want, "<=": have <= want, ">": have > want, "<": have < want,
                  "==": have == want, "!=": have != want}[operator]
        result = result and ok
    return result


def first_version(text: str) -> str | None:
    match = re.search(r"\d+\.\d+(?:\.\d+)*", text)
    return match.group(0) if match else None
