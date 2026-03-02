#pragma once
#include <cstdint>

struct EwVec3f { float x; float y; float z; };
struct EwQuatf { float x; float y; float z; float w; };

struct EwTransform {
    EwVec3f position{0.f,0.f,0.f};
    EwQuatf rotation{0.f,0.f,0.f,1.f};
};
