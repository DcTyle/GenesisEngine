#include "hydrator.hpp"

#include <cstdio>
#include <filesystem>

static bool write_file_bytes(const std::string& abs_path, const std::string& payload) {
    FILE* f = std::fopen(abs_path.c_str(), "wb");
    if (!f) return false;
    if (!payload.empty()) {
        const size_t n = std::fwrite(payload.data(), 1, payload.size(), f);
        if (n != payload.size()) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool EwHydrator::hydrate_workspace(
    const std::string& root_dir,
    uint64_t hydration_tick_u64,
    const std::vector<EwInspectorArtifact>& committed,
    EwHydrationReceipt& out_receipt,
    std::string& out_error
) {
    out_receipt.hydration_tick_u64 = hydration_tick_u64;
    out_receipt.rows.clear();
    out_error.clear();

    if (root_dir.empty()) {
        out_error = "root_dir empty";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_dir, ec);
    if (ec) {
        out_error = "failed to create root_dir";
        return false;
    }

    for (const auto& a : committed) {
        const std::filesystem::path p = std::filesystem::path(root_dir) / std::filesystem::path(a.rel_path);
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            out_error = "failed to create parent dirs";
            return false;
        }
        const std::string abs_path = p.string();
        if (!write_file_bytes(abs_path, a.payload)) {
            out_error = "failed to write file";
            return false;
        }
        EwHydrationReceiptRow row;
        row.coord_sig9_u64 = a.coord_coord9_u64;
        row.rel_path = a.rel_path;
        row.bytes_written_u32 = (uint32_t)a.payload.size();
        out_receipt.rows.push_back(row);
    }

    return true;
}
