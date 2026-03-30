#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

struct GeMetaFrequencyVector4 {
    int32_t f_code = 0;
    uint16_t a_code = 0;
    uint16_t v_code = 0;
    uint16_t i_code = 0;
};

struct GeMetaFrequencyDftBin {
    int64_t re_q32_32 = 0;
    int64_t im_q32_32 = 0;
    uint16_t magnitude_q15 = 0;
};

struct GeMetaFrequencyTensor {
    GeMetaFrequencyVector4 channels;
    int32_t pair_gradient_i32[4][4] = {};
    GeMetaFrequencyDftBin dft[4] = {};
    uint16_t compression_ratio_q15 = 0;
    uint32_t lattice_edge_hint_u32 = 64;
    uint64_t carrier_phase_u64 = 0;
};

struct GeVirtualStateRecord {
    std::string key_utf8;
    std::string type_utf8;
    std::string metadata_utf8;
    std::vector<uint8_t> value_bytes;
    GeMetaFrequencyTensor tensor;
};

bool ge_build_meta_frequency_tensor(const std::string& type_utf8,
                                    const std::string& metadata_utf8,
                                    const std::vector<uint8_t>& value_bytes,
                                    GeMetaFrequencyTensor& out_tensor);

class VirtualStateDrive {
public:
    bool put_bytes(const std::string& key_utf8,
                   const std::string& type_utf8,
                   const std::string& metadata_utf8,
                   const uint8_t* value_bytes,
                   size_t value_size);

    bool put_text(const std::string& key_utf8,
                  const std::string& metadata_utf8,
                  const std::string& text_utf8);

    bool put_statevector(const std::string& key_utf8,
                         const std::string& metadata_utf8,
                         const std::vector<std::complex<double>>& statevector);

    bool put_i64(const std::string& key_utf8,
                 const std::string& metadata_utf8,
                 int64_t value_i64);

    bool put_u64(const std::string& key_utf8,
                 const std::string& metadata_utf8,
                 uint64_t value_u64);

    bool put_f64(const std::string& key_utf8,
                 const std::string& metadata_utf8,
                 double value_f64);

    bool get_record(const std::string& key_utf8, GeVirtualStateRecord& out_record) const;
    bool get_bytes(const std::string& key_utf8, std::vector<uint8_t>& out_value_bytes) const;
    bool get_text(const std::string& key_utf8, std::string& out_text_utf8) const;
    bool get_statevector(const std::string& key_utf8,
                         std::vector<std::complex<double>>& out_statevector) const;
    bool get_i64(const std::string& key_utf8, int64_t& out_value_i64) const;
    bool get_u64(const std::string& key_utf8, uint64_t& out_value_u64) const;
    bool get_f64(const std::string& key_utf8, double& out_value_f64) const;

    bool erase(const std::string& key_utf8);
    void clear();
    size_t size() const;

    void keys(std::vector<std::string>& out_keys_utf8) const;
    void export_records(std::vector<GeVirtualStateRecord>& out_records) const;

    bool serialize_binary(std::vector<uint8_t>& out_blob) const;
    bool deserialize_binary(const uint8_t* data, size_t size);

private:
    std::vector<GeVirtualStateRecord> records_;

    static size_t find_record_index_(const std::vector<GeVirtualStateRecord>& records,
                                     const std::string& key_utf8,
                                     bool& out_found);
};

} // namespace genesis