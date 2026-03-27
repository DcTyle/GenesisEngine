# Path: Quantum Application/bios/policy.py
# ============================================================================
# Version: v5.1 "Unified Imports + Silent Error Hardening"
# VirtualMiner / BIOS
# File: policy.py
# Directory: /BIOS/
# ASCII-ONLY SOURCE FILE
# Note: This file avoids any non-ASCII characters. Jarvis ADA v4.7 Hybrid ready.
# ============================================================================
"""
Purpose
-------
Policy helpers enforcing global duty caps and confidence thresholds.
This module centralizes BIOS policy so that
allocators and schedulers can consult a single source of truth.

Public API
---------
DutyCap(cap=0.75, headroom=0.10)
    .target() -> float
    .over(util) -> bool

Confidence(threshold=0.80)
    .allow_trade(direction: str, conf: float) -> bool
"""

from __future__ import annotations
from typing import Dict, Any
import logging

# ---------------------------------------------------------------
# Local logger
# ---------------------------------------------------------------
_logger = logging.getLogger("bios.policy")


# ---------------------------------------------------------------
# Clamp
# ---------------------------------------------------------------
def _clamp(v: float, lo: float, hi: float) -> float:
    try:
        return max(lo, min(hi, float(v)))
    except Exception as exc:
        _logger.error(
            "_clamp: failed to clamp value '%s': %s",
            v,
            exc,
            exc_info=True,
        )
        return lo


# ---------------------------------------------------------------
# Global Duty Cap
# ---------------------------------------------------------------
class DutyCap:
    def __init__(self, cap: float = 0.75, headroom: float = 0.10):
        try:
            self.cap = _clamp(float(cap), 0.10, 0.95)
            self.headroom = _clamp(float(headroom), 0.0, 0.50)
        except Exception as exc:
            _logger.error(
                "DutyCap.__init__: invalid inputs cap='%s' headroom='%s': %s",
                cap,
                headroom,
                exc,
                exc_info=True,
            )
            self.cap = 0.75
            self.headroom = 0.10

    def target(self) -> float:
        # This logic unchanged and safe.
        try:
            return max(0.0, self.cap - self.headroom)
        except Exception as exc:
            _logger.error(
                "DutyCap.target: calculation failed: %s",
                exc,
                exc_info=True,
            )
            return max(0.0, 0.75 - 0.10)

    def over(self, util: float) -> bool:
        try:
            return float(util) > self.target()
        except Exception as exc:
            _logger.error(
                "DutyCap.over: comparison failed util='%s': %s",
                util,
                exc,
                exc_info=True,
            )
            return False
 


# ---------------------------------------------------------------
# Confidence Gate
# ---------------------------------------------------------------
class Confidence:
    def __init__(self, threshold: float = 0.80):
        try:
            self.t = _clamp(float(threshold), 0.0, 1.0)
        except Exception as exc:
            _logger.error(
                "Confidence.__init__: invalid threshold '%s': %s",
                threshold,
                exc,
                exc_info=True,
            )
            self.t = 0.80

    def allow_trade(self, direction: str, conf: float) -> bool:
        """
        Only applies confidence gating to upward trades.
        User directives:
          * No high-confidence rule for downward trends.
          * No trades allowed unless direction == 'up'.
        """
        try:
            if direction not in ("up", "down"):
                return False
            if direction == "down":
                return False
            return float(conf) >= self.t
        except Exception as exc:
            _logger.error(
                "Confidence.allow_trade: failed for direction='%s' conf='%s': %s",
                direction,
                conf,
                exc,
                exc_info=True,
            )
            return False
