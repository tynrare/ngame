// agent: composer-2.5 | 2026-08-10 | fill emit col3 texelFetch | e7332d
// agent: composer-2.5 | 2026-08-10 | fill stronger floor bounce | a0063f
/* Dir-packed WS cascade: SDF march; hit radiance from emit + rough/metal bounce. */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform int ng_dir_count;
uniform int ng_max_steps;
uniform int ng_prim_count;
uniform float ng_t0;
uniform float ng_t1;
uniform vec3 ng_sky;

out vec4 finalColor;

const float EMIT_BOOST = 6.0;
/* Floor/wall exitance into probes — sphere undersides need strong down-hemisphere. */
const float BOUNCE = 1.65;
const float E_LIT = 1.35; /* assume surfaces lit at exposure 1 when hit */
const float HIT_A = 0.88;
const float HIT_EPS = 0.02;
const float HALF_PAD = 1.12;
const int DIR_MAX = 48;
const int PRIM_MAX = 64;

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

/** emit+rough in col3; albedo+metal in col4. Diffuse bounce only (spec not marched). */
vec3 prim_radiance(vec4 emit_rough, vec4 alb_metal) {
  float rough = clamp(emit_rough.a, 0.04, 1.0);
  float metal = clamp(alb_metal.w, 0.0, 1.0);
  /* Rough → more Lambert bounce; metal → almost none. E_LIT ≈ direct exposure on hit. */
  float bounce = BOUNCE * (1.0 - metal) * mix(0.45, 1.0, rough) * E_LIT;
  return emit_rough.rgb * EMIT_BOOST + alb_metal.rgb * bounce;
}

vec4 scene_sdf(vec3 p) {
  float best_d = 1e9;
  vec3 best_rad = vec3(0.0);
  int n = clamp(ng_prim_count, 0, PRIM_MAX);
  for (int i = 0; i < PRIM_MAX; i++) {
    if (i >= n) {
      break;
    }
    vec4 c0 = fetch_prim_col(i, 0);
    vec4 c1 = fetch_prim_col(i, 1);
    vec4 c2 = fetch_prim_col(i, 2);
    vec4 c3 = fetch_prim_col(i, 3);
    vec4 c4 = fetch_prim_col(i, 4);
    vec3 center = c0.xyz;
    float typ = c0.w;
    vec3 halfv = c1.xyz * HALF_PAD;
    vec4 quat = c2;
    vec3 pl = quat_inv_rotate(quat, p - center);
    float d;
    if (typ > 0.5) {
      d = sd_sphere(pl, halfv.x);
    } else {
      d = sd_box(pl, halfv);
    }
    if (d < best_d) {
      best_d = d;
      best_rad = prim_radiance(c3, c4);
    }
  }
  return vec4(best_rad, best_d);
}

bool inside_clip(vec3 p) {
  vec3 uvw = (p - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  return uvw.x >= 0.0 && uvw.y >= 0.0 && uvw.z >= 0.0 && uvw.x <= 1.0 && uvw.y <= 1.0 &&
         uvw.z <= 1.0;
}

void main() {
  float n = max(ng_probe_res, 1.0);
  int nd = clamp(ng_dir_count, 1, DIR_MAX);
  float px = floor(gl_FragCoord.x);
  float pyz = floor(gl_FragCoord.y);
  float d = floor(px / n);
  float ix = mod(px, n);
  float iy = mod(pyz, n);
  float iz = floor(pyz / n);
  if (d >= float(nd) || ix >= n || iy >= n || iz >= n) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 uvw = (vec3(ix, iy, iz) + 0.5) / n;
  vec3 origin = ng_ws_origin + uvw * ng_ws_size;
  vec3 dir = dir_from_index(int(d), nd);
  float t0 = max(ng_t0, 0.0);
  float t1 = max(ng_t1, t0 + 0.001);
  int steps = max(ng_max_steps, 1);
  int max_s = clamp(steps * 5, 16, 56);
  float t = t0;
  float dt_min = max((t1 - t0) / float(max_s), 0.01);

  for (int s = 0; s < 56; s++) {
    if (s >= max_s || t > t1) {
      break;
    }
    vec3 p = origin + dir * t;
    if (!inside_clip(p)) {
      break;
    }
    vec4 hit = scene_sdf(p);
    float dist = hit.a;
    if (dist < HIT_EPS) {
      finalColor = vec4(hit.rgb, HIT_A);
      return;
    }
    t += max(dist, dt_min);
  }
  finalColor = vec4(ng_sky * 0.05, 0.0);
}
// agent: composer-2.5 | 2026-08-10 | fill emit col3 texelFetch | e7332d
// agent: composer-2.5 | 2026-08-10 | fill stronger floor bounce | a0063f
