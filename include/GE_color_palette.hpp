#pragma once

#include <cstdint>

// Deterministic false-color + audio preset mapping.
// Hard rule: palette is derived-only; it must never be used to drive simulation.
// Bands are small (0..7) to keep UI/audio stable and cheap.

struct EwPaletteEntry {
    uint8_t r_u8 = 0;
    uint8_t g_u8 = 0;
    uint8_t b_u8 = 0;
    // Audio preset index (0..15). Renderer/audio may map this to EQ curves.
    uint8_t audio_eq_preset_u8 = 0;
    // Additional derived audio-shaping presets (0..15). These are contracts only;
    // audio backend may map them to reverb/occlusion tables.
    uint8_t audio_reverb_preset_u8 = 0;
    uint8_t audio_occlusion_preset_u8 = 0;
    uint8_t pad0_u8 = 0;
};

static inline EwPaletteEntry ew_palette_lookup(uint8_t band_u8) {
    static const EwPaletteEntry kTbl[8] = {
        // r,g,b, eq, reverb, occlusion, pad
        {  16,  16,  20,  0,  0,  0, 0},
        {  24,  64, 160,  1,  1,  1, 0},
        {  32, 160, 192,  2,  2,  2, 0},
        {  32, 192,  64,  3,  3,  3, 0},
        { 160, 200,  32,  4,  4,  5, 0},
        { 224, 160,  32,  5,  6,  7, 0},
        { 224,  64,  48,  6,  9, 10, 0},
        { 240, 240, 240,  7, 12, 12, 0},
    };
    return kTbl[band_u8 & 7u];
}
