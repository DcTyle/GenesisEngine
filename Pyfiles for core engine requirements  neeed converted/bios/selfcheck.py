# ============================================================================
# Quantum Application / bios
# ASCII-ONLY SOURCE FILE
# File: selfcheck.py
# Version: v7.1 "Unified SystemUtils Schema + Silent Error Purge"
# ============================================================================
"""
Purpose
-------
Performs BIOS-level self-diagnostics using a unified hardware schema.

This module:
- Uses VHW.system_utils.device_snapshot() and system_headroom()
- Verifies schema conformity
- Checks:
    * core.utils.hw_snapshot()
    * VSD key "system/bios_boot_ok"
    * EventBus publish sanity
- Appends results to telemetry under "bios_selfcheck".
"""

from __future__ import annotations
from typing import Dict, Any
import time
import logging
import os
import re
from pathlib import Path

# ----------------------------------------------------------------------------
# Local logger
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.selfcheck")

# ----------------------------------------------------------------------------
# core.utils (safe fallbacks)
# ----------------------------------------------------------------------------
try:
    # from core.utils import hw_snapshot, append_telemetry
    pass
except Exception as exc:
    _logger.warning(
        "selfcheck: core.utils import failed, using fallback: %s",
        exc,
        exc_info=True,
    )

    def hw_snapshot() -> Dict[str, Any]:
        return {}

    def append_telemetry(topic: str, data: Dict[str, Any]) -> None:
        return

# ----------------------------------------------------------------------------
# BIOS EventBus
# ----------------------------------------------------------------------------
try:
    from bios.event_bus import get_event_bus
except Exception as exc:
    _logger.warning(
        "selfcheck: bios.event_bus import failed, using fallback: %s",
        exc,
        exc_info=True,
    )

    class _NoBus:
        def publish(self, topic: str, data: Dict[str, Any] | None = None) -> None:
            return

    def get_event_bus():
        return _NoBus()

# ----------------------------------------------------------------------------
# VSD Manager
# ----------------------------------------------------------------------------
try:
    from VHW.vsd_manager import VSDManager
except Exception as exc:
    _logger.warning(
        "selfcheck: VHW.vsd_manager import failed, using fallback: %s",
        exc,
        exc_info=True,
    )

    class VSDManager:
        def __init__(self) -> None:
            self._kv: Dict[str, Any] = {}

        def get(self, key: str, default: Any = None) -> Any:
            try:
                return self._kv.get(str(key), default)
            except Exception as exc2:
                _logger.error(
                    "VSDManager.get fallback failed for key '%s': %s",
                    key,
                    exc2,
                    exc_info=True,
                )
                return default

        def store(self, key: str, value: Any) -> None:
            try:
                self._kv[str(key)] = value
            except Exception as exc2:
                _logger.error(
                    "VSDManager.store fallback failed for key '%s': %s",
                    key,
                    exc2,
                    exc_info=True,
                )

# ----------------------------------------------------------------------------
# System hardware utilities
# ----------------------------------------------------------------------------
try:
    from VHW.system_utils import device_snapshot, system_headroom
except Exception as exc:
    _logger.warning(
        "selfcheck: VHW.system_utils import failed, using fallback: %s",
        exc,
        exc_info=True,
    )

    def device_snapshot() -> Dict[str, Any]:
        return {}

    def system_headroom() -> Dict[str, Any]:
        return {}

# ----------------------------------------------------------------------------
# Schema helpers
# ----------------------------------------------------------------------------
def _bool_isinstance(obj: Any, type_: Any) -> bool:
    try:
        return isinstance(obj, type_)
    except Exception as exc:
        _logger.warning(
            "_bool_isinstance failed for object '%s': %s",
            obj,
            exc,
            exc_info=True,
        )
        return False


def _validate_device_snapshot(snap: Dict[str, Any]) -> bool:
    """Ensure device_snapshot conforms to the canonical schema."""
    if not _bool_isinstance(snap, dict):
        return False

    try:
        cpu = snap["cpu"]
        gpu = snap["gpu"]
        mem = snap["memory"]
        bw = snap["bandwidth"]

        checks = [
            _bool_isinstance(snap.get("timestamp"), (int, float)),
            _bool_isinstance(cpu.get("count"), int),
            _bool_isinstance(cpu.get("threads"), int),
            _bool_isinstance(cpu.get("util"), (int, float)),
            _bool_isinstance(gpu.get("model"), str),
            _bool_isinstance(gpu.get("memory_total_mb"), (int, float)),
            _bool_isinstance(gpu.get("memory_used_mb"), (int, float)),
            _bool_isinstance(gpu.get("util"), (int, float)),
            _bool_isinstance(gpu.get("temperature_c"), (int, float)),
            _bool_isinstance(mem.get("total_gb"), (int, float)),
            _bool_isinstance(mem.get("used_gb"), (int, float)),
            _bool_isinstance(mem.get("util"), (int, float)),
            _bool_isinstance(bw.get("pcie_gbps"), (int, float)),
            _bool_isinstance(bw.get("memory_bw_gbps"), (int, float)),
        ]
        return all(checks)
    except Exception as exc:
        _logger.error(
            "_validate_device_snapshot failed: %s",
            exc,
            exc_info=True,
        )
        return False


def _validate_system_headroom(hdr: Dict[str, Any]) -> bool:
    """Ensure system_headroom conforms to the canonical schema."""
    if not _bool_isinstance(hdr, dict):
        return False
    try:
        checks = [
            _bool_isinstance(hdr.get("timestamp"), (int, float)),
            _bool_isinstance(hdr.get("global_util"), (int, float)),
            _bool_isinstance(hdr.get("cpu_util"), (int, float)),
            _bool_isinstance(hdr.get("gpu_util"), (int, float)),
            _bool_isinstance(hdr.get("mem_util"), (int, float)),
            _bool_isinstance(hdr.get("headroom"), (int, float)),
        ]
        return all(checks)
    except Exception as exc:
        _logger.error(
            "_validate_system_headroom failed: %s",
            exc,
            exc_info=True,
        )
        return False

# ----------------------------------------------------------------------------
# BIOS Selfcheck
# ----------------------------------------------------------------------------
def run_selfcheck() -> Dict[str, Any]:
    """Minimal BIOS-level self-check using unified system_utils schema."""
    results: Dict[str, Any] = {}

    bus = get_event_bus()
    vsd = VSDManager()

    # 1) Hardware snapshot ---------------------------------------------------
    try:
        results["hardware"] = hw_snapshot()
        results["hardware_ok"] = True
    except Exception as exc:
        _logger.error("run_selfcheck: hw_snapshot failed: %s", exc, exc_info=True)
        results["hardware_ok"] = False
        results["hardware_err"] = str(exc)

    # 2) device_snapshot() ---------------------------------------------------
    try:
        dev = device_snapshot()
        results["device_snapshot"] = dev
        results["device_snapshot_schema_ok"] = _validate_device_snapshot(dev)
    except Exception as exc:
        _logger.error(
            "run_selfcheck: device_snapshot failed: %s",
            exc,
            exc_info=True,
        )
        results["device_snapshot"] = {}
        results["device_snapshot_schema_ok"] = False
        results["device_snapshot_err"] = str(exc)

    # 3) system_headroom() ---------------------------------------------------
    try:
        hdr = system_headroom()
        results["system_headroom"] = hdr
        results["system_headroom_schema_ok"] = _validate_system_headroom(hdr)
    except Exception as exc:
        _logger.error(
            "run_selfcheck: system_headroom failed: %s",
            exc,
            exc_info=True,
        )
        results["system_headroom"] = {}
        results["system_headroom_schema_ok"] = False
        results["system_headroom_err"] = str(exc)

    # 4) BIOS boot flag ------------------------------------------------------
    try:
        results["bios_boot_ok"] = bool(vsd.get("system/bios_boot_ok", False))
    except Exception as exc:
        _logger.error(
            "run_selfcheck: vsd.get(bios_boot_ok) failed: %s",
            exc,
            exc_info=True,
        )
        results["bios_boot_ok"] = False
        results["bios_boot_err"] = str(exc)

    # 5) EventBus ping -------------------------------------------------------
    try:
        bus.publish(
            "bios.selfcheck.ping",
            {
                "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "source": "bios.selfcheck",
            },
        )
        results["event_bus_ok"] = True
    except Exception as exc:
        _logger.error(
            "run_selfcheck: event bus publish failed: %s",
            exc,
            exc_info=True,
        )
        results["event_bus_ok"] = False
        results["event_bus_err"] = str(exc)

    # Timestamp ---------------------------------------------------------------
    results["timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    # Telemetry ---------------------------------------------------------------
    try:
        append_telemetry("bios_selfcheck", results)
    except Exception as exc:
        _logger.error(
            "run_selfcheck: append_telemetry failed: %s",
            exc,
            exc_info=True,
        )

    return results

# ----------------------------------------------------------------------------
# Forbidden Dependency Scan (BIOS-only)
# ----------------------------------------------------------------------------
def scan_forbidden_imports(bios_root: str | Path) -> Dict[str, Any]:
    """Scan BIOS Python files for forbidden imports of miner/prediction_engine.

    Returns a dict with keys: violations (list), count (int), scanned (int).
    """
    try:
        root = Path(bios_root)
    except Exception:
        root = Path('.')
    violations: list[dict] = []
    scanned = 0
    patts = [
        re.compile(r"^\s*from\s+miner\b"),
        re.compile(r"^\s*import\s+miner\b"),
        re.compile(r"^\s*from\s+prediction_engine\b"),
        re.compile(r"^\s*import\s+prediction_engine\b"),
    ]
    try:
        for p in root.rglob('*.py'):
            scanned += 1
            try:
                text = p.read_text(encoding='utf-8', errors='ignore')
            except Exception:
                text = ''
            for i, line in enumerate(text.splitlines(), start=1):
                for rx in patts:
                    try:
                        if rx.search(line):
                            violations.append({
                                'file': p.relative_to(root).as_posix(),
                                'line': i,
                                'snippet': line.strip(),
                            })
                            break
                    except Exception:
                        continue
    except Exception as exc:
        _logger.warning("scan_forbidden_imports failed: %s", exc, exc_info=True)
    return {'violations': violations, 'count': len(violations), 'scanned': scanned}

# ----------------------------------------------------------------------------
# Stand-alone execution
# ----------------------------------------------------------------------------
if __name__ == "__main__":
    out = run_selfcheck()
    print("BIOS Selfcheck Results:")
    for k, v in out.items():
        print(f"{k}: {v}")

# ============================================================================
# End bios/selfcheck.py
# ============================================================================
