#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "assimp_fbx_io.hpp" // FBX/glTF import/export (Assimp)
#include "GE_object_dna.hpp"

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
    // Canonical photonic-confinement DNA for manifold/existence coupling.
    EwObjectDna object_dna;

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

    // -----------------------------------------------------------------
    // UV Data Atlas (variables baked into UV space)
    // -----------------------------------------------------------------
    // The engine auto-generates deterministic UVs and bakes per-object
    // voxel/local-lattice variables into an atlas texture.
    // Atlas format:
    //  RGBA8_UNORM packed (0..255)
    //    R: density/occupancy
    //    G: coherence proxy
    //    B: curvature proxy
    //    A: object-id low byte (identity tag)
    uint32_t uv_atlas_w_u32 = 0;
    uint32_t uv_atlas_h_u32 = 0;
    uint32_t uv_atlas_format_u32 = 0;
    uint64_t uv_atlas_blob_id_u64 = 0;

    // -----------------------------------------------------------------
    // Object-local lattice state (per-voxel)
    // -----------------------------------------------------------------
    // This is the per-object sublattice state used for internal dynamics.
    // It is coupled to the global lattice via boundary exchange.
    // Local lattice format:
    //  0 = unset
    //  1 = phi_q15_s16 (int16 per voxel, Q15 in [-1,1] represented as [-32768,32767])
    uint32_t local_grid_x_u32 = 0;
    uint32_t local_grid_y_u32 = 0;
    uint32_t local_grid_z_u32 = 0;
    uint32_t local_format_u32 = 0;
    uint64_t local_blob_id_u64 = 0;
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

    // Upsert a baked UV data atlas for an object.
    bool upsert_uv_atlas_rgba8(uint64_t object_id_u64,
                              uint32_t w_u32,
                              uint32_t h_u32,
                              const uint8_t* rgba8,
                              size_t byte_count);

    bool view_uv_atlas(uint64_t object_id_u64,
                       uint32_t& out_w_u32,
                       uint32_t& out_h_u32,
                       uint32_t& out_format_u32,
                       const uint8_t*& out_bytes,
                       size_t& out_byte_count) const;

    // Upsert object-local lattice state (phi_q15_s16).
    bool upsert_local_phi_q15_s16(uint64_t object_id_u64,
                                  uint32_t grid_x_u32,
                                  uint32_t grid_y_u32,
                                  uint32_t grid_z_u32,
                                  const int16_t* phi_q15_s16,
                                  size_t byte_count);

    // View object-local lattice state (phi_q15_s16).
    bool view_local_phi(uint64_t object_id_u64,
                        uint32_t& out_gx_u32,
                        uint32_t& out_gy_u32,
                        uint32_t& out_gz_u32,
                        uint32_t& out_format_u32,
                        const int16_t*& out_phi_q15_s16,
                        size_t& out_byte_count) const;

    // Deterministic counts in ascending object_id order.
    void compute_object_counts_sorted(const std::vector<uint64_t>& object_ids,
                                      std::vector<std::pair<uint64_t, uint32_t>>& out_counts_sorted) const;

    // List all object ids in ascending order (deterministic).
    void list_object_ids_sorted(std::vector<uint64_t>& out_object_ids) const;

    // Deterministic binary serialization for simulation replay/injection.
    // Format versioned; no compression; bounded by caller policy.
    bool serialize_binary(std::ostream& out) const;
    bool deserialize_binary(std::istream& in);

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

    struct UvAtlasIndex {
        uint64_t object_id_u64;
        uint64_t offset_u64;
        uint64_t size_u64;
        uint32_t w_u32;
        uint32_t h_u32;
        uint32_t format_u32;
    };
    std::vector<UvAtlasIndex> uv_index_sorted_;
    std::vector<uint8_t> uv_blob_bytes_;

    struct LocalIndex {
        uint64_t object_id_u64;
        uint64_t offset_u64;
        uint64_t size_u64;
        uint32_t grid_x_u32;
        uint32_t grid_y_u32;
        uint32_t grid_z_u32;
        uint32_t format_u32;
    };
    std::vector<LocalIndex> local_index_sorted_;
    std::vector<uint8_t> local_blob_bytes_;
};
