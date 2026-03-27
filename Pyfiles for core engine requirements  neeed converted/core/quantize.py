from __future__ import annotations

"""
Lightweight quantization helpers for complex state vectors.

Design goals:
- Pure-Python with optional NumPy acceleration when available.
- No hard dependency on GPU libraries; CPU fallback only.
- Chunked per-block scales to enable compact storage with bounded error.

API:
- quantize_chunked_real(values, chunk_size=64, bitwidth=8, symmetric=True) -> (qvals, scales, zero_points)
- dequantize_chunked_real(qvals, scales, zero_points, chunk_size=64, bitwidth=8, symmetric=True) -> values
- quantize_chunked_complex(vec_complex, ...) -> dict
- dequantize_chunked_complex(blob) -> list[complex]
"""

from typing import List, Tuple, Optional, Dict

try:
    import numpy as _np  # Optional; used if present
except Exception:  # pragma: no cover - optional
    _np = None  # type: ignore


def _to_list_floats(x) -> List[float]:
    if _np is not None and hasattr(x, "tolist"):
        return [float(v) for v in x.tolist()]
    return [float(v) for v in x]


def _min_max(vals: List[float]) -> Tuple[float, float]:
    if not vals:
        return 0.0, 0.0
    if _np is not None:
        arr = _np.asarray(vals, dtype=_np.float64)
        return float(arr.min()), float(arr.max())
    vmin, vmax = vals[0], vals[0]
    for v in vals[1:]:
        if v < vmin:
            vmin = v
        if v > vmax:
            vmax = v
    return float(vmin), float(vmax)


def _compute_scale_zero_point(vmin: float, vmax: float, bitwidth: int = 8, symmetric: bool = True) -> Tuple[float, int]:
    assert bitwidth in (8, 7)
    if symmetric:
        qmin, qmax = -(2 ** (bitwidth - 1)), (2 ** (bitwidth - 1)) - 1
        amax = max(abs(vmin), abs(vmax))
        scale = (amax / qmax) if amax > 0 else 1.0
        zp = 0
        return float(max(scale, 1e-12)), int(zp)
    else:
        qmin, qmax = 0, (2 ** bitwidth) - 1
        if vmax <= vmin:
            return 1.0, 0
        scale = (vmax - vmin) / float(qmax - qmin)
        zp = int(round(qmin - vmin / scale))
        return float(max(scale, 1e-12)), int(zp)


def quantize_chunked_real(values: List[float], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True) -> Tuple[List[int], List[float], List[int]]:
    if chunk_size <= 0:
        raise ValueError("chunk_size must be positive")
    qvals: List[int] = []
    scales: List[float] = []
    zps: List[int] = []
    n = len(values)
    for i in range(0, n, chunk_size):
        chunk = values[i:i + chunk_size]
        vmin, vmax = _min_max(chunk)
        scale, zp = _compute_scale_zero_point(vmin, vmax, bitwidth=bitwidth, symmetric=symmetric)
        scales.append(scale)
        zps.append(zp)
        if symmetric:
            qmin, qmax = -(2 ** (bitwidth - 1)), (2 ** (bitwidth - 1)) - 1
            for v in chunk:
                q = int(round(v / scale))
                if q < qmin:
                    q = qmin
                if q > qmax:
                    q = qmax
                qvals.append(q)
        else:
            qmin, qmax = 0, (2 ** bitwidth) - 1
            for v in chunk:
                q = int(round(v / scale + zp))
                if q < qmin:
                    q = qmin
                if q > qmax:
                    q = qmax
                qvals.append(q)
    return qvals, scales, zps


def dequantize_chunked_real(qvals: List[int], scales: List[float], zps: List[int], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True) -> List[float]:
    if chunk_size <= 0:
        raise ValueError("chunk_size must be positive")
    out: List[float] = []
    n = len(qvals)
    n_chunks = (n + chunk_size - 1) // chunk_size
    if len(scales) != n_chunks or len(zps) != n_chunks:
        raise ValueError("scale/zero-point length mismatch for provided qvals")
    for ci in range(n_chunks):
        scale = float(scales[ci])
        zp = int(zps[ci])
        beg = ci * chunk_size
        end = min(beg + chunk_size, n)
        if symmetric:
            for q in qvals[beg:end]:
                out.append(float(int(q)) * scale)
        else:
            for q in qvals[beg:end]:
                out.append((float(int(q)) - zp) * scale)
    return out


def quantize_chunked_complex(vec_complex: List[complex], *, chunk_size: int = 64, bitwidth: int = 8, symmetric: bool = True) -> Dict[str, object]:
    real = [float(getattr(z, "real", 0.0)) for z in vec_complex]
    imag = [float(getattr(z, "imag", 0.0)) for z in vec_complex]
    qreal, s_r, zp_r = quantize_chunked_real(real, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
    qimag, s_i, zp_i = quantize_chunked_real(imag, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
    return {
        "length": int(len(vec_complex)),
        "chunk_size": int(chunk_size),
        "bitwidth": int(bitwidth),
        "symmetric": bool(symmetric),
        "qreal": qreal,
        "qimag": qimag,
        "scale_r": s_r,
        "scale_i": s_i,
        "zp_r": zp_r,
        "zp_i": zp_i,
    }


def dequantize_chunked_complex(blob: Dict[str, object]) -> List[complex]:
    length = int(blob.get("length", 0))
    chunk_size = int(blob.get("chunk_size", 64))
    bitwidth = int(blob.get("bitwidth", 8))
    symmetric = bool(blob.get("symmetric", True))
    qreal: List[int] = [int(v) for v in blob.get("qreal", [])]  # type: ignore
    qimag: List[int] = [int(v) for v in blob.get("qimag", [])]  # type: ignore
    s_r: List[float] = [float(v) for v in blob.get("scale_r", [])]  # type: ignore
    s_i: List[float] = [float(v) for v in blob.get("scale_i", [])]  # type: ignore
    zp_r: List[int] = [int(v) for v in blob.get("zp_r", [])]  # type: ignore
    zp_i: List[int] = [int(v) for v in blob.get("zp_i", [])]  # type: ignore
    real = dequantize_chunked_real(qreal, s_r, zp_r, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
    imag = dequantize_chunked_real(qimag, s_i, zp_i, chunk_size=chunk_size, bitwidth=bitwidth, symmetric=symmetric)
    n = min(length, min(len(real), len(imag)))
    return [complex(real[i], imag[i]) for i in range(n)]
