#!/usr/bin/env python3
"""Deterministic release guard.

Refuses to ship if a new release is missing files that existed in the previous release,
except for explicitly allowed removals.

Supports two modes:
  1) old.zip vs new.zip
  2) old.zip vs new_root_dir (expanded tree)

No locale-dependent behavior; path matching is simple prefix / glob.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import sys
import zipfile
from pathlib import Path

DEFAULT_ALLOW_PREFIXES = [
    "Source/",
    "Config/",
    "ue_adapter/",
    "ThirdParty/",
    "build/",
    "Binaries/",
    "Intermediate/",
    "Saved/",
    ".vs/",
]

DEFAULT_ALLOW_GLOBS = [
    "*.uplugin",
    "README_PLUGIN.txt",
    "BuildConfiguration.xml",
]


def _norm(p: str) -> str:
    # Normalize to forward slashes and strip leading ./
    p = p.replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p


def list_zip(path: str) -> set[str]:
    with zipfile.ZipFile(path, "r") as zf:
        return set(_norm(n) for n in zf.namelist() if not n.endswith("/"))


def list_root(root: str) -> set[str]:
    rootp = Path(root)
    out: set[str] = set()
    for p in rootp.rglob("*"):
        if p.is_file():
            rel = p.relative_to(rootp).as_posix()
            out.add(_norm(rel))
    return out


def load_allowlist(path: str | None) -> tuple[list[str], list[str]]:
    prefixes = list(DEFAULT_ALLOW_PREFIXES)
    globs = list(DEFAULT_ALLOW_GLOBS)
    if not path:
        return prefixes, globs

    txt = Path(path).read_text(encoding="utf-8", errors="replace")
    for raw in txt.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        line = _norm(line)
        # Heuristic: glob if it contains wildcard chars.
        if any(ch in line for ch in "*?[]"):
            globs.append(line)
        else:
            prefixes.append(line)
    return prefixes, globs


def is_allowed(path: str, prefixes: list[str], globs: list[str]) -> bool:
    p = _norm(path)
    for pfx in prefixes:
        if p.startswith(_norm(pfx)):
            return True
    base = p.split("/")[-1]
    for g in globs:
        g2 = _norm(g)
        # Match both full path and basename.
        if fnmatch.fnmatch(p, g2) or fnmatch.fnmatch(base, g2):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--old_zip", required=True)
    ap.add_argument("--new_zip")
    ap.add_argument("--new_root")
    ap.add_argument("--allow_remove", help="Path to allowed_removed_paths.txt")

    args = ap.parse_args()
    if bool(args.new_zip) == bool(args.new_root):
        print("ERROR: specify exactly one of --new_zip or --new_root")
        return 2

    old = list_zip(args.old_zip)
    new = list_zip(args.new_zip) if args.new_zip else list_root(args.new_root)

    prefixes, globs = load_allowlist(args.allow_remove)

    removed = sorted(old - new)
    unexpected = [p for p in removed if not is_allowed(p, prefixes, globs)]

    if unexpected:
        print("ERROR: unexpected removals detected (refuse to ship):")
        for p in unexpected[:200]:
            print("  -", p)
        if len(unexpected) > 200:
            print(f"  ... ({len(unexpected)-200} more)")
        return 3

    print("OK: no unexpected removals.")
    if removed:
        print(f"Note: {len(removed)} allowed removals.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
