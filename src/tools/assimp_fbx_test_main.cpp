#include "assimp_fbx_io.hpp"
#include "ew_cli_args.hpp"

#include "ew_substrate_manager.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    ew::CliArgsKV kv;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, kv)) {
        std::fprintf(stderr, "assimp_fbx_test: failed to parse args\n");
        return 2;
    }

    std::string in_path;
    if (!ew::ew_cli_get_str(kv, "in", in_path) && !ew::ew_cli_get_str(kv, "input", in_path)) {
        std::fprintf(stderr, "usage: ew_assimp_fbx_test in=<file.fbx> [out=<file.ewmesh>] [export=<dst>] [fmt=gltf2|fbx] [texdir=<dir>]\n");
        return 1;
    }

    std::string out_ewmesh;
    ew::ew_cli_get_str(kv, "out", out_ewmesh);

    std::string export_dst;
    ew::ew_cli_get_str(kv, "export", export_dst);

    std::string fmt = "gltf2";
    ew::ew_cli_get_str(kv, "fmt", fmt);

    std::string texdir = "texture_dump";
    ew::ew_cli_get_str(kv, "texdir", texdir);

    std::string report;
    genesis::EwMeshV1 mesh;
    SubstrateManager sm = ew_substrate_manager_build_default(/*seed=*/1u);
    sm.materials_calib_done = true;
    if (!genesis::assimp_import_to_ewmesh_v1(&sm, in_path, mesh, &report)) {
        std::fprintf(stderr, "%s", report.c_str());
        return 3;
    }
    std::printf("%s", report.c_str());

    if (!out_ewmesh.empty()) {
        std::string rep2;
        if (!genesis::assimp_import_fbx_to_ewmesh_file(&sm, in_path, out_ewmesh, &rep2)) {
            std::fprintf(stderr, "%s", rep2.c_str());
            return 4;
        }
        std::printf("%s", rep2.c_str());
    }

    if (!export_dst.empty()) {
        std::string rep3;
        if (!genesis::assimp_export_with_external_textures(in_path, export_dst, fmt, texdir, &rep3)) {
            std::fprintf(stderr, "%s", rep3.c_str());
            return 5;
        }
        std::printf("%s", rep3.c_str());
    }

    return 0;
}
