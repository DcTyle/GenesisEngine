# Path: Quantum Application/bios/state_manager.py
# ================================================================
# Quantum Application / BIOS
# ASCII-ONLY SOURCE FILE
# File: state_manager.py
# Version: v5.1 "Unified Imports + Silent Error Purge"
# ================================================================
"""
Purpose
-------
Manages in-memory and persistent runtime state for the BIOS and
Prediction Engine subsystems.  Handles:

  * Thread-safe VSD access
  * ASCII floatmap snapshots
  * Trade log rotation (<=500 entries)
  * ModelCore model persistence
  * Health and telemetry snapshots
"""

from __future__ import annotations
from typing import Any, Dict, List, Optional
import threading
import time
import json
import logging

# ---------------------------------------------------------------
# Local logger
# ---------------------------------------------------------------
_logger = logging.getLogger("bios.state_manager")


# ================================================================
# Core StateManager
# ================================================================

class StateManager:
    """Thread-safe VSD interface for storing BIOS runtime state."""

    def __init__(self, vsd):
        self.vsd = vsd
        self._lock = threading.RLock()
        self.snapshot_key = "snapshots/state_text"
        self._floatmap_scale = 10_000_000

    # ------------------------------------------------------------
    def get(self, key: str, default: Any = None) -> Any:
        with self._lock:
            try:
                return self.vsd.get(key, default)
            except Exception as exc:
                _logger.error(
                    "StateManager.get: failed to retrieve key '%s': %s",
                    key,
                    exc,
                    exc_info=True,
                )
                return default

    def store(self, key: str, value: Any) -> None:
        with self._lock:
            try:
                self.vsd.store(key, value)
            except Exception as exc:
                _logger.error(
                    "StateManager.store: failed to store key '%s': %s",
                    key,
                    exc,
                    exc_info=True,
                )

    # ------------------------------------------------------------
    def snapshot_ascii_floatmap(self, data: Dict[str, list]) -> str:
        """
        Encode complex vectors into canonical tokenized vector representation.
        Deterministic and reversible.
        Handles both scalars and lists of complex numbers for each key as full vectors.
        """
        from core.utils import serialize_vector_with_tokens
        out = {}
        for k, v in data.items():
            try:
                # Always treat as list for vector encoding
                if not isinstance(v, list):
                    v = [v]
                # Accept both float and complex
                complex_vec = [complex(x.real, x.imag) if isinstance(x, complex) else complex(float(x), 0.0) for x in v]
                out[k] = serialize_vector_with_tokens(complex_vec)
            except Exception as exc:
                _logger.warning(
                    "snapshot_ascii_floatmap: failed to encode key '%s': %s",
                    k,
                    exc,
                    exc_info=True,
                )
                continue
        try:
            with self._lock:
                self.vsd.store(self.snapshot_key, out)
        except Exception as exc:
            _logger.error(
                "snapshot_ascii_floatmap: failed to store snapshot '%s': %s",
                self.snapshot_key,
                exc,
                exc_info=True,
            )
        return str(out)

    # ------------------------------------------------------------
    def restore_ascii_floatmap(self) -> Dict[str, list]:
        """Decode canonical tokenized vector back into dictionary of complex vectors (full vectors per key)."""
        from core.utils import deserialize_vector_with_tokens
        try:
            snap = self.vsd.get(self.snapshot_key, {})
        except Exception as exc:
            _logger.error(
                "restore_ascii_floatmap: failed to fetch snapshot '%s': %s",
                self.snapshot_key,
                exc,
                exc_info=True,
            )
            return {}

        if not isinstance(snap, dict) or not snap:
            return {}

        out: Dict[str, list] = {}
        for k, v in snap.items():
            try:
                decoded = deserialize_vector_with_tokens(v)
                out[k] = decoded
            except Exception as exc:
                _logger.warning(
                    "restore_ascii_floatmap: failed to decode segment '%s': %s",
                    k,
                    exc,
                    exc_info=True,
                )
                continue
        return out

    # ------------------------------------------------------------
    def mark_timestamp(self, label: str) -> None:
        """Record a simple UTC timestamp under a given key."""
        ts = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        try:
            self.store(f"timestamps/{label}", ts)
        except Exception as exc:
            _logger.error(
                "mark_timestamp: failed to record timestamp for '%s': %s",
                label,
                exc,
                exc_info=True,
            )


# ================================================================
# v4.8.5 Extension: Prediction Engine Persistence
# ================================================================

class PredictionAwareStateManager(StateManager):
    """
    Unified BIOS + PredictionEngine state manager.
    Extends base StateManager with model and trade persistence.
    """

    def __init__(self, vsd):
        super().__init__(vsd)
        self.trade_key = "trade/logs"
        self.model_key = "model/snapshots"

    # ------------------------------------------------------------
    def append_trade_log(self, trade: Dict[str, Any]) -> None:
        """Append a trade entry (rotating up to 500)."""
        try:
            with self._lock:
                hist = self.vsd.get(self.trade_key, []) or []
                entry = dict(trade)
                entry["ts"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
                hist.append(entry)
                self.vsd.store(self.trade_key, hist[-500:])
        except Exception as exc:
            _logger.error(
                "append_trade_log: failed to append trade: %s",
                exc,
                exc_info=True,
            )

    # ------------------------------------------------------------
    def save_models(self, model_core: Any) -> None:
        """Persist ModelCore.models into the VSD."""
        try:
            models = getattr(model_core, "models", {})
            meta = {
                "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "count": len(models),
            }
            blob = json.dumps(models, separators=(",", ":"))
            self.vsd.store(self.model_key, {"meta": meta, "data": blob})
        except Exception as exc:
            _logger.error(
                "save_models: failed to persist model snapshots: %s",
                exc,
                exc_info=True,
            )

    # ------------------------------------------------------------
    def load_models(self, model_core: Any) -> bool:
        """Restore models from VSD into ModelCore."""
        try:
            snap = self.vsd.get(self.model_key, {})
            if not snap or "data" not in snap:
                return False
            models = json.loads(snap["data"])
            model_core.models = models
            return True
        except Exception as exc:
            _logger.error(
                "load_models: failed to restore model snapshots: %s",
                exc,
                exc_info=True,
            )
            return False

    # ------------------------------------------------------------
    def record_prediction_health(self, assets: int, signals: int, orders: int) -> None:
        """Store quick telemetry snapshot for diagnostics."""
        try:
            self.vsd.store(
                "prediction/health",
                {
                    "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    "assets": int(assets),
                    "signals": int(signals),
                    "orders": int(orders),
                },
            )
        except Exception as exc:
            _logger.error(
                "record_prediction_health: failed to store health snapshot: %s",
                exc,
                exc_info=True,
            )


# ---------------------------------------------------------------
# Example standalone test
# ---------------------------------------------------------------
if __name__ == "__main__":
    class DummyVSD:
        def __init__(self):
            self.m = {}
        def get(self, k, d=None): return self.m.get(k, d)
        def store(self, k, v): self.m[k] = v

    vsd = DummyVSD()
    sm = PredictionAwareStateManager(vsd)
    sm.store("test/key", 123)
    print("Fetch test:", sm.get("test/key"))
    snap = sm.snapshot_ascii_floatmap({"a": 1.23, "b": 4.56})
    print("Snapshot:", snap)
    restored = sm.restore_ascii_floatmap()
    print("Restored:", restored)
    sm.append_trade_log({"symbol": "BTC_USDT", "price": 100.0})
    print("Trade logs:", vsd.m.get("trade/logs"))
    sm.record_prediction_health(assets=4, signals=8, orders=2)
    print("Health:", vsd.m.get("prediction/health"))
