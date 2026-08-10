// agent: composer-2.5 | 2026-08-10 | B1 debug near far lattice | 75995b
// agent: composer-2.5 | 2026-08-10 | probes gray checker no stripes | 5d8730
/* WS RC debug: 0=probe lattice, 1=far UVW, 2=grid occupancy. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform sampler2D tex_grid;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform vec3 ng_far_origin;
uniform vec3 ng_far_size;
uniform float ng_probe_res;
uniform float ng_grid_res;
uniform int ng_debug_mode;
uniform vec2 ng_resolution;

out vec4 finalColor;

const vec3 BASE = vec3(0.35, 0.55, 0.75);
const vec3 GRAY = vec3(0.45);

vec3 lattice_color(vec3 uvw, float n) {
  vec3 cell = floor(uvw * n);
  float parity = mod(cell.x + cell.y + cell.z, 2.0);
  return mix(BASE, GRAY, parity);
}

void main() {
  vec2 uv = gl_FragCoord.xy / max(ng_resolution, vec2(1.0));
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 far_uvw = depth_pack.rgb;
  bool outside_far =
      any(lessThan(far_uvw, vec3(0.0))) || any(greaterThan(far_uvw, vec3(1.0)));
  int mode = ng_debug_mode;

  if (mode == 1) {
    finalColor = outside_far ? vec4(0.35, 0.0, 0.0, 1.0) : vec4(far_uvw, 1.0);
    return;
  }

  if (outside_far) {
    finalColor = vec4(0.25, 0.02, 0.02, 1.0);
    return;
  }

  if (mode == 2) {
    float gr = max(ng_grid_res, 1.0);
    ivec3 ic = ivec3(clamp(floor(far_uvw * gr), vec3(0.0), vec3(gr - 1.0)));
    int gx = int(gr);
    vec4 slots = texelFetch(tex_grid, ivec2(ic.x, ic.y + ic.z * gx), 0) * 255.0;
    float filled = 0.0;
    filled += step(slots.r, 254.5);
    filled += step(slots.g, 254.5);
    filled += step(slots.b, 254.5);
    filled += step(slots.a, 254.5);
    float t = filled / 4.0;
    vec3 heat = mix(vec3(0.05, 0.08, 0.12), vec3(0.15, 0.85, 0.35), clamp(t, 0.0, 1.0));
    heat = mix(heat, vec3(0.95, 0.55, 0.1), step(3.5, filled));
    finalColor = vec4(heat, 1.0);
    return;
  }

  /* mode 0: near lattice inside near, else far lattice (cell size differs). */
  float n = max(ng_probe_res, 1.0);
  vec3 world = ng_far_origin + far_uvw * ng_far_size;
  vec3 near_uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  bool in_near = all(greaterThanEqual(near_uvw, vec3(0.0))) && all(lessThanEqual(near_uvw, vec3(1.0)));
  vec3 base = in_near ? lattice_color(near_uvw, n) : lattice_color(far_uvw, n);
  finalColor = vec4(base, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | B1 debug near far lattice | 75995b
// agent: composer-2.5 | 2026-08-10 | probes gray checker no stripes | 5d8730
