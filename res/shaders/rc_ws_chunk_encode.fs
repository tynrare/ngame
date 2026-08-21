// agent: grok-4.6 | 2026-08-21 | chunk encode 8x64 cache | ecc78f
/* Dest 512x64: page p at x=[p*8,p*8+8), y=iy+iz*8. Average merged c0 dirs. */
in vec2 fragTexCoord;

uniform sampler2D tex_atlas;
uniform int ng_dir_count;
uniform int ng_page;

out vec4 finalColor;

void main() {
  int page = ng_page;
  int ix = int(floor(gl_FragCoord.x)) - page * 8;
  int y = int(floor(gl_FragCoord.y));
  if (ix < 0 || ix >= 8 || y < 0 || y >= 64) {
    discard;
  }
  int iy = y % 8;
  int iz = y / 8;
  int h = ix + 8 * (iy + 8 * iz);
  int nd = max(ng_dir_count, 1);
  vec3 acc = vec3(0.0);
  float w = 0.0;
  for (int d = 0; d < 32; d++) {
    if (d >= nd) {
      break;
    }
    vec4 s = texelFetch(tex_atlas, ivec2(d, h), 0);
    acc += s.rgb;
    w += 1.0;
  }
  finalColor = vec4(acc / max(w, 1.0), 1.0);
}
// agent: grok-4.6 | 2026-08-21 | chunk encode 8x64 cache | ecc78f
