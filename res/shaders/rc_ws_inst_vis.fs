// agent: composer-2.5 | 2026-08-12 | inst expand cull shader | 0cbb9f
// agent: composer-2.5 | 2026-08-13 | rename packed GLSL keyword | 36d46b
// agent: composer-2.5 | 2026-08-13 | O1 inst prim vis map | c7d4e3
/* Expand prim frustum vis → per-graph-inst vis (O(1) via tex_inst_prim). */
in vec2 fragTexCoord;

uniform sampler2D tex_inst_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_inst_count;

out vec4 finalColor;

const int INST_MAX = 2048;

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
  float prim_r = texelFetch(tex_inst_prim, ivec2(0, x), 0).r;
  int prim = int(round(prim_r)) - 1;
  if (prim < 0) {
    finalColor = vec4(1.0);
    return;
  }
  finalColor = vec4(prim_vis_at(prim), float(x + 1), 0.0, 1.0);
}
// agent: composer-2.5 | 2026-08-13 | rename packed GLSL keyword | 36d46b
// agent: composer-2.5 | 2026-08-13 | O1 inst prim vis map | c7d4e3
