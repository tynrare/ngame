// agent: composer-2.5 | 2026-08-12 | deferred prim probe id passes | 5f5d17
/* Build screen-space prim/probe IDs from depth world positions.
 * ng_id_mode: 0=prim_id, 1=probe_id. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_prim;
uniform sampler2D tex_hash;
uniform float ng_prim_count;
uniform float ng_world_cell;
uniform float ng_hash_size;
uniform int ng_id_mode;
uniform vec2 ng_resolution;

out vec4 finalColor;

const int PRIM_MAX = 64;
const int LOD_STRIDE = 1000;
const int LOD_WALK = 25;
const float HIT_EPS = 0.12;

vec3 quat_inv_rotate(vec4 q, vec3 v) {
  vec3 qv = -q.xyz;
  float qw = q.w;
  vec3 t = 2.0 * cross(qv, v);
  return v + qw * t + cross(qv, t);
}

float sd_box(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sd_sphere(vec3 p, float r) {
  return length(p) - r;
}

vec4 prim_col(int row, int col) {
  return texelFetch(tex_prim, ivec2(col, row), 0);
}

float prim_sdf(int row, vec3 p) {
  vec4 c0 = prim_col(row, 0);
  vec4 c1 = prim_col(row, 1);
  vec4 c2 = prim_col(row, 2);
  vec3 pl = quat_inv_rotate(c2, p - c0.xyz);
  if (c0.w > 0.5) {
    return sd_sphere(pl, c1.x);
  }
  return sd_box(pl, c1.xyz);
}

uint cell_hash(int lod, ivec3 c) {
  uint h = uint(lod) * 2654435761u;
  h ^= uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

float lookup_slot(int lod, ivec3 c) {
  int hsz = int(max(ng_hash_size, 1.0));
  int mask = hsz - 1;
  int h = int(cell_hash(lod, c) & uint(mask));
  for (int i = 0; i < 16; i++) {
    vec4 e = texelFetch(tex_hash, ivec2(h, 0), 0);
    if (e.r < 0.5) {
      return -1.0;
    }
    int enc = int(e.r);
    int elod = enc / LOD_STRIDE;
    int slot = (enc % LOD_STRIDE) - 1;
    if (elod == lod && ivec3(e.gba) == c && slot >= 0) {
      return float(slot);
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
  vec3 world = depth_pack.rgb;
  if (ng_id_mode == 0) {
    int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
    int best = -1;
    float best_d = 1e9;
    for (int row = 0; row < PRIM_MAX; row++) {
      if (row >= nprim) {
        break;
      }
      float d = prim_sdf(row, world);
      if (d < best_d) {
        best_d = d;
        best = row;
      }
    }
    if (best < 0 || best_d > HIT_EPS) {
      finalColor = vec4(0.0);
      return;
    }
    finalColor = vec4(float(best + 1), 0.0, 0.0, 1.0);
    return;
  }

  for (int L = 0; L < LOD_WALK; L++) {
    float cell = max(ng_world_cell, 0.001) * exp2(float(L));
    ivec3 ic = ivec3(floor(world / cell));
    float slot = lookup_slot(L, ic);
    if (slot >= 0.0) {
      float parity = mod(float(ic.x + ic.y + ic.z), 2.0);
      finalColor = vec4(slot + 1.0, float(L), parity, 1.0);
      return;
    }
  }
  finalColor = vec4(0.0);
}
// agent: composer-2.5 | 2026-08-12 | deferred prim probe id passes | 5f5d17
