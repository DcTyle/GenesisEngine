#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    vec3 sunPosCam;
    float pointSize;
    vec4 debug; // x: resonance_only(0/1), y: spectrum_band, z: spectrum_phase, w: selected carrier coupling [0,1]
    vec4 visual; // x: vector_enabled, y: vector_gain_01, z: field_depth_m, w: focal_length_mm
} pc;

layout(location=0) in vec2 vUV;
layout(location=1) flat in vec3 vCenterCam;
layout(location=2) flat in uint vKind;
layout(location=3) flat in vec4 vAlbedo;
layout(location=4) flat in vec4 vAtmColor;
layout(location=5) flat in vec4 vParams0;
layout(location=6) flat in vec4 vParams1;

layout(location=0) out vec4 oColor;

// Deterministic triangle wave (no trig). Used to express ripple/interference
// from existing harmonic magnitudes without adding synthetic noise.
float tri(float x) {
    float f = fract(x);
    float t = abs(f - 0.5);
    return 1.0 - clamp(t * 2.0, 0.0, 1.0);
}

void main(){
    float radius = max(vParams0.x, 0.001);
    float emissive = max(vParams0.y, 0.0);
    float atm_thick = max(vParams0.z, 0.0);
    float phase_density = clamp(vParams0.w, 0.0, 1.0);
    float phase_bias = clamp(vParams1.x, -1.0, 1.0);
    float specularity = clamp(vParams1.y, 0.0, 1.0);
    float roughness = clamp(vParams1.z, 0.0, 1.0);
    float clarity = clamp(vParams1.w, 0.0, 1.0);
    float occlusion = clamp(vAtmColor.a, 0.0, 1.0);
    float vector_enabled = (pc.visual.x > 0.5) ? 1.0 : 0.0;
    float vector_gain = clamp(pc.visual.y, 0.0, 1.0);
    float focus_depth_m = max(pc.visual.z, 0.5);
    float focal_mm = clamp(pc.visual.w, 18.0, 200.0);
    float focal_norm = clamp((focal_mm - 18.0) / 182.0, 0.0, 1.0);
    float depth_m = max(abs(vCenterCam.z), 0.001);
    float focus_ratio = depth_m / focus_depth_m;
    float dof_blur = clamp(abs(log2(focus_ratio)) * (0.35 + 1.10 * focal_norm), 0.0, 1.0);
    float dof_focus = 1.0 - dof_blur;

    // Sun: pure emissive disc (still derives intensity from emissive channel)
    if (vKind == 1u) {
        float r2 = dot(vUV, vUV);
        if (r2 > 1.0) discard;
        float soft = clamp(1.0 - r2, 0.0, 1.0);
        soft = pow(soft, 0.75 + 1.25 * phase_density);
        vec3 c = vAlbedo.rgb * soft;
        c *= (0.25 + emissive);
        oColor = vec4(c, 1.0);
        return;
    }

    float r2 = dot(vUV, vUV);
    if (r2 > 1.0) discard;

    float z = sqrt(max(0.0, 1.0 - r2));
    vec3 n = normalize(vec3(vUV, z));
    vec3 phase_normal = normalize(mix(n, normalize(vAtmColor.rgb * 2.0 - 1.0), 0.65));
    vec3 L = normalize(pc.sunPosCam - vCenterCam);
    float ndl = max(dot(phase_normal, L), 0.0);
    if (vKind >= 3u && vKind <= 8u) {
        bool lattice_kind = (vKind >= 7u);
        bool echo_kind = (vKind == 8u);
        float radial = length(vUV);
        float shell = smoothstep(1.0, 0.0, radial);
        vec2 axis = normalize(phase_normal.xy + vec2(0.0013, 0.0009));
        float stream_a = tri(dot(vUV, axis) * (lattice_kind ? (12.0 + 34.0 * phase_density) : (8.0 + 28.0 * phase_density)) - pc.debug.z * (0.35 + 1.15 * specularity));
        float stream_b = tri(dot(vUV, vec2(-axis.y, axis.x)) * (lattice_kind ? (6.0 + 20.0 * (1.0 - roughness)) : (4.0 + 17.0 * (1.0 - roughness))) + phase_bias * (lattice_kind ? 3.5 : 2.5));
        float stream = pow(clamp(stream_a * stream_b, 0.0, 1.0), mix(2.8, 1.1, specularity));
        float halo = pow(clamp(1.0 - radial, 0.0, 1.0), lattice_kind ? (1.8 + 0.8 * roughness) : (1.2 + 1.0 * roughness));
        float core = smoothstep(lattice_kind ? 0.24 : 0.34, 0.0, radial);
        vec3 class_col = vAlbedo.rgb;
        vec3 col = class_col * (0.25 + emissive * 0.42) * halo;
        col += class_col * stream * (0.18 + 1.35 * specularity + 0.55 * phase_density);
        col += vec3(1.0) * core * (0.22 + 0.85 * phase_density);
        col += class_col * ndl * (0.08 + 0.22 * (1.0 - roughness));
        if (lattice_kind) {
            float sheer = tri(dot(vUV, axis) * (18.0 + 28.0 * phase_density) + phase_bias * 4.0 + pc.debug.z * 0.35);
            float ribbon = pow(clamp(sheer * (1.0 - radial), 0.0, 1.0), echo_kind ? 1.6 : 1.1);
            col += mix(vec3(1.0), class_col, 0.55) * ribbon * (echo_kind ? 0.35 : 0.80);
        }
        if (vector_enabled > 0.5) {
            float vector_a = tri(dot(vUV, axis) * (10.0 + 34.0 * vector_gain) + pc.debug.z * (0.15 + 0.55 * vector_gain));
            float vector_b = tri(dot(vUV, vec2(-axis.y, axis.x)) * (3.0 + 12.0 * vector_gain) - phase_bias * 1.75);
            float vector_glow = pow(clamp(vector_a * vector_b, 0.0, 1.0), mix(3.6, 1.3, vector_gain));
            col += mix(vec3(0.28, 0.74, 1.0), class_col, 0.65) * vector_glow * (0.08 + 1.30 * vector_gain);
        }
        col *= mix(0.62, 1.0, dof_focus);
        float alpha = clamp((lattice_kind ? 0.18 : 0.50) + 0.35 * phase_density + 0.15 * specularity, 0.0, 1.0);
        alpha *= shell * (1.0 - 0.30 * occlusion);
        if (echo_kind) alpha *= 0.45;
        alpha *= mix(0.65, 1.0, dof_focus);
        oColor = vec4(col, alpha);
        return;
    }
    if (pc.debug.x > 0.5) {
        float band = pc.debug.y;
        float ph = pc.debug.z;
        float coupling = clamp(pc.debug.w, 0.0, 1.0);
        float base = 0.14 + 1.30 * (0.46 * phase_density + 0.32 * specularity + 0.22 * (1.0 - roughness));
        base = clamp(base, 0.0, 2.2);
        float radial = length(vUV);
        float ring_freq = 4.0 + 8.0 * coupling + 1.5 * clamp(band, -2.0, 6.0) + 2.0 * specularity;
        float rings = tri(ph * (0.4 + 0.4 * coupling) + radial * ring_freq + 2.5 * specularity);
        float lattice = tri(ph * 0.55 + (vUV.x + vUV.y) * (5.0 + 3.0 * phase_density));
        float shell = smoothstep(0.98, 0.10, radial);
        float field = shell * mix(rings, lattice, 0.35 + 0.35 * coupling);
        vec3 gold = vec3(1.0, 0.72, 0.20);
        vec3 amber = vec3(1.0, 0.56, 0.12);
        vec3 c = mix(amber, gold, phase_density) * clamp(base * field * (1.0 - 0.35 * occlusion), 0.0, 2.4);
        c += gold * smoothstep(0.45, 0.0, radial) * (0.10 + 0.35 * specularity);
        if (vector_enabled > 0.5) {
            vec2 axis = normalize(phase_normal.xy + vec2(0.001, 0.0007));
            float strands_a = tri(dot(vUV, axis) * (6.0 + 22.0 * vector_gain) + ph * (0.20 + 0.70 * vector_gain));
            float strands_b = tri(dot(vUV, vec2(-axis.y, axis.x)) * (4.0 + 17.0 * vector_gain) - ph * 0.25);
            float strands = pow(clamp(strands_a * strands_b, 0.0, 1.0), mix(2.8, 1.1, vector_gain));
            vec3 vector_col = mix(vec3(0.20, 0.62, 1.0), vec3(1.0, 0.76, 0.24), phase_density);
            c += vector_col * strands * (0.10 + 0.95 * vector_gain);
        }
        c *= mix(0.58, 1.0, dof_focus);
        oColor = vec4(c, 1.0);
        return;
    }

    vec3 phase_tint = vec3(
        1.0 - 0.25 * max(0.0, phase_bias),
        1.0,
        1.0 - 0.25 * max(0.0, -phase_bias));
    float edge = pow(clamp(1.0 - z, 0.0, 1.0), mix(1.45, 0.85, phase_density));
    float glow = phase_density * (0.20 + 0.80 * edge);
    float ripple = tri(dot(vUV, vec2(15.0, 11.0)) * (1.0 + 4.0 * specularity) + 2.0 * phase_density);
    vec3 lit = vAlbedo.rgb * (0.15 + 0.85 * ndl) * phase_tint;
    vec3 spec_col = mix(vec3(0.08), vec3(1.0), specularity);
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 H = normalize(L + V);
    float spec_pow = mix(6.0, 64.0, 1.0 - roughness);
    float spec = pow(max(dot(phase_normal, H), 0.0), spec_pow) * (0.15 + 0.85 * specularity);
    vec3 col = lit * (1.0 - 0.45 * occlusion);
    col += spec_col * spec;
    col += vAlbedo.rgb * glow * (0.24 + 0.76 * emissive);
    col += vAlbedo.rgb * (0.04 + 0.14 * clarity) * ripple * (1.0 - 0.40 * roughness);
    if (vector_enabled > 0.5) {
        vec2 axis = normalize(phase_normal.xy + vec2(0.001, 0.0007));
        float strands_a = tri(dot(vUV, axis) * (8.0 + 26.0 * vector_gain) + pc.debug.z * (0.15 + 0.65 * vector_gain));
        float strands_b = tri(dot(vUV, vec2(-axis.y, axis.x)) * (5.0 + 19.0 * vector_gain) - phase_bias * 1.5);
        float strands = pow(clamp(strands_a * strands_b, 0.0, 1.0), mix(2.6, 1.05, vector_gain));
        vec3 vector_col = mix(vec3(0.25, 0.66, 1.0), vec3(1.0, 0.80, 0.26), phase_density);
        col += vector_col * strands * (0.08 + 1.20 * vector_gain);
    }

    if (vKind == 2u && atm_thick > 0.0) {
        float rim = pow(1.0 - z, 2.5);
        float haze = rim * clamp(atm_thick / max(radius, 1.0), 0.0, 1.0) * (0.55 + 0.45 * (1.0 - roughness));
        float forward = pow(max(dot(phase_normal, normalize(-L)), 0.0), 5.0) * (0.35 + 0.65 * phase_density);
        vec3 atm = vAlbedo.rgb * (0.45*haze + 0.35*forward);
        atm *= (0.15 + 0.85 * ndl);
        col += atm;
    }
    col *= mix(0.60, 1.0, dof_focus);
    vec3 luma = vec3(dot(col, vec3(0.299, 0.587, 0.114)));
    col = mix(luma, col, 0.68 + 0.32 * dof_focus);

    float alpha = clamp(1.0 - (r2 - 1.0) * 8.0, 0.0, 1.0);
    alpha *= vAlbedo.a;
    alpha *= (0.60 + 0.40 * phase_density);
    alpha *= (1.0 - 0.35 * occlusion);
    alpha *= mix(0.55, 1.0, dof_focus);
    oColor = vec4(col, alpha);
}
