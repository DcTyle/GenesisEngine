#include "GE_object_memory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

static inline bool _lt_by_id(const EwObjectEntry& a, uint64_t id) {
    return a.object_id_u64 < id;
}

bool EwObjectStore::upsert(const EwObjectEntry& e) {
    // Binary search insertion point.
    size_t lo = 0;
    size_t hi = entries_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entries_sorted_[mid].object_id_u64 < e.object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    if (lo < entries_sorted_.size() && entries_sorted_[lo].object_id_u64 == e.object_id_u64) {
        entries_sorted_[lo] = e;
        return true;
    }
    entries_sorted_.insert(entries_sorted_.begin() + (std::ptrdiff_t)lo, e);
    return true;
}

const EwObjectEntry* EwObjectStore::find(uint64_t object_id_u64) const {
    size_t lo = 0;
    size_t hi = entries_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const uint64_t mid_id = entries_sorted_[mid].object_id_u64;
        if (mid_id == object_id_u64) return &entries_sorted_[mid];
        if (mid_id < object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

bool EwObjectStore::upsert_voxel_volume_occupancy_u8(uint64_t object_id_u64,
                                                     uint32_t grid_x_u32,
                                                     uint32_t grid_y_u32,
                                                     uint32_t grid_z_u32,
                                                     const uint8_t* voxel_u8,
                                                     size_t byte_count) {
    if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) return false;
    const uint64_t need = (uint64_t)grid_x_u32 * (uint64_t)grid_y_u32 * (uint64_t)grid_z_u32;
    if (need == 0) return false;
    if (byte_count != (size_t)need) return false;
    if (!voxel_u8) return false;

    // Append-only blob pack.
    const uint64_t off = (uint64_t)voxel_blob_bytes_.size();
    voxel_blob_bytes_.resize((size_t)(off + need));
    std::memcpy(voxel_blob_bytes_.data() + (size_t)off, voxel_u8, (size_t)need);

    // Upsert index (kept sorted by object id).
    size_t lo = 0;
    size_t hi = voxel_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (voxel_index_sorted_[mid].object_id_u64 < object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    EwObjectStore::VoxelIndex idx;
    idx.object_id_u64 = object_id_u64;
    idx.offset_u64 = off;
    idx.size_u64 = need;
    idx.grid_x_u32 = grid_x_u32;
    idx.grid_y_u32 = grid_y_u32;
    idx.grid_z_u32 = grid_z_u32;
    idx.format_u32 = 1;
    if (lo < voxel_index_sorted_.size() && voxel_index_sorted_[lo].object_id_u64 == object_id_u64) {
        voxel_index_sorted_[lo] = idx;
    } else {
        voxel_index_sorted_.insert(voxel_index_sorted_.begin() + (std::ptrdiff_t)lo, idx);
    }
    return true;
}

bool EwObjectStore::view_voxel_volume(uint64_t object_id_u64, EwVoxelVolumeView& out_view) const {
    out_view = EwVoxelVolumeView{};
    size_t lo = 0;
    size_t hi = voxel_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const uint64_t mid_id = voxel_index_sorted_[mid].object_id_u64;
        if (mid_id == object_id_u64) {
            const auto& idx = voxel_index_sorted_[mid];
            if (idx.offset_u64 + idx.size_u64 > (uint64_t)voxel_blob_bytes_.size()) return false;
            out_view.grid_x_u32 = idx.grid_x_u32;
            out_view.grid_y_u32 = idx.grid_y_u32;
            out_view.grid_z_u32 = idx.grid_z_u32;
            out_view.format_u32 = idx.format_u32;
            out_view.bytes = voxel_blob_bytes_.data() + (size_t)idx.offset_u64;
            out_view.byte_count = (size_t)idx.size_u64;
            return true;
        }
        if (mid_id < object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

void EwObjectStore::compute_object_counts_sorted(const std::vector<uint64_t>& object_ids,
                                                std::vector<std::pair<uint64_t, uint32_t>>& out_counts_sorted) const {
    out_counts_sorted.clear();
    // Count by sorting a copy of ids (deterministic by numeric order).
    std::vector<uint64_t> ids = object_ids;
    // Deterministic sort (std::sort is deterministic for same inputs).
    std::sort(ids.begin(), ids.end());
    uint64_t cur = 0;
    uint32_t cnt = 0;
    bool have = false;
    for (uint64_t id : ids) {
        if (!have) { cur = id; cnt = 1; have = true; continue; }
        if (id == cur) { cnt += 1; continue; }
        out_counts_sorted.push_back(std::make_pair(cur, cnt));
        cur = id;
        cnt = 1;
    }
    if (have) out_counts_sorted.push_back(std::make_pair(cur, cnt));
}
