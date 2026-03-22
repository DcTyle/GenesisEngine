#include "obj_import.hpp"
#include <fstream>
#include <sstream>
#include <map>

static bool read_all_text_utf8(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static inline void trim_cr(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

static inline bool parse_floats(const std::string& line, float* dst, int n) {
    std::istringstream iss(line);
    std::string tag;
    if (!(iss >> tag)) return false;
    for (int i=0;i<n;i++) {
        if (!(iss >> dst[i])) return false;
    }
    return true;
}

static inline bool parse_face_token(const std::string& tok, int& vi, int& ti, int& ni) {
    vi = ti = ni = 0;
    // forms: v, v/vt, v//vn, v/vt/vn
    size_t a = tok.find('/');
    if (a == std::string::npos) {
        vi = std::stoi(tok);
        return true;
    }
    size_t b = tok.find('/', a+1);
    vi = std::stoi(tok.substr(0,a));
    if (b == std::string::npos) {
        ti = std::stoi(tok.substr(a+1));
        return true;
    }
    if (b == a+1) {
        // v//vn
        ni = std::stoi(tok.substr(b+1));
        return true;
    }
    ti = std::stoi(tok.substr(a+1, b-(a+1)));
    ni = std::stoi(tok.substr(b+1));
    return true;
}

static inline uint32_t hash_key(int vi, int ti, int ni) {
    // Deterministic packing.
    return (uint32_t)((vi & 0x3FF) | ((ti & 0x3FF) << 10) | ((ni & 0x3FF) << 20));
}

std::string ew_obj_basename_utf8(const std::string& path_utf8) {
    size_t s = path_utf8.find_last_of("/\\");
    std::string name = (s == std::string::npos) ? path_utf8 : path_utf8.substr(s+1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0,dot);
    if (name.empty()) name = "object";
    return name;
}

bool ew_obj_load_utf8(const std::string& path_utf8, EwObjMesh& out) {
    out.vertices.clear();
    out.indices.clear();

    std::string txt;
    if (!read_all_text_utf8(path_utf8, txt)) return false;

    std::vector<float> pos; pos.reserve(1024*3);
    std::vector<float> uv;  uv.reserve(1024*2);
    std::vector<float> nrm; nrm.reserve(1024*3);

    // Deterministic ordered map. Avoid hash-based containers.
    std::map<uint32_t, uint32_t> map;

    std::istringstream iss(txt);
    std::string line;
    while (std::getline(iss, line)) {
        trim_cr(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        if (line.rfind("v ", 0) == 0) {
            float v[3];
            if (!parse_floats(line, v, 3)) continue;
            pos.push_back(v[0]); pos.push_back(v[1]); pos.push_back(v[2]);
        } else if (line.rfind("vt ", 0) == 0) {
            float v[2];
            if (!parse_floats(line, v, 2)) continue;
            uv.push_back(v[0]); uv.push_back(v[1]);
        } else if (line.rfind("vn ", 0) == 0) {
            float v[3];
            if (!parse_floats(line, v, 3)) continue;
            nrm.push_back(v[0]); nrm.push_back(v[1]); nrm.push_back(v[2]);
        } else if (line.rfind("f ", 0) == 0) {
            std::istringstream fs(line);
            std::string tag; fs >> tag;
            std::vector<std::string> toks;
            std::string t;
            while (fs >> t) toks.push_back(t);
            if (toks.size() < 3) continue;

            auto emit_vertex = [&](const std::string& tok)->uint32_t {
                int vi, ti, ni;
                if (!parse_face_token(tok, vi, ti, ni)) return 0;
                // OBJ indices are 1-based; negative indices count from end.
                auto fix = [](int idx, int count)->int {
                    if (idx > 0) return idx - 1;
                    if (idx < 0) return count + idx;
                    return 0;
                };
                const int vcount = (int)(pos.size()/3);
                const int tcount = (int)(uv.size()/2);
                const int ncount = (int)(nrm.size()/3);
                vi = fix(vi, vcount);
                ti = (ti==0) ? -1 : fix(ti, tcount);
                ni = (ni==0) ? -1 : fix(ni, ncount);

                // Clamp pack range for map key; larger files still work but map collisions could occur.
                // This importer is a minimal MVP; high-poly content should be processed by a real pipeline.
                const uint32_t key = hash_key(vi, (ti<0?0:ti), (ni<0?0:ni));
                auto it = map.find(key);
                if (it != map.end()) return it->second;

                EwObjMesh::Vtx vtx{};
                vtx.px = pos[(size_t)vi*3+0];
                vtx.py = pos[(size_t)vi*3+1];
                vtx.pz = pos[(size_t)vi*3+2];
                if (ti >= 0) {
                    vtx.u = uv[(size_t)ti*2+0];
                    vtx.v = uv[(size_t)ti*2+1];
                } else {
                    vtx.u = 0.f; vtx.v = 0.f;
                }
                if (ni >= 0) {
                    vtx.nx = nrm[(size_t)ni*3+0];
                    vtx.ny = nrm[(size_t)ni*3+1];
                    vtx.nz = nrm[(size_t)ni*3+2];
                } else {
                    vtx.nx = 0.f; vtx.ny = 0.f; vtx.nz = 1.f;
                }

                const uint32_t idx = (uint32_t)out.vertices.size();
                out.vertices.push_back(vtx);
                map[key] = idx;
                return idx;
            };

            // Fan triangulate: (0,i,i+1)
            const uint32_t i0 = emit_vertex(toks[0]);
            for (size_t i=1;i+1<toks.size();i++) {
                const uint32_t i1 = emit_vertex(toks[i]);
                const uint32_t i2 = emit_vertex(toks[i+1]);
                out.indices.push_back(i0);
                out.indices.push_back(i1);
                out.indices.push_back(i2);
            }
        }
    }

    return !out.vertices.empty() && !out.indices.empty();
}
