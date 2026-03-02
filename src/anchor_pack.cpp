#include "anchor_pack.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EigenWare {

EwId9 AnchorPack_id9_from_relpath(const std::string& relpath_utf8) {
    // Spec-bundle compliant: coord_sig9(namespace_sig9, bytes). No hashing / crypto.
    const EwId9 ns = ew_namespace_sig9_anchorpack();
    return coord_sig9(ns,
                      reinterpret_cast<const uint8_t*>(relpath_utf8.data()),
                      relpath_utf8.size());
}

static void ew_build_sin_cos_tables_q15(int16_t sin_q15[32][256], int16_t cos_q15[32][256]) {
    for (int k = 0; k < 32; ++k) {
        int32_t re = 32767;
        int32_t im = 0;
        // w = 2*pi*k/256 in Q1.15. Use fixed integer approximation.
        // 2*pi/256 * 32768 ~= 804.247... => 804 (deterministic).
        const int32_t w_q15 = (int32_t)(k * 804);
        for (int n = 0; n < 256; ++n) {
            cos_q15[k][n] = (int16_t)re;
            sin_q15[k][n] = (int16_t)im;
            const int32_t re2 = re - ((im * w_q15) >> 15);
            const int32_t im2 = im + ((re * w_q15) >> 15);
            re = re2;
            im = im2;
            const int32_t mag = (int32_t)(((int64_t)re * re + (int64_t)im * im) >> 15);
            if (mag > 0) {
                int32_t y = 32767;
                const int64_t yy = ((int64_t)y * y) >> 15;
                const int64_t term = ((int64_t)mag * yy) >> 15;
                const int64_t corr = ((int64_t)49152 - (term >> 1));
                y = (int32_t)(((int64_t)y * corr) >> 15);
                re = (int32_t)(((int64_t)re * y) >> 15);
                im = (int32_t)(((int64_t)im * y) >> 15);
            }
        }
    }
}

void AnchorPack_bytes_to_harmonics32_q15(const uint8_t* bytes, size_t n, uint16_t out_q15[32]) {
    if (!out_q15) return;
    for (int i = 0; i < 32; ++i) out_q15[i] = 0;
    if (!bytes || n == 0) return;

    static int16_t sin_q15[32][256];
    static int16_t cos_q15[32][256];
    static bool tables_ready = false;
    if (!tables_ready) {
        ew_build_sin_cos_tables_q15(sin_q15, cos_q15);
        tables_ready = true;
    }

    int16_t x[256];
    for (int i = 0; i < 256; ++i) {
        const uint8_t bb = bytes[(size_t)i % n];
        x[i] = (int16_t)((int)bb - 128);
    }

    for (int k = 0; k < 32; ++k) {
        int64_t acc_re = 0;
        int64_t acc_im = 0;
        for (int i = 0; i < 256; ++i) {
            acc_re += (int32_t)x[i] * (int32_t)cos_q15[k][i];
            acc_im += (int32_t)x[i] * (int32_t)sin_q15[k][i];
        }
        int64_t are = (acc_re < 0) ? -acc_re : acc_re;
        int64_t aim = (acc_im < 0) ? -acc_im : acc_im;
        int64_t mag = are + aim;
        mag = mag >> 20;
        if (mag > 32767) mag = 32767;
        if (mag < 0) mag = 0;
        out_q15[k] = (uint16_t)mag;
    }
}

static inline bool ew_relpath_less(const AnchorPackBlob& a, const AnchorPackBlob& b) {
    return a.relpath_utf8 < b.relpath_utf8;
}

bool AnchorPack_install(std::vector<AnchorPackRecord>& out_records,
                        uint32_t lane_u32,
                        const EwId9& domain_id9) {
    std::vector<AnchorPackBlob> blobs;
    AnchorPackGen::get_embedded_blobs(blobs);
    std::stable_sort(blobs.begin(), blobs.end(), ew_relpath_less);

    out_records.clear();
    out_records.reserve(blobs.size());

    for (const auto& b : blobs) {
        AnchorPackRecord r;
        r.relpath_utf8 = b.relpath_utf8;
        r.artifact_id9 = AnchorPack_id9_from_relpath(b.relpath_utf8);
        r.anchor.reset();
        r.anchor.anchor_id9 = r.artifact_id9;
        r.anchor.domain_id9 = domain_id9;
        r.anchor.lane_u8 = (uint8_t)(lane_u32 & 0xFFu);
        r.anchor.seq_u32 = 0u;
        r.anchor.offset_u32 = 0u;
        r.anchor.size_u32 = (uint32_t)b.comp_bytes_u8.size();
        AnchorPack_bytes_to_harmonics32_q15(b.comp_bytes_u8.data(), b.comp_bytes_u8.size(), r.anchor.harmonics_q15);
        out_records.push_back(std::move(r));
    }
    return true;
}

} // namespace EigenWare
