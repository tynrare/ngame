// agent: composer-2.5 | 2026-08-12 | probe release freeable shader | 27c2cc
/* Release: clear slot if outside vis-union AABB or empty/shell fail vs vis prims.
 * Draw slots → slots_b. a<0.5 = free. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform sampler2D tex_union; /* 2×1: 0=umin 1=umax */
uniform float ng_prim_count;
uniform float ng_world_cell;
uniform float ng_slot_cap;
uniform float ng_shell_lod_max;

out vec4 finalColor;

const int PRIM_MAX = 2048;
const int SLOT_MAX = 512;

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

float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
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

void prim_aabb(int row, out vec3 bmin, out vec3 bmax) {
  vec4 ctr = prim_col(row, 0);
  vec4 halfv = prim_col(row, 1);
  if (ctr.w > 0.5) {
    float r = halfv.x;
    bmin = ctr.xyz - vec3(r);
    bmax = ctr.xyz + vec3(r);
  } else {
    float br = max(halfv.w, max(halfv.x, max(halfv.y, halfv.z)));
    bmin = ctr.xyz - vec3(br);
    bmax = ctr.xyz + vec3(br);
  }
}

bool cell_aabb_hits_prim(int row, int lod, ivec3 ic) {
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  vec3 cmin = vec3(ic) * cell;
  vec3 cmax = cmin + vec3(cell);
  vec3 pmin;
  vec3 pmax;
  prim_aabb(row, pmin, pmax);
  return !(pmin.x > cmax.x || pmax.x < cmin.x || pmin.y > cmax.y || pmax.y < cmin.y ||
           pmin.z > cmax.z || pmax.z < cmin.z);
}

bool cell_hits_prim_shell(int row, int lod, ivec3 ic) {
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  float x0 = float(ic.x) * cell;
  float y0 = float(ic.y) * cell;
  float z0 = float(ic.z) * cell;
  float h = 0.5 * cell;
  float band = h * 1.7320508;
  float mind = 1e30;
  float maxd = -1e30;
  float minabs = 1e30;
  vec3 samples[15];
  samples[0] = vec3(x0, y0, z0);
  samples[1] = vec3(x0 + cell, y0, z0);
  samples[2] = vec3(x0, y0 + cell, z0);
  samples[3] = vec3(x0 + cell, y0 + cell, z0);
  samples[4] = vec3(x0, y0, z0 + cell);
  samples[5] = vec3(x0 + cell, y0, z0 + cell);
  samples[6] = vec3(x0, y0 + cell, z0 + cell);
  samples[7] = vec3(x0 + cell, y0 + cell, z0 + cell);
  samples[8] = vec3(x0 + h, y0 + h, z0 + h);
  samples[9] = vec3(x0 + h, y0 + h, z0);
  samples[10] = vec3(x0 + h, y0 + h, z0 + cell);
  samples[11] = vec3(x0 + h, y0, z0 + h);
  samples[12] = vec3(x0 + h, y0 + cell, z0 + h);
  samples[13] = vec3(x0, y0 + h, z0 + h);
  samples[14] = vec3(x0 + cell, y0 + h, z0 + h);
  for (int s = 0; s < 15; s++) {
    float d = prim_sdf(row, samples[s]);
    mind = min(mind, d);
    maxd = max(maxd, d);
    minabs = min(minabs, abs(d));
  }
  if (mind < 0.0 && maxd > 0.0) {
    return true;
  }
  return minabs <= band;
}

bool cell_keep_vis(int lod, ivec3 ic) {
  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  int shell_max = int(clamp(ng_shell_lod_max, 0.0, 24.0));
  for (int row = 0; row < PRIM_MAX; row++) {
    if (row >= nprim) {
      break;
    }
    if (prim_vis_at(row) < 0.5) {
      continue;
    }
    if (!cell_aabb_hits_prim(row, lod, ic)) {
      continue;
    }
    if (lod > shell_max || cell_hits_prim_shell(row, lod, ic)) {
      return true;
    }
  }
  return false;
}

void main() {
  int si = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  if (si < 0 || si >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
  if (s.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  vec3 umin = texelFetch(tex_union, ivec2(0, 0), 0).rgb;
  vec3 umax = texelFetch(tex_union, ivec2(1, 0), 0).rgb;
  if (texelFetch(tex_union, ivec2(0, 0), 0).a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  int lod = int(round(s.a)) - 1;
  ivec3 ic = ivec3(round(s.xyz));
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  vec3 cmin = vec3(ic) * cell;
  vec3 cmax = cmin + vec3(cell);
  if (cmax.x < umin.x || cmin.x > umax.x || cmax.y < umin.y || cmin.y > umax.y ||
      cmax.z < umin.z || cmin.z > umax.z) {
    finalColor = vec4(0.0);
    return;
  }
  if (!cell_keep_vis(lod, ic)) {
    finalColor = vec4(0.0);
    return;
  }
  finalColor = s;
}
// agent: composer-2.5 | 2026-08-12 | probe release freeable shader | 27c2cc
