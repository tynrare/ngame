// agent: composer-2.5 | 2026-08-10 | dir-packed WS cascade fill | 165350
/* One atlas texel = (probe, dir) → (RGB, opacity) for cascade interval [t0,t1]. */
in vec2 fragTexCoord;

uniform sampler2D tex_vox;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform float ng_vox_res;
uniform int ng_dir_count;
uniform int ng_max_steps;
uniform float ng_t0;
uniform float ng_t1;
uniform vec3 ng_sky;

out vec4 finalColor;

const float EMIT_SCALE = 1.0;
const float HIT_A = 0.92;
const int DIR_MAX = 48;

vec3 dir_from_index(int i, int n) {
  float t = (float(i) + 0.5) / float(max(n, 1));
  float z = 1.0 - 2.0 * t;
  float r = sqrt(max(0.0, 1.0 - z * z));
  float a = 2.3999632 * float(i);
  return vec3(cos(a) * r, z, sin(a) * r);
}

vec4 sample_vox(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return vec4(0.0);
  }
  float res = max(ng_vox_res, 1.0);
  vec3 p = floor(clamp(uvw, 0.0, 0.999999) * res);
  float u = (p.x + 0.5) / res;
  float v = (p.y + p.z * res + 0.5) / (res * res);
  return texture(tex_vox, vec2(u, v));
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
  float dt = max((t1 - t0) / float(steps), 0.04);

  for (float t = t0 + dt * 0.5; t <= t1; t += dt) {
    vec4 s = sample_vox(origin + dir * t);
    if (s.a > 0.5) {
      finalColor = vec4(s.rgb * EMIT_SCALE, HIT_A);
      return;
    }
  }
  finalColor = vec4(ng_sky * 0.06, 0.0);
}
// agent: composer-2.5 | 2026-08-10 | dir-packed WS cascade fill | 165350
