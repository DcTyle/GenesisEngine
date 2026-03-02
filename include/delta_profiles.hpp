#pragma once
#include <cstdint>

// Delta encoding profiles (Spec Section 3.6).
// Profiles are versioned constants that select:
//  - axis weights wi (Q10, sum ~ 1024)
//  - normalization denominators denom_q per axis (TURN_SCALE domain)
//  - harmonic mapping behavior (bucket size, weights law)

enum EwDeltaProfileId : uint8_t {
    EW_PROFILE_CORE_EVOLUTION = 0,
    EW_PROFILE_LANGUAGE_INJECTION = 1,
    EW_PROFILE_CRAWLER_INGESTION = 2,
    EW_PROFILE_IMAGE = 3,
    EW_PROFILE_AUDIO = 4,
};

struct EwDeltaProfile {
    uint8_t profile_id;
    int32_t weights_q10[9];
    int64_t denom_q[9];
    // Harmonic selection parameters (Spec 3.7/3.8).
    uint16_t mode_bucket_size;
};

// Populate out_profile for the requested id. Returns 0 on success, non-zero on failure.
int ew_get_delta_profile(uint8_t profile_id, EwDeltaProfile* out_profile);
