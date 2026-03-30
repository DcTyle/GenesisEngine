#include "assimp_fbx_io.hpp"

namespace {
static void ew_set_assimp_unavailable_report(std::string* out_report_utf8) {
    if (!out_report_utf8) return;
    *out_report_utf8 = "Assimp tools disabled at build time (EW_BUILD_ASSIMP_TOOLS=OFF).";
}
}

namespace genesis {

bool assimp_import_to_ewmesh_v1(const ::SubstrateManager*,
                               const std::string&,
                               EwMeshV1&,
                               std::string* out_report_utf8) {
    ew_set_assimp_unavailable_report(out_report_utf8);
    return false;
}

bool assimp_import_fbx_to_ewmesh_file(const ::SubstrateManager*,
                                     const std::string&,
                                     const std::string&,
                                     std::string* out_report_utf8) {
    ew_set_assimp_unavailable_report(out_report_utf8);
    return false;
}

bool assimp_export_with_external_textures(const std::string&,
                                         const std::string&,
                                         const std::string&,
                                         const std::string&,
                                         std::string* out_report_utf8) {
    ew_set_assimp_unavailable_report(out_report_utf8);
    return false;
}

bool assimp_export_ewmesh_v1_single_material(const EwMeshV1&,
                                            const std::string&,
                                            const std::string&,
                                            const std::string&,
                                            std::string* out_report_utf8) {
    ew_set_assimp_unavailable_report(out_report_utf8);
    return false;
}

} // namespace genesis

