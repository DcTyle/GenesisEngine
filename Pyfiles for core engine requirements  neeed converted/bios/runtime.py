# Path: Quantum Application/bios/runtime.py
# ============================================================================
# Version: v6.1 "Silent Error Purge + Logging"
# VirtualMiner / BIOS
# File: runtime.py
# Directory: /bios/
# ASCII-ONLY SOURCE FILE
# Jarvis ADA v4.8.x Hybrid Ready
# ============================================================================

"""
Purpose
-------
BIOS runtime helpers: start/stop guards, health snapshots,
and a generic service registry for the VirtualMiner runtime.
Also provides an optional prediction supervisor helper that can be
used by the prediction engine module itself.

Design Rules
------------
- Does NOT import miner or prediction_engine modules directly.
- Only depends on core.utils (telemetry helpers) plus standard library.
- Can be imported by miner / prediction_engine as a utility library.
"""

from __future__ import annotations
from typing import Dict, Any, Callable
import time
import threading
import logging

# ---------------------------------------------------------------------------
# Local logger
# ---------------------------------------------------------------------------
_logger = logging.getLogger("bios.runtime")

# ---------------------------------------------------------------------------
# core.utils helpers (best-effort)
# ---------------------------------------------------------------------------
# try:
#     from core.utils import append_telemetry, get, store
# except Exception as exc:
#     _logger.warning(
#         "bios.runtime: core.utils import failed: %s",
#         exc,
#         exc_info=True,
#     )
#
#     def append_telemetry(topic: str, payload: Dict[str, Any]) -> None:
#         _logger.warning(
#             "append_telemetry fallback invoked for topic '%s'",
#             topic,
#         )
#
#     def get(key: str, default: Any = None) -> Any:
#         _logger.warning(
#             "get() fallback invoked for key '%s'",
#             key,
#         )
#         return default
#
#     def store(key: str, value: Any) -> None:
#         _logger.warning(
#             "store() fallback invoked for key '%s'",
#             key,
#         )

# ---------------------------------------------------------------------------
# Service Registry
# ---------------------------------------------------------------------------
class ServiceRegistry:
    """
    Minimal service registry for coordinating start/stop of named services.
    """

    def __init__(self) -> None:
        self._items: list[tuple[str, Callable[[], None], Callable[[], None]]] = []
        self._lock = threading.RLock()
        self._running = False

    def register(self, name: str,
                 start_fn: Callable[[], None],
                 stop_fn: Callable[[], None]) -> None:
        with self._lock:
            self._items.append((str(name), start_fn, stop_fn))

    def start_all(self) -> None:
        with self._lock:
            if self._running:
                return
            for name, start_fn, _ in self._items:
                try:
                    _logger.info("ServiceRegistry: starting '%s'", name)
                    start_fn()
                except Exception as exc:
                    _logger.error(
                        "ServiceRegistry: failed to start '%s': %s",
                        name,
                        exc,
                        exc_info=True,
                    )
            self._running = True

    def stop_all(self, timeout: float = 2.0) -> None:
        with self._lock:
            if not self._running:
                return
            for name, _, stop_fn in reversed(self._items):
                try:
                    _logger.info("ServiceRegistry: stopping '%s'", name)
                    stop_fn()
                except Exception as exc:
                    _logger.error(
                        "ServiceRegistry: failed to stop '%s': %s",
                        name,
                        exc,
                        exc_info=True,
                    )
            self._running = False

    def status(self) -> Dict[str, Any]:
        with self._lock:
            return {
                "services": [name for name, _, _ in self._items],
                "running": self._running,
            }

# ---------------------------------------------------------------------------
# Health Snapshot
# ---------------------------------------------------------------------------
def health_snapshot(reader: Callable[[], Dict[str, Any]]) -> Dict[str, Any]:
    """
    Normalize a telemetry frame into a standard health snapshot dict.
    """
    try:
        d = reader() or {}
    except Exception as exc:
        _logger.error(
            "health_snapshot: reader() failed: %s",
            exc,
            exc_info=True,
        )
        d = {}

    return {
        "timestamp": d.get("timestamp"),
        "global_util": float(d.get("global_util", 0.0)),
        "gpu_util": float(d.get("gpu_util", 0.0)),
        "gpu_mem_util": float(d.get("gpu_mem_util", 0.0)),
        "mem_bw_util": float(d.get("mem_bw_util", 0.0)),
        "cpu_util": float(d.get("cpu_util", 0.0)),
        "util_cap": float(d.get("util_cap", 0.75)),
        "headroom": float(d.get("group_headroom", 0.10)),
    }

# ---------------------------------------------------------------------------
# Prediction Engine Supervisor (generic)
# ---------------------------------------------------------------------------
def start_prediction_supervisor(engine: Any,
                                interval_s: float = 5.0) -> threading.Thread:
    """
    Background thread that monitors a prediction engine instance,
    logs telemetry, and adjusts confidence gate parameters.
    """

    def loop() -> None:
        _logger.info("PredictionSupervisor: thread started.")

        while True:
            try:
                snap = engine.snapshot()
                conf = float(snap.get("confidence", 0.0))
                samples = int(snap.get("samples", 0))
                last_trade = snap.get("last_trade", {})

                # Adaptive Murphy-style gate logic
                if conf > 0.995 and samples > 100:
                    gate = min(0.999, conf + 0.001)
                    engine.update_config({"confidence_gate": gate})
                    # store("murphy/last_update", {
                    #     "t": time.time(),
                    #     "reason": "confidence_up",
                    #     "new_gate": gate,
                    # })
                    _logger.info(
                        "PredictionSupervisor: gate raised to %.3f",
                        gate,
                    )

                elif conf < 0.90:
                    gate = max(0.90, conf)
                    engine.update_config({"confidence_gate": gate})
                    # store("murphy/last_update", {
                    #     "t": time.time(),
                    #     "reason": "confidence_down",
                    #     "new_gate": gate,
                    # })
                    _logger.info(
                        "PredictionSupervisor: gate reduced to %.3f",
                        gate,
                    )

            except Exception as exc:
                _logger.error(
                    "PredictionSupervisor: error processing loop: %s",
                    exc,
                    exc_info=True,
                )

            time.sleep(interval_s)

    t = threading.Thread(target=loop, daemon=True)
    t.start()
    return t
