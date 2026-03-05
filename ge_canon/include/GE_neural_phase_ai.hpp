#pragma once

#include <cstdint>
#include <vector>

#include <iosfwd>

// -----------------------------------------------------------------------------
//  Neural Phase AI (Spec/Blueprint: neural phase dynamics controller)
// -----------------------------------------------------------------------------
// Minimal deterministic cognition loop inside the substrate.
//
// Mechanism (exact):
//   1) Observe committed substrate state (anchors + lanes).
//   2) Derive a 9D coordinate fold (sig9_u64) from those observables.
//   3) Classify sig9 into a discrete class id.
//   4) Maintain a small attractor memory (sig9 buckets + strength).
//   5) Emit bounded internal control as standard pulses injected through the
//      same inbound admission path as external inputs.
//
// Constraints:
//   - Integer / fixed-point only.
//   - Deterministic ordering; no platform variance.
//   - No networking, no file IO.
//   - Bookkeeping uses coordinate folds (no security semantics).

struct EwAiStatus {
    uint64_t tick_u64;
    uint32_t class_id_u32;
    uint32_t reserved0_u32;
    int64_t  confidence_q32_32;
    uint64_t sig9_u64;
};

struct EwAttractorEntry {
    uint64_t sig9_u64;
    int64_t  strength_q32_32;
    uint64_t last_tick_u64;
};

class SubstrateMicroprocessor;

class EwNeuralPhaseAI {
public:
    EwNeuralPhaseAI();

    void init(uint64_t projection_seed);

    // Pre-tick: internal control actions (bounded) before candidate evolution.
    void pre_tick(SubstrateMicroprocessor* sm);

    // Post-tick: update memory + classification from committed state.
    void post_tick(SubstrateMicroprocessor* sm);

    const EwAiStatus& status() const { return status_; }

    // Most recent attractor strength for the current sig9 context.
    int64_t last_attractor_strength_q32_32() const { return last_strength_q32_32_; }

    // Deterministic binary snapshot for autosave/rehydration.
    // This is strictly a state dump (no codegen, no compression).
    bool serialize_binary(std::ostream& out) const;
    bool deserialize_binary(std::istream& in);

private:
    uint64_t seed_u64_;
    std::vector<EwAttractorEntry> mem_;
    EwAiStatus status_;
    int64_t last_strength_q32_32_ = 0;

    static uint64_t sig9_fold(uint64_t acc, uint64_t x);
    static uint64_t sig9_from_state(const SubstrateMicroprocessor* sm);
    static uint32_t class_id_from_sig9(uint64_t sig9_u64);
    static int64_t  confidence_from_state(const SubstrateMicroprocessor* sm);
    int64_t attractor_strength_for_sig9(uint64_t sig9_u64) const;
};
