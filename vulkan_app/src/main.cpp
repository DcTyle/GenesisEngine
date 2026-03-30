
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include "GE_app.hpp"

static bool ew_wstr_has_flag(const wchar_t* cmd, const wchar_t* flag) {
    if (!cmd || !flag) return false;
    return (wcsstr(cmd, flag) != nullptr);
}

static std::string ew_wstr_parse_utf8_opt(const wchar_t* cmd, const wchar_t* key, const std::string& defv) {
    if (!cmd || !key) return defv;
    const wchar_t* p = wcsstr(cmd, key);
    if (!p) return defv;
    p += wcslen(key);
    if (*p == L'=' || *p == L':') p++;
    const wchar_t* e = p;
    while (*e != 0 && *e != L' ' && *e != L'\t') ++e;
    if (e == p) return defv;
    const int wn = (int)(e - p);
    int n = WideCharToMultiByte(CP_UTF8, 0, p, wn, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return defv;
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, p, wn, out.data(), n, nullptr, nullptr);
    return out;
}

static uint32_t ew_wstr_parse_u32_opt(const wchar_t* cmd, const wchar_t* key, uint32_t defv) {
    if (!cmd || !key) return defv;
    const wchar_t* p = wcsstr(cmd, key);
    if (!p) return defv;
    p += wcslen(key);
    if (*p == L'=' || *p == L':') p++;
    wchar_t* endp = nullptr;
    unsigned long v = wcstoul(p, &endp, 10);
    if (endp == p) return defv;
    return (uint32_t)v;
}

static inline int64_t q32_32_mul(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

static inline int64_t q32_32_div_i64(int64_t num_q32_32, int64_t den_i64) {
    if (den_i64 == 0) return 0;
    __int128 p = (__int128)num_q32_32;
    p = (p << 32) / (__int128)den_i64;
    return (int64_t)p;
}

static inline uint64_t ew_isqrt_u128(unsigned __int128 x) {
    // Deterministic integer sqrt for up to 128-bit values.
    // Returns floor(sqrt(x)).
    unsigned __int128 res = 0;
    unsigned __int128 bit = (unsigned __int128)1 << 126;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint64_t)res;
}

static inline int64_t q32_32_sqrt_from_q64_64(__int128 r2_q64_64) {
    if (r2_q64_64 <= 0) return 0;
    const unsigned __int128 u = (unsigned __int128)r2_q64_64;
    const uint64_t s = ew_isqrt_u128(u);
    return (int64_t)s;
}

static inline int64_t q32_32_inv(int64_t x_q32_32) {
    if (x_q32_32 == 0) return 0;
    __int128 n = (__int128)1 << 64; // 1.0 in Q32.32 lifted
    return (int64_t)(n / (__int128)x_q32_32);
}

struct EwCelestialBodyState {
    uint64_t object_id_u64 = 0;
    int64_t mass_solar_q32_32 = 0;
    int64_t mu_au3_day2_q32_32 = 0;
    int64_t pos_au_q32_32[3] = {0,0,0};
    int64_t vel_au_per_day_q32_32[3] = {0,0,0};
};

static void ew_celestial_seed_threebody_sun_earth_moon(EwCelestialBodyState out_bodies[3]) {
    auto q = [&](double v)->int64_t {
        const double s = v * 4294967296.0;
        return (int64_t)s; // trunc
    };

    // Gauss gravitational constant in AU^(3/2)/day.
    const double k = 0.01720209895;
    const double mu_sun = k * k;
    const double m_sun = 1.0;
    const double m_earth = 3.003e-6;
    const double m_moon = 3.694e-8;

    EwCelestialBodyState sun{};
    sun.object_id_u64 = 0x534E554CULL; // 'SUNL'
    sun.mass_solar_q32_32 = q(m_sun);
    sun.mu_au3_day2_q32_32 = q(mu_sun);

    EwCelestialBodyState earth{};
    earth.object_id_u64 = 0x45525448ULL; // 'ERTH'
    earth.mass_solar_q32_32 = q(m_earth);
    earth.mu_au3_day2_q32_32 = q(mu_sun * m_earth);
    earth.pos_au_q32_32[0] = q(1.0);
    earth.vel_au_per_day_q32_32[1] = q(k);

    EwCelestialBodyState moon{};
    moon.object_id_u64 = 0x4D4F4F4EULL; // 'MOON'
    moon.mass_solar_q32_32 = q(m_moon);
    moon.mu_au3_day2_q32_32 = q(mu_sun * m_moon);
    const double moon_r_au = 384400.0 / 149597870.7;
    const double moon_v_au_day = (1.022 * 86400.0) / 149597870.7;
    moon.pos_au_q32_32[0] = q(1.0 + moon_r_au);
    moon.vel_au_per_day_q32_32[1] = q(k + moon_v_au_day);

    // Deterministic order by object_id (sun, earth, moon based on ids above)
    out_bodies[0] = sun;
    out_bodies[1] = earth;
    out_bodies[2] = moon;
}

static int64_t ew_celestial_total_energy_q32_32(const EwCelestialBodyState* bodies, uint32_t n) {
    if (!bodies || n == 0) return 0;
    __int128 E_q64_64 = 0;

    // Kinetic
    for (uint32_t i = 0; i < n; ++i) {
        const auto& b = bodies[i];
        const int64_t vx = b.vel_au_per_day_q32_32[0];
        const int64_t vy = b.vel_au_per_day_q32_32[1];
        const int64_t vz = b.vel_au_per_day_q32_32[2];
        __int128 v2 = (__int128)vx*vx + (__int128)vy*vy + (__int128)vz*vz; // Q64.64
        __int128 mv2 = (__int128)b.mass_solar_q32_32 * v2; // Q96.96
        __int128 kin_q32_32 = (mv2 >> 64) / 2;
        E_q64_64 += (kin_q32_32 << 32);
    }

    // Potential
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            const auto& a = bodies[i];
            const auto& b = bodies[j];
            const int64_t dx = b.pos_au_q32_32[0] - a.pos_au_q32_32[0];
            const int64_t dy = b.pos_au_q32_32[1] - a.pos_au_q32_32[1];
            const int64_t dz = b.pos_au_q32_32[2] - a.pos_au_q32_32[2];
            __int128 r2_q64_64 = (__int128)dx*dx + (__int128)dy*dy + (__int128)dz*dz;
            const int64_t eps_q32_32 = 4; // ~9e-10 AU
            r2_q64_64 += (__int128)eps_q32_32 * (__int128)eps_q32_32;
            const int64_t r_q32_32 = q32_32_sqrt_from_q64_64(r2_q64_64);
            if (r_q32_32 <= 0) continue;
            const int64_t inv_r_q32_32 = q32_32_inv(r_q32_32);

            __int128 t0 = (__int128)a.mass_solar_q32_32 * (__int128)b.mu_au3_day2_q32_32;
            __int128 t1 = (__int128)b.mass_solar_q32_32 * (__int128)a.mu_au3_day2_q32_32;
            __int128 num_q64_64 = t0 + t1;
            __int128 pot_q64_64 = (num_q64_64 * (__int128)inv_r_q32_32) >> 32;
            E_q64_64 -= pot_q64_64;
        }
    }

    return (int64_t)(E_q64_64 >> 32);
}

static void ew_celestial_step_verlet(EwCelestialBodyState* bodies, uint32_t n, int64_t dt_days_q32_32) {
    if (!bodies || n == 0) return;
    const int64_t half_dt_q32_32 = dt_days_q32_32 / 2;

    // Acceleration in AU/day^2 (Q32.32)
    int64_t acc[3*16];
    int64_t acc2[3*16];
    if (n > 16) n = 16;

    auto compute_acc = [&](int64_t* out_acc) {
        for (uint32_t i = 0; i < n*3; ++i) out_acc[i] = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const auto& bi = bodies[i];
            for (uint32_t j = 0; j < n; ++j) {
                if (i == j) continue;
                const auto& bj = bodies[j];
                const int64_t dx = bj.pos_au_q32_32[0] - bi.pos_au_q32_32[0];
                const int64_t dy = bj.pos_au_q32_32[1] - bi.pos_au_q32_32[1];
                const int64_t dz = bj.pos_au_q32_32[2] - bi.pos_au_q32_32[2];
                __int128 r2_q64_64 = (__int128)dx*dx + (__int128)dy*dy + (__int128)dz*dz;
                const int64_t eps_q32_32 = 4;
                r2_q64_64 += (__int128)eps_q32_32 * (__int128)eps_q32_32;
                const int64_t r_q32_32 = q32_32_sqrt_from_q64_64(r2_q64_64);
                if (r_q32_32 <= 0) continue;
                const int64_t inv_r_q32_32 = q32_32_inv(r_q32_32);
                const int64_t inv_r2_q32_32 = q32_32_mul(inv_r_q32_32, inv_r_q32_32);
                const int64_t inv_r3_q32_32 = q32_32_mul(inv_r2_q32_32, inv_r_q32_32);

                const int64_t mu = bj.mu_au3_day2_q32_32;
                out_acc[i*3+0] += q32_32_mul(mu, q32_32_mul(dx, inv_r3_q32_32));
                out_acc[i*3+1] += q32_32_mul(mu, q32_32_mul(dy, inv_r3_q32_32));
                out_acc[i*3+2] += q32_32_mul(mu, q32_32_mul(dz, inv_r3_q32_32));
            }
        }
    };

    compute_acc(acc);

    // v_{n+1/2}, x_{n+1}
    for (uint32_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        for (int k = 0; k < 3; ++k) {
            const int64_t v_half = b.vel_au_per_day_q32_32[k] + q32_32_mul(acc[i*3+k], half_dt_q32_32);
            b.pos_au_q32_32[k] += q32_32_mul(v_half, dt_days_q32_32);
            b.vel_au_per_day_q32_32[k] = v_half;
        }
    }

    compute_acc(acc2);

    // v_{n+1}
    for (uint32_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        for (int k = 0; k < 3; ++k) {
            b.vel_au_per_day_q32_32[k] = b.vel_au_per_day_q32_32[k] + q32_32_mul(acc2[i*3+k], half_dt_q32_32);
        }
    }
}

static int ew_run_stability_test(uint32_t ticks, uint32_t max_drift_ppm) {
    // Long-run stability harness for a symplectic N-body integrator.
    // This is runtime-only and Vulkan-free.
    EwCelestialBodyState bodies[3];
    ew_celestial_seed_threebody_sun_earth_moon(bodies);

    // dt = 1/360 s converted to days.
    const double dt_s = 1.0 / 360.0;
    const int64_t dt_seconds_q32_32 = (int64_t)(dt_s * 4294967296.0);
    const int64_t dt_days_q32_32 = q32_32_div_i64(dt_seconds_q32_32, 86400);

    const int64_t E0 = ew_celestial_total_energy_q32_32(bodies, 3);
    for (uint32_t t = 0; t < ticks; ++t) {
        ew_celestial_step_verlet(bodies, 3, dt_days_q32_32);
    }
    const int64_t E1 = ew_celestial_total_energy_q32_32(bodies, 3);

    const int64_t dE = (E1 >= E0) ? (E1 - E0) : (E0 - E1);
    const int64_t absE0 = (E0 >= 0) ? E0 : -E0;
    if (absE0 == 0) {
        std::fprintf(stderr, "STAB_FAIL: E0==0\n");
        return 32;
    }
    const long long drift_ppm = (long long)(((__int128)dE * 1000000) / (__int128)absE0);
    std::fprintf(stdout, "STAB_OK: ticks=%u E0=%lld E1=%lld drift_ppm=%lld (limit=%u)\n",
                 ticks, (long long)E0, (long long)E1, drift_ppm, max_drift_ppm);
    if ((uint32_t)drift_ppm > max_drift_ppm) {
        std::fprintf(stderr, "STAB_FAIL: drift_ppm=%lld > %u\n", drift_ppm, max_drift_ppm);
        return 33;
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    const wchar_t* cmd = GetCommandLineW();

#if defined(GENESIS_GAME_BUILD) && !defined(GENESIS_EDITOR_BUILD)
    if (ew_wstr_has_flag(cmd, L"--stability_test") || ew_wstr_has_flag(cmd, L"--stability-test")) {
        const uint32_t ticks = ew_wstr_parse_u32_opt(cmd, L"--ticks", 86400u);
        const uint32_t ppm = ew_wstr_parse_u32_opt(cmd, L"--max_drift_ppm", 2000u);
        return ew_run_stability_test(ticks, ppm);
    }
#endif

    ewv::AppConfig cfg;
#if defined(GENESIS_GAME_BUILD) && !defined(GENESIS_EDITOR_BUILD)
    cfg.app_title_utf8 = "Genesis Game";
#else
    cfg.app_title_utf8 = "Genesis Editor";
#endif
    cfg.initial_width = 1600;
    cfg.initial_height = 900;
    cfg.stov_mode = ew_wstr_has_flag(cmd, L"--stov") || ew_wstr_has_flag(cmd, L"--stov_mode=1");
    cfg.stov_data_log_path_utf8 = ew_wstr_parse_utf8_opt(cmd, L"--stov_data_log", "");
    cfg.stov_audio_out_path_utf8 = ew_wstr_parse_utf8_opt(cmd, L"--stov_audio_out", "");

    ewv::App app(cfg);
    return app.Run(hInst);
}
