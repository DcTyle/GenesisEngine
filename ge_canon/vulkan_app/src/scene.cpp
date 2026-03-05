#include "GE_scene_types.hpp"

EwScene& ew_scene_singleton() {
    static EwScene s;
    return s;
}
