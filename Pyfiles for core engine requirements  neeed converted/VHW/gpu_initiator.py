from __future__ import annotations

"""
GPU initiator: keeps a tiny workload active to warm up the GPU context and
exposes a simple API to run the evolve kernel on demand.
"""

from typing import Any
import threading
import time
import math

from VHW.vsd_manager import VSDManager
from VHW.gpu_kernel import is_available as _gpu_ok, evolve_rotate

_thread: threading.Thread | None = None
_stop = threading.Event()


def _bg_warm_loop(vsd: VSDManager, sustain_pct: float) -> None:
    try:
        import numpy as np
        n = 1024
        re = np.ones(n, dtype=np.float32)
        im = np.zeros(n, dtype=np.float32)
        t0 = time.perf_counter()
        period = 1.0  # 1 Hz warm loop
        work_s = max(0.0, min(0.90, float(sustain_pct))) * period
        idle_s = max(0.0, period - work_s)
        while not _stop.is_set():
            t = time.perf_counter() - t0
            theta = 2.0 * math.pi * ((t / period) % 1.0)
            # Keep the GPU context warm if available
            if _gpu_ok():
                evolve_rotate((re, im), theta)
            # Record simple heartbeat in VSD
            try:
                vsd.store("vhw/gpu/last_heartbeat", time.time())
                vsd.store("vhw/gpu/available", bool(_gpu_ok()))
            except Exception:
                pass
            # Simulate target duty cycle
            if work_s > 0:
                time.sleep(work_s)
            if idle_s > 0:
                time.sleep(idle_s)
    except Exception:
        # Never raise from background thread
        pass


def start_gpu_initiator(vsd: VSDManager | None = None, sustain_pct: float = 0.05) -> Any:
    global _thread
    if _thread is not None and _thread.is_alive():
        return _thread
    if vsd is None:
        vsd = VSDManager()
    _stop.clear()
    _thread = threading.Thread(target=_bg_warm_loop, args=(vsd, float(sustain_pct)), daemon=True)
    _thread.start()
    return _thread


def stop_gpu_initiator() -> None:
    global _thread
    _stop.set()
    t = _thread
    _thread = None
    if t is not None:
        try:
            t.join(timeout=1.0)
        except Exception:
            pass
