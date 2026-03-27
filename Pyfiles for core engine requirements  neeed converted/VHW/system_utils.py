from __future__ import annotations

from typing import Any, Dict
import time
import os
import shutil
import subprocess


def clamp(x, lo, hi):
    try:
        x = float(x)
        lo = float(lo)
        hi = float(hi)
    except Exception:
        pass
    if lo > hi:
        lo, hi = hi, lo
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x


try:
    import psutil  # type: ignore
    _PSUTIL_OK = True
except Exception:
    psutil = None  # type: ignore
    _PSUTIL_OK = False


def _cpu_stats() -> Dict[str, Any]:
    try:
        if _PSUTIL_OK and psutil is not None:
            phys = psutil.cpu_count(logical=False) or os.cpu_count() or 1
            threads = psutil.cpu_count(logical=True) or phys
            util = float(psutil.cpu_percent(interval=0.05) or 0.0)
        else:
            phys = os.cpu_count() or 1
            threads = phys
            util = 0.0
        return {"count": int(phys or 1), "threads": int(threads or phys or 1), "util": float(util)}
    except Exception:
        return {"count": int(os.cpu_count() or 1), "threads": int(os.cpu_count() or 1), "util": 0.0}


def _mem_stats() -> Dict[str, Any]:
    try:
        if _PSUTIL_OK and psutil is not None:
            vm = psutil.virtual_memory()
            total_gb = float(vm.total) / (1024**3)
            used_gb = float(vm.used) / (1024**3)
            util = float(vm.percent)
        else:
            total_gb = 0.0
            used_gb = 0.0
            util = 0.0
        return {"total_gb": float(total_gb), "used_gb": float(used_gb), "util": float(util)}
    except Exception:
        return {"total_gb": 0.0, "used_gb": 0.0, "util": 0.0}


def _gpu_stats() -> Dict[str, Any]:
    """Best-effort GPU stats via nvidia-smi if available; otherwise zeros.

    Works across CUDA versions as long as the NVIDIA driver exposes nvidia-smi.
    """
    try:
        smi = shutil.which("nvidia-smi")
        if not smi:
            raise FileNotFoundError("nvidia-smi not found")
        # Query first GPU; return noheader, no units for simple parsing
        # Fields: name, memory.total, memory.used, utilization.gpu, temperature.gpu
        cmd = [
            smi,
            "--query-gpu=name,memory.total,memory.used,utilization.gpu,temperature.gpu",
            "--format=csv,noheader,nounits",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=1.5)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or "nvidia-smi returned error")
        line = (proc.stdout or "").strip().splitlines()[0]
        parts = [p.strip() for p in line.split(",")]
        name = parts[0] if len(parts) > 0 else "NVIDIA GPU"
        mem_total = float(parts[1]) if len(parts) > 1 else 0.0
        mem_used = float(parts[2]) if len(parts) > 2 else 0.0
        util = float(parts[3]) if len(parts) > 3 else 0.0
        temp = float(parts[4]) if len(parts) > 4 else 0.0
        return {
            "model": name,
            "memory_total_mb": mem_total,
            "memory_used_mb": mem_used,
            "util": util,
            "temperature_c": temp,
        }
    except Exception:
        return {
            "model": "N/A",
            "memory_total_mb": 0.0,
            "memory_used_mb": 0.0,
            "util": 0.0,
            "temperature_c": 0.0,
        }


def _bandwidth_stats() -> Dict[str, Any]:
    try:
        return {"pcie_gbps": 0.0, "memory_bw_gbps": 0.0}
    except Exception:
        return {"pcie_gbps": 0.0, "memory_bw_gbps": 0.0}


def device_snapshot() -> Dict[str, Any]:
    """Return a unified point-in-time hardware snapshot.

    Schema expected by bios.selfcheck:
    {
      timestamp: float,
      cpu: {count:int, threads:int, util:float},
      gpu: {model:str, memory_total_mb:float, memory_used_mb:float, util:float, temperature_c:float},
      memory: {total_gb:float, used_gb:float, util:float},
      bandwidth: {pcie_gbps:float, memory_bw_gbps:float}
    }
    """
    try:
        snap = {
            "timestamp": time.time(),
            "cpu": _cpu_stats(),
            "gpu": _gpu_stats(),
            "memory": _mem_stats(),
            "bandwidth": _bandwidth_stats(),
        }
        return snap
    except Exception:
        # Guarantee schema shape even on failure
        return {
            "timestamp": time.time(),
            "cpu": {"count": int(os.cpu_count() or 1), "threads": int(os.cpu_count() or 1), "util": 0.0},
            "gpu": {"model": "N/A", "memory_total_mb": 0.0, "memory_used_mb": 0.0, "util": 0.0, "temperature_c": 0.0},
            "memory": {"total_gb": 0.0, "used_gb": 0.0, "util": 0.0},
            "bandwidth": {"pcie_gbps": 0.0, "memory_bw_gbps": 0.0},
        }


def system_headroom() -> Dict[str, Any]:
    """Return aggregate system utilization and derived headroom percentage.

    Schema expected by bios.selfcheck:
    {
      timestamp: float,
      global_util: float,
      cpu_util: float,
      gpu_util: float,
      mem_util: float,
      headroom: float
    }
    """
    try:
        dev = device_snapshot()
        cpu_u = float(dev.get("cpu", {}).get("util", 0.0) or 0.0)
        gpu_u = float(dev.get("gpu", {}).get("util", 0.0) or 0.0)
        mem_u = float(dev.get("memory", {}).get("util", 0.0) or 0.0)
        # Weighted blend; memory pressure dominates for safety
        glob = clamp((0.25 * cpu_u) + (0.25 * gpu_u) + (0.50 * mem_u), 0.0, 100.0)
        head = clamp(100.0 - glob, 0.0, 100.0)
        return {
            "timestamp": time.time(),
            "global_util": float(glob),
            "cpu_util": float(cpu_u),
            "gpu_util": float(gpu_u),
            "mem_util": float(mem_u),
            "headroom": float(head),
        }
    except Exception:
        return {
            "timestamp": time.time(),
            "global_util": 0.0,
            "cpu_util": 0.0,
            "gpu_util": 0.0,
            "mem_util": 0.0,
            "headroom": 100.0,
        }
