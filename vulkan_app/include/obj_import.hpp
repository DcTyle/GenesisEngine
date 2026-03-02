#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct EwObjMesh {
    struct Vtx { float px,py,pz; float nx,ny,nz; float u,v; };
    std::vector<Vtx> vertices;
    std::vector<uint32_t> indices;
};

// Deterministic, minimal OBJ importer (triangulates polygons by fan).
// Supports: v, vt, vn, f.
// Ignores materials. UTF-8 file path is accepted, file content is treated as UTF-8.
bool ew_obj_load_utf8(const std::string& path_utf8, EwObjMesh& out);

// Returns a short, stable mesh name from file path.
std::string ew_obj_basename_utf8(const std::string& path_utf8);
