#include "field_lattice_cpu.hpp"

#include <algorithm>

static inline float q32_32_to_float(int64_t v_q32_32) {
    return (float)((double)v_q32_32 / 4294967296.0);
}

EwFieldLatticeCpu::EwFieldLatticeCpu(uint32_t gx, uint32_t gy, uint32_t gz) : gx_(gx), gy_(gy), gz_(gz) {
    const size_t n = (size_t)gx_ * (size_t)gy_ * (size_t)gz_;
    E_prev_.assign(n, 0.0f);
    E_curr_.assign(n, 0.0f);
    E_next_.assign(n, 0.0f);
    flux_.assign(n, 0.0f);
    coherence_.assign(n, 1.0f);
    curvature_.assign(n, 0.0f);
    doppler_.assign(n, 0.0f);
    density_u8_.assign(n, 0u);
    bh_exclude_u8_.assign(n, 0u);

    // Operator vector fields start at zero. They are evolved deterministically
    // and modulate transport/damping per-voxel (compute encoded as fields).
    opA_.assign(n, 0.0f);
    opB_.assign(n, 0.0f);
    L0_.assign(n, 0.0f);
    L1_.assign(n, 0.0f);
    L2_.assign(n, 0.0f);
    L3_.assign(n, 0.0f);
}

int EwFieldLatticeCpu::idx3_(int x, int y, int z) const {
    return (z * (int)gy_ + y) * (int)gx_ + x;
}

void EwFieldLatticeCpu::init(uint64_t seed) {
    (void)seed;
    std::fill(E_prev_.begin(), E_prev_.end(), 0.0f);
    std::fill(E_curr_.begin(), E_curr_.end(), 0.0f);
    std::fill(E_next_.begin(), E_next_.end(), 0.0f);
    std::fill(flux_.begin(), flux_.end(), 0.0f);
    std::fill(coherence_.begin(), coherence_.end(), 1.0f);
    std::fill(curvature_.begin(), curvature_.end(), 0.0f);
    std::fill(doppler_.begin(), doppler_.end(), 0.0f);
    std::fill(density_u8_.begin(), density_u8_.end(), 0u);
    std::fill(bh_exclude_u8_.begin(), bh_exclude_u8_.end(), 0u);
    std::fill(opA_.begin(), opA_.end(), 0.0f);
    std::fill(opB_.begin(), opB_.end(), 0.0f);
    tick_index_ = 0;
    frame_seq_ = 0;
    pending_text_amp_q32_32_ = 0;
    pending_image_amp_q32_32_ = 0;
    pending_audio_amp_q32_32_ = 0;
}

void EwFieldLatticeCpu::upload_density_mask_u8(const uint8_t* mask_u8, size_t bytes) {
    const size_t need = density_u8_.size();
    // UE modules typically compile with exceptions disabled.
    // Deterministic fail-closed: if the input is invalid, clear the density mask.
    if (!mask_u8 || bytes != need) {
        std::fill(density_u8_.begin(), density_u8_.end(), 0u);
        std::fill(bh_exclude_u8_.begin(), bh_exclude_u8_.end(), 0u);
        return;
    }
    for (size_t i = 0; i < need; ++i) density_u8_[i] = mask_u8[i];

    // Derive black hole event-horizon exclusion mask deterministically.
    // Rationale: our critical-mass regions can map to large effective "amperage"
    // in the substrate representation. Simulating inside the event horizon
    // (including the center) is both physically meaningless for our purposes
    // and a performance hazard (can drive runaway energy/thermal load).
    //
    // Rule: density_u8 >= BH_CORE_DENSITY_U8 is treated as a black-hole core.
    // The event horizon radius (in voxels) grows with density; all voxels within
    // that radius are excluded from evolution.
    constexpr uint8_t BH_CORE_DENSITY_U8 = 250u;
    constexpr int BH_RADIUS_MIN = 2;   // at threshold
    constexpr int BH_RADIUS_MAX = 12;  // at 255
    std::fill(bh_exclude_u8_.begin(), bh_exclude_u8_.end(), 0u);

    for (uint32_t z = 0; z < gz_; ++z) {
        for (uint32_t y = 0; y < gy_; ++y) {
            for (uint32_t x = 0; x < gx_; ++x) {
                const int iC = idx3_((int)x, (int)y, (int)z);
                const uint8_t d = density_u8_[(size_t)iC];
                if (d < BH_CORE_DENSITY_U8) continue;

                // radius = BH_RADIUS_MIN + (d - BH_CORE_DENSITY_U8) * 2, clamped.
                int r = BH_RADIUS_MIN + (int)(d - BH_CORE_DENSITY_U8) * 2;
                if (r > BH_RADIUS_MAX) r = BH_RADIUS_MAX;
                const int r2 = r * r;

                const int x0 = (int)x;
                const int y0 = (int)y;
                const int z0 = (int)z;
                const int xmin = std::max(0, x0 - r);
                const int xmax = std::min((int)gx_ - 1, x0 + r);
                const int ymin = std::max(0, y0 - r);
                const int ymax = std::min((int)gy_ - 1, y0 + r);
                const int zmin = std::max(0, z0 - r);
                const int zmax = std::min((int)gz_ - 1, z0 + r);

                for (int zz = zmin; zz <= zmax; ++zz) {
                    const int dz = zz - z0;
                    for (int yy = ymin; yy <= ymax; ++yy) {
                        const int dy = yy - y0;
                        for (int xx = xmin; xx <= xmax; ++xx) {
                            const int dx = xx - x0;
                            const int dist2 = dx * dx + dy * dy + dz * dz;
                            if (dist2 <= r2) {
                                const int iE = idx3_(xx, yy, zz);
                                bh_exclude_u8_[(size_t)iE] = 1u;
                            }
                        }
                    }
                }
            }
        }
    }
}

void EwFieldLatticeCpu::inject_text_amplitude_q32_32(int64_t amp_q32_32) { pending_text_amp_q32_32_ += amp_q32_32; }
void EwFieldLatticeCpu::inject_image_amplitude_q32_32(int64_t amp_q32_32) { pending_image_amp_q32_32_ += amp_q32_32; }
void EwFieldLatticeCpu::inject_audio_amplitude_q32_32(int64_t amp_q32_32) { pending_audio_amp_q32_32_ += amp_q32_32; }

void EwFieldLatticeCpu::step_one_tick() {
    tick_index_++;

    // Deterministic injection at center.
    const float amp_text = q32_32_to_float(pending_text_amp_q32_32_);
    const float amp_image = q32_32_to_float(pending_image_amp_q32_32_);
    const float amp_audio = q32_32_to_float(pending_audio_amp_q32_32_);
    pending_text_amp_q32_32_ = 0;
    pending_image_amp_q32_32_ = 0;
    pending_audio_amp_q32_32_ = 0;

    const int cx = (int)gx_ / 2;
    const int cy = (int)gy_ / 2;
    const int cz = (int)gz_ / 2;
    const int i0 = idx3_(cx, cy, cz);
    const float src = amp_text + amp_image + amp_audio;
    // Do not inject inside a black hole event horizon.
    if (bh_exclude_u8_[(size_t)i0] == 0u) {
        E_curr_[i0] += src;
        flux_[i0] += src;

        // Phase-dynamics operator encoding: inputs also modulate the operator field.
        // This keeps "how the math runs" inside the substrate by moving evolution
        // controls into resident vector fields (opA/opB), rather than UE parameters.
        opA_[i0] += 0.25f * src;
        opB_[i0] += 0.10f * src;
        doppler_[i0] = (float)((tick_index_ % 1024ULL) / 1024.0);
    }

    // Wave step.
    const float dt2 = dt_ * dt_;
    for (uint32_t z = 0; z < gz_; ++z) {
        const int zm = (z > 0) ? (int)(z - 1) : (int)z;
        const int zp = (z + 1 < gz_) ? (int)(z + 1) : (int)z;
        for (uint32_t y = 0; y < gy_; ++y) {
            const int ym = (y > 0) ? (int)(y - 1) : (int)y;
            const int yp = (y + 1 < gy_) ? (int)(y + 1) : (int)y;
            for (uint32_t x = 0; x < gx_; ++x) {
                const int xm = (x > 0) ? (int)(x - 1) : (int)x;
                const int xp = (x + 1 < gx_) ? (int)(x + 1) : (int)x;
                const int iC = idx3_((int)x, (int)y, (int)z);
                const int iXm = idx3_(xm, (int)y, (int)z);
                const int iXp = idx3_(xp, (int)y, (int)z);
                const int iYm = idx3_((int)x, ym, (int)z);
                const int iYp = idx3_((int)x, yp, (int)z);
                const int iZm = idx3_((int)x, (int)y, zm);
                const int iZp = idx3_((int)x, (int)y, zp);

                // Black hole event-horizon clamp: do not evolve inside.
                if (bh_exclude_u8_[(size_t)iC] != 0u) {
                    E_next_[(size_t)iC] = E_curr_[(size_t)iC];
                    coherence_[(size_t)iC] = 0.0f;
                    curvature_[(size_t)iC] = 0.0f;
                    flux_[(size_t)iC] = 0.98f * flux_[(size_t)iC];
                    doppler_[(size_t)iC] = 0.0f;
                    opA_[(size_t)iC] = 0.999f * opA_[(size_t)iC];
                    opB_[(size_t)iC] = 0.999f * opB_[(size_t)iC];
                    continue;
                }

                const float Ec = E_curr_[iC];
                const float lap = (E_curr_[iXm] + E_curr_[iXp] + E_curr_[iYm] + E_curr_[iYp] + E_curr_[iZm] + E_curr_[iZp] - 6.0f * Ec);
                const float dens = (float)density_u8_[iC] / 255.0f;
                // Base operator terms (geometry-driven) plus operator-field modulation.
                const float damp = (beta_ * (1.0f + 4.0f * dens)) + opB_[iC];
                const float trans = (c2_ * (1.0f - 0.65f * dens)) + opA_[iC];
                float En = 2.0f * Ec - E_prev_[iC] + trans * lap * dt2 - damp * Ec * dt2;
                E_next_[iC] = En;

                coherence_[iC] = 1.0f - dens;
                curvature_[iC] = (lap >= 0.0f) ? lap : -lap;
                flux_[iC] = 0.98f * flux_[iC] + 0.02f * En;
                const float dx = E_curr_[iXp] - E_curr_[iXm];
                const float adx = (dx >= 0.0f) ? dx : -dx;
                doppler_[iC] = 0.995f * doppler_[iC] + 0.005f * adx;

                // Deterministic operator-field persistence/decay.
                // This is the minimal "operator as field" mechanism: operator
                // parameters are state that evolves inside the substrate.
                opA_[iC] = 0.999f * opA_[iC];
                opB_[iC] = 0.999f * opB_[iC];
            }
        }
    }

    E_prev_.swap(E_curr_);
    E_curr_.swap(E_next_);
}

void EwFieldLatticeCpu::get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr) {
    if (slice_z >= gz_) slice_z = gz_ / 2;
    const size_t n = (size_t)gx_ * (size_t)gy_ * (size_t)gz_;

    // Deterministic mean_rho.
    double sum_rho = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double f = (double)flux_[i];
        const double c = (double)curvature_[i];
        sum_rho += (f * f + c * c);
    }
    const float mean_rho = (float)(sum_rho / (double)n);
    const float mean_abs_time = (float)((double)tick_index_ * (double)dt_);

    const size_t n_slice = (size_t)gx_ * (size_t)gy_;
    out_bgra8.assign(n_slice * 4, 0u);

    for (uint32_t y = 0; y < gy_; ++y) {
        for (uint32_t x = 0; x < gx_; ++x) {
            const int i = idx3_((int)x, (int)y, (int)slice_z);
            if (bh_exclude_u8_[(size_t)i] != 0u) {
                const size_t o = ((size_t)y * (size_t)gx_ + (size_t)x) * 4;
                out_bgra8[o + 0] = 0;
                out_bgra8[o + 1] = 0;
                out_bgra8[o + 2] = 0;
                out_bgra8[o + 3] = 255;
                L0_[i] = 0.0f;
                L1_[i] = 0.0f;
                L2_[i] = 0.0f;
                L3_[i] = 0.0f;
                continue;
            }
            const float D4 = flux_[i];
            const float D5 = coherence_[i];
            const float D6 = curvature_[i];
            const float D7 = doppler_[i];

            const float rho = D4 * D4 + D6 * D6;
            const float gamma_t = 1.0f / (1.0f + mean_abs_time);
            const float Ic = (D5 * D5) / (1.0f + mean_rho);
            const float aD7 = (D7 >= 0.0f) ? D7 : -D7;
            const float kD = D7 / (1.0f + aD7);
            const float L = rho * gamma_t * Ic;

            L0_[i] = L;
            L1_[i] = kD;
            L2_[i] = rho;
            L3_[i] = Ic;

            float intensity = L;
            if (intensity < 0.0f) intensity = 0.0f;
            if (intensity > 1.0f) intensity = 1.0f;
            const uint8_t I = (uint8_t)(intensity * 255.0f);

            float kd01 = (kD * 0.5f + 0.5f);
            if (kd01 < 0.0f) kd01 = 0.0f;
            if (kd01 > 1.0f) kd01 = 1.0f;
            const uint8_t C = (uint8_t)(kd01 * 255.0f);

            const size_t o = ((size_t)y * (size_t)gx_ + (size_t)x) * 4;
            out_bgra8[o + 0] = I;
            out_bgra8[o + 1] = C;
            out_bgra8[o + 2] = I;
            out_bgra8[o + 3] = 255;
        }
    }

    frame_seq_++;
    out_hdr.frame_seq_begin = frame_seq_;
    out_hdr.tick_index = tick_index_;
    out_hdr.grid_x = gx_;
    out_hdr.grid_y = gy_;
    out_hdr.grid_z = gz_;
    out_hdr.slice_z = slice_z;
    out_hdr.frame_seq_end = frame_seq_;
}
