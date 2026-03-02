#include <cuda_runtime.h>
#include <cstdint>

extern "C" {

// Minimal GPU helper kernel: apply leakage to a q15 amp array (REGION_CAP elements).
__global__ void ge_phase_current_leak_q15(uint16_t* amp_q15, const uint16_t* leak_q15, uint32_t n) {
    const uint32_t i = (uint32_t)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= n) return;
    const uint32_t a = amp_q15[i];
    const uint32_t leak = leak_q15[i];
    const uint32_t dec = (a * leak) >> 15;
    amp_q15[i] = (dec >= a) ? 0u : (uint16_t)(a - dec);
}

} // extern "C"
