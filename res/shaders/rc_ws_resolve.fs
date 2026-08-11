// agent: composer-2.5 | 2026-08-10 | B4 resolve multi-res trilinear | a88a62
// agent: composer-2.5 | 2026-08-10 | rename packed GLSL keyword | 25b403
// agent: composer-2.5 | 2026-08-10 | B4 resolve cheaper parent | 59febf
// agent: composer-2.5 | 2026-08-11 | B5 resolve covering leaf blend | b45545
// agent: composer-2.5 | 2026-08-11 | B5 resolve cheaper parent earlyout | 52e559
// agent: composer-2.5 | 2026-08-11 | unlimit resolve lod walk | 4609f2
/* Sparse L1 SH: covering-leaf trilinear + soft parent blend (no distance rings). */
in vec2 fragTexCoord;

uniform sampler2D tex_sh;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform sampler2D tex_meta;
uniform sampler2D tex_hash;
uniform vec3 ng_sky;
uniform vec3 ng_far_origin;
uniform vec3 ng_far_size;
uniform vec3 ng_eye;
uniform float ng_world_cell;
uniform float ng_hash_size;
uniform vec2 ng_sh_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

const float SELF_BIAS = 0.09;
const int PROBE_MAX = 16;
const int LOD_MAX = 25; /* NG_RC_WS_LOD_SOFT_MAX+1 */
const int LOD_STRIDE = 1000;

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

uint cell_hash(int lod, ivec3 c) {
  uint h = uint(lod) * 2654435761u;
  h ^= uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

/** Lookup slot for (lod,cell); -1 miss. */
float lookup_slot(int lod, ivec3 cell) {
  int hsz = int(max(ng_hash_size, 1.0));
  int mask = hsz - 1;
  int h = int(cell_hash(lod, cell) & uint(mask));
  for (int i = 0; i < PROBE_MAX; i++) {
    vec4 e = texelFetch(tex_hash, ivec2(h, 0), 0);
    if (e.r < 0.5) {
      return -1.0;
    }
    int enc = int(e.r);
    int elod = enc / LOD_STRIDE;
    int slot = (enc % LOD_STRIDE) - 1;
    ivec3 kc = ivec3(e.gba);
    if (elod == lod && kc == cell && slot >= 0) {
      return float(slot);
    }
    h = (h + 1) & mask;
  }
  return -1.0;
}

void add_probe(int lod, ivec3 cell, float w, vec3 n, inout vec3 acc, inout float wsum) {
  if (w <= 1e-6) {
    return;
  }
  float slot = lookup_slot(lod, cell);
  if (slot < 0.0) {
    return;
  }
  acc += eval_sh(slot, n) * w;
  wsum += w;
}

void sample_lod(int lod, vec3 world, vec3 n, float bias, inout vec3 acc, inout float wsum) {
  float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
  vec3 pf = (world + n * bias) / cell - 0.5;
  ivec3 i0 = ivec3(floor(pf));
  vec3 f = fract(pf);
  vec3 f1 = 1.0 - f;
  add_probe(lod, i0 + ivec3(0, 0, 0), f1.x * f1.y * f1.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(1, 0, 0), f.x * f1.y * f1.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(0, 1, 0), f1.x * f.y * f1.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(1, 1, 0), f.x * f.y * f1.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(0, 0, 1), f1.x * f1.y * f.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(1, 0, 1), f.x * f1.y * f.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(0, 1, 1), f1.x * f.y * f.z, n, acc, wsum);
  add_probe(lod, i0 + ivec3(1, 1, 1), f.x * f.y * f.z, n, acc, wsum);
}

/** Finest depth that has any corner hit at world. Early-out on first hit. */
int find_covering_lod(vec3 world, vec3 n, float bias) {
  for (int L = 0; L < LOD_MAX; L++) {
    float cell = max(ng_world_cell, 0.001) * exp2(float(L));
    vec3 pf = (world + n * bias) / cell - 0.5;
    ivec3 i0 = ivec3(floor(pf));
    for (int c = 0; c < 8; c++) {
      ivec3 ic = i0 + ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
      if (lookup_slot(L, ic) >= 0.0) {
        return L;
      }
    }
  }
  return -1;
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
  float cell0 = max(ng_world_cell, 0.001);
  float bias = max(SELF_BIAS, cell0 * 0.25);
  int lod = find_covering_lod(world, n, bias);
  if (lod < 0) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  sample_lod(lod, world, n, bias, acc, wsum);

  /* Parent only when child sparse — avoid always sampling two LODs. */
  if (wsum < 0.35 && lod < LOD_MAX - 1) {
    vec3 pacc = vec3(0.0);
    float pw = 0.0;
    sample_lod(lod + 1, world, n, bias, pacc, pw);
    float child_w = clamp(wsum, 0.0, 1.0);
    float parent_w = (1.0 - child_w) * clamp(pw, 0.0, 1.0);
    acc = acc * child_w + pacc * parent_w;
    wsum = child_w * max(wsum, 1e-5) + parent_w * max(pw, 1e-5);
  }

  if (wsum < 1e-5) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }
  finalColor = vec4(acc / wsum, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | B4 resolve multi-res trilinear | a88a62
// agent: composer-2.5 | 2026-08-10 | rename packed GLSL keyword | 25b403
// agent: composer-2.5 | 2026-08-10 | B4 resolve cheaper parent | 59febf
// agent: composer-2.5 | 2026-08-11 | B5 resolve covering leaf blend | b45545
// agent: composer-2.5 | 2026-08-11 | B5 resolve cheaper parent earlyout | 52e559
// agent: composer-2.5 | 2026-08-11 | unlimit resolve lod walk | 4609f2
