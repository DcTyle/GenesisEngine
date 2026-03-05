#include "assimp_fbx_io.hpp"

#include "GE_runtime.hpp"
#include "GE_uv_unwrap.hpp"
#include "ewmesh_voxelizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace genesis {

static aiScene* build_single_mesh_scene(const EwMeshV1& mesh,
                                       const std::string& diffuse_texture_rel_utf8,
                                       std::string* report) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return nullptr;
    if ((mesh.indices.size() % 3u) != 0u) return nullptr;

    aiScene* sc = new aiScene();
    sc->mRootNode = new aiNode();
    sc->mRootNode->mName = aiString("root");

    // One mesh.
    sc->mNumMeshes = 1;
    sc->mMeshes = new aiMesh*[1];
    sc->mMeshes[0] = new aiMesh();
    aiMesh* am = sc->mMeshes[0];
    am->mMaterialIndex = 0;
    am->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

    const unsigned vcount = (unsigned)mesh.vertices.size();
    const unsigned fcount = (unsigned)(mesh.indices.size() / 3u);
    am->mNumVertices = vcount;
    am->mVertices = new aiVector3D[vcount];
    am->mNormals = new aiVector3D[vcount];
    am->mTextureCoords[0] = new aiVector3D[vcount];
    am->mNumUVComponents[0] = 2;

    for (unsigned i = 0; i < vcount; ++i) {
        const auto& v = mesh.vertices[(size_t)i];
        am->mVertices[i] = aiVector3D(v.px, v.py, v.pz);
        am->mNormals[i]  = aiVector3D(v.nx, v.ny, v.nz);
        am->mTextureCoords[0][i] = aiVector3D(v.u, v.v, 0.0f);
    }

    am->mNumFaces = fcount;
    am->mFaces = new aiFace[fcount];
    for (unsigned f = 0; f < fcount; ++f) {
        aiFace& face = am->mFaces[f];
        face.mNumIndices = 3;
        face.mIndices = new unsigned[3];
        face.mIndices[0] = mesh.indices[(size_t)f * 3u + 0u];
        face.mIndices[1] = mesh.indices[(size_t)f * 3u + 1u];
        face.mIndices[2] = mesh.indices[(size_t)f * 3u + 2u];
    }

    // One material.
    sc->mNumMaterials = 1;
    sc->mMaterials = new aiMaterial*[1];
    sc->mMaterials[0] = new aiMaterial();
    aiMaterial* mat = sc->mMaterials[0];

    aiString mat_name("GenesisMaterial0");
    mat->AddProperty(&mat_name, AI_MATKEY_NAME);

    // Set diffuse texture path (relative) if provided.
    if (!diffuse_texture_rel_utf8.empty()) {
        aiString tex(diffuse_texture_rel_utf8);
        mat->AddProperty(&tex, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0));
        append_report(report, std::string("ASSIMP_EXPORT_OBJ:DIFFUSE_TEX=") + diffuse_texture_rel_utf8);
    }

    // Node references mesh 0.
    sc->mRootNode->mNumMeshes = 1;
    sc->mRootNode->mMeshes = new unsigned[1];
    sc->mRootNode->mMeshes[0] = 0;
    return sc;
}

static inline int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int64_t q32_32_from_double(double x) {
    // Clamp to signed 32.32 representable range: integer part fits in signed 32.
    const double lo = -2147483648.0;
    const double hi =  2147483647.0;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    const double scaled = x * 4294967296.0; // 2^32
    // round to nearest, ties away from zero (deterministic for finite values)
    const double r = (scaled >= 0.0) ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
    const long double rr = (long double)r;
    const long double i64_lo = (long double)INT64_MIN;
    const long double i64_hi = (long double)INT64_MAX;
    long double cl = rr;
    if (cl < i64_lo) cl = i64_lo;
    if (cl > i64_hi) cl = i64_hi;
    return (int64_t)cl;
}

static inline float float_from_q32_32(int64_t q) {
    const double x = (double)q / 4294967296.0;
    // float cast is deterministic given IEEE-754.
    return (float)x;
}

static inline void append_report(std::string* out, const std::string& line) {
    if (!out) return;
    out->append(line);
    if (!out->empty() && out->back() != '\n') out->push_back('\n');
}

static uint32_t stable_postprocess_flags() {
    // Keep flags fixed; avoid stochastic/heuristic steps.
    return (uint32_t)(
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_ImproveCacheLocality |
        aiProcess_RemoveRedundantMaterials |
        aiProcess_FindInvalidData |
        aiProcess_ValidateDataStructure |
        aiProcess_OptimizeMeshes |
        aiProcess_OptimizeGraph
    );
}

static bool scene_to_ewmesh(const aiScene* sc, EwMeshV1& out_mesh, std::string* report) {
    out_mesh.vertices.clear();
    out_mesh.indices.clear();
    if (!sc) return false;
    if (!sc->HasMeshes()) return false;

    size_t total_v = 0;
    size_t total_i = 0;
    for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
        const aiMesh* mesh = sc->mMeshes[m];
        if (!mesh) continue;
        total_v += (size_t)mesh->mNumVertices;
        total_i += (size_t)mesh->mNumFaces * 3u;
    }
    if (total_v == 0 || total_i == 0) return false;
    if (total_v > (1u << 26)) return false;
    if (total_i > (1u << 28)) return false;

    out_mesh.vertices.reserve(total_v);
    out_mesh.indices.reserve(total_i);

    uint32_t base = 0;
    for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
        const aiMesh* mesh = sc->mMeshes[m];
        if (!mesh) continue;
        if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
            // Triangulate flag should ensure triangles only, but fail closed.
            return false;
        }

        const bool has_normals = mesh->HasNormals();
        const bool has_uv0 = mesh->HasTextureCoords(0);

        // vertices
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            const aiVector3D p = mesh->mVertices[i];
            const int64_t px_q = q32_32_from_double((double)p.x);
            const int64_t py_q = q32_32_from_double((double)p.y);
            const int64_t pz_q = q32_32_from_double((double)p.z);

            EwMeshV1::Vtx v{};
            v.px = float_from_q32_32(px_q);
            v.py = float_from_q32_32(py_q);
            v.pz = float_from_q32_32(pz_q);

            if (has_normals) {
                const aiVector3D n = mesh->mNormals[i];
                v.nx = (float)n.x;
                v.ny = (float)n.y;
                v.nz = (float)n.z;
            } else {
                v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
            }
            if (has_uv0) {
                const aiVector3D t = mesh->mTextureCoords[0][i];
                v.u = (float)t.x;
                v.v = (float)t.y;
            } else {
                v.u = 0.0f; v.v = 0.0f;
            }
            out_mesh.vertices.push_back(v);
        }

        // indices
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) return false;
            const uint32_t a = base + (uint32_t)face.mIndices[0];
            const uint32_t b = base + (uint32_t)face.mIndices[1];
            const uint32_t c = base + (uint32_t)face.mIndices[2];
            out_mesh.indices.push_back(a);
            out_mesh.indices.push_back(b);
            out_mesh.indices.push_back(c);
        }

        base += (uint32_t)mesh->mNumVertices;
    }

    append_report(report, "ASSIMP_IMPORT:mesh_count=" + std::to_string(sc->mNumMeshes) +
                          " vertices=" + std::to_string(out_mesh.vertices.size()) +
                          " indices=" + std::to_string(out_mesh.indices.size()));

    // Deterministic auto-UV unwrap for synthesis. Import UVs are treated as hints.
    if (!ge_auto_uv_unwrap_box(out_mesh)) {
        append_report(report, "ASSIMP_IMPORT:auto_uv_unwrap=FAIL");
        return false;
    }
    append_report(report, "ASSIMP_IMPORT:auto_uv_unwrap=OK");

    // also list material names deterministically
    if (sc->HasMaterials()) {
        for (unsigned mi = 0; mi < sc->mNumMaterials; ++mi) {
            const aiMaterial* mat = sc->mMaterials[mi];
            aiString name;
            if (mat && mat->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS) {
                append_report(report, "ASSIMP_IMPORT:MATERIAL[" + std::to_string(mi) + "]=" + std::string(name.C_Str()));
            } else {
                append_report(report, "ASSIMP_IMPORT:MATERIAL[" + std::to_string(mi) + "]=<unnamed>");
            }
        }
    }
    return true;
}

bool assimp_import_to_ewmesh_v1(const SubstrateMicroprocessor* sm,
                               const std::string& src_path_utf8,
                               EwMeshV1& out_mesh,
                               std::string* out_report_utf8) {
    if (!sm) return false;
    if (!sm->materials_calib_done) return false;

    Assimp::Importer imp;
    imp.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    const uint32_t flags = stable_postprocess_flags() | aiProcess_GenNormals;

    const aiScene* sc = imp.ReadFile(src_path_utf8, flags);
    if (!sc || !sc->mRootNode) {
        append_report(out_report_utf8, std::string("ASSIMP_IMPORT:FAIL:") + imp.GetErrorString());
        return false;
    }
    if (!scene_to_ewmesh(sc, out_mesh, out_report_utf8)) {
        append_report(out_report_utf8, "ASSIMP_IMPORT:FAIL:scene_to_ewmesh");
        return false;
    }
    return true;
}

bool assimp_import_fbx_to_ewmesh_file(const SubstrateMicroprocessor* sm,
                                     const std::string& fbx_path_utf8,
                                     const std::string& out_ewmesh_path_utf8,
                                     std::string* out_report_utf8) {
    EwMeshV1 mesh;
    if (!assimp_import_to_ewmesh_v1(sm, fbx_path_utf8, mesh, out_report_utf8)) return false;
    if (!ewmesh_write_v1(out_ewmesh_path_utf8, mesh)) {
        append_report(out_report_utf8, "ASSIMP_IMPORT:FAIL:ewmesh_write_v1");
        return false;
    }
    append_report(out_report_utf8, "ASSIMP_IMPORT:WROTE_EWM1:" + out_ewmesh_path_utf8);
    return true;
}

static bool write_all_bytes(const std::string& path, const void* data, size_t n) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (n) f.write(reinterpret_cast<const char*>(data), (std::streamsize)n);
    return f.good();
}

// minimal uncompressed TGA writer (32bpp BGRA) for aiTexel arrays
static bool write_tga_32bpp(const std::string& path, uint32_t w, uint32_t h, const aiTexel* texels) {
    if (!texels || w == 0 || h == 0) return false;
    uint8_t hdr[18];
    std::memset(hdr, 0, sizeof(hdr));
    hdr[2] = 2; // uncompressed true-color
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)((w >> 8) & 0xFF);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)((h >> 8) & 0xFF);
    hdr[16] = 32; // bpp
    hdr[17] = 8;  // alpha bits

    std::vector<uint8_t> pixels;
    pixels.resize((size_t)w * (size_t)h * 4u);

    // Assimp aiTexel is ARGB in bytes (r,g,b,a) as fields; we emit BGRA for TGA.
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const aiTexel t = texels[(size_t)y * w + x];
            const size_t o = ((size_t)y * w + x) * 4u;
            pixels[o + 0] = (uint8_t)t.b;
            pixels[o + 1] = (uint8_t)t.g;
            pixels[o + 2] = (uint8_t)t.r;
            pixels[o + 3] = (uint8_t)t.a;
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(hdr), 18);
    f.write(reinterpret_cast<const char*>(pixels.data()), (std::streamsize)pixels.size());
    return f.good();
}

static std::string sanitize_rel_tex_name(unsigned material_i, unsigned slot_i, const std::string& ext) {
    std::string e = ext;
    if (!e.empty() && e[0] != '.') e = "." + e;
    if (e.empty()) e = ".bin";
    return "tex_m" + std::to_string(material_i) + "_s" + std::to_string(slot_i) + e;
}

static bool ensure_dir(const std::string& dir_utf8) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir_utf8), ec);
    return !ec;
}

static bool dump_embedded_textures_and_rewrite(aiScene* sc,
                                               const std::string& dump_dir,
                                               std::string* report) {
    if (!sc) return false;
    if (!ensure_dir(dump_dir)) return false;

    // Build a quick map of embedded texture name -> pointer
    // Embedded textures are referenced by "*0", "*1", ...
    const unsigned tcount = sc->mNumTextures;
    std::vector<const aiTexture*> embedded;
    embedded.reserve(tcount);
    for (unsigned i = 0; i < tcount; ++i) embedded.push_back(sc->mTextures[i]);

    auto dump_by_index = [&](unsigned tex_i, unsigned mat_i, unsigned slot_i) -> std::string {
        if (tex_i >= embedded.size() || !embedded[tex_i]) return std::string();
        const aiTexture* tex = embedded[tex_i];
        std::string out_path;
        std::string rel_name;

        if (tex->mHeight == 0) {
            // Compressed (PNG/JPG/etc) blob, tex->achFormatHint may help.
            std::string ext;
            if (tex->achFormatHint[0] != 0) {
                ext = std::string(".") + std::string(tex->achFormatHint);
            } else {
                ext = ".bin";
            }
            rel_name = sanitize_rel_tex_name(mat_i, slot_i, ext);
            out_path = (std::filesystem::path(dump_dir) / rel_name).string();
            const size_t n = (size_t)tex->mWidth;
            if (!write_all_bytes(out_path, tex->pcData, n)) return std::string();
        } else {
            // Uncompressed aiTexel array.
            rel_name = sanitize_rel_tex_name(mat_i, slot_i, ".tga");
            out_path = (std::filesystem::path(dump_dir) / rel_name).string();
            if (!write_tga_32bpp(out_path, (uint32_t)tex->mWidth, (uint32_t)tex->mHeight, tex->pcData)) return std::string();
        }

        append_report(report, "ASSIMP_EXPORT:DUMP_TEXTURE:" + rel_name);
        return rel_name;
    };

    // Rewrite material texture paths (diffuse/specular/normal etc).
    for (unsigned mi = 0; mi < sc->mNumMaterials; ++mi) {
        aiMaterial* mat = sc->mMaterials[mi];
        if (!mat) continue;

        const aiTextureType types[] = {
            aiTextureType_DIFFUSE,
            aiTextureType_SPECULAR,
            aiTextureType_NORMALS,
            aiTextureType_BASE_COLOR,
            aiTextureType_METALNESS,
            aiTextureType_DIFFUSE_ROUGHNESS,
            aiTextureType_EMISSIVE,
            aiTextureType_AMBIENT_OCCLUSION
        };

        unsigned slot_global = 0;
        for (aiTextureType tt : types) {
            const unsigned n = mat->GetTextureCount(tt);
            for (unsigned ti = 0; ti < n; ++ti, ++slot_global) {
                aiString path;
                aiTextureMapping mapping;
                unsigned uvindex = 0;
                float blend = 0.0f;
                aiTextureOp op;
                aiTextureMapMode mapmode[2];
                if (mat->GetTexture(tt, ti, &path, &mapping, &uvindex, &blend, &op, mapmode) != aiReturn_SUCCESS) {
                    continue;
                }
                std::string p = path.C_Str();
                if (!p.empty() && p[0] == '*') {
                    // embedded index
                    const unsigned idx = (unsigned)std::strtoul(p.c_str() + 1, nullptr, 10);
                    const std::string rel = dump_by_index(idx, mi, slot_global);
                    if (!rel.empty()) {
                        aiString newp(rel);
                        mat->AddProperty(&newp, AI_MATKEY_TEXTURE(tt, ti));
                    }
                } else {
                    // External texture: normalize to just filename if it contains directories.
                    std::filesystem::path pp(p);
                    const std::string fname = pp.filename().string();
                    if (!fname.empty() && fname != p) {
                        aiString newp(fname);
                        mat->AddProperty(&newp, AI_MATKEY_TEXTURE(tt, ti));
                    }
                }
            }
        }
    }
    return true;
}

bool assimp_export_with_external_textures(const std::string& src_path_utf8,
                                         const std::string& dst_path_utf8,
                                         const std::string& dst_format_id_utf8,
                                         const std::string& texture_dump_dir_utf8,
                                         std::string* out_report_utf8) {
    Assimp::Importer imp;
    const uint32_t flags = stable_postprocess_flags(); // don't mutate geometry unnecessarily on export
    const aiScene* sc_in = imp.ReadFile(src_path_utf8, flags);
    if (!sc_in || !sc_in->mRootNode) {
        append_report(out_report_utf8, std::string("ASSIMP_EXPORT:FAIL:") + imp.GetErrorString());
        return false;
    }

    // We need a mutable copy to rewrite materials. Assimp doesn't provide a deep copy helper here,
    // so we re-import and operate on the non-const pointer via const_cast with care.
    aiScene* sc = const_cast<aiScene*>(sc_in);

    if (!dump_embedded_textures_and_rewrite(sc, texture_dump_dir_utf8, out_report_utf8)) {
        append_report(out_report_utf8, "ASSIMP_EXPORT:FAIL:texture_dump");
        return false;
    }

    Assimp::Exporter exp;
    const aiReturn r = exp.Export(sc, dst_format_id_utf8.c_str(), dst_path_utf8);
    if (r != aiReturn_SUCCESS) {
        append_report(out_report_utf8, std::string("ASSIMP_EXPORT:FAIL:") + exp.GetErrorString());
        return false;
    }
    append_report(out_report_utf8, "ASSIMP_EXPORT:OK:" + dst_path_utf8);
    return true;
}

bool assimp_export_ewmesh_v1_single_material(const EwMeshV1& mesh,
                                            const std::string& dst_path_utf8,
                                            const std::string& dst_format_id_utf8,
                                            const std::string& diffuse_texture_rel_utf8,
                                            std::string* out_report_utf8) {
    // Build and export a single-mesh scene.
    aiScene* sc = build_single_mesh_scene(mesh, diffuse_texture_rel_utf8, out_report_utf8);
    if (!sc || !sc->mRootNode) {
        append_report(out_report_utf8, "ASSIMP_EXPORT_OBJ:FAIL:build_scene");
        delete sc;
        return false;
    }
    Assimp::Exporter exp;
    const aiReturn r = exp.Export(sc, dst_format_id_utf8.c_str(), dst_path_utf8);
    if (r != aiReturn_SUCCESS) {
        append_report(out_report_utf8, std::string("ASSIMP_EXPORT_OBJ:FAIL:") + exp.GetErrorString());
        delete sc;
        return false;
    }
    append_report(out_report_utf8, "ASSIMP_EXPORT_OBJ:OK:" + dst_path_utf8);
    delete sc;
    return true;
}

} // namespace genesis
