#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    vec3 sunPosCam;
    float pointSize;
    vec4 debug; // x: resonance_only(0/1), y: spectrum_band, z: spectrum_phase, w: reserved
    vec4 visual; // x: vector_enabled, y: vector_gain_01, z: field_depth_m, w: focal_length_mm
} pc;

struct Inst {
    uint anchor_id_u32;
    uint kind_u32;
    uint albedo_rgba8;
    uint atmosphere_rgba8;
    int radius_q16_16;
    int emissive_q16_16;
    int atmosphere_thickness_q16_16;
    int lod_bias_q16_16;
    ivec4 rel_pos_q16_16;
    int clarity_q16_16;

    uint carrier_x_u32;
    uint carrier_y_u32;
    uint carrier_z_u32;
};

layout(set=0, binding=0, std430) readonly buffer Instances {
    Inst inst[];
} instances;

layout(location=0) out vec2 vUV;
layout(location=1) flat out vec3 vCenterCam;
layout(location=2) flat out uint vKind;
layout(location=3) flat out vec4 vAlbedo;
layout(location=4) flat out vec4 vAtmColor;
layout(location=5) flat out vec4 vParams0;
layout(location=6) flat out vec4 vParams1;

vec3 q16_16_to_f3(ivec3 v){
    return vec3(v) / 65536.0;
}
float q16_16_to_f1(int v){ return float(v) / 65536.0; }

vec4 rgba8_to_f4(uint u){
    float r = float((u      ) & 255u) / 255.0;
    float g = float((u >>  8) & 255u) / 255.0;
    float b = float((u >> 16) & 255u) / 255.0;
    float a = float((u >> 24) & 255u) / 255.0;
    return vec4(r,g,b,a);
}

void main(){
    Inst I = instances.inst[gl_InstanceIndex];
    vec3 center = q16_16_to_f3(I.rel_pos_q16_16.xyz);
    float radius = max(0.001, q16_16_to_f1(I.radius_q16_16));

    // Angular-size billboard: screen-space size proportional to radius / distance.
    float d = max(1.0, length(center));
    float s = pc.pointSize * (radius / d);

    // Quad (two triangles), UV in [-1,1].
    vec2 offs;
    vec2 uv;
    int vid = int(gl_VertexIndex) % 6;
    if (vid == 0) { offs = vec2(-s,-s); uv = vec2(-1,-1); }
    else if (vid == 1) { offs = vec2( s,-s); uv = vec2( 1,-1); }
    else if (vid == 2) { offs = vec2(-s, s); uv = vec2(-1, 1); }
    else if (vid == 3) { offs = vec2(-s, s); uv = vec2(-1, 1); }
    else if (vid == 4) { offs = vec2( s,-s); uv = vec2( 1,-1); }
    else { offs = vec2( s, s); uv = vec2( 1, 1); }

    vec4 clip = pc.proj * vec4(center + vec3(offs, 0.0), 1.0);
    gl_Position = clip;

    vUV = uv;
    vCenterCam = center;
    vKind = I.kind_u32;
    vAlbedo = rgba8_to_f4(I.albedo_rgba8);
    vAtmColor = rgba8_to_f4(I.atmosphere_rgba8);
    float phase_density = float(int(I.carrier_x_u32)) / 65536.0;
    float phase_bias = float(int(I.carrier_y_u32)) / 65536.0;
    float specularity = float(I.carrier_z_u32 & 65535u) / 32767.0;
    float roughness = float((I.carrier_z_u32 >> 16) & 65535u) / 32767.0;
    float clarity = float(I.clarity_q16_16) / 65536.0;
    vParams0 = vec4(
        radius,
        float(I.emissive_q16_16) / 65536.0,
        float(I.atmosphere_thickness_q16_16) / 65536.0,
        clamp(phase_density, 0.0, 1.0));
    vParams1 = vec4(
        clamp(phase_bias, -1.0, 1.0),
        clamp(specularity, 0.0, 1.0),
        clamp(roughness, 0.0, 1.0),
        clamp(clarity, 0.0, 1.0));
}
