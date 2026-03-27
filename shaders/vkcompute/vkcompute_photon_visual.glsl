// vkcompute_photon_visual.glsl
// Vulkan compute shader for mapping lattice phase/amplitude to RGB
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba32f) uniform image3D lattice_phase;
layout(binding = 1, rgba32f) uniform image3D lattice_amplitude;
layout(binding = 2, rgba8) writeonly uniform image3D out_rgb;

void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
    vec4 phase = imageLoad(lattice_phase, pos);
    vec4 amplitude = imageLoad(lattice_amplitude, pos);
    // Map phase and amplitude to HSV, then to RGB
    float hue = mod(phase.x / 6.2831853, 1.0); // phase.x in [0, 2pi]
    float sat = clamp(amplitude.x, 0.0, 1.0);
    float val = clamp(amplitude.x, 0.0, 1.0);
    // Simple HSV to RGB
    float c = val * sat;
    float x = c * (1.0 - abs(mod(hue * 6.0, 2.0) - 1.0));
    float m = val - c;
    vec3 rgb;
    if (hue < 1.0/6.0)      rgb = vec3(c, x, 0.0);
    else if (hue < 2.0/6.0) rgb = vec3(x, c, 0.0);
    else if (hue < 3.0/6.0) rgb = vec3(0.0, c, x);
    else if (hue < 4.0/6.0) rgb = vec3(0.0, x, c);
    else if (hue < 5.0/6.0) rgb = vec3(x, 0.0, c);
    else                    rgb = vec3(c, 0.0, x);
    rgb += m;
    imageStore(out_rgb, pos, vec4(rgb, 1.0));
}