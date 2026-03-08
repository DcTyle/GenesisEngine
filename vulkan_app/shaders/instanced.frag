#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    vec3 sunPosCam;
    float pointSize;
    vec4 debug; // x: resonance_only(0/1), y: spectrum_band, z: spectrum_phase, w: selected carrier coupling [0,1]
} pc;

layout(location=0) in vec2 vUV;
layout(location=1) flat in vec3 vCenterCam;
layout(location=2) flat in uint vKind;
layout(location=3) in vec4 vAlbedo;
layout(location=4) in vec4 vAtmColor;
layout(location=5) in float vRadius;
layout(location=6) in float vEmissive;
layout(location=7) in float vAtmThick;
layout(location=8) in float vLodBias;
layout(location=9) in float vClarity;
layout(location=10) in float vDopplerK;
layout(location=11) in float vLeak;
layout(location=12) in float vHarmMean;
layout(location=13) in float vFluxGrad;
layout(location=14) in float vDensity;

// Virtual texturing (production): atlas texture + page table SSBO.
// Atlas is a packed tile cache. Page table maps (mip,tile_x,tile_y) -> atlas tile coord.
layout(set=0, binding=1) uniform sampler2D uAtlasTex;

layout(std430, set=0, binding=2) readonly buffer VtPageTable {
    // Header
    uint atlas_dim;     // pixels (e.g., 4096)
    uint tile_size;     // pixels (e.g., 128)
    uint mip_count;     // number of mips available in virtual texture
    uint virtual_dim;   // base virtual dimension (square)
    uint mip_offset[16];        // offset into entries[] per mip
    uint mip_tiles_per_row[16]; // tiles per row per mip
    // Entries packed as: 0xFFFFFFFF = not resident; else (ay<<16)|ax in atlas tile grid
    uint entries[];
} vt;

layout(location=0) out vec4 oColor;

// Deterministic triangle wave (no trig). Used to express ripple/interference
// from existing harmonic magnitudes without adding synthetic noise.
float tri(float x) {
    float f = fract(x);
    float t = abs(f - 0.5);
    return 1.0 - clamp(t * 2.0, 0.0, 1.0);
}

void main(){
    // Sun: pure emissive disc (still derives intensity from emissive channel)
    if (vKind == 1u) {
        float r2 = dot(vUV, vUV);
        if (r2 > 1.0) discard;
        // Emergent soft edge from leak proxy (volumetric density)
        float soft = clamp(1.0 - r2, 0.0, 1.0);
        soft = pow(soft, 0.75 + 1.25 * clamp(vLeak, 0.0, 1.0));
        vec3 c = vAlbedo.rgb * soft;
        c *= (0.25 + vEmissive);
        oColor = vec4(c, 1.0);
        return;
    }

    // Planet / object sphere impostor
    float r2 = dot(vUV, vUV);
    if (r2 > 1.0) discard;

    float z = sqrt(max(0.0, 1.0 - r2));
    vec3 n = normalize(vec3(vUV, z));

    // Light direction: from surface point toward sun (camera space)
    vec3 L = normalize(pc.sunPosCam - vCenterCam);
    float ndl = max(dot(n, L), 0.0);



// Resonance-only viewport mode:
// When enabled, render only substrate resonance proxies (leak/harmonics/flux/density)
// as a black-and-gold carrier field with bounded Fourier-like rings and coupling filaments.
if (pc.debug.x > 0.5) {
    float band = pc.debug.y;
    float ph = pc.debug.z;
    float coupling = clamp(pc.debug.w, 0.0, 1.0);

    float leak = clamp(vLeak, 0.0, 1.0);
    float harm = clamp(vHarmMean, 0.0, 1.0);
    float flux = clamp(abs(vFluxGrad), 0.0, 1.0);
    float dens = clamp(vDensity, 0.0, 1.0);

    float base = 0.10 + 1.60 * (0.50 * leak + 0.28 * harm + 0.22 * flux);
    base = clamp(base, 0.0, 2.0);

    float k = 6.0 + 2.0 * clamp(band, -4.0, 8.0);
    float p = tri(ph + k * (vUV.x * 0.73 + vUV.y * 0.91) + 3.0 * harm);
    float q = tri(ph * 0.5 + k * (vUV.x * 1.31 - vUV.y * 0.57) + 2.0 * flux);
    float speckle = 0.48 + 0.52 * (p * q);

    float radial = length(vUV);
    float ring_freq = 4.0 + 10.0 * coupling + 1.5 * clamp(band, -2.0, 6.0);
    float rings = tri(ph * (0.35 + 0.45 * coupling) + radial * ring_freq + 2.5 * harm);
    rings = pow(rings, mix(2.8, 1.4, coupling));

    float filament_a = tri(ph * 0.75 + (vUV.x + vUV.y) * (5.0 + 5.0 * coupling) + 1.7 * flux);
    float filament_b = tri(ph * 0.45 + (vUV.x - vUV.y) * (6.0 + 4.0 * harm) + 2.3 * leak);
    float filaments = pow(max(filament_a, filament_b), 3.0);

    float interior = 0.20 + 0.80 * dens;
    float shell = smoothstep(0.95, 0.30, radial);
    float ring_mask = shell * (0.35 + 0.65 * rings);
    float filament_mask = shell * filaments * (0.25 + 0.75 * coupling);
    float field = speckle * interior * (0.55 + 0.45 * ring_mask) + filament_mask;
    float I = clamp(base * field, 0.0, 2.4);

    vec3 gold = vec3(1.0, 0.72, 0.20);
    vec3 amber = vec3(1.0, 0.58, 0.14);
    float warm = clamp(0.5 + 0.08 * band + 0.15 * coupling, 0.0, 1.0);
    vec3 c = mix(amber, gold, warm) * I;

    float core = smoothstep(0.55, 0.0, radial) * (0.18 + 0.42 * harm);
    c += gold * core * (0.25 + 0.75 * coupling);

    oColor = vec4(c, 1.0);
    return;
}
    // Emergent lighting proxy:
    // - Base radiance from emissive + leak density (field leakage -> volumetric glow)
    // - Directional visibility from surface normal & sun vector (geometry only)
    // NOTE: We intentionally avoid post-process hacks; the softness and glow
    // come from leak-density and harmonic interference below.
    float vis = ndl;

    // UV for the impostor: map [-1,1] disc coordinates to [0,1].
    vec2 tUV = vUV * 0.5 + 0.5;

    // Virtual texture sampling.
    // Compute an approximate LOD from derivatives in virtual space.
    float dudx = length(dFdx(tUV));
    float dudy = length(dFdy(tUV));
    float d = max(dudx, dudy);
    float vdim = float(max(vt.virtual_dim, 1u));
    float lod = log2(max(d * vdim, 1.0e-6));
    lod = clamp(lod + vLodBias, 0.0, float(max(vt.mip_count, 1u) - 1u));
    uint mip = uint(lod);

    // Virtual dimension at mip
    uint vdim_m = max(vt.virtual_dim >> mip, 1u);
    float f_vdim_m = float(vdim_m);
    float px = tUV.x * f_vdim_m;
    float py = tUV.y * f_vdim_m;

    uint ts = max(vt.tile_size, 1u);
    uint tx = uint(px) / ts;
    uint ty = uint(py) / ts;
    uint tiles_per_row = vt.mip_tiles_per_row[mip];
    uint base = vt.mip_offset[mip];
    uint idx = base + ty * tiles_per_row + tx;

    vec3 texCol = vAlbedo.rgb;
    if (idx < base + tiles_per_row * tiles_per_row) {
        uint entry = vt.entries[idx];
        if (entry != 0xFFFFFFFFu) {
            uint ax = entry & 0xFFFFu;
            uint ay = (entry >> 16) & 0xFFFFu;
            // Local coords in tile
            float lx = fract(px / float(ts));
            float ly = fract(py / float(ts));
            // Atlas UV (packed tiles in a grid)
            float atlas_dim_f = float(max(vt.atlas_dim, 1u));
            vec2 atlas_uv;
            atlas_uv.x = (float(ax) * float(ts) + lx * float(ts)) / atlas_dim_f;
            atlas_uv.y = (float(ay) * float(ts) + ly * float(ts)) / atlas_dim_f;
            texCol = texture(uAtlasTex, atlas_uv).rgb;
        }
    }

    // Base albedo (still uses VT when present) then modulate with harmonic ripple.
    vec3 baseAlbedo = mix(vAlbedo.rgb, texCol, clamp(vClarity, 0.0, 1.0));

    // Harmonic ripple: interpret mean harmonic magnitude as phase density.
    // No synthetic noise: ripple is purely a deterministic modulation of UV
    // driven by harmonic magnitude.
    float harm = clamp(vHarmMean, 0.0, 2.0);
    float ripple = tri(dot(vUV, vec2(17.0, 13.0)) * (1.0 + 6.0 * harm));
    float ripple_w = 0.15 * clamp(harm, 0.0, 1.0);
    vec3 rippleAlbedo = mix(baseAlbedo, baseAlbedo * (0.75 + 0.5 * ripple), ripple_w);

    // Doppler color shift: vDopplerK in [-1,+1] shifts toward blue/red.
    float dk = clamp(vDopplerK, -1.0, 1.0);
    vec3 dopplerTint = vec3(1.0 - 0.35 * max(0.0, dk), 1.0, 1.0 - 0.35 * max(0.0, -dk));

    // Leak density drives soft volumetric edge/glow.
    float leak = clamp(vLeak, 0.0, 1.0);
    float edge = pow(clamp(1.0 - z, 0.0, 1.0), 1.25);
    float density = clamp(leak * (0.35 + 0.65 * edge), 0.0, 1.0);

    // -------------------------------------------------------------------------
    // D-noise factor (diffuse/noise):
    // Use flux-gradient proxy as the "between objects" interference driver.
    // Dense objects dominate: noise amplitude is suppressed as density increases.
    // This modulates emissive/glow and slightly diffuses visibility on low-density
    // objects while preserving determinism (no RNG, no trig).
    // -------------------------------------------------------------------------
    float fluxg = clamp(vFluxGrad, 0.0, 1.0);
    float dens = clamp(vDensity, 0.0, 1.0);
    // Deterministic interference pattern driven by flux gradient + harmonics.
    float dn = tri(dot(vUV, vec2(37.0, 29.0)) * (1.0 + 4.0 * fluxg) + 11.0 * harm);
    // Noise amplitude: flux increases it, density suppresses it.
    float dn_amp = clamp(fluxg * (1.0 - dens), 0.0, 1.0);
    // Energy diffusion factor: low-density objects get more diffusion.
    float dn_diff = mix(1.0, 0.70 + 0.30 * dn, dn_amp);
    // Dense objects dominate energy flux: scale energy terms up with density.
    float dom = 0.35 + 0.65 * dens;

    // Emergent radiance: base reflected (visibility) + leakage glow + emissive breathing.
    float breathe = 0.65 + 0.35 * clamp(vEmissive, 0.0, 2.0);
    // Slightly diffuse visibility for low-density objects under high flux gradients.
    float vis_dn = mix(vis, 0.55 * vis + 0.45 * dn, dn_amp);
    vec3 col = rippleAlbedo * (0.10 + 0.90 * vis_dn) * dopplerTint;
    // Apply D-noise diffusion to glow/emissive channels; dense objects dominate.
    col += rippleAlbedo * density * (0.20 + 0.80 * breathe) * dn_diff * dom;
    col += vAlbedo.rgb * max(0.0, vEmissive) * (0.25 + 0.75 * leak) * dn_diff * dom;

    // Atmosphere (Earth): rim scattering derived from leak proxy + geometry.
    if (vKind == 2u && vAtmThick > 0.0) {
        float rim = pow(1.0 - z, 2.5);
        float haze = rim * clamp(vAtmThick / max(vRadius, 1.0), 0.0, 1.0);
        // Leak proxy strengthens forward scatter (field leakage -> aerosol density)
        float forward = pow(max(dot(n, normalize(-L)), 0.0), 5.0) * (0.35 + 0.65 * leak);
        vec3 atm = vAtmColor.rgb * (0.45*haze + 0.35*forward);
        atm *= (0.15 + 0.85 * vis);
        col += atm;
    }

    // Soft discard edge: density drives alpha near boundary.
    float alpha = clamp(1.0 - (r2 - 1.0) * 8.0, 0.0, 1.0);
    alpha *= (0.65 + 0.35 * density);
    oColor = vec4(col, alpha);
}
