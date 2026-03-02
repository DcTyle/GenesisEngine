#include "delta_profiles.hpp"
#include "fixed_point.hpp"

static void fill_profile(EwDeltaProfile* p,
                         uint8_t profile_id,
                         const int32_t w_q10[9],
                         const int64_t denom_q[9],
                         uint16_t bucket) {
    p->profile_id = profile_id;
    for (int i = 0; i < 9; ++i) {
        p->weights_q10[i] = w_q10[i];
        p->denom_q[i] = denom_q[i];
    }
    p->mode_bucket_size = bucket;
}

int ew_get_delta_profile(uint8_t profile_id, EwDeltaProfile* out_profile) {
    if (!out_profile) return 1;

    // Denominators are TURN_SCALE domain bounds used to map deltas into [-1,1].
    // These are conservative defaults and must remain constants for replay.
    static const int64_t denom_core[9] = {
        TURN_SCALE, TURN_SCALE, TURN_SCALE,
        TURN_SCALE, TURN_SCALE,
        TURN_SCALE, TURN_SCALE, TURN_SCALE, TURN_SCALE
    };
    static const int64_t denom_lang[9] = {
        TURN_SCALE / 2, TURN_SCALE / 2, TURN_SCALE / 2,
        TURN_SCALE, TURN_SCALE,
        TURN_SCALE / 2, TURN_SCALE, TURN_SCALE, TURN_SCALE
    };
    static const int64_t denom_crawl[9] = {
        TURN_SCALE * 2, TURN_SCALE * 2, TURN_SCALE * 2,
        TURN_SCALE, TURN_SCALE,
        TURN_SCALE * 2, TURN_SCALE * 2, TURN_SCALE * 2, TURN_SCALE
    };

    // Q10 weights sum ~ 1024.
    static const int32_t w_core[9] = {
        64, 64, 64, 64,
        256, 192,
        128, 128, 64
    };
    static const int32_t w_lang[9] = {
        192, 64, 64, 64,
        320, 128,
        64, 64, 64
    };
    static const int32_t w_crawl[9] = {
        32, 32, 32, 64,
        192, 128,
        192, 192, 160
    };

    // Bucket size is a deterministic selector used by the harmonic mapper.
    const uint16_t bucket = 64;

    // Image/audio profiles share the same denominator bounds as the core
    // evolution profile, but emphasize their primary modality axis.
    static const int32_t w_image[9] = {
        32, 192, 32, 64,
        256, 160,
        96, 96, 96
    };
    static const int32_t w_audio[9] = {
        32, 32, 192, 64,
        256, 160,
        96, 96, 96
    };

    if (profile_id == EW_PROFILE_CORE_EVOLUTION) {
        fill_profile(out_profile, profile_id, w_core, denom_core, bucket);
        return 0;
    }
    if (profile_id == EW_PROFILE_LANGUAGE_INJECTION) {
        fill_profile(out_profile, profile_id, w_lang, denom_lang, bucket);
        return 0;
    }
    if (profile_id == EW_PROFILE_CRAWLER_INGESTION) {
        fill_profile(out_profile, profile_id, w_crawl, denom_crawl, bucket);
        return 0;
    }

    if (profile_id == EW_PROFILE_IMAGE) {
        fill_profile(out_profile, profile_id, w_image, denom_core, bucket);
        return 0;
    }
    if (profile_id == EW_PROFILE_AUDIO) {
        fill_profile(out_profile, profile_id, w_audio, denom_core, bucket);
        return 0;
    }

    // Unknown profile id.
    return 2;
}
