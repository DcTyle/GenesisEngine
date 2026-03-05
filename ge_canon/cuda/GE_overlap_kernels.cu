#include <cuda_runtime.h>
#include <stdint.h>

static __device__ __forceinline__ int64_t ge_abs_i64_dev(int64_t x) { return x < 0 ? -x : x; }

struct GeCarrierDev {
    int64_t f_q32_32;
    int64_t phi_q32_32;
    int64_t a_q32_32;
    int64_t v_q32_32;
    int32_t f_code,a_code,v_code,i_code;
    uint8_t lane_u8;
    uint32_t domain_lane[9];
};

static __device__ __forceinline__ bool ge_id9_eq(const uint32_t* a, const uint32_t* b) {
    #pragma unroll
    for (int i=0;i<9;i++) if (a[i]!=b[i]) return false;
    return true;
}

static __device__ __forceinline__ int64_t ge_score(const GeCarrierDev& q, const GeCarrierDev& r) {
    const int64_t base = (int64_t(1) << 32);
    const int64_t df = ge_abs_i64_dev(q.f_q32_32 - r.f_q32_32);
    const int64_t dphi = ge_abs_i64_dev(q.phi_q32_32 - r.phi_q32_32);
    const int64_t da = ge_abs_i64_dev(q.a_q32_32 - r.a_q32_32);
    const int64_t dv = ge_abs_i64_dev(q.v_q32_32 - r.v_q32_32);

    // Conservative penalty; deterministic fixed-point.
    int64_t penalty = (df >> 2) + (dphi >> 2) + (da >> 3) + (dv >> 3);
    int64_t score = base - penalty;
    if (score < 0) score = 0;
    return score;
}

extern "C" __global__ void ge_overlap_scores_kernel(const GeCarrierDev q, const GeCarrierDev* recs, int64_t* out, size_t n,
                                                    uint8_t lane_filter, const uint32_t* opt_domain_id9) {
    const size_t idx = (size_t)blockIdx.x * (size_t)blockDim.x + (size_t)threadIdx.x;
    if (idx >= n) return;

    const GeCarrierDev r = recs[idx];
    if (r.lane_u8 != lane_filter) { out[idx] = 0; return; }
    if (opt_domain_id9 && !ge_id9_eq(r.domain_lane, opt_domain_id9)) { out[idx] = 0; return; }

    out[idx] = ge_score(q, r);
}
