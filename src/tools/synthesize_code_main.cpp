#include "GE_runtime.hpp"

#include <cstdio>
#include <string>

static std::string join_args(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) out.push_back(' ');
        out += argv[i];
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ew_synthesize_code <request>\n");
        std::fprintf(stderr, "example: ew_synthesize_code create module MyThing\n");
        return 2;
    }

    SubstrateMicroprocessor sm;
    sm.set_projection_seed(1u);

    const std::string req = join_args(argc, argv);
    sm.ui_submit_user_text_line(std::string("SYNTHCODE:") + req);

    // Deterministic small tick budget.
    for (int i = 0; i < 12; ++i) sm.tick();

    // Print UI lines for operator visibility.
    for (;;) {
        std::string out;
        if (!sm.ui_pop_output_text(out)) break;
        std::printf("%s\n", out.c_str());
    }

    return 0;
}
