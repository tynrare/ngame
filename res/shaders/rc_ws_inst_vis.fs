// agent: composer-2.5 | 2026-08-12 | inst expand cull shader | 0cbb9f
/* Expand prim frustum vis → per-graph-inst vis.
 * One fragment = one inst index. Default visible; prim-backed insts copy prim_vis.
 * tex_prim col5.a = inst_i+1 (packed at rebuild). */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;
uniform float ng_inst_count;

out vec4 finalColor;

const int PRIM_MAX = 64;
const int INST_MAX = 512;

float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
}

void main() {
  int x = int(floor(gl_FragCoord.x));
  int ninst = int(clamp(ng_inst_count, 0.0, float(INST_MAX)));
  if (x < 0 || x >= ninst) {
    finalColor = vec4(0.0);
    return;
  }
  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  bool matched = false;
  float vis = 0.0;
  for (int p = 0; p < PRIM_MAX; p++) {
    if (p >= nprim) {
      break;
    }
    int ii = int(round(texelFetch(tex_prim, ivec2(5, p), 0).a)) - 1;
    if (ii != x) {
      continue;
    }
    matched = true;
    vis = max(vis, prim_vis_at(p));
  }
  if (!matched) {
    finalColor = vec4(1.0); /* non-prim inst: always draw */
    return;
  }
  finalColor = vec4(vis, float(x + 1), 0.0, 1.0);
}
// agent: composer-2.5 | 2026-08-12 | inst expand cull shader | 0cbb9f
