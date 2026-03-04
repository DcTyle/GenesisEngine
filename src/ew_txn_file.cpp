#include "ew_txn_file.hpp"

#include <fstream>

namespace genesis {

enum EwTxnPhase : uint32_t {
    EW_TXN_BEGIN = 0u,
    EW_TXN_TEMP_WRITTEN = 1u,
    EW_TXN_SWAPPED = 2u,
    EW_TXN_DONE = 3u
};

struct EwTxnJournalV1 {
    uint32_t magic = 0x4E4A4547U; // 'GEJN'
    uint32_t ver = 1u;
    uint32_t phase = 0u;
    uint32_t reserved0 = 0u;
    uint64_t tick_u64 = 0u;
};

static inline void ew_txn_try_remove_path(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) (void)std::filesystem::remove(p, ec);
}

static inline bool ew_txn_write_journal_atomically_(const std::filesystem::path& jnl_path, EwTxnPhase phase, uint64_t tick_u64) {
    EwTxnJournalV1 j{};
    j.phase = (uint32_t)phase;
    j.tick_u64 = tick_u64;

    const std::filesystem::path tmp = jnl_path.u8string() + std::string(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) return false;
        out.write(reinterpret_cast<const char*>(&j), (std::streamsize)sizeof(j));
        out.flush();
        if (!out.good()) return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(jnl_path, ec)) {
        (void)std::filesystem::remove(jnl_path, ec);
        ec.clear();
    }
    std::filesystem::rename(tmp, jnl_path, ec);
    if (ec) return false;
    return true;
}

static inline bool ew_txn_read_journal_(const std::filesystem::path& jnl_path, EwTxnJournalV1& out_j) {
    std::ifstream in(jnl_path, std::ios::binary);
    if (!in.good()) return false;
    in.read(reinterpret_cast<char*>(&out_j), (std::streamsize)sizeof(out_j));
    if (!in.good()) return false;
    if (out_j.magic != 0x4E4A4547U || out_j.ver != 1u) return false;
    if (out_j.phase > 3u) return false;
    return true;
}

bool ew_txn_recover_file(const std::filesystem::path& final_path) {
    const std::filesystem::path jnl = final_path.u8string() + std::string(".jnl");
    const std::filesystem::path tmp = final_path.u8string() + std::string(".tmp");
    const std::filesystem::path old = final_path.u8string() + std::string(".old");

    EwTxnJournalV1 j{};
    if (!ew_txn_read_journal_(jnl, j)) return false;

    std::error_code ec;
    const bool has_final = std::filesystem::exists(final_path, ec);
    ec.clear();
    const bool has_tmp = std::filesystem::exists(tmp, ec);
    ec.clear();
    const bool has_old = std::filesystem::exists(old, ec);
    ec.clear();

    const EwTxnPhase phase = (EwTxnPhase)j.phase;

    if (phase == EW_TXN_BEGIN) {
        if (has_tmp) ew_txn_try_remove_path(tmp);
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_TEMP_WRITTEN) {
        if (!has_tmp) {
            ew_txn_try_remove_path(jnl);
            return true;
        }
        if (has_final && !has_old) {
            std::filesystem::rename(final_path, old, ec);
            ec.clear();
        }
        if (!std::filesystem::exists(final_path, ec)) {
            ec.clear();
            std::filesystem::rename(tmp, final_path, ec);
            if (!ec) {
                ew_txn_try_remove_path(old);
                ew_txn_try_remove_path(jnl);
                return true;
            }
        }
        if (!std::filesystem::exists(final_path, ec) && has_old) {
            ec.clear();
            std::filesystem::rename(old, final_path, ec);
        }
        ew_txn_try_remove_path(tmp);
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_SWAPPED) {
        if (has_tmp) {
            if (has_final) ew_txn_try_remove_path(final_path);
            std::filesystem::rename(tmp, final_path, ec);
            if (!ec) {
                ew_txn_try_remove_path(old);
                ew_txn_try_remove_path(jnl);
                return true;
            }
        }
        if (!std::filesystem::exists(final_path, ec) && has_old) {
            ec.clear();
            std::filesystem::rename(old, final_path, ec);
        }
        ew_txn_try_remove_path(tmp);
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_DONE) {
        ew_txn_try_remove_path(tmp);
        ew_txn_try_remove_path(old);
        ew_txn_try_remove_path(jnl);
        return true;
    }

    return true;
}

bool ew_txn_write_file_bytes(const std::filesystem::path& final_path,
                            const uint8_t* bytes,
                            size_t byte_count,
                            uint64_t tick_u64,
                            std::string* out_err) {
    if (!bytes && byte_count != 0) {
        if (out_err) *out_err = "null_bytes";
        return false;
    }

    // Recover any incomplete prior transaction.
    (void)ew_txn_recover_file(final_path);

    const std::filesystem::path jnl = final_path.u8string() + std::string(".jnl");
    const std::filesystem::path tmp = final_path.u8string() + std::string(".tmp");
    const std::filesystem::path old = final_path.u8string() + std::string(".old");

    std::error_code ec;
    std::filesystem::create_directories(final_path.parent_path(), ec);
    ec.clear();

    if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_BEGIN, tick_u64)) {
        if (out_err) *out_err = "journal_begin_fail";
        return false;
    }

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            if (out_err) *out_err = "tmp_open_fail";
            return false;
        }
        if (byte_count) out.write(reinterpret_cast<const char*>(bytes), (std::streamsize)byte_count);
        out.flush();
        if (!out.good()) {
            if (out_err) *out_err = "tmp_write_fail";
            return false;
        }
    }

    if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_TEMP_WRITTEN, tick_u64)) {
        if (out_err) *out_err = "journal_temp_fail";
        return false;
    }

    const bool has_final = std::filesystem::exists(final_path, ec);
    ec.clear();
    if (has_final) {
        if (std::filesystem::exists(old, ec)) {
            (void)std::filesystem::remove(old, ec);
        }
        ec.clear();
        std::filesystem::rename(final_path, old, ec);
        ec.clear();
        if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_SWAPPED, tick_u64)) {
            if (out_err) *out_err = "journal_swap_fail";
            return false;
        }
    }

    if (std::filesystem::exists(final_path, ec)) {
        (void)std::filesystem::remove(final_path, ec);
        ec.clear();
    }

    std::filesystem::rename(tmp, final_path, ec);
    if (ec) {
        // Roll back if possible.
        ec.clear();
        if (!std::filesystem::exists(final_path, ec) && has_final && std::filesystem::exists(old, ec)) {
            ec.clear();
            std::filesystem::rename(old, final_path, ec);
        }
        ew_txn_try_remove_path(tmp);
        if (out_err) *out_err = "rename_tmp_to_final_fail";
        return false;
    }

    (void)ew_txn_write_journal_atomically_(jnl, EW_TXN_DONE, tick_u64);
    ew_txn_try_remove_path(old);
    ew_txn_try_remove_path(jnl);
    return true;
}

bool ew_txn_write_file_text(const std::filesystem::path& final_path,
                           const std::string& utf8_text,
                           uint64_t tick_u64,
                           std::string* out_err) {
    return ew_txn_write_file_bytes(final_path, reinterpret_cast<const uint8_t*>(utf8_text.data()), utf8_text.size(), tick_u64, out_err);
}

bool ew_txn_recover_dir(const std::filesystem::path& final_dir) {
    const std::filesystem::path jnl = final_dir.u8string() + std::string(".jnl");
    const std::filesystem::path tmp = final_dir.u8string() + std::string(".tmp");
    const std::filesystem::path old = final_dir.u8string() + std::string(".old");

    EwTxnJournalV1 j{};
    if (!ew_txn_read_journal_(jnl, j)) return false;

    std::error_code ec;
    const bool has_final = std::filesystem::exists(final_dir, ec);
    ec.clear();
    const bool has_tmp = std::filesystem::exists(tmp, ec);
    ec.clear();
    const bool has_old = std::filesystem::exists(old, ec);
    ec.clear();

    const EwTxnPhase phase = (EwTxnPhase)j.phase;

    if (phase == EW_TXN_BEGIN) {
        if (has_tmp) {
            std::filesystem::remove_all(tmp, ec);
            ec.clear();
        }
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_TEMP_WRITTEN) {
        if (!has_tmp) {
            ew_txn_try_remove_path(jnl);
            return true;
        }
        if (has_final && !has_old) {
            std::filesystem::rename(final_dir, old, ec);
            ec.clear();
        }
        if (!std::filesystem::exists(final_dir, ec)) {
            ec.clear();
            std::filesystem::rename(tmp, final_dir, ec);
            if (!ec) {
                if (std::filesystem::exists(old, ec)) { ec.clear(); std::filesystem::remove_all(old, ec); }
                ew_txn_try_remove_path(jnl);
                return true;
            }
        }
        if (!std::filesystem::exists(final_dir, ec) && has_old) {
            ec.clear();
            std::filesystem::rename(old, final_dir, ec);
        }
        if (std::filesystem::exists(tmp, ec)) { ec.clear(); std::filesystem::remove_all(tmp, ec); }
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_SWAPPED) {
        if (has_tmp) {
            if (has_final) {
                std::filesystem::remove_all(final_dir, ec);
                ec.clear();
            }
            std::filesystem::rename(tmp, final_dir, ec);
            if (!ec) {
                if (std::filesystem::exists(old, ec)) { ec.clear(); std::filesystem::remove_all(old, ec); }
                ew_txn_try_remove_path(jnl);
                return true;
            }
        }
        if (!std::filesystem::exists(final_dir, ec) && has_old) {
            ec.clear();
            std::filesystem::rename(old, final_dir, ec);
        }
        if (std::filesystem::exists(tmp, ec)) { ec.clear(); std::filesystem::remove_all(tmp, ec); }
        ew_txn_try_remove_path(jnl);
        return true;
    }

    if (phase == EW_TXN_DONE) {
        if (has_tmp) { std::filesystem::remove_all(tmp, ec); ec.clear(); }
        if (has_old) { std::filesystem::remove_all(old, ec); ec.clear(); }
        ew_txn_try_remove_path(jnl);
        return true;
    }

    return true;
}

bool ew_txn_commit_dir(const std::filesystem::path& final_dir,
                       const std::filesystem::path& tmp_dir,
                       uint64_t tick_u64,
                       std::string* out_err) {
    // Recover any incomplete prior transaction for this directory.
    (void)ew_txn_recover_dir(final_dir);

    std::error_code ec;
    const std::filesystem::path jnl = final_dir.u8string() + std::string(".jnl");
    const std::filesystem::path old = final_dir.u8string() + std::string(".old");

    if (!std::filesystem::exists(tmp_dir, ec) || !std::filesystem::is_directory(tmp_dir, ec)) {
        if (out_err) *out_err = "tmp_dir_missing";
        return false;
    }
    ec.clear();
    std::filesystem::create_directories(final_dir.parent_path(), ec);
    ec.clear();

    if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_BEGIN, tick_u64)) {
        if (out_err) *out_err = "journal_begin_fail";
        return false;
    }

    if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_TEMP_WRITTEN, tick_u64)) {
        if (out_err) *out_err = "journal_temp_fail";
        return false;
    }

    const bool has_final = std::filesystem::exists(final_dir, ec);
    ec.clear();
    if (has_final) {
        if (std::filesystem::exists(old, ec)) {
            std::filesystem::remove_all(old, ec);
            ec.clear();
        }
        std::filesystem::rename(final_dir, old, ec);
        ec.clear();
        if (!ew_txn_write_journal_atomically_(jnl, EW_TXN_SWAPPED, tick_u64)) {
            if (out_err) *out_err = "journal_swap_fail";
            return false;
        }
    }

    if (std::filesystem::exists(final_dir, ec)) {
        std::filesystem::remove_all(final_dir, ec);
        ec.clear();
    }

    std::filesystem::rename(tmp_dir, final_dir, ec);
    if (ec) {
        // rollback
        ec.clear();
        if (!std::filesystem::exists(final_dir, ec) && has_final && std::filesystem::exists(old, ec)) {
            ec.clear();
            std::filesystem::rename(old, final_dir, ec);
        }
        if (out_err) *out_err = "rename_tmp_to_final_fail";
        return false;
    }

    (void)ew_txn_write_journal_atomically_(jnl, EW_TXN_DONE, tick_u64);
    if (std::filesystem::exists(old, ec)) { ec.clear(); std::filesystem::remove_all(old, ec); }
    ew_txn_try_remove_path(jnl);
    return true;
}

}
