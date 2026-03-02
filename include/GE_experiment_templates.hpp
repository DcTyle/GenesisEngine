#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EigenWare {

// Deterministic experiment request parsed from a UI text line.
// This is a thin, stable schema that compiles into operator packets.
struct EwExperimentRequest {
    // Template name (lowercase ASCII, '-' and '_' allowed).
    std::string name;

    // Microsteps to run on the selected lattice (world or probe).
    uint32_t micro_ticks_u32 = 0;

    // Visualization tag.
    bool tag_render = false;
    uint32_t slice_z_u32 = 0;
    uint32_t stride_u32 = 2;
    uint32_t max_points_u32 = 50000;
    uint32_t intensity_min_u8 = 8;

    // Deterministic amplitude derived from the prompt line.
    int64_t amp_q32_32 = 0;

    // Optional pattern parameters (injection coordinates).
    uint32_t x_u32 = 0;
    uint32_t y_u32 = 0;
    uint32_t z_u32 = 0;

    // Optional probe isolate origin in world lattice coordinates.
    uint32_t origin_x_u32 = 0;
    uint32_t origin_y_u32 = 0;
    uint32_t origin_z_u32 = 0;
    uint32_t pattern_kind_u32 = 0;
    uint32_t pattern_radius_u32 = 0;

    // Graph parameters (Q32.32) for graph_* templates.
    // For graph_1d: y = a*x + b on [xmin..xmax] sampled at samples.
    int64_t graph_a_q32_32 = 0;
    int64_t graph_b_q32_32 = 0;
    int64_t graph_xmin_q32_32 = 0;
    int64_t graph_xmax_q32_32 = 0;
    uint32_t graph_samples_u32 = 0;

    // Graph_2d parameters (Q32.32): f(x,y) = ax*x + by*y + c.
    int64_t graph2_ax_q32_32 = 0;
    int64_t graph2_by_q32_32 = 0;
    int64_t graph2_c_q32_32 = 0;
    int64_t graph2_xmin_q32_32 = 0;
    int64_t graph2_xmax_q32_32 = 0;
    int64_t graph2_ymin_q32_32 = 0;
    int64_t graph2_ymax_q32_32 = 0;
    uint32_t graph2_samples_x_u32 = 0;
    uint32_t graph2_samples_y_u32 = 0;

    // Pemdas visualization expression (ASCII, no spaces recommended).
    std::string expr_ascii;
};

// Parse a UI text line into an experiment request.
// Recognized forms:
//   /exp <name> [k=v ...]
//   exp:<name> [k=v ...]
// Returns true on success.
bool ew_parse_experiment_request(const std::string& utf8_line, EwExperimentRequest& out);

// Compile an experiment request into one or more operator packets (1500-byte each).
// Packets are returned in deterministic exec_order.
bool ew_compile_experiment_to_operator_packets(const EwExperimentRequest& req,
                                               std::vector<std::vector<uint8_t>>& out_packets);

} // namespace EigenWare
