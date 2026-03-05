#include <cuda_runtime.h>
#include <stdint.h>

static constexpr int64_t GE_TOPK_SENTINEL_SCORE = (int64_t)0x8000000000000000LL; // INT64_MIN
static constexpr uint32_t GE_TOPK_SENTINEL_INDEX = 0xFFFFFFFFu;

static __device__ __forceinline__ bool ge_better(int64_t score_a, uint32_t idx_a, int64_t score_b, uint32_t idx_b) {
    // Descending score, then ascending index.
    if (score_a > score_b) return true;
    if (score_a < score_b) return false;
    return idx_a < idx_b;
}

static __device__ __forceinline__ void ge_read_list(const uint32_t* in_idx, const int64_t* in_score,
                                                    uint32_t lists, uint32_t list_id,
                                                    uint32_t k, uint32_t pos,
                                                    uint32_t* out_idx, int64_t* out_score) {
    if (list_id >= lists || pos >= k) {
        *out_idx = GE_TOPK_SENTINEL_INDEX;
        *out_score = GE_TOPK_SENTINEL_SCORE;
        return;
    }
    *out_idx = in_idx[list_id * k + pos];
    *out_score = in_score[list_id * k + pos];
}

extern "C" __global__ void ge_topk_block_kernel(const int64_t* scores, uint32_t n,
                                                uint32_t k,
                                                uint32_t chunk,
                                                uint32_t* out_idx,
                                                int64_t* out_score) {
    if (threadIdx.x != 0) return;
    const uint32_t bid = (uint32_t)blockIdx.x;
    const uint32_t start = bid * chunk;
    const uint32_t end = (start + chunk < n) ? (start + chunk) : n;

    // Initialize top-k with sentinel (never wins against real scores)
    uint32_t base = bid * k;
    for (uint32_t i = 0; i < k; ++i) {
        out_idx[base + i] = GE_TOPK_SENTINEL_INDEX;
        out_score[base + i] = GE_TOPK_SENTINEL_SCORE;
    }

    // Scalar scan
    for (uint32_t i = start; i < end; ++i) {
        int64_t s = scores[i];
        if (s <= 0) continue;

        // Insert into sorted list
        for (uint32_t pos = 0; pos < k; ++pos) {
            int64_t cur_s = out_score[base + pos];
            uint32_t cur_i = out_idx[base + pos];
            if (!ge_better(s, i, cur_s, cur_i)) continue;

            // shift down
            for (uint32_t j = k - 1; j > pos; --j) {
                out_score[base + j] = out_score[base + j - 1];
                out_idx[base + j] = out_idx[base + j - 1];
            }
            out_score[base + pos] = s;
            out_idx[base + pos] = i;
            break;
        }
    }
}

extern "C" __global__ void ge_topk_merge_kernel(const uint32_t* in_idx, const int64_t* in_score,
                                                uint32_t lists, uint32_t k,
                                                uint32_t* out_idx, int64_t* out_score) {
    if (threadIdx.x != 0) return;
    uint32_t out_list = (uint32_t)blockIdx.x;
    uint32_t a = out_list * 2;
    uint32_t b = a + 1;

    // Merge list a and b into out_list
    uint32_t out_base = out_list * k;

    uint32_t ia = 0, ib = 0;

    for (uint32_t o = 0; o < k; ++o) {
        int64_t sa = 0, sb = 0;
        uint32_t xa = 0, xb = 0;
        ge_read_list(in_idx, in_score, lists, a, k, ia, &xa, &sa);
        ge_read_list(in_idx, in_score, lists, b, k, ib, &xb, &sb);

        bool take_a = ge_better(sa, xa, sb, xb);

        if (take_a) {
            out_score[out_base + o] = sa;
            out_idx[out_base + o] = xa;
            if (a < lists && ia + 1 < k) ia++;
            else { /* keep */ }
        } else {
            out_score[out_base + o] = sb;
            out_idx[out_base + o] = xb;
            if (b < lists && ib + 1 < k) ib++;
            else { /* keep */ }
        }
    }
}
