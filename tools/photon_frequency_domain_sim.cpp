#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Config {
    int packet_count = 32;
    int bin_count = 128;
    int steps = 48;
    int recon_samples = 256;
    int equivalent_grid_linear = 256;
    int low_bin_count = 12;
    double kappa_a = 0.085;
    double kappa_f = 0.055;
    double kappa_couple = 0.080;
    double kappa_leak = 0.035;
    double max_amplitude = 2.75;
    // GPU pulse quartet injected into the field equations (simulation only).
    double pulse_frequency_norm = 0.245;
    double pulse_amplitude_norm = 0.19;
    double pulse_amperage_norm = 0.35;
    double pulse_voltage_norm = 0.35;
    bool nist_strict = false;
};

struct Seed {
    const char* name = "D_track";
    double f = 0.2;
    double a = 0.19;
    double v = 0.35;
    double i = 0.35;
};

struct Packet {
    int id = 0;
    int charge = 1;
    Seed seed{};
    double amp_drive = 1.0;
    double freq_drive = 1.0;
    double current_drive = 1.0;
    double volt_drive = 1.0;
    std::array<std::vector<std::complex<double>>, 3> spectrum{};
};

struct NistSiliconRef {
    double lattice_constant_m = 5.431020511e-10;
    double density_g_cm3 = 2.33;
    double mean_excitation_energy_ev = 173.0;
    double first_ionization_energy_ev = 8.15168;
    bool loaded = false;
    std::string source_path;
    std::string load_error;
};

constexpr double kPi = 3.1415926535897932384626433832795;

double clampd(double v, double lo, double hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }
std::complex<double> cis(double p) { return {std::cos(p), std::sin(p)}; }

std::vector<double> linspace(double a, double b, int n) {
    std::vector<double> out((size_t)n, 0.0);
    if (n <= 1) {
        if (n == 1) out[0] = a;
        return out;
    }
    const double d = (b - a) / (double)(n - 1);
    for (int i = 0; i < n; ++i) out[(size_t)i] = a + d * (double)i;
    return out;
}

std::vector<double> grad(const std::vector<double>& v) {
    const int n = (int)v.size();
    std::vector<double> g((size_t)n, 0.0);
    if (n <= 1) return g;
    g[0] = v[1] - v[0];
    for (int i = 1; i < n - 1; ++i) g[(size_t)i] = 0.5 * (v[(size_t)(i + 1)] - v[(size_t)(i - 1)]);
    g[(size_t)(n - 1)] = v[(size_t)(n - 1)] - v[(size_t)(n - 2)];
    return g;
}

std::vector<double> blur1d(const std::vector<double>& x, double sigma) {
    const int n = (int)x.size();
    const int r = std::max(1, (int)std::ceil(3.0 * sigma));
    std::vector<double> k((size_t)(2 * r + 1), 0.0);
    double ks = 0.0;
    for (int i = -r; i <= r; ++i) {
        const double v = std::exp(-0.5 * ((double)i / std::max(1.0e-6, sigma)) * ((double)i / std::max(1.0e-6, sigma)));
        k[(size_t)(i + r)] = v;
        ks += v;
    }
    for (double& v : k) v /= ks;
    std::vector<double> y((size_t)n, 0.0);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = -r; j <= r; ++j) {
            int idx = i + j;
            if (idx < 0) idx = 0;
            if (idx >= n) idx = n - 1;
            s += x[(size_t)idx] * k[(size_t)(j + r)];
        }
        y[(size_t)i] = s;
    }
    return y;
}

std::array<std::vector<double>, 3> ifft3(const Packet& p, int n) {
    std::array<std::vector<double>, 3> out{{std::vector<double>((size_t)n), std::vector<double>((size_t)n), std::vector<double>((size_t)n)}};
    const int kmax = (int)p.spectrum[0].size();
    for (int a = 0; a < 3; ++a) {
        for (int t = 0; t < n; ++t) {
            std::complex<double> s(0.0, 0.0);
            for (int k = 0; k < kmax; ++k) s += p.spectrum[(size_t)a][(size_t)k] * cis(2.0 * kPi * (double)(t * k) / (double)n);
            out[(size_t)a][(size_t)t] = s.real() / (double)n;
        }
    }
    return out;
}

double phase_lock(const Packet& a, const Packet& b) {
    std::complex<double> num(0.0, 0.0);
    double da = 0.0, db = 0.0;
    for (int ax = 0; ax < 3; ++ax) {
        for (size_t i = 0; i < a.spectrum[(size_t)ax].size(); ++i) {
            num += std::conj(a.spectrum[(size_t)ax][i]) * b.spectrum[(size_t)ax][i];
            da += std::norm(a.spectrum[(size_t)ax][i]);
            db += std::norm(b.spectrum[(size_t)ax][i]);
        }
    }
    return std::abs(num) / (std::sqrt(da) * std::sqrt(db) + 1.0e-9);
}

void append_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFFu)); out.push_back((uint8_t)((v >> 8) & 0xFFu));
    out.push_back((uint8_t)((v >> 16) & 0xFFu)); out.push_back((uint8_t)((v >> 24) & 0xFFu));
}
void append_u64(std::vector<uint8_t>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back((uint8_t)((v >> (i * 8)) & 0xFFu)); }
void append_blob(std::vector<uint8_t>& out, const std::string& s) { append_u32(out, (uint32_t)s.size()); out.insert(out.end(), s.begin(), s.end()); }
void append_blob(std::vector<uint8_t>& out, const std::vector<uint8_t>& b) { append_u32(out, (uint32_t)b.size()); out.insert(out.end(), b.begin(), b.end()); }

void write_txt(const std::filesystem::path& p, const std::string& c) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << c;
}

bool read_text_file(const std::filesystem::path& path, std::string& out) {
    out.clear();
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.good()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

bool extract_json_number(const std::string& text, const char* key, double& out_value) {
    if (!key) return false;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t kpos = text.find(needle);
    if (kpos == std::string::npos) return false;
    const size_t cpos = text.find(':', kpos + needle.size());
    if (cpos == std::string::npos) return false;
    const char* p = text.c_str() + cpos + 1u;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    char* endp = nullptr;
    const double v = std::strtod(p, &endp);
    if (endp == p) return false;
    out_value = v;
    return true;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8u);
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

bool load_nist_silicon_reference(const std::filesystem::path& path, NistSiliconRef& out_ref) {
    out_ref = NistSiliconRef{};
    out_ref.source_path = path.string();
    std::string text;
    if (!read_text_file(path, text)) {
        out_ref.load_error = "unable_to_read_file";
        return false;
    }
    double lattice = 0.0;
    double density = 0.0;
    double excitation = 0.0;
    double ionization = 0.0;
    const bool ok =
        extract_json_number(text, "lattice_constant_m", lattice) &&
        extract_json_number(text, "density_g_cm3", density) &&
        extract_json_number(text, "mean_excitation_energy_ev", excitation) &&
        extract_json_number(text, "first_ionization_energy_ev", ionization);
    if (!ok) {
        out_ref.load_error = "missing_required_nist_keys";
        return false;
    }
    out_ref.lattice_constant_m = lattice;
    out_ref.density_g_cm3 = density;
    out_ref.mean_excitation_energy_ev = excitation;
    out_ref.first_ionization_energy_ev = ionization;
    out_ref.loaded = true;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root = std::filesystem::current_path() / "ResearchConfinement";
    std::filesystem::path out_dir = root / "frequency_domain_runs" / "latest";
    std::filesystem::path nist_ref_path = root / "nist_silicon_reference.json";
    Config cfg{};
    bool write_root = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next_i = [&](int d) { if (i + 1 >= argc) return d; ++i; return std::stoi(argv[i]); };
        auto next_d = [&](double d) { if (i + 1 >= argc) return d; ++i; return std::stod(argv[i]); };
        auto next_s = [&](const std::string& d) { if (i + 1 >= argc) return d; ++i; return std::string(argv[i]); };
        if (a == "--packet-count") cfg.packet_count = next_i(cfg.packet_count);
        else if (a == "--bin-count") cfg.bin_count = next_i(cfg.bin_count);
        else if (a == "--steps") cfg.steps = next_i(cfg.steps);
        else if (a == "--recon-samples") cfg.recon_samples = next_i(cfg.recon_samples);
        else if (a == "--equivalent-grid-linear") cfg.equivalent_grid_linear = next_i(cfg.equivalent_grid_linear);
        else if (a == "--output-dir") out_dir = next_s(out_dir.string());
        else if (a == "--nist-ref") nist_ref_path = next_s(nist_ref_path.string());
        else if (a == "--pulse-frequency") cfg.pulse_frequency_norm = next_d(cfg.pulse_frequency_norm);
        else if (a == "--pulse-amplitude") cfg.pulse_amplitude_norm = next_d(cfg.pulse_amplitude_norm);
        else if (a == "--pulse-amperage") cfg.pulse_amperage_norm = next_d(cfg.pulse_amperage_norm);
        else if (a == "--pulse-voltage") cfg.pulse_voltage_norm = next_d(cfg.pulse_voltage_norm);
        else if (a == "--nist-strict") cfg.nist_strict = true;
        else if (a == "--write-root-samples") write_root = true;
    }

    cfg.pulse_frequency_norm = clampd(cfg.pulse_frequency_norm, 0.0, 1.0);
    cfg.pulse_amplitude_norm = clampd(cfg.pulse_amplitude_norm, 0.0, 1.0);
    cfg.pulse_amperage_norm = clampd(cfg.pulse_amperage_norm, 0.0, 1.0);
    cfg.pulse_voltage_norm = clampd(cfg.pulse_voltage_norm, 0.0, 1.0);

    NistSiliconRef nist{};
    const bool nist_ok = load_nist_silicon_reference(nist_ref_path, nist);
    if (!nist_ok) {
        std::cerr << "warn: unable to load NIST silicon reference from " << nist_ref_path.string()
                  << " reason=" << nist.load_error << " (using fallback constants)\n";
        if (cfg.nist_strict) {
            std::cerr << "error: --nist-strict specified; aborting run\n";
            return 2;
        }
    }
    const double lattice_scale = clampd(nist.lattice_constant_m / 5.431020511e-10, 0.50, 1.50);
    const double density_scale = clampd(nist.density_g_cm3 / 2.33, 0.50, 2.00);
    const double excitation_scale = clampd(nist.mean_excitation_energy_ev / 173.0, 0.25, 4.00);
    const double ionization_scale = clampd(nist.first_ionization_energy_ev / 8.15168, 0.25, 4.00);

    const std::array<Seed, 3> seeds{{
        {"D_track", 0.245, 0.19, 0.35, 0.35},
        {"I_accum", 0.1775, 0.19, 0.37, 0.45},
        {"L_smooth", 0.175, 0.19, 0.37, 0.37},
    }};
    std::vector<Packet> packets;
    packets.reserve((size_t)cfg.packet_count);
    for (int p = 0; p < cfg.packet_count; ++p) {
        Packet pk{};
        pk.id = p;
        pk.seed = seeds[(size_t)(p % 3)];
        pk.charge = (p % 4 == 0) ? -2 : (p % 4 == 1 ? -1 : (p % 4 == 2 ? 1 : 2));
        // Inject GPU pulse quartet (F/A/I/V) directly into spectral field drivers.
        pk.amp_drive =
            (0.55 + 1.35 * pk.seed.a) *
            (0.60 + 1.10 * cfg.pulse_amplitude_norm) *
            std::sqrt(density_scale);
        pk.freq_drive =
            (1.0 + 0.35 * pk.seed.f + 0.08 * (double)pk.charge) *
            (0.55 + 1.10 * cfg.pulse_frequency_norm) *
            (0.85 + 0.30 * lattice_scale);
        pk.current_drive =
            (0.25 + 1.30 * pk.seed.i) *
            (0.55 + 1.10 * cfg.pulse_amperage_norm);
        pk.volt_drive =
            (0.30 + 1.50 * pk.seed.v) *
            (0.55 + 1.10 * cfg.pulse_voltage_norm) *
            ionization_scale;
        const double amp_sigma =
            (2.2 + 3.8 * (1.0 - pk.seed.i)) * (0.70 + 1.10 * cfg.pulse_amperage_norm);
        for (int a = 0; a < 3; ++a) {
            pk.spectrum[(size_t)a].resize((size_t)cfg.bin_count);
            const double center =
                cfg.bin_count * (0.15 + 0.55 * pk.seed.f) * (0.90 + 0.25 * cfg.pulse_frequency_norm) +
                (double)p * 0.2 + (double)a * 3.0;
            std::vector<double> amp((size_t)cfg.bin_count, 0.0);
            for (int b = 0; b < cfg.bin_count; ++b) {
                const double x = ((double)b - center) / std::max(1.0e-6, amp_sigma);
                const double h = std::exp(-0.5 * x * x);
                const double bn = (double)b / std::max(1, cfg.bin_count - 1);
                const double envelope = std::exp(-bn * (0.8 + pk.seed.i * density_scale));
                // NIST excitation term keeps silicon wafer characteristics inside the field.
                const double nist_excitation = 0.004 * excitation_scale * std::exp(-bn * (2.5 + (double)a));
                amp[(size_t)b] = pk.amp_drive * h * envelope + nist_excitation;
            }
            amp = blur1d(amp, 1.0 + 1.25 * pk.current_drive);
            for (int b = 0; b < cfg.bin_count; ++b) {
                const double bn = (double)b / std::max(1, cfg.bin_count - 1);
                const double ph = (double)pk.charge * (2.0 * kPi * (double)p / std::max(1, cfg.packet_count))
                                + (double)a * (kPi / 2.0)
                                + pk.freq_drive * (double)(a + 1) * bn * 2.0 * kPi
                                + pk.volt_drive * bn * kPi;
                pk.spectrum[(size_t)a][(size_t)b] = amp[(size_t)b] * cis(ph);
            }
        }
        packets.push_back(std::move(pk));
    }

    const std::vector<double> freq_axis = linspace(1.0, (double)cfg.bin_count, cfg.bin_count);
    std::ostringstream traj_json, traj_csv, class_json, class_csv, tensor_json, tensor_grad_json,
        vec_json, glyph_json, shader_json, audio_json, summary_json, debug_json, recon_json, pulse_effects_json;
    traj_json << "// photon_packet_trajectory_sample.json\n[\n";
    class_json << "// photon_packet_path_classification_sample.json\n[\n";
    tensor_json << "// photon_lattice_tensor6d_sample.json\n[\n";
    tensor_grad_json << "// photon_tensor_gradient_sample.json\n[\n";
    vec_json << "// photon_vector_excitation_sample.json\n[\n";
    glyph_json << "// photon_tensor_gradient_glyph_sample.json\n[\n";
    shader_json << "// photon_shader_texture_sample.json\n[\n";
    audio_json << "// photon_audio_waveform_sample.json\n[\n";
    debug_json << "// packet_frequency_debug.json\n[\n";
    recon_json << "// reconstructed_packet_paths.json\n[\n";
    pulse_effects_json << "// photon_pulse_effects_sample.json\n{\n";
    traj_csv << "// photon_packet_trajectory_sample.csv\npacket_id,timestep,x,y,z,theta,amplitude,freq_x,freq_y,freq_z,phase_coupling,temporal_inertia,curvature,coherence,flux\n";
    class_csv << "// photon_packet_path_classification_sample.csv\npacket_id,classification,group_id,phase_lock_score,curvature_depth,coherence_score\n";

    std::vector<double> shared_score((size_t)cfg.packet_count, 0.0);
    std::vector<double> curvature_mean((size_t)cfg.packet_count, 0.0);
    std::vector<double> coherence_mean((size_t)cfg.packet_count, 0.0);
    std::vector<double> inertia_mean((size_t)cfg.packet_count, 0.0);
    std::vector<std::array<int, 3>> last_bins((size_t)cfg.packet_count);
    std::vector<std::array<double, 3>> last_freq((size_t)cfg.packet_count);
    std::vector<double> last_theta((size_t)cfg.packet_count, 0.0);
    std::vector<double> last_amp((size_t)cfg.packet_count, 0.0);

    auto axis_mean_mag = [&](const Packet& pk, int b) {
        return (std::abs(pk.spectrum[0][(size_t)b]) + std::abs(pk.spectrum[1][(size_t)b]) + std::abs(pk.spectrum[2][(size_t)b])) / 3.0;
    };

    size_t traj_count = 0;
    for (int step = 0; step < cfg.steps; ++step) {
        const std::vector<double> bin_norm = linspace(0.0, 1.0, cfg.bin_count);
        for (int p = 0; p < cfg.packet_count; ++p) {
            double pair_coupling = 0.0;
            if (cfg.packet_count > 1) {
                const int q = (p + 1) % cfg.packet_count;
                pair_coupling = phase_lock(packets[(size_t)p], packets[(size_t)q]);
            }
            for (int ax = 0; ax < 3; ++ax) {
                std::vector<double> mag((size_t)cfg.bin_count, 0.0), loga((size_t)cfg.bin_count, 0.0), logf((size_t)cfg.bin_count, 0.0);
                for (int b = 0; b < cfg.bin_count; ++b) {
                    mag[(size_t)b] = std::abs(packets[(size_t)p].spectrum[(size_t)ax][(size_t)b]);
                    loga[(size_t)b] = std::log(mag[(size_t)b] + 1.0e-9);
                    logf[(size_t)b] = std::log(
                        freq_axis[(size_t)b] *
                        (1.0 + 0.12 * packets[(size_t)p].freq_drive * (0.70 + 0.80 * cfg.pulse_frequency_norm)) +
                        1.0e-6);
                }
                const std::vector<double> dln_a = grad(loga);
                const std::vector<double> dln_f = grad(logf);
                for (int b = 0; b < cfg.bin_count; ++b) {
                    const double bn = bin_norm[(size_t)b];
                    const double vph =
                        packets[(size_t)p].volt_drive *
                        (0.35 + 0.85 * cfg.pulse_voltage_norm) *
                        bn * (0.5 + 0.35 * std::sin((double)step * 0.15));
                    const double couple_phase = cfg.kappa_couple * pair_coupling * (0.5 - bn);
                    packets[(size_t)p].spectrum[(size_t)ax][(size_t)b] *=
                        std::exp(cfg.kappa_a * dln_a[(size_t)b]) *
                        cis(cfg.kappa_f * dln_f[(size_t)b] + vph + couple_phase);
                }
                mag.assign((size_t)cfg.bin_count, 0.0);
                for (int b = 0; b < cfg.bin_count; ++b) mag[(size_t)b] = std::abs(packets[(size_t)p].spectrum[(size_t)ax][(size_t)b]);
                const std::vector<double> blurred = blur1d(mag, 0.95 + 1.35 * packets[(size_t)p].current_drive);
                for (int b = 0; b < cfg.bin_count; ++b) {
                    const double m = clampd(blurred[(size_t)b], 0.0, cfg.max_amplitude);
                    packets[(size_t)p].spectrum[(size_t)ax][(size_t)b] = m * cis(std::arg(packets[(size_t)p].spectrum[(size_t)ax][(size_t)b]));
                }
            }
        }

        for (int p = 0; p < cfg.packet_count; ++p) {
            double best_lock = 0.0;
            for (int q = 0; q < cfg.packet_count; ++q) {
                if (p == q) continue;
                best_lock = std::max(best_lock, phase_lock(packets[(size_t)p], packets[(size_t)q]));
            }
            shared_score[(size_t)p] += best_lock;
        }

        for (int p = 0; p < cfg.packet_count; ++p) {
            const auto pos = ifft3(packets[(size_t)p], cfg.recon_samples);
            const auto vel = std::array<std::vector<double>, 3>{grad(pos[0]), grad(pos[1]), grad(pos[2])};
            const auto acc = std::array<std::vector<double>, 3>{grad(vel[0]), grad(vel[1]), grad(vel[2])};
            const int idx = std::min(cfg.recon_samples - 1, (int)std::llround((double)step * (double)(cfg.recon_samples - 1) / std::max(1, cfg.steps - 1)));
            const double x = pos[0][(size_t)idx], y = pos[1][(size_t)idx], z = pos[2][(size_t)idx];
            const double vx = vel[0][(size_t)idx], vy = vel[1][(size_t)idx], vz = vel[2][(size_t)idx];
            const double ax = acc[0][(size_t)idx], ay = acc[1][(size_t)idx], az = acc[2][(size_t)idx];
            const double theta = (std::arg(packets[(size_t)p].spectrum[0][0]) + std::arg(packets[(size_t)p].spectrum[1][0]) + std::arg(packets[(size_t)p].spectrum[2][0])) / 3.0;
            double amp = 0.0;
            for (int b = 0; b < cfg.bin_count; ++b) amp += axis_mean_mag(packets[(size_t)p], b);
            amp /= (double)cfg.bin_count;
            last_theta[(size_t)p] = theta;
            last_amp[(size_t)p] = amp;
            const double inertia = std::sqrt(ax * ax + ay * ay + az * az);
            const double cx = vy * az - vz * ay, cy = vz * ax - vx * az, cz = vx * ay - vy * ax;
            const double curvature = std::sqrt(cx * cx + cy * cy + cz * cz) / (std::pow(std::sqrt(vx * vx + vy * vy + vz * vz), 3.0) + 1.0e-9);
            const double coh = shared_score[(size_t)p] / (double)(step + 1);
            curvature_mean[(size_t)p] += curvature;
            coherence_mean[(size_t)p] += coh;
            inertia_mean[(size_t)p] += inertia;
            for (int a = 0; a < 3; ++a) {
                int mb = 0;
                double mm = -1.0;
                for (int b = 0; b < cfg.bin_count; ++b) {
                    const double m = std::abs(packets[(size_t)p].spectrum[(size_t)a][(size_t)b]);
                    if (m > mm) { mm = m; mb = b; }
                }
                last_bins[(size_t)p][(size_t)a] = mb;
                last_freq[(size_t)p][(size_t)a] = freq_axis[(size_t)mb];
            }
            const double fx = last_freq[(size_t)p][0], fy = last_freq[(size_t)p][1], fz = last_freq[(size_t)p][2];
            traj_json << "  {\"packet_id\":" << p << ",\"timestep\":" << step
                      << ",\"x\":" << std::setprecision(10) << x << ",\"y\":" << y << ",\"z\":" << z
                      << ",\"theta\":" << theta << ",\"amplitude\":" << amp
                      << ",\"freq_x\":" << fx << ",\"freq_y\":" << fy << ",\"freq_z\":" << fz
                      << ",\"phase_coupling\":" << coh << ",\"temporal_inertia\":" << inertia
                      << ",\"curvature\":" << curvature << ",\"coherence\":" << coh
                      << ",\"flux\":" << (std::sqrt(vx * vx + vy * vy + vz * vz) * amp) << "}";
            traj_json << ((++traj_count < (size_t)cfg.steps * (size_t)cfg.packet_count) ? ",\n" : "\n");
            traj_csv << p << "," << step << "," << x << "," << y << "," << z << ","
                     << theta << "," << amp << "," << fx << "," << fy << "," << fz << ","
                     << coh << "," << inertia << "," << curvature << "," << coh << ","
                     << (std::sqrt(vx * vx + vy * vy + vz * vz) * amp) << "\n";
        }
    }

    for (int p = 0; p < cfg.packet_count; ++p) {
        shared_score[(size_t)p] /= (double)cfg.steps;
        curvature_mean[(size_t)p] /= (double)cfg.steps;
        coherence_mean[(size_t)p] /= (double)cfg.steps;
        inertia_mean[(size_t)p] /= (double)cfg.steps;
    }
    std::vector<double> idx = shared_score;
    std::sort(idx.begin(), idx.end());
    const double threshold = idx[(size_t)std::llround(0.55 * (double)std::max(0, cfg.packet_count - 1))];
    int shared_count = 0;
    for (int p = 0; p < cfg.packet_count; ++p) {
        const bool shared = shared_score[(size_t)p] >= threshold;
        shared_count += shared ? 1 : 0;
        const char* cls = shared ? "shared" : "individual";
        class_json << "  {\"packet_id\":" << p << ",\"classification\":\"" << cls << "\",\"group_id\":" << (shared ? (p % 2) : p)
                   << ",\"phase_lock_score\":" << shared_score[(size_t)p] << ",\"curvature_depth\":" << curvature_mean[(size_t)p]
                   << ",\"coherence_score\":" << coherence_mean[(size_t)p] << "}" << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        class_csv << p << "," << cls << "," << (shared ? (p % 2) : p) << "," << shared_score[(size_t)p] << ","
                  << curvature_mean[(size_t)p] << "," << coherence_mean[(size_t)p] << "\n";
        const double oam = (double)packets[(size_t)p].charge * 0.5 * coherence_mean[(size_t)p];
        const auto final_pos = ifft3(packets[(size_t)p], cfg.recon_samples);
        const auto phase_g0 = grad([&]{
            std::vector<double> v((size_t)cfg.bin_count, 0.0);
            for (int b = 0; b < cfg.bin_count; ++b) v[(size_t)b] = std::arg(packets[(size_t)p].spectrum[0][(size_t)b]);
            return v;
        }());
        const auto amp_g0 = grad([&]{
            std::vector<double> v((size_t)cfg.bin_count, 0.0);
            for (int b = 0; b < cfg.bin_count; ++b) v[(size_t)b] = std::abs(packets[(size_t)p].spectrum[0][(size_t)b]);
            return v;
        }());
        const double phase_grad_mean = std::accumulate(phase_g0.begin(), phase_g0.end(), 0.0) / std::max(1, cfg.bin_count);
        const double amp_grad_mean = std::accumulate(amp_g0.begin(), amp_g0.end(), 0.0) / std::max(1, cfg.bin_count);
        tensor_json << "  {\"x\":" << p << ",\"y\":" << last_bins[(size_t)p][0] << ",\"z\":" << last_bins[(size_t)p][1]
                    << ",\"phase_coherence\":" << coherence_mean[(size_t)p] << ",\"curvature\":" << curvature_mean[(size_t)p]
                    << ",\"flux\":" << (last_amp[(size_t)p] * shared_score[(size_t)p]) << ",\"inertia\":" << inertia_mean[(size_t)p]
                    << ",\"freq_x\":" << last_freq[(size_t)p][0] << ",\"freq_y\":" << last_freq[(size_t)p][1] << ",\"freq_z\":" << last_freq[(size_t)p][2]
                    << ",\"dtheta_dt\":0.0,\"d2theta_dt2\":0.0,\"oam_twist\":" << oam
                    << ",\"spin_vector\":[0,0,1],\"higgs_inertia\":" << std::max(0.0, inertia_mean[(size_t)p] * shared_score[(size_t)p] - curvature_mean[(size_t)p]) << "}"
                    << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        tensor_grad_json << "  {\"packet_id\":" << p << ",\"bin_center\":" << ((last_bins[(size_t)p][0] + last_bins[(size_t)p][1] + last_bins[(size_t)p][2]) / 3)
                         << ",\"tensor\":[[1,0,0],[0,1,0],[0,0,1]],\"phase_gradient\":[" << phase_grad_mean << "," << phase_grad_mean << "," << phase_grad_mean
                         << "],\"amplitude_gradient\":[" << amp_grad_mean << "," << amp_grad_mean << "," << amp_grad_mean
                         << "],\"oam_twist\":" << oam << ",\"temporal_inertia\":" << inertia_mean[(size_t)p] << "}"
                         << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        vec_json << "  {\"x\":" << p << ",\"y\":0,\"z\":0,\"vec_x\":" << last_freq[(size_t)p][0]
                 << ",\"vec_y\":" << last_freq[(size_t)p][1] << ",\"vec_z\":" << last_freq[(size_t)p][2]
                 << ",\"spin\":[0,0,1],\"oam_twist\":" << oam << "}" << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        glyph_json << "  {\"x\":" << p << ",\"y\":" << last_bins[(size_t)p][0] << ",\"z\":" << last_bins[(size_t)p][2]
                   << ",\"tensor\":[[1,0,0],[0,1,0],[0,0,1]],\"color\":[" << clampd(0.2 + 0.7 * coherence_mean[(size_t)p], 0.0, 1.0)
                   << "," << clampd(0.15 + 0.65 * std::abs(oam), 0.0, 1.0) << "," << clampd(0.2 + 0.5 * std::abs(oam), 0.0, 1.0) << "]}"
                   << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        shader_json << "  {\"x\":" << p << ",\"y\":" << last_bins[(size_t)p][0] << ",\"z\":" << last_bins[(size_t)p][1]
                    << ",\"rgb\":[" << clampd(last_amp[(size_t)p] / cfg.max_amplitude, 0.0, 1.0)
                    << "," << clampd((std::sin(last_theta[(size_t)p]) + 1.0) * 0.5, 0.0, 1.0)
                    << "," << clampd(std::abs(oam) * 4.0, 0.0, 1.0) << "]}"
                    << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        std::vector<double> top_mag((size_t)cfg.bin_count, 0.0);
        for (int b = 0; b < cfg.bin_count; ++b) top_mag[(size_t)b] = axis_mean_mag(packets[(size_t)p], b);
        const int b10 = std::min(10, cfg.bin_count - 1);
        debug_json << "  {\"packet_id\":" << p << ",\"cohort\":\"" << packets[(size_t)p].seed.name
                   << "\",\"dominant_bin\":" << ((last_bins[(size_t)p][0] + last_bins[(size_t)p][1] + last_bins[(size_t)p][2]) / 3)
                   << ",\"bin10_mag\":" << top_mag[(size_t)b10]
                   << ",\"bin10_phase_rad\":" << ((std::arg(packets[(size_t)p].spectrum[0][(size_t)b10]) + std::arg(packets[(size_t)p].spectrum[1][(size_t)b10]) + std::arg(packets[(size_t)p].spectrum[2][(size_t)b10])) / 3.0)
                   << ",\"vector_curl_deg\":0.0,\"color_hex\":\"#66aaff\",\"top_bins\":[]}"
                   << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
        recon_json << "  {\"packet_id\":" << p << ",\"cohort\":\"" << packets[(size_t)p].seed.name << "\",\"points\":[";
        for (int i = 0; i < cfg.recon_samples; ++i) {
            recon_json << "{\"x\":" << final_pos[0][(size_t)i] << ",\"y\":" << final_pos[1][(size_t)i] << ",\"z\":" << final_pos[2][(size_t)i] << "}";
            if (i + 1 < cfg.recon_samples) recon_json << ",";
        }
        recon_json << "]}" << ((p + 1 < cfg.packet_count) ? ",\n" : "\n");
    }

    for (int i = 0; i < cfg.recon_samples; ++i) {
        const double t = (1.0 / 60.0) * ((double)i / std::max(1, cfg.recon_samples - 1));
        const double s0 = std::sin(2.0 * kPi * 220.0 * t);
        const double s1 = std::sin(2.0 * kPi * 330.0 * t);
        const double s2 = std::sin(2.0 * kPi * 440.0 * t);
        const double s3 = 0.5 * (s0 + s1);
        audio_json << "  {\"time\":" << t << ",\"channels\":[" << s0 << "," << s1 << "," << s2 << "," << s3 << "]}"
                   << ((i + 1 < cfg.recon_samples) ? ",\n" : "\n");
    }

    traj_json << "]"; class_json << "]"; tensor_json << "]"; tensor_grad_json << "]"; vec_json << "]";
    glyph_json << "]"; shader_json << "]"; audio_json << "]"; debug_json << "]"; recon_json << "]";

    double low_freq_ratio_accum = 0.0;
    for (int p = 0; p < cfg.packet_count; ++p) {
        double low = 0.0;
        double total = 0.0;
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < cfg.bin_count; ++b) {
                const double e = std::norm(packets[(size_t)p].spectrum[(size_t)a][(size_t)b]);
                total += e;
                if (b < cfg.low_bin_count) low += e;
            }
        }
        low_freq_ratio_accum += (total > 0.0) ? (low / total) : 0.0;
    }
    const double low_freq_ratio = low_freq_ratio_accum / std::max(1, cfg.packet_count);
    const double mean_shared_score =
        std::accumulate(shared_score.begin(), shared_score.end(), 0.0) / std::max(1, cfg.packet_count);
    const double mean_inertia =
        std::accumulate(inertia_mean.begin(), inertia_mean.end(), 0.0) / std::max(1, cfg.packet_count);
    const double mean_curvature =
        std::accumulate(curvature_mean.begin(), curvature_mean.end(), 0.0) / std::max(1, cfg.packet_count);

    const double trap_target = clampd(0.48 + 0.30 * cfg.pulse_amperage_norm + 0.12 * density_scale - 0.08 * cfg.pulse_frequency_norm, 0.0, 1.0);
    const double coherence_target = clampd(0.52 + 0.20 * density_scale + 0.08 * cfg.pulse_amplitude_norm - 0.10 * cfg.pulse_voltage_norm, 0.0, 1.0);
    const double trap_match = clampd(1.0 - std::abs(low_freq_ratio - trap_target), 0.0, 1.0);
    const double coherence_match = clampd(1.0 - std::abs(mean_shared_score - coherence_target), 0.0, 1.0);
    const double lattice_match = clampd(1.0 - std::abs(lattice_scale - 1.0) / 0.5, 0.0, 1.0);
    const double silicon_reproduction_score =
        clampd(0.45 * trap_match + 0.35 * coherence_match + 0.20 * lattice_match, 0.0, 1.0);
    const std::string nist_path_json = json_escape(nist_ref_path.string());

    pulse_effects_json
        << "  \"simulation_mode\": \"nist_silicon_wafer_with_injected_gpu_pulse_quartet\",\n"
        << "  \"gpu_pulse_quartet\": {\"frequency\": " << cfg.pulse_frequency_norm
        << ", \"amplitude\": " << cfg.pulse_amplitude_norm
        << ", \"amperage\": " << cfg.pulse_amperage_norm
        << ", \"voltage\": " << cfg.pulse_voltage_norm << "},\n"
        << "  \"nist_reference\": {\n"
        << "    \"path\": \"" << nist_path_json << "\",\n"
        << "    \"loaded\": " << (nist.loaded ? "true" : "false") << ",\n"
        << "    \"lattice_constant_m\": " << nist.lattice_constant_m << ",\n"
        << "    \"density_g_cm3\": " << nist.density_g_cm3 << ",\n"
        << "    \"mean_excitation_energy_ev\": " << nist.mean_excitation_energy_ev << ",\n"
        << "    \"first_ionization_energy_ev\": " << nist.first_ionization_energy_ev << "\n"
        << "  },\n"
        << "  \"field_response\": {\n"
        << "    \"low_frequency_trap_ratio\": " << low_freq_ratio << ",\n"
        << "    \"mean_temporal_coherence\": " << mean_shared_score << ",\n"
        << "    \"mean_temporal_inertia\": " << mean_inertia << ",\n"
        << "    \"mean_curvature\": " << mean_curvature << "\n"
        << "  },\n"
        << "  \"silicon_reproduction_score\": " << silicon_reproduction_score << "\n"
        << "}\n";

    const int indiv_count = cfg.packet_count - shared_count;
    summary_json << "// frequency_domain_run_summary.json\n{\n"
                 << "  \"mode\": \"frequency_domain_packets_only_nist_wafer\",\n"
                 << "  \"description\": \"Headless spectral silicon-wafer photon confinement with injected GPU pulse quartet field terms.\",\n"
                 << "  \"packet_count\": " << cfg.packet_count << ",\n"
                 << "  \"bin_count\": " << cfg.bin_count << ",\n"
                 << "  \"steps\": " << cfg.steps << ",\n"
                 << "  \"recon_samples\": " << cfg.recon_samples << ",\n"
                 << "  \"equivalent_grid_linear\": " << cfg.equivalent_grid_linear << ",\n"
                 << "  \"voxel_grid_materialized\": false,\n"
                 << "  \"fft_backend\": \"cpp_dft_ifft\",\n"
                 << "  \"gpu_pulse_quartet\": {\"frequency\": " << cfg.pulse_frequency_norm
                 << ",\"amplitude\": " << cfg.pulse_amplitude_norm
                 << ",\"amperage\": " << cfg.pulse_amperage_norm
                 << ",\"voltage\": " << cfg.pulse_voltage_norm << "},\n"
                 << "  \"nist_reference\": {\"path\": \"" << nist_path_json << "\",\"loaded\": " << (nist.loaded ? "true" : "false")
                 << ",\"lattice_constant_m\": " << nist.lattice_constant_m
                 << ",\"density_g_cm3\": " << nist.density_g_cm3 << "},\n"
                 << "  \"aggregate_metrics\": {\"mean_shared_score\": "
                 << mean_shared_score
                 << ",\"low_frequency_trap_ratio\": " << low_freq_ratio
                 << ",\"silicon_reproduction_score\": " << silicon_reproduction_score
                 << ",\"packet_class_counts\": {\"shared\": " << shared_count << ",\"individual\": " << indiv_count << "}}\n"
                 << "}\n";

    auto write_all = [&](const std::filesystem::path& dst) {
        write_txt(dst / "photon_packet_trajectory_sample.json", traj_json.str());
        write_txt(dst / "photon_packet_trajectory_sample.csv", traj_csv.str());
        write_txt(dst / "photon_packet_path_classification_sample.json", class_json.str());
        write_txt(dst / "photon_packet_path_classification_sample.csv", class_csv.str());
        write_txt(dst / "photon_lattice_tensor6d_sample.json", tensor_json.str());
        write_txt(dst / "photon_tensor_gradient_sample.json", tensor_grad_json.str());
        write_txt(dst / "photon_vector_excitation_sample.json", vec_json.str());
        write_txt(dst / "photon_tensor_gradient_glyph_sample.json", glyph_json.str());
        write_txt(dst / "photon_shader_texture_sample.json", shader_json.str());
        write_txt(dst / "photon_audio_waveform_sample.json", audio_json.str());
        write_txt(dst / "frequency_domain_run_summary.json", summary_json.str());
        write_txt(dst / "photon_pulse_effects_sample.json", pulse_effects_json.str());
        write_txt(dst / "packet_frequency_debug.json", debug_json.str());
        write_txt(dst / "reconstructed_packet_paths.json", recon_json.str());
        write_txt(dst / "debug_view.html", "<html><body><h1>Photon Frequency Debug</h1></body></html>");

        std::vector<uint8_t> vsd;
        const uint32_t base_record_count = 8u; // edge, scale, f_code, a_code, i_code, v_code, nist_lattice, nist_density
        append_u32(vsd, 0x31545356u); append_u32(vsd, 1u); append_u32(vsd, base_record_count + (uint32_t)cfg.packet_count);
        append_blob(vsd, std::string("photon/volume/edge")); append_blob(vsd, std::string("u64")); append_blob(vsd, std::string("expanded lattice edge hint"));
        std::vector<uint8_t> edge; append_u64(edge, (uint64_t)cfg.equivalent_grid_linear); append_blob(vsd, edge);
        append_blob(vsd, std::string("photon/volume/scale")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("normalized volume scale"));
        std::vector<uint8_t> scale(sizeof(double), 0u); const double sv = 1.35; std::memcpy(scale.data(), &sv, sizeof(double)); append_blob(vsd, scale);
        append_blob(vsd, std::string("photon/runtime/f_code")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("pulse frequency norm"));
        std::vector<uint8_t> fcode(sizeof(double), 0u); std::memcpy(fcode.data(), &cfg.pulse_frequency_norm, sizeof(double)); append_blob(vsd, fcode);
        append_blob(vsd, std::string("photon/runtime/a_code")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("pulse amplitude norm"));
        std::vector<uint8_t> acode(sizeof(double), 0u); std::memcpy(acode.data(), &cfg.pulse_amplitude_norm, sizeof(double)); append_blob(vsd, acode);
        append_blob(vsd, std::string("photon/runtime/i_code")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("pulse amperage norm"));
        std::vector<uint8_t> icode(sizeof(double), 0u); std::memcpy(icode.data(), &cfg.pulse_amperage_norm, sizeof(double)); append_blob(vsd, icode);
        append_blob(vsd, std::string("photon/runtime/v_code")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("pulse voltage norm"));
        std::vector<uint8_t> vcode(sizeof(double), 0u); std::memcpy(vcode.data(), &cfg.pulse_voltage_norm, sizeof(double)); append_blob(vsd, vcode);
        append_blob(vsd, std::string("photon/nist/lattice_constant_m")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("nist lattice constant"));
        std::vector<uint8_t> nlat(sizeof(double), 0u); std::memcpy(nlat.data(), &nist.lattice_constant_m, sizeof(double)); append_blob(vsd, nlat);
        append_blob(vsd, std::string("photon/nist/density_g_cm3")); append_blob(vsd, std::string("f64")); append_blob(vsd, std::string("nist density"));
        std::vector<uint8_t> ndens(sizeof(double), 0u); std::memcpy(ndens.data(), &nist.density_g_cm3, sizeof(double)); append_blob(vsd, ndens);
        for (int p = 0; p < cfg.packet_count; ++p) {
            std::ostringstream k; k << "photon/tensor/" << std::setw(4) << std::setfill('0') << p;
            append_blob(vsd, k.str()); append_blob(vsd, std::string("freq_tensor_q16"));
            append_blob(vsd, std::string("packet=" + std::to_string(p))); std::vector<uint8_t> payload(16u, 0u); append_blob(vsd, payload);
        }
        std::filesystem::create_directories(dst);
        std::ofstream f(dst / "photon_volume_expansion.gevsd", std::ios::binary | std::ios::trunc);
        f.write((const char*)vsd.data(), (std::streamsize)vsd.size());
    };

    write_all(out_dir);
    if (write_root) write_all(root);
    std::cout << "frequency-domain run complete: " << out_dir.string() << "\n";
    std::cout << "shared packets=" << shared_count << "/" << cfg.packet_count << "\n";
    std::cout << "nist loaded=" << (nist.loaded ? "true" : "false")
              << " lattice_constant_m=" << nist.lattice_constant_m
              << " density_g_cm3=" << nist.density_g_cm3 << "\n";
    std::cout << "gpu_pulse_quartet F=" << cfg.pulse_frequency_norm
              << " A=" << cfg.pulse_amplitude_norm
              << " I=" << cfg.pulse_amperage_norm
              << " V=" << cfg.pulse_voltage_norm << "\n";
    std::cout << "silicon_reproduction_score=" << silicon_reproduction_score << "\n";
    return 0;
}
