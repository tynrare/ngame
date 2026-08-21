// agent: grok-4.6 | 2026-08-21 | resolve SH N dot E | 6cc6f4
/* Screen irr: SH 8-tap; E = max(L0 + L·N, 0). */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform sampler2D tex_page_hash;
uniform sampler2D tex_atlas;
uniform float ng_chunk_extent;
uniform vec2 ng_resolution;

out vec4 finalColor;

const int N = 8;
const int HASH = 128;
const int HASH_PROBE = 8;

int find_page(vec3 cc) {
  int cx = int(floor(cc.x));
  int cy = int(floor(cc.y));
  int cz = int(floor(cc.z));
  uint h = (uint(cx) * 73856093u) ^ (uint(cy) * 19349663u) ^ (uint(cz) * 83492791u);
  int mask = HASH - 1;
  int slot = int(h & uint(mask));
  for (int p = 0; p < HASH_PROBE; p++) {
    vec4 q = texelFetch(tex_page_hash, ivec2((slot + p) & mask, 0), 0);
    if (q.a < 0.5) {
      return -1;
    }
    if (all(lessThan(abs(q.xyz - cc), vec3(0.5)))) {
      return int(q.a + 0.5) - 1;
    }
  }
  return -1;
}

vec3 cache_texel(int page, ivec3 local, int band) {
  ivec3 ic = clamp(local, ivec3(0), ivec3(N - 1));
  int x = page * N + ic.x;
  int y = ic.y + ic.z * N + band * 64;
  return texelFetch(tex_atlas, ivec2(x, y), 0).rgb;
}

vec3 mix8(vec3 c000, vec3 c100, vec3 c010, vec3 c110, vec3 c001, vec3 c101, vec3 c011, vec3 c111,
          vec3 f) {
  vec3 c00 = mix(c000, c100, f.x);
  vec3 c10 = mix(c010, c110, f.x);
  vec3 c01 = mix(c001, c101, f.x);
  vec3 c11 = mix(c011, c111, f.x);
  return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
}

vec3 mix_band(int page, ivec3 i0, vec3 f, int band) {
  return mix8(cache_texel(page, i0, band), cache_texel(page, i0 + ivec3(1, 0, 0), band),
              cache_texel(page, i0 + ivec3(0, 1, 0), band), cache_texel(page, i0 + ivec3(1, 1, 0), band),
              cache_texel(page, i0 + ivec3(0, 0, 1), band), cache_texel(page, i0 + ivec3(1, 0, 1), band),
              cache_texel(page, i0 + ivec3(0, 1, 1), band), cache_texel(page, i0 + ivec3(1, 1, 1), band), f);
}

vec3 eval_sh(vec3 l0, vec3 lx, vec3 ly, vec3 lz, vec3 n) {
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

int page_at(ivec3 cc, int home, ivec3 home_cc) {
  if (all(equal(cc, home_cc))) {
    return home;
  }
  int p = find_page(vec3(cc));
  return p >= 0 ? p : home;
}

vec3 sample_home(int page, vec3 uvw, vec3 n) {
  vec3 g = uvw * float(N) - 0.5;
  ivec3 i0 = ivec3(floor(g));
  vec3 f = fract(g);
  return eval_sh(mix_band(page, i0, f, 0), mix_band(page, i0, f, 1), mix_band(page, i0, f, 2),
                 mix_band(page, i0, f, 3), n);
}

int nbr_page(int p[8], ivec3 o) {
  return p[clamp(o.x, 0, 1) + 2 * clamp(o.y, 0, 1) + 4 * clamp(o.z, 0, 1)];
}

vec3 tap(int p[8], ivec3 ca, ivec3 world_ijk, int home, int band) {
  ivec3 cc = ivec3(floor(vec3(world_ijk) / float(N)));
  int page = nbr_page(p, cc - ca);
  if (page < 0) {
    page = home;
  }
  ivec3 local = world_ijk - cc * N;
  return cache_texel(page, local, band);
}

vec3 mix_skirt_band(int p[8], ivec3 ca, ivec3 i0, int home, int band, vec3 f) {
  return mix8(tap(p, ca, i0, home, band), tap(p, ca, i0 + ivec3(1, 0, 0), home, band),
              tap(p, ca, i0 + ivec3(0, 1, 0), home, band), tap(p, ca, i0 + ivec3(1, 1, 0), home, band),
              tap(p, ca, i0 + ivec3(0, 0, 1), home, band), tap(p, ca, i0 + ivec3(1, 0, 1), home, band),
              tap(p, ca, i0 + ivec3(0, 1, 1), home, band), tap(p, ca, i0 + ivec3(1, 1, 1), home, band), f);
}

vec3 sample_skirt(vec3 world, float cell, int home, ivec3 home_cc, vec3 n) {
  vec3 g = world / cell - 0.5;
  ivec3 i0 = ivec3(floor(g));
  vec3 f = fract(g);
  ivec3 i1 = i0 + ivec3(1);
  ivec3 ca = ivec3(floor(vec3(i0) / float(N)));
  ivec3 cb = ivec3(floor(vec3(i1) / float(N)));
  int p[8];
  for (int i = 0; i < 8; i++) {
    p[i] = -1;
  }
  p[0] = page_at(ca, home, home_cc);
  if (cb.x != ca.x) {
    p[1] = page_at(ivec3(cb.x, ca.y, ca.z), home, home_cc);
  }
  if (cb.y != ca.y) {
    p[2] = page_at(ivec3(ca.x, cb.y, ca.z), home, home_cc);
  }
  if (cb.x != ca.x && cb.y != ca.y) {
    p[3] = page_at(ivec3(cb.x, cb.y, ca.z), home, home_cc);
  }
  if (cb.z != ca.z) {
    p[4] = page_at(ivec3(ca.x, ca.y, cb.z), home, home_cc);
  }
  if (cb.x != ca.x && cb.z != ca.z) {
    p[5] = page_at(ivec3(cb.x, ca.y, cb.z), home, home_cc);
  }
  if (cb.y != ca.y && cb.z != ca.z) {
    p[6] = page_at(ivec3(ca.x, cb.y, cb.z), home, home_cc);
  }
  if (cb.x != ca.x && cb.y != ca.y && cb.z != ca.z) {
    p[7] = page_at(cb, home, home_cc);
  }
  return eval_sh(mix_skirt_band(p, ca, i0, home, 0, f), mix_skirt_band(p, ca, i0, home, 1, f),
                 mix_skirt_band(p, ca, i0, home, 2, f), mix_skirt_band(p, ca, i0, home, 3, f), n);
}

void main() {
  vec2 uv = fragTexCoord;
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  vec3 n = normalize(texture(tex_normal, uv).rgb * 2.0 - 1.0);
  vec3 world = depth_pack.rgb;
  float e = max(ng_chunk_extent, 0.001);
  float cell = e / float(N);
  vec3 cc = floor(world / e);
  int page = find_page(cc);
  if (page < 0) {
    finalColor = vec4(0.0);
    return;
  }
  vec3 uvw = world / e - cc;
  vec3 gloc = uvw * float(N) - 0.5;
  ivec3 i0 = ivec3(floor(gloc));
  vec3 irr;
  if (all(greaterThanEqual(i0, ivec3(0))) && all(lessThan(i0, ivec3(N - 1)))) {
    irr = sample_home(page, uvw, n);
  } else {
    irr = sample_skirt(world, cell, page, ivec3(cc), n);
  }
  finalColor = vec4(irr, 1.0);
}
// agent: grok-4.6 | 2026-08-21 | resolve SH N dot E | 6cc6f4
