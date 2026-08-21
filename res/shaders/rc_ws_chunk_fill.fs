// agent: grok-4.6 | 2026-08-21 | fill loop 32 chunk overlap | 154831
// agent: grok-4.6 | 2026-08-21 | fill sky last cascade only | d35194
/* Atlas W=dirs H=cells(n^3). Probe at chunk cascade grid; SDF tex_prim. */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform vec3 ng_chunk_origin;
uniform float ng_cell;
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
const float BOUNCE = 1.65;
const float E_LIT = 1.35;
const float HIT_A = 0.88;
const float HIT_EPS = 0.04;
const float HALF_PAD = 1.12;
const float SELF_T_MIN = 0.08;
const int DIR_MAX = 32;
const int PRIM_MAX = 2048;
const int PRIM_LOOP = 32;

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

vec3 prim_radiance(vec4 emit_rough, vec4 alb_metal) {
  float rough = clamp(emit_rough.a, 0.04, 1.0);
  float metal = clamp(alb_metal.w, 0.0, 1.0);
  float bounce = BOUNCE * (1.0 - metal) * mix(0.45, 1.0, rough) * E_LIT;
  return emit_rough.rgb * EMIT_BOOST + alb_metal.rgb * bounce;
}

void consider_prim(int row, vec3 p, inout float best_d, inout vec3 best_rad) {
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
  vec4 c3 = fetch_prim_col(row, 3);
  vec4 c4 = fetch_prim_col(row, 4);
  vec3 halfv = c1.xyz * HALF_PAD;
  vec3 pl = quat_inv_rotate(c2, p - center);
  float d = c0.w > 0.5 ? sd_sphere(pl, halfv.x) : sd_box(pl, halfv);
  if (d < best_d) {
    best_d = d;
    best_rad = prim_radiance(c3, c4);
  }
}

vec4 scene_sdf(vec3 p) {
  float best_d = 1e9;
  vec3 best_rad = vec3(0.0);
  for (int i = 0; i < PRIM_LOOP; i++) {
    if (i >= g_nlive) {
      break;
    }
    consider_prim(g_live[i], p, best_d, best_rad);
  }
  return vec4(best_rad, best_d);
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
    vec4 hit = scene_sdf(p);
    float dist = hit.a;
    if (dist < HIT_EPS) {
      if (t >= self_t) {
        finalColor = vec4(hit.rgb, HIT_A);
        return;
      }
      t += max(dist, dt_min);
      continue;
    }
    t += clamp(dist, dt_min, dt_max);
  }
  vec3 sky = ng_sky_on > 0.5 ? ng_sky : vec3(0.0);
  finalColor = vec4(sky, 0.0);
}
// agent: grok-4.6 | 2026-08-21 | fill loop 32 chunk overlap | 154831
// agent: grok-4.6 | 2026-08-21 | fill sky last cascade only | d35194
