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
    std::string export_dir;
    uint64_t export_object_id_u64 = 0;
    uint32_t ticks = 600;

    if (const char* v = ew::ew_cli_get_str(args, "lang-bootstrap")) lang_dir = v;
    if (lang_dir.empty()) {
        if (const char* v2 = ew::ew_cli_get_str(args, "lang_bootstrap")) lang_dir = v2;
    }
    (void)ew::ew_cli_get_u32(args, "ticks", ticks);
    if (const char* od = ew::ew_cli_get_str(args, "export_dir")) export_dir = od;
    if (const char* od2 = ew::ew_cli_get_str(args, "export-dir")) export_dir = od2;
    (void)ew::ew_cli_get_u64(args, "export_object_id", export_object_id_u64);
    (void)ew::ew_cli_get_u64(args, "export-object-id", export_object_id_u64);

    SubstrateMicroprocessor sm(64);
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

    if (!export_dir.empty() && export_object_id_u64 != 0) {
        std::string rep;
        const bool ok = sm.export_object_bundle(export_object_id_u64, export_dir, &rep);
        std::printf("export_bundle=%s\n", ok ? "true" : "false");
        std::printf("export_report=\n%s\n", rep.c_str());
    }

    print_u64_hex("tick_u64", sm.canonical_tick_u64());
    std::printf("curriculum_stage_u32=%u\n", sm.learning_curriculum_stage_u32);
    const genesis::LangLexiconStats st = sm.language_foundation.stats();
    std::printf("lex_words=%u pron=%u senses=%u rel=%u concepts=%u speech_utt=%u\n",
                st.word_count_u32, st.pron_count_u32, st.senses_count_u32, st.relations_count_u32,
                st.concept_count_u32, st.speech_utt_count_u32);

    return 0;
}
