// agent: grok-4.6 | 2026-08-21 | debug drop sparse hash lookups | 284e3b
/* WS RC debug: 0=cell id, 1=world frac, 2=vis-chunk grid, 3=culling. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform sampler2D tex_chunks;
uniform sampler2D tex_pages;
uniform vec3 ng_eye;
uniform float ng_world_cell;
uniform float ng_prim_count;
uniform int ng_debug_mode;
uniform vec2 ng_resolution;
uniform int ng_chunk_count;
uniform float ng_chunk_extent;

out vec4 finalColor;

const int PRIM_MAX = 2048;
const float HIT_EPS = 0.12;

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

/** Oriented SDF for one prim row. */
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

float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
}

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

vec3 id_hash_color(float idf) {
  return prim_hash_color(int(idf * 17.0));
}

void main() {
  vec2 uv = fragTexCoord;
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  vec3 world = depth_pack.rgb;
  int mode = ng_debug_mode;

  if (mode == 1) {
    float cell = max(ng_world_cell, 0.001);
    finalColor = vec4(fract(world / cell), 1.0);
    return;
  }

  if (mode == 3) {
    int prim = find_prim_at(world);
    if (prim < 0) {
      finalColor = vec4(0.85, 0.1, 0.75, 1.0);
      return;
    }
    if (prim_vis_at(prim) < 0.5) {
      finalColor = vec4(0.1, 0.85, 0.9, 1.0);
      return;
    }
    finalColor = vec4(prim_hash_color(prim), 1.0);
    return;
  }

  float e = max(ng_chunk_extent, 0.001);
  float cell = max(ng_world_cell, 0.001);
  vec3 cc = floor(world / e);
  if (mode == 0) {
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
    }
    finalColor = vec4(col, 1.0);
    return;
  }

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
}
// agent: grok-4.6 | 2026-08-21 | debug drop sparse hash lookups | 284e3b
