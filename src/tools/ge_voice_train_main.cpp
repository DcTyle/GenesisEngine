#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>

#include "GE_speech_alignment.hpp"
#include "GE_audio_wav.hpp"
#include "GE_audio_features.hpp"
#include "GE_voice_synth.hpp"
#include "GE_voice_predictive_model.hpp"
#include "GE_forced_aligner.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_voice_train usage:\n");
    std::printf("  ge_voice_train --manifest <wav_text.tsv> --out_model model.json [--max N] [--base_f0 130] [--lambda 0.001]\n");
    std::printf("  ge_voice_train --commonvoice_tsv validated.tsv --clips_dir clips --out_model model.json [--max N] [--base_f0 130] [--lambda 0.001]\n");
    std::printf("Trains a tiny deterministic prosody model (dur, f0_ratio, amp_ratio) from aligned wav+text.\n");
}

static bool solve_linear_system_gauss_jordan(std::vector<double>* A, std::vector<double>* b, uint32_t n) {
    // A is row-major n*n, b is size n. In-place.
    if (!A || !b) return false;
    if (A->size() != (size_t)n * (size_t)n) return false;
    if (b->size() != (size_t)n) return false;

    for (uint32_t col = 0; col < n; ++col) {
        // pivot
        uint32_t piv = col;
        double best = std::fabs((*A)[(size_t)col * n + col]);
        for (uint32_t r = col + 1; r < n; ++r) {
            double v = std::fabs((*A)[(size_t)r * n + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) return false;

        if (piv != col) {
            for (uint32_t c = 0; c < n; ++c) std::swap((*A)[(size_t)col * n + c], (*A)[(size_t)piv * n + c]);
            std::swap((*b)[col], (*b)[piv]);
        }

        double diag = (*A)[(size_t)col * n + col];
        double inv = 1.0 / diag;
        for (uint32_t c = 0; c < n; ++c) (*A)[(size_t)col * n + c] *= inv;
        (*b)[col] *= inv;

        for (uint32_t r = 0; r < n; ++r) {
            if (r == col) continue;
            double f = (*A)[(size_t)r * n + col];
            if (std::fabs(f) < 1e-18) continue;
            for (uint32_t c = 0; c < n; ++c) {
                (*A)[(size_t)r * n + c] -= f * (*A)[(size_t)col * n + c];
            }
            (*b)[r] -= f * (*b)[col];
        }
    }
    return true;
}

struct Accum {
    double sum = 0.0;
    uint64_t n = 0;
    void add(double v) { sum += v; n++; }
    double mean(double def=0.0) const { return (n==0) ? def : (sum / (double)n); }
};

int main(int argc, char** argv) {
    std::string tsv;
    std::string clips;
    std::string manifest;
    std::string out_model = "voice_model.json";
    uint32_t max_n = 4000;
    uint32_t base_f0_hz = 130;
    double lambda = 0.001;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--commonvoice_tsv" && i + 1 < argc) tsv = argv[++i];
        else if (a == "--clips_dir" && i + 1 < argc) clips = argv[++i];
        else if (a == "--manifest" && i + 1 < argc) manifest = argv[++i];
        else if (a == "--out_model" && i + 1 < argc) out_model = argv[++i];
        else if (a == "--max" && i + 1 < argc) max_n = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--base_f0" && i + 1 < argc) base_f0_hz = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--lambda" && i + 1 < argc) lambda = std::stod(argv[++i]);
        else if (a == "--help") { usage(); return 0; }
    }

    if (out_model.empty()) { usage(); return 2; }

    SpeechCorpusLoadResult lr;
    if (!manifest.empty()) lr = ge_load_wav_text_manifest(manifest, max_n);
    else if (!tsv.empty()) lr = ge_load_common_voice_tsv(tsv, clips, max_n);
    else { usage(); return 2; }

    if (!lr.ok) {
        std::printf("load_failed: %s\n", lr.info.c_str());
        return 3;
    }

    // Fixed feature size from ge_voice_phone_features.
    const uint32_t D = 7;

    // Normal equations accumulators.
    std::vector<double> XtX((size_t)D * (size_t)D, 0.0);
    std::vector<double> XtY_dur(D, 0.0);
    std::vector<double> XtY_f0r(D, 0.0);
    std::vector<double> XtY_ampr(D, 0.0);

    Accum dur_mean;
    Accum f0r_mean;
    Accum ampr_mean;

    uint32_t used = 0;
    AudioFeatureConfig fcfg;
    fcfg.frame_samples_u32 = 1024;
    fcfg.hop_samples_u32 = 256;

    for (const auto& s : lr.samples) {
        WavPcm16Mono wav;
        if (!ge_wav_read_pcm16_mono(s.wav_path, &wav)) continue;
        if (wav.sample_rate_hz_u32 == 0 || wav.samples_i16.size() < 2048) continue;

        auto frames = ge_extract_audio_features(wav, fcfg);
        if (frames.empty()) continue;

        // Derive targets from audio.
        double audio_ms = 1000.0 * (double)wav.samples_i16.size() / (double)wav.sample_rate_hz_u32;

        // Mean voiced f0 and mean rms.
        double f0_sum = 0.0;
        double f0_w = 0.0;
        double rms_sum = 0.0;
        for (const auto& fr : frames) {
            double rms = (double)fr.rms_q16_u32 / 65536.0;
            rms_sum += rms;
            double f0 = (double)fr.f0_hz_q16_u32 / 65536.0;
            double vc = (double)fr.voiced_q16_u32 / 65536.0;
            if (f0 > 20.0 && vc > 0.2) {
                f0_sum += f0 * vc;
                f0_w += vc;
            }
        }
        double f0_mean = (f0_w > 1e-6) ? (f0_sum / f0_w) : (double)base_f0_hz;
        double rms_mean = rms_sum / (double)frames.size();

        double f0_ratio = f0_mean / (double)base_f0_hz;
        if (f0_ratio < 0.5) f0_ratio = 0.5;
        if (f0_ratio > 2.0) f0_ratio = 2.0;

        // Use a stable baseline rms of 0.08 (rough speech-ish in normalized PCM) so ratios are reasonable.
        const double rms_base = 0.08;
        double amp_ratio = (rms_base > 1e-6) ? (rms_mean / rms_base) : 1.0;
        if (amp_ratio < 0.2) amp_ratio = 0.2;
        if (amp_ratio > 2.0) amp_ratio = 2.0;

        // Phones from text (deterministic).
        auto phones = ge_text_to_phones_english(s.text_utf8);
        // Forced alignment (phones <-> audio frames) for measurable per-phone durations.
        // Fail-closed per spec: if alignment fails, skip the sample deterministically.
        ForcedAlignResult ar = ge_forced_align_phones_to_audio_frames(phones, frames);
        if (!ar.ok) continue;
        std::vector<uint32_t> durs = ge_alignment_phone_durations_ms(
            phones, frames, fcfg.hop_samples_u32, wav.sample_rate_hz_u32, ar);
        if (durs.size() != phones.size()) continue;
        for (size_t pi = 0; pi < phones.size(); ++pi) {
            if (phones[pi].phone == "WB") continue;
            if (durs[pi] > 0) phones[pi].dur_ms_u32 = durs[pi];
        }
        if (phones.empty()) continue;

        double dur_per_phone = audio_ms / (double)phones.size();
        if (dur_per_phone < 20.0 || dur_per_phone > 600.0) continue;

        // Accumulate per-phone training rows.
        for (uint32_t i = 0; i < (uint32_t)phones.size(); ++i) {
            auto feat = ge_voice_phone_features(phones, i);
            feat.resize(D, 0.0);

            // Targets. We keep it simple: per-phone duration is baseline with a vowel boost.
            // This is enough to start prosody shaping, then you can refine with forced alignment.
            std::string p = phones[i].phone;
            std::string pu = p; for (char& c : pu) c = (char)std::toupper((unsigned char)c);
            bool is_vowel = (pu != "SP") && (pu.rfind("AA",0)==0 || pu.rfind("AE",0)==0 || pu.rfind("AH",0)==0 || pu.rfind("AO",0)==0 || pu.rfind("AW",0)==0 || pu.rfind("AY",0)==0 || pu.rfind("EH",0)==0 || pu.rfind("ER",0)==0 || pu.rfind("EY",0)==0 || pu.rfind("IH",0)==0 || pu.rfind("IY",0)==0 || pu.rfind("OW",0)==0 || pu.rfind("OY",0)==0 || pu.rfind("UH",0)==0 || pu.rfind("UW",0)==0);

            double y_dur = dur_per_phone * (is_vowel ? 1.15 : 0.90);
            if (pu == "SP") y_dur = std::max(phones[i].dur_ms_u32 * 1.0, 80.0);

            double y_f0r = f0_ratio;
            double y_ampr = amp_ratio;

            // Update means (for baseline extraction)
            dur_mean.add(y_dur);
            f0r_mean.add(y_f0r);
            ampr_mean.add(y_ampr);

            // XtX and XtY
            for (uint32_t r = 0; r < D; ++r) {
                XtY_dur[r] += feat[r] * y_dur;
                XtY_f0r[r] += feat[r] * y_f0r;
                XtY_ampr[r] += feat[r] * y_ampr;
                for (uint32_t c = 0; c < D; ++c) {
                    XtX[(size_t)r * D + c] += feat[r] * feat[c];
                }
            }
        }

        used++;
        if (used >= max_n) break;
    }

    if (used < 20) {
        std::printf("not_enough_samples used=%u\n", used);
        return 4;
    }

    // Ridge regularization.
    for (uint32_t i = 0; i < D; ++i) XtX[(size_t)i * D + i] += lambda;

    // Solve three systems.
    auto solve = [&](const std::vector<double>& XtY) -> std::vector<double> {
        std::vector<double> A = XtX;
        std::vector<double> b = XtY;
        if (!solve_linear_system_gauss_jordan(&A, &b, D)) {
            // fallback zeros
            return std::vector<double>(D, 0.0);
        }
        return b;
    };

    VoicePredictiveModel m;
    m.ok = true;
    m.info = "ok";
    m.feat_dim_u32 = D;

    // Baselines as means; weights then model residuals around baseline.
    m.base_dur_ms = dur_mean.mean(80.0);
    m.base_f0_ratio = f0r_mean.mean(1.0);
    m.base_amp_ratio = ampr_mean.mean(1.0);

    // Adjust XtY to fit residuals: y' = y - base.
    // Since our feature vector includes bias term 1.0, the solver can also capture base;
    // but we keep explicit baselines for stability.
    std::vector<double> XtY_dur_res = XtY_dur;
    std::vector<double> XtY_f0r_res = XtY_f0r;
    std::vector<double> XtY_ampr_res = XtY_ampr;
    // Subtract base contribution from XtY for the bias feature (index 0).
    XtY_dur_res[0] -= m.base_dur_ms * XtX[0];
    XtY_f0r_res[0] -= m.base_f0_ratio * XtX[0];
    XtY_ampr_res[0] -= m.base_amp_ratio * XtX[0];

    m.w_dur = solve(XtY_dur_res);
    m.w_f0_ratio = solve(XtY_f0r_res);
    m.w_amp_ratio = solve(XtY_ampr_res);

    if (!ge_voice_model_save(out_model, m)) {
        std::printf("save_failed out=%s\n", out_model.c_str());
        return 5;
    }

    std::printf("voice_train_ok used=%u out=%s base_dur=%.2f base_f0r=%.3f base_ampr=%.3f\n",
        used, out_model.c_str(), m.base_dur_ms, m.base_f0_ratio, m.base_amp_ratio);
    return 0;
}
