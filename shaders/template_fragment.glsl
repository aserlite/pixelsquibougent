#version 410 core

// ─────────────────────────────────────────────────────────────────────────────
//  VJ Engine — Fragment Shader  (v0.6.0)
//  Sources BG : 0=Noise  1=Curl(fluide)  2=Tunnel  3=Metaballs
//               4=Slime Mold (Ping-Pong) 5=Reaction-Diffusion (Ping-Pong)
//  Filtres    : 0=Raw    1=Sobel         2=ASCII
// ─────────────────────────────────────────────────────────────────────────────

in  vec2 v_uv;
out vec4 frag_color;

uniform float     u_time;
uniform vec2      u_resolution;
uniform float     u_kick;
uniform float     u_energy;
uniform float     u_sub_bass;
uniform float     u_highs;
uniform int       u_mode;
uniform int       u_effect_index;
uniform int       u_bg_source_index;
uniform int       u_is_transitioning;
uniform int       u_target_bg_source_index;
uniform float     u_trans_progress;
uniform int       u_trans_type;
uniform sampler2D u_camera_texture;
uniform int       u_camera_active;
uniform sampler2D u_prev_frame;
uniform int       u_sim_pass;
uniform int       u_zscore_impact;
uniform sampler2D u_history_frames[8];
uniform sampler2D u_deck_texture;

uniform vec3 u_col_break1;
uniform vec3 u_col_break2;
uniform vec3 u_col_drop1;
uniform vec3 u_col_drop2;

uniform float u_pad_x;
uniform float u_pad_y;
uniform vec3  u_color_break;
uniform vec3  u_color_drop;
uniform int   u_impact_index;

// ─── Helpers ──────────────────────────────────────────────────────────────────

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3  cam_raw(vec2 uv)  { return texture(u_camera_texture, vec2(uv.x, 1.0-uv.y)).rgb; }

vec3 sample_history(int idx, vec2 uv) {
    idx = clamp(idx, 0, 7);
    switch (idx) {
        case 0: return texture(u_history_frames[0], uv).rgb;
        case 1: return texture(u_history_frames[1], uv).rgb;
        case 2: return texture(u_history_frames[2], uv).rgb;
        case 3: return texture(u_history_frames[3], uv).rgb;
        case 4: return texture(u_history_frames[4], uv).rgb;
        case 5: return texture(u_history_frames[5], uv).rgb;
        case 6: return texture(u_history_frames[6], uv).rgb;
        case 7: return texture(u_history_frames[7], uv).rgb;
    }
    return texture(u_history_frames[0], uv).rgb;
}

vec3 palette_col(float t) {
    t = clamp(t, 0.0, 1.0);
    return (u_mode == 1) ? mix(u_col_drop1, u_col_drop2, t)
                         : mix(u_col_break1, u_col_break2, t);
}

// ─── Noise primitives ─────────────────────────────────────────────────────────

vec2 _hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1,311.7)), dot(p, vec2(269.5,183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}
float _noise2D(vec2 p) {
    const float K1 = 0.366025404, K2 = 0.211324865;
    vec2 i = floor(p + (p.x+p.y)*K1);
    vec2 a = p - i + (i.x+i.y)*K2;
    vec2 o = (a.x>a.y) ? vec2(1,0) : vec2(0,1);
    vec2 b = a - o + K2;
    vec2 c = a - 1.0 + 2.0*K2;
    vec3 h = max(0.5 - vec3(dot(a,a),dot(b,b),dot(c,c)), 0.0);
    vec3 n = h*h*h*h * vec3(dot(a,_hash2(i)), dot(b,_hash2(i+o)), dot(c,_hash2(i+1.0)));
    return dot(n, vec3(70.0));
}
float _fbm(vec2 p) {
    float f = 0.0, w = 0.5;
    for (int i = 0; i < 4; i++) { f += w*_noise2D(p); p *= 2.02; w *= 0.5; }
    return f;
}

// ─── SOURCE 0 : FBM Noise Field ───────────────────────────────────────────────

vec3 src_noise(vec2 uv) {
    float e   = clamp(u_energy, 0.0, 1.0);
    float spd = 0.30 + float(u_mode)*0.5 + e*0.8;
    vec2  p   = uv * (2.5 + float(u_mode)*1.2);

    vec2 q = vec2(_fbm(p + vec2(0.0,   u_time*spd*0.4)),
                  _fbm(p + vec2(5.2, 1.3-u_time*spd*0.4)));
    vec2 r = vec2(_fbm(p + 3.0*q + vec2(1.7, 9.2)),
                  _fbm(p + 3.0*q + vec2(8.3, 2.8)));
    float n = _fbm(p + 3.0*r);

    float t1 = clamp(n*0.5+0.5, 0.0, 1.0);
    float t2 = clamp(r.x*0.5+0.5, 0.0, 1.0);

    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    float blend = clamp(float(u_mode)*e*0.7, 0.0, 1.0);
    vec3 col = mix(palette_col(t1), palette_col(t2*0.5+0.5), blend);
    col = mix(col, custom_c, 0.55);
    col *= (0.55 + 0.45*t1);
    col += vec3(u_kick*0.30) * t1*t1;
    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 1 : Curl Noise (Liquid / Fluid) ───────────────────────────────────

vec2 curl2D(vec2 p, float t) {
    const float eps = 0.01;
    float n0 = _fbm(p + vec2(0.0, eps));
    float n1 = _fbm(p - vec2(0.0, eps));
    float n2 = _fbm(p + vec2(eps, 0.0));
    float n3 = _fbm(p - vec2(eps, 0.0));
    return vec2((n0-n1)/(2.0*eps), -(n2-n3)/(2.0*eps));
}

vec3 src_curl(vec2 uv) {
    float e   = clamp(u_energy, 0.0, 1.0);
    float hi  = clamp(u_highs * 80.0, 0.0, 1.0);
    float spd = 0.18 + float(u_mode)*0.22 + e*0.35;

    vec2 p = uv * (1.8 + float(u_mode)*0.8);
    vec2 t_offset = vec2(u_time*spd, u_time*spd*0.73);

    vec2 vel = curl2D(p + t_offset, u_time);
    p += vel * (0.30 + hi*0.25);
    vel = curl2D(p * 1.8 + t_offset*1.4, u_time);
    p += vel * 0.18;
    vel = curl2D(p * 3.2 + t_offset*1.9, u_time);
    p += vel * 0.09;

    float n = _fbm(p);
    float t1 = clamp(n*0.5+0.5, 0.0, 1.0);

    float n2 = _fbm(p + vec2(3.7, 1.1));
    float t2 = clamp(n2*0.5+0.5, 0.0, 1.0);

    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    vec3 col = mix(palette_col(t1), palette_col(1.0-t2), 0.4 + e*0.3);
    col = mix(col, custom_c, 0.55);
    col *= (0.50 + 0.50*t1);
    col += vec3(u_kick*0.25) * t2;
    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 2 : 3D Raymarching Tunnel ─────────────────────────────────────────

mat2 _rot2D(float a) { float s=sin(a),c=cos(a); return mat2(c,-s,s,c); }

float _map_tunnel(vec3 p) {
    p.xy *= _rot2D(p.z*0.12 + u_time*0.25);
    float e = clamp(u_energy, 0.0, 1.0);
    float r    = length(p.xy) - (1.7 + 0.35*sin(p.z*1.8 - u_time*3.5)*e);
    float ribs = abs(fract(p.z*0.7 - u_time*1.2) - 0.5) - (0.12 + 0.08*u_kick);
    return max(-r, ribs);
}

vec3 src_raymarching(vec2 uv) {
    vec2 st = uv*2.0 - 1.0;
    st.x *= u_resolution.x / u_resolution.y;
    vec3 ro = vec3(0.0, 0.0, u_time*2.2);
    vec3 rd = normalize(vec3(st, 1.3));
    if (u_mode == 1) rd.xy *= _rot2D(sin(u_time*0.6)*0.22*clamp(u_energy,0.0,1.0));

    float t = 0.0, glow = 0.0;
    for (int i = 0; i < 40; i++) {
        vec3  p = ro + rd*t;
        float d = _map_tunnel(p);
        if (d < 0.008 || t > 22.0) break;
        t    += d*0.70;
        glow += 0.018 / (0.04 + abs(d));
    }

    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    vec3 col = vec3(0.0);
    if (t < 22.0) {
        float fog = exp(-t*0.10);
        col = mix(palette_col(0.2), custom_c, 0.65) * fog * (1.0 + clamp(u_energy,0.0,1.0)*1.2);
    }
    col += clamp(glow*0.12, 0.0, 0.8) * mix(palette_col(0.8), custom_c, 0.5) * (1.0 + u_kick*0.6);
    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 3 : 3D Metaballs ──────────────────────────────────────────────────

float smin(float a, float b, float k) {
    float h = max(k - abs(a-b), 0.0) / k;
    return min(a, b) - h*h*h*k*(1.0/6.0);
}

float map_meta(vec3 p) {
    float e   = clamp(u_energy,   0.0, 1.0);
    float sub = clamp(u_sub_bass * 60.0, 0.0, 1.0);
    float hi  = clamp(u_highs  * 80.0, 0.0, 1.0);
    float kick = clamp(u_kick,   0.0, 1.0);

    float spd = 0.6 + float(u_mode)*0.4;

    vec3 p0 = p - vec3(sin(u_time*spd*0.7)*1.2, cos(u_time*spd*0.5)*1.0, sin(u_time*spd*0.4)*0.6);
    float d0 = length(p0) - (0.55 + sub*0.35);

    vec3 p1 = p - vec3(cos(u_time*spd*0.9)*0.9, sin(u_time*spd*1.1)*1.1, cos(u_time*spd*0.6)*0.8);
    float d1 = length(p1) - (0.45 + kick*0.50);

    vec3 p2 = p - vec3(sin(u_time*spd*0.5+2.0)*1.3, cos(u_time*spd*0.3+1.0)*0.7, sin(u_time*spd*0.8)*1.0);
    float d2 = length(p2) - (0.50 + e*0.30);

    vec3 p3 = p - vec3(cos(u_time*spd*1.8+1.5)*0.7, sin(u_time*spd*2.1)*0.5, cos(u_time*spd*1.4+0.8)*1.2);
    float d3 = length(p3) - (0.25 + hi*0.20);

    float k_val = 0.6;
    float d = smin(d0, d1, k_val);
    d = smin(d, d2, k_val);
    d = smin(d, d3, k_val*0.7);
    return d;
}

vec3 src_metaballs(vec2 uv) {
    vec2 st = uv*2.0 - 1.0;
    st.x *= u_resolution.x / u_resolution.y;

    vec3 ro = vec3(0.0, 0.0, -4.0);
    vec3 rd = normalize(vec3(st, 2.0));

    if (u_mode == 1) rd.xy *= _rot2D(sin(u_time*0.3)*0.15);

    float t = 0.0;
    bool  hit = false;
    vec3  hit_p = vec3(0.0);

    for (int i = 0; i < 48; i++) {
        vec3  p = ro + rd*t;
        float d = map_meta(p);
        if (d < 0.005) { hit = true; hit_p = p; break; }
        if (t > 12.0)  break;
        t += d * 0.55;
    }

    vec3 col = vec3(0.0);
    if (hit) {
        vec2 eps = vec2(0.002, 0.0);
        vec3 nm = normalize(vec3(
            map_meta(hit_p + eps.xyy) - map_meta(hit_p - eps.xyy),
            map_meta(hit_p + eps.yxy) - map_meta(hit_p - eps.yxy),
            map_meta(hit_p + eps.yyx) - map_meta(hit_p - eps.yyx)
        ));

        vec3  light = normalize(vec3(1.0, 2.0, -2.0));
        float diff  = max(dot(nm, light), 0.0);
        float spec  = pow(max(dot(reflect(-light, nm), -rd), 0.0), 32.0);
        float rim   = pow(1.0 - abs(dot(nm, -rd)), 3.0);

        float t_pal = clamp(hit_p.y*0.4 + 0.5 + clamp(u_energy,0.0,1.0)*0.3, 0.0, 1.0);
        vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
        vec3 base = mix(palette_col(t_pal), custom_c, 0.65);

        col = base * diff * 0.9
            + vec3(1.0) * spec * (0.5 + u_kick*0.4)
            + palette_col(1.0-t_pal) * rim * 0.5;

        float fog = exp(-t*0.08);
        col *= fog;
    }

    float halo = 0.04 / (0.04 + length(st)*0.5);
    col += palette_col(0.5) * halo * (0.4 + clamp(u_energy,0.0,1.0)*0.6);

    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 4 : SDF Fluid Morphing Blob (3D Raymarching) ─────────────────────

float sdf_fluid_blob(vec3 p) {
    float r_base = 0.8;
    float r_eff = r_base + clamp(u_energy, 0.0, 1.0) * 0.5;
    float noise_val = _fbm((p.xy + vec2(u_time * 0.5)) * 2.5) * 0.5 + _hash2(p.yz * 3.0).x * 0.2;
    r_eff += noise_val * u_kick;
    return length(p) - r_eff;
}

vec3 src_fluid_blob(vec2 uv) {
    vec2 st = (uv - 0.5) * 2.0;
    st.x *= u_resolution.x / u_resolution.y;
    
    vec3 ro = vec3(0.0, 0.0, -3.0);
    vec3 rd = normalize(vec3(st, 1.5));
    
    float t = 0.0;
    int max_steps = 48;
    float d = 0.0;
    vec3 p = ro;
    for (int i = 0; i < max_steps; ++i) {
        p = ro + rd * t;
        d = sdf_fluid_blob(p);
        if (d < 0.005 || t > 8.0) break;
        t += d * 0.7;
    }
    
    if (t > 8.0) {
        return vec3(0.02, 0.02, 0.05);
    }
    
    vec2 e = vec2(0.01, 0.0);
    vec3 n = normalize(vec3(
        sdf_fluid_blob(p + e.xyy) - sdf_fluid_blob(p - e.xyy),
        sdf_fluid_blob(p + e.yxy) - sdf_fluid_blob(p - e.yxy),
        sdf_fluid_blob(p + e.yyx) - sdf_fluid_blob(p - e.yyx)
    ));
    
    vec3 light = normalize(vec3(1.0, 2.0, -2.0));
    float diff = max(dot(n, light), 0.05);
    float spec = pow(max(dot(reflect(-light, n), -rd), 0.0), 16.0);
    
    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    vec3 base_col = mix(palette_col(length(p) * 0.3), custom_c, 0.65);
    
    vec3 col = base_col * diff + vec3(spec * 0.6);
    if (u_mode == 1) col += vec3(u_kick * 0.3);
    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 5 : Reaction-Diffusion / Gray-Scott (Ping-Pong) ───────────────────
// R: Chemical U | G: Chemical V

vec3 src_reaction_diffusion(vec2 uv) {
    vec4 prev = texture(u_prev_frame, uv);

    if (u_sim_pass == 1) {
        // ── Simulation Pass (written to FBO) ──
        float U = prev.r;
        float V = prev.g;

        vec2 tx = 1.0 / u_resolution;
        vec2 lap = -1.0 * prev.rg;
        lap += 0.2 * (texture(u_prev_frame, fract(uv + tx * vec2(-1, 0))).rg +
                      texture(u_prev_frame, fract(uv + tx * vec2( 1, 0))).rg +
                      texture(u_prev_frame, fract(uv + tx * vec2( 0,-1))).rg +
                      texture(u_prev_frame, fract(uv + tx * vec2( 0, 1))).rg);
        lap += 0.05 * (texture(u_prev_frame, fract(uv + tx * vec2(-1,-1))).rg +
                       texture(u_prev_frame, fract(uv + tx * vec2( 1,-1))).rg +
                       texture(u_prev_frame, fract(uv + tx * vec2(-1, 1))).rg +
                       texture(u_prev_frame, fract(uv + tx * vec2( 1, 1))).rg);

        float f = 0.0545 + 0.008 * clamp(u_energy, 0.0, 1.0) + 0.004 * float(u_mode);
        float k = 0.0620 + 0.004 * clamp(u_sub_bass * 60.0, 0.0, 1.0);
        float Du = 1.0;
        float Dv = 0.5;
        float dt = 1.0;

        float uv2 = U * V * V;
        float dU = (Du * lap.r - uv2 + f * (1.0 - U)) * dt;
        float dV = (Dv * lap.g + uv2 - (f + k) * V) * dt;

        U = clamp(U + dU, 0.0, 1.0);
        V = clamp(V + dV, 0.0, 1.0);

        // Initialization & Kick injection
        if (u_time < 0.5 || (U == 0.0 && V == 0.0)) {
            U = 1.0;
            V = step(length(uv - 0.5), 0.12) * step(0.3, _fbm(uv * 25.0));
        }
        if (u_kick > 0.20) {
            float pulse = step(abs(length(uv - 0.5) - 0.18 - 0.15 * sin(u_time * 2.0)), 0.025);
            V = clamp(V + pulse * u_kick, 0.0, 1.0);
        }

        return vec3(U, V, 0.0);
    } else {
        // ── Visual Filter Pass (read from FBO -> colorized) ──
        vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
        float V = prev.g;
        float t_pal = clamp(V * 2.5, 0.0, 1.0);
        vec3 col = mix(palette_col(0.1), palette_col(0.9), t_pal);
        col = mix(col, custom_c, 0.60);
        col += vec3(u_kick * 0.35) * t_pal;
        return clamp(col, 0.0, 1.0);
    }
}

// ─── SOURCE 6 : Clifford Attractor ──────────────────────────────────────────
vec3 src_clifford(vec2 uv) {
    vec2 p = (uv - 0.5) * 4.0;
    float a = -1.4 + u_energy * 0.5 + sin(u_time * 0.2) * 0.2;
    float b =  1.6 + u_sub_bass * 30.0;
    float c =  1.0 + float(u_mode) * 0.5;
    float d =  0.7 + cos(u_time * 0.3) * 0.3;
    
    float acc = 0.0;
    vec2 x = p;
    for (int i = 0; i < 32; ++i) {
        if (u_kick > 0.3) {
            x += vec2(sin(u_time * 50.0 + float(i)), cos(u_time * 50.0 + float(i))) * 0.05 * u_kick;
        }
        vec2 nx = vec2(sin(a * x.y) + c * cos(a * x.x),
                       sin(b * x.x) + d * cos(b * x.y));
        float dist = length(nx - p);
        acc += exp(-dist * dist * 8.0);
        x = nx;
    }
    float norm = clamp(acc * 0.08, 0.0, 1.0);
    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    vec3 col = mix(palette_col(norm), custom_c, norm * 0.7) * norm + vec3(u_kick * 0.3) * norm;
    return clamp(col, 0.0, 1.0);
}

// ─── SOURCE 7 : Voronoi Tessellation ────────────────────────────────────────
vec3 src_voronoi(vec2 uv) {
    vec2 st = uv * 6.0;
    vec2 i_st = floor(st);
    vec2 f_st = fract(st);
    
    float m_dist = 10.0;
    vec2 m_point = vec2(0.0);
    
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = _hash2(i_st + neighbor);
            point = 0.5 + 0.5 * sin(u_time + 6.2831 * point);
            vec2 diff = neighbor + point - f_st;
            float dist = length(diff);
            if (dist < m_dist) {
                m_dist = dist;
                m_point = point;
            }
        }
    }
    
    vec3 custom_c = (u_mode == 1) ? u_color_drop : u_color_break;
    vec3 col = mix(palette_col(m_dist), custom_c, 0.65);
    if (u_zscore_impact == 1 || u_kick > 0.5) {
        col += palette_col(1.0 - m_dist) * (1.0 - smoothstep(0.0, 0.15, m_dist)) * 2.0;
    }
    col *= (1.0 - 0.3 * m_dist);
    return clamp(col, 0.0, 1.0);
}

// ─── Source selector ──────────────────────────────────────────────────────────

vec3 src_get(vec2 uv, int index) {
    switch (index) {
        case 1:  return src_curl(uv);
        case 2:  return src_raymarching(uv);
        case 3:  return src_metaballs(uv);
        case 4:  return src_fluid_blob(uv);
        case 5:  return src_reaction_diffusion(uv);
        case 6:  return src_clifford(uv);
        case 7:  return src_voronoi(uv);
        case 8:  return (u_camera_active == 1) ? cam_raw(uv) : src_noise(uv);
        case 9:  return texture(u_deck_texture, vec2(uv.x, 1.0 - uv.y)).rgb;
        default: return src_noise(uv);
    }
}

vec3 src_get_blended(vec2 uv) {
    vec3 colorA = src_get(uv, u_bg_source_index);
    if (u_is_transitioning == 0 || u_trans_progress <= 0.001) {
        return colorA;
    }
    vec3 colorB = src_get(uv, u_target_bg_source_index);
    if (u_trans_progress >= 0.999) {
        return colorB;
    }

    if (u_trans_type == 1) {
        // Type 1 (Fade) : Linear interpolation
        return mix(colorA, colorB, u_trans_progress);
    } else if (u_trans_type == 2) {
        // Type 2 (Wipe / Rideau) : Horizontal wipe
        return (uv.x < u_trans_progress) ? colorB : colorA;
    } else if (u_trans_type == 3) {
        // Type 3 (Glitch Displacement)
        float blocks = floor(uv.y * 24.0);
        float noise_val = fract(sin(blocks * 43.132 + u_time * 4.0) * 43758.5453);
        if (abs(noise_val - u_trans_progress) < 0.15) {
            vec2 glitch_uv = uv;
            glitch_uv.x += (noise_val - 0.5) * 0.12;
            vec3 cA = src_get(glitch_uv, u_bg_source_index);
            vec3 cB = src_get(glitch_uv, u_target_bg_source_index);
            return (noise_val < u_trans_progress) ? cB : cA;
        }
        return (noise_val < u_trans_progress) ? colorB : colorA;
    } else if (u_trans_type == 4) {
        // Type 4 (Luma Melt)
        float luma = dot(colorA, vec3(0.299, 0.587, 0.114));
        float threshold = clamp(u_trans_progress * 1.2 - luma * 0.2, 0.0, 1.0);
        return mix(colorA, colorB, threshold);
    } else if (u_trans_type == 5) {
        // Type 5 (Radial Wipe)
        float angle = atan(uv.y - 0.5, uv.x - 0.5);
        float norm_angle = (angle / 6.28318530718) + 0.5;
        return (norm_angle < u_trans_progress) ? colorB : colorA;
    } else if (u_trans_type == 6) {
        // Type 6 (Zoom Cross)
        vec2 uvA = 0.5 + (uv - 0.5) * (1.0 - u_trans_progress * 0.5);
        vec2 uvB = 0.5 + (uv - 0.5) * (2.0 - u_trans_progress * 1.0);
        vec3 cA = src_get(uvA, u_bg_source_index);
        vec3 cB = src_get(uvB, u_target_bg_source_index);
        return mix(cA, cB, u_trans_progress);
    }
    return colorB;
}

vec3 sample_source_texture(vec2 uv) {
    return texture(u_history_frames[0], uv).rgb;
}

// ─── FILTRE 0 : Raw ───────────────────────────────────────────────────────────

vec3 flt_raw(vec2 uv) {
    vec3 col = sample_source_texture(uv);
    col += vec3(u_kick*0.22);
    if (u_mode == 1) {
        float lum = luminance(col);
        col = mix(vec3(lum), col, 1.0 + clamp(u_energy,0.0,1.0)*0.4);
    }
    return clamp(col, 0.0, 1.0);
}

// ─── FILTRE 1 : Sobel Neon ────────────────────────────────────────────────────

float sobel_magnitude(vec2 uv) {
    vec2 tx = 1.5 / u_resolution;
    float tl=luminance(sample_source_texture(uv+tx*vec2(-1, 1)));
    float t =luminance(sample_source_texture(uv+tx*vec2( 0, 1)));
    float tr=luminance(sample_source_texture(uv+tx*vec2( 1, 1)));
    float l =luminance(sample_source_texture(uv+tx*vec2(-1, 0)));
    float r =luminance(sample_source_texture(uv+tx*vec2( 1, 0)));
    float bl=luminance(sample_source_texture(uv+tx*vec2(-1,-1)));
    float b =luminance(sample_source_texture(uv+tx*vec2( 0,-1)));
    float br=luminance(sample_source_texture(uv+tx*vec2( 1,-1)));
    float gx = -tl-2.0*l-bl+tr+2.0*r+br;
    float gy = -tl-2.0*t-tr+bl+2.0*b+br;
    return clamp(sqrt(gx*gx+gy*gy)*3.5, 0.0, 1.0);
}

vec3 flt_sobel(vec2 uv) {
    float e    = clamp(u_energy, 0.0, 1.0);
    float edge = sobel_magnitude(uv);
    edge = clamp(edge * ((u_mode==1) ? 1.0+e*1.2 : 1.0), 0.0, 1.0);
    vec3 edge_col = palette_col(edge) + vec3(u_kick*0.40)*edge;
    float bg_w = (u_bg_source_index == 8 && u_camera_active == 1) ? 0.12 : 0.18;
    return clamp(sample_source_texture(uv)*bg_w + edge_col*edge, 0.0, 1.0);
}

// ─── FILTRE 2 : ASCII Art ─────────────────────────────────────────────────────

float ascii_pattern(vec2 p, int lv) {
    float cx=abs(p.x), cy=abs(p.y);
    if (lv<=0) return 0.0;
    if (lv==1) return step(length(p), 0.22);
    if (lv==2) { float d1=length(p-vec2(0,.42)),d2=length(p-vec2(0,-.42)); return step(min(d1,d2),0.18); }
    if (lv==3) return step(min(cx,cy),0.13)*step(max(cx,cy),0.78);
    if (lv==4) { float rr=length(p); return step(0.45,rr)*step(rr,0.62); }
    if (lv==5) { return clamp(step(abs(cx-0.4),0.09)+step(abs(cy-0.4),0.09),0.0,1.0); }
    if (lv==6) { return clamp((step(cx,0.75)-step(cx,0.60))+(step(cy,0.75)-step(cy,0.60)),0.0,1.0); }
    return step(max(cx,cy), 0.82);
}

vec3 flt_ascii(vec2 uv) {
    float e = clamp(u_energy, 0.0, 1.0);
    float cells = clamp(160.0 - float(u_mode)*e*18.0, 80.0, 200.0);
    float aspect = u_resolution.x / u_resolution.y;
    vec2  grid   = vec2(cells*aspect, cells);
    vec2 cid = floor(uv*grid);
    vec2 cuv = fract(uv*grid);
    vec2 p   = cuv*2.0 - 1.0;
    vec2 ctr = (cid+0.5)/grid;
    vec2 off = 0.5/grid;
    vec3 sc  = (sample_source_texture(ctr) + sample_source_texture(ctr+vec2(off.x,0)) + sample_source_texture(ctr-vec2(off.x,0))) / 3.0;
    float lum = luminance(sc);
    float mask = ascii_pattern(p, int(clamp(lum*8.0,0.0,7.0)));
    vec3 fg = mix(sc, palette_col(lum), 0.55);
    if (u_mode==1) fg += vec3(u_kick*0.20);
    vec3 col = mix(sc*0.08, fg, mask);
    col *= mix(1.0, 0.70, (1.0-smoothstep(0.88,0.95,cuv.y))*0.45);
    return clamp(col, 0.0, 1.0);
}

// ─── FILTRE 3 : Ordered Dither (1-Bit Retro Look) ─────────────────────────────

vec3 flt_dither(vec2 uv) {
    vec3 col = sample_source_texture(uv);
    float lum = luminance(col);
    
    lum += u_kick * 0.35 + clamp(u_energy, 0.0, 1.0) * 0.15;
    
    int bayer[16] = int[](
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    );
    
    ivec2 coord = ivec2(gl_FragCoord.xy) % 4;
    int index = coord.y * 4 + coord.x;
    float threshold = float(bayer[index]) / 16.0;
    
    if (u_mode == 1) {
        threshold = mix(threshold, fract(threshold + u_time * 0.5), u_kick * 0.5);
    }
    
    float mask = step(threshold, lum);
    vec3 retro_col = palette_col(lum);
    if (u_mode == 1) retro_col += vec3(u_kick * 0.3);
    
    return clamp(mix(vec3(0.02, 0.02, 0.05), retro_col, mask), 0.0, 1.0);
}

// ─── FILTRE 4 : Halftone Pop-Art (Trame d'imprimerie) ─────────────────────────

vec3 flt_halftone(vec2 uv) {
    float e = clamp(u_energy, 0.0, 1.0);
    float cell_size = clamp(12.0 - e * 4.0 - u_kick * 3.0, 6.0, 20.0);
    
    vec2 grid_res = u_resolution / cell_size;
    vec2 cid = floor(uv * grid_res);
    vec2 cuv = fract(uv * grid_res);
    vec2 ctr = (cid + 0.5) / grid_res;
    
    vec3 sc = sample_source_texture(ctr);
    float lum = luminance(sc);
    
    float r_max = 0.48;
    float r = r_max * sqrt(clamp(lum + u_kick * 0.25, 0.0, 1.0));
    
    float dist = length(cuv - 0.5);
    float disk = 1.0 - smoothstep(r - 0.05, r + 0.05, dist);
    
    vec3 pop_col = mix(sc, palette_col(lum), 0.65);
    if (u_mode == 1) pop_col += vec3(u_kick * 0.25);
    
    vec3 bg_col = vec3(0.08, 0.08, 0.10);
    return clamp(mix(bg_col, pop_col, disk), 0.0, 1.0);
}

// ─── FILTRE 5 : Cross-Hatch Shading (Chromatique & Couplé Pad XY) ─────────────

vec3 flt_crosshatch(vec2 uv) {
    vec3 c = sample_source_texture(uv);
    float luma = luminance(c);
    
    luma += u_kick * 0.20 + clamp(u_energy, 0.0, 1.0) * 0.10;
    
    float spacing   = 12.0 + u_pad_x * 24.0;
    float thickness = 0.6 + u_pad_y * 1.4;
    
    vec3 inkColor = mix(u_color_break * 0.15, u_color_drop * 0.15, float(u_mode));
    vec3 paper    = (u_mode == 1) ? vec3(0.12, 0.08, 0.15) : vec3(0.95, 0.93, 0.88);
    
    vec3 outColor = mix(c, paper, 0.75);
    
    float diag1 = gl_FragCoord.x + gl_FragCoord.y;
    float diag2 = gl_FragCoord.x - gl_FragCoord.y;
    
    float density = 0.0;
    
    if (luma < 0.60) {
        if (mod(diag1, spacing) < thickness) density = 1.0;
    }
    if (luma < 0.45) {
        if (mod(diag2, spacing) < thickness) density = 1.0;
    }
    if (luma < 0.25) {
        if (mod(diag1 + spacing * 0.5, spacing) < thickness) density = 1.0;
    }
    if (luma < 0.10) {
        if (mod(diag2 + spacing * 0.5, spacing) < thickness) density = 1.0;
    }
    
    if (density > 0.0) {
        outColor = mix(outColor, inkColor, 0.85);
    }
    
    if (luma > 0.80) {
        float highlight = smoothstep(0.80, 1.0, luma);
        outColor += vec3(highlight * 0.55);
    }
    
    return clamp(outColor, 0.0, 1.0);
}

// ─── Filter & Impact Routing Helpers ──────────────────────────────────────────

vec3 apply_filter(int idx, vec2 uv) {
    switch (idx) {
        case 0:  return flt_raw(uv);
        case 1:  return flt_sobel(uv);
        case 2:  return flt_ascii(uv);
        case 3:  return flt_dither(uv);
        case 4:  return flt_halftone(uv);
        case 5:  return flt_crosshatch(uv);
        default: return flt_raw(uv);
    }
}

vec3 imp_crt(vec2 uv) {
    vec2 sample_uv = uv;
    if (u_zscore_impact == 1 || u_kick > 0.15) {
        float wave = sin(uv.y * 25.0 + u_time * 20.0) * sin(uv.y * 8.0 - u_time * 15.0);
        sample_uv.x += wave * (0.02 + u_kick * 0.04);
    }
    
    float split = (u_kick * 0.02 + clamp(u_energy, 0.0, 1.0) * 0.01) * sin(u_time * 10.0);
    if (u_mode == 1) split *= 1.5;
    
    bool using_cam = (u_bg_source_index == 8 && u_camera_active == 1);
    float r = texture(u_camera_texture, vec2(sample_uv.x + split, 1.0 - sample_uv.y)).r;
    float g = texture(u_camera_texture, vec2(sample_uv.x,         1.0 - sample_uv.y)).g;
    float b = texture(u_camera_texture, vec2(sample_uv.x - split, 1.0 - sample_uv.y)).b;
    vec3 col = using_cam ? vec3(r, g, b) : apply_filter(u_effect_index, sample_uv);
    
    if (!using_cam && abs(split) > 0.001) {
        vec3 col_r = apply_filter(u_effect_index, sample_uv + vec2(split, 0.0));
        vec3 col_b = apply_filter(u_effect_index, sample_uv - vec2(split, 0.0));
        col = vec3(col_r.r, col.g, col_b.b);
    }
    
    float scanline = 0.85 + 0.15 * sin(uv.y * u_resolution.y * 3.14159);
    col *= scanline;
    col += palette_col(luminance(col)) * u_kick * 0.35;
    return clamp(col, 0.0, 1.0);
}

vec3 imp_pixelsort(vec2 uv) {
    vec3 c = apply_filter(u_effect_index, uv);
    float l = luminance(c);
    
    float sort_thresh = mix(0.1, 0.8, 1.0 - u_pad_x) - u_kick * 0.3 - clamp(u_energy, 0.0, 1.0) * 0.2;
    if (u_mode == 1) sort_thresh -= 0.15;
    
    if (l > sort_thresh) {
        float stretch = mix(0.02, 0.25, u_pad_y);
        float offset = (l - sort_thresh) * (stretch + u_kick * 0.15);
        vec2 sort_uv = uv + vec2(0.0, offset);
        sort_uv.y = fract(sort_uv.y);
        c = apply_filter(u_effect_index, sort_uv);
        c += palette_col(l) * u_kick * 0.45;
    }
    return clamp(c, 0.0, 1.0);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

void main() {
    if (u_sim_pass == 1) {
        int sim_idx = (u_is_transitioning == 1 && (u_target_bg_source_index == 5))
                      ? u_target_bg_source_index
                      : u_bg_source_index;
        if (sim_idx == 5) frag_color = vec4(src_reaction_diffusion(v_uv), 1.0);
        else              frag_color = vec4(0.0);
        return;
    }
    if (u_sim_pass == 2) {
        frag_color = vec4(src_get_blended(v_uv), 1.0);
        return;
    }

    vec2 warpedUV = v_uv;
    bool impact_active = (u_impact_index > 0) && (u_kick > 0.15 || u_mode == 1 || u_zscore_impact == 1);
    
    // 1. Étape Pré-Filtre : Spatial Slit-Scan UV Warping (Horizontal Tearing)
    if (impact_active && u_impact_index == 3) {
        float slices = floor(warpedUV.y * (15.0 + u_pad_y * 45.0));
        float shift  = sin(slices * 11.45 + u_time * 8.0) * (0.04 + u_pad_x * 0.16) * u_kick;
        warpedUV.x += shift;
    }

    // 2. Application du Filtre Permanent sur la coordonnée déformée
    vec3 col = apply_filter(u_effect_index, warpedUV);

    // 3. Étape Post-Filtre : Glitches Destructifs (CRT & Pixel Sort)
    if (impact_active) {
        if (u_impact_index == 1) {
            col = imp_crt(warpedUV);
        } else if (u_impact_index == 2) {
            col = imp_pixelsort(warpedUV);
        }
    }

    col += vec3(u_kick * 0.05);
    frag_color = vec4(clamp(col, 0.0, 1.0), 1.0);
}

