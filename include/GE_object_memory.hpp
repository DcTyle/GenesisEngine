#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "assimp_fbx_io.hpp" // FBX/glTF import/export (Assimp)

// Object Memory Reference Operator (OMRO)
// Blueprint: Section C.

struct EwGeomCoord9 {
    uint64_t u64x9[9];
};

struct EwObjectEntry {
    uint64_t object_id_u64;
    // Optional human label (UTF-8). Empty string indicates unset.
    // NOTE: Determinism rule: no locale-dependent transforms are applied.
    std::string label_utf8;
    // Mass/cost debit for creation/import (Q32.32).
    int64_t mass_or_cost_q32_32;
    // Immutable geometry coordinate coord-tag (9D).
    EwGeomCoord9 geomcoord9_u64x9;
    // Deterministically derived phase key.
    uint64_t phase_seed_u64;

    // -----------------------------------------------------------------
    // Genesis synthesis outputs (voxelized volume)
    // -----------------------------------------------------------------
    // All imported assets must be synthesized into a voxel volume before
    // they are considered runtime-usable for physics/control binding.
    //
    // Voxel format:
    //  0 = unset
    //  1 = occupancy_u8 (0..255)
    uint32_t voxel_grid_x_u32 = 0;
    uint32_t voxel_grid_y_u32 = 0;
    uint32_t voxel_grid_z_u32 = 0;
    uint32_t voxel_format_u32 = 0;
    // Blob id used by EwObjectStore to locate the packed volume bytes.
    // Determinism rule: blob id equals object_id.
    uint64_t voxel_blob_id_u64 = 0;
};

struct EwVoxelVolumeView {
    uint32_t grid_x_u32 = 0;
    uint32_t grid_y_u32 = 0;
    uint32_t grid_z_u32 = 0;
    uint32_t format_u32 = 0;
    const uint8_t* bytes = nullptr;
    size_t byte_count = 0;
};

class EwObjectStore {
public:
    // Insert or replace an object entry by object_id.
    // Storage is kept in ascending object_id order for deterministic iteration.
    bool upsert(const EwObjectEntry& e);

    // Lookup by id.
    const EwObjectEntry* find(uint64_t object_id_u64) const;

    // Upsert voxel volume bytes for an object.
    // Returns false if the grid is invalid.
    bool upsert_voxel_volume_occupancy_u8(uint64_t object_id_u64,
                                          uint32_t grid_x_u32,
                                          uint32_t grid_y_u32,
                                          uint32_t grid_z_u32,
                                          const uint8_t* voxel_u8,
                                          size_t byte_count);

    // View voxel bytes for an object (if present).
    bool view_voxel_volume(uint64_t object_id_u64, EwVoxelVolumeView& out_view) const;

    // Deterministic counts in ascending object_id order.
    void compute_object_counts_sorted(const std::vector<uint64_t>& object_ids,
                                      std::vector<std::pair<uint64_t, uint32_t>>& out_counts_sorted) const;

private:
    std::vector<EwObjectEntry> entries_sorted_;

    // Packed voxel volumes keyed by object_id (blob id). Storage is append-only
    // to keep offsets stable for deterministic replay.
    struct VoxelIndex {
        uint64_t object_id_u64;
        uint64_t offset_u64;
        uint64_t size_u64;
        uint32_t grid_x_u32;
        uint32_t grid_y_u32;
        uint32_t grid_z_u32;
        uint32_t format_u32;
    };
    std::vector<VoxelIndex> voxel_index_sorted_;
    std::vector<uint8_t> voxel_blob_bytes_;
};