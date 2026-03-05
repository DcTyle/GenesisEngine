#!/usr/bin/env python3
# Deterministic manifest generator (no security semantics).
# Produces a compact per-file integrity record to detect accidental omissions.

from __future__ import annotations
import os, sys, json
from pathlib import Path

def id9_sums(data: bytes) -> dict:
    # Two simple deterministic 32-bit sums (not cryptographic).
    a = 0
    b = 0
    mod = 2**32
    for i, by in enumerate(data, start=1):
        a = (a + by) % mod
        b = (b + (by * i)) % mod
    return {"id9_a_u32": a, "id9_b_u32": b}

def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make_release_manifest.py <root_dir> <out_json>")
        return 2
    root = Path(sys.argv[1]).resolve()
    outp = Path(sys.argv[2]).resolve()

    rows = []
    for p in sorted(root.rglob("*")):
        if p.is_dir():
            continue
        rel = p.relative_to(root).as_posix()
        # Skip build outputs by default.
        if "/build/" in f"/{rel}/" or rel.startswith("build/"):
            continue
        data = p.read_bytes()
        rec = {"path": rel, "bytes": len(data)}
        rec.update(id9_sums(data))
        rows.append(rec)

    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_text(json.dumps({"root": root.as_posix(), "files": rows}, indent=2), encoding="utf-8")
    print(f"Wrote manifest: {outp} ({len(rows)} files)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
