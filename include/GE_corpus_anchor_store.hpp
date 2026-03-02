#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anchor.hpp"
#include "ew_id9.hpp"
#include "frequency_collapse.hpp"

struct GE_CorpusAnchorRecord {
    EwId9 anchor_id9{};
    EwId9 domain_id9{};
    EwId9 source_id9{};
    uint8_t lane_u8 = 0;
    uint32_t seq_u32 = 0;
    uint32_t offset_u32 = 0;
    uint32_t size_u32 = 0;

    SpiderCode4 sc4{};
    EwCarrierParams carrier{};

    std::string payload_relpath_utf8;
    uint64_t payload_byte_off_u64 = 0;
};

struct GE_CorpusAnchorStore {
    std::vector<GE_CorpusAnchorRecord> records;
    void clear();
    void sort_and_dedupe();
    bool save_to_file(const std::string& path_utf8) const;
    bool load_from_file(const std::string& path_utf8);
    std::vector<size_t> find_by_domain_lane(const EwId9& domain_id9, uint8_t lane_u8) const;
};

// Anchor identity must incorporate carrier parameters (addressable basis), not
// only provenance. This prevents distinct chunks with different carriers from
// aliasing under the same 9D identity.
EwId9 GE_anchor_id9_from_provenance_and_carrier(uint8_t lane_u8,
                                               const EwId9& domain_id9,
                                               const EwId9& source_id9,
                                               uint32_t seq_u32,
                                               uint32_t offset_u32,
                                               const EwCarrierParams& carrier);
