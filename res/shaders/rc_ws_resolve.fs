// agent: composer-2.5 | 2026-08-10 | resolve soft nest LOD blend | abb143
// agent: composer-2.5 | 2026-08-10 | B3 sparse hash resolve | f083bd
/* Sparse L1 SH via open-address hash (tex_hash + tex_meta). */
in vec2 fragTexCoord;

uniform sampler2D tex_sh;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform sampler2D tex_meta;
uniform sampler2D tex_hash;
uniform vec3 ng_sky;
uniform vec3 ng_far_origin;
uniform vec3 ng_far_size;
uniform float ng_world_cell;
uniform float ng_hash_size;
uniform vec2 ng_sh_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

const float SELF_BIAS = 0.09;
const int PROBE_MAX = 16;

vec3 fetch_band(float slot, float band) {
  return texelFetch(tex_sh, ivec2(int(band), int(slot)), 0).rgb;
}

vec3 eval_sh(float slot, vec3 n) {
  vec3 l0 = fetch_band(slot, 0.0);
  vec3 lx = fetch_band(slot, 1.0);
  vec3 ly = fetch_band(slot, 2.0);
  vec3 lz = fetch_band(slot, 3.0);
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

uint cell_hash(ivec3 c) {
  uint h = uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

/** Lookup slot for cell; -1 miss. */
float lookup_slot(ivec3 cell) {
  int hsz = int(max(ng_hash_size, 1.0));
  int mask = hsz - 1;
  int h = int(cell_hash(cell) & uint(mask));
  for (int i = 0; i < PROBE_MAX; i++) {
    vec4 e = texelFetch(tex_hash, ivec2(h, 0), 0);
    if (e.r < 0.5) {
      return -1.0;
    }
    ivec3 kc = ivec3(e.gba);
    if (kc == cell) {
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

  vec3 n = normalize(texture(tex_normal, uv).rgb * 2.0 - 1.0);
  vec3 world = ng_far_origin + depth_pack.rgb * ng_far_size;
  float cell = max(ng_world_cell, 0.001);
  float bias = max(SELF_BIAS, cell * 0.25);
  vec3 p = world + n * bias;
  ivec3 ic = ivec3(floor(p / cell));

  float slot = lookup_slot(ic);
  if (slot < 0.0) {
    /* Soft: try face neighbors. */
    for (int f = 0; f < 6; f++) {
      ivec3 o = ic;
      if (f == 0) {
        o.x += 1;
      } else if (f == 1) {
        o.x -= 1;
      } else if (f == 2) {
        o.y += 1;
      } else if (f == 3) {
        o.y -= 1;
      } else if (f == 4) {
        o.z += 1;
      } else {
        o.z -= 1;
      }
      slot = lookup_slot(o);
      if (slot >= 0.0) {
        break;
      }
    }
  }
  if (slot < 0.0) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }
  finalColor = vec4(eval_sh(slot, n), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | resolve soft nest LOD blend | abb143
// agent: composer-2.5 | 2026-08-10 | B3 sparse hash resolve | f083bd
