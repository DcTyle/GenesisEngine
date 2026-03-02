#pragma once
#include <cstdint>
#include <string>

#include "GE_scene_types.hpp"

EwScene& ew_scene_singleton();

// Convenience helpers.
uint32_t ew_scene_add_object(const std::string& name_utf8, const std::string& path_utf8);
EwSceneObject* ew_scene_find(uint32_t id);
uint32_t ew_scene_selected();
void ew_scene_select(uint32_t id);
