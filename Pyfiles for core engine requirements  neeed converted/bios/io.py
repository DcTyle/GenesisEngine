# Path: Quantum Application/bios/io.py
# ============================================================================
# Quantum Application / bios
# File: io.py
# Version: v5.1 "Unified Imports + Silent Error Hardened"
# ============================================================================
"""
Purpose
-------
Input/output layer for BIOS configuration and runtime state.
Provides standardized get/store/delete wrappers for VSD keys
and manages lightweight persistence to the in-memory state map.

Notes
-----
- No direct disk I/O is performed here.
- AI_Processor handles dehydration and snapshotting of state.
"""

from __future__ import annotations
from typing import Any, Dict
import threading
import time
import logging

# ---------------------------------------------------------------------------
# Local logger
# ---------------------------------------------------------------------------
_logger = logging.getLogger("bios.io")

# ---------------------------------------------------------------------------
# Layered imports
# ---------------------------------------------------------------------------
from core.utils import get, store, delete


# ---------------------------------------------------------------------------
# I/O manager
# ---------------------------------------------------------------------------
class BIOSIO:
    """Encapsulates all BIOS I/O operations for configuration and telemetry."""

    def __init__(self, vsd: Any):
        self.vsd = vsd
        self._lock = threading.RLock()
        self.last_write_ts = 0.0
        self.write_count = 0

    # ------------------------------------------------------------
    # Basic read/write/delete wrappers (now hardened)
    # ------------------------------------------------------------
    def read(self, key: str, default: Any = None) -> Any:
        with self._lock:
            try:
                return get(self.vsd, key, default)
            except Exception as exc:
                _logger.error(
                    "BIOSIO.read: failed to read key '%s': %s",
                    key,
                    exc,
                    exc_info=True,
                )
                return default

    def write(self, key: str, value: Any) -> None:
        with self._lock:
            try:
                store(self.vsd, key, value)
                self.last_write_ts = time.time()
                self.write_count += 1
            except Exception as exc:
                _logger.error(
                    "BIOSIO.write: failed to write key '%s': %s",
                    key,
                    exc,
                    exc_info=True,
                )

    def erase(self, key: str) -> None:
        with self._lock:
            try:
                delete(self.vsd, key)
            except Exception as exc:
                _logger.error(
                    "BIOSIO.erase: failed to delete key '%s': %s",
                    key,
                    exc,
                    exc_info=True,
                )

    # ------------------------------------------------------------
    # Convenience helpers
    # ------------------------------------------------------------
    def dump_state(self) -> Dict[str, Any]:
        return {
            "write_count": self.write_count,
            "last_write_ts": self.last_write_ts,
        }

# End of file
