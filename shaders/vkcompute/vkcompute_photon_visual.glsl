#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform PhotonVisualPc {
    uint count;
} pc;

layout(std430, binding = 0) readonly buffer PhotonVisualIn {
    // x=phase_bias [-1..1], y=amplitude [0..1], z=oam/curl [-1..1], w=temporal_coupling [0..1]
    vec4 packet_state[];
} visual_in;

layout(std430, binding = 1) writeonly buffer PhotonVisualOut {
    vec4 rgba_out[];
} visual_out;

vec3 hsv_to_rgb(vec3 hsv) {
    vec3 rgb = clamp(abs(mod(hsv.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    return hsv.z * mix(vec3(1.0), rgb, hsv.y);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.count) return;

    vec4 s = visual_in.packet_state[idx];
    float phase = s.x;
    float amp = clamp(s.y, 0.0, 1.0);
    float oam = s.z;
    float coupling = clamp(s.w, 0.0, 1.0);

    float hue = fract((phase * 0.5) + 0.5);
    float sat = clamp(0.20 + 0.65 * abs(oam) + 0.15 * coupling, 0.0, 1.0);
    float val = clamp(0.10 + 0.80 * amp + 0.10 * coupling, 0.0, 1.0);
    vec3 rgb = hsv_to_rgb(vec3(hue, sat, val));

    if (oam < 0.0) {
        rgb = mix(rgb, vec3(0.20, 0.45, 1.00), clamp(abs(oam), 0.0, 1.0));
    } else {
        rgb = mix(rgb, vec3(1.00, 0.45, 0.20), clamp(oam, 0.0, 1.0));
    }

    visual_out.rgba_out[idx] = vec4(rgb, clamp(0.25 + 0.75 * amp, 0.0, 1.0));
}
