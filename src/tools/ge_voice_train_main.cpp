#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>

#include "GE_speech_alignment.hpp"
#include "GE_audio_wav.hpp"
#include "GE_audio_features.hpp"
#include "GE_g2p_lexicon.hpp"
#include "GE_forced_aligner.hpp"
#include "GE_voice_synth.hpp"
#include "GE_voice_predictive_model.hpp"
#include "GE_prosody_priors.hpp"
#include "GE_phrase_priors.hpp"
#include "GE_pause_priors.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_voice_train usage:\n");
    std::printf("  ge_voice_train --manifest <wav_text.tsv> --out_model model.json [--out_priors model.json.priors.ewcfg] [--out_phrase_priors model.json.phrase_priors.ewcfg] [--out_pause_priors model.json.pause_priors.ewcfg] [--max N] [--base_f0 130] [--lambda 0.001] [--no_align]\n");
    std::printf("  ge_voice_train --commonvoice_tsv validated.tsv --clips_dir clips --out_model model.json [--out_priors model.json.priors.ewcfg] [--out_phrase_priors model.json.phrase_priors.ewcfg] [--out_pause_priors model.json.pause_priors.ewcfg] [--max N] [--base_f0 130] [--lambda 0.001] [--no_align]\n");
    std::printf("Trains a deterministic prosody model (dur, f0_ratio, amp_ratio).\n");
    std::printf("By default, uses a deterministic DTW forced aligner to derive per-phoneme spans from audio features.\n");
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
    std::string out_priors;
    std::string out_phrase_priors;
    std::string out_pause_priors;
    uint32_t max_n = 4000;
    uint32_t base_f0_hz = 130;
    double lambda = 0.001;
    bool use_align = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--commonvoice_tsv" && i + 1 < argc) tsv = argv[++i];
        else if (a == "--clips_dir" && i + 1 < argc) clips = argv[++i];
        else if (a == "--manifest" && i + 1 < argc) manifest = argv[++i];
        else if (a == "--out_model" && i + 1 < argc) out_model = argv[++i];
        else if (a == "--out_priors" && i + 1 < argc) out_priors = argv[++i];
        else if (a == "--out_phrase_priors" && i + 1 < argc) out_phrase_priors = argv[++i];
        else if (a == "--out_pause_priors" && i + 1 < argc) out_pause_priors = argv[++i];
        else if (a == "--max" && i + 1 < argc) max_n = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--base_f0" && i + 1 < argc) base_f0_hz = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--lambda" && i + 1 < argc) lambda = std::stod(argv[++i]);
        else if (a == "--no_align") use_align = false;
        else if (a == "--help") { usage(); return 0; }
    }

    if (out_model.empty()) { usage(); return 2; }
    if (out_priors.empty()) out_priors = out_model + ".priors.ewcfg";
    if (out_phrase_priors.empty()) out_phrase_priors = out_model + ".phrase_priors.ewcfg";
    if (out_pause_priors.empty()) out_pause_priors = out_model + ".pause_priors.ewcfg";

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

    // Global priors accumulators (Q16.16 sums).
    struct PriorAcc { std::string phone; uint64_t sum_dur_q16=0, sum_f0r_q16=0, sum_ampr_q16=0; uint32_t n=0; };
    std::vector<PriorAcc> pri_acc;
    pri_acc.reserve(128);
    auto pri_find_or_add = [&](const std::string& ph) -> size_t {
        for (size_t i = 0; i < pri_acc.size(); ++i) if (pri_acc[i].phone == ph) return i;
        PriorAcc a; a.phone = ph; pri_acc.push_back(a); return pri_acc.size()-1;
    };

    // Phrase priors accumulators.
    struct PhraseAcc { PhraseMode mode; PhrasePos pos; uint64_t d=0,f0=0,a=0; uint32_t n=0; };
    std::vector<PhraseAcc> phr_acc;
    auto phr_find_or_add = [&](PhraseMode m, PhrasePos p) -> size_t {
        for (size_t i=0;i<phr_acc.size();++i) if (phr_acc[i].mode==m && phr_acc[i].pos==p) return i;
        PhraseAcc x; x.mode=m; x.pos=p; phr_acc.push_back(x); return phr_acc.size()-1;
    };

    // Pause priors accumulators.
    struct PauseAcc { PauseKind kind; uint8_t bin_u8; uint64_t sum_dur_q16=0; uint32_t n=0; };
    std::vector<PauseAcc> pau_acc;
    auto pau_find_or_add = [&](PauseKind k, uint8_t bin_u8) -> size_t {
        for (size_t i=0;i<pau_acc.size();++i) if (pau_acc[i].kind==k && pau_acc[i].bin_u8==bin_u8) return i;
        PauseAcc x; x.kind=k; x.bin_u8=bin_u8; pau_acc.push_back(x); return pau_acc.size()-1;
    };

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

        // Phones from text (deterministic) + explicit pause kinds aligned to phones.
        std::vector<PhonemeSpan> phones;
        std::vector<PauseKind> pause_kinds;
    std::vector<uint8_t> pause_strength_u8;
        if (!ge_text_to_phones_english_with_pause_meta(s.text_utf8, &phones, &pause_kinds) || phones.empty()) continue;

        const PhraseMode mode = ge_phrase_mode_from_text(s.text_utf8);

        // Forced alignment spans (frame indices). If unavailable, fall back to equal split.
        std::vector<ForcedAlignSpan> spans;
        spans.reserve(phones.size());
        if (use_align) {
            std::vector<std::string> phone_str;
            phone_str.reserve(phones.size());
            for (const auto& ph : phones) phone_str.push_back(ph.phone);

            ForcedAlignConfig acfg;
            acfg.feat_cfg = fcfg;
            auto ar = ge_forced_align_dtw(frames, phone_str, acfg);
            if (ar.ok) spans = std::move(ar.spans);
        }
        if (spans.empty()) {
            const uint32_t T = (uint32_t)frames.size();
            const uint32_t P = (uint32_t)phones.size();
            for (uint32_t i = 0; i < P; ++i) {
                ForcedAlignSpan sp;
                sp.phone = phones[i].phone;
                sp.frame_start_u32 = (uint32_t)((uint64_t)i * (uint64_t)T / (uint64_t)P);
                sp.frame_end_u32 = (uint32_t)((uint64_t)(i + 1u) * (uint64_t)T / (uint64_t)P);
                spans.push_back(sp);
            }
        }

        // Update global priors (auditable per-phone averages) from spans + frames.
        {
            std::vector<std::string> phone_str;
            std::vector<uint32_t> ss;
            std::vector<uint32_t> se;
            phone_str.reserve(spans.size());
            ss.reserve(spans.size());
            se.reserve(spans.size());
            for (const auto& sp : spans) { phone_str.push_back(sp.phone); ss.push_back(sp.frame_start_u32); se.push_back(sp.frame_end_u32); }

            std::vector<uint32_t> rms_q16, f0_q16, vc_q16;
            rms_q16.reserve(frames.size());
            f0_q16.reserve(frames.size());
            vc_q16.reserve(frames.size());
            for (const auto& fr : frames) {
                rms_q16.push_back(fr.rms_q16_u32);
                f0_q16.push_back(fr.f0_hz_q16_u32);
                vc_q16.push_back(fr.voiced_q16_u32);
            }

            ProsodyPriors one;
            if (ge_prosody_priors_build_from_alignment(phone_str, ss, se, rms_q16, f0_q16, vc_q16,
                    fcfg.hop_samples_u32, wav.sample_rate_hz_u32, base_f0_hz, &one)) {
                for (const auto& r : one.rows) {
                    size_t idx = pri_find_or_add(r.phone);
                    pri_acc[idx].sum_dur_q16 += (uint64_t)r.mean_dur_ms_q16_u32 * (uint64_t)r.count_u32;
                    pri_acc[idx].sum_f0r_q16 += (uint64_t)r.mean_f0_ratio_q16_u32 * (uint64_t)r.count_u32;
                    pri_acc[idx].sum_ampr_q16 += (uint64_t)r.mean_amp_ratio_q16_u32 * (uint64_t)r.count_u32;
                    pri_acc[idx].n += r.count_u32;
                }
            }
        }

        // Accumulate per-phone training rows using spans.
        // Also collect observed per-phone controls for phrase priors.
        std::vector<std::string> phone_str_obs;
        std::vector<uint32_t> dur_ms_q16_obs;
        std::vector<uint32_t> f0r_q16_obs;
        std::vector<uint32_t> ampr_q16_obs;
        phone_str_obs.reserve(phones.size());
        dur_ms_q16_obs.reserve(phones.size());
        f0r_q16_obs.reserve(phones.size());
        ampr_q16_obs.reserve(phones.size());
        for (uint32_t i = 0; i < (uint32_t)phones.size(); ++i) {
            auto feat = ge_voice_phone_features(phones, i);
            feat.resize(D, 0.0);

            const uint32_t fs = (i < spans.size()) ? spans[i].frame_start_u32 : 0u;
            const uint32_t fe = (i < spans.size()) ? spans[i].frame_end_u32 : 0u;
            const uint32_t fsa = std::min(fs, (uint32_t)frames.size());
            const uint32_t fea = std::min(std::max(fe, fsa), (uint32_t)frames.size());
            const uint32_t span_n = (fea > fsa) ? (fea - fsa) : 0u;

            const double frame_hop_s = (double)fcfg.hop_samples_u32 / (double)wav.sample_rate_hz_u32;
            double y_dur = (span_n > 0) ? (1000.0 * frame_hop_s * (double)span_n) : (audio_ms / (double)phones.size());
            if (y_dur < 10.0) y_dur = 10.0;
            if (y_dur > 800.0) y_dur = 800.0;

            double span_f0_sum = 0.0;
            double span_f0_w = 0.0;
            double span_rms_sum = 0.0;
            if (span_n > 0) {
                for (uint32_t k = fsa; k < fea; ++k) {
                    const auto& fr = frames[k];
                    double rms = (double)fr.rms_q16_u32 / 65536.0;
                    span_rms_sum += rms;
                    double f0 = (double)fr.f0_hz_q16_u32 / 65536.0;
                    double vc = (double)fr.voiced_q16_u32 / 65536.0;
                    if (f0 > 20.0 && vc > 0.2) {
                        span_f0_sum += f0 * vc;
                        span_f0_w += vc;
                    }
                }
            }
            double span_f0 = (span_f0_w > 1e-6) ? (span_f0_sum / span_f0_w) : f0_mean;
            double span_rms = (span_n > 0) ? (span_rms_sum / (double)span_n) : rms_mean;

            double y_f0r = span_f0 / (double)base_f0_hz;
            if (y_f0r < 0.5) y_f0r = 0.5;
            if (y_f0r > 2.0) y_f0r = 2.0;

            const double rms_base = 0.08;
            double y_ampr = (rms_base > 1e-6) ? (span_rms / rms_base) : 1.0;
            if (y_ampr < 0.2) y_ampr = 0.2;
            if (y_ampr > 2.0) y_ampr = 2.0;

            phone_str_obs.push_back(phones[i].phone);
            dur_ms_q16_obs.push_back((uint32_t)(y_dur * 65536.0 + 0.5));
            f0r_q16_obs.push_back((uint32_t)(y_f0r * 65536.0 + 0.5));
            ampr_q16_obs.push_back((uint32_t)(y_ampr * 65536.0 + 0.5));

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

        // Per-sample phrase priors (mode + start/mid/end/solo), accumulated globally.
        {
            PhrasePriors ppri;
            if (ge_phrase_priors_build_from_observations(mode, phone_str_obs, dur_ms_q16_obs, f0r_q16_obs, ampr_q16_obs, &ppri) && ppri.ok) {
                for (const auto& r : ppri.rows) {
                    size_t idx = phr_find_or_add(r.mode, r.pos);
                    phr_acc[idx].d += (uint64_t)r.dur_mul_q16_u32 * (uint64_t)r.count_u32;
                    phr_acc[idx].f0 += (uint64_t)r.f0_mul_q16_u32 * (uint64_t)r.count_u32;
                    phr_acc[idx].a += (uint64_t)r.amp_mul_q16_u32 * (uint64_t)r.count_u32;
                    phr_acc[idx].n += r.count_u32;
                }
            }
        }

        // Per-sample pause priors (space/comma/clause/terminal/newline), accumulated globally.
        {
            PausePriors zp;
            if (ge_pause_priors_build_from_observations(pause_kinds, pause_strength_u8, phone_str_obs, dur_ms_q16_obs, &zp) && zp.ok) {
                for (const auto& r : zp.rows) {
                    size_t idx = pau_find_or_add(r.kind, r.strength_bin_u8);
                    pau_acc[idx].sum_dur_q16 += (uint64_t)r.mean_dur_mul_q16_u32 * (uint64_t)r.count_u32;
                    pau_acc[idx].n += r.count_u32;
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

    // Finalize and write deterministic per-phone priors.
    {
        ProsodyPriors pri;
        pri.ok = false;
        pri.info.clear();
        pri.rows.clear();
        pri.rows.reserve(pri_acc.size());
        for (const auto& a : pri_acc) {
            if (a.n == 0) continue;
            ProsodyPriorRow r;
            r.phone = a.phone;
            r.count_u32 = a.n;
            r.mean_dur_mul_q16_u32 = (uint32_t)(a.sum_dur_q16 / (uint64_t)a.n);
            r.mean_f0_ratio_q16_u32 = (uint32_t)(a.sum_f0r_q16 / (uint64_t)a.n);
            r.mean_amp_ratio_q16_u32 = (uint32_t)(a.sum_ampr_q16 / (uint64_t)a.n);
            pri.rows.push_back(std::move(r));
        }
        std::sort(pri.rows.begin(), pri.rows.end(), [](const ProsodyPriorRow& x, const ProsodyPriorRow& y){ return x.phone < y.phone; });
        pri.ok = !pri.rows.empty();
        pri.info = pri.ok ? "ok" : "no_rows";
        if (pri.ok) {
            if (!ge_prosody_priors_write_ewcfg(out_priors, pri)) {
                std::printf("priors_save_failed out=%s\n", out_priors.c_str());
                return 6;
            }
            std::printf("priors_ok out=%s rows=%u\n", out_priors.c_str(), (uint32_t)pri.rows.size());
        }
    }

    // Finalize and write phrase-level priors.
    {
        PhrasePriors pri;
        pri.ok = false;
        pri.rows.clear();
        for (const auto& a : phr_acc) {
            if (a.n == 0) continue;
            PhrasePriorRow r;
            r.mode = a.mode;
            r.pos = a.pos;
            r.count_u32 = a.n;
            r.dur_mul_q16_u32 = (uint32_t)(a.d / (uint64_t)a.n);
            r.f0_mul_q16_u32  = (uint32_t)(a.f0 / (uint64_t)a.n);
            r.amp_mul_q16_u32 = (uint32_t)(a.a / (uint64_t)a.n);
            pri.rows.push_back(r);
        }
        pri.ok = !pri.rows.empty();
        pri.info = pri.ok ? "ok" : "no_rows";
        if (pri.ok) {
            if (!ge_phrase_priors_write_ewcfg(out_phrase_priors, pri)) {
                std::printf("phrase_priors_save_failed out=%s\n", out_phrase_priors.c_str());
                return 7;
            }
            std::printf("phrase_priors_ok out=%s rows=%u\n", out_phrase_priors.c_str(), (uint32_t)pri.rows.size());
        }
    }

    // Finalize and write pause-class priors.
    {
        PausePriors pri;
        pri.ok = false;
        pri.rows.clear();
        for (const auto& a : pau_acc) {
            if (a.n == 0) continue;
            PausePriorRow r;
            r.kind = a.kind;
            r.count_u32 = a.n;
            r.strength_bin_u8 = a.bin_u8;
            r.mean_dur_mul_q16_u32 = (uint32_t)(a.sum_dur_q16 / (uint64_t)a.n);
            pri.rows.push_back(r);
        }
        std::sort(pri.rows.begin(), pri.rows.end(), [](const PausePriorRow& x, const PausePriorRow& y){
            if ((uint32_t)x.kind != (uint32_t)y.kind) return (uint32_t)x.kind < (uint32_t)y.kind;
            return (uint32_t)x.strength_bin_u8 < (uint32_t)y.strength_bin_u8;
        });
        pri.ok = !pri.rows.empty();
        pri.info = pri.ok ? "ok" : "no_rows";
        if (pri.ok) {
            if (!ge_pause_priors_write_ewcfg(out_pause_priors, pri)) {
                std::printf("pause_priors_save_failed out=%s\n", out_pause_priors.c_str());
                return 8;
            }
            std::printf("pause_priors_ok out=%s rows=%u\n", out_pause_priors.c_str(), (uint32_t)pri.rows.size());
        }
    }

    std::printf("voice_train_ok used=%u out=%s base_dur=%.2f base_f0r=%.3f base_ampr=%.3f\n",
        used, out_model.c_str(), m.base_dur_ms, m.base_f0_ratio, m.base_amp_ratio);
    return 0;
}
