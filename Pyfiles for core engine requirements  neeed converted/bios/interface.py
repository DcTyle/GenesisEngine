# Path: Quantum Application/bios/interface.py
# ============================================================================
# Version: v5.1 "Unified Imports + Silent Error Purge"
# VirtualMiner / BIOS
# ASCII-ONLY SOURCE FILE
# NOTE: This file intentionally avoids any non-ASCII characters.
# Jarvis ADA v4.7 Hybrid compatible. Unified rule-block tokens A..G.
# ============================================================================

"""
File: interface.py
Purpose:
  Facade that wires a telemetry reader, Monitor, Writer, and Live loop. This
  keeps a simple start/stop surface while allowing headless operation.

Imports:
  Tries to import Monitor, BufferWriter, and LiveDriver from sibling modules.
  If not available yet, provides minimal fallbacks so the file remains runnable
  during staged deployments.

Public API:
  Interface(telemetry_reader: callable read() -> dict)
  .start() / .stop() / .snapshot()
"""

from __future__ import annotations
from typing import Dict, Any, Optional
import time
import math
import logging

# ----------------------------------------------------------------------------
# Local logger
# ----------------------------------------------------------------------------
_logger = logging.getLogger("bios.interface")

# Rule helpers
ASCII_MIN = 32
ASCII_MAX = 126


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def _now() -> float:
    return float(time.time())


def _fetch_rule_manifest() -> Dict[str, Any]:
    return {
        "token_table": {
            "@A": [0.191489, -0.106383],
            "@B": [0.468085, 0.191489],
            "@C": [-0.085106, 0.106383],
            "@D": [0.319149, -0.680851],
            "@E": [0.106383, 0.276596],
            "@F": [-0.148936, 0.468085],
            "@G": [0.063830, -0.425532],
        }
    }


class RuleParams:
    def __init__(self):
        T = _fetch_rule_manifest()["token_table"]

        def get_local(k):
            return T.get(k, [0.0, 0.0])

        self.A = get_local("@A")
        self.B = get_local("@B")
        self.C = get_local("@C")
        self.D = get_local("@D")
        self.E = get_local("@E")
        self.F = get_local("@F")
        self.G = get_local("@G")

    @property
    def ema_alpha(self) -> float:
        return _clamp(0.20 + 0.15 * (self.C[1]), 0.05, 0.35)

    @property
    def headroom(self) -> float:
        return _clamp(0.12 + 0.08 * (self.E[1] - 0.1), 0.05, 0.20)

    @property
    def phase_step(self) -> float:
        return _clamp(0.010 + 0.010 * (self.G[0] + 0.2), 0.005, 0.025)


# ----------------------------------------------------------------------------
# Dynamic imports with fallbacks
# ----------------------------------------------------------------------------

try:
    from VHW.monitor import Monitor
except Exception as exc:
    _logger.warning(
        "bios.interface: Monitor import failed; using fallback Monitor (%s)",
        exc,
        exc_info=True,
    )

    class _EMA:
        def __init__(self, a):
            self.a = a
            self.y = None

        def update(self, x):
            if self.y is None:
                self.y = x
            else:
                self.y = self.a * x + (1.0 - self.a) * self.y
            return self.y

    class Monitor:
        def __init__(self, read_telemetry, util_cap: float = 0.75):
            self.rule = RuleParams()
            self.read = read_telemetry
            self.util_cap = float(util_cap)
            self.ema = _EMA(self.rule.ema_alpha)

        def frame(self) -> Dict[str, Any]:
            d = self.read() or {}
            glob = float(d.get("global_util", 0.0))
            G = self.ema.update(glob)
            target = max(0.0, self.util_cap - self.rule.headroom)
            return {
                "timestamp": d.get("timestamp"),
                "global_util_ema": G,
                "target_util": target,
                "alert": bool(G > target),
            }


try:
    from VHW.writer import BufferWriter
except Exception as exc:
    _logger.warning(
        "bios.interface: BufferWriter import failed; using fallback (%s)",
        exc,
        exc_info=True,
    )

    class BufferWriter:
        def __init__(self, root: str = "/VHW/VD", base: str = "/telemetry"):
            self.root = root
            self.base = base
            self._buf = []

        def start(self):
            return None

        def stop(self):
            return None

        def write(self, line: str):
            self._buf.append(str(line))


try:
    from VHW.live import LiveDriver
except Exception as exc:
    _logger.warning(
        "bios.interface: LiveDriver import failed; using fallback (%s)",
        exc,
        exc_info=True,
    )

    class LiveDriver:
        def __init__(self, monitor, writer, refresh_s: float = 0.5):
            self.monitor = monitor
            self.writer = writer
            self.refresh = float(refresh_s)
            self._stop = False

        def start(self):
            return None

        def stop(self, timeout: float = 2.0):
            return None


# ============================================================================
# Interface Facade
# ============================================================================
class Interface:
    def __init__(self, telemetry_reader=None):
        self.rule = RuleParams()

        if telemetry_reader is None:
            class _SyntheticReader:
                def __init__(self):
                    self.t0 = time.time()

                def _osc(self, k, b, a):
                    t = time.time() - self.t0
                    return _clamp(b + a * math.sin(k * t + 0.13), 0.0, 1.0)

                def read(self) -> Dict[str, Any]:
                    gu = self._osc(0.7, 0.64, 0.18)
                    gm = self._osc(0.5, 0.58, 0.22)
                    bw = self._osc(0.6, 0.52, 0.25)
                    cu = self._osc(0.9, 0.35, 0.15)
                    comp = 0.45 * gu + 0.25 * gm + 0.20 * bw + 0.10 * cu
                    return {
                        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                        "global_util": comp,
                        "gpu_util": gu,
                        "gpu_mem_util": gm,
                        "mem_bw_util": bw,
                        "cpu_util": cu,
                        "util_cap": 0.75,
                        "group_headroom": self.rule.headroom,
                    }

            self.reader = _SyntheticReader()
        else:
            self.reader = telemetry_reader

        self.monitor = Monitor(read_telemetry=self.reader.read, util_cap=0.75)
        self.writer = BufferWriter(root="/VHW/VD", base="/telemetry")
        self.live = LiveDriver(self.monitor, self.writer, refresh_s=0.5)

    def start(self):
        try:
            self.live.start()
        except Exception as exc:
            _logger.error("bios.interface.start failed: %s", exc, exc_info=True)

    def stop(self):
        try:
            self.live.stop()
        except Exception as exc:
            _logger.error("bios.interface.stop failed: %s", exc, exc_info=True)

    def snapshot(self) -> Dict[str, Any]:
        return {
            "t_s": _now(),
            "phase_step": self.rule.phase_step,
            "ema_alpha": self.rule.ema_alpha,
            "headroom": self.rule.headroom,
        }
