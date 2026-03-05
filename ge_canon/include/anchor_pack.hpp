#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anchor.hpp"
#include "ew_id9.hpp"

namespace EigenWare {

struct AnchorPackBlob {
    std::string relpath_utf8;
    std::vector<uint8_t> comp_bytes_u8; // embedded bytes (raw)
    uint32_t raw_size_u32 = 0;
};

struct AnchorPackRecord {
    EwId9 artifact_id9{};
    std::string relpath_utf8;
    Anchor anchor;
};

namespace AnchorPackGen {
    void get_embedded_blobs(std::vector<AnchorPackBlob>& out);
}

bool AnchorPack_install(std::vector<AnchorPackRecord>& out_records,
                        uint32_t lane_u32,
                        const EwId9& domain_id9);

void AnchorPack_bytes_to_harmonics32_q15(const uint8_t* bytes, size_t n,
                                         uint16_t out_q15[32]);

EwId9 AnchorPack_id9_from_relpath(const std::string& relpath_utf8);

} // namespace EigenWare
