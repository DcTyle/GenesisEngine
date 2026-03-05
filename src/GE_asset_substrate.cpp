#include "GE_asset_substrate.hpp"

#include "GE_runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace genesis {

static inline bool _ensure_dirs(const std::string& p) {
    std::error_code ec;
    (void)std::filesystem::create_directories(p, ec);
    return !ec;
}

const char* GeAssetSubstrate::partition_dirname(GeAssetPartition p) {
    switch (p) {
        case GeAssetPartition::Worlds: return "Worlds";
        case GeAssetPartition::Planets: return "Planets";
        case GeAssetPartition::Simulations: return "Simulations";
        case GeAssetPartition::Actors: return "Actors";
        case GeAssetPartition::Character: return "Character";
        case GeAssetPartition::Foliage: return "Foliage";
        case GeAssetPartition::Assets: return "Assets";
        case GeAssetPartition::Ai: return "AI";
        default: return "Assets";
    }
}

std::string GeAssetSubstrate::join_path_(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string GeAssetSubstrate::sanitize_filename_ascii_(const std::string& in_utf8) {
    std::string s = in_utf8;
    if (s.empty()) s = "asset";
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char b = (unsigned char)s[i];
        const bool ok = (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_' || b == '-' || b == '.';
        if (!ok) s[i] = '_';
    }
    // Prevent directory traversal semantics.
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' || s[i] == '\\') s[i] = '_';
    }
    return s;
}

void GeAssetSubstrate::ensure_default_tree_(const std::string& root_utf8) {
    (void)_ensure_dirs(root_utf8);
    (void)_ensure_dirs(join_path_(root_utf8, "Worlds"));
    (void)_ensure_dirs(join_path_(root_utf8, "Planets"));
    (void)_ensure_dirs(join_path_(root_utf8, "Simulations"));
    (void)_ensure_dirs(join_path_(root_utf8, "Actors"));
    (void)_ensure_dirs(join_path_(root_utf8, "Character"));
    (void)_ensure_dirs(join_path_(root_utf8, "Foliage"));
    (void)_ensure_dirs(join_path_(root_utf8, "Assets"));

    // AI partition with stable subfolders.
    const std::string ai = join_path_(root_utf8, "AI");
    (void)_ensure_dirs(ai);
    (void)_ensure_dirs(join_path_(ai, "research"));
    (void)_ensure_dirs(join_path_(ai, "experiments"));
    (void)_ensure_dirs(join_path_(ai, "machines"));
    (void)_ensure_dirs(join_path_(ai, "inventions"));
}

bool GeAssetSubstrate::init(const std::string& project_root_utf8,
                            const std::string& global_cache_root_utf8,
                            const std::string& content_index_filename_utf8,
                            std::string* out_err) {
    project_root_utf8_ = project_root_utf8;
    global_cache_root_utf8_ = global_cache_root_utf8;
    index_filename_utf8_ = content_index_filename_utf8.empty() ? "content_index.gecontent" : content_index_filename_utf8;

    ensure_default_tree_(project_root_utf8_);
    ensure_default_tree_(global_cache_root_utf8_);

    // Create/refresh project index at init.
    return rebuild_project_index(out_err);
}

bool GeAssetSubstrate::save_into_root_(SubstrateMicroprocessor* sm,
                                      const std::string& root_utf8,
                                      uint64_t object_id_u64,
                                      uint32_t kind_u32,
                                      GeAssetPartition partition,
                                      std::string* out_path_utf8,
                                      std::string* out_err) {
    if (!sm) {
        if (out_err) *out_err = "null_substrate";
        return false;
    }
    const EwObjectEntry* e = sm->object_store.find(object_id_u64);
    if (!e) {
        if (out_err) *out_err = "object_not_found";
        return false;
    }

    const std::string part_dir = join_path_(root_utf8, partition_dirname(partition));
    (void)_ensure_dirs(part_dir);

    // Deterministic file name: label + kind + object id.
    std::string label = e->label_utf8.empty() ? std::string("asset") : e->label_utf8;
    label = sanitize_filename_ascii_(label);

    const std::string fname = label + "_k" + std::to_string((unsigned)kind_u32) + "_oid" + std::to_string((unsigned long long)object_id_u64) + ".geasset";
    const std::string full = join_path_(part_dir, fname);
    if (out_path_utf8) *out_path_utf8 = full;

    // Write the geasset via substrate-owned serializer.
    if (!sm->sim_save_object_asset_to_exact_path(full, object_id_u64, kind_u32)) {
        if (out_err) *out_err = "write_failed";
        return false;
    }
    return true;
}

bool GeAssetSubstrate::save_object_asset(SubstrateMicroprocessor* sm,
                                        uint64_t object_id_u64,
                                        uint32_t kind_u32,
                                        GeAssetPartition partition,
                                        bool mirror_to_global_cache,
                                        std::string* out_err) {
    std::string p;
    if (!save_into_root_(sm, project_root_utf8_, object_id_u64, kind_u32, partition, &p, out_err)) {
        return false;
    }
    if (mirror_to_global_cache) {
        std::string p2;
        (void)save_into_root_(sm, global_cache_root_utf8_, object_id_u64, kind_u32, partition, &p2, nullptr);
    }
    // Rebuild project index deterministically after write.
    return rebuild_project_index(out_err);
}

bool GeAssetSubstrate::scan_entries_(const std::string& root_utf8, std::vector<GeAssetEntry>& out_entries, std::string* out_err) const {
    out_entries.clear();
    std::error_code ec;
    if (!std::filesystem::exists(root_utf8, ec)) {
        if (out_err) *out_err = "root_missing";
        return false;
    }

    auto scan_partition = [&](GeAssetPartition part) {
        const std::string dir = join_path_(root_utf8, partition_dirname(part));
        if (!std::filesystem::exists(dir, ec)) return;
        for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            const std::string ext = it->path().extension().string();
            if (ext != ".geasset") continue;
            GeAssetEntry e{};
            e.partition = part;

            // Partition-relative path.
            std::filesystem::path rel = std::filesystem::relative(it->path(), root_utf8, ec);
            e.relpath_utf8 = ec ? it->path().string() : rel.generic_string();

            // Best-effort parse of the deterministic file name fields.
            // label_k<kind>_oid<object>.geasset
            const std::string stem = it->path().stem().string();
            e.label_utf8 = stem;
            size_t kpos = stem.rfind("_k");
            size_t opos = stem.rfind("_oid");
            if (kpos != std::string::npos && opos != std::string::npos && opos > kpos + 2) {
                e.label_utf8 = stem.substr(0, kpos);
                const std::string kstr = stem.substr(kpos + 2, opos - (kpos + 2));
                const std::string ostr = stem.substr(opos + 4);
                char* end = nullptr;
                long long kk = std::strtoll(kstr.c_str(), &end, 10);
                if (end && *end == '\0' && kk >= 0 && kk <= 0x7FFFFFFFLL) e.kind_u32 = (uint32_t)kk;
                end = nullptr;
                unsigned long long oo = std::strtoull(ostr.c_str(), &end, 10);
                if (end && *end == '\0') e.object_id_u64 = (uint64_t)oo;
            }
            out_entries.push_back(e);
        }
    };

    scan_partition(GeAssetPartition::Worlds);
    scan_partition(GeAssetPartition::Planets);
    scan_partition(GeAssetPartition::Simulations);
    scan_partition(GeAssetPartition::Actors);
    scan_partition(GeAssetPartition::Character);
    scan_partition(GeAssetPartition::Foliage);
    scan_partition(GeAssetPartition::Assets);
    scan_partition(GeAssetPartition::Ai);

    std::sort(out_entries.begin(), out_entries.end(), [](const GeAssetEntry& a, const GeAssetEntry& b) {
        if ((uint32_t)a.partition != (uint32_t)b.partition) return (uint32_t)a.partition < (uint32_t)b.partition;
        return a.relpath_utf8 < b.relpath_utf8;
    });
    return true;
}

bool GeAssetSubstrate::write_index_file_(const std::string& root_utf8, const std::vector<GeAssetEntry>& entries, std::string* out_err) const {
    const std::string idx = join_path_(root_utf8, index_filename_utf8_);
    std::ofstream out(idx.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        if (out_err) *out_err = "index_write_failed";
        return false;
    }

    // Binary format (deterministic):
    //  u32 magic 'GECP' (Genesis Engine Content Panel)
    //  u32 ver 1
    //  u32 entry_count
    //  repeated entries:
    //    u32 partition
    //    u32 relpath_len
    //    bytes relpath
    //    u32 label_len
    //    bytes label
    //    u64 object_id
    //    u32 kind
    const uint32_t magic = 0x50434547U; // 'GECP'
    const uint32_t ver = 1u;
    const uint32_t n = (uint32_t)entries.size();
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const auto& e : entries) {
        const uint32_t part = (uint32_t)e.partition;
        const uint32_t rlen = (uint32_t)e.relpath_utf8.size();
        const uint32_t llen = (uint32_t)e.label_utf8.size();
        out.write(reinterpret_cast<const char*>(&part), sizeof(part));
        out.write(reinterpret_cast<const char*>(&rlen), sizeof(rlen));
        if (rlen) out.write(e.relpath_utf8.data(), (std::streamsize)rlen);
        out.write(reinterpret_cast<const char*>(&llen), sizeof(llen));
        if (llen) out.write(e.label_utf8.data(), (std::streamsize)llen);
        out.write(reinterpret_cast<const char*>(&e.object_id_u64), sizeof(e.object_id_u64));
        out.write(reinterpret_cast<const char*>(&e.kind_u32), sizeof(e.kind_u32));
    }
    if (!out.good()) {
        if (out_err) *out_err = "index_write_incomplete";
        return false;
    }
    return true;
}

bool GeAssetSubstrate::rebuild_project_index(std::string* out_err) {
    std::vector<GeAssetEntry> entries;
    if (!scan_entries_(project_root_utf8_, entries, out_err)) return false;
    return write_index_file_(project_root_utf8_, entries, out_err);
}

bool GeAssetSubstrate::list_project_entries(std::vector<GeAssetEntry>& out_entries, std::string* out_err) const {
    return scan_entries_(project_root_utf8_, out_entries, out_err);
}

} // namespace genesis
