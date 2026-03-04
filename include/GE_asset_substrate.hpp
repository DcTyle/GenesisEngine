#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SubstrateManager;

namespace genesis {

// Per-project asset library substrate.
//
// Goals:
//  - deterministic folder/partition layout on disk
//  - stable, rebuildable content index used by a project content panel
//  - optional global cache that can be reused across projects
//
// This module does not implement UI; it provides a stable file format and
// deterministic listing outputs that the editor can project.

enum class GeAssetPartition : uint32_t {
    Worlds = 0,
    Planets = 1,
    Simulations = 2,
    Actors = 3,
    Character = 4,
    Foliage = 5,
    Assets = 6,
    Ai = 7,
};

struct GeAssetEntry {
    GeAssetPartition partition = GeAssetPartition::Assets;
    std::string relpath_utf8; // partition-relative (e.g. "Actors/box.geasset")
    std::string label_utf8;
    uint64_t object_id_u64 = 0;
    uint32_t kind_u32 = 0;
};

class GeAssetSubstrate {
public:
    GeAssetSubstrate() = default;

    // Initialize substrate roots and create default partitions/folders.
    // Safe to call multiple times.
    bool init(const std::string& project_root_utf8,
              const std::string& global_cache_root_utf8,
              const std::string& content_index_filename_utf8,
              std::string* out_err);

    // Save a runtime object as a .geasset into the project substrate, and (optionally)
    // mirror it into the global cache for reuse.
    bool save_object_asset(SubstrateManager* sm,
                           uint64_t object_id_u64,
                           uint32_t kind_u32,
                           GeAssetPartition partition,
                           bool mirror_to_global_cache,
                           std::string* out_err);

    // Scan the project substrate and rebuild its content index file.
    bool rebuild_project_index(std::string* out_err);

    // Deterministically list project substrate entries. Output is stable-sorted.
    bool list_project_entries(std::vector<GeAssetEntry>& out_entries, std::string* out_err) const;

    const std::string& project_root() const { return project_root_utf8_; }
    const std::string& global_cache_root() const { return global_cache_root_utf8_; }

private:
    std::string project_root_utf8_;
    std::string global_cache_root_utf8_;
    std::string index_filename_utf8_;

    static const char* partition_dirname(GeAssetPartition p);
    static void ensure_default_tree_(const std::string& root_utf8);
    static std::string join_path_(const std::string& a, const std::string& b);
    static std::string sanitize_filename_ascii_(const std::string& in_utf8);

    bool write_index_file_(const std::string& root_utf8, const std::vector<GeAssetEntry>& entries, std::string* out_err) const;
    bool scan_entries_(const std::string& root_utf8, std::vector<GeAssetEntry>& out_entries, std::string* out_err) const;
    bool save_into_root_(SubstrateManager* sm,
                         const std::string& root_utf8,
                         uint64_t object_id_u64,
                         uint32_t kind_u32,
                         GeAssetPartition partition,
                         std::string* out_path_utf8,
                         std::string* out_err);
};

} // namespace genesis
