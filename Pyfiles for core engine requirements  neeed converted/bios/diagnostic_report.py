# ============================================================================
# Quantum Application / BIOS
# ASCII-ONLY SOURCE FILE
# File: diagnostic_report.py
# Version: v4.8.9.2 (Health Snapshot + BIOS Compat)
# ============================================================================
"""
Purpose
-------
Provide a structured health snapshot for the Quantum Application.

This module is used by BIOS init logic to generate a diagnostic report
at startup or on demand. It inspects:

- BIOS boot flags persisted in VSD
- Core config status
- Miner runtime hooks (if available)
- Prediction engine hooks (if available)
- Neuralis AI status (if available)
- High level warnings for missing subsystems

Compat
------
Older BIOS code expects a callable:

    generate_diagnostic_report(event_bus, vsd_manager)

This implementation provides that exact function name, with optional
parameters, and returns a plain dict that can be logged or persisted.
"""

from __future__ import annotations

import logging
import time
from typing import Any, Dict, Optional

# ----------------------------------------------------------------------------
# Structured logging
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.diagnostic_report")
if not _logger.handlers:
    _h = logging.StreamHandler()
    _fmt = logging.Formatter(
        fmt="[%(asctime)s] BIOS.Diagnostic %(levelname)s: %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ",
    )
    _h.setFormatter(_fmt)
    _logger.addHandler(_h)
_logger.setLevel(logging.INFO)

# ----------------------------------------------------------------------------
# Safe imports for optional subsystems
# ----------------------------------------------------------------------------
def _safe_import(path: str) -> Optional[Any]:
    """
    Import a dotted path if possible, return None on failure.
    No exceptions leak out of this helper.
    """
    try:
        module = __import__(path, fromlist=["*"])
        return module
    except Exception as exc:  # SILENT-ERROR PATCH
        _logger.warning(
            "diagnostic_report: failed to import %s: %s",
            path,
            exc,
            exc_info=True,
        )
        return None

def _get_event_bus():
    bus_mod = _safe_import("bios.event_bus")
    if bus_mod and hasattr(bus_mod, "get_event_bus"):
        try:
            return bus_mod.get_event_bus()
        except Exception as exc:  # SILENT-ERROR PATCH
            _logger.warning(
                "diagnostic_report: get_event_bus() failed, using _NoBus: %s",
                exc,
                exc_info=True,
            )

    class _NoBus:
        def publish(self, topic: str, data: Dict[str, Any]) -> None:
            return
    return _NoBus()

def _get_vsd_manager():
    vhw_mod = _safe_import("VHW.vsd_manager")
    if vhw_mod and hasattr(vhw_mod, "VSDManager"):
        try:
            return vhw_mod.VSDManager()
        except Exception as exc:  # SILENT-ERROR PATCH
            _logger.warning(
                "diagnostic_report: VSDManager() init failed, using _DummyVSD: %s",
                exc,
                exc_info=True,
            )

    class _DummyVSD:
        def get(self, key: str, default: Any = None) -> Any:
            return default

        def store(self, key: str, value: Any) -> None:
            return

        def exists(self, key: str) -> bool:
            return False

    return _DummyVSD()

def _get_config():
    # Core/VHW-only rule: avoid config.manager dependency; return null config.
    class _NullCfg:
        def get(self, key: str, default: Any = None) -> Any:
            return default

    return _NullCfg()

# ----------------------------------------------------------------------------
# Snapshot structure
# ----------------------------------------------------------------------------
class DiagnosticSnapshot:
    """
    In-memory representation of a BIOS health snapshot.

    Use from_dict() and to_dict() to interoperate with JSON.
    """

    def __init__(self) -> None:
        self.timestamp_utc: str = ""
        self.bios_boot_ok: bool = False
        self.vsd_root_present: bool = False
        self.vsd_keys_total: int = 0

        self.config_profile: str = "unknown"
        self.config_network: str = "unknown"

        self.miner_enabled: bool = False
        self.miner_status: str = "unknown"

        self.prediction_engine_enabled: bool = False
        self.prediction_engine_status: str = "unknown"

        self.neuralis_enabled: bool = False
        self.neuralis_status: str = "unknown"

        self.warnings: Dict[str, str] = {}

    # ------------------------------------------------------------------------
    def to_dict(self) -> Dict[str, Any]:
        return {
            "timestamp_utc": self.timestamp_utc,
            "bios_boot_ok": self.bios_boot_ok,
            "vsd_root_present": self.vsd_root_present,
            "vsd_keys_total": self.vsd_keys_total,
            "config_profile": self.config_profile,
            "config_network": self.config_network,
            "miner_enabled": self.miner_enabled,
            "miner_status": self.miner_status,
            "prediction_engine_enabled": self.prediction_engine_enabled,
            "prediction_engine_status": self.prediction_engine_status,
            "neuralis_enabled": self.neuralis_enabled,
            "neuralis_status": self.neuralis_status,
            "warnings": dict(self.warnings),
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "DiagnosticSnapshot":
        inst = cls()
        inst.timestamp_utc = str(data.get("timestamp_utc", ""))
        inst.bios_boot_ok = bool(data.get("bios_boot_ok", False))
        inst.vsd_root_present = bool(data.get("vsd_root_present", False))
        inst.vsd_keys_total = int(data.get("vsd_keys_total", 0))
        inst.config_profile = str(data.get("config_profile", "unknown"))
        inst.config_network = str(data.get("config_network", "unknown"))
        inst.miner_enabled = bool(data.get("miner_enabled", False))
        inst.miner_status = str(data.get("miner_status", "unknown"))
        inst.prediction_engine_enabled = bool(data.get("prediction_engine_enabled", False))
        inst.prediction_engine_status = str(data.get("prediction_engine_status", "unknown"))
        inst.neuralis_enabled = bool(data.get("neuralis_enabled", False))
        inst.neuralis_status = str(data.get("neuralis_status", "unknown"))
        inst.warnings = dict(data.get("warnings", {}))
        return inst

# ----------------------------------------------------------------------------
# Core report builder
# ----------------------------------------------------------------------------
def _probe_vsd(vsd) -> Dict[str, Any]:
    info: Dict[str, Any] = {}
    try:
        # This will work for our VSDManager; fall back to simple checks otherwise.
        if hasattr(vsd, "list_keys"):
            keys = vsd.list_keys("")
            info["vsd_root_present"] = True
            info["vsd_keys_total"] = len(keys)
        else:
            # Try a cheap probe
            known_key = "system/bios_boot_ok"
            value = vsd.get(known_key, None)
            info["vsd_root_present"] = value is not None
            info["vsd_keys_total"] = 1 if value is not None else 0
    except Exception:
        _logger.exception("VSD probe failed")
        info["vsd_root_present"] = False
        info["vsd_keys_total"] = 0
    return info

def _probe_miner(vsd: Any) -> Dict[str, Any]:
    info = {
        "miner_enabled": False,
        "miner_status": "unavailable",
    }
    if vsd is None:
        return info
    # Read status from VSD-only keys to comply with core/VHW rule.
    status_keys = [
        "miner/runtime/status",
        "miner/engine/status",
        "miner/status",
    ]
    enabled_keys = [
        "miner/runtime/started",
        "miner/runtime/running",
        "miner/engine/running",
    ]
    try:
        for k in status_keys:
            val = vsd.get(k, None)
            if val is not None:
                info["miner_status"] = str(val)
                break
    except Exception:
        _logger.debug("diagnostic_report: miner status read failed", exc_info=True)
    try:
        for k in enabled_keys:
            val = vsd.get(k, None)
            if isinstance(val, bool):
                info["miner_enabled"] = info["miner_enabled"] or bool(val)
            elif val is not None:
                info["miner_enabled"] = True
        # If a status string exists, treat miner as enabled for reporting purposes
        if info["miner_status"] != "unavailable":
            info["miner_enabled"] = True
    except Exception:
        _logger.debug("diagnostic_report: miner enabled probe failed", exc_info=True)
    return info


def _probe_prediction_engine(vsd: Any) -> Dict[str, Any]:
    info = {
        "prediction_engine_enabled": False,
        "prediction_engine_status": "unavailable",
    }
    if vsd is None:
        return info
    status_keys = [
        "prediction/status",
        "prediction_engine/status",
    ]
    enabled_keys = [
        "prediction/running",
        "prediction_engine/running",
    ]
    try:
        for k in status_keys:
            val = vsd.get(k, None)
            if val is not None:
                info["prediction_engine_status"] = str(val)
                break
    except Exception:
        _logger.debug("diagnostic_report: prediction status read failed", exc_info=True)
    try:
        for k in enabled_keys:
            val = vsd.get(k, None)
            if isinstance(val, bool):
                info["prediction_engine_enabled"] = info["prediction_engine_enabled"] or bool(val)
            elif val is not None:
                info["prediction_engine_enabled"] = True
        if info["prediction_engine_status"] != "unavailable":
            info["prediction_engine_enabled"] = True
    except Exception:
        _logger.debug("diagnostic_report: prediction enabled probe failed", exc_info=True)
    return info


def _probe_neuralis(vsd: Any) -> Dict[str, Any]:
    info = {
        "neuralis_enabled": False,
        "neuralis_status": "unavailable",
    }
    if vsd is None:
        return info
    status_keys = [
        "neuralis/status",
        "neuralis/runtime/status",
    ]
    enabled_keys = [
        "neuralis/running",
        "neuralis/runtime/running",
    ]
    try:
        for k in status_keys:
            val = vsd.get(k, None)
            if val is not None:
                info["neuralis_status"] = str(val)
                break
    except Exception:
        _logger.debug("diagnostic_report: neuralis status read failed", exc_info=True)
    try:
        for k in enabled_keys:
            val = vsd.get(k, None)
            if isinstance(val, bool):
                info["neuralis_enabled"] = info["neuralis_enabled"] or bool(val)
            elif val is not None:
                info["neuralis_enabled"] = True
        if info["neuralis_status"] != "unavailable":
            info["neuralis_enabled"] = True
    except Exception:
        _logger.debug("diagnostic_report: neuralis enabled probe failed", exc_info=True)
    return info

def generate_report(event_bus: Any = None, vsd_manager: Any = None) -> Dict[str, Any]:
    """
    Build a complete diagnostic snapshot and return it as a dict.

    Parameters
    ----------
    event_bus : optional
        Event bus instance. If omitted, the default BIOS bus is used.
    vsd_manager : optional
        VSD manager instance. If omitted, the default VSDManager is used.

    Returns
    -------
    report : dict
        JSON serializable health snapshot payload.
    """
    bus = event_bus or _get_event_bus()
    vsd = vsd_manager or _get_vsd_manager()
    cfg = _get_config()

    snap = DiagnosticSnapshot()
    snap.timestamp_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    # BIOS boot flag from VSD
    try:
        bios_flag = vsd.get("system/bios_boot_ok", False)
        snap.bios_boot_ok = bool(bios_flag)
    except Exception:
        _logger.exception("Failed to read BIOS boot flag from VSD")
        snap.bios_boot_ok = False

    # VSD details
    vsd_info = _probe_vsd(vsd)
    snap.vsd_root_present = bool(vsd_info.get("vsd_root_present", False))
    snap.vsd_keys_total = int(vsd_info.get("vsd_keys_total", 0))

    # Config hints
    try:
        snap.config_profile = str(cfg.get("profile.name", "default"))
        snap.config_network = str(cfg.get("network.primary", "unset"))
    except Exception:
        _logger.exception("Config probe failed")
        snap.config_profile = "error"
        snap.config_network = "error"

    # Subsystem probes
    miner_info = _probe_miner(vsd)
    pe_info = _probe_prediction_engine(vsd)
    neu_info = _probe_neuralis(vsd)

    snap.miner_enabled = miner_info.get("miner_enabled", False)
    snap.miner_status = miner_info.get("miner_status", "unknown")
    snap.prediction_engine_enabled = pe_info.get("prediction_engine_enabled", False)
    snap.prediction_engine_status = pe_info.get("prediction_engine_status", "unknown")
    snap.neuralis_enabled = neu_info.get("neuralis_enabled", False)
    snap.neuralis_status = neu_info.get("neuralis_status", "unknown")

    # Derived warnings
    if not snap.vsd_root_present:
        snap.warnings["vsd"] = "VSD root not detected or unreadable"
    if not snap.bios_boot_ok:
        snap.warnings["bios_boot"] = "BIOS boot flag not set in VSD"
    if not snap.miner_enabled:
        snap.warnings["miner"] = "Miner engine disabled or not reachable"
    if not snap.prediction_engine_enabled:
        snap.warnings["prediction_engine"] = "Prediction engine disabled or not reachable"
    if not snap.neuralis_enabled:
        snap.warnings["neuralis"] = "Neuralis AI not reachable"

    payload = snap.to_dict()

    # Emit an event for telemetry consoles
    try:
        bus.publish("bios.diagnostic.report", payload)
    except Exception:
        _logger.warning("Failed to publish bios.diagnostic.report event")

    _logger.info("Diagnostic report built with %d warnings", len(snap.warnings))
    return payload

# ----------------------------------------------------------------------------
# Backwards-compatible entry point
# ----------------------------------------------------------------------------
def generate_diagnostic_report(event_bus: Any = None, vsd_manager: Any = None) -> Dict[str, Any]:
    """
    Legacy-friendly wrapper. Delegates to generate_report(...).

    Older BIOS init code calls:
        generate_diagnostic_report(bus, vsd)

    This keeps that call site working without modification.
    """
    return generate_report(event_bus=event_bus, vsd_manager=vsd_manager)


__all__ = [
    "DiagnosticSnapshot",
    "generate_report",
    "generate_diagnostic_report",
]
