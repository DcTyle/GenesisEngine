from __future__ import annotations

"""
GPU-backed evolve kernel (optional).

Uses CuPy when available; falls back to NumPy otherwise. The kernel applies
phase rotation to complex state vectors (re, im) in-place.
"""

from typing import Tuple

try:  # Optional dependency
    import cupy as cp  # type: ignore
    _GPU_OK = True
except Exception:  # pragma: no cover
    cp = None  # type: ignore
    _GPU_OK = False

import numpy as np
import math


def is_available() -> bool:
    return bool(_GPU_OK)


def evolve_rotate(re_im: Tuple, theta: float) -> None:
    """Rotate (re, im) arrays by angle theta in-place.

    Accepts either (numpy arrays) or (cupy arrays). If CPU arrays are passed
    and CuPy is available, this will transparently move data to GPU, operate,
    and copy back.
    """
    re, im = re_im
    c = math.cos(theta)
    s = math.sin(theta)

    if _GPU_OK:
        # Move to GPU if needed
        re_gpu = cp.asarray(re)
        im_gpu = cp.asarray(im)
        new_re = c * re_gpu - s * im_gpu
        new_im = s * re_gpu + c * im_gpu
        # Write back in-place to original buffers
        re[...] = cp.asnumpy(new_re)
        im[...] = cp.asnumpy(new_im)
        return

    # CPU fallback
    new_re = c * re - s * im
    new_im = s * re + c * im
    re[...] = new_re
    im[...] = new_im
