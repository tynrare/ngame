// agent: composer-2.5 | 2026-08-12 | probe cover fill free slots | 403e6c
/* Incremental Gen0: keep used slots; fill free slots with missing cover_lod cells
 * over vis-union AABB (from tex_union). Coarsen lod until cell count fits free+used. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform sampler2D tex_union;
uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;
uniform float ng_world_cell;
uniform float ng_slot_cap;
uniform float ng_lod_soft_max;
uniform float ng_cover_lod;

out vec4 finalColor;

const int PRIM_MAX = 64;
const int SLOT_MAX = 512;
const int LOD_SOFT = 24;

bool slot_has_key(int scap, int lod, ivec3 ic) {
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 s = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (s.a < 0.5) {
      continue;
    }
    if (int(round(s.a)) - 1 == lod && ivec3(round(s.xyz)) == ic) {
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

  vec4 cur = texelFetch(tex_slots, ivec2(si, 0), 0);
  if (cur.a >= 0.5) {
    finalColor = cur;
    return;
  }

  vec4 u0 = texelFetch(tex_union, ivec2(0, 0), 0);
  vec4 u1 = texelFetch(tex_union, ivec2(1, 0), 0);
  if (u0.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  vec3 umin = u0.rgb;
  vec3 umax = u1.rgb;

  int free_rank = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap || i >= si) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(i, 0), 0).a < 0.5) {
      free_rank++;
    }
  }

  int lod_max = int(clamp(ng_lod_soft_max, 0.0, float(LOD_SOFT)));
  int lod = int(clamp(ng_cover_lod, 0.0, float(lod_max)));
  int ix0 = 0;
  int iy0 = 0;
  int iz0 = 0;
  int nx = 1;
  int ny = 1;
  int nz = 1;
  int nneed = 1;
  for (int step = 0; step <= LOD_SOFT; step++) {
    if (lod > lod_max) {
      lod = lod_max;
    }
    float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
    ix0 = int(floor(umin.x / cell));
    iy0 = int(floor(umin.y / cell));
    iz0 = int(floor(umin.z / cell));
    int ix1 = int(floor(umax.x / cell));
    int iy1 = int(floor(umax.y / cell));
    int iz1 = int(floor(umax.z / cell));
    nx = ix1 - ix0 + 1;
    ny = iy1 - iy0 + 1;
    nz = iz1 - iz0 + 1;
    nneed = nx * ny * nz;
    if (nneed <= scap || lod >= lod_max) {
      break;
    }
    lod++;
  }

  int missing = 0;
  for (int t = 0; t < SLOT_MAX; t++) {
    if (t >= nneed) {
      break;
    }
    int tt = t;
    int ix = ix0 + (tt % nx);
    tt /= nx;
    int iy = iy0 + (tt % ny);
    tt /= ny;
    int iz = iz0 + tt;
    ivec3 ic = ivec3(ix, iy, iz);
    if (slot_has_key(scap, lod, ic)) {
      continue;
    }
    if (missing == free_rank) {
      finalColor = vec4(float(ic.x), float(ic.y), float(ic.z), float(lod + 1));
      return;
    }
    missing++;
  }
  finalColor = vec4(0.0);
}
// agent: composer-2.5 | 2026-08-12 | probe cover fill free slots | 403e6c
