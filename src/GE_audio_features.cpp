#include "GE_audio_features.hpp"

#include <cmath>
#include <algorithm>

namespace genesis {

static uint32_t ge_q16_from_double(double x) {
    if (x <= 0.0) return 0;
    double q = x * 65536.0;
    if (q > 4294967295.0) q = 4294967295.0;
    return (uint32_t)std::llround(q);
}

static uint32_t ge_q16_from_double_signed(double x) {
    // helper for internal use; clamps to uint32
    if (x <= 0.0) return 0;
    double q = x * 65536.0;
    if (q > 4294967295.0) q = 4294967295.0;
    return (uint32_t)std::llround(q);
}

// Deterministic autocorrelation pitch estimator (very small, CPU reference).
// Returns f0 in Hz; 0 if unvoiced.
static double ge_estimate_f0_hz_autocorr(const int16_t* x, uint32_t n, uint32_t sr, double* voiced_conf_out) {
    if (voiced_conf_out) *voiced_conf_out = 0.0;
    if (!x || n < 64 || sr < 8000) return 0.0;

    // Basic energy gate.
    double sum_sq = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        double v = (double)x[i] / 32768.0;
        sum_sq += v * v;
    }
    double rms = std::sqrt(sum_sq / (double)n);
    if (rms < 0.01) return 0.0;

    // Remove DC.
    double mean = 0.0;
    for (uint32_t i = 0; i < n; ++i) mean += (double)x[i];
    mean /= (double)n;

    // Search typical voice range ~60..400 Hz
    const uint32_t lag_min = std::max(1u, (uint32_t)std::floor((double)sr / 400.0));
    const uint32_t lag_max = std::min(n - 2u, (uint32_t)std::ceil((double)sr / 60.0));
    if (lag_min >= lag_max) return 0.0;

    // Precompute denominator (energy).
    double denom = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        double v = ((double)x[i] - mean) / 32768.0;
        denom += v * v;
    }
    if (denom <= 1e-12) return 0.0;

    double best_corr = -1.0;
    uint32_t best_lag = 0;

    for (uint32_t lag = lag_min; lag <= lag_max; ++lag) {
        double num = 0.0;
        for (uint32_t i = 0; i + lag < n; ++i) {
            double a = ((double)x[i] - mean) / 32768.0;
            double b = ((double)x[i + lag] - mean) / 32768.0;
            num += a * b;
        }
        double corr = num / denom;
        if (corr > best_corr) { best_corr = corr; best_lag = lag; }
    }

    // Heuristic voiced threshold.
    if (best_corr < 0.25 || best_lag == 0) {
        if (voiced_conf_out) *voiced_conf_out = std::max(0.0, best_corr);
        return 0.0;
    }

    if (voiced_conf_out) {
        double vc = best_corr;
        if (vc < 0.0) vc = 0.0;
        if (vc > 1.0) vc = 1.0;
        *voiced_conf_out = vc;
    }

    return (double)sr / (double)best_lag;
}

// Naive DFT magnitude for centroid (small N only; deterministic).
static double ge_centroid_hz(const int16_t* x, uint32_t n, uint32_t sample_rate) {
    if (n < 8) return 0.0;
    double sum_mag = 0.0;
    double sum_wmag = 0.0;
    const uint32_t kmax = n / 2;
    for (uint32_t k = 1; k < kmax; ++k) {
        double re = 0.0;
        double im = 0.0;
        for (uint32_t t = 0; t < n; ++t) {
            double a = 2.0 * 3.141592653589793 * (double)k * (double)t / (double)n;
            double v = (double)x[t] / 32768.0;
            re += v * std::cos(a);
            im -= v * std::sin(a);
        }
        double mag = std::sqrt(re * re + im * im) + 1e-12;
        double freq = ((double)k * (double)sample_rate) / (double)n;
        sum_mag += mag;
        sum_wmag += mag * freq;
    }
    if (sum_mag <= 0.0) return 0.0;
    return sum_wmag / sum_mag;
}

std::vector<AudioFrameFeatures> ge_extract_audio_features(const WavPcm16Mono& wav, const AudioFeatureConfig& cfg) {
    const uint32_t frame_n = (cfg.frame_samples_u32 == 0) ? 1024u : cfg.frame_samples_u32;
    const uint32_t hop = (cfg.hop_samples_u32 == 0) ? (frame_n / 4u) : cfg.hop_samples_u32;
    const uint32_t sr = (wav.sample_rate_hz_u32 == 0) ? 48000u : wav.sample_rate_hz_u32;

    std::vector<AudioFrameFeatures> out;
    if (wav.samples_i16.empty() || frame_n > wav.samples_i16.size()) return out;

    for (uint32_t start = 0; start + frame_n <= wav.samples_i16.size(); start += hop) {
        const int16_t* x = wav.samples_i16.data() + start;

        double sum_sq = 0.0;
        uint32_t zc = 0;
        int16_t prev = x[0];
        for (uint32_t i = 0; i < frame_n; ++i) {
            double v = (double)x[i] / 32768.0;
            sum_sq += v * v;
            if ((prev < 0 && x[i] >= 0) || (prev >= 0 && x[i] < 0)) zc++;
            prev = x[i];
        }
        double rms = std::sqrt(sum_sq / (double)frame_n);
        double zcr = (double)zc / (double)frame_n;
        double centroid = ge_centroid_hz(x, frame_n, sr);

        double voiced_conf = 0.0;
        double f0_hz = ge_estimate_f0_hz_autocorr(x, frame_n, sr, &voiced_conf);

        AudioFrameFeatures f;
        f.rms_q16_u32 = ge_q16_from_double(rms);
        f.zcr_q16_u32 = ge_q16_from_double(zcr);
        f.centroid_hz_q16_u32 = ge_q16_from_double(centroid);
        f.f0_hz_q16_u32 = ge_q16_from_double_signed(f0_hz);
        f.voiced_q16_u32 = ge_q16_from_double(voiced_conf);
        out.push_back(f);
    }
    return out;
}

} // namespace genesis
