from __future__ import annotations

"""
Core storage helpers for super-compressed per-particle complex state.

This module ties together quantization (CPU) and optional GPU buffers.
It does not alter existing call-sites; it offers new, opt-in APIs.
"""

from typing import List, Dict, Optional

from core import quantize

try:
    from core.gpu_backend import ParticleBuffer, gpu_enabled  # type: ignore
except Exception:  # pragma: no cover - optional
    ParticleBuffer = None  # type: ignore
    def gpu_enabled() -> bool:  # type: ignore
        return False


def compress_state_vector(vec: List[complex], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True) -> Dict[str, object]:
    """
    CPU-side compressor for complex vectors using chunked int8 format.
    Returns a JSON-serializable dict.
    """
    return quantize.quantize_chunked_complex(vec, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)


def decompress_state_vector(blob: Dict[str, object]) -> List[complex]:
    return quantize.dequantize_chunked_complex(blob)


def to_gpu_buffer(vec: List[complex], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True):
    """
    Create a ParticleBuffer on GPU if enabled, else return None.
    """
    if gpu_enabled() and ParticleBuffer is not None:
        return ParticleBuffer.from_complex_vector(vec, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
    return None
