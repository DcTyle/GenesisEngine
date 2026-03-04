#include "GE_object_ancilla.hpp"

#include <algorithm>
#include <string>

#include "GE_object_memory.hpp"
#include "GE_runtime.hpp"
#include "GE_uv_atlas_baker.hpp"
#include "anchor.hpp"
#include "fixed_point.hpp"

#include "GE_object_local_cuda.hpp"

namespace {

static inline uint16_t clamp_q15_u16(int64_t v) {
    if (v < 0) v = 0;
    if (v > 32768) v = 32768;
    return (uint16_t)v;
}

static inline uint16_t q15_from_ratio_u64(uint64_t num, uint64_t den) {
    if (den == 0) return 0;
    // Q15 = 32768 * num/den.
    const __uint128_t n = (__uint128_t)num * (__uint128_t)32768u;
    uint64_t q = (uint64_t)(n / (__uint128_t)den);
    if (q > 32768u) q = 32768u;
    return (uint16_t)q;
}

static inline int64_t turns_from_centered_q15(uint16_t q15) {
    // Map q15 [0..32768] -> centered [-1..1] turns.
    const int64_t c = (int64_t)q15 - 16384;
    // Scale into TURN_SCALE domain.
    return (c * (int64_t)TURN_SCALE) / 16384;
}

static inline Pulse mk_pulse(uint32_t aid, int32_t f_code, uint16_t a_code, uint16_t v_code, uint16_t i_code, uint8_t profile, uint8_t causal, uint64_t tick) {
    Pulse p{};
    p.anchor_id = aid;
    p.f_code = f_code;
    p.a_code = a_code;
    p.v_code = v_code;
    p.i_code = i_code;
    p.profile_id = profile;
    p.causal_tag = causal;
    p.pad0 = 0;
    p.pad1 = 0;
    p.tick = tick;
    return p;
}

} // namespace

namespace genesis {

void EwObjectAncilla::ensure_canonical_global_ancillas(SubstrateManager* sm) {
    if (!sm) return;
    // Ensure at least ids 0..2 exist.
    const uint32_t need = 3u;
    if (sm->anchors.size() < need) {
        const uint32_t old = (uint32_t)sm->anchors.size();
        sm->anchors.reserve(need);
        for (uint32_t i = old; i < need; ++i) sm->anchors.emplace_back(i);
    }
    if (sm->ancilla.size() < need) sm->ancilla.resize(need);
    if (sm->redirect_to.size() < need) sm->redirect_to.resize(need, 0u);
    if (sm->split_child_a.size() < need) sm->split_child_a.resize(need, 0u);
    if (sm->split_child_b.size() < need) sm->split_child_b.resize(need, 0u);
    if (sm->next_anchor_id_u32 < need) sm->next_anchor_id_u32 = need;

    // Tag canonical global ancillas.
    sm->anchors[LEDGER_ANCILLA_ID].kind_u32 = EW_ANCHOR_KIND_CONTEXT_ROOT; // semantic: global authority
    sm->anchors[LEDGER_ANCILLA_ID].context_id_u32 = 0u;
    sm->anchors[LEDGER_ANCILLA_ID].object_id_u64 = 0u;

    sm->anchors[OBJECTS_ANCILLA_ID].kind_u32 = EW_ANCHOR_KIND_SYLLABUS_ROOT; // semantic: global object scheduler
    sm->anchors[OBJECTS_ANCILLA_ID].context_id_u32 = 0u;
    sm->anchors[OBJECTS_ANCILLA_ID].object_id_u64 = 0u;
}

uint32_t EwObjectAncilla::ensure_object_update_anchor(SubstrateManager* sm, uint64_t object_id_u64) {
    if (!sm) return 0u;
    ensure_canonical_global_ancillas(sm);

    // Deterministic policy: use existing anchor if already bound to this object.
    // Search is O(n) but bounded by anchor count; can be optimized later.
    for (uint32_t i = 0; i < (uint32_t)sm->anchors.size(); ++i) {
        if (sm->anchors[i].object_id_u64 == object_id_u64 && sm->anchors[i].kind_u32 == EW_ANCHOR_KIND_DOC_ROOT) {
            return i;
        }
    }

    // Allocate a new anchor id deterministically.
    const uint32_t id = sm->next_anchor_id_u32++;
    if (id >= sm->anchors.size()) sm->anchors.emplace_back(id);
    if (id >= sm->ancilla.size()) sm->ancilla.resize((size_t)id + 1);
    if (id >= sm->redirect_to.size()) sm->redirect_to.resize((size_t)id + 1, 0u);
    if (id >= sm->split_child_a.size()) sm->split_child_a.resize((size_t)id + 1, 0u);
    if (id >= sm->split_child_b.size()) sm->split_child_b.resize((size_t)id + 1, 0u);

    Anchor& a = sm->anchors[id];
    a.kind_u32 = EW_ANCHOR_KIND_DOC_ROOT; // re-use: "doc" kind as a stable per-object leaf (no version churn)
    a.context_id_u32 = OBJECTS_ANCILLA_ID;
    a.object_id_u64 = object_id_u64;
    const EwObjectEntry* e = sm->object_store.find(object_id_u64);
    if (e) a.object_phase_seed_u64 = e->phase_seed_u64;
    return id;
}

bool EwObjectAncilla::compute_coupling_obs_gpu(SubstrateManager* sm,
                                               uint64_t object_id_u64,
                                               EwObjectCouplingObs& out_obs) {
    out_obs = EwObjectCouplingObs{};
    out_obs.object_id_u64 = object_id_u64;

    EwVoxelVolumeView vv;
    if (!sm->object_store.view_voxel_volume(object_id_u64, vv)) {
        return false;
    }
    if (vv.format_u32 != 1u || !vv.bytes || vv.byte_count == 0) {
        return false;
    }

    // Object-local sublattice state: phi_q15_s16 per voxel.
    uint32_t lgx=0,lgy=0,lgz=0,lfmt=0;
    const int16_t* phi_in = nullptr;
    size_t phi_bytes = 0;

    std::vector<int16_t> phi_init;
    if (!sm->object_store.view_local_phi(object_id_u64, lgx, lgy, lgz, lfmt, phi_in, phi_bytes) ||
        lfmt != 1u || !phi_in || phi_bytes != vv.byte_count * 2u ||
        lgx != vv.grid_x_u32 || lgy != vv.grid_y_u32 || lgz != vv.grid_z_u32) {
        // Deterministic initialization: phi = centered occupancy.
        const size_t nvox = vv.byte_count;
        phi_init.resize(nvox);
        for (size_t i = 0; i < nvox; ++i) {
            const uint8_t o = vv.bytes[i];
            // Map [0..255] -> [0..32767], then center to [-16384..+16383].
            const int32_t q = (int32_t)((uint32_t)o * 32767u) / 255u;
            phi_init[i] = (int16_t)(q - 16384);
        }
        (void)sm->object_store.upsert_local_phi_q15_s16(object_id_u64, vv.grid_x_u32, vv.grid_y_u32, vv.grid_z_u32,
                                                        phi_init.data(), phi_init.size() * sizeof(int16_t));
        // Re-view.
        if (!sm->object_store.view_local_phi(object_id_u64, lgx, lgy, lgz, lfmt, phi_in, phi_bytes)) return false;
    }

    const size_t nvox = vv.byte_count;
    std::vector<int16_t> phi_out;
    phi_out.resize(nvox);

    // Bidirectional boundary exchange: sample world lattice means around the
    // object center and feed them as boundary biases into the object-local step.
    genesis::EwWorldBoundaryBiasQ15 world_bias{};
    if (EwFieldLatticeGpu* wl = sm->world_lattice_gpu_for_learning()) {
        const uint32_t gxw = wl->grid_x();
        const uint32_t gyw = wl->grid_y();
        const uint32_t gzw = wl->grid_z();
        const EwObjectEntry* e = sm->object_store.find(object_id_u64);
        if (e && gxw && gyw && gzw) {
            auto q32_32_to_i32_round = [](uint64_t q)->int32_t {
                const int64_t s = (int64_t)q;
                const int64_t r = (s >= 0) ? (s + (1ll << 31)) : (s - (1ll << 31));
                return (int32_t)(r >> 32);
            };
            int32_t cx = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[0]) + (int32_t)(gxw / 2u);
            int32_t cy = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[1]) + (int32_t)(gyw / 2u);
            int32_t cz = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[2]) + (int32_t)(gzw / 2u);
            if (cx < 0) cx = 0; if (cx >= (int32_t)gxw) cx = (int32_t)gxw - 1;
            if (cy < 0) cy = 0; if (cy >= (int32_t)gyw) cy = (int32_t)gyw - 1;
            if (cz < 0) cz = 0; if (cz >= (int32_t)gzw) cz = (int32_t)gzw - 1;
            // Small deterministic neighborhood.
            const EwBoundarySampleQ15 s = wl->sample_boundary_means_q15_box((uint32_t)cx, (uint32_t)cy, (uint32_t)cz, 1u, 1u, 1u);
            world_bias.e_curr_q15 = s.e_curr_q15;
            world_bias.flux_q15 = s.flux_q15;
            world_bias.coherence_q15 = s.coherence_q15;
            world_bias.curvature_q15 = s.curvature_q15;
            world_bias.doppler_q15 = s.doppler_q15;

            // Store a Q0.15 magnitude for later use (lighting diffusion/noise dominance).
            const int32_t fg = (int32_t)s.flux_grad_q15;
            const int32_t fga = (fg < 0) ? -fg : fg;
            out_obs.world_flux_grad_mean_q15 = (uint16_t)clamp_u32((uint32_t)fga, 0u, 32767u);
        }
    }

    genesis::EwObjectLocalStepStats st;
    if (!ge_cuda_object_local_step_q15(vv.bytes, phi_in, vv.grid_x_u32, vv.grid_y_u32, vv.grid_z_u32,
                                       world_bias,
                                       phi_out.data(), st)) {
        return false;
    }

    // Persist updated local state.
    (void)sm->object_store.upsert_local_phi_q15_s16(object_id_u64, vv.grid_x_u32, vv.grid_y_u32, vv.grid_z_u32,
                                                    phi_out.data(), phi_out.size() * sizeof(int16_t));

    // Density mean: occ_sum / (255*vox_count).
    const uint64_t denom = st.vox_count_u64 * 255ull;
    const uint16_t mean_q15 = q15_from_ratio_u64(st.occ_sum_u64, (denom == 0 ? 1ull : denom));
    const uint64_t bden = st.boundary_count_u64 * 255ull;
    const uint16_t bmean_q15 = q15_from_ratio_u64(st.boundary_occ_sum_u64, (bden == 0 ? 1ull : bden));
    out_obs.density_mean_q15 = mean_q15;
    out_obs.boundary_density_q15 = bmean_q15;

    // Curvature derived from boundary gradient magnitude.
    // Normalize grad_sum by (count * 6 * 32768) to Q15-ish then center.
    uint16_t grad_q15 = 0;
    if (st.boundary_grad_count_u64) {
        const __uint128_t n = (__uint128_t)st.boundary_grad_sum_u64 * (__uint128_t)32768u;
        const uint64_t d = st.boundary_grad_count_u64 * 6ull * 32768ull;
        uint64_t q = (uint64_t)(n / (__uint128_t)(d ? d : 1ull));
        if (q > 32768u) q = 32768u;
        grad_q15 = (uint16_t)q;
    }
    out_obs.curvature_turns_q = turns_from_centered_q15(grad_q15);

    // Doppler is derived from change in mean phi (stored in the object's ancilla as Q32.32).
    // We store the current mean phi (Q32.32) in obs.doppler_turns_q as a carrier; the
    // apply step computes the delta against the previous stored mean.
    const int64_t phi_mean_q32_32 = (st.phi_count_u64 == 0) ? 0 : (int64_t)(((__int128)st.phi_sum_i64 << 32) / (__int128)st.phi_count_u64);
    out_obs.doppler_turns_q = phi_mean_q32_32;
    return true;
}

void EwObjectAncilla::apply_obs_to_anchor_and_emit(SubstrateManager* sm,
                                                   uint32_t object_anchor_id_u32,
                                                   const EwObjectCouplingObs& obs) {
    if (!sm || object_anchor_id_u32 >= sm->anchors.size()) return;
    Anchor& a = sm->anchors[object_anchor_id_u32];
    // Doppler is stored in obs.doppler_turns_q as phi_mean_q32_32 from compute step.
    const int64_t phi_mean_q32_32 = obs.doppler_turns_q;
    const int64_t prev_phi_mean_q32_32 = sm->ancilla[object_anchor_id_u32].convergence_metric_q32_32;
    sm->ancilla[object_anchor_id_u32].convergence_metric_q32_32 = phi_mean_q32_32;
    const int64_t dphi_q32_32 = phi_mean_q32_32 - prev_phi_mean_q32_32;
    // Map delta into TURN_SCALE domain with a small fixed gain (deterministic).
    const int64_t doppler_turns_q = (dphi_q32_32 * (int64_t)TURN_SCALE) / (1ll << 32) / 1024;

    // Update derived terms deterministically.
    a.update_derived_terms(obs.curvature_turns_q, doppler_turns_q);
    a.world_flux_grad_mean_q15 = obs.world_flux_grad_mean_q15;
    a.sync_basis9_from_core();

    // Refresh UV atlas: write G/B from curvature/doppler proxies.
    uint32_t w = 0, h = 0, fmt = 0;
    const uint8_t* bytes = nullptr;
    size_t nbytes = 0;
    if (sm->object_store.view_uv_atlas(a.object_id_u64, w, h, fmt, bytes, nbytes) && fmt == 1u && bytes && nbytes == (size_t)w * (size_t)h * 4u) {
        std::vector<uint8_t> tmp(bytes, bytes + nbytes);
        const uint8_t g = (uint8_t)clamp_u32((uint32_t)((obs.density_mean_q15 * 255u) / 32768u), 0u, 255u);
        const uint8_t b = (uint8_t)clamp_u32((uint32_t)((obs.boundary_density_q15 * 255u) / 32768u), 0u, 255u);
        for (size_t i = 0; i + 3 < tmp.size(); i += 4) {
            // Keep R and A; update G/B.
            tmp[i + 1] = g;
            tmp[i + 2] = b;
        }
        (void)sm->object_store.upsert_uv_atlas_rgba8(a.object_id_u64, w, h, tmp.data(), tmp.size());
    }

    // Emit bounded fan-out pulses (3 lanes: 0/1/2) into inbound.
    // Encode amplitude from density; use curvature sign for f_code.
    const uint16_t a_code = (uint16_t)clamp_u32((uint32_t)((obs.density_mean_q15 * 65535u) / 32768u), 0u, 65535u);
    const uint16_t v_code = (uint16_t)clamp_u32((uint32_t)((obs.boundary_density_q15 * V_MAX) / 32768u), 0u, V_MAX);
    const uint16_t i_code = (uint16_t)clamp_u32((uint32_t)((obs.boundary_density_q15 * I_MAX) / 32768u), 0u, I_MAX);
    const int32_t f0 = (obs.curvature_turns_q < 0) ? -F_SCALE/4 : F_SCALE/4;

    const uint64_t t = sm->canonical_tick;
    // Profile 3 = split mapping in injection stage.
    if (sm->inbound.size() < 4096) {
        sm->inbound.push_back(mk_pulse(object_anchor_id_u32, f0, a_code, v_code, i_code, 3u, 0u, t));
        sm->inbound.push_back(mk_pulse(object_anchor_id_u32, -f0, a_code, v_code, i_code, 3u, 1u, t));
        sm->inbound.push_back(mk_pulse(object_anchor_id_u32, f0/2, a_code, v_code, i_code, 3u, 2u, t));
    }
}

void EwObjectAncilla::tick_object_updates(SubstrateManager* sm, uint32_t max_objects_per_tick) {
    if (!sm) return;
    ensure_canonical_global_ancillas(sm);
    if (max_objects_per_tick == 0) return;

    std::vector<uint64_t> ids;
    sm->object_store.list_object_ids_sorted(ids);
    if (ids.empty()) return;

    // Record which objects were updated this tick (bounded, deterministic).
    sm->object_updates_last_tick_u64.clear();

    // Deterministic round-robin start index based on canonical tick.
    const uint64_t tick = sm->canonical_tick;
    const uint32_t start = (uint32_t)(tick % (uint64_t)ids.size());
    uint32_t done = 0;
    for (uint32_t k = 0; k < (uint32_t)ids.size() && done < max_objects_per_tick; ++k) {
        const uint32_t idx = (start + k) % (uint32_t)ids.size();
        const uint64_t oid = ids[idx];
        const uint32_t aid = ensure_object_update_anchor(sm, oid);
        EwObjectCouplingObs obs;
        if (!compute_coupling_obs_gpu(sm, oid, obs)) {
            // Fail closed: if object update cannot be computed on GPU,
            // do not attempt CPU emulation here.
            continue;
        }
        apply_obs_to_anchor_and_emit(sm, aid, obs);
        sm->object_updates_last_tick_u64.push_back(oid);
        ++done;
    }
}

} // namespace genesis
