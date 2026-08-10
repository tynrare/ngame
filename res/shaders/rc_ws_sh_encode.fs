// agent: composer-2.5 | 2026-08-10 | sh encode texelFetch cascade | cb332d
// agent: composer-2.5 | 2026-08-10 | B3 sparse SH slot rows | 43834b
/* Dir-packed sparse cascade → L1 SH. Atlas W=4 bands, H=slots. Cascade W=dirs H=slots. */
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
  float nslots = max(ng_probe_res, 1.0);
  int nd = clamp(ng_dir_count, 1, DIR_MAX);
  float band = floor(gl_FragCoord.x);
  float slot = floor(gl_FragCoord.y);
  if (band > 3.0 || slot >= nslots) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 sh0 = vec3(0.0);
  vec3 shx = vec3(0.0);
  vec3 shy = vec3(0.0);
  vec3 shz = vec3(0.0);
  int sy = int(slot);
  for (int i = 0; i < DIR_MAX; i++) {
    if (i >= nd) {
      break;
    }
    vec3 rad = texelFetch(tex_cascade, ivec2(i, sy), 0).rgb;
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
// agent: composer-2.5 | 2026-08-10 | sh encode texelFetch cascade | cb332d
// agent: composer-2.5 | 2026-08-10 | B3 sparse SH slot rows | 43834b
