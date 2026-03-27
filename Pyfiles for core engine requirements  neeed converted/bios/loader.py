# Path: Quantum Application/bios/loader.py
# ============================================================================
# Quantum Application / BIOS
# ASCII-ONLY SOURCE FILE
# File: loader.py
# Version: v6.1 "EventBus-Only Loader + Silent Error Visibility"
# ============================================================================
"""
Purpose
-------
Handles BIOS runtime loading and shutdown operations for VirtualMiner.
Ensures VSD persistence and emits EventBus notifications for loader lifecycle.

Design Rules
------------
- Does NOT import or call other BIOS modules directly.
- Does NOT call bios.emergency.trigger_emergency_stop.
- Uses EventBus to signal failures so listeners (emergency handlers) can react.
"""

from __future__ import annotations
import threading
import time
import logging
from typing import Dict, Any

# ----------------------------------------------------------------------------
# Structured Logging
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.loader")
if not _logger.handlers:
    _h = logging.StreamHandler()
    _fmt = logging.Formatter(
        fmt="[%(asctime)s] BIOS_LOADER %(levelname)s: %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ",
    )
    _h.setFormatter(_fmt)
    _logger.addHandler(_h)
_logger.setLevel(logging.INFO)

# Force UTC timestamps
logging.Formatter.converter = time.gmtime  # type: ignore[attr-defined]

# ----------------------------------------------------------------------------
# EventBus
# ----------------------------------------------------------------------------
try:
    from bios.event_bus import get_event_bus
except Exception as exc:
    _logger.warning(
        "bios.event_bus unavailable; using NoOp bus: %s",
        exc,
        exc_info=True,
    )

    class _NoBus:
        def publish(self, topic: str, data=None) -> None:
            return

        def subscribe(
            self,
            topic: str,
            handler=None,
            once: bool = False,
            priority: int = 0,
            name: str | None = None,
        ) -> None:
            return

    def get_event_bus():  # type: ignore[no-redef]
        return _NoBus()

_bus = get_event_bus()

# ----------------------------------------------------------------------------
# VSD Manager
# ----------------------------------------------------------------------------
try:
    from VHW.vsd_manager import VSDManager
except Exception as exc:
    _logger.warning(
        "VSDManager import failed; using in-memory fallback VSDManager: %s",
        exc,
        exc_info=True,
    )

    class VSDManager:  # type: ignore[no-redef]
        def __init__(self) -> None:
            self._kv: Dict[str, Any] = {}

        def store(self, key: str, value: Any) -> None:
            self._kv[str(key)] = value

        def get(self, key: str, default: Any = None) -> Any:
            return self._kv.get(str(key), default)

# ----------------------------------------------------------------------------
# Globals
# ----------------------------------------------------------------------------
_loader_lock = threading.RLock()
_vsd = VSDManager()
_loaded: bool = False

# ----------------------------------------------------------------------------
# Helper: BIOS Readiness Flag
# ----------------------------------------------------------------------------
def _bios_ready() -> bool:
    """Return True if BIOS boot has completed."""
    try:
        return bool(_vsd.get("system/bios_boot_ok", False))
    except Exception as exc:
        _logger.warning(
            "_bios_ready: failed to read 'system/bios_boot_ok' from VSD: %s",
            exc,
            exc_info=True,
        )
        return False

# ----------------------------------------------------------------------------
# BIOSLoader
# ----------------------------------------------------------------------------
class BIOSLoader:
    """
    Handles runtime load and shutdown for BIOS.
    Emits EventBus notifications and persists state to VSD.

    Notes
    -----
    - On failures, publishes emergency events instead of calling other BIOS modules.
    """

    def __init__(self) -> None:
        self._lock = _loader_lock
        self._vsd = _vsd
        self._loaded = False

    # ------------------------------------------------------------------------
    # Load System State
    # ------------------------------------------------------------------------
    def load(self) -> bool:
        """Load system state from VSD and initialize components."""
        global _loaded
        with self._lock:
            if not _bios_ready():
                _logger.warning("Loader: BIOS not ready; load aborted.")
                return False
            try:
                state = self._vsd.get("loader/state", {})
                if not state:
                    state = {"boot_ts": time.time()}
                    _logger.info("Loader: initializing new state")
                else:
                    state["reload_ts"] = time.time()
                    _logger.info("Loader: reloading existing state")
                self._vsd.store("loader/state", state)
                _loaded = True
                self._publish_event("loader.load_complete", {"ts": time.time()})
                return True
            except Exception as exc:
                _logger.exception("Loader: load failed: %s", exc)
                self._publish_event(
                    "emergency.trigger",
                    {
                        "source": "bios.loader",
                        "reason": "loader_load_failed",
                        "ts": time.time(),
                    },
                )
                return False

    # ------------------------------------------------------------------------
    # Shutdown System
    # ------------------------------------------------------------------------
    def shutdown(self) -> bool:
        """Persist shutdown metadata to VSD and signal shutdown over EventBus."""
        global _loaded
        with self._lock:
            try:
                meta = {"shutdown_ts": time.time(), "reason": "normal"}
                self._vsd.store("loader/shutdown", meta)
                self._vsd.store("loader/state", {})  # clear state
                _loaded = False
                self._publish_event("loader.shutdown_complete", {"ts": time.time()})
                _logger.info("Loader: shutdown complete")
                return True
            except Exception as exc:
                _logger.exception("Loader: shutdown failed: %s", exc)
                self._publish_event(
                    "emergency.trigger",
                    {
                        "source": "bios.loader",
                        "reason": "loader_shutdown_failed",
                        "ts": time.time(),
                    },
                )
                return False

    # ------------------------------------------------------------------------
    # Status Query
    # ------------------------------------------------------------------------
    def status(self) -> Dict[str, Any]:
        return {"loaded": _loaded}

    # ------------------------------------------------------------------------
    # EventBus Publish Helper
    # ------------------------------------------------------------------------
    def _publish_event(self, topic: str, payload: Dict[str, Any]) -> None:
        try:
            _bus.publish(topic, dict(payload))
            _logger.info("Loader: published event '%s'", topic)
        except Exception as exc:
            _logger.warning(
                "Loader: failed to publish event '%s': %s",
                topic,
                exc,
                exc_info=True,
            )

# ----------------------------------------------------------------------------
# Entry Point
# ----------------------------------------------------------------------------
_loader_instance: BIOSLoader | None = None

def start_loader() -> BIOSLoader:
    """External entry for loader initialization."""
    global _loader_instance
    if _loader_instance is None:
        _loader_instance = BIOSLoader()
    return _loader_instance

# ----------------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------------
__all__ = [
    "BIOSLoader",
    "start_loader",
    "_bios_ready",
    "_loader_lock",
    "_vsd",
    "_loaded",
]
