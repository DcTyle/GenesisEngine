#include "GE_pause_priors.hpp"

#include "GE_voice_predictive_model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace genesis {

static bool is_sp_phone(const std::string& p) {
    return p == "SP" || p == "sp";
}

static std::string phone_strip_stress_digits(const std::string& p) {
    if (p.empty()) return p;
    size_t n = p.size();
    while (n > 0 && std::isdigit((unsigned char)p[n - 1])) {
        n--;
    }
    return p.substr(0, n);
}

static bool phone_is_vowel_english(const std::string& p) {
    const std::string b = phone_strip_stress_digits(p);
    // ARPAbet vowel inventory (common).
    return b == "AA" || b == "AE" || b == "AH" || b == "AO" || b == "AW" || b == "AY" ||
           b == "EH" || b == "ER" || b == "EY" || b == "IH" || b == "IY" || b == "OW" ||
           b == "OY" || b == "UH" || b == "UW";
}

static uint8_t pause_prev_class_from_phone_stream(size_t sp_index,
                                                  const std::vector<std::string>& phone_str) {
    // 0 = none/start, 1 = prev_vowel, 2 = prev_consonant
    if (sp_index == 0 || phone_str.empty()) return 0u;
    for (size_t j = sp_index; j > 0; --j) {
        const size_t k = j - 1;
        if (k >= phone_str.size()) continue;
        if (is_sp_phone(phone_str[k])) continue;
        return phone_is_vowel_english(phone_str[k]) ? 1u : 2u;
    }
    return 0u;
}

static uint8_t pause_next_class_from_phone_stream(size_t sp_index,
                                                  const std::vector<std::string>& phone_str) {
    // 0 = none/end, 1 = next_vowel, 2 = next_consonant
    if (phone_str.empty()) return 0u;
    for (size_t k = sp_index + 1; k < phone_str.size(); ++k) {
        if (is_sp_phone(phone_str[k])) continue;
        return phone_is_vowel_english(phone_str[k]) ? 1u : 2u;
    }
    return 0u;
}

static uint32_t pause_kind_base_ms_q16(PauseKind k) {
    // Base pause durations (ms) used by planner before learned priors.
    switch (k) {
        case PauseKind::Space: return (uint32_t)(60u * 65536u);
        case PauseKind::Comma: return (uint32_t)(120u * 65536u);
        case PauseKind::Clause: return (uint32_t)(160u * 65536u);
        case PauseKind::TerminalPeriod: return (uint32_t)(220u * 65536u);
        case PauseKind::TerminalQuestion: return (uint32_t)(240u * 65536u);
        case PauseKind::TerminalExclaim: return (uint32_t)(200u * 65536u);
        case PauseKind::Newline: return (uint32_t)(260u * 65536u);
        default: return (uint32_t)(60u * 65536u);
    }
}

static uint32_t q16_mul(uint32_t a_q16, uint32_t b_q16) {
    return (uint32_t)(((uint64_t)a_q16 * (uint64_t)b_q16 + 32768u) >> 16);
}

static uint32_t q16_div(uint32_t num_q16, uint32_t den_q16) {
    if (den_q16 == 0u) return 0u;
    return (uint32_t)(((uint64_t)num_q16 << 16) / (uint64_t)den_q16);
}

const char* ge_pause_kind_to_cstr(PauseKind k) {
    switch (k) {
        case PauseKind::Space: return "space";
        case PauseKind::Comma: return "comma";
        case PauseKind::Clause: return "clause";
        case PauseKind::TerminalPeriod: return "period";
        case PauseKind::TerminalQuestion: return "question";
        case PauseKind::TerminalExclaim: return "exclaim";
        case PauseKind::Newline: return "newline";
        default: return "unknown";
    }
}

bool ge_pause_kind_from_token(const std::string& tok, PauseKind* out) {
    if (!out) return false;
    if (tok == "space") { *out = PauseKind::Space; return true; }
    if (tok == "comma") { *out = PauseKind::Comma; return true; }
    if (tok == "clause") { *out = PauseKind::Clause; return true; }
    if (tok == "period") { *out = PauseKind::TerminalPeriod; return true; }
    if (tok == "question") { *out = PauseKind::TerminalQuestion; return true; }
    if (tok == "exclaim") { *out = PauseKind::TerminalExclaim; return true; }
    if (tok == "newline") { *out = PauseKind::Newline; return true; }
    *out = PauseKind::Unknown;
    return false;
}

const char* ge_pause_prev_class_to_cstr(uint8_t prev_class_u8) {
    switch (prev_class_u8) {
        case 1u: return "pv"; // prev vowel
        case 2u: return "pc"; // prev consonant
        default: return "ps"; // prev start/none
    }
}

bool ge_pause_prev_class_from_token(const std::string& tok, uint8_t* out_prev_class_u8) {
    if (!out_prev_class_u8) return false;
    if (tok == "pv" || tok == "prev_vowel") { *out_prev_class_u8 = 1u; return true; }
    if (tok == "pc" || tok == "prev_consonant") { *out_prev_class_u8 = 2u; return true; }
    if (tok == "ps" || tok == "prev_start" || tok == "none") { *out_prev_class_u8 = 0u; return true; }
    return false;
}

const char* ge_pause_next_class_to_cstr(uint8_t next_class_u8) {
    switch (next_class_u8) {
        case 1u: return "nv"; // next vowel
        case 2u: return "nc"; // next consonant
        default: return "ns"; // next end/none
    }
}

bool ge_pause_next_class_from_token(const std::string& tok, uint8_t* out_next_class_u8) {
    if (!out_next_class_u8) return false;
    if (tok == "nv" || tok == "next_vowel") { *out_next_class_u8 = 1u; return true; }
    if (tok == "nc" || tok == "next_consonant") { *out_next_class_u8 = 2u; return true; }
    if (tok == "ns" || tok == "next_end" || tok == "none") { *out_next_class_u8 = 0u; return true; }
    return false;
}


uint32_t ge_pause_strength_mul_q16_from_strength(uint8_t strength) {
    // Base strength multiplier: 1.0 + 0.15*(strength-1), clamped to [1.0, 2.0]
    // Q16.16 integer-first.
    if (strength <= 1u) return 65536u;
    const uint32_t step = 9830u; // round(0.15 * 65536)
    uint32_t mul = 65536u + (uint32_t)(strength - 1u) * step;
    if (mul > 131072u) mul = 131072u;
    if (mul < 65536u) mul = 65536u;
    return mul;
}

uint8_t ge_pause_strength_bin_from_mul_q16(uint32_t mul_q16) {
    // Quantize [1.0, 2.0] into 5 bins (0..4) uniformly.
    if (mul_q16 <= 65536u) return 0u;
    if (mul_q16 >= 131072u) return 4u;
    // bin = round((mul-1.0)*4)
    const uint32_t num = (mul_q16 - 65536u) * 4u + 32768u; // +0.5 for rounding
    uint32_t bin = num / 65536u;
    if (bin > 4u) bin = 4u;
    return (uint8_t)bin;
}

uint8_t ge_pause_strength_bin_from_strength(uint8_t strength) {
    return ge_pause_strength_bin_from_mul_q16(ge_pause_strength_mul_q16_from_strength(strength));
}

const char* ge_pause_bin_to_cstr(uint8_t bin_u8) {
    switch (bin_u8) {
        case 0u: return "b0";
        case 1u: return "b1";
        case 2u: return "b2";
        case 3u: return "b3";
        case 4u: return "b4";
        default: return "b0";
    }
}

bool ge_pause_bin_from_token(const std::string& tok, uint8_t* out_bin_u8) {
    if (!out_bin_u8) return false;
    if (tok == "b0" || tok == "bin0") { *out_bin_u8 = 0u; return true; }
    if (tok == "b1" || tok == "bin1") { *out_bin_u8 = 1u; return true; }
    if (tok == "b2" || tok == "bin2") { *out_bin_u8 = 2u; return true; }
    if (tok == "b3" || tok == "bin3") { *out_bin_u8 = 3u; return true; }
    if (tok == "b4" || tok == "bin4") { *out_bin_u8 = 4u; return true; }
    // Back-compat tokens
    if (tok == "s1") { *out_bin_u8 = 0u; return true; }
    if (tok == "s2") { *out_bin_u8 = 1u; return true; }
    if (tok == "s3p" || tok == "s3+") { *out_bin_u8 = 2u; return true; }
    return false;
}

static void commit_run(std::vector<PauseKind>& inferred,
                       bool has_nl, bool has_comma, bool has_clause,
                       bool has_period, bool has_q, bool has_exc) {
    if (has_nl) { inferred.push_back(PauseKind::Newline); return; }
    if (has_q) { inferred.push_back(PauseKind::TerminalQuestion); return; }
    if (has_exc) { inferred.push_back(PauseKind::TerminalExclaim); return; }
    if (has_period) { inferred.push_back(PauseKind::TerminalPeriod); return; }
    if (has_clause) { inferred.push_back(PauseKind::Clause); return; }
    if (has_comma) { inferred.push_back(PauseKind::Comma); return; }
    inferred.push_back(PauseKind::Space);
}

std::vector<PauseKind> ge_pause_kinds_from_text_and_phones(const std::string& text_utf8,
                                                          const std::vector<PhonemeSpan>& phones) {
    std::vector<PauseKind> out(phones.size(), PauseKind::Unknown);

    std::vector<PauseKind> inferred;
    inferred.reserve(16);

    bool in_sep = false;
    bool has_nl=false, has_comma=false, has_clause=false, has_period=false, has_q=false, has_exc=false;

    for (size_t i = 0; i < text_utf8.size(); ++i) {
        unsigned char c = (unsigned char)text_utf8[i];
        const bool sep = std::isspace(c) || c==',' || c==';' || c==':' || c=='.' || c=='?' || c=='!' || c=='\n' || c=='\r';
        if (sep) {
            if (!in_sep) {
                in_sep = true;
                has_nl = has_comma = has_clause = has_period = has_q = has_exc = false;
            }
            if (c=='\n' || c=='\r') has_nl = true;
            else if (c==',') has_comma = true;
            else if (c==';' || c==':') has_clause = true;
            else if (c=='.') has_period = true;
            else if (c=='?') has_q = true;
            else if (c=='!') has_exc = true;
        } else {
            if (in_sep) {
                commit_run(inferred, has_nl, has_comma, has_clause, has_period, has_q, has_exc);
                in_sep = false;
            }
        }
    }
    if (in_sep) commit_run(inferred, has_nl, has_comma, has_clause, has_period, has_q, has_exc);

    size_t k = 0;
    for (size_t i = 0; i < phones.size(); ++i) {
        if (!is_sp_phone(phones[i].phone)) continue;
        PauseKind pk = PauseKind::Space;
        if (k < inferred.size()) pk = inferred[k++];
        out[i] = pk;
    }
    return out;
}


bool ge_pause_meta_from_text_and_phones(const std::string& text_utf8,
                                        const std::vector<PhonemeSpan>& phones,
                                        std::vector<PauseKind>* out_kinds,
                                        std::vector<uint8_t>* out_strength_u8) {
    if (!out_kinds || !out_strength_u8) return false;
    out_kinds->assign(phones.size(), PauseKind::Unknown);
    out_strength_u8->assign(phones.size(), 0u);

    // Infer pause runs from separators in text, including run-length.
    struct Run { PauseKind kind; uint8_t strength; };
    std::vector<Run> inferred;
    inferred.reserve(16);

    bool in_sep = false;
    bool has_nl=false, has_comma=false, has_clause=false, has_period=false, has_q=false, has_exc=false;
    uint32_t run_len = 0;

    auto commit = [&]() {
        if (!in_sep) return;
        PauseKind k = PauseKind::Space;
        if (has_nl) k = PauseKind::Newline;
        else if (has_q) k = PauseKind::TerminalQuestion;
        else if (has_exc) k = PauseKind::TerminalExclaim;
        else if (has_period) k = PauseKind::TerminalPeriod;
        else if (has_clause) k = PauseKind::Clause;
        else if (has_comma) k = PauseKind::Comma;
        uint8_t s = (run_len > 255u) ? 255u : (uint8_t)run_len;
        if (s == 0u) s = 1u;
        inferred.push_back(Run{k, s});
        in_sep = false;
        run_len = 0;
    };

    for (size_t i = 0; i < text_utf8.size(); ++i) {
        unsigned char c = (unsigned char)text_utf8[i];
        const bool sep = std::isspace(c) || c==',' || c==';' || c==':' || c=='.' || c=='?' || c=='!' || c=='\n' || c=='\r';
        if (sep) {
            if (!in_sep) {
                in_sep = true;
                has_nl = has_comma = has_clause = has_period = has_q = has_exc = false;
                run_len = 0;
            }
            run_len++;
            if (c=='\n' || c=='\r') has_nl = true;
            else if (c==',') has_comma = true;
            else if (c==';' || c==':') has_clause = true;
            else if (c=='.') has_period = true;
            else if (c=='?') has_q = true;
            else if (c=='!') has_exc = true;
        } else {
            commit();
        }
    }
    commit();

    size_t kidx = 0;
    for (size_t i = 0; i < phones.size(); ++i) {
        if (!is_sp_phone(phones[i].phone)) continue;
        PauseKind pk = PauseKind::Space;
        uint8_t strength = 1u;
        if (kidx < inferred.size()) { pk = inferred[kidx].kind; strength = inferred[kidx].strength; kidx++; }
        (*out_kinds)[i] = pk;
        (*out_strength_u8)[i] = strength;
    }
    return true;
}


bool ge_pause_priors_build_from_observations(const std::vector<PauseKind>& pause_kinds,
                                            const std::vector<uint8_t>& pause_strength_u8,
                                            const std::vector<std::string>& phone_str_obs,
                                            const std::vector<uint32_t>& dur_mul_q16_obs,
                                            PausePriors* out) {
    if (!out) return false;
    out->ok = false;
    out->rows.clear();
    if (phone_str_obs.size() != dur_mul_q16_obs.size()) { out->info = "size_mismatch"; return false; }
    if (!pause_kinds.empty() && pause_kinds.size() != phone_str_obs.size()) { out->info = "pause_kind_size_mismatch"; return false; }
    if (!pause_strength_u8.empty() && pause_strength_u8.size() != phone_str_obs.size()) { out->info = "pause_strength_size_mismatch"; return false; }

    
	struct Acc { uint64_t sum=0; uint32_t n=0; };
	// index = kind_index * (5*9) + bin(0..4)*9 + prev(0..2)*3 + next(0..2)
	Acc acc[((int)PauseKind::Newline + 1) * 5 * 9] = {};

	auto idx = [&](PauseKind k, uint8_t bin_u8, uint8_t prev_u8, uint8_t next_u8) -> int {
	    const int ki = (int)k;
	    const int bi = (int)bin_u8;
	    const int pi = (int)prev_u8;
	    const int ni = (int)next_u8;
	    if (ki < 0 || ki > (int)PauseKind::Newline) return -1;
	    if (bi < 0 || bi > 4) return -1;
	    if (pi < 0 || pi > 2) return -1;
	    if (ni < 0 || ni > 2) return -1;
	    return ki * 45 + bi * 9 + pi * 3 + ni;
	};

for (size_t i = 0; i < phone_str_obs.size(); ++i) {
    if (!is_sp_phone(phone_str_obs[i])) continue;
    PauseKind k = PauseKind::Space;
    if (!pause_kinds.empty()) k = pause_kinds[i];
    uint8_t s = 1u;
    if (!pause_strength_u8.empty()) s = pause_strength_u8[i];
    const uint32_t strength_mul_q16 = ge_pause_strength_mul_q16_from_strength(s);
    const uint8_t bin_u8 = ge_pause_strength_bin_from_mul_q16(strength_mul_q16);
	    const uint8_t prev_u8 = pause_prev_class_from_phone_stream(i, phone_str_obs);
	    const uint8_t next_u8 = pause_next_class_from_phone_stream(i, phone_str_obs);
	    const int ii = idx(k, bin_u8, prev_u8, next_u8);
    if (ii < 0) continue;

    // Observed duration is in ms (Q16.16). Convert to multiplier over the planner baseline (kind_base_ms * strength_mul).
    const uint32_t obs_ms_q16 = dur_mul_q16_obs[i];
    const uint32_t base_ms_q16 = pause_kind_base_ms_q16(k);
    const uint32_t baseline_ms_q16 = q16_mul(base_ms_q16, strength_mul_q16);
    uint32_t obs_mul_q16 = q16_div(obs_ms_q16, (baseline_ms_q16 == 0u ? 1u : baseline_ms_q16));
    if (obs_mul_q16 < 16384u) obs_mul_q16 = 16384u; // clamp to 0.25x
    if (obs_mul_q16 > 262144u) obs_mul_q16 = 262144u; // clamp to 4.0x

    acc[ii].sum += (uint64_t)obs_mul_q16;
    acc[ii].n += 1;
}

	for (int ki = 0; ki <= (int)PauseKind::Newline; ++ki) {
	    for (int bi = 0; bi <= 4; ++bi) {
	        for (int pi = 0; pi <= 2; ++pi) {
	            for (int ni = 0; ni <= 2; ++ni) {
	                const int ii = ki * 45 + bi * 9 + pi * 3 + ni;
	                if (acc[ii].n == 0) continue;
	                PausePriorRow r;
	                r.kind = (PauseKind)ki;
	                r.strength_bin_u8 = (uint8_t)bi;
	                r.prev_class_u8 = (uint8_t)pi;
	                r.next_class_u8 = (uint8_t)ni;
	                r.mean_dur_mul_q16_u32 = (uint32_t)(acc[ii].sum / acc[ii].n);
	                r.count_u32 = acc[ii].n;
	                out->rows.push_back(r);
	            }
	        }
	    }
	}
    std::sort(out->rows.begin(), out->rows.end(), [](const PausePriorRow& a, const PausePriorRow& b){
        if ((int)a.kind != (int)b.kind) return (int)a.kind < (int)b.kind;
	        if ((int)a.strength_bin_u8 != (int)b.strength_bin_u8) return (int)a.strength_bin_u8 < (int)b.strength_bin_u8;
	        if ((int)a.prev_class_u8 != (int)b.prev_class_u8) return (int)a.prev_class_u8 < (int)b.prev_class_u8;
	        return (int)a.next_class_u8 < (int)b.next_class_u8;
    });
    out->ok = true;
    out->info = "ok";
    return true;
}

bool ge_pause_priors_write_ewcfg(const std::string& path_utf8, const PausePriors& pri) {
    std::ofstream f(path_utf8.c_str(), std::ios::binary);
    if (!f) return false;
    for (const auto& r : pri.rows) {
	    f << "pause " << ge_pause_kind_to_cstr(r.kind) << " " << ge_pause_bin_to_cstr(r.strength_bin_u8)
	      << " " << ge_pause_prev_class_to_cstr(r.prev_class_u8)
	      << " " << ge_pause_next_class_to_cstr(r.next_class_u8)
	      << " " << r.mean_dur_mul_q16_u32 << " " << r.count_u32 << "\n";
    }
    return true;
}

bool ge_pause_priors_read_ewcfg(const std::string& path_utf8, PausePriors* out) {
    if (!out) return false;
    out->ok = false;
    out->rows.clear();
    std::ifstream f(path_utf8.c_str(), std::ios::binary);
    if (!f) { out->info = "open_failed"; return false; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag; ss >> tag;
        if (tag != "pause") continue;
		
		std::string kind_tok; ss >> kind_tok;
		std::string tok2; ss >> tok2;
		std::string tok3; ss >> tok3;
		std::string tok4; ss >> tok4;
		uint32_t mul_q16 = 65536u, count = 0;
		uint8_t bin_u8 = 0u;
		uint8_t prev_class_u8 = 0u;
		uint8_t next_class_u8 = 0u;

		// Back-compat formats:
		// 1) pause <kind> <mul> <count>
		// 2) pause <kind> <bin> <mul> <count>
		// 3) pause <kind> <bin> <prev_ctx> <mul> <count>
		// 4) pause <kind> <bin> <prev_ctx> <next_ctx> <mul> <count>
		const bool tok2_is_number = !tok2.empty() && std::isdigit((unsigned char)tok2[0]);
		if (tok2_is_number) {
			mul_q16 = (uint32_t)std::strtoul(tok2.c_str(), nullptr, 10);
			// shift: tok3 already read as next token
			count = tok3.empty() ? 0u : (uint32_t)std::strtoul(tok3.c_str(), nullptr, 10);
		} else {
			if (!ge_pause_bin_from_token(tok2, &bin_u8)) bin_u8 = 0u;
			const bool tok3_is_number = !tok3.empty() && std::isdigit((unsigned char)tok3[0]);
			if (tok3_is_number) {
				mul_q16 = (uint32_t)std::strtoul(tok3.c_str(), nullptr, 10);
				// tok4 is count
				count = tok4.empty() ? 0u : (uint32_t)std::strtoul(tok4.c_str(), nullptr, 10);
			} else {
				if (!ge_pause_prev_class_from_token(tok3, &prev_class_u8)) prev_class_u8 = 0u;
				// tok4 may be mul (format 3) or next_ctx (format 4)
				const bool tok4_is_number = !tok4.empty() && std::isdigit((unsigned char)tok4[0]);
				if (tok4_is_number) {
					mul_q16 = (uint32_t)std::strtoul(tok4.c_str(), nullptr, 10);
					ss >> count;
				} else {
					if (!ge_pause_next_class_from_token(tok4, &next_class_u8)) next_class_u8 = 0u;
					ss >> mul_q16 >> count;
				}
			}
		}
		
		PauseKind k;

        if (!ge_pause_kind_from_token(kind_tok, &k)) continue;
        PausePriorRow r;
        r.kind = k;
        r.strength_bin_u8 = bin_u8;
	    r.prev_class_u8 = prev_class_u8;
	    r.next_class_u8 = next_class_u8;
        r.mean_dur_mul_q16_u32 = mul_q16;
        r.count_u32 = count;
        out->rows.push_back(r);
    }
    std::sort(out->rows.begin(), out->rows.end(), [](const PausePriorRow& a, const PausePriorRow& b){
        if ((int)a.kind != (int)b.kind) return (int)a.kind < (int)b.kind;
	    if ((int)a.strength_bin_u8 != (int)b.strength_bin_u8) return (int)a.strength_bin_u8 < (int)b.strength_bin_u8;
	    if ((int)a.prev_class_u8 != (int)b.prev_class_u8) return (int)a.prev_class_u8 < (int)b.prev_class_u8;
	    return (int)a.next_class_u8 < (int)b.next_class_u8;
    });
    out->ok = true;
    out->info = "ok";
    return true;
}


static bool find_row(const PausePriors& pri, PauseKind k, uint8_t bin_u8, uint8_t prev_class_u8, uint8_t next_class_u8, PausePriorRow* out) {
    for (const auto& r : pri.rows) {
	    if (r.kind == k && r.strength_bin_u8 == bin_u8 && r.prev_class_u8 == prev_class_u8 && r.next_class_u8 == next_class_u8) {
	        if (out) *out = r;
	        return true;
	    }
    }
    return false;
}


bool ge_pause_priors_apply_blend(const PausePriors& pri,
                                 const std::vector<PauseKind>& pause_kinds,
                                 const std::vector<uint8_t>& pause_strength_u8,
                                 const std::vector<std::string>& phone_str,
                                 uint32_t blend_q16,
                                 std::vector<VoiceProsodyControl>* io) {
    if (!io) return false;
    if (io->size() != phone_str.size()) return false;
    if (!pause_kinds.empty() && pause_kinds.size() != phone_str.size()) return false;
    if (!pause_strength_u8.empty() && pause_strength_u8.size() != phone_str.size()) return false;
    if (!pri.ok) return false;

    for (size_t i = 0; i < phone_str.size(); ++i) {
        if (!is_sp_phone(phone_str[i])) continue;
        PauseKind k = PauseKind::Space;
        if (!pause_kinds.empty()) k = pause_kinds[i];
        uint8_t s = 1u;
        if (!pause_strength_u8.empty()) s = pause_strength_u8[i];
        const uint8_t bin_u8 = ge_pause_strength_bin_from_strength(s);
	    const uint8_t prev_u8 = pause_prev_class_from_phone_stream(i, phone_str);
	    const uint8_t next_u8 = pause_next_class_from_phone_stream(i, phone_str);
        PausePriorRow r;
	    if (!find_row(pri, k, bin_u8, prev_u8, next_u8, &r)) continue;

        // factor = lerp(1.0, prior_mul, blend)
        const uint64_t inv = 65536u - (uint64_t)blend_q16;
        const uint64_t factor_q16 = (inv * 65536u + (uint64_t)blend_q16 * (uint64_t)r.mean_dur_mul_q16_u32 + 32768u) >> 16;
        const uint64_t dur = ((uint64_t)(*io)[i].dur_ms_u32 * factor_q16 + 32768u) >> 16;
        (*io)[i].dur_ms_u32 = (uint32_t)std::min<uint64_t>(2000ull, dur);
    }
    return true;
}

} // namespace genesis
