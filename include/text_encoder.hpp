#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

// Canonical 9D embedding and aggregation for UTF-8 text.
// Implements EigenWareSpec:
// - 17.4 Canonical codepoint -> 9D embedding
// - 17.5 Aggregate file displacement
//
// Determinism closure note: the canonical math uses sin/cos.
// This implementation evaluates sin/cos via deterministic integer CORDIC
// (ew_cordic_sincos_q32_32), avoiding platform-dependent transcendentals.

struct E9d {
    double v[9];
};

// -----------------------------------------------------------------------------
//  Canonical normalization + segmentation helpers (Blueprint registry helpers)
// -----------------------------------------------------------------------------
// normalize_text:
//  - Deterministic byte-stable normalization for input text.
//  - Collapses ASCII whitespace runs to a single space.
//  - Converts CRLF/CR to LF.
//  - Does not attempt Unicode normalization; non-ASCII bytes are preserved.
std::string normalize_text(const std::string& utf8);

// segment_text_blocks:
//  - Deterministic segmentation into blocks.
//  - Splits on two or more consecutive newlines ("\n\n+"), preserving order.
//  - Empty blocks are discarded.
std::vector<std::string> segment_text_blocks(const std::string& normalized_utf8);

// Convert a UTF-8 byte stream to aggregated 9D displacement S_F.
// - Invalid UTF-8 sequences are replaced with U+FFFD.
// - No segmentation: the entire stream is aggregated deterministically.
E9d ew_text_aggregate_utf8_to_SF(const std::string& utf8);

// Canonical aggregate file displacement (17.5).
// Computes the normalized 9D displacement for a UTF‑8 byte stream.
// This function sums the per-codepoint embeddings and then scales by the
// reciprocal of the Euclidean norm of the aggregate.  If the norm is zero
// (e.g. empty input), the zero vector is returned.
E9d ew_file_aggregate_utf8_to_SF(const std::string& utf8);

// Coherence‑weighted inner product of two 9D vectors.
// Implements Omega.2.2: P(a,b) = sum_{i=0..8} a_i * b_i * w_i,
// where w_i = 1 for i != 5 and w_5 = |a_5|.  This formalizes
// coherence‑weighted interaction in the canonical math.
static inline double P_coherence_weighted(const E9d& a, const E9d& b) {
    double sum = 0.0;
    const double w5 = std::fabs(a.v[5]);
    for (int i = 0; i < 9; ++i) {
        const double wi = (i == 5) ? w5 : 1.0;
        sum += a.v[i] * b.v[i] * wi;
    }
    return sum;
}

// -----------------------------------------------------------------------------
//  Blueprint 3.3.1 symbol-phase primitive encoder (ring semantics)
// -----------------------------------------------------------------------------
// Deterministically maps bytes to phase bins on a fixed ring (N_sym = 256).
// Returns a TURN_SCALE-domain cumulative phase push for TEXT->x injection.
// No global uniform normalization is performed.
int64_t ew_text_utf8_to_phase_ring_push_turns(const std::string& utf8);

// -----------------------------------------------------------------------------
//  Spec 5.11.3/5.11.4 + Spec 3.5 spider-graph compression path
// -----------------------------------------------------------------------------
// Returns a signed frequency code derived from UTF-8 bytes using the canonical
// spider graph encoder under the requested delta profile.
//
// This is the preferred TEXT injection encoding when TEXT is treated as a
// frequency driver (f_code) rather than a direct phase push.
//
// Determinism: uses only byte->phase mapping, shortest-wrap deltas, fixed-point
// quantization, and the existing integer spider encoder on Anchor.
int32_t ew_text_utf8_to_frequency_code(const std::string& utf8, uint8_t profile_id);
