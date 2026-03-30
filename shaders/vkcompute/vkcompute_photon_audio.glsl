#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform PhotonAudioPc {
    uint count;
} pc;

layout(std430, binding = 0) readonly buffer LatticeTemporal {
    float temporal_phase[];
} temporal_in;

layout(std430, binding = 1) readonly buffer LatticeCirculation {
    float oam_circulation[];
} circulation_in;

layout(std430, binding = 2) writeonly buffer AudioBuffer {
    vec4 audio_waveform[];
} audio_out;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.count) return;

    float phase = temporal_in.temporal_phase[idx];
    float circulation = circulation_in.oam_circulation[idx];

    float dry = sin(phase);
    float wet = sin(phase + circulation * 1.5707963);
    float spread = clamp(abs(circulation), 0.0, 1.0);

    audio_out.audio_waveform[idx] = vec4(
        dry,
        mix(dry, wet, spread),
        mix(dry, -wet, spread),
        wet
    );
}
