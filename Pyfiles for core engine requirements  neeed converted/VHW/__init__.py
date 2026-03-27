"""VHW namespace: Virtual Hardware support for BIOS/core integration.

Exports minimal components used by BIOS and core modules:
- VSDManager: in-process key-value store for shared state
- gpu_initiator: background GPU warm loop
- gpu_kernel: optional CuPy-backed evolve kernel
"""

from .vsd_manager import VSDManager  # noqa: F401
