from __future__ import annotations
from VHW.system_utils import clamp


def compute_delta_evolution_vector(vec_a, vec_b, method: str = "diff", **kwargs):
    """Vectorized deviation-of-evolution helper.

    This function is the canonical way to express a per-particle/state
    evolution delta across the stack (miner, VHW, neural subsystems).

    Parameters
    ----------
    vec_a : sequence of float/complex
        Previous state vector.
    vec_b : sequence of float/complex
        Next state vector.
    method : str
        "diff" (default) computes element-wise clamped difference b - a.

    Returns
    -------
    list
        List of floats or complex numbers representing the delta.
    """
    if not isinstance(vec_a, (list, tuple)) or not isinstance(vec_b, (list, tuple)):
        raise ValueError("Inputs must be list or tuple of numeric values")
    if len(vec_a) != len(vec_b):
        raise ValueError("Vectors must be same length")

    if method == "diff":
        out = []
        for a, b in zip(vec_a, vec_b):
            if isinstance(a, complex) or isinstance(b, complex):
                ar = float(getattr(a, "real", a))
                ai = float(getattr(a, "imag", 0.0))
                br = float(getattr(b, "real", b))
                bi = float(getattr(b, "imag", 0.0))
                dr = clamp(br - ar, -1.0, 1.0)
                di = clamp(bi - ai, -1.0, 1.0)
                out.append(complex(dr, di))
            else:
                dv = clamp(float(b) - float(a), -1.0, 1.0)
                out.append(dv)
        return out

    # Future methods (e.g., phase-based evolution) can be added here.
    raise NotImplementedError("Unknown delta evolution method: %s" % (method,))

# ================================================================
# Path: core/utils.py
# Version: v4.8.9.2 Hybrid (Jarvis ADA v4.7 compatible)
# Description:
#   Live-mode utility layer for the VirtualMiner stack.
#   Handles VSD I/O, telemetry, hardware snapshots, atomic locks,
#   hashrate conversions, profit computations, and tokenized vector codec.
# ================================================================

# ---------------------------
# Standard library imports
# ---------------------------
import time
import os
import json
import math
import hashlib
import threading
import random
import re
import sys
import logging
from typing import Any, Dict, List, Optional, Tuple, Union

# ---------------------------
# Third-party (guarded)
# ---------------------------
try:
    import requests
except Exception:
    requests = None  # type: ignore

# ---------------------------
# Logger (UTC, ASCII-only)
# ---------------------------
def log() -> logging.Logger:
    logger = logging.getLogger("core.utils")
    if not logger.handlers:
        h = logging.StreamHandler(stream=sys.stdout)
        h.setFormatter(logging.Formatter(
            "[%(asctime)s] %(levelname)s core.utils: %(message)s"
        ))
        logger.addHandler(h)
    logger.setLevel(logging.INFO)
    return logger

log = log()


# ---------------------------
# BIOS EventBus (guarded)
# ---------------------------
class _NoOpBus:
    def publish(self, topic: str, data: Dict[str, Any] | None = None) -> None:
        return
    def subscribe(self, *a, **kw) -> None:
        return

def _get_bus():
    try:
        from bios.event_bus import get_event_bus  # type: ignore
        return get_event_bus()
    except Exception as exc:
        log.warning("bios.event_bus unavailable, using NoOp bus: %s", exc)
        return _NoOpBus()

_bus = _get_bus()

# ---------------------------
# VSD Manager (guarded) + local adapter
# ---------------------------
try:
    from VHW.vsd_manager import VSDManager  # type: ignore
except Exception:
    VSDManager = None  # type: ignore

class _DictVSD:
    """Sandbox-safe in-memory VSD fallback (ALWAYS-PERSIST semantics)."""
    def __init__(self) -> None:
        self._kv: Dict[str, Any] = {}
        self._lock = threading.RLock()
    def get(self, key: str, default: Any = None) -> Any:
        with self._lock:
            return self._kv.get(key, default)
    def set(self, key: str, value: Any) -> None:
        with self._lock:
            self._kv[key] = value
    def delete(self, key: str) -> None:
        with self._lock:
            if key in self._kv:
                del self._kv[key]

def _get_vsd():
    try:
        if VSDManager is not None:
            return VSDManager()
    except Exception as exc:
        log.warning("VSDManager unavailable, using _DictVSD: %s", exc)
    return _DictVSD()

vsd_manager = _get_vsd()

# ---------------------------
# Core helpers
# ---------------------------
def _now_s() -> float:
    return time.time()

def sha256_text(x: Any) -> str:
    try:
        return hashlib.sha256(str(x).encode("utf-8")).hexdigest()
    except Exception:
        try:
            return hashlib.sha256(repr(x).encode("utf-8")).hexdigest()
        except Exception:
            return "0" * 64

def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))

# ---------------------------
# Hashrate Conversion Helpers
# ---------------------------
_HASH_MULTIPLIERS = {
    "H/S": 1.0, "KH/S": 1e3, "MH/S": 1e6, "GH/S": 1e9,
    "TH/S": 1e12, "PH/S": 1e15, "EH/S": 1e18, "ZH/S": 1e21, "YH/S": 1e24
}

def unit_to_multiplier(unit: str) -> float:
    return _HASH_MULTIPLIERS.get(str(unit).upper().replace("/S", "H/S"), 1.0)

def to_hs(value: Union[str, float, int]) -> float:
    h = recognize_hashrate(value)
    return float(h["value"]) * float(h["scale"])

def hs_to_best_unit(hs_value: float) -> Tuple[str, float]:
    for u, m in reversed(list(_HASH_MULTIPLIERS.items())):
        if hs_value >= m:
            return (u, hs_value / m)
    return ("H/S", hs_value)

# ---------------------------
# Live-mode enforcement
# ---------------------------
def confirm_live_mode() -> None:
    try:
        mode = vsd_manager.get("system/mode", "live")
        if mode != "live":
            vsd_manager.set("system/mode", "live")
            vsd_manager.set("system/simulate", False)
            vsd_manager.set("system/sandbox", False)
    except Exception as exc:
        log.error("confirm_live_mode failed (ignored): %s", exc)

# ---------------------------
# VSD I/O (always persistent)
# ---------------------------
def get(key: str, default: Any = None) -> Any:
    confirm_live_mode()
    try:
        return vsd_manager.get(str(key), default)
    except Exception as exc:
        log.error("get failed key=%s: %s", key, exc)
        return default

def store(key: str, value: Any, persist: bool = True) -> None:
    confirm_live_mode()
    try:
        vsd_manager.set(str(key), value)
        try:
            _bus.publish("vsd.write", {"key": str(key), "ts": _now_s()})
        except Exception:
            pass
    except Exception as exc:
        log.error("store failed key=%s: %s", key, exc)

def delete(key: str) -> None:
    confirm_live_mode()
    try:
        vsd_manager.delete(str(key))
    except Exception as exc:
        log.error("delete failed key=%s: %s", key, exc)

def exists(key: str) -> bool:
    return get(key, None) is not None

# ---------------------------
# Atomic VSD locks
# ---------------------------
_lock_local = threading.RLock()

def _lock_key(name: str) -> str:
    return "locks/" + str(name)

def vsd_lock_acquire(name: str, ttl_s: float = 5.0, owner: Optional[str] = None) -> bool:
    confirm_live_mode()
    owner_val = owner or ("pid-" + str(os.getpid()) + "-" + str(random.randint(100000, 999999)))
    key = _lock_key(name)
    with _lock_local:
        rec = get(key, None)
        t = _now_s()
        if isinstance(rec, dict):
            exp = float(rec.get("expires_s", 0.0))
            if exp > t and str(rec.get("owner", "")) != owner_val:
                return False
        store(key, {"owner": owner_val, "expires_s": t + max(0.1, float(ttl_s))})
        return True

def vsd_lock_release(name: str, owner: Optional[str] = None) -> bool:
    confirm_live_mode()
    key = _lock_key(name)
    with _lock_local:
        rec = get(key, None)
        if not isinstance(rec, dict):
            return True
        if owner is None or str(rec.get("owner", "")) == str(owner) or float(rec.get("expires_s", 0.0)) <= _now_s():
            delete(key)
            return True
        return False

# ---------------------------
# Hardware metrics
# ---------------------------
def _try_psutil() -> Dict[str, float]:
    try:
        import psutil
        cpu = float(psutil.cpu_percent(interval=None)) / 100.0
        vm = psutil.virtual_memory()
        ram_used = float(vm.used) / (1024.0 ** 3)
        ram_total = float(vm.total) / (1024.0 ** 3)
        return {"cpu_util": cpu, "ram_used_gb": ram_used, "ram_total_gb": ram_total}
    except Exception:
        return {}

def _try_gputil() -> Dict[str, float]:
    try:
        import GPUtil
        gpus = GPUtil.getGPUs()
        if not gpus: return {}
        g = gpus[0]
        return {
            "gpu_util": float(g.load),
            "vram_used_gb": float(g.memoryUsed) / 1024.0,
            "vram_total_gb": float(g.memoryTotal) / 1024.0,
        }
    except Exception:
        return {}

def hw_snapshot() -> Dict[str, float]:
    confirm_live_mode()
    out: Dict[str, float] = {}
    out.update(_try_psutil())
    out.update(_try_gputil())
    mirrors = get("hw/mirrors", {})
    if isinstance(mirrors, dict):
        for k, v in mirrors.items():
            if k not in out and isinstance(v, (int, float)):
                out[k] = float(v)
    out["vram_total_gb"] = float(out.get("vram_total_gb", get("hw/vram_total_gb", 0.0)))
    out["mem_bandwidth_gbps"] = float(get("hw/mem_bandwidth_gbps", 0.0))
    out["compute_throughput_tops"] = float(get("hw/compute_throughput_tops", 0.0))
    store("hw/snapshot/last", out)
    return out

# ---------------------------
# Telemetry
# ---------------------------
def append_telemetry(path: str, payload: Dict[str, Any]) -> None:
    confirm_live_mode()
    key = "telemetry/" + str(path)
    arr = get(key, [])
    if not isinstance(arr, list):
        arr = []
    record = {"t_s": _now_s()}
    if isinstance(payload, dict):
        record.update(payload)
    else:
        record["value"] = str(payload)
    arr.append(record)
    store(key, arr)
    try:
        _bus.publish("telemetry.append", {"path": str(path), "ts": record["t_s"]})
    except Exception:
        pass

# ---------------------------
# Tokenized vector codec
# ---------------------------
def _codec_params() -> Dict[str, Any]:
    cfg = get("codec/config", None)
    if isinstance(cfg, dict): return cfg
    cfg = {"scale": 47.0, "offset": 32.0, "float_range": [-1.0, 1.0], "dict_seed": "0xJARVIS-SIM-V200K", "token_table": {}}
    store("codec/config", cfg)
    return cfg

def _float_to_char(v: float, scale: float, offset: float) -> str:
    x = int((float(v) + 1.0) * scale) + int(offset)
    x = max(32, min(126, x))
    return chr(x)

def _char_to_float(c: str, scale: float, offset: float) -> float:
    x = ord(c)
    x = max(32, min(126, x))
    return (float(x - int(offset)) / scale) - 1.0

def _pack_complex(z: complex, scale: float, offset: float) -> str:
    return _float_to_char(z.real, scale, offset) + _float_to_char(z.imag, scale, offset)

def _unpack_complex(chars: str, i: int, scale: float, offset: float) -> Tuple[complex, int]:
    a = _char_to_float(chars[i], scale, offset)
    b = _char_to_float(chars[i + 1], scale, offset)
    return complex(a, b), i + 2

def _tokenize(text: str) -> Dict[str, Any]:
    if not text: return {"text": "", "dict": {}}
    step = 4
    counts: Dict[str, int] = {}
    for i in range(0, len(text) - step + 1, step):
        s = text[i:i + step]
        counts[s] = counts.get(s, 0) + 1
    keep = {k: i for i, (k, _) in enumerate(sorted(counts.items(), key=lambda kv: -kv[1])[:64])}
    out = []
    i = 0
    while i < len(text):
        chunk = text[i:i + step]
        if chunk in keep:
            out.append("@{0:02d}".format(keep[chunk]))
            i += step
        else:
            out.append(text[i])
            i += 1
    return {"text": "".join(out), "dict": {"{0:02d}".format(i): k for k, i in keep.items()}}

def _detokenize(text: str, dct: Dict[str, str]) -> str:
    if not text: return ""
    out = []
    i = 0
    while i < len(text):
        if i + 3 <= len(text) and text[i] == "@" and text[i+1:i+3].isdigit():
            key = text[i+1:i+3]
            rep = dct.get(key, "")
            if rep: out.append(rep)
            i += 3
        else:
            out.append(text[i])
            i += 1
    return "".join(out)

def serialize_vector_with_tokens(vec: List[complex]) -> Dict[str, Any]:
    cfg = _codec_params()
    scale = float(cfg["scale"])
    offset = float(cfg["offset"])
    raw_text = "".join([_pack_complex(z, scale, offset) for z in vec])
    tok = _tokenize(raw_text)
    return {"text": tok["text"], "dict": tok["dict"], "hash": sha256_text(tok["text"])}

def deserialize_vector_with_tokens(blob: Any) -> List[complex]:
    cfg = _codec_params()
    scale = float(cfg["scale"])
    offset = float(cfg["offset"])
    text = ""
    dct: Dict[str, str] = {}
    if isinstance(blob, dict):
        text = str(blob.get("text", ""))
        dct = {k: str(v) for k, v in blob.get("dict", {}).items()}
        text = _detokenize(text, dct)
    else:
        text = str(blob)
    out: List[complex] = []
    i = 0
    while i + 1 < len(text):
        z, i = _unpack_complex(text, i, scale, offset)
        out.append(z)
    return out

# ---------------------------
# HTTP / JSON utilities
# ---------------------------
def fetch_json(url: str, timeout: float = 10.0) -> dict:
    try:
        if requests is None: return {}
        r = requests.get(url, timeout=timeout)
        if getattr(r, "status_code", 0) != 200: return {}
        return r.json()  # type: ignore
    except Exception:
        return {}

# ---------------------------
# Hashrate Recognition
# ---------------------------
_HASH_UNITS = [
    ("YH/S", 1e24), ("ZH/S", 1e21), ("EH/S", 1e18),
    ("PH/S", 1e15), ("TH/S", 1e12), ("GH/S", 1e9),
    ("MH/S", 1e6), ("KH/S", 1e3), ("H/S", 1.0)
]
_HASHRATE_UNITS = {u.upper(): s for u, s in _HASH_UNITS}
_display_unit_selector: Dict[str, str] = {}
_ALLOW_UPCONVERT: bool = False

def recognize_hashrate(value: Union[str, float, int]) -> Dict[str, Any]:
    try:
        if isinstance(value, (int, float)):
            return {"value": float(value), "unit": "H/S", "scale": 1.0, "canonical": f"{value} H/S"}
        s = str(value).strip().upper().replace("X10^", "E").replace(" ", "")
        if re.fullmatch(r"[0-9.+\-E]+", s):
            return {"value": float(s), "unit": "H/S", "scale": 1.0, "canonical": f"{s} H/S"}
        num = re.findall(r"[0-9.+\-E]+", s)
        unit = re.sub(r"[0-9.+\-E]", "", s)
        num = num[0] if num else "0"
        if "H" not in unit: unit += "H/S"
        if not unit.endswith("/S"): unit = unit.replace("HS", "H/S")
        if unit not in _HASHRATE_UNITS: unit = "H/S"
        v = float(num)
        return {"value": v, "unit": unit, "scale": _HASHRATE_UNITS[unit], "canonical": f"{v} {unit}"}
    except Exception:
        return {"value": 0.0, "unit": "H/S", "scale": 1.0, "canonical": "0 H/S"}

def set_upconvert_mode(enable: bool) -> None:
    global _ALLOW_UPCONVERT
    _ALLOW_UPCONVERT = bool(enable)

def set_display_unit(network: str, unit: str) -> None:
    _display_unit_selector[str(network).upper()] = unit.upper()

def get_display_unit(network: str) -> str:
    return _display_unit_selector.get(str(network).upper(), "AUTO")

def _fmt_hs_value(val: float, unit: str) -> str:
    s = f"{val:.2f}"
    if s.endswith(".00"): s = s[:-3]
    return s + " " + unit


def auto_format_hs(value: float, minimum_unit: str = "H/S", upconvert: bool = True) -> str:
    """
    Format a hashrate value into a human-readable string (H/S, KH/S, MH/S, ...).

    Parameters
    ----------
    value : float
        Raw hashrate in hashes per second.
    minimum_unit : str, optional
        Smallest unit to allow in the output. One of:
        "H/S", "KH/S", "MH/S", "GH/S", "TH/S", "PH/S", "EH/S", "ZH/S", "YH/S".
    upconvert : bool, optional
        If True, very small values will be shown in the next larger unit when
        it yields a clearer value.

    Returns
    -------
    str
        Formatted hashrate like "125.4 MH/S".
    """
    v = float(value)
    if v <= 0.0:
        return "0 H/S"

    # Filter units based on minimum_unit
    units = list(_HASH_UNITS)
    start_index = 0
    for i, (unit_name, _scale) in enumerate(units):
        if unit_name == minimum_unit:
            start_index = i
            break
    units = units[start_index:]

    # Choose the largest unit where scaled value is >= 1
    chosen_unit = None
    chosen_value = None
    for unit_name, scale in units:
        scaled = v / scale
        if scaled >= 1.0:
            chosen_unit = unit_name
            chosen_value = scaled
            break

    # No unit produced value >= 1.0
    if chosen_unit is None:
        # If upconvert is enabled and there exists a larger unit before minimum_unit,
        # show in that larger unit to avoid tiny decimals.
        if upconvert and start_index > 0:
            larger_unit_name, larger_scale = _HASH_UNITS[start_index - 1]
            chosen_unit = larger_unit_name
            chosen_value = v / larger_scale
        else:
            # Fall back to minimum unit
            unit_name, scale = units[-1]
            chosen_unit = unit_name
            chosen_value = v / scale

    return _fmt_hs_value(chosen_value, chosen_unit)
