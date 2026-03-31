#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SubstrateManager;

// RepoReaderAdapter: deterministic corpus source that ingests the engine's own
// source/spec files into the same observation pipeline as the crawler.
//
// This bridge is intentionally opt-in. It allows the canonical runtime to read
// merged-repository source/spec surfaces without rewriting the live runtime tree.
struct GE_RepoReader {
    bool enabled = false;
    bool scanned = false;

    // Deterministic file list (relative ASCII paths).
    std::vector<std::string> files_rel_ascii;
    std::string repo_root_ascii;
    uint32_t cursor_u32 = 0;
    uint64_t next_seq_u64 = 1;

    // Build a deterministic file list from the repo root.
    void scan_repo_root();

    // Tick: read up to files_per_tick, each capped at bytes_per_file, emit observations.
    void tick(SubstrateManager* sm, uint32_t files_per_tick_u32, uint32_t bytes_per_file_u32);

    // Deterministic status line for UI.
    std::string status_line(uint32_t files_per_tick_u32, uint32_t bytes_per_file_u32) const;
};
