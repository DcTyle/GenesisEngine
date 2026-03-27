# ============================================================================

# Quantum Application / bios

    # Miner autostart/config handling removed.
            h = hashlib.sha256(tb_str.encode('utf-8', errors='ignore')).hexdigest()

            return h

        except Exception:

            return hashlib.sha256(str(record).encode('utf-8', errors='ignore')).hexdigest()



    def emit(self, record: logging.LogRecord) -> None:

        try:

            # Only process ERROR and above

            if int(record.levelno) < int(logging.ERROR):

                return

            if self.vsd is None:

                return

            net_key = "global"

            # Derive signature for this error/traceback

            sig = self._sig_from_record(record)

            # Keys

            amp_key_sig = f"vsd/phase_amplitude/errors/{sig}"

            amp_key_net = f"vsd/phase_amplitude/{net_key}"

            # Read existing amplitudes

            try:

                a_sig = float(self.vsd.get(amp_key_sig, 0.0) or 0.0)

            except Exception:

                a_sig = 0.0

            try:

                a_net = float(self.vsd.get(amp_key_net, 0.0) or 0.0)

            except Exception:

                a_net = 0.0

            # Apply decay and negative delta

            delta = -0.10

            a_sig = self._clamp((a_sig * self._decay) + delta)

            a_net = self._clamp((a_net * self._decay) + delta)

            # Persist amplitudes

            try:

                self.vsd.store(amp_key_sig, float(a_sig))

                self.vsd.store(amp_key_net, float(a_net))

            except Exception:

                pass

            # Persist last-event metadata for explainability

            try:

                meta = {

                    "logger": str(record.name),

                    "level": str(record.levelname),

                    "delta": float(delta),

                    "amplitude_sig": float(a_sig),

                    "amplitude_net": float(a_net),

                    "message": str(record.getMessage()),

                    "has_exc_info": bool(record.exc_info is not None),

                    "ts": time.time(),

                }

                self.vsd.store(f"vsd/phase_amplitude/errors/{sig}/_last_event", meta)

            except Exception:

                pass

        except Exception:

            # Handler must never raise

            pass



def _install_vsd_error_handler(vsd: Any) -> None:

    """Install the VSDPhaseErrorHandler onto the root logger."""

    try:

        if vsd is None:

            return

        root = logging.getLogger()

        # Avoid duplicate handlers

        for h in list(root.handlers or []):

            if isinstance(h, VSDPhaseErrorHandler):

                return

        root.addHandler(VSDPhaseErrorHandler(vsd=vsd))

    except Exception:

        pass



# ---------------------------------------------------------------------------

# Telemetry Console (optional, no stub fallback)

# ---------------------------------------------------------------------------

try:

    from Control_Center.telemetry_console_live import LiveConsoleRunner

    CONSOLE_OK = True

except Exception as exc:

    _logger.warning(

        "Failed to import LiveConsoleRunner; live console disabled: %s",

        exc,

        exc_info=True,

    )

    CONSOLE_OK = False



# ---------------------------------------------------------------------------

# Telemetry Reader (Monitor first, fallback to VSD)

# ---------------------------------------------------------------------------

def _make_reader(boot_obj: Any):

    if hasattr(boot_obj, "monitor") and hasattr(boot_obj.monitor, "frame"):

        return boot_obj.monitor.frame

    raise RuntimeError("Telemetry reader unavailable: boot_obj.monitor.frame is required")








# ---------------------------------------------------------------------------

# RUN BIOS + RUNTIME

# ---------------------------------------------------------------------------

def run(config: Dict[str, Any] | None = None) -> Dict[str, Any]:

    from bios.boot import start as start_boot

    boot_obj = start_boot()

    # Remove requirement for miner attributes on BIOS boot_obj

    required_attrs = []

    missing = []

    # ASCII-only enforcement for boot attributes

        _logger.warning(
            "Failed to replay boot.complete event: %s",
            exc,
            exc_info=True,
        )

    # Miner logic fully removed from BIOS.

    # Optional Telemetry Console

        "env": env,

        "gpu_initiator": gpu_init,

        "event_bus": _bus,

        "vsd": _vsd,

    }



    # Install terminal-error → negative phase amplitude handler

    try:

        _install_vsd_error_handler(_vsd)

    except Exception:

        _logger.debug("VSDPhaseErrorHandler install failed", exc_info=True)



    # 3-Agent Continuous Integration: CI hooks

    try:

        from bios.scheduler import MurphyWatchdog

        wd = MurphyWatchdog(

            env={

                "vsd": _vsd,

                "failsafe": getattr(boot_obj, "failsafe", None),

                "allocator_ref": getattr(boot_obj, "share_allocator", None),

            },

            failsafe=getattr(boot_obj, "failsafe", None),

            allocator=getattr(boot_obj, "share_allocator", None),

            vsd=_vsd,

        )

        _bus.publish("ci.cycle.start", {"ts": time.time(), "source": "bios.main_runtime"})

        # Simulate CI event triggers

        for event in ["commit", "push", "pull_request"]:

            _bus.publish(f"ci.event.{event}", {"ts": time.time(), "source": "bios.main_runtime"})

            # Neuralis: repo-wide checks

            _bus.publish(f"ci.neuralis.check.{event}", {"ts": time.time(), "status": "pending"})

            # Jarvis: architecture enforcement

            _bus.publish(f"ci.jarvis.validate.{event}", {"ts": time.time(), "status": "pending"})

            # Codex: stage/apply approved changes

            _bus.publish(f"ci.codex.apply.{event}", {"ts": time.time(), "status": "pending"})

        # Murphy Watchdog: monitor and rollback

        _bus.publish("ci.murphy.monitor", {"ts": time.time(), "status": "active"})

        # EventBus: publish CI events to VSD

        _bus.publish("ci.events.published", {"ts": time.time(), "source": "bios.main_runtime"})

    except Exception as exc:

        _logger.warning("MurphyWatchdog unavailable for CI routing: %s", exc)



    # Miner subsystem logic has been removed from BIOS.



    # -----------------------------------------------------------------------

    # CRITICAL FIX:

    # BIOS publishes boot.complete BEFORE engines subscribe, so we replay it.

    # -----------------------------------------------------------------------



    # Publish boot.complete as before

    try:

        _bus.publish(

            "boot.complete",

            {

                "ts": time.time(),

                "source": "bios.main_runtime",

                "replayed": True,

            },

        )

    except Exception as exc:

        _logger.warning(

            "Failed to replay boot.complete event: %s",

            exc,

            exc_info=True,

        )



    # Miner autostart/config handling removed.

    # Load miner config from file (robust absolute path with working-dir fallback)

    try:

        repo_root = Path(__file__).resolve().parent.parent

        cfg_candidates = [

            repo_root / "miner" / "miner_runtime_config.json",

            Path.cwd() / "miner" / "miner_runtime_config.json",

        ]

        cfg_path = None

        for p in cfg_candidates:

            try:

                if p.is_file():

                    cfg_path = p

                    break

            except Exception:

                # Ignore path resolution issues and continue

                pass



        if cfg_path is None:

            # Log both attempted absolute paths for operator clarity

            _logger.critical(

                "[FATAL] miner_runtime_config.json not found. Tried: %s",

                ", ".join(str(p) for p in cfg_candidates),

            )

            raise RuntimeError("[FATAL] BIOS cannot continue without miner config.")



        with open(cfg_path, "r", encoding="utf-8") as f:

            miner_cfg = json.load(f)

        _logger.info("Loaded miner config from %s", cfg_path)

    except Exception as exc:

        miner_cfg = {}

        _logger.critical(f"[FATAL] Failed to load miner_runtime_config.json: {exc}")

        raise RuntimeError("[FATAL] BIOS cannot continue without miner config.")

    # BIOS does not publish subsystem.start for miner; miner starts after BIOS boot flag is set in VSD

    # Persist miner config into VSD so downstream components (FailsafeGovernor)

    # can read `miner/config` and compute per-coin governance during boot.

    try:

        _vsd.store('miner/config', miner_cfg or {})

    except Exception:

        _logger.warning('Failed to persist miner/config into VSD')



    # Seed assigned_difficulty from static config so failsafe governance has a starting point

    try:

        _seed_assigned_difficulty_from_config(miner_cfg, _vsd)

    except Exception as exc:

        _logger.warning("[BIOS] failed to seed assigned_difficulty from config: %s", exc)



    # Hydrate network hashrate per coin from pool stats before governance runs

    def _hydrate_network_hashrate(cfg: Dict[str, Any], vsd: Any, cfg_path: str | None) -> None:

        """Best-effort hydration without miner dependencies (rule: bios only core/VHW).



        In environments where miner.pool_stats is unavailable or disallowed, skip hydration and

        leave network hashrate unset so downstream governance can fall back to adapter/env hints.

        """

        _logger.info("[BIOS] pool stats hydration skipped (core/VHW-only rule)")

        return



    # Optional Telemetry Console

    try:

        if CONSOLE_OK:

            console = LiveConsoleRunner(env, refresh_s=0.5)

            console.start()

            ctx["console"] = console

            _logger.info("Live telemetry console started.")

    except Exception as exc:

        _logger.warning("Live console failed to start: %s", exc, exc_info=True)

        ctx["console"] = None

    # BIOS must not depend on miner modules. Failsafe governance will be initialized

    # by the miner subsystem after BIOS boot. Persisting miner config is sufficient.

    try:

        ctx['failsafe'] = None

        _logger.info("BIOS: deferring FailsafeGovernor initialization to miner startup.")

    except Exception:

        pass



    ctx["started_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    return ctx



# ---------------------------------------------------------------------------

# SHUTDOWN

# ---------------------------------------------------------------------------

def shutdown(ctx: Dict[str, Any]) -> None:

    if not isinstance(ctx, dict):

        return

    try:

        c = ctx.get("console")

        if c:

            c.stop()

    except Exception as exc:

        _logger.warning(

            "Shutdown cleanup failed while stopping console: %s",

            exc,

            exc_info=True,

        )

    try:

        _bus.publish(

            "system.shutdown",

            {

                "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),

                "source": "bios.main_runtime",

            },

        )

    except Exception as exc:

        _logger.warning(

            "Failed to publish system.shutdown event: %s",

            exc,

            exc_info=True,

        )



    # Stop GPU initiator if present

    try:

        gi = ctx.get("gpu_initiator") if isinstance(ctx, dict) else None

        if gi is not None:

            from VHW.gpu_initiator import stop_gpu_initiator

            stop_gpu_initiator()

            # This shutdown message can be verbose during tests; make it DEBUG-only

            _logger.debug("GPU initiator stopped on shutdown")

    except Exception as exc:

        _logger.warning("Failed to stop GPU initiator: %s", exc)

