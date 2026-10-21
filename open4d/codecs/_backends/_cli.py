"""The open4d backend command group."""

import json
from pathlib import Path
import sys

from . import _manager, _recipe, _registry


def register(commands):
    group = commands.add_parser("backend", help="plan, install, attach and diagnose research runtimes")
    actions = group.add_subparsers(dest="backend_command", required=True)
    listing = actions.add_parser("list")
    listing.add_argument("--store", type=Path)
    for name in ("plan", "install", "attach", "doctor"):
        command = actions.add_parser(name)
        command.add_argument("name")
        command.add_argument("--store", type=Path)
        if name in ("plan", "install"):
            command.add_argument("--profile")
        if name == "install":
            command.add_argument("--accept-license", action="store_true")
            command.add_argument("--jobs", type=int, default=2)
        if name == "attach":
            command.add_argument("root", type=Path)
            command.add_argument("--python")
            command.add_argument("--tool", action="append", default=[], metavar="ROLE=PATH")


def run(args):
    try:
        if args.backend_command == "list":
            result = {"recipes": _recipe.names(), "registered": _registry.load(args.store)["backends"]}
        elif args.backend_command == "plan":
            result = _manager.plan(args.name, profile=args.profile)
        elif args.backend_command == "install":
            result = _manager.install(args.name, profile=args.profile, store=args.store,
                                      jobs=args.jobs, accept_license=args.accept_license)
        elif args.backend_command == "attach":
            tools = dict(value.split("=", 1) for value in args.tool)
            result = _manager.attach(args.name, args.root, python=args.python, tools=tools, store=args.store)
        else:
            result = _manager.doctor(args.name, store=args.store)
        print(json.dumps(result, indent=2))
        return 0 if result.get("ok", True) else 1
    except (OSError, ValueError) as error:
        print(f"open4d backend: {error}", file=sys.stderr)
        return 1
