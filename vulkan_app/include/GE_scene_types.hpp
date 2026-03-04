#pragma once
#include "GE_vulkan_types.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

struct EwSceneObject {
    uint32_t id = 0;
    std::string name_utf8;
    std::string source_path_utf8;
    EwTransform xform;
    // Cached fixed-point representation for render-time use (no draw-time float conversions).
    int32_t pos_q16_16[3] = {0,0,0};
    void refresh_fixed_cache() {
        // Deterministic float->fixed conversion (round to nearest).
        pos_q16_16[0] = (int32_t)llround((double)xform.position.x * 65536.0);
        pos_q16_16[1] = (int32_t)llround((double)xform.position.y * 65536.0);
        pos_q16_16[2] = (int32_t)llround((double)xform.position.z * 65536.0);
    }
};

struct EwScene {
    std::vector<EwSceneObject> objects;
    uint32_t next_id = 1;
    uint32_t selected_id = 0;

    EwSceneObject* find(uint32_t id) {
        for (auto& o : objects) if (o.id == id) return &o;
        return nullptr;
    }

    uint32_t add_object(const std::string& name_utf8, const std::string& path_utf8) {
        EwSceneObject o;
        o.id = next_id++;
        o.name_utf8 = name_utf8;
        o.source_path_utf8 = path_utf8;
        objects.push_back(o);
        if (selected_id == 0) selected_id = o.id;
        return o.id;
    }
};