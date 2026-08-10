// agent: composer-2.5 | 2026-08-10 | fill FragCoord atlas | 00380b
/* Probe volume fill: index by gl_FragCoord (GL y=0 bottom); march tex_vox. */
in vec2 fragTexCoord;

uniform sampler2D tex_vox;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform float ng_vox_res;
uniform int ng_dir_count;
uniform int ng_cascades;
uniform int ng_max_steps;
uniform vec3 ng_sky;

out vec4 finalColor;

const float EMIT_SCALE = 1.0;
const float HIT_A = 0.92;

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
  float x = uvw.x * (res - 1.0);
  float y = uvw.y * (res - 1.0);
  float z = uvw.z * (res - 1.0);
  float u = (x + 0.5) / res;
  float v = (y + z * res + 0.5) / (res * res);
  return texture(tex_vox, vec2(u, v));
}

void main() {
  float n = max(ng_probe_res, 1.0);
  float px = floor(gl_FragCoord.x);
  float pyz = floor(gl_FragCoord.y);
  float py = mod(pyz, n);
  float pz = floor(pyz / n);
  if (px >= n || py >= n || pz >= n) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 uvw = (vec3(px, py, pz) + 0.5) / n;
  vec3 origin = ng_ws_origin + uvw * ng_ws_size;
  float cell = min(ng_ws_size.x, min(ng_ws_size.y, ng_ws_size.z)) / n;
  float bias = cell * 0.85;
  int nd = clamp(ng_dir_count, 1, 16);
  int nc = clamp(ng_cascades, 1, 4);
  int steps = max(ng_max_steps, 1);

  vec3 acc = vec3(0.0);
  for (int di = 0; di < 16; di++) {
    if (di >= nd) {
      break;
    }
    vec3 dir = dir_from_index(di, nd);
    float T = 1.0;
    vec3 dir_rad = vec3(0.0);
    float t0 = bias;
    float t1 = cell * 1.25;
    for (int c = 0; c < 4; c++) {
      if (c >= nc) {
        break;
      }
      float dt = max((t1 - t0) / float(steps), 0.04);
      vec3 hit_rad = vec3(0.0);
      float hit = 0.0;
      for (float t = t0 + dt * 0.5; t <= t1; t += dt) {
        vec4 s = sample_vox(origin + dir * t);
        if (s.a > 0.5) {
          hit_rad = s.rgb * EMIT_SCALE;
          hit = HIT_A;
          break;
        }
      }
      dir_rad += T * hit_rad;
      T *= (1.0 - hit);
      if (T < 0.02) {
        break;
      }
      t0 = t1;
      t1 *= 2.0;
    }
    dir_rad += T * ng_sky * 0.06;
    acc += dir_rad;
  }
  finalColor = vec4(acc / float(nd), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | fill FragCoord atlas | 00380b
