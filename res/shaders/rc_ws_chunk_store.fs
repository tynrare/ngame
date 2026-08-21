// agent: grok-4.6 | 2026-08-21 | chunk store page cascade strip | 4dcb97
/* Copy scratch cascade into page atlas strip x=[page*nd, page*nd+nd). */
in vec2 fragTexCoord;

uniform sampler2D tex_src;
uniform int ng_page;
uniform int ng_nd;

out vec4 finalColor;

void main() {
  int nd = max(ng_nd, 1);
  int dir = int(floor(gl_FragCoord.x)) - ng_page * nd;
  int h = int(floor(gl_FragCoord.y));
  if (dir < 0 || dir >= nd || h < 0) {
    discard;
  }
  finalColor = texelFetch(tex_src, ivec2(dir, h), 0);
}
// agent: grok-4.6 | 2026-08-21 | chunk store page cascade strip | 4dcb97
