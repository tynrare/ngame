// agent: composer-2.5 | 2026-08-10 | resolve soft nest LOD blend | abb143
/* Trilinear L1 SH near+far. Absolute radiance; soft nest blend. */
in vec2 fragTexCoord;

uniform sampler2D tex_sh;
uniform sampler2D tex_sh_far;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform vec3 ng_far_origin;
uniform vec3 ng_far_size;
uniform float ng_probe_res;
uniform vec2 ng_sh_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

const float SELF_BIAS = 0.09;

vec3 fetch_band(sampler2D sh, float res, float ix, float iy, float iz, float band) {
  int x = int(band * res + ix);
  int y = int(iy + iz * res);
  return texelFetch(sh, ivec2(x, y), 0).rgb;
}

vec3 eval_sh(sampler2D sh, float res, float ix, float iy, float iz, vec3 n) {
  vec3 l0 = fetch_band(sh, res, ix, iy, iz, 0.0);
  vec3 lx = fetch_band(sh, res, ix, iy, iz, 1.0);
  vec3 ly = fetch_band(sh, res, ix, iy, iz, 2.0);
  vec3 lz = fetch_band(sh, res, ix, iy, iz, 3.0);
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

bool inside_volume(vec3 origin, vec3 size, vec3 world) {
  vec3 uvw = (world - origin) / max(size, vec3(0.001));
  return all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0)));
}

vec3 sample_volume(sampler2D sh, vec3 origin, vec3 size, vec3 world, vec3 n, float res) {
  float spacing = min(size.x, min(size.y, size.z)) / max(res, 1.0);
  float bias = max(SELF_BIAS, spacing * 0.25);
  vec3 p = world + n * bias;
  vec3 uvw = (p - origin) / max(size, vec3(0.001));
  uvw = clamp(uvw, vec3(0.0), vec3(0.999999));

  float n1 = res - 1.0;
  vec3 pf = uvw * res - 0.5;
  vec3 i0 = floor(pf);
  vec3 f = clamp(pf - i0, 0.0, 1.0);
  i0 = clamp(i0, vec3(0.0), vec3(n1));
  vec3 i1 = min(i0 + 1.0, vec3(n1));

  vec3 c000 = eval_sh(sh, res, i0.x, i0.y, i0.z, n);
  vec3 c100 = eval_sh(sh, res, i1.x, i0.y, i0.z, n);
  vec3 c010 = eval_sh(sh, res, i0.x, i1.y, i0.z, n);
  vec3 c110 = eval_sh(sh, res, i1.x, i1.y, i0.z, n);
  vec3 c001 = eval_sh(sh, res, i0.x, i0.y, i1.z, n);
  vec3 c101 = eval_sh(sh, res, i1.x, i0.y, i1.z, n);
  vec3 c011 = eval_sh(sh, res, i0.x, i1.y, i1.z, n);
  vec3 c111 = eval_sh(sh, res, i1.x, i1.y, i1.z, n);

  vec3 c00 = mix(c000, c100, f.x);
  vec3 c10 = mix(c010, c110, f.x);
  vec3 c01 = mix(c001, c101, f.x);
  vec3 c11 = mix(c011, c111, f.x);
  return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
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
  float res = max(ng_probe_res, 1.0);

  if (!inside_volume(ng_far_origin, ng_far_size, world)) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }

  vec3 far_c = sample_volume(tex_sh_far, ng_far_origin, ng_far_size, world, n, res);
  if (!inside_volume(ng_ws_origin, ng_ws_size, world)) {
    finalColor = vec4(far_c, 1.0);
    return;
  }

  vec3 near_c = sample_volume(tex_sh, ng_ws_origin, ng_ws_size, world, n, res);
  vec3 nuvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  float edge = min(min(nuvw.x, 1.0 - nuvw.x), min(min(nuvw.y, 1.0 - nuvw.y), min(nuvw.z, 1.0 - nuvw.z)));
  /* ~1 near probe cell in UVW */
  float shell = 1.0 / res;
  float w = smoothstep(0.0, shell, edge);
  finalColor = vec4(mix(far_c, near_c, w), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | resolve soft nest LOD blend | abb143
