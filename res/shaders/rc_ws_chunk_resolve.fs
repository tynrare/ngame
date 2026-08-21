// agent: grok-4.6 | 2026-08-21 | resolve cosine over C0 dirs | 41e1e4
/* Screen irr: C0 8-tap; E = (2/nd) Σ L(ω) max(N·ω, 0). */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform sampler2D tex_page_hash;
uniform sampler2D tex_atlas;
uniform float ng_chunk_extent;
uniform vec2 ng_resolution;

out vec4 finalColor;

const int N = 8;
const int ND = 6;
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

vec3 dir_from_index(int i, int n) {
  float t = (float(i) + 0.5) / float(max(n, 1));
  float z = 1.0 - 2.0 * t;
  float r = sqrt(max(0.0, 1.0 - z * z));
  float a = 2.3999632 * float(i);
  return vec3(cos(a) * r, z, sin(a) * r);
}

vec3 cache_texel(int page, ivec3 local, int d) {
  ivec3 ic = clamp(local, ivec3(0), ivec3(N - 1));
  int x = page * ND + d;
  int y = ic.x + N * (ic.y + N * ic.z);
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

vec3 mix_dir(int page, ivec3 i0, vec3 f, int d) {
  return mix8(cache_texel(page, i0, d), cache_texel(page, i0 + ivec3(1, 0, 0), d),
              cache_texel(page, i0 + ivec3(0, 1, 0), d), cache_texel(page, i0 + ivec3(1, 1, 0), d),
              cache_texel(page, i0 + ivec3(0, 0, 1), d), cache_texel(page, i0 + ivec3(1, 0, 1), d),
              cache_texel(page, i0 + ivec3(0, 1, 1), d), cache_texel(page, i0 + ivec3(1, 1, 1), d), f);
}

vec3 cosine_dirs(int page, ivec3 i0, vec3 f, vec3 n) {
  vec3 e = vec3(0.0);
  for (int d = 0; d < ND; d++) {
    vec3 L = mix_dir(page, i0, f, d);
    vec3 w = dir_from_index(d, ND);
    e += L * max(dot(n, w), 0.0);
  }
  return e * (2.0 / float(ND));
}

int page_at(ivec3 cc, int home, ivec3 home_cc) {
  if (all(equal(cc, home_cc))) {
    return home;
  }
  int p = find_page(vec3(cc));
  return p >= 0 ? p : home;
}

int nbr_page(int p[8], ivec3 o) {
  return p[clamp(o.x, 0, 1) + 2 * clamp(o.y, 0, 1) + 4 * clamp(o.z, 0, 1)];
}

vec3 tap(int p[8], ivec3 ca, ivec3 world_ijk, int home, int d) {
  ivec3 cc = ivec3(floor(vec3(world_ijk) / float(N)));
  int page = nbr_page(p, cc - ca);
  if (page < 0) {
    page = home;
  }
  ivec3 local = world_ijk - cc * N;
  return cache_texel(page, local, d);
}

vec3 mix_skirt_dir(int p[8], ivec3 ca, ivec3 i0, int home, int d, vec3 f) {
  return mix8(tap(p, ca, i0, home, d), tap(p, ca, i0 + ivec3(1, 0, 0), home, d),
              tap(p, ca, i0 + ivec3(0, 1, 0), home, d), tap(p, ca, i0 + ivec3(1, 1, 0), home, d),
              tap(p, ca, i0 + ivec3(0, 0, 1), home, d), tap(p, ca, i0 + ivec3(1, 0, 1), home, d),
              tap(p, ca, i0 + ivec3(0, 1, 1), home, d), tap(p, ca, i0 + ivec3(1, 1, 1), home, d), f);
}

vec3 cosine_skirt(int p[8], ivec3 ca, ivec3 i0, int home, vec3 f, vec3 n) {
  vec3 e = vec3(0.0);
  for (int d = 0; d < ND; d++) {
    vec3 L = mix_skirt_dir(p, ca, i0, home, d, f);
    vec3 w = dir_from_index(d, ND);
    e += L * max(dot(n, w), 0.0);
  }
  return e * (2.0 / float(ND));
}

vec3 sample_home(int page, vec3 uvw, vec3 n) {
  vec3 g = uvw * float(N) - 0.5;
  ivec3 i0 = ivec3(floor(g));
  vec3 f = fract(g);
  return cosine_dirs(page, i0, f, n);
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
  return cosine_skirt(p, ca, i0, home, f, n);
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
// agent: grok-4.6 | 2026-08-21 | resolve cosine over C0 dirs | 41e1e4
