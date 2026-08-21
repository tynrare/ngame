// agent: grok-4.6 | 2026-08-21 | encode L1 SH four bands | 26f87b
/* Dest PAGE*8 x 256: y=(iy+iz*8)+band*64; bands L0,Lx,Ly,Lz. */
in vec2 fragTexCoord;

uniform sampler2D tex_atlas;
uniform int ng_dir_count;
uniform int ng_page;

out vec4 finalColor;

const int DIR_MAX = 32;

vec3 dir_from_index(int i, int n) {
  float t = (float(i) + 0.5) / float(max(n, 1));
  float z = 1.0 - 2.0 * t;
  float r = sqrt(max(0.0, 1.0 - z * z));
  float a = 2.3999632 * float(i);
  return vec3(cos(a) * r, z, sin(a) * r);
}

void main() {
  int page = ng_page;
  int ix = int(floor(gl_FragCoord.x)) - page * 8;
  int y = int(floor(gl_FragCoord.y));
  if (ix < 0 || ix >= 8 || y < 0 || y >= 256) {
    discard;
  }
  int band = y / 64;
  int y0 = y % 64;
  int iy = y0 % 8;
  int iz = y0 / 8;
  int h = ix + 8 * (iy + 8 * iz);
  int nd = clamp(ng_dir_count, 1, DIR_MAX);
  vec3 sh0 = vec3(0.0);
  vec3 shx = vec3(0.0);
  vec3 shy = vec3(0.0);
  vec3 shz = vec3(0.0);
  for (int d = 0; d < DIR_MAX; d++) {
    if (d >= nd) {
      break;
    }
    vec3 rad = texelFetch(tex_atlas, ivec2(d, h), 0).rgb;
    vec3 dir = dir_from_index(d, nd);
    sh0 += rad;
    shx += rad * dir.x;
    shy += rad * dir.y;
    shz += rad * dir.z;
  }
  float inv = 1.0 / float(nd);
  vec3 outc = sh0 * inv;
  if (band == 1) {
    outc = shx * inv;
  } else if (band == 2) {
    outc = shy * inv;
  } else if (band == 3) {
    outc = shz * inv;
  }
  finalColor = vec4(outc, 1.0);
}
// agent: grok-4.6 | 2026-08-21 | encode L1 SH four bands | 26f87b
