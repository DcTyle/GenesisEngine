#include "GE_scene_api.hpp"

extern EwScene& ew_scene_singleton();

uint32_t ew_scene_add_object(const std::string& name_utf8, const std::string& path_utf8) {
    return ew_scene_singleton().add_object(name_utf8, path_utf8);
}

EwSceneObject* ew_scene_find(uint32_t id) {
    return ew_scene_singleton().find(id);
}

uint32_t ew_scene_selected() {
    return ew_scene_singleton().selected_id;
}

void ew_scene_select(uint32_t id) {
    ew_scene_singleton().selected_id = id;
}
