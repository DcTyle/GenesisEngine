#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct GE_LaneThresholdPoint {
    uint32_t epoch_u32 = 0;
    int64_t rel_err_max_q32_32 = 0; // accept if <= this
};

struct GE_LaneThresholdSchedule {
    uint8_t lane_u8 = 0;
    std::vector<GE_LaneThresholdPoint> points;

    // Deterministic: choose last point with epoch <= current_epoch; if none, use first; if empty, return default.
    int64_t rel_err_limit_for_epoch_q32_32(uint32_t current_epoch_u32, int64_t default_limit_q32_32) const;
};

struct GE_AllLaneThresholds {
    std::vector<GE_LaneThresholdSchedule> lanes;
    const GE_LaneThresholdSchedule* find_lane(uint8_t lane_u8) const;
};

// Deterministic plain-text parser.
// Format (ASCII):
// lane=<u8>
// epoch=<u32> rel_err_max=<q32.32_as_float>
// Blank lines and '#' comments ignored.
bool GE_load_lane_thresholds_from_file(const std::string& path_utf8, GE_AllLaneThresholds* out);
