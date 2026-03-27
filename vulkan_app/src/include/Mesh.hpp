#pragma once
#include <vector>
#include <cstdint>

struct Mesh {
    std::vector<uint32_t> indices;
    std::vector<float> vertices;
    Mesh() = default;
    Mesh(const Mesh&) = default;
    Mesh(Mesh&&) = default;
    Mesh& operator=(const Mesh&) = default;
    Mesh& operator=(Mesh&&) = default;
};

Mesh make_box_mesh(float width, float height, float depth);
Mesh make_octa_mesh(float size);
