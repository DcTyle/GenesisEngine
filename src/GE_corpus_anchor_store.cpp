#include "GE_corpus_anchor_store.hpp"

#include <algorithm>
#include <fstream>

#include "ew_packed_io.hpp"

void GE_CorpusAnchorStore::clear() { records.clear(); }

static inline uint32_t ge_rotl32(uint32_t x, uint32_t r) {
    return (x << r) | (x >> (32u - r));
}

static inline uint32_t ge_fold_u32(uint32_t a, uint32_t b) {
    // Deterministic fold (non-cryptographic). Used only to spread bits for 9D IDs.
    uint32_t x = a ^ ge_rotl32(b, 13) ^ 0x9E3779B9u;
    x ^= ge_rotl32(x, 7);
    x += 0x7F4A7C15u;
    x ^= ge_rotl32(x, 17);
    return x;
}

EwId9 GE_anchor_id9_from_provenance_and_carrier(uint8_t lane_u8,
                                               const EwId9& domain_id9,
                                               const EwId9& source_id9,
                                               uint32_t seq_u32,
                                               uint32_t offset_u32,
                                               const EwCarrierParams& carrier) {
    EwId9 out = domain_id9;

    const uint32_t f_lo = uint32_t(uint64_t(carrier.f_carrier_turns_q32_32) & 0xFFFFFFFFull);
    const uint32_t A_lo = uint32_t(uint64_t(carrier.A_carrier_q32_32) & 0xFFFFFFFFull);
    const uint32_t phi_lo = uint32_t(uint64_t(carrier.phi_carrier_turns_q32_32) & 0xFFFFFFFFull);
    const uint32_t cc = carrier.component_count_u32;

    out.lane[6] = ge_fold_u32(source_id9.lane[0], ge_fold_u32(f_lo, cc));
    out.lane[7] = ge_fold_u32(source_id9.lane[1], ge_fold_u32(phi_lo, A_lo));

    // Pack lane + seq + chunk_offset_q into lane[8], then fold in residual carrier bits.
    const uint32_t lane_pack = (uint32_t(lane_u8) & 0xFFu) << 24;
    const uint32_t seq_pack = (seq_u32 & 0x00FFFu) << 12;
    const uint32_t chunk_off_q = ((offset_u32 >> 8) & 0x00FFFu);
    uint32_t p = lane_pack | seq_pack | chunk_off_q;
    p = ge_fold_u32(p, ge_fold_u32(A_lo ^ f_lo, phi_lo ^ cc));
    out.lane[8] = p;

    return out;
}

void GE_CorpusAnchorStore::sort_and_dedupe() {
    std::sort(records.begin(), records.end(), [](const GE_CorpusAnchorRecord& a, const GE_CorpusAnchorRecord& b) {
        if (a.lane_u8 != b.lane_u8) return a.lane_u8 < b.lane_u8;
        if (a.domain_id9 != b.domain_id9) return a.domain_id9 < b.domain_id9;
        if (a.source_id9 != b.source_id9) return a.source_id9 < b.source_id9;
        if (a.seq_u32 != b.seq_u32) return a.seq_u32 < b.seq_u32;
        return a.offset_u32 < b.offset_u32;
    });
    records.erase(std::unique(records.begin(), records.end(), [](const GE_CorpusAnchorRecord& a, const GE_CorpusAnchorRecord& b) {
        return a.lane_u8 == b.lane_u8 && a.domain_id9 == b.domain_id9 && a.source_id9 == b.source_id9 &&
               a.seq_u32 == b.seq_u32 && a.offset_u32 == b.offset_u32;
    }), records.end());
}

static inline void ge_write_id9(std::ostream& os, const EwId9& id) {
    for (int i = 0; i < 9; ++i) {
        uint8_t buf[4];
        ew_write_u32_le(buf, id.lane[i]);
        os.write(reinterpret_cast<const char*>(buf), 4);
    }
}

static inline bool ge_read_id9(std::istream& is, EwId9& id) {
    for (int i = 0; i < 9; ++i) {
        uint8_t buf[4];
        if (!is.read(reinterpret_cast<char*>(buf), 4)) return false;
        id.lane[i] = uint32_t(buf[0]) | (uint32_t(buf[1]) << 8) | (uint32_t(buf[2]) << 16) | (uint32_t(buf[3]) << 24);
    }
    return true;
}

bool GE_CorpusAnchorStore::save_to_file(const std::string& path_utf8) const {
    std::ofstream os(path_utf8, std::ios::binary);
    if (!os) return false;
    const char hdr[8] = {'G','E','C','A','S','1','\0','\0'};
    os.write(hdr, 8);
    uint8_t buf4[4];
    ew_write_u32_le(buf4, uint32_t(records.size()));
    os.write(reinterpret_cast<const char*>(buf4), 4);
    for (const auto& r : records) {
        ge_write_id9(os, r.anchor_id9);
        ge_write_id9(os, r.domain_id9);
        ge_write_id9(os, r.source_id9);
        os.put(char(r.lane_u8));
        os.put('\0'); os.put('\0'); os.put('\0');
        ew_write_u32_le(buf4, r.seq_u32); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, r.offset_u32); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, r.size_u32); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, uint32_t(r.sc4.f_code)); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, uint32_t(r.sc4.a_code)); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, uint32_t(r.sc4.v_code)); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, uint32_t(r.sc4.i_code)); os.write(reinterpret_cast<const char*>(buf4), 4);
        uint8_t buf8[8];
        ew_write_i64_le(buf8, r.carrier.f_carrier_turns_q32_32); os.write(reinterpret_cast<const char*>(buf8), 8);
        ew_write_i64_le(buf8, r.carrier.A_carrier_q32_32); os.write(reinterpret_cast<const char*>(buf8), 8);
        ew_write_i64_le(buf8, r.carrier.phi_carrier_turns_q32_32); os.write(reinterpret_cast<const char*>(buf8), 8);
        ew_write_u32_le(buf4, r.carrier.component_count_u32); os.write(reinterpret_cast<const char*>(buf4), 4);
        ew_write_u32_le(buf4, uint32_t(r.payload_relpath_utf8.size())); os.write(reinterpret_cast<const char*>(buf4), 4);
        os.write(r.payload_relpath_utf8.data(), std::streamsize(r.payload_relpath_utf8.size()));
        ew_write_u64_le(buf8, r.payload_byte_off_u64); os.write(reinterpret_cast<const char*>(buf8), 8);
    }
    return bool(os);
}

bool GE_CorpusAnchorStore::load_from_file(const std::string& path_utf8) {
    std::ifstream is(path_utf8, std::ios::binary);
    if (!is) return false;
    char hdr[8];
    if (!is.read(hdr, 8)) return false;
    if (!(hdr[0]=='G'&&hdr[1]=='E'&&hdr[2]=='C'&&hdr[3]=='A'&&hdr[4]=='S'&&hdr[5]=='1')) return false;
    uint8_t buf4[4];
    if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
    const uint32_t n = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
    records.clear();
    records.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        GE_CorpusAnchorRecord r;
        if (!ge_read_id9(is, r.anchor_id9)) return false;
        if (!ge_read_id9(is, r.domain_id9)) return false;
        if (!ge_read_id9(is, r.source_id9)) return false;
        int c0 = is.get(); if (c0 == EOF) return false;
        r.lane_u8 = uint8_t(c0);
        if (is.get()==EOF || is.get()==EOF || is.get()==EOF) return false;
        if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
        r.seq_u32 = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
        if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
        r.offset_u32 = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
        if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
        r.size_u32 = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
        auto rd_i32 = [&](int32_t& out)->bool{
            if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
            out = int32_t(uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24));
            return true;
        };
        if (!rd_i32(r.sc4.f_code) || !rd_i32(r.sc4.a_code) || !rd_i32(r.sc4.v_code) || !rd_i32(r.sc4.i_code)) return false;
        uint8_t buf8[8];
        auto rd_i64 = [&](int64_t& out)->bool{
            if (!is.read(reinterpret_cast<char*>(buf8), 8)) return false;
            out = int64_t(uint64_t(buf8[0]) | (uint64_t(buf8[1])<<8) | (uint64_t(buf8[2])<<16) | (uint64_t(buf8[3])<<24) |
                         (uint64_t(buf8[4])<<32) | (uint64_t(buf8[5])<<40) | (uint64_t(buf8[6])<<48) | (uint64_t(buf8[7])<<56));
            return true;
        };
        if (!rd_i64(r.carrier.f_carrier_turns_q32_32) || !rd_i64(r.carrier.A_carrier_q32_32) || !rd_i64(r.carrier.phi_carrier_turns_q32_32)) return false;
        if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
        r.carrier.component_count_u32 = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
        if (!is.read(reinterpret_cast<char*>(buf4), 4)) return false;
        const uint32_t slen = uint32_t(buf4[0]) | (uint32_t(buf4[1])<<8) | (uint32_t(buf4[2])<<16) | (uint32_t(buf4[3])<<24);
        r.payload_relpath_utf8.resize(slen);
        if (!is.read(&r.payload_relpath_utf8[0], std::streamsize(slen))) return false;
        if (!is.read(reinterpret_cast<char*>(buf8), 8)) return false;
        r.payload_byte_off_u64 = uint64_t(buf8[0]) | (uint64_t(buf8[1])<<8) | (uint64_t(buf8[2])<<16) | (uint64_t(buf8[3])<<24) |
                                (uint64_t(buf8[4])<<32) | (uint64_t(buf8[5])<<40) | (uint64_t(buf8[6])<<48) | (uint64_t(buf8[7])<<56);
        records.push_back(r);
    }
    return true;
}

std::vector<size_t> GE_CorpusAnchorStore::find_by_domain_lane(const EwId9& domain_id9, uint8_t lane_u8) const {
    std::vector<size_t> out;
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].lane_u8 == lane_u8 && records[i].domain_id9 == domain_id9) out.push_back(i);
    }
    return out;
}
