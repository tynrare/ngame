// agent: grok-4.6 | 2026-08-21 | merge 4 nearest parent dirs | 0187c6
/* T-merge: world-cell parent 8-tap + 4 nearest parent dirs by dot. */
in vec2 fragTexCoord;

uniform sampler2D tex_near;
uniform sampler2D tex_far;
uniform sampler2D tex_page_hash;
uniform int ng_n_side;
uniform int ng_n_side_p;
uniform int ng_dir_count;
uniform int ng_dir_count_p;
uniform int ng_home_page;
uniform vec3 ng_chunk_origin;
uniform vec3 ng_chunk_cc;
uniform float ng_cell_n;
uniform float ng_cell_p;

out vec4 finalColor;

const int HASH = 128;
const int HASH_PROBE = 8;
const int DIR_P_MAX = 96;

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

vec4 far_at(ivec3 world_ijk, int dir_p) {
  int np = max(ng_n_side_p, 1);
  int ndp = max(ng_dir_count_p, 1);
  vec3 cc = floor(vec3(world_ijk) / float(np));
  ivec3 local = world_ijk - ivec3(cc) * np;
  int page = find_page(cc);
  if (page < 0) {
    page = ng_home_page;
    local = world_ijk - ivec3(ng_chunk_cc) * np;
  }
  local = clamp(local, ivec3(0), ivec3(np - 1));
  int h = local.x + np * (local.y + np * local.z);
  dir_p = clamp(dir_p, 0, ndp - 1);
  return texelFetch(tex_far, ivec2(page * ndp + dir_p, h), 0);
}

vec4 far_trilinear(vec3 g, int dir_p) {
  ivec3 i0 = ivec3(floor(g));
  vec3 f = fract(g);
  vec4 c000 = far_at(i0, dir_p);
  vec4 c100 = far_at(i0 + ivec3(1, 0, 0), dir_p);
  vec4 c010 = far_at(i0 + ivec3(0, 1, 0), dir_p);
  vec4 c110 = far_at(i0 + ivec3(1, 1, 0), dir_p);
  vec4 c001 = far_at(i0 + ivec3(0, 0, 1), dir_p);
  vec4 c101 = far_at(i0 + ivec3(1, 0, 1), dir_p);
  vec4 c011 = far_at(i0 + ivec3(0, 1, 1), dir_p);
  vec4 c111 = far_at(i0 + ivec3(1, 1, 1), dir_p);
  vec4 c00 = mix(c000, c100, f.x);
  vec4 c10 = mix(c010, c110, f.x);
  vec4 c01 = mix(c001, c101, f.x);
  vec4 c11 = mix(c011, c111, f.x);
  vec4 c0 = mix(c00, c10, f.y);
  vec4 c1 = mix(c01, c11, f.y);
  return mix(c0, c1, f.z);
}

void consider_parent(int p, float s, inout int i0, inout int i1, inout int i2, inout int i3,
                     inout float s0, inout float s1, inout float s2, inout float s3) {
  if (s > s0) {
    s3 = s2;
    i3 = i2;
    s2 = s1;
    i2 = i1;
    s1 = s0;
    i1 = i0;
    s0 = s;
    i0 = p;
  } else if (s > s1) {
    s3 = s2;
    i3 = i2;
    s2 = s1;
    i2 = i1;
    s1 = s;
    i1 = p;
  } else if (s > s2) {
    s3 = s2;
    i3 = i2;
    s2 = s;
    i2 = p;
  } else if (s > s3) {
    s3 = s;
    i3 = p;
  }
}

void main() {
  int nd = max(ng_dir_count, 1);
  int n = max(ng_n_side, 1);
  int d = int(floor(gl_FragCoord.x));
  int h = int(floor(gl_FragCoord.y));
  if (d >= nd || h >= n * n * n) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 near_r = texelFetch(tex_near, ivec2(d, h), 0);
  float a = clamp(near_r.a, 0.0, 1.0);
  if (a >= 0.999) {
    finalColor = near_r;
    return;
  }
  int ix = h % n;
  int iy = (h / n) % n;
  int iz = h / (n * n);
  float cell_n = max(ng_cell_n, 0.001);
  float cell_p = max(ng_cell_p, 0.001);
  vec3 W = ng_chunk_origin + (vec3(float(ix), float(iy), float(iz)) + 0.5) * cell_n;
  vec3 g = W / cell_p - 0.5;
  int ndp = clamp(ng_dir_count_p, 1, DIR_P_MAX);
  vec3 omega = dir_from_index(d, nd);
  int i0 = 0;
  int i1 = 0;
  int i2 = 0;
  int i3 = 0;
  float s0 = -2.0;
  float s1 = -2.0;
  float s2 = -2.0;
  float s3 = -2.0;
  for (int p = 0; p < DIR_P_MAX; p++) {
    if (p >= ndp) {
      break;
    }
    float s = dot(omega, dir_from_index(p, ndp));
    consider_parent(p, s, i0, i1, i2, i3, s0, s1, s2, s3);
  }
  float w0 = max(s0, 0.0);
  float w1 = max(s1, 0.0);
  float w2 = max(s2, 0.0);
  float w3 = max(s3, 0.0);
  float wsum = w0 + w1 + w2 + w3;
  if (wsum < 1e-6) {
    w0 = w1 = w2 = w3 = 0.25;
    wsum = 1.0;
  }
  vec4 far_r = (far_trilinear(g, i0) * w0 + far_trilinear(g, i1) * w1 + far_trilinear(g, i2) * w2 +
                far_trilinear(g, i3) * w3) /
               wsum;
  float T = 1.0 - a;
  vec3 rad = near_r.rgb + T * far_r.rgb;
  float ao = a + T * clamp(far_r.a, 0.0, 1.0);
  finalColor = vec4(rad, ao);
}
// agent: grok-4.6 | 2026-08-21 | merge 4 nearest parent dirs | 0187c6
