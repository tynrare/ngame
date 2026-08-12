// agent: composer-2.5 | 2026-08-12 | cover flood overlap cells | 6f27f7
/* Gen0: stamp every world cell at cover_lod that overlaps union AABB of ALL prims.
 * One cell cannot cover an AABB that straddles the origin-aligned grid (rc floor).
 * Coarsen lod until cell count fits slot_cap. Slot i < n writes that cell; else clear. */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;
uniform float ng_world_cell;
uniform float ng_slot_cap;
uniform float ng_lod_soft_max;
uniform float ng_cover_lod; /* preferred start lod (e.g. LOD_MAX-2) */

out vec4 finalColor;

const int PRIM_MAX = 64;
const int SLOT_MAX = 512;
const int LOD_SOFT = 24;

vec4 prim_col(int row, int col) {
  return texelFetch(tex_prim, ivec2(col, row), 0);
}

/** Sphere: radius; box: bound_r in half.a (fallback half extents). */
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

void main() {
  int si = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  if (si < 0 || si >= scap) {
    finalColor = vec4(0.0);
    return;
  }

  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  bool any = false;
  vec3 umin = vec3(1e30);
  vec3 umax = vec3(-1e30);
  for (int p = 0; p < PRIM_MAX; p++) {
    if (p >= nprim) {
      break;
    }
    vec3 bmin;
    vec3 bmax;
    prim_aabb(p, bmin, bmax);
    umin = min(umin, bmin);
    umax = max(umax, bmax);
    any = true;
  }
  if (!any) {
    finalColor = vec4(0.0);
    return;
  }

  int lod_max = int(clamp(ng_lod_soft_max, 0.0, float(LOD_SOFT)));
  int lod = int(clamp(ng_cover_lod, 0.0, float(lod_max)));
  int ix0 = 0;
  int iy0 = 0;
  int iz0 = 0;
  int nx = 1;
  int ny = 1;
  int nz = 1;
  int n = 1;
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
    n = nx * ny * nz;
    if (n <= scap || lod >= lod_max) {
      break;
    }
    lod++;
  }
  if (n > scap) {
    n = scap;
  }
  if (si >= n) {
    finalColor = vec4(0.0);
    return;
  }
  int t = si;
  int ix = ix0 + (t % nx);
  t /= nx;
  int iy = iy0 + (t % ny);
  t /= ny;
  int iz = iz0 + t;
  finalColor = vec4(float(ix), float(iy), float(iz), float(lod + 1));
}
// agent: composer-2.5 | 2026-08-12 | cover flood overlap cells | 6f27f7
