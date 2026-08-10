// agent: composer-2.5 | 2026-08-09 | SS dir T-merge FS | 3805eb
/* Per-dir T-merge with nearest parent probe. Child dirs map 4:1 into parent. */
in vec2 fragTexCoord;

uniform sampler2D tex_self;
uniform sampler2D tex_parent;
uniform vec2 ng_cascade_res;   /* child atlas size */
uniform vec2 ng_probes;        /* child probe counts */
uniform vec2 ng_parent_probes; /* parent probe counts */
uniform vec2 ng_parent_res;    /* parent atlas size */
uniform int ng_dir_side;       /* child dir_side */
uniform int ng_parent_dir_side;

out vec4 finalColor;

void main() {
  vec2 atlas_px = fragTexCoord * ng_cascade_res;
  float px = floor(atlas_px.x);
  float py = floor(atlas_px.y);
  float probes_x = max(ng_probes.x, 1.0);
  float probes_y = max(ng_probes.y, 1.0);
  int dir_side = max(ng_dir_side, 1);

  int dir_col = int(floor(px / probes_x));
  int dir_row = int(floor(py / probes_y));
  int probe_x = int(mod(px, probes_x));
  int probe_y = int(mod(py, probes_y));
  int dir_i = dir_row * dir_side + dir_col;
  if (dir_col >= dir_side || dir_row >= dir_side) {
    finalColor = vec4(0.0);
    return;
  }

  vec4 self_r = texture(tex_self, fragTexCoord);
  float a = clamp(self_r.a, 0.0, 1.0);
  if (a >= 0.999) {
    finalColor = self_r;
    return;
  }

  int p_side = max(ng_parent_dir_side, 1);
  /* Child dir → first of 4 parent dirs in the same cone. */
  int p_dir = dir_i * 4;
  if (p_dir >= p_side * p_side) {
    p_dir = p_side * p_side - 1;
  }
  int p_dir_col = p_dir % p_side;
  int p_dir_row = p_dir / p_side;
  int ppx = probe_x / 2;
  int ppy = probe_y / 2;
  float ppx_max = max(ng_parent_probes.x - 1.0, 0.0);
  float ppy_max = max(ng_parent_probes.y - 1.0, 0.0);
  ppx = int(clamp(float(ppx), 0.0, ppx_max));
  ppy = int(clamp(float(ppy), 0.0, ppy_max));

  float parent_px = float(p_dir_col) * ng_parent_probes.x + float(ppx) + 0.5;
  float parent_py = float(p_dir_row) * ng_parent_probes.y + float(ppy) + 0.5;
  vec2 parent_uv = vec2(parent_px, parent_py) / max(ng_parent_res, vec2(1.0));
  vec4 parent_r = texture(tex_parent, parent_uv);

  float T = 1.0 - a;
  vec3 rad = self_r.rgb + T * parent_r.rgb;
  float ao = a + T * clamp(parent_r.a, 0.0, 1.0);
  finalColor = vec4(rad, ao);
}
// agent: composer-2.5 | 2026-08-09 | SS dir T-merge FS | 3805eb
