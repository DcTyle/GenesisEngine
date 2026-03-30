#include "GE_asset_substrate.hpp"
#include "VirtualStateDrive.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
    auto fail = [](const std::string& message) -> int {
        std::cerr << "FAIL: " << message << "\n";
        return 1;
    };

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "genesis_asset_substrate_vsd_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const std::filesystem::path project_root = root / "project";
    const std::filesystem::path cache_root = root / "cache";

    genesis::GeAssetSubstrate substrate;
    std::string err;
    if (!substrate.init(project_root.string(), cache_root.string(), "content_index.gecontent", &err)) {
        return fail(std::string("substrate init failed: ") + err);
    }

    genesis::VirtualStateDrive drive;
    if (!drive.put_text("fft/f_code", "domain=fourier\nkind=channel", "meta frequency channel")) {
        return fail("put_text failed");
    }
    if (!drive.put_i64("gradients/f_a", "domain=tensor\nkind=pair_gradient", 4096ll)) {
        return fail("put_i64 failed");
    }

    std::string saved_path;
    if (!substrate.save_virtual_state_drive(drive,
                                            "retro_fft_bundle",
                                            genesis::GeAssetPartition::Ai,
                                            true,
                                            &saved_path,
                                            &err)) {
        return fail(std::string("save_virtual_state_drive failed: ") + err);
    }

    if (!std::filesystem::exists(saved_path)) {
        return fail("saved .gevsd path does not exist");
    }

    genesis::VirtualStateDrive loaded_drive;
    if (!substrate.load_virtual_state_drive(saved_path, loaded_drive, &err)) {
        return fail(std::string("load_virtual_state_drive failed: ") + err);
    }

    std::string restored_text;
    if (!loaded_drive.get_text("fft/f_code", restored_text)) {
        return fail("loaded drive missing fft/f_code");
    }
    if (restored_text != "meta frequency channel") {
        return fail("loaded text mismatch");
    }

    std::vector<genesis::GeAssetEntry> entries;
    if (!substrate.list_project_entries(entries, &err)) {
        return fail(std::string("list_project_entries failed: ") + err);
    }

    bool found_drive = false;
    for (const genesis::GeAssetEntry& entry : entries) {
        if (entry.relpath_utf8.find("retro_fft_bundle.gevsd") != std::string::npos) {
            found_drive = true;
            if (entry.partition != genesis::GeAssetPartition::Ai) {
                return fail("drive entry partition mismatch");
            }
        }
    }
    if (!found_drive) {
        return fail(".gevsd entry was not indexed");
    }

    std::cout << "PASS: asset substrate saved and loaded virtual state drive\n";
    return 0;
}