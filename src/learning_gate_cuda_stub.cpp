#include "learning_gate_cuda.hpp"

namespace genesis {

bool ew_learning_gate_tick_cuda(MetricTask* task_host,
                                uint64_t canonical_tick_u64,
                                uint64_t tries_this_tick_u64,
                                uint32_t steps_this_tick_u32) {
    (void)task_host;
    (void)canonical_tick_u64;
    (void)tries_this_tick_u64;
    (void)steps_this_tick_u32;
    return false;
}

bool ew_learning_bind_world_lattice_cuda(const float* d_E_curr,
                                         const float* d_flux,
                                         const float* d_coherence,
                                         const float* d_curvature,
                                         const float* d_doppler,
                                         int gx,
                                         int gy,
                                         int gz) {
    (void)d_E_curr;
    (void)d_flux;
    (void)d_coherence;
    (void)d_curvature;
    (void)d_doppler;
    (void)gx;
    (void)gy;
    (void)gz;
    return false;
}

bool ew_learning_bind_probe_lattice_cuda(const float* d_E_curr,
                                         const float* d_flux,
                                         const float* d_coherence,
                                         const float* d_curvature,
                                         const float* d_doppler,
                                         int gx,
                                         int gy,
                                         int gz) {
    (void)d_E_curr;
    (void)d_flux;
    (void)d_coherence;
    (void)d_curvature;
    (void)d_doppler;
    (void)gx;
    (void)gy;
    (void)gz;
    return false;
}

} // namespace genesis
