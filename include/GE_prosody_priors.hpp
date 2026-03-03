#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GE_voice_predictive_model.hpp"

namespace genesis {

struct ProsodyPriorRow {
    std::string phone; // uppercase
    uint32_t mean_dur_ms_q16_u32 = 80u << 16;
    uint32_t mean_f0_ratio_q16_u32 = 65536;
    uint32_t mean_amp_ratio_q16_u32 = 65536;
    uint32_t count_u32 = 0;
};

struct ProsodyPriors {
    bool ok = false;
    std::string info;
    std::vector<ProsodyPriorRow> rows;
};

// Read/write deterministic ewcfg.
bool ge_prosody_priors_read_ewcfg(const std::string& path, ProsodyPriors* out);
bool ge_prosody_priors_write_ewcfg(const std::string& path, const ProsodyPriors& pri);

// Blend priors into per-phone controls.
// - phones_upper length must match controls.
// - blend_q16 in [0..65536]
void ge_prosody_apply_priors(
    const ProsodyPriors& pri,
    const std::vector<std::string>& phones_upper,
    uint32_t blend_q16,
    std::vector<VoiceProsodyControl>* inout_controls);

} // namespace genesis
