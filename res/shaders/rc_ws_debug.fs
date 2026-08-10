// agent: composer-2.5 | 2026-08-10 | B1 debug near far lattice | 75995b
// agent: composer-2.5 | 2026-08-10 | probes gray checker no stripes | 5d8730
// agent: composer-2.5 | 2026-08-10 | B3 sparse probes debug | b17473
// agent: composer-2.5 | 2026-08-10 | probes outside still sample | 8fadf9
/* WS RC debug: 0=sparse slot hit, 1=far UVW, 2=grid occupancy. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform sampler2D tex_grid;
uniform sampler2D tex_hash;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform vec3 ng_far_origin;
uniform vec3 ng_far_size;
uniform float ng_probe_res;
uniform float ng_grid_res;
uniform float ng_world_cell;
uniform float ng_hash_size;
uniform int ng_debug_mode;
uniform vec2 ng_resolution;

out vec4 finalColor;

const vec3 LIVE = vec3(0.25, 0.55, 0.95);
const vec3 MISS = vec3(0.4);

uint cell_hash(ivec3 c) {
  uint h = uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

float lookup_slot(ivec3 cell) {
  int hsz = int(max(ng_hash_size, 1.0));
  int mask = hsz - 1;
  int h = int(cell_hash(cell) & uint(mask));
  for (int i = 0; i < 16; i++) {
    vec4 e = texelFetch(tex_hash, ivec2(h, 0), 0);
    if (e.r < 0.5) {
      return -1.0;
    }
    if (ivec3(e.gba) == cell) {
      return e.r - 1.0;
    }
    h = (h + 1) & mask;
  }
  return -1.0;
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

  /* mode 0/2: allow outside far cube (sparse keys are world-CELL). */
  if (mode == 2) {
    if (outside_far) {
      finalColor = vec4(0.25, 0.02, 0.02, 1.0);
      return;
    }
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

  /* mode 0: blue if sparse slot hit, gray miss. */
  float cell = max(ng_world_cell, 0.001);
  vec3 world = ng_far_origin + far_uvw * ng_far_size;
  ivec3 ic = ivec3(floor(world / cell));
  float slot = lookup_slot(ic);
  float parity = mod(float(ic.x + ic.y + ic.z), 2.0);
  vec3 base = slot >= 0.0 ? mix(LIVE, LIVE * 0.7, parity) : MISS;
  if (outside_far) {
    base *= 0.65; /* dim but still show hit/miss outside cube */
  }
  finalColor = vec4(base, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | B1 debug near far lattice | 75995b
// agent: composer-2.5 | 2026-08-10 | probes gray checker no stripes | 5d8730
// agent: composer-2.5 | 2026-08-10 | B3 sparse probes debug | b17473
// agent: composer-2.5 | 2026-08-10 | probes outside still sample | 8fadf9
