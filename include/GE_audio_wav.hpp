#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

// Minimal deterministic PCM16 WAV IO (no external deps).
struct WavPcm16Mono {
    uint32_t sample_rate_hz_u32 = 48000;
    std::vector<int16_t> samples_i16;
};

bool ge_wav_read_pcm16_mono(const std::string& path, WavPcm16Mono* out_wav);
bool ge_wav_write_pcm16_mono(const std::string& path, const WavPcm16Mono& wav);

} // namespace genesis
