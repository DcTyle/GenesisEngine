#include "GE_object_local_cuda.hpp"

#if EW_ENABLE_CUDA
#include <cuda_runtime.h>

extern "C" bool ge_object_local_step_q15_cuda(const uint8_t* host_occ_u8,
                                              const int16_t* host_phi_in_q15_s16,
                                              uint32_t gx, uint32_t gy, uint32_t gz,
                                              int16_t bias_e_curr_q15,
                                              int16_t bias_flux_q15,
                                              int16_t bias_coherence_q15,
                                              int16_t bias_curvature_q15,
                                              int16_t bias_doppler_q15,
                                              int16_t* host_phi_out_q15_s16,
                                              genesis::EwObjectLocalStepStats* out_host);

namespace genesis {

bool ge_cuda_object_local_step_q15(const uint8_t* occ_u8,
                                  const int16_t* phi_in_q15_s16,
                                  uint32_t gx_u32, uint32_t gy_u32, uint32_t gz_u32,
                                  const EwWorldBoundaryBiasQ15& world_bias,
                                  int16_t* phi_out_q15_s16,
                                  EwObjectLocalStepStats& out_stats) {
    out_stats = EwObjectLocalStepStats{};
    if (!occ_u8 || !phi_in_q15_s16 || !phi_out_q15_s16) return false;
    if (gx_u32 == 0 || gy_u32 == 0 || gz_u32 == 0) return false;
    return ge_object_local_step_q15_cuda(occ_u8, phi_in_q15_s16, gx_u32, gy_u32, gz_u32,
                                         world_bias.e_curr_q15,
                                         world_bias.flux_q15,
                                         world_bias.coherence_q15,
                                         world_bias.curvature_q15,
                                         world_bias.doppler_q15,
                                         phi_out_q15_s16, &out_stats);
}

} // namespace genesis

#else
namespace genesis {
bool ge_cuda_object_local_step_q15(const uint8_t*, const int16_t*, uint32_t, uint32_t, uint32_t, const EwWorldBoundaryBiasQ15&, int16_t*, EwObjectLocalStepStats&) {
    return false;
}
} // namespace genesis
#endif
