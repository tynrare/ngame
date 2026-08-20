// agent: grok-4.6 | 2026-08-12 | probe unmet 1x1 flag | f671d1
// agent: composer-2.5 | 2026-08-13 | GPU split gate lod max | 876029
/* 1×1: r=unmet g=lod_max/LOD_SOFT b=allow_split (cover ok or coarse band). */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform sampler2D tex_union;
uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;
uniform float ng_world_cell;
uniform float ng_slot_cap;
uniform float ng_cover_lod;
uniform float ng_lod_soft_max;

out vec4 finalColor;

const int PRIM_MAX = 2048;
const int SLOT_MAX = 512;
const int LOD_SOFT = 24;

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

bool cell_hits_vis_surface(int lod, ivec3 ic) {
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  float x0 = float(ic.x) * cell;
  float y0 = float(ic.y) * cell;
  float z0 = float(ic.z) * cell;
  float h = 0.5 * cell;
  float band = h * 1.7320508;
  int pc = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  for (int p = 0; p < PRIM_MAX; p++) {
    if (p >= pc) {
      break;
    }
    if (prim_vis_at(p) < 0.5) {
      continue;
    }
    float mind = 1e30;
    float maxd = -1e30;
    float minabs = 1e30;
    for (int c = 0; c < 8; c++) {
      vec3 s = vec3(x0 + float(c & 1) * cell, y0 + float((c >> 1) & 1) * cell,
                    z0 + float((c >> 2) & 1) * cell);
      float d = prim_sdf(p, s);
      mind = min(mind, d);
      maxd = max(maxd, d);
      minabs = min(minabs, abs(d));
    }
    vec3 mid = vec3(x0 + h, y0 + h, z0 + h);
    float dm = prim_sdf(p, mid);
    mind = min(mind, dm);
    maxd = max(maxd, dm);
    minabs = min(minabs, abs(dm));
    if (mind <= band && maxd >= -band && minabs <= band) {
      return true;
    }
  }
  return false;
}

bool octant_occupied(int scap, int lod, ivec3 ic) {
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 s = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (s.a < 0.5) {
      continue;
    }
    int sl = int(round(s.a)) - 1;
    ivec3 sc = ivec3(round(s.xyz));
    if (sl == lod) {
      if (sc == ic) {
        return true;
      }
    } else if (sl < lod) {
      float den = exp2(float(lod - sl));
      if (ivec3(floor(vec3(sc) / den)) == ic) {
        return true;
      }
    } else {
      float den = exp2(float(sl - lod));
      if (ivec3(floor(vec3(ic) / den)) == sc) {
        return true;
      }
    }
  }
  return false;
}

int slot_lod_max(int scap) {
  int lmax = -1;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 s = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (s.a < 0.5) {
      continue;
    }
    int lod = int(round(s.a)) - 1;
    if (lod > lmax) {
      lmax = lod;
    }
  }
  return lmax;
}

float split_allow(float unmet, int lmax, int cover_lod) {
  if (unmet < 0.5) {
    return 1.0;
  }
  return (lmax >= cover_lod && lmax > 0) ? 1.0 : 0.0;
}

void main() {
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  int cover_lod = int(clamp(ng_cover_lod, 0.0, float(LOD_SOFT)));
  int lmax = slot_lod_max(scap);
  int lod_max = int(clamp(ng_lod_soft_max, 0.0, float(LOD_SOFT)));
  int lod = int(clamp(ng_cover_lod, 0.0, float(lod_max)));
  vec4 u0 = texelFetch(tex_union, ivec2(0, 0), 0);
  vec4 u1 = texelFetch(tex_union, ivec2(1, 0), 0);
  if (u0.a < 0.5) {
    finalColor = vec4(0.0, float(lmax) / float(LOD_SOFT), split_allow(0.0, lmax, cover_lod), 1.0);
    return;
  }
  vec3 umin = u0.rgb;
  vec3 umax = u1.rgb;
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  int ix0 = int(floor(umin.x / cell));
  int iy0 = int(floor(umin.y / cell));
  int iz0 = int(floor(umin.z / cell));
  int ix1 = int(floor(umax.x / cell));
  int iy1 = int(floor(umax.y / cell));
  int iz1 = int(floor(umax.z / cell));
  int nx = max(ix1 - ix0 + 1, 1);
  int ny = max(iy1 - iy0 + 1, 1);
  int nz = max(iz1 - iz0 + 1, 1);
  int nneed = nx * ny * nz;
  int lim = min(nneed, SLOT_MAX);
  for (int t = 0; t < SLOT_MAX; t++) {
    if (t >= lim) {
      break;
    }
    int tt = t;
    int ix = ix0 + (tt % nx);
    tt /= nx;
    int iy = iy0 + (tt % ny);
    tt /= ny;
    int iz = iz0 + tt;
    ivec3 ic = ivec3(ix, iy, iz);
    if (octant_occupied(scap, lod, ic)) {
      continue;
    }
    if (cell_hits_vis_surface(lod, ic)) {
      finalColor = vec4(1.0, float(lmax) / float(LOD_SOFT), split_allow(1.0, lmax, cover_lod), 1.0);
      return;
    }
  }
  finalColor = vec4(0.0, float(lmax) / float(LOD_SOFT), split_allow(0.0, lmax, cover_lod), 1.0);
}
// agent: composer-2.5 | 2026-08-13 | GPU split gate lod max | 876029
// agent: grok-4.6 | 2026-08-12 | probe unmet 1x1 flag | f671d1
