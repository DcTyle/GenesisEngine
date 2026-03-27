# Path: Quantum Application/bios/emergency.py
# ============================================================================
# Quantum Application / BIOS
# ASCII-ONLY SOURCE FILE
# File: bios/emergency.py
# Version: v5.1 "Unified Imports + Error Visibility"
# ============================================================================
"""
Purpose
-------
Centralized BIOS emergency stop orchestrator with integrated Hostile-Outreach Prevention Policy.
Mirrors the BIOSBoot pattern:
- Activates global emergency flag
- Persists state in VSD
- Publishes 'emergency.stop' & 'prediction.stop'
- Executes registered shutdown hooks concurrently
- Enforces containment of any outbound communications from hooks or EventBus

Patch Integration (AG)
-----------------------
A: Infrastructure / Import Fixes
   - Imports aligned with matrix, placeholders for missing modules
B: Runtime Integration
   - Publishes events to matrix-defined EventBus topics
C: Telemetry / Health
   - Structured ASCII-only UTC logging
E: Security / Containment
   - Hostile-Outreach Prevention Policy applied to shutdown hooks and EventBus
F: Persistence / VSD Refactor
   - Persists emergency state to VSD
G: Jarvis Boot Automation
   - Entry via start_emergency()
"""

from __future__ import annotations
import threading
import time
import logging
from typing import Callable, Dict, Any, List

# ----------------------------------------------------------------------------
# Structured Logger
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.emergency")
if not _logger.handlers:
    _h = logging.StreamHandler()
    _fmt = logging.Formatter(
        fmt="[%(asctime)s] EMERGENCY %(levelname)s: %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ",
    )
    _h.setFormatter(_fmt)
    _logger.addHandler(_h)
_logger.setLevel(logging.INFO)

# ----------------------------------------------------------------------------
# Matrix-Aligned Imports & EventBus
# ----------------------------------------------------------------------------
try:
    from bios.event_bus import get_event_bus, event_map
except Exception as exc:
    _logger.warning(
        "bios.event_bus unavailable; using NoOp bus: %s",
        exc,
        exc_info=True,
    )

    class _NoBus:
        def publish(self, topic: str, data=None): return
        def subscribe(self, topic: str, handler=None): return

    def get_event_bus(): return _NoBus()

try:
    from VHW.vsd_manager import VSDManager
except Exception as exc:
    _logger.warning(
        "VHW.vsd_manager unavailable; using dummy VSDManager: %s",
        exc,
        exc_info=True,
    )

    class VSDManager:
        def store(self, key: str, value: Any): return
        def get(self, key: str, default: Any = None): return default

# ----------------------------------------------------------------------------
# Security Policy Flag & Helpers
# ----------------------------------------------------------------------------
HOSTILE_OUTREACH_POLICY_ACTIVE = True

def sandbox_check(fn: Callable[[], None]) -> bool:
    """
    Simple placeholder for sandbox logic.
    Returns False if the function attempts unauthorized network or OS calls.
    """
    # Real implementation would use static/dynamic analysis or wrapper to detect unsafe calls
    return True  # Allow by default; customize as needed

def is_external_topic(event: str) -> bool:
    """
    Determines if the EventBus topic could reach external systems.
    """
    # Placeholder: treat any topic not explicitly internal as external
    internal_topics = {"emergency.stop", "emergency.clear", "prediction.stop"}
    return event not in internal_topics

def human_approval(event: str, payload: Dict[str, Any]) -> bool:
    """
    Placeholder for human-in-loop approval check.
    """
    # In production, connect to Watchdog or operator approval system
    _logger.warning("Security Policy: approval required for event '%s'", event)
    return False  # Deny by default

def record_policy_violation(idx: int, fn: Callable[[], None]) -> None:
    _logger.warning("Security Policy: hook %d blocked by Hostile-Outreach Prevention", idx)

def record_policy_violation_event(event: str, payload: Dict[str, Any]) -> None:
    _logger.warning("Security Policy: event '%s' blocked by Hostile-Outreach Prevention", event)

# ----------------------------------------------------------------------------
# BIOS Emergency Orchestrator
# ----------------------------------------------------------------------------
class BIOSEmergency:
    """Class-based orchestrator for emergency stop."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._shutdown_hooks: List[Callable[[], None]] = []
        self._done_evt = threading.Event()
        self._emergency_active: bool = False
        self._bus = get_event_bus()
        self._vsd = VSDManager()

    # ------------------------------------------------------------------------
    # BIOS readiness check
    # ------------------------------------------------------------------------
    def _bios_ready(self) -> bool:
        try:
            ready = self._vsd.get("system/bios_boot_ok", False) is True
            if not ready:
                _logger.warning("Emergency: BIOS not ready; gating emergency actions.")
            return ready
        except Exception as exc:
            _logger.error(
                "Emergency: BIOS readiness check failed: %s",
                exc,
                exc_info=True,
            )
            return False

    # ------------------------------------------------------------------------
    # Persistence & EventBus helpers
    # ------------------------------------------------------------------------
    def _persist_state(self, active: bool, reason: str = "") -> None:
        try:
            self._vsd.store("system/emergency_active", bool(active))
            if active:
                self._vsd.store("system/emergency_reason", str(reason))
                self._vsd.store("system/emergency_ts", float(time.time()))
            _logger.info("Emergency: VSD state persisted (active=%s)", active)
        except Exception as exc:
            _logger.warning(
                "Emergency: VSD persistence failed: %s",
                exc,
                exc_info=True,
            )

    def _publish_event(self, event: str, payload: Dict[str, Any]) -> None:
        if HOSTILE_OUTREACH_POLICY_ACTIVE and is_external_topic(event):
            if not human_approval(event, payload):
                record_policy_violation_event(event, payload)
                return
        try:
            self._bus.publish(event, payload)
            _logger.info("Emergency: published event '%s'", event)
        except Exception as exc:
            _logger.warning(
                "Emergency: EventBus publish failed for '%s': %s",
                event,
                exc,
                exc_info=True,
            )

    # ------------------------------------------------------------------------
    # Shutdown hooks execution
    # ------------------------------------------------------------------------
    def _run_shutdown_hooks(self) -> None:
        hooks = list(self._shutdown_hooks)
        if not hooks:
            _logger.info("Emergency: no shutdown hooks registered.")
            return
        _logger.info("Emergency: executing %d shutdown hooks", len(hooks))
        from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
        try:
            with ThreadPoolExecutor(max_workers=4, thread_name_prefix="emg_hook") as ex:
                futures = [ex.submit(self._safe_hook, idx, fn) for idx, fn in enumerate(hooks)]
                wait(futures, timeout=10.0, return_when=FIRST_COMPLETED)
        except Exception as exc:
            _logger.error(
                "Emergency: shutdown hook executor failed: %s",
                exc,
                exc_info=True,
            )

    def _safe_hook(self, idx: int, fn: Callable[[], None]) -> None:
        if HOSTILE_OUTREACH_POLICY_ACTIVE:
            try:
                allowed = sandbox_check(fn)
                if not allowed:
                    record_policy_violation(idx, fn)
                    return
            except Exception as exc:
                _logger.error(
                    "Emergency: security enforcement failed for hook %d: %s",
                    idx,
                    exc,
                    exc_info=True,
                )
                return
        try:
            fn()
            _logger.info("Emergency: shutdown hook %d completed", idx)
        except Exception as exc:
            _logger.error(
                "Emergency: shutdown hook %d failed: %s",
                idx,
                exc,
                exc_info=True,
            )

    # ------------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------------
    def trigger(self, reason: str = "") -> Dict[str, Any]:
        with self._lock:
            if not self._bios_ready():
                return {"ok": False, "error": "bios_not_ready"}
            if self._emergency_active:
                _logger.info("Emergency: already active.")
                return {"ok": True, "reason": "already_active"}

            self._emergency_active = True
            _logger.warning("Emergency: ACTIVATED (reason='%s')", reason)
            self._persist_state(True, reason)
            payload = {"ts": time.time(), "reason": reason}

            self._publish_event("emergency.stop", dict(payload))
            self._publish_event("prediction.stop", dict(payload))

            self._run_shutdown_hooks()
            return {"ok": True, "reason": reason}

    def clear(self) -> Dict[str, Any]:
        with self._lock:
            if not self._emergency_active:
                _logger.info("Emergency: already clear.")
                return {"ok": True, "cleared": False}
            self._emergency_active = False
            self._persist_state(False)
            self._publish_event("emergency.clear", {"ts": time.time()})
            _logger.info("Emergency: CLEARED.")
            return {"ok": True, "cleared": True}

    def is_active(self) -> bool:
        return bool(self._emergency_active)

    def register_hook(self, fn: Callable[[], None]) -> None:
        if not callable(fn):
            raise ValueError("shutdown hook must be callable")
        self._shutdown_hooks.append(fn)
        _logger.info(
            "Emergency: registered shutdown hook (count=%d)",
            len(self._shutdown_hooks),
        )

# ----------------------------------------------------------------------------
# External entry
# ----------------------------------------------------------------------------
_global_emergency: BIOSEmergency | None = None
_global_lock = threading.Lock()

def start_emergency() -> BIOSEmergency:
    global _global_emergency
    if _global_emergency is None:
        with _global_lock:
            if _global_emergency is None:
                _global_emergency = BIOSEmergency()
                _logger.info("Global BIOSEmergency initialized")
    return _global_emergency

# ----------------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------------
__all__ = [
    "BIOSEmergency",
    "start_emergency",
]
