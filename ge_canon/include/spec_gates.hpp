#pragma once

#include <cstdint>
#include <stdexcept>

// Spec-mandated gate exceptions.
// These are deliberately minimal and deterministic.

struct PhaseViolation : public std::runtime_error {
    explicit PhaseViolation(const char* msg) : std::runtime_error(msg) {}
};

struct CoherenceCollapse : public std::runtime_error {
    explicit CoherenceCollapse(const char* msg) : std::runtime_error(msg) {}
};

struct SpecViolation : public std::runtime_error {
    explicit SpecViolation(const char* msg) : std::runtime_error(msg) {}
};

// Tick context (Spec 9.2.4): all mutating functions must carry tick_index + delta_tick.
struct TickContext {
    int64_t tick_index = 0;
    int64_t delta_tick = 0;
};

// Spec 9.2.1: phase domain choke point.
// Returns theta_fp modulo theta_scale (positive ring wrap).
inline int64_t enforce_phase_domain(int64_t theta_fp, int64_t theta_scale) {
    if (theta_scale <= 0) {
        throw PhaseViolation("theta_scale_invalid");
    }
    int64_t r = theta_fp % theta_scale;
    if (r < 0) r += theta_scale;
    return r;
}

// Spec 9.2.3: coherence hard gate.
inline void enforce_coherence_gate(int64_t chi_q, int64_t chi_min_q) {
    if (chi_q < chi_min_q) {
        throw CoherenceCollapse("chi_below_min");
    }
}

// -----------------------------------------------------------------------------
// Learning pipeline honesty gate helpers (canonical default: 6% relative error)
// Note: these are deterministic utilities intended for learning/crawler gating,
// not for replacing core physics phase/coherence gates.
// -----------------------------------------------------------------------------

static constexpr int64_t DEFAULT_REL_TOLERANCE_PPM = 60000; // 6.000% in parts-per-million
static constexpr int64_t DEFAULT_REL_TOLERANCE_NUM = 6;     // 6%
static constexpr int64_t DEFAULT_REL_TOLERANCE_DEN = 100;   // /100

// Returns true if |sim-target|/max(|target|,eps) <= tol (relative tolerance).
inline bool within_rel_tolerance_i64(int64_t sim, int64_t target, int64_t tol_num = DEFAULT_REL_TOLERANCE_NUM, int64_t tol_den = DEFAULT_REL_TOLERANCE_DEN, int64_t eps = 1) {
    const int64_t denom = (target >= 0 ? target : -target);
    const int64_t d = (denom > eps ? denom : eps);
    const int64_t diff = (sim >= target ? sim - target : target - sim);
    // diff/d <= tol_num/tol_den  => diff*tol_den <= d*tol_num
    return (diff * tol_den) <= (d * tol_num);
}

