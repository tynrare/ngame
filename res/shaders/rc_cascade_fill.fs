// agent: composer-2.5 | 2026-08-10 | SS dist from world uvw | 39bad7
/* Direction-first packed fill: one texel = one (probe, dir) interval march. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_albedo;
uniform sampler2D tex_glow;
uniform vec2 ng_resolution;     /* gbuf / base cascade pixel size */
uniform vec2 ng_cascade_res;    /* this cascade atlas size */
uniform vec2 ng_probes;         /* probe counts x,y */
uniform int ng_dir_side;        /* sqrt(dir_count), dirs = side^2 */
uniform int ng_spacing;         /* probe spacing in base pixels */
uniform float ng_interval0;
uniform float ng_interval1;
uniform int ng_max_steps;
uniform vec3 ng_sky;
uniform vec3 ng_cam_pos;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;

out vec4 finalColor;

const float FAR = 80.0;
const float EPS = 0.02;
const float THICKNESS = 0.45;
const float FRONT_BIAS = 0.12;
const float EMIT_BOOST = 2.4;
const float BOUNCE_SCALE = 1.85;
const float GLOW_HIT = 0.025;
const float GEO_OPACITY = 0.8;

vec2 dir_from_index(int i, int n) {
  float a = 6.2831853 * (float(i) + 0.5) / float(max(n, 1));
  return vec2(cos(a), sin(a));
}

float depth_world_dist(vec2 uv) {
  vec4 pack = texture(tex_depth, uv);
  if (pack.a < 0.5) {
    return 0.0;
  }
  vec3 world = ng_ws_origin + pack.rgb * ng_ws_size;
  return length(world - ng_cam_pos);
}

void main() {
  vec2 atlas_px = fragTexCoord * ng_cascade_res;
  float px = floor(atlas_px.x);
  float py = floor(atlas_px.y);
  float probes_x = max(ng_probes.x, 1.0);
  float probes_y = max(ng_probes.y, 1.0);
  int dir_side = max(ng_dir_side, 1);
  int dirs = dir_side * dir_side;

  int dir_col = int(floor(px / probes_x));
  int dir_row = int(floor(py / probes_y));
  int probe_x = int(mod(px, probes_x));
  int probe_y = int(mod(py, probes_y));
  int dir_i = dir_row * dir_side + dir_col;
  if (dir_col >= dir_side || dir_row >= dir_side || dir_i >= dirs) {
    finalColor = vec4(0.0);
    return;
  }

  float spacing = float(max(ng_spacing, 1));
  vec2 screen_px = (vec2(float(probe_x), float(probe_y)) + 0.5) * spacing;
  vec2 uv = screen_px / max(ng_resolution, vec2(1.0));
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
    finalColor = vec4(0.0);
    return;
  }

  float my_d = depth_world_dist(uv);
  if (my_d < EPS) {
    finalColor = vec4(ng_sky * 0.03, 0.0);
    return;
  }

  vec2 dir = dir_from_index(dir_i, dirs);
  float t0 = ng_interval0;
  float t1 = max(ng_interval1, t0 + 1.0);
  int steps = max(ng_max_steps, 1);
  float step_px = max(1.0, (t1 - t0) / float(steps));
  vec2 texel = 1.0 / max(ng_resolution, vec2(1.0));

  vec3 rad = vec3(0.0);
  float T = 1.0;
  for (float t = t0 + step_px * 0.5; t <= t1; t += step_px) {
    vec2 p = uv + dir * t * texel;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) {
      break;
    }
    vec3 g = texture(tex_glow, p).rgb;
    float glow_lum = max(g.r, max(g.g, g.b));
    float zd = depth_world_dist(p);
    bool geo = false;
    if (zd > EPS && zd < FAR) {
      geo = (zd < my_d - FRONT_BIAS) || (abs(zd - my_d) > THICKNESS);
    }
    if (glow_lum > GLOW_HIT || geo) {
      vec3 a = texture(tex_albedo, p).rgb;
      vec3 L = g * EMIT_BOOST + a * BOUNCE_SCALE;
      float opac = glow_lum > GLOW_HIT ? 1.0 : GEO_OPACITY;
      rad += T * L;
      T *= (1.0 - opac);
      if (T < 0.02) {
        break;
      }
    }
  }
  rad += T * ng_sky * 0.015;
  finalColor = vec4(rad, clamp(1.0 - T, 0.0, 1.0));
}
// agent: composer-2.5 | 2026-08-10 | SS dist from world uvw | 39bad7
