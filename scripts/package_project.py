#!/usr/bin/env python3

"""Package a Genesis Engine project into a distributable directory.

Roadmap requirement (production polish):
  - Package game settings, project native UI, input bindings, and content.
  - Do NOT package editor UI.
  - Do NOT package AI vault / research caches unless explicitly requested.

This script is intentionally conservative: it copies only allowlisted runtime
components and project settings. It is deterministic (sorted walks) and avoids
including build artifacts.
"""

from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path


ALLOWLIST_DIRS = [
    # Project/user settings needed at runtime.
    Path("Draft Container") / "ProjectSettings",
    # Engine policies needed for deterministic behavior.
    Path("GenesisEngineState") / "Policies",
]

ALLOWLIST_CONTENT_ROOTS = [
    # Optional user content (if present): this is the only place we pull assets
    # from by default.
    Path("Content"),
    Path("Draft Container") / "Content",
]


def _safe_mkdir(p: Path) -> None:
    p.mkdir(parents=True, exist_ok=True)


def _copy_tree_sorted(src: Path, dst: Path) -> None:
    if not src.exists():
        return
    for root, dirs, files in os.walk(src):
        root_p = Path(root)
        rel = root_p.relative_to(src)
        out_dir = dst / rel
        _safe_mkdir(out_dir)
        dirs.sort()
        files.sort()
        for fn in files:
            s = root_p / fn
            d = out_dir / fn
            shutil.copy2(s, d)


def _copy_runtime_binary(build_dir: Path, out_dir: Path) -> None:
    # Try a few likely locations.
    candidates = [
        build_dir / "runtime_cli" / "genesis_runtime_cli",
        build_dir / "runtime_cli" / "genesis_runtime_cli.exe",
        build_dir / "vulkan_app" / "genesis_vulkan_app",
        build_dir / "vulkan_app" / "genesis_vulkan_app.exe",
    ]
    for c in candidates:
        if c.exists() and c.is_file():
            _safe_mkdir(out_dir / "Binaries")
            shutil.copy2(c, out_dir / "Binaries" / c.name)
            return


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo_root", required=True, help="Path to repo root")
    ap.add_argument("--build_dir", required=True, help="Path to CMake build directory")
    ap.add_argument("--out_dir", required=True, help="Output directory for packaged build")
    args = ap.parse_args()

    repo_root = Path(args.repo_root).resolve()
    build_dir = Path(args.build_dir).resolve()
    out_dir = Path(args.out_dir).resolve()

    if out_dir.exists():
        shutil.rmtree(out_dir)
    _safe_mkdir(out_dir)

    # Copy allowlisted settings/policies.
    for rel in ALLOWLIST_DIRS:
        _copy_tree_sorted(repo_root / rel, out_dir / rel)

    # Copy allowlisted content (if present).
    for rel in ALLOWLIST_CONTENT_ROOTS:
        _copy_tree_sorted(repo_root / rel, out_dir / rel)

    # Copy runtime binary if present.
    _copy_runtime_binary(build_dir, out_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
