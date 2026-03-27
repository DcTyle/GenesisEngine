from __future__ import annotations

"""
Optional CuPy-backed GPU helpers for compressed per-particle state.

Notes:
- Safe to import without CuPy installed; all GPU paths are opt-in.
- Focused on storage/transcode primitives; compute kernels can be layered on.
- Pairs with core.quantize for chunked int8 storage of complex vectors.
"""

import os
from typing import List, Optional, Tuple, Dict

try:
    import cupy as cp  # type: ignore
except Exception:  # pragma: no cover - optional
    cp = None  # type: ignore

from core import quantize


def gpu_available() -> bool:
    if cp is None:
        return False
    try:
        _ = cp.cuda.runtime.getDeviceCount()
        return True
    except Exception:
        return False


def gpu_enabled() -> bool:
    # Environment + availability gate
    return os.environ.get("VHW_GPU_ENABLED", "0") in ("1", "true", "TRUE") and gpu_available()


class ParticleBuffer:
    """
    Compressed, chunked, int8 storage of complex state on GPU (or CPU fallback).

    Layout:
    - qreal, qimag: int8 device arrays of length N
    - scale_r, scale_i: float32 device arrays of length n_chunks
    - zp_r, zp_i: int32 device arrays of length n_chunks (zero-points; 0 for symmetric)
    - chunk_size: granularity of scales/zero-points

    Methods provide de/quantization and host–device roundtrips.
    """

    def __init__(
        self,
        qreal,  # device or CPU array (int8)
        qimag,  # device or CPU array (int8)
        scale_r,
        scale_i,
        zp_r,
        zp_i,
        length: int,
        chunk_size: int,
        symmetric: bool = True,
        bitwidth: int = 8,
    ) -> None:
        self.qreal = qreal
        self.qimag = qimag
        self.scale_r = scale_r
        self.scale_i = scale_i
        self.zp_r = zp_r
        self.zp_i = zp_i
        self.length = int(length)
        self.chunk_size = int(chunk_size)
        self.symmetric = bool(symmetric)
        self.bitwidth = int(bitwidth)

    @staticmethod
    def from_complex_vector(vec: List[complex], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True) -> "ParticleBuffer":
        blob = quantize.quantize_chunked_complex(vec, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
        length = int(blob["length"])  # type: ignore
        # Device or CPU arrays
        if gpu_enabled():
            qreal = cp.asarray(blob["qreal"], dtype=cp.int8)  # type: ignore
            qimag = cp.asarray(blob["qimag"], dtype=cp.int8)  # type: ignore
            scale_r = cp.asarray(blob["scale_r"], dtype=cp.float32)  # type: ignore
            scale_i = cp.asarray(blob["scale_i"], dtype=cp.float32)  # type: ignore
            zp_r = cp.asarray(blob["zp_r"], dtype=cp.int32)  # type: ignore
            zp_i = cp.asarray(blob["zp_i"], dtype=cp.int32)  # type: ignore
        else:
            # CPU fallback via Python lists (keeps import safety); not performance-oriented.
            qreal = list(int(v) for v in blob["qreal"])  # type: ignore
            qimag = list(int(v) for v in blob["qimag"])  # type: ignore
            scale_r = list(float(v) for v in blob["scale_r"])  # type: ignore
            scale_i = list(float(v) for v in blob["scale_i"])  # type: ignore
            zp_r = list(int(v) for v in blob["zp_r"])  # type: ignore
            zp_i = list(int(v) for v in blob["zp_i"])  # type: ignore
        return ParticleBuffer(
            qreal=qreal,
            qimag=qimag,
            scale_r=scale_r,
            scale_i=scale_i,
            zp_r=zp_r,
            zp_i=zp_i,
            length=length,
            chunk_size=int(blob["chunk_size"]),  # type: ignore
            symmetric=bool(blob["symmetric"]),  # type: ignore
            bitwidth=int(blob["bitwidth"])  # type: ignore
        )

    def to_complex_vector(self) -> List[complex]:
        # Host roundtrip
        if gpu_enabled():
            blob = {
                "length": self.length,
                "chunk_size": self.chunk_size,
                "bitwidth": self.bitwidth,
                "symmetric": self.symmetric,
                "qreal": cp.asnumpy(self.qreal).tolist(),
                "qimag": cp.asnumpy(self.qimag).tolist(),
                "scale_r": cp.asnumpy(self.scale_r).tolist(),
                "scale_i": cp.asnumpy(self.scale_i).tolist(),
                "zp_r": cp.asnumpy(self.zp_r).tolist(),
                "zp_i": cp.asnumpy(self.zp_i).tolist(),
            }
        else:
            blob = {
                "length": self.length,
                "chunk_size": self.chunk_size,
                "bitwidth": self.bitwidth,
                "symmetric": self.symmetric,
                "qreal": list(self.qreal),
                "qimag": list(self.qimag),
                "scale_r": list(self.scale_r),
                "scale_i": list(self.scale_i),
                "zp_r": list(self.zp_r),
                "zp_i": list(self.zp_i),
            }
        return quantize.dequantize_chunked_complex(blob)

    # Placeholder kernel hooks (expand as needed)
    def apply_phase_rotation_ip(self, theta: float) -> None:
        """
        Applies a global phase rotation e^{i theta} to all complex amplitudes in-place.
        For compressed storage, this operates on dequantize->rotate->requantize path for simplicity.
        Optimized kernels can replace this later.
        """
        # Simple, safe implementation: roundtrip for now.
        vec = self.to_complex_vector()
        import math
        c, s = math.cos(theta), math.sin(theta)
        rotated = [complex(z.real * c - z.imag * s, z.real * s + z.imag * c) for z in vec]
        new = ParticleBuffer.from_complex_vector(rotated, chunk_size=self.chunk_size, bitwidth=self.bitwidth, symmetric=self.symmetric)
        # Swap buffers
        self.qreal = new.qreal
        self.qimag = new.qimag
        self.scale_r = new.scale_r
        self.scale_i = new.scale_i
        self.zp_r = new.zp_r
        self.zp_i = new.zp_i
        self.length = new.length
