#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ew_id9.hpp"
#include "frequency_collapse.hpp"
#include "anchor.hpp"
#include "GE_corpus_canonicalizer.hpp"

// Deterministic replay log for corpus ingestion. No hashing/crypto.
// Each record references a payload byte-range within an on-disk blob and includes
// the computed SpiderCode4 + carrier parameters so replay can verify identical
// canonicalization/encoding.

struct GE_CorpusPulseRecord {
    uint8_t lane_u8 = 0;
    EwId9 domain_id9{};
    EwId9 source_id9{};
    uint32_t seq_u32 = 0;
    uint32_t offset_u32 = 0;
    uint32_t size_u32 = 0;

    SpiderCode4 sc4{};
    EwCarrierParams carrier{};

    std::string payload_relpath_utf8;
    uint64_t payload_byte_off_u64 = 0;
};

struct GE_CorpusPulseLog {
    std::vector<GE_CorpusPulseRecord> records;

    void clear();
    void sort_stable(); // deterministic ordering
    bool save_to_file(const std::string& path_utf8) const; // GEPL v1
    bool load_from_file(const std::string& path_utf8);     // GEPL v1
};

// Replay verification is UTF-8 strict and uses the CUDA encoder (no CPU path).
bool GE_pulse_record_verify_against_payload(const GE_CorpusPulseRecord& rec,
                                            const std::string& corpus_root_utf8,
                                            std::string* opt_err_utf8);
