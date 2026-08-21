// agent: grok-4.6 | 2026-08-21 | fill bounce plus directional sky | 5e2cda
/* Atlas W=dirs H=cells(n^3). Hit = emit + albedo×prev SH; sky last cascade. */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform sampler2D tex_atlas;
uniform sampler2D tex_page_hash;
uniform vec3 ng_chunk_origin;
uniform float ng_cell;
uniform float ng_chunk_extent;
uniform int ng_n_side;
uniform int ng_dir_count;
uniform int ng_max_steps;
uniform int ng_prim_count;
uniform float ng_t0;
uniform float ng_t1;
uniform vec3 ng_sky;
uniform float ng_sky_on;

out vec4 finalColor;

const float EMIT_BOOST = 6.0;
const float BOUNCE_K = 0.9;
const float HIT_A = 0.88;
const float HIT_EPS = 0.04;
const float HALF_PAD = 1.12;
const float SELF_T_MIN = 0.08;
// agent: grok-4.6 | 2026-08-21 | fill DIR_MAX 96 | f4faf8
const int DIR_MAX = 96;
const int PRIM_MAX = 2048;
const int PRIM_LOOP = 32;
const int NGRID = 8;
const int HASH = 128;
const int HASH_PROBE = 8;

int g_live[32];
int g_nlive;

vec3 dir_from_index(int i, int n) {
  float t = (float(i) + 0.5) / float(max(n, 1));
  float z = 1.0 - 2.0 * t;
  float r = sqrt(max(0.0, 1.0 - z * z));
  float a = 2.3999632 * float(i);
  return vec3(cos(a) * r, z, sin(a) * r);
}

vec3 quat_inv_rotate(vec4 q, vec3 v) {
  vec3 qv = -q.xyz;
  float qw = q.w;
  vec3 t = 2.0 * cross(qv, v);
  return v + qw * t + cross(qv, t);
}

vec3 quat_rotate(vec4 q, vec3 v) {
  vec3 qv = q.xyz;
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

vec3 sky_of(vec3 d) {
  float u = clamp(d.y, 0.0, 1.0);
  return ng_sky * mix(0.22, 1.0, u * u);
}

// agent: grok-4.6 | 2026-08-21 | fill C2 sun lobe env | 6d4dcc
vec3 sun_lobe(vec3 d) {
  vec3 L = normalize(vec3(0.35, 0.85, 0.40));
  float m = max(dot(normalize(d), L), 0.0);
  return vec3(2.0, 1.85, 1.55) * pow(m, 48.0);
}

vec3 env_of(vec3 d) {
  vec3 e = sky_of(d);
  if (ng_sky_on > 0.5) {
    e += sun_lobe(d);
  }
  return e;
}

void consider_prim(int row, vec3 p, inout float best_d, inout int best_row) {
  if (row < 0 || row >= PRIM_MAX) {
    return;
  }
  vec4 c0 = fetch_prim_col(row, 0);
  vec4 c1 = fetch_prim_col(row, 1);
  vec3 center = c0.xyz;
  float R = max(c1.w, 0.0) * HALF_PAD;
  float db = length(p - center) - R;
  if (db >= best_d) {
    return;
  }
  vec4 c2 = fetch_prim_col(row, 2);
  vec3 halfv = c1.xyz * HALF_PAD;
  vec3 pl = quat_inv_rotate(c2, p - center);
  float d = c0.w > 0.5 ? sd_sphere(pl, halfv.x) : sd_box(pl, halfv);
  if (d < best_d) {
    best_d = d;
    best_row = row;
  }
}

float scene_sdf(vec3 p, out int best_row) {
  float best_d = 1e9;
  best_row = -1;
  for (int i = 0; i < PRIM_LOOP; i++) {
    if (i >= g_nlive) {
      break;
    }
    consider_prim(g_live[i], p, best_d, best_row);
  }
  return best_d;
}

int find_page(vec3 cc) {
  int cx = int(floor(cc.x));
  int cy = int(floor(cc.y));
  int cz = int(floor(cc.z));
  uint h = (uint(cx) * 73856093u) ^ (uint(cy) * 19349663u) ^ (uint(cz) * 83492791u);
  int mask = HASH - 1;
  int slot = int(h & uint(mask));
  for (int p = 0; p < HASH_PROBE; p++) {
    vec4 q = texelFetch(tex_page_hash, ivec2((slot + p) & mask, 0), 0);
    if (q.a < 0.5) {
      return -1;
    }
    if (all(lessThan(abs(q.xyz - cc), vec3(0.5)))) {
      return int(q.a + 0.5) - 1;
    }
  }
  return -1;
}

vec3 cache_texel(int page, ivec3 local, int band) {
  ivec3 ic = clamp(local, ivec3(0), ivec3(NGRID - 1));
  int x = page * NGRID + ic.x;
  int y = ic.y + ic.z * NGRID + band * 64;
  return texelFetch(tex_atlas, ivec2(x, y), 0).rgb;
}

vec3 mix8(vec3 c000, vec3 c100, vec3 c010, vec3 c110, vec3 c001, vec3 c101, vec3 c011, vec3 c111,
          vec3 f) {
  vec3 c00 = mix(c000, c100, f.x);
  vec3 c10 = mix(c010, c110, f.x);
  vec3 c01 = mix(c001, c101, f.x);
  vec3 c11 = mix(c011, c111, f.x);
  return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
}

vec3 mix_band(int page, ivec3 i0, vec3 f, int band) {
  return mix8(cache_texel(page, i0, band), cache_texel(page, i0 + ivec3(1, 0, 0), band),
              cache_texel(page, i0 + ivec3(0, 1, 0), band), cache_texel(page, i0 + ivec3(1, 1, 0), band),
              cache_texel(page, i0 + ivec3(0, 0, 1), band), cache_texel(page, i0 + ivec3(1, 0, 1), band),
              cache_texel(page, i0 + ivec3(0, 1, 1), band), cache_texel(page, i0 + ivec3(1, 1, 1), band), f);
}

vec3 sample_Ein(vec3 world, vec3 n) {
  float e = max(ng_chunk_extent, 0.001);
  vec3 cc = floor(world / e);
  int page = find_page(cc);
  if (page < 0) {
    return vec3(0.0);
  }
  vec3 uvw = clamp(world / e - cc, vec3(0.0), vec3(1.0));
  vec3 g = uvw * float(NGRID) - 0.5;
  ivec3 i0 = ivec3(floor(g));
  vec3 f = fract(g);
  vec3 l0 = mix_band(page, i0, f, 0);
  vec3 lx = mix_band(page, i0, f, 1);
  vec3 ly = mix_band(page, i0, f, 2);
  vec3 lz = mix_band(page, i0, f, 3);
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

vec3 prim_normal(int row, vec3 p) {
  vec4 c0 = fetch_prim_col(row, 0);
  vec4 c1 = fetch_prim_col(row, 1);
  vec4 c2 = fetch_prim_col(row, 2);
  vec3 halfv = max(c1.xyz * HALF_PAD, vec3(1e-4));
  vec3 pl = quat_inv_rotate(c2, p - c0.xyz);
  vec3 nl;
  if (c0.w > 0.5) {
    nl = normalize(pl);
  } else {
    vec3 a = abs(pl) - halfv;
    if (max(a.x, max(a.y, a.z)) < 0.0) {
      vec3 k = abs(pl) / halfv;
      if (k.x >= k.y && k.x >= k.z) {
        nl = vec3(sign(pl.x), 0.0, 0.0);
      } else if (k.y >= k.z) {
        nl = vec3(0.0, sign(pl.y), 0.0);
      } else {
        nl = vec3(0.0, 0.0, sign(pl.z));
      }
    } else {
      nl = normalize(max(a, vec3(0.0)) * sign(pl));
    }
  }
  vec3 nw = quat_rotate(c2, nl);
  float len = length(nw);
  return len > 1e-5 ? nw / len : vec3(0.0, 1.0, 0.0);
}

vec3 hit_radiance(int row, vec3 p) {
  if (row < 0) {
    return vec3(0.0);
  }
  vec3 emit = fetch_prim_col(row, 3).rgb * EMIT_BOOST;
  vec3 albedo = fetch_prim_col(row, 4).rgb;
  vec3 n = prim_normal(row, p);
  vec3 ein = sample_Ein(p, n);
  if (dot(ein, ein) < 1e-8) {
    ein = env_of(n);
  }
  return emit + albedo * ein * BOUNCE_K;
}

void main() {
  int nd = clamp(ng_dir_count, 1, DIR_MAX);
  int n = clamp(ng_n_side, 1, 8);
  int d = int(floor(gl_FragCoord.x));
  int h = int(floor(gl_FragCoord.y));
  int ncells = n * n * n;
  if (d >= nd || h >= ncells) {
    finalColor = vec4(0.0);
    return;
  }
  int ix = h % n;
  int iy = (h / n) % n;
  int iz = h / (n * n);
  float cell = max(ng_cell, 0.001);
  vec3 origin = ng_chunk_origin + (vec3(float(ix), float(iy), float(iz)) + 0.5) * cell;
  vec3 bmin = ng_chunk_origin - vec3(max(ng_t1, 0.0));
  vec3 bmax = ng_chunk_origin + vec3(float(n) * cell) + vec3(max(ng_t1, 0.0));
  g_nlive = 0;
  int nprim = clamp(ng_prim_count, 0, PRIM_LOOP);
  for (int i = 0; i < PRIM_LOOP; i++) {
    if (i >= nprim || g_nlive >= PRIM_LOOP) {
      break;
    }
    vec4 c0 = fetch_prim_col(i, 0);
    vec4 c1 = fetch_prim_col(i, 1);
    vec3 center = c0.xyz;
    float R = max(c1.w, 0.0) * HALF_PAD;
    vec3 q = clamp(center, bmin, bmax);
    if (length(center - q) > R) {
      continue;
    }
    g_live[g_nlive] = i;
    g_nlive += 1;
  }
  vec3 dir = dir_from_index(d, nd);
  float t0 = max(ng_t0, 0.0);
  float t1 = max(ng_t1, t0 + 0.001);
  int max_s = clamp(ng_max_steps, 8, 40);
  float t = t0;
  float dt_min = max((t1 - t0) / float(max_s), 0.02);
  float dt_max = max((t1 - t0) * 0.35, dt_min * 4.0);
  float self_t = max(SELF_T_MIN, HIT_EPS * 4.0);
  for (int s = 0; s < 40; s++) {
    if (s >= max_s || t > t1) {
      break;
    }
    vec3 p = origin + dir * t;
    int row = -1;
    float dist = scene_sdf(p, row);
    if (dist < HIT_EPS) {
      if (t >= self_t) {
        finalColor = vec4(hit_radiance(row, p), HIT_A);
        return;
      }
      t += max(dist, dt_min);
      continue;
    }
    t += clamp(dist, dt_min, dt_max);
  }
  vec3 sky = ng_sky_on > 0.5 ? env_of(dir) : vec3(0.0);
  finalColor = vec4(sky, 0.0);
}
// agent: grok-4.6 | 2026-08-21 | fill C2 sun lobe env | 6d4dcc
// agent: grok-4.6 | 2026-08-21 | fill DIR_MAX 96 | f4faf8
// agent: grok-4.6 | 2026-08-21 | fill bounce plus directional sky | 5e2cda
