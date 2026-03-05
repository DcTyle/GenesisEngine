#include "GE_corpus_pulse_log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "GE_corpus_canonicalizer.hpp"
#include "crawler_encode_cuda.hpp"
#include "frequency_collapse.hpp"

static inline bool ge_write_u32(FILE* f, uint32_t v) { return fwrite(&v, 1, sizeof(v), f) == sizeof(v); }
static inline bool ge_write_u64(FILE* f, uint64_t v) { return fwrite(&v, 1, sizeof(v), f) == sizeof(v); }
static inline bool ge_write_u8(FILE* f, uint8_t v) { return fwrite(&v, 1, sizeof(v), f) == sizeof(v); }
static inline bool ge_write_i64(FILE* f, int64_t v) { return fwrite(&v, 1, sizeof(v), f) == sizeof(v); }

static inline bool ge_read_u32(FILE* f, uint32_t* out) { return fread(out, 1, sizeof(*out), f) == sizeof(*out); }
static inline bool ge_read_u64(FILE* f, uint64_t* out) { return fread(out, 1, sizeof(*out), f) == sizeof(*out); }
static inline bool ge_read_u8(FILE* f, uint8_t* out) { return fread(out, 1, sizeof(*out), f) == sizeof(*out); }
static inline bool ge_read_i64(FILE* f, int64_t* out) { return fread(out, 1, sizeof(*out), f) == sizeof(*out); }

void GE_CorpusPulseLog::clear() { records.clear(); }

void GE_CorpusPulseLog::sort_stable() {
    std::stable_sort(records.begin(), records.end(),
                     [](const GE_CorpusPulseRecord& a, const GE_CorpusPulseRecord& b) {
                         if (a.lane_u8 != b.lane_u8) return a.lane_u8 < b.lane_u8;
                         if (!(a.domain_id9 == b.domain_id9)) return a.domain_id9 < b.domain_id9;
                         if (!(a.source_id9 == b.source_id9)) return a.source_id9 < b.source_id9;
                         if (a.seq_u32 != b.seq_u32) return a.seq_u32 < b.seq_u32;
                         if (a.offset_u32 != b.offset_u32) return a.offset_u32 < b.offset_u32;
                         if (a.size_u32 != b.size_u32) return a.size_u32 < b.size_u32;
                         return a.payload_relpath_utf8 < b.payload_relpath_utf8;
                     });
}

static const uint32_t GEPL_MAGIC = 0x4C504547; // 'GEPL'
static const uint32_t GEPL_VERSION = 1;

bool GE_CorpusPulseLog::save_to_file(const std::string& path_utf8) const {
    FILE* f = std::fopen(path_utf8.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    ok = ok && ge_write_u32(f, GEPL_MAGIC);
    ok = ok && ge_write_u32(f, GEPL_VERSION);
    ok = ok && ge_write_u32(f, (uint32_t)records.size());

    for (const auto& r : records) {
        ok = ok && ge_write_u8(f, r.lane_u8);
        ok = ok && ge_write_u32(f, 0); // reserved/pad for alignment (deterministic)
        ok = ok && (std::fwrite(r.domain_id9.u32.data(), 1, sizeof(uint32_t)*9, f) == sizeof(uint32_t)*9);
        ok = ok && (std::fwrite(r.source_id9.u32.data(), 1, sizeof(uint32_t)*9, f) == sizeof(uint32_t)*9);
        ok = ok && ge_write_u32(f, r.seq_u32);
        ok = ok && ge_write_u32(f, r.offset_u32);
        ok = ok && ge_write_u32(f, r.size_u32);

        ok = ok && ge_write_u32(f, r.sc4.f_code);
        ok = ok && ge_write_u32(f, r.sc4.a_code);
        ok = ok && ge_write_u32(f, r.sc4.v_code);
        ok = ok && ge_write_u32(f, r.sc4.i_code);

        ok = ok && ge_write_i64(f, r.carrier.f_carrier_turns_q32_32);
        ok = ok && ge_write_i64(f, r.carrier.A_carrier_q32_32);
        ok = ok && ge_write_i64(f, r.carrier.phi_carrier_turns_q32_32);
        ok = ok && ge_write_u32(f, r.carrier.component_count_u32);

        const uint32_t slen = (uint32_t)r.payload_relpath_utf8.size();
        ok = ok && ge_write_u32(f, slen);
        if (slen) ok = ok && (fwrite(r.payload_relpath_utf8.data(), 1, slen, f) == slen);
        ok = ok && ge_write_u64(f, r.payload_byte_off_u64);
        ok = ok && ge_write_u32(f, 0); // reserved
        if (!ok) break;
    }

    std::fclose(f);
    return ok;
}

bool GE_CorpusPulseLog::load_from_file(const std::string& path_utf8) {
    FILE* f = std::fopen(path_utf8.c_str(), "rb");
    if (!f) return false;
    uint32_t magic=0, ver=0, count=0;
    bool ok = ge_read_u32(f,&magic) && ge_read_u32(f,&ver) && ge_read_u32(f,&count);
    ok = ok && (magic==GEPL_MAGIC) && (ver==GEPL_VERSION);
    if (!ok) { std::fclose(f); return false; }
    records.clear();
    records.reserve(count);
    for (uint32_t i=0;i<count;i++){
        GE_CorpusPulseRecord r;
        uint32_t pad=0;
        ok = ok && ge_read_u8(f,&r.lane_u8);
        ok = ok && ge_read_u32(f,&pad);
        ok = ok && (std::fread(r.domain_id9.u32.data(), 1, sizeof(uint32_t)*9, f) == sizeof(uint32_t)*9);
        ok = ok && (std::fread(r.source_id9.u32.data(), 1, sizeof(uint32_t)*9, f) == sizeof(uint32_t)*9);
        ok = ok && ge_read_u32(f,&r.seq_u32);
        ok = ok && ge_read_u32(f,&r.offset_u32);
        ok = ok && ge_read_u32(f,&r.size_u32);

        {
            uint32_t tmp = 0;
            ok = ok && ge_read_u32(f, &tmp);
            r.sc4.f_code = (int32_t)tmp;
            ok = ok && ge_read_u32(f, &tmp);
            r.sc4.a_code = (uint16_t)tmp;
            ok = ok && ge_read_u32(f, &tmp);
            r.sc4.v_code = (uint16_t)tmp;
            ok = ok && ge_read_u32(f, &tmp);
            r.sc4.i_code = (uint16_t)tmp;
        }

        ok = ok && ge_read_i64(f,&r.carrier.f_carrier_turns_q32_32);
        ok = ok && ge_read_i64(f,&r.carrier.A_carrier_q32_32);
        ok = ok && ge_read_i64(f,&r.carrier.phi_carrier_turns_q32_32);
        ok = ok && ge_read_u32(f,&r.carrier.component_count_u32);

        uint32_t slen=0;
        ok = ok && ge_read_u32(f,&slen);
        r.payload_relpath_utf8.assign(slen, '\0');
        if (slen) ok = ok && (fread(&r.payload_relpath_utf8[0],1,slen,f)==slen);
        ok = ok && ge_read_u64(f,&r.payload_byte_off_u64);
        ok = ok && ge_read_u32(f,&pad);
        if (!ok) break;
        records.push_back(std::move(r));
    }
    std::fclose(f);
    return ok;
}

static bool ge_read_payload_range(const std::string& full_path, uint64_t off, uint32_t sz, std::string* out) {
    out->clear();
    FILE* f = std::fopen(full_path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, (long)off, SEEK_SET) != 0) { std::fclose(f); return false; }
    out->assign(sz, '\0');
    const size_t got = fread(&(*out)[0], 1, sz, f);
    std::fclose(f);
    return got == sz;
}

bool GE_pulse_record_verify_against_payload(const GE_CorpusPulseRecord& rec,
                                            const std::string& corpus_root_utf8,
                                            std::string* opt_err_utf8) {
    const std::string full = corpus_root_utf8 + "/" + rec.payload_relpath_utf8;
    std::string raw;
    if (!ge_read_payload_range(full, rec.payload_byte_off_u64, rec.size_u32, &raw)) {
        if (opt_err_utf8) *opt_err_utf8 = "payload_read_failed:" + full;
        return false;
    }

    // Canonicalize via the strict CUDA-backed path used by ingestion.
    GE_CorpusCanonicalizeStats stats;
    std::string canon;
    if (!GE_canonicalize_utf8_strict(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), canon, stats)) {
        if (opt_err_utf8) *opt_err_utf8 = "canonicalize_failed";
        return false;
    }

    // Encode via CUDA (no CPU encoder path).
    SpiderCode4 sc4{};
    if (!ew_encode_spidercode4_from_bytes_chunked_cuda(reinterpret_cast<const uint8_t*>(canon.data()), canon.size(), 4096u, &sc4)) {
        if (opt_err_utf8) *opt_err_utf8 = "encode_failed";
        return false;
    }

    // Deterministic fixed-point carrier collapse.
    std::vector<EwFreqComponentQ32_32> comps;
    comps.reserve(4);
    auto push_comp = [&](int32_t f_code, int32_t a_code, int32_t phi_code) {
        EwFreqComponentQ32_32 c;
        c.f_turns_q32_32 = int64_t(f_code) << 16;
        c.a_q32_32 = (int64_t(a_code) << 16);
        c.phi_turns_q32_32 = int64_t(phi_code) << 16;
        comps.push_back(c);
    };
    push_comp(sc4.f_code, sc4.a_code, sc4.v_code);
    push_comp(sc4.a_code, sc4.v_code, sc4.i_code);
    push_comp(sc4.v_code, sc4.i_code, sc4.f_code);
    push_comp(sc4.i_code, sc4.f_code, sc4.a_code);

    EwCarrierWaveQ32_32 carrier{};
    if (!ew_collapse_frequency_components_q32_32(comps, carrier)) {
        if (opt_err_utf8) *opt_err_utf8 = "carrier_collapse_failed";
        return false;
    }

    if (!(sc4.f_code == rec.sc4.f_code &&
          sc4.a_code == rec.sc4.a_code &&
          sc4.v_code == rec.sc4.v_code &&
          sc4.i_code == rec.sc4.i_code)) {
        if (opt_err_utf8) *opt_err_utf8 = "sc4_mismatch";
        return false;
    }
    if (carrier.f_carrier_turns_q32_32 != rec.carrier.f_carrier_turns_q32_32 ||
        carrier.A_carrier_q32_32 != rec.carrier.A_carrier_q32_32 ||
        carrier.phi_carrier_turns_q32_32 != rec.carrier.phi_carrier_turns_q32_32 ||
        carrier.component_count_u32 != rec.carrier.component_count_u32) {
        if (opt_err_utf8) *opt_err_utf8 = "carrier_mismatch";
        return false;
    }
    return true;
}
