#include <stdint.h>

static const long long TURN_SCALE = 1000000000000000000LL;
static const int F_SCALE = 1 << 20;
static const int F_MIN = -(1 << 30);
static const int F_MAX = (1 << 30) - 1;
static const unsigned short A_MAX = 65535;
static const unsigned short V_MAX = 65535;
static const unsigned short I_MAX = 65535;

__device__ __forceinline__ long long wrap_turns(long long theta) {
    long long mod = theta % TURN_SCALE;
    if (mod < 0) mod += TURN_SCALE;
    return mod;
}

__device__ __forceinline__ long long delta_turns(long long a, long long b) {
    long long d = a - b;
    d %= TURN_SCALE;
    if (d >= TURN_SCALE / 2) d -= TURN_SCALE;
    if (d < -TURN_SCALE / 2) d += TURN_SCALE;
    return d;
}

__device__ __forceinline__ long long round_half_even_ll(long long value, long long divisor) {
    long long q = value / divisor;
    long long r = value % divisor;
    if (r > divisor / 2) return q + 1;
    if (r < divisor / 2) return q;
    return (q % 2 == 0) ? q : q + 1;
}

__device__ __forceinline__ int clamp_i32(int v) {
    if (v < F_MIN) return F_MIN;
    if (v > F_MAX) return F_MAX;
    return v;
}

__device__ __forceinline__ unsigned short clamp_u16(unsigned int v) {
    return (v > A_MAX) ? A_MAX : (unsigned short)v;
}

extern "C" __global__
void evolve_kernel(const long long* proj_d,   // 9*count packed (axis-major)
                   const int* weights_q10,    // 9 weights (sum 1024)
                   const long long* denom_q,  // 9 denominators
                   long long* theta_q,
                   long long* chi_q,
                   long long* m_q,
                   unsigned long long* tau_q,
                   int* f_code_out,
                   unsigned short* a_code_out,
                   unsigned short* v_code_out,
                   unsigned short* i_code_out,
                   long long* leak_out,
                   int count,
                   unsigned long long canonical_tick)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;

    long long theta = theta_q[i];
    long long chi = chi_q[i];
    long long mass = m_q[i];

    long long basis9_d[9];
    #pragma unroll
    for (int k = 0; k < 9; ++k) basis9_d[k] = 0;

    basis9_d[3] = (long long)canonical_tick * TURN_SCALE;
    basis9_d[4] = theta;
    basis9_d[5] = chi;
    basis9_d[8] = mass;

    long long delta9[9];
    delta9[0] = proj_d[0 * count + i] - basis9_d[0];
    delta9[1] = proj_d[1 * count + i] - basis9_d[1];
    delta9[2] = proj_d[2 * count + i] - basis9_d[2];
    delta9[3] = proj_d[3 * count + i] - basis9_d[3];
    delta9[4] = delta_turns(proj_d[4 * count + i], basis9_d[4]);
    delta9[5] = proj_d[5 * count + i] - basis9_d[5];
    delta9[6] = proj_d[6 * count + i] - basis9_d[6];
    delta9[7] = proj_d[7 * count + i] - basis9_d[7];
    delta9[8] = proj_d[8 * count + i] - basis9_d[8];

    long long s_q20 = 0;
    #pragma unroll
    for (int k = 0; k < 9; ++k) {
        long long denom = denom_q[k];
        if (denom <= 0) denom = 1;
        long long scaled = delta9[k] * 1024LL;
        long long xi = round_half_even_ll(scaled, denom);
        if (xi < -1024) xi = -1024;
        if (xi >  1024) xi =  1024;
        s_q20 += (long long)weights_q10[k] * xi;
    }

    long long f_ll = round_half_even_ll(s_q20 * (long long)F_SCALE, 1024LL * 1024LL);
    int f_code = clamp_i32((int)f_ll);

    unsigned int u = (unsigned int)(chi % TURN_SCALE);
    unsigned short a_code = clamp_u16(u % (unsigned int)A_MAX);

    // Voltage/current carrier observables (deterministic proxies).
    unsigned int chi_abs = (unsigned int)((chi < 0) ? -chi : chi);
    unsigned int f_abs = (unsigned int)((f_code < 0) ? -f_code : f_code);
    unsigned int v_u = (chi_abs % (unsigned int)TURN_SCALE) + (f_abs << 6) + ((unsigned int)a_code << 2);
    unsigned short v_code = clamp_u16(v_u % (unsigned int)V_MAX);

    unsigned int theta_abs = (unsigned int)((theta < 0) ? -theta : theta);
    unsigned int i_u = ((theta_abs >> 3) + (chi_abs >> 4) + ((unsigned int)a_code << 1));
    unsigned short i_code = clamp_u16(i_u % (unsigned int)I_MAX);

    long long delta_theta = (long long)f_code * (TURN_SCALE / (long long)F_SCALE);
    theta = wrap_turns(theta + delta_theta);

    chi += (long long)a_code * 1000LL;
    if (chi < 0) chi = 0;

    chi = chi / 2;
    if (chi < 0) chi = 0;

    long long leak = (mass * 1000LL) / TURN_SCALE;
    mass -= leak;
    if (mass < 0) mass = 0;

    theta_q[i] = theta;
    chi_q[i] = chi;
    m_q[i] = mass;
    tau_q[i] = canonical_tick;

    f_code_out[i] = f_code;
    a_code_out[i] = a_code;
    v_code_out[i] = v_code;
    i_code_out[i] = i_code;
    leak_out[i] = leak;
}
