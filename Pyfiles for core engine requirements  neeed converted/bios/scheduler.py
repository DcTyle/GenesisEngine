# Path: Quantum Application/bios/scheduler.py
# ============================================================================
# Quantum Application / bios
# ASCII-ONLY SOURCE FILE
# File: scheduler.py
# Version: v5 "Unified Imports"
# Jarvis ADA v4.7 Hybrid Ready
# ============================================================================
"""
Purpose
-------
BIOS scheduler for VirtualMiner. Orchestrates the live streaming pipeline and
coordinates lane-level stability using Murphy Watchdog v4.x (trade-aware).
The watchdog supervises stability, triggers targeted pruning on the most-
difficult networks, persists pruned lanes as tokenized float64 state-vectors,
and schedules reconnects per network after cooldown (default 60 s). Adds
trade supervision and stale-order cleanup integrated with the Prediction
Engine.

Phase Matrix (Applied)
----------------------
Phase A  Infrastructure & Import Fixes:
  - Import EventBus from bios.event_bus (global singleton getter).
  - Safe guards for optional modules; ASCII-only logging.
  - BIOS gate via VSD key: system/bios_boot_ok.

Phase B  Operational Runtime Integration:
  - LivePump streaming from Monitor -> LiveTelemetry -> VSD mirror.
  - Configurable refresh and cooldown via ConfigManager.

Phase C  Telemetry & Health Optimization:
  - Emits scheduler.task.* and prediction.archive_complete.
  - Bounded cadence, compact state snapshots to VSD.

Phase E  Failsafe & Resilience Upgrade:
  - Trade-aware Murphy watchdog with auto-cancel of stale orders.
  - Difficulty-ordered pruning and timed reconnects.

Phase G  Jarvis BIOS Boot Automation:
  - Refuses to start until BIOS ready; aligns with boot.complete flow.

Notes
-----
- All source is ASCII-only.
- This file expects these modules to exist:
  * bios.event_bus.get_event_bus()
  * VHW.monitor.Monitor, VHW.live.LiveTelemetry
  * miner.failsafe.FailsafeGovernor
  * prediction_engine.safe_trade_executor.SafeTradeExecutor (preferred) or legacy TradeExecutor
  * miner.crypto_com_api.CryptoComAPI
  * VHW.vsd_manager.VSDManager
"""

from __future__ import annotations
from typing import Dict, Any, Callable, Optional, List, Tuple
import time
import threading
import logging

# ----------------------------------------------------------------------------
# Safe imports (guarded with fallbacks)
# ----------------------------------------------------------------------------

# EventBus (pub/sub) from BIOS package (matrix-aligned)
try:
    from bios.event_bus import get_event_bus  # expected API
except Exception:
    def get_event_bus() -> Any:
        class _NoBus:
            def publish(self, *_a, **_k):  # no-op
                return None
            def subscribe(self, *_a, **_k):  # no-op
                return None
        return _NoBus()

# ConfigManager (core-only rule: no external imports)
class ConfigManager:  # minimal fallback
    _cache: Dict[str, Any] = {}

    @classmethod
    def get(cls, key: str, default: Any = None) -> Any:
        return cls._cache.get(key, default)

    @classmethod
    def set(cls, key: str, value: Any) -> None:
        cls._cache[key] = value

# VSDManager safe import (BIOS gate + storage)
try:
    from VHW.vsd_manager import VSDManager
except Exception:
    class VSDManager:  # minimal fallback mirrors a dict
        def __init__(self) -> None:
            self._m: Dict[str, Any] = {}
            self._lock = threading.RLock()
        def get(self, key: str, default: Any = None) -> Any:
            with self._lock:
                return self._m.get(str(key), default)
        def store(self, key: str, value: Any) -> None:
            with self._lock:
                self._m[str(key)] = value

# Layered application imports (guarded)
try:
    from VHW.monitor import Monitor
except Exception:
    Monitor = None  # will raise in run_forever if missing

try:
    from VHW.live import LiveTelemetry
except Exception:
    LiveTelemetry = None  # will raise in run_forever if missing

# Failsafe/Trading placeholders (core/VHW-only policy)
class FailsafeGovernor:  # placeholder to satisfy type checks without miner import
    pass


class TradeExecutor:
    def place_order(self, *_a, **_k) -> Dict[str, Any]:
        return {"ok": False, "error": "trade_executor_unavailable"}


class CryptoComAPI:
    def get_open_orders(self, _symbol: str) -> List[Dict[str, Any]]:
        return []

    def cancel_order(
        self,
        _symbol: str,
        _side: Optional[str] = None,
        order_id: Optional[str] = None,
    ) -> Dict[str, Any]:
        return {"ok": True, "cancelled": order_id or "na"}

# ----------------------------------------------------------------------------
# Logger setup (UTC formatter, ASCII-only)
# ----------------------------------------------------------------------------
_LOG = logging.getLogger("BIOS.Scheduler")
if not _LOG.handlers:
    _LOG.setLevel(logging.INFO)
    _h = logging.StreamHandler()
    _h.setLevel(logging.INFO)
    _h.setFormatter(logging.Formatter(
        fmt="%(asctime)sZ | %(name)s | %(levelname)s | %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S"
    ))
    # Force UTC timestamps
    logging.Formatter.converter = time.gmtime
    _LOG.addHandler(_h)

# Simloop bridge intentionally disabled to avoid non-core/VHW dependency

# ----------------------------------------------------------------------------
# Utility helpers (ASCII-safe)
# ----------------------------------------------------------------------------
def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

def _now_ts() -> float:
    return float(time.time())

def _utc_now_str() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

def _encode_float_to_char(x: float) -> str:
    y = int((float(x) + 1.0) * 47) + 32
    y = max(32, min(126, y))
    return chr(y)

def _floatmap_to_ascii(vec: List[float]) -> str:
    return "".join(_encode_float_to_char(v) for v in vec)

# ----------------------------------------------------------------------------
# Fallback in-memory VSD (used if env does not pass one)
# ----------------------------------------------------------------------------
class _MapVSD:
    def __init__(self) -> None:
        self._m: Dict[str, Any] = {}
        self._lock = threading.RLock()
    def get(self, key: str, default: Any = None) -> Any:
        with self._lock:
            return self._m.get(str(key), default)
    def store(self, key: str, value: Any) -> None:
        with self._lock:
            self._m[str(key)] = value

# ----------------------------------------------------------------------------
# BIOS gating helpers
# ----------------------------------------------------------------------------
def _bios_ready(vsd: Any) -> bool:
    try:
        return bool(vsd.get("system/bios_boot_ok", False))
    except Exception:
        return False

# ----------------------------------------------------------------------------
# Module state
# ----------------------------------------------------------------------------
class _Status:
    def __init__(self):
        self.started: Optional[str] = None
        self.iterations: int = 0
        self.last_plan: Dict[str, Any] = {}
        self.last_frame: Dict[str, Any] = {}
        self.running: bool = False
        self.last_prune_note: str = ""
        self.last_reconnect_note: str = ""
        self.lanes_active: int = 0

_STATUS = _Status()
_STOP = False
_THREAD: Optional[threading.Thread] = None

# ----------------------------------------------------------------------------
# Murphy Watchdog v4.x (Trade-Aware)
# ----------------------------------------------------------------------------
class MurphyWatchdog:
    """
    Supervises stability, trade execution safety, and reconnects.
    Extends v3.x with live trading control and order cleanup.
    Adds EventBus hooks and ConfigManager tuning for cooldown.
    """

    def __init__(self, env: Dict[str, Any], failsafe: Any,
                 allocator: Any, vsd: Any, cooldown_s: float = 60.0):
        self.env = env
        self.failsafe = failsafe
        self.allocator = allocator
        self.vsd = vsd
        # allow config override
        cfg_cool = ConfigManager.get("scheduler.cooldown_s", None)
        self.cooldown_s = float(cfg_cool if cfg_cool is not None else cooldown_s)

        self._overload_ticks = 0
        self._last_frame_ts = _now_ts()
        self._last_plan_ts = _now_ts()
        self._lock = threading.RLock()
        self._stop = False
        self._thread: Optional[threading.Thread] = None
        self.alloc = env.get("allocator_ref", allocator)
        self.planner = env.get("planner_ref")
        self.system_cap = 0.20
        self.last_prop_ts = 0.0

        # trade supervision fields
        self.trade_exec = TradeExecutor()
        self.trade_api = CryptoComAPI()
        self.daily_order_limit = int(ConfigManager.get("trade.daily_order_limit", 100))
        self.orders_sent = 0
        self.last_order_ts = 0.0
        self.order_cooldown_s = float(ConfigManager.get("trade.order_cooldown_s", 30.0))

        # bus
        self.bus = get_event_bus()

    # ----------------------------- Ratio propagation ------------------------
    def _propagate_system_cap(self) -> None:
        """Clamp and push ratio limits into allocator/planner only when changed."""
        try:
            if not self.vsd:
                return
            cfg_cap = float(self.vsd.get("config/system_share_cap", 0.20))
            new_system_cap = _clamp(cfg_cap, 0.0, 0.20)
            nets = self.vsd.get("config/network_caps", {}) or {}
            changed = False

            if abs(new_system_cap - getattr(self, "system_cap", 0.20)) > 1e-9:
                self.system_cap = new_system_cap
                self.vsd.store("config/system_share_cap", self.system_cap)
                changed = True

            local_nets = dict(nets)
            for net, val in list(local_nets.items()):
                fv = float(val)
                if fv > self.system_cap:
                    local_nets[net] = self.system_cap
                    changed = True

            if changed:
                self.vsd.store("config/network_caps", local_nets)
                if getattr(self, "alloc", None):
                    setattr(self.alloc, "system_share_cap", self.system_cap)
                    if hasattr(self.alloc, "vsd"):
                        self.alloc.vsd.store("config/system_share_cap", self.system_cap)
                    if hasattr(self.alloc, "network_caps"):
                        try:
                            self.alloc.network_caps = dict(local_nets)
                        except Exception as e:
                            _LOG.warning("Failed to update allocator network_caps: %s", e)
                if getattr(self, "planner", None):
                    setattr(self.planner, "system_share_cap", self.system_cap)
                # store change marker
                try:
                    self.vsd.store("murphy/last_cap_push", {
                        "ts": _utc_now_str(),
                        "system_cap": self.system_cap,
                        "nets": local_nets
                    })
                except Exception as e:
                    _LOG.warning("Failed to store cap push marker: %s", e)
        except Exception as e:
            _LOG.error("ratio propagation error: %s", e)

    # ----------------------------- Main loop --------------------------------
    def _loop(self) -> None:
        """Continuous background supervision loop with trade cleanup."""
        _LOG.info("MurphyWatchdog loop starting...")
        while not self._stop:
            try:
                # periodic cap propagation
                if time.time() - getattr(self, "last_prop_ts", 0.0) > 5.0:
                    self._propagate_system_cap()
                    self.last_prop_ts = time.time()
            except Exception as e:
                _LOG.warning("Cap propagation failed: %s", e)

            try:
                with self._lock:
                    overload = (self._overload_ticks >= 3)

                if overload:
                    self._prune_until_stable()

                self._try_reconnect_networks()

                # perform stale-order cleanup
                self.cleanup_open_orders(max_age_s=float(
                    ConfigManager.get("trade.stale_order_max_age_s", 300.0)
                ))

                try:
                    self.vsd.store("murphy/health", {
                        "ts": _utc_now_str(),
                        "overload_ticks": int(self._overload_ticks),
                        "last_frame_age_s": max(0.0, _now_ts() - self._last_frame_ts),
                        "last_plan_age_s": max(0.0, _now_ts() - self._last_plan_ts)
                    })
                except Exception as e:
                    _LOG.warning("Failed to store Murphy health: %s", e)

                # Density-aware miner governance (decoupled via EventBus + VSD)
                try:
                    self._govern_miner_density()
                except Exception as e:
                    _LOG.warning("Miner density governance failed: %s", e)

            except Exception:
                self._store_alert("murphy_loop_error", {})

            time.sleep(1.0)

        _LOG.info("MurphyWatchdog loop stopped.")

    # ----------------------------- Public controls --------------------------
    def start(self) -> None:
        if self._thread:
            return
        self._stop = False
        self._thread = threading.Thread(target=self._loop,
                                        daemon=True,
                                        name="murphy_watchdog_v4")
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        if not self._thread:
            return
        self._stop = True
        self._thread.join(timeout=timeout)
        self._thread = None

    def update_frame(self, frame: Dict[str, Any]) -> None:
        with self._lock:
            self._last_frame_ts = _now_ts()
            try:
                gu = float(frame.get("global_util", 0.0))
                cap = float(frame.get("util_cap", 0.75))
                head = float(frame.get("group_headroom", 0.10))
                target = max(0.0, cap - head)
                if gu > (target + 0.05):
                    self._overload_ticks += 1
                else:
                    self._overload_ticks = max(0, self._overload_ticks - 1)
            except Exception:
                self._overload_ticks += 1

    def update_plan(self) -> None:
        with self._lock:
            self._last_plan_ts = _now_ts()

    # ----------------------------- Trade helpers ----------------------------
    def approve_trade(self, signal: Dict[str, Any]) -> bool:
        """Return True if trade allowed by limit and cooldown."""
        now = _now_ts()
        if self.orders_sent >= self.daily_order_limit:
            return False
        if now - self.last_order_ts < self.order_cooldown_s:
            return False
        conf = float(signal.get("avg_confidence", 0.0))
        if conf < 0.99:
            return False
        self.last_order_ts = now
        self.orders_sent += 1
        return True

    def cleanup_open_orders(self, max_age_s: float = 300.0) -> None:
        """Cancel open orders older than max_age_s seconds."""
        try:
            now = _now_ts()
            assets = self.vsd.get("telemetry/predictions/latest", [])
            if not isinstance(assets, list):
                return
            for sig in assets:
                symbol = sig.get("symbol")
                if not symbol:
                    continue
                open_orders = self.trade_api.get_open_orders(symbol)
                for order in open_orders:
                    age = max(0.0, now - float(order.get("create_time", 0)))
                    if age > max_age_s:
                        oid = order.get("order_id", "")
                        self.trade_api.cancel_order(symbol, order_id=oid)
                        self._store_alert("auto_cancel_order",
                                          {"symbol": symbol, "order_id": oid})
        except Exception as e:
            _LOG.error("Order cleanup failed: %s", e, exc_info=True)

    # ----------------------------- Miner density governance ----------------
    def _govern_miner_density(self) -> None:
        """Publish miner.control intents based on share rates and density hints.

        No miner imports; uses VSD keys written by miner and publishes EventBus intents:
          - miner.control.scale_lanes { coin, delta, ts }
          - miner.control.batch_hint { coin, hint, ttl_sec, ts }
        """
        try:
            coins = self.vsd.get("config/miner_networks", [
                "BTC","ETC","RVN","ERG","KAS","BCH","NEXA","XNA","ETHW","CLORE"
            ])
            target_pct = float(self.vsd.get("config/miner_density_target_pct", 0.02))
            now = int(time.time())
            for coin in coins:
                sr = self.vsd.get(f"miner/runtime/share_rate/{coin}", {}) or {}
                submitted = float(sr.get("submitted_hs", 0.0))
                accepted = float(sr.get("accepted_hs", 0.0))
                acc_rate = (accepted / submitted) if submitted > 0 else 0.0
                density_hint = float(self.vsd.get(f"miner/runtime/network_density/{coin}", 0.0) or 0.0)
                # lane scaling decision
                delta = 0
                if density_hint < target_pct:
                    delta = +1
                if acc_rate < 0.25:
                    delta = max(delta, +1)
                elif acc_rate > 0.80 and density_hint > target_pct * 1.25:
                    delta = -1
                if delta != 0:
                    try:
                        self.bus.publish("miner.control.scale_lanes", {"coin": coin, "delta": int(delta), "ts": now})
                        self.vsd.store(f"murphy/last_lane_scale/{coin}", {"delta": int(delta), "ts": _utc_now_str()})
                    except Exception:
                        pass
                # batch hint adjustment
                hint = int(self.vsd.get(f"miner/runtime/nonce_batch_hint/{coin}", 256) or 256)
                if acc_rate < 0.25:
                    hint = 512
                elif acc_rate > 0.85:
                    hint = 128
                try:
                    self.bus.publish("miner.control.batch_hint", {"coin": coin, "hint": int(hint), "ttl_sec": 10, "ts": now})
                    self.vsd.store(f"murphy/last_batch_hint/{coin}", {"hint": int(hint), "ts": _utc_now_str()})
                except Exception:
                    pass
        except Exception as e:
            _LOG.warning("govern_miner_density error: %s", e)

    # ----------------------------- Lane snapshotting ------------------------
    def _store_alert(self, note: str, data: Dict[str, Any]) -> None:
        try:
            rec = {"ts": _utc_now_str(), "note": str(note), "data": dict(data or {})}
            alerts = self.vsd.get("murphy/alerts", [])
            if isinstance(alerts, list):
                alerts.append(rec)
                if len(alerts) > 200:
                    alerts = alerts[-200:]
                self.vsd.store("murphy/alerts", alerts)
        except Exception as e:
            _LOG.warning("Failed to store alert: %s", e)

    def _persist_lane_snapshot(self, lane_id: str, lane_state: Dict[str, Any]) -> None:
        try:
            vec = lane_state.get("vector", [])
            if not isinstance(vec, list) or not vec:
                return
            vf = [_clamp(float(x), -1.0, 1.0) for x in vec]
            ascii_tok = _floatmap_to_ascii(vf)
            header = {
                "type": "json_statevector_v2",
                "encoding": "ascii_floatmap_v1",
                "timestamp": _utc_now_str()
            }
            self.vsd.store(f"lanes/snapshots/{lane_id}/last_vector", {
                "header": header,
                "vector_ascii": ascii_tok
            })
        except Exception as e:
            _LOG.warning("Failed to persist lane snapshot for %s: %s", lane_id, e)

    # ----------------------------- Difficulty ordering ----------------------
    def _difficulty_order(self) -> List[Tuple[str, float]]:
        score: Dict[str, float] = {}
        try:
            table = getattr(self.allocator, "difficulty_table", {})
            if isinstance(table, dict) and table:
                for k, v in table.items():
                    score[str(k).upper()] = float(v)
        except Exception:
            score = {}

        if not score:
            try:
                index = self.vsd.get("telemetry/metrics/index", [])
                if isinstance(index, list):
                    for net in index:
                        cur = self.vsd.get(f"telemetry/metrics/{net}/current", {})
                        bt = float(cur.get("block_time_s", 0.0)) or 1.0
                        score[str(net).upper()] = 1.0 / bt
            except Exception as e:
                _LOG.warning("Failed to calculate difficulty scores from telemetry: %s", e)

        items = list(score.items())
        items.sort(key=lambda kv: kv[1], reverse=True)
        return items

    # ----------------------------- Pruning and reconnect --------------------
    def _prune_until_stable(self) -> None:
        try:
            ordered = self._difficulty_order()
            if not ordered:
                self._store_alert("no_difficulty_info", {})
                return
            frame = self.env.get("telemetry_reader", lambda: {})() or {}
            cap = float(frame.get("util_cap", 0.75))
            head = float(frame.get("group_headroom", 0.10))
            target = max(0.0, cap - head)
            cur = float(frame.get("global_util", 0.0))
            for net, _score in ordered:
                try:
                    self.failsafe.prune_network_first(net, target, cur)
                    _STATUS.last_prune_note = f"prune_network_first:{net}"
                    self._store_alert("prune_network_first", {"network": net})
                except Exception as e:
                    _LOG.warning("Failed to prune network %s: %s", net, e)
                    continue
                try:
                    lanes = getattr(self.allocator, "list_lanes_by_network", lambda n: [])(net) or []
                    for lane in lanes:
                        if not isinstance(lane, dict):
                            continue
                        if not lane.get("active"):
                            lane_id = str(lane.get("lane_id", ""))
                            state = getattr(self.allocator, "read_lane_state", lambda i: {})(lane_id) or {}
                            self._persist_lane_snapshot(lane_id, state)
                            self.vsd.store(f"lanes/state/{lane_id}", state)
                except Exception as e:
                    _LOG.warning("Failed to snapshot lanes for network %s: %s", net, e)
                try:
                    self.vsd.store(f"lanes/reconnect/{net}/next_ts", _now_ts() + self.cooldown_s)
                except Exception as e:
                    _LOG.warning("Failed to store reconnect timestamp for %s: %s", net, e)
                frame = self.env.get("telemetry_reader", lambda: {})() or {}
                cur = float(frame.get("global_util", 0.0))
                if cur <= (target + 0.02):
                    break
        except Exception:
            self._store_alert("prune_until_stable_error", {})

    def _try_reconnect_networks(self) -> None:
        try:
            index = self.vsd.get("telemetry/metrics/index", [])
            if not isinstance(index, list):
                return
            now = _now_ts()
            for net in index:
                ts = float(self.vsd.get(f"lanes/reconnect/{net}/next_ts", 0.0) or 0.0)
                if ts > 0.0 and now >= ts:
                    try:
                        self.failsafe.reconnect_network(str(net))
                        self.vsd.store(f"lanes/reconnect/{net}/next_ts", 0.0)
                        _STATUS.last_reconnect_note = f"reconnect:{net}"
                        self._store_alert("reconnect_network", {"network": net})
                    except Exception as e:
                        _LOG.warning("Failed to reconnect network %s: %s", net, e)
        except Exception as e:
            _LOG.error("Network reconnect process failed: %s", e, exc_info=True)


# ----------------------------------------------------------------------------
# LivePump (Monitor -> LiveTelemetry -> VSD mirror)
# ----------------------------------------------------------------------------
class _LivePump:
    def __init__(self, vsd: Any, live_tick: Callable[[], Dict[str, Any]],
                 refresh_s: float = 0.5) -> None:
        self.vsd = vsd
        self.live_tick = live_tick
        self.refresh_s = float(max(0.05, refresh_s))
        self._thr: Optional[threading.Thread] = None
        self._stop = False

    def start(self) -> None:
        if self._thr:
            return
        self._stop = False
        self._thr = threading.Thread(target=self._loop,
                                     daemon=True,
                                     name="bios_live_pump")
        self._thr.start()

    def stop(self, timeout: float = 2.0) -> None:
        if not self._thr:
            return
        self._stop = True
        self._thr.join(timeout=timeout)
        self._thr = None

    def _loop(self) -> None:
        while not self._stop:
            try:
                fr = self.live_tick() or {}
                self.vsd.store("telemetry/live/current", fr)
            except Exception as e:
                _LOG.warning("Failed to store live telemetry: %s", e)
            time.sleep(self.refresh_s)


# ----------------------------------------------------------------------------
# EventBus wiring for scheduler
# ----------------------------------------------------------------------------
def _wire_eventbus(bus: Any, vsd: Any) -> None:
    """
    Subscribe to events required by the scheduler. Minimal handlers only.
    """
    def _on_prediction_daily_rollup(payload: Dict[str, Any]) -> None:
        try:
            # payload should contain {date, digest, frames, errors}
            vsd.store("prediction/daily_rollup/last", {
                "ts": _utc_now_str(),
                "payload": dict(payload or {})
            })
        except Exception as e:
            _LOG.warning("Failed to store daily rollup: %s", e)

    try:
        bus.subscribe("prediction.daily_rollup", _on_prediction_daily_rollup)
    except Exception as e:
        _LOG.warning("Failed to subscribe to daily_rollup events: %s", e)


# ----------------------------------------------------------------------------
# Scheduler runtime (no writer)
# ----------------------------------------------------------------------------
def run_forever(env: Dict[str, Any],
                allocator_factory: Callable[[Dict[str, Any]], Any],
                refresh_s: float = 0.5) -> None:
    """
    Start the BIOS scheduler and associated background loops. This function
    blocks by spawning a background thread which maintains the control loop.
    """
    global _STOP, _THREAD

    if _THREAD:
        _LOG.info("scheduler already running; ignoring duplicate start")
        return

    vsd = env.get("vsd")
    if vsd is None:
        vsd = _MapVSD()
        env["vsd"] = vsd

    # BIOS gate
    if not _bios_ready(vsd):
        _LOG.error("BIOS not ready (system/bios_boot_ok is False); refusing to start scheduler")
        raise RuntimeError("bios_not_ready")

    telemetry_reader = env.get("telemetry_reader", None)
    if not callable(telemetry_reader):
        _LOG.error("env['telemetry_reader'] must be callable")
        raise RuntimeError("env['telemetry_reader'] must be callable")

    if Monitor is None or LiveTelemetry is None:
        _LOG.error("Monitor and LiveTelemetry are required")
        raise RuntimeError("monitor_or_live_missing")

    # Configurable refresh cadence
    cfg_refresh = ConfigManager.get("scheduler.refresh_s", None)
    if cfg_refresh is not None:
        refresh_s = float(cfg_refresh)
    refresh_s = max(0.05, float(refresh_s))

    # Build monitor pipeline
    monitor = Monitor(read_telemetry=telemetry_reader, util_cap=0.75)
    live = LiveTelemetry(monitor.frame)
    pump = _LivePump(vsd, live.tick, refresh_s=refresh_s)
    pump.start()
    _LOG.info("LivePump started with refresh_s=%.3f", refresh_s)

    # Allocator and failsafe
    alloc = allocator_factory(env)
    failsafe = env.get("failsafe", None)
    if failsafe is None or FailsafeGovernor is None:
        _LOG.error("env['failsafe'] must be provided (FailsafeGovernor)")
        raise RuntimeError("failsafe_missing")

    murphy = MurphyWatchdog(env=env,
                            failsafe=failsafe,
                            allocator=alloc,
                            vsd=vsd,
                            cooldown_s=float(ConfigManager.get("scheduler.cooldown_s", 60.0)))

    # Wire lane state callback if available
    if hasattr(alloc, "read_lane_state"):
        try:
            failsafe.register_lane_state_fetch(getattr(alloc, "read_lane_state"))
        except Exception as e:
            _LOG.warning("Failed to register lane state get callback: %s", e)

    # Event bus wiring
    bus = get_event_bus()
    _wire_eventbus(bus, vsd)

    murphy.start()
    _STOP = False
    _STATUS.started = _utc_now_str()
    _STATUS.running = True

    _LOG.info("Scheduler runtime entered; started at %s", _STATUS.started)

    # Publish a scheduler start task event
    try:
        bus.publish("scheduler.task.start", {
            "ts": _STATUS.started,
            "refresh_s": refresh_s
        })
    except Exception as e:
        _LOG.warning("Failed to publish scheduler start event: %s", e)

    # ------------------------------ Loop -----------------------------------
    def _loop():
        cadence_div = int(max(4, 2.0 / max(0.05, refresh_s)))
        while not _STOP:
            try:
                fr = monitor.frame()
                aligned = {
                    "timestamp": fr.get("timestamp", _utc_now_str()),
                    "global_util": float(fr.get("global_util", 0.0)),
                    "gpu_util": float(fr.get("gpu_util", 0.0)),
                    "gpu_mem_util": float(fr.get("gpu_mem_util", 0.0)),
                    "mem_bw_util": float(fr.get("mem_bw_util", 0.0)),
                    "cpu_util": float(fr.get("cpu_util", 0.0)),
                    "util_cap": float(fr.get("util_cap", 0.75)),
                    "group_headroom": float(fr.get("group_headroom", 0.10)),
                    "target_util": float(fr.get("target_util", 0.65)),
                    "alert": bool(fr.get("alert", False)),
                }
                _STATUS.last_frame = aligned
                murphy.update_frame(aligned)

                if _STATUS.iterations % cadence_div == 0:
                    try:
                        if hasattr(alloc, "compute_plan"):
                            plan_dict = alloc.compute_plan()
                        else:
                            plan_dict = {"ts": _utc_now_str(),
                                         "note": "allocator has no compute_plan"}
                        _STATUS.last_plan = plan_dict
                        murphy.update_plan()
                        vsd.store("telemetry/plan/current", plan_dict)
                    except Exception as e:
                        _LOG.warning("Failed to compute or store plan: %s", e)

                try:
                    if hasattr(alloc, "list_active"):
                        _STATUS.lanes_active = len(alloc.list_active() or [])
                except Exception as e:
                    _LOG.warning("Failed to get active lanes count: %s", e)

                # Optional daily rollup trigger sample (publish a task intent)
                if _STATUS.iterations % (cadence_div * 60) == 0:
                    try:
                        bus.publish("scheduler.task.rollup", {
                            "ts": _utc_now_str(),
                            "kind": "daily",
                            "hint": "prediction archival request"
                        })
                    except Exception as e:
                        _LOG.warning("Failed to publish rollup task: %s", e)

                _STATUS.iterations += 1

            except Exception as exc:
                _LOG.error("scheduler loop error: %s", exc)

            time.sleep(refresh_s)

        # Cleanup on stop
        try:
            pump.stop(timeout=2.0)
        except Exception as e:
            _LOG.warning("Failed to stop live pump: %s", e)
        try:
            murphy.stop(timeout=2.0)
        except Exception as e:
            _LOG.warning("Failed to stop Murphy watchdog: %s", e)
        _STATUS.running = False

        # Signal archive completion if a rollup was pending (best-effort)
        try:
            bus.publish("prediction.archive_complete", {
                "ts": _utc_now_str(),
                "by": "bios.scheduler",
                "note": "scheduler shutdown"
            })
        except Exception as e:
            _LOG.warning("Failed to publish archive complete event: %s", e)

        _LOG.info("Scheduler runtime exited.")

    # Spawn thread
    _THREAD = threading.Thread(target=_loop,
                               daemon=True,
                               name="bios_scheduler")
    _THREAD.start()

# ----------------------------------------------------------------------------
# Control functions
# ----------------------------------------------------------------------------
def stop(timeout: float = 2.0) -> None:
    """
    Stop the scheduler and background threads gracefully.
    """
    global _STOP, _THREAD
    if not _THREAD:
        return
    _STOP = True
    try:
        _THREAD.join(timeout=timeout)
    except Exception as e:
        _LOG.warning("Failed to join scheduler thread: %s", e)
    _THREAD = None
    _STATUS.running = False
    _LOG.info("scheduler stopped")

def status() -> Dict[str, Any]:
    """
    Return a structured snapshot of current scheduler state.
    """
    try:
        return {
            "started": _STATUS.started,
            "iterations": _STATUS.iterations,
            "running": _STATUS.running,
            "lanes_active": _STATUS.lanes_active,
            "last_plan_keys": list((_STATUS.last_plan or {}).keys()),
            "last_frame_keys": list((_STATUS.last_frame or {}).keys()),
            "last_prune_note": _STATUS.last_prune_note,
            "last_reconnect_note": _STATUS.last_reconnect_note,
            "timestamp": _utc_now_str(),
        }
    except Exception as exc:
        return {"error": str(exc)}

# ----------------------------------------------------------------------------
# Diagnostics helpers
# ----------------------------------------------------------------------------
def dump_alerts(vsd: Any) -> List[Dict[str, Any]]:
    """
    Retrieve the latest Murphy alerts for inspection (last 50).
    """
    try:
        alerts = vsd.get("murphy/alerts", [])
        if isinstance(alerts, list):
            return alerts[-50:]
    except Exception as e:
        _LOG.warning("Failed to get alerts: %s", e)
    return []

def summarize_health(vsd: Any) -> Dict[str, Any]:
    """
    Return condensed health statistics for BIOS diagnostics.
    """
    try:
        h = vsd.get("murphy/health", {})
        return {
            "timestamp": h.get("ts"),
            "overload_ticks": h.get("overload_ticks"),
            "last_frame_age_s": h.get("last_frame_age_s"),
            "last_plan_age_s": h.get("last_plan_age_s"),
        }
    except Exception as e:
        _LOG.warning("Failed to get health summary: %s", e)
        return {}

# ----------------------------------------------------------------------------
# Extended hooks (optional)
# ----------------------------------------------------------------------------
def get_system_cap(vsd: Any) -> float:
    """
    Helper for reading the current system share cap.
    """
    try:
        val = float(vsd.get("config/system_share_cap", 0.20))
        return _clamp(val, 0.0, 0.20)
    except Exception as e:
        _LOG.warning("Failed to get system cap, using default: %s", e)
        return 0.20

def set_system_cap(vsd: Any, value: float) -> None:
    """
    Helper for updating the system share cap value.
    """
    try:
        val = _clamp(float(value), 0.0, 0.20)
        vsd.store("config/system_share_cap", val)
    except Exception as e:
        _LOG.warning("Failed to set system cap: %s", e)

def get_network_caps(vsd: Any) -> Dict[str, float]:
    """
    Return a copy of the network share caps dictionary.
    """
    try:
        nets = vsd.get("config/network_caps", {}) or {}
        return {k: float(v) for k, v in nets.items()}
    except Exception as e:
        _LOG.warning("Failed to get network caps: %s", e)
        return {}

def update_network_cap(vsd: Any, network: str, value: float) -> None:
    """
    Update a single network cap and push to storage.
    """
    try:
        nets = vsd.get("config/network_caps", {}) or {}
        nets[str(network)] = _clamp(float(value), 0.0, 0.20)
        vsd.store("config/network_caps", nets)
    except Exception as e:
        _LOG.warning("Failed to update network cap for %s: %s", network, e)

# ----------------------------------------------------------------------------
# Manual trigger utilities
# ----------------------------------------------------------------------------
def force_prune(murphy: MurphyWatchdog) -> None:
    """
    Trigger manual pruning sequence immediately.
    """
    try:
        murphy._prune_until_stable()
        _LOG.info("manual prune triggered")
    except Exception as exc:
        _LOG.error("manual prune failed: %s", exc)

def force_reconnect_all(murphy: MurphyWatchdog) -> None:
    """
    Force reconnection attempts for all networks.
    """
    try:
        murphy._try_reconnect_networks()
        _LOG.info("manual reconnect triggered")
    except Exception as exc:
        _LOG.error("manual reconnect failed: %s", exc)

# ----------------------------------------------------------------------------
# Inline self-test section (kept minimal, ASCII-only)
# ----------------------------------------------------------------------------
if __name__ == "__main__":
    _LOG.info("running basic BIOS scheduler self-test")
    try:
        dummy_vsd = _MapVSD()
        # gate BIOS for self-test
        dummy_vsd.store("system/bios_boot_ok", True)

        # a very small dummy failsafe for test only
        class _DummyFailsafe:
            def __init__(self, allocator=None, vsd=None) -> None:
                self.allocator = allocator
                self.vsd = vsd
            def register_lane_state_fetch(self, _f): pass
            def prune_network_first(self, _n, _t, _c): pass
            def reconnect_network(self, _n): pass

        dummy_env: Dict[str, Any] = {
            "telemetry_reader": lambda: {
                "timestamp": _utc_now_str(),
                "global_util": 0.5,
                "gpu_util": 0.5,
                "gpu_mem_util": 0.5,
                "mem_bw_util": 0.5,
                "cpu_util": 0.5,
                "util_cap": 0.75,
                "group_headroom": 0.10,
                "target_util": 0.65,
                "alert": False,
            },
            "vsd": dummy_vsd,
            "failsafe": _DummyFailsafe(allocator=None, vsd=dummy_vsd),
        }

        def dummy_allocator_factory(env: Dict[str, Any]) -> Any:
            class DummyAlloc:
                def compute_plan(self): return {"ts": _utc_now_str()}
                def list_active(self): return [{"lane": 1}]
                difficulty_table = {"ETC": 2.5, "RVN": 3.0}
                def read_lane_state(self, lane_id: str): return {"vector": [0.0, 0.1, -0.1]}
            return DummyAlloc()

        run_forever(dummy_env, dummy_allocator_factory, refresh_s=0.2)
        time.sleep(2.0)
        _LOG.info("status: %s", status())
        stop()
        _LOG.info("alerts: %s", dump_alerts(dummy_vsd))
        _LOG.info("health: %s", summarize_health(dummy_vsd))
    except Exception as e:
        _LOG.error("self-test error: %s", e)

# ============================================================================
# End of bios/scheduler.py
# ============================================================================