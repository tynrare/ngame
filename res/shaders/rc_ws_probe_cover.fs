// agent: composer-2.5 | 2026-08-12 | probe cover keep apply shaders | ab4fc2
/* Gen0 cover: work[x] = AABB cell of culled-visible prims at cover_lod.
 * Skip prims with tex_prim_vis < 0.5. Empty → rgba 0. */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;
uniform float ng_cover_lod;
uniform float ng_world_cell;
uniform float ng_work_count;

out vec4 finalColor;

const int PRIM_MAX = 64;
const int WORK_MAX = 2048;
const int LOD_STRIDE = 1000;

vec4 prim_col(int row, int col) {
  return texelFetch(tex_prim, ivec2(col, row), 0);
}

float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
}

void prim_aabb(int row, out vec3 bmin, out vec3 bmax) {
  vec4 ctr = prim_col(row, 0);
  vec4 halfv = prim_col(row, 1);
  if (ctr.w > 0.5) {
    float r = halfv.x;
    bmin = ctr.xyz - vec3(r);
    bmax = ctr.xyz + vec3(r);
  } else {
    bmin = ctr.xyz - halfv.xyz;
    bmax = ctr.xyz + halfv.xyz;
  }
}

void main() {
  int x = int(floor(gl_FragCoord.x));
  int nwork = int(clamp(ng_work_count, 1.0, float(WORK_MAX)));
  if (x < 0 || x >= nwork) {
    finalColor = vec4(0.0);
    return;
  }
  int lod = int(clamp(ng_cover_lod, 0.0, 24.0));
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  int remain = x;
  for (int p = 0; p < PRIM_MAX; p++) {
    if (p >= nprim) {
      break;
    }
    if (prim_vis_at(p) < 0.5) {
      continue;
    }
    vec3 bmin;
    vec3 bmax;
    prim_aabb(p, bmin, bmax);
    int ix0 = int(floor(bmin.x / cell));
    int iy0 = int(floor(bmin.y / cell));
    int iz0 = int(floor(bmin.z / cell));
    int ix1 = int(floor(bmax.x / cell));
    int iy1 = int(floor(bmax.y / cell));
    int iz1 = int(floor(bmax.z / cell));
    int nx = max(ix1 - ix0 + 1, 0);
    int ny = max(iy1 - iy0 + 1, 0);
    int nz = max(iz1 - iz0 + 1, 0);
    int count = nx * ny * nz;
    if (count <= 0) {
      continue;
    }
    if (remain >= count) {
      remain -= count;
      continue;
    }
    int iz = remain / (nx * ny);
    int rem = remain - iz * nx * ny;
    int iy = rem / nx;
    int ix = rem - iy * nx;
    finalColor = vec4(float(ix0 + ix), float(iy0 + iy), float(iz0 + iz),
                      float(lod + p * LOD_STRIDE));
    return;
  }
  finalColor = vec4(0.0);
}
// agent: composer-2.5 | 2026-08-12 | probe cover keep apply shaders | ab4fc2
