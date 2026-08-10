// agent: composer-2.5 | 2026-08-10 | SS resolve hemisphere wt | 39f33d
/* Hemisphere-weight c0 dirs at screen pixel → irradiance RGB. */
in vec2 fragTexCoord;

uniform sampler2D tex_cascade;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform vec2 ng_resolution;
uniform vec2 ng_cascade_res;
uniform vec2 ng_probes;
uniform int ng_dir_side;
uniform int ng_spacing;

out vec4 finalColor;

vec2 dir_from_index(int i, int n) {
  float a = 6.2831853 * (float(i) + 0.5) / float(max(n, 1));
  return vec2(cos(a), sin(a));
}

void main() {
  vec2 uv = fragTexCoord;
  float d = texture(tex_depth, uv).r;
  if (d < 0.02) {
    finalColor = vec4(0.0);
    return;
  }

  float spacing = float(max(ng_spacing, 1));
  vec2 screen_px = uv * ng_resolution;
  int probe_x = int(floor(screen_px.x / spacing));
  int probe_y = int(floor(screen_px.y / spacing));
  float probes_x = max(ng_probes.x, 1.0);
  float probes_y = max(ng_probes.y, 1.0);
  probe_x = int(clamp(float(probe_x), 0.0, probes_x - 1.0));
  probe_y = int(clamp(float(probe_y), 0.0, probes_y - 1.0));

  vec3 n = texture(tex_normal, uv).rgb * 2.0 - 1.0;
  float n2len = length(n.xy);
  vec2 n2 = n2len > 1e-4 ? n.xy / n2len : vec2(0.0);

  int dir_side = max(ng_dir_side, 1);
  int dirs = dir_side * dir_side;
  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i < 64; i++) {
    if (i >= dirs) {
      break;
    }
    int dir_col = i % dir_side;
    int dir_row = i / dir_side;
    float ax = float(dir_col) * probes_x + float(probe_x) + 0.5;
    float ay = float(dir_row) * probes_y + float(probe_y) + 0.5;
    vec2 cuv = vec2(ax, ay) / max(ng_cascade_res, vec2(1.0));
    vec4 s = texture(tex_cascade, cuv);
    vec2 dir = dir_from_index(i, dirs);
    /* Walls: screen-projected N·L. Floors (n.xy≈0): equal dirs. */
    float hemi = (n2len > 0.25) ? max(dot(n2, dir), 0.0) : 1.0;
    float w = hemi * (0.35 + 0.65 * clamp(s.a, 0.0, 1.0));
    acc += s.rgb * w;
    wsum += w;
  }
  finalColor = vec4(acc / max(wsum, 1e-3), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | SS resolve hemisphere wt | 39f33d
