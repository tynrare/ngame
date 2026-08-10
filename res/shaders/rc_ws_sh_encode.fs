// agent: composer-2.5 | 2026-08-10 | L1 SH encode at probes | df9aeb
/* Dir-packed cascade → L1 SH per probe. Atlas W=N*4 (L0,L1x,L1y,L1z), H=N*N. */
in vec2 fragTexCoord;

uniform sampler2D tex_cascade;
uniform float ng_probe_res;
uniform int ng_dir_count;
uniform vec2 ng_cascade_res;

out vec4 finalColor;

const int DIR_MAX = 48;

vec3 dir_from_index(int i, int n) {
  float t = (float(i) + 0.5) / float(max(n, 1));
  float z = 1.0 - 2.0 * t;
  float r = sqrt(max(0.0, 1.0 - z * z));
  float a = 2.3999632 * float(i);
  return vec3(cos(a) * r, z, sin(a) * r);
}

void main() {
  float n = max(ng_probe_res, 1.0);
  int nd = clamp(ng_dir_count, 1, DIR_MAX);
  float px = floor(gl_FragCoord.x);
  float pyz = floor(gl_FragCoord.y);
  float band = floor(px / n);
  float ix = mod(px, n);
  float iy = mod(pyz, n);
  float iz = floor(pyz / n);
  if (band > 3.0 || ix >= n || iy >= n || iz >= n) {
    finalColor = vec4(0.0);
    return;
  }

  vec2 cres = max(ng_cascade_res, vec2(1.0));
  vec3 sh0 = vec3(0.0);
  vec3 shx = vec3(0.0);
  vec3 shy = vec3(0.0);
  vec3 shz = vec3(0.0);
  for (int i = 0; i < DIR_MAX; i++) {
    if (i >= nd) {
      break;
    }
    float ax = float(i) * n + ix + 0.5;
    float ay = iy + iz * n + 0.5;
    vec3 rad = texture(tex_cascade, vec2(ax, ay) / cres).rgb;
    vec3 d = dir_from_index(i, nd);
    sh0 += rad;
    shx += rad * d.x;
    shy += rad * d.y;
    shz += rad * d.z;
  }
  float inv = 1.0 / float(nd);
  sh0 *= inv;
  shx *= inv;
  shy *= inv;
  shz *= inv;

  vec3 outc = sh0;
  if (band > 0.5 && band < 1.5) {
    outc = shx;
  } else if (band > 1.5 && band < 2.5) {
    outc = shy;
  } else if (band > 2.5) {
    outc = shz;
  }
  finalColor = vec4(outc, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | L1 SH encode at probes | df9aeb
