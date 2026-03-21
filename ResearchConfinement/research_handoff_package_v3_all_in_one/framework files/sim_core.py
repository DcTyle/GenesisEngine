"""Core simulation substrate: canonical SimCore singleton.

This module exposes a `SimCore` singleton that wraps the ParticleSim
simulation substrate (GPU-aware) and provides a simple attach/register
API for other engines to use as a compute amplifier / parallel simulation
substrate. It is intentionally abstract and safe: no networking, no PoW.

Engines may call `register_engine(engine_id, metadata, attach_cb)` to
register; they can then `attach_qubits(engine_id, state_array)` or
`submit_compute(engine_id, payload)` to ask the substrate for compute.
"""
from __future__ import annotations
import threading
import time
import logging
from typing import Any, Callable, Dict, Optional

from core.utils import store, append_telemetry, get
try:
    from bios.event_bus import get_event_bus
except Exception:
    def get_event_bus():
        return None

try:
    from Neuralis_AI.particle_sim import ParticleSim, create_default_sim
except Exception:
    # Safe fallback: lazy import error-handling to avoid boot-time failure
    ParticleSim = None
    create_default_sim = None

_logger = logging.getLogger("core.sim_core")
if not _logger.handlers:
    _h = logging.StreamHandler()
    _h.setFormatter(logging.Formatter("%(asctime)s | %(levelname)s | %(message)s"))
    _logger.addHandler(_h)
_logger.setLevel(logging.INFO)


class SimCore:
    """Singleton wrapper exposing the compute substrate.

    Methods:
      - start(n_particles)
      - stop()
      - register_engine(engine_id, metadata, attach_callback)
      - unregister_engine(engine_id)
      - attach_qubits(engine_id, qubit_state)
      - submit_compute(engine_id, payload)
      - get_metrics()
    """

    _instance: Optional["SimCore"] = None

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._sim = None
        self._engines: Dict[str, Dict[str, Any]] = {}
        self._running = False
        self._last_metrics: Dict[str, Any] = {}
        self._scheduler_callbacks: List[Callable[[Dict[str, Any]], None]] = []
        # subscribe to BIOS EventBus for scheduler ticks if available
        try:
            self._bus = get_event_bus()
            if self._bus is not None:
                # handler will be invoked on scheduler.tick events
                def _on_scheduler_tick(payload: Dict[str, Any]) -> None:
                    try:
                        # collapse into a simple compute request or notify callbacks
                        self._handle_scheduler_event(payload)
                    except Exception:
                        _logger.exception("Scheduler tick handler failed")

                self._bus.subscribe("scheduler.tick", _on_scheduler_tick, priority=0, name="SimCore.scheduler_tick")
        except Exception:
            self._bus = None

    @classmethod
    def get(cls) -> "SimCore":
        if cls._instance is None:
            cls._instance = SimCore()
        return cls._instance

    def start(self, n_particles: int = 128, dt: float = 0.02, use_gpu: Optional[bool] = None) -> None:
        with self._lock:
            if self._running:
                _logger.info("SimCore already running")
                return
            use_gpu_flag = use_gpu if use_gpu is not None else bool(get("sim/use_gpu", False))
            if ParticleSim is None or create_default_sim is None:
                _logger.warning("ParticleSim backend unavailable; using no-op substrate")
                self._sim = None
            else:
                sim = ParticleSim(n_particles=n_particles, dt=dt, use_gpu=use_gpu_flag)
                # if we have an event bus, inject it so the sim can publish sim.tick
                try:
                    if self._bus is not None and hasattr(sim, "__init__"):
                        # re-create with bus if constructor supports it
                        try:
                            sim = ParticleSim(n_particles=n_particles, dt=dt, use_gpu=use_gpu_flag, event_bus=self._bus)
                        except TypeError:
                            # older constructor signature; fallback
                            pass
                except Exception:
                    pass
                self._sim = sim
                sim.start()
            self._running = True
            store("sim/core/status", "running")
            append_telemetry("sim/core", {"action": "start", "n_particles": n_particles})
            _logger.info("SimCore started: particles=%d use_gpu=%s", n_particles, use_gpu_flag)

    def stop(self) -> None:
        with self._lock:
            if not self._running:
                return
            if self._sim is not None:
                try:
                    self._sim.stop()
                except Exception:
                    _logger.exception("Error stopping backend sim")
            self._sim = None
            self._running = False
            store("sim/core/status", "stopped")
            append_telemetry("sim/core", {"action": "stop"})
            _logger.info("SimCore stopped")

    def register_engine(self, engine_id: str, metadata: Dict[str, Any] | None = None, attach_callback: Optional[Callable[[Any], None]] = None) -> None:
        with self._lock:
            if engine_id in self._engines:
                _logger.info("Engine %s already registered; updating", engine_id)
            self._engines[engine_id] = {
                "meta": dict(metadata or {}),
                "attach_cb": attach_callback,
                "last_attach_ts": None,
            }
            store(f"sim/engines/{engine_id}/registered", True)
            append_telemetry("sim/core/engine_registered", {"engine_id": engine_id})

        # ------------------------------------------------------------------
        def register_scheduler_callback(self, fn: Callable[[Dict[str, Any]], None]) -> None:
            """Register a callback that will be invoked when scheduler events arrive.

            The callback receives the published scheduler payload dict.
            """
            with self._lock:
                if callable(fn):
                    self._scheduler_callbacks.append(fn)
                    append_telemetry("sim/core/scheduler_cb", {"registered": True})

        def _handle_scheduler_event(self, payload: Dict[str, Any]) -> None:
            # Default handling: notify registered callbacks and optionally submit a compute request
            with self._lock:
                # notify callbacks
                for cb in list(self._scheduler_callbacks):
                    try:
                        cb(payload)
                    except Exception:
                        _logger.exception("Scheduler callback failed")
                # store an audit of the scheduler tick
                try:
                    store("sim/scheduler/last_tick", payload)
                    append_telemetry("sim/core/scheduler_tick", {"payload_len": len(str(payload))})
                except Exception:
                    _logger.exception("Failed storing scheduler tick")

    def unregister_engine(self, engine_id: str) -> None:
        with self._lock:
            self._engines.pop(engine_id, None)
            store(f"sim/engines/{engine_id}/registered", False)
            append_telemetry("sim/core/engine_unregistered", {"engine_id": engine_id})

    def attach_qubits(self, engine_id: str, qubit_state: Any) -> bool:
        """Allow an engine to attach qubit state to the substrate.

        This will call the engine's attach callback if provided. Returns True
        on success, False otherwise.
        """
        with self._lock:
            info = self._engines.get(engine_id)
            if info is None:
                _logger.warning("attach_qubits: unknown engine %s", engine_id)
                return False
            cb = info.get("attach_cb")
            try:
                if callable(cb):
                    cb(qubit_state)
                info["last_attach_ts"] = time.time()
                store(f"sim/engines/{engine_id}/last_attach_ts", info["last_attach_ts"])
                append_telemetry("sim/core/attach", {"engine_id": engine_id})
                return True
            except Exception:
                _logger.exception("Engine attach callback failed for %s", engine_id)
                return False

    def submit_compute(self, engine_id: str, payload: Dict[str, Any]) -> Dict[str, Any]:
        """Submit a compute request to the substrate.

        This function is intentionally abstract: it stores the request in VSD and
        returns a safe diagnostics dict. Engines that require synchronous callbacks
        should register an attach_callback that receives qubit states.
        """
        with self._lock:
            if engine_id not in self._engines:
                _logger.warning("submit_compute: unknown engine %s", engine_id)
                return {"error": "unknown_engine"}
            # store a compact record for auditing
            ts = time.time()
            store(f"sim/requests/{engine_id}/{int(ts)}", payload)
            append_telemetry("sim/core/submit", {"engine_id": engine_id, "payload_len": len(str(payload))})
            # return lightweight metrics and echo
            return {"status": "accepted", "ts": ts}

    def get_metrics(self) -> Dict[str, Any]:
        with self._lock:
            # collect a few safe metrics from substrate
            if self._sim is None:
                return {"running": self._running, "sim": "none", "engines": list(self._engines.keys())}
            m = {
                "running": self._running,
                "engines": list(self._engines.keys()),
                "particles": getattr(self._sim, "n", None),
                "step": getattr(self._sim, "step", None),
            }
            self._last_metrics = m
            return m


def get_core() -> SimCore:
    return SimCore.get()


if __name__ == "__main__":
    sc = get_core()
    sc.start(n_particles=256)
    try:
        while True:
            time.sleep(1.0)
            print(sc.get_metrics())
    except KeyboardInterrupt:
        sc.stop()
