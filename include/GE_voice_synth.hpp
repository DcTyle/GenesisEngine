#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "GE_audio_wav.hpp"

namespace genesis {

// Deterministic minimal English TTS scaffold (no external engines).
// Not intended to match commercial neural TTS quality; it exists to provide a
// self-hosted vocal output path and to generate measurable audio artifacts for
// the learning gate.

struct VoiceSynthConfig {
    uint32_t sample_rate_hz_u32 = 48000;
    // Overall amplitude scale in Q15 (0..32767).
    int16_t amp_q15_i16 = 12000;
    // Nominal speaking rate in phones per second (fixed-point Q16.16).
    uint32_t phones_per_sec_q16_u32 = 3u << 16;
    // Fundamental frequency (pitch) in Hz (Q16.16).
    uint32_t f0_hz_q16_u32 = 130u << 16;
};

struct PhonemeSpan {
    // ARPABET phone, e.g., "AH0", "S", "T"
    std::string phone;
    // duration in milliseconds
    uint32_t dur_ms_u32 = 80;
};

struct TtsResult {
    bool ok = false;
    std::string info;
    WavPcm16Mono wav;
};

// Convert English text into ARPABET-like phones using a tiny deterministic rule set.
// If a CMU-style lexicon is available in the LanguageFoundation, callers should
// prefer that and only fall back to this.
std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text);

// Synthesize PCM16 mono waveform from a phoneme sequence using a simple
// deterministic source-filter / formant-ish synthesizer (very small, fully local).
TtsResult ge_synthesize_phones_to_wav(const std::vector<PhonemeSpan>& phones, const VoiceSynthConfig& cfg);

} // namespace genesis
