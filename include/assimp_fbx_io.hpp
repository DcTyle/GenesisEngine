#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ewmesh_voxelizer.hpp"

class SubstrateManager;

// Assimp-based FBX/glTF import/export utilities.
// Determinism rules:
// - Import uses fixed postprocess flags and stable iteration order.
// - All positions are quantized to Q32.32 then converted back to float (no drift).
// - Texture extraction uses deterministic names based on material/slot indices (no hashing).
//
// NOTE: These functions require Assimp to be available at build time.

namespace genesis {


// Load an FBX (or any Assimp-supported) file and convert it to a single EWM1 mesh.
// Meshes are concatenated in scene mesh order, with indices offset accordingly.
// UVs: uses first channel if present, else 0.
// Normals: uses imported normals if present; if absent, Assimp must be configured to generate them.
bool assimp_import_to_ewmesh_v1(const ::SubstrateManager* sm,
                               const std::string& src_path_utf8,
                               EwMeshV1& out_mesh,
                               std::string* out_report_utf8);

// Convenience: import and write EWM1.
bool assimp_import_fbx_to_ewmesh_file(const ::SubstrateManager* sm,
                                     const std::string& fbx_path_utf8,
                                     const std::string& out_ewmesh_path_utf8,
                                     std::string* out_report_utf8);

// Export: load a source scene (FBX/glTF/etc), dump embedded textures externally,
// rewrite material texture paths to the dumped files, then export to dst format.
// dst_format_id examples: "gltf2", "gltf", "fbx" (Assimp exporter IDs).
// Texture dump directory is created if missing.
// Returns false on any error and writes a short report.
bool assimp_export_with_external_textures(const std::string& src_path_utf8,
                                         const std::string& dst_path_utf8,
                                         const std::string& dst_format_id_utf8,
                                         const std::string& texture_dump_dir_utf8,
                                         std::string* out_report_utf8);

} // namespace genesis
