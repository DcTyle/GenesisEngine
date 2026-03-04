#pragma once

#include <cstdint>
#include <string>
#include <filesystem>

namespace genesis {

// Crash-safe, deterministic transactional file write.
// Writes to <final>.tmp, swaps <final> to <final>.old, renames tmp to final, and cleans up.
// Uses a small journal <final>.jnl so recovery can finalize/rollback deterministically.
bool ew_txn_write_file_bytes(const std::filesystem::path& final_path,
                            const uint8_t* bytes,
                            size_t byte_count,
                            uint64_t tick_u64,
                            std::string* out_err);

bool ew_txn_write_file_text(const std::filesystem::path& final_path,
                           const std::string& utf8_text,
                           uint64_t tick_u64,
                           std::string* out_err);

// Recovery for file transactions (final + .tmp/.old/.jnl).
// Safe to call unconditionally before reads.
bool ew_txn_recover_file(const std::filesystem::path& final_path);

// Transactional directory commit:
// - tmp_dir is assumed to contain a complete new directory tree
// - commit swaps final_dir -> .old, tmp_dir -> final_dir, then removes .old
// - uses a journal at <final_dir>.jnl and supports deterministic recovery
bool ew_txn_commit_dir(const std::filesystem::path& final_dir,
                       const std::filesystem::path& tmp_dir,
                       uint64_t tick_u64,
                       std::string* out_err);

bool ew_txn_recover_dir(const std::filesystem::path& final_dir);

}
