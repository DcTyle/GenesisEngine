#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "GE_runtime.hpp"
#include "ew_cli_args.hpp"

static void print_u64_hex(const char* label, uint64_t v) {
    std::printf("%s=0x%016llx\n", label, (unsigned long long)v);
}

int main(int argc, char** argv) {
    const ew::EwCliArgs args = ew::ew_cli_parse_kv_ascii(argc, argv);

    std::string lang_dir;
    uint32_t ticks = 600;

    if (const char* v = ew::ew_cli_get_str(args, "lang-bootstrap")) lang_dir = v;
    if (lang_dir.empty()) {
        if (const char* v2 = ew::ew_cli_get_str(args, "lang_bootstrap")) lang_dir = v2;
    }
    (void)ew::ew_cli_get_u32(args, "ticks", ticks);

    SubstrateManager sm(64);
    sm.set_projection_seed(0xC0FFEE1234ULL);

    // Configure tick dt deterministically (360 Hz).
    const double dt_s = 1.0 / 360.0;
    const int64_t dt_q32_32 = (int64_t)(dt_s * 4294967296.0);
    const int64_t h0_ref_q32_32 = hubble_h0_ref_default_q32_32();
    sm.configure_cosmic_expansion(h0_ref_q32_32, dt_q32_32);

    if (!lang_dir.empty()) {
        const bool ok = sm.language_bootstrap_from_dir(lang_dir);
        std::printf("lang_bootstrap=%s\n", ok ? "true" : "false");
        // Language tasks require full window per task; run enough ticks to complete
        // all enqueued language checkpoints (4 * 360 = 1440).
        if (ticks < 1500u) ticks = 1500u;
    }

    for (uint32_t t = 0; t < ticks; ++t) {
        sm.tick();
        // Drain and print UI lines deterministically (bounded).
        std::string out;
        uint32_t printed = 0;
        while (printed < 8u && sm.ui_pop_output_text(out)) {
            std::printf("UI:%s\n", out.c_str());
            printed++;
        }
    }

    print_u64_hex("tick_u64", sm.canonical_tick_u64());
    std::printf("curriculum_stage_u32=%u\n", sm.learning_curriculum_stage_u32);
    const genesis::LangLexiconStats st = sm.language_foundation.stats();
    std::printf("lex_words=%u pron=%u senses=%u rel=%u concepts=%u speech_utt=%u\n",
                st.word_count_u32, st.pron_count_u32, st.senses_count_u32, st.relations_count_u32,
                st.concept_count_u32, st.speech_utt_count_u32);

    return 0;
}
