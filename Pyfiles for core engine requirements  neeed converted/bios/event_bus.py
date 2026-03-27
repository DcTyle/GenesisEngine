# ============================================================================
# Quantum Application / bios
# ASCII-ONLY SOURCE FILE
# File: event_bus.py
# Version: v5.2 "Unified Event Core" (Silent-Error Purge Applied)
# Jarvis ADA v4.7 Hybrid Ready
# ============================================================================
"""
Purpose
-------
Implements a lightweight, thread-safe EventBus for the BIOS layer.
Provides publish/subscribe infrastructure for system events and telemetry.

This file has been upgraded for:
  * full error visibility
  * exc_info=True on all error logs
  * no silent failures
  * consistent exception logging

No logic or behavior has been changed.
"""

from __future__ import annotations

import threading
import logging
import time
from typing import Callable, Dict, Any, List, Tuple

# ----------------------------------------------------------------------------
# Structured logging
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.event_bus")
if not _logger.handlers:
    handler = logging.StreamHandler()
    formatter = logging.Formatter(
        fmt="%(asctime)sZ | %(name)s | %(levelname)s | %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S",
    )
    handler.setFormatter(formatter)
    _logger.addHandler(handler)
try:
    DEBUG
except NameError:
    DEBUG = False
_logger.setLevel(logging.INFO if DEBUG else logging.WARNING)

# ----------------------------------------------------------------------------
# Event map (canonical topics)
# ----------------------------------------------------------------------------
event_map: Dict[str, str] = {
    "BOOT_INIT": "bios.init",
    "BOOT_COMPLETE": "boot.complete",

    "SCHEDULER_TICK": "scheduler.tick",
    "CONFIG_RELOAD": "config.update",

    "PREDICTION_ROLLUP": "prediction.rollup",
    "FAILSAFE_TRIGGER": "failsafe.trigger",
    "MINER_UPDATE": "miner.update",

    "TELEMETRY_PUSH": "telemetry.push",
    "DIAGNOSTIC_REPORT": "diagnostic.generate",

    "WARN": "bios.warn",
    "ERROR": "bios.error",
    "INFO": "bios.info",
}

# ----------------------------------------------------------------------------
# EventBus Core
# ----------------------------------------------------------------------------
class EventBus:
    """Thread-safe publish/subscribe bus with priority ordering."""

    def __init__(self) -> None:
        # topic -> list[(priority, handler)]
        self._subs: Dict[str, List[Tuple[int, Callable[[Dict[str, Any]], None]]]] = {}
        self._lock = threading.RLock()
        self._enabled = True
        self._event_count = 0

    # ---------------------------------------------------------------------
    def subscribe(
        self,
        topic: str,
        handler: Callable[[Dict[str, Any]], None],
        *,
        once: bool = False,
        priority: int = 0,
        name: str | None = None,
    ) -> None:
        """
        Subscribe handler to topic; higher priority executes first.
        """
        try:
            with self._lock:
                lst = self._subs.setdefault(topic, [])
                lst.append((priority, handler))
                lst.sort(key=lambda item: item[0], reverse=True)
                _logger.debug(
                    "Subscribed handler %s to %s",
                    name or getattr(handler, "__name__", "<handler>"),
                    topic,
                )
        except Exception as exc:
            _logger.error(
                "EventBus.subscribe failed (topic=%s): %s",
                topic,
                exc,
                exc_info=True,
            )

    # ---------------------------------------------------------------------
    def publish(self, topic: str, data: Dict[str, Any] | None = None) -> None:
        """Publish an event to all subscribers. Failures are isolated."""
        if not self._enabled:
            return

        payload: Dict[str, Any] = dict(data or {})

        try:
            with self._lock:
                handlers = list(self._subs.get(topic, []))
        except Exception as exc:
            _logger.error(
                "EventBus.publish failed to read handlers (topic=%s): %s",
                topic,
                exc,
                exc_info=True,
            )
            return

        self._event_count += 1

        for _, handler in handlers:
            try:
                handler(payload)
            except Exception as exc:
                _logger.warning(
                    "Handler failure for topic %s: %s",
                    topic,
                    exc,
                    exc_info=True,
                )

    # ---------------------------------------------------------------------
    def unsubscribe(
        self,
        topic: str,
        handler: Callable[[Dict[str, Any]], None],
    ) -> None:
        """Remove a handler from a topic."""
        try:
            with self._lock:
                lst = self._subs.get(topic, [])
                filtered = [(p, h) for (p, h) in lst if h is not handler]
                if filtered:
                    self._subs[topic] = filtered
                else:
                    self._subs.pop(topic, None)
        except Exception as exc:
            _logger.error(
                "EventBus.unsubscribe error for topic=%s: %s",
                topic,
                exc,
                exc_info=True,
            )

    # ---------------------------------------------------------------------
    def clear(self) -> None:
        """Clear all subscriptions and reset counters."""
        try:
            with self._lock:
                self._subs.clear()
                self._event_count = 0
        except Exception as exc:
            _logger.error("EventBus.clear failed: %s", exc, exc_info=True)

    # ---------------------------------------------------------------------
    def stats(self) -> Dict[str, Any]:
        """Return basic statistics for diagnostics."""
        try:
            with self._lock:
                handler_count = sum(len(v) for v in self._subs.values())
                topics = list(self._subs.keys())
            return {
                "topics": len(topics),
                "handlers": handler_count,
                "events_published": self._event_count,
                "timestamp": time.time(),
                "topic_names": topics,
            }
        except Exception as exc:
            _logger.error("EventBus.stats failed: %s", exc, exc_info=True)
            return {"error": str(exc)}

# ----------------------------------------------------------------------------
# Singleton factory
# ----------------------------------------------------------------------------
_global_bus: EventBus | None = None
_global_bus_lock = threading.Lock()

def get_event_bus() -> EventBus:
    """Return the global EventBus instance, creating it if necessary."""
    global _global_bus
    try:
        if _global_bus is None:
            with _global_bus_lock:
                if _global_bus is None:
                    _global_bus = EventBus()
                    _logger.info("Global EventBus initialized")
        return _global_bus
    except Exception as exc:
        _logger.error("get_event_bus failed: %s", exc, exc_info=True)
        raise

# ----------------------------------------------------------------------------
# BIOS Utilities Facade
# ----------------------------------------------------------------------------
class BIOSUtilities:
    """
    Lightweight facades over:
      - EventBus
      - Diagnostics
      - SelfCheck
      - Policy
    """

    def __init__(self) -> None:
        self.bus = get_event_bus()

        try:
            from bios.policy import DutyCap, Confidence  # type: ignore
            self.DutyCap = DutyCap
            self.NetworkCap = None
            self.Confidence = Confidence
        except Exception as exc:
            _logger.warning(
                "BIOSUtilities: failed to import policy modules: %s",
                exc,
                exc_info=True,
            )
            self.DutyCap = None
            self.NetworkCap = None
            self.Confidence = None

        try:
            from bios.selfcheck import run_selfcheck  # type: ignore
            self.run_selfcheck = run_selfcheck
        except Exception as exc:
            _logger.warning(
                "BIOSUtilities: run_selfcheck unavailable: %s",
                exc,
                exc_info=True,
            )
            self.run_selfcheck = lambda: {"status": "unavailable"}

        try:
            from bios.runtime import ServiceRegistry  # type: ignore
            self.ServiceRegistry = ServiceRegistry
        except Exception as exc:
            _logger.warning(
                "BIOSUtilities: ServiceRegistry unavailable: %s",
                exc,
                exc_info=True,
            )
            self.ServiceRegistry = None

        import threading as _threading
        import time as _time
        self._lock = _threading.RLock()
        self._state: Dict[str, Any] = {}
        self._time = _time

    # ------------------------------------------------------------------
    def publish(self, topic: str, payload: Dict[str, Any] | None = None) -> None:
        try:
            self.bus.publish(topic, dict(payload or {}))
        except Exception as exc:
            _logger.error("BIOSUtilities.publish error: %s", exc, exc_info=True)

    # ------------------------------------------------------------------
    def subscribe(
        self,
        topic: str,
        handler: Callable[[Dict[str, Any]], None],
        *,
        priority: int = 0,
        name: str | None = None,
    ) -> None:
        try:
            self.bus.subscribe(topic, handler, priority=priority, name=name)
        except Exception as exc:
            _logger.error("BIOSUtilities.subscribe error: %s", exc, exc_info=True)

    # ------------------------------------------------------------------
    def set_state(self, key: str, value: Any) -> None:
        with self._lock:
            self._state[key] = value

    def get_state(self, key: str, default: Any = None) -> Any:
        with self._lock:
            return self._state.get(key, default)

    # ------------------------------------------------------------------
    def lock_acquire(self, name: str, ttl_s: float = 2.0) -> bool:
        key = "lock::" + name
        now = self._time.time()
        with self._lock:
            record = self._state.get(key)
            if isinstance(record, dict) and record.get("exp", 0.0) > now:
                return False
            self._state[key] = {"exp": now + ttl_s}
            return True

    def lock_release(self, name: str) -> None:
        key = "lock::" + name
        with self._lock:
            self._state.pop(key, None)

    # ------------------------------------------------------------------
    def telemetry(self, path: str, payload: Dict[str, Any]) -> None:
        try:
            from core.utils import append_telemetry  # type: ignore
            append_telemetry(path, dict(payload or {}))
        except Exception as exc:
            _logger.warning(
                "telemetry unavailable for path=%s: %s",
                path,
                exc,
                exc_info=True,
            )

    # ------------------------------------------------------------------
    def log_diagnostic(self, message: str, level: str = "info") -> None:
        if callable(getattr(self, "run_diagnostics", None)):
            try:
                self.run_diagnostics(
                    extra={"message": message, "level": level}
                )
                return
            except Exception as exc:
                _logger.warning(
                    "diagnostic handler failed: %s",
                    exc,
                    exc_info=True,
                )
        log_fn = getattr(_logger, level, _logger.info)
        log_fn("DIAG: %s", message)

    # ------------------------------------------------------------------
    def schedule(self, fn: Callable[[], None], delay_s: float = 0.0) -> None:
        try:
            import threading as _threading
            timer = _threading.Timer(delay_s, fn)
            timer.daemon = True
            timer.start()
        except Exception as exc:
            _logger.error("BIOSUtilities.schedule error: %s", exc, exc_info=True)

    # ------------------------------------------------------------------
    def snapshot(self) -> Dict[str, Any]:
        try:
            with self._lock:
                state_copy = dict(self._state)
            bus_stats = self.bus.stats()
            return {
                "state": state_copy,
                "time": self._time.time(),
                "bus": bus_stats,
            }
        except Exception as exc:
            _logger.error("BIOSUtilities.snapshot failed: %s", exc, exc_info=True)
            return {"error": str(exc)}

# Global instance
bios_utils = BIOSUtilities()

__all__ = [
    "EventBus",
    "get_event_bus",
    "event_map",
    "BIOSUtilities",
    "bios_utils",
]
