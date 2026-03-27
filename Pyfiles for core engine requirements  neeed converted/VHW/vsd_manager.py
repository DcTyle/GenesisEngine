from __future__ import annotations

from typing import Any, Dict
import threading


class VSDManager:
    """Minimal key-value store for Virtual State Directory (VSD).

    - Thread-safe in-process dictionary.
    - Keys are arbitrary strings like "vsd/phase_amplitude/global".
    - API surface used by BIOS: store(), get(), delete(), set().
    """

    def __init__(self) -> None:
        self._kv: Dict[str, Any] = {}
        self._lock = threading.RLock()

    def store(self, key: str, value: Any) -> None:
        k = str(key)
        with self._lock:
            self._kv[k] = value

    def get(self, key: str, default: Any = None) -> Any:
        k = str(key)
        with self._lock:
            return self._kv.get(k, default)

    def delete(self, key: str) -> None:
        k = str(key)
        with self._lock:
            self._kv.pop(k, None)

    # Compatibility with callers expecting a dict-like .set()
    def set(self, key: str, value: Any) -> None:
        self.store(key, value)
