#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "inspector_fields.hpp"

// Hydrator projects substrate-resident inspector artifacts into a functioning
// workspace file tree. This is a deterministic projection step.

struct EwHydrationReceiptRow {
    uint64_t coord_sig9_u64 = 0;
    std::string rel_path;
    uint32_t bytes_written_u32 = 0;
};

struct EwHydrationReceipt {
    uint64_t hydration_tick_u64 = 0;
    std::vector<EwHydrationReceiptRow> rows;
};

class EwHydrator {
public:
    // Project all commit-ready artifacts into root_dir.
    // Returns true on full success. On failure, no promises about partial writes.
    static bool hydrate_workspace(
        const std::string& root_dir,
        uint64_t hydration_tick_u64,
        const std::vector<EwInspectorArtifact>& committed,
        EwHydrationReceipt& out_receipt,
        std::string& out_error
    );
};
