#include "GE_object_memory.hpp"

static inline bool ew_write_u32(std::ostream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return out.good();
}
static inline bool ew_write_u64(std::ostream& out, uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return out.good();
}
static inline bool ew_write_i64(std::ostream& out, int64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return out.good();
}
static inline bool ew_write_f32(std::ostream& out, float v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return out.good();
}
static inline bool ew_read_u32(std::istream& in, uint32_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.good();
}
static inline bool ew_read_u64(std::istream& in, uint64_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.good();
}
static inline bool ew_read_i64(std::istream& in, int64_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.good();
}
static inline bool ew_read_f32(std::istream& in, float& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.good();
}

bool EwObjectStore::serialize_binary(std::ostream& out) const {
    // Header
    const uint32_t magic = 0x4F4D524FUL; // 'OMRO'
    const uint32_t ver = 2u;
    if (!ew_write_u32(out, magic)) return false;
    if (!ew_write_u32(out, ver)) return false;

    // Entries (metadata)
    if (!ew_write_u32(out, (uint32_t)entries_sorted_.size())) return false;
    for (const auto& e : entries_sorted_) {
        if (!ew_write_u64(out, e.object_id_u64)) return false;
        // label
        uint32_t n = (uint32_t)e.label_utf8.size();
        if (n > 4096u) n = 4096u;
        if (!ew_write_u32(out, n)) return false;
        if (n) out.write(e.label_utf8.data(), (std::streamsize)n);
        if (!out.good()) return false;
        if (!ew_write_i64(out, e.mass_or_cost_q32_32)) return false;
        for (size_t i = 0; i < 9u; ++i) {
            if (!ew_write_u64(out, e.geomcoord9_u64x9.u64x9[i])) return false;
        }
        if (!ew_write_u64(out, e.phase_seed_u64)) return false;
#define EW_OBJECT_DNA_WRITE_SCALAR(name, default_value) if (!ew_write_f32(out, e.object_dna.name)) return false;
        EW_OBJECT_DNA_SCALAR_FIELDS(EW_OBJECT_DNA_WRITE_SCALAR)
        EW_OBJECT_DNA_6DOF_FIELDS(EW_OBJECT_DNA_WRITE_SCALAR)
#undef EW_OBJECT_DNA_WRITE_SCALAR
        if (!ew_write_u32(out, e.voxel_grid_x_u32)) return false;
        if (!ew_write_u32(out, e.voxel_grid_y_u32)) return false;
        if (!ew_write_u32(out, e.voxel_grid_z_u32)) return false;
        if (!ew_write_u32(out, e.voxel_format_u32)) return false;
        if (!ew_write_u64(out, e.voxel_blob_id_u64)) return false;
        if (!ew_write_u32(out, e.uv_atlas_w_u32)) return false;
        if (!ew_write_u32(out, e.uv_atlas_h_u32)) return false;
        if (!ew_write_u32(out, e.uv_atlas_format_u32)) return false;
        if (!ew_write_u64(out, e.uv_atlas_blob_id_u64)) return false;
        if (!ew_write_u32(out, e.local_grid_x_u32)) return false;
        if (!ew_write_u32(out, e.local_grid_y_u32)) return false;
        if (!ew_write_u32(out, e.local_grid_z_u32)) return false;
        if (!ew_write_u32(out, e.local_format_u32)) return false;
        if (!ew_write_u64(out, e.local_blob_id_u64)) return false;
    }

    // Voxel index + blob
    if (!ew_write_u32(out, (uint32_t)voxel_index_sorted_.size())) return false;
    for (const auto& vi : voxel_index_sorted_) {
        if (!ew_write_u64(out, vi.object_id_u64)) return false;
        if (!ew_write_u64(out, vi.offset_u64)) return false;
        if (!ew_write_u64(out, vi.size_u64)) return false;
        if (!ew_write_u32(out, vi.grid_x_u32)) return false;
        if (!ew_write_u32(out, vi.grid_y_u32)) return false;
        if (!ew_write_u32(out, vi.grid_z_u32)) return false;
        if (!ew_write_u32(out, vi.format_u32)) return false;
    }
    if (!ew_write_u64(out, (uint64_t)voxel_blob_bytes_.size())) return false;
    if (!voxel_blob_bytes_.empty()) {
        out.write(reinterpret_cast<const char*>(voxel_blob_bytes_.data()), (std::streamsize)voxel_blob_bytes_.size());
        if (!out.good()) return false;
    }

    // UV index + blob
    if (!ew_write_u32(out, (uint32_t)uv_index_sorted_.size())) return false;
    for (const auto& ui : uv_index_sorted_) {
        if (!ew_write_u64(out, ui.object_id_u64)) return false;
        if (!ew_write_u64(out, ui.offset_u64)) return false;
        if (!ew_write_u64(out, ui.size_u64)) return false;
        if (!ew_write_u32(out, ui.w_u32)) return false;
        if (!ew_write_u32(out, ui.h_u32)) return false;
        if (!ew_write_u32(out, ui.format_u32)) return false;
    }
    if (!ew_write_u64(out, (uint64_t)uv_blob_bytes_.size())) return false;
    if (!uv_blob_bytes_.empty()) {
        out.write(reinterpret_cast<const char*>(uv_blob_bytes_.data()), (std::streamsize)uv_blob_bytes_.size());
        if (!out.good()) return false;
    }

    // Local lattice index + blob
    if (!ew_write_u32(out, (uint32_t)local_index_sorted_.size())) return false;
    for (const auto& li : local_index_sorted_) {
        if (!ew_write_u64(out, li.object_id_u64)) return false;
        if (!ew_write_u64(out, li.offset_u64)) return false;
        if (!ew_write_u64(out, li.size_u64)) return false;
        if (!ew_write_u32(out, li.grid_x_u32)) return false;
        if (!ew_write_u32(out, li.grid_y_u32)) return false;
        if (!ew_write_u32(out, li.grid_z_u32)) return false;
        if (!ew_write_u32(out, li.format_u32)) return false;
    }
    if (!ew_write_u64(out, (uint64_t)local_blob_bytes_.size())) return false;
    if (!local_blob_bytes_.empty()) {
        out.write(reinterpret_cast<const char*>(local_blob_bytes_.data()), (std::streamsize)local_blob_bytes_.size());
        if (!out.good()) return false;
    }

    return out.good();
}

bool EwObjectStore::deserialize_binary(std::istream& in) {
    uint32_t magic = 0, ver = 0;
    if (!ew_read_u32(in, magic)) return false;
    if (!ew_read_u32(in, ver)) return false;
    if (magic != 0x4F4D524FUL || ver != 2u) return false;

    entries_sorted_.clear();
    voxel_index_sorted_.clear();
    uv_index_sorted_.clear();
    voxel_blob_bytes_.clear();
    uv_blob_bytes_.clear();
    local_index_sorted_.clear();
    local_blob_bytes_.clear();

    uint32_t n_entries = 0;
    if (!ew_read_u32(in, n_entries)) return false;
    if (n_entries > 100000u) return false;
    entries_sorted_.reserve(n_entries);
    for (uint32_t i = 0; i < n_entries; ++i) {
        EwObjectEntry e{};
        if (!ew_read_u64(in, e.object_id_u64)) return false;
        uint32_t nlab = 0;
        if (!ew_read_u32(in, nlab)) return false;
        if (nlab > 4096u) return false;
        e.label_utf8.resize(nlab);
        if (nlab) {
            in.read(&e.label_utf8[0], (std::streamsize)nlab);
            if (!in.good()) return false;
        }
        if (!ew_read_i64(in, e.mass_or_cost_q32_32)) return false;
        for (size_t g = 0; g < 9u; ++g) {
            if (!ew_read_u64(in, e.geomcoord9_u64x9.u64x9[g])) return false;
        }
        if (!ew_read_u64(in, e.phase_seed_u64)) return false;
#define EW_OBJECT_DNA_READ_SCALAR(name, default_value) if (!ew_read_f32(in, e.object_dna.name)) return false;
        EW_OBJECT_DNA_SCALAR_FIELDS(EW_OBJECT_DNA_READ_SCALAR)
        EW_OBJECT_DNA_6DOF_FIELDS(EW_OBJECT_DNA_READ_SCALAR)
#undef EW_OBJECT_DNA_READ_SCALAR
        if (!ew_read_u32(in, e.voxel_grid_x_u32)) return false;
        if (!ew_read_u32(in, e.voxel_grid_y_u32)) return false;
        if (!ew_read_u32(in, e.voxel_grid_z_u32)) return false;
        if (!ew_read_u32(in, e.voxel_format_u32)) return false;
        if (!ew_read_u64(in, e.voxel_blob_id_u64)) return false;
        if (!ew_read_u32(in, e.uv_atlas_w_u32)) return false;
        if (!ew_read_u32(in, e.uv_atlas_h_u32)) return false;
        if (!ew_read_u32(in, e.uv_atlas_format_u32)) return false;
        if (!ew_read_u64(in, e.uv_atlas_blob_id_u64)) return false;
        if (!ew_read_u32(in, e.local_grid_x_u32)) return false;
        if (!ew_read_u32(in, e.local_grid_y_u32)) return false;
        if (!ew_read_u32(in, e.local_grid_z_u32)) return false;
        if (!ew_read_u32(in, e.local_format_u32)) return false;
        if (!ew_read_u64(in, e.local_blob_id_u64)) return false;
        entries_sorted_.push_back(e);
    }

    uint32_t n_vox = 0;
    if (!ew_read_u32(in, n_vox)) return false;
    if (n_vox > 100000u) return false;
    voxel_index_sorted_.reserve(n_vox);
    for (uint32_t i = 0; i < n_vox; ++i) {
        VoxelIndex vi{};
        if (!ew_read_u64(in, vi.object_id_u64)) return false;
        if (!ew_read_u64(in, vi.offset_u64)) return false;
        if (!ew_read_u64(in, vi.size_u64)) return false;
        if (!ew_read_u32(in, vi.grid_x_u32)) return false;
        if (!ew_read_u32(in, vi.grid_y_u32)) return false;
        if (!ew_read_u32(in, vi.grid_z_u32)) return false;
        if (!ew_read_u32(in, vi.format_u32)) return false;
        voxel_index_sorted_.push_back(vi);
    }
    uint64_t vox_blob_n = 0;
    if (!ew_read_u64(in, vox_blob_n)) return false;
    if (vox_blob_n > (uint64_t)256 * 1024 * 1024) return false;
    voxel_blob_bytes_.resize((size_t)vox_blob_n);
    if (vox_blob_n) {
        in.read(reinterpret_cast<char*>(voxel_blob_bytes_.data()), (std::streamsize)vox_blob_n);
        if (!in.good()) return false;
    }

    uint32_t n_uv = 0;
    if (!ew_read_u32(in, n_uv)) return false;
    if (n_uv > 100000u) return false;
    uv_index_sorted_.reserve(n_uv);
    for (uint32_t i = 0; i < n_uv; ++i) {
        UvAtlasIndex ui{};
        if (!ew_read_u64(in, ui.object_id_u64)) return false;
        if (!ew_read_u64(in, ui.offset_u64)) return false;
        if (!ew_read_u64(in, ui.size_u64)) return false;
        if (!ew_read_u32(in, ui.w_u32)) return false;
        if (!ew_read_u32(in, ui.h_u32)) return false;
        if (!ew_read_u32(in, ui.format_u32)) return false;
        uv_index_sorted_.push_back(ui);
    }
    uint64_t uv_blob_n = 0;
    if (!ew_read_u64(in, uv_blob_n)) return false;
    if (uv_blob_n > (uint64_t)256 * 1024 * 1024) return false;
    uv_blob_bytes_.resize((size_t)uv_blob_n);
    if (uv_blob_n) {
        in.read(reinterpret_cast<char*>(uv_blob_bytes_.data()), (std::streamsize)uv_blob_n);
        if (!in.good()) return false;
    }

    // Local lattice index + blob
    uint32_t n_loc = 0;
    if (!ew_read_u32(in, n_loc)) return false;
    if (n_loc > 100000u) return false;
    local_index_sorted_.reserve(n_loc);
    for (uint32_t i = 0; i < n_loc; ++i) {
        LocalIndex li{};
        if (!ew_read_u64(in, li.object_id_u64)) return false;
        if (!ew_read_u64(in, li.offset_u64)) return false;
        if (!ew_read_u64(in, li.size_u64)) return false;
        if (!ew_read_u32(in, li.grid_x_u32)) return false;
        if (!ew_read_u32(in, li.grid_y_u32)) return false;
        if (!ew_read_u32(in, li.grid_z_u32)) return false;
        if (!ew_read_u32(in, li.format_u32)) return false;
        local_index_sorted_.push_back(li);
    }
    uint64_t loc_blob_n = 0;
    if (!ew_read_u64(in, loc_blob_n)) return false;
    if (loc_blob_n > (uint64_t)512 * 1024 * 1024) return false;
    local_blob_bytes_.resize((size_t)loc_blob_n);
    if (loc_blob_n) {
        in.read(reinterpret_cast<char*>(local_blob_bytes_.data()), (std::streamsize)loc_blob_n);
        if (!in.good()) return false;
    }
    return true;
}

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

void EwObjectStore::list_object_ids_sorted(std::vector<uint64_t>& out_object_ids) const {
    out_object_ids.clear();
    out_object_ids.reserve(entries_sorted_.size());
    for (const auto& e : entries_sorted_) out_object_ids.push_back(e.object_id_u64);
}

bool EwObjectStore::upsert_uv_atlas_rgba8(uint64_t object_id_u64,
                                         uint32_t w_u32,
                                         uint32_t h_u32,
                                         const uint8_t* rgba8,
                                         size_t byte_count) {
    if (w_u32 == 0 || h_u32 == 0) return false;
    const uint64_t need = (uint64_t)w_u32 * (uint64_t)h_u32 * 4u;
    if (need == 0) return false;
    if (byte_count != (size_t)need) return false;
    if (!rgba8) return false;

    const uint64_t off = (uint64_t)uv_blob_bytes_.size();
    uv_blob_bytes_.resize((size_t)(off + need));
    std::memcpy(uv_blob_bytes_.data() + (size_t)off, rgba8, (size_t)need);

    size_t lo = 0;
    size_t hi = uv_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (uv_index_sorted_[mid].object_id_u64 < object_id_u64) lo = mid + 1;
        else hi = mid;
    }

    EwObjectStore::UvAtlasIndex idx;
    idx.object_id_u64 = object_id_u64;
    idx.offset_u64 = off;
    idx.size_u64 = need;
    idx.w_u32 = w_u32;
    idx.h_u32 = h_u32;
    idx.format_u32 = 1; // RGBA8_UNORM
    if (lo < uv_index_sorted_.size() && uv_index_sorted_[lo].object_id_u64 == object_id_u64) {
        uv_index_sorted_[lo] = idx;
    } else {
        uv_index_sorted_.insert(uv_index_sorted_.begin() + (std::ptrdiff_t)lo, idx);
    }
    return true;
}

bool EwObjectStore::view_uv_atlas(uint64_t object_id_u64,
                                 uint32_t& out_w_u32,
                                 uint32_t& out_h_u32,
                                 uint32_t& out_format_u32,
                                 const uint8_t*& out_bytes,
                                 size_t& out_byte_count) const {
    out_w_u32 = 0;
    out_h_u32 = 0;
    out_format_u32 = 0;
    out_bytes = nullptr;
    out_byte_count = 0;

    size_t lo = 0;
    size_t hi = uv_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const uint64_t mid_id = uv_index_sorted_[mid].object_id_u64;
        if (mid_id == object_id_u64) {
            const auto& idx = uv_index_sorted_[mid];
            if (idx.offset_u64 + idx.size_u64 > (uint64_t)uv_blob_bytes_.size()) return false;
            out_w_u32 = idx.w_u32;
            out_h_u32 = idx.h_u32;
            out_format_u32 = idx.format_u32;
            out_bytes = uv_blob_bytes_.data() + (size_t)idx.offset_u64;
            out_byte_count = (size_t)idx.size_u64;
            return true;
        }
        if (mid_id < object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

bool EwObjectStore::upsert_local_phi_q15_s16(uint64_t object_id_u64,
                                             uint32_t grid_x_u32,
                                             uint32_t grid_y_u32,
                                             uint32_t grid_z_u32,
                                             const int16_t* phi_q15_s16,
                                             size_t byte_count) {
    if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) return false;
    const uint64_t vox_n = (uint64_t)grid_x_u32 * (uint64_t)grid_y_u32 * (uint64_t)grid_z_u32;
    if (vox_n == 0) return false;
    const uint64_t need = vox_n * 2ull;
    if (byte_count != (size_t)need) return false;
    if (!phi_q15_s16) return false;

    const uint64_t off = (uint64_t)local_blob_bytes_.size();
    local_blob_bytes_.resize((size_t)(off + need));
    std::memcpy(local_blob_bytes_.data() + (size_t)off, phi_q15_s16, (size_t)need);

    size_t lo = 0;
    size_t hi = local_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (local_index_sorted_[mid].object_id_u64 < object_id_u64) lo = mid + 1;
        else hi = mid;
    }

    EwObjectStore::LocalIndex idx;
    idx.object_id_u64 = object_id_u64;
    idx.offset_u64 = off;
    idx.size_u64 = need;
    idx.grid_x_u32 = grid_x_u32;
    idx.grid_y_u32 = grid_y_u32;
    idx.grid_z_u32 = grid_z_u32;
    idx.format_u32 = 1u; // phi_q15_s16
    if (lo < local_index_sorted_.size() && local_index_sorted_[lo].object_id_u64 == object_id_u64) {
        local_index_sorted_[lo] = idx;
    } else {
        local_index_sorted_.insert(local_index_sorted_.begin() + (std::ptrdiff_t)lo, idx);
    }
    return true;
}

bool EwObjectStore::view_local_phi(uint64_t object_id_u64,
                                   uint32_t& out_gx_u32,
                                   uint32_t& out_gy_u32,
                                   uint32_t& out_gz_u32,
                                   uint32_t& out_format_u32,
                                   const int16_t*& out_phi_q15_s16,
                                   size_t& out_byte_count) const {
    out_gx_u32 = out_gy_u32 = out_gz_u32 = 0;
    out_format_u32 = 0;
    out_phi_q15_s16 = nullptr;
    out_byte_count = 0;

    size_t lo = 0;
    size_t hi = local_index_sorted_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const uint64_t mid_id = local_index_sorted_[mid].object_id_u64;
        if (mid_id == object_id_u64) {
            const auto& idx = local_index_sorted_[mid];
            if (idx.offset_u64 + idx.size_u64 > (uint64_t)local_blob_bytes_.size()) return false;
            out_gx_u32 = idx.grid_x_u32;
            out_gy_u32 = idx.grid_y_u32;
            out_gz_u32 = idx.grid_z_u32;
            out_format_u32 = idx.format_u32;
            out_phi_q15_s16 = reinterpret_cast<const int16_t*>(local_blob_bytes_.data() + (size_t)idx.offset_u64);
            out_byte_count = (size_t)idx.size_u64;
            return true;
        }
        if (mid_id < object_id_u64) lo = mid + 1;
        else hi = mid;
    }
    return false;
}
