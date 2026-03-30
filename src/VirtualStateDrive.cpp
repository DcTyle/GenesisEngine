#include "VirtualStateDrive.hpp"

#include "bytes_encoder.hpp"
#include "fixed_point.hpp"
#include "frequency_collapse.hpp"
#include "statevector_serialization.hpp"
#include "text_encoder.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace genesis {

namespace {

static uint16_t ge_clamp_u16_u64(uint64_t value_u64) {
    return (value_u64 > 65535ull) ? 65535u : (uint16_t)value_u64;
}

static uint32_t ge_clamp_u32_u64(uint64_t value_u64, uint32_t lo_u32, uint32_t hi_u32) {
    if (value_u64 < (uint64_t)lo_u32) return lo_u32;
    if (value_u64 > (uint64_t)hi_u32) return hi_u32;
    return (uint32_t)value_u64;
}

static uint64_t ge_abs_i64_u64(int64_t value_i64) {
    return (uint64_t)((value_i64 < 0) ? -value_i64 : value_i64);
}

static int64_t ge_theta_byte_turns_q(uint8_t byte_u8) {
    return ((int64_t)byte_u8 * (int64_t)TURN_SCALE) / 256;
}

static void ge_append_u32(std::vector<uint8_t>& out_blob, uint32_t value_u32) {
    out_blob.push_back((uint8_t)(value_u32 & 0xFFu));
    out_blob.push_back((uint8_t)((value_u32 >> 8) & 0xFFu));
    out_blob.push_back((uint8_t)((value_u32 >> 16) & 0xFFu));
    out_blob.push_back((uint8_t)((value_u32 >> 24) & 0xFFu));
}

static void ge_append_u64(std::vector<uint8_t>& out_blob, uint64_t value_u64) {
    for (uint32_t shift = 0u; shift < 64u; shift += 8u) {
        out_blob.push_back((uint8_t)((value_u64 >> shift) & 0xFFu));
    }
}

static bool ge_read_u32(const uint8_t*& cursor, const uint8_t* end, uint32_t& out_value_u32) {
    if ((size_t)(end - cursor) < 4u) return false;
    out_value_u32 = (uint32_t)cursor[0]
                  | ((uint32_t)cursor[1] << 8)
                  | ((uint32_t)cursor[2] << 16)
                  | ((uint32_t)cursor[3] << 24);
    cursor += 4;
    return true;
}

static bool ge_read_u64(const uint8_t*& cursor, const uint8_t* end, uint64_t& out_value_u64) {
    if ((size_t)(end - cursor) < 8u) return false;
    out_value_u64 = 0u;
    for (uint32_t shift = 0u; shift < 64u; shift += 8u) {
        out_value_u64 |= ((uint64_t)(*cursor++) << shift);
    }
    return true;
}

static bool ge_append_string_blob(std::vector<uint8_t>& out_blob, const std::string& text_utf8) {
    if (text_utf8.size() > (size_t)std::numeric_limits<uint32_t>::max()) return false;
    ge_append_u32(out_blob, (uint32_t)text_utf8.size());
    out_blob.insert(out_blob.end(), text_utf8.begin(), text_utf8.end());
    return true;
}

static bool ge_read_string_blob(const uint8_t*& cursor,
                                const uint8_t* end,
                                std::string& out_text_utf8) {
    uint32_t size_u32 = 0u;
    if (!ge_read_u32(cursor, end, size_u32)) return false;
    if ((size_t)(end - cursor) < (size_t)size_u32) return false;
    out_text_utf8.assign((const char*)cursor, (size_t)size_u32);
    cursor += size_u32;
    return true;
}

static bool ge_append_bytes_blob(std::vector<uint8_t>& out_blob, const std::vector<uint8_t>& value_bytes) {
    if (value_bytes.size() > (size_t)std::numeric_limits<uint32_t>::max()) return false;
    ge_append_u32(out_blob, (uint32_t)value_bytes.size());
    out_blob.insert(out_blob.end(), value_bytes.begin(), value_bytes.end());
    return true;
}

static bool ge_read_bytes_blob(const uint8_t*& cursor,
                               const uint8_t* end,
                               std::vector<uint8_t>& out_value_bytes) {
    uint32_t size_u32 = 0u;
    if (!ge_read_u32(cursor, end, size_u32)) return false;
    if ((size_t)(end - cursor) < (size_t)size_u32) return false;
    out_value_bytes.assign(cursor, cursor + size_u32);
    cursor += size_u32;
    return true;
}

static void ge_build_component_bins(const std::vector<uint8_t>& merged_bytes,
                                    std::vector<int64_t>& out_f_bins_turns_q,
                                    std::vector<int64_t>& out_a_bins_q32_32,
                                    std::vector<int64_t>& out_phi_bins_turns_q,
                                    uint64_t& out_phase_delta_sum_turns_q,
                                    uint64_t& out_info_delta_sum_u64,
                                    uint64_t& out_transition_energy_u64) {
    out_f_bins_turns_q.clear();
    out_a_bins_q32_32.clear();
    out_phi_bins_turns_q.clear();
    out_phase_delta_sum_turns_q = 0u;
    out_info_delta_sum_u64 = 0u;
    out_transition_energy_u64 = 0u;
    if (merged_bytes.empty()) return;

    out_f_bins_turns_q.reserve(merged_bytes.size());
    out_a_bins_q32_32.reserve(merged_bytes.size());
    out_phi_bins_turns_q.reserve(merged_bytes.size());

    int64_t prev_theta_turns_q = ge_theta_byte_turns_q(merged_bytes[0]);
    int64_t accum_turns_q = prev_theta_turns_q;
    uint8_t prev_byte_u8 = merged_bytes[0];

    for (size_t i = 0; i < merged_bytes.size(); ++i) {
        const uint8_t byte_u8 = merged_bytes[i];
        const int64_t theta_turns_q = ge_theta_byte_turns_q(byte_u8);
        const int64_t delta_turns_q = delta_turns(prev_theta_turns_q, theta_turns_q);
        accum_turns_q = wrap_turns(accum_turns_q + delta_turns_q);
        prev_theta_turns_q = theta_turns_q;

        out_f_bins_turns_q.push_back(accum_turns_q);
        out_a_bins_q32_32.push_back((((int64_t)byte_u8 + 1ll) << 32) / 256ll);
        out_phi_bins_turns_q.push_back(theta_turns_q);

        const uint64_t abs_delta_turns_q = ge_abs_i64_u64(delta_turns_q);
        out_phase_delta_sum_turns_q += abs_delta_turns_q;
        out_transition_energy_u64 += (uint64_t)byte_u8 * (uint64_t)(byte_u8 + 1u);

        if (i > 0u) {
            const int32_t diff_i32 = (int32_t)byte_u8 - (int32_t)prev_byte_u8;
            out_info_delta_sum_u64 += (uint64_t)((diff_i32 < 0) ? -diff_i32 : diff_i32);
        }
        prev_byte_u8 = byte_u8;
    }
}

static uint16_t ge_channel_to_q15(int64_t channel_q32_32) {
    uint64_t abs_q32_32 = ge_abs_i64_u64(channel_q32_32);
    if (abs_q32_32 > (uint64_t)(1ull << 32)) abs_q32_32 = (uint64_t)(1ull << 32);
    return ge_clamp_u16_u64((abs_q32_32 * 65535ull) >> 32);
}

static uint16_t ge_mean_phase_delta_to_q15(uint64_t phase_delta_sum_turns_q, size_t sample_count) {
    if (sample_count == 0u) return 0u;
    const uint64_t mean_turns_q = phase_delta_sum_turns_q / (uint64_t)sample_count;
    int64_t mean_q32_32 = turns_q_to_q32_32((int64_t)mean_turns_q);
    if (mean_q32_32 < 0) mean_q32_32 = -mean_q32_32;
    if (mean_q32_32 > (1ll << 32)) mean_q32_32 = (1ll << 32);
    return ge_clamp_u16_u64(((uint64_t)mean_q32_32 * 65535ull) >> 32);
}

static uint16_t ge_information_delta_to_q15(uint64_t info_delta_sum_u64, size_t transition_count) {
    if (transition_count == 0u) return 0u;
    const uint64_t mean_delta_u64 = info_delta_sum_u64 / (uint64_t)transition_count;
    const uint64_t scaled_u64 = (mean_delta_u64 * 65535ull) / 255ull;
    return ge_clamp_u16_u64(scaled_u64);
}

static int64_t ge_q32_32_from_channel_i32(int32_t value_i32, int32_t scale_i32) {
    if (scale_i32 <= 0) return 0;
    return (((int64_t)value_i32) << 32) / (int64_t)scale_i32;
}

static void ge_build_dft(const GeMetaFrequencyVector4& channels,
                         GeMetaFrequencyDftBin out_dft[4]) {
    const int32_t abs_f_i32 = (channels.f_code < 0) ? -channels.f_code : channels.f_code;
    int32_t scale_i32 = abs_f_i32;
    if ((int32_t)channels.a_code > scale_i32) scale_i32 = (int32_t)channels.a_code;
    if ((int32_t)channels.v_code > scale_i32) scale_i32 = (int32_t)channels.v_code;
    if ((int32_t)channels.i_code > scale_i32) scale_i32 = (int32_t)channels.i_code;
    if (scale_i32 <= 0) scale_i32 = 1;

    const int64_t x0_q32_32 = ge_q32_32_from_channel_i32(channels.f_code, scale_i32);
    const int64_t x1_q32_32 = ge_q32_32_from_channel_i32((int32_t)channels.a_code, scale_i32);
    const int64_t x2_q32_32 = ge_q32_32_from_channel_i32((int32_t)channels.v_code, scale_i32);
    const int64_t x3_q32_32 = ge_q32_32_from_channel_i32((int32_t)channels.i_code, scale_i32);

    out_dft[0].re_q32_32 = x0_q32_32 + x1_q32_32 + x2_q32_32 + x3_q32_32;
    out_dft[0].im_q32_32 = 0;
    out_dft[1].re_q32_32 = x0_q32_32 - x2_q32_32;
    out_dft[1].im_q32_32 = x3_q32_32 - x1_q32_32;
    out_dft[2].re_q32_32 = x0_q32_32 - x1_q32_32 + x2_q32_32 - x3_q32_32;
    out_dft[2].im_q32_32 = 0;
    out_dft[3].re_q32_32 = x0_q32_32 - x2_q32_32;
    out_dft[3].im_q32_32 = x1_q32_32 - x3_q32_32;

    for (uint32_t i = 0u; i < 4u; ++i) {
        const uint64_t mag_l1_q32_32 = ge_abs_i64_u64(out_dft[i].re_q32_32) + ge_abs_i64_u64(out_dft[i].im_q32_32);
        uint64_t mag_q15_u64 = (mag_l1_q32_32 > (uint64_t)(4ull << 32))
            ? 65535ull
            : ((mag_l1_q32_32 * 65535ull) / (uint64_t)(4ull << 32));
        out_dft[i].magnitude_q15 = ge_clamp_u16_u64(mag_q15_u64);
    }
}

static uint16_t ge_build_compression_ratio_q15(size_t payload_size, size_t merged_size) {
    const uint64_t raw_bytes_u64 = (payload_size == 0u) ? 1ull : (uint64_t)payload_size;
    const uint64_t collapsed_bytes_u64 = (merged_size == 0u) ? 1ull : std::min<uint64_t>((uint64_t)merged_size, 24ull);
    uint64_t ratio_q15_u64 = (collapsed_bytes_u64 << 15) / raw_bytes_u64;
    if (ratio_q15_u64 > 65535ull) ratio_q15_u64 = 65535ull;
    return ge_clamp_u16_u64(ratio_q15_u64);
}

static uint16_t ge_build_gradient_energy_q15(const int32_t pair_gradient_i32[4][4], int32_t scale_i32) {
    if (scale_i32 <= 0) scale_i32 = 1;
    uint64_t acc_u64 = 0u;
    uint32_t count_u32 = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t j = i + 1u; j < 4u; ++j) {
            acc_u64 += (uint64_t)((pair_gradient_i32[i][j] < 0) ? -pair_gradient_i32[i][j] : pair_gradient_i32[i][j]);
            ++count_u32;
        }
    }
    if (count_u32 == 0u) return 0u;
    uint64_t mean_grad_u64 = acc_u64 / (uint64_t)count_u32;
    uint64_t grad_q15_u64 = (mean_grad_u64 * 65535ull) / (uint64_t)scale_i32;
    return ge_clamp_u16_u64(grad_q15_u64);
}

static uint32_t ge_build_lattice_edge_hint(const GeMetaFrequencyTensor& tensor,
                                           size_t payload_size) {
    uint32_t richness_q15 = (uint32_t)tensor.dft[1].magnitude_q15
                          + (uint32_t)tensor.dft[2].magnitude_q15
                          + (uint32_t)tensor.dft[3].magnitude_q15;
    richness_q15 /= 3u;

    const uint64_t raw_bytes_u64 = (payload_size == 0u) ? 1ull : (uint64_t)payload_size;
    uint64_t compression_gain_q15 = (raw_bytes_u64 << 15) / 24ull;
    if (compression_gain_q15 > 65535ull) compression_gain_q15 = 65535ull;

    const int32_t abs_f_i32 = (tensor.channels.f_code < 0) ? -tensor.channels.f_code : tensor.channels.f_code;
    int32_t scale_i32 = abs_f_i32;
    if ((int32_t)tensor.channels.a_code > scale_i32) scale_i32 = (int32_t)tensor.channels.a_code;
    if ((int32_t)tensor.channels.v_code > scale_i32) scale_i32 = (int32_t)tensor.channels.v_code;
    if ((int32_t)tensor.channels.i_code > scale_i32) scale_i32 = (int32_t)tensor.channels.i_code;
    const uint16_t gradient_q15 = ge_build_gradient_energy_q15(tensor.pair_gradient_i32, scale_i32);

    uint64_t edge_u64 = 64ull;
    edge_u64 += ((uint64_t)richness_q15 * 128ull) / 65535ull;
    edge_u64 += ((uint64_t)gradient_q15 * 128ull) / 65535ull;
    edge_u64 += (compression_gain_q15 * 192ull) / 65535ull;
    if (edge_u64 > 512ull) edge_u64 = 512ull;
    uint32_t edge_u32 = ge_clamp_u32_u64(edge_u64, 64u, 512u);
    edge_u32 = (edge_u32 + 15u) & ~15u;
    return ge_clamp_u32_u64(edge_u32, 64u, 512u);
}

static void ge_build_record_tensor(const std::string& type_utf8,
                                   const std::string& metadata_utf8,
                                   const std::vector<uint8_t>& value_bytes,
                                   GeMetaFrequencyTensor& out_tensor) {
    out_tensor = GeMetaFrequencyTensor{};

    const std::string normalized_type_utf8 = normalize_text(type_utf8);
    const std::string normalized_metadata_utf8 = normalize_text(metadata_utf8);
    const std::string merged_meta_utf8 = normalized_type_utf8 + "\n" + normalized_metadata_utf8;

    std::vector<uint8_t> merged_bytes;
    merged_bytes.reserve(merged_meta_utf8.size() + 1u + value_bytes.size());
    merged_bytes.insert(merged_bytes.end(), merged_meta_utf8.begin(), merged_meta_utf8.end());
    merged_bytes.push_back(0u);
    merged_bytes.insert(merged_bytes.end(), value_bytes.begin(), value_bytes.end());

    out_tensor.channels.f_code = ew_bytes_to_frequency_code(merged_bytes.data(), merged_bytes.size(), 0u);

    std::vector<int64_t> f_bins_turns_q;
    std::vector<int64_t> a_bins_q32_32;
    std::vector<int64_t> phi_bins_turns_q;
    uint64_t phase_delta_sum_turns_q = 0u;
    uint64_t info_delta_sum_u64 = 0u;
    uint64_t transition_energy_u64 = 0u;
    ge_build_component_bins(merged_bytes,
                            f_bins_turns_q,
                            a_bins_q32_32,
                            phi_bins_turns_q,
                            phase_delta_sum_turns_q,
                            info_delta_sum_u64,
                            transition_energy_u64);

    EwCarrierParams carrier{};
    if (!f_bins_turns_q.empty()) {
        carrier = carrier_params(f_bins_turns_q, a_bins_q32_32, phi_bins_turns_q);
    }
    const int64_t carrier_amp_mean_q32_32 = (carrier.component_count_u32 == 0u)
        ? 0ll
        : (carrier.A_carrier_q32_32 / (int64_t)carrier.component_count_u32);

    out_tensor.channels.a_code = ge_channel_to_q15(carrier_amp_mean_q32_32);
    out_tensor.channels.v_code = ge_mean_phase_delta_to_q15(phase_delta_sum_turns_q, merged_bytes.size());
    out_tensor.channels.i_code = ge_information_delta_to_q15(info_delta_sum_u64,
                                                             (merged_bytes.size() > 1u) ? (merged_bytes.size() - 1u) : 1u);
    if (out_tensor.channels.i_code < ge_clamp_u16_u64(transition_energy_u64 / (merged_bytes.empty() ? 1u : (uint64_t)merged_bytes.size()) / 8u)) {
        out_tensor.channels.i_code = ge_clamp_u16_u64(transition_energy_u64 / (merged_bytes.empty() ? 1u : (uint64_t)merged_bytes.size()) / 8u);
    }
    out_tensor.carrier_phase_u64 = carrier_phase_u64(carrier.phi_carrier_turns_q32_32);

    const int32_t values_i32[4] = {
        out_tensor.channels.f_code,
        (int32_t)out_tensor.channels.a_code,
        (int32_t)out_tensor.channels.v_code,
        (int32_t)out_tensor.channels.i_code,
    };
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t j = 0u; j < 4u; ++j) {
            out_tensor.pair_gradient_i32[i][j] = values_i32[j] - values_i32[i];
        }
    }

    ge_build_dft(out_tensor.channels, out_tensor.dft);
    out_tensor.compression_ratio_q15 = ge_build_compression_ratio_q15(value_bytes.size(), merged_bytes.size());
    out_tensor.lattice_edge_hint_u32 = ge_build_lattice_edge_hint(out_tensor, value_bytes.size());
}

static std::string ge_f64_to_hex_ascii(double value_f64) {
    std::ostringstream oss;
    oss << std::hexfloat << value_f64;
    return oss.str();
}

static bool ge_parse_f64_hex_ascii(const std::string& text_utf8, double& out_value_f64) {
    std::istringstream iss(text_utf8);
    iss >> std::hexfloat >> out_value_f64;
    return !iss.fail();
}

} // namespace

bool ge_build_meta_frequency_tensor(const std::string& type_utf8,
                                    const std::string& metadata_utf8,
                                    const std::vector<uint8_t>& value_bytes,
                                    GeMetaFrequencyTensor& out_tensor) {
    ge_build_record_tensor(type_utf8, metadata_utf8, value_bytes, out_tensor);
    return true;
}

size_t VirtualStateDrive::find_record_index_(const std::vector<GeVirtualStateRecord>& records,
                                             const std::string& key_utf8,
                                             bool& out_found) {
    const auto it = std::lower_bound(records.begin(), records.end(), key_utf8,
        [](const GeVirtualStateRecord& record, const std::string& key) {
            return record.key_utf8 < key;
        });
    out_found = (it != records.end() && it->key_utf8 == key_utf8);
    return (size_t)(it - records.begin());
}

bool VirtualStateDrive::put_bytes(const std::string& key_utf8,
                                  const std::string& type_utf8,
                                  const std::string& metadata_utf8,
                                  const uint8_t* value_bytes,
                                  size_t value_size) {
    if (key_utf8.empty()) return false;
    std::vector<uint8_t> data;
    if (value_bytes && value_size != 0u) {
        data.assign(value_bytes, value_bytes + value_size);
    }

    GeVirtualStateRecord record{};
    record.key_utf8 = key_utf8;
    record.type_utf8 = normalize_text(type_utf8);
    record.metadata_utf8 = normalize_text(metadata_utf8);
    record.value_bytes = data;
    ge_build_record_tensor(record.type_utf8, record.metadata_utf8, record.value_bytes, record.tensor);

    bool found = false;
    const size_t index = find_record_index_(records_, key_utf8, found);
    if (found) {
        records_[index] = record;
    } else {
        records_.insert(records_.begin() + (ptrdiff_t)index, record);
    }
    return true;
}

bool VirtualStateDrive::put_text(const std::string& key_utf8,
                                 const std::string& metadata_utf8,
                                 const std::string& text_utf8) {
    return put_bytes(key_utf8,
                     "text_utf8",
                     metadata_utf8,
                     (const uint8_t*)text_utf8.data(),
                     text_utf8.size());
}

bool VirtualStateDrive::put_statevector(const std::string& key_utf8,
                                        const std::string& metadata_utf8,
                                        const std::vector<std::complex<double>>& statevector) {
    const std::string blob = serialize_statevector(statevector);
    return put_bytes(key_utf8,
                     "statevector",
                     metadata_utf8,
                     (const uint8_t*)blob.data(),
                     blob.size());
}

bool VirtualStateDrive::put_i64(const std::string& key_utf8,
                                const std::string& metadata_utf8,
                                int64_t value_i64) {
    uint8_t raw[sizeof(value_i64)] = {};
    std::memcpy(raw, &value_i64, sizeof(value_i64));
    return put_bytes(key_utf8, "int64", metadata_utf8, raw, sizeof(raw));
}

bool VirtualStateDrive::put_u64(const std::string& key_utf8,
                                const std::string& metadata_utf8,
                                uint64_t value_u64) {
    uint8_t raw[sizeof(value_u64)] = {};
    std::memcpy(raw, &value_u64, sizeof(value_u64));
    return put_bytes(key_utf8, "uint64", metadata_utf8, raw, sizeof(raw));
}

bool VirtualStateDrive::put_f64(const std::string& key_utf8,
                                const std::string& metadata_utf8,
                                double value_f64) {
    const std::string hex_utf8 = ge_f64_to_hex_ascii(value_f64);
    return put_bytes(key_utf8,
                     "float64_hex",
                     metadata_utf8,
                     (const uint8_t*)hex_utf8.data(),
                     hex_utf8.size());
}

bool VirtualStateDrive::get_record(const std::string& key_utf8, GeVirtualStateRecord& out_record) const {
    bool found = false;
    const size_t index = find_record_index_(records_, key_utf8, found);
    if (!found) return false;
    out_record = records_[index];
    return true;
}

bool VirtualStateDrive::get_bytes(const std::string& key_utf8, std::vector<uint8_t>& out_value_bytes) const {
    GeVirtualStateRecord record{};
    if (!get_record(key_utf8, record)) return false;
    out_value_bytes = record.value_bytes;
    return true;
}

bool VirtualStateDrive::get_text(const std::string& key_utf8, std::string& out_text_utf8) const {
    GeVirtualStateRecord record{};
    if (!get_record(key_utf8, record)) return false;
    out_text_utf8.assign((const char*)record.value_bytes.data(), record.value_bytes.size());
    return true;
}

bool VirtualStateDrive::get_statevector(const std::string& key_utf8,
                                        std::vector<std::complex<double>>& out_statevector) const {
    std::string blob;
    if (!get_text(key_utf8, blob)) return false;
    out_statevector = deserialize_statevector(blob);
    return true;
}

bool VirtualStateDrive::get_i64(const std::string& key_utf8, int64_t& out_value_i64) const {
    GeVirtualStateRecord record{};
    if (!get_record(key_utf8, record)) return false;
    if (record.value_bytes.size() != sizeof(out_value_i64)) return false;
    std::memcpy(&out_value_i64, record.value_bytes.data(), sizeof(out_value_i64));
    return true;
}

bool VirtualStateDrive::get_u64(const std::string& key_utf8, uint64_t& out_value_u64) const {
    GeVirtualStateRecord record{};
    if (!get_record(key_utf8, record)) return false;
    if (record.value_bytes.size() != sizeof(out_value_u64)) return false;
    std::memcpy(&out_value_u64, record.value_bytes.data(), sizeof(out_value_u64));
    return true;
}

bool VirtualStateDrive::get_f64(const std::string& key_utf8, double& out_value_f64) const {
    std::string hex_utf8;
    if (!get_text(key_utf8, hex_utf8)) return false;
    return ge_parse_f64_hex_ascii(hex_utf8, out_value_f64);
}

bool VirtualStateDrive::erase(const std::string& key_utf8) {
    bool found = false;
    const size_t index = find_record_index_(records_, key_utf8, found);
    if (!found) return false;
    records_.erase(records_.begin() + (ptrdiff_t)index);
    return true;
}

void VirtualStateDrive::clear() {
    records_.clear();
}

size_t VirtualStateDrive::size() const {
    return records_.size();
}

void VirtualStateDrive::keys(std::vector<std::string>& out_keys_utf8) const {
    out_keys_utf8.clear();
    out_keys_utf8.reserve(records_.size());
    for (const GeVirtualStateRecord& record : records_) {
        out_keys_utf8.push_back(record.key_utf8);
    }
}

void VirtualStateDrive::export_records(std::vector<GeVirtualStateRecord>& out_records) const {
    out_records = records_;
}

bool VirtualStateDrive::serialize_binary(std::vector<uint8_t>& out_blob) const {
    out_blob.clear();
    ge_append_u32(out_blob, 0x31545356u);
    ge_append_u32(out_blob, 1u);
    if (records_.size() > (size_t)std::numeric_limits<uint32_t>::max()) return false;
    ge_append_u32(out_blob, (uint32_t)records_.size());

    for (const GeVirtualStateRecord& record : records_) {
        if (!ge_append_string_blob(out_blob, record.key_utf8)) return false;
        if (!ge_append_string_blob(out_blob, record.type_utf8)) return false;
        if (!ge_append_string_blob(out_blob, record.metadata_utf8)) return false;
        if (!ge_append_bytes_blob(out_blob, record.value_bytes)) return false;
    }
    return true;
}

bool VirtualStateDrive::deserialize_binary(const uint8_t* data, size_t size) {
    records_.clear();
    if (!data || size < 12u) return false;

    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    uint32_t magic_u32 = 0u;
    uint32_t version_u32 = 0u;
    uint32_t count_u32 = 0u;
    if (!ge_read_u32(cursor, end, magic_u32)) return false;
    if (!ge_read_u32(cursor, end, version_u32)) return false;
    if (!ge_read_u32(cursor, end, count_u32)) return false;
    if (magic_u32 != 0x31545356u || version_u32 != 1u) return false;

    records_.reserve(count_u32);
    for (uint32_t i = 0u; i < count_u32; ++i) {
        GeVirtualStateRecord record{};
        if (!ge_read_string_blob(cursor, end, record.key_utf8)) return false;
        if (!ge_read_string_blob(cursor, end, record.type_utf8)) return false;
        if (!ge_read_string_blob(cursor, end, record.metadata_utf8)) return false;
        if (!ge_read_bytes_blob(cursor, end, record.value_bytes)) return false;
        ge_build_record_tensor(record.type_utf8, record.metadata_utf8, record.value_bytes, record.tensor);
        records_.push_back(record);
    }

    std::sort(records_.begin(), records_.end(), [](const GeVirtualStateRecord& a, const GeVirtualStateRecord& b) {
        return a.key_utf8 < b.key_utf8;
    });
    return true;
}

} // namespace genesis
