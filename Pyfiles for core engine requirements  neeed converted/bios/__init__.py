# Path: Quantum Application/bios/__init__.py
# ============================================================================
# Quantum Application / bios
# ASCII-ONLY SOURCE FILE
# File: __init__.py
# Version: v5 "Unified Imports"
# Jarvis ADA v4.7 Hybrid Ready
# ============================================================================
"""
Purpose
-------
Package initializer for BIOS subsystem.
Initializes structured logging, safely imports core BIOS components,
announces BIOS initialization over EventBus, and persists readiness
status to the VSD manager.

Patch Integration (A-G)
-----------------------
A: Infrastructure / Import Fixes
   - Added _init_logger(), _safe_import(), and fallback bus.
B: Operational Runtime Integration
   - Announce BIOS init on EventBus.
C: Telemetry / Health
   - Structured ASCII-only UTC logger.
E: Failsafe / Resilience
   - All imports guarded; NoOp stubs for failed subsystems.
F: Persistence / VSD Refactor
   - Writes system/bios_boot_ok and version metadata to VSD.
G: Jarvis Boot Automation
   - Auto-announcement executed on import.
"""

from __future__ import annotations
import logging
import time
import sys
from typing import Any, Dict

# ----------------------------------------------------------------------------
# Structured logger (ASCII-safe UTC)  [Phases A, C]
# ----------------------------------------------------------------------------
def _init_logger() -> logging.Logger:
    """Initialize BIOS logger with ASCII-only UTC formatting."""
    log = logging.getLogger("bios.init")
    if not log.handlers:
        handler = logging.StreamHandler(stream=sys.stdout)
        fmt = logging.Formatter(
            fmt="[%(asctime)s] BIOS %(levelname)s: %(message)s",
            datefmt="%Y-%m-%dT%H:%M:%SZ",
        )
        handler.setFormatter(fmt)
        log.addHandler(handler)
    log.setLevel(logging.INFO)
    # Prevent duplicate emission if root logger has handlers
    log.propagate = False
    return log

_logger = _init_logger()

# ----------------------------------------------------------------------------
# EventBus (relocated under bios.event_bus)  [Phase A, E]
# ----------------------------------------------------------------------------
try:
    from bios.event_bus import get_event_bus
except Exception as exc:
    _logger.warning("bios.event_bus unavailable (%s); using NoOp bus", exc)

    class _NoBus:
        def publish(self, topic: str, data: Dict[str, Any] | None = None) -> None:
            return
        def subscribe(self, *a, **kw) -> None:
            return

    def get_event_bus() -> _NoBus:  # type: ignore
        return _NoBus()

_bus = get_event_bus()

# ----------------------------------------------------------------------------
# VSD Manager import with fallback  [Phase F]
# ----------------------------------------------------------------------------
try:
    from VHW.vsd_manager import VSDManager
    _vsd = VSDManager()
except Exception as exc:
    _logger.warning("VSDManager unavailable (%s); using in-memory fallback", exc)

    class _DictVSD:
        def __init__(self) -> None:
            self._kv: Dict[str, Any] = {}
        def set(self, key: str, value: Any) -> None:
            self._kv[key] = value
        def get(self, key: str, default: Any = None) -> Any:
            return self._kv.get(key, default)

    _vsd = _DictVSD()

# ----------------------------------------------------------------------------
# BIOS metadata
# ----------------------------------------------------------------------------
version: str = "v4.8.9.2"
bios_boot_ok: bool = False

# ----------------------------------------------------------------------------
# Safe import helper  [Phase A]
# ----------------------------------------------------------------------------
def _safe_import(name: str) -> Any:
    """Import a module safely, returning None on failure."""
    try:
        mod = __import__(name, fromlist=["*"])
        _logger.info("Imported module: %s", name)
        return mod
    except Exception as exc:
        _logger.warning("Safe import failed: %s (%s)", name, exc)
        return None

# ----------------------------------------------------------------------------
# BIOS announcement  [Phases B, F, G]
# ----------------------------------------------------------------------------
def announce_bios_init() -> None:
    """Announce BIOS initialization and persist boot status."""
    global bios_boot_ok
    bios_boot_ok = True
    ts = time.time()

    try:
        _vsd.set("system/bios_boot_ok", True)
        _vsd.set("system/bios_version", version)
        _vsd.set("system/bios_timestamp", ts)
        _logger.info("BIOS boot flag persisted to VSD")
    except Exception as exc:
        _logger.error("VSD persistence failed: %s", exc)

    try:
        _bus.publish("bios.init", {"version": version, "timestamp": ts})
        _logger.info("BIOS init event published (v%s)", version)
    except Exception as exc:
        _logger.warning("EventBus publish failed: %s", exc)

# ----------------------------------------------------------------------------
# Auto-import BIOS subsystems  [Phases A, E]
# ----------------------------------------------------------------------------
def _load_subsystems() -> None:
    """Attempt safe imports for BIOS subsystems: boot, scheduler, diagnostics."""
    global BIOSBoot, bios_start, scheduler_run, generate_diagnostic_report, VSDManager

    # boot
    try:
        from bios.boot import BIOSBoot, start as bios_start  # type: ignore
    except Exception as exc:
        _logger.warning("boot import failed: %s", exc)
        BIOSBoot = None  # type: ignore
        def bios_start() -> None:  # type: ignore
            _logger.error("bios_start unavailable")

    # scheduler
    try:
        from bios.scheduler import run_forever as scheduler_run  # type: ignore
    except Exception as exc:
        _logger.warning("scheduler import failed: %s", exc)
        def scheduler_run() -> None:  # type: ignore
            _logger.error("scheduler_run unavailable")

    # diagnostic
    try:
        from bios.diagnostic_report import generate_diagnostic_report  # type: ignore
    except Exception as exc:
        _logger.warning("diagnostic_report import failed: %s", exc)
        def generate_diagnostic_report() -> Dict[str, Any]:  # type: ignore
            _logger.error("diagnostic_report unavailable")
            return {}

_load_subsystems()

# ----------------------------------------------------------------------------
# Automatic announce on import  [Phase G]
# ----------------------------------------------------------------------------
try:
    announce_bios_init()
except Exception as exc:
    _logger.error("Automatic BIOS announce failed: %s", exc)

# ----------------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------------
__all__ = [
    "_init_logger",
    "_safe_import",
    "announce_bios_init",
    "BIOSBoot",
    "bios_start",
    "scheduler_run",
    "generate_diagnostic_report",
    "VSDManager",
    "version",
    "bios_boot_ok",
]