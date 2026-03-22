#include "assimp_fbx_io.hpp"

namespace genesis {

namespace {

static bool ew_assimp_unavailable(std::string* out_report_utf8) {
    if (out_report_utf8) {
        *out_report_utf8 = "Assimp support is disabled in this build. Reconfigure with EW_BUILD_ASSIMP_TOOLS=ON to enable import/export.";
    }
    return false;
}

} // namespace

bool assimp_import_to_ewmesh_v1(const ::SubstrateManager* sm,
                                const std::string& src_path_utf8,
                                EwMeshV1& out_mesh,
                                std::string* out_report_utf8) {
    (void)sm;
    (void)src_path_utf8;
    out_mesh = EwMeshV1{};
    return ew_assimp_unavailable(out_report_utf8);
}

bool assimp_import_fbx_to_ewmesh_file(const ::SubstrateManager* sm,
                                      const std::string& fbx_path_utf8,
                                      const std::string& out_ewmesh_path_utf8,
                                      std::string* out_report_utf8) {
    (void)sm;
    (void)fbx_path_utf8;
    (void)out_ewmesh_path_utf8;
    return ew_assimp_unavailable(out_report_utf8);
}

bool assimp_export_with_external_textures(const std::string& src_path_utf8,
                                          const std::string& dst_path_utf8,
                                          const std::string& dst_format_id_utf8,
                                          const std::string& texture_dump_dir_utf8,
                                          std::string* out_report_utf8) {
    (void)src_path_utf8;
    (void)dst_path_utf8;
    (void)dst_format_id_utf8;
    (void)texture_dump_dir_utf8;
    return ew_assimp_unavailable(out_report_utf8);
}

bool assimp_export_ewmesh_v1_single_material(const EwMeshV1& mesh,
                                             const std::string& dst_path_utf8,
                                             const std::string& dst_format_id_utf8,
                                             const std::string& diffuse_texture_rel_utf8,
                                             std::string* out_report_utf8) {
    (void)mesh;
    (void)dst_path_utf8;
    (void)dst_format_id_utf8;
    (void)diffuse_texture_rel_utf8;
    return ew_assimp_unavailable(out_report_utf8);
}

} // namespace genesis
