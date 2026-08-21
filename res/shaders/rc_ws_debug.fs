// agent: composer-2.5 | 2026-08-10 | B4 debug lod colors | 3cf97d
// agent: composer-2.5 | 2026-08-10 | rename packed GLSL keyword | dff788
// agent: composer-2.5 | 2026-08-11 | B5 debug covering leaf no rings | 2a65b1
// agent: composer-2.5 | 2026-08-11 | culling debug mode vis mask | 2d463e
// agent: composer-2.5 | 2026-08-11 | culling miss no-prim vs culled | e8734b
// agent: composer-2.5 | 2026-08-11 | linear leaf AABB prim lookup | cbc718
// agent: composer-2.5 | 2026-08-11 | fix culling debug sample associate | 282b54
// agent: composer-2.5 | 2026-08-11 | culling debug UV grid associate | 09ac20
// agent: composer-2.5 | 2026-08-11 | culling prefer closest visible prim | 7775dd
// agent: composer-2.5 | 2026-08-11 | culling debug SDF prim associate | 076914
// agent: composer-2.5 | 2026-08-11 | debug cam-relative world | 455296
// agent: composer-2.5 | 2026-08-11 | wipe outside_far from debug | ba5896
// agent: composer-2.5 | 2026-08-11 | probes debug leaf tiles stronger | c6de06
// agent: composer-2.5 | 2026-08-11 | crisp probe tile checker debug | 592ced
// agent: composer-2.5 | 2026-08-11 | restore probes LOD parity debug | f6e37a
// agent: composer-2.5 | 2026-08-11 | unlimit lod walk debug shader | 582e49
// agent: composer-2.5 | 2026-08-12 | debug consume prim probe ids | 224b28
// agent: composer-2.5 | 2026-08-12 | remove probe checkerboard debug | 09bf7e
// agent: composer-2.5 | 2026-08-12 | probe debug color from probe id texture | fb8b05
// agent: composer-2.5 | 2026-08-12 | probes-lod from probe id texture | 5c276e
// agent: grok-4.6 | 2026-08-21 | debug chunk AABB overlay | d95420
// agent: grok-4.6 | 2026-08-21 | debug cell grid in vis chunks | b51df9
// agent: grok-4.6 | 2026-08-21 | debug world UVW and fragTexCoord | 54422d
/* WS RC debug: 0=probe id, 1=world frac, 2=vis-chunk cell grid, 3=culling, 4=probes-lod. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform sampler2D tex_grid;
uniform sampler2D tex_hash;
uniform sampler2D tex_bvh;
uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform sampler2D tex_prim_id;
uniform sampler2D tex_probe_id;
uniform sampler2D tex_chunks;
uniform sampler2D tex_pages;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform vec3 ng_eye;
uniform float ng_probe_res;
uniform float ng_grid_res;
uniform float ng_world_cell;
uniform float ng_hash_size;
uniform float ng_bvh_count;
uniform float ng_bvh_root;
uniform float ng_prim_count;
uniform int ng_debug_mode;
uniform vec2 ng_resolution;
uniform int ng_chunk_count;
uniform float ng_chunk_extent;

out vec4 finalColor;

const int LOD_STRIDE = 1000;
const int LOD_WALK = 25; /* matches NG_RC_WS_LOD_SOFT_MAX+1 */
const int PRIM_MAX = 2048;
const float HIT_EPS = 0.12;
const float GRID_EMPTY = 255.0;
const vec3 LOD_COL[8] = vec3[](vec3(0.2, 0.5, 1.0), vec3(0.2, 0.85, 0.9), vec3(0.3, 0.9, 0.3),
                               vec3(0.9, 0.9, 0.2), vec3(1.0, 0.55, 0.15), vec3(1.0, 0.25, 0.2),
                               vec3(0.85, 0.2, 0.7), vec3(0.55, 0.35, 0.9));

uint cell_hash(int lod, ivec3 c) {
  uint h = uint(lod) * 2654435761u;
  h ^= uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

float lookup_slot(int lod, ivec3 c) {
  int hsz = int(max(ng_hash_size, 1.0));
  int mask = hsz - 1;
  int h = int(cell_hash(lod, c) & uint(mask));
  for (int i = 0; i < 16; i++) {
    vec4 e = texelFetch(tex_hash, ivec2(h, 0), 0);
    if (e.r < 0.5) {
      return -1.0;
    }
    int enc = int(e.r);
    int elod = enc / LOD_STRIDE;
    int slot = (enc % LOD_STRIDE) - 1;
    if (elod == lod && ivec3(e.gba) == c && slot >= 0) {
      return float(slot);
    }
    h = (h + 1) & mask;
  }
  return -1.0;
}

/** Finest covering leaf depth at world (hash walk). */
int find_covering_lod(vec3 world) {
  for (int L = 0; L < LOD_WALK; L++) {
    float cell = max(ng_world_cell, 0.001) * exp2(float(L));
    ivec3 ic = ivec3(floor(world / cell));
    if (lookup_slot(L, ic) >= 0.0) {
      return L;
    }
  }
  return -1;
}

vec3 quat_inv_rotate(vec4 q, vec3 v) {
  vec3 qv = -q.xyz;
  float qw = q.w;
  vec3 t = 2.0 * cross(qv, v);
  return v + qw * t + cross(qv, t);
}

float sd_box(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sd_sphere(vec3 p, float r) {
  return length(p) - r;
}

vec4 fetch_prim_col(int row, int col) {
  return texelFetch(tex_prim, ivec2(col, row), 0);
}

/** Oriented SDF for one prim row (unpadded half — matches drawn mesh). */
float prim_sdf(int row, vec3 p) {
  vec4 c0 = fetch_prim_col(row, 0);
  vec4 c1 = fetch_prim_col(row, 1);
  vec4 c2 = fetch_prim_col(row, 2);
  vec3 pl = quat_inv_rotate(c2, p - c0.xyz);
  if (c0.w > 0.5) {
    return sd_sphere(pl, c1.x);
  }
  return sd_box(pl, c1.xyz);
}

/** Raylib FBO may land on either row — take max. */
float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
}

// agent: composer-2.5 | 2026-08-11 | wipe outside_far from debug | ba5896
/** Closest analytic prim at world (full SDF scan — no clip grid). */
int find_prim_at(vec3 p) {
  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  int best = -1;
  float best_d = 1e9;
  for (int row = 0; row < PRIM_MAX; row++) {
    if (row >= nprim) {
      break;
    }
    float d = prim_sdf(row, p);
    if (d < best_d) {
      best_d = d;
      best = row;
    }
  }
  if (best < 0 || best_d > HIT_EPS) {
    return -1;
  }
  return best;
}

vec3 prim_hash_color(int prim) {
  float h = fract(sin(float(prim) * 12.9898 + 78.233) * 43758.5453);
  float s = 0.55 + 0.35 * fract(sin(float(prim) * 39.346 + 11.17) * 23421.63);
  float v = 0.65 + 0.3 * fract(sin(float(prim) * 7.91 + 3.1) * 9123.12);
  float ch = v * s;
  float x = ch * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
  float m = v - ch;
  vec3 rgb;
  if (h < 1.0 / 6.0) {
    rgb = vec3(ch, x, 0.0);
  } else if (h < 2.0 / 6.0) {
    rgb = vec3(x, ch, 0.0);
  } else if (h < 3.0 / 6.0) {
    rgb = vec3(0.0, ch, x);
  } else if (h < 4.0 / 6.0) {
    rgb = vec3(0.0, x, ch);
  } else if (h < 5.0 / 6.0) {
    rgb = vec3(x, 0.0, ch);
  } else {
    rgb = vec3(ch, 0.0, x);
  }
  return rgb + m;
}

/** Stable pseudo-color from integer id. */
vec3 id_hash_color(float idf) {
  float h = fract(sin(idf * 12.9898 + 78.233) * 43758.5453);
  float s = 0.55 + 0.35 * fract(sin(idf * 39.346 + 11.17) * 23421.63);
  float v = 0.65 + 0.3 * fract(sin(idf * 7.91 + 3.1) * 9123.12);
  float ch = v * s;
  float x = ch * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
  float m = v - ch;
  vec3 rgb;
  if (h < 1.0 / 6.0) {
    rgb = vec3(ch, x, 0.0);
  } else if (h < 2.0 / 6.0) {
    rgb = vec3(x, ch, 0.0);
  } else if (h < 3.0 / 6.0) {
    rgb = vec3(0.0, ch, x);
  } else if (h < 4.0 / 6.0) {
    rgb = vec3(0.0, x, ch);
  } else if (h < 5.0 / 6.0) {
    rgb = vec3(x, 0.0, ch);
  } else {
    rgb = vec3(ch, 0.0, x);
  }
  return rgb + m;
}

void main() {
  /* Carrier fs_draw Y-flips; sample with fragTexCoord (not FragCoord).
   * tex_depth RGB = world XYZ (float gbuf). */
  // agent: grok-4.6 | 2026-08-21 | debug world UVW and fragTexCoord | 54422d
  vec2 uv = fragTexCoord;
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 world = depth_pack.rgb;
  int mode = ng_debug_mode;

  if (mode == 0) {
    // agent: grok-4.6 | 2026-08-21 | probes debug world cell ids | f74f52
    float e = max(ng_chunk_extent, 0.001);
    float cell = max(ng_world_cell, 0.001);
    vec3 cc = floor(world / e);
    vec3 local = floor(world / cell) - cc * 8.0;
    local = clamp(local, vec3(0.0), vec3(7.0));
    float h = local.x + 8.0 * (local.y + 8.0 * local.z);
    vec3 col = id_hash_color(cc.x * 19.0 + cc.y * 47.0 + cc.z * 101.0 + h * 13.0 + 1.0);
    int page = -1;
    for (int i = 0; i < 64; i++) {
      vec4 q = texelFetch(tex_pages, ivec2(0, i), 0);
      if (q.a < 0.5) {
        continue;
      }
      if (all(lessThan(abs(q.xyz - cc), vec3(0.5)))) {
        page = i;
        break;
      }
    }
    if (page < 0) {
      col *= 0.35;
    } else {
      vec3 uvw = fract(world / cell);
      float edge = min(min(uvw.x, uvw.y), uvw.z);
      if (edge < 0.08) {
        col = mix(col, vec3(float(page) / 63.0), 0.35);
      }
    }
    finalColor = vec4(col, 1.0);
    return;
  }

  if (mode == 1) {
    /* World-stable cell UVW (not cam-relative). */
    float cell = max(ng_world_cell, 0.001);
    finalColor = vec4(fract(world / cell), 1.0);
    return;
  }

  if (mode == 2) {
    // agent: grok-4.6 | 2026-08-21 | grid always draw world cells | 74a821
    float e = max(ng_chunk_extent, 0.001);
    float cell = max(ng_world_cell, 0.001);
    vec3 cc = floor(world / e);
    vec3 col = id_hash_color(cc.x * 19.0 + cc.y * 47.0 + cc.z * 101.0 + 1.0);
    int hit = 0;
    int nch = clamp(ng_chunk_count, 0, 64);
    for (int i = 0; i < 64; i++) {
      if (i >= nch) {
        break;
      }
      vec4 q = texelFetch(tex_chunks, ivec2(0, i), 0);
      if (q.a < 0.5) {
        continue;
      }
      if (all(lessThan(abs(q.xyz - cc), vec3(0.5)))) {
        hit = 1;
        break;
      }
    }
    if (hit == 0) {
      col *= 0.45;
    }
    vec3 uvw = fract(world / cell);
    float edge = min(min(min(uvw.x, uvw.y), uvw.z), min(min(1.0 - uvw.x, 1.0 - uvw.y), 1.0 - uvw.z));
    col = mix(vec3(1.0), col, smoothstep(0.0, 0.08, edge));
    vec3 cu = fract(world / e);
    float ch_e = min(min(min(cu.x, cu.y), cu.z), min(min(1.0 - cu.x, 1.0 - cu.y), 1.0 - cu.z));
    col = mix(vec3(1.0, 0.85, 0.2), col, smoothstep(0.0, 0.03, ch_e));
    finalColor = vec4(col, 1.0);
    return;
  }

  if (mode == 3) {
    int prim = int(round(texture(tex_prim_id, uv).r)) - 1;
    if (prim < 0) {
      /* Magenta: depth ok but not on an analytic prim. */
      finalColor = vec4(0.85, 0.1, 0.75, 1.0);
      return;
    }
    float vis = prim_vis_at(prim);
    if (vis < 0.5) {
      /* Cyan: prim found, GPU flag says culled. */
      finalColor = vec4(0.1, 0.85, 0.9, 1.0);
      return;
    }
    finalColor = vec4(prim_hash_color(prim), 1.0);
    return;
  }

  vec4 pid = texture(tex_probe_id, uv);
  float slot = pid.r - 1.0;
  if (mode == 4) {
    finalColor = vec4(0.28, 0.28, 0.3, 1.0);
    return;
  }
  if (slot < 0.0) {
    finalColor = vec4(0.35, 0.35, 0.35, 1.0);
    return;
  }
  finalColor = vec4(id_hash_color(slot + 1.0), 1.0);
}
// agent: grok-4.6 | 2026-08-21 | debug world UVW and fragTexCoord | 54422d
// agent: grok-4.6 | 2026-08-21 | probes debug world cell ids | f74f52
// agent: grok-4.6 | 2026-08-21 | grid always draw world cells | 74a821
// agent: grok-4.6 | 2026-08-21 | debug cell grid in vis chunks | b51df9
// agent: grok-4.6 | 2026-08-21 | debug chunk AABB overlay | d95420
// agent: composer-2.5 | 2026-08-10 | B4 debug lod colors | 3cf97d
// agent: composer-2.5 | 2026-08-10 | rename packed GLSL keyword | dff788
// agent: composer-2.5 | 2026-08-11 | B5 debug covering leaf no rings | 2a65b1
// agent: composer-2.5 | 2026-08-11 | culling debug mode vis mask | 2d463e
// agent: composer-2.5 | 2026-08-11 | culling miss no-prim vs culled | e8734b
// agent: composer-2.5 | 2026-08-11 | linear leaf AABB prim lookup | cbc718
// agent: composer-2.5 | 2026-08-11 | fix culling debug sample associate | 282b54
// agent: composer-2.5 | 2026-08-11 | culling debug UV grid associate | 09ac20
// agent: composer-2.5 | 2026-08-11 | culling prefer closest visible prim | 7775dd
// agent: composer-2.5 | 2026-08-11 | culling debug SDF prim associate | 076914
// agent: composer-2.5 | 2026-08-11 | debug cam-relative world | 455296
// agent: composer-2.5 | 2026-08-11 | wipe outside_far from debug | ba5896
// agent: composer-2.5 | 2026-08-11 | probes debug leaf tiles stronger | c6de06
// agent: composer-2.5 | 2026-08-11 | crisp probe tile checker debug | 592ced
// agent: composer-2.5 | 2026-08-11 | restore probes LOD parity debug | f6e37a
// agent: composer-2.5 | 2026-08-11 | unlimit lod walk debug shader | 582e49
// agent: composer-2.5 | 2026-08-12 | debug consume prim probe ids | 224b28
// agent: composer-2.5 | 2026-08-12 | remove probe checkerboard debug | 09bf7e
// agent: composer-2.5 | 2026-08-12 | probe debug color from probe id texture | fb8b05
// agent: composer-2.5 | 2026-08-12 | probes-lod from probe id texture | 5c276e
