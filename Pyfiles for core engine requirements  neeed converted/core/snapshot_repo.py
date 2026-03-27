from __future__ import annotations

"""
Super-compressed VSD snapshot of a repository.

Builds a tar.gz archive in-memory, base64-encodes it (ASCII-only),
and stores it in the VSD under a deterministic key namespace.

Usage:
- call create_repo_snapshot(root_path) to generate and store the snapshot.
- returns a dict summary with keys: key, file_count, size_bytes, sha256.
"""

import base64
import hashlib
import io
import os
import tarfile
import time
from typing import Dict, List, Optional

from core import utils


DEFAULT_EXCLUDES = {
    ".venv", "__pycache__", ".git", "build", "dist", ".pytest_cache",
}


def _iter_repo_files(root_path: str, excludes: Optional[set[str]] = None) -> List[str]:
    root = os.path.abspath(root_path)
    ex = set(excludes or DEFAULT_EXCLUDES)
    out: List[str] = []
    for dirpath, dirnames, filenames in os.walk(root):
        # prune excluded dirs
        dirnames[:] = [d for d in dirnames if d not in ex]
        for fn in filenames:
            # skip large binary caches by extension (best-effort)
            if os.path.splitext(fn)[1].lower() in {".pyc", ".pyd", ".dll", ".so", ".exe"}:
                continue
            out.append(os.path.join(dirpath, fn))
    return out


def _make_tar_b64(root_path: str, files: List[str], *, algo: str = "xz", level: int = 9) -> tuple[str, int, str]:
    buf = io.BytesIO()
    # Choose compression mode and kwargs
    algo = (algo or "xz").lower().strip()
    if   algo == "gz":  mode = "w:gz";  kw = {"compresslevel": int(level)}
    elif algo == "bz2": mode = "w:bz2"; kw = {"compresslevel": int(level)}
    else:                mode = "w:xz";  kw = {"preset": max(0, min(9, int(level)))}
    # Create compressed tar archive
    with tarfile.open(mode=mode, fileobj=buf, **kw) as tf:
        base = os.path.abspath(root_path)
        for fp in files:
            try:
                rel = os.path.relpath(fp, base)
                arcname = rel.replace("\\", "/")
                tf.add(fp, arcname=arcname, recursive=False)
            except Exception:
                continue
    raw = buf.getvalue()
    h = hashlib.sha256(raw).hexdigest()
    b64 = base64.b64encode(raw).decode("ascii")
    return b64, len(raw), h


def create_repo_snapshot(root_path: str = ".", excludes: Optional[List[str]] = None, *, algo: str = "xz", level: int = 9) -> Dict[str, object]:
    files = _iter_repo_files(root_path, excludes=set(excludes or []))
    b64, size, sha = _make_tar_b64(root_path, files, algo=str(algo), level=int(level))
    ts = int(time.time())
    key = f"repo/snapshot/{ts}_{sha[:12]}"
    info = {
        "root": os.path.abspath(root_path),
        "file_count": int(len(files)),
        "size_bytes": int(size),
        "sha256": sha,
        "epoch": ts,
        "algo": str(algo),
        "level": int(level),
    }
    # Store in VSD under key namespace
    try:
        utils.store(f"{key}/blob_b64", b64)
        utils.store(f"{key}/info", info)
        # Track latest and append to simple index list
        utils.store("repo/snapshot/latest", key)
        index = utils.get("repo/snapshot/index", []) or []
        if isinstance(index, list):
            index.append({"key": key, "epoch": ts, "sha": sha, "size": size})
            utils.store("repo/snapshot/index", index)
    except Exception as exc:
        # Best-effort; return summary even if VSD unavailable
        info["vsd_error"] = str(exc)
    return {"key": key, **info}


def restore_repo_snapshot(key: Optional[str] = None, *, out_tar: Optional[str] = None, extract_dir: Optional[str] = None) -> Dict[str, object]:
    """
    Restore a VSD snapshot: decode base64 tar to --out-tar and/or extract to --extract.
    If key is None, uses repo/snapshot/latest.
    Returns a summary dict with key, wrote_tar, extracted_files.
    """
    if key is None:
        key = utils.get("repo/snapshot/latest", None)
    if not key:
        raise ValueError("No snapshot key provided and no latest key found")
    b64 = utils.get(f"{key}/blob_b64", None)
    info = utils.get(f"{key}/info", {}) or {}
    if not b64:
        raise ValueError(f"Snapshot blob missing for key: {key}")
    raw = base64.b64decode(str(b64))
    wrote_tar = None
    extracted_files = 0
    if out_tar:
        try:
            with open(out_tar, "wb") as f:
                f.write(raw)
            wrote_tar = os.path.abspath(out_tar)
        except Exception:
            wrote_tar = None
    if extract_dir:
        try:
            os.makedirs(extract_dir, exist_ok=True)
            with tarfile.open(mode="r:*", fileobj=io.BytesIO(raw)) as tf:
                members = tf.getmembers()
                tf.extractall(path=extract_dir, members=members)
                extracted_files = len(members)
        except Exception:
            extracted_files = 0
    return {"key": key, "wrote_tar": wrote_tar, "extracted_files": int(extracted_files), "info": info}
