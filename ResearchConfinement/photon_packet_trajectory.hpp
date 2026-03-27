// photon_packet_trajectory.hpp
// Data structure for per-packet tracking and trajectory logging
#pragma once
#include <cstdint>
#include <vector>
#include <array>

struct PhotonPacketTrajectoryEntry {
    uint64_t packet_id;
    uint32_t timestep;
    float x, y, z;
    float theta;
    float amplitude;
    float freq_x, freq_y, freq_z;
    float phase_coupling;
    float temporal_inertia;
    float curvature;
    float coherence;
    float flux;
    // Add more as needed for 6DoF tensor, OAM, spin, etc.
};

using PhotonPacketTrajectoryLog = std::vector<PhotonPacketTrajectoryEntry>;

// Utility to write log to CSV or JSON (to be implemented in host code)
void write_trajectory_log_csv(const PhotonPacketTrajectoryLog& log, const char* filename);
void write_trajectory_log_json(const PhotonPacketTrajectoryLog& log, const char* filename);