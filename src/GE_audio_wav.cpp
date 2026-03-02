#include "GE_audio_wav.hpp"

#include <cstdio>
#include <cstring>

namespace genesis {

static uint32_t ge_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t ge_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static void ge_write_u32_le(std::vector<uint8_t>* b, uint32_t v) {
    b->push_back((uint8_t)(v & 255u));
    b->push_back((uint8_t)((v >> 8) & 255u));
    b->push_back((uint8_t)((v >> 16) & 255u));
    b->push_back((uint8_t)((v >> 24) & 255u));
}
static void ge_write_u16_le(std::vector<uint8_t>* b, uint16_t v) {
    b->push_back((uint8_t)(v & 255u));
    b->push_back((uint8_t)((v >> 8) & 255u));
}

bool ge_wav_read_pcm16_mono(const std::string& path, WavPcm16Mono* out_wav) {
    if (!out_wav) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> data;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    data.resize((size_t)sz);
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) { std::fclose(f); return false; }
    std::fclose(f);

    if (data.size() < 44) return false;
    if (std::memcmp(data.data(), "RIFF", 4) != 0) return false;
    if (std::memcmp(data.data() + 8, "WAVE", 4) != 0) return false;

    uint32_t sample_rate = 0;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t* pcm_ptr = nullptr;
    uint32_t pcm_bytes = 0;

    size_t off = 12;
    while (off + 8 <= data.size()) {
        const uint8_t* ck = data.data() + off;
        uint32_t ck_sz = ge_u32_le(ck + 4);
        off += 8;
        if (off + ck_sz > data.size()) break;

        if (std::memcmp(ck, "fmt ", 4) == 0) {
            if (ck_sz < 16) return false;
            audio_format = ge_u16_le(data.data() + off + 0);
            channels = ge_u16_le(data.data() + off + 2);
            sample_rate = ge_u32_le(data.data() + off + 4);
            bits_per_sample = ge_u16_le(data.data() + off + 14);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            pcm_ptr = data.data() + off;
            pcm_bytes = ck_sz;
        }
        off += ck_sz + (ck_sz & 1u); // pad
    }

    if (!pcm_ptr) return false;
    if (audio_format != 1) return false; // PCM
    if (channels != 1) return false;
    if (bits_per_sample != 16) return false;
    if (sample_rate == 0) return false;
    if ((pcm_bytes % 2u) != 0) return false;

    out_wav->sample_rate_hz_u32 = sample_rate;
    out_wav->samples_i16.resize(pcm_bytes / 2u);
    std::memcpy(out_wav->samples_i16.data(), pcm_ptr, pcm_bytes);
    return true;
}

bool ge_wav_write_pcm16_mono(const std::string& path, const WavPcm16Mono& wav) {
    const uint32_t sample_rate = (wav.sample_rate_hz_u32 == 0) ? 48000u : wav.sample_rate_hz_u32;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t fmt_tag = 1;
    const uint32_t byte_rate = sample_rate * channels * (bits / 8u);
    const uint16_t block_align = channels * (bits / 8u);
    const uint32_t data_bytes = (uint32_t)(wav.samples_i16.size() * sizeof(int16_t));
    const uint32_t riff_size = 36u + data_bytes;

    std::vector<uint8_t> b;
    b.insert(b.end(), {'R','I','F','F'});
    ge_write_u32_le(&b, riff_size);
    b.insert(b.end(), {'W','A','V','E'});
    b.insert(b.end(), {'f','m','t',' '});
    ge_write_u32_le(&b, 16u);
    ge_write_u16_le(&b, fmt_tag);
    ge_write_u16_le(&b, channels);
    ge_write_u32_le(&b, sample_rate);
    ge_write_u32_le(&b, byte_rate);
    ge_write_u16_le(&b, block_align);
    ge_write_u16_le(&b, bits);
    b.insert(b.end(), {'d','a','t','a'});
    ge_write_u32_le(&b, data_bytes);
    const uint8_t* pcm = (const uint8_t*)wav.samples_i16.data();
    b.insert(b.end(), pcm, pcm + data_bytes);

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t wrote = std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
    return wrote == b.size();
}

} // namespace genesis
