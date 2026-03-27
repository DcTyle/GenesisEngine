#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "Mesh.hpp"


struct Transform {
    float pos[3] = {0,0,0};
};

struct Object {
    std::string name_utf8;
    Transform xf;
    Mesh mesh;
    uint64_t object_id_u64 = 0;
    uint32_t anchor_id_u32 = 0;
    float radius_m_f32 = 1.0f;
    float atmosphere_thickness_m_f32 = 0.0f;
    uint32_t albedo_rgba8 = 0xFFFFFFFFu;
    uint32_t atmosphere_rgba8 = 0x00000000u;
    float emissive_f32 = 0.0f;
    uint8_t pbr_scan_u8 = 0;
    uint8_t ai_training_meta_ready_u8 = 0;
    std::string material_meta_hint_utf8;
    std::vector<uint8_t> object_dna;
    int32_t pos_q16_16[3] = {0,0,0};
    int32_t rot_quat_q16_16[4] = {0,0,0,65536};
    int32_t radius_q16_16 = 0;
    int32_t atmosphere_thickness_q16_16 = 0;
    int32_t emissive_q16_16 = 0;

    Object() = default;
    Object(const Object&) = default;
    Object(Object&&) = default;
    Object& operator=(const Object&) = default;
    Object& operator=(Object&&) = default;
    bool operator==(const Object& o) const { return object_id_u64 == o.object_id_u64; }

    void refresh_fixed_cache() {
        pos_q16_16[0] = (int32_t)llround((double)xf.pos[0] * 65536.0);
        pos_q16_16[1] = (int32_t)llround((double)xf.pos[1] * 65536.0);
        pos_q16_16[2] = (int32_t)llround((double)xf.pos[2] * 65536.0);
        radius_q16_16 = (int32_t)llround((double)radius_m_f32 * 65536.0);
        atmosphere_thickness_q16_16 = (int32_t)llround((double)atmosphere_thickness_m_f32 * 65536.0);
        emissive_q16_16 = (int32_t)llround((double)emissive_f32 * 65536.0);
    }
};

std::vector<uint8_t> object_dna_seed_from_geometry(uint64_t id, const Mesh& mesh, uint32_t a, uint32_t b);
